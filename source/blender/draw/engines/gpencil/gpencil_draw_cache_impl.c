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
#include "BKE_lattice.h"
#include "BKE_paint.h"

#include "ED_gpencil.h"
#include "ED_view3d.h"

#include "DNA_gpencil_types.h"
#include "DNA_view3d_types.h"
#include "DNA_modifier_types.h"

 /* If builtin shaders are needed */
#include "GPU_shader.h"
#include "GPU_texture.h"

/* For EvaluationContext... */
#include "DEG_depsgraph.h"

#include "IMB_imbuf_types.h"

#include "draw_cache_impl.h"
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
void gpencil_object_cache_add(tGPencilObjectCache *cache_array, Object *ob, int *gp_cache_used)
{
	const DRWContextState *draw_ctx = DRW_context_state_get();
	tGPencilObjectCache *cache = &cache_array[*gp_cache_used];
	RegionView3D *rv3d = draw_ctx->rv3d;
	
	/* zero out all vfx_* pointers */
	memset(cache, 0, sizeof(*cache));
	
	/* save object */
	cache->ob = ob;
	
	cache->init_grp = 0;
	cache->end_grp = -1;
	
	/* calculate zdepth from point of view */
	float zdepth = 0.0;
	if (rv3d) {
		if (rv3d->is_persp) {
			zdepth = ED_view3d_calc_zfac(rv3d, ob->loc, NULL);
		}
		else {
			zdepth = -dot_v3v3(rv3d->viewinv[2], ob->loc);
		}
	}
	else {
		/* In render mode, rv3d is not available, so use the distance to camera.
		 * The real distance is not important, but the relative distance to the camera plane
		 * in order to sort by z_depth of the objects
		 */
		float vn[3] = { 0.0f, 0.0f, -1.0f }; /* always face down */
		float plane_cam[4];
		struct Object *camera = draw_ctx->scene->camera;
		if (camera) {
			mul_m4_v3(camera->obmat, vn);
			normalize_v3(vn);
			plane_from_point_normal_v3(plane_cam, camera->loc, vn);
			zdepth = dist_squared_to_plane_v3(ob->loc, plane_cam);
		}
	}
	cache->zdepth = zdepth;
	/* increase slots used in cache */
	(*gp_cache_used)++;
}

static GpencilBatchCache *gpencil_batch_get_element(Object *ob)
{
	bGPdata *gpd = ob->data;
	if (gpd->batch_cache_data == NULL) {
		gpd->batch_cache_data = BLI_ghash_str_new("GP batch cache data");
		return NULL;
	}

	return (GpencilBatchCache *) BLI_ghash_lookup(gpd->batch_cache_data, ob->id.name);
}

/* verify if cache is valid */
static bool gpencil_batch_cache_valid(Object *ob, bGPdata *gpd, int cfra)
{
	GpencilBatchCache *cache = gpencil_batch_get_element(ob);

	if (cache == NULL) {
		return false;
	}

	cache->is_editmode = GPENCIL_ANY_EDIT_MODE(gpd);

	if (cfra != cache->cache_frame) {
		return false;
	}

	if (gpd->flag & GP_DATA_CACHE_IS_DIRTY) {
		return false;
	}

	if (cache->is_editmode) {
		return false;
	}

	if (cache->is_dirty) {
		return false;
	}

	return true;
}

/* resize the cache to the number of slots */
static void gpencil_batch_cache_resize(GpencilBatchCache *cache, int slots)
{
	cache->cache_size = slots;
	cache->batch_stroke = MEM_recallocN(cache->batch_stroke, sizeof(struct Gwn_Batch *) * slots);
	cache->batch_fill = MEM_recallocN(cache->batch_fill, sizeof(struct Gwn_Batch *) * slots);
	cache->batch_edit = MEM_recallocN(cache->batch_edit, sizeof(struct Gwn_Batch *) * slots);
	cache->batch_edlin = MEM_recallocN(cache->batch_edlin, sizeof(struct Gwn_Batch *) * slots);
}

/* check size and increase if no free slots */
static void gpencil_batch_cache_check_free_slots(Object *ob)
{
	GpencilBatchCache *cache = gpencil_batch_get_element(ob);

	/* the memory is reallocated by chunks, not for one slot only to improve speed */
	if (cache->cache_idx >= cache->cache_size) {
		cache->cache_size += GPENCIL_MIN_BATCH_SLOTS_CHUNK;
		gpencil_batch_cache_resize(cache, cache->cache_size);
	}
}

/* cache init */
static void gpencil_batch_cache_init(Object *ob, int cfra)
{
	GpencilBatchCache *cache = gpencil_batch_get_element(ob);
	bGPdata *gpd = ob->data;

	if (G.debug_value >= 664) {
		printf("gpencil_batch_cache_init: %s\n", ob->id.name);
	}

	if (!cache) {
		cache = MEM_callocN(sizeof(*cache), __func__);
		BLI_ghash_insert(gpd->batch_cache_data, ob->id.name, cache);
	}
	else {
		memset(cache, 0, sizeof(*cache));
	}

	cache->cache_size = GPENCIL_MIN_BATCH_SLOTS_CHUNK;
	cache->batch_stroke = MEM_callocN(sizeof(struct Gwn_Batch *) * cache->cache_size, "Gpencil_Batch_Stroke");
	cache->batch_fill = MEM_callocN(sizeof(struct Gwn_Batch *) * cache->cache_size, "Gpencil_Batch_Fill");
	cache->batch_edit = MEM_callocN(sizeof(struct Gwn_Batch *) * cache->cache_size, "Gpencil_Batch_Edit");
	cache->batch_edlin = MEM_callocN(sizeof(struct Gwn_Batch *) * cache->cache_size, "Gpencil_Batch_Edlin");

	cache->is_editmode = GPENCIL_ANY_EDIT_MODE(gpd);
	gpd->flag &= ~GP_DATA_CACHE_IS_DIRTY;

	cache->cache_idx = 0;
	cache->is_dirty = true;
	cache->cache_frame = cfra;
}

