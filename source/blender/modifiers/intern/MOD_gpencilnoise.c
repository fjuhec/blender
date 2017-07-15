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

/** \file blender/modifiers/intern/MOD_gpencilnoise.c
 *  \ingroup modifiers
 */

#include <stdio.h>

#include "DNA_scene_types.h"

#include "BLI_utildefines.h"

#include "MOD_modifiertypes.h"

static void initData(ModifierData *md)
{
	GpencilNoiseModifierData *gpmd = (GpencilNoiseModifierData *)md;
	gpmd->passindex = 0;
	gpmd->seed = 1;     
	gpmd->factor = 0.5f;

}

static void copyData(ModifierData *md, ModifierData *target)
{
	GpencilNoiseModifierData *smd = (GpencilNoiseModifierData *)md;
	GpencilNoiseModifierData *tsmd = (GpencilNoiseModifierData *)target;

	modifier_copyData_generic(md, target);
}

ModifierTypeInfo modifierType_GpencilNoise = {
	/* name */              "GP_Noise",
	/* structName */        "GpencilNoiseModifierData",
	/* structSize */        sizeof(GpencilNoiseModifierData),
	/* type */             	eModifierTypeType_Gpencil,
	/* flags */             eModifierTypeFlag_GpencilMod,

	/* copyData */          copyData,
	/* deformVerts */       NULL,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     NULL,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     NULL,
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
