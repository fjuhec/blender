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
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Lukas Toenne
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/gpencil.c
 *  \ingroup bke
 */

 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"
#include "BLI_string_utils.h"

#include "BLT_translation.h"

#include "DNA_groom_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_animsys.h"
#include "BKE_global.h"
#include "BKE_groom.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_object.h"


void BKE_groom_init(Groom *groom)
{
	BLI_assert(MEMCMP_STRUCT_OFS_IS_ZERO(groom, id));
	
	groom->bb = BKE_boundbox_alloc_unit();
}

void *BKE_groom_add(Main *bmain, const char *name)
{
	Groom *groom = BKE_libblock_alloc(bmain, ID_GM, name, 0);

	BKE_groom_init(groom);

	return groom;
}

static void groom_bundles_free(ListBase *bundles)
{
	for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
	{
		BLI_freelistN(&bundle->sections);
	}
	BLI_freelistN(bundles);
}

/** Free (or release) any data used by this groom (does not free the groom itself). */
void BKE_groom_free(Groom *groom)
{
	if (groom->editgroom)
	{
		EditGroom *edit = groom->editgroom;
		
		groom_bundles_free(&edit->bundles);
		
		MEM_freeN(edit);
		groom->editgroom = NULL;
	}
	
	MEM_SAFE_FREE(groom->bb);
	
	groom_bundles_free(&groom->bundles);
	
	BKE_animdata_free(&groom->id, false);
}

/**
 * Only copy internal data of Groom ID from source to already allocated/initialized destination.
 * You probably never want to use that directly, use id_copy or BKE_id_copy_ex for typical needs.
 *
 * WARNING! This function will not handle ID user count!
 *
 * \param flag  Copying options (see BKE_library.h's LIB_ID_COPY_... flags for more).
 */
void BKE_groom_copy_data(Main *UNUSED(bmain), Groom *groom_dst, const Groom *groom_src, const int UNUSED(flag))
{
	groom_dst->bb = MEM_dupallocN(groom_src->bb);
	
	BLI_duplicatelist(&groom_dst->bundles, &groom_src->bundles);
	
	groom_dst->editgroom = NULL;
}

Groom *BKE_groom_copy(Main *bmain, const Groom *groom)
{
	Groom *groom_copy;
	BKE_id_copy_ex(bmain, &groom->id, (ID **)&groom_copy, 0, false);
	return groom_copy;
}

void BKE_groom_make_local(Main *bmain, Groom *groom, const bool lib_local)
{
	BKE_id_make_local_generic(bmain, &groom->id, true, lib_local);
}


bool BKE_groom_minmax(Groom *groom, float min[3], float max[3])
{
	// TODO
	UNUSED_VARS(groom, min, max);
	return true;
}

void BKE_groom_boundbox_calc(Groom *groom, float r_loc[3], float r_size[3])
{
	if (groom->bb == NULL)
	{
		groom->bb = MEM_callocN(sizeof(BoundBox), "boundbox");
	}

	float mloc[3], msize[3];
	if (!r_loc)
	{
		r_loc = mloc;
	}
	if (!r_size)
	{
		r_size = msize;
	}

	float min[3], max[3];
	INIT_MINMAX(min, max);
	if (!BKE_groom_minmax(groom, min, max)) {
		min[0] = min[1] = min[2] = -1.0f;
		max[0] = max[1] = max[2] = 1.0f;
	}

	mid_v3_v3v3(r_loc, min, max);

	r_size[0] = (max[0] - min[0]) / 2.0f;
	r_size[1] = (max[1] - min[1]) / 2.0f;
	r_size[2] = (max[2] - min[2]) / 2.0f;

	BKE_boundbox_init_from_minmax(groom->bb, min, max);
	groom->bb->flag &= ~BOUNDBOX_DIRTY;
}
