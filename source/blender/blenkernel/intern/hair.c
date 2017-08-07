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

/** \file blender/blenkernel/intern/strands.c
 *  \ingroup bke
 */

#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_kdtree.h"
#include "BLI_rand.h"

#include "DNA_hair_types.h"

#include "BKE_DerivedMesh.h"
#include "BKE_mesh_sample.h"
#include "BKE_hair.h"

#include "bmesh.h"

HairPattern* BKE_hair_new(void)
{
	HairPattern *hair = MEM_mallocN(sizeof(HairPattern), "hair");
	
	hair->follicles = NULL;
	hair->num_follicles = 0;
	
	return hair;
}

HairPattern* BKE_hair_copy(HairPattern *hair)
{
	HairPattern *newhair = MEM_dupallocN(hair);
	
	newhair->follicles = MEM_dupallocN(hair->follicles);
	
	return newhair;
}

void BKE_hair_free(struct HairPattern *hair)
{
	if (hair->follicles) {
		MEM_freeN(hair->follicles);
	}
	
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

/* ================================= */

bool BKE_hair_fiber_get_location(const HairFiber *fiber, DerivedMesh *root_dm, float loc[3])
{
	float nor[3], tang[3];
	if (BKE_mesh_sample_eval(root_dm, &fiber->root, loc, nor, tang)) {
		return true;
	}
	else {
		zero_v3(loc);
		return false;
	}
}

bool BKE_hair_fiber_get_vectors(const HairFiber *fiber, DerivedMesh *root_dm,
                                float loc[3], float nor[3], float tang[3])
{
	if (BKE_mesh_sample_eval(root_dm, &fiber->root, loc, nor, tang)) {
		return true;
	}
	else {
		zero_v3(loc);
		zero_v3(nor);
		zero_v3(tang);
		return false;
	}
}

bool BKE_hair_fiber_get_matrix(const HairFiber *fiber, DerivedMesh *root_dm, float mat[4][4])
{
	if (BKE_mesh_sample_eval(root_dm, &fiber->root, mat[3], mat[2], mat[0])) {
		cross_v3_v3v3(mat[1], mat[2], mat[0]);
		mat[0][3] = 0.0f;
		mat[1][3] = 0.0f;
		mat[2][3] = 0.0f;
		mat[3][3] = 1.0f;
		return true;
	}
	else {
		unit_m4(mat);
		return false;
	}
}

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

static void strands_calc_weights(const StrandsView *strands, struct DerivedMesh *scalp, HairFiber *fibers, int num_fibers)
{
	const int num_strands = strands->get_num_strands(strands);
	if (num_strands == 0)
		return;
	
	float (*strandloc)[3] = MEM_mallocN(sizeof(float) * 3 * num_strands, "strand locations");
	{
		MeshSample *roots = MEM_mallocN(sizeof(MeshSample) * num_strands, "strand roots");
		strands->get_strand_roots(strands, roots);
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

HairFiber* BKE_hair_fibers_create(const StrandsView *strands,
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
	
	strands_calc_weights(strands, scalp, fibers, amount);
	
	return fibers;
}

static int hair_get_strand_subdiv_numverts(int numstrands, int numverts, int subdiv)
{
	return ((numverts - numstrands) << subdiv) + numstrands;
}

static void hair_get_strand_subdiv_lengths(int *lengths, const int *orig_lengths, int num_strands, int subdiv)
{
	for (int i = 0; i < num_strands; ++i) {
		lengths[i] = ((orig_lengths[i] - 1) << subdiv) + 1;
	}
}

int* BKE_hair_get_fiber_lengths(const HairFiber *fibers, int totfibers, const StrandsView *strands, int subdiv)
{
	int *fiber_length = MEM_mallocN(sizeof(int) * totfibers, "fiber length");

	const int num_strands = strands->get_num_strands(strands);
	int *lengths = MEM_mallocN(sizeof(int) * num_strands, "strand length");
	strands->get_strand_lengths(strands, lengths);
	hair_get_strand_subdiv_lengths(lengths, lengths, num_strands, subdiv);
	
	for (int i = 0; i < totfibers; ++i) {
		
		// Calculate the length of the fiber from the weighted average of its control strands
		float fiblen = 0.0f;
		for (int k = 0; k < 4; ++k) {
			int si = fibers[i].parent_index[k];
			float sw = fibers[i].parent_weight[k];
			if (si == STRAND_INDEX_NONE || sw == 0.0f) {
				break;
			}
			BLI_assert(si < num_strands);
			
			fiblen += (float)lengths[si] * sw;
		}
		
		// use rounded number of segments
		fiber_length[i] = (int)(fiblen + 0.5f);
	}
	
	MEM_freeN(lengths);
	
	return fiber_length;
}

typedef struct HairFiberTextureBuffer {
	unsigned int parent_index[4];
	float parent_weight[4];
	float root_position[3];
	int pad;
} HairFiberTextureBuffer;
BLI_STATIC_ASSERT_ALIGN(HairFiberTextureBuffer, 8)

typedef struct HairStrandVertexTextureBuffer {
	float co[3];
	float nor[3];
	float tang[3];
	int pad;
} HairStrandVertexTextureBuffer;
BLI_STATIC_ASSERT_ALIGN(HairStrandVertexTextureBuffer, 8)

typedef struct HairStrandMapTextureBuffer {
	unsigned int vertex_start;
	unsigned int vertex_count;
} HairStrandMapTextureBuffer;
BLI_STATIC_ASSERT_ALIGN(HairStrandMapTextureBuffer, 8)

static void hair_strand_transport_frame(const float co1[3], const float co2[3],
                                        float prev_tang[3], float prev_nor[3],
                                        float r_tang[3], float r_nor[3])
{
	/* segment direction */
	sub_v3_v3v3(r_tang, co2, co1);
	normalize_v3(r_tang);
	
	/* rotate the frame */
	float rot[3][3];
	rotation_between_vecs_to_mat3(rot, prev_tang, r_tang);
	mul_v3_m3v3(r_nor, rot, prev_nor);
	
	copy_v3_v3(prev_tang, r_tang);
	copy_v3_v3(prev_nor, r_nor);
}

static void hair_strand_calc_vectors(const float (*positions)[3], int num_verts, float rootmat[3][3],
                                     HairStrandVertexTextureBuffer *strand)
{
	for (int i = 0; i < num_verts; ++i) {
		copy_v3_v3(strand[i].co, positions[i]);
	}
	
	// Calculate tangent and normal vectors
	{
		BLI_assert(num_verts >= 2);
		
		float prev_tang[3], prev_nor[3];
		
		copy_v3_v3(prev_tang, rootmat[2]);
		copy_v3_v3(prev_nor, rootmat[0]);
		
		hair_strand_transport_frame(strand[0].co, strand[1].co,
		        prev_tang, prev_nor,
		        strand[0].tang, strand[0].nor);
		
		for (int i = 1; i < num_verts - 1; ++i) {
			hair_strand_transport_frame(strand[i-1].co, strand[i+1].co,
			        prev_tang, prev_nor,
			        strand[i].tang, strand[i].nor);
		}
		
		hair_strand_transport_frame(strand[num_verts-2].co, strand[num_verts-1].co,
		        prev_tang, prev_nor,
		        strand[num_verts-1].tang, strand[num_verts-1].nor);
	}
}

static int hair_strand_subdivide(float (*verts)[3], const float (*verts_orig)[3], int numverts_orig, int subdiv)
{
	{
		/* Move vertex positions from the dense array to their initial configuration for subdivision. */
		const int step = (1 << subdiv);
		const float (*src)[3] = verts_orig;
		float (*dst)[3] = verts;
		for (int i = 0; i < numverts_orig; ++i) {
			copy_v3_v3(*dst, *src);
			
			++src;
			dst += step;
		}
	}
	
	/* Subdivide */
	for (int d = 0; d < subdiv; ++d) {
		const int num_edges = (numverts_orig - 1) << d;
		const int hstep = 1 << (subdiv - d - 1);
		const int step = 1 << (subdiv - d);
		
		/* Calculate edge points */
		{
			int index = 0;
			for (int k = 0; k < num_edges; ++k, index += step) {
				add_v3_v3v3(verts[index + hstep], verts[index], verts[index + step]);
				mul_v3_fl(verts[index + hstep], 0.5f);
			}
		}
		
		/* Move original points */
		{
			int index = step;
			for (int k = 1; k < num_edges; ++k, index += step) {
				add_v3_v3v3(verts[index], verts[index - hstep], verts[index + hstep]);
				mul_v3_fl(verts[index], 0.5f);
			}
		}
	}
	
	const int num_verts = ((numverts_orig - 1) << subdiv) + 1;
	return num_verts;
}

static void hair_get_fiber_buffer(const HairFiber *fibers, int totfibers, DerivedMesh *scalp,
                                  HairFiberTextureBuffer *fiber_buf)
{
	const HairFiber *fiber = fibers;
	HairFiberTextureBuffer *fb = fiber_buf;
	float nor[3], tang[3];
	for (int i = 0; i < totfibers; ++i, ++fiber, ++fb) {
		memcpy(fb->parent_index, fiber->parent_index, sizeof(fb->parent_index));
		memcpy(fb->parent_weight, fiber->parent_weight, sizeof(fb->parent_weight));
		
		BKE_mesh_sample_eval(scalp, &fiber->root, fb->root_position, nor, tang);
	}
}

void BKE_hair_get_texture_buffer_size(const StrandsView *strands, int totfibers, int subdiv,
                                      int *r_size, int *r_strand_map_start,
                                      int *r_strand_vertex_start, int *r_fiber_start)
{
	const int totstrands = strands->get_num_strands(strands);
	const int totverts = strands->get_num_verts(strands);
	const int totverts_subdiv = hair_get_strand_subdiv_numverts(totstrands, totverts, subdiv);
	*r_strand_map_start = 0;
	*r_strand_vertex_start = *r_strand_map_start + totstrands * sizeof(HairStrandMapTextureBuffer);
	*r_fiber_start = *r_strand_vertex_start + totverts_subdiv * sizeof(HairStrandVertexTextureBuffer);
	*r_size = *r_fiber_start + totfibers * sizeof(HairFiberTextureBuffer);
}

void BKE_hair_get_texture_buffer(const StrandsView *strands, DerivedMesh *scalp,
                                 const HairFiber *fibers, int totfibers, int subdiv,
                                 void *buffer)
{
	const int totstrands = strands->get_num_strands(strands);
	const int totverts_orig = strands->get_num_verts(strands);
	const int totverts = hair_get_strand_subdiv_numverts(totstrands, totverts_orig, subdiv);
	const int strand_map_start = 0;
	const int strand_vertex_start = strand_map_start + totstrands * sizeof(HairStrandMapTextureBuffer);
	const int fiber_start = strand_vertex_start + totverts * sizeof(HairStrandVertexTextureBuffer);
	
	int *lengths_orig = MEM_mallocN(sizeof(int) * totstrands, "strand lengths");
	float (*positions_orig)[3] = MEM_mallocN(sizeof(float[3]) * totverts_orig, "strand vertex positions");
	MeshSample *roots = MEM_mallocN(sizeof(MeshSample) * totstrands, "strand roots");
	strands->get_strand_lengths(strands, lengths_orig);
	strands->get_strand_vertices(strands, positions_orig);
	strands->get_strand_roots(strands, roots);

	int *lengths;
	float (*positions)[3];
	if (subdiv > 0) {
		lengths = MEM_mallocN(sizeof(int) * totstrands, "strand lengths subdivided");
		hair_get_strand_subdiv_lengths(lengths, lengths_orig, totstrands, subdiv);
		
		positions = MEM_mallocN(sizeof(float[3]) * totverts, "strand vertex positions subdivided");
	}
	else {
		lengths = lengths_orig;
		positions = positions_orig;
	}
	
	HairStrandMapTextureBuffer *smap = (HairStrandMapTextureBuffer*)((char*)buffer + strand_map_start);
	HairStrandVertexTextureBuffer *svert = (HairStrandVertexTextureBuffer*)((char*)buffer + strand_vertex_start);
	int vertex_orig_start = 0;
	int vertex_start = 0;
	for (int i = 0; i < totstrands; ++i) {
		const int len_orig = lengths_orig[i];
		const int len = lengths[i];
		smap->vertex_start = vertex_start;
		smap->vertex_count = len;
		
		if (subdiv > 0) {
			hair_strand_subdivide(positions + vertex_start, positions_orig + vertex_orig_start, len_orig, subdiv);
		}
		
		{
			float pos[3];
			float matrix[3][3];
			BKE_mesh_sample_eval(scalp, &roots[i], pos, matrix[2], matrix[0]);
			cross_v3_v3v3(matrix[1], matrix[2], matrix[0]);
			hair_strand_calc_vectors(positions + vertex_start, len, matrix, svert);
		}
		
		vertex_orig_start += len_orig;
		vertex_start += len;
		++smap;
		svert += len;
	}
	
	MEM_freeN(lengths_orig);
	MEM_freeN(positions_orig);
	MEM_freeN(roots);
	if (subdiv > 0) {
		MEM_freeN(lengths);
		MEM_freeN(positions);
	}
	
	hair_get_fiber_buffer(fibers, totfibers, scalp, (HairFiberTextureBuffer*)((char*)buffer + fiber_start));
}
