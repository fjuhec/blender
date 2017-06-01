/*
 * Copyright 2017, Blender Foundation.
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
 * Contributor(s): Antonio Vazquez
 *
 */

/** \file blender/draw/engines/gpencil/gpencil_draw_cache_impl.c
 *  \ingroup draw
 */

#include "DRW_engine.h"
#include "DRW_render.h"

#include "BKE_global.h"
#include "BKE_gpencil.h"
#include "BKE_image.h"
#include "ED_gpencil.h"
#include "ED_view3d.h"

#include "DNA_gpencil_types.h"
#include "DNA_view3d_types.h"

 /* If builtin shaders are needed */
#include "GPU_shader.h"
#include "GPU_texture.h"

#include "IMB_imbuf_types.h"

#include "gpencil_engine.h"

/* allocate cache to store GP objects */
tGPencilObjectCache *gpencil_object_cache_allocate(tGPencilObjectCache *cache, int *gp_cache_size, int *gp_cache_used)
{
	tGPencilObjectCache *p = NULL;

	/* By default a cache is created with one block with a predefined number of free slots,
	if the size is not enough, the cache is reallocated adding a new block of free slots.
	This is done in order to keep cache small */
	if (*gp_cache_used + 1 > *gp_cache_size) {
		if ((*gp_cache_size == 0) || (cache == NULL)) {
			p = MEM_callocN(sizeof(struct tGPencilObjectCache) * GP_CACHE_BLOCK_SIZE, "tGPencilObjectCache");
			*gp_cache_size = GP_CACHE_BLOCK_SIZE;
		}
		else {
			*gp_cache_size += GP_CACHE_BLOCK_SIZE;
			p = MEM_recallocN(cache, sizeof(struct tGPencilObjectCache) * *gp_cache_size);
		}
		cache = p;
	}
	return cache;
}

/* add a gpencil object to cache to defer drawing */
void gpencil_object_cache_add(tGPencilObjectCache *cache, RegionView3D *rv3d, Object *ob, int *gp_cache_used)
{
	/* save object */
	cache[*gp_cache_used].ob = ob;

	/* calculate zdepth from point of view */
	float zdepth = 0.0;
	if (rv3d->is_persp) {
		zdepth = ED_view3d_calc_zfac(rv3d, ob->loc, NULL);
	}
	else {
		zdepth = -dot_v3v3(rv3d->viewinv[2], ob->loc);
	}
	cache[*gp_cache_used].zdepth = zdepth;

	/* increase slots used in cache */
	++*gp_cache_used;
}

/* helper function to sort gpencil objects using qsort */
static int gpencil_object_cache_compare_zdepth(const void *a1, const void *a2)
{
	const tGPencilObjectCache *ps1 = a1, *ps2 = a2;

	if (ps1->zdepth > ps2->zdepth) return 1;
	else if (ps1->zdepth < ps2->zdepth) return -1;

	return 0;
}

/* draw objects in cache from back to from */
void gpencil_object_cache_draw(GPENCIL_e_data *e_data, GPENCIL_Data *vedata, ToolSettings *ts,
	Scene *scene, tGPencilObjectCache *cache, int gp_cache_used)
{
	if (gp_cache_used > 0) {
		/* sort by zdepth */
		qsort(cache, gp_cache_used, sizeof(tGPencilObjectCache), gpencil_object_cache_compare_zdepth);
		/* inverse loop to draw from back to front */
		for (int i = gp_cache_used; i > 0; --i) {
			Object *ob = cache[i - 1].ob;
			DRW_gpencil_populate_datablock(e_data, vedata, scene, ob, ts, ob->gpd);
		}
	}
	/* free memory */
	MEM_SAFE_FREE(cache);
}

/* verify if cache is valid */
static bool gpencil_batch_cache_valid(bGPdata *gpd, int cfra)
{
	GpencilBatchCache *cache = gpd->batch_cache;

	if (cache == NULL) {
		return false;
	}

	if (gpd->flag & GP_DATA_CACHE_IS_DIRTY) {
		return false;
	}

	cache->is_editmode = gpd->flag & GP_DATA_STROKE_EDITMODE;
	if (cache->is_editmode) {
		return false;
	}

	if (cfra != cache->cache_frame) {
		return false;
	}

	return true;
}

