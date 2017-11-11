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
#include "DNA_object_types.h"

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

#if 0
BLI_INLINE void verify_fiber_weights(HairFiber *fiber)
{
	const float *w = fiber->parent_weight;
	
	BLI_assert(w[0] >= 0.0f && w[1] >= 0.0f && w[2] >= 0.0f && w[3] >= 0.0f);
	float sum = w[0] + w[1] + w[2] + w[3];
	float epsilon = 1.0e-2;
	BLI_assert(sum > 1.0f - epsilon && sum < 1.0f + epsilon);
	UNUSED_VARS(sum, epsilon);
	
	BLI_assert(w[0] >= w[1] && w[1] >= w[2] && w[2] >= w[3]);
}

static void sort_fiber_weights(HairFiber *fiber)
{
	unsigned int *idx = fiber->parent_index;
	float *w = fiber->parent_weight;

#define FIBERSWAP(a, b) \
	SWAP(unsigned int, idx[a], idx[b]); \
	SWAP(float, w[a], w[b]);

	for (int k = 0; k < 3; ++k) {
		int maxi = k;
		float maxw = w[k];
		for (int i = k+1; i < 4; ++i) {
			if (w[i] > maxw) {
				maxi = i;
				maxw = w[i];
			}
		}
		if (maxi != k)
			FIBERSWAP(k, maxi);
	}
	
#undef FIBERSWAP
}

static void strand_find_closest(HairFiber *fiber, const float loc[3],
                                const KDTree *tree, const float (*strandloc)[3])
{
	/* Use the 3 closest strands for interpolation.
	 * Note that we have up to 4 possible weights, but we
	 * only look for a triangle with this method.
	 */
	KDTreeNearest nearest[3];
	const float *sloc[3] = {NULL};
	int k, found = BLI_kdtree_find_nearest_n(tree, loc, nearest, 3);
	for (k = 0; k < found; ++k) {
		fiber->parent_index[k] = nearest[k].index;
		sloc[k] = strandloc[nearest[k].index];
	}
	for (; k < 4; ++k) {
		fiber->parent_index[k] = STRAND_INDEX_NONE;
		fiber->parent_weight[k] = 0.0f;
	}
	
	/* calculate barycentric interpolation weights */
	if (found == 3) {
		float closest[3];
		closest_on_tri_to_point_v3(closest, loc, sloc[0], sloc[1], sloc[2]);
		
		float w[3];
		interp_weights_tri_v3(w, sloc[0], sloc[1], sloc[2], closest);
		copy_v3_v3(fiber->parent_weight, w);
		/* float precisions issues can cause slightly negative weights */
		CLAMP3(fiber->parent_weight, 0.0f, 1.0f);
	}
	else if (found == 2) {
		fiber->parent_weight[1] = line_point_factor_v3(loc, sloc[0], sloc[1]);
		fiber->parent_weight[0] = 1.0f - fiber->parent_weight[1];
		/* float precisions issues can cause slightly negative weights */
		CLAMP2(fiber->parent_weight, 0.0f, 1.0f);
	}
	else if (found == 1) {
		fiber->parent_weight[0] = 1.0f;
	}
	
	sort_fiber_weights(fiber);
}

static void strand_calc_root_distance(HairFiber *fiber, const float loc[3], const float nor[3], const float tang[3],
                                      const float (*strandloc)[3])
{
	if (fiber->parent_index[0] == STRAND_INDEX_NONE)
		return;
	
	float cotang[3];
	cross_v3_v3v3(cotang, nor, tang);
	
	const float *sloc0 = strandloc[fiber->parent_index[0]];
	float dist[3];
	sub_v3_v3v3(dist, loc, sloc0);
	fiber->root_distance[0] = dot_v3v3(dist, tang);
	fiber->root_distance[1] = dot_v3v3(dist, cotang);
}

