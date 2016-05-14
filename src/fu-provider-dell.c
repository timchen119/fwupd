/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Mario Limonciello <mario_limonciello@dell.com>
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
#include <fwup.h>
#include <glib-object.h>

#include <appstream-glib.h>
#include <glib/gstdio.h>

#include <smbios_c/smbios.h>
#include <smbios_c/smi.h>
#include <unistd.h>
#include <fcntl.h>

#include "fu-device.h"
#include "fu-provider-dell.h"

static void	fu_provider_dell_finalize	(GObject	*object);


/* system ID's that can switch TPM 1.2 -> TPM 2.0 from 2015
 * This is currently documented here:
 * http://en.community.dell.com/techcenter/enterprise-client/w/wiki/11850.how-to-change-tpm-modes-1-2-2-0
 * Latitude 3470, 3570, E5270, E5470, E5570, E7270, E7470
 * Optiplex 3040, 3240, 5040, 7040, 7240
 * Precision 3420, 3620, 3510, 5510, 7710
 * XPS 9550, 9350
 *
 * This is not strictly necessary to look up thanks to DACI calls, but in case it's needed for debugging
 * the information is here.
 *
 * static guint16 gen7_tpm_switch[] = {0x06F2, 0x06F3, 0x06DD, 0x06DE, 0x06DF,
 *				    0x06DB, 0x06DC, 0x06BB, 0x06C6, 0x06BA,
 *				    0x06B9, 0x05CA, 0x06C7, 0x06B7, 0x06E0,
 *				    0x06E5, 0x06D9, 0x06DA, 0x06E4, 0x0704};
 */

G_DEFINE_TYPE (FuProviderDell, fu_provider_dell, FU_TYPE_PROVIDER)

/**
 * fu_provider_dell_get_name:
 **/
static const gchar *
fu_provider_dell_get_name (FuProvider *provider)
{
	return "DELL";
}

/**
 * fu_provider_dell_get_limited_flashes:
 * checks if the device is limited in flashes
 **/
static gboolean
fu_provider_dell_get_limited_flashes (FuDevice *device)
{
	const efi_guid_t limited_guid[] = { TPM_GUID_1_2, TPM_GUID_2_0 };
	const gchar *device_guid;
	gchar *compare_guid;
	gboolean limited = FALSE;
	guint i;

	device_guid = fu_device_get_guid_default(device);
	for (i = 0; i < G_N_ELEMENTS(limited_guid); i++) {
		compare_guid = g_strdup ("00000000-0000-0000-0000-000000000000");
		efi_guid_to_str(&limited_guid[i], &compare_guid);
		if (g_strcmp0(device_guid, compare_guid) == 0)
			limited = TRUE;
		g_free(compare_guid);
		if (limited)
			break;
	}
	return limited;
}

/**
 * fu_provider_dell_get_results:
 **/
static gboolean
fu_provider_dell_get_results (FuProvider *provider, FuDevice *device, GError **error)
{
	struct smbios_struct *de_table;
	guint16 completion_code = 0xFFFF;
	const gchar *tmp = NULL;

	/* look at offset 0x06 for identifier meaning completion code */
	de_table = smbios_get_next_struct_by_type(0, 0xDE);
	smbios_struct_get_data(de_table, &(completion_code), 0x06, sizeof(guint16));

	if (completion_code == DELL_SUCCESS)
		fu_device_set_update_state(device, FWUPD_UPDATE_STATE_SUCCESS);
	else {
		fu_device_set_update_state (device, FWUPD_UPDATE_STATE_FAILED);
		switch (completion_code) {
		case DELL_CONSISTENCY_FAIL:
			tmp = "The image failed one or more consistency checks.";
			break;
		case DELL_FLASH_MEMORY_FAIL:
			tmp = "The BIOS could not access the flash-memory device.";
			break;
		case DELL_FLASH_NOT_READY:
			tmp = "The flash-memory device was not ready when an erase was attempted.";
			break;
		case DELL_FLASH_DISABLED:
			tmp = "Flash programming is currently disabled on the system, or the voltage is low.";
			break;
		case DELL_BATTERY_MISSING:
			tmp = "A battery must be installed for the operation to complete.";
			break;
		case DELL_BATTERY_DEAD:
			tmp = "A fully-charged battery must be present for the operation to complete.";
			break;
		case DELL_AC_MISSING:
			tmp = "An external power adapter must be connected for the operation to complete.";
			break;
		case DELL_CANT_SET_12V:
			tmp = "The 12V required to program the flash-memory could not be set.";
			break;
		case DELL_CANT_UNSET_12V:
			tmp = "The 12V required to program the flash-memory could not be removed.";
			break;
		case DELL_FAILURE_BLOCK_ERASE :
			tmp = "A flash-memory failure occurred during a block-erase operation.";
			break;
		case DELL_GENERAL_FAILURE:
			tmp = "A general failure occurred during the flash programming.";
			break;
		case DELL_DATA_MISCOMPARE:
			tmp = "A data miscompare error occurred during the flash programming.";
			break;
		case DELL_IMAGE_MISSING:
			tmp = "The image could not be found in memory, i.e. the header could not be located.";
			break;
		case DELL_DID_NOTHING:
			tmp = "No update operation has been performed on the system.";
			break;
		default:
			break;
		}
		if (tmp != NULL)
			fu_device_set_update_error (device, tmp);
	}

	return TRUE;
}

