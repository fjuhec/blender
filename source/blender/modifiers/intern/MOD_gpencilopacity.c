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

/** \file blender/modifiers/intern/MOD_gpencilopacity.c
 *  \ingroup modifiers
 */

#include <stdio.h>

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"

#include "BLI_utildefines.h"
#include "BLI_ghash.h"

#include "BKE_global.h"
#include "BKE_DerivedMesh.h"
#include "BKE_gpencil.h"
#include "BKE_paint.h"

#include "MOD_modifiertypes.h"
#include "BKE_gpencil.h"

static void initData(ModifierData *md)
{
	GpencilOpacityModifierData *gpmd = (GpencilOpacityModifierData *)md;
	gpmd->passindex = 0;
	gpmd->factor = 1;
	gpmd->size = 1.0f;
	gpmd->layername[0] = '\0';
	gpmd->target = NULL;

	BKE_gpencil_batch_cache_alldirty();
}

static void copyData(ModifierData *md, ModifierData *target)
{
	GpencilOpacityModifierData *smd = (GpencilOpacityModifierData *)md;
	GpencilOpacityModifierData *tsmd = (GpencilOpacityModifierData *)target;

	modifier_copyData_generic(md, target);
}

static DerivedMesh *applyModifier(ModifierData *md, struct EvaluationContext *UNUSED(eval_ctx), Object *ob,
	DerivedMesh *UNUSED(dm),
	ModifierApplyFlag UNUSED(flag))
{
	GpencilOpacityModifierData *mmd = (GpencilOpacityModifierData *)md;
	bGPdata *gpd;
	Palette *newpalette = NULL;
	if ((!ob) || (!ob->gpd)) {
		return NULL;
	}
	gpd = ob->gpd;
	GHash *gh_layer = BLI_ghash_str_new("GP_Opacity Layer modifier");
	GHash *gh_color;

	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
				/* create a new set of colors */
				if (mmd->flag & GP_TINT_CREATE_COLORS) {
					
					/* look for palette */
					gh_color = (GHash *)BLI_ghash_lookup(gh_layer, gps->palette->id.name);
					if (gh_color == NULL) {
						gh_color = BLI_ghash_str_new("GP_Opacity Color modifier");
						BLI_ghash_insert(gh_layer, gps->palette->id.name, gh_color);
					}

					/* look for color */
					PaletteColor *newpalcolor = (PaletteColor *) BLI_ghash_lookup(gh_color, gps->palcolor->info);
					if (newpalcolor == NULL) {
						if (!newpalette) {
							newpalette = BKE_palette_add(G.main, "Palette");
						}
						newpalcolor = BKE_palette_color_copy(newpalette, gps->palcolor);
						BLI_ghash_insert(gh_color, gps->palcolor->info, newpalcolor);
					}

					if (newpalcolor) {
						BLI_strncpy(gps->colorname, newpalcolor->info, sizeof(gps->colorname));
						gps->palcolor = newpalcolor;
					}
				}

				ED_gpencil_opacity_modifier(-1, (GpencilOpacityModifierData *)md, gpl, gps);
			}
		}
	}
	/* free hash buffers */
	GHashIterator *ihash = BLI_ghashIterator_new(gh_layer);
	while (!BLI_ghashIterator_done(ihash)) {
		GHash *gh = BLI_ghashIterator_getValue(ihash);
		if (gh) {
			BLI_ghash_free(gh, NULL, NULL);
			gh = NULL;
		}
		BLI_ghashIterator_step(ihash);
	}
	BLI_ghashIterator_free(ihash);

	if (gh_layer) {
		BLI_ghash_free(gh_layer, NULL, NULL);
		gh_layer = NULL;
	}

	return NULL;
}

ModifierTypeInfo modifierType_GpencilOpacity = {
	/* name */              "Opacity",
	/* structName */        "GpencilOpacityModifierData",
	/* structSize */        sizeof(GpencilOpacityModifierData),
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
