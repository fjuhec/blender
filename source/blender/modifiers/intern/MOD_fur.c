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
 * Contributor(s): Daniel Dunbar
 *                 Ton Roosendaal,
 *                 Ben Batt,
 *                 Brecht Van Lommel,
 *                 Campbell Barton
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 */

/** \file blender/modifiers/intern/MOD_fur.c
 *  \ingroup modifiers
 */

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"

#include "DNA_object_types.h"
#include "DNA_hair_types.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_hair.h"
#include "BKE_library.h"
#include "BKE_library_query.h"
#include "BKE_modifier.h"

#include "DEG_depsgraph_build.h"

#include "MOD_util.h"


static void initData(ModifierData *md)
{
	FurModifierData *fmd = (FurModifierData *) md;
	
	fmd->hair_system = BKE_hair_new();
	
	fmd->flag |= 0;
	
	fmd->follicle_count = 100000;
	fmd->guides_count = 1000;
	
	fmd->draw_settings = BKE_hair_draw_settings_new();
}

static void copyData(ModifierData *md, ModifierData *target)
{
	FurModifierData *fmd = (FurModifierData *) md;
	FurModifierData *tfmd = (FurModifierData *) target;

	if (tfmd->hair_system) {
		BKE_hair_free(tfmd->hair_system);
	}
	if (tfmd->draw_settings)
	{
		BKE_hair_draw_settings_free(tfmd->draw_settings);
	}

	modifier_copyData_generic(md, target);
	
	if (fmd->hair_system) {
		tfmd->hair_system = BKE_hair_copy(fmd->hair_system);
	}
	if (fmd->draw_settings)
	{
		tfmd->draw_settings = BKE_hair_draw_settings_copy(fmd->draw_settings);
	}
}

static void freeData(ModifierData *md)
{
	FurModifierData *fmd = (FurModifierData *) md;
	
	if (fmd->hair_system) {
		BKE_hair_free(fmd->hair_system);
	}
	if (fmd->draw_settings)
	{
		BKE_hair_draw_settings_free(fmd->draw_settings);
	}
}

static DerivedMesh *applyModifier(ModifierData *md, const struct EvaluationContext *UNUSED(eval_ctx),
                                  Object *UNUSED(ob), DerivedMesh *dm,
                                  ModifierApplyFlag UNUSED(flag))
{
	FurModifierData *fmd = (FurModifierData *) md;
	
	UNUSED_VARS(fmd);
	
	return dm;
}

static void foreachObjectLink(
        ModifierData *md,
        Object *ob,
        ObjectWalkFunc walk,
        void *userData)
{
	FurModifierData *fmd = (FurModifierData *) md;
	UNUSED_VARS(ob, walk, userData, fmd);
}

static void foreachIDLink(
        ModifierData *md,
        Object *ob,
        IDWalkFunc walk,
        void *userData)
{
	FurModifierData *fmd = (FurModifierData *) md;
	
	if (fmd->hair_system)
	{
		walk(userData, ob, (ID **)&fmd->hair_system->mat, IDWALK_CB_USER);
	}
	
	foreachObjectLink(md, ob, (ObjectWalkFunc)walk, userData);
}

ModifierTypeInfo modifierType_Fur = {
	/* name */              "Fur",
	/* structName */        "FurModifierData",
	/* structSize */        sizeof(FurModifierData),
	/* type */              eModifierTypeType_NonGeometrical,
	/* flags */             eModifierTypeFlag_AcceptsMesh |
	                        eModifierTypeFlag_SupportsEditmode,

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
	/* isDisabled */        NULL,
	/* updateDepsgraph */   NULL,
	/* dependsOnTime */     NULL,
	/* dependsOnNormals */	NULL,
	/* foreachObjectLink */ foreachObjectLink,
	/* foreachIDLink */     foreachIDLink,
	/* foreachTexLink */    NULL,
};
