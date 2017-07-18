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

/** \file blender/modifiers/intern/MOD_gpenciltint.c
 *  \ingroup modifiers
 */

#include <stdio.h>

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"

#include "BLI_utildefines.h"
#include "BLI_ghash.h"

#include "BKE_DerivedMesh.h"
#include "BKE_gpencil.h"
#include "BKE_paint.h"

#include "MOD_modifiertypes.h"
#include "BKE_gpencil.h"

static void initData(ModifierData *md)
{
	GpencilTintModifierData *gpmd = (GpencilTintModifierData *)md;
	gpmd->passindex = 0;
	gpmd->factor = 0;
	gpmd->layername[0] = '\0';

	BKE_gpencil_batch_cache_alldirty();
}

static void copyData(ModifierData *md, ModifierData *target)
{
	GpencilTintModifierData *smd = (GpencilTintModifierData *)md;
	GpencilTintModifierData *tsmd = (GpencilTintModifierData *)target;

	modifier_copyData_generic(md, target);
}

static DerivedMesh *applyModifier(ModifierData *md, Object *ob,
	DerivedMesh *UNUSED(dm),
	ModifierApplyFlag UNUSED(flag))
{
	GpencilTintModifierData *mmd = (GpencilTintModifierData *)md;
	bGPdata *gpd;
	if ((!ob) || (!ob->gpd)) {
		return NULL;
	}
	gpd = ob->gpd;
	GHash *gh = BLI_ghash_str_new("GP_Tint modifier");
	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
				if (mmd->flag & GP_TINT_CREATE_COLORS) {
					/* TODO: add support for same name in two palettes */
					PaletteColor *newpalcolor = BLI_ghash_lookup(gh, gps->palcolor->info);
					if (newpalcolor == NULL) {
						newpalcolor = BKE_palette_color_copy(gps->palette, gps->palcolor);
						BLI_ghash_insert(gh, gps->palcolor->info, newpalcolor);
					}

					if (newpalcolor) {
						BLI_strncpy(gps->colorname, newpalcolor->info, sizeof(gps->colorname));
						gps->palcolor = newpalcolor;
					}
				}

				ED_gpencil_tint_modifier((GpencilTintModifierData *)md, gpl, gps);
			}
		}
	}
	/* free hash buffer */
	if (gh) {
		BLI_ghash_free(gh, NULL, NULL);
		gh = NULL;
	}

	return NULL;
}

ModifierTypeInfo modifierType_GpencilTint = {
	/* name */              "Tint",
	/* structName */        "GpencilTintModifierData",
	/* structSize */        sizeof(GpencilTintModifierData),
	/* type */             	eModifierTypeType_Gpencil,
	/* flags */             eModifierTypeFlag_GpencilMod,

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
