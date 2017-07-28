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

/** \file blender/modifiers/intern/MOD_gpencillattice.c
 *  \ingroup modifiers
 */

#include <stdio.h>

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"

#include "BLI_utildefines.h"

#include "BKE_DerivedMesh.h"
#include "BKE_gpencil.h"
#include "BKE_lattice.h"
#include "BKE_library_query.h"

#include "MEM_guardedalloc.h"

#include "MOD_modifiertypes.h"

static void initData(ModifierData *md)
{
	GpencilLatticeModifierData *gpmd = (GpencilLatticeModifierData *)md;
	gpmd->passindex = 0;
	gpmd->layername[0] = '\0';
	gpmd->object = NULL;
	gpmd->cache_data = NULL;
	gpmd->strength = 1.0f;

	BKE_gpencil_batch_cache_alldirty();
}

static void copyData(ModifierData *md, ModifierData *target)
{
	GpencilLatticeModifierData *smd = (GpencilLatticeModifierData *)md;
	GpencilLatticeModifierData *tsmd = (GpencilLatticeModifierData *)target;

	modifier_copyData_generic(md, target);
}

static DerivedMesh *applyModifier(ModifierData *md, struct EvaluationContext *UNUSED(eval_ctx), Object *ob,
	DerivedMesh *UNUSED(dm),
	ModifierApplyFlag UNUSED(flag))
{
	GpencilLatticeModifierData *mmd = (GpencilLatticeModifierData *)md;
	LatticeDeformData *ldata = (LatticeDeformData *)mmd->cache_data;
	bGPdata *gpd;
	Object *latob = NULL;

	if ((!ob) || (!ob->gpd)) {
		return NULL;
	}
	gpd = ob->gpd;
	latob = mmd->object;
	if ((!latob) || (latob->type != OB_LATTICE)) {
		return NULL;
	}

	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
				ED_gpencil_lattice_modifier(-1, (GpencilLatticeModifierData *)md, gpl, gps);
			}
		}
	}

	if (ldata) {
		end_latt_deform(ldata);
		mmd->cache_data = NULL;
	}

	return NULL;
}

static void freeData(ModifierData *md)
{
	GpencilLatticeModifierData *mmd = (GpencilLatticeModifierData *)md;
	LatticeDeformData *ldata = (LatticeDeformData *)mmd->cache_data;
	/* free deform data */
	if (ldata) {
		end_latt_deform(ldata);
	}
}

static bool isDisabled(ModifierData *md, int UNUSED(userRenderParams))
{
	GpencilLatticeModifierData *mmd = (GpencilLatticeModifierData *)md;

	return !mmd->object;
}

static void foreachObjectLink(
	ModifierData *md, Object *ob,
	ObjectWalkFunc walk, void *userData)
{
	GpencilLatticeModifierData *mmd = (GpencilLatticeModifierData *)md;

	walk(userData, ob, &mmd->object, IDWALK_CB_NOP);
}

ModifierTypeInfo modifierType_GpencilLattice = {
	/* name */              "Lattice",
	/* structName */        "GpencilLatticeModifierData",
	/* structSize */        sizeof(GpencilLatticeModifierData),
	/* type */             	eModifierTypeType_Gpencil,
	/* flags */             eModifierTypeFlag_GpencilMod | eModifierTypeFlag_Single,

	/* copyData */          copyData,
	/* deformVerts */       NULL,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     NULL,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     applyModifier,
	/* applyModifierEM */   NULL,
	/* initData */          initData,
	/* requiredDataMask */  NULL,
	/* freeData */          freeData,
	/* isDisabled */        isDisabled,
	/* updateDepsgraph */   NULL,
	/* dependsOnTime */     NULL,
	/* dependsOnNormals */	NULL,
	/* foreachObjectLink */ foreachObjectLink,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};
