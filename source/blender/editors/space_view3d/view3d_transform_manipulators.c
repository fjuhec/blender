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


/* threshold for testing view aligned axis manipulator */
#define TRANSFORM_MAN_AXIS_DOT_MIN 0.02f
#define TRANSFORM_MAN_AXIS_DOT_MAX 0.1f

#define TRANSFORM_MAN_AXIS_LINE_WIDTH 2.0f

/**
 * Transform axis type that can be used as index.
 */
enum TransformAxisType {
	/* single axes */
	TRANSFORM_AXIS_X     = 0,
	TRANSFORM_AXIS_Y     = 1,
	TRANSFORM_AXIS_Z     = 2,

	/* mulitple axes */
	TRANSFORM_AXIS_VIEW  = 3,
	TRANSFORM_AXIS_XY    = 4,
	TRANSFORM_AXIS_YZ    = 5,
	TRANSFORM_AXIS_ZX    = 6,
};

/**
 * Struct for carrying data of transform manipulators as wmManipulatorGroup.customdata.
 */
typedef struct TransformManipulatorsInfo {
	struct TranformAxisManipulator *axes; /* Array of axes */

	float mat[4][4]; /* Cached loc/rot matrix */
} TransformManipulatorsInfo;

/* Callback types */
typedef wmManipulator *TransformManipulatorInitFunc(struct TranformAxisManipulator *, wmManipulatorGroup *mgroup);
typedef void TransformManipulatorUpdateFunc(
        const bContext *, const TransformManipulatorsInfo *, const struct TranformAxisManipulator *);

/**
 * Struct that allows us to store info for each transform axis manipulator in a rather generic way.
 */
typedef struct TranformAxisManipulator {
	/* -- initialized using static array -- */

	enum TransformAxisType axis_type;
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


	/* The manipulator that represents this axis */
	wmManipulator *manipulator;
} TranformAxisManipulator;


/* -------------------------------------------------------------------- */
/* Manipulator init/update callbacks */

static wmManipulator *manipulator_arrow_init(TranformAxisManipulator *axis, wmManipulatorGroup *mgroup)
{
	return WM_arrow_manipulator_new(mgroup, axis->name, axis->manipulator_style);
}

static wmManipulator *manipulator_dial_init(TranformAxisManipulator *axis, wmManipulatorGroup *mgroup)
{
	return WM_dial_manipulator_new(mgroup, axis->name, axis->manipulator_style);
}

/**
 * Sets up \a r_start and \a r_len to define arrow line range.
 * Needed to adjust line drawing for combined manipulator axis types.
 */
static void manipulator_line_range(
        const TranformAxisManipulator *axis, const View3D *v3d,
        float *r_start, float *r_len)
{
	const float ofs = 0.2f;

	*r_start = 0.2f;
	*r_len = 1.0f;

	switch (axis->transform_type) {
		case V3D_MANIP_TRANSLATE:
			if (v3d->twtype & V3D_MANIP_SCALE) {
				*r_start = *r_len - ofs + 0.075f;
			}
			if (v3d->twtype & V3D_MANIP_ROTATE) {
				*r_len += ofs;
			}
			break;
		case V3D_MANIP_SCALE:
			if (v3d->twtype & (V3D_MANIP_TRANSLATE | V3D_MANIP_ROTATE)) {
				*r_len -= ofs + 0.025f;
			}
			break;
	}

	*r_len -= *r_start;
}

static void manipulator_arrow_update_line_range(const View3D *v3d, const TranformAxisManipulator *axis)
{
	float start[3] = {0.0f};
	float len;

	manipulator_line_range(axis, v3d, &start[2], &len);
	WM_manipulator_set_offset(axis->manipulator, start);
	WM_arrow_manipulator_set_length(axis->manipulator, len);
}

static void manipulator_arrow_refresh(
        const bContext *C, const TransformManipulatorsInfo *UNUSED(info), const TranformAxisManipulator *axis)
{
	manipulator_arrow_update_line_range(CTX_wm_view3d(C), axis);
}

static void manipulator_dial_refresh(
        const bContext *UNUSED(C), const TransformManipulatorsInfo *info, const TranformAxisManipulator *axis)
{
	WM_dial_manipulator_set_up_vector(axis->manipulator, info->mat[axis->axis_type]);
}