/* resize the cache to the number of slots */
static void gpencil_batch_cache_resize(bGPdata *gpd, int slots)
{
	GpencilBatchCache *cache = gpd->batch_cache;
	cache->cache_size = slots;
	cache->batch_stroke = MEM_recallocN(cache->batch_stroke, sizeof(struct Batch) * slots);
	cache->batch_fill = MEM_recallocN(cache->batch_fill, sizeof(struct Batch) * slots);
	cache->batch_edit = MEM_recallocN(cache->batch_edit, sizeof(struct Batch) * slots);
}

/* check size and increase if no free slots */
static void gpencil_batch_cache_check_free_slots(bGPdata *gpd)
{
	GpencilBatchCache *cache = gpd->batch_cache;

	/* the memory is reallocated by chunks, not for one slot only to improve speed */
	if (cache->cache_idx >= cache->cache_size)
	{
		cache->cache_size += GPENCIL_MIN_BATCH_SLOTS_CHUNK;
		gpencil_batch_cache_resize(gpd, cache->cache_size);
	}
}
/* cache init */
static void gpencil_batch_cache_init(bGPdata *gpd, int cfra)
{
	if (G.debug_value == 668) {
		printf("gpencil_batch_cache_init for %s\n", gpd->id.name);
	}
	GpencilBatchCache *cache = gpd->batch_cache;

	if (!cache) {
		cache = gpd->batch_cache = MEM_callocN(sizeof(*cache), __func__);
	}
	else {
		memset(cache, 0, sizeof(*cache));
	}

	cache->cache_size = GPENCIL_MIN_BATCH_SLOTS_CHUNK;
	cache->batch_stroke = MEM_callocN(sizeof(struct Batch) * cache->cache_size, "Gpencil_Batch_Stroke");
	cache->batch_fill = MEM_callocN(sizeof(struct Batch) * cache->cache_size, "Gpencil_Batch_Fill");
	cache->batch_edit = MEM_callocN(sizeof(struct Batch) * cache->cache_size, "Gpencil_Batch_Edit");

	cache->is_editmode = gpd->flag & GP_DATA_STROKE_EDITMODE;
	gpd->flag &= ~GP_DATA_CACHE_IS_DIRTY;

	cache->cache_idx = 0;
	cache->is_dirty = true;
	cache->cache_frame = cfra;
}

/*  clear cache */
void gpencil_batch_cache_clear(bGPdata *gpd)
{
	GpencilBatchCache *cache = gpd->batch_cache;
	if (!cache) {
		return;
	}
	if (gpd->flag & GP_DATA_CACHE_IS_DIRTY) {
		return;
	}
	if (G.debug_value == 668) {
		printf("gpencil_batch_cache_clear for %s\n", gpd->id.name);
	}

	if (cache->cache_size > 0) {
		for (int i = 0; i < cache->cache_size; ++i) {
		BATCH_DISCARD_ALL_SAFE(cache->batch_stroke[i]);
		BATCH_DISCARD_ALL_SAFE(cache->batch_fill[i]);
		BATCH_DISCARD_ALL_SAFE(cache->batch_edit[i]);
	}
		MEM_SAFE_FREE(cache->batch_stroke);
		MEM_SAFE_FREE(cache->batch_fill);
		MEM_SAFE_FREE(cache->batch_edit);
	}

	BATCH_DISCARD_ALL_SAFE(cache->batch_buffer_stroke);
	BATCH_DISCARD_ALL_SAFE(cache->batch_buffer_fill);
}

/* get cache */
static GpencilBatchCache *gpencil_batch_cache_get(bGPdata *gpd, int cfra)
{
	if (!gpencil_batch_cache_valid(gpd, cfra)) {
		gpencil_batch_cache_clear(gpd);
		gpencil_batch_cache_init(gpd, cfra);
	}
	return gpd->batch_cache;
}

 /* create shading group for filling */
