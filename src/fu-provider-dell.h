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
#define DELL_SUCCESS			0x0000
#define DELL_CONSISTENCY_FAIL		0x0001
#define DELL_FLASH_MEMORY_FAIL		0x0002
#define DELL_FLASH_NOT_READY		0x0003
#define DELL_FLASH_DISABLED		0x0004
#define DELL_BATTERY_MISSING		0x0005
#define DELL_BATTERY_DEAD		0x0006
#define DELL_AC_MISSING			0x0007
#define DELL_CANT_SET_12V		0x0008
#define DELL_CANT_UNSET_12V		0x0009
#define DELL_FAILURE_BLOCK_ERASE	0x000A
#define DELL_GENERAL_FAILURE		0x000B
#define DELL_DATA_MISCOMPARE		0x000C
#define DELL_IMAGE_MISSING		0x000D
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

/* These are DACI class/select needed for
 * flash capability queries
 */
#define DACI_FLASH_INTERFACE_CLASS	7
#define DACI_FLASH_INTERFACE_SELECT	3
#define DACI_FLASH_ARG_TPM		2

/* These are for dock query capabilities */
struct dock_count_in {
	guint32 argument;
	guint32 reserved1;
	guint32 reserved2;
	guint32 reserved3;
};

struct dock_count_out {
	guint32 ret;
	guint32 count;
	guint32 location;
	guint32 reserved;
};

/* Dock Info version 1 */
#pragma pack(1)
#define MAX_COMPONENTS 5
typedef struct _COMPONENTS {
	guint8		description[80];
	guint32		fw_version; 		/* BCD format: 0x00XXYYZZ */
} COMPONENTS;

typedef struct _DOCK_INFO {
	guint8		dock_description[80];
	guint32		flash_pkg_version;	/* BCD format: 0x00XXYYZZ */
	guint32		cable_type;		/* bit0-7 cable type, bit7-31 set to 0 */
	guint8		location;		/* Location of the dock */
	guint8		reserved;
	guint8		component_count;
	COMPONENTS	components[MAX_COMPONENTS];	/* number of component_count */
} DOCK_INFO;

typedef struct _DOCK_INFO_HEADER {
	guint8		dir_version;  		/* version 1, 2 … */
	guint8		dock_type;
	guint16		reserved;
} DOCK_INFO_HEADER;

typedef struct _DOCK_INFO_RECORD {
	DOCK_INFO_HEADER	dock_info_header; /* dock version specific definition */
	DOCK_INFO		dock_info;
} DOCK_INFO_RECORD;

typedef union _INFO_UNION{
	guint8 *buf;
	DOCK_INFO_RECORD *record;
} INFO_UNION;
#pragma pack()

typedef enum _DOCK_TYPE
{
	DOCK_TYPE_NONE,
	DOCK_TYPE_TB15,
	DOCK_TYPE_WD15
} DOCK_TYPE;

typedef enum _CABLE_TYPE
{
	CABLE_TYPE_NONE,
	CABLE_TYPE_TBT,
	CABLE_TYPE_OTHER,
	CABLE_TYPE_LEGACY
} CABLE_TYPE;

#define DACI_DOCK_CLASS			17
#define DACI_DOCK_SELECT		22
#define DACI_DOCK_ARG_COUNT		0
#define DACI_DOCK_ARG_INFO		1

/* These are for matching the components */
#define WD15_EC		"2 0 2 2 0"
#define TB15_EC		"2 0 2 1 0"
#define TB15_PC_2	"2 1 0 1 1"
#define TB15_PC_1	"2 1 0 1 0"
#define WD15_PC_1	"2 1 0 2 0"
#define LEGACY_CABLE	"2 2 2 1 0"
#define OTHER_CABLE	"2 2 2 2 0"
#define TBT_CABLE	"2 2 2 3 0"

#endif /* __FU_PROVIDER_DELL_H */
