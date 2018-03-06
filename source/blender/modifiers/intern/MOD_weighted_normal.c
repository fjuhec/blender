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

#include "limits.h"

#include "MEM_guardedalloc.h"

#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_mesh.h"

#include "BLI_math.h"
#include "BLI_stack.h"

#include "bmesh_class.h"

#include "MOD_modifiertypes.h"
#include "MOD_util.h"

#define INDEX_UNSET INT_MIN
#define INDEX_INVALID -1
#define IS_EDGE_SHARP(_e2l) (ELEM((_e2l)[1], INDEX_UNSET, INDEX_INVALID))

typedef struct ModePair {
	float val;  /* Contains mode based value (face area / corner angle). */
	int index;  /* Index value per poly or per loop. */
} ModePair;

/* Sorting function used in modifier, sorts in decreasing order. */
static int modepair_cmp_by_val_inverse(const void *p1, const void *p2)
{
	ModePair *r1 = (ModePair *)p1;
	ModePair *r2 = (ModePair *)p2;

	if (r1->val < r2->val)
		return 1;
	else if (r1->val > r2->val)
		return -1;

	return 0;
}

/* Sorts by index in increasing order. */
static int modepair_cmp_by_index(const void *p1, const void *p2)
{
	ModePair *r1 = (ModePair *)p1;
	ModePair *r2 = (ModePair *)p2;

	if (r1->index > r2->index)
		return 1;
	else if (r1->index < r2->index)
		return -1;

	return 0;
}

static bool check_strength(int strength, int *cur_strength, float *cur_val, int *vertcount, float (*custom_normal)[3])
{
	if ((strength == FACE_STRENGTH_STRONG && *cur_strength != FACE_STRENGTH_STRONG) ||
	    (strength == FACE_STRENGTH_MEDIUM && *cur_strength == FACE_STRENGTH_WEAK))
	{
		*cur_strength = strength;
		*cur_val = 0.0f;
		*vertcount = 0;
		zero_v3(*custom_normal);
	}
	else if (strength != *cur_strength) {
		return false;
	}
	return true;
}

static void apply_weights_sharp_loops(
        WeightedNormalModifierData *wnmd, int *loop_index, int size, ModePair *mode_pair,
        float(*loop_normal)[3], int *loops_to_poly, float(*polynors)[3], int weight, int *strength)
{
	for (int i = 0; i < size - 1; i++) {
		for (int j = 0; j < size - i - 1; j++) {
			if (wnmd->mode == MOD_WEIGHTEDNORMAL_MODE_FACE
				&& mode_pair[loops_to_poly[loop_index[j]]].val < mode_pair[loops_to_poly[loop_index[j + 1]]].val) {
				int temp = loop_index[j];
				loop_index[j] = loop_index[j + 1];
				loop_index[j + 1] = temp;
			}
			else if ((wnmd->mode == MOD_WEIGHTEDNORMAL_MODE_ANGLE || wnmd->mode == MOD_WEIGHTEDNORMAL_MODE_FACE_ANGLE)
				&& mode_pair[loop_index[j]].val < mode_pair[loop_index[j + 1]].val) {
				int temp = loop_index[j];
				loop_index[j] = loop_index[j + 1];
				loop_index[j + 1] = temp;
			}
		}
	}
	float cur_val = 0, custom_normal[3] = { 0.0f };
	int  vertcount = 0, cur_strength = FACE_STRENGTH_WEAK;
	const bool face_influence = (wnmd->flag & MOD_WEIGHTEDNORMAL_FACE_INFLUENCE) != 0;

	for (int i = 0; i < size; i++) {
		float wnor[3];
		int j, mp_index;
		bool do_loop = true;

		if (wnmd->mode == MOD_WEIGHTEDNORMAL_MODE_FACE) {
			j = loops_to_poly[loop_index[i]];
			mp_index = mode_pair[j].index;
		}
		else {
			j = loop_index[i];
			mp_index = loops_to_poly[j];
		} 
		if (face_influence && strength) {
			do_loop = check_strength(strength[mp_index], &cur_strength,	&cur_val, &vertcount, &custom_normal);
		}
		if (do_loop) {
			if (!cur_val) {
				cur_val = mode_pair[j].val;
			}
			if (!compare_ff(cur_val, mode_pair[j].val, wnmd->thresh)) {
				vertcount++;
				cur_val = mode_pair[j].val;
			}
			float n_weight = pow(weight, vertcount);
			copy_v3_v3(wnor, polynors[mp_index]);

			mul_v3_fl(wnor, mode_pair[j].val * (1.0f / n_weight));
			add_v3_v3(custom_normal, wnor);
		}
	}
	normalize_v3(custom_normal);

	for (int i = 0; i < size; i++) {
		copy_v3_v3(loop_normal[loop_index[i]], custom_normal);
	}
}