static DRWShadingGroup *DRW_gpencil_shgroup_fill_create(GPENCIL_Data *vedata, DRWPass *pass, GPUShader *shader, bGPdata *gpd, PaletteColor *palcolor, int id)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

	/* e_data.gpencil_fill_sh */
	DRWShadingGroup *grp = DRW_shgroup_create(shader, pass);
	DRW_shgroup_uniform_vec4(grp, "color2", palcolor->scolor, 1);
	stl->shgroups[id].fill_style = palcolor->fill_style;
	DRW_shgroup_uniform_int(grp, "fill_type", &stl->shgroups[id].fill_style, 1);
	DRW_shgroup_uniform_float(grp, "mix_factor", &palcolor->mix_factor, 1);

	DRW_shgroup_uniform_float(grp, "g_angle", &palcolor->g_angle, 1);
	DRW_shgroup_uniform_float(grp, "g_radius", &palcolor->g_radius, 1);
	DRW_shgroup_uniform_float(grp, "g_boxsize", &palcolor->g_boxsize, 1);
	DRW_shgroup_uniform_vec2(grp, "g_scale", palcolor->g_scale, 1);
	DRW_shgroup_uniform_vec2(grp, "g_shift", palcolor->g_shift, 1);

	DRW_shgroup_uniform_float(grp, "t_angle", &palcolor->t_angle, 1);
	DRW_shgroup_uniform_vec2(grp, "t_scale", palcolor->t_scale, 1);
	DRW_shgroup_uniform_vec2(grp, "t_shift", palcolor->t_shift, 1);
	DRW_shgroup_uniform_float(grp, "t_opacity", &palcolor->t_opacity, 1);

	stl->shgroups[id].t_mix = palcolor->flag & PAC_COLOR_TEX_MIX ? 1 : 0;
	DRW_shgroup_uniform_int(grp, "t_mix", &stl->shgroups[id].t_mix, 1);

	stl->shgroups[id].t_flip = palcolor->flag & PAC_COLOR_FLIP_FILL ? 1 : 0;
	DRW_shgroup_uniform_int(grp, "t_flip", &stl->shgroups[id].t_flip, 1);

	DRW_shgroup_uniform_int(grp, "xraymode", (const int *) &gpd->xray_mode, 1);

	/* image texture */
	if ((palcolor->fill_style == FILL_STYLE_TEXTURE) || (palcolor->flag & PAC_COLOR_TEX_MIX)) {
		ImBuf *ibuf;
		Image *image = palcolor->ima;
		ImageUser iuser = { NULL };
		void *lock;

		iuser.ok = true;

		ibuf = BKE_image_acquire_ibuf(image, &iuser, &lock);

		if (ibuf == NULL || ibuf->rect == NULL) {
			BKE_image_release_ibuf(image, ibuf, NULL);
		}
		else {
			GPUTexture *texture = GPU_texture_from_blender(palcolor->ima, &iuser, GL_TEXTURE_2D, true, 0.0, 0);
			DRW_shgroup_uniform_texture(grp, "myTexture", texture);

			stl->shgroups[id].t_clamp = palcolor->flag & PAC_COLOR_TEX_CLAMP ? 1 : 0;
			DRW_shgroup_uniform_int(grp, "t_clamp", &stl->shgroups[id].t_clamp, 1);

			BKE_image_release_ibuf(image, ibuf, NULL);
		}
	}

	return grp;
}

/* create shading group for volumetric points */
DRWShadingGroup *DRW_gpencil_shgroup_point_volumetric_create(DRWPass *pass, GPUShader *shader)
{
	/* e_data.gpencil_volumetric_sh */
	DRWShadingGroup *grp = DRW_shgroup_create(shader, pass);

	return grp;
}

