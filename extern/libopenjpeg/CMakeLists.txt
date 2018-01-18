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
# The Original Code is Copyright (C) 2006, Blender Foundation
# All rights reserved.
#
# The Original Code is: all of this file.
#
# Contributor(s): Jacques Beaurain.
#
# ***** END GPL LICENSE BLOCK *****

set(INC
	.
)

set(INC_SYS

)

add_definitions(
	-DUSE_JPIP
	# see: https://github.com/uclouvain/openjpeg/issues/254
	${OPENJPEG_DEFINES}
)

set(SRC
	bench_dwt.c
	bio.c
	cidx_manager.c
	cio.c
	dwt.c
	event.c
	function_list.c
	image.c
	invert.c
	j2k.c
	jp2.c
	mct.c
	mqc.c
	openjpeg.c
	opj_clock.c
	opj_malloc.c
	phix_manager.c
	pi.c
	ppix_manager.c
	sparse_array.c
	t1.c
	t1_generate_luts.c
	t2.c
	tcd.c
	test_sparse_array.c
	tgt.c
	thix_manager.c
	thread.c
	tpix_manager.c

	bio.h
	cidx_manager.h
	cio.h
	dwt.h
	event.h
	function_list.h
	image.h
	indexbox_manager.h
	invert.h
	j2k.h
	jp2.h
	mct.h
	mqc.h
	mqc_inl.h
	openjpeg.h
	opj_clock.h
	opj_codec.h
	opj_common.h
	opj_includes.h
	opj_intmath.h
	opj_inttypes.h
	opj_malloc.h
	opj_stdint.h
	pi.h
	sparse_array.h
	t1.h
	t1_luts.h
	t2.h
	tcd.h
	tgt.h
	thread.h
	tls_keys.h
)

blender_add_lib(extern_openjpeg "${SRC}" "${INC}" "${INC_SYS}")