/* Modified version of loop_split_worker_do which sets custom_normals without considering smoothness of faces or
 * loop normal space array.
 * Used only to work on sharp edges. */
static void loop_split_worker(
        WeightedNormalModifierData *wnmd, ModePair *mode_pair, MLoop *ml_curr, MLoop *ml_prev,
        int ml_curr_index, int ml_prev_index, int *e2l_prev, int mp_index,
        float (*loop_normal)[3], int *loops_to_poly, float (*polynors)[3],
        MEdge *medge, MLoop *mloop, MPoly *mpoly, int (*edge_to_loops)[2], int weight, int *strength)
{
	if (e2l_prev) {
		int *e2lfan_curr = e2l_prev;
		const MLoop *mlfan_curr = ml_prev;
		int mlfan_curr_index = ml_prev_index;
		int mlfan_vert_index = ml_curr_index;
		int mpfan_curr_index = mp_index;

		BLI_Stack *loop_index = BLI_stack_new(sizeof(int), __func__);

		while (true) {
			const unsigned int mv_pivot_index = ml_curr->v;
			const MEdge *me_curr = &medge[mlfan_curr->e];
			const MEdge *me_org = &medge[ml_curr->e];

			BLI_stack_push(loop_index, &mlfan_vert_index);

			if (IS_EDGE_SHARP(e2lfan_curr) || (me_curr == me_org)) {
				break;
			}

			BKE_mesh_loop_manifold_fan_around_vert_next(
			            mloop, mpoly, loops_to_poly, e2lfan_curr, mv_pivot_index,
			            &mlfan_curr, &mlfan_curr_index, &mlfan_vert_index, &mpfan_curr_index);

			e2lfan_curr = edge_to_loops[mlfan_curr->e];
		}

		int *index = MEM_mallocN(sizeof(*index) * BLI_stack_count(loop_index), __func__);
		int cur = 0;
		while (!BLI_stack_is_empty(loop_index)) {
			BLI_stack_pop(loop_index, &index[cur]);
			cur++;
		}
		apply_weights_sharp_loops(wnmd, index, cur, mode_pair, loop_normal, loops_to_poly, polynors, weight, strength);
		MEM_freeN(index);
		BLI_stack_free(loop_index);
	}
	else {
		copy_v3_v3(loop_normal[ml_curr_index], polynors[loops_to_poly[ml_curr_index]]);
	}
}