/* clear cache */
static void gpencil_batch_cache_clear(GpencilBatchCache *cache, bGPdata *gpd)
{
	if (!cache) {
		return;
	}

	if (cache->cache_size == 0) {
		return;
	}

	if (G.debug_value >= 664) {
		printf("gpencil_batch_cache_clear: %s\n", gpd->id.name);
	}

	if (cache->cache_size > 0) {
		for (int i = 0; i < cache->cache_size; i++) {
			GWN_BATCH_DISCARD_SAFE(cache->batch_stroke[i]);
			GWN_BATCH_DISCARD_SAFE(cache->batch_fill[i]);
			GWN_BATCH_DISCARD_SAFE(cache->batch_edit[i]);
			GWN_BATCH_DISCARD_SAFE(cache->batch_edlin[i]);
		}
		MEM_SAFE_FREE(cache->batch_stroke);
		MEM_SAFE_FREE(cache->batch_fill);
		MEM_SAFE_FREE(cache->batch_edit);
		MEM_SAFE_FREE(cache->batch_edlin);
	}

	MEM_SAFE_FREE(cache);
}

/* get cache */
static GpencilBatchCache *gpencil_batch_cache_get(Object *ob, int cfra)
{
	bGPdata *gpd = ob->data;

	if (!gpencil_batch_cache_valid(ob, gpd, cfra)) {
		if (G.debug_value >= 664) {
			printf("gpencil_batch_cache: %s\n", gpd->id.name);
		}

		GpencilBatchCache *cache = gpencil_batch_get_element(ob);
		if (cache) {
			gpencil_batch_cache_clear(cache, gpd);
			BLI_ghash_remove(gpd->batch_cache_data, ob->id.name, NULL, NULL);
		}
		gpencil_batch_cache_init(ob, cfra);
	}

	return gpencil_batch_get_element(ob);
}

/* create shading group for filling */
static DRWShadingGroup *DRW_gpencil_shgroup_fill_create(GPENCIL_e_data *e_data, GPENCIL_Data *vedata, DRWPass *pass, 
	GPUShader *shader, bGPdata *gpd, PaletteColor *palcolor, int id)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

	/* e_data.gpencil_fill_sh */
	DRWShadingGroup *grp = DRW_shgroup_create(shader, pass);
	stl->g_data->tot_sh++;
	stl->g_data->tot_sh_fill++;

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
	DRW_shgroup_uniform_vec2(grp, "t_offset", palcolor->t_offset, 1);
	DRW_shgroup_uniform_float(grp, "t_opacity", &palcolor->t_opacity, 1);

	stl->shgroups[id].t_mix = palcolor->flag & PAC_COLOR_TEX_MIX ? 1 : 0;
	DRW_shgroup_uniform_int(grp, "t_mix", &stl->shgroups[id].t_mix, 1);

	stl->shgroups[id].t_flip = palcolor->flag & PAC_COLOR_FLIP_FILL ? 1 : 0;
	DRW_shgroup_uniform_int(grp, "t_flip", &stl->shgroups[id].t_flip, 1);

	DRW_shgroup_uniform_int(grp, "xraymode", (const int *) &gpd->xray_mode, 1);
	/* image texture */
	if ((palcolor->fill_style == FILL_STYLE_TEXTURE) || (palcolor->fill_style == FILL_STYLE_PATTERN) || (palcolor->flag & PAC_COLOR_TEX_MIX)) {
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
	else {
		/* if no texture defined, need a blank texture to avoid errors in draw manager */
		DRW_shgroup_uniform_texture(grp, "myTexture", e_data->gpencil_blank_texture);
		stl->shgroups[id].t_clamp = 0;
		DRW_shgroup_uniform_int(grp, "t_clamp", &stl->shgroups[id].t_clamp, 1);
	}

	return grp;
}

/* create shading group for strokes */
DRWShadingGroup *DRW_gpencil_shgroup_stroke_create(GPENCIL_e_data *e_data, GPENCIL_Data *vedata, DRWPass *pass, GPUShader *shader, Object *ob,
	bGPdata *gpd, PaletteColor *palcolor, int id)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	const float *viewport_size = DRW_viewport_size_get();

	/* e_data.gpencil_stroke_sh */
	DRWShadingGroup *grp = DRW_shgroup_create(shader, pass);
	stl->g_data->tot_sh++;
	if (id != -1) {
		stl->g_data->tot_sh_stroke++;
	}

	DRW_shgroup_uniform_vec2(grp, "Viewport", viewport_size, 1);

	DRW_shgroup_uniform_float(grp, "pixsize", stl->storage->pixsize, 1);
	DRW_shgroup_uniform_float(grp, "pixelsize", &U.pixelsize, 1);

	/* avoid wrong values */
	if ((gpd) && (gpd->pixfactor == 0)) {
		gpd->pixfactor = GP_DEFAULT_PIX_FACTOR;
	}

	/* object scale and depth */
	if ((ob) && (id > -1)) {
		stl->shgroups[id].obj_scale = (ob->size[0] + ob->size[1] + ob->size[2]) / 3.0f;
		DRW_shgroup_uniform_float(grp, "objscale", &stl->shgroups[id].obj_scale, 1);
		stl->shgroups[id].keep_size = (int)((gpd) && (gpd->flag & GP_DATA_STROKE_KEEPTHICKNESS));
		DRW_shgroup_uniform_int(grp, "keep_size", &stl->shgroups[id].keep_size, 1);

		stl->shgroups[id].stroke_style = palcolor->stroke_style;
		stl->shgroups[id].color_type = GPENCIL_COLOR_SOLID;
		if (palcolor->stroke_style == STROKE_STYLE_TEXTURE) {
			stl->shgroups[id].color_type = GPENCIL_COLOR_TEXTURE;
			if (palcolor->flag & PAC_COLOR_PATTERN) {
				stl->shgroups[id].color_type = GPENCIL_COLOR_PATTERN;
			}
		}
		DRW_shgroup_uniform_int(grp, "color_type", &stl->shgroups[id].color_type, 1);
		DRW_shgroup_uniform_int(grp, "pixfactor", &gpd->pixfactor, 1);
	}
	else {
		stl->storage->obj_scale = 1.0f;
		stl->storage->keep_size = 0;
		stl->storage->pixfactor = GP_DEFAULT_PIX_FACTOR;
		DRW_shgroup_uniform_float(grp, "objscale", &stl->storage->obj_scale, 1);
		DRW_shgroup_uniform_int(grp, "keep_size", &stl->storage->keep_size, 1);
		DRW_shgroup_uniform_int(grp, "color_type", &stl->storage->color_type, 1);
		if (gpd) {
			DRW_shgroup_uniform_int(grp, "pixfactor", &gpd->pixfactor, 1);
		}
		else {
			DRW_shgroup_uniform_int(grp, "pixfactor", &stl->storage->pixfactor, 1);
		}
	}

	if ((gpd) && (id > -1)) {
		DRW_shgroup_uniform_int(grp, "xraymode", (const int *) &gpd->xray_mode, 1);
	}
	else {
		/* for drawing always on front */
		DRW_shgroup_uniform_int(grp, "xraymode", &stl->storage->xray, 1);
	}

	/* image texture for pattern */
	if ((palcolor) && (palcolor->stroke_style == STROKE_STYLE_TEXTURE)) {
		ImBuf *ibuf;
		Image *image = palcolor->sima;
		ImageUser iuser = { NULL };
		void *lock;

		iuser.ok = true;

		ibuf = BKE_image_acquire_ibuf(image, &iuser, &lock);

		if (ibuf == NULL || ibuf->rect == NULL) {
			BKE_image_release_ibuf(image, ibuf, NULL);
		}
		else {
			GPUTexture *texture = GPU_texture_from_blender(palcolor->sima, &iuser, GL_TEXTURE_2D, true, 0.0, 0);
			DRW_shgroup_uniform_texture(grp, "myTexture", texture);

			BKE_image_release_ibuf(image, ibuf, NULL);
		}
	}
	else {
		/* if no texture defined, need a blank texture to avoid errors in draw manager */
		DRW_shgroup_uniform_texture(grp, "myTexture", e_data->gpencil_blank_texture);
	}


	return grp;
}