/* create shading group for strokes */
DRWShadingGroup *DRW_gpencil_shgroup_stroke_create(GPENCIL_Data *vedata, DRWPass *pass, GPUShader *shader, bGPdata *gpd)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

	const float *viewport_size = DRW_viewport_size_get();
	const DRWContextState *draw_ctx = DRW_context_state_get();
	RegionView3D *rv3d = draw_ctx->rv3d;

	/* e_data.gpencil_stroke_sh */
	DRWShadingGroup *grp = DRW_shgroup_create(shader, pass);
	DRW_shgroup_uniform_vec2(grp, "Viewport", viewport_size, 1);

	DRW_shgroup_uniform_float(grp, "pixsize", &rv3d->pixsize, 1);
	DRW_shgroup_uniform_float(grp, "pixelsize", &U.pixelsize, 1);

	stl->storage->is_persp = rv3d->is_persp ? 1 : 0;
	DRW_shgroup_uniform_int(grp, "is_persp", &stl->storage->is_persp, 1);

	if (gpd) {
		DRW_shgroup_uniform_int(grp, "xraymode", (const int *) &gpd->xray_mode, 1);
	}
	else {
		/* for drawing always on front */
		DRW_shgroup_uniform_int(grp, "xraymode", &stl->storage->xray, 1);
	}

	return grp;
}

/* create shading group for edit points using volumetric */
DRWShadingGroup *DRW_gpencil_shgroup_edit_volumetric_create(DRWPass *pass, GPUShader *shader)
{
	/* e_data.gpencil_volumetric_sh */
	DRWShadingGroup *grp = DRW_shgroup_create(shader, pass);

	return grp;
}

/* create shading group for drawing fill in buffer */
DRWShadingGroup *DRW_gpencil_shgroup_drawing_fill_create(DRWPass *pass, GPUShader *shader)
{
	/* e_data.gpencil_drawing_fill_sh */
	DRWShadingGroup *grp = DRW_shgroup_create(shader, pass);
	return grp;
}

/* add fill shading group to pass */
static void gpencil_add_fill_shgroup(GpencilBatchCache *cache, DRWShadingGroup *fillgrp, 
	bGPdata *gpd, bGPDlayer *gpl, bGPDframe *gpf, bGPDstroke *gps, 
	const float tintcolor[4], const bool onion, const bool custonion)
{
	if (gps->totpoints >= 3) {
		float tfill[4];
		/* set color using palette, tint color and opacity */
		interp_v3_v3v3(tfill, gps->palcolor->fill, tintcolor, tintcolor[3]);
		tfill[3] = gps->palcolor->fill[3] * gpl->opacity;
		if ((tfill[3] > GPENCIL_ALPHA_OPACITY_THRESH) || (gps->palcolor->fill_style > 0)) {
			const float *color;
			if (!onion) {
				color = tfill;
			}
			else {
				if (custonion) {
					color = tintcolor;
				}
				else {
					ARRAY_SET_ITEMS(tfill, UNPACK3(gps->palcolor->fill), tintcolor[3]);
					color = tfill;
				}
			}
			if (cache->is_dirty) {
				gpencil_batch_cache_check_free_slots(gpd);
				cache->batch_fill[cache->cache_idx] = DRW_gpencil_get_fill_geom(gps, color);
			}
			DRW_shgroup_call_add(fillgrp, cache->batch_fill[cache->cache_idx], gpf->viewmatrix);
		}
	}
}

/* add stroke shading group to pass */
static void gpencil_add_stroke_shgroup(GPENCIL_StorageList *stl, GpencilBatchCache *cache, DRWShadingGroup *strokegrp,
	bGPdata *gpd, bGPDlayer *gpl, bGPDframe *gpf, bGPDstroke *gps, 
	const float opacity, const float tintcolor[4], const bool onion, const bool custonion)
{
	float tcolor[4];
	float ink[4];

	/* set color using palette, tint color and opacity */
	if (!onion) {
		interp_v3_v3v3(tcolor, gps->palcolor->rgb, tintcolor, tintcolor[3]);
		tcolor[3] = gps->palcolor->rgb[3] * opacity;
		copy_v4_v4(ink, tcolor);
	}
	else {
		if (custonion) {
			copy_v4_v4(ink, tintcolor);
		}
		else {
			ARRAY_SET_ITEMS(tcolor, gps->palcolor->rgb[0], gps->palcolor->rgb[1], gps->palcolor->rgb[2], opacity);
			copy_v4_v4(ink, tcolor);
		}
	}
	short sthickness = gps->thickness + gpl->thickness;
	if (sthickness > 0) {
		if (gps->totpoints > 1) {
			if (cache->is_dirty) {
				gpencil_batch_cache_check_free_slots(gpd);
				cache->batch_stroke[cache->cache_idx] = DRW_gpencil_get_stroke_geom(gpf, gps, sthickness, ink);
			}
			DRW_shgroup_call_add(strokegrp, cache->batch_stroke[cache->cache_idx], gpf->viewmatrix);
		}
		else if (gps->totpoints == 1) {
			if (cache->is_dirty) {
				gpencil_batch_cache_check_free_slots(gpd);
				cache->batch_stroke[cache->cache_idx] = DRW_gpencil_get_point_geom(gps->points, sthickness, ink);
			}
			DRW_shgroup_call_add(stl->g_data->shgrps_point_volumetric, cache->batch_stroke[cache->cache_idx], gpf->viewmatrix);
		}
	}
}

