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

#include "DNA_anim_types.h"
#include "DNA_cachefile_types.h"
#include "DNA_scene_types.h"

#include "BLI_fileops.h"
#include "BLI_listbase.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BKE_animsys.h"
#include "BKE_cachefile.h"
#include "BKE_global.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_scene.h"

#ifdef WITH_ALEMBIC
#  include "ABC_alembic.h"
#endif

static void get_absolute_path(char *r_absolute, const char *relative, const char *base)
{
	BLI_strncpy(r_absolute, relative, FILE_MAX);

	if (BLI_path_is_rel(r_absolute)) {
		BLI_path_abs(r_absolute, base);
	}
}

void *BKE_cachefile_add(Main *bmain, const char *name)
{
	CacheFile *cache_file = BKE_libblock_alloc(bmain, ID_CF, name);

	cache_file->handle = NULL;
	cache_file->filepath[0] = '\0';
	cache_file->override_frame = false;
	cache_file->frame = 0.0f;
	cache_file->is_sequence = false;
	cache_file->scale = 1.0f;

	return cache_file;
}

/** Free (or release) any data used by this cachefile (does not free the cachefile itself). */
void BKE_cachefile_free(CacheFile *cache_file)
{
	BKE_animdata_free((ID *)cache_file, false);

#ifdef WITH_ALEMBIC
	ABC_free_handle(cache_file->handle);
#endif

	BLI_freelistN(&cache_file->object_paths);
}

CacheFile *BKE_cachefile_copy(Main *bmain, CacheFile *cache_file)
{
	CacheFile *new_cache_file = BKE_cachefile_add(bmain, cache_file->id.name + 2);

	BLI_strncpy(new_cache_file->filepath, cache_file->filepath, FILE_MAX);

	new_cache_file->frame = cache_file->frame;
	new_cache_file->override_frame = cache_file->override_frame;
	new_cache_file->is_sequence = cache_file->is_sequence;
	new_cache_file->scale = cache_file->scale;

	if (cache_file->handle) {
		BKE_cachefile_load(new_cache_file, bmain->name);
	}

	if (ID_IS_LINKED_DATABLOCK(cache_file)) {
		BKE_id_lib_local_paths(bmain, cache_file->id.lib, &new_cache_file->id);
	}

	return new_cache_file;
}

void BKE_cachefile_load(CacheFile *cache_file, const char *relabase)
{
	char filename[FILE_MAX];
	get_absolute_path(filename, cache_file->filepath, relabase);

#ifdef WITH_ALEMBIC
	if (cache_file->handle) {
		ABC_free_handle(cache_file->handle);
	}

	cache_file->handle = ABC_create_handle(filename, &cache_file->object_paths);
#endif
}

void BKE_cachefile_update_frame(Main *bmain, Scene *scene, const float ctime, const float fps)
{
	CacheFile *cache_file;
	char filename[FILE_MAX];

	for (cache_file = bmain->cachefiles.first; cache_file; cache_file = cache_file->id.next) {
		/* Execute drivers only, as animation has already been done. */
		BKE_animsys_evaluate_animdata(scene, &cache_file->id, cache_file->adt, ctime, ADT_RECALC_DRIVERS);

		if (!cache_file->is_sequence) {
			continue;
		}

		const float time = BKE_cachefile_time_offset(cache_file, ctime, fps);

		if (BKE_cachefile_filepath_get(cache_file, time, filename)) {
#ifdef WITH_ALEMBIC
			ABC_free_handle(cache_file->handle);
			cache_file->handle = ABC_create_handle(filename, NULL);
#endif
		}
	}
}

bool BKE_cachefile_filepath_get(CacheFile *cache_file, float frame, char *r_filepath)
{
	get_absolute_path(r_filepath, cache_file->filepath, G.main->name);

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

float BKE_cachefile_time_offset(CacheFile *cache_file, const float time, const float fps)
{
	const float frame = (cache_file->override_frame ? cache_file->frame : time);
	return cache_file->is_sequence ? frame : frame / fps;
}