/* create shading group for volumetrics */
DRWShadingGroup *DRW_gpencil_shgroup_point_create(GPENCIL_e_data *e_data, GPENCIL_Data *vedata, DRWPass *pass, GPUShader *shader, Object *ob,
	bGPdata *gpd, PaletteColor *palcolor, int id)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	const float *viewport_size = DRW_viewport_size_get();

	/* e_data.gpencil_stroke_sh */
	DRWShadingGroup *grp = DRW_shgroup_create(shader, pass);
	stl->g_data->tot_sh++;
	stl->g_data->tot_sh_point++;

	DRW_shgroup_uniform_vec2(grp, "Viewport", viewport_size, 1);
	DRW_shgroup_uniform_float(grp, "pixsize", stl->storage->pixsize, 1);
	DRW_shgroup_uniform_float(grp, "pixelsize", &U.pixelsize, 1);
	
	/* avoid wrong values */
	if ((gpd) && (gpd->pixfactor == 0)) {
		gpd->pixfactor = GP_DEFAULT_PIX_FACTOR;
	}

	/* object scale and depth */
	if ((ob) && (id > -1)) {
		stl->shgroups[id].obj_scale = (ob->size[0] + ob->size[1] + ob->size[2]) / 3.0f;
		DRW_shgroup_uniform_float(grp, "objscale", &stl->shgroups[id].obj_scale, 1);
		stl->shgroups[id].keep_size = (int)((gpd) && (gpd->flag & GP_DATA_STROKE_KEEPTHICKNESS));
		DRW_shgroup_uniform_int(grp, "keep_size", &stl->shgroups[id].keep_size, 1);

		stl->shgroups[id].stroke_style = palcolor->stroke_style;
		stl->shgroups[id].color_type = GPENCIL_COLOR_SOLID;
		if (palcolor->stroke_style == STROKE_STYLE_TEXTURE) {
			stl->shgroups[id].color_type = GPENCIL_COLOR_TEXTURE;
			if (palcolor->flag & PAC_COLOR_PATTERN) {
				stl->shgroups[id].color_type = GPENCIL_COLOR_PATTERN;
			}
		}
		DRW_shgroup_uniform_int(grp, "color_type", &stl->shgroups[id].color_type, 1);
		DRW_shgroup_uniform_int(grp, "pixfactor", &gpd->pixfactor, 1);
	}
	else {
		stl->storage->obj_scale = 1.0f;
		stl->storage->keep_size = 0;
		stl->storage->pixfactor = GP_DEFAULT_PIX_FACTOR;
		DRW_shgroup_uniform_float(grp, "objscale", &stl->storage->obj_scale, 1);
		DRW_shgroup_uniform_int(grp, "keep_size", &stl->storage->keep_size, 1);
		DRW_shgroup_uniform_int(grp, "color_type", &stl->storage->color_type, 1);
		if (gpd) {
			DRW_shgroup_uniform_int(grp, "pixfactor", &gpd->pixfactor, 1);
		}
		else {
			DRW_shgroup_uniform_int(grp, "pixfactor", &stl->storage->pixfactor, 1);
		}
	}

	if (gpd) {
		DRW_shgroup_uniform_int(grp, "xraymode", (const int *)&gpd->xray_mode, 1);
	}
	else {
		/* for drawing always on front */
		DRW_shgroup_uniform_int(grp, "xraymode", &stl->storage->xray, 1);
	}

	/* image texture */
	if ((palcolor) && (palcolor->stroke_style == STROKE_STYLE_TEXTURE)) {
		ImBuf *ibuf;
		Image *image = palcolor->sima;
		ImageUser iuser = { NULL };
		void *lock;

		iuser.ok = true;

		ibuf = BKE_image_acquire_ibuf(image, &iuser, &lock);

		if (ibuf == NULL || ibuf->rect == NULL) {
			BKE_image_release_ibuf(image, ibuf, NULL);
		}
		else {
			GPUTexture *texture = GPU_texture_from_blender(palcolor->sima, &iuser, GL_TEXTURE_2D, true, 0.0, 0);
			DRW_shgroup_uniform_texture(grp, "myTexture", texture);

			BKE_image_release_ibuf(image, ibuf, NULL);
		}
	}
	else {
		/* if no texture defined, need a blank texture to avoid errors in draw manager */
		DRW_shgroup_uniform_texture(grp, "myTexture", e_data->gpencil_blank_texture);
	}

	return grp;
}

/* add fill shading group to pass */
static void gpencil_add_fill_shgroup(GpencilBatchCache *cache, DRWShadingGroup *fillgrp, 
	Object *ob, bGPdata *gpd, bGPDlayer *gpl, bGPDframe *gpf, bGPDstroke *gps,
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
				gpencil_batch_cache_check_free_slots(ob);
				cache->batch_fill[cache->cache_idx] = DRW_gpencil_get_fill_geom(gps, color);
			}
			DRW_shgroup_call_add(fillgrp, cache->batch_fill[cache->cache_idx], gpf->viewmatrix);
		}
	}
}