/**
 * fu_provider_dell_detect_tpm:
 **/
static gboolean
fu_provider_dell_detect_tpm (FuProvider *provider, GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDevice) dev_alt = NULL;
	g_autofree gchar *tpm_guid = NULL;
	g_autofree gchar *tpm_guid_alt = NULL;
	g_autofree gchar *pretty_tpm_name = NULL;
	g_autofree gchar *pretty_tpm_name_alt = NULL;
	g_autofree gchar *tpm_id = NULL;
	g_autofree gchar *tpm_id_alt = NULL;
	g_autofree gchar *version_str = NULL;
	g_autofree gchar *product_name = NULL;
	efi_guid_t guid_raw;
	efi_guid_t guid_raw_alt;
	const gchar *tpm_mode;
	const gchar *tpm_mode_alt;
	g_autofree struct tpm_status *out;
	g_autofree guint32 *args;
	guint ret;

	args = g_malloc0(sizeof(guint32) *4);
	out = g_malloc0(sizeof(struct tpm_status));

	/* Execute TPM Status Query */
	args[0] = DACI_ARG_TPM;
	ret = dell_simple_ci_smi(DACI_FLASH_INTERFACE_CLASS,
				 DACI_FLASH_INTERFACE_SELECT,
				 args,
				 (guint32 *) out);

	if (ret || out->ret != 0) {
		g_debug("DELL: Failed to query system for TPM information: "
			"(%d) (%d)", ret, out->ret);
		return FALSE;
	}
	/* HW version is output in second /input/ arg
	 * it may be relevant as next gen TPM is enabled
	 */
	g_debug("DELL: TPM HW version: 0x%x", args[1]);
	g_debug("DELL: TPM Status: 0x%x", out->status);

	/* Test TPM enabled (Bit 0) */
	if (!(out->status & TPM_EN_MASK)) {
		g_debug("DELL: TPM not enabled (%x)", out->status);
		return FALSE;
	}

	/* Test TPM mode to determine current mode */
	if (((out->status & TPM_TYPE_MASK) >> 8) == TPM_1_2_MODE) {
		tpm_mode = "1.2";
		tpm_mode_alt = "2.0";
		guid_raw = 	TPM_GUID_1_2;
		guid_raw_alt = 	TPM_GUID_2_0;
	}
	else if (((out->status & TPM_TYPE_MASK) >> 8) == TPM_2_0_MODE) {
		tpm_mode = "2.0";
		tpm_mode_alt = "1.2";
		guid_raw = 	TPM_GUID_2_0;
		guid_raw_alt = 	TPM_GUID_1_2;
	}
	else {
		g_debug("DELL: Unable to determine TPM mode");
		return FALSE;
	}
	efi_guid_to_str(&guid_raw, &tpm_guid);
	efi_guid_to_str(&guid_raw_alt, &tpm_guid_alt);

	tpm_id = g_strdup_printf ("DELL-%s" G_GUINT64_FORMAT, tpm_guid);
	tpm_id_alt = g_strdup_printf ("DELL-%s" G_GUINT64_FORMAT, tpm_guid_alt);
	version_str = as_utils_version_from_uint32 (out->fw_version,
						    AS_VERSION_PARSE_FLAG_NONE);

	/* Make it clear that the TPM is a discrete device of the product */
	if (!g_file_get_contents("/sys/class/dmi/id/product_name",
				&product_name,NULL, NULL)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Unable to read product information");
		return FALSE;
	}
	g_strchomp(product_name);
	pretty_tpm_name = g_strdup_printf( "%s TPM %s", product_name, tpm_mode);
	pretty_tpm_name_alt = g_strdup_printf( "%s TPM %s", product_name, tpm_mode_alt);

	/* Build Standard device nodes */
	dev = fu_device_new ();
	fu_device_set_id(dev, tpm_id);
	fu_device_add_guid (dev, tpm_guid);
	fu_device_set_name (dev, pretty_tpm_name);
	fu_device_set_version (dev, version_str);
	fu_device_add_flag (dev, FU_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (dev, FU_DEVICE_FLAG_REQUIRE_AC);
	if (out->flashes_left > 0) {
		fu_device_add_flag (dev, FU_DEVICE_FLAG_ALLOW_OFFLINE);
		fu_device_set_flashes_left (dev, out->flashes_left);
	}

	/* Build alternate device node */
	dev_alt = fu_device_new();
	fu_device_set_id(dev_alt, tpm_id_alt);
	fu_device_add_guid (dev_alt, tpm_guid_alt);
	fu_device_set_name (dev_alt, pretty_tpm_name_alt);
	fu_device_add_flag (dev_alt, FU_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (dev_alt, FU_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag (dev_alt, FU_DEVICE_FLAG_LOCKED);
	fu_device_set_alternate(dev_alt, dev);


	/* If TPM is not owned and at least 1 flash left allow mode switching
	 *
	 * Mode switching is turned on by setting flashes left on alternate
	 * device.
	 */
	if (!((out->status) & TPM_OWN_MASK) &&
	    out->flashes_left > 0) {
		fu_device_set_flashes_left (dev_alt, out->flashes_left);
	}
	else
		g_debug("DELL: %s mode switch disabled due to TPM ownership",
			pretty_tpm_name);

	fu_provider_device_add (provider, dev);
	fu_provider_device_add (provider, dev_alt);
	return TRUE;
}

/**
 * fu_provider_dell_coldplug:
 **/
static gboolean
fu_provider_dell_coldplug (FuProvider *provider, GError **error)
{
	guint8 dell_supported = 0;
	gint uefi_supported = 0;
	struct smbios_struct *de_table;

	/* look at offset 0x00 for identifier meaning DELL is supported */
	de_table = smbios_get_next_struct_by_type(0, 0xDE);
	smbios_struct_get_data(de_table, &(dell_supported), 0x00, sizeof(guint8));

	if (dell_supported != 0xDE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "DELL: firmware updating not supported (%x)",
			     dell_supported);
		return FALSE;
	}

	/* Check and make sure that ESRT is supported as well.
	 *
	 * This will indicate capsule support on the system.
	 *
	 * If ESRT is not turned on, fwupd will have already created an
	 * unlock device (if compiled with support).
	 *
	 * Once unlocked, that will enable this provider too.
	 *
	 * that means we should only look for supported = 1
	 */
	uefi_supported = fwup_supported ();
	if (uefi_supported != 1) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "DELL: UEFI capsule firmware updating not supported (%x)",
			     dell_supported);
		return FALSE;
	}

	/* FIXME detect dock & hotplug */

	if (!fu_provider_dell_detect_tpm (provider, error)) {
		g_debug("DELL: No switchable TPM detected");
		return FALSE;
	}
	return TRUE;
}


