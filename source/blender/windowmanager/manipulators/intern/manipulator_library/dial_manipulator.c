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
 * \brief Circle shaped widget for circular interaction.
 * Currently no own handling, use with operator only.
 */

#include "BIF_gl.h"

#include "BKE_context.h"

#include "BLI_math.h"

#include "ED_screen.h"
#include "ED_view3d.h"

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


/* to use custom dials exported to geom_dial_widget.c */
//#define WIDGET_USE_CUSTOM_DIAS

#ifdef WIDGET_USE_CUSTOM_DIAS
ManipulatorDrawInfo dial_draw_info = {0};
#endif

typedef struct DialManipulator {
	wmManipulator widget;
	int style;
	float direction[3];
} DialManipulator;

typedef struct DialInteraction {
	float init_mval[2];

	/* cache the last angle to detect rotations bigger than -/+ PI */
	float last_angle;
	/* number of full rotations */
	int rotations;
} DialInteraction;

#define DIAL_WIDTH       1.0f
#define DIAL_RESOLUTION 32


/* -------------------------------------------------------------------- */

static void dial_geom_draw(const DialManipulator *dial, const float col[4], const bool select)
{
#ifdef WIDGET_USE_CUSTOM_DIAS
	manipulator_draw_intern(&dial_draw_info, select);
#else
	const bool filled = (dial->style == MANIPULATOR_DIAL_STYLE_RING_FILLED);

	glLineWidth(dial->widget.line_width);
	glColor4fv(col);

	GLUquadricObj *qobj = gluNewQuadric();
	gluQuadricDrawStyle(qobj, filled ? GLU_FILL : GLU_SILHOUETTE);
	/* inner at 0.0 with silhouette drawing confuses OGL selection, so draw it at width */
	gluDisk(qobj, filled ? 0.0 : DIAL_WIDTH, DIAL_WIDTH, DIAL_RESOLUTION, 1);
	gluDeleteQuadric(qobj);

	UNUSED_VARS(select);
#endif
}

/**
 * Draws a line from (0, 0, 0) to \a co_outer, at \a angle.
 */
static void dial_ghostarc_draw_helpline(const float angle, const float co_outer[3])
{
	glLineWidth(1.0f);

	glPushMatrix();
	glRotatef(RAD2DEGF(angle), 0.0f, 0.0f, -1.0f);
//	glScalef(0.0f, DIAL_WIDTH - dial->widget.line_width * 0.5f / U.widget_scale, 0.0f);
	glBegin(GL_LINE_STRIP);
	glVertex3f(0.0f, 0.0f, 0.0f);
	glVertex3fv(co_outer);
	glEnd();
	glPopMatrix();
}

static void dial_ghostarc_draw(const DialManipulator *dial, const float angle_ofs, const float angle_delta)
{
	GLUquadricObj *qobj = gluNewQuadric();
	const float width_inner = DIAL_WIDTH - dial->widget.line_width * 0.5f / U.widget_scale;

	gluQuadricDrawStyle(qobj, GLU_FILL);
	gluPartialDisk(qobj, 0.0, width_inner, DIAL_RESOLUTION, 1, RAD2DEGF(angle_ofs), RAD2DEGF(angle_delta));
	gluDeleteQuadric(qobj);
}

