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

/** \file blender/blenkernel/intern/DerivedMesh.c
 *  \ingroup bke
 */

#include "MEM_guardedalloc.h"

#include "DNA_key_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"

#include "BLI_array.h"
#include "BLI_blenlib.h"
#include "BLI_math.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_key.h"
#include "BKE_library.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"
#include "BKE_object.h"
#include "BKE_global.h" /* For debug flag, DM_update_tessface_data() func. */

#include "GPU_buffers.h"
#include "GPU_glew.h"
#include "GPU_shader.h"

static void shapekey_layers_to_keyblocks(DerivedMesh *dm, Mesh *me, int actshape_uid);

/* -------------------------------------------------------------------- */

static MVert *dm_getVertArray(DerivedMesh *dm)
{
	MVert *mvert = CustomData_get_layer(&dm->vertData, CD_MVERT);

	if (!mvert) {
		mvert = CustomData_add_layer(&dm->vertData, CD_MVERT, CD_CALLOC, NULL,
		                             dm->getNumVerts(dm));
		CustomData_set_layer_flag(&dm->vertData, CD_MVERT, CD_FLAG_TEMPORARY);
		dm->copyVertArray(dm, mvert);
	}

	return mvert;
}

static MEdge *dm_getEdgeArray(DerivedMesh *dm)
{
	MEdge *medge = CustomData_get_layer(&dm->edgeData, CD_MEDGE);

	if (!medge) {
		medge = CustomData_add_layer(&dm->edgeData, CD_MEDGE, CD_CALLOC, NULL,
		                             dm->getNumEdges(dm));
		CustomData_set_layer_flag(&dm->edgeData, CD_MEDGE, CD_FLAG_TEMPORARY);
		dm->copyEdgeArray(dm, medge);
	}

	return medge;
}

static MFace *dm_getTessFaceArray(DerivedMesh *dm)
{
	MFace *mface = CustomData_get_layer(&dm->faceData, CD_MFACE);

	if (!mface) {
		int numTessFaces = dm->getNumTessFaces(dm);
		
		if (!numTessFaces) {
			/* Do not add layer if there's no elements in it, this leads to issues later when
			 * this layer is needed with non-zero size, but currently CD stuff does not check
			 * for requested layer size on creation and just returns layer which was previously
			 * added (sergey) */
			return NULL;
		}
		
		mface = CustomData_add_layer(&dm->faceData, CD_MFACE, CD_CALLOC, NULL, numTessFaces);
		CustomData_set_layer_flag(&dm->faceData, CD_MFACE, CD_FLAG_TEMPORARY);
		dm->copyTessFaceArray(dm, mface);
	}

	return mface;
}

static MLoop *dm_getLoopArray(DerivedMesh *dm)
{
	MLoop *mloop = CustomData_get_layer(&dm->loopData, CD_MLOOP);

	if (!mloop) {
		mloop = CustomData_add_layer(&dm->loopData, CD_MLOOP, CD_CALLOC, NULL,
		                             dm->getNumLoops(dm));
		CustomData_set_layer_flag(&dm->loopData, CD_MLOOP, CD_FLAG_TEMPORARY);
		dm->copyLoopArray(dm, mloop);
	}

	return mloop;
}

static MPoly *dm_getPolyArray(DerivedMesh *dm)
{
	MPoly *mpoly = CustomData_get_layer(&dm->polyData, CD_MPOLY);

	if (!mpoly) {
		mpoly = CustomData_add_layer(&dm->polyData, CD_MPOLY, CD_CALLOC, NULL,
		                             dm->getNumPolys(dm));
		CustomData_set_layer_flag(&dm->polyData, CD_MPOLY, CD_FLAG_TEMPORARY);
		dm->copyPolyArray(dm, mpoly);
	}

	return mpoly;
}

static MVert *dm_dupVertArray(DerivedMesh *dm)
{
	MVert *tmp = MEM_mallocN(sizeof(*tmp) * dm->getNumVerts(dm),
	                         "dm_dupVertArray tmp");

	if (tmp) dm->copyVertArray(dm, tmp);

	return tmp;
}

static MEdge *dm_dupEdgeArray(DerivedMesh *dm)
{
	MEdge *tmp = MEM_mallocN(sizeof(*tmp) * dm->getNumEdges(dm),
	                         "dm_dupEdgeArray tmp");

	if (tmp) dm->copyEdgeArray(dm, tmp);

	return tmp;
}

static MFace *dm_dupFaceArray(DerivedMesh *dm)
{
	MFace *tmp = MEM_mallocN(sizeof(*tmp) * dm->getNumTessFaces(dm),
	                         "dm_dupFaceArray tmp");

	if (tmp) dm->copyTessFaceArray(dm, tmp);

	return tmp;
}

static MLoop *dm_dupLoopArray(DerivedMesh *dm)
{
	MLoop *tmp = MEM_mallocN(sizeof(*tmp) * dm->getNumLoops(dm),
	                         "dm_dupLoopArray tmp");

	if (tmp) dm->copyLoopArray(dm, tmp);

	return tmp;
}

static MPoly *dm_dupPolyArray(DerivedMesh *dm)
{
	MPoly *tmp = MEM_mallocN(sizeof(*tmp) * dm->getNumPolys(dm),
	                         "dm_dupPolyArray tmp");

	if (tmp) dm->copyPolyArray(dm, tmp);

	return tmp;
}

static int dm_getNumLoopTri(DerivedMesh *dm)
{
	return dm->looptris.num;
}

static CustomData *dm_getVertCData(DerivedMesh *dm)
{
	return &dm->vertData;
}

static CustomData *dm_getEdgeCData(DerivedMesh *dm)
{
	return &dm->edgeData;
}

static CustomData *dm_getTessFaceCData(DerivedMesh *dm)
{
	return &dm->faceData;
}

static CustomData *dm_getLoopCData(DerivedMesh *dm)
{
	return &dm->loopData;
}

static CustomData *dm_getPolyCData(DerivedMesh *dm)
{
	return &dm->polyData;
}

/**
 * Utility function to initialize a DerivedMesh's function pointers to
 * the default implementation (for those functions which have a default)
 */
void DM_init_funcs(DerivedMesh *dm)
{
	/* default function implementations */
	dm->getVertArray = dm_getVertArray;
	dm->getEdgeArray = dm_getEdgeArray;
	dm->getTessFaceArray = dm_getTessFaceArray;
	dm->getLoopArray = dm_getLoopArray;
	dm->getPolyArray = dm_getPolyArray;
	dm->dupVertArray = dm_dupVertArray;
	dm->dupEdgeArray = dm_dupEdgeArray;
	dm->dupTessFaceArray = dm_dupFaceArray;
	dm->dupLoopArray = dm_dupLoopArray;
	dm->dupPolyArray = dm_dupPolyArray;

	/* subtypes handle getting actual data */
	dm->getNumLoopTri = dm_getNumLoopTri;

	dm->getVertDataLayout = dm_getVertCData;
	dm->getEdgeDataLayout = dm_getEdgeCData;
	dm->getTessFaceDataLayout = dm_getTessFaceCData;
	dm->getLoopDataLayout = dm_getLoopCData;
	dm->getPolyDataLayout = dm_getPolyCData;

	dm->getVertData = DM_get_vert_data;
	dm->getEdgeData = DM_get_edge_data;
	dm->getTessFaceData = DM_get_tessface_data;
	dm->getPolyData = DM_get_poly_data;
	dm->getVertDataArray = DM_get_vert_data_layer;
	dm->getEdgeDataArray = DM_get_edge_data_layer;
	dm->getTessFaceDataArray = DM_get_tessface_data_layer;
	dm->getPolyDataArray = DM_get_poly_data_layer;
	dm->getLoopDataArray = DM_get_loop_data_layer;

	bvhcache_init(&dm->bvhCache);
}

/**
 * Utility function to initialize a DerivedMesh for the desired number
 * of vertices, edges and faces (doesn't allocate memory for them, just
 * sets up the custom data layers)
 */
void DM_init(
        DerivedMesh *dm, DerivedMeshType type, int numVerts, int numEdges,
        int numTessFaces, int numLoops, int numPolys)
{
	dm->type = type;
	dm->numVertData = numVerts;
	dm->numEdgeData = numEdges;
	dm->numTessFaceData = numTessFaces;
	dm->numLoopData = numLoops;
	dm->numPolyData = numPolys;

	DM_init_funcs(dm);
	
	dm->needsFree = 1;
	dm->auto_bump_scale = -1.0f;
	dm->dirty = 0;

	/* don't use CustomData_reset(...); because we dont want to touch customdata */
	copy_vn_i(dm->vertData.typemap, CD_NUMTYPES, -1);
	copy_vn_i(dm->edgeData.typemap, CD_NUMTYPES, -1);
	copy_vn_i(dm->faceData.typemap, CD_NUMTYPES, -1);
	copy_vn_i(dm->loopData.typemap, CD_NUMTYPES, -1);
	copy_vn_i(dm->polyData.typemap, CD_NUMTYPES, -1);
}

/**
 * Utility function to initialize a DerivedMesh for the desired number
 * of vertices, edges and faces, with a layer setup copied from source
 */
void DM_from_template_ex(
        DerivedMesh *dm, DerivedMesh *source, DerivedMeshType type,
        int numVerts, int numEdges, int numTessFaces,
        int numLoops, int numPolys,
        CustomDataMask mask)
{
	CustomData_copy(&source->vertData, &dm->vertData, mask, CD_CALLOC, numVerts);
	CustomData_copy(&source->edgeData, &dm->edgeData, mask, CD_CALLOC, numEdges);
	CustomData_copy(&source->faceData, &dm->faceData, mask, CD_CALLOC, numTessFaces);
	CustomData_copy(&source->loopData, &dm->loopData, mask, CD_CALLOC, numLoops);
	CustomData_copy(&source->polyData, &dm->polyData, mask, CD_CALLOC, numPolys);

	dm->cd_flag = source->cd_flag;

	dm->type = type;
	dm->numVertData = numVerts;
	dm->numEdgeData = numEdges;
	dm->numTessFaceData = numTessFaces;
	dm->numLoopData = numLoops;
	dm->numPolyData = numPolys;

	DM_init_funcs(dm);

	dm->needsFree = 1;
	dm->dirty = 0;
}
void DM_from_template(
        DerivedMesh *dm, DerivedMesh *source, DerivedMeshType type,
        int numVerts, int numEdges, int numTessFaces,
        int numLoops, int numPolys)
{
	DM_from_template_ex(
	        dm, source, type,
	        numVerts, numEdges, numTessFaces,
	        numLoops, numPolys,
	        CD_MASK_DERIVEDMESH);
}

