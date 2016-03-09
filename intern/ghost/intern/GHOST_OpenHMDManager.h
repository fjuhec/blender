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
 * Contributor(s):
 *   Mike Erwin
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __GHOST_OPENHMDMANAGER_H__
#define __GHOST_OPENHMDMANAGER_H__

#include "GHOST_System.h"

// TODO probably shouldn't forward declare this here
struct ohmd_context;
struct ohmd_device;

class GHOST_OpenHMDManager
{
public:
    GHOST_OpenHMDManager(GHOST_System&); // TODO maybe cut out dependency on the system? (only used for getMilliSeconds and none of the others without platform implementations use it)
    virtual ~GHOST_OpenHMDManager();

    bool processEvents();

protected:
    GHOST_System&   m_system;

private:
    bool            m_available;

    ohmd_context*   m_context;
    ohmd_device*    m_device;





    /*
	// whether multi-axis functionality is available (via the OS or driver)
	// does not imply that a device is plugged in or being used
	virtual bool available() = 0;

	// each platform's device detection should call this
	// use standard USB/HID identifiers
	bool setDevice(unsigned short vendor_id, unsigned short product_id);

	// the latest raw axis data from the device
	// NOTE: axis data should be in blender view coordinates
	//       +X is to the right
	//       +Y is up
	//       +Z is out of the screen
	//       for rotations, look from origin to each +axis
	//       rotations are + when CCW, - when CW
	// each platform is responsible for getting axis data into this form
	// these values should not be scaled (just shuffled or flipped)
	void updateTranslation(const short t[3], GHOST_TUns64 time);
	void updateRotation(const short r[3], GHOST_TUns64 time);

	// processes and sends most recent raw data as an NDOFMotion event
	// returns whether an event was sent
	bool sendMotionEvent();

protected:
	GHOST_System& m_system;

private:
	void sendButtonEvent(NDOF_ButtonT, bool press, GHOST_TUns64 time, GHOST_IWindow *);
	void sendKeyEvent(GHOST_TKey, bool press, GHOST_TUns64 time, GHOST_IWindow *);

	NDOF_DeviceT m_deviceType;
	int m_buttonCount;
	int m_buttonMask;
	const NDOF_ButtonT *m_hidMap;

	short m_translation[3];
	short m_rotation[3];
	int m_buttons; // bit field

	GHOST_TUns64 m_motionTime; // in milliseconds
	GHOST_TUns64 m_prevMotionTime; // time of most recent Motion event sent

	GHOST_TProgress m_motionState;
	bool m_motionEventPending;
	float m_deadZone; // discard motion with each component < this
	*/
};

#endif //__GHOST_OPENHMDMANAGER_H__

