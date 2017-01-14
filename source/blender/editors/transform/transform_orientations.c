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
 * Contributor(s): Martin Poirier
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/transform/transform_orientations.c
 *  \ingroup edtransform
 */

#include <string.h>
#include <stddef.h>
#include <ctype.h>

#include "MEM_guardedalloc.h"

#include "DNA_armature_types.h"
#include "DNA_curve_types.h"
#include "DNA_meta_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"

#include "BLI_math.h"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_path_util.h"
#include "BLI_utildefines.h"

#include "BKE_action.h"
#include "BKE_curve.h"
#include "BKE_context.h"
#include "BKE_editmesh.h"
#include "BKE_report.h"
#include "BKE_main.h"
#include "BKE_screen.h"

#include "BLT_translation.h"

#include "ED_armature.h"

#include "transform.h"

/* *********************** TransSpace ************************** */

void BIF_clearTransformOrientation(bContext *C)
{
	View3D *v3d = CTX_wm_view3d(C);

	ListBase *transform_spaces = &CTX_data_scene(C)->transform_spaces;
	BLI_freelistN(transform_spaces);
	
	// Need to loop over all view3d
	if (v3d && v3d->transform_orientation >= V3D_TRANS_ORIENTATION_CUSTOM) {
		v3d->transform_orientation = V3D_TRANS_ORIENTATION_GLOBAL; /* fallback to global	*/
	}
}

static TransformOrientation *findOrientationName(ListBase *lb, const char *name)
{
	return BLI_findstring(lb, name, offsetof(TransformOrientation, name));
}

static bool uniqueOrientationNameCheck(void *arg, const char *name)
{
	return findOrientationName((ListBase *)arg, name) != NULL;
}

static void uniqueOrientationName(ListBase *lb, char *name)
{
	BLI_uniquename_cb(uniqueOrientationNameCheck, lb, CTX_DATA_(BLT_I18NCONTEXT_ID_SCENE, "Space"), '.', name,
	                  sizeof(((TransformOrientation *)NULL)->name));
}

static TransformOrientation *createViewSpace(bContext *C, ReportList *UNUSED(reports),
                                             const char *name, const bool overwrite)
{
	RegionView3D *rv3d = CTX_wm_region_view3d(C);
	float mat[3][3];

	if (!rv3d)
		return NULL;

	copy_m3_m4(mat, rv3d->viewinv);
	normalize_m3(mat);

	if (name[0] == 0) {
		View3D *v3d = CTX_wm_view3d(C);
		if (rv3d->persp == RV3D_CAMOB && v3d->camera) {
			/* If an object is used as camera, then this space is the same as object space! */
			name = v3d->camera->id.name + 2;
		}
		else {
			name = "Custom View";
		}
	}

	return addMatrixSpace(C, mat, name, overwrite);
}

static TransformOrientation *createObjectSpace(bContext *C, ReportList *UNUSED(reports),
                                               const char *name, const bool overwrite)
{
	Base *base = CTX_data_active_base(C);
	Object *ob;
	float mat[3][3];

	if (base == NULL)
		return NULL;

	ob = base->object;

	copy_m3_m4(mat, ob->obmat);
	normalize_m3(mat);

	/* use object name if no name is given */
	if (name[0] == 0) {
		name = ob->id.name + 2;
	}

	return addMatrixSpace(C, mat, name, overwrite);
}

static TransformOrientation *createBoneSpace(bContext *C, ReportList *reports,
                                             const char *name, const bool overwrite)
{
	float mat[3][3];
	float normal[3], plane[3];

	getLocalTransformOrientation(C, normal, plane);

	if (createSpaceNormalTangent(mat, normal, plane) == 0) {
		BKE_reports_prepend(reports, "Cannot use zero-length bone");
		return NULL;
	}

	if (name[0] == 0) {
		name = "Bone";
	}

	return addMatrixSpace(C, mat, name, overwrite);
}

static TransformOrientation *createCurveSpace(bContext *C, ReportList *reports,
                                              const char *name, const bool overwrite)
{
	float mat[3][3];
	float normal[3], plane[3];

	getLocalTransformOrientation(C, normal, plane);

	if (createSpaceNormalTangent(mat, normal, plane) == 0) {
		BKE_reports_prepend(reports, "Cannot use zero-length curve");
		return NULL;
	}

	if (name[0] == 0) {
		name = "Curve";
	}

	return addMatrixSpace(C, mat, name, overwrite);
}