/* add stroke shading group to pass */
static void gpencil_add_stroke_shgroup(GpencilBatchCache *cache, DRWShadingGroup *strokegrp,
	Object *ob, bGPdata *gpd, bGPDlayer *gpl, bGPDframe *gpf, bGPDstroke *gps,
	const float opacity, const float tintcolor[4], const bool onion, const bool custonion)
{
	float tcolor[4];
	float ink[4];
	short sthickness;

	/* set color using palette, tint color and opacity */
	if (!onion) {
		/* if special stroke, use fill color as stroke color */
		if (gps->flag & GP_STROKE_NOFILL) {
			interp_v3_v3v3(tcolor, gps->palcolor->fill, tintcolor, tintcolor[3]);
			tcolor[3] = gps->palcolor->fill[3] * opacity;
		}
		else {
			interp_v3_v3v3(tcolor, gps->palcolor->rgb, tintcolor, tintcolor[3]);
			tcolor[3] = gps->palcolor->rgb[3] * opacity;
		}
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

	sthickness = gps->thickness + gpl->thickness;
	CLAMP_MIN(sthickness, 1);
	if (cache->is_dirty) {
		gpencil_batch_cache_check_free_slots(ob);
		if ((gps->totpoints > 1) && ((gps->palcolor->flag & PAC_COLOR_DOT) == 0)) {
			cache->batch_stroke[cache->cache_idx] = DRW_gpencil_get_stroke_geom(gpf, gps, sthickness, ink);
		}
		else {
			cache->batch_stroke[cache->cache_idx] = DRW_gpencil_get_point_geom(gps, sthickness, ink);
		}
	}
	DRW_shgroup_call_add(strokegrp, cache->batch_stroke[cache->cache_idx], gpf->viewmatrix);
}

/* add edit points shading group to pass */
static void gpencil_add_editpoints_shgroup(
		GPENCIL_StorageList *stl, GpencilBatchCache *cache,ToolSettings *ts, Object *ob, 
		bGPdata *gpd, bGPDlayer *gpl, bGPDframe *gpf, bGPDstroke *gps)
{
	if (GPENCIL_ANY_EDIT_MODE(gpd)) {
		const DRWContextState *draw_ctx = DRW_context_state_get();
		Object *obact = draw_ctx->obact;
		if ((!obact) || (obact->type != OB_GPENCIL)) {
			return;
		}
		bool is_weight_paint = (gpd) && (gpd->flag & GP_DATA_STROKE_WEIGHTMODE);

		/* line of the original stroke */
		if (cache->is_dirty) {
			gpencil_batch_cache_check_free_slots(ob);
			cache->batch_edlin[cache->cache_idx] = DRW_gpencil_get_edlin_geom(gps, ts->gp_sculpt.alpha, gpd->flag);
		}
		if (cache->batch_edlin[cache->cache_idx]) {
			if ((obact) && (obact == ob) && (gpd->flag & GP_DATA_STROKE_SHOW_EDIT_LINES)) {
				DRW_shgroup_call_add(stl->g_data->shgrps_edit_line, cache->batch_edlin[cache->cache_idx], gpf->viewmatrix);
			}
		}
		/* edit points */
		if ((gps->flag & GP_STROKE_SELECT) || (is_weight_paint)) {
			if ((gpl->flag & GP_LAYER_UNLOCK_COLOR) || ((gps->palcolor->flag & PC_COLOR_LOCKED) == 0)) {
				if (cache->is_dirty) {
					gpencil_batch_cache_check_free_slots(ob);
					cache->batch_edit[cache->cache_idx] = DRW_gpencil_get_edit_geom(gps, ts->gp_sculpt.alpha, gpd->flag);
				}
				if (cache->batch_edit[cache->cache_idx]) {
					if ((obact) && (obact == ob)) {
						/* edit pass */
						DRW_shgroup_call_add(stl->g_data->shgrps_edit_point, cache->batch_edit[cache->cache_idx], gpf->viewmatrix);
					}
				}
			}
		}
	}
}

/* function to draw strokes for onion only */
static void gpencil_draw_onion_strokes(GpencilBatchCache *cache, GPENCIL_e_data *e_data, void *vedata, Object *ob,
	bGPdata *gpd, bGPDlayer *gpl, bGPDframe *gpf, const float opacity, const float tintcolor[4], const bool custonion)
{
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	float viewmatrix[4][4];

	/* get parent matrix and save as static data */
	ED_gpencil_parent_location(ob, gpd, gpl, viewmatrix);
	copy_m4_m4(gpf->viewmatrix, viewmatrix);

	for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
		int id = stl->storage->shgroup_id;
		/* check if stroke can be drawn */
		if (gpencil_can_draw_stroke(gps, true) == false) {
			continue;
		}
		/* limit the number of shading groups */
		if (id >= GPENCIL_MAX_SHGROUPS) {
			continue;
		}

		stl->shgroups[id].shgrps_fill = NULL;
		if ((gps->totpoints > 1) && ((gps->palcolor->flag & PAC_COLOR_DOT) == 0)) {
			stl->shgroups[id].shgrps_stroke = DRW_gpencil_shgroup_stroke_create(e_data, vedata, psl->stroke_pass, e_data->gpencil_stroke_sh, ob, gpd, gps->palcolor, id);
		}
		else {
			stl->shgroups[id].shgrps_stroke = DRW_gpencil_shgroup_point_create(e_data, vedata, psl->stroke_pass, e_data->gpencil_point_sh, ob, gpd, gps->palcolor, id);
		}

		/* stroke */
		gpencil_add_stroke_shgroup(cache, stl->shgroups[id].shgrps_stroke, ob, gpd, gpl, gpf, gps, opacity, tintcolor, true, custonion);

		stl->storage->shgroup_id++;
		cache->cache_idx++;
	}
}


/* Construct eval_ctx from draw_ctx state
 * Note: This function copies CTX_data_eval_ctx() / DEG_evaluation_context_init_from_scene()
 */
static void gpencil_init_evalctx_from_drawctx(const DRWContextState *draw_ctx, EvaluationContext *eval_ctx)
{
	Scene *scene = draw_ctx->scene;
	
	eval_ctx->depsgraph = BKE_scene_get_depsgraph(scene, draw_ctx->view_layer, false);
	eval_ctx->ctime = BKE_scene_frame_get(scene);
	eval_ctx->view_layer = draw_ctx->view_layer;
	eval_ctx->engine_type = draw_ctx->engine_type;
}

