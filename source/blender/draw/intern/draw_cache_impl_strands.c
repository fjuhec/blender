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

#include "BKE_editstrands.h"

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
/* Strands Gwn_Batch Cache */

typedef enum VertexDrawFlags
{
	STRANDS_VERTEX_SELECT = (1 << 0),
} VertexDrawFlags;

typedef struct StrandsBatchCache {
	Gwn_VertBuf *pos;
	Gwn_IndexBuf *segments;
	Gwn_IndexBuf *tips_idx;
	Gwn_IndexBuf *roots_idx;

	Gwn_Batch *wires;
	Gwn_Batch *tips;
	Gwn_Batch *roots;
	Gwn_Batch *points;

	struct {
		Gwn_VertBuf *verts;
		Gwn_IndexBuf *segments;

		Gwn_Batch *fibers;
		bool use_ribbons;

		DRWHairFiberTextureBuffer texbuffer;
	} hair;

	int segment_count;
	int point_count;

	/* settings to determine if cache is invalid */
	bool is_dirty;
} StrandsBatchCache;

/* Gwn_Batch cache management. */

static void editstrands_batch_cache_clear(BMEditStrands *es);

static bool editstrands_batch_cache_valid(BMEditStrands *es)
{
	StrandsBatchCache *cache = es->batch_cache;

	if (cache == NULL) {
		return false;
	}

	if (cache->is_dirty) {
		return false;
	}

	return true;
}

static void editstrands_batch_cache_init(BMEditStrands *es)
{
	StrandsBatchCache *cache = es->batch_cache;
	
	if (!cache) {
		cache = es->batch_cache = MEM_callocN(sizeof(*cache), __func__);
	}
	else {
		memset(cache, 0, sizeof(*cache));
	}
	
	cache->is_dirty = false;
}

static StrandsBatchCache *editstrands_batch_cache_get(BMEditStrands *es)
{
	if (!editstrands_batch_cache_valid(es)) {
		editstrands_batch_cache_clear(es);
		editstrands_batch_cache_init(es);
	}
	return es->batch_cache;
}

void DRW_editstrands_batch_cache_dirty(BMEditStrands *es, int mode)
{
	StrandsBatchCache *cache = es->batch_cache;
	if (cache == NULL) {
		return;
	}
	switch (mode) {
		case BKE_STRANDS_BATCH_DIRTY_ALL:
		case BKE_STRANDS_BATCH_DIRTY_SELECT:
			cache->is_dirty = true;
			break;
		default:
			BLI_assert(0);
	}
}