static TransformOrientation *createMeshSpace(bContext *C, ReportList *reports,
                                             const char *name, const bool overwrite)
{
	float mat[3][3];
	float normal[3], plane[3];
	int type;

	type = getLocalTransformOrientation(C, normal, plane);
	
	switch (type) {
		case ORIENTATION_VERT:
			if (createSpaceNormal(mat, normal) == 0) {
				BKE_reports_prepend(reports, "Cannot use vertex with zero-length normal");
				return NULL;
			}
	
			if (name[0] == 0) {
				name = "Vertex";
			}
			break;
		case ORIENTATION_EDGE:
			if (createSpaceNormalTangent(mat, normal, plane) == 0) {
				BKE_reports_prepend(reports, "Cannot use zero-length edge");
				return NULL;
			}
	
			if (name[0] == 0) {
				name = "Edge";
			}
			break;
		case ORIENTATION_FACE:
			if (createSpaceNormalTangent(mat, normal, plane) == 0) {
				BKE_reports_prepend(reports, "Cannot use zero-area face");
				return NULL;
			}
	
			if (name[0] == 0) {
				name = "Face";
			}
			break;
		default:
			return NULL;
	}

	return addMatrixSpace(C, mat, name, overwrite);
}

bool createSpaceNormal(float mat[3][3], const float normal[3])
{
	float tangent[3] = {0.0f, 0.0f, 1.0f};
	
	copy_v3_v3(mat[2], normal);
	if (normalize_v3(mat[2]) == 0.0f) {
		return false;  /* error return */
	}

	cross_v3_v3v3(mat[0], mat[2], tangent);
	if (is_zero_v3(mat[0])) {
		tangent[0] = 1.0f;
		tangent[1] = tangent[2] = 0.0f;
		cross_v3_v3v3(mat[0], tangent, mat[2]);
	}

	cross_v3_v3v3(mat[1], mat[2], mat[0]);

	normalize_m3(mat);
	
	return true;
}

/**
 * \note To recreate an orientation from the matrix:
 * - (plane  == mat[1])
 * - (normal == mat[2])
 */
bool createSpaceNormalTangent(float mat[3][3], const float normal[3], const float tangent[3])
{
	if (normalize_v3_v3(mat[2], normal) == 0.0f) {
		return false;  /* error return */
	}

	/* negate so we can use values from the matrix as input */
	negate_v3_v3(mat[1], tangent);
	/* preempt zero length tangent from causing trouble */
	if (is_zero_v3(mat[1])) {
		mat[1][2] = 1.0f;
	}

	cross_v3_v3v3(mat[0], mat[2], mat[1]);
	if (normalize_v3(mat[0]) == 0.0f) {
		return false;  /* error return */
	}
	
	cross_v3_v3v3(mat[1], mat[2], mat[0]);
	normalize_v3(mat[1]);

	/* final matrix must be normalized, do inline */
	// normalize_m3(mat);
	
	return true;
}

void BIF_createTransformOrientation(bContext *C, ReportList *reports,
                                    const char *name, const bool use_view,
                                    const bool activate, const bool overwrite)
{
	TransformOrientation *ts = NULL;

	if (use_view) {
		ts = createViewSpace(C, reports, name, overwrite);
	}
	else {
		Object *obedit = CTX_data_edit_object(C);
		Object *ob = CTX_data_active_object(C);
		if (obedit) {
			if (obedit->type == OB_MESH)
				ts = createMeshSpace(C, reports, name, overwrite);
			else if (obedit->type == OB_ARMATURE)
				ts = createBoneSpace(C, reports, name, overwrite);
			else if (obedit->type == OB_CURVE)
				ts = createCurveSpace(C, reports, name, overwrite);
		}
		else if (ob && (ob->mode & OB_MODE_POSE)) {
			ts = createBoneSpace(C, reports, name, overwrite);
		}
		else {
			ts = createObjectSpace(C, reports, name, overwrite);
		}
	}

	if (activate && ts != NULL) {
		BIF_selectTransformOrientation(C, ts);
	}
}

TransformOrientation *addMatrixSpace(bContext *C, float mat[3][3],
                                     const char *name, const bool overwrite)
{
	ListBase *transform_spaces = &CTX_data_scene(C)->transform_spaces;
	TransformOrientation *ts = NULL;
	char name_unique[sizeof(ts->name)];

	if (overwrite) {
		ts = findOrientationName(transform_spaces, name);
	}
	else {
		BLI_strncpy(name_unique, name, sizeof(name_unique));
		uniqueOrientationName(transform_spaces, name_unique);
		name = name_unique;
	}

	/* if not, create a new one */
	if (ts == NULL) {
		ts = MEM_callocN(sizeof(TransformOrientation), "UserTransSpace from matrix");
		BLI_addtail(transform_spaces, ts);
		BLI_strncpy(ts->name, name, sizeof(ts->name));
	}

	/* copy matrix into transform space */
	copy_m3_m3(ts->mat, mat);

	return ts;
}

void BIF_removeTransformOrientation(bContext *C, TransformOrientation *target)
{
	Scene *scene = CTX_data_scene(C);
	ListBase *transform_spaces = &scene->transform_spaces;
	const int i = BLI_findindex(transform_spaces, target);

	if (i != -1) {
		Main *bmain = CTX_data_main(C);
		BKE_screen_view3d_main_transform_orientation_remove(&bmain->screen, scene, i);
		BLI_freelinkN(transform_spaces, target);
	}
}

