# ***** BEGIN GPL LICENSE BLOCK *****
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ***** END GPL LICENSE BLOCK *****

set(INC
	.
	src
	include
)

set(INC_SYS

)

set(SRC
	src/openhmd.c
	src/platform-win32.c
	src/drv_dummy/dummy.c
	src/omath.c
	src/platform-posix.c
	src/fusion.c
)

# TODO: Either remove this or move to main CMakeLists.txt
OPTION(OPENHMD_DRIVER_OCULUS_RIFT "Oculus Rift DK1 and DK2" ON)
OPTION(OPENHMD_DRIVER_EXTERNAL "External sensor driver" OFF)
OPTION(OPENHMD_DRIVER_ANDROID "General Android driver" OFF)

if(MSVC)
	add_definitions(-DOHMD_STATIC)
endif(MSVC)


if(OPENHMD_DRIVER_OCULUS_RIFT)
	list(APPEND SRC
		src/drv_oculus_rift/rift.c
		src/drv_oculus_rift/packet.c
	)
	add_definitions(-DDRIVER_OCULUS_RIFT)

	find_package(HIDAPI REQUIRED)
	list(APPEND INC_SYS ${HIDAPI_INCLUDE_DIRS})
endif()

if(OPENHMD_DRIVER_EXTERNAL)
	list(append SRC
		src/drv_external/external.c
	)
	add_definitions(-DDRIVER_EXTERNAL)
endif()

if(OPENHMD_DRIVER_ANDROID)
	list(append SRC
		src/drv_android/android.c
	)
	add_definitions(-DDRIVER_ANDROID)
endif()

blender_add_lib(extern_openhmd "${SRC}" "${INC}" "${INC_SYS}")
