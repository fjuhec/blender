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

/** \file blender/blenkernel/intern/hair.c
 *  \ingroup bke
 */

#include <limits.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_kdtree.h"
#include "BLI_listbase.h"
#include "BLI_rand.h"
#include "BLI_sort.h"
#include "BLI_string_utf8.h"
#include "BLI_string_utils.h"

#include "DNA_hair_types.h"

#include "BKE_DerivedMesh.h"
#include "BKE_mesh_sample.h"
#include "BKE_hair.h"

#include "BLT_translation.h"

HairSystem* BKE_hair_new(void)
{
	HairSystem *hair = MEM_callocN(sizeof(HairSystem), "hair system");
	
	return hair;
}

HairSystem* BKE_hair_copy(HairSystem *hsys)
{
	HairSystem *nhsys = MEM_dupallocN(hsys);
	
	if (hsys->pattern)
	{
		nhsys->pattern = MEM_dupallocN(hsys->pattern);
		nhsys->pattern->follicles = MEM_dupallocN(hsys->pattern->follicles);
	}
	
	return nhsys;
}

void BKE_hair_free(HairSystem *hsys)
{
	if (hsys->pattern)
	{
		if (hsys->pattern->follicles)
		{
			MEM_freeN(hsys->pattern->follicles);
		}
		MEM_freeN(hsys->pattern);
	}
	
	MEM_freeN(hsys);
}

#if 0
void BKE_hair_set_num_follicles(HairPattern *hair, int count)
{
	if (hair->num_follicles != count) {
		if (count > 0) {
			if (hair->follicles) {
				hair->follicles = MEM_reallocN_id(hair->follicles, sizeof(HairFollicle) * count, "hair follicles");
			}
			else {
				hair->follicles = MEM_callocN(sizeof(HairFollicle) * count, "hair follicles");
			}
		}
		else {
			if (hair->follicles) {
				MEM_freeN(hair->follicles);
				hair->follicles = NULL;
			}
		}
		hair->num_follicles = count;
	}
}

void BKE_hair_follicles_generate(HairPattern *hair, DerivedMesh *scalp, int count, unsigned int seed)
{
	BKE_hair_set_num_follicles(hair, count);
	if (count == 0) {
		return;
	}
	
	MeshSampleGenerator *gen = BKE_mesh_sample_gen_surface_random(seed, true, NULL, NULL);
	BKE_mesh_sample_generator_bind(gen, scalp);
	unsigned int i;
	
	HairFollicle *foll = hair->follicles;
	for (i = 0; i < count; ++i, ++foll) {
		bool ok = BKE_mesh_sample_generate(gen, &foll->mesh_sample);
		if (!ok) {
			/* clear remaining samples */
			memset(foll, 0, sizeof(HairFollicle) * (count - i));
			break;
		}
	}
	
	BKE_mesh_sample_free_generator(gen);
	
	BKE_hair_batch_cache_dirty(hair, BKE_HAIR_BATCH_DIRTY_ALL);
	
	BKE_hair_update_groups(hair);
}
#endif

/* ================================= */