int DM_release(DerivedMesh *dm)
{
	if (dm->needsFree) {
		bvhcache_free(&dm->bvhCache);
		GPU_drawobject_free(dm);
		CustomData_free(&dm->vertData, dm->numVertData);
		CustomData_free(&dm->edgeData, dm->numEdgeData);
		CustomData_free(&dm->faceData, dm->numTessFaceData);
		CustomData_free(&dm->loopData, dm->numLoopData);
		CustomData_free(&dm->polyData, dm->numPolyData);

		if (dm->mat) {
			MEM_freeN(dm->mat);
			dm->mat = NULL;
			dm->totmat = 0;
		}

		MEM_SAFE_FREE(dm->looptris.array);
		dm->looptris.num = 0;
		dm->looptris.num_alloc = 0;

		return 1;
	}
	else {
		CustomData_free_temporary(&dm->vertData, dm->numVertData);
		CustomData_free_temporary(&dm->edgeData, dm->numEdgeData);
		CustomData_free_temporary(&dm->faceData, dm->numTessFaceData);
		CustomData_free_temporary(&dm->loopData, dm->numLoopData);
		CustomData_free_temporary(&dm->polyData, dm->numPolyData);

		return 0;
	}
}

void DM_DupPolys(DerivedMesh *source, DerivedMesh *target)
{
	CustomData_free(&target->loopData, source->numLoopData);
	CustomData_free(&target->polyData, source->numPolyData);

	CustomData_copy(&source->loopData, &target->loopData, CD_MASK_DERIVEDMESH, CD_DUPLICATE, source->numLoopData);
	CustomData_copy(&source->polyData, &target->polyData, CD_MASK_DERIVEDMESH, CD_DUPLICATE, source->numPolyData);

	target->numLoopData = source->numLoopData;
	target->numPolyData = source->numPolyData;

	if (!CustomData_has_layer(&target->polyData, CD_MPOLY)) {
		MPoly *mpoly;
		MLoop *mloop;

		mloop = source->dupLoopArray(source);
		mpoly = source->dupPolyArray(source);
		CustomData_add_layer(&target->loopData, CD_MLOOP, CD_ASSIGN, mloop, source->numLoopData);
		CustomData_add_layer(&target->polyData, CD_MPOLY, CD_ASSIGN, mpoly, source->numPolyData);
	}
}

void DM_ensure_normals(DerivedMesh *dm)
{
	if (dm->dirty & DM_DIRTY_NORMALS) {
		dm->calcNormals(dm);
	}
	BLI_assert((dm->dirty & DM_DIRTY_NORMALS) == 0);
}

/* note: until all modifiers can take MPoly's as input,
 * use this at the start of modifiers  */
void DM_ensure_tessface(DerivedMesh *dm)
{
	const int numTessFaces = dm->getNumTessFaces(dm);
	const int numPolys =     dm->getNumPolys(dm);

	if ((numTessFaces == 0) && (numPolys != 0)) {
		dm->recalcTessellation(dm);

		if (dm->getNumTessFaces(dm) != 0) {
			/* printf("info %s: polys -> ngons calculated\n", __func__); */
		}
		else {
			printf("warning %s: could not create tessfaces from %d polygons, dm->type=%u\n",
			       __func__, numPolys, dm->type);
		}
	}

	else if (dm->dirty & DM_DIRTY_TESS_CDLAYERS) {
		BLI_assert(CustomData_has_layer(&dm->faceData, CD_ORIGINDEX) || numTessFaces == 0);
		DM_update_tessface_data(dm);
	}

	dm->dirty &= ~DM_DIRTY_TESS_CDLAYERS;
}

/**
 * Ensure the array is large enough
 */
void DM_ensure_looptri_data(DerivedMesh *dm)
{
	const unsigned int totpoly = dm->numPolyData;
	const unsigned int totloop = dm->numLoopData;
	const int looptris_num = poly_to_tri_count(totpoly, totloop);

	if ((looptris_num > dm->looptris.num_alloc) ||
	    (looptris_num < dm->looptris.num_alloc * 2) ||
	    (totpoly == 0))
	{
		MEM_SAFE_FREE(dm->looptris.array);
		dm->looptris.num_alloc = 0;
		dm->looptris.num = 0;
	}

	if (totpoly) {
		if (dm->looptris.array == NULL) {
			dm->looptris.array = MEM_mallocN(sizeof(*dm->looptris.array) * looptris_num, __func__);
			dm->looptris.num_alloc = looptris_num;
		}

		dm->looptris.num = looptris_num;
	}
}

/**
 * The purpose of this function is that we can call:
 * `dm->getLoopTriArray(dm)` and get the array returned.
 */
void DM_ensure_looptri(DerivedMesh *dm)
{
	const int numPolys =  dm->getNumPolys(dm);

	if ((dm->looptris.num == 0) && (numPolys != 0)) {
		dm->recalcLoopTri(dm);
	}
}

void DM_verttri_from_looptri(MVertTri *verttri, const MLoop *mloop, const MLoopTri *looptri, int looptri_num)
{
	int i;
	for (i = 0; i < looptri_num; i++) {
		verttri[i].tri[0] = mloop[looptri[i].tri[0]].v;
		verttri[i].tri[1] = mloop[looptri[i].tri[1]].v;
		verttri[i].tri[2] = mloop[looptri[i].tri[2]].v;
	}
}

/* Update tessface CD data from loop/poly ones. Needed when not retessellating after modstack evaluation. */
/* NOTE: Assumes dm has valid tessellated data! */
void DM_update_tessface_data(DerivedMesh *dm)
{
	MFace *mf, *mface = dm->getTessFaceArray(dm);
	MPoly *mp = dm->getPolyArray(dm);
	MLoop *ml = dm->getLoopArray(dm);

	CustomData *fdata = dm->getTessFaceDataLayout(dm);
	CustomData *pdata = dm->getPolyDataLayout(dm);
	CustomData *ldata = dm->getLoopDataLayout(dm);

	const int totface = dm->getNumTessFaces(dm);
	int mf_idx;

	int *polyindex = CustomData_get_layer(fdata, CD_ORIGINDEX);
	unsigned int (*loopindex)[4];

	/* Should never occure, but better abort than segfault! */
	if (!polyindex)
		return;

	CustomData_from_bmeshpoly(fdata, pdata, ldata, totface);

	if (CustomData_has_layer(fdata, CD_MTFACE) ||
	    CustomData_has_layer(fdata, CD_MCOL) ||
	    CustomData_has_layer(fdata, CD_PREVIEW_MCOL) ||
	    CustomData_has_layer(fdata, CD_ORIGSPACE) ||
	    CustomData_has_layer(fdata, CD_TESSLOOPNORMAL) ||
	    CustomData_has_layer(fdata, CD_TANGENT))
	{
		loopindex = MEM_mallocN(sizeof(*loopindex) * totface, __func__);

		for (mf_idx = 0, mf = mface; mf_idx < totface; mf_idx++, mf++) {
			const int mf_len = mf->v4 ? 4 : 3;
			unsigned int *ml_idx = loopindex[mf_idx];
			int i, not_done;

			/* Find out loop indices. */
			/* NOTE: This assumes tessface are valid and in sync with loop/poly... Else, most likely, segfault! */
			for (i = mp[polyindex[mf_idx]].loopstart, not_done = mf_len; not_done; i++) {
				const int tf_v = BKE_MESH_TESSFACE_VINDEX_ORDER(mf, ml[i].v);
				if (tf_v != -1) {
					ml_idx[tf_v] = i;
					not_done--;
				}
			}
		}

		/* NOTE: quad detection issue - fourth vertidx vs fourth loopidx:
		 * Here, our tfaces' fourth vertex index is never 0 for a quad. However, we know our fourth loop index may be
		 * 0 for quads (because our quads may have been rotated compared to their org poly, see tessellation code).
		 * So we pass the MFace's, and BKE_mesh_loops_to_tessdata will use MFace->v4 index as quad test.
		 */
		BKE_mesh_loops_to_tessdata(fdata, ldata, pdata, mface, polyindex, loopindex, totface);

		MEM_freeN(loopindex);
	}

	if (G.debug & G_DEBUG)
		printf("%s: Updated tessellated customdata of dm %p\n", __func__, dm);

	dm->dirty &= ~DM_DIRTY_TESS_CDLAYERS;
}