/**
 * _fwup_resource_iter_free:
 **/
static void
_fwup_resource_iter_free (fwup_resource_iter *iter)
{
	fwup_resource_iter_destroy (&iter);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(fwup_resource_iter, _fwup_resource_iter_free);

/**
 * fu_provider_dell_update:
 **/
static gboolean
fu_provider_dell_unlock(FuProvider *provider,
			FuDevice *device,
			GError **error)
{
	FuDevice *device_alt = NULL;
	FwupdDeviceFlags device_flags_alt = 0;
	guint flashes_left = 0;
	guint flashes_left_alt = 0;

	/* For unlocking TPM1.2 <-> TPM2.0 switching */
	g_debug ("DELL: Unlocking upgrades for: %s (%s)", fu_device_get_name(device),
		 fu_device_get_id (device));
	device_alt = fu_device_get_alternate(device);

	if (!device_alt)
		return FALSE;
	g_debug ("DELL: Preventing upgrades for: %s (%s)", fu_device_get_name(device_alt),
		 fu_device_get_id (device_alt));

	flashes_left = fu_device_get_flashes_left(device);
	flashes_left_alt = fu_device_get_flashes_left(device_alt);
	if (flashes_left == 0) {
		/* Flashes left == 0 on both means no flashes left */
		if (flashes_left_alt == 0)
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "ERROR: %s has no flashes left.",
				     fu_device_get_name(device));
		/* Flashes left == 0 on just unlocking device is ownership. */
		else
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "ERROR: %s is currently OWNED. "
				     "Ownership must be removed to switch modes.",
				     fu_device_get_name(device_alt));
		return FALSE;
	}


	/* clone the info from real device but prevent it from being flashed */
	device_flags_alt = fu_device_get_flags(device_alt);
	fu_device_set_flags(device, device_flags_alt);
	fu_device_set_flags(device_alt, device_flags_alt & ~FU_DEVICE_FLAG_ALLOW_OFFLINE);

	/* Make sure that this unlocked device can be updated */
	fu_device_set_version(device, "0.0.0.0");

	return TRUE;
}

