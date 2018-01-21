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

/** \file draw_cache_impl_groom.c
 *  \ingroup draw
 *
 * \brief Groom API for render engines
 */

#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"
#include "BLI_listbase.h"
#include "BLI_math.h"

#include "DNA_groom_types.h"
#include "DNA_scene_types.h"
#include "DNA_userdef_types.h"

#include "BKE_groom.h"

#include "GPU_batch.h"

#include "draw_cache_impl.h"  /* own include */

#define SELECT   1

static void groom_batch_cache_clear(Groom *groom);

/* ---------------------------------------------------------------------- */
/* Groom Gwn_Batch Cache */

typedef struct GroomBatchCache {
	Gwn_VertBuf *pos;
	Gwn_IndexBuf *edges;

	Gwn_Batch *all_verts;
	Gwn_Batch *all_edges;

	Gwn_Batch *overlay_verts;

	/* settings to determine if cache is invalid */
	bool is_dirty;

	bool is_editmode;
} GroomBatchCache;

/* Gwn_Batch cache management. */

static bool groom_batch_cache_valid(Groom *groom)
{
	GroomBatchCache *cache = groom->batch_cache;

	if (cache == NULL) {
		return false;
	}

	if (cache->is_editmode != (groom->editgroom != NULL)) {
		return false;
	}

	if (cache->is_dirty == false) {
		return true;
	}
	else {
		if (cache->is_editmode) {
			return false;
		}
	}

	return true;
}

static void groom_batch_cache_init(Groom *groom)
{
	GroomBatchCache *cache = groom->batch_cache;

	if (!cache) {
		cache = groom->batch_cache = MEM_callocN(sizeof(*cache), __func__);
	}
	else {
		memset(cache, 0, sizeof(*cache));
	}

	cache->is_editmode = (groom->editgroom != NULL);

	cache->is_dirty = false;
}

static GroomBatchCache *groom_batch_cache_get(Groom *groom)
{
	if (!groom_batch_cache_valid(groom)) {
		groom_batch_cache_clear(groom);
		groom_batch_cache_init(groom);
	}
	return groom->batch_cache;
}

void DRW_groom_batch_cache_dirty(Groom *groom, int mode)
{
	GroomBatchCache *cache = groom->batch_cache;
	if (cache == NULL) {
		return;
	}
	switch (mode) {
		case BKE_GROOM_BATCH_DIRTY_ALL:
			cache->is_dirty = true;
			break;
		case BKE_GROOM_BATCH_DIRTY_SELECT:
			/* TODO Separate Flag vbo */
			GWN_BATCH_DISCARD_SAFE(cache->overlay_verts);
			break;
		default:
			BLI_assert(0);
	}
}

static void groom_batch_cache_clear(Groom *groom)
{
	GroomBatchCache *cache = groom->batch_cache;
	if (!cache) {
		return;
	}

	GWN_BATCH_DISCARD_SAFE(cache->all_verts);
	GWN_BATCH_DISCARD_SAFE(cache->all_edges);
	GWN_BATCH_DISCARD_SAFE(cache->overlay_verts);

	GWN_VERTBUF_DISCARD_SAFE(cache->pos);
	GWN_INDEXBUF_DISCARD_SAFE(cache->edges);
}

void DRW_groom_batch_cache_free(Groom *groom)
{
	groom_batch_cache_clear(groom);
	MEM_SAFE_FREE(groom->batch_cache);
}

enum {
	VFLAG_VERTEX_SELECTED = 1 << 0,
	VFLAG_VERTEX_ACTIVE   = 1 << 1,
};

BLI_INLINE char make_vertex_flag(bool active, bool selected)
{
	char vflag = 0;
	if (active)
	{
		vflag |= VFLAG_VERTEX_ACTIVE;
	}
	if (selected)
	{
		vflag |= VFLAG_VERTEX_SELECTED;
	}
	return vflag;
}

/* Parts of the groom object to render */
typedef enum GroomRenderPart
{
	GM_RENDER_REGIONS   = (1 << 0),   /* Draw scalp regions */
	GM_RENDER_CURVES    = (1 << 1),   /* Draw center curves of bundles */
	GM_RENDER_SECTIONS  = (1 << 2),   /* Draw section curves */
	
	GM_RENDER_ALL       = GM_RENDER_REGIONS | GM_RENDER_CURVES | GM_RENDER_SECTIONS,
} GroomRenderPart;

