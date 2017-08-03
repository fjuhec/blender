/*
 * Copyright 2016, Blender Foundation.
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
 * Contributor(s): Blender Institute
 *
 */

/** \file draw_hair.c
 *  \ingroup draw
 */

#include "DNA_scene_types.h"

#include "DRW_render.h"

#include "BLI_utildefines.h"

#include "GPU_extensions.h"
#include "GPU_texture.h"

#include "draw_common.h"

const char* DRW_hair_shader_defines(void)
{
	static char str[256];
	
	BLI_snprintf(str, sizeof(str), "#define HAIR_SHADER_FIBERS\n#define HAIR_SHADER_TEX_WIDTH %d\n",
	             GPU_max_texture_size());
	
	return str;
}

void DRW_hair_shader_uniforms(DRWShadingGroup *shgrp, Scene *scene,
                              GPUTexture **fibertex, const DRWHairFiberTextureBuffer *texbuffer)
{
	const HairEditSettings *tsettings = &scene->toolsettings->hair_edit;
	
	DRW_shgroup_uniform_vec2(shgrp, "viewport_size", DRW_viewport_size_get(), 1);
	DRW_shgroup_uniform_float(shgrp, "ribbon_width", &tsettings->hair_draw_size, 1);
	
	DRW_shgroup_uniform_buffer(shgrp, "strand_data", fibertex);
	DRW_shgroup_uniform_int(shgrp, "strand_map_start", &texbuffer->strand_map_start, 1);
	DRW_shgroup_uniform_int(shgrp, "strand_vertex_start", &texbuffer->strand_vertex_start, 1);
	DRW_shgroup_uniform_int(shgrp, "fiber_start", &texbuffer->fiber_start, 1);
}
