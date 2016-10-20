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
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/space_view3d/view3d_transform_manipulators.c
 *  \ingroup spview3d
 */


#include "BLI_math.h"

#include "BKE_action.h"
#include "BKE_context.h"

#include "DNA_armature_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"

#include "ED_armature.h"
#include "ED_transform.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"

#include "UI_resources.h"

#include "view3d_intern.h"

#include "WM_api.h"
#include "WM_types.h"


/* axes as index */
enum TransformAxisType {
	MAN_AXIS_TRANS_X = 0,
	MAN_AXIS_TRANS_Y,
	MAN_AXIS_TRANS_Z,
	MAN_AXIS_TRANS_C,

	MAN_AXIS_ROT_X,
	MAN_AXIS_ROT_Y,
	MAN_AXIS_ROT_Z,
	MAN_AXIS_ROT_C,
	MAN_AXIS_ROT_T, /* trackball rotation */

	MAN_AXIS_SCALE_X,
	MAN_AXIS_SCALE_Y,
	MAN_AXIS_SCALE_Z,
	MAN_AXIS_SCALE_C,

	/* special */
	MAN_AXIS_TRANS_XY,
	MAN_AXIS_TRANS_YZ,
	MAN_AXIS_TRANS_ZX,

	MAN_AXIS_SCALE_XY,
	MAN_AXIS_SCALE_YZ,
	MAN_AXIS_SCALE_ZX,

	MAN_AXIS_LAST,
};

/* threshold for testing view aligned axis manipulator */
#define TRANSFORM_MAN_AXIS_DOT_MIN 0.02f
#define TRANSFORM_MAN_AXIS_DOT_MAX 0.1f

#define TRANSFORM_MAN_AXIS_LINE_WIDTH 2.0f

/**
 * Struct for carrying data of transform manipulators as wmManipulatorGroup.customdata.
 */
typedef struct TransformManipulatorsInfo {
	struct TranformAxisManipulator *axes; /* Array of axes */

	wmManipulatorGroup *mgroup;
	float mat[4][4]; /* Cached loc/rot matrix */
} TransformManipulatorsInfo;

typedef wmManipulator *TransformManipulatorInitFunc(const TransformManipulatorsInfo *,
                                                    struct TranformAxisManipulator *);
typedef void TransformManipulatorUpdateFunc(const bContext *, const TransformManipulatorsInfo *,
                                            const struct TranformAxisManipulator *);

typedef struct TranformAxisManipulator {
	/* -- initialized using static array -- */

	enum TransformAxisType index;
	int transform_type; /* View3d->twtype */

	/* per-manipulator callbacks for initializing/updating data */
	TransformManipulatorInitFunc   (*init);
	TransformManipulatorUpdateFunc (*refresh);
	TransformManipulatorUpdateFunc (*draw_prepare);

	const char *name;
	int constraint[3]; /* {x, y, z} */
	int protectflag; /* the protectflags this axis checks (e.g. OB_LOCK_LOCZ) */

	/* appearance */
	float scale;
	float line_width;
	int theme_colorid;
	int manipulator_style;


	/* -- initialized later -- */

	wmManipulator *manipulator;
} TranformAxisManipulator;

static TransformManipulatorInitFunc   manipulator_arrow_init;
static TransformManipulatorUpdateFunc manipulator_arrow_draw_prepare;
static TransformManipulatorInitFunc   manipulator_dial_init;
static TransformManipulatorUpdateFunc manipulator_dial_refresh;
static TransformManipulatorUpdateFunc manipulator_view_dial_draw_prepare;

/**
 * This TranformAxisManipulator array contains all the info we need to initialize, store and identify all
 * transform manipulators. When creating a new group instance we simply create an allocated version of this.
 *
 * \note Order matches drawing order! (TODO: How should drawing order and index work together?)
 */
