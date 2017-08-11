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

/** \file blender/blenkernel/intern/editstrands.c
 *  \ingroup bke
 */

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_mempool.h"

#include "DNA_customdata_types.h"
#include "DNA_key_types.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"

#include "BKE_bvhutils.h"
#include "BKE_customdata.h"
#include "BKE_cdderivedmesh.h"
#include "BKE_DerivedMesh.h"
#include "BKE_editstrands.h"
#include "BKE_effect.h"
#include "BKE_hair.h"
#include "BKE_mesh_sample.h"
#include "BKE_object.h"
#include "BKE_particle.h"

#include "BPH_strands.h"

#include "intern/bmesh_mesh_conv.h"
#include "intern/bmesh_strands_conv.h"

BMEditStrands *BKE_editstrands_create(BMesh *bm, DerivedMesh *root_dm)
{
	BMEditStrands *es = MEM_callocN(sizeof(BMEditStrands), __func__);
	
	es->base.bm = bm;
	es->root_dm = CDDM_copy(root_dm);
	
	BKE_editstrands_batch_cache_dirty(es, BKE_STRANDS_BATCH_DIRTY_ALL);
	
	return es;
}

BMEditStrands *BKE_editstrands_copy(BMEditStrands *es)
{
	BMEditStrands *es_copy = MEM_callocN(sizeof(BMEditStrands), __func__);
	*es_copy = *es;
	
	es_copy->base.bm = BM_mesh_copy(es->base.bm);
	es_copy->root_dm = CDDM_copy(es->root_dm);
	
	BKE_editstrands_batch_cache_dirty(es_copy, BKE_STRANDS_BATCH_DIRTY_ALL);
	
	return es_copy;
}

/**
 * \brief Return the BMEditStrands for a given object's particle systems
 */
BMEditStrands *BKE_editstrands_from_object_particles(Object *ob, ParticleSystem **r_psys)
{
	ParticleSystem *psys = psys_get_current(ob);
	if (psys && psys->hairedit) {
		if (r_psys) {
			*r_psys = psys;
		}
		return psys->hairedit;
	}
	
	if (r_psys) {
		*r_psys = NULL;
	}
	return NULL;
}

/**
 * \brief Return the BMEditStrands for a given object
 */
BMEditStrands *BKE_editstrands_from_object(Object *ob)
{
	if (ob && ob->type == OB_MESH) {
		Mesh *me = ob->data;
		if (me->edit_strands)
			return me->edit_strands;
	}
	
	return BKE_editstrands_from_object_particles(ob, NULL);
}

void BKE_editstrands_update_linked_customdata(BMEditStrands *UNUSED(es))
{
}

/*does not free the BMEditStrands struct itself*/
void BKE_editstrands_free(BMEditStrands *es)
{
	BKE_editstrands_batch_cache_free(es);
	BKE_editstrands_hair_free(es);
	
	if (es->base.bm)
		BM_mesh_free(es->base.bm);
	if (es->root_dm)
		es->root_dm->release(es->root_dm);
}

/* === Hair fibers === */

typedef struct EditStrandsView {
	HairDrawDataInterface base;
	BMEditStrands *edit;
} EditStrandsView;

static int get_num_strands(const HairDrawDataInterface *hairdata_)
{
	const EditStrandsView *strands = (EditStrandsView *)hairdata_;
	BMesh *bm = strands->edit->base.bm;
	return BM_strands_count(bm);
}

static int get_num_verts(const HairDrawDataInterface *hairdata_)
{
	const EditStrandsView *strands = (EditStrandsView *)hairdata_;
	BMesh *bm = strands->edit->base.bm;
	return bm->totvert;
}

static void get_strand_lengths(const HairDrawDataInterface* hairdata_, int *r_lengths)
{
	const EditStrandsView *strands = (EditStrandsView *)hairdata_;
	BMesh *bm = strands->edit->base.bm;
	BMVert *v;
	BMIter iter;
	int i;
	
	int *length = r_lengths;
	BM_ITER_STRANDS_INDEX(v, &iter, bm, BM_STRANDS_OF_MESH, i) {
		*length = BM_strands_keys_count(v);
		++length;
	}
}

static void get_strand_roots(const HairDrawDataInterface* hairdata_, struct MeshSample *r_roots)
{
	const EditStrandsView *strands = (EditStrandsView *)hairdata_;
	BMesh *bm = strands->edit->base.bm;
	BMVert *v;
	BMIter iter;
	int i;
	
	MeshSample *root = r_roots;
	BM_ITER_STRANDS_INDEX(v, &iter, bm, BM_STRANDS_OF_MESH, i) {
		BM_elem_meshsample_data_named_get(&bm->vdata, v, CD_MSURFACE_SAMPLE, CD_HAIR_ROOT_LOCATION, root);
		++root;
	}
}