void DM_generate_tangent_tessface_data(DerivedMesh *dm, bool generate)
{
	MFace *mf, *mface = dm->getTessFaceArray(dm);
	MPoly *mp = dm->getPolyArray(dm);
	MLoop *ml = dm->getLoopArray(dm);

	CustomData *fdata = dm->getTessFaceDataLayout(dm);
	CustomData *pdata = dm->getPolyDataLayout(dm);
	CustomData *ldata = dm->getLoopDataLayout(dm);

	const int totface = dm->getNumTessFaces(dm);
	int mf_idx;

	int *polyindex = CustomData_get_layer(fdata, CD_ORIGINDEX);
	unsigned int (*loopindex)[4];

	/* Should never occure, but better abort than segfault! */
	if (!polyindex)
		return;

	if (generate) {
		for (int i = 0; i < ldata->totlayer; i++) {
			if (ldata->layers[i].type == CD_TANGENT) {
				CustomData_add_layer_named(fdata, CD_TANGENT, CD_CALLOC, NULL, totface, ldata->layers[i].name);
			}
		}
		CustomData_bmesh_update_active_layers(fdata, pdata, ldata);
	}

	BLI_assert(CustomData_from_bmeshpoly_test(fdata, pdata, ldata, true));

	loopindex = MEM_mallocN(sizeof(*loopindex) * totface, __func__);

	for (mf_idx = 0, mf = mface; mf_idx < totface; mf_idx++, mf++) {
		const int mf_len = mf->v4 ? 4 : 3;
		unsigned int *ml_idx = loopindex[mf_idx];
		int i, not_done;

		/* Find out loop indices. */
		/* NOTE: This assumes tessface are valid and in sync with loop/poly... Else, most likely, segfault! */
		for (i = mp[polyindex[mf_idx]].loopstart, not_done = mf_len; not_done; i++) {
			const int tf_v = BKE_MESH_TESSFACE_VINDEX_ORDER(mf, ml[i].v);
			if (tf_v != -1) {
				ml_idx[tf_v] = i;
				not_done--;
			}
		}
	}

	/* NOTE: quad detection issue - fourth vertidx vs fourth loopidx:
	 * Here, our tfaces' fourth vertex index is never 0 for a quad. However, we know our fourth loop index may be
	 * 0 for quads (because our quads may have been rotated compared to their org poly, see tessellation code).
	 * So we pass the MFace's, and BKE_mesh_loops_to_tessdata will use MFace->v4 index as quad test.
	 */
	BKE_mesh_tangent_loops_to_tessdata(fdata, ldata, mface, polyindex, loopindex, totface);

	MEM_freeN(loopindex);

	if (G.debug & G_DEBUG)
		printf("%s: Updated tessellated tangents of dm %p\n", __func__, dm);
}


void DM_update_materials(DerivedMesh *dm, Object *ob)
{
	int i, totmat = ob->totcol + 1; /* materials start from 1, default material is 0 */
	dm->totmat = totmat;

	/* invalidate old materials */
	if (dm->mat)
		MEM_freeN(dm->mat);

	dm->mat = MEM_callocN(totmat * sizeof(*dm->mat), "DerivedMesh.mat");

	/* we leave last material as empty - rationale here is being able to index
	 * the materials by using the mf->mat_nr directly and leaving the last
	 * material as NULL in case no materials exist on mesh, so indexing will not fail */
	for (i = 0; i < totmat - 1; i++) {
		dm->mat[i] = give_current_material(ob, i + 1);
	}
}

MLoopUV *DM_paint_uvlayer_active_get(DerivedMesh *dm, int mat_nr)
{
	MLoopUV *uv_base;

	BLI_assert(mat_nr < dm->totmat);

	if (dm->mat[mat_nr] && dm->mat[mat_nr]->texpaintslot &&
	    dm->mat[mat_nr]->texpaintslot[dm->mat[mat_nr]->paint_active_slot].uvname)
	{
		uv_base = CustomData_get_layer_named(&dm->loopData, CD_MLOOPUV,
		                                     dm->mat[mat_nr]->texpaintslot[dm->mat[mat_nr]->paint_active_slot].uvname);
		/* This can fail if we have changed the name in the UV layer list and have assigned the old name in the material
		 * texture slot.*/
		if (!uv_base)
			uv_base = CustomData_get_layer(&dm->loopData, CD_MLOOPUV);
	}
	else {
		uv_base = CustomData_get_layer(&dm->loopData, CD_MLOOPUV);
	}

	return uv_base;
}

void DM_to_mesh(DerivedMesh *dm, Mesh *me, Object *ob, CustomDataMask mask, bool take_ownership)
{
	/* dm might depend on me, so we need to do everything with a local copy */
	Mesh tmp = *me;
	int totvert, totedge /*, totface */ /* UNUSED */, totloop, totpoly;
	int did_shapekeys = 0;
	int alloctype = CD_DUPLICATE;

	if (take_ownership && dm->type == DM_TYPE_CDDM && dm->needsFree) {
		bool has_any_referenced_layers =
		        CustomData_has_referenced(&dm->vertData) ||
		        CustomData_has_referenced(&dm->edgeData) ||
		        CustomData_has_referenced(&dm->loopData) ||
		        CustomData_has_referenced(&dm->faceData) ||
		        CustomData_has_referenced(&dm->polyData);
		if (!has_any_referenced_layers) {
			alloctype = CD_ASSIGN;
		}
	}

	CustomData_reset(&tmp.vdata);
	CustomData_reset(&tmp.edata);
	CustomData_reset(&tmp.fdata);
	CustomData_reset(&tmp.ldata);
	CustomData_reset(&tmp.pdata);

	DM_ensure_normals(dm);

	totvert = tmp.totvert = dm->getNumVerts(dm);
	totedge = tmp.totedge = dm->getNumEdges(dm);
	totloop = tmp.totloop = dm->getNumLoops(dm);
	totpoly = tmp.totpoly = dm->getNumPolys(dm);
	tmp.totface = 0;

	CustomData_copy(&dm->vertData, &tmp.vdata, mask, alloctype, totvert);
	CustomData_copy(&dm->edgeData, &tmp.edata, mask, alloctype, totedge);
	CustomData_copy(&dm->loopData, &tmp.ldata, mask, alloctype, totloop);
	CustomData_copy(&dm->polyData, &tmp.pdata, mask, alloctype, totpoly);
	tmp.cd_flag = dm->cd_flag;

	if (CustomData_has_layer(&dm->vertData, CD_SHAPEKEY)) {
		KeyBlock *kb;
		int uid;
		
		if (ob) {
			kb = BLI_findlink(&me->key->block, ob->shapenr - 1);
			if (kb) {
				uid = kb->uid;
			}
			else {
				printf("%s: error - could not find active shapekey %d!\n",
				       __func__, ob->shapenr - 1);

				uid = INT_MAX;
			}
		}
		else {
			/* if no object, set to INT_MAX so we don't mess up any shapekey layers */
			uid = INT_MAX;
		}

		shapekey_layers_to_keyblocks(dm, me, uid);
		did_shapekeys = 1;
	}

	/* copy texture space */
	if (ob) {
		BKE_mesh_texspace_copy_from_object(&tmp, ob);
	}
	
	/* not all DerivedMeshes store their verts/edges/faces in CustomData, so
	 * we set them here in case they are missing */
	if (!CustomData_has_layer(&tmp.vdata, CD_MVERT)) {
		CustomData_add_layer(&tmp.vdata, CD_MVERT, CD_ASSIGN,
		                     (alloctype == CD_ASSIGN) ? dm->getVertArray(dm) : dm->dupVertArray(dm),
		                     totvert);
	}
	if (!CustomData_has_layer(&tmp.edata, CD_MEDGE)) {
		CustomData_add_layer(&tmp.edata, CD_MEDGE, CD_ASSIGN,
		                     (alloctype == CD_ASSIGN) ? dm->getEdgeArray(dm) : dm->dupEdgeArray(dm),
		                     totedge);
	}
	if (!CustomData_has_layer(&tmp.pdata, CD_MPOLY)) {
		tmp.mloop = (alloctype == CD_ASSIGN) ? dm->getLoopArray(dm) : dm->dupLoopArray(dm);
		tmp.mpoly = (alloctype == CD_ASSIGN) ? dm->getPolyArray(dm) : dm->dupPolyArray(dm);

		CustomData_add_layer(&tmp.ldata, CD_MLOOP, CD_ASSIGN, tmp.mloop, tmp.totloop);
		CustomData_add_layer(&tmp.pdata, CD_MPOLY, CD_ASSIGN, tmp.mpoly, tmp.totpoly);
	}

	/* object had got displacement layer, should copy this layer to save sculpted data */
	/* NOTE: maybe some other layers should be copied? nazgul */
	if (CustomData_has_layer(&me->ldata, CD_MDISPS)) {
		if (totloop == me->totloop) {
			MDisps *mdisps = CustomData_get_layer(&me->ldata, CD_MDISPS);
			CustomData_add_layer(&tmp.ldata, CD_MDISPS, alloctype, mdisps, totloop);
		}
	}

	/* yes, must be before _and_ after tessellate */
	BKE_mesh_update_customdata_pointers(&tmp, false);

	/* since 2.65 caller must do! */
	// BKE_mesh_tessface_calc(&tmp);

	CustomData_free(&me->vdata, me->totvert);
	CustomData_free(&me->edata, me->totedge);
	CustomData_free(&me->fdata, me->totface);
	CustomData_free(&me->ldata, me->totloop);
	CustomData_free(&me->pdata, me->totpoly);

	/* ok, this should now use new CD shapekey data,
	 * which should be fed through the modifier
	 * stack */
	if (tmp.totvert != me->totvert && !did_shapekeys && me->key) {
		printf("%s: YEEK! this should be recoded! Shape key loss!: ID '%s'\n", __func__, tmp.id.name);
		if (tmp.key)
			id_us_min(&tmp.key->id);
		tmp.key = NULL;
	}

	/* Clear selection history */
	MEM_SAFE_FREE(tmp.mselect);
	tmp.totselect = 0;
	BLI_assert(ELEM(tmp.bb, NULL, me->bb));
	if (me->bb) {
		MEM_freeN(me->bb);
		tmp.bb = NULL;
	}

	/* skip the listbase */
	MEMCPY_STRUCT_OFS(me, &tmp, id.prev);

	if (take_ownership) {
		if (alloctype == CD_ASSIGN) {
			CustomData_free_typemask(&dm->vertData, dm->numVertData, ~mask);
			CustomData_free_typemask(&dm->edgeData, dm->numEdgeData, ~mask);
			CustomData_free_typemask(&dm->loopData, dm->numLoopData, ~mask);
			CustomData_free_typemask(&dm->polyData, dm->numPolyData, ~mask);
		}
		dm->release(dm);
	}
}

