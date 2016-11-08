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
 * The Original Code is Copyright (C) 2014 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/manipulators/intern/manipulator_library/dial_manipulator.c
 *  \ingroup wm
 *
 * \name Dial Manipulator
 *
 * 3D Manipulator
 *
 * \brief Circle shaped manipulator for circular interaction.
 * Currently no own handling, use with operator only.
 */

#include "BIF_gl.h"
#include "BIF_glutil.h"

#include "BKE_context.h"

#include "BLI_math.h"

#include "ED_screen.h"
#include "ED_view3d.h"

#include "GPU_immediate.h"
#include "GPU_matrix.h"
#include "GPU_select.h"

#include "MEM_guardedalloc.h"

#include "WM_api.h"
#include "WM_types.h"

/* own includes */
#include "WM_manipulator_types.h"
#include "WM_manipulator_library.h"
#include "wm_manipulator_wmapi.h"
#include "wm_manipulator_intern.h"
#include "manipulator_geometry.h"
#include "manipulator_library_intern.h"


typedef struct DialManipulator {
	wmManipulator manipulator;
	int style;
	float direction[3];
	/* will later be converted into angle */
	double value;
} DialManipulator;

typedef struct DialInteraction {
	float value_indicator_offset;
} DialInteraction;

#define DIAL_WIDTH       1.0f
#define DIAL_RESOLUTION 32


/* -------------------------------------------------------------------- */

static void dial_geom_draw(
        const DialManipulator *dial, const float mat[4][4], const float clipping_plane[3], const float col[4])
{
	const bool is_active = dial->manipulator.interaction_data;
	const bool use_clipping = (dial->style == MANIPULATOR_DIAL_STYLE_RING_CLIPPED) && !is_active;
	const bool filled = (dial->style == MANIPULATOR_DIAL_STYLE_RING_FILLED);
	const unsigned int pos = add_attrib(immVertexFormat(), "pos", GL_FLOAT, 3, KEEP_FLOAT);

	glLineWidth(dial->manipulator.line_width);

	immBindBuiltinProgram(use_clipping ? GPU_SHADER_3D_CLIPPED_UNIFORM_COLOR : GPU_SHADER_3D_UNIFORM_COLOR);
	immUniformColor4fv(col);

	if (use_clipping) {
		glEnable(GL_CLIP_DISTANCE0);
		immUniform4fv("ClipPlane", clipping_plane);
		immUniformMat4("ModelMatrix", mat);
	}

	if (filled) {
		imm_draw_filled_circle_3D(pos, 0.0f, 0.0f, DIAL_WIDTH, DIAL_RESOLUTION);
	}
	else {
		imm_draw_lined_circle_3D(pos, 0.0f, 0.0f, DIAL_WIDTH, DIAL_RESOLUTION);
	}

	immUnbindProgram();
}


static void dial_ghostarc_draw_helpline(const float angle, const float co_outer[3], const float col[4])
{
	const unsigned int pos = add_attrib(immVertexFormat(), "pos", GL_FLOAT, 3, KEEP_FLOAT);

	immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
	immUniformColor4fv(col);

	gpuMatrixBegin3D_legacy();
	gpuPushMatrix();
	gpuRotateAxis(-RAD2DEGF(angle), 'Z');

	immBegin(GL_LINE_STRIP, 2);
	immVertex3f(pos, 0.0f, 0.0f, 0.0f);
	immVertex3fv(pos, co_outer);
	immEnd();

	gpuPopMatrix();
	gpuMatrixEnd();

	immUnbindProgram();
}

static void dial_ghostarc_draw_inner(const float init_angle_offset, const float angle, const float col[4])
{
	const unsigned int pos = add_attrib(immVertexFormat(), "pos", GL_FLOAT, 3, KEEP_FLOAT);

	immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
	immUniformColor4fv(col);
	/* not entirely sure why the 90 degree offset is needed here and why values have to be negative */
	imm_draw_filled_arc_3D(pos, -init_angle_offset + (float)M_PI_2, -angle, DIAL_WIDTH, DIAL_RESOLUTION);
	immUnbindProgram();
}