/* main function to draw strokes */
static void gpencil_draw_strokes(GpencilBatchCache *cache, GPENCIL_e_data *e_data, void *vedata, ToolSettings *ts, Object *ob,
	bGPdata *gpd, bGPDlayer *gpl, bGPDframe *src_gpf, bGPDframe *derived_gpf,
	const float opacity, const float tintcolor[4], const bool custonion)
{
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	bGPDstroke *gps, *src_gps;
	DRWShadingGroup *fillgrp;
	DRWShadingGroup *strokegrp;
	float viewmatrix[4][4];
	bool is_multiedit = (bool)GPENCIL_MULTIEDIT_SESSIONS_ON(gpd);
	bool playing = (bool)stl->storage->playing;
	bool is_render = (bool)stl->storage->is_render;

	/* Get evaluation context */
	/* NOTE: We must check if C is valid, otherwise we get crashes when trying to save files
	 * (i.e. the thumbnail offscreen rendering fails) 
	 */
	const DRWContextState *draw_ctx = DRW_context_state_get();
	const bContext *C = draw_ctx->evil_C;

	EvaluationContext eval_ctx = {0};
	if (C) {
		CTX_data_eval_ctx(C, &eval_ctx);
	}
	else {
		gpencil_init_evalctx_from_drawctx(draw_ctx, &eval_ctx);
	}

	/* get parent matrix and save as static data */
	ED_gpencil_parent_location(ob, gpd, gpl, viewmatrix);
	copy_m4_m4(derived_gpf->viewmatrix, viewmatrix);

	/* apply geometry modifiers */
	if ((cache->is_dirty) && (ob->modifiers.first) && (!is_multiedit)) {
		if (!GP_SIMPLIFY_MODIF(ts, playing)) {
			if (BKE_gpencil_has_geometry_modifiers(ob)) {
				BKE_gpencil_geometry_modifiers(&eval_ctx, ob, gpl, derived_gpf, stl->storage->is_render);
			}
		}
	}

	if (src_gpf) {
		src_gps = src_gpf->strokes.first;
	}
	else {
		src_gps = NULL;
	}

	for (gps = derived_gpf->strokes.first; gps; gps = gps->next) {
		/* check if stroke can be drawn */
		if (gpencil_can_draw_stroke(gps, false) == false) {
			continue;
		}
		/* limit the number of shading groups */
		if (stl->storage->shgroup_id >= GPENCIL_MAX_SHGROUPS) {
			continue;
		}
		/* if the fill has any value, it's considered a fill and is not drawn if simplify fill is enabled */
		if ((GP_SIMPLIFY_FILL(ts, playing)) && (ts->gpencil_simplify & GP_TOOL_FLAG_SIMPLIFY_REMOVE_LINE)) {
			if ((gps->palcolor->fill[3] > GPENCIL_ALPHA_OPACITY_THRESH) || 
			    (gps->palcolor->fill_style > FILL_STYLE_SOLID))
			{
				continue;
			}
		}

#if 0   /* if we use the reallocate the shading group is doing weird thing, so disable while find a solution 
		   and allocate the max size on cache_init */
		   /* realloc memory */
		GPENCIL_shgroup *p = NULL;
		int size = stl->storage->shgroup_id + 1;
		p = MEM_recallocN(stl->shgroups, sizeof(struct GPENCIL_shgroup) * size);
		if (p != NULL) {
			stl->shgroups = p;
		}
#endif
		if ((gpl->actframe->framenum == derived_gpf->framenum) || (!is_multiedit) || ((gpd->flag & GP_DATA_STROKE_MULTIEDIT_LINES) == 0)) {
			int id = stl->storage->shgroup_id;
			if ((gps->totpoints > 1) && ((gps->palcolor->flag & PAC_COLOR_DOT) == 0)) {
				if ((gps->totpoints > 2) && (!GP_SIMPLIFY_FILL(ts, playing)) && 
					((gps->palcolor->fill[3] > GPENCIL_ALPHA_OPACITY_THRESH) || (gps->palcolor->fill_style > 0)) &&
					((gps->flag & GP_STROKE_NOFILL) == 0))
				{
					stl->shgroups[id].shgrps_fill = DRW_gpencil_shgroup_fill_create(e_data, vedata, psl->stroke_pass, e_data->gpencil_fill_sh, gpd, gps->palcolor, id);
				}
				else {
					stl->shgroups[id].shgrps_fill = NULL;
				}
				stl->shgroups[id].shgrps_stroke = DRW_gpencil_shgroup_stroke_create(e_data, vedata, psl->stroke_pass, e_data->gpencil_stroke_sh, ob, gpd, gps->palcolor, id);
			}
			else {
				stl->shgroups[id].shgrps_fill = NULL;
				if (gps->totpoints > 0) {
					stl->shgroups[id].shgrps_stroke = DRW_gpencil_shgroup_point_create(e_data, vedata, psl->stroke_pass, e_data->gpencil_point_sh, ob, gpd, gps->palcolor, id);
				}
				else {
					stl->shgroups[id].shgrps_stroke = NULL;
				}
			}
			stl->storage->shgroup_id++;

			fillgrp = stl->shgroups[id].shgrps_fill;
			strokegrp = stl->shgroups[id].shgrps_stroke;

			/* apply modifiers (only modify geometry, but not create ) */
			if ((cache->is_dirty) && (ob->modifiers.first) && (!is_multiedit)) {
				if (!GP_SIMPLIFY_MODIF(ts, playing)) {
					BKE_gpencil_stroke_modifiers(&eval_ctx, ob, gpl, derived_gpf, gps, stl->storage->is_render);
				}
			}
			/* fill */
			if ((fillgrp) && (!GP_SIMPLIFY_FILL(ts, playing))) {
				gpencil_add_fill_shgroup(cache, fillgrp, ob, gpd, gpl, derived_gpf, gps, tintcolor, false, custonion);
			}
			/* stroke */
			if (strokegrp) {
				gpencil_add_stroke_shgroup(cache, strokegrp, ob, gpd, gpl, derived_gpf, gps, opacity, tintcolor, false, custonion);
			}
		}

		/* edit points (only in edit mode and not play animation not render) */
		if ((src_gps) && (!playing) && (!is_render)) {
			if (!stl->g_data->shgrps_edit_line) {
				stl->g_data->shgrps_edit_line = DRW_shgroup_create(e_data->gpencil_line_sh, psl->edit_pass);
				stl->g_data->tot_sh++;
			}
			if (!stl->g_data->shgrps_edit_point) {
				stl->g_data->shgrps_edit_point = DRW_shgroup_create(e_data->gpencil_edit_point_sh, psl->edit_pass);
				const float *viewport_size = DRW_viewport_size_get();
				DRW_shgroup_uniform_vec2(stl->g_data->shgrps_edit_point, "Viewport", viewport_size, 1);
				stl->g_data->tot_sh++;
			}

			gpencil_add_editpoints_shgroup(stl, cache, ts, ob, gpd, gpl, derived_gpf, src_gps);
		}

		if (src_gps) {
			src_gps = src_gps->next;
		}

		cache->cache_idx++;
	}
}

 /* draw stroke in drawing buffer */