void DM_to_meshkey(DerivedMesh *dm, Mesh *me, KeyBlock *kb)
{
	int a, totvert = dm->getNumVerts(dm);
	float *fp;
	MVert *mvert;
	
	if (totvert == 0 || me->totvert == 0 || me->totvert != totvert) {
		return;
	}
	
	if (kb->data) MEM_freeN(kb->data);
	kb->data = MEM_mallocN(me->key->elemsize * me->totvert, "kb->data");
	kb->totelem = totvert;
	
	fp = kb->data;
	mvert = dm->getVertDataArray(dm, CD_MVERT);
	
	for (a = 0; a < kb->totelem; a++, fp += 3, mvert++) {
		copy_v3_v3(fp, mvert->co);
	}
}

/**
 * set the CD_FLAG_NOCOPY flag in custom data layers where the mask is
 * zero for the layer type, so only layer types specified by the mask
 * will be copied
 */
void DM_set_only_copy(DerivedMesh *dm, CustomDataMask mask)
{
	CustomData_set_only_copy(&dm->vertData, mask);
	CustomData_set_only_copy(&dm->edgeData, mask);
	CustomData_set_only_copy(&dm->faceData, mask);
	/* this wasn't in 2.63 and is disabled for 2.64 because it gives problems with
	 * weight paint mode when there are modifiers applied, needs further investigation,
	 * see replies to r50969, Campbell */
#if 0
	CustomData_set_only_copy(&dm->loopData, mask);
	CustomData_set_only_copy(&dm->polyData, mask);
#endif
}

void DM_add_vert_layer(DerivedMesh *dm, int type, int alloctype, void *layer)
{
	CustomData_add_layer(&dm->vertData, type, alloctype, layer, dm->numVertData);
}

void DM_add_edge_layer(DerivedMesh *dm, int type, int alloctype, void *layer)
{
	CustomData_add_layer(&dm->edgeData, type, alloctype, layer, dm->numEdgeData);
}

void DM_add_tessface_layer(DerivedMesh *dm, int type, int alloctype, void *layer)
{
	CustomData_add_layer(&dm->faceData, type, alloctype, layer, dm->numTessFaceData);
}

void DM_add_loop_layer(DerivedMesh *dm, int type, int alloctype, void *layer)
{
	CustomData_add_layer(&dm->loopData, type, alloctype, layer, dm->numLoopData);
}

void DM_add_poly_layer(DerivedMesh *dm, int type, int alloctype, void *layer)
{
	CustomData_add_layer(&dm->polyData, type, alloctype, layer, dm->numPolyData);
}

void *DM_get_vert_data(DerivedMesh *dm, int index, int type)
{
	BLI_assert(index >= 0 && index < dm->getNumVerts(dm));
	return CustomData_get(&dm->vertData, index, type);
}

void *DM_get_edge_data(DerivedMesh *dm, int index, int type)
{
	BLI_assert(index >= 0 && index < dm->getNumEdges(dm));
	return CustomData_get(&dm->edgeData, index, type);
}

void *DM_get_tessface_data(DerivedMesh *dm, int index, int type)
{
	BLI_assert(index >= 0 && index < dm->getNumTessFaces(dm));
	return CustomData_get(&dm->faceData, index, type);
}

void *DM_get_poly_data(DerivedMesh *dm, int index, int type)
{
	BLI_assert(index >= 0 && index < dm->getNumPolys(dm));
	return CustomData_get(&dm->polyData, index, type);
}


void *DM_get_vert_data_layer(DerivedMesh *dm, int type)
{
	if (type == CD_MVERT)
		return dm->getVertArray(dm);

	return CustomData_get_layer(&dm->vertData, type);
}

void *DM_get_edge_data_layer(DerivedMesh *dm, int type)
{
	if (type == CD_MEDGE)
		return dm->getEdgeArray(dm);

	return CustomData_get_layer(&dm->edgeData, type);
}

void *DM_get_tessface_data_layer(DerivedMesh *dm, int type)
{
	if (type == CD_MFACE)
		return dm->getTessFaceArray(dm);

	return CustomData_get_layer(&dm->faceData, type);
}

void *DM_get_poly_data_layer(DerivedMesh *dm, int type)
{
	return CustomData_get_layer(&dm->polyData, type);
}

void *DM_get_loop_data_layer(DerivedMesh *dm, int type)
{
	return CustomData_get_layer(&dm->loopData, type);
}

void DM_set_vert_data(DerivedMesh *dm, int index, int type, void *data)
{
	CustomData_set(&dm->vertData, index, type, data);
}

void DM_set_edge_data(DerivedMesh *dm, int index, int type, void *data)
{
	CustomData_set(&dm->edgeData, index, type, data);
}

void DM_set_tessface_data(DerivedMesh *dm, int index, int type, void *data)
{
	CustomData_set(&dm->faceData, index, type, data);
}

void DM_copy_vert_data(DerivedMesh *source, DerivedMesh *dest,
                       int source_index, int dest_index, int count)
{
	CustomData_copy_data(&source->vertData, &dest->vertData,
	                     source_index, dest_index, count);
}

void DM_copy_edge_data(DerivedMesh *source, DerivedMesh *dest,
                       int source_index, int dest_index, int count)
{
	CustomData_copy_data(&source->edgeData, &dest->edgeData,
	                     source_index, dest_index, count);
}

void DM_copy_tessface_data(DerivedMesh *source, DerivedMesh *dest,
                           int source_index, int dest_index, int count)
{
	CustomData_copy_data(&source->faceData, &dest->faceData,
	                     source_index, dest_index, count);
}

void DM_copy_loop_data(DerivedMesh *source, DerivedMesh *dest,
                       int source_index, int dest_index, int count)
{
	CustomData_copy_data(&source->loopData, &dest->loopData,
	                     source_index, dest_index, count);
}

void DM_copy_poly_data(DerivedMesh *source, DerivedMesh *dest,
                       int source_index, int dest_index, int count)
{
	CustomData_copy_data(&source->polyData, &dest->polyData,
	                     source_index, dest_index, count);
}

void DM_free_vert_data(struct DerivedMesh *dm, int index, int count)
{
	CustomData_free_elem(&dm->vertData, index, count);
}

void DM_free_edge_data(struct DerivedMesh *dm, int index, int count)
{
	CustomData_free_elem(&dm->edgeData, index, count);
}

void DM_free_tessface_data(struct DerivedMesh *dm, int index, int count)
{
	CustomData_free_elem(&dm->faceData, index, count);
}

void DM_free_loop_data(struct DerivedMesh *dm, int index, int count)
{
	CustomData_free_elem(&dm->loopData, index, count);
}

void DM_free_poly_data(struct DerivedMesh *dm, int index, int count)
{
	CustomData_free_elem(&dm->polyData, index, count);
}

/**
 * interpolates vertex data from the vertices indexed by src_indices in the
 * source mesh using the given weights and stores the result in the vertex
 * indexed by dest_index in the dest mesh
 */
void DM_interp_vert_data(
        DerivedMesh *source, DerivedMesh *dest,
        int *src_indices, float *weights,
        int count, int dest_index)
{
	CustomData_interp(&source->vertData, &dest->vertData, src_indices,
	                  weights, NULL, count, dest_index);
}

/**
 * interpolates edge data from the edges indexed by src_indices in the
 * source mesh using the given weights and stores the result in the edge indexed
 * by dest_index in the dest mesh.
 * if weights is NULL, all weights default to 1.
 * if vert_weights is non-NULL, any per-vertex edge data is interpolated using
 * vert_weights[i] multiplied by weights[i].
 */
void DM_interp_edge_data(
        DerivedMesh *source, DerivedMesh *dest,
        int *src_indices,
        float *weights, EdgeVertWeight *vert_weights,
        int count, int dest_index)
{
	CustomData_interp(&source->edgeData, &dest->edgeData, src_indices,
	                  weights, (float *)vert_weights, count, dest_index);
}

/**
 * interpolates face data from the faces indexed by src_indices in the
 * source mesh using the given weights and stores the result in the face indexed
 * by dest_index in the dest mesh.
 * if weights is NULL, all weights default to 1.
 * if vert_weights is non-NULL, any per-vertex face data is interpolated using
 * vert_weights[i] multiplied by weights[i].
 */
void DM_interp_tessface_data(
        DerivedMesh *source, DerivedMesh *dest,
        int *src_indices,
        float *weights, FaceVertWeight *vert_weights,
        int count, int dest_index)
{
	CustomData_interp(&source->faceData, &dest->faceData, src_indices,
	                  weights, (float *)vert_weights, count, dest_index);
}

void DM_swap_tessface_data(DerivedMesh *dm, int index, const int *corner_indices)
{
	CustomData_swap_corners(&dm->faceData, index, corner_indices);
}

void DM_interp_loop_data(
        DerivedMesh *source, DerivedMesh *dest,
        int *src_indices,
        float *weights, int count, int dest_index)
{
	CustomData_interp(&source->loopData, &dest->loopData, src_indices,
	                  weights, NULL, count, dest_index);
}

void DM_interp_poly_data(
        DerivedMesh *source, DerivedMesh *dest,
        int *src_indices,
        float *weights, int count, int dest_index)
{
	CustomData_interp(&source->polyData, &dest->polyData, src_indices,
	                  weights, NULL, count, dest_index);
}