static float manipulator_value_to_angle(const double value)
{
	return (float)(value * M_PI * 2.0);
}

static void dial_geom_value_indicator_draw(
        const DialManipulator *dial, DialInteraction *inter,
        const float col_outer[4], const float col_inner[4])
{
	const float co_outer[4] = {0.0f, DIAL_WIDTH, 0.0f}; /* coordinate at which the arc drawing will be started */
	const float angle = manipulator_value_to_angle(dial->value);

	dial_ghostarc_draw_helpline(inter->value_indicator_offset, co_outer, col_outer);
	dial_ghostarc_draw_helpline(inter->value_indicator_offset + angle, co_outer, col_outer);
	dial_ghostarc_draw_inner(inter->value_indicator_offset, angle, col_inner);
}

static void dial_clipping_plane_get(DialManipulator *dial, RegionView3D *rv3d, float r_clipping_plane[3])
{
	copy_v3_v3(r_clipping_plane, rv3d->viewinv[2]);
	r_clipping_plane[3] = -dot_v3v3(rv3d->viewinv[2], dial->manipulator.origin);
}

static void dial_matrix_get(const DialManipulator *dial, float r_mat[4][4])
{
	const float up[3] = {0.0f, 0.0f, 1.0f};
	float rot[3][3];

	rotation_between_vecs_to_mat3(rot, up, dial->direction);
	copy_m4_m3(r_mat, rot);
	copy_v3_v3(r_mat[3], dial->manipulator.origin);
	mul_mat3_m4_fl(r_mat, dial->manipulator.scale);
}

static void dial_draw_intern(const bContext *C, DialManipulator *dial, const bool highlight)
{
	DialInteraction *inter = dial->manipulator.interaction_data;
	const bool is_active = (inter != NULL);
	const bool use_value_indicator = is_active && (dial->manipulator.flag & WM_MANIPULATOR_DRAW_ACTIVE);
	const bool use_clipping = (dial->style == MANIPULATOR_DIAL_STYLE_RING_CLIPPED) && !is_active;
	float clipping_plane[3];
	float mat[4][4];
	float col[4];

	BLI_assert(CTX_wm_area(C)->spacetype == SPACE_VIEW3D);

	/* get all data we need */
	manipulator_color_get(&dial->manipulator, highlight, col);
	dial_matrix_get(dial, mat);
	if (use_clipping) {
		dial_clipping_plane_get(dial, CTX_wm_region_view3d(C), clipping_plane);
	}

	glPushMatrix();
	glMultMatrixf(mat);

	if (use_value_indicator) {
		const float col_inner[] = {0.8f, 0.8f, 0.8f, 0.4f};
		/* draw rotation indicator arc first */
		dial_geom_value_indicator_draw(dial, inter, col, col_inner);
	}
	/* draw actual dial manipulator */
	dial_geom_draw(dial, mat, clipping_plane, col);

	glPopMatrix();
}

static void manipulator_dial_render_3d_intersect(const bContext *C, wmManipulator *manipulator, int selectionbase)
{
	DialManipulator *dial = (DialManipulator *)manipulator;

	GPU_select_load_id(selectionbase);
	dial_draw_intern(C, dial, false);
}

static void manipulator_dial_draw(const bContext *C, wmManipulator *manipulator)
{
	DialManipulator *dial = (DialManipulator *)manipulator;

	glEnable(GL_BLEND);
	dial_draw_intern(C, dial, (manipulator->state & WM_MANIPULATOR_HIGHLIGHT) != 0);
	glDisable(GL_BLEND);
}

/**
 * This function calculates the angle between the current mouse coordinate \a mval and the default
 * starting coordinate of the value indicator arc (projected onto the 2d viewing plane). The resulting
 * angle is used to offset the value indicator arc so that it starts where the user starts dragging.
 *
 * Not sure yet if this is only useful for rotation manipulators or for other dial manipulators
 * as well. Might consider moving it to view3d_transform_manipulators.c if not.
 */
