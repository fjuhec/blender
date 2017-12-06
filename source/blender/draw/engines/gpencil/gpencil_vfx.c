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

/** \file blender/draw/engines/gpencil/gpencil_vfx.c
 *  \ingroup draw
 */

#include "BKE_modifier.h"
#include "BKE_global.h"

#include "DRW_engine.h"
#include "DRW_render.h"

#include "DNA_gpencil_types.h"
#include "DNA_modifier_types.h"
#include "DNA_screen_types.h"

#include "ED_view3d.h"
#include "ED_gpencil.h"

#include "gpencil_engine.h"

/* verify if this modifier is  available in the context, return NULL if not available */
static ModifierData *modifier_available(Object *ob, ModifierType type)
{
	ModifierData *md = modifiers_findByType(ob, type);
	if (md == NULL) {
		return NULL;
	}

	bGPdata *gpd = ob->data;
	if (gpd == NULL) {
		return NULL;
	}

	bool is_edit = GPENCIL_ANY_EDIT_MODE(gpd);
	if (((md->mode & eModifierMode_Editmode) == 0) && (is_edit)) {
		return NULL;
	}
	if (((md->mode & eModifierMode_Realtime) && ((G.f & G_RENDER_OGL) == 0)) ||
	    ((md->mode & eModifierMode_Render) && (G.f & G_RENDER_OGL)))
	{
		return md;
	}

	return NULL;
}

/* verify if this modifier is active */
static bool modifier_is_active(Object *ob, ModifierData *md)
{
	if (md == NULL) {
		return false;
	}

	bGPdata *gpd = ob->data;
	if (gpd == NULL) {
		return false;
	}

	bool is_edit = GPENCIL_ANY_EDIT_MODE(gpd);
	if (((md->mode & eModifierMode_Editmode) == 0) && (is_edit)) {
		return false;
	}

	if (((md->mode & eModifierMode_Realtime) && ((G.f & G_RENDER_OGL) == 0)) ||
	    ((md->mode & eModifierMode_Render) && (G.f & G_RENDER_OGL)))
	{
		return true;
	}

	return false;
}

/* Copy image as is to fill vfx texture */
static void DRW_gpencil_vfx_copy(
        int UNUSED(ob_idx), GPENCIL_e_data *e_data, GPENCIL_Data *vedata,
        Object *UNUSED(ob), tGPencilObjectCache *cache)
{
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

	struct Gwn_Batch *vfxquad = DRW_cache_fullscreen_quad_get();
	DRWShadingGroup *vfx_shgrp = DRW_shgroup_create(e_data->gpencil_fullscreen_sh, psl->vfx_wave_pass);
	++stl->g_data->tot_sh;
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->temp_fbcolor_color_tx);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->temp_fbcolor_depth_tx);

	/* set first effect sh */
	if (cache->init_vfx_wave_sh == NULL) {
		cache->init_vfx_wave_sh = vfx_shgrp;
	}

	/* set last effect sh */
	cache->end_vfx_wave_sh = vfx_shgrp;
}

/* Wave Distorsion VFX */
static void DRW_gpencil_vfx_wave(
        ModifierData *md, int ob_idx, GPENCIL_e_data *e_data, GPENCIL_Data *vedata,
        Object *UNUSED(ob), tGPencilObjectCache *cache)
{
	if (md == NULL) {
		return;
	}

	GpencilWaveModifierData *mmd = (GpencilWaveModifierData *)md;

	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	stl->vfx[ob_idx].vfx_wave.amplitude = mmd->amplitude;
	stl->vfx[ob_idx].vfx_wave.period = mmd->period;
	stl->vfx[ob_idx].vfx_wave.phase = mmd->phase;
	stl->vfx[ob_idx].vfx_wave.orientation = mmd->orientation;

	const float *viewport_size = DRW_viewport_size_get();
	copy_v2_v2(stl->vfx[ob_idx].vfx_wave.wsize, viewport_size);

	struct Gwn_Batch *vfxquad = DRW_cache_fullscreen_quad_get();

	DRWShadingGroup *vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_wave_sh, psl->vfx_wave_pass);
	++stl->g_data->tot_sh;
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->temp_fbcolor_color_tx);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->temp_fbcolor_depth_tx);

	DRW_shgroup_uniform_float(vfx_shgrp, "amplitude", &stl->vfx[ob_idx].vfx_wave.amplitude, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "period", &stl->vfx[ob_idx].vfx_wave.period, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "phase", &stl->vfx[ob_idx].vfx_wave.phase, 1);
	DRW_shgroup_uniform_int(vfx_shgrp, "orientation", &stl->vfx[ob_idx].vfx_wave.orientation, 1);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "wsize", stl->vfx[ob_idx].vfx_wave.wsize, 1);

	/* set first effect sh */
	if (cache->init_vfx_wave_sh == NULL) {
		cache->init_vfx_wave_sh = vfx_shgrp;
	}

	/* set last effect sh */
	cache->end_vfx_wave_sh = vfx_shgrp;
}