void BIF_removeTransformOrientationIndex(bContext *C, int index)
{
	ListBase *transform_spaces = &CTX_data_scene(C)->transform_spaces;
	TransformOrientation *ts = BLI_findlink(transform_spaces, index);

	if (ts) {
		BIF_removeTransformOrientation(C, ts);
	}
}

void BIF_selectTransformOrientation(bContext *C, TransformOrientation *target)
{
	ListBase *transform_spaces = &CTX_data_scene(C)->transform_spaces;
	const int i = BLI_findindex(transform_spaces, target);

	if (i != -1) {
		View3D *v3d = CTX_wm_view3d(C);
		v3d->transform_orientation = V3D_TRANS_ORIENTATION_CUSTOM + i;
	}
}

void BIF_selectTransformOrientationValue(bContext *C, int orientation)
{
	View3D *v3d = CTX_wm_view3d(C);
	if (v3d) /* currently using generic poll */
		v3d->transform_orientation = orientation;
}

int BIF_countTransformOrientation(const bContext *C)
{
	ListBase *transform_spaces = &CTX_data_scene(C)->transform_spaces;
	return BLI_listbase_count(transform_spaces);
}

bool applyTransformOrientation(const bContext *C, float mat[3][3], char *r_name, int index)
{
	ListBase *transform_spaces = &CTX_data_scene(C)->transform_spaces;
	TransformOrientation *ts = BLI_findlink(transform_spaces, index);

	BLI_assert(index >= 0);

	if (ts) {
		if (r_name) {
			BLI_strncpy(r_name, ts->name, MAX_NAME);
		}

		copy_m3_m3(mat, ts->mat);
		return true;
	}
	else {
		/* invalid index, can happen sometimes */
		return false;
	}
}

static int count_bone_select(bArmature *arm, ListBase *lb, const bool do_it)
{
	Bone *bone;
	bool do_next;
	int total = 0;
	
	for (bone = lb->first; bone; bone = bone->next) {
		bone->flag &= ~BONE_TRANSFORM;
		do_next = do_it;
		if (do_it) {
			if (bone->layer & arm->layer) {
				if (bone->flag & BONE_SELECTED) {
					bone->flag |= BONE_TRANSFORM;
					total++;

					/* no transform on children if one parent bone is selected */
					do_next = false;
				}
			}
		}
		total += count_bone_select(arm, &bone->childbase, do_next);
	}
	
	return total;
}

/* could move into BLI_math however this is only useful for display/editing purposes */
static void axis_angle_to_gimbal_axis(float gmat[3][3], const float axis[3], const float angle)
{
	/* X/Y are arbitrary axies, most importantly Z is the axis of rotation */

	float cross_vec[3];
	float quat[4];

	/* this is an un-scientific method to get a vector to cross with
	 * XYZ intentionally YZX */
	cross_vec[0] = axis[1];
	cross_vec[1] = axis[2];
	cross_vec[2] = axis[0];

	/* X-axis */
	cross_v3_v3v3(gmat[0], cross_vec, axis);
	normalize_v3(gmat[0]);
	axis_angle_to_quat(quat, axis, angle);
	mul_qt_v3(quat, gmat[0]);

	/* Y-axis */
	axis_angle_to_quat(quat, axis, M_PI_2);
	copy_v3_v3(gmat[1], gmat[0]);
	mul_qt_v3(quat, gmat[1]);

	/* Z-axis */
	copy_v3_v3(gmat[2], axis);

	normalize_m3(gmat);
}

static int test_rotmode_euler(short rotmode)
{
	return (ELEM(rotmode, ROT_MODE_AXISANGLE, ROT_MODE_QUAT)) ? 0 : 1;
}

static bool gimbal_axis(Object *ob, float gmat[3][3])
{
	if (ob) {
		if (ob->mode & OB_MODE_POSE) {
			bPoseChannel *pchan = BKE_pose_channel_active(ob);

			if (pchan) {
				float mat[3][3], tmat[3][3], obmat[3][3];
				if (test_rotmode_euler(pchan->rotmode)) {
					eulO_to_gimbal_axis(mat, pchan->eul, pchan->rotmode);
				}
				else if (pchan->rotmode == ROT_MODE_AXISANGLE) {
					axis_angle_to_gimbal_axis(mat, pchan->rotAxis, pchan->rotAngle);
				}
				else { /* quat */
					return 0;
				}


				/* apply bone transformation */
				mul_m3_m3m3(tmat, pchan->bone->bone_mat, mat);

				if (pchan->parent) {
					float parent_mat[3][3];

					copy_m3_m4(parent_mat, pchan->parent->pose_mat);
					mul_m3_m3m3(mat, parent_mat, tmat);

					/* needed if object transformation isn't identity */
					copy_m3_m4(obmat, ob->obmat);
					mul_m3_m3m3(gmat, obmat, mat);
				}
				else {
					/* needed if object transformation isn't identity */
					copy_m3_m4(obmat, ob->obmat);
					mul_m3_m3m3(gmat, obmat, tmat);
				}

				normalize_m3(gmat);
				return 1;
			}
		}
		else {
			if (test_rotmode_euler(ob->rotmode)) {
				eulO_to_gimbal_axis(gmat, ob->rot, ob->rotmode);
			}
			else if (ob->rotmode == ROT_MODE_AXISANGLE) {
				axis_angle_to_gimbal_axis(gmat, ob->rotAxis, ob->rotAngle);
			}
			else { /* quat */
				return 0;
			}

			if (ob->parent) {
				float parent_mat[3][3];
				copy_m3_m4(parent_mat, ob->parent->obmat);
				normalize_m3(parent_mat);
				mul_m3_m3m3(gmat, parent_mat, gmat);
			}
			return 1;
		}
	}

	return 0;
}