static float manipulator_init_angle_offset_get(
        const DialManipulator *dial, const ARegion *ar,
        const float mat[4][4], const float mval[2])
{
	RegionView3D *rv3d = ar->regiondata;
	/* model-space coordinate at which the arc drawing will be started (converted to world-space later) */
	float co_outer[3] = {0.0f, DIAL_WIDTH, 0.0f};
	float origin_2d[2], co_outer_2d[2], rel_mval[2], rel_co_outer_2d[2];
	bool inv = false;

	/* we might need to invert the direction of the angles */
	float view_vec[3], axis_vec[3];
	ED_view3d_global_to_vector(rv3d, mat[3], view_vec);
	normalize_v3_v3(axis_vec, dial->direction);
	if (dot_v3v3(view_vec, axis_vec) < 0.0f) {
		inv = true;
	}

	/* convert to world-space */
	mul_m4_v3(mat, co_outer);

	/* project origin and starting coordinate of arc onto 2D view plane */
	ED_view3d_project_float_global(ar, mat[3], origin_2d, V3D_PROJ_TEST_NOP);
	ED_view3d_project_float_global(ar, co_outer, co_outer_2d, V3D_PROJ_TEST_NOP);

	/* convert to origin relative space */
	sub_v2_v2v2(rel_mval, mval, origin_2d);
	sub_v2_v2v2(rel_co_outer_2d, co_outer_2d, origin_2d);

	/* return angle between co_outer_2d and mval around origin */
	return angle_signed_v2v2(rel_co_outer_2d, rel_mval) * (inv ? -1 : 1);
}

static int manipulator_dial_invoke(bContext *C, const wmEvent *event, wmManipulator *manipulator)
{
	if (manipulator->flag & WM_MANIPULATOR_DRAW_ACTIVE) {
		const ARegion *ar = CTX_wm_region(C);
		const DialManipulator *dial = (DialManipulator *)manipulator;
		const float mval[2] = {event->mval[0], event->mval[1]};
		DialInteraction *inter = MEM_callocN(sizeof(DialInteraction), __func__);
		float mat[4][4];

		dial_matrix_get(dial, mat);
		inter->value_indicator_offset = manipulator_init_angle_offset_get(dial, ar, mat, mval);

		manipulator->interaction_data = inter;
	}

	return OPERATOR_RUNNING_MODAL;
}


/* -------------------------------------------------------------------- */
/** \name Dial Manipulator API
 *
 * \{ */

wmManipulator *WM_dial_manipulator_new(wmManipulatorGroup *mgroup, const char *name, const int style)
{
	DialManipulator *dial = MEM_callocN(sizeof(DialManipulator), name);
	const float dir_default[3] = {0.0f, 0.0f, 1.0f};

	dial->manipulator.draw = manipulator_dial_draw;
	dial->manipulator.intersect = NULL;
	dial->manipulator.render_3d_intersection = manipulator_dial_render_3d_intersect;
	dial->manipulator.invoke = manipulator_dial_invoke;

	dial->style = style;

	/* defaults */
	copy_v3_v3(dial->direction, dir_default);

	wm_manipulator_register(mgroup, &dial->manipulator, name);

	return (wmManipulator *)dial;
}

/**
 * Define up-direction of the dial manipulator
 */
void WM_dial_manipulator_set_up_vector(wmManipulator *manipulator, const float direction[3])
{
	DialManipulator *dial = (DialManipulator *)manipulator;

	copy_v3_v3(dial->direction, direction);
	normalize_v3(dial->direction);
}

void WM_dial_manipulator_set_value(wmManipulator *manipulator, const double value)
{
	DialManipulator *dial = (DialManipulator *)manipulator;
	dial->value = value;
}

/** \} */ // Dial Manipulator API


/* -------------------------------------------------------------------- */

void fix_linking_manipulator_dial(void)
{
	(void)0;
}
