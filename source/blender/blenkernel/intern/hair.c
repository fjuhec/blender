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
#include "BKE_hair.h"
#include "BKE_mesh.h"
#include "BKE_mesh_sample.h"

#include "BLT_translation.h"

HairSystem* BKE_hair_new(void)
{
	HairSystem *hair = MEM_callocN(sizeof(HairSystem), "hair system");
	
	hair->pattern = MEM_callocN(sizeof(HairPattern), "hair pattern");
	
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
	
	if (hsys->curves)
	{
		nhsys->curves = MEM_dupallocN(hsys->curves);
	}
	if (hsys->verts)
	{
		nhsys->verts = MEM_dupallocN(hsys->verts);
	}
	
	nhsys->draw_batch_cache = NULL;
	nhsys->draw_texture_cache = NULL;
	
	return nhsys;
}

void BKE_hair_free(HairSystem *hsys)
{
	BKE_hair_batch_cache_free(hsys);
	
	if (hsys->curves)
	{
		MEM_freeN(hsys->curves);
	}
	if (hsys->verts)
	{
		MEM_freeN(hsys->verts);
	}
	
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

/* Calculate surface area of a scalp mesh */
float BKE_hair_calc_surface_area(struct DerivedMesh *scalp)
{
	BLI_assert(scalp != NULL);
	
	int numpolys = scalp->getNumPolys(scalp);
	MPoly *mpolys = scalp->getPolyArray(scalp);
	MLoop *mloops = scalp->getLoopArray(scalp);
	MVert *mverts = scalp->getVertArray(scalp);

	float area = 0.0f;
	for (int i = 0; i < numpolys; ++i)
	{
		area += BKE_mesh_calc_poly_area(&mpolys[i], mloops + mpolys[i].loopstart, mverts);
	}
	return area;
}

/* Calculate a density value based on surface area and sample count */
float BKE_hair_calc_density_from_count(float area, int count)
{
	return area > 0.0f ? count / area : 0.0f;
}

/* Calculate maximum sample count based on surface area and density */
int BKE_hair_calc_max_count_from_density(float area, float density)
{
	return (int)(density * area);
}

/* Calculate a density value based on a minimum distance */
float BKE_hair_calc_density_from_min_distance(float min_distance)
{
	// max. circle packing density (sans pi factor): 1 / (2 * sqrt(3))
	static const float max_factor = 0.288675135;
	
	return min_distance > 0.0f ? max_factor / (min_distance * min_distance) : 0.0f;
}

/* Calculate a minimum distance based on density */
float BKE_hair_calc_min_distance_from_density(float density)
{
	// max. circle packing density (sans pi factor): 1 / (2 * sqrt(3))
	static const float max_factor = 0.288675135;
	
	return density > 0.0f ? sqrt(max_factor / density) : 0.0f;
}

/* Distribute hair follicles on a scalp mesh */
void BKE_hair_generate_follicles(
        HairSystem* hsys,
        struct DerivedMesh *scalp,
        unsigned int seed,
        float min_distance,
        int max_count)
{
	HairPattern *pattern = hsys->pattern;
	
	// Limit max_count to theoretical limit based on area
	float scalp_area = BKE_hair_calc_surface_area(scalp);
	float density = BKE_hair_calc_density_from_min_distance(min_distance);
	max_count = min_ii(max_count, (int)(density * scalp_area));
	
	if (pattern->follicles)
	{
		MEM_freeN(pattern->follicles);
	}
	pattern->follicles = MEM_callocN(sizeof(HairFollicle) * max_count, "hair follicles");
	
	{
		MeshSampleGenerator *gen = BKE_mesh_sample_gen_surface_poissondisk(seed, min_distance, max_count, NULL, NULL);
		
		BKE_mesh_sample_generator_bind(gen, scalp);
		
		static const bool use_threads = false;
		pattern->num_follicles = BKE_mesh_sample_generate_batch_ex(
		            gen,
		            &pattern->follicles->mesh_sample,
		            sizeof(HairFollicle),
		            max_count,
		            use_threads);
		
		BKE_mesh_sample_free_generator(gen);
	}
	
	hsys->flag |= HAIR_SYSTEM_UPDATE_FOLLICLE_BINDING;
	BKE_hair_batch_cache_dirty(hsys, BKE_HAIR_BATCH_DIRTY_ALL);
}

/* ================================= */

void BKE_hair_guide_curves_begin(HairSystem *hsys, int totcurves, int totverts)
{
	if (totcurves != hsys->totcurves)
	{
		hsys->curves = MEM_reallocN(hsys->curves, sizeof(HairGuideCurve) * totcurves);
		hsys->totcurves = totcurves;

		hsys->flag |= HAIR_SYSTEM_UPDATE_GUIDE_VERT_OFFSET | HAIR_SYSTEM_UPDATE_FOLLICLE_BINDING;
		BKE_hair_batch_cache_dirty(hsys, BKE_HAIR_BATCH_DIRTY_ALL);
	}
	if (totverts != hsys->totverts)
	{
		hsys->verts = MEM_reallocN(hsys->verts, sizeof(HairGuideVertex) * totverts);
		hsys->totverts = totverts;

		BKE_hair_batch_cache_dirty(hsys, BKE_HAIR_BATCH_DIRTY_ALL);
	}
}

void BKE_hair_set_guide_curve(HairSystem *hsys, int index, const MeshSample *mesh_sample, int numverts)
{
	BLI_assert(index <= hsys->totcurves);
	
	HairGuideCurve *curve = &hsys->curves[index];
	memcpy(&curve->mesh_sample, mesh_sample, sizeof(MeshSample));
	curve->numverts = numverts;
	
	hsys->flag |= HAIR_SYSTEM_UPDATE_GUIDE_VERT_OFFSET | HAIR_SYSTEM_UPDATE_FOLLICLE_BINDING;
	BKE_hair_batch_cache_dirty(hsys, BKE_HAIR_BATCH_DIRTY_ALL);
}

void BKE_hair_set_guide_vertex(HairSystem *hsys, int index, int flag, const float co[3])
{
	BLI_assert(index <= hsys->totverts);
	
	HairGuideVertex *vertex = &hsys->verts[index];
	vertex->flag = flag;
	copy_v3_v3(vertex->co, co);
	
	BKE_hair_batch_cache_dirty(hsys, BKE_HAIR_BATCH_DIRTY_ALL);
}

void BKE_hair_guide_curves_end(HairSystem *hsys)
{
	/* Recalculate vertex offsets */
	if (!(hsys->flag & HAIR_SYSTEM_UPDATE_GUIDE_VERT_OFFSET))
	{
		return;
	}
	hsys->flag &= ~HAIR_SYSTEM_UPDATE_GUIDE_VERT_OFFSET;
	
	int vertstart = 0;
	for (int i = 0; i < hsys->totcurves; ++i)
	{
		hsys->curves[i].vertstart = vertstart;
		vertstart += hsys->curves[i].numverts;
	}
}

/* ================================= */

BLI_INLINE void hair_fiber_verify_weights(HairFollicle *follicle)
{
	const float *w = follicle->parent_weight;
	
	BLI_assert(w[0] >= 0.0f && w[1] >= 0.0f && w[2] >= 0.0f && w[3] >= 0.0f);
	float sum = w[0] + w[1] + w[2] + w[3];
	float epsilon = 1.0e-2;
	BLI_assert(sum > 1.0f - epsilon && sum < 1.0f + epsilon);
	UNUSED_VARS(sum, epsilon);
	
	BLI_assert(w[0] >= w[1] && w[1] >= w[2] && w[2] >= w[3]);
}

static void hair_fiber_sort_weights(HairFollicle *follicle)
{
	unsigned int *idx = follicle->parent_index;
	float *w = follicle->parent_weight;

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

static void hair_fiber_find_closest_strand(
        HairFollicle *follicle,
        const float loc[3],
        const KDTree *tree,
        const float (*strandloc)[3])
{
	/* Use the 3 closest strands for interpolation.
	 * Note that we have up to 4 possible weights, but we
	 * only look for a triangle with this method.
	 */
	KDTreeNearest nearest[3];
	const float *sloc[3] = {NULL};
	int k, found = BLI_kdtree_find_nearest_n(tree, loc, nearest, 3);
	for (k = 0; k < found; ++k) {
		follicle->parent_index[k] = (unsigned int)nearest[k].index;
		sloc[k] = strandloc[nearest[k].index];
	}
	for (; k < 4; ++k) {
		follicle->parent_index[k] = HAIR_STRAND_INDEX_NONE;
		follicle->parent_weight[k] = 0.0f;
	}
	
	/* calculate barycentric interpolation weights */
	if (found == 3) {
		float closest[3];
		closest_on_tri_to_point_v3(closest, loc, sloc[0], sloc[1], sloc[2]);
		
		float w[3];
		interp_weights_tri_v3(w, sloc[0], sloc[1], sloc[2], closest);
		copy_v3_v3(follicle->parent_weight, w);
		/* float precisions issues can cause slightly negative weights */
		CLAMP3(follicle->parent_weight, 0.0f, 1.0f);
	}
	else if (found == 2) {
		follicle->parent_weight[1] = line_point_factor_v3(loc, sloc[0], sloc[1]);
		follicle->parent_weight[0] = 1.0f - follicle->parent_weight[1];
		/* float precisions issues can cause slightly negative weights */
		CLAMP2(follicle->parent_weight, 0.0f, 1.0f);
	}
	else if (found == 1) {
		follicle->parent_weight[0] = 1.0f;
	}
	
	hair_fiber_sort_weights(follicle);
}

void BKE_hair_bind_follicles(HairSystem *hsys, DerivedMesh *scalp)
{
	if (!(hsys->flag & HAIR_SYSTEM_UPDATE_FOLLICLE_BINDING))
	{
		return;
	}
	hsys->flag &= ~HAIR_SYSTEM_UPDATE_FOLLICLE_BINDING;
	
	HairPattern *pattern = hsys->pattern;
	const int num_strands = hsys->totcurves;
	if (num_strands == 0 || !pattern)
		return;
	
	float (*strandloc)[3] = MEM_mallocN(sizeof(float) * 3 * num_strands, "strand locations");
	{
		for (int i = 0; i < num_strands; ++i) {
			float nor[3], tang[3];
			if (!BKE_mesh_sample_eval(scalp, &hsys->curves[i].mesh_sample, strandloc[i], nor, tang)) {
				zero_v3(strandloc[i]);
			}
		}
	}
	
	KDTree *tree = BLI_kdtree_new(num_strands);
	for (int c = 0; c < num_strands; ++c) {
		BLI_kdtree_insert(tree, c, strandloc[c]);
	}
	BLI_kdtree_balance(tree);
	
	HairFollicle *follicle = pattern->follicles;
	for (int i = 0; i < pattern->num_follicles; ++i, ++follicle) {
		float loc[3], nor[3], tang[3];
		if (BKE_mesh_sample_eval(scalp, &follicle->mesh_sample, loc, nor, tang)) {
			hair_fiber_find_closest_strand(follicle, loc, tree, strandloc);
			hair_fiber_verify_weights(follicle);
		}
	}
	
	BLI_kdtree_free(tree);
	MEM_freeN(strandloc);
}