/* Gaussian Blur VFX
 * The effect is done using two shading groups because is faster to apply horizontal
 * and vertical in different operations.
 */
static void DRW_gpencil_vfx_blur(
        ModifierData *md, int ob_idx, GPENCIL_e_data *e_data, GPENCIL_Data *vedata,
        Object *UNUSED(ob), tGPencilObjectCache *cache)
{
	if (md == NULL) {
		return;
	}

	GpencilBlurModifierData *mmd = (GpencilBlurModifierData *)md;

	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	DRWShadingGroup *vfx_shgrp;
	const float *viewport_size = DRW_viewport_size_get();
	stl->vfx[ob_idx].vfx_blur.x = mmd->radius[0];
	stl->vfx[ob_idx].vfx_blur.y = mmd->radius[1] * (viewport_size[1] / viewport_size[0]);

	struct Gwn_Batch *vfxquad = DRW_cache_fullscreen_quad_get();
	/* === Pass 1 === */
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_blur_pass_1);
	++stl->g_data->tot_sh;
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_a);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_a);
	DRW_shgroup_uniform_float(vfx_shgrp, "blurx", &stl->vfx[ob_idx].vfx_blur.x, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blury", &stl->vfx[ob_idx].vfx_blur.y, 1);

	/* set first effect sh */
	if (cache->init_vfx_blur_sh_1 == NULL) {
		cache->init_vfx_blur_sh_1 = vfx_shgrp;
	}

	/* set last effect sh */
	cache->end_vfx_blur_sh_1 = vfx_shgrp;

	/* === Pass 2 === */
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_blur_pass_2);
	++stl->g_data->tot_sh;
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_b);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_b);
	DRW_shgroup_uniform_float(vfx_shgrp, "blurx", &stl->vfx[ob_idx].vfx_blur.x, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blury", &stl->vfx[ob_idx].vfx_blur.y, 1);

	/* set first effect sh */
	if (cache->init_vfx_blur_sh_2 == NULL) {
		cache->init_vfx_blur_sh_2 = vfx_shgrp;
	}

	/* set last effect sh */
	cache->end_vfx_blur_sh_2 = vfx_shgrp;

	/* === Pass 3 === */
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_blur_pass_3);
	++stl->g_data->tot_sh;
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_a);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_a);
	DRW_shgroup_uniform_float(vfx_shgrp, "blurx", &stl->vfx[ob_idx].vfx_blur.x, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blury", &stl->vfx[ob_idx].vfx_blur.y, 1);

	/* set first effect sh */
	if (cache->init_vfx_blur_sh_3 == NULL) {
		cache->init_vfx_blur_sh_3 = vfx_shgrp;
	}

	/* set last effect sh */
	cache->end_vfx_blur_sh_3 = vfx_shgrp;

	/* === Pass 4 === */
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_blur_pass_4);
	++stl->g_data->tot_sh;
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_b);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_b);
	DRW_shgroup_uniform_float(vfx_shgrp, "blurx", &stl->vfx[ob_idx].vfx_blur.x, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blury", &stl->vfx[ob_idx].vfx_blur.y, 1);

	/* set first effect sh */
	if (cache->init_vfx_blur_sh_4 == NULL) {
		cache->init_vfx_blur_sh_4 = vfx_shgrp;
	}

	/* set last effect sh */
	cache->end_vfx_blur_sh_4 = vfx_shgrp;
}

/* Pixelate VFX */
static void DRW_gpencil_vfx_pixel(
        ModifierData *md, int ob_idx, GPENCIL_e_data *e_data, GPENCIL_Data *vedata,
        Object *UNUSED(ob), tGPencilObjectCache *cache)
{
	if (md == NULL) {
		return;
	}

	GpencilPixelModifierData *mmd = (GpencilPixelModifierData *)md;

	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	DRWShadingGroup *vfx_shgrp;
	stl->vfx[ob_idx].vfx_pixel.size[0] = mmd->size[0];
	stl->vfx[ob_idx].vfx_pixel.size[1] = mmd->size[1];
	copy_v4_v4(stl->vfx[ob_idx].vfx_pixel.rgba, mmd->rgba);
	stl->vfx[ob_idx].vfx_pixel.lines = (int)mmd->flag & GP_PIXEL_USE_LINES;

	struct Gwn_Batch *vfxquad = DRW_cache_fullscreen_quad_get();
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_pixel_sh, psl->vfx_pixel_pass);
	++stl->g_data->tot_sh;
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_a);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_a);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "size", &stl->vfx[ob_idx].vfx_pixel.size[0], 1);
	DRW_shgroup_uniform_vec4(vfx_shgrp, "color", &stl->vfx[ob_idx].vfx_pixel.rgba[0], 1);
	DRW_shgroup_uniform_int(vfx_shgrp, "uselines", &stl->vfx[ob_idx].vfx_pixel.lines, 1);

	/* set first effect sh */
	if (cache->init_vfx_pixel_sh == NULL) {
		cache->init_vfx_pixel_sh = vfx_shgrp;
	}

	/* set last effect sh */
	cache->end_vfx_pixel_sh = vfx_shgrp;
}