static void manipulator_arrow_draw_prepare(
        const bContext *UNUSED(C), const TransformManipulatorsInfo *info, const TranformAxisManipulator *axis)
{
	WM_arrow_manipulator_set_direction(axis->manipulator, info->mat[axis->axis_type]);
}

static void manipulator_view_dial_draw_prepare(
        const bContext *C, const TransformManipulatorsInfo *UNUSED(info), const TranformAxisManipulator *axis)
{
	RegionView3D *rv3d = CTX_wm_region_view3d(C);
	WM_dial_manipulator_set_up_vector(axis->manipulator, rv3d->viewinv[2]);
}


/* -------------------------------------------------------------------- */
/* General helpers */

/**
 * This TranformAxisManipulator array contains all the info we need to initialize, store and identify all
 * transform manipulators. When creating a new group instance we simply create an allocated version of this.
 *
 * \note Order matches drawing order!
 */
static TranformAxisManipulator tman_axes[] = {
	{
		TRANSFORM_AXIS_X, V3D_MANIP_TRANSLATE,
		manipulator_arrow_init, manipulator_arrow_refresh, manipulator_arrow_draw_prepare,
		"translate_x", {1, 0, 0}, OB_LOCK_LOCX,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH, TH_AXIS_X, MANIPULATOR_ARROW_STYLE_CONE,
	},
	{
		TRANSFORM_AXIS_Y, V3D_MANIP_TRANSLATE,
		manipulator_arrow_init, manipulator_arrow_refresh, manipulator_arrow_draw_prepare,
		"translate_y", {0, 1, 0}, OB_LOCK_LOCY,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH, TH_AXIS_Y, MANIPULATOR_ARROW_STYLE_CONE,
	},
	{
		TRANSFORM_AXIS_Z, V3D_MANIP_TRANSLATE,
		manipulator_arrow_init, manipulator_arrow_refresh, manipulator_arrow_draw_prepare,
		"translate_z", {0, 0, 1}, OB_LOCK_LOCZ,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH, TH_AXIS_Z, MANIPULATOR_ARROW_STYLE_CONE,
	},
	{
		TRANSFORM_AXIS_VIEW, V3D_MANIP_TRANSLATE,
		manipulator_dial_init, manipulator_dial_refresh, manipulator_view_dial_draw_prepare,
		"translate_view", {0}, 0,
		0.2f, TRANSFORM_MAN_AXIS_LINE_WIDTH, -1, MANIPULATOR_DIAL_STYLE_RING,
	},
	{
		TRANSFORM_AXIS_X, V3D_MANIP_ROTATE,
		manipulator_dial_init, manipulator_dial_refresh, NULL,
		"rotate_x", {1, 0, 0}, OB_LOCK_ROTX,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH + 1.0f, TH_AXIS_X, MANIPULATOR_DIAL_STYLE_RING_CLIPPED,
	},
	{
		TRANSFORM_AXIS_Y, V3D_MANIP_ROTATE,
		manipulator_dial_init, manipulator_dial_refresh, NULL,
		"rotate_y", {0, 1, 0}, OB_LOCK_ROTY,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH + 1.0f, TH_AXIS_Y, MANIPULATOR_DIAL_STYLE_RING_CLIPPED,
	},
	{
		TRANSFORM_AXIS_Z, V3D_MANIP_ROTATE,
		manipulator_dial_init, manipulator_dial_refresh, NULL,
		"rotate_y", {0, 0, 1}, OB_LOCK_ROTZ,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH + 1.0f, TH_AXIS_Z, MANIPULATOR_DIAL_STYLE_RING_CLIPPED,
	},
	{
		TRANSFORM_AXIS_VIEW, V3D_MANIP_ROTATE,
		manipulator_dial_init, NULL, manipulator_view_dial_draw_prepare,
		"rotate_view", {0}, OB_LOCK_ROTZ,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH + 1.0f, -1, MANIPULATOR_DIAL_STYLE_RING,
	},
	{
		TRANSFORM_AXIS_X, V3D_MANIP_SCALE,
		manipulator_arrow_init, manipulator_arrow_refresh, manipulator_arrow_draw_prepare,
		"scale_x", {1, 0, 0}, OB_LOCK_SCALEX,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH, TH_AXIS_X, MANIPULATOR_ARROW_STYLE_CUBE,
	},
	{
		TRANSFORM_AXIS_Y, V3D_MANIP_SCALE,
		manipulator_arrow_init, manipulator_arrow_refresh, manipulator_arrow_draw_prepare,
		"scale_y", {0, 1, 0}, OB_LOCK_SCALEY,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH, TH_AXIS_Y, MANIPULATOR_ARROW_STYLE_CUBE,
	},
	{
		TRANSFORM_AXIS_Z, V3D_MANIP_SCALE,
		manipulator_arrow_init, manipulator_arrow_refresh, manipulator_arrow_draw_prepare,
		"scale_y", {0, 0, 1}, OB_LOCK_SCALEZ,
		1.0f, TRANSFORM_MAN_AXIS_LINE_WIDTH, TH_AXIS_Z, MANIPULATOR_ARROW_STYLE_CUBE,
	},
	{
		TRANSFORM_AXIS_VIEW, V3D_MANIP_SCALE,
		manipulator_dial_init, manipulator_dial_refresh, manipulator_view_dial_draw_prepare,
		"scale_view", {0}, 0,
		0.2f, TRANSFORM_MAN_AXIS_LINE_WIDTH, -1, MANIPULATOR_DIAL_STYLE_RING,
	},
	{0, 0, NULL}
};

