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

static void hair_group_free(HairGroup *group)
{
	BKE_hair_batch_cache_free(group);
	
	if (group->strands_parent_index) {
		MEM_freeN(group->strands_parent_index);
	}
	if (group->strands_parent_weight) {
		MEM_freeN(group->strands_parent_weight);
	}
}

void BKE_hair_free(struct HairPattern *hair)
{
	if (hair->follicles) {
		MEM_freeN(hair->follicles);
	}
	
	for (HairGroup *group = hair->groups.first; group; group = group->next) {
		hair_group_free(group);
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
	
	BKE_hair_batch_cache_all_dirty(hair, BKE_HAIR_BATCH_DIRTY_ALL);
	
	BKE_hair_update_groups(hair);
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
	
	hair_group_free(group);
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

#define HAIR_FOLLICLE_GROUP_NONE INT_MAX

static void hair_claim_group_follicle(HairGroup *group, int group_index, int *follicle_group, int i)
{
	if (follicle_group[i] == HAIR_FOLLICLE_GROUP_NONE) {
		follicle_group[i] = group_index;
		++group->num_follicles;
	}
}

static void hair_group_follicles_normals(HairPattern *hair, HairGroup *group, int group_index, int *follicle_group)
{
	const int num_follicles = hair->num_follicles;
	for (int i = 0; i < num_follicles; ++i) {
		// claim all
		hair_claim_group_follicle(group, group_index, follicle_group, i);
	}
}

static void hair_group_follicles_strands(HairPattern *hair, HairGroup *group, int group_index, int *follicle_group)
{
	// TODO
	UNUSED_VARS(hair, group, group_index, follicle_group);
}

typedef struct HairFollicleSortContext {
	const HairFollicle *start;
	const int *follicle_group;
} HairFollicleSortContext;

static int cmpHairFollicleByGroup(const void *a, const void *b, void *ctx_)
{
	const HairFollicleSortContext *ctx = (const HairFollicleSortContext *)ctx_;
	const size_t ia = (const HairFollicle *)a - ctx->start;
	const size_t ib = (const HairFollicle *)b - ctx->start;
	return ctx->follicle_group[ib] - ctx->follicle_group[ia];
}

void BKE_hair_update_groups(HairPattern *hair)
{
	const int num_follicles = hair->num_follicles;
	int *follicle_group = MEM_mallocN(sizeof(int) * num_follicles, "hair follicle group index");
	for (int i = 0; i < num_follicles; ++i) {
		follicle_group[i] = HAIR_FOLLICLE_GROUP_NONE;
	}
	
	int group_index = 0;
	for (HairGroup *group = hair->groups.first; group; group = group->next, ++group_index) {
		// Note: follicles array is sorted below
		if (group->prev) {
			group->follicles = group->prev->follicles + group->prev->num_follicles;
		}
		else {
			group->follicles = hair->follicles;
		}
		
		group->num_follicles = 0;
		switch (group->type) {
			case HAIR_GROUP_TYPE_NORMALS:
				hair_group_follicles_normals(hair, group, group_index, follicle_group);
				break;
			case HAIR_GROUP_TYPE_STRANDS:
				hair_group_follicles_strands(hair, group, group_index, follicle_group);
				break;
		}
	}
	
	{
		HairFollicleSortContext ctx;
		ctx.start = hair->follicles;
		ctx.follicle_group = follicle_group;
		BLI_qsort_r(hair->follicles, num_follicles, sizeof(HairFollicle), cmpHairFollicleByGroup, &ctx);
	}
	
	MEM_freeN(follicle_group);
	
	BKE_hair_batch_cache_all_dirty(hair, BKE_HAIR_BATCH_DIRTY_ALL);
}

/* ================================= */

typedef struct HairGroupDrawDataInterface {
	HairDrawDataInterface base;
	int numstrands;
	int numverts_orig;
} HairGroupDrawDataInterface;

static int get_num_strands(const HairDrawDataInterface *hairdata_)
{
	const HairGroupDrawDataInterface *hairdata = (HairGroupDrawDataInterface *)hairdata_;
	return hairdata->numstrands;
}

static int get_num_verts(const HairDrawDataInterface *hairdata_)
{
	const HairGroupDrawDataInterface *hairdata = (HairGroupDrawDataInterface *)hairdata_;
	return hairdata->numverts_orig;
}

static void get_strand_lengths_normals(const HairDrawDataInterface* hairdata_, int *r_lengths)
{
	const HairGroupDrawDataInterface *hairdata = (HairGroupDrawDataInterface *)hairdata_;
	for (int i = 0; i < hairdata->numstrands; ++i)
	{
		r_lengths[i] = 2;
	}
}

static void get_strand_roots_normals(const HairDrawDataInterface* hairdata_, struct MeshSample *r_roots)
{
	const HairGroupDrawDataInterface *hairdata = (HairGroupDrawDataInterface *)hairdata_;
	DerivedMesh *scalp = hairdata->base.scalp;
	
	MeshSampleGenerator *gen = BKE_mesh_sample_gen_surface_vertices();
	BKE_mesh_sample_generator_bind(gen, scalp);
	
	int i = 0;
	for (; i < hairdata->numstrands; ++i)
	{
		if (!BKE_mesh_sample_generate(gen, &r_roots[i])) {
			break;
		}
	}
	// clear remaining samples, if any
	for (; i < hairdata->numstrands; ++i)
	{
		BKE_mesh_sample_clear(&r_roots[i]);
	}
	
	BKE_mesh_sample_free_generator(gen);
}

static void get_strand_vertices_normals(const HairDrawDataInterface* hairdata_, float (*r_verts)[3])
{
	const HairGroupDrawDataInterface *hairdata = (HairGroupDrawDataInterface *)hairdata_;
	DerivedMesh *scalp = hairdata->base.scalp;
	
	MeshSampleGenerator *gen = BKE_mesh_sample_gen_surface_vertices();
	BKE_mesh_sample_generator_bind(gen, scalp);
	
	int i = 0;
	for (; i < hairdata->numstrands; ++i)
	{
		MeshSample sample;
		if (!BKE_mesh_sample_generate(gen, &sample)) {
			break;
		}
		
		float co[3], nor[3], tang[3];
		BKE_mesh_sample_eval(scalp, &sample, co, nor, tang);
		
		copy_v3_v3(r_verts[i << 1], co);
		madd_v3_v3v3fl(r_verts[(i << 1) + 1], co, nor, hairdata->base.group->normals_max_length);
	}
	// clear remaining data, if any
	for (; i < hairdata->numstrands; ++i)
	{
		zero_v3(r_verts[i << 1]);
		zero_v3(r_verts[(i << 1) + 1]);
	}
	
	BKE_mesh_sample_free_generator(gen);
}

static void get_strand_lengths_strands(const HairDrawDataInterface* hairdata_, int *r_lengths)
{
	const HairGroupDrawDataInterface *hairdata = (HairGroupDrawDataInterface *)hairdata_;
	for (int i = 0; i < hairdata->numstrands; ++i)
	{
		// TODO
		r_lengths[i] = 0;
	}
}

static void get_strand_roots_strands(const HairDrawDataInterface* hairdata_, struct MeshSample *r_roots)
{
	const HairGroupDrawDataInterface *hairdata = (HairGroupDrawDataInterface *)hairdata_;
	for (int i = 0; i < hairdata->numstrands; ++i)
	{
		// TODO
		memset(&r_roots[i], 0, sizeof(MeshSample));
	}
}

static void get_strand_vertices_strands(const HairDrawDataInterface* hairdata_, float (*r_verts)[3])
{
	const HairGroupDrawDataInterface *hairdata = (HairGroupDrawDataInterface *)hairdata_;
	for (int i = 0; i < hairdata->numverts_orig; ++i)
	{
		// TODO
		zero_v3(r_verts[i]);
	}
}

static HairGroupDrawDataInterface hair_group_get_interface(HairGroup *group, DerivedMesh *scalp)
{
	HairGroupDrawDataInterface hairdata;
	hairdata.base.group = group;
	hairdata.base.scalp = scalp;
	hairdata.base.get_num_strands = get_num_strands;
	hairdata.base.get_num_verts = get_num_verts;
	
	switch (group->type) {
		case HAIR_GROUP_TYPE_NORMALS: {
			hairdata.numstrands = scalp->getNumVerts(scalp);
			hairdata.numverts_orig = 2 * hairdata.numstrands;
			hairdata.base.get_strand_lengths = get_strand_lengths_normals;
			hairdata.base.get_strand_roots = get_strand_roots_normals;
			hairdata.base.get_strand_vertices = get_strand_vertices_normals;
			break;
		}
		case HAIR_GROUP_TYPE_STRANDS: {
			// TODO
			hairdata.numstrands = 0;
			hairdata.numverts_orig = 0;
			hairdata.base.get_strand_lengths = get_strand_lengths_strands;
			hairdata.base.get_strand_roots = get_strand_roots_strands;
			hairdata.base.get_strand_vertices = get_strand_vertices_strands;
			break;
		}
	}
	
	return hairdata;
}

int* BKE_hair_group_get_fiber_lengths(HairGroup *group, DerivedMesh *scalp, int subdiv)
{
	HairGroupDrawDataInterface hairdata = hair_group_get_interface(group, scalp);
	return BKE_hair_strands_get_fiber_lengths(&hairdata.base, subdiv);
}

void BKE_hair_group_get_texture_buffer_size(HairGroup *group, DerivedMesh *scalp, int subdiv,
                                            int *r_size, int *r_strand_map_start,
                                            int *r_strand_vertex_start, int *r_fiber_start)
{
	HairGroupDrawDataInterface hairdata = hair_group_get_interface(group, scalp);
	BKE_hair_strands_get_texture_buffer_size(&hairdata.base, subdiv,
	                                         r_size, r_strand_map_start, r_strand_vertex_start, r_fiber_start);
}

void BKE_hair_group_get_texture_buffer(HairGroup *group, DerivedMesh *scalp, int subdiv, void *buffer)
{
	HairGroupDrawDataInterface hairdata = hair_group_get_interface(group, scalp);
	BKE_hair_strands_get_texture_buffer(&hairdata.base, subdiv, buffer);
}
