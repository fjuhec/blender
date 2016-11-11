/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/intern/wm_device.c
 *  \ingroup wm
 *
 * Get/set functions and utilities for physical devices (GHOST wrappers).
 */

#ifdef WITH_INPUT_HMD

#include "BKE_context.h"

#include "BLI_math.h"

#include "DNA_userdef_types.h"

#include "GHOST_C-api.h"

#include "WM_api.h"
#include "WM_types.h"
#include "wm.h"


/* -------------------------------------------------------------------- */
/* HMDs */

/** \name Head Mounted Displays
 * \{ */

/* ------ Get/Set Wrappers ------ */

int WM_device_HMD_num_devices_get(void)
{
	return GHOST_HMDgetNumDevices();
}

/**
 * Get index of currently open device.
 */
int WM_device_HMD_current_get(void)
{
	return GHOST_HMDgetOpenDeviceIndex();
}

const char *WM_device_HMD_name_get(int index)
{
	BLI_assert(index < MAX_HMD_DEVICES);
	return GHOST_HMDgetDeviceName(index);
}

const char *WM_device_HMD_vendor_get(int index)
{
	BLI_assert(index < MAX_HMD_DEVICES);
	return GHOST_HMDgetVendorName(index);
}

/**
 * Get IPD from currently opened HMD.
 */
float WM_device_HMD_IPD_get(void)
{
	return GHOST_HMDgetDeviceIPD();
}
void WM_device_HMD_IPD_set(float value)
{
	GHOST_HMDsetDeviceIPD(value);
}


/* ------ Utilities ------ */

/**
 * Enable or disable an HMD.
 */
void WM_device_HMD_state_set(const int device, const bool enable)
{
	BLI_assert(device < MAX_HMD_DEVICES);
	if (enable && (device >= 0)) {
		/* GHOST closes previously opened device if needed */
		GHOST_HMDopenDevice(device);
	}
	else {
		GHOST_HMDcloseDevice();
	}
}

void WM_device_HMD_modelview_matrix_get(const bool is_left, float r_modelviewmat[4][4])
{
	if (U.hmd_settings.device == -1) {
		unit_m4(r_modelviewmat);
	}
	else if (is_left) {
		GHOST_HMDgetLeftModelviewMatrix(r_modelviewmat);
	}
	else {
		GHOST_HMDgetRightModelviewMatrix(r_modelviewmat);
	}
}

void WM_device_HMD_projection_matrix_get(const bool is_left, float r_projmat[4][4])
{
	if (U.hmd_settings.device == -1) {
		unit_m4(r_projmat);
	}
	else if (is_left) {
		GHOST_HMDgetLeftProjectionMatrix(r_projmat);
	}
	else {
		GHOST_HMDgetRightProjectionMatrix(r_projmat);
	}
}

/** \} */

#endif
