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

#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_kdtree.h"
#include "BLI_listbase.h"
#include "BLI_rand.h"
#include "BLI_string_utf8.h"
#include "BLI_string_utils.h"

#include "DNA_hair_types.h"

#include "BKE_DerivedMesh.h"
#include "BKE_mesh_sample.h"
#include "BKE_hair.h"

#include "BLT_translation.h"

HairPattern* BKE_hair_new(void)
{
	HairPattern *hair = MEM_callocN(sizeof(HairPattern), "hair");
	
	/* add a default hair group */
	BKE_hair_group_new(hair, HAIR_GROUP_TYPE_NORMALS);
	
	return hair;
}

HairPattern* BKE_hair_copy(HairPattern *hair)
{
	HairPattern *nhair = MEM_dupallocN(hair);
	
	nhair->follicles = MEM_dupallocN(hair->follicles);
	
	BLI_duplicatelist(&nhair->groups, &hair->groups);
	
	return nhair;
}

void BKE_hair_free(struct HairPattern *hair)
{
	if (hair->follicles) {
		MEM_freeN(hair->follicles);
	}
	
	BLI_freelistN(&hair->groups);
	
	MEM_freeN(hair);
}

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
	
	MeshSampleGenerator *gen = BKE_mesh_sample_gen_surface_random(scalp, seed);
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
}

HairGroup* BKE_hair_group_new(HairPattern *hair, int type)
{
	HairGroup *group = MEM_callocN(sizeof(HairGroup), "hair group");
	
	group->type = type;
	BKE_hair_group_name_set(hair, group, DATA_("Group"));
	
	switch (type) {
		case HAIR_GROUP_TYPE_NORMALS:
			group->normals_max_length = 0.1f;
			break;
		case HAIR_GROUP_TYPE_STRANDS:
			break;
	}
	
	BLI_addtail(&hair->groups, group);
	
	return group;
}

void BKE_hair_group_remove(HairPattern *hair, HairGroup *group)
{
	if (!group) {
		return;
	}
	BLI_assert(BLI_findindex(&hair->groups, group) >= 0);
	
	BLI_remlink(&hair->groups, group);
	MEM_freeN(group);
}

HairGroup* BKE_hair_group_copy(HairPattern *hair, HairGroup *group)
{
	if (!group) {
		return NULL;
	}
	
	HairGroup *ngroup = MEM_dupallocN(group);
	
	BLI_insertlinkafter(&hair->groups, group, ngroup);
	return ngroup;
}

void BKE_hair_group_moveto(HairPattern *hair, HairGroup *group, int position)
{
	if (!group) {
		return;
	}
	BLI_assert(BLI_findindex(&hair->groups, group) >= 0);
	
	BLI_remlink(&hair->groups, group);
	BLI_insertlinkbefore(&hair->groups, BLI_findlink(&hair->groups, position), group);
}

void BKE_hair_group_name_set(HairPattern *hair, HairGroup *group, const char *name)
{
	BLI_strncpy_utf8(group->name, name, sizeof(group->name));
	BLI_uniquename(&hair->groups, group, DATA_("Group"), '.', offsetof(HairGroup, name), sizeof(group->name));
}

/* ================================= */

typedef struct HairGroupStrandsView {
	HairDrawDataInterface base;
	int numstrands;
	int numverts_orig;
} HairGroupStrandsView;

static int get_num_strands(const HairDrawDataInterface *hairdata_)
{
	const HairGroupStrandsView *strands = (HairGroupStrandsView *)hairdata_;
	return strands->numstrands;
}

static int get_num_verts(const HairDrawDataInterface *hairdata_)
{
	const HairGroupStrandsView *strands = (HairGroupStrandsView *)hairdata_;
	return strands->numverts_orig;
}

static void get_strand_lengths(const HairDrawDataInterface* hairdata_, int *r_lengths)
{
	const HairGroupStrandsView *strands = (HairGroupStrandsView *)hairdata_;
	for (int i = 0; i < strands->numstrands; ++i)
	{
		// TODO
		r_lengths[i] = 0;
	}
}

static void get_strand_roots(const HairDrawDataInterface* hairdata_, struct MeshSample *r_roots)
{
	const HairGroupStrandsView *strands = (HairGroupStrandsView *)hairdata_;
	for (int i = 0; i < strands->numstrands; ++i)
	{
		// TODO
		memset(&r_roots[i], 0, sizeof(MeshSample));
	}
}

static void get_strand_vertices(const HairDrawDataInterface* hairdata_, float (*r_verts)[3])
{
	const HairGroupStrandsView *strands = (HairGroupStrandsView *)hairdata_;
	for (int i = 0; i < strands->numverts_orig; ++i)
	{
		// TODO
		zero_v3(r_verts[i]);
	}
}

static HairGroupStrandsView hair_strands_get_view(HairGroup *group)
{
	HairGroupStrandsView hairdata;
	hairdata.base.get_num_strands = get_num_strands;
	hairdata.base.get_num_verts = get_num_verts;
	hairdata.base.get_strand_lengths = get_strand_lengths;
	hairdata.base.get_strand_roots = get_strand_roots;
	hairdata.base.get_strand_vertices = get_strand_vertices;
	
	switch (group->type) {
		case HAIR_GROUP_TYPE_NORMALS: {
			hairdata.numstrands = 0;
			hairdata.numverts_orig = 0;
			break;
		}
		case HAIR_GROUP_TYPE_STRANDS: {
			// TODO
			hairdata.numstrands = 0;
			hairdata.numverts_orig = 0;
			break;
		}
	}
	
	return hairdata;
}

void BKE_hair_group_get_texture_buffer_size(HairGroup *group, int subdiv,
                                            int *r_size, int *r_strand_map_start,
                                            int *r_strand_vertex_start, int *r_fiber_start)
{
	HairGroupStrandsView hairdata = hair_strands_get_view(group);
	BKE_hair_strands_get_texture_buffer_size(&hairdata.base, group->num_follicles, subdiv,
	                                         r_size, r_strand_map_start, r_strand_vertex_start, r_fiber_start);
}

void BKE_hair_group_get_texture_buffer(HairGroup *group, DerivedMesh *scalp, int subdiv, void *buffer)
{
	HairGroupStrandsView hairdata = hair_strands_get_view(group);
	BKE_hair_strands_get_texture_buffer(&hairdata.base, scalp, NULL, 0, subdiv, buffer);
}