/* add edit points shading group to pass */
static void gpencil_add_editpoints_shgroup(GPENCIL_StorageList *stl, GpencilBatchCache *cache, ToolSettings *ts,
	bGPdata *gpd, bGPDlayer *gpl, bGPDframe *gpf, bGPDstroke *gps) {
	if ((gpl->flag & GP_LAYER_LOCKED) == 0 && (gpd->flag & GP_DATA_STROKE_EDITMODE))
	{
		if (gps->flag & GP_STROKE_SELECT) {
			if ((gpl->flag & GP_LAYER_UNLOCK_COLOR) || ((gps->palcolor->flag & PC_COLOR_LOCKED) == 0)) {
				if (cache->is_dirty) {
					gpencil_batch_cache_check_free_slots(gpd);
					cache->batch_edit[cache->cache_idx] = DRW_gpencil_get_edit_geom(gps, ts->gp_sculpt.alpha, gpd->flag);
				}
				DRW_shgroup_call_add(stl->g_data->shgrps_edit_volumetric, cache->batch_edit[cache->cache_idx], gpf->viewmatrix);
			}
		}
	}
}

/* main function to draw strokes */
static void gpencil_draw_strokes(GpencilBatchCache *cache, GPENCIL_e_data *e_data, void *vedata, ToolSettings *ts, Object *ob,
	bGPdata *gpd, bGPDlayer *gpl, bGPDframe *gpf,
	const float opacity, const float tintcolor[4], const bool onion, const bool custonion)
{
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	const DRWContextState *draw_ctx = DRW_context_state_get();
	RegionView3D *rv3d = draw_ctx->rv3d;
	Scene *scene = draw_ctx->scene;

	DRWShadingGroup *fillgrp;
	DRWShadingGroup *strokegrp;
	float viewmatrix[4][4];

	/* get parent matrix and save as static data */
	ED_gpencil_parent_location(ob, gpd, gpl, viewmatrix);
	copy_m4_m4(gpf->viewmatrix, viewmatrix);

	for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
		/* check if stroke can be drawn */
		if (gpencil_can_draw_stroke(rv3d, gpf, gps) == false) {
			continue;
		}
		/* limit the number of shading groups */
		if (stl->storage->pal_id >= GPENCIL_MAX_SHGROUPS) {
			continue;
		}
#if 0   /* if we use the reallocate the shading group is doing weird thing, so disable while find a solution 
		   and allocate the max size on cache_init */
		/* realloc memory */
		GPENCIL_shgroup *p = NULL;
		int size = stl->storage->pal_id + 1;
		p = MEM_recallocN(stl->shgroups, sizeof(struct GPENCIL_shgroup) * size);
		if (p != NULL) {
			stl->shgroups = p;
		}
#endif
		if (gps->totpoints > 1) {
			int id = stl->storage->pal_id;
			stl->shgroups[id].shgrps_fill = DRW_gpencil_shgroup_fill_create(vedata, psl->stroke_pass, e_data->gpencil_fill_sh, gpd, gps->palcolor, id);
			stl->shgroups[id].shgrps_stroke = DRW_gpencil_shgroup_stroke_create(vedata, psl->stroke_pass, e_data->gpencil_stroke_sh, gpd);
			++stl->storage->pal_id;
			
			fillgrp = stl->shgroups[id].shgrps_fill;
			strokegrp = stl->shgroups[id].shgrps_stroke;
		}
		/* fill */
		gpencil_add_fill_shgroup(cache, fillgrp, gpd, gpl, gpf, gps, tintcolor, onion, custonion);

		/* stroke */
		gpencil_add_stroke_shgroup(stl, cache, strokegrp, gpd, gpl, gpf, gps, opacity, tintcolor, onion, custonion);

		/* edit points (only in edit mode) */
		if (!onion) {
			gpencil_add_editpoints_shgroup(stl, cache, ts, gpd, gpl, gpf, gps);
		}

		++cache->cache_idx;
	}
}

 /* draw stroke in drawing buffer */
