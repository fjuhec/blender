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
 * The Original Code is Copyright (C) 2017 by Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Blender Foundation, Mike Erwin, Dalai Felinto
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file draw_cache_impl_strands.c
 *  \ingroup draw
 *
 * \brief Strands API for render engines
 */

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"
#include "BLI_math_vector.h"
#include "BLI_ghash.h"

#include "DNA_hair_types.h"
#include "DNA_scene_types.h"

#include "BKE_hair.h"
#include "BKE_mesh_sample.h"

#include "DEG_depsgraph.h"

#include "GPU_batch.h"
#include "GPU_extensions.h"
#include "GPU_texture.h"

#include "draw_common.h"
#include "draw_cache_impl.h"  /* own include */
#include "DRW_render.h"

// timing
//#define DEBUG_TIME
#ifdef DEBUG_TIME
#  include "PIL_time_utildefines.h"
#else
#  define TIMEIT_START(var)
#  define TIMEIT_VALUE(var)
#  define TIMEIT_VALUE_PRINT(var)
#  define TIMEIT_END(var)
#  define TIMEIT_BENCH(expr, id) (expr)
#  define TIMEIT_BLOCK_INIT(var)
#  define TIMEIT_BLOCK_START(var)
#  define TIMEIT_BLOCK_END(var)
#  define TIMEIT_BLOCK_STATS(var)
#endif

/* ---------------------------------------------------------------------- */
/* Hair Gwn_Batch Cache */

typedef struct HairBatchCache {
	Gwn_VertBuf *fiber_verts;
	Gwn_IndexBuf *fiber_edges;
	Gwn_Batch *fibers;
	DRWHairFiberTextureBuffer texbuffer;
	
	Gwn_VertBuf *follicle_verts;
	Gwn_IndexBuf *follicle_edges;
	Gwn_Batch *follicles;
	
	Gwn_VertBuf *guide_curve_verts;
	Gwn_IndexBuf *guide_curve_edges;
	Gwn_Batch *guide_curves;
	
	/* settings to determine if cache is invalid */
	bool is_dirty;
} HairBatchCache;

/* Gwn_Batch cache management. */

static void hair_batch_cache_clear(HairSystem *hsys);

static bool hair_batch_cache_valid(HairSystem *hsys)
{
	HairBatchCache *cache = hsys->draw_batch_cache;

	if (cache == NULL) {
		return false;
	}

	if (cache->is_dirty) {
		return false;
	}

	return true;
}

static void hair_batch_cache_init(HairSystem *hsys)
{
	HairBatchCache *cache = hsys->draw_batch_cache;
	
	if (!cache) {
		cache = hsys->draw_batch_cache = MEM_callocN(sizeof(*cache), __func__);
	}
	else {
		memset(cache, 0, sizeof(*cache));
	}
	
	cache->is_dirty = false;
}

static HairBatchCache *hair_batch_cache_get(HairSystem *hsys)
{
	if (!hair_batch_cache_valid(hsys)) {
		hair_batch_cache_clear(hsys);
		hair_batch_cache_init(hsys);
	}
	return hsys->draw_batch_cache;
}

void DRW_hair_batch_cache_dirty(HairSystem *hsys, int mode)
{
	HairBatchCache *cache = hsys->draw_batch_cache;
	if (cache == NULL) {
		return;
	}
	switch (mode) {
		case BKE_HAIR_BATCH_DIRTY_ALL:
			cache->is_dirty = true;
			break;
		default:
			BLI_assert(0);
	}
}

static void hair_batch_cache_clear(HairSystem *hsys)
{
	HairBatchCache *cache = hsys->draw_batch_cache;
	
	if (hsys->draw_texture_cache) {
		GPU_texture_free(hsys->draw_texture_cache);
		hsys->draw_texture_cache = NULL;
	}
	
	if (cache) {
		GWN_BATCH_DISCARD_SAFE(cache->fibers);
		GWN_VERTBUF_DISCARD_SAFE(cache->fiber_verts);
		GWN_INDEXBUF_DISCARD_SAFE(cache->fiber_edges);
		
		GWN_BATCH_DISCARD_SAFE(cache->follicles);
		GWN_VERTBUF_DISCARD_SAFE(cache->follicle_verts);
		GWN_INDEXBUF_DISCARD_SAFE(cache->follicle_edges);
		
		GWN_BATCH_DISCARD_SAFE(cache->guide_curves);
		GWN_VERTBUF_DISCARD_SAFE(cache->guide_curve_verts);
		GWN_INDEXBUF_DISCARD_SAFE(cache->guide_curve_edges);
		
		{
			DRWHairFiberTextureBuffer *buffer = &cache->texbuffer;
			if (buffer->data) {
				MEM_freeN(buffer->data);
				buffer->data = NULL;
			}
			buffer->fiber_start = 0;
			buffer->strand_map_start = 0;
			buffer->strand_vertex_start = 0;
			buffer->width = 0;
			buffer->height = 0;
		}
	}
}

