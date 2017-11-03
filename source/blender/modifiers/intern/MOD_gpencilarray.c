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
 * Contributor(s): Antonio Vazquez, Joshua Leung
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 */

/** \file blender/modifiers/intern/MOD_gpencilarray.c
 *  \ingroup modifiers
 */

#include <stdio.h>
 
#include "MEM_guardedalloc.h"

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BKE_gpencil.h"
#include "BKE_context.h"
#include "BKE_global.h"
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
	gpmd->flag |= GP_ARRAY_MAKE_OBJECTS;
	
	/* fill random values */
	gp_mod_fill_random_array(gpmd->rnd, 20);
	gpmd->rnd[0] = 1;
}

static void copyData(ModifierData *md, ModifierData *target)
{
	modifier_copyData_generic(md, target);
}

/* -------------------------------- */

/* helper function for per-instance positioning */
void BKE_gpencil_array_modifier_instance_tfm(GpencilArrayModifierData *mmd, const int elem_idx[3], float r_mat[4][4])
{
	float offset[3], rot[3], scale[3];
	int ri = mmd->rnd[0];
	float factor;
	
	offset[0] = mmd->offset[0] * elem_idx[0];
	offset[1] = mmd->offset[1] * elem_idx[1];
	offset[2] = mmd->offset[2] * elem_idx[2];
	
	/* rotation */
	if (mmd->flag & GP_ARRAY_RANDOM_ROT) {
		factor = mmd->rnd_rot * mmd->rnd[ri];
		mul_v3_v3fl(rot, mmd->rot, factor);
		add_v3_v3(rot, mmd->rot);
	}
	else {
		copy_v3_v3(rot, mmd->rot);
	}
	
	/* scale */
	if (mmd->flag & GP_ARRAY_RANDOM_SIZE) {
		factor = mmd->rnd_size * mmd->rnd[ri];
		mul_v3_v3fl(scale, mmd->scale, factor);
		add_v3_v3(scale, mmd->scale);
	}
	else {
		copy_v3_v3(scale, mmd->scale);
	}
	
	/* advance random index */
	mmd->rnd[0]++;
	if (mmd->rnd[0] > 19) {
		mmd->rnd[0] = 1;
	}
	
	/* calculate matrix */
	loc_eul_size_to_mat4(r_mat, offset, rot, scale);
}

/* -------------------------------- */

