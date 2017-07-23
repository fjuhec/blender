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

/** \file blender/modifiers/intern/MOD_gpencilarray.c
 *  \ingroup modifiers
 */

#include <stdio.h>

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"

#include "BLI_utildefines.h"
#include "BLI_math_vector.h"

#include "BKE_DerivedMesh.h"
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
	ED_gpencil_fill_random_array(gpmd->rnd, 20);
	gpmd->rnd[0] = 1;

	BKE_gpencil_batch_cache_alldirty();
}

static void copyData(ModifierData *md, ModifierData *target)
{
	GpencilArrayModifierData *smd = (GpencilArrayModifierData *)md;
	GpencilArrayModifierData *tsmd = (GpencilArrayModifierData *)target;

	modifier_copyData_generic(md, target);
}

/* helper to create a new object */
static Object *object_add_type(bContext *C,	int type, const char *name)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	SceneLayer *sl = CTX_data_scene_layer(C);
	Object *ob;
	EvaluationContext eval_ctx;
	const float loc[3] = { 0, 0, 0 };
	const float rot[3] = { 0, 0, 0 };

	CTX_data_eval_ctx(C, &eval_ctx);

	ob = BKE_object_add(bmain, scene, sl, type, name);
	copy_v3_v3(ob->loc, loc);
	copy_v3_v3(ob->rot, rot);

	BKE_object_where_is_calc(&eval_ctx, scene, ob);

	DEG_id_type_tag(bmain, ID_OB);
	DEG_relations_tag_update(bmain);
	DEG_id_tag_update(&scene->id, 0);

	/* define size */
	BKE_object_obdata_size_init(ob, GP_OBGPENCIL_DEFAULT_SIZE);

	return ob;
}

static DerivedMesh *applyModifier(ModifierData *md, struct EvaluationContext *UNUSED(eval_ctx), Object *ob,
	DerivedMesh *UNUSED(dm),
	ModifierApplyFlag UNUSED(flag))
{
	GpencilArrayModifierData *mmd = (GpencilArrayModifierData *)md;
	bContext *C = (bContext *)mmd->C;
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	SceneLayer *sl = CTX_data_scene_layer(C);
	SceneCollection *sc = CTX_data_scene_collection(C);
	Object *newob = NULL;
	Base *base_new = NULL;
	int xyz[3], sh;
	float mat[4][4], finalmat[4][4];
	float rot[3];

	if ((!ob) || (!ob->gpd)) {
		return NULL;
	}

	/* reset random */
	mmd->rnd[0] = 1;
	for (int x = 0; x < mmd->count[0]; ++x) {
		for (int y = 0; y < mmd->count[1]; ++y) {
			for (int z = 0; z < mmd->count[2]; ++z) {
				ARRAY_SET_ITEMS(xyz, x, y, z);
				if ((x == 0) && (y == 0) && (z == 0)) {
					continue;
				}
				ED_gpencil_array_modifier(0, mmd, ob, xyz, mat);
				mul_m4_m4m4(finalmat, mat, ob->obmat);

				newob = object_add_type(C, OB_GPENCIL, md->name);
				newob->gpd = ob->gpd;
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
	return NULL;
}

ModifierTypeInfo modifierType_GpencilArray = {
	/* name */              "Array",
	/* structName */        "GpencilArrayModifierData",
	/* structSize */        sizeof(GpencilArrayModifierData),
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
