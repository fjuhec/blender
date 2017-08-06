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
	ModifierData  *md = modifiers_findByType(ob, type);
	if (md == NULL) {
		return NULL;
	}

	bGPdata *gpd = ob->gpd;
	if (gpd == NULL) {
		return NULL;
	}

	bool is_edit = (bool)((gpd->flag & (GP_DATA_STROKE_EDITMODE | GP_DATA_STROKE_SCULPTMODE | GP_DATA_STROKE_WEIGHTMODE)));
	if ((md->mode & eModifierMode_Realtime) && (G.f & G_RENDER_OGL)) {
		return NULL;
	}

	if (((md->mode & eModifierMode_Render) == 0) && (G.f & G_RENDER_OGL)) {
		return NULL;
	}

	if ((md->mode & eModifierMode_Editmode) && (!is_edit)) {
		return NULL;
	}

	return md;
}

/* Gaussian Blur VFX
 * The effect is done using two shading groups because is faster to apply horizontal
 * and vertical in different operations.
 */
void DRW_gpencil_vfx_blur(int ob_idx, GPENCIL_e_data *e_data, GPENCIL_Data *vedata, Object *ob, tGPencilObjectCache *cache)
{
	ModifierData *md = modifier_available(ob, eModifierType_GpencilBlur);
	if (md == NULL) {
		return;
	}

	GpencilBlurModifierData *mmd = (GpencilBlurModifierData *)md;

	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	const float *viewport_size = DRW_viewport_size_get();
	stl->vfx[ob_idx].vfx_blur.x = mmd->radius[0];
	stl->vfx[ob_idx].vfx_blur.y = mmd->radius[1] * (viewport_size[1] / viewport_size[0]);

	struct Gwn_Batch *vfxquad = DRW_cache_fullscreen_quad_get();
	/* horizontal blur */
	DRWShadingGroup *vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_pass);
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->temp_fbcolor_color_tx);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->temp_fbcolor_depth_tx);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "dir", stl->storage->blur1, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blur", &stl->vfx[ob_idx].vfx_blur.x, 1);

	/* set first effect sh */
	if (cache->init_vfx_sh == NULL) {
		cache->init_vfx_sh = vfx_shgrp;
	}

	/* vertical blur */
	vfx_shgrp = DRW_shgroup_create(e_data->gpencil_vfx_blur_sh, psl->vfx_pass);
	DRW_shgroup_call_add(vfx_shgrp, vfxquad, NULL);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeColor", &e_data->temp_fbcolor_color_tx);
	DRW_shgroup_uniform_buffer(vfx_shgrp, "strokeDepth", &e_data->temp_fbcolor_depth_tx);
	DRW_shgroup_uniform_vec2(vfx_shgrp, "dir", stl->storage->blur2, 1);
	DRW_shgroup_uniform_float(vfx_shgrp, "blur", &stl->vfx[ob_idx].vfx_blur.y, 1);

	/* set last effect sh */
	cache->end_vfx_sh = vfx_shgrp;
}