static int groom_count_verts(Groom *groom, int parts, bool use_curve_cache)
{
	const ListBase *bundles = groom->editgroom ? &groom->editgroom->bundles : &groom->bundles;
	int vert_len = 0;
	
	if (parts & GM_RENDER_REGIONS)
	{
		// TODO
	}
	if (parts & GM_RENDER_CURVES)
	{
		for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
		{
			if (use_curve_cache)
			{
				vert_len += bundle->curvesize;
			}
			else
			{
				vert_len += bundle->totsections;
			}
		}
	}
	if (parts & GM_RENDER_SECTIONS)
	{
		for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
		{
			if (use_curve_cache)
			{
				vert_len += bundle->curvesize * bundle->numshapeverts;
			}
			else
			{
				vert_len += bundle->totverts;
			}
		}
	}
	
	return vert_len;
}

static int groom_count_edges(Groom *groom, int parts, bool use_curve_cache)
{
	const ListBase *bundles = groom->editgroom ? &groom->editgroom->bundles : &groom->bundles;
	int edge_len = 0;
	
	if (parts & GM_RENDER_REGIONS)
	{
		// TODO
	}
	if (parts & GM_RENDER_CURVES)
	{
		for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
		{
			if (use_curve_cache)
			{
				if (bundle->curvesize > 0)
				{
					edge_len += bundle->curvesize - 1;
				}
			}
			else
			{
				if (bundle->totsections > 0)
				{
					edge_len += bundle->totsections - 1;
				}
			}
		}
	}
	if (parts & GM_RENDER_SECTIONS)
	{
		for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
		{
			if (bundle->numshapeverts > 1)
			{
				// Closed edge loop, 1 edge per vertex
				if (use_curve_cache)
				{
					/* a curve for each shape vertex */
					int numedges_curves = (bundle->curvesize - 1) * bundle->numshapeverts;
					/* a loop for each section */
					int numedges_sections = bundle->numshapeverts * bundle->totsections;
					edge_len += numedges_curves + numedges_sections;
				}
				else
				{
					edge_len += bundle->totverts;
				}
			}
		}
	}
	
	return edge_len;
}

#define GM_ATTR_ID_UNUSED 0xFFFFFFFF

static void groom_get_verts(
        Groom *groom,
        int parts,
        bool use_curve_cache,
        Gwn_VertBuf *vbo,
        uint id_pos,
        uint id_flag)
{
	int vert_len = groom_count_verts(groom, parts, use_curve_cache);
	const ListBase *bundles = groom->editgroom ? &groom->editgroom->bundles : &groom->bundles;
	
	GWN_vertbuf_data_alloc(vbo, vert_len);
	
	uint idx = 0;
	if (parts & GM_RENDER_REGIONS)
	{
		// TODO
	}
	if (parts & GM_RENDER_CURVES)
	{
		for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
		{
			if (use_curve_cache)
			{
				GroomCurveCache *cache = bundle->curvecache;
				for (int i = 0; i < bundle->curvesize; ++i, ++cache)
				{
					if (id_pos != GM_ATTR_ID_UNUSED)
					{
						GWN_vertbuf_attr_set(vbo, id_pos, idx, cache->co);
					}
					if (id_flag != GM_ATTR_ID_UNUSED)
					{
						char vflag = make_vertex_flag(false, false);
						GWN_vertbuf_attr_set(vbo, id_flag, idx, &vflag);
					}
					
					++idx;
				}
			}
			else
			{
				const bool active = bundle->flag & GM_BUNDLE_SELECT;
				GroomSection *section = bundle->sections;
				for (int i = 0; i < bundle->totsections; ++i, ++section)
				{
					if (id_pos != GM_ATTR_ID_UNUSED)
					{
						GWN_vertbuf_attr_set(vbo, id_pos, idx, section->center);
					}
					if (id_flag != GM_ATTR_ID_UNUSED)
					{
						char vflag = make_vertex_flag(active, section->flag & GM_SECTION_SELECT);
						GWN_vertbuf_attr_set(vbo, id_flag, idx, &vflag);
					}
					
					++idx;
				}
			}
		}
	}
	if (parts & GM_RENDER_SECTIONS)
	{
		for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
		{
			if (use_curve_cache)
			{
				GroomCurveCache *cache = bundle->curvecache;
				for (int i = 0; i < bundle->numshapeverts; ++i)
				{
					for (int j = 0; j < bundle->curvesize; ++j, ++cache)
					{
						if (id_pos != GM_ATTR_ID_UNUSED)
						{
							GWN_vertbuf_attr_set(vbo, id_pos, idx, cache->co);
						}
						if (id_flag != GM_ATTR_ID_UNUSED)
						{
							char vflag = make_vertex_flag(false, false);
							GWN_vertbuf_attr_set(vbo, id_flag, idx, &vflag);
						}
						
						++idx;
					}
				}
			}
			else
			{
				GroomSection *section = bundle->sections;
				GroomSectionVertex *vertex = bundle->verts;
				for (int i = 0; i < bundle->totsections; ++i, ++section)
				{
					const bool active = (bundle->flag & GM_BUNDLE_SELECT) && (section->flag & GM_SECTION_SELECT);
					
					for (int j = 0; j < bundle->numshapeverts; ++j, ++vertex)
					{
						if (id_pos != GM_ATTR_ID_UNUSED)
						{
							float co[3] = {vertex->co[0], vertex->co[1], 0.0f};
							mul_m3_v3(section->mat, co);
							add_v3_v3(co, section->center);
							GWN_vertbuf_attr_set(vbo, id_pos, idx, co);
						}
						if (id_flag != GM_ATTR_ID_UNUSED)
						{
							char vflag = make_vertex_flag(active, vertex->flag & GM_VERTEX_SELECT);
							GWN_vertbuf_attr_set(vbo, id_flag, idx, &vflag);
						}
						
						++idx;
					}
				}
			}
		}
	}
}

