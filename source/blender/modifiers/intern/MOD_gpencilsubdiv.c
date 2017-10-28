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
 * The Original Code is Copyright (C) 2005 by the Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Antonio Vazquez
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 */

/** \file blender/modifiers/intern/MOD_gpencilsubdiv.c
 *  \ingroup modifiers
 */

#include <stdio.h>

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"

#include "BLI_utildefines.h"
#include "BKE_DerivedMesh.h"
#include "BKE_gpencil.h"

#include "MOD_modifiertypes.h"

static void initData(ModifierData *md)
{
	GpencilSubdivModifierData *gpmd = (GpencilSubdivModifierData *)md;
	gpmd->pass_index = 0;
	gpmd->flag |= GP_SUBDIV_SIMPLE;
	gpmd->level = 1;
	gpmd->layername[0] = '\0';
}

static void copyData(ModifierData *md, ModifierData *target)
{
	modifier_copyData_generic(md, target);
}

static DerivedMesh *applyModifier(
        ModifierData *md, const struct EvaluationContext *UNUSED(eval_ctx), Object *ob,
        DerivedMesh *UNUSED(dm),
        ModifierApplyFlag UNUSED(flag))
{
	bGPdata *gpd;
	if ((!ob) || (!ob->data)) {
		return NULL;
	}
	gpd = ob->data;

	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
				BKE_gpencil_subdiv_modifier(-1, (GpencilSubdivModifierData *)md, ob, gpl, gps);
			}
		}
	}

	return NULL;
}

ModifierTypeInfo modifierType_GpencilSubdiv = {
	/* name */              "Subdivision",
	/* structName */        "GpencilSubdivModifierData",
	/* structSize */        sizeof(GpencilSubdivModifierData),
	/* type */              eModifierTypeType_Gpencil,
	/* flags */             eModifierTypeFlag_GpencilMod | eModifierTypeFlag_SupportsEditmode,

	/* copyData */          copyData,
	/* deformVerts */       NULL,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     NULL,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     applyModifier,
	/* applyModifierEM */   NULL,
	/* initData */          initData,
	/* requiredDataMask */  NULL,
	/* freeData */          NULL,
	/* isDisabled */        NULL,
	/* updateDepsgraph */   NULL,
	/* dependsOnTime */     NULL,
	/* dependsOnNormals */	NULL,
	/* foreachObjectLink */ NULL,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};
