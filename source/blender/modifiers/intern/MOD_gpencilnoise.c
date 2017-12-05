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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2017, Blender Foundation
 * This is a new part of Blender
 *
 * Contributor(s): Antonio Vazquez
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 */

/** \file blender/modifiers/intern/MOD_gpencilnoise.c
 *  \ingroup modifiers
 */

#include <stdio.h>

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_math_vector.h"
#include "BLI_rand.h"

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_modifier_types.h"

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_deform.h"
#include "BKE_gpencil.h"
#include "BKE_modifier.h"
#include "BKE_object.h"

#include "DEG_depsgraph.h"

#include "MOD_modifiertypes.h"
#include "MOD_gpencil_util.h"

static void initData(ModifierData *md)
{
	GpencilNoiseModifierData *gpmd = (GpencilNoiseModifierData *)md;
	gpmd->pass_index = 0;
	gpmd->flag |= GP_NOISE_MOD_LOCATION;
	gpmd->flag |= GP_NOISE_FULL_STROKE;
	gpmd->flag |= GP_NOISE_USE_RANDOM;
	gpmd->factor = 0.5f;
	gpmd->layername[0] = '\0';
	gpmd->vgname[0] = '\0';
	gpmd->step = 1;
	gpmd->scene_frame = -999999;
	gpmd->gp_frame = -999999;
	gpmd->vrand1 = 1.0;
	gpmd->vrand2 = 1.0;
}

static void copyData(ModifierData *md, ModifierData *target)
{
	modifier_copyData_generic(md, target);
}

static bool dependsOnTime(ModifierData *md)
{
	GpencilNoiseModifierData *mmd = (GpencilNoiseModifierData *)md;
	return (mmd->flag & GP_NOISE_USE_RANDOM) != 0;
}

/* aply noise effect based on stroke direction */
static void deformStroke(ModifierData *md, const EvaluationContext *UNUSED(eval_ctx),
                         Object *ob, bGPDlayer *gpl, bGPDstroke *gps)
{
	GpencilNoiseModifierData *mmd = (GpencilNoiseModifierData *)md;
	bGPDspoint *pt0, *pt1;
	float shift, vran, vdir;
	float normal[3];
	float vec1[3], vec2[3];
	Scene *scene = NULL;
	int sc_frame = 0;
	int sc_diff = 0;
	int vindex = defgroup_name_index(ob, mmd->vgname);
	float weight = 1.0f;

	if (!is_stroke_affected_by_modifier(
	        mmd->layername, mmd->pass_index, 3, gpl, gps,
	        mmd->flag & GP_NOISE_INVERSE_LAYER, mmd->flag & GP_NOISE_INVERSE_PASS))
	{
		return;
	}

	scene = mmd->modifier.scene;
	sc_frame = (scene) ? CFRA : 0;

	zero_v3(vec2);

	/* calculate stroke normal*/
	BKE_gpencil_stroke_normal(gps, normal);

	/* move points */
	for (int i = 0; i < gps->totpoints; i++) {
		if (((i == 0) || (i == gps->totpoints - 1)) && ((mmd->flag & GP_NOISE_MOVE_EXTREME) == 0)) {
			continue;
		}

		/* last point is special */
		if (i == gps->totpoints) {
			pt0 = &gps->points[i - 2];
			pt1 = &gps->points[i - 1];
		}
		else {
			pt0 = &gps->points[i - 1];
			pt1 = &gps->points[i];
		}

		/* verify vertex group */
		weight = is_point_affected_by_modifier(pt0, (int)(!(mmd->flag & GP_NOISE_INVERSE_VGROUP) == 0), vindex);
		if (weight < 0) {
			continue;
		}

		/* initial vector (p0 -> p1) */
		sub_v3_v3v3(vec1, &pt1->x, &pt0->x);
		vran = len_v3(vec1);
		/* vector orthogonal to normal */
		cross_v3_v3v3(vec2, vec1, normal);
		normalize_v3(vec2);
		/* use random noise */
		if (mmd->flag & GP_NOISE_USE_RANDOM) {
			sc_diff = abs(mmd->scene_frame - sc_frame);
			/* only recalc if the gp frame change or the number of scene frames is bigger than step */
			if ((!gpl->actframe) || (mmd->gp_frame != gpl->actframe->framenum) || 
			    (sc_diff >= mmd->step))
			{
				vran = mmd->vrand1 = BLI_frand();
				vdir = mmd->vrand2 = BLI_frand();
				mmd->gp_frame = gpl->actframe->framenum;
				mmd->scene_frame = sc_frame;
			}
			else {
				vran = mmd->vrand1;
				if (mmd->flag & GP_NOISE_FULL_STROKE) {
					vdir = mmd->vrand2;
				}
				else {
					int f = (mmd->vrand2 * 10.0f) + i;
					vdir = f % 2;
				}
			}
		}
		else {
			vran = 1.0f;
			if (mmd->flag & GP_NOISE_FULL_STROKE) {
				vdir = gps->totpoints % 2;
			}
			else {
				vdir = i % 2;
			}
			mmd->gp_frame = -999999;
		}

		/* apply randomness to location of the point */
		if (mmd->flag & GP_NOISE_MOD_LOCATION) {
			/* factor is too sensitive, so need divide */
			shift = ((vran * mmd->factor) / 1000.0f) * weight;
			if (vdir > 0.5f) {
				mul_v3_fl(vec2, shift);
			}
			else {
				mul_v3_fl(vec2, shift * -1.0f);
			}
			add_v3_v3(&pt1->x, vec2);
		}

		/* apply randomness to thickness */
		if (mmd->flag & GP_NOISE_MOD_THICKNESS) {
			if (vdir > 0.5f) {
				pt1->pressure -= pt1->pressure * vran * mmd->factor;
			}
			else {
				pt1->pressure += pt1->pressure * vran * mmd->factor;
			}
			CLAMP_MIN(pt1->pressure, GPENCIL_STRENGTH_MIN);
		}

		/* apply randomness to color strength */
		if (mmd->flag & GP_NOISE_MOD_STRENGTH) {
			if (vdir > 0.5f) {
				pt1->strength -= pt1->strength * vran * mmd->factor;
			}
			else {
				pt1->strength += pt1->strength * vran * mmd->factor;
			}
			CLAMP_MIN(pt1->strength, GPENCIL_STRENGTH_MIN);
		}
	}
}

static void bakeModifierGP(const bContext *UNUSED(C), const EvaluationContext *eval_ctx,
                           ModifierData *md, Object *ob)
{
	bGPdata *gpd = ob->data;

	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
				deformStroke(md, eval_ctx, ob, gpl, gps);
			}
		}
	}
}

ModifierTypeInfo modifierType_GpencilNoise = {
	/* name */              "Noise",
	/* structName */        "GpencilNoiseModifierData",
	/* structSize */        sizeof(GpencilNoiseModifierData),
	/* type */              eModifierTypeType_Gpencil,
	/* flags */             eModifierTypeFlag_GpencilMod | eModifierTypeFlag_SupportsEditmode,

	/* copyData */          copyData,
	/* deformVerts */       NULL,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     NULL,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     NULL,
	/* applyModifierEM */   NULL,
	/* deformStroke */      deformStroke,
	/* generateStrokes */   NULL,
	/* bakeModifierGP */    bakeModifierGP,
	/* initData */          initData,
	/* requiredDataMask */  NULL,
	/* freeData */          NULL,
	/* isDisabled */        NULL,
	/* updateDepsgraph */   NULL,
	/* dependsOnTime */     dependsOnTime,
	/* dependsOnNormals */	NULL,
	/* foreachObjectLink */ NULL,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};