static TranformAxisManipulator tman_axes[] = {
	{
		MAN_AXIS_TRANS_X, V3D_MANIP_TRANSLATE,
		manipulator_arrow_init, NULL, manipulator_arrow_draw_prepare,
		"translate_x", {1, 0, 0}, OB_LOCK_LOCX,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH, TH_AXIS_X, MANIPULATOR_ARROW_STYLE_NORMAL,
	},
	{
		MAN_AXIS_TRANS_Y, V3D_MANIP_TRANSLATE,
		manipulator_arrow_init, NULL, manipulator_arrow_draw_prepare,
		"translate_y", {0, 1, 0}, OB_LOCK_LOCY,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH, TH_AXIS_Y, MANIPULATOR_ARROW_STYLE_NORMAL,
	},
	{
		MAN_AXIS_TRANS_Z, V3D_MANIP_TRANSLATE,
		manipulator_arrow_init, NULL, manipulator_arrow_draw_prepare,
		"translate_z", {0, 0, 1}, OB_LOCK_LOCZ,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH, TH_AXIS_Z, MANIPULATOR_ARROW_STYLE_NORMAL,
	},
	{
		MAN_AXIS_TRANS_C, V3D_MANIP_TRANSLATE,
		manipulator_dial_init, manipulator_dial_refresh, manipulator_view_dial_draw_prepare,
		"translate_c", {0}, 0,
		0.2f, TRANSFORM_MAN_AXIS_LINE_WIDTH, -1, MANIPULATOR_DIAL_STYLE_RING,
	},
	{
		MAN_AXIS_ROT_X, V3D_MANIP_ROTATE,
		manipulator_dial_init, manipulator_dial_refresh, NULL,
		"rotate_x", {1, 0, 0}, OB_LOCK_ROTX,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH + 1.0f, TH_AXIS_X, MANIPULATOR_DIAL_STYLE_RING_CLIPPED,
	},
	{
		MAN_AXIS_ROT_Y, V3D_MANIP_ROTATE,
		manipulator_dial_init, manipulator_dial_refresh, NULL,
		"rotate_y", {0, 1, 0}, OB_LOCK_ROTY,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH + 1.0f, TH_AXIS_Y, MANIPULATOR_DIAL_STYLE_RING_CLIPPED,
	},
	{
		MAN_AXIS_ROT_Z, V3D_MANIP_ROTATE,
		manipulator_dial_init, manipulator_dial_refresh, NULL,
		"rotate_y", {0, 0, 1}, OB_LOCK_ROTZ,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH + 1.0f, TH_AXIS_Z, MANIPULATOR_DIAL_STYLE_RING_CLIPPED,
	},
	{
		MAN_AXIS_ROT_C, V3D_MANIP_ROTATE,
		manipulator_dial_init, NULL, manipulator_view_dial_draw_prepare,
		"rotate_c", {0}, OB_LOCK_ROTZ,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH + 1.0f, -1, MANIPULATOR_DIAL_STYLE_RING,
	},
	{0, 0, NULL}
};


/* -------------------------------------------------------------------- */
/* General helpers */

static void transform_manipulators_info_free(void *customdata)
{
	TransformManipulatorsInfo *info = customdata;

	MEM_freeN(info->axes);
	MEM_freeN(info);
}

static unsigned int transform_axis_index_normalize(const int axis_idx)
{
	if (axis_idx > MAN_AXIS_TRANS_ZX) {
		return axis_idx - 16;
	}
	else if (axis_idx > MAN_AXIS_SCALE_C) {
		return axis_idx - 13;
	}
	else if (axis_idx > MAN_AXIS_ROT_T) {
		return axis_idx - 9;
	}
	else if (axis_idx > MAN_AXIS_TRANS_C) {
		return axis_idx - 4;
	}

	return axis_idx;
}


/* -------------------------------------------------------------------- */
/* init callback and helpers */


static wmManipulator *manipulator_arrow_init(const TransformManipulatorsInfo *info, TranformAxisManipulator *axis)
{
	return WM_arrow_manipulator_new(info->mgroup, axis->name);
}

static wmManipulator *manipulator_dial_init(const TransformManipulatorsInfo *info, TranformAxisManipulator *axis)
{
	return WM_dial_manipulator_new(info->mgroup, axis->name, axis->manipulator_style);
}

/**
 * Custom handler for transform manipulators to update them while modal transform operator runs.
 */
static int transform_axis_manipulator_handler(
        bContext *C, const wmEvent *UNUSED(event), wmManipulator *widget, const int UNUSED(flag))
{
	View3D *v3d = CTX_wm_view3d(C);
	float origin[3];

	/* update origin */
	if (calculateTransformCenter(C, v3d->around, origin, NULL)) {
		WM_manipulator_set_origin(widget, origin);
	}

	return OPERATOR_PASS_THROUGH;
}

static const char *transform_axis_ot_name_get(int transform_type)
{
	const char *name = NULL;

	switch (transform_type) {
		case V3D_MANIP_TRANSLATE:
			name = "TRANSFORM_OT_translate";
			break;
		case V3D_MANIP_ROTATE:
			name = "TRANSFORM_OT_rotate";
			break;
		case V3D_MANIP_SCALE:
			name = "TRANSFORM_OT_scale";
			break;
		default:
			BLI_assert(0);
			break;
	}

	return name;
}

