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
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Lukas Toenne
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/gpencil.c
 *  \ingroup bke
 */

 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"
#include "BLI_string_utils.h"

#include "BLT_translation.h"

#include "DNA_groom_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_animsys.h"
#include "BKE_customdata.h"
#include "BKE_cdderivedmesh.h"
#include "BKE_global.h"
#include "BKE_groom.h"
#include "BKE_hair.h"
#include "BKE_bvhutils.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_mesh_sample.h"
#include "BKE_object.h"
#include "BKE_object_facemap.h"

#include "DEG_depsgraph.h"

#include "bmesh.h"


void BKE_groom_init(Groom *groom)
{
	BLI_assert(MEMCMP_STRUCT_OFS_IS_ZERO(groom, id));
	
	groom->bb = BKE_boundbox_alloc_unit();
	
	groom->curve_res = 12;
	
	groom->hair_system = BKE_hair_new();
	groom->hair_draw_settings = BKE_hair_draw_settings_new();
}

void *BKE_groom_add(Main *bmain, const char *name)
{
	Groom *groom = BKE_libblock_alloc(bmain, ID_GM, name, 0);

	BKE_groom_init(groom);

	return groom;
}

void BKE_groom_bundle_curve_cache_clear(GroomBundle *bundle)
{
	if (bundle->curvecache)
	{
		MEM_freeN(bundle->curvecache);
		bundle->curvecache = NULL;
		bundle->curvesize = 0;
		bundle->totcurvecache = 0;
	}
}

static void groom_bundles_free(ListBase *bundles)
{
	for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
	{
		BKE_groom_bundle_curve_cache_clear(bundle);
		
		if (bundle->sections)
		{
			MEM_freeN(bundle->sections);
		}
		if (bundle->verts)
		{
			MEM_freeN(bundle->verts);
		}
	}
	BLI_freelistN(bundles);
}

/** Free (or release) any data used by this groom (does not free the groom itself). */
void BKE_groom_free(Groom *groom)
{
	BKE_groom_batch_cache_free(groom);
	
	if (groom->editgroom)
	{
		EditGroom *edit = groom->editgroom;
		
		groom_bundles_free(&edit->bundles);
		
		MEM_freeN(edit);
		groom->editgroom = NULL;
	}
	
	MEM_SAFE_FREE(groom->bb);
	
	if (groom->hair_system)
	{
		BKE_hair_free(groom->hair_system);
	}
	if (groom->hair_draw_settings)
	{
		BKE_hair_draw_settings_free(groom->hair_draw_settings);
	}
	
	groom_bundles_free(&groom->bundles);
	
	BKE_animdata_free(&groom->id, false);
}

/**
 * Only copy internal data of Groom ID from source to already allocated/initialized destination.
 * You probably never want to use that directly, use id_copy or BKE_id_copy_ex for typical needs.
 *
 * WARNING! This function will not handle ID user count!
 *
 * \param flag  Copying options (see BKE_library.h's LIB_ID_COPY_... flags for more).
 */
void BKE_groom_copy_data(Main *UNUSED(bmain), Groom *groom_dst, const Groom *groom_src, const int UNUSED(flag))
{
	groom_dst->bb = MEM_dupallocN(groom_src->bb);
	
	BLI_duplicatelist(&groom_dst->bundles, &groom_src->bundles);
	for (GroomBundle *bundle = groom_dst->bundles.first; bundle; bundle = bundle->next)
	{
		if (bundle->curvecache)
		{
			bundle->curvecache = MEM_dupallocN(bundle->curvecache);
		}
		if (bundle->sections)
		{
			bundle->sections = MEM_dupallocN(bundle->sections);
		}
		if (bundle->verts)
		{
			bundle->verts = MEM_dupallocN(bundle->verts);
		}
	}
	
	groom_dst->editgroom = NULL;
	
	if (groom_dst->hair_system)
	{
		groom_dst->hair_system = BKE_hair_copy(groom_dst->hair_system);
	}
	if (groom_dst->hair_draw_settings)
	{
		groom_dst->hair_draw_settings = BKE_hair_draw_settings_copy(groom_dst->hair_draw_settings);
	}
}