static void groom_get_edges(
        Groom *groom,
        int parts,
        bool use_curve_cache,
        Gwn_IndexBuf **ibo)
{
	Gwn_IndexBufBuilder elb;
	
	int vert_len = groom_count_verts(groom, parts, use_curve_cache);
	int edge_len = groom_count_edges(groom, parts, use_curve_cache);
	const ListBase *bundles = groom->editgroom ? &groom->editgroom->bundles : &groom->bundles;
	
	GWN_indexbuf_init(&elb, GWN_PRIM_LINES, edge_len, vert_len);
	
	uint idx = 0;
	if (parts & GM_RENDER_REGIONS)
	{
		// TODO
	}
	if (parts & GM_RENDER_CURVES)
	{
		for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
		{
			if (use_curve_cache)
			{
				GroomCurveCache *cache = bundle->curvecache;
				for (int i = 0; i < bundle->curvesize - 1; ++i, ++cache)
				{
					GWN_indexbuf_add_line_verts(&elb, idx + i, idx + i + 1);
				}
				idx += bundle->curvesize;
			}
			else
			{
				GroomSection *section = bundle->sections;
				for (int i = 0; i < bundle->totsections - 1; ++i, ++section)
				{
					GWN_indexbuf_add_line_verts(&elb, idx + i, idx + i + 1);
				}
				idx += bundle->totsections;
			}
		}
	}
	if (parts & GM_RENDER_SECTIONS)
	{
		const int curve_res = groom->curve_res;
		for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
		{
			const int numshapeverts = bundle->numshapeverts;
			if (numshapeverts > 1)
			{
				if (use_curve_cache)
				{
					const int curvesize = bundle->curvesize;
					if (curvesize == 0)
					{
						continue;
					}
					
					/* a curve for each shape vertex */
					for (int i = 0; i < numshapeverts; ++i)
					{
						uint idx0 = idx + i * bundle->curvesize;
						for (int j = 0; j < bundle->curvesize - 1; ++j)
						{
							GWN_indexbuf_add_line_verts(
							            &elb,
							            idx0 +  j,
							            idx0 + (j+1));
						}
					}
					
					/* a loop for each section */
					for (int i = 0; i < bundle->totsections; ++i)
					{
						uint idx0 = idx + i * curve_res;
						for (int j = 0; j < numshapeverts - 1; ++j)
						{
							GWN_indexbuf_add_line_verts(&elb, idx0 + j * curvesize, idx0 + (j + 1) * curvesize);
						}
						// close the loop
						GWN_indexbuf_add_line_verts(&elb, idx0 + (numshapeverts-1) * curvesize, idx0);
					}
					
					idx += bundle->curvesize * bundle->numshapeverts;
				}
				else
				{
					for (int i = 0; i < bundle->totsections; ++i)
					{
						uint idx0 = idx + i * numshapeverts;
						for (int j = 0; j < numshapeverts - 1; ++j)
						{
							GWN_indexbuf_add_line_verts(&elb, idx0 + j, idx0 + j + 1);
						}
						// close the loop
						GWN_indexbuf_add_line_verts(&elb, idx0 + (numshapeverts-1), idx0);
					}
					
					idx += bundle->totverts;
				}
			}
		}
	}
	
	*ibo = GWN_indexbuf_build(&elb);
}