/* Swirl VFX */
static void DRW_gpencil_vfx_swirl(
        ModifierData *md, int ob_idx, GPENCIL_e_data *e_data, GPENCIL_Data *vedata,
        Object *ob, tGPencilObjectCache *cache)
{
	if (md == NULL) {
		return;
	}

	GpencilSwirlModifierData *mmd = (GpencilSwirlModifierData *)md;

	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	DRWShadingGroup *vfx_shgrp;
	const DRWContextState *draw_ctx = DRW_context_state_get();
	ARegion *ar = draw_ctx->ar;
	int co[2];
	if (mmd->flag & GP_SWIRL_USE_OB_LOC) {
		ED_view3d_project_int_global(ar, ob->loc, co, V3D_PROJ_TEST_NOP);
		stl->vfx[ob_idx].vfx_swirl.center[0] = co[0];
		stl->vfx[ob_idx].vfx_swirl.center[1] = co[1];
	}
	else {
		stl->vfx[ob_idx].vfx_swirl.center[0] = mmd->center[0];
		stl->vfx[ob_idx].vfx_swirl.center[1] = mmd->center[1];
	}
	stl->vfx[ob_idx].vfx_swirl.radius = mmd->radius;
	stl->vfx[ob_idx].vfx_swirl.angle = mmd->angle;
	stl->vfx[ob_idx].vfx_swirl.transparent = (int)mmd->flag & GP_SWIRL_MAKE_TRANSPARENT;

	struct Gwn_Batch *vfxquad = DRW_cache_fullscreen_quad_get();
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_swirl_sh, psl->vfx_swirl_pass);
	++stl->g_data->tot_sh;
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_a);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_a);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "center", &stl->vfx[ob_idx].vfx_swirl.center[0], 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "radius", &stl->vfx[ob_idx].vfx_swirl.radius, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "angle", &stl->vfx[ob_idx].vfx_swirl.angle, 1);
	DRW_shgroup_uniform_int(vfx_shgrp, "transparent", &stl->vfx[ob_idx].vfx_swirl.transparent, 1);

	/* set first effect sh */
	if (cache->init_vfx_swirl_sh == NULL) {
		cache->init_vfx_swirl_sh = vfx_shgrp;
	}

	/* set last effect sh */
	cache->end_vfx_swirl_sh = vfx_shgrp;
}

/* Flip VFX */
static void DRW_gpencil_vfx_flip(
	ModifierData *md, int ob_idx, GPENCIL_e_data *e_data, GPENCIL_Data *vedata,
	Object *ob, tGPencilObjectCache *cache)
{
	if (md == NULL) {
		return;
	}

	GpencilFlipModifierData *mmd = (GpencilFlipModifierData *)md;

	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	DRWShadingGroup *vfx_shgrp;
	if (mmd->flag & GP_FLIP_HORIZONTAL) {
		stl->vfx[ob_idx].vfx_flip.flipmode[0] = 1.0f;
	}
	else {
		stl->vfx[ob_idx].vfx_flip.flipmode[0] = 0;
	};
	if (mmd->flag & GP_FLIP_VERTICAL) {
		stl->vfx[ob_idx].vfx_flip.flipmode[1] = 1.0f;
	}
	else {
		stl->vfx[ob_idx].vfx_flip.flipmode[1] = 0;
	};

	struct Gwn_Batch *vfxquad = DRW_cache_fullscreen_quad_get();
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_flip_sh, psl->vfx_flip_pass);
	++stl->g_data->tot_sh;
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_a);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_a);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "mode", &stl->vfx[ob_idx].vfx_flip.flipmode[0], 1);

	const float *viewport_size = DRW_viewport_size_get();
	copy_v2_v2(stl->vfx[ob_idx].vfx_flip.wsize, viewport_size);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "wsize", stl->vfx[ob_idx].vfx_flip.wsize, 1);

	/* set first effect sh */
	if (cache->init_vfx_flip_sh == NULL) {
		cache->init_vfx_flip_sh = vfx_shgrp;
	}

	/* set last effect sh */
	cache->end_vfx_flip_sh = vfx_shgrp;
}