Groom *BKE_groom_copy(Main *bmain, const Groom *groom)
{
	Groom *groom_copy;
	BKE_id_copy_ex(bmain, &groom->id, (ID **)&groom_copy, 0, false);
	return groom_copy;
}

void BKE_groom_make_local(Main *bmain, Groom *groom, const bool lib_local)
{
	BKE_id_make_local_generic(bmain, &groom->id, true, lib_local);
}


bool BKE_groom_minmax(Groom *groom, float min[3], float max[3])
{
	// TODO
	UNUSED_VARS(groom, min, max);
	return true;
}

void BKE_groom_boundbox_calc(Groom *groom, float r_loc[3], float r_size[3])
{
	if (groom->bb == NULL)
	{
		groom->bb = MEM_callocN(sizeof(BoundBox), "boundbox");
	}

	float mloc[3], msize[3];
	if (!r_loc)
	{
		r_loc = mloc;
	}
	if (!r_size)
	{
		r_size = msize;
	}

	float min[3], max[3];
	INIT_MINMAX(min, max);
	if (!BKE_groom_minmax(groom, min, max)) {
		min[0] = min[1] = min[2] = -1.0f;
		max[0] = max[1] = max[2] = 1.0f;
	}

	mid_v3_v3v3(r_loc, min, max);

	r_size[0] = (max[0] - min[0]) / 2.0f;
	r_size[1] = (max[1] - min[1]) / 2.0f;
	r_size[2] = (max[2] - min[2]) / 2.0f;

	BKE_boundbox_init_from_minmax(groom->bb, min, max);
	groom->bb->flag &= ~BOUNDBOX_DIRTY;
}


/* === Scalp regions === */

void BKE_groom_bind_scalp_regions(Groom *groom, bool force_rebind)
{
	if (groom->editgroom)
	{
		for (GroomBundle *bundle = groom->editgroom->bundles.first; bundle; bundle = bundle->next)
		{
			BKE_groom_bundle_bind(groom, bundle, force_rebind);
		}
	}
	else
	{
		for (GroomBundle *bundle = groom->bundles.first; bundle; bundle = bundle->next)
		{
			BKE_groom_bundle_bind(groom, bundle, force_rebind);
		}
	}
}

static bool groom_shape_rebuild(GroomBundle *bundle, int numshapeverts, Object *scalp_ob)
{
	BLI_assert(bundle->scalp_region != NULL);
	BLI_assert(scalp_ob->type == OB_MESH);
	
	bool result = true;
	float (*shape)[2] = MEM_mallocN(sizeof(*shape) * numshapeverts, "groom section shape");
	
	Mesh *me = scalp_ob->data;
	// XXX MeshSample will use Mesh instead of DerivedMesh in the future
	DerivedMesh *dm = CDDM_from_mesh(me);
	
	/* last sample is the center position */
	MeshSample *center_sample = &bundle->scalp_region[numshapeverts];
	float center_co[3], center_nor[3], center_tang[3], center_binor[3];
	if (!BKE_mesh_sample_eval(dm, center_sample, center_co, center_nor, center_tang))
	{
		result = false;
		goto cleanup;
	}
	cross_v3_v3v3(center_binor, center_nor, center_tang);
	
	MeshSample *sample = bundle->scalp_region;
	GroomSectionVertex *vert0 = bundle->verts;
	for (int i = 0; i < numshapeverts; ++i, ++sample, ++vert0)
	{
		/* 3D position of the shape vertex origin on the mesh */
		float co[3], nor[3], tang[3];
		if (!BKE_mesh_sample_eval(dm, sample, co, nor, tang))
		{
			result = false;
			goto cleanup;
		}
		/* Get relative offset from the center */
		sub_v3_v3(co, center_co);
		/* Convert mesh surface positions to 2D shape
		 * by projecting onto the normal plane
		 */
		shape[i][0] = dot_v3v3(co, center_binor);
		shape[i][1] = dot_v3v3(co, center_tang);
	}
	
	bundle->numshapeverts = numshapeverts;
	bundle->totverts = numshapeverts * bundle->totsections;
	bundle->verts = MEM_reallocN_id(bundle->verts, sizeof(*bundle->verts) * bundle->totverts, "groom bundle vertices");
	/* Set the shape for all sections */
	GroomSectionVertex *vert = bundle->verts;
	for (int i = 0; i < bundle->totsections; ++i)
	{
		for (int j = 0; j < numshapeverts; ++j, ++vert)
		{
			copy_v2_v2(vert->co, shape[j]);
			vert->flag = 0;
		}
	}
	
cleanup:
	MEM_freeN(shape);
	dm->release(dm);
	
	return result;
}

