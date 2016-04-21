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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2005 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/modifier_calc.c
 *  \ingroup bke
 */

#include "MEM_guardedalloc.h"

#include "DNA_cloth_types.h"
#include "DNA_key_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BLI_math.h"
#include "BLI_linklist.h"
#include "BLI_listbase.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_depsgraph.h"
#include "BKE_editmesh.h"
#include "BKE_key.h"
#include "BKE_modifier.h"
#include "BKE_mesh.h"
#include "BKE_object.h"
#include "BKE_object_deform.h"
#include "BKE_paint.h"
#include "BKE_texture.h"
#include "BKE_multires.h"
#include "BKE_deform.h"

#ifdef WITH_GAMEENGINE
#include "BKE_navmesh_conversion.h"
static DerivedMesh *navmesh_dm_createNavMeshForVisualization(DerivedMesh *dm);
#endif

#include "GPU_buffers.h"
#include "GPU_glew.h"

#ifdef WITH_OPENSUBDIV
#  include "DNA_userdef_types.h"
#endif

/* very slow! enable for testing only! */
//#define USE_MODIFIER_VALIDATE

#ifdef USE_MODIFIER_VALIDATE
#  define ASSERT_IS_VALID_DM(dm) (BLI_assert((dm == NULL) || (DM_is_valid(dm) == true)))
#else
#  define ASSERT_IS_VALID_DM(dm)
#endif

static void add_shapekey_layers(DerivedMesh *dm, Mesh *me, Object *ob);

/* -------------------------------------------------------------------- */

static void DM_calc_loop_normals(DerivedMesh *dm, const bool use_split_normals, float split_angle)
{
	dm->calcLoopNormals(dm, use_split_normals, split_angle);
	dm->dirty |= DM_DIRTY_TESS_CDLAYERS;
}

DerivedMesh *mesh_create_derived(Mesh *me, float (*vertCos)[3])
{
	DerivedMesh *dm = CDDM_from_mesh(me);
	
	if (!dm)
		return NULL;
	
	if (vertCos) {
		CDDM_apply_vert_coords(dm, vertCos);
	}

	return dm;
}

DerivedMesh *mesh_create_derived_for_modifier(
        Scene *scene, Object *ob,
        ModifierData *md, int build_shapekey_layers)
{
	Mesh *me = ob->data;
	const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
	DerivedMesh *dm;
	KeyBlock *kb;

	md->scene = scene;
	
	if (!(md->mode & eModifierMode_Realtime)) {
		return NULL;
	}

	if (mti->isDisabled && mti->isDisabled(md, 0)) {
		return NULL;
	}
	
	if (build_shapekey_layers && me->key && (kb = BLI_findlink(&me->key->block, ob->shapenr - 1))) {
		BKE_keyblock_convert_to_mesh(kb, me);
	}
	
	if (mti->type == eModifierTypeType_OnlyDeform) {
		int numVerts;
		float (*deformedVerts)[3] = BKE_mesh_vertexCos_get(me, &numVerts);

		modwrap_deformVerts(md, ob, NULL, deformedVerts, numVerts, 0);
		dm = mesh_create_derived(me, deformedVerts);

		if (build_shapekey_layers)
			add_shapekey_layers(dm, me, ob);
		
		MEM_freeN(deformedVerts);
	}
	else {
		DerivedMesh *tdm = mesh_create_derived(me, NULL);

		if (build_shapekey_layers)
			add_shapekey_layers(tdm, me, ob);
		
		dm = modwrap_applyModifier(md, ob, tdm, 0);
		ASSERT_IS_VALID_DM(dm);

		if (tdm != dm) tdm->release(tdm);
	}

	return dm;
}

static float (*get_editbmesh_orco_verts(BMEditMesh *em))[3]
{
	BMIter iter;
	BMVert *eve;
	float (*orco)[3];
	int i;

	/* these may not really be the orco's, but it's only for preview.
	 * could be solver better once, but isn't simple */
	
	orco = MEM_mallocN(sizeof(float) * 3 * em->bm->totvert, "BMEditMesh Orco");

	BM_ITER_MESH_INDEX (eve, &iter, em->bm, BM_VERTS_OF_MESH, i) {
		copy_v3_v3(orco[i], eve->co);
	}
	
	return orco;
}

/* orco custom data layer */
static float (*get_orco_coords_dm(Object *ob, BMEditMesh *em, int layer, int *free))[3]
{
	*free = 0;

	if (layer == CD_ORCO) {
		/* get original coordinates */
		*free = 1;

		if (em)
			return get_editbmesh_orco_verts(em);
		else
			return BKE_mesh_orco_verts_get(ob);
	}
	else if (layer == CD_CLOTH_ORCO) {
		/* apply shape key for cloth, this should really be solved
		 * by a more flexible customdata system, but not simple */
		if (!em) {
			ClothModifierData *clmd = (ClothModifierData *)modifiers_findByType(ob, eModifierType_Cloth);
			KeyBlock *kb = BKE_keyblock_from_key(BKE_key_from_object(ob), clmd->sim_parms->shapekey_rest);

			if (kb && kb->data) {
				return kb->data;
			}
		}

		return NULL;
	}

	return NULL;
}

static DerivedMesh *create_orco_dm(Object *ob, Mesh *me, BMEditMesh *em, int layer)
{
	DerivedMesh *dm;
	float (*orco)[3];
	int free;

	if (em) {
		dm = CDDM_from_editbmesh(em, false, false);
	}
	else {
		dm = CDDM_from_mesh(me);
	}

	orco = get_orco_coords_dm(ob, em, layer, &free);

	if (orco) {
		CDDM_apply_vert_coords(dm, orco);
		if (free) MEM_freeN(orco);
	}

	return dm;
}

static void add_orco_dm(
        Object *ob, BMEditMesh *em, DerivedMesh *dm,
        DerivedMesh *orcodm, int layer)
{
	float (*orco)[3], (*layerorco)[3];
	int totvert, free;

	totvert = dm->getNumVerts(dm);

	if (orcodm) {
		orco = MEM_callocN(sizeof(float[3]) * totvert, "dm orco");
		free = 1;

		if (orcodm->getNumVerts(orcodm) == totvert)
			orcodm->getVertCos(orcodm, orco);
		else
			dm->getVertCos(dm, orco);
	}
	else
		orco = get_orco_coords_dm(ob, em, layer, &free);

	if (orco) {
		if (layer == CD_ORCO)
			BKE_mesh_orco_verts_transform(ob->data, orco, totvert, 0);

		if (!(layerorco = DM_get_vert_data_layer(dm, layer))) {
			DM_add_vert_layer(dm, layer, CD_CALLOC, NULL);
			layerorco = DM_get_vert_data_layer(dm, layer);
		}

		memcpy(layerorco, orco, sizeof(float) * 3 * totvert);
		if (free) MEM_freeN(orco);
	}
}

/* weight paint colors */

/* Something of a hack, at the moment deal with weightpaint
 * by tucking into colors during modifier eval, only in
 * wpaint mode. Works ok but need to make sure recalc
 * happens on enter/exit wpaint.
 */

void weight_to_rgb(float r_rgb[3], const float weight)
{
	const float blend = ((weight / 2.0f) + 0.5f);

	if (weight <= 0.25f) {    /* blue->cyan */
		r_rgb[0] = 0.0f;
		r_rgb[1] = blend * weight * 4.0f;
		r_rgb[2] = blend;
	}
	else if (weight <= 0.50f) {  /* cyan->green */
		r_rgb[0] = 0.0f;
		r_rgb[1] = blend;
		r_rgb[2] = blend * (1.0f - ((weight - 0.25f) * 4.0f));
	}
	else if (weight <= 0.75f) {  /* green->yellow */
		r_rgb[0] = blend * ((weight - 0.50f) * 4.0f);
		r_rgb[1] = blend;
		r_rgb[2] = 0.0f;
	}
	else if (weight <= 1.0f) {  /* yellow->red */
		r_rgb[0] = blend;
		r_rgb[1] = blend * (1.0f - ((weight - 0.75f) * 4.0f));
		r_rgb[2] = 0.0f;
	}
	else {
		/* exceptional value, unclamped or nan,
		 * avoid uninitialized memory use */
		r_rgb[0] = 1.0f;
		r_rgb[1] = 0.0f;
		r_rgb[2] = 1.0f;
	}
}

/* draw_flag's for calc_weightpaint_vert_color */
enum {
	/* only one of these should be set, keep first (for easy bit-shifting) */
	CALC_WP_GROUP_USER_ACTIVE   = (1 << 1),
	CALC_WP_GROUP_USER_ALL      = (1 << 2),

	CALC_WP_MULTIPAINT          = (1 << 3),
	CALC_WP_AUTO_NORMALIZE      = (1 << 4),
	CALC_WP_MIRROR_X            = (1 << 5),
};

typedef struct DMWeightColorInfo {
	const ColorBand *coba;
	const char *alert_color;
} DMWeightColorInfo;

static int dm_drawflag_calc(const ToolSettings *ts, const Mesh *me)
{
	return ((ts->multipaint ? CALC_WP_MULTIPAINT : 0) |
	        /* CALC_WP_GROUP_USER_ACTIVE or CALC_WP_GROUP_USER_ALL */
	        (1 << ts->weightuser) |
	        (ts->auto_normalize ? CALC_WP_AUTO_NORMALIZE : 0) |
	        ((me->editflag & ME_EDIT_MIRROR_X) ? CALC_WP_MIRROR_X : 0));
}

static void weightpaint_color(unsigned char r_col[4], DMWeightColorInfo *dm_wcinfo, const float input)
{
	float colf[4];

	if (dm_wcinfo && dm_wcinfo->coba) {
		do_colorband(dm_wcinfo->coba, input, colf);
	}
	else {
		weight_to_rgb(colf, input);
	}

	/* don't use rgb_float_to_uchar() here because
	 * the resulting float doesn't need 0-1 clamp check */
	r_col[0] = (unsigned char)(colf[0] * 255.0f);
	r_col[1] = (unsigned char)(colf[1] * 255.0f);
	r_col[2] = (unsigned char)(colf[2] * 255.0f);
	r_col[3] = 255;
}

