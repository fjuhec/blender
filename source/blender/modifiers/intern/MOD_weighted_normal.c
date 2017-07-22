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
 * ***** END GPL LICENSE BLOCK *****
 *
 */

/** \file blender/modifiers/intern/MOD_weighted_normal.c
 *  \ingroup modifiers
 */

#include "MEM_guardedalloc.h"

#include "DNA_mesh_types.h"
#include "DNA_object_types.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_mesh.h"

#include "BLI_math.h"

#include "MOD_modifiertypes.h"

typedef struct pair {
	float val;		/* contains mode based value (face area/ corner angle) */
	int index;		/* index value per poly or per loop */
} pair;

/* Sorting function used in modifier, sorts in non increasing order */
static int sort_by_val(const void *p1, const void *p2)
{
	pair *r1 = (pair *)p1;
	pair *r2 = (pair *)p2;
	if (r1->val < r2->val)
		return 1;
	else if (r1->val > r2->val)
		return -1;
	else
		return 0;
}

static void apply_weights_vertex_normal(WeightedNormalModifierData *wnmd, Object *ob, DerivedMesh *dm,
	short(*clnors)[2], MVert *mvert, const int numVerts, MEdge *medge,
	const int numEdges, MLoop *mloop, const int numLoops, MPoly *mpoly,
	const int numPoly, float(*polynors)[3], const float weight, short mode, pair *mode_pair, float *index_angle)
{
	float(*custom_normal)[3] = MEM_callocN(sizeof(*custom_normal) * numVerts, "__func__");
	int *vertcount = MEM_callocN(sizeof(*vertcount) * numVerts, "__func__");	/* count number of loops using this vertex so far */
	float *cur_val = MEM_callocN(sizeof(*cur_val) * numVerts, "__func__");		/* current max val for this vertex */

	if (mode == MOD_WEIGHTEDNORMAL_MODE_FACE) {
		for (int i = 0; i < numPoly; i++) {			/* iterate through each pair in descending order */
			int ml_index = mpoly[mode_pair[i].index].loopstart;
			const int ml_index_end = ml_index + mpoly[mode_pair[i].index].totloop;

			for (; ml_index < ml_index_end; ml_index++) {
				float wnor[3];
				int mv_index = mloop[ml_index].v;

				if (!cur_val[mv_index]) {			/* if cur_val is 0 init it to present value */
					cur_val[mv_index] = mode_pair[i].val;
				}
				if (!compare_ff(cur_val[mv_index], mode_pair[i].val, wnmd->thresh)) {
					vertcount[mv_index]++;			/* cur_val and present value differ more than threshold, update  */
					cur_val[mv_index] = mode_pair[i].val;
				}
				float n_weight = pow(weight, vertcount[mv_index]);		/* exponentially divided weight for each normal */

				copy_v3_v3(wnor, polynors[mode_pair[i].index]);
				mul_v3_fl(wnor, mode_pair[i].val * (1.0f / n_weight));
				add_v3_v3(custom_normal[mv_index], wnor);
			}
		}
	}
	else if (mode == MOD_WEIGHTEDNORMAL_MODE_ANGLE || mode == MOD_WEIGHTEDNORMAL_MODE_FACE_ANGLE) {
		for (int i = 0; i < numLoops; i++) {
			float wnor[3];
			int ml_index = mode_pair[i].index;
			int mv_index = mloop[ml_index].v;

			if (!cur_val[mv_index]) {
				cur_val[mv_index] = mode_pair[i].val;
			}
			if (!compare_ff(cur_val[mv_index], mode_pair[i].val, wnmd->thresh)) {
				vertcount[mv_index]++;
				cur_val[mv_index] = mode_pair[i].val;
			}
			float n_weight = pow(weight, vertcount[mv_index]);

			copy_v3_v3(wnor, polynors[(int)index_angle[ml_index]]);
			mul_v3_fl(wnor, mode_pair[i].val * (1.0f / n_weight));
			add_v3_v3(custom_normal[mv_index], wnor);
		}
	}
	MEM_freeN(vertcount);
	MEM_freeN(cur_val);

	for (int mv_index = 0; mv_index < numVerts; mv_index++) {
		normalize_v3(custom_normal[mv_index]);
	}

	BKE_mesh_normals_loop_custom_from_vertices_set(mvert, custom_normal, numVerts, medge, numEdges,
		mloop, numLoops, mpoly, polynors, numPoly, clnors);

	MEM_freeN(custom_normal);
}

