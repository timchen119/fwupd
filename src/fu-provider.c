/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <fwupd.h>
#include <appstream-glib.h>
#include <errno.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <string.h>

#include "fu-device.h"
#include "fu-pending.h"
#include "fu-plugin.h"
#include "fu-provider-uefi.h"

#include "snappy.h"

static void	fu_provider_finalize	(GObject	*object);

enum {
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_STATUS_CHANGED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (FuProvider, fu_provider, G_TYPE_OBJECT)

static gboolean
fu_provider_offline_invalidate (GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file1 = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	file1 = g_file_new_for_path (FU_OFFLINE_TRIGGER_FILENAME);
	if (!g_file_query_exists (file1, NULL))
		return TRUE;
	if (!g_file_delete (file1, NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Cannot delete %s: %s",
			     FU_OFFLINE_TRIGGER_FILENAME,
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_provider_offline_setup (GError **error)
{
	gint rc;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create symlink for the systemd-system-update-generator */
	rc = symlink ("/var/lib/fwupd", FU_OFFLINE_TRIGGER_FILENAME);
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to create symlink %s to %s: %s",
			     FU_OFFLINE_TRIGGER_FILENAME,
			     "/var/lib", strerror (errno));
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_provider_coldplug (FuProvider *provider, GError **error)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	if (klass->coldplug != NULL)
		return klass->coldplug (provider, error);
	return TRUE;
}

static gboolean
fu_provider_schedule_update (FuProvider *provider,
			     FuDevice *device,
			     GBytes *blob_cab,
			     GError **error)
{
	gchar tmpname[] = {"XXXXXX.cap"};
	guint i;
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FwupdResult) res_tmp = NULL;
	g_autoptr(FuPending) pending = NULL;
	g_autoptr(GFile) file = NULL;

	/* id already exists */
	pending = fu_pending_new ();
	res_tmp = fu_pending_get_device (pending, fu_device_get_id (device), NULL);
	if (res_tmp != NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_ALREADY_PENDING,
			     "%s is already scheduled to be updated",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* create directory */
	if (get_snap_app_data_path())
		dirname = g_build_filename (get_snap_app_data_path(), LOCALSTATEDIR, "lib", "fwupd", NULL);
	else
		dirname = g_build_filename (LOCALSTATEDIR, "lib", "fwupd", NULL);
	file = g_file_new_for_path (dirname);
	if (!g_file_query_exists (file, NULL)) {
		if (!g_file_make_directory_with_parents (file, NULL, error))
			return FALSE;
	}

	/* get a random filename */
	for (i = 0; i < 6; i++)
		tmpname[i] = g_random_int_range ('A', 'Z');
	filename = g_build_filename (dirname, tmpname, NULL);

	/* just copy to the temp file */
	fu_provider_set_status (provider, FWUPD_STATUS_SCHEDULING);
	if (!g_file_set_contents (filename,
				  g_bytes_get_data (blob_cab, NULL),
				  g_bytes_get_size (blob_cab),
				  error))
		return FALSE;

	/* schedule for next boot */
	g_debug ("schedule %s to be installed to %s on next boot",
		 filename, fu_device_get_id (device));
	fu_device_set_update_filename (device, filename);

	/* add to database */
	if (!fu_pending_add_device (pending, FWUPD_RESULT (device), error))
		return FALSE;

	/* next boot we run offline */
	return fu_provider_offline_setup (error);
}

gboolean
fu_provider_verify (FuProvider *provider,
		    FuDevice *device,
		    FuProviderVerifyFlags flags,
		    GError **error)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	if (klass->verify != NULL)
		return klass->verify (provider, device, flags, error);
	return TRUE;
}

gboolean
fu_provider_unlock (FuProvider *provider,
		    FuDevice *device,
		    GError **error)
{
	guint64 flags;

	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);

	/* final check */
	flags = fu_device_get_flags (device);
	if ((flags & FU_DEVICE_FLAG_LOCKED) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Device %s is not locked",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* run provider method */
	if (klass->unlock != NULL) {
		if (!klass->unlock (provider, device, error))
			return FALSE;
	}

	/* update with correct flags */
	flags = fu_device_get_flags (device);
	fu_device_set_flags (device, flags &= ~FU_DEVICE_FLAG_LOCKED);
	fu_device_set_modified (device, g_get_real_time () / G_USEC_PER_SEC);
	return TRUE;
}

gboolean
fu_provider_update (FuProvider *provider,
		    FuDevice *device,
		    GBytes *blob_cab,
		    GBytes *blob_fw,
		    FuPlugin *plugin,
		    FwupdInstallFlags flags,
		    GError **error)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	g_autoptr(FuPending) pending = NULL;
	g_autoptr(FwupdResult) res_pending = NULL;
	GError *error_update = NULL;

	/* schedule for next reboot, or handle in the provider */
	if (flags & FWUPD_INSTALL_FLAG_OFFLINE) {
		if (klass->update_offline == NULL)
			return fu_provider_schedule_update (provider,
							    device,
							    blob_cab,
							    error);
		return klass->update_offline (provider, device, blob_fw, flags, error);
	}

	/* cancel the pending action */
	if (!fu_provider_offline_invalidate (error))
		return FALSE;

	/* we have support using a plugin */
	if (plugin != NULL) {
		return fu_plugin_run_device_update (plugin,
						    device,
						    blob_fw,
						    error);
	}

	/* online */
	if (klass->update_online == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "No online update possible");
		return FALSE;
	}
	pending = fu_pending_new ();
	res_pending = fu_pending_get_device (pending, fu_device_get_id (device), NULL);
	if (!klass->update_online (provider, device, blob_fw, flags, &error_update)) {
		/* save the error to the database */
		if (res_pending != NULL) {
			fu_pending_set_error_msg (pending, FWUPD_RESULT (device),
						  error_update->message, NULL);
		}
		g_propagate_error (error, error_update);
		return FALSE;
	}