static void calc_weightpaint_vert_color(
        unsigned char r_col[4],
        const MDeformVert *dv,
        DMWeightColorInfo *dm_wcinfo,
        const int defbase_tot, const int defbase_act,
        const bool *defbase_sel, const int defbase_sel_tot,
        const int draw_flag)
{
	float input = 0.0f;
	
	bool show_alert_color = false;

	if ((defbase_sel_tot > 1) && (draw_flag & CALC_WP_MULTIPAINT)) {
		/* Multi-Paint feature */
		input = BKE_defvert_multipaint_collective_weight(
		        dv, defbase_tot, defbase_sel, defbase_sel_tot, (draw_flag & CALC_WP_AUTO_NORMALIZE) != 0);

		/* make it black if the selected groups have no weight on a vertex */
		if (input == 0.0f) {
			show_alert_color = true;
		}
	}
	else {
		/* default, non tricky behavior */
		input = defvert_find_weight(dv, defbase_act);

		if (draw_flag & CALC_WP_GROUP_USER_ACTIVE) {
			if (input == 0.0f) {
				show_alert_color = true;
			}
		}
		else if (draw_flag & CALC_WP_GROUP_USER_ALL) {
			if (input == 0.0f) {
				show_alert_color = defvert_is_weight_zero(dv, defbase_tot);
			}
		}
	}

	if (show_alert_color == false) {
		CLAMP(input, 0.0f, 1.0f);
		weightpaint_color(r_col, dm_wcinfo, input);
	}
	else {
		copy_v3_v3_char((char *)r_col, dm_wcinfo->alert_color);
		r_col[3] = 255;
	}
}

static DMWeightColorInfo G_dm_wcinfo;

void vDM_ColorBand_store(const ColorBand *coba, const char alert_color[4])
{
	G_dm_wcinfo.coba        = coba;
	G_dm_wcinfo.alert_color = alert_color;
}

/**
 * return an array of vertex weight colors, caller must free.
 *
 * \note that we could save some memory and allocate RGB only but then we'd need to
 * re-arrange the colors when copying to the face since MCol has odd ordering,
 * so leave this as is - campbell
 */
static void calc_weightpaint_vert_array(
        Object *ob, DerivedMesh *dm, int const draw_flag, DMWeightColorInfo *dm_wcinfo,
        unsigned char (*r_wtcol_v)[4])
{
	MDeformVert *dv = DM_get_vert_data_layer(dm, CD_MDEFORMVERT);
	int numVerts = dm->getNumVerts(dm);

	if (dv && (ob->actdef != 0)) {
		unsigned char (*wc)[4] = r_wtcol_v;
		unsigned int i;

		/* variables for multipaint */
		const int defbase_tot = BLI_listbase_count(&ob->defbase);
		const int defbase_act = ob->actdef - 1;

		int defbase_sel_tot = 0;
		bool *defbase_sel = NULL;

		if (draw_flag & CALC_WP_MULTIPAINT) {
			defbase_sel = BKE_object_defgroup_selected_get(ob, defbase_tot, &defbase_sel_tot);

			if (defbase_sel_tot > 1 && (draw_flag & CALC_WP_MIRROR_X)) {
				BKE_object_defgroup_mirror_selection(ob, defbase_tot, defbase_sel, defbase_sel, &defbase_sel_tot);
			}
		}

		for (i = numVerts; i != 0; i--, wc++, dv++) {
			calc_weightpaint_vert_color((unsigned char *)wc, dv, dm_wcinfo, defbase_tot, defbase_act, defbase_sel, defbase_sel_tot, draw_flag);
		}

		if (defbase_sel) {
			MEM_freeN(defbase_sel);
		}
	}
	else {
		unsigned char col[4];
		if ((ob->actdef == 0) && !BLI_listbase_is_empty(&ob->defbase)) {
			/* color-code for missing data (full brightness isn't easy on the eye). */
			ARRAY_SET_ITEMS(col, 0xa0, 0, 0xa0, 0xff);
		}
		else if (draw_flag & (CALC_WP_GROUP_USER_ACTIVE | CALC_WP_GROUP_USER_ALL)) {
			copy_v3_v3_char((char *)col, dm_wcinfo->alert_color);
			col[3] = 255;
		}
		else {
			weightpaint_color(col, dm_wcinfo, 0.0f);
		}
		copy_vn_i((int *)r_wtcol_v, numVerts, *((int *)col));
	}
}

/** return an array of vertex weight colors from given weights, caller must free.
 *
 * \note that we could save some memory and allocate RGB only but then we'd need to
 * re-arrange the colors when copying to the face since MCol has odd ordering,
 * so leave this as is - campbell
 */
static void calc_colors_from_weights_array(
        const int num, const float *weights,
        unsigned char (*r_wtcol_v)[4])
{
	unsigned char (*wc)[4] = r_wtcol_v;
	int i;

	for (i = 0; i < num; i++, wc++, weights++) {
		weightpaint_color((unsigned char *)wc, NULL, *weights);
	}
}

void DM_update_weight_mcol(
        Object *ob, DerivedMesh *dm, int const draw_flag,
        float *weights, int num, const int *indices)
{
	BMEditMesh *em = (dm->type == DM_TYPE_EDITBMESH) ? BKE_editmesh_from_object(ob) : NULL;
	unsigned char (*wtcol_v)[4];
	int numVerts = dm->getNumVerts(dm);
	int i;

	if (em) {
		BKE_editmesh_color_ensure(em, BM_VERT);
		wtcol_v = em->derivedVertColor;
	}
	else {
		wtcol_v = MEM_mallocN(sizeof(*wtcol_v) * numVerts, __func__);
	}

	/* Weights are given by caller. */
	if (weights) {
		float *w = weights;
		/* If indices is not NULL, it means we do not have weights for all vertices,
		 * so we must create them (and set them to zero)... */
		if (indices) {
			w = MEM_callocN(sizeof(float) * numVerts, "Temp weight array DM_update_weight_mcol");
			i = num;
			while (i--)
				w[indices[i]] = weights[i];
		}

		/* Convert float weights to colors. */
		calc_colors_from_weights_array(numVerts, w, wtcol_v);

		if (indices)
			MEM_freeN(w);
	}
	else {
		/* No weights given, take them from active vgroup(s). */
		calc_weightpaint_vert_array(ob, dm, draw_flag, &G_dm_wcinfo, wtcol_v);
	}

	if (dm->type == DM_TYPE_EDITBMESH) {
		/* editmesh draw function checks specifically for this */
	}
	else {
		const int dm_totpoly = dm->getNumPolys(dm);
		const int dm_totloop = dm->getNumLoops(dm);
		unsigned char(*wtcol_l)[4] = CustomData_get_layer(dm->getLoopDataLayout(dm), CD_PREVIEW_MLOOPCOL);
		MLoop *mloop = dm->getLoopArray(dm), *ml;
		MPoly *mp = dm->getPolyArray(dm);
		int l_index;
		int j;

		/* now add to loops, so the data can be passed through the modifier stack
		 * If no CD_PREVIEW_MLOOPCOL existed yet, we have to add a new one! */
		if (!wtcol_l) {
			wtcol_l = MEM_mallocN(sizeof(*wtcol_l) * dm_totloop, __func__);
			CustomData_add_layer(&dm->loopData, CD_PREVIEW_MLOOPCOL, CD_ASSIGN, wtcol_l, dm_totloop);
		}

		l_index = 0;
		for (i = 0; i < dm_totpoly; i++, mp++) {
			ml = mloop + mp->loopstart;

			for (j = 0; j < mp->totloop; j++, ml++, l_index++) {
				copy_v4_v4_uchar(&wtcol_l[l_index][0],
				                 &wtcol_v[ml->v][0]);
			}
		}
		MEM_freeN(wtcol_v);

		dm->dirty |= DM_DIRTY_TESS_CDLAYERS;
	}
}

static void DM_update_statvis_color(const Scene *scene, Object *ob, DerivedMesh *dm)
{
	BMEditMesh *em = BKE_editmesh_from_object(ob);

	BKE_editmesh_statvis_calc(em, dm, &scene->toolsettings->statvis);
}

static void add_shapekey_layers(DerivedMesh *dm, Mesh *me, Object *UNUSED(ob))
{
	KeyBlock *kb;
	Key *key = me->key;
	int i;
	const size_t shape_alloc_len = sizeof(float) * 3 * me->totvert;

	if (!me->key)
		return;

	/* ensure we can use mesh vertex count for derived mesh custom data */
	if (me->totvert != dm->getNumVerts(dm)) {
		fprintf(stderr,
		        "%s: vertex size mismatch (mesh/dm) '%s' (%d != %d)\n",
		        __func__, me->id.name + 2, me->totvert, dm->getNumVerts(dm));
		return;
	}

	for (i = 0, kb = key->block.first; kb; kb = kb->next, i++) {
		int ci;
		float *array;

		if (me->totvert != kb->totelem) {
			fprintf(stderr,
			        "%s: vertex size mismatch (Mesh '%s':%d != KeyBlock '%s':%d)\n",
			        __func__, me->id.name + 2, me->totvert, kb->name, kb->totelem);
			array = MEM_callocN(shape_alloc_len, __func__);
		}
		else {
			array = MEM_mallocN(shape_alloc_len, __func__);
			memcpy(array, kb->data, shape_alloc_len);
		}

		CustomData_add_layer_named(&dm->vertData, CD_SHAPEKEY, CD_ASSIGN, array, dm->numVertData, kb->name);
		ci = CustomData_get_layer_index_n(&dm->vertData, CD_SHAPEKEY, i);

		dm->vertData.layers[ci].uid = kb->uid;
	}
}

/**
 * Called after calculating all modifiers.
 *
 * \note tessfaces should already be calculated.
 */