void DRW_gpencil_populate_buffer_strokes(GPENCIL_e_data *e_data, void *vedata, ToolSettings *ts, Object *ob)
{
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	bGPDbrush *brush = BKE_gpencil_brush_getactive(ts);
	bGPdata *gpd = ob->data;
	float obscale = (ob->size[0] + ob->size[1] + ob->size[2]) / 3.0f;

	bGPDpaletteref *palslot = BKE_gpencil_paletteslot_get_active(gpd);
	PaletteColor *palcolor = BKE_palette_color_get_active((palslot) ? palslot->palette : NULL);
	
	/* drawing strokes */
	/* Check if may need to draw the active stroke cache, only if this layer is the active layer
	 * that is being edited. (Stroke buffer is currently stored in gp-data)
	 */
	if (ED_gpencil_session_active() && (gpd->sbuffer_size > 0)) {
		if ((gpd->sbuffer_sflag & GP_STROKE_ERASER) == 0) {
			/* It should also be noted that sbuffer contains temporary point types
			* i.e. tGPspoints NOT bGPDspoints
			*/
			short lthick = brush->thickness * obscale;
			/* if only one point, don't need to draw buffer because the user has no time to see it */
			if (gpd->sbuffer_size > 1) {
				if ((palcolor) && (palcolor->flag & PAC_COLOR_DOT) == 0) {
					stl->g_data->shgrps_drawing_stroke = DRW_gpencil_shgroup_stroke_create(e_data, vedata, psl->drawing_pass, e_data->gpencil_stroke_sh, NULL, gpd, palcolor, -1);
				}
				else {
					stl->g_data->shgrps_drawing_stroke = DRW_gpencil_shgroup_point_create(e_data, vedata, psl->drawing_pass, e_data->gpencil_point_sh, NULL, gpd, palcolor, -1);
				}

				/* use unit matrix because the buffer is in screen space and does not need conversion */
				if ((gpd->sflag & PAC_COLOR_DOT) == 0) {
					stl->g_data->batch_buffer_stroke = DRW_gpencil_get_buffer_stroke_geom(gpd, stl->storage->unit_matrix, lthick);
				}
				else {
					stl->g_data->batch_buffer_stroke = DRW_gpencil_get_buffer_point_geom(gpd, stl->storage->unit_matrix, lthick);
				}

				DRW_shgroup_call_add(stl->g_data->shgrps_drawing_stroke, stl->g_data->batch_buffer_stroke, stl->storage->unit_matrix);

				if ((gpd->sbuffer_size >= 3) && (gpd->sfill[3] > GPENCIL_ALPHA_OPACITY_THRESH) && 
					((gpd->sbuffer_sflag & GP_STROKE_NOFILL) == 0)) 
				{
					/* if not solid, fill is simulated with solid color */
					if (gpd->bfill_style > 0) {
						gpd->sfill[3] = 0.5f;
					}
					stl->g_data->shgrps_drawing_fill = DRW_shgroup_create(e_data->gpencil_drawing_fill_sh, psl->drawing_pass);
					stl->g_data->tot_sh++;
					stl->g_data->batch_buffer_fill = DRW_gpencil_get_buffer_fill_geom(gpd);
					DRW_shgroup_call_add(stl->g_data->shgrps_drawing_fill, stl->g_data->batch_buffer_fill, stl->storage->unit_matrix);
				}
			}
		}
	}
}

/* get alpha factor for onion strokes */
static void gpencil_get_onion_alpha(float color[4], bGPdata *gpd, bGPDlayer *gpl)
{
	#define MIN_ALPHA_VALUE 0.01f

	/* if fade is disabled, opacity is equal in all frames */
	if (gpl->onion_flag & GP_LAYER_ONION_OVERRIDE) {
		if ((gpl->onion_flag & GP_ONION_FADE) == 0) {
			color[3] = gpl->onion_factor;
		}
		else {
			/* add override opacity factor */
			color[3] += gpl->onion_factor - 0.5f;
		}
	}
	else {
		if ((gpd->onion_flag & GP_ONION_FADE) == 0) {
			color[3] = gpd->onion_factor;
		}
		else {
			/* add override opacity factor */
			color[3] += gpd->onion_factor - 0.5f;
		}
	}

	CLAMP(color[3], MIN_ALPHA_VALUE, 1.0f);
}