static void shapekey_layers_to_keyblocks(DerivedMesh *dm, Mesh *me, int actshape_uid)
{
	KeyBlock *kb;
	int i, j, tot;
	
	if (!me->key)
		return;
	
	tot = CustomData_number_of_layers(&dm->vertData, CD_SHAPEKEY);
	for (i = 0; i < tot; i++) {
		CustomDataLayer *layer = &dm->vertData.layers[CustomData_get_layer_index_n(&dm->vertData, CD_SHAPEKEY, i)];
		float (*cos)[3], (*kbcos)[3];
		
		for (kb = me->key->block.first; kb; kb = kb->next) {
			if (kb->uid == layer->uid)
				break;
		}
		
		if (!kb) {
			kb = BKE_keyblock_add(me->key, layer->name);
			kb->uid = layer->uid;
		}
		
		if (kb->data)
			MEM_freeN(kb->data);
		
		cos = CustomData_get_layer_n(&dm->vertData, CD_SHAPEKEY, i);
		kb->totelem = dm->numVertData;

		kb->data = kbcos = MEM_mallocN(sizeof(float) * 3 * kb->totelem, "kbcos DerivedMesh.c");
		if (kb->uid == actshape_uid) {
			MVert *mvert = dm->getVertArray(dm);
			
			for (j = 0; j < dm->numVertData; j++, kbcos++, mvert++) {
				copy_v3_v3(*kbcos, mvert->co);
			}
		}
		else {
			for (j = 0; j < kb->totelem; j++, cos++, kbcos++) {
				copy_v3_v3(*kbcos, *cos);
			}
		}
	}
	
	for (kb = me->key->block.first; kb; kb = kb->next) {
		if (kb->totelem != dm->numVertData) {
			if (kb->data)
				MEM_freeN(kb->data);
			
			kb->totelem = dm->numVertData;
			kb->data = MEM_callocN(sizeof(float) * 3 * kb->totelem, "kb->data derivedmesh.c");
			fprintf(stderr, "%s: lost a shapekey layer: '%s'! (bmesh internal error)\n", __func__, kb->name);
		}
	}
}

/* same as above but for vert coords */
typedef struct {
	float (*vertexcos)[3];
	BLI_bitmap *vertex_visit;
} MappedUserData;

static void make_vertexcos__mapFunc(
        void *userData, int index, const float co[3],
        const float UNUSED(no_f[3]), const short UNUSED(no_s[3]))
{
	MappedUserData *mappedData = (MappedUserData *)userData;

	if (BLI_BITMAP_TEST(mappedData->vertex_visit, index) == 0) {
		/* we need coord from prototype vertex, not from copies,
		 * assume they stored in the beginning of vertex array stored in DM
		 * (mirror modifier for eg does this) */
		copy_v3_v3(mappedData->vertexcos[index], co);
		BLI_BITMAP_ENABLE(mappedData->vertex_visit, index);
	}
}

void mesh_get_mapped_verts_coords(DerivedMesh *dm, float (*r_cos)[3], const int totcos)
{
	if (dm->foreachMappedVert) {
		MappedUserData userData;
		memset(r_cos, 0, sizeof(*r_cos) * totcos);
		userData.vertexcos = r_cos;
		userData.vertex_visit = BLI_BITMAP_NEW(totcos, "vertexcos flags");
		dm->foreachMappedVert(dm, make_vertexcos__mapFunc, &userData, DM_FOREACH_NOP);
		MEM_freeN(userData.vertex_visit);
	}
	else {
		int i;
		for (i = 0; i < totcos; i++) {
			dm->getVertCo(dm, i, r_cos[i]);
		}
	}
}

/* ******************* GLSL ******************** */

/** \name Tangent Space Calculation
 * \{ */

/* Necessary complexity to handle looptri's as quads for correct tangents */
#define USE_LOOPTRI_DETECT_QUADS

typedef struct {
	float (*precomputedFaceNormals)[3];
	float (*precomputedLoopNormals)[3];
	const MLoopTri *looptri;
	MLoopUV *mloopuv;   /* texture coordinates */
	MPoly *mpoly;       /* indices */
	MLoop *mloop;       /* indices */
	MVert *mvert;       /* vertices & normals */
	float (*orco)[3];
	float (*tangent)[4];    /* destination */
	int numTessFaces;

#ifdef USE_LOOPTRI_DETECT_QUADS
	/* map from 'fake' face index to looptri,
	 * quads will point to the first looptri of the quad */
	const int    *face_as_quad_map;
	int       num_face_as_quad_map;
#endif

} SGLSLMeshToTangent;

/* interface */
#include "mikktspace.h"

static int dm_ts_GetNumFaces(const SMikkTSpaceContext *pContext)
{
	SGLSLMeshToTangent *pMesh = pContext->m_pUserData;

#ifdef USE_LOOPTRI_DETECT_QUADS
	return pMesh->num_face_as_quad_map;
#else
	return pMesh->numTessFaces;
#endif
}

static int dm_ts_GetNumVertsOfFace(const SMikkTSpaceContext *pContext, const int face_num)
{
#ifdef USE_LOOPTRI_DETECT_QUADS
	SGLSLMeshToTangent *pMesh = pContext->m_pUserData;
	if (pMesh->face_as_quad_map) {
		const MLoopTri *lt = &pMesh->looptri[pMesh->face_as_quad_map[face_num]];
		const MPoly *mp = &pMesh->mpoly[lt->poly];
		if (mp->totloop == 4) {
			return 4;
		}
	}
	return 3;
#else
	UNUSED_VARS(pContext, face_num);
	return 3;
#endif
}

static void dm_ts_GetPosition(
        const SMikkTSpaceContext *pContext, float r_co[3],
        const int face_num, const int vert_index)
{
	//assert(vert_index >= 0 && vert_index < 4);
	SGLSLMeshToTangent *pMesh = pContext->m_pUserData;
	const MLoopTri *lt;
	int loop_index;
	const float *co;

#ifdef USE_LOOPTRI_DETECT_QUADS
	if (pMesh->face_as_quad_map) {
		lt = &pMesh->looptri[pMesh->face_as_quad_map[face_num]];
		const MPoly *mp = &pMesh->mpoly[lt->poly];
		if (mp->totloop == 4) {
			loop_index = mp->loopstart + vert_index;
			goto finally;
		}
		/* fall through to regular triangle */
	}
	else {
		lt = &pMesh->looptri[face_num];
	}
#else
	lt = &pMesh->looptri[face_num];
#endif
	loop_index = lt->tri[vert_index];

finally:
	co = pMesh->mvert[pMesh->mloop[loop_index].v].co;
	copy_v3_v3(r_co, co);
}

static void dm_ts_GetTextureCoordinate(
        const SMikkTSpaceContext *pContext, float r_uv[2],
        const int face_num, const int vert_index)
{
	//assert(vert_index >= 0 && vert_index < 4);
	SGLSLMeshToTangent *pMesh = pContext->m_pUserData;
	const MLoopTri *lt;
	int loop_index;

#ifdef USE_LOOPTRI_DETECT_QUADS
	if (pMesh->face_as_quad_map) {
		lt = &pMesh->looptri[pMesh->face_as_quad_map[face_num]];
		const MPoly *mp = &pMesh->mpoly[lt->poly];
		if (mp->totloop == 4) {
			loop_index = mp->loopstart + vert_index;
			goto finally;
		}
		/* fall through to regular triangle */
	}
	else {
		lt = &pMesh->looptri[face_num];
	}
#else
	lt = &pMesh->looptri[face_num];
#endif
	loop_index = lt->tri[vert_index];

finally:
	if (pMesh->mloopuv != NULL) {
		const float *uv = pMesh->mloopuv[loop_index].uv;
		copy_v2_v2(r_uv, uv);
	}
	else {
		const float *orco = pMesh->orco[pMesh->mloop[loop_index].v];
		map_to_sphere(&r_uv[0], &r_uv[1], orco[0], orco[1], orco[2]);
	}
}

static void dm_ts_GetNormal(
        const SMikkTSpaceContext *pContext, float r_no[3],
        const int face_num, const int vert_index)
{
	//assert(vert_index >= 0 && vert_index < 4);
	SGLSLMeshToTangent *pMesh = (SGLSLMeshToTangent *) pContext->m_pUserData;
	const MLoopTri *lt;
	int loop_index;

#ifdef USE_LOOPTRI_DETECT_QUADS
	if (pMesh->face_as_quad_map) {
		lt = &pMesh->looptri[pMesh->face_as_quad_map[face_num]];
		const MPoly *mp = &pMesh->mpoly[lt->poly];
		if (mp->totloop == 4) {
			loop_index = mp->loopstart + vert_index;
			goto finally;
		}
		/* fall through to regular triangle */
	}
	else {
		lt = &pMesh->looptri[face_num];
	}
#else
	lt = &pMesh->looptri[face_num];
#endif
	loop_index = lt->tri[vert_index];

finally:
	if (pMesh->precomputedLoopNormals) {
		copy_v3_v3(r_no, pMesh->precomputedLoopNormals[loop_index]);
	}
	else if ((pMesh->mpoly[lt->poly].flag & ME_SMOOTH) == 0) {  /* flat */
		if (pMesh->precomputedFaceNormals) {
			copy_v3_v3(r_no, pMesh->precomputedFaceNormals[lt->poly]);
		}
		else {
#ifdef USE_LOOPTRI_DETECT_QUADS
			const MPoly *mp = &pMesh->mpoly[lt->poly];
			if (mp->totloop == 4) {
				normal_quad_v3(
				        r_no,
				        pMesh->mvert[pMesh->mloop[mp->loopstart + 0].v].co,
				        pMesh->mvert[pMesh->mloop[mp->loopstart + 1].v].co,
				        pMesh->mvert[pMesh->mloop[mp->loopstart + 2].v].co,
				        pMesh->mvert[pMesh->mloop[mp->loopstart + 3].v].co);
			}
			else
#endif
			{
				normal_tri_v3(
				        r_no,
				        pMesh->mvert[pMesh->mloop[lt->tri[0]].v].co,
				        pMesh->mvert[pMesh->mloop[lt->tri[1]].v].co,
				        pMesh->mvert[pMesh->mloop[lt->tri[2]].v].co);
			}
		}
	}
	else {
		const short *no = pMesh->mvert[pMesh->mloop[loop_index].v].no;
		normal_short_to_float_v3(r_no, no);
	}
}