static void dm_ensure_display_normals(DerivedMesh *dm)
{
	/* Note: dm *may* have a poly CD_NORMAL layer (generated by a modifier needing poly normals e.g.).
	 *       We do not use it here, though. And it should be tagged as temp!
	 */
	/* BLI_assert((CustomData_has_layer(&dm->polyData, CD_NORMAL) == false)); */

	if ((dm->type == DM_TYPE_CDDM) &&
	    ((dm->dirty & DM_DIRTY_NORMALS) || CustomData_has_layer(&dm->polyData, CD_NORMAL) == false))
	{
		/* if normals are dirty we want to calculate vertex normals too */
		CDDM_calc_normals_mapping_ex(dm, (dm->dirty & DM_DIRTY_NORMALS) ? false : true);
	}
}

/* immutable settings and precomputed temporary data */
typedef struct ModifierEvalContext {
	int draw_flag;
	int required_mode;
	bool need_mapping;

	bool do_mod_mcol;
	bool do_final_wmcol;
	bool do_init_wmcol;
	bool do_mod_wmcol;
	
	bool do_loop_normals;
	float loop_normals_split_angle;
	
	ModifierApplyFlag app_flags;
	ModifierApplyFlag deform_app_flags;
	
	bool sculpt_mode;
	bool sculpt_dyntopo;
	bool sculpt_only_deform;
	bool has_multires;
	
	bool build_shapekey_layers;
	bool special_gameengine_hack;
	
	VirtualModifierData virtualModifierData;
	float (*inputVertexCos)[3]; /* XXX needed for freeing deformedVerts, not nice ... */
	
	ModifierData *md_begin;
	ModifierData *md_end;
	ModifierData *previewmd;
	CDMaskLink *datamasks;
} ModifierEvalContext;

static void mesh_init_modifier_context(ModifierEvalContext *ctx,
                                       Scene *scene, Object *ob,
                                       float (*inputVertexCos)[3],
                                       const bool useRenderParams, int useDeform,
                                       const bool need_mapping,
                                       CustomDataMask dataMask,
                                       const int index,
                                       const bool useCache,
                                       const bool build_shapekey_layers,
                                       const bool allow_gpu)
{
	Mesh *me = ob->data;
	MultiresModifierData *mmd = get_multires_modifier(scene, ob, 0);
	CustomDataMask previewmask = 0;
	const bool skipVirtualArmature = (useDeform < 0);

	ctx->inputVertexCos = inputVertexCos;

	ctx->app_flags =   (useRenderParams ? MOD_APPLY_RENDER : 0)
	                 | (useCache ? MOD_APPLY_USECACHE : 0)
	                 | (allow_gpu ? MOD_APPLY_ALLOW_GPU : 0);
	
	ctx->deform_app_flags =   ctx->app_flags
	                        | (useDeform ? MOD_APPLY_USECACHE : 0);
	
	ctx->draw_flag = dm_drawflag_calc(scene->toolsettings, me);
	ctx->required_mode = useRenderParams ? eModifierMode_Render : eModifierMode_Realtime;
	ctx->need_mapping = need_mapping;
	
	/* Generic preview only in object mode! */
	ctx->do_mod_mcol = (ob->mode == OB_MODE_OBJECT);
#if 0 /* XXX Will re-enable this when we have global mod stack options. */
	ctx->do_final_wmcol = (scene->toolsettings->weights_preview == WP_WPREVIEW_FINAL) && do_wmcol;
#else
	ctx->do_final_wmcol = false;
#endif
	ctx->do_init_wmcol = ((dataMask & CD_MASK_PREVIEW_MLOOPCOL) && (ob->mode & OB_MODE_WEIGHT_PAINT) && !ctx->do_final_wmcol);
	/* XXX Same as above... For now, only weights preview in WPaint mode. */
	ctx->do_mod_wmcol = ctx->do_init_wmcol;

	ctx->do_loop_normals = (me->flag & ME_AUTOSMOOTH) != 0;
	ctx->loop_normals_split_angle = me->smoothresh;

	ctx->sculpt_mode = (ob->mode & OB_MODE_SCULPT) && ob->sculpt && !useRenderParams;
	ctx->sculpt_dyntopo = (ctx->sculpt_mode && ob->sculpt->bm)  && !useRenderParams;
	ctx->sculpt_only_deform = (scene->toolsettings->sculpt->flags & SCULPT_ONLY_DEFORM);

	ctx->has_multires = (mmd && mmd->sculptlvl != 0);

	/*
	 * new value for useDeform -1  (hack for the gameengine):
	 *
	 * - apply only the modifier stack of the object, skipping the virtual modifiers,
	 * - don't apply the key
	 * - apply deform modifiers and input vertexco
	 */
	ctx->special_gameengine_hack = (useDeform < 0);
	ctx->build_shapekey_layers = build_shapekey_layers;

	/* precompute data */

	if (!skipVirtualArmature) {
		ctx->md_begin = modifiers_getVirtualModifierList(ob, &ctx->virtualModifierData);
	}
	else {
		/* game engine exception */
		ctx->md_begin = ob->modifiers.first;
		if (ctx->md_begin && ctx->md_begin->type == eModifierType_Armature)
			ctx->md_begin = ctx->md_begin->next;
	}

	/* only handle modifiers until index */
	ctx->md_end = (index >= 0) ? BLI_findlink(&ob->modifiers, index) : NULL;

	if (ctx->do_mod_wmcol || ctx->do_mod_mcol) {
		/* Find the last active modifier generating a preview, or NULL if none. */
		/* XXX Currently, DPaint modifier just ignores this.
		 *     Needs a stupid hack...
		 *     The whole "modifier preview" thing has to be (re?)designed, anyway! */
		ctx->previewmd = modifiers_getLastPreview(scene, ctx->md_begin, ctx->required_mode);

		/* even if the modifier doesn't need the data, to make a preview it may */
		if (ctx->previewmd) {
			if (ctx->do_mod_wmcol) {
				previewmask = CD_MASK_MDEFORMVERT;
			}
		}
	}
	else
		ctx->previewmd = NULL;

	ctx->datamasks = modifiers_calcDataMasks(scene, ob, ctx->md_begin, dataMask, ctx->required_mode, ctx->previewmd, previewmask);
}

static void mesh_free_modifier_context(ModifierEvalContext *ctx)
{
	BLI_linklist_free((LinkNode *)ctx->datamasks, NULL);
}

/* combined iterator for modifier and associated data mask */
typedef struct ModifierEvalIterator {
	ModifierData *modifier;
	CDMaskLink *datamask;

	/* mutable flags */
	bool multires_applied;
	bool isPrevDeform;
	CustomDataMask append_mask;
} ModifierEvalIterator;

static bool mesh_calc_modifier_sculptmode_skip(const ModifierEvalContext *ctx, ModifierData *md,
                                               const bool multires_applied)
{
	const bool multires_pending = ctx->has_multires && !multires_applied;
	
	if (ctx->sculpt_mode && (!multires_pending || ctx->sculpt_dyntopo))
	{
		const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
		const bool useRenderParams = ctx->app_flags & MOD_APPLY_RENDER;
		bool unsupported = false;

		if (md->type == eModifierType_Multires && ((MultiresModifierData *)md)->sculptlvl == 0) {
			/* If multires is on level 0 skip it silently without warning message. */
			if (!ctx->sculpt_dyntopo) {
				return true;
			}
		}

		if (ctx->sculpt_dyntopo && !useRenderParams)
			unsupported = true;

		if (ctx->sculpt_only_deform)
			unsupported |= (mti->type != eModifierTypeType_OnlyDeform);

		unsupported |= multires_applied;

		if (unsupported) {
			if (ctx->sculpt_dyntopo)
				modifier_setError(md, "Not supported in dyntopo");
			else
				modifier_setError(md, "Not supported in sculpt mode");
			return true;
		}
		else {
			modifier_setError(md, "Hide, Mask and optimized display disabled");
		}
	}
	
	return false;
}

typedef struct ModifierEvalResult {
	DerivedMesh *dm;
	DerivedMesh *orcodm;
	DerivedMesh *clothorcodm;
	float (*deformedVerts)[3];
	int numVerts;
} ModifierEvalResult;

static void mesh_calc_deform_modifier(Object *ob, const ModifierEvalContext *ctx, const ModifierEvalIterator *iter,
                                      ModifierEvalResult *result)
{
	Mesh *me = ob->data;
	ModifierData *md = iter->modifier;
	const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
	
	if (!modifier_isEnabled(md->scene, md, ctx->required_mode))
		return;
	if (ctx->special_gameengine_hack && mti->dependsOnTime && mti->dependsOnTime(md))
		return;
	if (mesh_calc_modifier_sculptmode_skip(ctx, md, iter->multires_applied))
		return;
	
	if (result->dm) {
		/* add an orco layer if needed by this modifier */
		CustomDataMask mask = mti->requiredDataMask ? mti->requiredDataMask(ob, md) : 0;
		
		if (mask & CD_MASK_ORCO)
			add_orco_dm(ob, NULL, result->dm, result->orcodm, CD_ORCO);
	}

	/* No existing verts to deform, need to build them. */
	if (!result->deformedVerts) {
		if (result->dm) {
			/* Deforming a derived mesh, read the vertex locations
			 * out of the mesh and deform them. Once done with this
			 * run of deformers verts will be written back.
			 */
			result->numVerts = result->dm->getNumVerts(result->dm);
			result->deformedVerts =
			    MEM_mallocN(sizeof(*result->deformedVerts) * result->numVerts, "dfmv");
			result->dm->getVertCos(result->dm, result->deformedVerts);
		}
		else {
			result->deformedVerts = BKE_mesh_vertexCos_get(me, &result->numVerts);
		}
	}

	/* if this is not the last modifier in the stack then recalculate the normals
	 * to avoid giving bogus normals to the next modifier see: [#23673] */
	if (iter->isPrevDeform && mti->dependsOnNormals && mti->dependsOnNormals(md)) {
		/* XXX, this covers bug #23673, but we may need normal calc for other types */
		if (result->dm && result->dm->type == DM_TYPE_CDDM) {
			CDDM_apply_vert_coords(result->dm, result->deformedVerts);
		}
	}

	modwrap_deformVerts(md, ob, result->dm, result->deformedVerts, result->numVerts, ctx->deform_app_flags);
}