static void strands_calc_weights(const HairDrawDataInterface *hairdata, struct DerivedMesh *scalp, HairFiber *fibers, int num_fibers)
{
	const int num_strands = hairdata->get_num_strands(hairdata);
	if (num_strands == 0)
		return;
	
	float (*strandloc)[3] = MEM_mallocN(sizeof(float) * 3 * num_strands, "strand locations");
	{
		MeshSample *roots = MEM_mallocN(sizeof(MeshSample) * num_strands, "strand roots");
		hairdata->get_strand_roots(hairdata, roots);
		for (int i = 0; i < num_strands; ++i) {
			float nor[3], tang[3];
			if (!BKE_mesh_sample_eval(scalp, &roots[i], strandloc[i], nor, tang)) {
				zero_v3(strandloc[i]);
			}
		}
		MEM_freeN(roots);
	}
	
	KDTree *tree = BLI_kdtree_new(num_strands);
	for (int c = 0; c < num_strands; ++c) {
		BLI_kdtree_insert(tree, c, strandloc[c]);
	}
	BLI_kdtree_balance(tree);
	
	HairFiber *fiber = fibers;
	for (int i = 0; i < num_fibers; ++i, ++fiber) {
		float loc[3], nor[3], tang[3];
		if (BKE_mesh_sample_eval(scalp, &fiber->root, loc, nor, tang)) {
			
			strand_find_closest(fiber, loc, tree, strandloc);
			verify_fiber_weights(fiber);
			
			strand_calc_root_distance(fiber, loc, nor, tang, strandloc);
		}
	}
	
	BLI_kdtree_free(tree);
	MEM_freeN(strandloc);
}

HairFiber* BKE_hair_fibers_create(const HairDrawDataInterface *hairdata,
                                  struct DerivedMesh *scalp, unsigned int amount,
                                  unsigned int seed)
{
	MeshSampleGenerator *gen = BKE_mesh_sample_gen_surface_random(scalp, seed);
	unsigned int i;
	
	HairFiber *fibers = MEM_mallocN(sizeof(HairFiber) * amount, "HairFiber");
	HairFiber *fiber;
	
	for (i = 0, fiber = fibers; i < amount; ++i, ++fiber) {
		if (BKE_mesh_sample_generate(gen, &fiber->root)) {
			int k;
			/* influencing control strands are determined later */
			for (k = 0; k < 4; ++k) {
				fiber->parent_index[k] = STRAND_INDEX_NONE;
				fiber->parent_weight[k] = 0.0f;
			}
		}
		else {
			/* clear remaining samples */
			memset(fiber, 0, sizeof(HairFiber) * (amount - i));
			break;
		}
	}
	
	BKE_mesh_sample_free_generator(gen);
	
	strands_calc_weights(hairdata, scalp, fibers, amount);
	
	return fibers;
}
#endif

/* ================================= */

void BKE_hair_guide_curves_begin(HairSystem *hsys, int totcurves, int totverts)
{
	if (totcurves != hsys->totcurves)
	{
		hsys->curves = MEM_reallocN(hsys->curves, sizeof(HairGuideCurve) * totcurves);
		hsys->flag |= HAIR_GUIDE_CURVES_DIRTY;
	}
	if (totverts != hsys->totverts)
	{
		hsys->verts = MEM_reallocN(hsys->curves, sizeof(HairGuideCurve) * totverts);
		hsys->flag |= HAIR_GUIDE_VERTS_DIRTY;
	}
}

void BKE_hair_set_guide_curve(HairSystem *hsys, int index, const MeshSample *mesh_sample, int numverts)
{
	BLI_assert(index <= hsys->totcurves);
	
	HairGuideCurve *curve = &hsys->curves[index];
	memcpy(&curve->mesh_sample, mesh_sample, sizeof(MeshSample));
	curve->numverts = numverts;
	
	hsys->flag |= HAIR_GUIDE_CURVES_DIRTY;
}

void BKE_hair_set_guide_vertex(HairSystem *hsys, int index, int flag, const float co[3])
{
	BLI_assert(index <= hsys->totverts);
	
	HairGuideVertex *vertex = &hsys->verts[index];
	vertex->flag = flag;
	copy_v3_v3(vertex->co, co);
	
	hsys->flag |= HAIR_GUIDE_VERTS_DIRTY;
}

void BKE_hair_guide_curves_end(HairSystem *hsys)
{
	/* Recalculate vertex offsets */
	if (hsys->flag & HAIR_GUIDE_CURVES_DIRTY)
	{
		int vertstart = 0;
		for (int i = 0; i < hsys->totcurves; ++i)
		{
			hsys->curves[i].vertstart = vertstart;
			vertstart += hsys->curves[i].numverts;
		}
	}
}

DerivedMesh* BKE_hair_get_scalp(const HairSystem *hsys, struct Scene *scene, const struct EvaluationContext *eval_ctx)
{
	Object *ob = hsys->guide_object;
	
	if (ob)
	{
		if (eval_ctx)
		{
			CustomDataMask datamask = CD_MASK_BAREMESH;
			return mesh_get_derived_final(eval_ctx, scene, ob, datamask);
		}
		else
		{
			return ob->derivedFinal;
		}
	}
	
	return NULL;
}