static void gpencil_draw_buffer_strokes(GpencilBatchCache *cache, void *vedata, ToolSettings *ts, bGPdata *gpd)
{
	const DRWContextState *draw_ctx = DRW_context_state_get();
	Scene *scene = draw_ctx->scene;

	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	bGPDbrush *brush = BKE_gpencil_brush_getactive(ts);

	/* drawing strokes */
	/* Check if may need to draw the active stroke cache, only if this layer is the active layer
	* that is being edited. (Stroke buffer is currently stored in gp-data)
	*/
	if (ED_gpencil_session_active() && (gpd->sbuffer_size > 0))
	{
		if ((gpd->sbuffer_sflag & GP_STROKE_ERASER) == 0) {
			/* It should also be noted that sbuffer contains temporary point types
			* i.e. tGPspoints NOT bGPDspoints
			*/
			short lthick = brush->thickness;
			if (gpd->sbuffer_size == 1) {
				cache->batch_buffer_stroke = DRW_gpencil_get_buffer_point_geom(gpd, lthick);
				DRW_shgroup_call_add(stl->g_data->shgrps_point_volumetric, cache->batch_buffer_stroke, stl->storage->unit_matrix);
			}
			else {
				/* use unit matrix because the buffer is in screen space and does not need conversion */
				cache->batch_buffer_stroke = DRW_gpencil_get_buffer_stroke_geom(gpd, stl->storage->unit_matrix, lthick);
				DRW_shgroup_call_add(stl->g_data->shgrps_drawing_stroke, cache->batch_buffer_stroke, stl->storage->unit_matrix);

				if ((gpd->sbuffer_size >= 3) && ((gpd->sfill[3] > GPENCIL_ALPHA_OPACITY_THRESH) || (gpd->bfill_style > 0))) {
					/* if not solid, fill is simulated with solid color */
					if (gpd->bfill_style > 0) {
						gpd->sfill[3] = 0.5f;
					}
					cache->batch_buffer_fill = DRW_gpencil_get_buffer_fill_geom(gpd->sbuffer, gpd->sbuffer_size, gpd->sfill);
					DRW_shgroup_call_add(stl->g_data->shgrps_drawing_fill, cache->batch_buffer_fill, stl->storage->unit_matrix);
				}
			}
		}
	}
}