static void dial_ghostarc_get_angles(
        const DialManipulator *dial, const wmEvent *event, const ARegion *ar,
        float mat[4][4], const float co_outer[3],
        float *r_start, float *r_delta)
{
	DialInteraction *inter = dial->widget.interaction_data;
	const RegionView3D *rv3d = ar->regiondata;
	const float mval[2] = {event->x - ar->winrct.xmin, event->y - ar->winrct.ymin};
	bool inv = false;

	/* we might need to invert the direction of the angles */
	float view_vec[3], axis_vec[3];
	ED_view3d_global_to_vector(rv3d, rv3d->twmat[3], view_vec);
	normalize_v3_v3(axis_vec, dial->direction);
	if (dot_v3v3(view_vec, axis_vec) < 0.0f) {
		inv = true;
	}

	float co[3], origin2d[2], co2d[2];
	mul_v3_project_m4_v3(co, mat, co_outer);
	/* project 3d coordinats to 2d viewplane */
	ED_view3d_project_float_global(ar, dial->widget.origin, origin2d, V3D_PROJ_TEST_NOP);
	ED_view3d_project_float_global(ar, co, co2d, V3D_PROJ_TEST_NOP);

	/* convert to widget relative space */
	float rel_initmval[2], rel_mval[2], rel_co[2];
	sub_v2_v2v2(rel_initmval, inter->init_mval, origin2d);
	sub_v2_v2v2(rel_mval, mval, origin2d);
	sub_v2_v2v2(rel_co, co2d, origin2d);

	/* return angles */
	const float start = angle_signed_v2v2(rel_co, rel_initmval) * (inv ? -1 : 1);
	const float delta = angle_signed_v2v2(rel_initmval, rel_mval) * (inv ? -1 : 1);

	/* Change of sign, we passed the 180 degree threshold. This means we need to add a turn
	 * to distinguish between transition from 0 to -1 and -PI to +PI, use comparison with PI/2.
	 * Logic taken from BLI_dial_angle */
	if ((delta * inter->last_angle < 0.0f) &&
	    (fabsf(inter->last_angle) > (float)M_PI_2))
	{
		if (inter->last_angle < 0.0f)
			inter->rotations--;
		else
			inter->rotations++;
	}
	inter->last_angle = delta;

	*r_start = start;
	*r_delta = fmod(delta + 2.0f * (float)M_PI * inter->rotations, 2 * (float)M_PI);
}

static void dial_draw_intern(const bContext *C, DialManipulator *dial, const bool select, const bool highlight)
{
	float rot[3][3];
	float mat[4][4];
	const float up[3] = {0.0f, 0.0f, 1.0f};
	const float *col = manipulator_color_get(&dial->widget, highlight);

	BLI_assert(CTX_wm_area(C)->spacetype == SPACE_VIEW3D);

	rotation_between_vecs_to_mat3(rot, up, dial->direction);
	copy_m4_m3(mat, rot);
	copy_v3_v3(mat[3], dial->widget.origin);
	mul_mat3_m4_fl(mat, dial->widget.scale);

	glPushMatrix();
	glMultMatrixf(mat);
	glTranslate3fv(dial->widget.offset);

	/* draw rotation indicator arc first */
	if ((dial->widget.flag & WM_MANIPULATOR_DRAW_VALUE) && (dial->widget.flag & WM_MANIPULATOR_ACTIVE)) {
		wmWindow *win = CTX_wm_window(C);
		const float co_outer[4] = {0.0f, DIAL_WIDTH, 0.0f}; /* coordinate at which the arc drawing will be started */
		float angle_ofs, angle_delta;

		dial_ghostarc_get_angles(dial, win->eventstate, CTX_wm_region(C), mat, co_outer, &angle_ofs, &angle_delta);
		/* draw! */
		glColor4f(0.8f, 0.8f, 0.8f, 0.4f);
		dial_ghostarc_draw(dial, angle_ofs, angle_delta);

		glColor4fv(col);
		dial_ghostarc_draw_helpline(angle_ofs, co_outer); /* starting position */
		dial_ghostarc_draw_helpline(angle_ofs + angle_delta, co_outer); /* starting position + current value */
	}

	/* draw actual dial widget */
	dial_geom_draw(dial, col, select);

	glPopMatrix();

}