static void dm_ts_SetTSpace(
        const SMikkTSpaceContext *pContext, const float fvTangent[3], const float fSign,
        const int face_num, const int vert_index)
{
	//assert(vert_index >= 0 && vert_index < 4);
	SGLSLMeshToTangent *pMesh = (SGLSLMeshToTangent *) pContext->m_pUserData;
	const MLoopTri *lt;
	int loop_index;

#ifdef USE_LOOPTRI_DETECT_QUADS
	if (pMesh->face_as_quad_map) {
		lt = &pMesh->looptri[pMesh->face_as_quad_map[face_num]];
		const MPoly *mp = &pMesh->mpoly[lt->poly];
		if (mp->totloop == 4) {
			loop_index = mp->loopstart + vert_index;
			goto finally;
		}
		/* fall through to regular triangle */
	}
	else {
		lt = &pMesh->looptri[face_num];
	}
#else
	lt = &pMesh->looptri[face_num];
#endif
	loop_index = lt->tri[vert_index];

	float *pRes;

finally:
	pRes = pMesh->tangent[loop_index];
	copy_v3_v3(pRes, fvTangent);
	pRes[3] = fSign;
}

void DM_calc_loop_tangents(DerivedMesh *dm)
{
	/* mesh vars */
	const MLoopTri *looptri;
	MVert *mvert;
	MLoopUV *mloopuv;
	MPoly *mpoly;
	MLoop *mloop;
	float (*orco)[3] = NULL, (*tangent)[4];
	int /* totvert, */ totface;
	float (*fnors)[3];
	float (*tlnors)[3];

	if (CustomData_get_layer_index(&dm->loopData, CD_TANGENT) != -1)
		return;

	fnors = dm->getPolyDataArray(dm, CD_NORMAL);
	/* Note, we assume we do have tessellated loop normals at this point (in case it is object-enabled),
	 * have to check this is valid...
	 */
	tlnors = dm->getLoopDataArray(dm, CD_NORMAL);

	/* check we have all the needed layers */
	/* totvert = dm->getNumVerts(dm); */ /* UNUSED */
	looptri = dm->getLoopTriArray(dm);
	totface = dm->getNumLoopTri(dm);

	mvert = dm->getVertArray(dm);
	mpoly = dm->getPolyArray(dm);
	mloop = dm->getLoopArray(dm);
	mloopuv = dm->getLoopDataArray(dm, CD_MLOOPUV);

	if (!mloopuv) {
		orco = dm->getVertDataArray(dm, CD_ORCO);
		if (!orco)
			return;
	}
	
	/* create tangent layer */
	DM_add_loop_layer(dm, CD_TANGENT, CD_CALLOC, NULL);
	tangent = DM_get_loop_data_layer(dm, CD_TANGENT);
	
#ifdef USE_LOOPTRI_DETECT_QUADS
	int num_face_as_quad_map;
	int *face_as_quad_map = NULL;

	/* map faces to quads */
	if (totface != dm->getNumPolys(dm)) {
		/* over alloc, since we dont know how many ngon or quads we have */

		/* map fake face index to looptri */
		face_as_quad_map = MEM_mallocN(sizeof(int) * totface, __func__);
		int i, j;
		for (i = 0, j = 0; j < totface; i++, j++) {
			face_as_quad_map[i] = j;
			/* step over all quads */
			if (mpoly[looptri[j].poly].totloop == 4) {
				j++;  /* skips the nest looptri */
			}
		}
		num_face_as_quad_map = i;
	}
	else {
		num_face_as_quad_map = totface;
	}
#endif

	/* new computation method */
	{
		SGLSLMeshToTangent mesh2tangent = {NULL};
		SMikkTSpaceContext sContext = {NULL};
		SMikkTSpaceInterface sInterface = {NULL};

		mesh2tangent.precomputedFaceNormals = fnors;
		mesh2tangent.precomputedLoopNormals = tlnors;
		mesh2tangent.looptri = looptri;
		mesh2tangent.mloopuv = mloopuv;
		mesh2tangent.mpoly = mpoly;
		mesh2tangent.mloop = mloop;
		mesh2tangent.mvert = mvert;
		mesh2tangent.orco = orco;
		mesh2tangent.tangent = tangent;
		mesh2tangent.numTessFaces = totface;

#ifdef USE_LOOPTRI_DETECT_QUADS
		mesh2tangent.face_as_quad_map = face_as_quad_map;
		mesh2tangent.num_face_as_quad_map = num_face_as_quad_map;
#endif

		sContext.m_pUserData = &mesh2tangent;
		sContext.m_pInterface = &sInterface;
		sInterface.m_getNumFaces = dm_ts_GetNumFaces;
		sInterface.m_getNumVerticesOfFace = dm_ts_GetNumVertsOfFace;
		sInterface.m_getPosition = dm_ts_GetPosition;
		sInterface.m_getTexCoord = dm_ts_GetTextureCoordinate;
		sInterface.m_getNormal = dm_ts_GetNormal;
		sInterface.m_setTSpaceBasic = dm_ts_SetTSpace;

		/* 0 if failed */
		genTangSpaceDefault(&sContext);

#ifdef USE_LOOPTRI_DETECT_QUADS
		if (face_as_quad_map) {
			MEM_freeN(face_as_quad_map);
		}
#undef USE_LOOPTRI_DETECT_QUADS
#endif
	}
}

/** \} */


void DM_calc_auto_bump_scale(DerivedMesh *dm)
{
	/* int totvert = dm->getNumVerts(dm); */ /* UNUSED */
	int totface = dm->getNumTessFaces(dm);

	MVert *mvert = dm->getVertArray(dm);
	MFace *mface = dm->getTessFaceArray(dm);
	MTFace *mtface = dm->getTessFaceDataArray(dm, CD_MTFACE);

	if (mtface) {
		double dsum = 0.0;
		int nr_accumulated = 0;
		int f;

		for (f = 0; f < totface; f++) {
			{
				float *verts[4], *tex_coords[4];
				const int nr_verts = mface[f].v4 != 0 ? 4 : 3;
				bool is_degenerate;
				int i;

				verts[0] = mvert[mface[f].v1].co; verts[1] = mvert[mface[f].v2].co; verts[2] = mvert[mface[f].v3].co;
				tex_coords[0] = mtface[f].uv[0]; tex_coords[1] = mtface[f].uv[1]; tex_coords[2] = mtface[f].uv[2];
				if (nr_verts == 4) {
					verts[3] = mvert[mface[f].v4].co;
					tex_coords[3] = mtface[f].uv[3];
				}

				/* discard degenerate faces */
				is_degenerate = 0;
				if (equals_v3v3(verts[0], verts[1]) ||
				    equals_v3v3(verts[0], verts[2]) ||
				    equals_v3v3(verts[1], verts[2]) ||
				    equals_v2v2(tex_coords[0], tex_coords[1]) ||
				    equals_v2v2(tex_coords[0], tex_coords[2]) ||
				    equals_v2v2(tex_coords[1], tex_coords[2]))
				{
					is_degenerate = 1;
				}

				/* verify last vertex as well if this is a quad */
				if (is_degenerate == 0 && nr_verts == 4) {
					if (equals_v3v3(verts[3], verts[0]) ||
					    equals_v3v3(verts[3], verts[1]) ||
					    equals_v3v3(verts[3], verts[2]) ||
					    equals_v2v2(tex_coords[3], tex_coords[0]) ||
					    equals_v2v2(tex_coords[3], tex_coords[1]) ||
					    equals_v2v2(tex_coords[3], tex_coords[2]))
					{
						is_degenerate = 1;
					}

					/* verify the winding is consistent */
					if (is_degenerate == 0) {
						float prev_edge[2];
						bool is_signed = 0;
						sub_v2_v2v2(prev_edge, tex_coords[0], tex_coords[3]);

						i = 0;
						while (is_degenerate == 0 && i < 4) {
							float cur_edge[2], signed_area;
							sub_v2_v2v2(cur_edge, tex_coords[(i + 1) & 0x3], tex_coords[i]);
							signed_area = cross_v2v2(prev_edge, cur_edge);

							if (i == 0) {
								is_signed = (signed_area < 0.0f) ? 1 : 0;
							}
							else if ((is_signed != 0) != (signed_area < 0.0f)) {
								is_degenerate = 1;
							}

							if (is_degenerate == 0) {
								copy_v2_v2(prev_edge, cur_edge);
								i++;
							}
						}
					}
				}

				/* proceed if not a degenerate face */
				if (is_degenerate == 0) {
					int nr_tris_to_pile = 0;
					/* quads split at shortest diagonal */
					int offs = 0;  /* initial triangulation is 0,1,2 and 0, 2, 3 */
					if (nr_verts == 4) {
						float pos_len_diag0, pos_len_diag1;

						pos_len_diag0 = len_squared_v3v3(verts[2], verts[0]);
						pos_len_diag1 = len_squared_v3v3(verts[3], verts[1]);

						if (pos_len_diag1 < pos_len_diag0) {
							offs = 1;     // alter split
						}
						else if (pos_len_diag0 == pos_len_diag1) { /* do UV check instead */
							float tex_len_diag0, tex_len_diag1;

							tex_len_diag0 = len_squared_v2v2(tex_coords[2], tex_coords[0]);
							tex_len_diag1 = len_squared_v2v2(tex_coords[3], tex_coords[1]);

							if (tex_len_diag1 < tex_len_diag0) {
								offs = 1; /* alter split */
							}
						}
					}
					nr_tris_to_pile = nr_verts - 2;
					if (nr_tris_to_pile == 1 || nr_tris_to_pile == 2) {
						const int indices[6] = {offs + 0, offs + 1, offs + 2, offs + 0, offs + 2, (offs + 3) & 0x3 };
						int t;
						for (t = 0; t < nr_tris_to_pile; t++) {
							float f2x_area_uv;
							const float *p0 = verts[indices[t * 3 + 0]];
							const float *p1 = verts[indices[t * 3 + 1]];
							const float *p2 = verts[indices[t * 3 + 2]];

							float edge_t0[2], edge_t1[2];
							sub_v2_v2v2(edge_t0, tex_coords[indices[t * 3 + 1]], tex_coords[indices[t * 3 + 0]]);
							sub_v2_v2v2(edge_t1, tex_coords[indices[t * 3 + 2]], tex_coords[indices[t * 3 + 0]]);

							f2x_area_uv = fabsf(cross_v2v2(edge_t0, edge_t1));
							if (f2x_area_uv > FLT_EPSILON) {
								float norm[3], v0[3], v1[3], f2x_surf_area, fsurf_ratio;
								sub_v3_v3v3(v0, p1, p0);
								sub_v3_v3v3(v1, p2, p0);
								cross_v3_v3v3(norm, v0, v1);

								f2x_surf_area = len_v3(norm);
								fsurf_ratio = f2x_surf_area / f2x_area_uv;  /* tri area divided by texture area */

								nr_accumulated++;
								dsum += (double)(fsurf_ratio);
							}
						}
					}
				}
			}
		}

		/* finalize */
		{
			const float avg_area_ratio = (nr_accumulated > 0) ? ((float)(dsum / nr_accumulated)) : 1.0f;
			const float use_as_render_bump_scale = sqrtf(avg_area_ratio);       // use width of average surface ratio as your bump scale
			dm->auto_bump_scale = use_as_render_bump_scale;
		}
	}
	else {
		dm->auto_bump_scale = 1.0f;
	}
}