/* draw onion-skinning for a layer */
static void gpencil_draw_onionskins(GpencilBatchCache *cache, GPENCIL_e_data *e_data, void *vedata, ToolSettings *ts, Object *ob, bGPdata *gpd, bGPDlayer *gpl, bGPDframe *gpf)
{
	const float default_color[3] = { UNPACK3(U.gpencil_new_layer_col) };
	const float alpha = 1.0f;
	float color[4];

	/* 1) Draw Previous Frames First */
	if (gpl->flag & GP_LAYER_GHOST_PREVCOL) {
		copy_v3_v3(color, gpl->gcolor_prev);
	}
	else {
		copy_v3_v3(color, default_color);
	}

	if (gpl->gstep > 0) {
		/* draw previous frames first */
		for (bGPDframe *gf = gpf->prev; gf; gf = gf->prev) {
			/* check if frame is drawable */
			if ((gpf->framenum - gf->framenum) <= gpl->gstep) {
				/* alpha decreases with distance from curframe index */
				float fac = 1.0f - ((float)(gpf->framenum - gf->framenum) / (float)(gpl->gstep + 1));
				color[3] = alpha * fac * 0.66f;
				gpencil_draw_strokes(cache, e_data, vedata, ts, ob, gpd, gpl, gf, 1.0f, color, true, gpl->flag & GP_LAYER_GHOST_PREVCOL);
			}
			else
				break;
		}
	}
	else if (gpl->gstep == 0) {
		/* draw the strokes for the ghost frames (at half of the alpha set by user) */
		if (gpf->prev) {
			color[3] = (alpha / 7);
			gpencil_draw_strokes(cache, e_data, vedata, ts, ob, gpd, gpl, gpf->prev, 1.0f, color, true, gpl->flag & GP_LAYER_GHOST_PREVCOL);
		}
	}
	else {
		/* don't draw - disabled */
	}

	/* 2) Now draw next frames */
	if (gpl->flag & GP_LAYER_GHOST_NEXTCOL) {
		copy_v3_v3(color, gpl->gcolor_next);
	}
	else {
		copy_v3_v3(color, default_color);
	}

	if (gpl->gstep_next > 0) {
		/* now draw next frames */
		for (bGPDframe *gf = gpf->next; gf; gf = gf->next) {
			/* check if frame is drawable */
			if ((gf->framenum - gpf->framenum) <= gpl->gstep_next) {
				/* alpha decreases with distance from curframe index */
				float fac = 1.0f - ((float)(gf->framenum - gpf->framenum) / (float)(gpl->gstep_next + 1));
				color[3] = alpha * fac * 0.66f;
				gpencil_draw_strokes(cache, e_data, vedata, ts, ob, gpd, gpl, gf, 1.0f, color, true, gpl->flag & GP_LAYER_GHOST_NEXTCOL);
			}
			else
				break;
		}
	}
	else if (gpl->gstep_next == 0) {
		/* draw the strokes for the ghost frames (at half of the alpha set by user) */
		if (gpf->next) {
			color[3] = (alpha / 4);
			gpencil_draw_strokes(cache, e_data, vedata, ts, ob, gpd, gpl, gpf->next, 1.0f, color, true, gpl->flag & GP_LAYER_GHOST_NEXTCOL);
		}
	}
	else {
		/* don't draw - disabled */
	}
}

/* helper for populate a complete grease pencil datablock */
void DRW_gpencil_populate_datablock(GPENCIL_e_data *e_data, void *vedata, Scene *scene, Object *ob, ToolSettings *ts, bGPdata *gpd)
{
	if (G.debug_value == 668) {
		printf("DRW_gpencil_populate_datablock for %s\n", gpd->id.name);
	}

	GpencilBatchCache *cache = gpencil_batch_cache_get(gpd, CFRA);
	cache->cache_idx = 0;
	/* draw normal strokes */
	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		/* don't draw layer if hidden */
		if (gpl->flag & GP_LAYER_HIDE)
			continue;

		bGPDframe *gpf = BKE_gpencil_layer_getframe(gpl, CFRA, 0);
		if (gpf == NULL)
			continue;
		/* draw onion skins */
		if ((gpl->flag & GP_LAYER_ONIONSKIN) || (gpl->flag & GP_LAYER_GHOST_ALWAYS))
		{
			gpencil_draw_onionskins(cache, e_data, vedata, ts, ob, gpd, gpl, gpf);
		}
		/* draw normal strokes */
		gpencil_draw_strokes(cache, e_data, vedata, ts, ob, gpd, gpl, gpf, gpl->opacity, gpl->tintcolor, false, false);
	}
	/* draw current painting strokes */
	gpencil_draw_buffer_strokes(cache, vedata, ts, gpd);
	cache->is_dirty = false;
}

void DRW_gpencil_batch_cache_dirty(bGPdata *gpd, int mode)
{
	GpencilBatchCache *cache = gpd->batch_cache;
	if (cache == NULL) {
		return;
	}
	cache->is_dirty = true;
}

void DRW_gpencil_batch_cache_free(bGPdata *gpd)
{
	gpencil_batch_cache_clear(gpd);
	MEM_SAFE_FREE(gpd->batch_cache);
}

