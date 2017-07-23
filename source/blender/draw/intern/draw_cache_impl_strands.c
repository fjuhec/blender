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

#include "BKE_editstrands.h"

#include "GPU_batch.h"

#include "draw_cache_impl.h"  /* own include */

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

	if (cache->is_dirty == false) {
		return true;
	}
	else {
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

static void editstrands_batch_cache_clear(BMEditStrands *es)
{
	StrandsBatchCache *cache = es->batch_cache;
	if (!cache) {
		return;
	}

	GWN_BATCH_DISCARD_SAFE(cache->wires);
	GWN_BATCH_DISCARD_SAFE(cache->points);
	GWN_BATCH_DISCARD_SAFE(cache->tips);
	GWN_BATCH_DISCARD_SAFE(cache->roots);

	GWN_VERTBUF_DISCARD_SAFE(cache->pos);
	GWN_INDEXBUF_DISCARD_SAFE(cache->segments);
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

	static Gwn_VertFormat format = { 0 };
	static unsigned pos_id, flag_id;
	
	GWN_VERTBUF_DISCARD_SAFE(cache->pos);
	
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
