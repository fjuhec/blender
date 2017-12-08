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

/** \file blender/modifiers/intern/MOD_gpencillight.c
 *  \ingroup modifiers
 */

#include <stdio.h>

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"

#include "BLI_math_base.h"
#include "BLI_utildefines.h"

#include "BKE_library_query.h"

#include "MOD_modifiertypes.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"

static void initData(ModifierData *md)
{
	GpencilLightModifierData *gpmd = (GpencilLightModifierData *)md;
	ARRAY_SET_ITEMS(gpmd->loc, 0.0f, 0.0f, 2.0f);
	gpmd->energy = 10.0f;
	gpmd->ambient = 5.0f;
	gpmd->object = NULL;
}

static void updateDepsgraph(ModifierData *md,
	struct Main *UNUSED(bmain),
	struct Scene *UNUSED(scene),
	Object *object,
	struct DepsNodeHandle *node)
{
	GpencilLightModifierData *lmd = (GpencilLightModifierData *)md;
	if (lmd->object != NULL) {
		DEG_add_object_relation(node, lmd->object, DEG_OB_COMP_GEOMETRY, "Light Modifier");
		DEG_add_object_relation(node, lmd->object, DEG_OB_COMP_TRANSFORM, "Light Modifier");
	}
	DEG_add_object_relation(node, object, DEG_OB_COMP_TRANSFORM, "Light Modifier");
}

static bool isDisabled(ModifierData *md, int UNUSED(userRenderParams))
{
	GpencilLightModifierData *mmd = (GpencilLightModifierData *)md;

	return !mmd->object;
}

static void foreachObjectLink(
	ModifierData *md, Object *ob,
	ObjectWalkFunc walk, void *userData)
{
	GpencilLightModifierData *mmd = (GpencilLightModifierData *)md;

	walk(userData, ob, &mmd->object, IDWALK_CB_NOP);
}

ModifierTypeInfo modifierType_GpencilLight = {
	/* name */              "Light",
	/* structName */        "GpencilLightModifierData",
	/* structSize */        sizeof(GpencilLightModifierData),
	/* type */              eModifierTypeType_Gpencil,
	/* flags */             eModifierTypeFlag_GpencilMod | eModifierTypeFlag_SupportsEditmode |
	                        eModifierTypeFlag_GpencilVFX | eModifierTypeFlag_Single,

	/* copyData */          NULL,
	/* deformVerts */       NULL,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     NULL,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     NULL,
	/* applyModifierEM */   NULL,
	/* deformStroke */      NULL,
	/* generateStrokes */   NULL,
	/* bakeModifierGP */    NULL,
	/* initData */          initData,
	/* requiredDataMask */  NULL,
	/* freeData */          NULL,
	/* isDisabled */        isDisabled,
	/* updateDepsgraph */   updateDepsgraph,
	/* dependsOnTime */     NULL,
	/* dependsOnNormals */	NULL,
	/* foreachObjectLink */ foreachObjectLink,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};