static void WeightedNormal_FaceArea(
	WeightedNormalModifierData *wnmd, Object *ob, DerivedMesh *dm,
	short (*clnors)[2], MVert *mvert, const int numVerts, MEdge *medge,
	const int numEdges, MLoop *mloop, const int numLoops, MPoly *mpoly,
	const int numPoly, float (*polynors)[3], const float weight)
{
	pair *face_area = MEM_mallocN(sizeof(*face_area) * numPoly, "__func__");

	for (int mp_index = 0; mp_index < numPoly; mp_index++) {
		face_area[mp_index].val = BKE_mesh_calc_poly_area(&mpoly[mp_index], &mloop[mpoly[mp_index].loopstart], mvert);
		face_area[mp_index].index = mp_index;
	}

	qsort(face_area, numPoly, sizeof(face_area), sort_by_val);
	apply_weights_vertex_normal(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops, mpoly, numPoly, polynors, weight,
		MOD_WEIGHTEDNORMAL_MODE_FACE, face_area, NULL);

	MEM_freeN(face_area);
}

static void WeightedNormal_CornerAngle(WeightedNormalModifierData *wnmd, Object *ob, DerivedMesh *dm,
	short(*clnors)[2], MVert *mvert, const int numVerts, MEdge *medge,
	const int numEdges, MLoop *mloop, const int numLoops, MPoly *mpoly,
	const int numPoly, float (*polynors)[3], const float weight)
{
	pair *corner_angle = MEM_mallocN(sizeof(*corner_angle) * numLoops, "__func__");
	float *index_angle = MEM_mallocN(sizeof(*index_angle) * numLoops, "__func__");
	/* index_angle is first used to calculate corner angle and is then used to store poly index for each loop */

	for (int mp_index = 0; mp_index < numPoly; mp_index++) {
		int l_start = mpoly[mp_index].loopstart;
		BKE_mesh_calc_poly_angles(&mpoly[mp_index], &mloop[l_start], mvert, &index_angle[l_start]);

		for (int i = l_start; i < l_start + mpoly[mp_index].totloop; i++) {
			corner_angle[i].val = (float)M_PI - index_angle[i];
			corner_angle[i].index = i;
			index_angle[i] = (float)mp_index;
		}
	}

	qsort(corner_angle, numLoops, sizeof(*corner_angle), sort_by_val);
	apply_weights_vertex_normal(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops, mpoly, numPoly, polynors, weight,
		MOD_WEIGHTEDNORMAL_MODE_ANGLE, corner_angle, index_angle);

	MEM_freeN(corner_angle);
	MEM_freeN(index_angle);
}

static void WeightedNormal_FacewithAngle(WeightedNormalModifierData *wnmd, Object *ob, DerivedMesh *dm,
	short(*clnors)[2], MVert *mvert, const int numVerts, MEdge *medge,
	const int numEdges, MLoop *mloop, const int numLoops, MPoly *mpoly,
	const int numPoly, float(*polynors)[3], const float weight)
{
	pair *combined = MEM_mallocN(sizeof(*combined) * numLoops, "__func__");
	float *index_angle = MEM_mallocN(sizeof(*index_angle) * numLoops, "__func__");

	for (int mp_index = 0; mp_index < numPoly; mp_index++) {
		int l_start = mpoly[mp_index].loopstart;
		float face_area = BKE_mesh_calc_poly_area(&mpoly[mp_index], &mloop[l_start], mvert);

		BKE_mesh_calc_poly_angles(&mpoly[mp_index], &mloop[l_start], mvert, &index_angle[l_start]);

		for (int i = l_start; i < l_start + mpoly[mp_index].totloop; i++) {
			combined[i].val = ((float)M_PI - index_angle[i]) * face_area;		// in this case val is product of corner angle and face area
			combined[i].index = i;
			index_angle[i] = (float)mp_index;
		}
	}

	qsort(combined, numLoops, sizeof(*combined), sort_by_val);
	apply_weights_vertex_normal(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops, mpoly, numPoly, polynors, weight,
		MOD_WEIGHTEDNORMAL_MODE_FACE_ANGLE, combined, index_angle);

	MEM_freeN(combined);
	MEM_freeN(index_angle);
}