void initTransformOrientation(bContext *C, TransInfo *t)
{
	Object *ob = CTX_data_active_object(C);
	Object *obedit = CTX_data_active_object(C);

	switch (t->current_orientation) {
		case V3D_TRANS_ORIENTATION_GLOBAL:
			unit_m3(t->spacemtx);
			BLI_strncpy(t->spacename, IFACE_("global"), sizeof(t->spacename));
			break;

		case V3D_TRANS_ORIENTATION_GIMBAL:
			unit_m3(t->spacemtx);
			if (gimbal_axis(ob, t->spacemtx)) {
				BLI_strncpy(t->spacename, IFACE_("gimbal"), sizeof(t->spacename));
				break;
			}
			/* fall-through */  /* no gimbal fallthrough to normal */
		case V3D_TRANS_ORIENTATION_NORMAL:
			if (obedit || (ob && ob->mode & OB_MODE_POSE)) {
				BLI_strncpy(t->spacename, IFACE_("normal"), sizeof(t->spacename));
				ED_getLocalTransformOrientationMatrix(C, t->spacemtx, t->around);
				break;
			}
			/* fall-through */  /* we define 'normal' as 'local' in Object mode */
		case V3D_TRANS_ORIENTATION_LOCAL:
			BLI_strncpy(t->spacename, IFACE_("local"), sizeof(t->spacename));
		
			if (ob) {
				copy_m3_m4(t->spacemtx, ob->obmat);
				normalize_m3(t->spacemtx);
			}
			else {
				unit_m3(t->spacemtx);
			}
		
			break;
		
		case V3D_TRANS_ORIENTATION_VIEW:
			if ((t->spacetype == SPACE_VIEW3D) &&
			    (t->ar->regiontype == RGN_TYPE_WINDOW))
			{
				RegionView3D *rv3d = t->ar->regiondata;
				float mat[3][3];

				BLI_strncpy(t->spacename, IFACE_("view"), sizeof(t->spacename));
				copy_m3_m4(mat, rv3d->viewinv);
				normalize_m3(mat);
				copy_m3_m3(t->spacemtx, mat);
			}
			else {
				unit_m3(t->spacemtx);
			}
			break;
		default: /* V3D_MANIP_CUSTOM */
			if (applyTransformOrientation(C, t->spacemtx, t->spacename, t->current_orientation - V3D_TRANS_ORIENTATION_CUSTOM)) {
				/* pass */
			}
			else {
				unit_m3(t->spacemtx);
			}
			break;
	}
}

/**
 * utility function - get first n, selected vert/edge/faces
 */
static unsigned int bm_mesh_elems_select_get_n__internal(
        BMesh *bm, BMElem **elems, const unsigned int n,
        const BMIterType itype, const char htype)
{
	BMIter iter;
	BMElem *ele;
	unsigned int i;

	BLI_assert(ELEM(htype, BM_VERT, BM_EDGE, BM_FACE));
	BLI_assert(ELEM(itype, BM_VERTS_OF_MESH, BM_EDGES_OF_MESH, BM_FACES_OF_MESH));

	if (!BLI_listbase_is_empty(&bm->selected)) {
		/* quick check */
		BMEditSelection *ese;
		i = 0;
		for (ese = bm->selected.last; ese; ese = ese->prev) {
			/* shouldn't need this check */
			if (BM_elem_flag_test(ese->ele, BM_ELEM_SELECT)) {

				/* only use contiguous selection */
				if (ese->htype != htype) {
					i = 0;
					break;
				}

				elems[i++] = ese->ele;
				if (n == i) {
					break;
				}
			}
			else {
				BLI_assert(0);
			}
		}

		if (i == 0) {
			/* pass */
		}
		else if (i == n) {
			return i;
		}
	}

	i = 0;
	BM_ITER_MESH (ele, &iter, bm, itype) {
		BLI_assert(ele->head.htype == htype);
		if (BM_elem_flag_test(ele, BM_ELEM_SELECT)) {
			elems[i++] = ele;
			if (n == i) {
				break;
			}
		}
	}

	return i;
}

