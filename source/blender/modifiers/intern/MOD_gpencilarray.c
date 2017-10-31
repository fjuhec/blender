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

/** \file blender/modifiers/intern/MOD_gpencilarray.c
 *  \ingroup modifiers
 */

#include <stdio.h>

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"

#include "BLI_utildefines.h"
#include "BLI_math_vector.h"

#include "BKE_gpencil.h"
#include "BKE_context.h"
#include "BKE_object.h"
#include "BKE_main.h"
#include "BKE_scene.h"
#include "BKE_layer.h"
#include "BKE_collection.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"

#include "MOD_modifiertypes.h"
#include "MOD_gpencil_util.h"

static void initData(ModifierData *md)
{
	GpencilArrayModifierData *gpmd = (GpencilArrayModifierData *)md;
	gpmd->count[0] = 1;
	gpmd->count[1] = 1;
	gpmd->count[2] = 1;
	gpmd->offset[0] = 1.0f;
	gpmd->offset[1] = 1.0f;
	gpmd->offset[2] = 1.0f;
	gpmd->shift[0] = 0.0f;
	gpmd->shift[2] = 0.0f;
	gpmd->shift[3] = 0.0f;
	gpmd->scale[0] = 1.0f;
	gpmd->scale[1] = 1.0f;
	gpmd->scale[2] = 1.0f;
	gpmd->rnd_rot = 0.5f;
	gpmd->rnd_size = 0.5f;
	gpmd->lock_axis |= GP_LOCKAXIS_X;
	
	/* fill random values */
	gp_mod_fill_random_array(gpmd->rnd, 20);
	gpmd->rnd[0] = 1;
}

static void copyData(ModifierData *md, ModifierData *target)
{
	modifier_copyData_generic(md, target);
}

/* helper to create a new object */
static Object *object_add_type(const bContext *C,	int UNUSED(type), const char *UNUSED(name), Object *from_ob)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	Object *ob;
	const float loc[3] = { 0, 0, 0 };
	const float rot[3] = { 0, 0, 0 };

	ob = BKE_object_copy(bmain, from_ob);
	BKE_collection_object_add_from(scene, from_ob, ob);

	copy_v3_v3(ob->loc, loc);
	copy_v3_v3(ob->rot, rot);

	DEG_id_type_tag(bmain, ID_OB);
	DEG_relations_tag_update(bmain);
	DEG_id_tag_update(&scene->id, 0);

	return ob;
}

static void bakeModifierGP(const bContext *C, const EvaluationContext *UNUSED(eval_ctx),
                           ModifierData *md, Object *ob)
{
	GpencilArrayModifierData *mmd = (GpencilArrayModifierData *)md;
	ModifierData *fmd;
	Main *bmain = CTX_data_main(C);
	Object *newob = NULL;
	int xyz[3], sh;
	float mat[4][4], finalmat[4][4];
	float rot[3];

	/* reset random */
	mmd->rnd[0] = 1;
	for (int x = 0; x < mmd->count[0]; x++) {
		for (int y = 0; y < mmd->count[1]; y++) {
			for (int z = 0; z < mmd->count[2]; z++) {
				ARRAY_SET_ITEMS(xyz, x, y, z);
				if ((x == 0) && (y == 0) && (z == 0)) {
					continue;
				}
				BKE_gpencil_array_modifier(0, mmd, ob, xyz, mat);
				mul_m4_m4m4(finalmat, mat, ob->obmat);

				/* create a new object and new gp datablock */
				// XXX (aligorith): Whether this creates and discards and extra GP datablock instance
				newob = object_add_type(C, OB_GPENCIL, md->name, ob);
				id_us_min((ID *)ob->data);
				newob->data = BKE_gpencil_data_duplicate(bmain, ob->data, false);
				/* remove array on destination object */
				fmd = (ModifierData *) BLI_findstring(&newob->modifiers, md->name, offsetof(ModifierData, name));
				if (fmd) {
					BLI_remlink(&newob->modifiers, fmd);
					modifier_free(fmd);
				}
				/* moves to new origin */
				sh = x;
				if (mmd->lock_axis == GP_LOCKAXIS_Y) {
					sh = y;
				}
				if (mmd->lock_axis == GP_LOCKAXIS_Z) {
					sh = z;
				}
				madd_v3_v3fl(finalmat[3], mmd->shift, sh);
				copy_v3_v3(newob->loc, finalmat[3]);
				/* apply rotation */
				mat4_to_eul(rot, finalmat);
				copy_v3_v3(newob->rot, rot);
				/* apply scale */
				ARRAY_SET_ITEMS(newob->size, finalmat[0][0], finalmat[1][1], finalmat[2][2]);
			}
		}
	}
}

ModifierTypeInfo modifierType_GpencilArray = {
	/* name */              "Array",
	/* structName */        "GpencilArrayModifierData",
	/* structSize */        sizeof(GpencilArrayModifierData),
	/* type */              eModifierTypeType_Gpencil,
	/* flags */             eModifierTypeFlag_GpencilMod,

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
