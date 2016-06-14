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
 * The Original Code is Copyright (C) 2016 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Kevin Dietrich.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/cachefile.c
 *  \ingroup bke
 */

#include "DNA_cachefile_types.h"
#include "DNA_scene_types.h"

#include "BLI_fileops.h"
#include "BLI_path_util.h"
#include "BLI_string.h"

#include "BKE_cachefile.h"
#include "BKE_global.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_scene.h"

void *BKE_cachefile_add(Main *bmain, const char *name)
{
	CacheFile *cache_file = BKE_libblock_alloc(bmain, ID_CF, name);

	cache_file->filepath[0] = '\0';
	cache_file->frame_start = 0.0f;
	cache_file->frame_scale = 1.0f;
	cache_file->is_sequence = false;

	return cache_file;
}

bool BKE_cachefile_filepath_get(Scene *scene, CacheFile *cache_file, char *r_filepath)
{
	const float frame = BKE_scene_frame_get(scene);
	BLI_strncpy(r_filepath, cache_file->filepath, 1024);

	/* Ensure absolute paths. */
	if (BLI_path_is_rel(r_filepath)) {
		BLI_path_abs(r_filepath, G.main->name);
	}

	int fframe;
	int frame_len;

	if (cache_file->is_sequence && BLI_path_frame_get(r_filepath, &fframe, &frame_len)) {
		char ext[32];
		BLI_path_frame_strip(r_filepath, true, ext);
		BLI_path_frame(r_filepath, frame, frame_len);
		BLI_ensure_extension(r_filepath, 1024, ext);

		/* TODO(kevin): store sequence range? */
		return BLI_exists(r_filepath);
	}

	return true;
}

float BKE_cachefile_time_offset(CacheFile *cache_file, float time)
{
	return (cache_file->frame_scale * time) - cache_file->frame_start;
}
