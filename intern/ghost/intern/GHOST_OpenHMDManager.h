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
	// TODO maybe cut out dependency on the system? (only used for getMilliSeconds and
	// none of the others without platform implementations use it)
	GHOST_OpenHMDManager(GHOST_System&);
	virtual ~GHOST_OpenHMDManager();

	/**
	 *  \return True if there is a device opened and ready for polling, false otherwise.
	 */
	bool available() const;

	/**
	 *  Update the device context and generate an event containing the current orientation of the device.
	 *  \return A boolean indicating success.
	 */
	bool processEvents();

	/**
	 *  Select the device matching the given vendor and device name.
	 *  This device will then be the device for which ghost events will be generated.
	 *  If no device with the correct name and vendor can be found, or an error occurs,
	 *      the current device is preserved.
	 *  \param requested_vendor_name    The exact name of the vendor for the requested device
	 *  \param requested_device_name    The exact name of the requested device.
	 *  \return A boolean indicating success.
	 */
	bool setDevice(const char *requested_vendor_name, const char *requested_device_name);

	/**
	 *  Select a device by index
	 *  \param index    The index of the requested device
	 *  See setDevice(const char*, const char*) for more information.
	 */
	bool setDevice(int index);

	/**
	 *  \return The number of connected devices.
	 *  -1 is returned if available() is false.
	 */
	int getNumDevices() const;

	/**
	 *  \return A c-style string containing the last error as a human-readable message
	 *  NULL is returned if available() is false.
	 */
	 const char *getError() const;

	/**
	 *  \return A c-style string with the human-readable name of the current device.
	 *  NULL is returned if available() is false.
	 */
	const char *getDeviceName() const;

	/**
	 *  \return A c-style string with the human-readable name of the vendor of the the current device.
	 *  NULL is returned if available() is false.
	 */
	const char *getVendorName() const;

	/**
	 *  \return A c-style string with the driver-specific path where the current device is attached.
	 *  NULL is returned if available() is false.
	 */
	const char *getPath() const;

	/**
	 * \param orientation   The absolute orientation of the device, as quaternion, in blender format (w,x,y,z)
	 *  Nothing is written if available() is false.
	 */
	void    getRotationQuat(float orientation[4]) const;

	/**
	 * \param mat   A "ready to use" OpenGL style 4x4 matrix with a modelview matrix for the left eye of the HMD.
	 *  Nothing is written if available() is false.
	 */
	void    getLeftEyeGLModelviewMatrix(float mat[16]) const;

	/**
	 * \param mat   A "ready to use" OpenGL style 4x4 matrix with a modelview matrix for the right eye of the HMD.
	 *  Nothing is written if available() is false.
	 */
	void    getRightEyeGLModelviewMatrix(float mat[16]) const;

	/**
	 * \param mat   A "ready to use" OpenGL style 4x4 matrix with a projection matrix for the left eye of the HMD.
	 *  Nothing is written if available() is false.
	 */
	void    getLeftEyeGLProjectionMatrix(float mat[16]) const;

	/**
	 * \param mat   A "ready to use" OpenGL style 4x4 matrix with a projection matrix for the right eye of the HMD.
	 *  Nothing is written if available() is false.
	 */
	void    getRightEyeGLProjectionMatrix(float mat[16]) const;

	 /**
	 * \param position  A 3-D vector representing the absolute position of the device, in space.
	 *  Nothing is written if available() is false.
	 */
	void    getPositionVector(float position[3]) const;

	/**
	 * \return  Physical width of the device screen in metres.
	 *  -1 is returned if available() is false.
	 */
	float   getScreenHorizontalSize() const;

	/**
	 * \return  Physical height of the device screen in metres.
	 *  -1 is returned if available() is false.
	 */
	float   getScreenVerticalSize() const;

	/**
	 * \return  Physical separation of the device lenses in metres.
	 *  -1 is returned if available() is false.
	 */
	float   getLensHorizontalSeparation() const;

	/**
	 * \return  Physical vertical position of the lenses in metres.
	 *  -1 is returned if available() is false.
	 */
	float   getLensVerticalPosition() const;

	/**
	 * \return  Physical field of view for the left eye in degrees.
	 *  -1 is returned if available() is false.
	 */
	float   getLeftEyeFOV() const;

	/**
	 * \return  Physical display aspect ratio for the left eye screen.
	 *  -1 is returned if available() is false.
	 */
	float   getLeftEyeAspectRatio() const;

	/**
	 * \return  Physical display aspect ratio for the left eye screen.
	 *  -1 is returned if available() is false.
	 */
	float   getRightEyeFOV() const;

	/**
	 * \return  Physical display aspect ratio for the right eye screen.
	 *  -1 is returned if available() is false.
	 */
	float   getRightEyeAspectRatio() const;

	/**
	 * \return  Physical interpupillary distance of the user in metres.
	 *  -1 is returned if available() is false.
	 */
	float   getEyeIPD() const;

	/**
	 * \return   Z-far value for the projection matrix calculations (i.e. drawing distance).
	 *  -1 is returned if available() is false.
	 */
	float   getProjectionZFar() const;

	/**
	 * \return  Z-near value for the projection matrix calculations (i.e. close clipping distance).
	 *  -1 is returned if available() is false.
	 */
	float   getProjectionZNear() const;

	/**
	 * \param distortion    Device specific distortion value.
	 *  Nothing is written if available() is false.
	 */
	void    getDistortion(float distortion[6]) const;

	/**
	 *  \return Physical horizontal resolution of the device screen.
	 *  -1 is returned if available() is false.
	 */
	int     getScreenHorizontalResolution() const;

	/**
	 *  \return Physical vertical resolution of the device screen.
	 *  -1 is returned if available() is false.
	 */
	int     getScreenVerticalResolution() const;

	/**
	 *  Sets the physical interpupillary distance of the user in metres.
	 *  This function can only succeed if available() is true.
	 *  \param val  The value to be set.
	 *  \return     A boolean indicating success.
	 */
	bool setEyeIPD(float val);
	/**

	 *  Sets the Z-far value for the projection matrix calculations (i.e. drawing distance).
	 *  This function can only succeed if available() is true.
	 *  \param val  The value to be set.
	 *  \return     A boolean indicating success.
	 */
	bool setProjectionZFar(float val);

	/**
	 *  Sets the Z-near value for the projection matrix calculations (i.e. close clipping distance).
	 *  This function can only succeed if available() is true.
	 *  \param val  The value to be set.
	 *  \return     A boolean indicating success.
	 */
	bool setProjectionZNear(float val);

	/**
	 *  Get the internal OpenHMD context of this manager.
	 *  \return The context
	 *  Context is only valid if available() is true.
	 */
	 ohmd_context *getOpenHMDContext();

	/**
	 *  Get the internal OpenHMD device for the currently selected device of this manager.
	 *  \return The device
	 *  Device is only valid if available() is true.
	 */
	 ohmd_device *getOpenHMDDevice();

	 /**
	  * Get the index of the currently selected device of this manager.
	  * \return The index.
	  * Index is only valid if available() is true.
	  */
	 const int getDeviceIndex();

protected:
	GHOST_System& m_system;

private:
	bool m_available;

	ohmd_context *m_context;
	ohmd_device *m_device;
	int m_deviceIndex;
};

#endif //__GHOST_OPENHMDMANAGER_H__