/**
 * Create and initialize a manipulator for \a axis.
 */
static void transform_axis_manipulator_init(TransformManipulatorsInfo *info, TranformAxisManipulator *axis)
{
	axis->manipulator = axis->init(info, axis);

	const char *op_name = transform_axis_ot_name_get(axis->transform_type);
	PointerRNA *ptr = WM_manipulator_set_operator(axis->manipulator, op_name);
	WM_manipulator_set_custom_handler(axis->manipulator, transform_axis_manipulator_handler);
	WM_manipulator_set_scale(axis->manipulator, axis->scale);
	WM_manipulator_set_line_width(axis->manipulator, axis->line_width);

	if (RNA_struct_find_property(ptr, "constraint_axis")) {
		RNA_boolean_set_array(ptr, "constraint_axis", axis->constraint);
	}
	RNA_boolean_set(ptr, "release_confirm", 1);
}

static void transform_manipulatorgroup_init(const bContext *UNUSED(C), wmManipulatorGroup *mgroup)
{
	TransformManipulatorsInfo *info = MEM_callocN(sizeof(*info), __func__);

	info->mgroup = mgroup;
	info->axes = MEM_callocN(sizeof(tman_axes), STRINGIFY(TranformAxisManipulator));
	memcpy(info->axes, tman_axes, sizeof(tman_axes));

	for (int i = 0; i < MAN_AXIS_LAST && info->axes[i].name; i++) {
		transform_axis_manipulator_init(info, &info->axes[i]);
	}

	mgroup->customdata = info;
	mgroup->customdata_free = transform_manipulators_info_free;
}


/* -------------------------------------------------------------------- */
/* refresh callback and helpers */


static void manipulator_dial_refresh(
        const bContext *UNUSED(C), const TransformManipulatorsInfo *info, const TranformAxisManipulator *axis)
{
	const int idx_normalized = transform_axis_index_normalize(axis->index);
	WM_dial_manipulator_set_up_vector(axis->manipulator, info->mat[idx_normalized]);
}

static bool transform_axis_manipulator_is_visible(TranformAxisManipulator *axis, char transform_type, int protectflag)
{
	return ((axis->transform_type & transform_type) &&
	        (!axis->protectflag || (axis->protectflag & protectflag) != axis->protectflag));
}

static int transform_manipulators_protectflag_posemode_get(Object *ob, View3D *v3d)
{
	bPoseChannel *pchan;
	int protectflag = 0;

	if ((v3d->around == V3D_AROUND_ACTIVE) && (pchan = BKE_pose_channel_active(ob))) {
		if (pchan->bone) {
			protectflag = pchan->protectflag;
		}
	}
	else {
		/* use channels to get stats */
		for (pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
			Bone *bone = pchan->bone;
			if (bone && (bone->flag & BONE_TRANSFORM)) {
				protectflag |= pchan->protectflag;
			}
		}
	}

	return protectflag;
}

static int transform_manipulators_protectflag_editmode_get(Object *obedit, View3D *v3d)
{
	int protectflag = 0;

	if (obedit->type == OB_ARMATURE) {
		const bArmature *arm = obedit->data;
		EditBone *ebo;
		if ((v3d->around == V3D_AROUND_ACTIVE) && (ebo = arm->act_edbone)) {
			if (ebo->flag & BONE_EDITMODE_LOCKED) {
				protectflag = (OB_LOCK_LOC | OB_LOCK_ROT | OB_LOCK_SCALE);
			}
		}
		else {
			for (ebo = arm->edbo->first; ebo; ebo = ebo->next) {
				if (EBONE_VISIBLE(arm, ebo)) {
					if ((ebo->flag & BONE_SELECTED) && (ebo->flag & BONE_EDITMODE_LOCKED)) {
						protectflag = (OB_LOCK_LOC | OB_LOCK_ROT | OB_LOCK_SCALE);
						break;
					}
				}
			}
		}
	}

	return protectflag;
}

static int transform_manipulators_protectflag_objectmode_get(const Scene *scene, const View3D *v3d)
{
	int protectflag = 0;

	for (Base *base = scene->base.first; base; base = base->next) {
		if (TESTBASELIB(v3d, base)) {
			protectflag |= base->object->protectflag;
		}
	}

	return protectflag;
}