/* draw onion-skinning for a layer */
static void gpencil_draw_onionskins(
        GpencilBatchCache *cache, GPENCIL_e_data *e_data, void *vedata,
        Object *ob, bGPdata *gpd, bGPDlayer *gpl, bGPDframe *gpf)
{

	const float default_color[3] = { UNPACK3(U.gpencil_new_layer_col) };
	const float alpha = 1.0f;
	float color[4];
	int idx;
	float fac = 1.0f;
	int step = 0;
	int mode = 0;
	bool colflag = false;
	
	if (gpl->onion_flag & GP_LAYER_ONION_OVERRIDE) {
		if (gpl->onion_flag & GP_LAYER_GHOST_PREVCOL) {
			colflag = true;
		}
	}
	else {
		colflag = (bool)gpd->onion_flag & GP_ONION_GHOST_PREVCOL;
	}

	/* -------------------------------
	 * 1) Draw Previous Frames First
	 * ------------------------------- */
	if (gpl->onion_flag & GP_LAYER_ONION_OVERRIDE) {
		step = gpl->gstep;
		mode = gpl->onion_mode;

		if (gpl->onion_flag & GP_LAYER_GHOST_PREVCOL) {
			copy_v3_v3(color, gpl->gcolor_prev);
		}
		else {
			copy_v3_v3(color, default_color);
		}
	}
	else {
		step = gpd->gstep;
		mode = gpd->onion_mode;

		if (gpd->onion_flag & GP_ONION_GHOST_PREVCOL) {
			copy_v3_v3(color, gpd->gcolor_prev);
		}
		else {
			copy_v3_v3(color, default_color);
		}
	}

	idx = 0;
	for (bGPDframe *gf = gpf->prev; gf; gf = gf->prev) {
		/* only selected frames */
		if ((mode == GP_ONION_MODE_SELECTED) && ((gf->flag & GP_FRAME_SELECT) == 0)) {
			continue;
		}
		/* absolute range */
		if (mode == GP_ONION_MODE_ABSOLUTE) {
			if ((gpf->framenum - gf->framenum) > step) {
				break;
			}
		}
		/* relative range */
		if (mode == GP_ONION_MODE_RELATIVE) {
			idx++;
			if (idx > step) {
				break;
			}

		}
		/* alpha decreases with distance from curframe index */
		if (mode != GP_ONION_MODE_SELECTED) {
			if (mode == GP_ONION_MODE_ABSOLUTE) {
				fac = 1.0f - ((float)(gpf->framenum - gf->framenum) / (float)(step + 1));
			}
			else {
				fac = 1.0f - ((float)idx / (float)(step + 1));
			}
			color[3] = alpha * fac * 0.66f;
		}
		else {
			idx++;
			fac = alpha - ((1.1f - (1.0f / (float)idx)) * 0.66f);
			color[3] = fac;
		}

		gpencil_get_onion_alpha(color, gpd, gpl);
		gpencil_draw_onion_strokes(cache, e_data, vedata, ob, gpd, gpl, gf, color[3], color, colflag);
	}
	/* -------------------------------
	 * 2) Now draw next frames
	 * ------------------------------- */
	if (gpl->onion_flag & GP_LAYER_ONION_OVERRIDE) {
		step = gpl->gstep_next;
		mode = gpl->onion_mode;

		if (gpl->onion_flag & GP_LAYER_GHOST_NEXTCOL) {
			copy_v3_v3(color, gpl->gcolor_next);
		}
		else {
			copy_v3_v3(color, default_color);
		}
	}
	else {
		step = gpd->gstep_next;
		mode = gpd->onion_mode;

		if (gpd->onion_flag & GP_ONION_GHOST_NEXTCOL) {
			copy_v3_v3(color, gpd->gcolor_next);
		}
		else {
			copy_v3_v3(color, default_color);
		}
	}

	idx = 0;
	for (bGPDframe *gf = gpf->next; gf; gf = gf->next) {
		/* only selected frames */
		if ((mode == GP_ONION_MODE_SELECTED) && ((gf->flag & GP_FRAME_SELECT) == 0)) {
			continue;
		}
		/* absolute range */
		if (mode == GP_ONION_MODE_ABSOLUTE) {
			if ((gf->framenum - gpf->framenum) > step) {
				break;
			}
		}
		/* relative range */
		if (mode == GP_ONION_MODE_RELATIVE) {
			idx++;
			if (idx > step) {
				break;
			}

		}
		/* alpha decreases with distance from curframe index */
		if (mode != GP_ONION_MODE_SELECTED) {
			if (mode == GP_ONION_MODE_ABSOLUTE) {
				fac = 1.0f - ((float)(gf->framenum - gpf->framenum) / (float)(step + 1));
			}
			else {
				fac = 1.0f - ((float)idx / (float)(step + 1));
			}
			color[3] = alpha * fac * 0.66f;
		}
		else {
			idx++;
			fac = alpha - ((1.1f - (1.0f / (float)idx)) * 0.66f);
			color[3] = fac;
		}

		gpencil_get_onion_alpha(color, gpd, gpl);
		gpencil_draw_onion_strokes(cache, e_data, vedata, ob, gpd, gpl, gf, color[3], color, colflag);
	}
}

/* populate a datablock for multiedit (no onions, no modifiers) */
void DRW_gpencil_populate_multiedit(GPENCIL_e_data *e_data, void *vedata, Scene *scene, Object *ob, ToolSettings *ts, bGPdata *gpd)
{
	bGPDframe *gpf = NULL;

	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	GpencilBatchCache *cache = gpencil_batch_cache_get(ob, CFRA);
	cache->cache_idx = 0;

	/* check if playing animation */
	bool playing = (bool)stl->storage->playing;

	/* draw strokes */
	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		/* don't draw layer if hidden */
		if (gpl->flag & GP_LAYER_HIDE)
			continue;

		/* list of frames to draw */
		if (!playing) {
			for (gpf = gpl->frames.first; gpf; gpf = gpf->next) {
				if ((gpf == gpl->actframe) || (gpf->flag & GP_FRAME_SELECT)) {
					gpencil_draw_strokes(cache, e_data, vedata, ts, ob, gpd, gpl, gpf, gpf,
						gpl->opacity, gpl->tintcolor, false);
				}
			}
		}
		else {
			gpf = BKE_gpencil_layer_getframe(gpl, CFRA, 0);
			if (gpf) {
				gpencil_draw_strokes(cache, e_data, vedata, ts, ob, gpd, gpl, gpf, gpf,
					gpl->opacity, gpl->tintcolor, false);
			}
		}

	}

	cache->is_dirty = false;
}