void DRW_hair_batch_cache_free(HairSystem *hsys)
{
	hair_batch_cache_clear(hsys);
	MEM_SAFE_FREE(hsys->draw_batch_cache);
}

static void hair_batch_cache_ensure_fibers(HairSystem *hsys, int subdiv, HairBatchCache *cache)
{
	TIMEIT_START(hair_batch_cache_ensure_fibers);

	GWN_VERTBUF_DISCARD_SAFE(cache->fiber_verts);
	GWN_INDEXBUF_DISCARD_SAFE(cache->fiber_edges);
	
	const int totfibers = hsys->pattern ? hsys->pattern->num_follicles : 0;
	int *fiber_lengths = BKE_hair_get_fiber_lengths(hsys, subdiv);
	int totpoint = 0;
	for (int i = 0; i < totfibers; ++i) {
		totpoint += fiber_lengths[i];
	}
	const int totseg = totpoint - totfibers;
	
	static Gwn_VertFormat format = { 0 };
	static unsigned curve_param_id, fiber_index_id;
	
	/* initialize vertex format */
	if (format.attrib_ct == 0) {
		fiber_index_id = GWN_vertformat_attr_add(&format, "fiber_index", GWN_COMP_I32, 1, GWN_FETCH_INT);
		curve_param_id = GWN_vertformat_attr_add(&format, "curve_param", GWN_COMP_F32, 1, GWN_FETCH_FLOAT);
	}
	
	cache->fiber_verts = GWN_vertbuf_create_with_format(&format);

	Gwn_IndexBufBuilder elb;
	{
		TIMEIT_START(data_alloc);
		Gwn_PrimType prim_type;
		unsigned prim_ct, vert_ct;
		prim_type = GWN_PRIM_TRIS;
		prim_ct = 2 * totseg;
		vert_ct = 2 * totpoint;
		
		GWN_vertbuf_data_alloc(cache->fiber_verts, vert_ct);
		GWN_indexbuf_init(&elb, prim_type, prim_ct, vert_ct);
		TIMEIT_END(data_alloc);
	}
	
	TIMEIT_START(data_fill);
	TIMEIT_BLOCK_INIT(GWN_vertbuf_attr_set);
	TIMEIT_BLOCK_INIT(GWN_indexbuf_add_tri_verts);
	int vi = 0;
	for (int i = 0; i < totfibers; ++i) {
		const int fiblen = fiber_lengths[i];
		const float da = fiblen > 1 ? 1.0f / (fiblen-1) : 0.0f;
		
		float a = 0.0f;
		for (int k = 0; k < fiblen; ++k) {
			TIMEIT_BLOCK_START(GWN_vertbuf_attr_set);
			GWN_vertbuf_attr_set(cache->fiber_verts, fiber_index_id, vi, &i);
			GWN_vertbuf_attr_set(cache->fiber_verts, curve_param_id, vi, &a);
			GWN_vertbuf_attr_set(cache->fiber_verts, fiber_index_id, vi+1, &i);
			GWN_vertbuf_attr_set(cache->fiber_verts, curve_param_id, vi+1, &a);
			TIMEIT_BLOCK_END(GWN_vertbuf_attr_set);
			
			if (k > 0) {
				TIMEIT_BLOCK_START(GWN_indexbuf_add_tri_verts);
				GWN_indexbuf_add_tri_verts(&elb, vi-2, vi-1, vi+1);
				GWN_indexbuf_add_tri_verts(&elb, vi+1, vi, vi-2);
				TIMEIT_BLOCK_END(GWN_indexbuf_add_tri_verts);
			}
			
			vi += 2;
			a += da;
		}
	}
	TIMEIT_BLOCK_STATS(GWN_vertbuf_attr_set);
	TIMEIT_BLOCK_STATS(GWN_indexbuf_add_tri_verts);
#ifdef DEBUG_TIME
	printf("Total GWN time: %f\n", _timeit_var_GWN_vertbuf_attr_set + _timeit_var_GWN_indexbuf_add_tri_verts);
#endif
	fflush(stdout);
	TIMEIT_END(data_fill);
	
	if (fiber_lengths)
	{
		MEM_freeN(fiber_lengths);
	}
	
	TIMEIT_BENCH(cache->fiber_edges = GWN_indexbuf_build(&elb), indexbuf_build);

	TIMEIT_END(hair_batch_cache_ensure_fibers);
}