static BMesh *groom_create_scalp_bmesh(Mesh *me)
{
	const BMAllocTemplate allocsize = BMALLOC_TEMPLATE_FROM_ME(me);
	
	BMesh *bm = BM_mesh_create(&allocsize, &((struct BMeshCreateParams){
	        .use_toolflags = true,
	         }));
	
	BM_mesh_bm_from_me(bm, me, (&(struct BMeshFromMeshParams){
	        .calc_face_normal = true, .use_shapekey = false,
	        }));
	
	return bm;
}

static bool groom_bundle_region_from_mesh_fmap(GroomBundle *bundle, Object *scalp_ob)
{
	BLI_assert(scalp_ob->type == OB_MESH);
	
	BKE_groom_bundle_curve_cache_clear(bundle);
	
	Mesh *me = scalp_ob->data;
	const int scalp_fmap_nr = BKE_object_facemap_name_index(scalp_ob, bundle->scalp_facemap_name);
	const int cd_fmap_offset = CustomData_get_offset(&me->pdata, CD_FACEMAP);
	if (scalp_fmap_nr < 0 || cd_fmap_offset < 0)
	{
		return false;
	}
	
	BMesh *bm = groom_create_scalp_bmesh(me);
	bool result = true;
	
	/* Tag faces in the face map for the BMO walker */
	{
		BMFace *f;
		BMIter iter;
		BM_ITER_MESH(f, &iter, bm, BM_FACES_OF_MESH)
		{
			int fmap = BM_ELEM_CD_GET_INT(f, cd_fmap_offset);
			BM_elem_flag_set(f, BM_ELEM_TAG, fmap == scalp_fmap_nr);
		}
	}
	
	BMOperator op;
	BMO_op_initf(bm, &op, (BMO_FLAG_DEFAULTS & ~BMO_FLAG_RESPECT_HIDE),
	             "face_island_boundary faces=%hf", BM_ELEM_TAG);
	
	BMO_op_exec(bm, &op);
	if (BMO_error_occurred(bm))
	{
		result = false;
		goto finalize;
	}
	
	const int numshapeverts = BMO_slot_buffer_count(op.slots_out, "boundary");
	bundle->scalp_region = MEM_callocN(sizeof(*bundle->scalp_region) * (numshapeverts + 1), "groom bundle scalp region");
	
	float center_co[3]; /* average vertex location for placing the center */
	{
		BMLoop *l;
		BMOIter oiter;
		MeshSample *sample = bundle->scalp_region;
		zero_v3(center_co);
		BMO_ITER (l, &oiter, op.slots_out, "boundary", BM_LOOP)
		{
			sample->orig_poly = BM_elem_index_get(l->f);
			sample->orig_loops[0] = BM_elem_index_get(l);
			sample->orig_verts[0] = BM_elem_index_get(l->v);
			sample->orig_weights[0] = 1.0f;
			BLI_assert(BKE_mesh_sample_is_valid(sample));
			
			add_v3_v3(center_co, l->v->co);
			
			++sample;
		}
		if (numshapeverts > 0)
		{
			mul_v3_fl(center_co, 1.0f / (float)(numshapeverts));
		}
	}
	
	{
		/* BVH tree for binding the region center location */
		DerivedMesh *dm = CDDM_from_mesh(me);
		DM_ensure_tessface(dm);
		BVHTreeFromMesh bvhtree;
		//bvhtree_from_mesh_looptri(&bvhtree, dm, 0.0f, 4, 6);
		bvhtree_from_mesh_faces(&bvhtree, dm, 0.0f, 4, 6);
		if (bvhtree.tree != NULL) {
			BVHTreeNearest nearest;
			nearest.index = -1;
			nearest.dist_sq = FLT_MAX;
			
			BLI_bvhtree_find_nearest(bvhtree.tree, center_co, &nearest, bvhtree.nearest_callback, &bvhtree);
			if (nearest.index >= 0)
			{
				/* last sample is the center position */
				MeshSample *center_sample = &bundle->scalp_region[numshapeverts];
				BKE_mesh_sample_weights_from_loc(center_sample, dm, nearest.index, nearest.co);
				BLI_assert(BKE_mesh_sample_is_valid(center_sample));
			}
		}
		else
		{
			result = false;
		}
	
		free_bvhtree_from_mesh(&bvhtree);
		dm->release(dm);
	}
	
finalize:
	if (result == true)
	{
		groom_shape_rebuild(bundle, numshapeverts, scalp_ob);
	}
	else
	{
		if (bundle->scalp_region)
		{
			MEM_freeN(bundle->scalp_region);
			bundle->scalp_region = NULL;
		}
	}
	
	BMO_op_finish(bm, &op);
	BM_mesh_free(bm);
	
	return result;
}

