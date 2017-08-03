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
#include "BKE_deform.h"
#include "BKE_mesh.h"

#include "BLI_bitmap.h"
#include "BLI_linklist_stack.h"
#include "BLI_math.h"

#include "bmesh_class.h"

#include "MOD_modifiertypes.h"
#include "MOD_util.h"

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
	const int numPoly, float(*polynors)[3], MDeformVert *dvert, int defgrp_index,
	const bool use_invert_vgroup, const float weight, short mode, pair *mode_pair, float *index_angle)
{
	float(*custom_normal)[3] = MEM_callocN(sizeof(*custom_normal) * numVerts, "__func__");
	int *vertcount = MEM_callocN(sizeof(*vertcount) * numVerts, "__func__");	/* count number of loops using this vertex so far */
	float *cur_val = MEM_callocN(sizeof(*cur_val) * numVerts, "__func__");		/* current max val for this vertex */

	const bool keep_sharp = (wnmd->flag & MOD_WEIGHTEDNORMAL_KEEP_SHARP) != 0,
		 has_vgroup = dvert != NULL;

	if (mode == MOD_WEIGHTEDNORMAL_MODE_FACE) {
		for (int i = 0; i < numPoly; i++) {			/* iterate through each pair in descending order */
			int ml_index = mpoly[mode_pair[i].index].loopstart;
			const int ml_index_end = ml_index + mpoly[mode_pair[i].index].totloop;

			for (; ml_index < ml_index_end; ml_index++) {

				int mv_index = mloop[ml_index].v;
				const bool vert_of_group = has_vgroup && dvert[mv_index].dw != NULL && dvert[mv_index].dw->def_nr == defgrp_index;

				if ( ((vert_of_group) ^ use_invert_vgroup) || !dvert) {
					float wnor[3];

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
	}
	else if (mode == MOD_WEIGHTEDNORMAL_MODE_ANGLE || mode == MOD_WEIGHTEDNORMAL_MODE_FACE_ANGLE) {
		for (int i = 0; i < numLoops; i++) {
			float wnor[3];
			int ml_index = mode_pair[i].index;
			int mv_index = mloop[ml_index].v;
			const bool vert_of_group = has_vgroup && dvert[mv_index].dw != NULL && dvert[mv_index].dw->def_nr == defgrp_index;

			if (((vert_of_group) ^ use_invert_vgroup) || !dvert) {
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
	}
	MEM_freeN(vertcount);
	MEM_freeN(cur_val);

	for (int mv_index = 0; mv_index < numVerts; mv_index++) {
		normalize_v3(custom_normal[mv_index]);
	}

	if (!keep_sharp && !has_vgroup) {
		BKE_mesh_normals_loop_custom_from_vertices_set(mvert, custom_normal, numVerts, medge, numEdges,
			mloop, numLoops, mpoly, polynors, numPoly, clnors);
	}
	else {
		float(*loop_normal)[3] = MEM_callocN(sizeof(*loop_normal) * numLoops, "__func__");
		int *loops_to_poly = MEM_mallocN(sizeof(*loops_to_poly) * numLoops, "__func__");
		int numSharpVerts = 0;

		BLI_bitmap *sharp_verts = BLI_BITMAP_NEW(numVerts, "__func__");
		int *loops_per_vert = MEM_callocN(sizeof(*loops_per_vert) * numVerts, "__func__");

		for (int mp_index = 0; mp_index < numPoly; mp_index++) {
			int ml_index = mpoly[mp_index].loopstart;
			const int ml_index_end = ml_index + mpoly[mp_index].totloop;

			for (int i = ml_index; i < ml_index_end; i++) {
				if ((medge[mloop[i].e].flag & ME_SHARP) && !BLI_BITMAP_TEST(sharp_verts, mloop[i].v)) {
					numSharpVerts++;
					BLI_BITMAP_ENABLE(sharp_verts, mloop[i].v);
				}
				loops_per_vert[mloop[i].v]++;
				loops_to_poly[i] = mp_index;
				copy_v3_v3(loop_normal[i], custom_normal[mloop[i].v]);
			}
		}

		if (keep_sharp) {
			void **loops_of_vert = MEM_mallocN(sizeof(loops_of_vert) * numSharpVerts, "__func__");
			int cur = 0;

			for (int mp_index = 0; mp_index < numPoly; mp_index++) {
				int ml_index = mpoly[mp_index].loopstart;
				const int ml_index_end = ml_index + mpoly[mp_index].totloop;

				for (int i = ml_index, k = 0; i < ml_index_end; i++) {

					if (BLI_BITMAP_TEST(sharp_verts, mloop[i].v)) {

						bool found = false;
						for (k = 0; k < cur; k++) {
							int v_index = *(int *)loops_of_vert[k];
							if (mloop[i].v == v_index) {
								found = true;
								break;
							}
						}
						if (found) {
							int *loops = loops_of_vert[k];
							while (*loops != -1) {
								loops++;
							}
							*loops = i;
						}
						else {
							int *loops;
							loops_of_vert[k] = loops = MEM_callocN(sizeof(*loops) * (loops_per_vert[mloop[i].v] + 1), "__func__");
							memset(loops, -1, sizeof(*loops) * (loops_per_vert[mloop[i].v] + 1));
							loops[0] = mloop[i].v;
							loops[1] = i;
							cur++;
						}
					}
				}
			}
			MEM_freeN(sharp_verts);
			for (int i = 0; i < numSharpVerts; i++) {
				int *loops = loops_of_vert[i];
				int totloop = loops_per_vert[*loops];
				loops++;
				int *base_loop = loops;

				BLI_SMALLSTACK_DECLARE(loop_nors, float *);

				float avg_normal[3] = { 0 }, min_normal[3] = { 0 };
				int min = 0, stack = 0, sharp_edges = 0;
				bool check = (medge[mloop[*loops].e].flag & ME_SHARP) ? false : true;

				for (int k = 0; k < totloop; k++) {
					MPoly mp = mpoly[loops_to_poly[*loops]];
					MLoop *prev_loop = (*loops - 1 >= mp.loopstart ? &mloop[*loops - 1] : &mloop[mp.loopstart + mp.totloop - 1]),
						*next_loop = (*loops + 1 <= mp.loopstart + mp.totloop ? &mloop[*loops + 1] : &mloop[mp.loopstart]),
						*vert_loop;

					int other_v1 = BKE_mesh_edge_other_vert(&medge[prev_loop->e], prev_loop->v),
						other_v2 = BKE_mesh_edge_other_vert(&medge[next_loop->e], next_loop->v);

					if (other_v1 == mloop[*loops].v) {
						vert_loop = prev_loop;
					}
					else if (other_v2 == mloop[*loops].v) {
						vert_loop = next_loop;
					}
					if (medge[mloop[*loops].e].flag & ME_SHARP) {
						sharp_edges++;
						check = false;
					}
					if (check) {
						stack++;
						BLI_SMALLSTACK_PUSH(loop_nors, loop_normal[*loops]);
						add_v3_v3(min_normal, polynors[loops_to_poly[*loops]]);
						min = stack;
					}
					else {
						if ((medge[mloop[*loops].e].flag & ME_SHARP)) {
							normalize_v3(avg_normal);
							while (stack > min) {
								float *normal = BLI_SMALLSTACK_POP(loop_nors);
								copy_v3_v3(normal, avg_normal);
								stack--;
							}
							zero_v3(avg_normal);
						}
						stack++;
						BLI_SMALLSTACK_PUSH(loop_nors, loop_normal[*loops]);
						add_v3_v3(avg_normal, polynors[loops_to_poly[*loops]]);
					}
					
					for (int j = 0, *l_index = base_loop; j < totloop; j++, l_index++) {
						if (loops == l_index || *l_index == -1)
							continue;
						MPoly *mp = &mpoly[loops_to_poly[*l_index]];
						bool has_poly = false;

						for (int ml_index = mp->loopstart; ml_index < mp->loopstart + mp->totloop; ml_index++) {
							if (mloop[ml_index].e == vert_loop->e) {
								*loops = -1;
								loops = l_index;
								has_poly = true;
								break;
							}
						}
						if (has_poly) {
							break;
						}
					}
				}
				if (!BLI_SMALLSTACK_IS_EMPTY(loop_nors)) {
					float *normal;
					add_v3_v3(avg_normal, min_normal);
					normalize_v3(avg_normal);
					while ((normal = BLI_SMALLSTACK_POP(loop_nors))) {
						copy_v3_v3(normal, avg_normal);
					}
				}
			}
			for (int i = 0; i < numSharpVerts; i++) {
				MEM_freeN(loops_of_vert[i]);
			}
			MEM_freeN(loops_of_vert);
		}
		BKE_mesh_normals_loop_custom_set(mvert, numVerts, medge, numEdges,
			mloop, loop_normal, numLoops, mpoly, polynors, numPoly, clnors);
		MEM_freeN(loops_to_poly);
		MEM_freeN(loop_normal);
		MEM_freeN(loops_per_vert);
	}
		
	MEM_freeN(custom_normal);
}

static void WeightedNormal_FaceArea(
	WeightedNormalModifierData *wnmd, Object *ob, DerivedMesh *dm,
	short (*clnors)[2], MVert *mvert, const int numVerts, MEdge *medge,
	const int numEdges, MLoop *mloop, const int numLoops, MPoly *mpoly,
	const int numPoly, float (*polynors)[3], MDeformVert *dvert, 
	int defgrp_index, const bool use_invert_vgroup, const float weight)
{
	pair *face_area = MEM_mallocN(sizeof(*face_area) * numPoly, "__func__");
	const bool bool_weights = (wnmd->flag & MOD_WEIGHTEDNORMAL_BOOL_WEIGHTS) != 0;

	for (int mp_index = 0; mp_index < numPoly; mp_index++) {
		face_area[mp_index].val = BKE_mesh_calc_poly_area(&mpoly[mp_index], &mloop[mpoly[mp_index].loopstart], mvert);
		if (bool_weights && (mpoly[mp_index].flag & ME_SMOOTH)) {
			face_area[mp_index].val = 0;
		}
		face_area[mp_index].index = mp_index;
	}

	qsort(face_area, numPoly, sizeof(face_area), sort_by_val);
	apply_weights_vertex_normal(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops, mpoly, numPoly, polynors,
		dvert, defgrp_index, use_invert_vgroup, weight, MOD_WEIGHTEDNORMAL_MODE_FACE, face_area, NULL);

	MEM_freeN(face_area);
}

static void WeightedNormal_CornerAngle(WeightedNormalModifierData *wnmd, Object *ob, DerivedMesh *dm,
	short(*clnors)[2], MVert *mvert, const int numVerts, MEdge *medge,
	const int numEdges, MLoop *mloop, const int numLoops, MPoly *mpoly,
	const int numPoly, float (*polynors)[3], MDeformVert *dvert, 
	int defgrp_index, const bool use_invert_vgroup, const float weight)
{
	pair *corner_angle = MEM_mallocN(sizeof(*corner_angle) * numLoops, "__func__");
	float *index_angle = MEM_mallocN(sizeof(*index_angle) * numLoops, "__func__");
	const bool bool_weights = (wnmd->flag & MOD_WEIGHTEDNORMAL_BOOL_WEIGHTS) != 0;
	/* index_angle is first used to calculate corner angle and is then used to store poly index for each loop */

	for (int mp_index = 0; mp_index < numPoly; mp_index++) {
		int l_start = mpoly[mp_index].loopstart;
		BKE_mesh_calc_poly_angles(&mpoly[mp_index], &mloop[l_start], mvert, &index_angle[l_start]);

		for (int i = l_start; i < l_start + mpoly[mp_index].totloop; i++) {
			corner_angle[i].val = (float)M_PI - index_angle[i];
			if (bool_weights && (mpoly[mp_index].flag & ME_SMOOTH)) {
				corner_angle[i].val = 0;
			}
			corner_angle[i].index = i; 
			index_angle[i] = (float)mp_index;
		}
	}

	qsort(corner_angle, numLoops, sizeof(*corner_angle), sort_by_val);
	apply_weights_vertex_normal(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops, mpoly, numPoly, polynors, 
		dvert, defgrp_index, use_invert_vgroup, weight, MOD_WEIGHTEDNORMAL_MODE_ANGLE, corner_angle, index_angle);

	MEM_freeN(corner_angle);
	MEM_freeN(index_angle);
}

static void WeightedNormal_FacewithAngle(WeightedNormalModifierData *wnmd, Object *ob, DerivedMesh *dm,
	short(*clnors)[2], MVert *mvert, const int numVerts, MEdge *medge,
	const int numEdges, MLoop *mloop, const int numLoops, MPoly *mpoly,
	const int numPoly, float(*polynors)[3], MDeformVert *dvert,
	 int defgrp_index, const bool use_invert_vgroup, const float weight)
{
	pair *combined = MEM_mallocN(sizeof(*combined) * numLoops, "__func__");
	float *index_angle = MEM_mallocN(sizeof(*index_angle) * numLoops, "__func__");
	const bool bool_weights = (wnmd->flag & MOD_WEIGHTEDNORMAL_BOOL_WEIGHTS) != 0;

	for (int mp_index = 0; mp_index < numPoly; mp_index++) {
		int l_start = mpoly[mp_index].loopstart;
		float face_area = BKE_mesh_calc_poly_area(&mpoly[mp_index], &mloop[l_start], mvert);

		BKE_mesh_calc_poly_angles(&mpoly[mp_index], &mloop[l_start], mvert, &index_angle[l_start]);

		for (int i = l_start; i < l_start + mpoly[mp_index].totloop; i++) {
			combined[i].val = ((float)M_PI - index_angle[i]) * face_area;		// in this case val is product of corner angle and face area
			if (bool_weights && (mpoly[mp_index].flag & ME_SMOOTH)) {
				combined[i].val = 0;
			}
			combined[i].index = i;
			index_angle[i] = (float)mp_index;
		}
	}

	qsort(combined, numLoops, sizeof(*combined), sort_by_val);
	apply_weights_vertex_normal(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops, mpoly, numPoly, polynors,
		dvert, defgrp_index, use_invert_vgroup, weight, MOD_WEIGHTEDNORMAL_MODE_FACE_ANGLE, combined, index_angle);

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

	MDeformVert *dvert;
	int defgrp_index;

	const bool use_invert_vgroup = (wnmd->flag & MOD_WEIGHTEDNORMAL_INVERT_VGROUP) != 0;
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

	modifier_get_vgroup(ob, dm, wnmd->defgrp_name, &dvert, &defgrp_index);

	switch (wnmd->mode) {
		case MOD_WEIGHTEDNORMAL_MODE_FACE:
			WeightedNormal_FaceArea(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, 
				numLoops, mpoly, numPoly, polynors, dvert, defgrp_index, use_invert_vgroup, weight);
			break;
		case MOD_WEIGHTEDNORMAL_MODE_ANGLE:
			WeightedNormal_CornerAngle(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, 
				numLoops, mpoly, numPoly, polynors, dvert, defgrp_index, use_invert_vgroup, weight);
			break;
		case MOD_WEIGHTEDNORMAL_MODE_FACE_ANGLE:
			WeightedNormal_FacewithAngle(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, 
				numLoops, mpoly, numPoly, polynors, dvert, defgrp_index, use_invert_vgroup, weight);
			break;
	}
	if (free_polynors) {
		MEM_freeN(polynors);
	}

	return dm;
}

static void copyData(ModifierData *md, ModifierData *target)
{
	modifier_copyData_generic(md, target);
}

static void initData(ModifierData *md)
{
	WeightedNormalModifierData *wnmd = (WeightedNormalModifierData *)md;
	wnmd->mode = MOD_WEIGHTEDNORMAL_MODE_FACE;
	wnmd->weight = 10;
	wnmd->thresh = 1e-2f;
	wnmd->flag = 0;
}

static CustomDataMask requiredDataMask(Object *UNUSED(ob), ModifierData *md)
{
	WeightedNormalModifierData *wnmd = (WeightedNormalModifierData *)md;
	CustomDataMask dataMask = CD_CUSTOMLOOPNORMAL;

	if (wnmd->defgrp_name[0]) {
		dataMask |= CD_MASK_MDEFORMVERT;
	}

	return dataMask;
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
	/* requiredDataMask */  requiredDataMask,
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