static void manipulator_dial_render_3d_intersect(const bContext *C, wmManipulator *widget, int selectionbase)
{
	DialManipulator *dial = (DialManipulator *)widget;

	/* enable clipping if needed */
	if (dial->style == MANIPULATOR_DIAL_STYLE_RING_CLIPPED) {
		ARegion *ar = CTX_wm_region(C);
		RegionView3D *rv3d = ar->regiondata;
		double plane[4];

		copy_v3db_v3fl(plane, rv3d->viewinv[2]);
		plane[3] = -dot_v3v3(rv3d->viewinv[2], widget->origin);
		glClipPlane(GL_CLIP_PLANE0, plane);
		glEnable(GL_CLIP_PLANE0);
	}

	GPU_select_load_id(selectionbase);
	dial_draw_intern(C, dial, true, false);

	if (dial->style == MANIPULATOR_DIAL_STYLE_RING_CLIPPED) {
		glDisable(GL_CLIP_PLANE0);
	}
}

static void manipulator_dial_draw(const bContext *C, wmManipulator *widget)
{
	DialManipulator *dial = (DialManipulator *)widget;
	const bool active = widget->flag & WM_MANIPULATOR_ACTIVE;

	/* enable clipping if needed */
	if (!active && dial->style == MANIPULATOR_DIAL_STYLE_RING_CLIPPED) {
		double plane[4];
		ARegion *ar = CTX_wm_region(C);
		RegionView3D *rv3d = ar->regiondata;

		copy_v3db_v3fl(plane, rv3d->viewinv[2]);
		plane[3] = -dot_v3v3(rv3d->viewinv[2], widget->origin);
		glClipPlane(GL_CLIP_PLANE0, plane);
		glEnable(GL_CLIP_PLANE0);
	}

	glEnable(GL_BLEND);
	dial_draw_intern(C, dial, false, (widget->flag & WM_MANIPULATOR_HIGHLIGHT) != 0);
	glDisable(GL_BLEND);

	if (!active && dial->style == MANIPULATOR_DIAL_STYLE_RING_CLIPPED) {
		glDisable(GL_CLIP_PLANE0);
	}
}

static int manipulator_dial_invoke(bContext *UNUSED(C), const wmEvent *event, wmManipulator *widget)
{
	DialInteraction *inter = MEM_callocN(sizeof(DialInteraction), __func__);

	inter->init_mval[0] = event->mval[0];
	inter->init_mval[1] = event->mval[1];

	widget->interaction_data = inter;

	return OPERATOR_RUNNING_MODAL;
}


/* -------------------------------------------------------------------- */
/** \name Dial Manipulator API
 *
 * \{ */

wmManipulator *MANIPULATOR_dial_new(wmManipulatorGroup *wgroup, const char *name, const int style)
{
	DialManipulator *dial = MEM_callocN(sizeof(DialManipulator), name);
	const float dir_default[3] = {0.0f, 0.0f, 1.0f};

#ifdef WIDGET_USE_CUSTOM_DIAS
	if (!dial_draw_info.init) {
		dial_draw_info.nverts = _MANIPULATOR_nverts_dial,
		dial_draw_info.ntris = _MANIPULATOR_ntris_dial,
		dial_draw_info.verts = _MANIPULATOR_verts_dial,
		dial_draw_info.normals = _MANIPULATOR_normals_dial,
		dial_draw_info.indices = _MANIPULATOR_indices_dial,
		dial_draw_info.init = true;
	}
#endif

	dial->widget.draw = manipulator_dial_draw;
	dial->widget.intersect = NULL;
	dial->widget.render_3d_intersection = manipulator_dial_render_3d_intersect;
	dial->widget.invoke = manipulator_dial_invoke;
	dial->widget.flag |= WM_MANIPULATOR_SCALE_3D;

	dial->style = style;

	/* defaults */
	copy_v3_v3(dial->direction, dir_default);

	WM_manipulator_register(wgroup, &dial->widget, name);

	return (wmManipulator *)dial;
}

/**
 * Define up-direction of the dial widget
 */
void MANIPULATOR_dial_set_up_vector(wmManipulator *widget, const float direction[3])
{
	DialManipulator *dial = (DialManipulator *)widget;

	copy_v3_v3(dial->direction, direction);
	normalize_v3(dial->direction);
}

/** \} */ // Dial Manipulator API


/* -------------------------------------------------------------------- */

void fix_linking_manipulator_dial(void)
{
	(void)0;
}