static void apply_weights_vertex_normal(
        WeightedNormalModifierData *wnmd, Object *UNUSED(ob), DerivedMesh *UNUSED(dm), short(*clnors)[2],
        MVert *mvert, const int numVerts, MEdge *medge, const int numEdges, MLoop *mloop, const int numLoops,
        MPoly *mpoly, const int numPoly, float(*polynors)[3], MDeformVert *dvert, int defgrp_index,
        const bool use_invert_vgroup, const float weight, short mode, ModePair *mode_pair, int *strength)
{
	float(*custom_normal)[3] = MEM_callocN(sizeof(*custom_normal) * numVerts, __func__);
	int *vertcount = MEM_callocN(sizeof(*vertcount) * numVerts, __func__);  /* Count number of loops using this vertex so far. */
	float *cur_val = MEM_callocN(sizeof(*cur_val) * numVerts, __func__);  /* Current max val for this vertex. */
	int *cur_strength = MEM_mallocN(sizeof(*cur_strength) * numVerts, __func__); /* Current max strength encountered for this vertex. */
	int *loops_to_poly = MEM_mallocN(sizeof(*loops_to_poly) * numLoops, __func__);

	for (int mp_index = 0; mp_index < numPoly; mp_index++) {
		int ml_index = mpoly[mp_index].loopstart;
		const int ml_index_end = ml_index + mpoly[mp_index].totloop;

		for (; ml_index < ml_index_end; ml_index++) {
			loops_to_poly[ml_index] = mp_index;
		}
	}
	for (int i = 0; i < numVerts; i++) {
		cur_strength[i] = FACE_STRENGTH_WEAK;
	}

	const bool keep_sharp = (wnmd->flag & MOD_WEIGHTEDNORMAL_KEEP_SHARP) != 0;
	const bool face_influence = (wnmd->flag & MOD_WEIGHTEDNORMAL_FACE_INFLUENCE) != 0;
	const bool has_vgroup = dvert != NULL;

	if (mode == MOD_WEIGHTEDNORMAL_MODE_FACE) {
		for (int i = 0; i < numPoly; i++) {  /* Iterate through each pair in descending order. */
			int ml_index = mpoly[mode_pair[i].index].loopstart;
			const int ml_index_end = ml_index + mpoly[mode_pair[i].index].totloop;

			for (; ml_index < ml_index_end; ml_index++) {
				int mv_index = mloop[ml_index].v;
				const bool vert_of_group = has_vgroup && dvert[mv_index].dw != NULL && dvert[mv_index].dw->def_nr == defgrp_index;

				if ((vert_of_group ^ use_invert_vgroup) || !dvert) {
					float wnor[3];
					bool do_loop = true;

					if (face_influence && strength) {
						do_loop = check_strength(strength[mode_pair[i].index], &cur_strength[mv_index], 
						                         &cur_val[mv_index], &vertcount[mv_index], &custom_normal[mv_index]);
					}
					if (do_loop) {
						if (!cur_val[mv_index]) {  /* If cur_val is 0 init it to present value. */
							cur_val[mv_index] = mode_pair[i].val;
						}
						if (!compare_ff(cur_val[mv_index], mode_pair[i].val, wnmd->thresh)) {
							vertcount[mv_index]++;  /* cur_val and present value differ more than threshold, update. */
							cur_val[mv_index] = mode_pair[i].val;
						}
						float n_weight = pow(weight, vertcount[mv_index]);  /* Exponentially divided weight for each normal. */

						copy_v3_v3(wnor, polynors[mode_pair[i].index]);
						mul_v3_fl(wnor, mode_pair[i].val * (1.0f / n_weight));
						add_v3_v3(custom_normal[mv_index], wnor);
					}
				}
			}
		}
	}
	else if (ELEM(mode, MOD_WEIGHTEDNORMAL_MODE_ANGLE, MOD_WEIGHTEDNORMAL_MODE_FACE_ANGLE)) {
		for (int i = 0; i < numLoops; i++) {
			float wnor[3];
			int ml_index = mode_pair[i].index;
			int mv_index = mloop[ml_index].v;
			const bool vert_of_group = has_vgroup && dvert[mv_index].dw != NULL && dvert[mv_index].dw->def_nr == defgrp_index;

			if ((vert_of_group ^ use_invert_vgroup) || !dvert) {
				bool do_loop = true;

				if (face_influence && strength) {
					do_loop = check_strength(strength[loops_to_poly[ml_index]], &cur_strength[mv_index],
					                         &cur_val[mv_index], &vertcount[mv_index], &custom_normal[mv_index]);
				}
				if (do_loop) {
					if (!cur_val[mv_index]) {
						cur_val[mv_index] = mode_pair[i].val;
					}
					if (!compare_ff(cur_val[mv_index], mode_pair[i].val, wnmd->thresh)) {
						vertcount[mv_index]++;
						cur_val[mv_index] = mode_pair[i].val;
					}
					float n_weight = pow(weight, vertcount[mv_index]);

					copy_v3_v3(wnor, polynors[loops_to_poly[ml_index]]);
					mul_v3_fl(wnor, mode_pair[i].val * (1.0f / n_weight));
					add_v3_v3(custom_normal[mv_index], wnor);
				}
			}
		}
	}
	for (int mv_index = 0; mv_index < numVerts; mv_index++) {
		normalize_v3(custom_normal[mv_index]);
	}

	if (!keep_sharp && !has_vgroup) {
		BKE_mesh_normals_loop_custom_from_vertices_set(mvert, custom_normal, numVerts, medge, numEdges,
		                                               mloop, numLoops, mpoly, polynors, numPoly, clnors);
	}
	else {
		float(*loop_normal)[3] = MEM_callocN(sizeof(*loop_normal) * numLoops, "__func__");

		BKE_mesh_normals_loop_split(mvert, numVerts, medge, numEdges, mloop, loop_normal, numLoops, mpoly, polynors,
		                            numPoly, true, (float)M_PI, NULL, clnors, loops_to_poly);

		for (int mp_index = 0; mp_index < numPoly; mp_index++) {
			int ml_index = mpoly[mp_index].loopstart;
			const int ml_index_end = ml_index + mpoly[mp_index].totloop;

			for (int i = ml_index; i < ml_index_end; i++) {
				if (!is_zero_v3(custom_normal[mloop[i].v])) {
					copy_v3_v3(loop_normal[i], custom_normal[mloop[i].v]);
				}
			}
		}

		if (keep_sharp) {
			int (*edge_to_loops)[2] = MEM_callocN(sizeof(*edge_to_loops) * numEdges, __func__);

			if (wnmd->mode == MOD_WEIGHTEDNORMAL_MODE_FACE) {
				qsort(mode_pair, numPoly, sizeof(*mode_pair), modepair_cmp_by_index);
			}
			else {
				qsort(mode_pair, numLoops, sizeof(*mode_pair), modepair_cmp_by_index);
			}
			MPoly *mp;
			int mp_index;
			for (mp = mpoly, mp_index = 0; mp_index < numPoly; mp++, mp_index++) {
				int ml_curr_index = mp->loopstart;
				const int ml_last_index = (ml_curr_index + mp->totloop) - 1;

				MLoop *ml_curr = &mloop[ml_curr_index];

				for (; ml_curr_index <= ml_last_index; ml_curr++, ml_curr_index++) {
					int *e2l = edge_to_loops[ml_curr->e];

					if ((e2l[0] | e2l[1]) == 0) {
						e2l[0] = ml_curr_index;
						/* Not considering smoothness of faces, UNSET if first loop encountered on this edge. */
						e2l[1] = INDEX_UNSET;
					}
					else if (e2l[1] == INDEX_UNSET) {
						if ((medge[ml_curr->e].flag & ME_SHARP) || ml_curr->v == mloop[e2l[0]].v) {
							e2l[1] = INDEX_INVALID;
						}
						else {
							e2l[1] = ml_curr_index;
						}
					}
					else if (!IS_EDGE_SHARP(e2l)) {
						e2l[1] = INDEX_INVALID;
					}
				}
			}

			for (mp = mpoly, mp_index = 0; mp_index < numPoly; mp++, mp_index++) {
				const int ml_last_index = (mp->loopstart + mp->totloop) - 1;
				int ml_curr_index = mp->loopstart;
				int ml_prev_index = ml_last_index;

				MLoop *ml_curr = &mloop[ml_curr_index];
				MLoop *ml_prev = &mloop[ml_prev_index];

				for (; ml_curr_index <= ml_last_index; ml_curr++, ml_curr_index++) {
					int *e2l_curr = edge_to_loops[ml_curr->e];
					int *e2l_prev = edge_to_loops[ml_prev->e];

					if (IS_EDGE_SHARP(e2l_curr)) {
						if (IS_EDGE_SHARP(e2l_curr) && IS_EDGE_SHARP(e2l_prev)) {
							loop_split_worker(wnmd, mode_pair, ml_curr, ml_prev, ml_curr_index, -1, NULL,
							                  mp_index, loop_normal, loops_to_poly, polynors, medge, mloop, mpoly,
							                  edge_to_loops, weight, strength);
						}
						else {
							loop_split_worker(wnmd, mode_pair, ml_curr, ml_prev, ml_curr_index, ml_prev_index, e2l_prev,
							                  mp_index, loop_normal, loops_to_poly, polynors, medge, mloop, mpoly,
							                  edge_to_loops, weight, strength);
						}
					}
					ml_prev = ml_curr;
					ml_prev_index = ml_curr_index;
				}
			}
			MEM_freeN(edge_to_loops);
		}
		BKE_mesh_normals_loop_custom_set(mvert, numVerts, medge, numEdges,
		                                 mloop, loop_normal, numLoops, mpoly, polynors, numPoly, clnors);
		MEM_freeN(loop_normal);
	}

	MEM_freeN(loops_to_poly);
	MEM_freeN(cur_strength);
	MEM_freeN(vertcount);
	MEM_freeN(cur_val);
	MEM_freeN(custom_normal);
}

