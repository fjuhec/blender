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

    bool available() const;
    bool processEvents();

    bool setDevice(const char* requested_vendor_name, const char* requested_device_name);

protected:
    GHOST_System&   m_system;

private:
    bool            m_available;

    ohmd_context*   m_context;
    ohmd_device*    m_device;
};

#endif //__GHOST_OPENHMDMANAGER_H__

