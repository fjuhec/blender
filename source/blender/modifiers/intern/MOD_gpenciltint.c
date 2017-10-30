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

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_gpencil.h"
#include "BKE_paint.h"

#include "DEG_depsgraph.h"

#include "MOD_modifiertypes.h"

static void initData(ModifierData *md)
{
	GpencilTintModifierData *gpmd = (GpencilTintModifierData *)md;
	gpmd->pass_index = 0;
	gpmd->factor = 0;
	gpmd->layername[0] = '\0';
	gpmd->flag |= GP_TINT_CREATE_COLORS;
}

static void copyData(ModifierData *md, ModifierData *target)
{
	modifier_copyData_generic(md, target);
}

static void bakeModifierGP(const bContext *C, const EvaluationContext *UNUSED(eval_ctx),
                           ModifierData *md, Object *ob)
{
	GpencilTintModifierData *mmd = (GpencilTintModifierData *)md;
	bGPdata *gpd;
	Palette *newpalette = NULL;
	if ((!ob) || (!ob->data)) {
		return;
	}
	gpd = ob->data;
	GHash *gh_layer = BLI_ghash_str_new("GP_Tint Layer modifier");
	GHash *gh_color;

	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
				/* look for palette */
				gh_color = (GHash *)BLI_ghash_lookup(gh_layer, gps->palette->id.name);
				if (gh_color == NULL) {
					gh_color = BLI_ghash_str_new("GP_Tint Color modifier");
					BLI_ghash_insert(gh_layer, gps->palette->id.name, gh_color);
				}

				/* look for color */
				PaletteColor *newpalcolor = (PaletteColor *)BLI_ghash_lookup(gh_color, gps->palcolor->info);
				if (newpalcolor == NULL) {
					if (mmd->flag & GP_TINT_CREATE_COLORS) {
						if (!newpalette) {
							newpalette = BKE_palette_add(G.main, "Palette");
						}
						newpalcolor = BKE_palette_color_copy(newpalette, gps->palcolor);
						BLI_strncpy(gps->colorname, newpalcolor->info, sizeof(gps->colorname));
						gps->palcolor = newpalcolor;
					}
					else {
						newpalcolor = gps->palcolor;
					}
					BLI_ghash_insert(gh_color, gps->palcolor->info, newpalcolor);
					BKE_gpencil_tint_modifier(-1, (GpencilTintModifierData *)md, ob, gpl, gps);
				}
				else {
					gps->palcolor = newpalcolor;
				}
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
}

ModifierTypeInfo modifierType_GpencilTint = {
	/* name */              "Tint",
	/* structName */        "GpencilTintModifierData",
	/* structSize */        sizeof(GpencilTintModifierData),
	/* type */              eModifierTypeType_Gpencil,
	/* flags */             eModifierTypeFlag_GpencilMod | eModifierTypeFlag_SupportsEditmode,

	/* copyData */          copyData,
	/* deformVerts */       NULL,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     NULL,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     NULL,
	/* applyModifierEM */   NULL,
	/* deformStrokes */     NULL,
	/* generateStrokes */   NULL,
	/* bakeModifierGP */    bakeModifierGP,
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
