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

#include "BKE_context.h"

#include "BLI_math.h"

#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"

#include "ED_transform.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"

#include "UI_resources.h"

#include "view3d_intern.h"

#include "WM_api.h"
#include "WM_types.h"


/* axes as index */
enum {
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

/* axis types */
enum {
	MAN_AXES_ALL = 0,
	MAN_AXES_TRANSLATE,
	MAN_AXES_ROTATE,
	MAN_AXES_SCALE,
};

enum {
	MAN_CONSTRAINT_X = (1 << 0),
	MAN_CONSTRAINT_Y = (1 << 1),
	MAN_CONSTRAINT_Z = (1 << 2),
};

typedef struct TranformAxisManipulator {
	/* -- initialized using static array -- */

	int index, type;

	const char *name;
	/* op info */
	const char *op_name;
	int constraint[3]; /* {x, y, z} */

	/* appearance */
	int theme_colorid;
	int manipulator_type;


	/* -- initialized later -- */

	wmManipulator *manipulator;
} TranformAxisManipulator;

/**
 * This TranformAxisManipulator array contains all the info we need to initialize, store and identify all
 * transform manipulators. When creating a new group instance we simply create an allocated version of this.
 *
 * \note Order matches drawing order! (TODO: How should drawing order and index work together?)
 */
static TranformAxisManipulator tman_axes[] = {
	{
		MAN_AXIS_TRANS_X, MAN_AXES_TRANSLATE,
		"translate_x", "TRANSFORM_OT_translate", {1, 0, 0},
		TH_AXIS_X, MANIPULATOR_ARROW_STYLE_NORMAL,
	},
	{
		MAN_AXIS_TRANS_Y, MAN_AXES_TRANSLATE,
		"translate_y", "TRANSFORM_OT_translate", {0, 1, 0},
		TH_AXIS_Y, MANIPULATOR_ARROW_STYLE_NORMAL,
	},
	{
		MAN_AXIS_TRANS_Z, MAN_AXES_TRANSLATE,
		"translate_z", "TRANSFORM_OT_translate", {0, 0, 1},
		TH_AXIS_Z, MANIPULATOR_ARROW_STYLE_NORMAL,
	},
	{0, 0, NULL}
};


/* -------------------------------------------------------------------- */
/* init callback and helpers */

/**
 * Create and initialize a manipulator for \a axis.
 */
static void transform_axis_manipulator_init(TranformAxisManipulator *axis, wmManipulatorGroup *mgroup)
{
	switch (axis->manipulator_type) {
		case MANIPULATOR_ARROW_STYLE_NORMAL:
			axis->manipulator = WM_arrow_manipulator_new(mgroup, axis->name);
			break;
		default:
			BLI_assert(0);
			break;
	}

	PointerRNA *ptr = WM_manipulator_set_operator(axis->manipulator, axis->op_name);

	if (RNA_struct_find_property(ptr, "constraint_axis")) {
		RNA_boolean_set_array(ptr, "constraint_axis", axis->constraint);
	}
	RNA_boolean_set(ptr, "release_confirm", 1);
}

static void transform_manipulatorgroup_init(const bContext *UNUSED(C), wmManipulatorGroup *mgroup)
{
	TranformAxisManipulator *axes = MEM_callocN(sizeof(tman_axes), __func__);

	memcpy(axes, tman_axes, sizeof(tman_axes));

	for (int i = 0; i < MAN_AXIS_LAST && axes[i].name; i++) {
		transform_axis_manipulator_init(&axes[i], mgroup);
	}

	mgroup->customdata = axes;
}


/* -------------------------------------------------------------------- */
/* refresh callback and helpers */

static void transform_axis_manipulator_set_color(TranformAxisManipulator *axis)
{
	/* alpha values for normal/highlighted states */
	const float alpha = 0.6f;
	const float alpha_hi = 1.0f;
	float col[4], col_hi[4];

	UI_GetThemeColor4fv(axis->theme_colorid, col);
	copy_v4_v4(col_hi, col);

	col[3] = alpha;
	col_hi[3] = alpha_hi;

	WM_manipulator_set_colors(axis->manipulator, col, col_hi);
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

static void transform_manipulatorgroup_refresh(const bContext *C, wmManipulatorGroup *mgroup)
{
	View3D *v3d = CTX_wm_view3d(C);
	TranformAxisManipulator *axes = mgroup->customdata;

	float mat[4][4];
	const bool visible = transform_manipulators_matrix_get(C, v3d, mat);

	for (int i = 0; i < MAN_AXIS_LAST && axes[i].name; i++) {
		if (visible) {
			WM_manipulator_set_flag(axes[i].manipulator, WM_MANIPULATOR_HIDDEN, false);
		}
		else {
			WM_manipulator_set_flag(axes[i].manipulator, WM_MANIPULATOR_HIDDEN, true);
			continue;
		}
		WM_arrow_manipulator_set_direction(axes[i].manipulator, mat[i]);
		WM_manipulator_set_origin(axes[i].manipulator, mat[3]);
		transform_axis_manipulator_set_color(&axes[i]);
	}
}


/* -------------------------------------------------------------------- */
/* draw_prepare callback and helpers */

/**
 * Some transform orientation modes require updating the transform manipulators rotation matrix every redraw.
 * \return If manipulators need to update their rotation.
 */
static bool transform_manipulators_draw_rotmatrix_get(const bContext *C, const View3D *v3d, float r_mat[3][3])
{
	if (v3d->twmode == V3D_MANIP_VIEW) {
		ED_getTransformOrientationMatrix(C, v3d->twmode, v3d->around, r_mat);
		return true;
	}

	return false;
}

static void transform_manipulatorgroup_draw_prepare(const bContext *C, wmManipulatorGroup *mgroup)
{
	View3D *v3d = CTX_wm_view3d(C);
	TranformAxisManipulator *axes = mgroup->customdata;

	float rot[3][3];
	const bool update_rot = transform_manipulators_draw_rotmatrix_get(C, v3d, rot);

	for (int i = 0; i < MAN_AXIS_LAST && axes[i].name; i++) {
		if (update_rot) {
			WM_arrow_manipulator_set_direction(axes[i].manipulator, rot[i]);
		}
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