/* Gwn_Batch cache usage. */
static Gwn_VertBuf *groom_batch_cache_get_pos(Groom *groom, GroomBatchCache *cache, int parts)
{
	if (cache->pos == NULL) {
		static Gwn_VertFormat format = { 0 };
		static struct { uint pos, col; } attr_id;
		
		GWN_vertformat_clear(&format);
		
		/* initialize vertex format */
		attr_id.pos = GWN_vertformat_attr_add(&format, "pos", GWN_COMP_F32, 3, GWN_FETCH_FLOAT);
		
		cache->pos = GWN_vertbuf_create_with_format(&format);
		
		groom_get_verts(groom, parts, true, cache->pos, attr_id.pos, GM_ATTR_ID_UNUSED);
	}

	return cache->pos;
}

static Gwn_IndexBuf *groom_batch_cache_get_edges(Groom *groom, GroomBatchCache *cache, int parts)
{
	if (cache->edges == NULL) {
		groom_get_edges(groom, parts, true, &cache->edges);
	}

	return cache->edges;
}

static void groom_batch_cache_create_overlay_batches(Groom *groom, GroomBatchCache *cache, int parts)
{
	if (cache->overlay_verts == NULL) {
		static Gwn_VertFormat format = { 0 };
		static struct { uint pos, data; } attr_id;
		if (format.attrib_ct == 0) {
			/* initialize vertex format */
			attr_id.pos = GWN_vertformat_attr_add(&format, "pos", GWN_COMP_F32, 3, GWN_FETCH_FLOAT);
			attr_id.data = GWN_vertformat_attr_add(&format, "data", GWN_COMP_U8, 1, GWN_FETCH_INT);
		}
		
		Gwn_VertBuf *vbo = GWN_vertbuf_create_with_format(&format);
		
		groom_get_verts(groom, parts, false, vbo, attr_id.pos, attr_id.data);
		
		cache->overlay_verts = GWN_batch_create_ex(GWN_PRIM_POINTS, vbo, NULL, GWN_BATCH_OWNS_VBO);
	}	
}

Gwn_Batch *DRW_groom_batch_cache_get_all_edges(Groom *groom)
{
	GroomBatchCache *cache = groom_batch_cache_get(groom);

	if (cache->all_edges == NULL) {
		cache->all_edges = GWN_batch_create(
		                       GWN_PRIM_LINES,
		                       groom_batch_cache_get_pos(groom, cache, GM_RENDER_ALL),
		                       groom_batch_cache_get_edges(groom, cache, GM_RENDER_ALL));
	}

	return cache->all_edges;
}

Gwn_Batch *DRW_groom_batch_cache_get_all_verts(Groom *groom)
{
	GroomBatchCache *cache = groom_batch_cache_get(groom);

	if (cache->all_verts == NULL) {
		cache->all_verts = GWN_batch_create(
		                       GWN_PRIM_POINTS,
		                       groom_batch_cache_get_pos(groom, cache, GM_RENDER_ALL),
		                       NULL);
	}

	return cache->all_verts;
}

Gwn_Batch *DRW_groom_batch_cache_get_overlay_verts(Groom *groom, int mode)
{
	GroomBatchCache *cache = groom_batch_cache_get(groom);

	if (cache->overlay_verts == NULL) {
		GroomRenderPart parts = 0;
		switch ((GroomEditMode)mode)
		{
			case GM_EDIT_MODE_REGIONS: parts |= GM_RENDER_REGIONS; break;
			case GM_EDIT_MODE_CURVES: parts |= GM_RENDER_CURVES; break;
			case GM_EDIT_MODE_SECTIONS: parts |= GM_RENDER_SECTIONS; break;
		}
		
		groom_batch_cache_create_overlay_batches(groom, cache, parts);
	}

	return cache->overlay_verts;
}
