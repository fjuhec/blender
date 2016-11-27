#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BLI_alloca.h"
#include "BLI_math.h"
#include "BLI_math_geom.h"
#include "BLI_task.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_editmesh.h"
#include "BKE_library_query.h"
#include "BKE_modifier.h"

#include "depsgraph_private.h"

#include "MEM_guardedalloc.h"

#include "MOD_util.h"

typedef struct SDefBindCalcData {
	BVHTreeFromMesh *treeData;
	MPoly *mpoly;
	MLoop *mloop;
	MLoopTri *looptri;
	MVert *mvert;
	SDefVert *verts;
	float (*vertexCos)[3];
} SDefBindCalcData;

static void initData(ModifierData *md)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;
	smd->target	= NULL;
	smd->verts	= NULL;
	smd->flags  = 0;
}

static void freeData(ModifierData *md)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;

	if (smd->verts) {
		int i;
		for (i = 0; i < smd->numverts; i++) {
			if (smd->verts[i].vert_inds)
				MEM_freeN(smd->verts[i].vert_inds);

			if (smd->verts[i].vert_weights)
				MEM_freeN(smd->verts[i].vert_weights);
		}

		MEM_freeN(smd->verts);
	}
}

static void copyData(ModifierData *md, ModifierData *target)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;
	SurfaceDeformModifierData *tsmd = (SurfaceDeformModifierData *) target;

	*tsmd = *smd;

	if (smd->verts) {
		int i;

		tsmd->verts = MEM_dupallocN(smd->verts);

		for (i = 0; i < smd->numverts; i++) {
			if (smd->verts[i].vert_inds)
				tsmd->verts[i].vert_inds = MEM_dupallocN(smd->verts[i].vert_inds);

			if (smd->verts[i].vert_weights)
				tsmd->verts[i].vert_weights = MEM_dupallocN(smd->verts[i].vert_weights);
		}
	}
}

static void foreachObjectLink(ModifierData *md, Object *ob, ObjectWalkFunc walk, void *userData)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;

	walk(userData, ob, &smd->target, IDWALK_NOP);
}

static void updateDepgraph(ModifierData *md, DagForest *forest,
                           struct Main *UNUSED(bmain),
                           struct Scene *UNUSED(scene),
                           Object *UNUSED(ob),
                           DagNode *obNode)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;

	if (smd->target) {
		DagNode *curNode = dag_get_node(forest, smd->target);

		dag_add_relation(forest, curNode, obNode,
		                 DAG_RL_DATA_DATA | DAG_RL_OB_DATA | DAG_RL_DATA_OB | DAG_RL_OB_OB,
		                 "Surface Deform Modifier");
	}
}

static void updateDepsgraph(ModifierData *md,
                            struct Main *UNUSED(bmain),
                            struct Scene *UNUSED(scene),
                            Object *UNUSED(ob),
                            struct DepsNodeHandle *node)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *)md;
	if (smd->target != NULL) {
		DEG_add_object_relation(node, smd->target, DEG_OB_COMP_GEOMETRY, "Surface Deform Modifier");
	}
}

static void sortPolyVertsClosestEdge(int indices[], const int ind_map[], const float point[3],
                                     const float verts[][3], const int num) {
	int ind_prev, ind_curr, ind_target;
	float dist, dist_min;

	dist_min = FLT_MAX;

	ind_prev = num -1;
	ind_target = 0;

	/* Fill in vertex indices starting with the closest edge indices */
	for (ind_curr = 0; ind_curr < num; ind_curr++) {
		dist = dist_squared_to_line_segment_v3(point, verts[ind_prev], verts[ind_curr]);

		if (dist < dist_min) {
			dist_min = dist;
			indices[0] = ind_map[ind_prev];
			indices[1] = ind_map[ind_curr];
			ind_target = 1;
		}
		else if (ind_target > 1 && ind_target < num) {
			indices[ind_target] = ind_map[ind_curr];
		}

		ind_prev = ind_curr;
		ind_target++;
	}

	/* Fill in remaining vertex indices that occur before the closest edge indices */
	for (ind_curr = 0; ind_target < num; ind_target++, ind_curr++) {
		indices[ind_target] = ind_map[ind_curr];
	}
}