static int transform_manipulators_protectflag_get(const bContext *C, View3D *v3d)
{
	Scene *scene = CTX_data_scene(C);
	Object *ob = OBACT, *obedit = CTX_data_edit_object(C);
	bGPdata *gpd = CTX_data_gpencil_data(C);
	const bool is_gp_edit = (gpd && (gpd->flag & GP_DATA_STROKE_EDITMODE));
	int protectflag = 0;

	if (is_gp_edit) {
		/* pass */
	}
	else if (obedit) {
		protectflag = transform_manipulators_protectflag_editmode_get(obedit, v3d);
	}
	else if (ob && (ob->mode & OB_MODE_POSE)) {
		protectflag = transform_manipulators_protectflag_posemode_get(ob, v3d);
	}
	else if (ob && (ob->mode & OB_MODE_ALL_PAINT)) {
		/* pass */
	}
	else {
		protectflag = transform_manipulators_protectflag_objectmode_get(scene, v3d);
	}

	return protectflag;
}

/**
 * Get (or calculate if needed) location and rotation for the transform manipulators as
 * transformation matrix. This may iterate over entire selection so avoid as many calls as possible!
 *
 * \return If valid matrix has been created. This is not the case if no selection was found.
 */
static bool transform_manipulators_matrix_get(const bContext *C, const View3D *v3d, float r_mat[4][4])
{
	float origin[3];
	float rot[3][3];

	if (calculateTransformCenter((bContext *)C, v3d->around, origin, NULL)) {
		ED_getTransformOrientationMatrix(C, v3d->twmode, v3d->around, rot);

		copy_m4_m3(r_mat, rot);
		copy_v3_v3(r_mat[3], origin);

		return true;
	}

	return false;
}


/**
 * Performs some additional layer checks, #calculateTransformCenter does the rest of them.
 */
static bool transform_manipulators_layer_visible(const bContext *C, const View3D *v3d)
{
	Scene *scene = CTX_data_scene(C);
	Object *ob = OBACT;
	Object *obedit = CTX_data_edit_object(C);
	bGPdata *gpd = CTX_data_gpencil_data(C);
	const bool is_gp_edit = ((gpd) && (gpd->flag & GP_DATA_STROKE_EDITMODE));

	if (is_gp_edit) {
		/* TODO */
	}
	else if (obedit) {
		return (obedit->lay & v3d->lay) != 0;
	}
	else if (ob && (ob->mode & OB_MODE_POSE)) {
		return (ob->lay & v3d->lay) != 0;
	}

	return true;
}

static void transform_manipulatorgroup_refresh(const bContext *C, wmManipulatorGroup *mgroup)
{
	View3D *v3d = CTX_wm_view3d(C);
	TransformManipulatorsInfo *info = mgroup->customdata;

	float mat[4][4];
	const bool any_visible = transform_manipulators_layer_visible(C, v3d) &&
	                         transform_manipulators_matrix_get(C, v3d, mat);
	const int protectflag = transform_manipulators_protectflag_get(C, v3d);

	copy_m4_m4(info->mat, mat);

	TranformAxisManipulator *axis;
	for (int i = 0; i < MAN_AXIS_LAST && info->axes[i].name; i++) {
		axis = &info->axes[i];

		if (any_visible && transform_axis_manipulator_is_visible(axis, v3d->twtype, protectflag)) {
			WM_manipulator_set_flag(axis->manipulator, WM_MANIPULATOR_HIDDEN, false);
		}
		else {
			WM_manipulator_set_flag(axis->manipulator, WM_MANIPULATOR_HIDDEN, true);
			continue;
		}
		WM_manipulator_set_origin(axis->manipulator, mat[3]); /* Could do in callback, but we do it for all anyway */
		if (axis->refresh) {
			axis->refresh(C, info, axis);
		}
	}
}


/* -------------------------------------------------------------------- */
/* draw_prepare callback and helpers */


static float transform_axis_view_alpha_fac_get(
        const TranformAxisManipulator *axis, const RegionView3D *rv3d, const float mat[4][4])
{
	const int axis_idx_norm = transform_axis_index_normalize(axis->index);
	const float dot_min = TRANSFORM_MAN_AXIS_DOT_MIN;
	const float dot_max = TRANSFORM_MAN_AXIS_DOT_MAX;
	float view_vec[3], axis_vec[3];
	float idot[3], idot_axis;

	ED_view3d_global_to_vector(rv3d, mat[3], view_vec);

	for (int i = 0; i < 3; i++) {
		normalize_v3_v3(axis_vec, mat[i]);
		idot[i] = 1.0f - fabsf(dot_v3v3(view_vec, axis_vec));
	}
	idot_axis = idot[axis_idx_norm];

	return ((idot_axis > dot_max) ?
	        1.0f : (idot_axis < dot_min) ?
	        0.0f : ((idot_axis - dot_min) / (dot_max - dot_min)));
}