static unsigned int bm_mesh_verts_select_get_n(BMesh *bm, BMVert **elems, const unsigned int n)
{
	return bm_mesh_elems_select_get_n__internal(
	        bm, (BMElem **)elems, min_ii(n, bm->totvertsel),
	        BM_VERTS_OF_MESH, BM_VERT);
}
static unsigned int bm_mesh_edges_select_get_n(BMesh *bm, BMEdge **elems, const unsigned int n)
{
	return bm_mesh_elems_select_get_n__internal(
	        bm, (BMElem **)elems, min_ii(n, bm->totedgesel),
	        BM_EDGES_OF_MESH, BM_EDGE);
}
#if 0
static unsigned int bm_mesh_faces_select_get_n(BMesh *bm, BMVert **elems, const unsigned int n)
{
	return bm_mesh_elems_select_get_n__internal(
	        bm, (BMElem **)elems, min_ii(n, bm->totfacesel),
	        BM_FACES_OF_MESH, BM_FACE);
}
#endif

int getLocalTransformOrientation_ex(const bContext *C, float normal[3], float plane[3], const short around)
{
	Scene *scene = CTX_data_scene(C);
	Object *obedit = CTX_data_edit_object(C);
	Base *base;
	Object *ob = OBACT;
	int result = ORIENTATION_NONE;
	const bool activeOnly = (around == V3D_AROUND_ACTIVE);

	zero_v3(normal);
	zero_v3(plane);

	if (obedit) {
		float imat[3][3], mat[3][3];
		
		/* we need the transpose of the inverse for a normal... */
		copy_m3_m4(imat, ob->obmat);
		
		invert_m3_m3(mat, imat);
		transpose_m3(mat);

		ob = obedit;

		if (ob->type == OB_MESH) {
			BMEditMesh *em = BKE_editmesh_from_object(ob);
			BMEditSelection ese;
			float vec[3] = {0, 0, 0};
			
			/* USE LAST SELECTED WITH ACTIVE */
			if (activeOnly && BM_select_history_active_get(em->bm, &ese)) {
				BM_editselection_normal(&ese, normal);
				BM_editselection_plane(&ese, plane);
				
				switch (ese.htype) {
					case BM_VERT:
						result = ORIENTATION_VERT;
						break;
					case BM_EDGE:
						result = ORIENTATION_EDGE;
						break;
					case BM_FACE:
						result = ORIENTATION_FACE;
						break;
				}
			}
			else {
				if (em->bm->totfacesel >= 1) {
					BMFace *efa;
					BMIter iter;

					BM_ITER_MESH (efa, &iter, em->bm, BM_FACES_OF_MESH) {
						if (BM_elem_flag_test(efa, BM_ELEM_SELECT)) {
							BM_face_calc_tangent_auto(efa, vec);
							add_v3_v3(normal, efa->no);
							add_v3_v3(plane, vec);
						}
					}
					
					result = ORIENTATION_FACE;
				}
				else if (em->bm->totvertsel == 3) {
					BMVert *v_tri[3];

					if (bm_mesh_verts_select_get_n(em->bm, v_tri, 3) == 3) {
						BMEdge *e = NULL;
						float no_test[3];

						normal_tri_v3(normal, v_tri[0]->co, v_tri[1]->co, v_tri[2]->co);

						/* check if the normal is pointing opposite to vert normals */
						no_test[0] = v_tri[0]->no[0] + v_tri[1]->no[0] + v_tri[2]->no[0];
						no_test[1] = v_tri[0]->no[1] + v_tri[1]->no[1] + v_tri[2]->no[1];
						no_test[2] = v_tri[0]->no[2] + v_tri[1]->no[2] + v_tri[2]->no[2];
						if (dot_v3v3(no_test, normal) < 0.0f) {
							negate_v3(normal);
						}

						if (em->bm->totedgesel >= 1) {
							/* find an edge thats apart of v_tri (no need to search all edges) */
							float e_length;
							int j;

							for (j = 0; j < 3; j++) {
								BMEdge *e_test = BM_edge_exists(v_tri[j], v_tri[(j + 1) % 3]);
								if (e_test && BM_elem_flag_test(e_test, BM_ELEM_SELECT)) {
									const float e_test_length = BM_edge_calc_length_squared(e_test);
									if ((e == NULL) || (e_length < e_test_length)) {
										e = e_test;
										e_length = e_test_length;
									}
								}
							}
						}

						if (e) {
							BMVert *v_pair[2];
							if (BM_edge_is_boundary(e)) {
								BM_edge_ordered_verts(e, &v_pair[0], &v_pair[1]);
							}
							else {
								v_pair[0] = e->v1;
								v_pair[1] = e->v2;
							}
							sub_v3_v3v3(plane, v_pair[0]->co, v_pair[1]->co);
						}
						else {
							BM_vert_tri_calc_tangent_edge(v_tri, plane);
						}
					}
					else {
						BLI_assert(0);
					}

					result = ORIENTATION_FACE;
				}
				else if (em->bm->totedgesel == 1 || em->bm->totvertsel == 2) {
					BMVert *v_pair[2] = {NULL, NULL};
					BMEdge *eed = NULL;
					
					if (em->bm->totedgesel == 1) {
						if (bm_mesh_edges_select_get_n(em->bm, &eed, 1) == 1) {
							v_pair[0] = eed->v1;
							v_pair[1] = eed->v2;
						}
					}
					else {
						BLI_assert(em->bm->totvertsel == 2);
						bm_mesh_verts_select_get_n(em->bm, v_pair, 2);
					}

					/* should never fail */
					if (LIKELY(v_pair[0] && v_pair[1])) {
						bool v_pair_swap = false;
						/**
						 * Logic explained:
						 *
						 * - Edges and vert-pairs treated the same way.
						 * - Point the Y axis along the edge vector (towards the active vertex).
						 * - Point the Z axis outwards (the same direction as the normals).
						 *
						 * \note Z points outwards - along the normal.
						 * take care making changes here, see: T38592, T43708
						 */

						/* be deterministic where possible and ensure v_pair[0] is active */
						if (BM_mesh_active_vert_get(em->bm) == v_pair[1]) {
							v_pair_swap = true;
						}
						else if (eed && BM_edge_is_boundary(eed)) {
							/* predictable direction for boundary edges */
							if (eed->l->v != v_pair[0]) {
								v_pair_swap = true;
							}
						}

						if (v_pair_swap) {
							SWAP(BMVert *, v_pair[0], v_pair[1]);
						}

						add_v3_v3v3(normal, v_pair[0]->no, v_pair[1]->no);
						sub_v3_v3v3(plane, v_pair[0]->co, v_pair[1]->co);
						/* flip the plane normal so we point outwards */
						negate_v3(plane);
					}

					result = ORIENTATION_EDGE;
				}
				else if (em->bm->totvertsel == 1) {
					BMVert *v = NULL;

					if (bm_mesh_verts_select_get_n(em->bm, &v, 1) == 1) {
						copy_v3_v3(normal, v->no);
						BMEdge *e_pair[2];

						if (BM_vert_edge_pair(v, &e_pair[0], &e_pair[1])) {
							bool v_pair_swap = false;
							BMVert *v_pair[2] = {BM_edge_other_vert(e_pair[0], v), BM_edge_other_vert(e_pair[1], v)};
							float dir_pair[2][3];

							if (BM_edge_is_boundary(e_pair[0])) {
								if (e_pair[0]->l->v != v) {
									v_pair_swap = true;
								}
							}
							else {
								if (BM_edge_calc_length_squared(e_pair[0]) < BM_edge_calc_length_squared(e_pair[1])) {
									v_pair_swap = true;
								}
							}

							if (v_pair_swap) {
								SWAP(BMVert *, v_pair[0], v_pair[1]);
							}

							sub_v3_v3v3(dir_pair[0], v->co, v_pair[0]->co);
							sub_v3_v3v3(dir_pair[1], v_pair[1]->co, v->co);
							normalize_v3(dir_pair[0]);
							normalize_v3(dir_pair[1]);

							add_v3_v3v3(plane, dir_pair[0], dir_pair[1]);
						}
					}

					if (is_zero_v3(plane)) {
						result = ORIENTATION_VERT;
					}
					else {
						result = ORIENTATION_EDGE;
					}
				}
				else if (em->bm->totvertsel > 3) {
					BMIter iter;
					BMVert *v;

					zero_v3(normal);

					BM_ITER_MESH (v, &iter, em->bm, BM_VERTS_OF_MESH) {
						if (BM_elem_flag_test(v, BM_ELEM_SELECT)) {
							add_v3_v3(normal, v->no);
						}
					}
					normalize_v3(normal);
					result = ORIENTATION_VERT;
				}
			}

			/* not needed but this matches 2.68 and older behavior */
			negate_v3(plane);

		} /* end editmesh */
		else if (ELEM(obedit->type, OB_CURVE, OB_SURF)) {
			Curve *cu = obedit->data;
			Nurb *nu = NULL;
			BezTriple *bezt = NULL;
			int a;
			ListBase *nurbs = BKE_curve_editNurbs_get(cu);

			if (activeOnly && BKE_curve_nurb_vert_active_get(cu, &nu, (void *)&bezt)) {
				if (nu->type == CU_BEZIER) {
					BKE_nurb_bezt_calc_normal(nu, bezt, normal);
					BKE_nurb_bezt_calc_plane(nu, bezt, plane);
				}
			}
			else {
				const bool use_handle = (cu->drawflag & CU_HIDE_HANDLES) == 0;

				for (nu = nurbs->first; nu; nu = nu->next) {
					/* only bezier has a normal */
					if (nu->type == CU_BEZIER) {
						bezt = nu->bezt;
						a = nu->pntsu;
						while (a--) {
							short flag = 0;

#define SEL_F1 (1 << 0)
#define SEL_F2 (1 << 1)
#define SEL_F3 (1 << 2)

							if (use_handle) {
								if (bezt->f1 & SELECT) flag |= SEL_F1;
								if (bezt->f2 & SELECT) flag |= SEL_F2;
								if (bezt->f3 & SELECT) flag |= SEL_F3;
							}
							else {
								flag = (bezt->f2 & SELECT) ? (SEL_F1 | SEL_F2 | SEL_F3) : 0;
							}

							/* exception */
							if (flag) {
								float tvec[3];
								if ((around == V3D_AROUND_LOCAL_ORIGINS) ||
								    ELEM(flag, SEL_F2, SEL_F1 | SEL_F3, SEL_F1 | SEL_F2 | SEL_F3))
								{
									BKE_nurb_bezt_calc_normal(nu, bezt, tvec);
									add_v3_v3(normal, tvec);
								}
								else {
									/* ignore bezt->f2 in this case */
									if (flag & SEL_F1) {
										sub_v3_v3v3(tvec, bezt->vec[0], bezt->vec[1]);
										normalize_v3(tvec);
										add_v3_v3(normal, tvec);
									}
									if (flag & SEL_F3) {
										sub_v3_v3v3(tvec, bezt->vec[1], bezt->vec[2]);
										normalize_v3(tvec);
										add_v3_v3(normal, tvec);
									}
								}

								BKE_nurb_bezt_calc_plane(nu, bezt, tvec);
								add_v3_v3(plane, tvec);
							}

#undef SEL_F1
#undef SEL_F2
#undef SEL_F3

							bezt++;
						}
					}
				}
			}
			
			if (!is_zero_v3(normal)) {
				result = ORIENTATION_FACE;
			}
		}
		else if (obedit->type == OB_MBALL) {
			MetaBall *mb = obedit->data;
			MetaElem *ml;
			bool ok = false;
			float tmat[3][3];
			
			if (activeOnly && (ml = mb->lastelem)) {
				quat_to_mat3(tmat, ml->quat);
				add_v3_v3(normal, tmat[2]);
				add_v3_v3(plane, tmat[1]);
				ok = true;
			}
			else {
				for (ml = mb->editelems->first; ml; ml = ml->next) {
					if (ml->flag & SELECT) {
						quat_to_mat3(tmat, ml->quat);
						add_v3_v3(normal, tmat[2]);
						add_v3_v3(plane, tmat[1]);
						ok = true;
					}
				}
			}

			if (ok) {
				if (!is_zero_v3(plane)) {
					result = ORIENTATION_FACE;
				}
			}
		}
		else if (obedit->type == OB_ARMATURE) {
			bArmature *arm = obedit->data;
			EditBone *ebone;
			bool ok = false;
			float tmat[3][3];

			if (activeOnly && (ebone = arm->act_edbone)) {
				ED_armature_ebone_to_mat3(ebone, tmat);
				add_v3_v3(normal, tmat[2]);
				add_v3_v3(plane, tmat[1]);
				ok = true;
			}
			else {
				for (ebone = arm->edbo->first; ebone; ebone = ebone->next) {
					if (arm->layer & ebone->layer) {
						if (ebone->flag & BONE_SELECTED) {
							ED_armature_ebone_to_mat3(ebone, tmat);
							add_v3_v3(normal, tmat[2]);
							add_v3_v3(plane, tmat[1]);
							ok = true;
						}
					}
				}
			}
			
			if (ok) {
				if (!is_zero_v3(plane)) {
					result = ORIENTATION_EDGE;
				}
			}
		}

		/* Vectors from edges don't need the special transpose inverse multiplication */
		if (result == ORIENTATION_EDGE) {
			float tvec[3];

			mul_mat3_m4_v3(ob->obmat, normal);
			mul_mat3_m4_v3(ob->obmat, plane);

			/* align normal to edge direction (so normal is perpendicular to the plane).
			 * 'ORIENTATION_EDGE' will do the other way around.
			 * This has to be done **after** applying obmat, see T45775! */
			project_v3_v3v3(tvec, normal, plane);
			sub_v3_v3(normal, tvec);
		}
		else {
			mul_m3_v3(mat, normal);
			mul_m3_v3(mat, plane);
		}
	}
	else if (ob && (ob->mode & OB_MODE_POSE)) {
		bArmature *arm = ob->data;
		bPoseChannel *pchan;
		float imat[3][3], mat[3][3];
		bool ok = false;

		if (activeOnly && (pchan = BKE_pose_channel_active(ob))) {
			add_v3_v3(normal, pchan->pose_mat[2]);
			add_v3_v3(plane, pchan->pose_mat[1]);
			ok = true;
		}
		else {
			int totsel;

			totsel = count_bone_select(arm, &arm->bonebase, true);
			if (totsel) {
				/* use channels to get stats */
				for (pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
					if (pchan->bone && pchan->bone->flag & BONE_TRANSFORM) {
						add_v3_v3(normal, pchan->pose_mat[2]);
						add_v3_v3(plane, pchan->pose_mat[1]);
					}
				}
				ok = true;
			}
		}

		/* use for both active & all */
		if (ok) {
			/* we need the transpose of the inverse for a normal... */
			copy_m3_m4(imat, ob->obmat);
			
			invert_m3_m3(mat, imat);
			transpose_m3(mat);
			mul_m3_v3(mat, normal);
			mul_m3_v3(mat, plane);
			
			result = ORIENTATION_EDGE;
		}
	}
	else if (ob && (ob->mode & (OB_MODE_ALL_PAINT | OB_MODE_PARTICLE_EDIT))) {
		/* pass */
	}
	else {
		/* we need the one selected object, if its not active */
		View3D *v3d = CTX_wm_view3d(C);
		ob = OBACT;
		if (ob && (ob->flag & SELECT)) {
			/* pass */
		}
		else {
			/* first selected */
			ob = NULL;
			for (base = scene->base.first; base; base = base->next) {
				if (TESTBASELIB(v3d, base)) {
					ob = base->object;
					break;
				}
			}
		}
		
		if (ob) {
			copy_v3_v3(normal, ob->obmat[2]);
			copy_v3_v3(plane, ob->obmat[1]);
		}
		result = ORIENTATION_NORMAL;
	}
	
	return result;
}