static void meanValueCoordinates(float w[], const float point[3], const float verts[][3], const int num) {
	float vec_curr[3], vec_prev[3], vec_tmp[3];
	float tan_prev, tan_curr, mag_curr, mag_next;
	float tot_w = 0;
	int i, ind_curr;

	sub_v3_v3v3(vec_tmp, verts[num - 2], point);
	sub_v3_v3v3(vec_prev, verts[num - 1], point);

	mag_curr = normalize_v3(vec_prev);
	normalize_v3(vec_tmp);

	tan_prev = tan(acos(dot_v3v3(vec_prev, vec_tmp)) / 2.0f);

	for (i = 0; i < num; i++) {
		sub_v3_v3v3(vec_curr, verts[i], point);
		mag_next = normalize_v3(vec_curr);

		tan_curr = tan(acos(dot_v3v3(vec_curr, vec_prev)) / 2.0f);

		ind_curr = (i == 0) ? (num - 1) : (i - 1);

		if (mag_curr > FLT_EPSILON) {
			w[ind_curr] = (tan_prev + tan_curr) / mag_curr;
			tot_w += w[ind_curr];
		}
		else {
			for (i = 0; i < num; i++) {
				w[i] = 0.0f;
			}
			w[ind_curr] = 1.0f;
			return;
		}

		mag_curr = mag_next;
		tan_prev = tan_curr;
		copy_v3_v3(vec_prev, vec_curr);
	}

	for (i = 0; i < num; i++) {
		w[i] /= tot_w;
	}
}