static DerivedMesh *applyModifier(ModifierData *md, Object *ob, DerivedMesh *dm, ModifierApplyFlag UNUSED(flag))
{
	WeightedNormalModifierData *wnmd = (WeightedNormalModifierData *)md;

	Mesh *me = ob->data;
	int numPoly = dm->getNumPolys(dm);
	int numVerts = dm->getNumVerts(dm);
	int numEdges = dm->getNumEdges(dm);
	int numLoops = dm->getNumLoops(dm);
	MPoly *mpoly = dm->getPolyArray(dm);
	MVert *mvert = dm->getVertArray(dm);
	MEdge *medge = dm->getEdgeArray(dm);
	MLoop *mloop = dm->getLoopArray(dm);

	bool free_polynors = false;
	float weight = ((float)wnmd->weight) / 10.0f;
	float (*polynors)[3] = dm->getPolyDataArray(dm, CD_NORMAL);

	if (!polynors) {
		polynors = MEM_mallocN(sizeof(*polynors) * numPoly, __func__);
		BKE_mesh_calc_normals_poly(mvert, NULL, numVerts, mloop, mpoly, numLoops, numPoly, polynors, false);
		free_polynors = true;
	}

	short(*clnors)[2];

	if (!(me->flag & ME_AUTOSMOOTH)) {
		modifier_setError((ModifierData *)wnmd, "Enable 'Auto Smooth' option in mesh settings");
		return dm;
	}

	clnors = CustomData_duplicate_referenced_layer(&dm->loopData, CD_CUSTOMLOOPNORMAL, numLoops);
	if (!clnors) {
		DM_add_loop_layer(dm, CD_CUSTOMLOOPNORMAL, CD_CALLOC, NULL);
		clnors = dm->getLoopDataArray(dm, CD_CUSTOMLOOPNORMAL);
	}

	switch (wnmd->mode) {
		case MOD_WEIGHTEDNORMAL_MODE_FACE:
			WeightedNormal_FaceArea(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops, mpoly, numPoly, polynors, weight);
			break;
		case MOD_WEIGHTEDNORMAL_MODE_ANGLE:
			WeightedNormal_CornerAngle(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops, mpoly, numPoly, polynors, weight);
			break;
		case MOD_WEIGHTEDNORMAL_MODE_FACE_ANGLE:
			WeightedNormal_FacewithAngle(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops, mpoly, numPoly, polynors, weight);
			break;
	}
	if (free_polynors) {
		MEM_freeN(polynors);
	}

	return dm;
}

static void initData(ModifierData *md)
{
	WeightedNormalModifierData *wnmd = (WeightedNormalModifierData *)md;
	wnmd->mode = MOD_WEIGHTEDNORMAL_MODE_FACE;
	wnmd->weight = 10;
	wnmd->thresh = 1e-2f;
}

static void copyData(ModifierData *md, ModifierData *target)
{
	modifier_copyData_generic(md, target);
}

static bool dependsOnNormals(ModifierData *UNUSED(md))
{
	return true;
}

ModifierTypeInfo modifierType_WeightedNormal = {
	/* name */              "Weighted Normal",
	/* structName */        "WeightedNormalModifierData",
	/* structSize */        sizeof(WeightedNormalModifierData),
	/* type */              eModifierTypeType_Constructive,
	/* flags */             eModifierTypeFlag_AcceptsMesh |
	                        eModifierTypeFlag_SupportsEditmode |
	                        eModifierTypeFlag_EnableInEditmode,

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
	/* updateDepgraph */    NULL,
	/* updateDepsgraph */   NULL,
	/* dependsOnTime */     NULL,
	/* dependsOnNormals */  dependsOnNormals,
	/* foreachObjectLink */ NULL,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};