/* Light VFX */
static void DRW_gpencil_vfx_light(
	ModifierData *md, int ob_idx, GPENCIL_e_data *e_data, GPENCIL_Data *vedata,
	Object *ob, tGPencilObjectCache *cache)
{
	if (md == NULL) {
		return;
	}

	GpencilLightModifierData *mmd = (GpencilLightModifierData *)md;

	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	DRWShadingGroup *vfx_shgrp;
	const DRWContextState *draw_ctx = DRW_context_state_get();
	ARegion *ar = draw_ctx->ar;

	struct Gwn_Batch *vfxquad = DRW_cache_fullscreen_quad_get();
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_light_sh, psl->vfx_light_pass);
	++stl->g_data->tot_sh;
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_a);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_a);
	
	/* location of the light using obj location as origin */
	int co[2];
	ED_view3d_project_int_global(ar, ob->loc, co, V3D_PROJ_TEST_NOP);
	stl->vfx[ob_idx].vfx_light.loc[0] = (float)co[0] + mmd->loc[0];
	stl->vfx[ob_idx].vfx_light.loc[1] = (float)co[1] + mmd->loc[1];
	stl->vfx[ob_idx].vfx_light.loc[2] = (float)mmd->loc[2];
	DRW_shgroup_uniform_vec3(vfx_shgrp, "loc", &stl->vfx[ob_idx].vfx_light.loc[0], 1);

	copy_v2_v2(stl->vfx[ob_idx].vfx_light.color, mmd->color);
	DRW_shgroup_uniform_vec3(vfx_shgrp, "lightcolor", &stl->vfx[ob_idx].vfx_light.color[0], 1);

	stl->vfx[ob_idx].vfx_light.energy = mmd->energy;
	DRW_shgroup_uniform_float(vfx_shgrp, "energy", &stl->vfx[ob_idx].vfx_light.energy, 1);

	stl->vfx[ob_idx].vfx_light.ambient = mmd->ambient;
	DRW_shgroup_uniform_float(vfx_shgrp, "ambient", &stl->vfx[ob_idx].vfx_light.ambient, 1);

	/* set first effect sh */
	if (cache->init_vfx_light_sh == NULL) {
		cache->init_vfx_light_sh = vfx_shgrp;
	}

	/* set last effect sh */
	cache->end_vfx_light_sh = vfx_shgrp;
}

void DRW_gpencil_vfx_modifiers(
        int ob_idx, struct GPENCIL_e_data *e_data, struct GPENCIL_Data *vedata,
        struct Object *ob, struct tGPencilObjectCache *cache)
{
	bool ready = false;
	ModifierData *md_wave = modifier_available(ob, eModifierType_GpencilWave);

	if (md_wave) {
		DRW_gpencil_vfx_wave(md_wave, ob_idx, e_data, vedata, ob, cache);
		ready = true;
	}

	/* loop VFX modifiers 
	 * copy the original texture if wave modifier did not copy before
	 */
	for (ModifierData *md = ob->modifiers.first; md; md = md->next) {
		switch (md->type) {
			case eModifierType_GpencilBlur:
				if (modifier_is_active(ob, md)) {
					if (!ready) {
						DRW_gpencil_vfx_copy(ob_idx, e_data, vedata, ob, cache);
						ready = true;
					}
					DRW_gpencil_vfx_blur(md, ob_idx, e_data, vedata, ob, cache);
				}
				break;
			case eModifierType_GpencilPixel:
				if (modifier_is_active(ob, md)) {
					if (!ready) {
						DRW_gpencil_vfx_copy(ob_idx, e_data, vedata, ob, cache);
						ready = true;
					}
					DRW_gpencil_vfx_pixel(md, ob_idx, e_data, vedata, ob, cache);
				}
				break;
			case eModifierType_GpencilSwirl:
				if (modifier_is_active(ob, md)) {
					if (!ready) {
						DRW_gpencil_vfx_copy(ob_idx, e_data, vedata, ob, cache);
						ready = true;
					}
					DRW_gpencil_vfx_swirl(md, ob_idx, e_data, vedata, ob, cache);
				}
				break;
			case eModifierType_GpencilFlip:
				if (modifier_is_active(ob, md)) {
					if (!ready) {
						DRW_gpencil_vfx_copy(ob_idx, e_data, vedata, ob, cache);
						ready = true;
					}
					DRW_gpencil_vfx_flip(md, ob_idx, e_data, vedata, ob, cache);
				}
				break;
			case eModifierType_GpencilLight:
				if (modifier_is_active(ob, md)) {
					if (!ready) {
						DRW_gpencil_vfx_copy(ob_idx, e_data, vedata, ob, cache);
						ready = true;
					}
					DRW_gpencil_vfx_light(md, ob_idx, e_data, vedata, ob, cache);
				}
				break;
		}
	}

}