static void bindVert(void *userdata, void *UNUSED(userdata_chunk), const int index, const int UNUSED(threadid)) {
	SDefBindCalcData *data = (SDefBindCalcData *) userdata;
	BVHTreeNearest nearest = {.dist_sq = FLT_MAX, .index = -1};
	SDefVert *sdvert = data->verts + index;
	MLoopTri *looptri = data->looptri;
	MPoly *mpoly = data->mpoly;
	float vert_co[3];
	float vert_co_proj[3];
	float (*coords)[3];
	float v1[3], v2[3], v3[3];
	float norm[3], cent[3], plane[4];
	int *ind_map;
	int i;

	copy_v3_v3(vert_co, *(data->vertexCos + index));

	/* Assign nearest looptri and poly */
	BLI_bvhtree_find_nearest(data->treeData->tree, vert_co, &nearest, data->treeData->nearest_callback, data->treeData);
	looptri += nearest.index;
	mpoly += looptri->poly;

	/* Allocate and assign target poly vertex coordinates and indices */
	coords = BLI_array_alloca(coords, mpoly->totloop);
	ind_map = BLI_array_alloca(ind_map, mpoly->totloop);

	MLoop *loop = &data->mloop[mpoly->loopstart];

	for (i = 0; i < mpoly->totloop; i++, loop++) {
		copy_v3_v3(coords[i], data->mvert[loop->v].co);
		ind_map[i] = loop->v;
	}

	/* Assign target looptri vertex coordiantes */
	copy_v3_v3(v1, data->mvert[data->mloop[looptri->tri[0]].v].co);
	copy_v3_v3(v2, data->mvert[data->mloop[looptri->tri[1]].v].co);
	copy_v3_v3(v3, data->mvert[data->mloop[looptri->tri[2]].v].co);

	/* ---------- looptri mode ---------- */
	if (!is_poly_convex_v3(coords, mpoly->totloop)) {
		sdvert->mode = MOD_SDEF_MODE_LOOPTRI;
		sdvert->numverts = 3;
		sdvert->vert_weights = MEM_mallocN(sizeof(*sdvert->vert_weights) * 3, "SDefTriVertWeights");
		sdvert->vert_inds = MEM_mallocN(sizeof(*sdvert->vert_inds) * 3, "SDefTriVertInds");

		cent_tri_v3(cent, v1, v2, v3);
		normal_tri_v3(norm, v1, v2, v3);
		plane_from_point_normal_v3(plane, cent, norm);

		closest_to_plane_v3(vert_co_proj, plane, vert_co);

		interp_weights_face_v3(sdvert->vert_weights, v1, v2, v3, NULL, vert_co_proj);

		sdvert->vert_inds[0] = data->mloop[looptri->tri[0]].v;
		sdvert->vert_inds[1] = data->mloop[looptri->tri[1]].v;
		sdvert->vert_inds[2] = data->mloop[looptri->tri[2]].v;
	}
	else {
		/* Generic non-looptri mode operations */
		float v1_proj[3], v2_proj[3], v3_proj[3];
		float (*coords_proj)[3] = BLI_array_alloca(coords_proj, mpoly->totloop);

		sdvert->numverts = mpoly->totloop;

		cent_poly_v3(cent, coords, mpoly->totloop);
		normal_poly_v3(norm, coords, mpoly->totloop);
		plane_from_point_normal_v3(plane, cent, norm);

		closest_to_plane_v3(vert_co_proj, plane, vert_co);

		/* Project looptri vertex coords onto poly plane, to check if vert lies within poly.
		 * This has to be done because looptri normal can be different than poly normal. */
		closest_to_plane_v3(v1_proj, plane, v1);
		closest_to_plane_v3(v2_proj, plane, v2);
		closest_to_plane_v3(v3_proj, plane, v3);

		/* Project poly vertices onto poly plane, to provide planar coordinates for vertex weight computation */
		for (i = 0; i < mpoly->totloop; i++) {
			closest_to_plane_v3(coords_proj[i], plane, coords[i]);
		}

		/* ---------- ngon mode ---------- */
		if (isect_point_tri_prism_v3(vert_co_proj, v1_proj, v2_proj, v3_proj)) {
			sdvert->mode = MOD_SDEF_MODE_NGON;
			sdvert->vert_weights = MEM_mallocN(sizeof(*sdvert->vert_weights) * mpoly->totloop, "SDefNgonVertWeights");
			sdvert->vert_inds = MEM_mallocN(sizeof(*sdvert->vert_inds) * mpoly->totloop, "SDefNgonVertInds");

			meanValueCoordinates(sdvert->vert_weights, vert_co_proj, coords_proj, mpoly->totloop);

			/* Reproject vert based on weights and original poly verts, to reintroduce poly non-planarity */
			zero_v3(vert_co_proj);
			for (i = 0; i < mpoly->totloop; i++) {
				madd_v3_v3fl(vert_co_proj, coords[i], sdvert->vert_weights[i]);
				sdvert->vert_inds[i] = ind_map[i];
			}
		}
		/* ---------- centroid mode ---------- */
		else {
			sdvert->mode = MOD_SDEF_MODE_CENTROID;
			sdvert->vert_weights = MEM_mallocN(sizeof(*sdvert->vert_weights) * 3, "SDefCentVertWeights");
			sdvert->vert_inds = MEM_mallocN(sizeof(*sdvert->vert_inds) * mpoly->totloop, "SDefCentVertInds");

			sortPolyVertsClosestEdge(sdvert->vert_inds, ind_map, vert_co_proj, coords_proj, mpoly->totloop);

			copy_v3_v3(v1, data->mvert[sdvert->vert_inds[0]].co);
			copy_v3_v3(v2, data->mvert[sdvert->vert_inds[1]].co);
			copy_v3_v3(v3, cent);

			cent_tri_v3(cent, v1, v2, v3);
			normal_tri_v3(norm, v1, v2, v3);

			plane_from_point_normal_v3(plane, cent, norm);
			closest_to_plane_v3(vert_co_proj, plane, vert_co);

			interp_weights_face_v3(sdvert->vert_weights, v1, v2, v3, NULL, vert_co_proj);
		}
	}

	/* Compute normal displacement */
	float disp_vec[3];
	sub_v3_v3v3(disp_vec, vert_co, vert_co_proj);
	sdvert->normal_dist = len_v3(disp_vec);

	if (dot_v3v3(disp_vec, norm) < 0)
		sdvert->normal_dist *= -1;
}