/* helper for populate a complete grease pencil datablock */
void DRW_gpencil_populate_datablock(GPENCIL_e_data *e_data, void *vedata, Scene *scene, Object *ob, ToolSettings *ts, bGPdata *gpd)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	const DRWContextState *draw_ctx = DRW_context_state_get();
	const bContext *C = draw_ctx->evil_C;
	bGPDframe *derived_gpf = NULL;
	bool no_onion = (bool)(gpd->flag & GP_DATA_STROKE_WEIGHTMODE);

	if (G.debug_value == 665) {
		printf("DRW_gpencil_populate_datablock: %s\n", gpd->id.name);
	}

	/* check if playing animation */
	bool playing = (bool)stl->storage->playing;

	GpencilBatchCache *cache = gpencil_batch_cache_get(ob, CFRA);
	cache->cache_idx = 0;

	/* init general modifiers data */
	if (!GP_SIMPLIFY_MODIF(ts, playing)) {
		if ((cache->is_dirty) && (ob->modifiers.first)) {
			BKE_gpencil_lattice_init(ob);
		}
	}
	/* draw normal strokes */
	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		/* don't draw layer if hidden */
		if (gpl->flag & GP_LAYER_HIDE)
			continue;

		bGPDframe *gpf = BKE_gpencil_layer_getframe(gpl, CFRA, 0);
		if (gpf == NULL)
			continue;

		/* create GHash if need */
		if (gpl->derived_data == NULL) {
			gpl->derived_data = (GHash *)BLI_ghash_str_new(gpl->info);
		}

		derived_gpf = (bGPDframe *)BLI_ghash_lookup(gpl->derived_data, ob->id.name);
		if (derived_gpf == NULL) {
			cache->is_dirty = true;
		}
		if (cache->is_dirty) {
			if (derived_gpf != NULL) {
				/* first clear temp data */
				BKE_gpencil_free_layer_temp_data(gpl, derived_gpf);
				BLI_ghash_remove(gpl->derived_data, ob->id.name, NULL, NULL);
			}
			/* create new data */
			derived_gpf = BKE_gpencil_frame_color_duplicate(C, gpf);
			BLI_ghash_insert(gpl->derived_data, ob->id.name, derived_gpf);
		}

		/* draw onion skins */
		if ((gpd->flag & GP_DATA_SHOW_ONIONSKINS) && (!no_onion) &&
		    (gpl->onion_flag & GP_LAYER_ONIONSKIN) &&
		    ((!playing) || (gpd->onion_flag & GP_ONION_GHOST_ALWAYS)))
		{
			gpencil_draw_onionskins(cache, e_data, vedata, ob, gpd, gpl, gpf);
		}

		/* draw normal strokes */
		gpencil_draw_strokes(cache, e_data, vedata, ts, ob, gpd, gpl, gpf, derived_gpf,
			gpl->opacity, gpl->tintcolor, false);

	}

	/* clear any lattice data */
	if ((cache->is_dirty) && (ob->modifiers.first)) {
		BKE_gpencil_lattice_clear(ob);
	}

	cache->is_dirty = false;
}

void DRW_gpencil_batch_cache_dirty(bGPdata *gpd)
{
	if (gpd->batch_cache_data == NULL) {
		return;
	}

	GHashIterator *ihash = BLI_ghashIterator_new(gpd->batch_cache_data);
	while (!BLI_ghashIterator_done(ihash)) {
		GpencilBatchCache *cache = (GpencilBatchCache *)BLI_ghashIterator_getValue(ihash);
		if (cache) {
			cache->is_dirty = true;
		}
		BLI_ghashIterator_step(ihash);
	}
	BLI_ghashIterator_free(ihash);
}

void DRW_gpencil_batch_cache_free(bGPdata *gpd)
{
	if (gpd->batch_cache_data == NULL) {
		return;
	}

	GHashIterator *ihash = BLI_ghashIterator_new(gpd->batch_cache_data);
	while (!BLI_ghashIterator_done(ihash)) {
		GpencilBatchCache *cache = (GpencilBatchCache *)BLI_ghashIterator_getValue(ihash);
		if (cache) {
			gpencil_batch_cache_clear(cache, gpd);
		}
		BLI_ghashIterator_step(ihash);
	}
	BLI_ghashIterator_free(ihash);

	/* free hash */
	if (gpd->batch_cache_data) {
		BLI_ghash_free(gpd->batch_cache_data, NULL, NULL);
		gpd->batch_cache_data = NULL;
	}
}

struct GPUTexture *DRW_gpencil_create_blank_texture(int width, int height)
{
	struct GPUTexture *tex;
	int w = width;
	int h = height;
	float *final_rect = MEM_callocN(sizeof(float) * 4 * w * h, "Gpencil Blank Texture");

	tex = DRW_texture_create_2D(w, h, DRW_TEX_RGBA_8, DRW_TEX_FILTER, final_rect);
	MEM_freeN(final_rect);

	return tex;
}

/* Helper for gpencil_array_modifiers()
 * See also MOD_gpencilarray.c -> bakeModifierGP()
 */
static void gp_array_modifier_make_instances(GPENCIL_StorageList *stl, Object *ob, GpencilArrayModifierData *mmd)
{
	/* reset random */
	mmd->rnd[0] = 1;
	
	/* Generate instances */
	for (int x = 0; x < mmd->count[0]; x++) {
		for (int y = 0; y < mmd->count[1]; y++) {
			for (int z = 0; z < mmd->count[2]; z++) {
				Object *newob;
				
				const int elem_idx[3] = {x, y, z};
				float mat[4][4];
				int sh;
				
				/* original strokes are at index = 0,0,0 */
				if ((x == 0) && (y == 0) && (z == 0)) {
					continue;
				}
				
				/* compute transform for instance */
				BKE_gpencil_array_modifier_instance_tfm(mmd, elem_idx, mat);

				/* add object to cache */
				newob = MEM_dupallocN(ob);
				newob->id.tag |= LIB_TAG_NO_MAIN; /* use this mark to delete later */
				mul_m4_m4m4(newob->obmat, ob->obmat, mat);
				
				/* apply scale */
				ARRAY_SET_ITEMS(newob->size, mat[0][0], mat[1][1], mat[2][2]);
				
				/* apply shift */
				sh = x;
				if (mmd->lock_axis == GP_LOCKAXIS_Y) {
					sh = y;
				}
				if (mmd->lock_axis == GP_LOCKAXIS_Z) {
					sh = z;
				}
				madd_v3_v3fl(newob->obmat[3], mmd->shift, sh);
				
				/* add temp object to cache */
				stl->g_data->gp_object_cache = gpencil_object_cache_allocate(stl->g_data->gp_object_cache, &stl->g_data->gp_cache_size, &stl->g_data->gp_cache_used);
				gpencil_object_cache_add(stl->g_data->gp_object_cache, newob, &stl->g_data->gp_cache_used);
			}
		}
	}
}

/* create instances using array modifiers */
void gpencil_array_modifiers(GPENCIL_StorageList *stl, Object *ob)
{
	if ((ob) && (ob->data)) {
		bGPdata *gpd = ob->data;
		if (GPENCIL_ANY_EDIT_MODE(gpd)) {
			return;
		}
	}

	for (ModifierData *md = ob->modifiers.first; md; md = md->next) {
		if (((md->mode & eModifierMode_Realtime) && (stl->storage->is_render == false)) ||
		    ((md->mode & eModifierMode_Render) && (stl->storage->is_render == true)))
		{
			if (md->type == eModifierType_GpencilArray) {
				GpencilArrayModifierData *mmd = (GpencilArrayModifierData *)md;
				
				/* Only add instances if the "Make Objects" flag is set
				 * FIXME: This is a workaround for z-ordering weirdness when all instances are in the same object
				 */
				if (mmd->flag & GP_ARRAY_MAKE_OBJECTS) {
					gp_array_modifier_make_instances(stl, ob, mmd);
				}
			}
		}
	}
}