static void wn_face_area(
        WeightedNormalModifierData *wnmd, Object *ob, DerivedMesh *dm, short (*clnors)[2],
        MVert *mvert, const int numVerts, MEdge *medge, const int numEdges, MLoop *mloop, const int numLoops,
        MPoly *mpoly, const int numPoly, float (*polynors)[3],
        MDeformVert *dvert, int defgrp_index, const bool use_invert_vgroup, const float weight, int *strength)
{
	ModePair *face_area = MEM_mallocN(sizeof(*face_area) * numPoly, __func__);

	for (int mp_index = 0; mp_index < numPoly; mp_index++) {
		face_area[mp_index].val = BKE_mesh_calc_poly_area(&mpoly[mp_index], &mloop[mpoly[mp_index].loopstart], mvert);
		face_area[mp_index].index = mp_index;
	}

	qsort(face_area, numPoly, sizeof(*face_area), modepair_cmp_by_val_inverse);
	apply_weights_vertex_normal(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops,
	                            mpoly, numPoly, polynors, dvert, defgrp_index, use_invert_vgroup,
	                            weight, MOD_WEIGHTEDNORMAL_MODE_FACE, face_area, strength);

	MEM_freeN(face_area);
}

static void wn_corner_angle(
        WeightedNormalModifierData *wnmd, Object *ob, DerivedMesh *dm, short(*clnors)[2],
        MVert *mvert, const int numVerts, MEdge *medge, const int numEdges, MLoop *mloop, const int numLoops,
        MPoly *mpoly, const int numPoly, float (*polynors)[3],
        MDeformVert *dvert, int defgrp_index, const bool use_invert_vgroup, const float weight, int *strength)
{
	ModePair *corner_angle = MEM_mallocN(sizeof(*corner_angle) * numLoops, __func__);

	for (int mp_index = 0; mp_index < numPoly; mp_index++) {
		int l_start = mpoly[mp_index].loopstart;
		float *index_angle = MEM_mallocN(sizeof(*index_angle) * mpoly[mp_index].totloop, __func__);
		BKE_mesh_calc_poly_angles(&mpoly[mp_index], &mloop[l_start], mvert, index_angle);

		for (int i = l_start; i < l_start + mpoly[mp_index].totloop; i++) {
			corner_angle[i].val = (float)M_PI - index_angle[i - l_start];
			corner_angle[i].index = i; 
		}
		MEM_freeN(index_angle);
	}

	qsort(corner_angle, numLoops, sizeof(*corner_angle), modepair_cmp_by_val_inverse);
	apply_weights_vertex_normal(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops,
	                            mpoly, numPoly, polynors, dvert, defgrp_index, use_invert_vgroup,
	                            weight, MOD_WEIGHTEDNORMAL_MODE_ANGLE, corner_angle, strength);

	MEM_freeN(corner_angle);
}