int getLocalTransformOrientation(const bContext *C, float normal[3], float plane[3])
{
	/* dummy value, not V3D_AROUND_ACTIVE and not V3D_AROUND_LOCAL_ORIGINS */
	short around = V3D_AROUND_CENTER_BOUNDS;

	return getLocalTransformOrientation_ex(C, normal, plane, around);
}

void ED_getLocalTransformOrientationMatrix(const bContext *C, float orientation_mat[3][3], const short around)
{
	float normal[3] = {0.0, 0.0, 0.0};
	float plane[3] = {0.0, 0.0, 0.0};

	int type;

	type = getLocalTransformOrientation_ex(C, normal, plane, around);

	switch (type) {
		case ORIENTATION_NORMAL:
			if (createSpaceNormalTangent(orientation_mat, normal, plane) == 0) {
				type = ORIENTATION_NONE;
			}
			break;
		case ORIENTATION_VERT:
			if (createSpaceNormal(orientation_mat, normal) == 0) {
				type = ORIENTATION_NONE;
			}
			break;
		case ORIENTATION_EDGE:
			if (createSpaceNormalTangent(orientation_mat, normal, plane) == 0) {
				type = ORIENTATION_NONE;
			}
			break;
		case ORIENTATION_FACE:
			if (createSpaceNormalTangent(orientation_mat, normal, plane) == 0) {
				type = ORIENTATION_NONE;
			}
			break;
	}

	if (type == ORIENTATION_NONE) {
		unit_m3(orientation_mat);
	}
}