static void surfacedeformBind(SurfaceDeformModifierData *smd, float (*vertexCos)[3],
                              int numVerts, int numPoly, DerivedMesh *tdm) {
	BVHTreeFromMesh treeData = {NULL};

	/* Create a bvh-tree of the target mesh */
	bvhtree_from_mesh_looptri(&treeData, tdm, 0.0, 2, 6);
	if (treeData.tree == NULL) {
		printf("Surface Deform: Out of memory\n");
		return;
	}

	smd->verts = MEM_mallocN(sizeof(*smd->verts) * numVerts, "SDefBindVerts");

	smd->numverts = numVerts;
	smd->numpoly = numPoly;

	SDefBindCalcData data = {.treeData = &treeData,
		                     .mpoly = tdm->getPolyArray(tdm),
		                     .mloop = tdm->getLoopArray(tdm),
		                     .looptri = tdm->getLoopTriArray(tdm),
		                     .mvert = tdm->getVertArray(tdm),
		                     .verts = smd->verts,
		                     .vertexCos = vertexCos};

	BLI_task_parallel_range_ex(0, numVerts, &data, NULL, 0, bindVert,
	                           numVerts > 10000, false);

	free_bvhtree_from_mesh(&treeData);
}

static void surfacedeformModifier_do(ModifierData *md, float (*vertexCos)[3], int numVerts)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;
	DerivedMesh *tdm;
	int numPoly;
	SDefVert *sdvert;
	MVert *mvert;
	int i, j;
	float norm[3];

	/* Exit function if bind flag is not set (free bind data if any) */
	if (!(smd->flags & MOD_SDEF_BIND)) {
		if (smd->verts) {
			for (i = 0; i < smd->numverts; i++) {
				if (smd->verts[i].vert_inds)
					MEM_freeN(smd->verts[i].vert_inds);

				if (smd->verts[i].vert_weights)
					MEM_freeN(smd->verts[i].vert_weights);
			}

			MEM_freeN(smd->verts);
			smd->verts = NULL;
		}
		return;
	}

	/* Handle target mesh both in and out of edit mode */
	if (smd->target == md->scene->obedit) {
		BMEditMesh *em = BKE_editmesh_from_object(smd->target);
		tdm = em->derivedFinal;
	}
	else
		tdm = smd->target->derivedFinal;

	numPoly = tdm->getNumPolys(tdm);

	/* If not bound, execute bind */
	if (!(smd->verts))
		surfacedeformBind(smd, vertexCos, numVerts, numPoly, tdm);

	/* Poly count checks */
	if (smd->numverts != numVerts) {
		modifier_setError(md, "Verts changed from %d to %d", smd->numverts, numVerts);
		tdm->release(tdm);
		return;
	}
	else if (smd->numpoly != numPoly) {
		modifier_setError(md, "Target polygons changed from %d to %d", smd->numpoly, numPoly);
		tdm->release(tdm);
		return;
	}

	/* Actual vertex location update starts here */
	mvert = tdm->getVertArray(tdm);
	sdvert = smd->verts;

	for (i = 0; i < numVerts; i++, sdvert++) {
		/* ---------- looptri mode ---------- */
		if (sdvert->mode == MOD_SDEF_MODE_LOOPTRI) {
			normal_tri_v3(norm, mvert[sdvert->vert_inds[0]].co,
			                    mvert[sdvert->vert_inds[1]].co,
			                    mvert[sdvert->vert_inds[2]].co);

			zero_v3(vertexCos[i]);
			madd_v3_v3fl(vertexCos[i], mvert[sdvert->vert_inds[0]].co, sdvert->vert_weights[0]);
			madd_v3_v3fl(vertexCos[i], mvert[sdvert->vert_inds[1]].co, sdvert->vert_weights[1]);
			madd_v3_v3fl(vertexCos[i], mvert[sdvert->vert_inds[2]].co, sdvert->vert_weights[2]);
		}
		else {
			/* Generic non-looptri mode operations (allocate poly coordinates) */
			float (*coords)[3] = BLI_array_alloca(coords, sdvert->numverts);

			for (j = 0; j < sdvert->numverts; j++) {
				copy_v3_v3(coords[j], mvert[sdvert->vert_inds[j]].co);
			}

			/* ---------- ngon mode ---------- */
			if (sdvert->mode == MOD_SDEF_MODE_NGON) {
				normal_poly_v3(norm, coords, sdvert->numverts);

				zero_v3(vertexCos[i]);

				for (j = 0; j < sdvert->numverts; j++) {
					madd_v3_v3fl(vertexCos[i], coords[j], sdvert->vert_weights[j]);
				}
			}

			/* ---------- centroid mode ---------- */
			else if (sdvert->mode == MOD_SDEF_MODE_CENTROID) {
				float cent[3];
				cent_poly_v3(cent, coords, sdvert->numverts);

				normal_tri_v3(norm, mvert[sdvert->vert_inds[0]].co,
				                    mvert[sdvert->vert_inds[1]].co,
				                    cent);

				zero_v3(vertexCos[i]);
				madd_v3_v3fl(vertexCos[i], mvert[sdvert->vert_inds[0]].co, sdvert->vert_weights[0]);
				madd_v3_v3fl(vertexCos[i], mvert[sdvert->vert_inds[1]].co, sdvert->vert_weights[1]);
				madd_v3_v3fl(vertexCos[i], cent, sdvert->vert_weights[2]);
			}
		}

		/* Apply normal offset (generic for all modes) */
		madd_v3_v3fl(vertexCos[i], norm, sdvert->normal_dist);
	}

	tdm->release(tdm);
}