static void get_strand_vertices(const HairDrawDataInterface* hairdata_, float (*r_verts)[3])
{
	const EditStrandsView *strands = (EditStrandsView *)hairdata_;
	BMesh *bm = strands->edit->base.bm;
	BMVert *vert;
	BMIter iter;
	
	float (*co)[3] = r_verts;
	BM_ITER_MESH(vert, &iter, bm, BM_VERTS_OF_MESH) {
		copy_v3_v3(*co, vert->co);
		++co;
	}
}

static EditStrandsView editstrands_get_view(BMEditStrands *edit)
{
	EditStrandsView hairdata;
	hairdata.base.get_num_strands = get_num_strands;
	hairdata.base.get_num_verts = get_num_verts;
	hairdata.base.get_strand_lengths = get_strand_lengths;
	hairdata.base.get_strand_roots = get_strand_roots;
	hairdata.base.get_strand_vertices = get_strand_vertices;
	hairdata.edit = edit;
	return hairdata;
}

bool BKE_editstrands_hair_ensure(BMEditStrands *es)
{
	if (!es->root_dm || es->hair_totfibers == 0) {
		BKE_editstrands_hair_free(es);
		return false;
	}
	
	if (!es->hair_fibers) {
		EditStrandsView strands = editstrands_get_view(es);
		es->hair_fibers = BKE_hair_fibers_create(&strands.base, es->root_dm, es->hair_totfibers, es->hair_seed);
	}
	
	return true;
}

void BKE_editstrands_hair_free(BMEditStrands *es)
{
	if (es->hair_fibers)
	{
		MEM_freeN(es->hair_fibers);
		es->hair_fibers = NULL;
	}
}

int* BKE_editstrands_hair_get_fiber_lengths(BMEditStrands *es, int subdiv)
{
	EditStrandsView strands = editstrands_get_view(es);
	return BKE_hair_strands_get_fiber_lengths(es->hair_fibers, es->hair_totfibers, &strands.base, subdiv);
}

void BKE_editstrands_hair_get_texture_buffer_size(BMEditStrands *es, int subdiv, int *r_size,
                                                  int *r_strand_map_start, int *r_strand_vertex_start, int *r_fiber_start)
{
	EditStrandsView strands = editstrands_get_view(es);
	BKE_hair_strands_get_texture_buffer_size(&strands.base, es->hair_totfibers, subdiv, r_size,
	                                 r_strand_map_start, r_strand_vertex_start, r_fiber_start);
}

void BKE_editstrands_hair_get_texture_buffer(BMEditStrands *es, int subdiv, void *texbuffer)
{
	EditStrandsView strands = editstrands_get_view(es);
	BKE_hair_strands_get_texture_buffer(&strands.base, es->root_dm, es->hair_fibers, es->hair_totfibers, subdiv, texbuffer);
}

/* === Constraints === */

BMEditStrandsLocations BKE_editstrands_get_locations(BMEditStrands *edit)
{
	BMesh *bm = edit->base.bm;
	BMEditStrandsLocations locs = MEM_mallocN(3*sizeof(float) * bm->totvert, "editstrands locations");
	
	BMVert *v;
	BMIter iter;
	int i;
	
	BM_ITER_MESH_INDEX(v, &iter, bm, BM_VERTS_OF_MESH, i) {
		copy_v3_v3(locs[i], v->co);
	}
	
	return locs;
}

void BKE_editstrands_free_locations(BMEditStrandsLocations locs)
{
	MEM_freeN(locs);
}

void BKE_editstrands_solve_constraints(Object *ob, BMEditStrands *es, BMEditStrandsLocations orig)
{
	BKE_editstrands_ensure(es);
	
	BPH_strands_solve_constraints(ob, es, orig);
	
	BKE_editstrands_batch_cache_dirty(es, BKE_STRANDS_BATCH_DIRTY_ALL);
}

static void editstrands_calc_segment_lengths(BMesh *bm)
{
	BMVert *root, *v, *vprev;
	BMIter iter, iter_strand;
	int k;
	
	BM_ITER_STRANDS(root, &iter, bm, BM_STRANDS_OF_MESH) {
		BM_ITER_STRANDS_ELEM_INDEX(v, &iter_strand, root, BM_VERTS_OF_STRAND, k) {
			if (k > 0) {
				float length = len_v3v3(v->co, vprev->co);
				BM_elem_float_data_named_set(&bm->vdata, v, CD_PROP_FLT, CD_HAIR_SEGMENT_LENGTH, length);
			}
			vprev = v;
		}
	}
}