/**
 * Get (or calculate if needed) the rotation matrix for \a orientation_type.
 */
void ED_getTransformOrientationMatrix(
        const bContext *C, const char orientation_type, const short around,
        float r_orientation_mat[3][3])
{
	Object *ob = CTX_data_active_object(C);
	Object *obedit = CTX_data_active_object(C);
	float mat[3][3];

	BLI_assert(CTX_wm_area(C)->spacetype == SPACE_VIEW3D &&
	           CTX_wm_region(C)->regiontype == RGN_TYPE_WINDOW);

	unit_m3(mat);

	switch (orientation_type) {
		case V3D_TRANS_ORIENTATION_GLOBAL:
			/* use unit matrix */
			break;
		case V3D_TRANS_ORIENTATION_GIMBAL:
			if (gimbal_axis(ob, mat)) {
				break;
			}
			/* No break, fall-through here! If not gimbal, fall through to normal. */
		case V3D_TRANS_ORIENTATION_NORMAL:
			if (obedit || (ob && ob->mode & OB_MODE_POSE)) {
				ED_getLocalTransformOrientationMatrix(C, mat, around);
				break;
			}
			/* No break, fall-trough here! We define 'normal' as 'local' in Object mode. */
		case V3D_TRANS_ORIENTATION_LOCAL:
			if (ob && ob->mode & OB_MODE_POSE) {
				/* each bone moves on its own local axis, but  to avoid confusion,
				 * use the active pones axis for display [#33575], this works as expected on a single bone
				 * and users who select many bones will understand whats going on and what local means
				 * when they start transforming */
				ED_getLocalTransformOrientationMatrix(C, mat, around);
				break;
			}
			else if (ob) {
				copy_m3_m4(mat, ob->obmat);
				normalize_m3(mat);
			}
			break;
		case V3D_TRANS_ORIENTATION_VIEW:
		{
			RegionView3D *rv3d = CTX_wm_region_view3d(C);
			copy_m3_m4(mat, rv3d->viewinv);
			normalize_m3(mat);
			break;
		}
		default: /* custom (V3D_MANIP_CUSTOM and higher) */
			applyTransformOrientation(C, mat, NULL, orientation_type - V3D_TRANS_ORIENTATION_CUSTOM);
			break;
	}

	copy_m3_m3(r_orientation_mat, mat);
}