static void editstrands_batch_cache_clear_hair(BMEditStrands *es)
{
	StrandsBatchCache *cache = es->batch_cache;
	
	if (es->texture) {
		GPU_texture_free(es->texture);
		es->texture = NULL;
	}
	
	if (cache) {
		GWN_BATCH_DISCARD_SAFE(cache->hair.fibers);
		GWN_VERTBUF_DISCARD_SAFE(cache->hair.verts);
		GWN_INDEXBUF_DISCARD_SAFE(cache->hair.segments);
		
		{
			DRWHairFiberTextureBuffer *buffer = &cache->hair.texbuffer;
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

static void editstrands_batch_cache_clear(BMEditStrands *es)
{
	StrandsBatchCache *cache = es->batch_cache;
	
	editstrands_batch_cache_clear_hair(es);
	
	if (cache) {
		GWN_BATCH_DISCARD_SAFE(cache->wires);
		GWN_BATCH_DISCARD_SAFE(cache->points);
		GWN_BATCH_DISCARD_SAFE(cache->tips);
		GWN_BATCH_DISCARD_SAFE(cache->roots);
		GWN_VERTBUF_DISCARD_SAFE(cache->pos);
		GWN_INDEXBUF_DISCARD_SAFE(cache->segments);
	}
}

void DRW_editstrands_batch_cache_free(BMEditStrands *es)
{
	editstrands_batch_cache_clear(es);
	MEM_SAFE_FREE(es->batch_cache);
}

static void editstrands_batch_cache_ensure_pos(BMEditStrands *es, StrandsBatchCache *cache)
{
	if (cache->pos) {
		return;
	}
	
	GWN_VERTBUF_DISCARD_SAFE(cache->pos);
	
	static Gwn_VertFormat format = { 0 };
	static unsigned pos_id, flag_id;
	
	/* initialize vertex format */
	if (format.attrib_ct == 0) {
		pos_id = GWN_vertformat_attr_add(&format, "pos", GWN_COMP_F32, 3, GWN_FETCH_FLOAT);
		flag_id = GWN_vertformat_attr_add(&format, "flag", GWN_COMP_U8, 1, GWN_FETCH_INT);
	}
	
	BMesh *bm = es->base.bm;
	BMVert *vert;
	BMIter iter;
	int curr_point;
	
	cache->pos = GWN_vertbuf_create_with_format(&format);
	GWN_vertbuf_data_alloc(cache->pos, bm->totvert);
	
	BM_ITER_MESH_INDEX(vert, &iter, bm, BM_VERTS_OF_MESH, curr_point) {
		GWN_vertbuf_attr_set(cache->pos, pos_id, curr_point, vert->co);
		
		uint8_t flag = 0;
		if (BM_elem_flag_test(vert, BM_ELEM_SELECT))
			flag |= STRANDS_VERTEX_SELECT;
		GWN_vertbuf_attr_set(cache->pos, flag_id, curr_point, &flag);
	}
}

static void editstrands_batch_cache_ensure_segments(BMEditStrands *es, StrandsBatchCache *cache)
{
	if (cache->segments) {
		return;
	}
	
	GWN_INDEXBUF_DISCARD_SAFE(cache->segments);
	
	BMesh *bm = es->base.bm;
	BMEdge *edge;
	BMIter iter;
	
	Gwn_IndexBufBuilder elb;
	GWN_indexbuf_init(&elb, GWN_PRIM_LINES, bm->totedge, bm->totvert);
	
	BM_mesh_elem_index_ensure(bm, BM_VERT);
	
	BM_ITER_MESH(edge, &iter, bm, BM_EDGES_OF_MESH) {
		GWN_indexbuf_add_line_verts(&elb, BM_elem_index_get(edge->v1), BM_elem_index_get(edge->v2));
	}
	
	cache->segments = GWN_indexbuf_build(&elb);
}

static void editstrands_batch_cache_ensure_tips_idx(BMEditStrands *es, StrandsBatchCache *cache)
{
	if (cache->tips_idx) {
		return;
	}
	
	GWN_INDEXBUF_DISCARD_SAFE(cache->tips_idx);
	
	BMesh *bm = es->base.bm;
	int totstrands = BM_strands_count(bm);
	BMVert *root, *vert;
	BMIter iter, iter_strand;
	
	Gwn_IndexBufBuilder elb;
	GWN_indexbuf_init(&elb, GWN_PRIM_POINTS, totstrands, bm->totvert);
	
	BM_mesh_elem_index_ensure(bm, BM_VERT);
	
	BM_ITER_STRANDS(root, &iter, bm, BM_STRANDS_OF_MESH) {
		BM_ITER_STRANDS_ELEM(vert, &iter_strand, root, BM_VERTS_OF_STRAND)
		{
			if (BM_strands_vert_is_tip(vert)) {
				GWN_indexbuf_add_point_vert(&elb, BM_elem_index_get(vert));
				break;
			}
		}
	}
	
	cache->tips_idx = GWN_indexbuf_build(&elb);
}

static void editstrands_batch_cache_ensure_roots_idx(BMEditStrands *es, StrandsBatchCache *cache)
{
	if (cache->roots_idx) {
		return;
	}
	
	GWN_INDEXBUF_DISCARD_SAFE(cache->roots_idx);
	
	BMesh *bm = es->base.bm;
	int totstrands = BM_strands_count(bm);
	BMVert *root, *vert;
	BMIter iter, iter_strand;
	
	Gwn_IndexBufBuilder elb;
	GWN_indexbuf_init(&elb, GWN_PRIM_POINTS, totstrands, bm->totvert);
	
	BM_mesh_elem_index_ensure(bm, BM_VERT);
	
	BM_ITER_STRANDS(root, &iter, bm, BM_STRANDS_OF_MESH) {
		BM_ITER_STRANDS_ELEM(vert, &iter_strand, root, BM_VERTS_OF_STRAND)
		{
			if (BM_strands_vert_is_root(vert)) {
				GWN_indexbuf_add_point_vert(&elb, BM_elem_index_get(vert));
				break;
			}
		}
	}
	
	cache->roots_idx = GWN_indexbuf_build(&elb);
}

Gwn_Batch *DRW_editstrands_batch_cache_get_wires(BMEditStrands *es)
{
	StrandsBatchCache *cache = editstrands_batch_cache_get(es);

	if (cache->wires == NULL) {
		editstrands_batch_cache_ensure_pos(es, cache);
		editstrands_batch_cache_ensure_segments(es, cache);
		cache->wires = GWN_batch_create(GWN_PRIM_LINES, cache->pos, cache->segments);
	}

	return cache->wires;
}

Gwn_Batch *DRW_editstrands_batch_cache_get_tips(BMEditStrands *es)
{
	StrandsBatchCache *cache = editstrands_batch_cache_get(es);

	if (cache->tips == NULL) {
		editstrands_batch_cache_ensure_pos(es, cache);
		editstrands_batch_cache_ensure_tips_idx(es, cache);
		cache->tips = GWN_batch_create(GWN_PRIM_POINTS, cache->pos, cache->tips_idx);
	}

	return cache->tips;
}

Gwn_Batch *DRW_editstrands_batch_cache_get_roots(BMEditStrands *es)
{
	StrandsBatchCache *cache = editstrands_batch_cache_get(es);

	if (cache->roots == NULL) {
		editstrands_batch_cache_ensure_pos(es, cache);
		editstrands_batch_cache_ensure_roots_idx(es, cache);
		cache->roots = GWN_batch_create(GWN_PRIM_POINTS, cache->pos, cache->roots_idx);
	}

	return cache->roots;
}

Gwn_Batch *DRW_editstrands_batch_cache_get_points(BMEditStrands *es)
{
	StrandsBatchCache *cache = editstrands_batch_cache_get(es);

	if (cache->points == NULL) {
		editstrands_batch_cache_ensure_pos(es, cache);
		cache->points = GWN_batch_create(GWN_PRIM_POINTS, cache->pos, NULL);
	}

	return cache->points;
}

/* ---------------------------------------------------------------------- */
/* EditStrands Fibers Gwn_Batch Cache */

static void editstrands_batch_cache_ensure_hair_fibers(BMEditStrands *es, StrandsBatchCache *cache, bool use_ribbons, int subdiv)
{
	TIMEIT_START(editstrands_batch_cache_ensure_hair_fibers);

	GWN_VERTBUF_DISCARD_SAFE(cache->hair.verts);
	GWN_INDEXBUF_DISCARD_SAFE(cache->hair.segments);
	
	const int totfibers = es->hair_group->num_follicles;
	int *fiber_lengths = BKE_editstrands_hair_get_fiber_lengths(es, subdiv);
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
	
	cache->hair.verts = GWN_vertbuf_create_with_format(&format);

	Gwn_IndexBufBuilder elb;
	{
		TIMEIT_START(data_alloc);
		Gwn_PrimType prim_type;
		unsigned prim_ct, vert_ct;
		if (use_ribbons) {
			prim_type = GWN_PRIM_TRIS;
			prim_ct = 2 * totseg;
			vert_ct = 2 * totpoint;
		}
		else {
			prim_type = GWN_PRIM_LINES;
			prim_ct = totseg;
			vert_ct = totpoint;
		}
		
		GWN_vertbuf_data_alloc(cache->hair.verts, vert_ct);
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
			if (use_ribbons) {
				TIMEIT_BLOCK_START(GWN_vertbuf_attr_set);
				GWN_vertbuf_attr_set(cache->hair.verts, fiber_index_id, vi, &i);
				GWN_vertbuf_attr_set(cache->hair.verts, curve_param_id, vi, &a);
				GWN_vertbuf_attr_set(cache->hair.verts, fiber_index_id, vi+1, &i);
				GWN_vertbuf_attr_set(cache->hair.verts, curve_param_id, vi+1, &a);
				TIMEIT_BLOCK_END(GWN_vertbuf_attr_set);
				
				if (k > 0) {
					TIMEIT_BLOCK_START(GWN_indexbuf_add_tri_verts);
					GWN_indexbuf_add_tri_verts(&elb, vi-2, vi-1, vi+1);
					GWN_indexbuf_add_tri_verts(&elb, vi+1, vi, vi-2);
					TIMEIT_BLOCK_END(GWN_indexbuf_add_tri_verts);
				}
				
				vi += 2;
			}
			else {
				GWN_vertbuf_attr_set(cache->hair.verts, fiber_index_id, vi, &i);
				GWN_vertbuf_attr_set(cache->hair.verts, curve_param_id, vi, &a);
				
				if (k > 0) {
					GWN_indexbuf_add_line_verts(&elb, vi-1, vi);
				}
				
				vi += 1;
			}
			
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
	
	MEM_freeN(fiber_lengths);
	
	TIMEIT_BENCH(cache->hair.segments = GWN_indexbuf_build(&elb), indexbuf_build);

	TIMEIT_END(editstrands_batch_cache_ensure_hair_fibers);
}

static void editstrands_batch_cache_ensure_hair_fiber_texbuffer(BMEditStrands *es, StrandsBatchCache *cache, bool UNUSED(use_ribbons), int subdiv)
{
	DRWHairFiberTextureBuffer *buffer = &cache->hair.texbuffer;
	static const int elemsize = 8;
	const int width = GPU_max_texture_size();
	const int align = width * elemsize;
	
	// Offsets in bytes
	int b_size, b_strand_map_start, b_strand_vertex_start, b_fiber_start;
	BKE_editstrands_hair_get_texture_buffer_size(es, subdiv, &b_size, 
	        &b_strand_map_start, &b_strand_vertex_start, &b_fiber_start);
	// Pad for alignment
	b_size += align - b_size % align;
	
	// Convert to element size as texture offsets
	const int size = b_size / elemsize;
	const int height = size / width;
	
	buffer->data = MEM_mallocN(b_size, "hair fiber texture buffer");
	BKE_editstrands_hair_get_texture_buffer(es, subdiv, buffer->data);
	
	buffer->width = width;
	buffer->height = height;
	buffer->strand_map_start = b_strand_map_start / elemsize;
	buffer->strand_vertex_start = b_strand_vertex_start / elemsize;
	buffer->fiber_start = b_fiber_start / elemsize;
}

Gwn_Batch *DRW_editstrands_batch_cache_get_hair_fibers(BMEditStrands *es, bool use_ribbons, int subdiv,
                                                       const DRWHairFiberTextureBuffer **r_buffer)
{
	StrandsBatchCache *cache = editstrands_batch_cache_get(es);

	TIMEIT_START(DRW_editstrands_batch_cache_get_hair_fibers);

	if (cache->hair.use_ribbons != use_ribbons) {
		TIMEIT_BENCH(editstrands_batch_cache_clear_hair(es), editstrands_batch_cache_clear_hair);
	}

	if (cache->hair.fibers == NULL) {
		TIMEIT_BENCH(editstrands_batch_cache_ensure_hair_fibers(es, cache, use_ribbons, subdiv),
		             editstrands_batch_cache_ensure_hair_fibers);
		
		Gwn_PrimType prim_type = use_ribbons ? GWN_PRIM_TRIS : GWN_PRIM_LINES;
		TIMEIT_BENCH(cache->hair.fibers = GWN_batch_create(prim_type, cache->hair.verts, cache->hair.segments),
		             GWN_batch_create);
		cache->hair.use_ribbons = use_ribbons;

		TIMEIT_BENCH(editstrands_batch_cache_ensure_hair_fiber_texbuffer(es, cache, use_ribbons, subdiv),
		             editstrands_batch_cache_ensure_hair_fiber_texbuffer);
	}

	if (r_buffer) {
		*r_buffer = &cache->hair.texbuffer;
	}

	TIMEIT_END(DRW_editstrands_batch_cache_get_hair_fibers);

	return cache->hair.fibers;
}