bool BKE_groom_bundle_bind(Groom *groom, GroomBundle *bundle, bool force_rebind)
{
	if (bundle->scalp_region && !force_rebind)
	{
		return true;
	}
	
	BKE_groom_bundle_unbind(bundle);
	if (!groom->scalp_object)
	{
		return false;
	}
	if (!BKE_object_facemap_find_name(groom->scalp_object, bundle->scalp_facemap_name))
	{
		return false;
	}
	
	if (groom->scalp_object->type == OB_MESH)
	{
		groom_bundle_region_from_mesh_fmap(bundle, groom->scalp_object);
	}
	
	return (bundle->scalp_region != NULL);
}

void BKE_groom_bundle_unbind(GroomBundle *bundle)
{
	if (bundle->scalp_region)
	{
		MEM_freeN(bundle->scalp_region);
		bundle->scalp_region = NULL;
	}
}


/* === Curve cache === */

/* forward differencing method for cubic polynomial eval */
static void groom_forward_diff_cubic(float a, float b, float c, float d, float *p, int it, int stride)
{
	float f = (float)it;
	a *= 1.0f / (f*f*f);
	b *= 1.0f / (f*f);
	c *= 1.0f / (f);
	
	float q0 = d;
	float q1 = a + b + c;
	float q2 = 6 * a + 2 * b;
	float q3 = 6 * a;

	for (int i = 0; i <= it; i++) {
		*p = q0;
		p = POINTER_OFFSET(p, stride);
		q0 += q1;
		q1 += q2;
		q2 += q3;
	}
}

/* cubic bspline section eval */
static void groom_eval_curve_cache_section(GroomCurveCache *cache, int curve_res, const float *co0, const float *co1, const float *co2, const float *co3)
{
	float a, b, c, d;
	for (int k = 0; k < 3; ++k)
	{
		/* define tangents from segment direction */
		float n1, n2;
		
		if (co0)
		{
			n1 = 0.5f * (co2[k] - co0[k]);
		}
		else
		{
			n1 = co2[k] - co1[k];
		}
		
		if (co3)
		{
			n2 = 0.5f * (co3[k] - co1[k]);
		}
		else
		{
			n2 = co2[k] - co1[k];
		}
		
		/* Hermite spline interpolation */
		a = 2.0f * (co1[k] - co2[k]) + n1 + n2;
		b = 3.0f * (co2[k] - co1[k]) - 2.0f * n1 - n2;
		c = n1;
		d = co1[k];
		
		groom_forward_diff_cubic(a, b, c, d, cache->co + k, curve_res, sizeof(*cache));
	}
}