static DerivedMesh *mesh_calc_create_input_dm(Object *ob, const ModifierEvalContext *ctx, ModifierData *md,
                                              CustomDataMask mask, CustomDataMask append_mask, CustomDataMask nextmask,
                                              DerivedMesh *dm, DerivedMesh *orcodm, DerivedMesh *clothorcodm,
                                              float (*deformedVerts)[3])
{
	Mesh *me = ob->data;
	const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
	
	if (dm) {
		/* add an orco layer if needed by this modifier */
		CustomDataMask mask = mti->requiredDataMask ? mti->requiredDataMask(ob, md) : 0;
		if (mask & CD_MASK_ORCO)
			add_orco_dm(ob, NULL, dm, orcodm, CD_ORCO);
		
		/* apply vertex coordinates or build a DerivedMesh as necessary */
		if (deformedVerts) {
			DerivedMesh *tdm = CDDM_copy(dm);
			dm->release(dm);
			dm = tdm;

			CDDM_apply_vert_coords(dm, deformedVerts);
		}
	}
	else {
		dm = CDDM_from_mesh(me);
		ASSERT_IS_VALID_DM(dm);

		if (ctx->build_shapekey_layers)
			add_shapekey_layers(dm, me, ob);

		if (deformedVerts) {
			CDDM_apply_vert_coords(dm, deformedVerts);
		}

		if (ctx->do_init_wmcol)
			DM_update_weight_mcol(ob, dm, ctx->draw_flag, NULL, 0, NULL);

		/* Constructive modifiers need to have an origindex
		 * otherwise they wont have anywhere to copy the data from.
		 *
		 * Also create ORIGINDEX data if any of the following modifiers
		 * requests it, this way Mirror, Solidify etc will keep ORIGINDEX
		 * data by using generic DM_copy_vert_data() functions.
		 */
		if (ctx->need_mapping || (nextmask & CD_MASK_ORIGINDEX)) {
			/* calc */
			DM_add_vert_layer(dm, CD_ORIGINDEX, CD_CALLOC, NULL);
			DM_add_edge_layer(dm, CD_ORIGINDEX, CD_CALLOC, NULL);
			DM_add_poly_layer(dm, CD_ORIGINDEX, CD_CALLOC, NULL);

#pragma omp parallel sections if (dm->numVertData + dm->numEdgeData + dm->numPolyData >= BKE_MESH_OMP_LIMIT)
			{
#pragma omp section
				{ range_vn_i(DM_get_vert_data_layer(dm, CD_ORIGINDEX), dm->numVertData, 0); }
#pragma omp section
				{ range_vn_i(DM_get_edge_data_layer(dm, CD_ORIGINDEX), dm->numEdgeData, 0); }
#pragma omp section
				{ range_vn_i(DM_get_poly_data_layer(dm, CD_ORIGINDEX), dm->numPolyData, 0); }
			}
		}
	}

	/* set the DerivedMesh to only copy needed data */
	/* needMapping check here fixes bug [#28112], otherwise it's
	 * possible that it won't be copied */
	DM_set_only_copy(dm, mask | append_mask | (ctx->need_mapping ? CD_MASK_ORIGINDEX : 0));
	
	/* add cloth rest shape key if needed */
	if ((mask | append_mask) & CD_MASK_CLOTH_ORCO)
		add_orco_dm(ob, NULL, dm, clothorcodm, CD_CLOTH_ORCO);

	/* add an origspace layer if needed */
	if (mask & CD_MASK_ORIGSPACE_MLOOP) {
		if (!CustomData_has_layer(&dm->loopData, CD_ORIGSPACE_MLOOP)) {
			DM_add_loop_layer(dm, CD_ORIGSPACE_MLOOP, CD_CALLOC, NULL);
			DM_init_origspace(dm);
		}
	}
	
	return dm;
}

static void mesh_calc_constructive_modifier(Object *ob, const ModifierEvalContext *ctx, CustomDataMask data_mask,
                                            ModifierEvalIterator *iter, ModifierEvalResult *result)
{
	Mesh *me = ob->data;
	ModifierData *md = iter->modifier;
	const ModifierTypeInfo *mti = modifierType_getInfo(md->type);

	CustomDataMask mask = iter->datamask->mask;
	CustomDataMask append_mask = iter->append_mask;
	CustomDataMask nextmask = (iter->datamask->next) ? iter->datamask->next->mask : data_mask;

	if (!modifier_isEnabled(md->scene, md, ctx->required_mode))
		return;
	if (ctx->special_gameengine_hack && mti->dependsOnTime && mti->dependsOnTime(md))
		return;

	result->dm = mesh_calc_create_input_dm(ob, ctx, md, mask, append_mask, nextmask,
	                                       result->dm, result->orcodm, result->clothorcodm, result->deformedVerts);

	{
		DerivedMesh *ndm;

		ndm = modwrap_applyModifier(md, ob, result->dm, ctx->app_flags);
		ASSERT_IS_VALID_DM(ndm);

		if (ndm) {
			/* if the modifier returned a new dm, release the old one */
			if ((result->dm) && result->dm != ndm) (result->dm)->release(result->dm);

			result->dm = ndm;

			if (result->deformedVerts) {
				if (result->deformedVerts != ctx->inputVertexCos)
					MEM_freeN(result->deformedVerts);

				result->deformedVerts = NULL;
			}
		}
	}

	/* create an orco derivedmesh in parallel */
	if (nextmask & CD_MASK_ORCO) {
		DerivedMesh *ndm;

		if (!result->orcodm)
			result->orcodm = create_orco_dm(ob, me, NULL, CD_ORCO);

		nextmask &= ~CD_MASK_ORCO;
		DM_set_only_copy(result->orcodm, nextmask | CD_MASK_ORIGINDEX |
		                 (mti->requiredDataMask ?
		                  mti->requiredDataMask(ob, md) : 0));

		ndm = modwrap_applyModifier(md, ob, result->orcodm, (ctx->app_flags & ~MOD_APPLY_USECACHE) | MOD_APPLY_ORCO);
		ASSERT_IS_VALID_DM(ndm);

		if (ndm) {
			/* if the modifier returned a new dm, release the old one */
			if (result->orcodm && result->orcodm != ndm) result->orcodm->release(result->orcodm);
			result->orcodm = ndm;
		}
	}

	/* create cloth orco derivedmesh in parallel */
	if (nextmask & CD_MASK_CLOTH_ORCO) {
		DerivedMesh *ndm;

		if (!result->clothorcodm)
			result->clothorcodm = create_orco_dm(ob, me, NULL, CD_CLOTH_ORCO);

		nextmask &= ~CD_MASK_CLOTH_ORCO;
		DM_set_only_copy(result->clothorcodm, nextmask | CD_MASK_ORIGINDEX);

		ndm = modwrap_applyModifier(md, ob, result->clothorcodm, (ctx->app_flags & ~MOD_APPLY_USECACHE) | MOD_APPLY_ORCO);
		ASSERT_IS_VALID_DM(ndm);

		if (ndm) {
			/* if the modifier returned a new dm, release the old one */
			if (result->clothorcodm && result->clothorcodm != ndm) {
				result->clothorcodm->release(result->clothorcodm);
			}
			result->clothorcodm = ndm;
		}
	}

	/* in case of dynamic paint, make sure preview mask remains for following modifiers */
	/* XXX Temp and hackish solution! */
	if (md->type == eModifierType_DynamicPaint)
		iter->append_mask |= CD_MASK_PREVIEW_MLOOPCOL;
	/* In case of active preview modifier, make sure preview mask remains for following modifiers. */
	else if ((md == ctx->previewmd) && (ctx->do_mod_wmcol)) {
		DM_update_weight_mcol(ob, result->dm, ctx->draw_flag, NULL, 0, NULL);
		iter->append_mask |= CD_MASK_PREVIEW_MLOOPCOL;
	}
}

static DerivedMesh *mesh_calc_finalize_dm(Object *ob, const ModifierEvalContext *ctx, CustomDataMask data_mask,
                                          DerivedMesh *dm, DerivedMesh *orcodm, DerivedMesh *deform, float (*deformedVerts)[3])
{
	Mesh *me = ob->data;
	DerivedMesh *finaldm;

	if (dm && deformedVerts) {
		finaldm = CDDM_copy(dm);

		dm->release(dm);

		CDDM_apply_vert_coords(finaldm, deformedVerts);

#if 0 /* For later nice mod preview! */
		/* In case we need modified weights in CD_PREVIEW_MCOL, we have to re-compute it. */
		if (ctx->do_final_wmcol)
			DM_update_weight_mcol(ob, finaldm, ctx->draw_flag, NULL, 0, NULL);
#endif
	}
	else if (dm) {
		finaldm = dm;

#if 0 /* For later nice mod preview! */
		/* In case we need modified weights in CD_PREVIEW_MCOL, we have to re-compute it. */
		if (ctx->do_final_wmcol)
			DM_update_weight_mcol(ob, finaldm, ctx->draw_flag, NULL, 0, NULL);
#endif
	}
	else {
		finaldm = CDDM_from_mesh(me);
		
		if (ctx->build_shapekey_layers) {
			add_shapekey_layers(finaldm, me, ob);
		}
		
		if (deformedVerts) {
			CDDM_apply_vert_coords(finaldm, deformedVerts);
		}

		/* In this case, we should never have weight-modifying modifiers in stack... */
		if (ctx->do_init_wmcol)
			DM_update_weight_mcol(ob, finaldm, ctx->draw_flag, NULL, 0, NULL);
	}

	/* add an orco layer if needed */
	if (data_mask & CD_MASK_ORCO) {
		add_orco_dm(ob, NULL, finaldm, orcodm, CD_ORCO);

		if (deform)
			add_orco_dm(ob, NULL, deform, NULL, CD_ORCO);
	}

	if (ctx->do_loop_normals) {
		/* Compute loop normals (note: will compute poly and vert normals as well, if needed!) */
		DM_calc_loop_normals(finaldm, ctx->do_loop_normals, ctx->loop_normals_split_angle);
	}

	if (!ctx->sculpt_dyntopo) {
		/* watch this! after 2.75a we move to from tessface to looptri (by default) */
		if (data_mask & CD_MASK_MFACE) {
			DM_ensure_tessface(finaldm);
		}
		DM_ensure_looptri(finaldm);

		/* without this, drawing ngon tri's faces will show ugly tessellated face
		 * normals and will also have to calculate normals on the fly, try avoid
		 * this where possible since calculating polygon normals isn't fast,
		 * note that this isn't a problem for subsurf (only quads) or editmode
		 * which deals with drawing differently.
		 *
		 * Only calc vertex normals if they are flagged as dirty.
		 * If using loop normals, poly nors have already been computed.
		 */
		if (!ctx->do_loop_normals) {
			dm_ensure_display_normals(finaldm);
		}
	}
	
	return finaldm;
}