/* array modifier - generate geometry callback (for viewport/rendering) */
/* TODO: How to skip this for the simplify options?   -->  !GP_SIMPLIFY_MODIF(ts, playing) */
static void generate_geometry(ModifierData *md, const EvaluationContext *eval_ctx,
	                          Object *ob, bGPDlayer *gpl, bGPDframe *gpf,
	                          int modifier_index)
{
	GpencilArrayModifierData *mmd = (GpencilArrayModifierData *)md;
	ListBase stroke_cache = {NULL, NULL};
	bGPDstroke *gps;
	int idx;
	
	/* Check which strokes we can use once, and store those results in an array
	 * for quicker checking of what's valid (since string comparisons are expensive)
	 */
	const int num_strokes = BLI_listbase_count(&gpf->strokes);
	int num_valid = 0;
	
	bool *valid_strokes = MEM_callocN(sizeof(bool) * num_strokes, "GP ArrayMod valid_strokes");
	
	for (gps = gpf->strokes.first, idx = 0; gps; gps = gps->next, idx++) {
		/* Record whether this stroke can be used
		 * ATTENTION: The logic here is the inverse of what's used everywhere else!
		 */
		if (is_stroke_affected_by_modifier(
		        mmd->layername, mmd->pass_index, 1, gpl, gps,
		        mmd->flag & GP_ARRAY_INVERSE_LAYER, mmd->flag & GP_ARRAY_INVERSE_PASS))
		{
			valid_strokes[idx] = true;
			num_valid++;
		}
	}
	
	/* Early exit if no strokes can be copied */
	if (num_valid == 0) {
		if (G.debug & G_DEBUG) {
			printf("GP Array Mod - No strokes to be included\n");
		}
		
		MEM_SAFE_FREE(valid_strokes);
		return;
	}
	
	
	/* Generate new instances of all existing strokes,
	 * keeping each instance together so they maintain
	 * the correct ordering relative to each other
	 */
	for (int x = 0; x < mmd->count[0]; x++) {
		for (int y = 0; y < mmd->count[1]; y++) {
			for (int z = 0; z < mmd->count[2]; z++) {
				/* original strokes are at index = 0,0,0 */
				if ((x == 0) && (y == 0) && (z == 0)) {
					continue;
				}
				
				/* Compute transforms for this instance */
				const int elem_idx[3] = {x, y, z};
				float mat[4][4];
				
				BKE_gpencil_array_modifier_instance_tfm(mmd, elem_idx, mat);
				
				/* apply shift */
				int sh = x;
				if (mmd->lock_axis == GP_LOCKAXIS_Y) {
					sh = y;
				}
				if (mmd->lock_axis == GP_LOCKAXIS_Z) {
					sh = z;
				}
				madd_v3_v3fl(mat[3], mmd->shift, sh);
				
				/* Duplicate original strokes to create this instance */
				for (gps = gpf->strokes.first, idx = 0; gps; gps = gps->next, idx++) {
					/* check if stroke can be duplicated */
					if (valid_strokes[idx]) {
						/* Duplicate stroke */
						bGPDstroke *gps_dst = MEM_dupallocN(gps);
						if (modifier_index > -1) {
							/* in these cases, we're operating on data stored in the
							 * derived caches, so copies will get freed
							 */
							gps_dst->palcolor = MEM_dupallocN(gps->palcolor);
						}
						gps_dst->points = MEM_dupallocN(gps->points);
						BKE_gpencil_stroke_weights_duplicate(gps, gps_dst);
						
						gps_dst->triangles = MEM_dupallocN(gps->triangles);
						
						/* Move points */
						for (int i = 0; i < gps->totpoints; i++) {
							bGPDspoint *pt = &gps_dst->points[i];
							mul_m4_v3(mat, &pt->x);
						}
						
						/* Add new stroke to cache, to be added to the frame once
						 * all duplicates have been made
						 */
						BLI_addtail(&stroke_cache, gps_dst);
					}
				}
			}
		}
	}
		
	/* merge newly created stroke instances back into the main stroke list */
	BLI_movelisttolist(&gpf->strokes, &stroke_cache);
	
	/* free temp data */
	MEM_SAFE_FREE(valid_strokes);
}

/* bakeModifierGP - "Bake to Data" Mode */
static void bakeModifierGP_strokes(const bContext *C, const EvaluationContext *eval_ctx,
                                      ModifierData *md, Object *ob)
{
	bGPdata *gpd = ob->data;
	
	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			generate_geometry(md, eval_ctx, ob, gpl, gpf, -1);
		}
	}
}

/* -------------------------------- */

/* helper to create a new object */
static Object *array_instance_add_ob_copy(const bContext *C, Object *from_ob)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	Object *ob;
	
	ob = BKE_object_copy(bmain, from_ob);
	BKE_collection_object_add_from(scene, from_ob, ob);
	
	zero_v3(ob->loc);
	zero_v3(ob->rot);
	
	DEG_id_type_tag(bmain, ID_OB);
	DEG_relations_tag_update(bmain);
	DEG_id_tag_update(&scene->id, 0);
	
	return ob;
}