static void groom_eval_center_curve_section(
        GroomBundle *bundle,
        int curve_res)
{
	BLI_assert(bundle->totsections >= 2);
	BLI_assert(curve_res >= 1);
	
	/* last cache curve is the center curve */
	GroomCurveCache *cache = bundle->curvecache + bundle->curvesize * bundle->numshapeverts;
	for (int i = 0; i < bundle->totsections-1; ++i, cache += curve_res)
	{
		const GroomSection *section = &bundle->sections[i];
		const float *co0 = (i > 0) ? section[-1].center : NULL;
		const float *co1 = section[0].center;
		const float *co2 = section[1].center;
		const float *co3 = (i < bundle->totsections - 2) ? section[2].center : NULL;
		groom_eval_curve_cache_section(cache, curve_res, co0, co1, co2, co3);
	}
}

static void groom_eval_shape_curves(
        GroomBundle *bundle,
        int curve_res)
{
	BLI_assert(bundle->totsections >= 2);
	BLI_assert(curve_res >= 1);
	
	for (int i = 0; i < bundle->numshapeverts; ++i)
	{
		GroomCurveCache *cache = bundle->curvecache + i * bundle->curvesize;
		for (int j = 0; j < bundle->totsections-1; ++j, cache += curve_res)
		{
			const GroomSection *section = &bundle->sections[j];
			const float *co0 = NULL, *co1 = NULL, *co2 =NULL, *co3 = NULL;
			
			float vec0[3], vec1[3], vec2[3], vec3[3];
			if (j > 0)
			{
				const GroomSectionVertex *v0 = &bundle->verts[(j-1) * bundle->numshapeverts + i];
				float tmp[3] = {0.0f, 0.0f, 0.0f};
				copy_v2_v2(tmp, v0->co);
				mul_v3_m3v3(vec0, section[-1].mat, tmp);
				add_v3_v3(vec0, section[-1].center);
				co0 = vec0;
			}
			{
				const GroomSectionVertex *v1 = &bundle->verts[(j) * bundle->numshapeverts + i];
				float tmp[3] = {0.0f, 0.0f, 0.0f};
				copy_v2_v2(tmp, v1->co);
				mul_v3_m3v3(vec1, section[0].mat, tmp);
				add_v3_v3(vec1, section[0].center);
				co1 = vec1;
			}
			{
				const GroomSectionVertex *v2 = &bundle->verts[(j+1) * bundle->numshapeverts + i];
				float tmp[3] = {0.0f, 0.0f, 0.0f};
				copy_v2_v2(tmp, v2->co);
				mul_v3_m3v3(vec2, section[+1].mat, tmp);
				add_v3_v3(vec2, section[+1].center);
				co2 = vec2;
			}
			if (j < bundle->totsections - 2)
			{
				const GroomSectionVertex *v3 = &bundle->verts[(j+2) * bundle->numshapeverts + i];
				float tmp[3] = {0.0f, 0.0f, 0.0f};
				copy_v2_v2(tmp, v3->co);
				mul_v3_m3v3(vec3, section[+2].mat, tmp);
				add_v3_v3(vec3, section[+2].center);
				co3 = vec3;
			}
			
			groom_eval_curve_cache_section(cache, curve_res, co0, co1, co2, co3);
		}
	}
}

static void groom_eval_curve_step(float mat[3][3], const float mat_prev[3][3], const float co0[3], const float co1[3])
{
	float dir[3];
	sub_v3_v3v3(dir, co1, co0);
	normalize_v3(dir);
	
	float dir_prev[3];
	normalize_v3_v3(dir_prev, mat_prev[2]);
	float rot[3][3];
	rotation_between_vecs_to_mat3(rot, dir_prev, dir);
	
	mul_m3_m3m3(mat, rot, mat_prev);
}

