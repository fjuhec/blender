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
 * Data functions for physical devices (GHOST wrappers).
 */

#include "BKE_context.h"

#include "DNA_userdef_types.h"

#include "GHOST_C-api.h"

#include "WM_api.h"
#include "WM_types.h"
#include "wm.h"


/* -------------------------------------------------------------------- */
/* HMDs */

/** \name Head Mounted Displays
 * \{ */

/* XXX doing WITH_OPENHMD checks here is ugly */

int WM_device_HMD_num_devices_get(void)
{
#ifdef WITH_OPENHMD
	return GHOST_HMDgetNumDevices();
#else
	return 0.0f;
#endif
}

/**
 * Enable or disable an HMD.
 */
void WM_device_HMD_state_set(const int device, const bool enable)
{
#ifdef WITH_OPENHMD
	if (enable && (device >= 0)) {
		/* GHOST closes previously opened device if needed */
		GHOST_HMDopenDevice(device);
	}
	else {
		GHOST_HMDcloseDevice();
	}
#else
	UNUSED_VARS(device, enable);
#endif
}

/**
 * Get index of currently open device.
 */
int WM_device_HMD_current_get(void)
{
#ifdef WITH_OPENHMD
	return GHOST_HMDgetOpenDeviceIndex();
#else
	return -1;
#endif
}

const char *WM_device_HMD_name_get(int index)
{
#ifdef WITH_OPENHMD
	return GHOST_HMDgetDeviceName(index);
#else
	UNUSED_VARS(index);
	return "";
#endif
}

/**
 * Get IPD from currently opened HMD.
 */
float WM_device_HMD_IPD_get(void)
{
#ifdef WITH_OPENHMD
	return GHOST_HMDgetDeviceIPD();
#else
	return 0.0f;
#endif
}

/** \} */