	/* cleanup */
	if (res_pending != NULL) {
		const gchar *tmp;

		/* update pending database */
		fu_pending_set_state (pending, FWUPD_RESULT (device),
				      FWUPD_UPDATE_STATE_SUCCESS, NULL);

		/* delete cab file */
		tmp = fwupd_result_get_update_filename (res_pending);
		if (tmp != NULL && g_str_has_prefix (tmp, LIBEXECDIR)) {
			g_autoptr(GError) error_local = NULL;
			g_autoptr(GFile) file = NULL;
			file = g_file_new_for_path (tmp);
			if (!g_file_delete (file, NULL, &error_local)) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Failed to delete %s: %s",
					     tmp, error_local->message);
				return FALSE;
			}
		}
	}

	return TRUE;
}

gboolean
fu_provider_clear_results (FuProvider *provider, FuDevice *device, GError **error)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FwupdResult) res_pending = NULL;
	g_autoptr(FuPending) pending = NULL;

	/* handled by the provider */
	if (klass->clear_results != NULL)
		return klass->clear_results (provider, device, error);

	/* handled using the database */
	pending = fu_pending_new ();
	res_pending = fu_pending_get_device (pending,
					     fu_device_get_id (device),
					     &error_local);
	if (res_pending == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to find %s in pending database: %s",
			     fu_device_get_id (device),
			     error_local->message);
		return FALSE;
	}

	/* remove from pending database */
	return fu_pending_remove_device (pending, FWUPD_RESULT (device), error);
}

gboolean
fu_provider_get_results (FuProvider *provider, FuDevice *device, GError **error)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	FwupdUpdateState update_state;
	const gchar *tmp;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FwupdResult) res_pending = NULL;
	g_autoptr(FuPending) pending = NULL;

	/* handled by the provider */
	if (klass->get_results != NULL)
		return klass->get_results (provider, device, error);

	/* handled using the database */
	pending = fu_pending_new ();
	res_pending = fu_pending_get_device (pending,
					     fu_device_get_id (device),
					     &error_local);
	if (res_pending == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOTHING_TO_DO,
			     "Failed to find %s in pending database: %s",
			     fu_device_get_id (device),
			     error_local->message);
		return FALSE;
	}

	/* copy the important parts from the pending device to the real one */
	update_state = fwupd_result_get_update_state (res_pending);
	if (update_state == FWUPD_UPDATE_STATE_UNKNOWN ||
	    update_state == FWUPD_UPDATE_STATE_PENDING) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOTHING_TO_DO,
			     "Device %s has not been updated offline yet",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* copy */
	fu_device_set_update_state (device, update_state);
	tmp = fwupd_result_get_update_error (res_pending);
	if (tmp != NULL)
		fu_device_set_update_error (device, tmp);
	tmp = fwupd_result_get_device_version (res_pending);
	if (tmp != NULL)
		fu_device_set_version (device, tmp);
	tmp = fwupd_result_get_update_version (res_pending);
	if (tmp != NULL)
		fu_device_set_update_version (device, tmp);
	return TRUE;
}

const gchar *
fu_provider_get_name (FuProvider *provider)
{
	FuProviderClass *klass = FU_PROVIDER_GET_CLASS (provider);
	if (klass->get_name != NULL)
		return klass->get_name (provider);
	return NULL;
}

void
fu_provider_device_add (FuProvider *provider, FuDevice *device)
{
	g_debug ("emit added from %s: %s",
		 fu_provider_get_name (provider),
		 fu_device_get_id (device));
	fu_device_set_created (device, g_get_real_time () / G_USEC_PER_SEC);
	fu_device_set_provider (device, fu_provider_get_name (provider));
	g_signal_emit (provider, signals[SIGNAL_DEVICE_ADDED], 0, device);
}

void
fu_provider_device_remove (FuProvider *provider, FuDevice *device)
{
	g_debug ("emit removed from %s: %s",
		 fu_provider_get_name (provider),
		 fu_device_get_id (device));
	g_signal_emit (provider, signals[SIGNAL_DEVICE_REMOVED], 0, device);
}

void
fu_provider_set_status (FuProvider *provider, FwupdStatus status)
{
	g_signal_emit (provider, signals[SIGNAL_STATUS_CHANGED], 0, status);
}

GChecksumType
fu_provider_get_checksum_type (FuProviderVerifyFlags flags)
{
	if (flags & FU_PROVIDER_VERIFY_FLAG_USE_SHA256)
		return G_CHECKSUM_SHA256;
	return G_CHECKSUM_SHA1;
}

static void
fu_provider_class_init (FuProviderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_provider_finalize;
	signals[SIGNAL_DEVICE_ADDED] =
		g_signal_new ("device-added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuProviderClass, device_added),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_DEVICE_REMOVED] =
		g_signal_new ("device-removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuProviderClass, device_removed),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuProviderClass, status_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
fu_provider_init (FuProvider *provider)
{
}

static void
fu_provider_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_provider_parent_class)->finalize (object);
}