static void groom_eval_section_mats(GroomBundle *bundle, int curve_res)
{
	const int curvesize = bundle->curvesize;
	const int numshapeverts = bundle->numshapeverts;
	
	float mat[3][3];
	unit_m3(mat); // TODO take from scalp mesh sample
	
	GroomSection *section = bundle->sections;
	/* last curve cache is center curve */
	const GroomCurveCache *cache = bundle->curvecache + bundle->curvesize * numshapeverts;
	
	/* align to first segment */
	groom_eval_curve_step(mat, mat, cache[0].co, cache[1].co);
	copy_m3_m3(section->mat, mat);
	++cache;
	++section;
	
	for (int i = 1; i < curvesize - 1; ++i, ++cache)
	{
		/* align interior points to average of prev and next segment */
		groom_eval_curve_step(mat, mat, cache[-1].co, cache[+1].co);
		
		if (i % curve_res == 0)
		{
			/* set section matrix */
			copy_m3_m3(section->mat, mat);
			++section;
		}
	}
	
	/* align to last segment */
	groom_eval_curve_step(mat, mat, cache[-1].co, cache[0].co);
	/* last section is not handled in the loop */
	copy_m3_m3(section->mat, mat);
}

void BKE_groom_curve_cache_update(Groom *groom)
{
	ListBase *bundles = (groom->editgroom ? &groom->editgroom->bundles : &groom->bundles);
	
	for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
	{
		const int totsections = bundle->totsections;
		const int numshapeverts = bundle->numshapeverts;
		const int curve_res = groom->curve_res;
		if (totsections == 0)
		{
			BKE_groom_bundle_curve_cache_clear(bundle);
			
			/* nothing to do */
			continue;
		}
		
		bundle->curvesize = (totsections-1) * curve_res + 1;
		bundle->totcurvecache = bundle->curvesize * (numshapeverts + 1);
		bundle->curvecache = MEM_reallocN_id(bundle->curvecache, sizeof(GroomCurveCache) * bundle->totcurvecache, "groom bundle curve cache");
		
		if (totsections == 1)
		{
			/* degenerate case */
			copy_v3_v3(bundle->curvecache[numshapeverts].co, bundle->sections[0].center);
			
			unit_m3(bundle->sections[0].mat);
			
			for (int i = 0; i < numshapeverts; ++i)
			{
				copy_v2_v2(bundle->curvecache[i].co, bundle->verts[i].co);
				bundle->curvecache[i].co[2] = 0.0f;
			}
			
			continue;
		}
		
		/* Calculate center curve */
		groom_eval_center_curve_section(bundle, curve_res);
		
		/* Calculate coordinate frames for sections */
		groom_eval_section_mats(bundle, curve_res);
		
		/* Calculate shape curves */
		groom_eval_shape_curves(bundle, curve_res);
	}
}

void BKE_groom_curve_cache_clear(Groom *groom)
{
	for (GroomBundle *bundle = groom->bundles.first; bundle; bundle = bundle->next)
	{
		BKE_groom_bundle_curve_cache_clear(bundle);
	}
	if (groom->editgroom)
	{
		for (GroomBundle *bundle = groom->editgroom->bundles.first; bundle; bundle = bundle->next)
		{
			BKE_groom_bundle_curve_cache_clear(bundle);
		}
	}
}


/* === Depsgraph evaluation === */

void BKE_groom_eval_geometry(const EvaluationContext *UNUSED(eval_ctx), Groom *groom)
{
	if (G.debug & G_DEBUG_DEPSGRAPH) {
		printf("%s on %s\n", __func__, groom->id.name);
	}
	
	BKE_groom_curve_cache_update(groom);
	
	if (groom->bb == NULL || (groom->bb->flag & BOUNDBOX_DIRTY)) {
		BKE_groom_boundbox_calc(groom, NULL, NULL);
	}
}


/* === Draw Cache === */

void (*BKE_groom_batch_cache_dirty_cb)(Groom* groom, int mode) = NULL;
void (*BKE_groom_batch_cache_free_cb)(Groom* groom) = NULL;

void BKE_groom_batch_cache_dirty(Groom* groom, int mode)
{
	if (groom->batch_cache)
	{
		BKE_groom_batch_cache_dirty_cb(groom, mode);
	}
}

void BKE_groom_batch_cache_free(Groom *groom)
{
	if (groom->batch_cache)
	{
		BKE_groom_batch_cache_free_cb(groom);
	}
}

/* === Utility functions (DerivedMesh SOON TO BE DEPRECATED!) === */

struct DerivedMesh* BKE_groom_get_scalp(struct Groom *groom)
{
	return groom->scalp_object ? groom->scalp_object->derivedFinal : NULL;
}