static void transform_manipulators_info_init(TransformManipulatorsInfo *info)
{
	info->axes = MEM_callocN(sizeof(tman_axes), STRINGIFY(TranformAxisManipulator));
	memcpy(info->axes, tman_axes, sizeof(tman_axes));
}

static void transform_manipulators_info_free(void *customdata)
{
	TransformManipulatorsInfo *info = customdata;

	MEM_freeN(info->axes);
	MEM_freeN(info);
}


/* -------------------------------------------------------------------- */
/* init callback and helpers */

/**
 * Custom handler for transform manipulators to update them while modal transform operator runs.
 */
static int transform_axis_manipulator_handler(
        bContext *C, const wmEvent *UNUSED(event), wmManipulator *widget, const int UNUSED(flag))
{
	View3D *v3d = CTX_wm_view3d(C);
	float origin[3];

	/* update origin */
	if (ED_calculateTransformCenter(C, v3d->around, origin, NULL)) {
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
			name = "TRANSFORM_OT_resize";
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
static void transform_axis_manipulator_init(TranformAxisManipulator *axis, wmManipulatorGroup *mgroup)
{
	axis->manipulator = axis->init(axis, mgroup);

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
	transform_manipulators_info_init(info);

	for (int i = 0; info->axes[i].name; i++) {
		transform_axis_manipulator_init(&info->axes[i], mgroup);
	}

	mgroup->customdata = info;
	mgroup->customdata_free = transform_manipulators_info_free;
}


/* -------------------------------------------------------------------- */
/* refresh callback and helpers */

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

	if (ED_calculateTransformCenter((bContext *)C, v3d->around, origin, NULL)) {
		ED_getTransformOrientationMatrix(C, v3d->twmode, v3d->around, rot);

		copy_m4_m3(r_mat, rot);
		copy_v3_v3(r_mat[3], origin);

		return true;
	}

	return false;
}


/**
 * Performs some additional layer checks, #ED_calculateTransformCenter does the rest of them.
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

	for (int i = 0; info->axes[i].name; i++) {
		TranformAxisManipulator *axis = &info->axes[i];

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
	const float dot_min = TRANSFORM_MAN_AXIS_DOT_MIN;
	const float dot_max = TRANSFORM_MAN_AXIS_DOT_MAX;
	float view_vec[3], axis_vec[3];
	float idot[3], idot_axis;

	ED_view3d_global_to_vector(rv3d, mat[3], view_vec);

	for (int i = 0; i < 3; i++) {
		normalize_v3_v3(axis_vec, mat[i]);
		idot[i] = 1.0f - fabsf(dot_v3v3(view_vec, axis_vec));
	}
	idot_axis = idot[axis->axis_type];

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

	for (int i = 0; info->axes[i].name; i++) {
		if (info->axes[i].draw_prepare) {
			info->axes[i].draw_prepare(C, info, &info->axes[i]);
		}
		transform_axis_manipulator_set_color(&info->axes[i], rv3d, info->mat);
	}
}


/* -------------------------------------------------------------------- */

static bool transform_manipulatorgroup_poll(const bContext *C, wmManipulatorGroupType *UNUSED(mgt))
{
	const View3D *v3d = CTX_wm_view3d(C);
	const Object *ob = CTX_data_active_object(C);
	const Object *editob = CTX_data_edit_object(C);

	BLI_assert(v3d != NULL);

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
