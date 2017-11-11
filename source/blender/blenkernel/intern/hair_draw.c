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

/** \file blender/blenkernel/intern/hair_draw.c
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

static int hair_get_strand_subdiv_numverts(int numstrands, int numverts, int subdiv)
{
	return ((numverts - numstrands) << subdiv) + numstrands;
}

BLI_INLINE int hair_get_strand_subdiv_length(int orig_length, int subdiv)
{
	return ((orig_length - 1) << subdiv) + 1;
}

static void hair_get_strand_subdiv_lengths(int *lengths, const int *orig_lengths, int num_strands, int subdiv)
{
	for (int i = 0; i < num_strands; ++i) {
		lengths[i] = hair_get_strand_subdiv_length(orig_lengths[i], subdiv);
	}
}

int* BKE_hair_get_fiber_lengths(const HairSystem *hsys, int subdiv)
{
	if (!hsys->pattern) {
		return NULL;
	}
	
	const int totfibers = hsys->pattern->num_follicles;
	int *fiber_length = MEM_mallocN(sizeof(int) * totfibers, "fiber length");
	
	const int num_strands = BKE_hair_get_num_strands(hsys);
	int *lengths = MEM_mallocN(sizeof(int) * num_strands, "strand length");
	BKE_hair_get_strand_lengths(hsys, lengths);
	hair_get_strand_subdiv_lengths(lengths, lengths, num_strands, subdiv);
	
	// Calculate the length of the fiber from the weighted average of its guide strands
	unsigned int (*parent_indices)[4] = MEM_mallocN(sizeof(unsigned int) * 4 * totfibers, "parent index");
	float (*parent_weights)[4] = MEM_mallocN(sizeof(float) * 4 * totfibers, "parent weight");
	BKE_hair_get_follicle_weights(hsys, parent_indices, parent_weights);
	for (int i = 0; i < totfibers; ++i) {
		float fiblen = 0.0f;
		const unsigned int *parent_index = parent_indices[i];
		const float *parent_weight = parent_weights[i];
		
		for (int k = 0; k < 4; ++k) {
			int si = parent_index[k];
			float sw = parent_weight[k];
			if (si == HAIR_STRAND_INDEX_NONE || sw == 0.0f) {
				break;
			}
			BLI_assert(si < num_strands);
			
			fiblen += (float)lengths[si] * sw;
		}
		
		// use rounded number of segments
		fiber_length[i] = (int)(fiblen + 0.5f);
	}
	MEM_freeN(parent_indices);
	MEM_freeN(parent_weights);
	
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

static void hair_get_texture_buffer_size(int numstrands, int numverts_orig, int subdiv, int numfibers,
                                         int *r_size, int *r_strand_map_start,
                                         int *r_strand_vertex_start, int *r_fiber_start)
{
	const int numverts = hair_get_strand_subdiv_numverts(numstrands, numverts_orig, subdiv);
	*r_strand_map_start = 0;
	*r_strand_vertex_start = *r_strand_map_start + numstrands * sizeof(HairStrandMapTextureBuffer);
	*r_fiber_start = *r_strand_vertex_start + numverts * sizeof(HairStrandVertexTextureBuffer);
	*r_size = *r_fiber_start + numfibers * sizeof(HairFiberTextureBuffer);
}

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

static void hair_get_strand_buffer(DerivedMesh *scalp, int numstrands, int numverts_orig, int subdiv,
                                   const int *lengths_orig, const float (*vertco_orig)[3], const MeshSample *roots,
                                   HairStrandMapTextureBuffer *strand_map_buffer,
                                   HairStrandVertexTextureBuffer *strand_vertex_buffer)
{
	const int numverts = hair_get_strand_subdiv_numverts(numstrands, numverts_orig, subdiv);
	
	const int *lengths;
	const float (*vertco)[3];
	int *lengths_subdiv = NULL;
	float (*vertco_subdiv)[3] = NULL;
	if (subdiv > 0) {
		lengths = lengths_subdiv = MEM_mallocN(sizeof(int) * numstrands, "strand lengths subdivided");
		hair_get_strand_subdiv_lengths(lengths_subdiv, lengths_orig, numstrands, subdiv);
		
		vertco = vertco_subdiv = MEM_mallocN(sizeof(float[3]) * numverts, "strand vertex positions subdivided");
	}
	else {
		lengths = lengths_orig;
		vertco = vertco_orig;
	}
	
	HairStrandMapTextureBuffer *smap = strand_map_buffer;
	HairStrandVertexTextureBuffer *svert = strand_vertex_buffer;
	int vertex_orig_start = 0;
	int vertex_start = 0;
	for (int i = 0; i < numstrands; ++i) {
		const int len_orig = lengths_orig[i];
		const int len = lengths[i];
		smap->vertex_start = vertex_start;
		smap->vertex_count = len;
		
		if (subdiv > 0) {
			hair_strand_subdivide(vertco_subdiv + vertex_start, vertco_orig + vertex_orig_start, len_orig, subdiv);
		}
		
		{
			float pos[3];
			float matrix[3][3];
			BKE_mesh_sample_eval(scalp, &roots[i], pos, matrix[2], matrix[0]);
			cross_v3_v3v3(matrix[1], matrix[2], matrix[0]);
			hair_strand_calc_vectors(vertco + vertex_start, len, matrix, svert);
		}
		
		vertex_orig_start += len_orig;
		vertex_start += len;
		++smap;
		svert += len;
	}
	
	if (subdiv > 0) {
		MEM_freeN(lengths_subdiv);
		MEM_freeN(vertco_subdiv);
	}
}

static void hair_get_fiber_buffer(const HairSystem* hsys, DerivedMesh *scalp,
                                  HairFiberTextureBuffer *fiber_buf)
{
	const int totfibers = hsys->pattern->num_follicles;
	HairFiberTextureBuffer *fb = fiber_buf;
	
	BKE_hair_get_follicle_weights(hsys, &fb->parent_index, &fb->parent_weight);
	
	HairFollicle *foll = hsys->pattern->follicles;
	float nor[3], tang[3];
	for (int i = 0; i < totfibers; ++i, ++fb, ++foll) {
		BKE_mesh_sample_eval(scalp, &foll->mesh_sample, fb->root_position, nor, tang);
	}
}

void BKE_hair_get_texture_buffer_size(
        const HairSystem *hsys,
        int subdiv,
        int *r_size,
        int *r_strand_map_start,
        int *r_strand_vertex_start,
        int *r_fiber_start)
{
	const int totstrands = BKE_hair_get_num_strands(hsys);
	const int totverts_orig = BKE_hair_get_num_strands_verts(hsys);
	hair_get_texture_buffer_size(totstrands, totverts_orig, subdiv, hsys->pattern->num_follicles,
	                             r_size, r_strand_map_start, r_strand_vertex_start, r_fiber_start);
}

void BKE_hair_get_texture_buffer(
        const HairSystem *hsys,
        struct Scene *scene,
        struct EvaluationContext *eval_ctx,
        int subdiv,
        void *buffer)
{
	HairPattern* pattern = hsys->pattern;
	DerivedMesh *scalp = BKE_hair_get_scalp(hsys, scene, eval_ctx);
	const int totstrands = BKE_hair_get_num_strands(hsys);
	const int totverts_orig = BKE_hair_get_num_strands_verts(hsys);
	int size, strand_map_start, strand_vertex_start, fiber_start;
	hair_get_texture_buffer_size(totstrands, totverts_orig, subdiv, pattern->num_follicles,
	                             &size, &strand_map_start, &strand_vertex_start, &fiber_start);
	
	if (scalp)
	{
		HairStrandMapTextureBuffer *strand_map = (HairStrandMapTextureBuffer*)((char*)buffer + strand_map_start);
		HairStrandVertexTextureBuffer *strand_verts = (HairStrandVertexTextureBuffer*)((char*)buffer + strand_vertex_start);
		HairFiberTextureBuffer *fibers = (HairFiberTextureBuffer*)((char*)buffer + fiber_start);

		int *lengths_orig = MEM_mallocN(sizeof(int) * totstrands, "strand lengths");
		float (*vertco_orig)[3] = MEM_mallocN(sizeof(float[3]) * totverts_orig, "strand vertex positions");
		MeshSample *roots = MEM_mallocN(sizeof(MeshSample) * totstrands, "strand roots");
		BKE_hair_get_strand_lengths(hsys, lengths_orig);
		BKE_hair_get_strand_vertices(hsys, vertco_orig);
		BKE_hair_get_strand_roots(hsys, roots);
		
		hair_get_strand_buffer(scalp, totstrands, totverts_orig, subdiv,
		                       lengths_orig, vertco_orig, roots,
		                       strand_map, strand_verts);
		hair_get_fiber_buffer(hsys, scalp, fibers);
		
		MEM_freeN(lengths_orig);
		MEM_freeN(vertco_orig);
		MEM_freeN(roots);
	}
	else
	{
		memset(buffer, 0, size);
	}
}

void (*BKE_hair_batch_cache_dirty_cb)(HairSystem* hsys, int mode) = NULL;
void (*BKE_hair_batch_cache_free_cb)(HairSystem* hsys) = NULL;

void BKE_hair_batch_cache_dirty(HairSystem* hsys, int mode)
{
	if (hsys->draw_batch_cache) {
		BKE_hair_batch_cache_dirty_cb(hsys, mode);
	}
}

void BKE_hair_batch_cache_free(HairSystem* hsys)
{
	if (hsys->draw_batch_cache || hsys->draw_texture_cache) {
		BKE_hair_batch_cache_free_cb(hsys);
	}
}
