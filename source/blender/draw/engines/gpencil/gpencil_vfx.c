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

#include "gpencil_engine.h"

/* verify if this modifier is  available in the context, return NULL if not available */
static ModifierData *modifier_available(Object *ob, ModifierType type)
{
	ModifierData *md = modifiers_findByType(ob, type);
	if (md == NULL) {
		return NULL;
	}

	bGPdata *gpd = ob->gpd;
	if (gpd == NULL) {
		return NULL;
	}

	bool is_edit = (bool)((gpd->flag & (GP_DATA_STROKE_EDITMODE | GP_DATA_STROKE_SCULPTMODE | GP_DATA_STROKE_WEIGHTMODE)));
	if (((md->mode & eModifierMode_Realtime) && ((G.f & G_RENDER_OGL) == 0)) ||
		((md->mode & eModifierMode_Render) && (G.f & G_RENDER_OGL)) ||
		((md->mode & eModifierMode_Editmode) && (is_edit)))
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

	bGPdata *gpd = ob->gpd;
	if (gpd == NULL) {
		return false;
	}

	bool is_edit = (bool)((gpd->flag & (GP_DATA_STROKE_EDITMODE | GP_DATA_STROKE_SCULPTMODE | GP_DATA_STROKE_WEIGHTMODE)));
	if (((md->mode & eModifierMode_Realtime) && ((G.f & G_RENDER_OGL) == 0)) ||
		((md->mode & eModifierMode_Render) && (G.f & G_RENDER_OGL)) ||
		((md->mode & eModifierMode_Editmode) && (is_edit)))
	{
		return true;
	}

	return false;
}

/* Copy image as is to fill vfx texture */
static void DRW_gpencil_vfx_copy(int ob_idx, GPENCIL_e_data *e_data, GPENCIL_Data *vedata, Object *ob, tGPencilObjectCache *cache)
{
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;

	struct Gwn_Batch *vfxquad = DRW_cache_fullscreen_quad_get();
	DRWShadingGroup *vfx_shgrp = DRW_shgroup_create(e_data->gpencil_fullscreen_sh, psl->vfx_wave_pass);
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
static void DRW_gpencil_vfx_wave(ModifierData *md, int ob_idx, GPENCIL_e_data *e_data, GPENCIL_Data *vedata, Object *ob, tGPencilObjectCache *cache)
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
static void DRW_gpencil_vfx_blur(ModifierData *md, int ob_idx, GPENCIL_e_data *e_data, GPENCIL_Data *vedata, Object *ob, tGPencilObjectCache *cache)
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
	/* horizontal blur */
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_blur_pass_1);
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_a);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_a);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "dir", stl->storage->blur1, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blur", &stl->vfx[ob_idx].vfx_blur.x, 1);

	/* set first effect sh */
	if (cache->init_vfx_blur_sh_1 == NULL) {
		cache->init_vfx_blur_sh_1 = vfx_shgrp;
	}

	/* vertical blur */
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_blur_pass_1);
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_a);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_a);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "dir", stl->storage->blur2, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blur", &stl->vfx[ob_idx].vfx_blur.y, 1);

	/* set last effect sh */
	cache->end_vfx_blur_sh_1 = vfx_shgrp;

	/* === Pass 2 === */
	/* horizontal blur */
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_blur_pass_2);
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_b);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_b);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "dir", stl->storage->blur1, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blur", &stl->vfx[ob_idx].vfx_blur.x, 1);

	/* set first effect sh */
	if (cache->init_vfx_blur_sh_2 == NULL) {
		cache->init_vfx_blur_sh_2 = vfx_shgrp;
	}

	/* vertical blur */
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_blur_pass_2);
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_b);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_b);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "dir", stl->storage->blur2, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blur", &stl->vfx[ob_idx].vfx_blur.y, 1);

	/* set last effect sh */
	cache->end_vfx_blur_sh_2 = vfx_shgrp;

	/* === Pass 3 === */
	/* horizontal blur */
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_blur_pass_3);
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_a);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_a);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "dir", stl->storage->blur1, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blur", &stl->vfx[ob_idx].vfx_blur.x, 1);

	/* set first effect sh */
	if (cache->init_vfx_blur_sh_3 == NULL) {
		cache->init_vfx_blur_sh_3 = vfx_shgrp;
	}

	/* vertical blur */
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_blur_pass_3);
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_a);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_a);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "dir", stl->storage->blur2, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blur", &stl->vfx[ob_idx].vfx_blur.y, 1);

	/* set last effect sh */
	cache->end_vfx_blur_sh_3 = vfx_shgrp;

	/* === Pass 4 === */
	/* horizontal blur */
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_blur_pass_4);
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_b);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_b);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "dir", stl->storage->blur1, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blur", &stl->vfx[ob_idx].vfx_blur.x, 1);

	/* set first effect sh */
	if (cache->init_vfx_blur_sh_4 == NULL) {
		cache->init_vfx_blur_sh_4 = vfx_shgrp;
	}

	/* vertical blur */
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_blur_pass_4);
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->vfx_fbcolor_color_tx_b);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->vfx_fbcolor_depth_tx_b);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "dir", stl->storage->blur2, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blur", &stl->vfx[ob_idx].vfx_blur.y, 1);

	/* set last effect sh */
	cache->end_vfx_blur_sh_4 = vfx_shgrp;
}


void DRW_gpencil_vfx_modifiers(int ob_idx, struct GPENCIL_e_data *e_data, struct GPENCIL_Data *vedata, struct Object *ob, struct tGPencilObjectCache *cache)
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
		}
	}

}