void DM_vertex_attributes_from_gpu(DerivedMesh *dm, GPUVertexAttribs *gattribs, DMVertexAttribs *attribs)
{
	CustomData *vdata, *ldata;
	int a, b, layer;
	const bool is_editmesh = (dm->type == DM_TYPE_EDITBMESH);

	/* From the layers requested by the GLSL shader, figure out which ones are
	 * actually available for this derivedmesh, and retrieve the pointers */

	memset(attribs, 0, sizeof(DMVertexAttribs));

	vdata = &dm->vertData;
	ldata = dm->getLoopDataLayout(dm);
	
	/* calc auto bump scale if necessary */
	if (dm->auto_bump_scale <= 0.0f)
		DM_calc_auto_bump_scale(dm);

	/* add a tangent layer if necessary */
	for (b = 0; b < gattribs->totlayer; b++) {
		if (gattribs->layer[b].type == CD_TANGENT) {
			if (CustomData_get_layer_index(&dm->loopData, CD_TANGENT) == -1) {
				dm->calcLoopTangents(dm);
			}
			break;
		}
	}

	for (b = 0; b < gattribs->totlayer; b++) {
		if (gattribs->layer[b].type == CD_MTFACE) {
			/* uv coordinates */
			if (gattribs->layer[b].name[0])
				layer = CustomData_get_named_layer_index(ldata, CD_MLOOPUV, gattribs->layer[b].name);
			else
				layer = CustomData_get_active_layer_index(ldata, CD_MLOOPUV);

			a = attribs->tottface++;

			if (layer != -1) {
				attribs->tface[a].array = is_editmesh ? NULL : ldata->layers[layer].data;
				attribs->tface[a].em_offset = ldata->layers[layer].offset;
			}
			else {
				attribs->tface[a].array = NULL;
				attribs->tface[a].em_offset = -1;
			}

			attribs->tface[a].gl_index = gattribs->layer[b].glindex;
			attribs->tface[a].gl_texco = gattribs->layer[b].gltexco;
		}
		else if (gattribs->layer[b].type == CD_MCOL) {
			if (gattribs->layer[b].name[0])
				layer = CustomData_get_named_layer_index(ldata, CD_MLOOPCOL, gattribs->layer[b].name);
			else
				layer = CustomData_get_active_layer_index(ldata, CD_MLOOPCOL);

			a = attribs->totmcol++;

			if (layer != -1) {
				attribs->mcol[a].array = is_editmesh ? NULL : ldata->layers[layer].data;
				/* odd, store the offset for a different layer type here, but editmode draw code expects it */
				attribs->mcol[a].em_offset = ldata->layers[layer].offset;
			}
			else {
				attribs->mcol[a].array = NULL;
				attribs->mcol[a].em_offset = -1;
			}

			attribs->mcol[a].gl_index = gattribs->layer[b].glindex;
		}
		else if (gattribs->layer[b].type == CD_TANGENT) {
			/* note, even with 'is_editmesh' this uses the derived-meshes loop data */
			layer = CustomData_get_layer_index(&dm->loopData, CD_TANGENT);

			attribs->tottang = 1;

			if (layer != -1) {
				attribs->tang.array = dm->loopData.layers[layer].data;
				attribs->tang.em_offset = dm->loopData.layers[layer].offset;
			}
			else {
				attribs->tang.array = NULL;
				attribs->tang.em_offset = -1;
			}

			attribs->tang.gl_index = gattribs->layer[b].glindex;
		}
		else if (gattribs->layer[b].type == CD_ORCO) {
			/* original coordinates */
			layer = CustomData_get_layer_index(vdata, CD_ORCO);
			attribs->totorco = 1;

			if (layer != -1) {
				attribs->orco.array = vdata->layers[layer].data;
				attribs->orco.em_offset = vdata->layers[layer].offset;
			}
			else {
				attribs->orco.array = NULL;
				attribs->orco.em_offset = -1;
			}

			attribs->orco.gl_index = gattribs->layer[b].glindex;
			attribs->orco.gl_texco = gattribs->layer[b].gltexco;
		}
	}
}

/**
 * Set vertex shader attribute inputs for a particular tessface vert
 *
 * \param a: tessface index
 * \param index: vertex index
 * \param vert: corner index (0, 1, 2, 3)
 * \param loop: absolute loop corner index
 */
void DM_draw_attrib_vertex(DMVertexAttribs *attribs, int a, int index, int vert, int loop)
{
	const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	int b;

	UNUSED_VARS(a, vert);

	/* orco texture coordinates */
	if (attribs->totorco) {
		/*const*/ float (*array)[3] = attribs->orco.array;
		const float *orco = (array) ? array[index] : zero;

		if (attribs->orco.gl_texco)
			glTexCoord3fv(orco);
		else
			glVertexAttrib3fv(attribs->orco.gl_index, orco);
	}

	/* uv texture coordinates */
	for (b = 0; b < attribs->tottface; b++) {
		const float *uv;

		if (attribs->tface[b].array) {
			const MLoopUV *mloopuv = &attribs->tface[b].array[loop];
			uv = mloopuv->uv;
		}
		else {
			uv = zero;
		}

		if (attribs->tface[b].gl_texco)
			glTexCoord2fv(uv);
		else
			glVertexAttrib2fv(attribs->tface[b].gl_index, uv);
	}

	/* vertex colors */
	for (b = 0; b < attribs->totmcol; b++) {
		GLubyte col[4];

		if (attribs->mcol[b].array) {
			const MLoopCol *cp = &attribs->mcol[b].array[loop];
			copy_v4_v4_uchar(col, &cp->r);
		}
		else {
			col[0] = 0; col[1] = 0; col[2] = 0; col[3] = 0;
		}

		glVertexAttrib4ubv(attribs->mcol[b].gl_index, col);
	}

	/* tangent for normal mapping */
	if (attribs->tottang) {
		/*const*/ float (*array)[4] = attribs->tang.array;
		const float *tang = (array) ? array[loop] : zero;
		glVertexAttrib4fv(attribs->tang.gl_index, tang);
	}
}

/* Set object's bounding box based on DerivedMesh min/max data */
void DM_set_object_boundbox(Object *ob, DerivedMesh *dm)
{
	float min[3], max[3];

	INIT_MINMAX(min, max);
	dm->getMinMax(dm, min, max);

	if (!ob->bb)
		ob->bb = MEM_callocN(sizeof(BoundBox), "DM-BoundBox");

	BKE_boundbox_init_from_minmax(ob->bb, min, max);

	ob->bb->flag &= ~BOUNDBOX_DIRTY;
}

