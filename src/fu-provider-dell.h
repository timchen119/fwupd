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

#ifndef __FU_PROVIDER_DELL_H
#define __FU_PROVIDER_DELL_H

#include <glib-object.h>
#include <efivar.h>
#include "fu-device.h"
#include "fu-provider.h"

G_BEGIN_DECLS

#define FU_TYPE_PROVIDER_DELL (fu_provider_dell_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuProviderDell, fu_provider_dell, FU, PROVIDER_DELL, FuProvider)

struct _FuProviderDellClass
{
	FuProviderClass			 parent_class;
};

FuProvider	*fu_provider_dell_new		(void);

G_END_DECLS

/* These are used to indicate the status of a previous DELL flash */
#define DELL_SUCCESS		0x0000
#define DELL_CONSISTENCY_FAIL	0x0001
#define DELL_FLASH_MEMORY_FAIL	0x0002
#define DELL_FLASH_NOT_READY	0x0003
#define DELL_FLASH_DISABLED	0x0004
#define DELL_BATTERY_MISSING	0x0005
#define DELL_BATTERY_DEAD	0x0006
#define DELL_AC_MISSING		0x0007
#define DELL_CANT_SET_12V	0x0008
#define DELL_CANT_UNSET_12V	0x0009
#define DELL_FAILURE_BLOCK_ERASE	0x000A
#define DELL_GENERAL_FAILURE	0x000B
#define DELL_DATA_MISCOMPARE	0x000C
#define DELL_IMAGE_MISSING	0x000D
#define DELL_DID_NOTHING		0xFFFF

/* These are nodes that will indicate information about
 * the TPM status
 */
struct tpm_status {
	guint32 ret;
	guint32 fw_version;
	guint32 status;
	guint32 flashes_left;
};
#define TPM_EN_MASK	0x0001
#define TPM_OWN_MASK	0x0004
#define TPM_TYPE_MASK	0x0F00
#define TPM_1_2_MODE	0x0001
#define TPM_2_0_MODE	0x0002

#define TPM_GUID_1_2  EFI_GUID(0x5034BAC4, 0x0814, 0x4F53, 0x8050, 0x7E, 0x20, 0x99, 0x0D, 0x16, 0x38)
#define TPM_GUID_2_0  EFI_GUID(0xC22D63F4, 0xF182, 0x40FC, 0xB238, 0xE5, 0xF8, 0x9F, 0xBF, 0x3B, 0x87)

/* These are DACI class/select needed for
 * flash capability queries
 */
#define DACI_FLASH_INTERFACE_CLASS	7
#define DACI_FLASH_INTERFACE_SELECT	3
#define DACI_ARG_TPM	2

#endif /* __FU_PROVIDER_DELL_H */
