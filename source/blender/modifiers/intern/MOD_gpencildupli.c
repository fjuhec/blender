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

/** \file blender/modifiers/intern/MOD_gpencildupli.c
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
	GpencilDupliModifierData *gpmd = (GpencilDupliModifierData *)md;
	gpmd->passindex = 0;
	gpmd->layername[0] = '\0';
	gpmd->count = 1;
	gpmd->offset[0] = 1.0f;
	gpmd->scale[0] = 1.0f;
	gpmd->scale[1] = 1.0f;
	gpmd->scale[2] = 1.0f;
	gpmd->rnd_rot = 0.5f;
	gpmd->rnd_size = 0.5f;
	/* fill random values */
	BKE_gpencil_fill_random_array(gpmd->rnd, 20);
	gpmd->rnd[0] = 1;

	BKE_gpencil_batch_cache_alldirty();
}

static void copyData(ModifierData *md, ModifierData *target)
{
#if 0
	GpencilDupliModifierData *smd = (GpencilDupliModifierData *)md;
	GpencilDupliModifierData *tsmd = (GpencilDupliModifierData *)target;
#endif

	modifier_copyData_generic(md, target);
}

static DerivedMesh *applyModifier(ModifierData *md, const struct EvaluationContext *UNUSED(eval_ctx), Object *ob,
	DerivedMesh *UNUSED(dm),
	ModifierApplyFlag UNUSED(flag))
{
#if 0
	GpencilDupliModifierData *mmd = (GpencilDupliModifierData *)md;
#endif
	bGPdata *gpd;
	if ((!ob) || (!ob->gpd)) {
		return NULL;
	}
	gpd = ob->gpd;

	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			BKE_gpencil_dupli_modifier(-1, (GpencilDupliModifierData *)md, ob, gpl, gpf);
		}
	}

	return NULL;
}

ModifierTypeInfo modifierType_GpencilDupli = {
	/* name */              "Duplication",
	/* structName */        "GpencilDupliModifierData",
	/* structSize */        sizeof(GpencilDupliModifierData),
	/* type */             	eModifierTypeType_Gpencil,
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