static void mesh_calc_modifiers(
        Scene *scene, Object *ob, float (*inputVertexCos)[3],
        const bool useRenderParams, int useDeform,
        const bool need_mapping, CustomDataMask dataMask,
        const int index, const bool useCache, const bool build_shapekey_layers,
        const bool allow_gpu,
        /* return args */
        DerivedMesh **r_deform, DerivedMesh **r_final)
{
	Mesh *me = ob->data;
	ModifierEvalContext ctx;
	ModifierEvalIterator iter;
	ModifierEvalResult result = {0};
	DerivedMesh *finaldm;

	mesh_init_modifier_context(&ctx, scene, ob, inputVertexCos, useRenderParams, useDeform, need_mapping,
	                           dataMask, index, useCache, build_shapekey_layers, allow_gpu);

	iter.modifier = ctx.md_begin;
	iter.datamask = ctx.datamasks;
	iter.multires_applied = false;
	iter.isPrevDeform = false;
	/* XXX Always copying POLYINDEX, else tessellated data are no more valid! */
	iter.append_mask = CD_MASK_ORIGINDEX;

	modifiers_clearErrors(ob);

	if (r_deform)
		*r_deform = NULL;
	*r_final = NULL;

	result.deformedVerts = inputVertexCos;
	result.numVerts = me->totvert;

	if (useDeform) {
		if (!ctx.sculpt_dyntopo) {
			/* Apply all leading deforming modifiers */
			for (; iter.modifier != ctx.md_end; iter.modifier = iter.modifier->next, iter.datamask = iter.datamask->next) {
				ModifierData *md = iter.modifier;
				const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
				
				md->scene = scene;
				
				if (mti->type == eModifierTypeType_OnlyDeform) {
					mesh_calc_deform_modifier(ob, &ctx, &iter, &result);
				}
				else {
					break;
				}
			}
		}
	
		/* Result of all leading deforming modifiers is cached for
		 * places that wish to use the original mesh but with deformed
		 * coordinates (vpaint, etc.)
		 */
		if (r_deform) {
			*r_deform = CDDM_from_mesh(me);
	
			/* XXX build_shapekey_layers is never true,
			 * unreachable code path!
			 */
			if (ctx.build_shapekey_layers)
				add_shapekey_layers(*r_deform, me, ob);
			
			if (result.deformedVerts) {
				CDDM_apply_vert_coords(*r_deform, result.deformedVerts);
			}
		}
	}
	else {
		/* default behavior for meshes */
		if (!result.deformedVerts)
			result.deformedVerts = BKE_mesh_vertexCos_get(me, &result.numVerts);
	}


	/* Now apply all remaining modifiers. If useDeform is off then skip
	 * OnlyDeform ones. 
	 */
	result.dm = NULL;
	result.orcodm = NULL;
	result.clothorcodm = NULL;

	for (; iter.modifier != ctx.md_end; iter.modifier = iter.modifier->next, iter.datamask = iter.datamask->next) {
		ModifierData *md = iter.modifier;
		const ModifierTypeInfo *mti = modifierType_getInfo(md->type);

		md->scene = scene;

		if ((mti->flags & eModifierTypeFlag_RequiresOriginalData) && result.dm) {
			modifier_setError(md, "Modifier requires original data, bad stack position");
			continue;
		}

		if (need_mapping && !modifier_supportsMapping(md)) {
			continue;
		}

		/* How to apply modifier depends on (a) what we already have as
		 * a result of previous modifiers (could be a DerivedMesh or just
		 * deformed vertices) and (b) what type the modifier is.
		 */

		if (mti->type == eModifierTypeType_OnlyDeform) {
			if (useDeform)
				continue;
			
			mesh_calc_deform_modifier(ob, &ctx, &iter, &result);
		}
		else {
			mesh_calc_constructive_modifier(ob, &ctx, dataMask, &iter, &result);
		}

		iter.isPrevDeform = (mti->type == eModifierTypeType_OnlyDeform);

		if (ctx.sculpt_mode && md->type == eModifierType_Multires) {
			iter.multires_applied = true;
		}
	}

	for (iter.modifier = ctx.md_begin; iter.modifier; iter.modifier = iter.modifier->next)
		modifier_freeTemporaryData(iter.modifier);

	/* Yay, we are done. If we have a DerivedMesh and deformed vertices
	 * need to apply these back onto the DerivedMesh. If we have no
	 * DerivedMesh then we need to build one.
	 */
	finaldm = mesh_calc_finalize_dm(ob, &ctx, dataMask, result.dm, result.orcodm, r_deform? *r_deform: NULL, result.deformedVerts);

#ifdef WITH_GAMEENGINE
	/* NavMesh - this is a hack but saves having a NavMesh modifier */
	if ((ob->gameflag & OB_NAVMESH) && (finaldm->type == DM_TYPE_CDDM)) {
		DerivedMesh *tdm;
		tdm = navmesh_dm_createNavMeshForVisualization(finaldm);
		if (finaldm != tdm) {
			finaldm->release(finaldm);
			finaldm = tdm;
		}

		DM_ensure_tessface(finaldm);
	}
#endif /* WITH_GAMEENGINE */

	*r_final = finaldm;

	if (result.orcodm)
		result.orcodm->release(result.orcodm);
	if (result.clothorcodm)
		result.clothorcodm->release(result.clothorcodm);
	if (result.deformedVerts && result.deformedVerts != inputVertexCos)
		MEM_freeN(result.deformedVerts);
	
	mesh_free_modifier_context(&ctx);
}

float (*editbmesh_get_vertex_cos(BMEditMesh *em, int *r_numVerts))[3]
{
	BMIter iter;
	BMVert *eve;
	float (*cos)[3];
	int i;

	*r_numVerts = em->bm->totvert;

	cos = MEM_mallocN(sizeof(float) * 3 * em->bm->totvert, "vertexcos");

	BM_ITER_MESH_INDEX (eve, &iter, em->bm, BM_VERTS_OF_MESH, i) {
		copy_v3_v3(cos[i], eve->co);
	}

	return cos;
}

bool editbmesh_modifier_is_enabled(Scene *scene, ModifierData *md, DerivedMesh *dm)
{
	const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
	const int required_mode = eModifierMode_Realtime | eModifierMode_Editmode;

	if (!modifier_isEnabled(scene, md, required_mode)) {
		return false;
	}

	if ((mti->flags & eModifierTypeFlag_RequiresOriginalData) && dm) {
		modifier_setError(md, "Modifier requires original data, bad stack position");
		return false;
	}
	
	return true;
}