static void wn_face_with_angle(
        WeightedNormalModifierData *wnmd, Object *ob, DerivedMesh *dm, short(*clnors)[2],
        MVert *mvert, const int numVerts, MEdge *medge, const int numEdges, MLoop *mloop, const int numLoops,
        MPoly *mpoly, const int numPoly, float(*polynors)[3],
        MDeformVert *dvert, int defgrp_index, const bool use_invert_vgroup, const float weight, int *strength)
{
	ModePair *combined = MEM_mallocN(sizeof(*combined) * numLoops, __func__);

	for (int mp_index = 0; mp_index < numPoly; mp_index++) {
		int l_start = mpoly[mp_index].loopstart;
		float face_area = BKE_mesh_calc_poly_area(&mpoly[mp_index], &mloop[l_start], mvert);
		float *index_angle = MEM_mallocN(sizeof(*index_angle) * mpoly[mp_index].totloop, __func__);

		BKE_mesh_calc_poly_angles(&mpoly[mp_index], &mloop[l_start], mvert, index_angle);

		for (int i = l_start; i < l_start + mpoly[mp_index].totloop; i++) {
			/* In this case val is product of corner angle and face area. */
			combined[i].val = ((float)M_PI - index_angle[i - l_start]) * face_area;
			combined[i].index = i;
		}
		MEM_freeN(index_angle);
	}

	qsort(combined, numLoops, sizeof(*combined), modepair_cmp_by_val_inverse);
	apply_weights_vertex_normal(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops,
	                            mpoly, numPoly, polynors, dvert, defgrp_index, use_invert_vgroup,
	                            weight, MOD_WEIGHTEDNORMAL_MODE_FACE_ANGLE, combined, strength);

	MEM_freeN(combined);
}