/**
 * fu_provider_dell_update:
 **/
static gboolean
fu_provider_dell_update (FuProvider *provider,
			FuDevice *device,
			GBytes *blob_fw,
			FwupdInstallFlags flags,
			GError **error)
{
	g_autoptr(fwup_resource_iter) iter = NULL;
	fwup_resource *re = NULL;
	const gchar *name = NULL;
	const gchar *guidstr = NULL;
	gint rc;
	guint flashes_left;
	efi_guid_t guid;

	/* test the flash counter */
	if (fu_provider_dell_get_limited_flashes(device)) {
		flashes_left = fu_device_get_flashes_left(device);
		name = fu_device_get_name(device);
		g_debug("DELL: %s has %d flashes left", name, flashes_left);
		if (flashes_left == 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "ERROR: %s no flashes left.",
				     name);
			return FALSE;
		}
		else if (!(flags & FWUPD_INSTALL_FLAG_FORCE) &&
			 flashes_left <= 2) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "WARNING: %s only has %d flashes left. "
				     "To update anyway please run the update with --force.",
				     name, flashes_left);
			return FALSE;
		}
	}

	/* perform the update */
	g_debug ("DELL: Performing capsule update");

	fwup_resource_iter_create (&iter);
	fwup_resource_iter_next (iter, &re);
	guidstr = fu_device_get_guid_default (device);
	rc = efi_str_to_guid(guidstr, &guid);
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to convert guid to string");
		return FALSE;
	}
	rc = fwup_set_guid(iter, &re, &guid);
	if (rc < 0 || re == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to update GUID %s",
			     strerror (rc));
		return FALSE;
	}

	fu_provider_set_status (provider, FWUPD_STATUS_SCHEDULING);
	rc = fwup_set_up_update_with_buf (re, 0,
					  g_bytes_get_data (blob_fw, NULL),
					  g_bytes_get_size (blob_fw));
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "DELL capsule update failed: %s",
			     strerror (rc));
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_provider_dell_class_init:
 **/
static void
fu_provider_dell_class_init (FuProviderDellClass *klass)
{
	FuProviderClass *provider_class = FU_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->get_name = fu_provider_dell_get_name;
	provider_class->coldplug = fu_provider_dell_coldplug;
	provider_class->unlock = fu_provider_dell_unlock;
	provider_class->update_offline = fu_provider_dell_update;
	provider_class->get_results = fu_provider_dell_get_results;
	object_class->finalize = fu_provider_dell_finalize;
}

/**
 * fu_provider_dell_init:
 **/
static void
fu_provider_dell_init (FuProviderDell *provider_dell)
{
}

/**
 * fu_provider_dell_finalize:
 **/
static void
fu_provider_dell_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_provider_dell_parent_class)->finalize (object);
}

/**
 * fu_provider_dell_new:
 **/
FuProvider *
fu_provider_dell_new (void)
{
	FuProviderDell *provider;
	provider = g_object_new (FU_TYPE_PROVIDER_DELL , NULL);
	return FU_PROVIDER (provider);
}