static void transform_axis_manipulator_set_color(
        const TranformAxisManipulator *axis, const RegionView3D *rv3d, const float mat[4][4])
{
	/* alpha values for normal/highlighted states */
	const float alpha = 0.6f;
	const float alpha_hi = 1.0f;
	float alpha_fac = 1.0f;
	float col[4], col_hi[4];

	if (axis->theme_colorid == -1) {
		copy_v4_fl(col, 1.0f);
	}
	else {
		UI_GetThemeColor4fv(axis->theme_colorid, col);
		alpha_fac = transform_axis_view_alpha_fac_get(axis, rv3d, mat);
	}
	copy_v4_v4(col_hi, col);

	col[3] = alpha * alpha_fac;
	col_hi[3] = alpha_hi * alpha_fac;

	WM_manipulator_set_colors(axis->manipulator, col, col_hi);
}

static void manipulator_arrow_draw_prepare(
        const bContext *UNUSED(C), const TransformManipulatorsInfo *info, const TranformAxisManipulator *axis)
{
	const int axis_idx_norm = transform_axis_index_normalize(axis->index);

	WM_arrow_manipulator_set_direction(axis->manipulator, info->mat[axis_idx_norm]);
}

static void manipulator_view_dial_draw_prepare(
        const bContext *C, const TransformManipulatorsInfo *UNUSED(info), const TranformAxisManipulator *axis)
{
	RegionView3D *rv3d = CTX_wm_region_view3d(C);
	WM_dial_manipulator_set_up_vector(axis->manipulator, rv3d->viewinv[2]);
}

/**
 * Some transform orientation modes require updating the transform manipulators rotation matrix every redraw.
 * \return If manipulators need to update their rotation.
 */
static bool transform_manipulators_draw_rotmatrix_get(const bContext *C, const View3D *v3d, float r_rot[3][3])
{
	if (v3d->twmode == V3D_MANIP_VIEW) {
		ED_getTransformOrientationMatrix(C, v3d->twmode, v3d->around, r_rot);
		return true;
	}

	return false;
}

static void transform_manipulatorgroup_draw_prepare(const bContext *C, wmManipulatorGroup *mgroup)
{
	View3D *v3d = CTX_wm_view3d(C);
	RegionView3D *rv3d = CTX_wm_region_view3d(C);
	TransformManipulatorsInfo *info = mgroup->customdata;

	float rot[3][3];
	if (transform_manipulators_draw_rotmatrix_get(C, v3d, rot)) {
		copy_m4_m3(info->mat, rot);
	}

	for (int i = 0; i < MAN_AXIS_LAST && info->axes[i].name; i++) {
		if (info->axes[i].draw_prepare) {
			info->axes[i].draw_prepare(C, info, &info->axes[i]);
		}
		transform_axis_manipulator_set_color(&info->axes[i], rv3d, info->mat);
	}
}


/* -------------------------------------------------------------------- */

static bool transform_manipulatorgroup_poll(const bContext *C, wmManipulatorGroupType *UNUSED(mgt))
{
	/* it's a given we only use this in 3D view */
	const ScrArea *sa = CTX_wm_area(C);
	const View3D *v3d = sa->spacedata.first;
	const Object *ob = CTX_data_active_object(C);
	const Object *editob = CTX_data_edit_object(C);

	/* avoiding complex stuff here (like checking for selected vertices),
	 * this poll check runs on every redraw (and more) */
	return (((v3d->twflag & V3D_USE_MANIPULATOR) != 0) &&
	        ((v3d->twtype & (V3D_MANIP_TRANSLATE | V3D_MANIP_ROTATE | V3D_MANIP_SCALE)) != 0) &&
	        (ob || editob));
}

void VIEW3D_MGT_transform_manipulators(wmManipulatorGroupType *mgt)
{
	mgt->name = "Transform Manipulators";

	mgt->poll = transform_manipulatorgroup_poll;
	mgt->init = transform_manipulatorgroup_init;
	mgt->refresh = transform_manipulatorgroup_refresh;
	mgt->draw_prepare = transform_manipulatorgroup_draw_prepare;

	mgt->flag = (WM_MANIPULATORGROUPTYPE_IS_3D | WM_MANIPULATORGROUPTYPE_SCALE_3D);
}