static void editbmesh_calc_modifiers(
        Scene *scene, Object *ob, BMEditMesh *em,
        CustomDataMask dataMask,
        /* return args */
        DerivedMesh **r_cage, DerivedMesh **r_final)
{
	ModifierData *md, *previewmd = NULL;
	float (*deformedVerts)[3] = NULL;
	CustomDataMask mask, previewmask = 0, append_mask = 0;
	DerivedMesh *dm = NULL, *orcodm = NULL;
	int i, numVerts = 0, cageIndex = modifiers_getCageIndex(scene, ob, NULL, 1);
	CDMaskLink *datamasks, *curr;
	const int required_mode = eModifierMode_Realtime | eModifierMode_Editmode;
	int draw_flag = dm_drawflag_calc(scene->toolsettings, ob->data);

	// const bool do_mod_mcol = true; // (ob->mode == OB_MODE_OBJECT);
#if 0 /* XXX Will re-enable this when we have global mod stack options. */
	const bool do_final_wmcol = (scene->toolsettings->weights_preview == WP_WPREVIEW_FINAL) && do_wmcol;
#endif
	const bool do_final_wmcol = false;
	const bool do_init_wmcol = ((((Mesh *)ob->data)->drawflag & ME_DRAWEIGHT) && !do_final_wmcol);

	const bool do_init_statvis = ((((Mesh *)ob->data)->drawflag & ME_DRAW_STATVIS) && !do_init_wmcol);
	const bool do_mod_wmcol = do_init_wmcol;
	VirtualModifierData virtualModifierData;

	const bool do_loop_normals = (((Mesh *)(ob->data))->flag & ME_AUTOSMOOTH) != 0;
	const float loop_normals_split_angle = ((Mesh *)(ob->data))->smoothresh;

	modifiers_clearErrors(ob);

	if (r_cage && cageIndex == -1) {
		*r_cage = getEditDerivedBMesh(em, ob, NULL);
	}

	md = modifiers_getVirtualModifierList(ob, &virtualModifierData);

	/* copied from mesh_calc_modifiers */
	if (do_mod_wmcol) {
		previewmd = modifiers_getLastPreview(scene, md, required_mode);
		/* even if the modifier doesn't need the data, to make a preview it may */
		if (previewmd) {
			previewmask = CD_MASK_MDEFORMVERT;
		}
	}

	datamasks = modifiers_calcDataMasks(scene, ob, md, dataMask, required_mode, previewmd, previewmask);

	curr = datamasks;
	for (i = 0; md; i++, md = md->next, curr = curr->next) {
		const ModifierTypeInfo *mti = modifierType_getInfo(md->type);

		md->scene = scene;
		
		if (!editbmesh_modifier_is_enabled(scene, md, dm)) {
			continue;
		}

		/* add an orco layer if needed by this modifier */
		if (dm && mti->requiredDataMask) {
			mask = mti->requiredDataMask(ob, md);
			if (mask & CD_MASK_ORCO)
				add_orco_dm(ob, em, dm, orcodm, CD_ORCO);
		}

		/* How to apply modifier depends on (a) what we already have as
		 * a result of previous modifiers (could be a DerivedMesh or just
		 * deformed vertices) and (b) what type the modifier is.
		 */

		if (mti->type == eModifierTypeType_OnlyDeform) {
			/* No existing verts to deform, need to build them. */
			if (!deformedVerts) {
				if (dm) {
					/* Deforming a derived mesh, read the vertex locations
					 * out of the mesh and deform them. Once done with this
					 * run of deformers verts will be written back.
					 */
					numVerts = dm->getNumVerts(dm);
					deformedVerts =
					    MEM_mallocN(sizeof(*deformedVerts) * numVerts, "dfmv");
					dm->getVertCos(dm, deformedVerts);
				}
				else {
					deformedVerts = editbmesh_get_vertex_cos(em, &numVerts);
				}
			}

			if (mti->deformVertsEM)
				modwrap_deformVertsEM(md, ob, em, dm, deformedVerts, numVerts);
			else
				modwrap_deformVerts(md, ob, dm, deformedVerts, numVerts, 0);
		}
		else {
			DerivedMesh *ndm;

			/* apply vertex coordinates or build a DerivedMesh as necessary */
			if (dm) {
				if (deformedVerts) {
					DerivedMesh *tdm = CDDM_copy(dm);
					if (!(r_cage && dm == *r_cage)) {
						dm->release(dm);
					}
					dm = tdm;

					CDDM_apply_vert_coords(dm, deformedVerts);
				}
				else if (r_cage && dm == *r_cage) {
					/* dm may be changed by this modifier, so we need to copy it */
					dm = CDDM_copy(dm);
				}

			}
			else {
				dm = CDDM_from_editbmesh(em, false, false);
				ASSERT_IS_VALID_DM(dm);

				if (deformedVerts) {
					CDDM_apply_vert_coords(dm, deformedVerts);
				}

				if (do_init_wmcol) {
					DM_update_weight_mcol(ob, dm, draw_flag, NULL, 0, NULL);
				}
			}

			/* create an orco derivedmesh in parallel */
			mask = curr->mask;
			if (mask & CD_MASK_ORCO) {
				if (!orcodm)
					orcodm = create_orco_dm(ob, ob->data, em, CD_ORCO);

				mask &= ~CD_MASK_ORCO;
				DM_set_only_copy(orcodm, mask | CD_MASK_ORIGINDEX);

				if (mti->applyModifierEM) {
					ndm = modwrap_applyModifierEM(md, ob, em, orcodm, MOD_APPLY_ORCO);
				}
				else {
					ndm = modwrap_applyModifier(md, ob, orcodm, MOD_APPLY_ORCO);
				}
				ASSERT_IS_VALID_DM(ndm);

				if (ndm) {
					/* if the modifier returned a new dm, release the old one */
					if (orcodm && orcodm != ndm) orcodm->release(orcodm);
					orcodm = ndm;
				}
			}

			/* set the DerivedMesh to only copy needed data */
			mask |= append_mask;
			mask = curr->mask; /* CD_MASK_ORCO may have been cleared above */

			DM_set_only_copy(dm, mask | CD_MASK_ORIGINDEX);

			if (mask & CD_MASK_ORIGSPACE_MLOOP) {
				if (!CustomData_has_layer(&dm->loopData, CD_ORIGSPACE_MLOOP)) {
					DM_add_loop_layer(dm, CD_ORIGSPACE_MLOOP, CD_CALLOC, NULL);
					DM_init_origspace(dm);
				}
			}

			if (mti->applyModifierEM)
				ndm = modwrap_applyModifierEM(md, ob, em, dm, MOD_APPLY_USECACHE | MOD_APPLY_ALLOW_GPU);
			else
				ndm = modwrap_applyModifier(md, ob, dm, MOD_APPLY_USECACHE | MOD_APPLY_ALLOW_GPU);
			ASSERT_IS_VALID_DM(ndm);

			if (ndm) {
				if (dm && dm != ndm)
					dm->release(dm);

				dm = ndm;

				if (deformedVerts) {
					MEM_freeN(deformedVerts);
					deformedVerts = NULL;
				}
			}
		}

		/* In case of active preview modifier, make sure preview mask remains for following modifiers. */
		if ((md == previewmd) && (do_mod_wmcol)) {
			DM_update_weight_mcol(ob, dm, draw_flag, NULL, 0, NULL);
			append_mask |= CD_MASK_PREVIEW_MLOOPCOL;
		}

		if (r_cage && i == cageIndex) {
			if (dm && deformedVerts) {
				*r_cage = CDDM_copy(dm);
				CDDM_apply_vert_coords(*r_cage, deformedVerts);
			}
			else if (dm) {
				*r_cage = dm;
			}
			else {
				*r_cage = getEditDerivedBMesh(
				        em, ob,
				        deformedVerts ? MEM_dupallocN(deformedVerts) : NULL);
			}
		}
	}

	BLI_linklist_free((LinkNode *)datamasks, NULL);

	/* Yay, we are done. If we have a DerivedMesh and deformed vertices need
	 * to apply these back onto the DerivedMesh. If we have no DerivedMesh
	 * then we need to build one.
	 */
	if (dm && deformedVerts) {
		*r_final = CDDM_copy(dm);

		if (!(r_cage && dm == *r_cage)) {
			dm->release(dm);
		}

		CDDM_apply_vert_coords(*r_final, deformedVerts);
	}
	else if (dm) {
		*r_final = dm;
	}
	else if (!deformedVerts && r_cage && *r_cage) {
		/* cage should already have up to date normals */
		*r_final = *r_cage;

		/* In this case, we should never have weight-modifying modifiers in stack... */
		if (do_init_wmcol)
			DM_update_weight_mcol(ob, *r_final, draw_flag, NULL, 0, NULL);
		if (do_init_statvis)
			DM_update_statvis_color(scene, ob, *r_final);
	}
	else {
		/* this is just a copy of the editmesh, no need to calc normals */
		*r_final = getEditDerivedBMesh(em, ob, deformedVerts);
		deformedVerts = NULL;

		/* In this case, we should never have weight-modifying modifiers in stack... */
		if (do_init_wmcol)
			DM_update_weight_mcol(ob, *r_final, draw_flag, NULL, 0, NULL);
		if (do_init_statvis)
			DM_update_statvis_color(scene, ob, *r_final);
	}

	if (do_loop_normals) {
		/* Compute loop normals */
		DM_calc_loop_normals(*r_final, do_loop_normals, loop_normals_split_angle);
		if (r_cage && *r_cage && (*r_cage != *r_final)) {
			DM_calc_loop_normals(*r_cage, do_loop_normals, loop_normals_split_angle);
		}
	}

	/* BMESH_ONLY, ensure tessface's used for drawing,
	 * but don't recalculate if the last modifier in the stack gives us tessfaces
	 * check if the derived meshes are DM_TYPE_EDITBMESH before calling, this isn't essential
	 * but quiets annoying error messages since tessfaces wont be created. */
	if (dataMask & CD_MASK_MFACE) {
		if ((*r_final)->type != DM_TYPE_EDITBMESH) {
			DM_ensure_tessface(*r_final);
		}
		if (r_cage && *r_cage) {
			if ((*r_cage)->type != DM_TYPE_EDITBMESH) {
				if (*r_cage != *r_final) {
					DM_ensure_tessface(*r_cage);
				}
			}
		}
	}
	/* --- */

	/* same as mesh_calc_modifiers (if using loop normals, poly nors have already been computed). */
	if (!do_loop_normals) {
		dm_ensure_display_normals(*r_final);
	}

	/* add an orco layer if needed */
	if (dataMask & CD_MASK_ORCO)
		add_orco_dm(ob, em, *r_final, orcodm, CD_ORCO);

	if (orcodm)
		orcodm->release(orcodm);

	if (deformedVerts)
		MEM_freeN(deformedVerts);
}

#ifdef WITH_OPENSUBDIV
/* The idea is to skip CPU-side ORCO calculation when
 * we'll be using GPU backend of OpenSubdiv. This is so
 * playback performance is kept as high as possible.
 */
static bool calc_modifiers_skip_orco(const Object *ob)
{
	const ModifierData *last_md = ob->modifiers.last;
	if (last_md != NULL &&
	    last_md->type == eModifierType_Subsurf)
	{
		SubsurfModifierData *smd = (SubsurfModifierData *)last_md;
		/* TODO(sergey): Deduplicate this with checks from subsurf_ccg.c. */
		return smd->use_opensubdiv && U.opensubdiv_compute_type != USER_OPENSUBDIV_COMPUTE_NONE;
	}
	return false;
}
#endif

static void mesh_build_data(
        Scene *scene, Object *ob, CustomDataMask dataMask,
        const bool build_shapekey_layers, const bool need_mapping)
{
	BLI_assert(ob->type == OB_MESH);

	BKE_object_free_derived_caches(ob);
	BKE_object_sculpt_modifiers_changed(ob);

#ifdef WITH_OPENSUBDIV
	if (calc_modifiers_skip_orco(ob)) {
		dataMask &= ~(CD_MASK_ORCO | CD_MASK_PREVIEW_MCOL);
	}
#endif

	mesh_calc_modifiers(
	        scene, ob, NULL, false, 1, need_mapping, dataMask, -1, true, build_shapekey_layers,
	        true,
	        &ob->derivedDeform, &ob->derivedFinal);

	DM_set_object_boundbox(ob, ob->derivedFinal);

	ob->derivedFinal->needsFree = 0;
	ob->derivedDeform->needsFree = 0;
	ob->lastDataMask = dataMask;
	ob->lastNeedMapping = need_mapping;

	if ((ob->mode & OB_MODE_SCULPT) && ob->sculpt) {
		/* create PBVH immediately (would be created on the fly too,
		 * but this avoids waiting on first stroke) */

		BKE_sculpt_update_mesh_elements(scene, scene->toolsettings->sculpt, ob, false, false);
	}

	BLI_assert(!(ob->derivedFinal->dirty & DM_DIRTY_NORMALS));
}