void BKE_editstrands_ensure(BMEditStrands *es)
{
	BM_strands_cd_flag_ensure(es->base.bm, 0);
	
	if (es->flag & BM_STRANDS_DIRTY_SEGLEN) {
		editstrands_calc_segment_lengths(es->base.bm);
		
		es->flag &= ~BM_STRANDS_DIRTY_SEGLEN;
	}
}


/* === Particle Conversion === */

BMesh *BKE_editstrands_particles_to_bmesh(Object *ob, ParticleSystem *psys)
{
	ParticleSystemModifierData *psmd = psys_get_modifier(ob, psys);
	
	const BMAllocTemplate allocsize = BMALLOC_TEMPLATE_FROM_PSYS(psys);
	BMesh *bm;

	bm = BM_mesh_create(&allocsize,
	                    &((struct BMeshCreateParams){.use_toolflags = false,}));

	if (psmd && psmd->dm_final) {
		DM_ensure_tessface(psmd->dm_final);
		
		BM_strands_bm_from_psys(bm, ob, psys, psmd->dm_final, true, /*psys->shapenr*/ -1);
		
		editstrands_calc_segment_lengths(bm);
	}

	return bm;
}

void BKE_editstrands_particles_from_bmesh(Object *ob, ParticleSystem *psys)
{
	ParticleSystemModifierData *psmd = psys_get_modifier(ob, psys);
	BMesh *bm = psys->hairedit ? psys->hairedit->base.bm : NULL;
	
	if (bm) {
		if (psmd && psmd->dm_final) {
			BVHTreeFromMesh bvhtree = {NULL};
			
			DM_ensure_tessface(psmd->dm_final);
			
			bvhtree_from_mesh_faces(&bvhtree, psmd->dm_final, 0.0, 2, 6);
			
			BM_strands_bm_to_psys(bm, ob, psys, psmd->dm_final, &bvhtree);
			
			free_bvhtree_from_mesh(&bvhtree);
		}
	}
}


/* === Mesh Conversion === */

BMesh *BKE_editstrands_mesh_to_bmesh(Object *ob, Mesh *me)
{
	const BMAllocTemplate allocsize = BMALLOC_TEMPLATE_FROM_ME(me);
	BMesh *bm;
	struct BMeshFromMeshParams params = {0};
	
	bm = BM_mesh_create(&allocsize,
	                    &((struct BMeshCreateParams){.use_toolflags = false,}));
	
	params.use_shapekey = true;
	params.active_shapekey = ob->shapenr;
	BM_mesh_bm_from_me(bm, me, &params);
	BM_strands_cd_flag_ensure(bm, 0);
	
	editstrands_calc_segment_lengths(bm);
	
	return bm;
}

void BKE_editstrands_mesh_from_bmesh(Object *ob)
{
	Mesh *me = ob->data;
	BMesh *bm = me->edit_strands->base.bm;
	struct BMeshToMeshParams params = {0};

	/* Workaround for T42360, 'ob->shapenr' should be 1 in this case.
	 * however this isn't synchronized between objects at the moment. */
	if (UNLIKELY((ob->shapenr == 0) && (me->key && !BLI_listbase_is_empty(&me->key->block)))) {
		bm->shapenr = 1;
	}

	BM_mesh_bm_to_me(bm, me, &params);

#ifdef USE_TESSFACE_DEFAULT
	BKE_mesh_tessface_calc(me);
#endif

	/* free derived mesh. usually this would happen through depsgraph but there
	 * are exceptions like file save that will not cause this, and we want to
	 * avoid ending up with an invalid derived mesh then */
	BKE_object_free_derived_caches(ob);
}

/* === Draw Cache === */
void (*BKE_editstrands_batch_cache_dirty_cb)(BMEditStrands *es, int mode) = NULL;
void (*BKE_editstrands_batch_cache_free_cb)(BMEditStrands *es) = NULL;

void BKE_editstrands_batch_cache_dirty(BMEditStrands *es, int mode)
{
	if (es->batch_cache) {
		BKE_editstrands_batch_cache_dirty_cb(es, mode);
	}
}

void BKE_editstrands_batch_cache_free(BMEditStrands *es)
{
	if (es->batch_cache) {
		BKE_editstrands_batch_cache_free_cb(es);
	}
}