static DerivedMesh *applyModifier(ModifierData *md, Object *ob, DerivedMesh *dm, ModifierApplyFlag UNUSED(flag))
{
	WeightedNormalModifierData *wnmd = (WeightedNormalModifierData *)md;

	Mesh *me = ob->data;

	if (!(me->flag & ME_AUTOSMOOTH)) {
		modifier_setError((ModifierData *)wnmd, "Enable 'Auto Smooth' option in mesh settings");
		return dm;
	}

	const int numPoly = dm->getNumPolys(dm);
	const int numVerts = dm->getNumVerts(dm);
	const int numEdges = dm->getNumEdges(dm);
	const int numLoops = dm->getNumLoops(dm);
	MPoly *mpoly = dm->getPolyArray(dm);
	MVert *mvert = dm->getVertArray(dm);
	MEdge *medge = dm->getEdgeArray(dm);
	MLoop *mloop = dm->getLoopArray(dm);

	MDeformVert *dvert;
	int defgrp_index;

	const bool use_invert_vgroup = (wnmd->flag & MOD_WEIGHTEDNORMAL_INVERT_VGROUP) != 0;
	bool free_polynors = false;

	float weight = ((float)wnmd->weight) / 50.0f;

	if (wnmd->weight == 100) {
		weight = (float)SHRT_MAX;
	}
	else if (wnmd->weight == 1) {
		weight = 1 / (float)SHRT_MAX;
	}
	else if ((weight - 1) * 25 > 1) {
		weight = (weight - 1) * 25;
	}

	float (*polynors)[3] = dm->getPolyDataArray(dm, CD_NORMAL);

	if (!polynors) {
		polynors = MEM_mallocN(sizeof(*polynors) * numPoly, __func__);
		BKE_mesh_calc_normals_poly(mvert, NULL, numVerts, mloop, mpoly, numLoops, numPoly, polynors, false);
		free_polynors = true;
	}

	short (*clnors)[2];
	clnors = CustomData_duplicate_referenced_layer(&dm->loopData, CD_CUSTOMLOOPNORMAL, numLoops);
	if (!clnors) {
		DM_add_loop_layer(dm, CD_CUSTOMLOOPNORMAL, CD_CALLOC, NULL);
		clnors = dm->getLoopDataArray(dm, CD_CUSTOMLOOPNORMAL);
	}

	int *strength = CustomData_get_layer_named(&dm->polyData, CD_PROP_INT, MOD_WEIGHTEDNORMALS_FACEWEIGHT_CDLAYER_ID);

	modifier_get_vgroup(ob, dm, wnmd->defgrp_name, &dvert, &defgrp_index);

	switch (wnmd->mode) {
		case MOD_WEIGHTEDNORMAL_MODE_FACE:
			wn_face_area(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops,
			             mpoly, numPoly, polynors, dvert, defgrp_index, use_invert_vgroup, weight, strength);
			break;
		case MOD_WEIGHTEDNORMAL_MODE_ANGLE:
			wn_corner_angle(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops,
			                mpoly, numPoly, polynors, dvert, defgrp_index, use_invert_vgroup, weight, strength);
			break;
		case MOD_WEIGHTEDNORMAL_MODE_FACE_ANGLE:
			wn_face_with_angle(wnmd, ob, dm, clnors, mvert, numVerts, medge, numEdges, mloop, numLoops,
			                   mpoly, numPoly, polynors, dvert, defgrp_index, use_invert_vgroup, weight, strength);
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
	wnmd->weight = 50;
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

	if (wnmd->flag & MOD_WEIGHTEDNORMAL_FACE_INFLUENCE) {
		dataMask |= CD_MASK_PROP_INT;
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
	                        eModifierTypeFlag_SupportsMapping |
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
