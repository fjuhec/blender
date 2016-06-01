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
# The Original Code is Copyright (C) 2011, Blender Foundation
# All rights reserved.
#
# The Original Code is: all of this file.
#
# Contributor(s): Jeroen Bakker, Monique Dewanchand, Blender Developers Fund.
#
# ***** END GPL LICENSE BLOCK *****

set(INC
	.
	cmp
	device
	kernel
	kernel/cvm

	../blenkernel
	../blenlib
	../blentranslation
	../imbuf
	../makesdna
	../makesrna
	../windowmanager
	../nodes
	../nodes/composite
	../nodes/intern
	../render/extern/include
	../render/intern/include
	../../../extern/clew/include
	../../../intern/guardedalloc
	../../../intern/atomic
)

set(INC_SYS

)

set(SRC
	COM_compositor.h
	COM_defines.h

	cmp/cmp_compositor.cpp
	cmp/cmp_node.cpp
	cmp/cmp_nodesocket.cpp
	cmp/cmp_unroll.cpp
	cmp/cmp_output.cpp
	cmp/cmp_tilemanager.cpp

	device/device.cpp
	device/device_task.cpp
	device/device_cpu.cpp
	device/device_glsl.cpp
)

# Kernels
data_to_c_simple(kernel/kernel.h SRC)
data_to_c_simple(kernel/kernel_types.h SRC)
data_to_c_simple(kernel/kernel_functions.h SRC)
data_to_c_simple(kernel/kernel_constants.h SRC)

# CVM Nodes
data_to_c_simple(kernel/cvm/cvm_node_blur.h SRC)
data_to_c_simple(kernel/cvm/cvm_node_color.h SRC)
data_to_c_simple(kernel/cvm/cvm_node_dummy.h SRC)
data_to_c_simple(kernel/cvm/cvm_node_mix.h SRC)
data_to_c_simple(kernel/cvm/cvm_node_image.h SRC)
data_to_c_simple(kernel/cvm/cvm_node_value.h SRC)
data_to_c_simple(kernel/cvm/cvm_node_viewer.h SRC)

#list(APPEND INC
#	${CMAKE_CURRENT_BINARY_DIR}/operations
#)

if(WITH_INTERNATIONAL)
	add_definitions(-DWITH_INTERNATIONAL)
endif()

if(WITH_CYCLES AND WITH_CYCLES_DEBUG)
	add_definitions(-DWITH_CYCLES_DEBUG)
endif()

blender_add_lib(bf_compositor "${SRC}" "${INC}" "${INC_SYS}")
