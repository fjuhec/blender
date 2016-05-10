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
 * The Original Code is Copyright (C) 2016 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/transform/transform_manipulator2d.c
 *  \ingroup edtransform
 *
 * \name 2D Transform Manipulator
 *
 * Used for UV/Image Editor
 */

#include "BKE_context.h"
#include "BKE_editmesh.h"

#include "BLI_listbase.h"
#include "BLI_math.h"

#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"
#include "DNA_widget_types.h"

#include "ED_image.h"
#include "ED_screen.h"
#include "ED_uvedit.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"

#include "UI_resources.h"
#include "UI_view2d.h"

#include "WM_api.h"
#include "WM_types.h"
#include "wm.h" /* XXX */

#include "transform.h" /* own include */

/* axes as index */
enum {
	MAN2D_AXIS_TRANS_X = 0,
	MAN2D_AXIS_TRANS_Y,

	MAN2D_AXIS_LAST,
};

typedef struct ManipulatorGroup2D {
	wmWidget *translate_x,
	         *translate_y;

	/* Current origin in view space, used to update widget origin for possible view changes */
	float origin[2];
} ManipulatorGroup2D;


/* **************** Utilities **************** */

/* loop over axes */
#define MAN2D_ITER_AXES_BEGIN(axis, axis_idx) \
	{ \
		wmWidget *axis; \
		int axis_idx; \
		for (axis_idx = 0; axis_idx < MAN2D_AXIS_LAST; axis_idx++) { \
			axis = manipulator2d_get_axis_from_index(man, axis_idx);

#define MAN2D_ITER_AXES_END \
		} \
	} ((void)0)

static wmWidget *manipulator2d_get_axis_from_index(const ManipulatorGroup2D *man, const short axis_idx)
{
	BLI_assert(IN_RANGE_INCL(axis_idx, (float)MAN2D_AXIS_TRANS_X, (float)MAN2D_AXIS_TRANS_Y));

	switch (axis_idx) {
		case MAN2D_AXIS_TRANS_X:
			return man->translate_x;
		case MAN2D_AXIS_TRANS_Y:
			return man->translate_y;
	}

	return NULL;
}

static void manipulator2d_get_axis_color(const int axis_idx, float *r_col, float *r_col_hi)
{
	const float alpha = 0.6f;
	const float alpha_hi = 1.0f;
	int col_id;

	switch (axis_idx) {
		case MAN2D_AXIS_TRANS_X:
			col_id = TH_AXIS_X;
			break;
		case MAN2D_AXIS_TRANS_Y:
			col_id = TH_AXIS_Y;
			break;
	}

	UI_GetThemeColor4fv(col_id, r_col);

	copy_v4_v4(r_col_hi, r_col);
	r_col[3] *= alpha;
	r_col_hi[3] *= alpha_hi;
}

static ManipulatorGroup2D *manipulatorgroup2d_init(wmWidgetGroup *wgroup)
{
	ManipulatorGroup2D *man = MEM_callocN(sizeof(ManipulatorGroup2D), __func__);

	man->translate_x = WIDGET_arrow2d_new(wgroup, "translate_x");
	man->translate_y = WIDGET_arrow2d_new(wgroup, "translate_y");

	return man;
}

/**
 * Calculates origin in view space, use with #manipulator2d_origin_to_region.
 */
static void manipulator2d_calc_origin(const bContext *C, float *r_origin)
{
	SpaceImage *sima = CTX_wm_space_image(C);
	Image *ima = ED_space_image(sima);

	if (sima->around == V3D_AROUND_CURSOR) {
		copy_v2_v2(r_origin, sima->cursor);
	}
	else {
		ED_uvedit_center(CTX_data_scene(C), ima, CTX_data_edit_object(C), r_origin, sima->around);
	}
}

/**
 * Convert origin (or any other point) from view to region space.
 */
BLI_INLINE void manipulator2d_origin_to_region(ARegion *ar, float *r_origin)
{
	UI_view2d_view_to_region_fl(&ar->v2d, r_origin[0], r_origin[1], &r_origin[0], &r_origin[1]);
}

/**
 * Custom handler for manipulator widgets
 */