static void deformVerts(ModifierData *md, Object *UNUSED(ob),
                        DerivedMesh *UNUSED(derivedData),
                        float (*vertexCos)[3], int numVerts,
                        ModifierApplyFlag UNUSED(flag))
{
	surfacedeformModifier_do(md, vertexCos, numVerts);
}

static void deformVertsEM(ModifierData *md, Object *UNUSED(ob),
                          struct BMEditMesh *UNUSED(editData),
                          DerivedMesh *UNUSED(derivedData),
                          float (*vertexCos)[3], int numVerts)
{
	surfacedeformModifier_do(md, vertexCos, numVerts);
}

static bool isDisabled(ModifierData *md, int UNUSED(useRenderParams))
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;

	return !smd->target;
}

ModifierTypeInfo modifierType_SurfaceDeform = {
	/* name */              "Surface Deform",
	/* structName */        "SurfaceDeformModifierData",
	/* structSize */        sizeof(SurfaceDeformModifierData),
	/* type */              eModifierTypeType_OnlyDeform,
	/* flags */             eModifierTypeFlag_AcceptsMesh |
	                        eModifierTypeFlag_SupportsEditmode,

	/* copyData */          copyData,
	/* deformVerts */       deformVerts,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     deformVertsEM,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     NULL,
	/* applyModifierEM */   NULL,
	/* initData */          initData,
	/* requiredDataMask */  NULL,
	/* freeData */          freeData,
	/* isDisabled */        isDisabled,
	/* updateDepgraph */    updateDepgraph,
	/* updateDepsgraph */   updateDepsgraph,
	/* dependsOnTime */     NULL,
	/* dependsOnNormals */  NULL,
	/* foreachObjectLink */ foreachObjectLink,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};