static void editbmesh_build_data(Scene *scene, Object *obedit, BMEditMesh *em, CustomDataMask dataMask)
{
	BKE_object_free_derived_caches(obedit);
	BKE_object_sculpt_modifiers_changed(obedit);

	BKE_editmesh_free_derivedmesh(em);

#ifdef WITH_OPENSUBDIV
	if (calc_modifiers_skip_orco(obedit)) {
		dataMask &= ~(CD_MASK_ORCO | CD_MASK_PREVIEW_MCOL);
	}
#endif

	editbmesh_calc_modifiers(
	        scene, obedit, em, dataMask,
	        &em->derivedCage, &em->derivedFinal);

	DM_set_object_boundbox(obedit, em->derivedFinal);

	em->lastDataMask = dataMask;
	em->derivedFinal->needsFree = 0;
	em->derivedCage->needsFree = 0;

	BLI_assert(!(em->derivedFinal->dirty & DM_DIRTY_NORMALS));
}

static CustomDataMask object_get_datamask(const Scene *scene, Object *ob, bool *r_need_mapping)
{
	Object *actob = scene->basact ? scene->basact->object : NULL;
	CustomDataMask mask = ob->customdata_mask;

	if (r_need_mapping) {
		*r_need_mapping = false;
	}

	if (ob == actob) {
		bool editing = BKE_paint_select_face_test(ob);

		/* weight paint and face select need original indices because of selection buffer drawing */
		if (r_need_mapping) {
			*r_need_mapping = (editing || (ob->mode & (OB_MODE_WEIGHT_PAINT | OB_MODE_VERTEX_PAINT)));
		}

		/* check if we need tfaces & mcols due to face select or texture paint */
		if ((ob->mode & OB_MODE_TEXTURE_PAINT) || editing) {
			mask |= CD_MASK_MLOOPUV | CD_MASK_MLOOPCOL;
		}

		/* check if we need mcols due to vertex paint or weightpaint */
		if (ob->mode & OB_MODE_VERTEX_PAINT) {
			mask |= CD_MASK_MLOOPCOL;
		}

		if (ob->mode & OB_MODE_WEIGHT_PAINT) {
			mask |= CD_MASK_PREVIEW_MLOOPCOL;
		}

		if (ob->mode & OB_MODE_EDIT)
			mask |= CD_MASK_MVERT_SKIN;
	}

	return mask;
}

void makeDerivedMesh(
        Scene *scene, Object *ob, BMEditMesh *em,
        CustomDataMask dataMask, const bool build_shapekey_layers)
{
	bool need_mapping;
	dataMask |= object_get_datamask(scene, ob, &need_mapping);

	if (em) {
		editbmesh_build_data(scene, ob, em, dataMask);
	}
	else {
		mesh_build_data(scene, ob, dataMask, build_shapekey_layers, need_mapping);
	}
}

void BKE_object_eval_mesh(EvaluationContext *eval_ctx,
                          Scene *scene,
                          Object *ob)
{
	BMEditMesh *em = (ob == scene->obedit) ? BKE_editmesh_from_object(ob) : NULL;
	
	if (!em) {
		bool need_mapping;
		CustomDataMask data_mask = scene->customdata_mask | CD_MASK_BAREMESH;
		data_mask |= object_get_datamask(scene, ob, &need_mapping);
#ifdef WITH_FREESTYLE
		/* make sure Freestyle edge/face marks appear in DM for render (see T40315) */
		if (eval_ctx->mode != DAG_EVAL_VIEWPORT) {
			data_mask |= CD_MASK_FREESTYLE_EDGE | CD_MASK_FREESTYLE_FACE;
		}
#else
		UNUSED_VARS(eval_ctx);
#endif
		
		mesh_build_data(scene, ob, data_mask, false, need_mapping);
	}
}

void BKE_object_eval_editmesh(EvaluationContext *eval_ctx,
                              Scene *scene,
                              Object *ob)
{
	BMEditMesh *em = (ob == scene->obedit) ? BKE_editmesh_from_object(ob) : NULL;

	if (em) {
		bool need_mapping;
		CustomDataMask data_mask = scene->customdata_mask | CD_MASK_BAREMESH;
		data_mask |= object_get_datamask(scene, ob, &need_mapping);
#ifdef WITH_FREESTYLE
		/* make sure Freestyle edge/face marks appear in DM for render (see T40315) */
		if (eval_ctx->mode != DAG_EVAL_VIEWPORT) {
			data_mask |= CD_MASK_FREESTYLE_EDGE | CD_MASK_FREESTYLE_FACE;
		}
#else
		UNUSED_VARS(eval_ctx);
#endif
		
		editbmesh_build_data(scene, ob, em, data_mask);
	}
}

/***/

DerivedMesh *mesh_get_derived_final(Scene *scene, Object *ob, CustomDataMask dataMask)
{
	/* if there's no derived mesh or the last data mask used doesn't include
	 * the data we need, rebuild the derived mesh
	 */
	bool need_mapping;
	dataMask |= object_get_datamask(scene, ob, &need_mapping);

	if (!ob->derivedFinal ||
	    ((dataMask & ob->lastDataMask) != dataMask) ||
	    (need_mapping != ob->lastNeedMapping))
	{
		mesh_build_data(scene, ob, dataMask, false, need_mapping);
	}

	if (ob->derivedFinal) { BLI_assert(!(ob->derivedFinal->dirty & DM_DIRTY_NORMALS)); }
	return ob->derivedFinal;
}

DerivedMesh *mesh_get_derived_deform(Scene *scene, Object *ob, CustomDataMask dataMask)
{
	/* if there's no derived mesh or the last data mask used doesn't include
	 * the data we need, rebuild the derived mesh
	 */
	bool need_mapping;

	dataMask |= object_get_datamask(scene, ob, &need_mapping);

	if (!ob->derivedDeform ||
	    ((dataMask & ob->lastDataMask) != dataMask) ||
	    (need_mapping != ob->lastNeedMapping))
	{
		mesh_build_data(scene, ob, dataMask, false, need_mapping);
	}

	return ob->derivedDeform;
}

DerivedMesh *mesh_create_derived_render(Scene *scene, Object *ob, CustomDataMask dataMask)
{
	DerivedMesh *final;
	
	mesh_calc_modifiers(
	        scene, ob, NULL, true, 1, false, dataMask, -1, false, false, false,
	        NULL, &final);

	return final;
}

DerivedMesh *mesh_create_derived_index_render(Scene *scene, Object *ob, CustomDataMask dataMask, int index)
{
	DerivedMesh *final;

	mesh_calc_modifiers(
	        scene, ob, NULL, true, 1, false, dataMask, index, false, false, false,
	        NULL, &final);

	return final;
}

DerivedMesh *mesh_create_derived_view(
        Scene *scene, Object *ob,
        CustomDataMask dataMask)
{
	DerivedMesh *final;

	/* XXX hack
	 * psys modifier updates particle state when called during dupli-list generation,
	 * which can lead to wrong transforms. This disables particle system modifier execution.
	 */
	ob->transflag |= OB_NO_PSYS_UPDATE;

	mesh_calc_modifiers(
	        scene, ob, NULL, false, 1, false, dataMask, -1, false, false, false,
	        NULL, &final);

	ob->transflag &= ~OB_NO_PSYS_UPDATE;

	return final;
}

DerivedMesh *mesh_create_derived_no_deform(
        Scene *scene, Object *ob, float (*vertCos)[3],
        CustomDataMask dataMask)
{
	DerivedMesh *final;
	
	mesh_calc_modifiers(
	        scene, ob, vertCos, false, 0, false, dataMask, -1, false, false, false,
	        NULL, &final);

	return final;
}

DerivedMesh *mesh_create_derived_no_virtual(
        Scene *scene, Object *ob, float (*vertCos)[3],
        CustomDataMask dataMask)
{
	DerivedMesh *final;
	
	mesh_calc_modifiers(
	        scene, ob, vertCos, false, -1, false, dataMask, -1, false, false, false,
	        NULL, &final);

	return final;
}

DerivedMesh *mesh_create_derived_physics(
        Scene *scene, Object *ob, float (*vertCos)[3],
        CustomDataMask dataMask)
{
	DerivedMesh *final;
	
	mesh_calc_modifiers(
	        scene, ob, vertCos, false, -1, true, dataMask, -1, false, false, false,
	        NULL, &final);

	return final;
}

DerivedMesh *mesh_create_derived_no_deform_render(
        Scene *scene, Object *ob,
        float (*vertCos)[3],
        CustomDataMask dataMask)
{
	DerivedMesh *final;

	mesh_calc_modifiers(
	        scene, ob, vertCos, true, 0, false, dataMask, -1, false, false, false,
	        NULL, &final);

	return final;
}

/***/

DerivedMesh *editbmesh_get_derived_cage_and_final(
        Scene *scene, Object *obedit, BMEditMesh *em,
        CustomDataMask dataMask,
        /* return args */
        DerivedMesh **r_final)
{
	/* if there's no derived mesh or the last data mask used doesn't include
	 * the data we need, rebuild the derived mesh
	 */
	dataMask |= object_get_datamask(scene, obedit, NULL);

	if (!em->derivedCage ||
	    (em->lastDataMask & dataMask) != dataMask)
	{
		editbmesh_build_data(scene, obedit, em, dataMask);
	}

	*r_final = em->derivedFinal;
	if (em->derivedFinal) { BLI_assert(!(em->derivedFinal->dirty & DM_DIRTY_NORMALS)); }
	return em->derivedCage;
}