static int manipulator2d_handler(bContext *C, const wmEvent *UNUSED(event), wmWidget *widget, const int UNUSED(flag))
{
	ARegion *ar = CTX_wm_region(C);
	float origin[3];

	manipulator2d_calc_origin(C, origin);
	manipulator2d_origin_to_region(ar, origin);
	WM_widget_set_origin(widget, origin);

	ED_region_tag_redraw(ar);

	return OPERATOR_PASS_THROUGH;
}

void WIDGETGROUP_manipulator2d_init(const bContext *UNUSED(C), wmWidgetGroup *wgroup)
{
	ManipulatorGroup2D *man = manipulatorgroup2d_init(wgroup);
	wgroup->customdata = man;

	MAN2D_ITER_AXES_BEGIN(axis, axis_idx)
	{
		const float offset[3] = {0.0f, 0.2f};

		float col[4], col_hi[4];
		manipulator2d_get_axis_color(axis_idx, col, col_hi);

		/* custom handler! */
		axis->handler = manipulator2d_handler;
		/* set up widget data */
		WIDGET_arrow2d_set_angle(axis, -M_PI_2 * axis_idx);
		WIDGET_arrow2d_set_line_len(axis, 0.8f);
		WM_widget_set_offset(axis, offset);
		WM_widget_set_line_width(axis, MANIPULATOR_AXIS_LINE_WIDTH);
		WM_widget_set_scale(axis, U.widget_scale);
		WM_widget_set_colors(axis, col, col_hi);

		/* assign operator */
		PointerRNA *ptr = WM_widget_set_operator(axis, "TRANSFORM_OT_translate");
		int constraint[3] = {0.0f};
		constraint[(axis_idx + 1) % 2] = 1;
		if (RNA_struct_find_property(ptr, "constraint_axis"))
			RNA_boolean_set_array(ptr, "constraint_axis", constraint);
		RNA_boolean_set(ptr, "release_confirm", 1);
	}
	MAN2D_ITER_AXES_END;
}

void WIDGETGROUP_manipulator2d_refresh(const bContext *C, wmWidgetGroup *wgroup)
{
	ManipulatorGroup2D *man = wgroup->customdata;
	float origin[3];

	manipulator2d_calc_origin(C, origin);
	copy_v2_v2(man->origin, origin);
}

void WIDGETGROUP_manipulator2d_draw_prepare(const bContext *C, wmWidgetGroup *wgroup)
{
	ManipulatorGroup2D *man = wgroup->customdata;
	float origin[3] = {UNPACK2(man->origin), 0.0f};

	manipulator2d_origin_to_region(CTX_wm_region(C), origin);

	MAN2D_ITER_AXES_BEGIN(axis, axis_idx)
	{
		WM_widget_set_origin(axis, origin);
	}
	MAN2D_ITER_AXES_END;
}

/* TODO (Julian)
 * - Called on every redraw, better to do a more simple poll and check for selection in _refresh
 * - UV editing only, could be expanded for other things.
 */
int WIDGETGROUP_manipulator2d_poll(const bContext *C, wmWidgetGroupType *UNUSED(wgrouptype))
{
	SpaceImage *sima = CTX_wm_space_image(C);
	Object *obedit = CTX_data_edit_object(C);

	if (ED_space_image_show_uvedit(sima, obedit)) {
		Image *ima = ED_space_image(sima);
		Scene *scene = CTX_data_scene(C);
		BMEditMesh *em = BKE_editmesh_from_object(obedit);
		BMFace *efa;
		BMLoop *l;
		BMIter iter, liter;
		MTexPoly *tf;

		const int cd_loop_uv_offset  = CustomData_get_offset(&em->bm->ldata, CD_MLOOPUV);
		const int cd_poly_tex_offset = CustomData_get_offset(&em->bm->pdata, CD_MTEXPOLY);

		/* check if there's a selected poly */
		BM_ITER_MESH (efa, &iter, em->bm, BM_FACES_OF_MESH) {
			tf = BM_ELEM_CD_GET_VOID_P(efa, cd_poly_tex_offset);
			if (!uvedit_face_visible_test(scene, ima, efa, tf))
				continue;

			BM_ITER_ELEM (l, &liter, efa, BM_LOOPS_OF_FACE) {
				if (uvedit_uv_select_test(scene, l, cd_loop_uv_offset)) {
					return true;
				}
			}
		}
	}

	return false;
}