static void hair_batch_cache_ensure_fiber_texbuffer(HairSystem *hsys, struct DerivedMesh *scalp, int subdiv, HairBatchCache *cache)
{
	DRWHairFiberTextureBuffer *buffer = &cache->texbuffer;
	static const int elemsize = 8;
	const int width = GPU_max_texture_size();
	const int align = width * elemsize;
	
	// Offsets in bytes
	int b_size, b_strand_map_start, b_strand_vertex_start, b_fiber_start;
	BKE_hair_get_texture_buffer_size(hsys, subdiv, &b_size, 
	        &b_strand_map_start, &b_strand_vertex_start, &b_fiber_start);
	// Pad for alignment
	b_size += align - b_size % align;
	
	// Convert to element size as texture offsets
	const int size = b_size / elemsize;
	const int height = size / width;
	
	buffer->data = MEM_mallocN(b_size, "hair fiber texture buffer");
	BKE_hair_get_texture_buffer(hsys, scalp, subdiv, buffer->data);
	
	buffer->width = width;
	buffer->height = height;
	buffer->strand_map_start = b_strand_map_start / elemsize;
	buffer->strand_vertex_start = b_strand_vertex_start / elemsize;
	buffer->fiber_start = b_fiber_start / elemsize;
}

Gwn_Batch *DRW_hair_batch_cache_get_fibers(
        HairSystem *hsys,
        struct DerivedMesh *scalp,
        int subdiv,
        const DRWHairFiberTextureBuffer **r_buffer)
{
	HairBatchCache *cache = hair_batch_cache_get(hsys);

	TIMEIT_START(DRW_hair_batch_cache_get_fibers);

	if (cache->fibers == NULL) {
		TIMEIT_BENCH(hair_batch_cache_ensure_fibers(hsys, subdiv, cache),
		             hair_batch_cache_ensure_fibers);
		
		TIMEIT_BENCH(cache->fibers = GWN_batch_create(GWN_PRIM_TRIS, cache->fiber_verts, cache->fiber_edges),
		             GWN_batch_create);

		TIMEIT_BENCH(hair_batch_cache_ensure_fiber_texbuffer(hsys, scalp, subdiv, cache),
		             hair_batch_cache_ensure_fiber_texbuffer);
	}

	if (r_buffer) {
		*r_buffer = &cache->texbuffer;
	}

	TIMEIT_END(DRW_hair_batch_cache_get_fibers);

	return cache->fibers;
}

static void hair_batch_cache_ensure_follicles(
        HairSystem *hsys,
        struct DerivedMesh *scalp,
        eHairDrawFollicleMode mode,
        HairBatchCache *cache)
{
	GWN_VERTBUF_DISCARD_SAFE(cache->follicle_verts);
	GWN_INDEXBUF_DISCARD_SAFE(cache->follicle_edges);
	
	const HairPattern *pattern = hsys->pattern;
	
	static Gwn_VertFormat format = { 0 };
	static unsigned pos_id;
	
	/* initialize vertex format */
	if (format.attrib_ct == 0) {
		pos_id = GWN_vertformat_attr_add(&format, "pos", GWN_COMP_F32, 3, GWN_FETCH_FLOAT);
	}
	
	cache->follicle_verts = GWN_vertbuf_create_with_format(&format);
	
	GWN_vertbuf_data_alloc(cache->follicle_verts, pattern->num_follicles);
	
	HairFollicle *follicle = pattern->follicles;
	for (int i = 0; i < pattern->num_follicles; ++i, ++follicle) {
		float co[3], nor[3], tang[3];
		BKE_mesh_sample_eval(scalp, &follicle->mesh_sample, co, nor, tang);
		
		GWN_vertbuf_attr_set(cache->follicle_verts, pos_id, (unsigned int)i, co);
	}
}

Gwn_Batch *DRW_hair_batch_cache_get_follicle_points(
        HairSystem *hsys,
        struct DerivedMesh *scalp)
{
	HairBatchCache *cache = hair_batch_cache_get(hsys);

	if (cache->follicles == NULL) {
		hair_batch_cache_ensure_follicles(hsys, scalp, HAIR_DRAW_FOLLICLE_POINTS, cache);
		
		cache->follicles = GWN_batch_create(GWN_PRIM_POINTS, cache->follicle_verts, NULL);
	}

	return cache->follicles;
	
}

Gwn_Batch *DRW_hair_batch_cache_get_follicle_axes(
        HairSystem *hsys,
        struct DerivedMesh *scalp)
{
	return NULL;
}

Gwn_Batch *DRW_hair_batch_cache_get_guide_curve_points(
        HairSystem *hsys,
        struct DerivedMesh *scalp,
        int subdiv)
{
	return NULL;
}

Gwn_Batch *DRW_hair_batch_cache_get_guide_curve_edges(
        HairSystem *hsys,
        struct DerivedMesh *scalp,
        int subdiv)
{
	return NULL;
}