/* bakeModifierGP - "Make Objects" Mode */
static void bakeModifierGP_objects(const bContext *C, ModifierData *md, Object *ob)
{
	GpencilArrayModifierData *mmd = (GpencilArrayModifierData *)md;
	Main *bmain = CTX_data_main(C);
	
	/* reset random */
	mmd->rnd[0] = 1;
	
	/* generate instances as objects */
	for (int x = 0; x < mmd->count[0]; x++) {
		for (int y = 0; y < mmd->count[1]; y++) {
			for (int z = 0; z < mmd->count[2]; z++) {
				Object *newob;
				ModifierData *fmd;
				
				const int elem_idx[3] = {x, y, z};
				float mat[4][4], finalmat[4][4];
				int sh;
				
				/* original strokes are at index = 0,0,0 */
				if ((x == 0) && (y == 0) && (z == 0)) {
					continue;
				}
				
				/* compute transform for instance */
				BKE_gpencil_array_modifier_instance_tfm(mmd, elem_idx, mat);
				mul_m4_m4m4(finalmat, ob->obmat, mat);
				
				/* moves to new origin */
				sh = x;
				if (mmd->lock_axis == GP_LOCKAXIS_Y) {
					sh = y;
				}
				if (mmd->lock_axis == GP_LOCKAXIS_Z) {
					sh = z;
				}
				madd_v3_v3fl(finalmat[3], mmd->shift, sh);

				/* Create a new object
				 *
				 * NOTE: Copies share the same original GP datablock
				 * Artists can later user make_single_user on these
				 * to make them unique (if necessary), without too
				 * much extra memory usage.
				 */
				newob = array_instance_add_ob_copy(C, ob);
				
				/* remove array on destination object */
				fmd = (ModifierData *)BLI_findstring(&newob->modifiers, md->name, offsetof(ModifierData, name));
				if (fmd) {
					BLI_remlink(&newob->modifiers, fmd);
					modifier_free(fmd);
				}
				
				/* copy transforms to destination object */
				copy_m4_m4(newob->obmat, finalmat);
				
				copy_v3_v3(newob->loc, finalmat[3]);
				mat4_to_eul(newob->rot, finalmat);
				mat4_to_size(newob->size, finalmat);
			}
		}
	}
}

/* -------------------------------- */

/* Generic "generateStrokes" callback */
static void generateStrokes(ModifierData *md, const EvaluationContext *eval_ctx,
	                        Object *ob, bGPDlayer *gpl, bGPDframe *gpf,
	                        int modifier_index)
{
	GpencilArrayModifierData *mmd = (GpencilArrayModifierData *)md;
	
	/* When the "make_objects" flag is set, this modifier is handled as part of the 
	 * draw engine instead. The main benefit is that the instances won't suffer from
	 * z-ordering problems.
	 *
	 * FIXME: Ultimately, the draw-engine hack here shouldn't be necessary, but until
	 *        we find a better fix to the z-ordering problems, it's better to have
	 *        working functionality
	 */
	if ((mmd->flag & GP_ARRAY_MAKE_OBJECTS) == 0) {
		generate_geometry(md, eval_ctx, ob, gpl, gpf, modifier_index);
	}
}

/* Generic "bakeModifierGP" callback */
static void bakeModifierGP(const bContext *C, const EvaluationContext *eval_ctx,
                           ModifierData *md, Object *ob)
{
	GpencilArrayModifierData *mmd = (GpencilArrayModifierData *)md;
	
	/* Create new objects or add all to current datablock.
	 * Sometimes it's useful to have the option to do either of these...
	 */
	if (mmd->flag & GP_ARRAY_MAKE_OBJECTS) {
		bakeModifierGP_objects(C, md, ob);
	}
	else {
		bakeModifierGP_strokes(C, eval_ctx, md, ob);
	}
}

ModifierTypeInfo modifierType_GpencilArray = {
	/* name */              "Array",
	/* structName */        "GpencilArrayModifierData",
	/* structSize */        sizeof(GpencilArrayModifierData),
	/* type */              eModifierTypeType_Gpencil,
	/* flags */             eModifierTypeFlag_GpencilMod | eModifierTypeFlag_SupportsEditmode,

	/* copyData */          copyData,
	/* deformVerts */       NULL,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     NULL,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     NULL,
	/* applyModifierEM */   NULL,
	/* deformStroke */      NULL,
	/* generateStrokes */   generateStrokes,
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