DerivedMesh *editbmesh_get_derived_cage(Scene *scene, Object *obedit, BMEditMesh *em, CustomDataMask dataMask)
{
	/* if there's no derived mesh or the last data mask used doesn't include
	 * the data we need, rebuild the derived mesh
	 */
	dataMask |= object_get_datamask(scene, obedit, NULL);

	if (!em->derivedCage ||
	    (em->lastDataMask & dataMask) != dataMask)
	{
		editbmesh_build_data(scene, obedit, em, dataMask);
	}

	return em->derivedCage;
}

DerivedMesh *editbmesh_get_derived_base(Object *obedit, BMEditMesh *em)
{
	return getEditDerivedBMesh(em, obedit, NULL);
}

/***/

/* get derived mesh from an object, using editbmesh if available. */
DerivedMesh *object_get_derived_final(Object *ob, const bool for_render)
{
	Mesh *me = ob->data;
	BMEditMesh *em = me->edit_btmesh;

	if (for_render) {
		/* TODO(sergey): use proper derived render here in the future. */
		return ob->derivedFinal;
	}

	/* only return the editmesh if its from this object because
	 * we don't a mesh from another object's modifier stack: T43122 */
	if (em && (em->ob == ob)) {
		DerivedMesh *dm = em->derivedFinal;
		return dm;
	}

	return ob->derivedFinal;
}


/* UNUSED */
#if 0

/* ********* For those who don't grasp derived stuff! (ton) :) *************** */

static void make_vertexcosnos__mapFunc(void *userData, int index, const float co[3],
                                       const float no_f[3], const short no_s[3])
{
	DMCoNo *co_no = &((DMCoNo *)userData)[index];

	/* check if we've been here before (normal should not be 0) */
	if (!is_zero_v3(co_no->no)) {
		return;
	}

	copy_v3_v3(co_no->co, co);
	if (no_f) {
		copy_v3_v3(co_no->no, no_f);
	}
	else {
		normal_short_to_float_v3(co_no->no, no_s);
	}
}

/* always returns original amount me->totvert of vertices and normals, but fully deformed and subsurfered */
/* this is needed for all code using vertexgroups (no subsurf support) */
/* it stores the normals as floats, but they can still be scaled as shorts (32767 = unit) */
/* in use now by vertex/weight paint and particle generating */

DMCoNo *mesh_get_mapped_verts_nors(Scene *scene, Object *ob)
{
	Mesh *me = ob->data;
	DerivedMesh *dm;
	DMCoNo *vertexcosnos;
	
	/* lets prevent crashing... */
	if (ob->type != OB_MESH || me->totvert == 0)
		return NULL;
	
	dm = mesh_get_derived_final(scene, ob, CD_MASK_BAREMESH | CD_MASK_ORIGINDEX);
	
	if (dm->foreachMappedVert) {
		vertexcosnos = MEM_callocN(sizeof(DMCoNo) * me->totvert, "vertexcosnos map");
		dm->foreachMappedVert(dm, make_vertexcosnos__mapFunc, vertexcosnos);
	}
	else {
		DMCoNo *v_co_no = vertexcosnos = MEM_mallocN(sizeof(DMCoNo) * me->totvert, "vertexcosnos map");
		int a;
		for (a = 0; a < me->totvert; a++, v_co_no++) {
			dm->getVertCo(dm, a, v_co_no->co);
			dm->getVertNo(dm, a, v_co_no->no);
		}
	}
	
	dm->release(dm);
	return vertexcosnos;
}

#endif

/* --- NAVMESH (begin) --- */
#ifdef WITH_GAMEENGINE

/* BMESH_TODO, navmesh is not working right currently
 * All tools set this as MPoly data, but derived mesh currently draws from MFace (tessface)
 *
 * Proposed solution, rather then copy CD_RECAST into the MFace array,
 * use ORIGINDEX to get the original poly index and then get the CD_RECAST
 * data from the original me->mpoly layer. - campbell
 */


BLI_INLINE int navmesh_bit(int a, int b)
{
	return (a & (1 << b)) >> b;
}

BLI_INLINE void navmesh_intToCol(int i, float col[3])
{
	int r = navmesh_bit(i, 0) + navmesh_bit(i, 3) * 2 + 1;
	int g = navmesh_bit(i, 1) + navmesh_bit(i, 4) * 2 + 1;
	int b = navmesh_bit(i, 2) + navmesh_bit(i, 5) * 2 + 1;
	col[0] = 1 - r * 63.0f / 255.0f;
	col[1] = 1 - g * 63.0f / 255.0f;
	col[2] = 1 - b * 63.0f / 255.0f;
}

static void navmesh_drawColored(DerivedMesh *dm)
{
	int a, glmode;
	MVert *mvert = (MVert *)CustomData_get_layer(&dm->vertData, CD_MVERT);
	MFace *mface = (MFace *)CustomData_get_layer(&dm->faceData, CD_MFACE);
	int *polygonIdx = (int *)CustomData_get_layer(&dm->polyData, CD_RECAST);
	float col[3];

	if (!polygonIdx)
		return;

#if 0
	//UI_ThemeColor(TH_WIRE);
	glLineWidth(2.0);
	dm->drawEdges(dm, 0, 1);
#endif

	/* if (GPU_buffer_legacy(dm) ) */ /* TODO - VBO draw code, not high priority - campbell */
	{
		DEBUG_VBO("Using legacy code. drawNavMeshColored\n");
		//glShadeModel(GL_SMOOTH);
		glBegin(glmode = GL_QUADS);
		for (a = 0; a < dm->numTessFaceData; a++, mface++) {
			int new_glmode = mface->v4 ? GL_QUADS : GL_TRIANGLES;
			int pi = polygonIdx[a];
			if (pi <= 0) {
				zero_v3(col);
			}
			else {
				navmesh_intToCol(pi, col);
			}

			if (new_glmode != glmode) {
				glEnd();
				glBegin(glmode = new_glmode);
			}
			glColor3fv(col);
			glVertex3fv(mvert[mface->v1].co);
			glVertex3fv(mvert[mface->v2].co);
			glVertex3fv(mvert[mface->v3].co);
			if (mface->v4) {
				glVertex3fv(mvert[mface->v4].co);
			}
		}
		glEnd();
	}
}

static void navmesh_DM_drawFacesTex(
        DerivedMesh *dm,
        DMSetDrawOptionsTex UNUSED(setDrawOptions),
        DMCompareDrawOptions UNUSED(compareDrawOptions),
        void *UNUSED(userData), DMDrawFlag UNUSED(flag))
{
	navmesh_drawColored(dm);
}

static void navmesh_DM_drawFacesSolid(
        DerivedMesh *dm,
        float (*partial_redraw_planes)[4],
        bool UNUSED(fast), DMSetMaterial UNUSED(setMaterial))
{
	UNUSED_VARS(partial_redraw_planes);

	//drawFacesSolid_original(dm, partial_redraw_planes, fast, setMaterial);
	navmesh_drawColored(dm);
}

static DerivedMesh *navmesh_dm_createNavMeshForVisualization(DerivedMesh *dm)
{
	DerivedMesh *result;
	int maxFaces = dm->getNumPolys(dm);
	int *recastData;
	int vertsPerPoly = 0, nverts = 0, ndtris = 0, npolys = 0;
	float *verts = NULL;
	unsigned short *dtris = NULL, *dmeshes = NULL, *polys = NULL;
	int *dtrisToPolysMap = NULL, *dtrisToTrisMap = NULL, *trisToFacesMap = NULL;
	int res;

	result = CDDM_copy(dm);
	if (!CustomData_has_layer(&result->polyData, CD_RECAST)) {
		int *sourceRecastData = (int *)CustomData_get_layer(&dm->polyData, CD_RECAST);
		if (sourceRecastData) {
			CustomData_add_layer_named(&result->polyData, CD_RECAST, CD_DUPLICATE,
			                           sourceRecastData, maxFaces, "recastData");
		}
	}
	recastData = (int *)CustomData_get_layer(&result->polyData, CD_RECAST);

	/* note: This is not good design! - really should not be doing this */
	result->drawFacesTex =  navmesh_DM_drawFacesTex;
	result->drawFacesSolid = navmesh_DM_drawFacesSolid;


	/* process mesh */
	res  = buildNavMeshDataByDerivedMesh(dm, &vertsPerPoly, &nverts, &verts, &ndtris, &dtris,
	                                     &npolys, &dmeshes, &polys, &dtrisToPolysMap, &dtrisToTrisMap,
	                                     &trisToFacesMap);
	if (res) {
		size_t polyIdx;

		/* invalidate concave polygon */
		for (polyIdx = 0; polyIdx < (size_t)npolys; polyIdx++) {
			unsigned short *poly = &polys[polyIdx * 2 * vertsPerPoly];
			if (!polyIsConvex(poly, vertsPerPoly, verts)) {
				/* set negative polygon idx to all faces */
				unsigned short *dmesh = &dmeshes[4 * polyIdx];
				unsigned short tbase = dmesh[2];
				unsigned short tnum = dmesh[3];
				unsigned short ti;

				for (ti = 0; ti < tnum; ti++) {
					unsigned short triidx = dtrisToTrisMap[tbase + ti];
					unsigned short faceidx = trisToFacesMap[triidx];
					if (recastData[faceidx] > 0) {
						recastData[faceidx] = -recastData[faceidx];
					}
				}
			}
		}
	}
	else {
		printf("Navmesh: Unable to generate valid Navmesh");
	}

	/* clean up */
	if (verts != NULL)
		MEM_freeN(verts);
	if (dtris != NULL)
		MEM_freeN(dtris);
	if (dmeshes != NULL)
		MEM_freeN(dmeshes);
	if (polys != NULL)
		MEM_freeN(polys);
	if (dtrisToPolysMap != NULL)
		MEM_freeN(dtrisToPolysMap);
	if (dtrisToTrisMap != NULL)
		MEM_freeN(dtrisToTrisMap);
	if (trisToFacesMap != NULL)
		MEM_freeN(trisToFacesMap);

	return result;
}

#endif /* WITH_GAMEENGINE */

/* --- NAVMESH (end) --- */