void DM_init_origspace(DerivedMesh *dm)
{
	const float default_osf[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

	OrigSpaceLoop *lof_array = CustomData_get_layer(&dm->loopData, CD_ORIGSPACE_MLOOP);
	const int numpoly = dm->getNumPolys(dm);
	// const int numloop = dm->getNumLoops(dm);
	MVert *mv = dm->getVertArray(dm);
	MLoop *ml = dm->getLoopArray(dm);
	MPoly *mp = dm->getPolyArray(dm);
	int i, j, k;

	float (*vcos_2d)[2] = NULL;
	BLI_array_staticdeclare(vcos_2d, 64);

	for (i = 0; i < numpoly; i++, mp++) {
		OrigSpaceLoop *lof = lof_array + mp->loopstart;

		if (mp->totloop == 3 || mp->totloop == 4) {
			for (j = 0; j < mp->totloop; j++, lof++) {
				copy_v2_v2(lof->uv, default_osf[j]);
			}
		}
		else {
			MLoop *l = &ml[mp->loopstart];
			float p_nor[3], co[3];
			float mat[3][3];

			float min[2] = {FLT_MAX, FLT_MAX}, max[2] = {FLT_MIN, FLT_MIN};
			float translate[2], scale[2];

			BKE_mesh_calc_poly_normal(mp, l, mv, p_nor);
			axis_dominant_v3_to_m3(mat, p_nor);

			BLI_array_empty(vcos_2d);
			BLI_array_reserve(vcos_2d, mp->totloop);
			for (j = 0; j < mp->totloop; j++, l++) {
				mul_v3_m3v3(co, mat, mv[l->v].co);
				copy_v2_v2(vcos_2d[j], co);

				for (k = 0; k < 2; k++) {
					if (co[k] > max[k])
						max[k] = co[k];
					else if (co[k] < min[k])
						min[k] = co[k];
				}
			}

			/* Brings min to (0, 0). */
			negate_v2_v2(translate, min);

			/* Scale will bring max to (1, 1). */
			sub_v2_v2v2(scale, max, min);
			if (scale[0] == 0.0f)
				scale[0] = 1e-9f;
			if (scale[1] == 0.0f)
				scale[1] = 1e-9f;
			invert_v2(scale);

			/* Finally, transform all vcos_2d into ((0, 0), (1, 1)) square and assing them as origspace. */
			for (j = 0; j < mp->totloop; j++, lof++) {
				add_v2_v2v2(lof->uv, vcos_2d[j], translate);
				mul_v2_v2(lof->uv, scale);
			}
		}
	}

	dm->dirty |= DM_DIRTY_TESS_CDLAYERS;
	BLI_array_free(vcos_2d);
}



/* derivedmesh info printing function,
 * to help track down differences DM output */

#ifndef NDEBUG
#include "BLI_dynstr.h"

static void dm_debug_info_layers(
        DynStr *dynstr, DerivedMesh *dm, CustomData *cd,
        void *(*getElemDataArray)(DerivedMesh *, int))
{
	int type;

	for (type = 0; type < CD_NUMTYPES; type++) {
		if (CustomData_has_layer(cd, type)) {
			/* note: doesnt account for multiple layers */
			const char *name = CustomData_layertype_name(type);
			const int size = CustomData_sizeof(type);
			const void *pt = getElemDataArray(dm, type);
			const int pt_size = pt ? (int)(MEM_allocN_len(pt) / size) : 0;
			const char *structname;
			int structnum;
			CustomData_file_write_info(type, &structname, &structnum);
			BLI_dynstr_appendf(dynstr,
			                   "        dict(name='%s', struct='%s', type=%d, ptr='%p', elem=%d, length=%d),\n",
			                   name, structname, type, (const void *)pt, size, pt_size);
		}
	}
}

char *DM_debug_info(DerivedMesh *dm)
{
	DynStr *dynstr = BLI_dynstr_new();
	char *ret;
	const char *tstr;

	BLI_dynstr_appendf(dynstr, "{\n");
	BLI_dynstr_appendf(dynstr, "    'ptr': '%p',\n", (void *)dm);
	switch (dm->type) {
		case DM_TYPE_CDDM:     tstr = "DM_TYPE_CDDM";     break;
		case DM_TYPE_EDITBMESH: tstr = "DM_TYPE_EDITMESH";  break;
		case DM_TYPE_CCGDM:    tstr = "DM_TYPE_CCGDM";     break;
		default:               tstr = "UNKNOWN";           break;
	}
	BLI_dynstr_appendf(dynstr, "    'type': '%s',\n", tstr);
	BLI_dynstr_appendf(dynstr, "    'numVertData': %d,\n", dm->numVertData);
	BLI_dynstr_appendf(dynstr, "    'numEdgeData': %d,\n", dm->numEdgeData);
	BLI_dynstr_appendf(dynstr, "    'numTessFaceData': %d,\n", dm->numTessFaceData);
	BLI_dynstr_appendf(dynstr, "    'numPolyData': %d,\n", dm->numPolyData);
	BLI_dynstr_appendf(dynstr, "    'deformedOnly': %d,\n", dm->deformedOnly);

	BLI_dynstr_appendf(dynstr, "    'vertexLayers': (\n");
	dm_debug_info_layers(dynstr, dm, &dm->vertData, dm->getVertDataArray);
	BLI_dynstr_appendf(dynstr, "    ),\n");

	BLI_dynstr_appendf(dynstr, "    'edgeLayers': (\n");
	dm_debug_info_layers(dynstr, dm, &dm->edgeData, dm->getEdgeDataArray);
	BLI_dynstr_appendf(dynstr, "    ),\n");

	BLI_dynstr_appendf(dynstr, "    'loopLayers': (\n");
	dm_debug_info_layers(dynstr, dm, &dm->loopData, dm->getLoopDataArray);
	BLI_dynstr_appendf(dynstr, "    ),\n");

	BLI_dynstr_appendf(dynstr, "    'polyLayers': (\n");
	dm_debug_info_layers(dynstr, dm, &dm->polyData, dm->getPolyDataArray);
	BLI_dynstr_appendf(dynstr, "    ),\n");

	BLI_dynstr_appendf(dynstr, "    'tessFaceLayers': (\n");
	dm_debug_info_layers(dynstr, dm, &dm->faceData, dm->getTessFaceDataArray);
	BLI_dynstr_appendf(dynstr, "    ),\n");

	BLI_dynstr_appendf(dynstr, "}\n");

	ret = BLI_dynstr_get_cstring(dynstr);
	BLI_dynstr_free(dynstr);
	return ret;
}

void DM_debug_print(DerivedMesh *dm)
{
	char *str = DM_debug_info(dm);
	puts(str);
	fflush(stdout);
	MEM_freeN(str);
}

void DM_debug_print_cdlayers(CustomData *data)
{
	int i;
	const CustomDataLayer *layer;

	printf("{\n");

	for (i = 0, layer = data->layers; i < data->totlayer; i++, layer++) {

		const char *name = CustomData_layertype_name(layer->type);
		const int size = CustomData_sizeof(layer->type);
		const char *structname;
		int structnum;
		CustomData_file_write_info(layer->type, &structname, &structnum);
		printf("        dict(name='%s', struct='%s', type=%d, ptr='%p', elem=%d, length=%d),\n",
		       name, structname, layer->type, (const void *)layer->data, size, (int)(MEM_allocN_len(layer->data) / size));
	}

	printf("}\n");
}

bool DM_is_valid(DerivedMesh *dm)
{
	const bool do_verbose = true;
	const bool do_fixes = false;

	bool is_valid = true;
	bool changed = true;

	is_valid &= BKE_mesh_validate_all_customdata(
	        dm->getVertDataLayout(dm),
	        dm->getEdgeDataLayout(dm),
	        dm->getLoopDataLayout(dm),
	        dm->getPolyDataLayout(dm),
	        false,  /* setting mask here isn't useful, gives false positives */
	        do_verbose, do_fixes, &changed);

	is_valid &= BKE_mesh_validate_arrays(
	        NULL,
	        dm->getVertArray(dm), dm->getNumVerts(dm),
	        dm->getEdgeArray(dm), dm->getNumEdges(dm),
	        dm->getTessFaceArray(dm), dm->getNumTessFaces(dm),
	        dm->getLoopArray(dm), dm->getNumLoops(dm),
	        dm->getPolyArray(dm), dm->getNumPolys(dm),
	        dm->getVertDataArray(dm, CD_MDEFORMVERT),
	        do_verbose, do_fixes, &changed);

	BLI_assert(changed == false);

	return is_valid;
}

#endif /* NDEBUG */

/* -------------------------------------------------------------------- */

MVert *DM_get_vert_array(DerivedMesh *dm, bool *allocated)
{
	CustomData *vert_data = dm->getVertDataLayout(dm);
	MVert *mvert = CustomData_get_layer(vert_data, CD_MVERT);
	*allocated = false;

	if (mvert == NULL) {
		mvert = MEM_mallocN(sizeof(MVert) * dm->getNumVerts(dm), "dmvh vert data array");
		dm->copyVertArray(dm, mvert);
		*allocated = true;
	}

	return mvert;
}

MEdge *DM_get_edge_array(DerivedMesh *dm, bool *allocated)
{
	CustomData *edge_data = dm->getEdgeDataLayout(dm);
	MEdge *medge = CustomData_get_layer(edge_data, CD_MEDGE);
	*allocated = false;

	if (medge == NULL) {
		medge = MEM_mallocN(sizeof(MEdge) * dm->getNumEdges(dm), "dm medge data array");
		dm->copyEdgeArray(dm, medge);
		*allocated = true;
	}

	return medge;
}

MLoop *DM_get_loop_array(DerivedMesh *dm, bool *r_allocated)
{
	CustomData *loop_data = dm->getLoopDataLayout(dm);
	MLoop *mloop = CustomData_get_layer(loop_data, CD_MLOOP);
	*r_allocated = false;

	if (mloop == NULL) {
		mloop = MEM_mallocN(sizeof(MLoop) * dm->getNumLoops(dm), "dm loop data array");
		dm->copyLoopArray(dm, mloop);
		*r_allocated = true;
	}

	return mloop;
}

MPoly *DM_get_poly_array(DerivedMesh *dm, bool *r_allocated)
{
	CustomData *poly_data = dm->getPolyDataLayout(dm);
	MPoly *mpoly = CustomData_get_layer(poly_data, CD_MPOLY);
	*r_allocated = false;

	if (mpoly == NULL) {
		mpoly = MEM_mallocN(sizeof(MPoly) * dm->getNumPolys(dm), "dm poly data array");
		dm->copyPolyArray(dm, mpoly);
		*r_allocated = true;
	}

	return mpoly;
}

MFace *DM_get_tessface_array(DerivedMesh *dm, bool *r_allocated)
{
	CustomData *tessface_data = dm->getTessFaceDataLayout(dm);
	MFace *mface = CustomData_get_layer(tessface_data, CD_MFACE);
	*r_allocated = false;

	if (mface == NULL) {
		int numTessFaces = dm->getNumTessFaces(dm);

		if (numTessFaces > 0) {
			mface = MEM_mallocN(sizeof(MFace) * numTessFaces, "bvh mface data array");
			dm->copyTessFaceArray(dm, mface);
			*r_allocated = true;
		}
	}

	return mface;
}

const MLoopTri *DM_get_looptri_array(
        DerivedMesh *dm,
        const MVert *mvert,
        const MPoly *mpoly, int mpoly_len,
        const MLoop *mloop, int mloop_len,
        bool *r_allocated)
{
	const MLoopTri *looptri = dm->getLoopTriArray(dm);
	*r_allocated = false;

	if (looptri == NULL) {
		if (mpoly_len > 0) {
			const int looptris_num = poly_to_tri_count(mpoly_len, mloop_len);
			MLoopTri *looptri_data;

			looptri_data = MEM_mallocN(sizeof(MLoopTri) * looptris_num, __func__);

			BKE_mesh_recalc_looptri(
			        mloop, mpoly,
			        mvert,
			        mloop_len, mpoly_len,
			        looptri_data);

			looptri = looptri_data;

			*r_allocated = true;
		}
	}

	return looptri;
}
