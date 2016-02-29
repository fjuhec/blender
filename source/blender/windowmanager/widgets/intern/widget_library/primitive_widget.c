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
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/widgets/intern/widget_library/primitive_widget.c
 *  \ingroup wm
 *
 * \name Primitive Widget
 *
 * 3D Widget
 *
 * \brief Widget with primitive drawing type (plane, cube, etc.).
 * Currently only plane primitive supported without own handling, use with operator only.
 */

#include "BIF_gl.h"

#include "BKE_context.h"

#include "BLI_math.h"

#include "DNA_view3d_types.h"
#include "DNA_widget_types.h"

#include "GPU_select.h"

#include "MEM_guardedalloc.h"

#include "WM_api.h"
#include "WM_types.h"

/* own includes */
#include "WM_widget_types.h"
#include "WM_widget_library.h"
#include "wm_widget_wmapi.h"
#include "wm_widget_intern.h"
#include "widget_library_intern.h"


/* PrimitiveWidget->flag */
#define PRIM_UP_VECTOR_SET 1

typedef struct PrimitiveWidget {
	wmWidget widget;

	float direction[3];
	float up[3];
	int style;
	int flag;
} PrimitiveWidget;


static float verts_plane[4][3] = {
	{-1, -1, 0},
	{ 1, -1, 0},
	{ 1,  1, 0},
	{-1,  1, 0},
};


/* -------------------------------------------------------------------- */

static void widget_primitive_draw_geom(const float col_inner[4], const float col_outer[4], const int style)
{
	float (*verts)[3];
	float vert_count;

	if (style == WIDGET_PRIMITIVE_STYLE_PLANE) {
		verts = verts_plane;
		vert_count = ARRAY_SIZE(verts_plane);
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, verts);
	glColor4fv(col_inner);
	glDrawArrays(GL_QUADS, 0, vert_count);
	glColor4fv(col_outer);
	glDrawArrays(GL_LINE_LOOP, 0, vert_count);
	glDisableClientState(GL_VERTEX_ARRAY);
}

static void widget_primitive_draw_intern(PrimitiveWidget *prim, const bool UNUSED(select), const bool highlight)
{
	float col_inner[4], col_outer[4];
	float rot[3][3];
	float mat[4][4];

	if (prim->flag & PRIM_UP_VECTOR_SET) {
		copy_v3_v3(rot[2], prim->direction);
		copy_v3_v3(rot[1], prim->up);
		cross_v3_v3v3(rot[0], prim->up, prim->direction);
	}
	else {
		const float up[3] = {0.0f, 0.0f, 1.0f};
		rotation_between_vecs_to_mat3(rot, up, prim->direction);
	}

	copy_m4_m3(mat, rot);
	copy_v3_v3(mat[3], prim->widget.origin);
	mul_mat3_m4_fl(mat, prim->widget.scale);

	glPushMatrix();
	glMultMatrixf(mat);

	if (highlight && (prim->widget.flag & WM_WIDGET_DRAW_HOVER) == 0) {
		copy_v4_v4(col_inner, prim->widget.col_hi);
		copy_v4_v4(col_outer, prim->widget.col_hi);
	}
	else {
		copy_v4_v4(col_inner, prim->widget.col);
		copy_v4_v4(col_outer, prim->widget.col);
	}
	col_inner[3] *= 0.5f;

	glEnable(GL_BLEND);
	glTranslate3fv(prim->widget.offset);
	widget_primitive_draw_geom(col_inner, col_outer, prim->style);
	glDisable(GL_BLEND);

	glPopMatrix();

	if (prim->widget.interaction_data) {
		WidgetInteraction *inter = prim->widget.interaction_data;

		copy_v4_fl(col_inner, 0.5f);
		copy_v3_fl(col_outer, 0.5f);
		col_outer[3] = 0.8f;

		copy_m4_m3(mat, rot);
		copy_v3_v3(mat[3], inter->init_origin);
		mul_mat3_m4_fl(mat, inter->init_scale);

		glPushMatrix();
		glMultMatrixf(mat);

		glEnable(GL_BLEND);
		glTranslate3fv(prim->widget.offset);
		widget_primitive_draw_geom(col_inner, col_outer, prim->style);
		glDisable(GL_BLEND);

		glPopMatrix();
	}
}

static void widget_primitive_render_3d_intersect(const bContext *UNUSED(C), wmWidget *widget, int selectionbase)
{
	GPU_select_load_id(selectionbase);
	widget_primitive_draw_intern((PrimitiveWidget *)widget, true, false);
}

static void widget_primitive_draw(const bContext *UNUSED(C), wmWidget *widget)
{
	widget_primitive_draw_intern((PrimitiveWidget *)widget, false, (widget->flag & WM_WIDGET_HIGHLIGHT));
}

static int widget_primitive_invoke(bContext *UNUSED(C), const wmEvent *UNUSED(event), wmWidget *widget)
{
	WidgetInteraction *inter = MEM_callocN(sizeof(WidgetInteraction), __func__);

	copy_v3_v3(inter->init_origin, widget->origin);
	inter->init_scale = widget->scale;

	widget->interaction_data = inter;

	return OPERATOR_RUNNING_MODAL;
}


/* -------------------------------------------------------------------- */
/** \name Primitive Widget API
 *
 * \{ */

wmWidget *WIDGET_primitive_new(wmWidgetGroup *wgroup, const char *name, const int style)
{
	PrimitiveWidget *prim = MEM_callocN(sizeof(PrimitiveWidget), name);
	const float dir_default[3] = {0.0f, 0.0f, 1.0f};

	prim->widget.draw = widget_primitive_draw;
	prim->widget.invoke = widget_primitive_invoke;
	prim->widget.intersect = NULL;
	prim->widget.render_3d_intersection = widget_primitive_render_3d_intersect;
	prim->widget.flag |= (WM_WIDGET_DRAW_ACTIVE | WM_WIDGET_SCALE_3D);
	prim->style = style;

	/* defaults */
	copy_v3_v3(prim->direction, dir_default);

	wm_widget_register(wgroup, &prim->widget, name);

	return (wmWidget *)prim;
}

/**
 * Define direction the primitive will point towards
 */
void WIDGET_primitive_set_direction(wmWidget *widget, const float direction[3])
{
	PrimitiveWidget *prim = (PrimitiveWidget *)widget;

	copy_v3_v3(prim->direction, direction);
	normalize_v3(prim->direction);
}

/**
 * Define up-direction of the primitive widget
 */
void WIDGET_primitive_set_up_vector(wmWidget *widget, const float direction[3])
{
	PrimitiveWidget *prim = (PrimitiveWidget *)widget;

	if (direction) {
		copy_v3_v3(prim->up, direction);
		normalize_v3(prim->up);
		prim->flag |= PRIM_UP_VECTOR_SET;
	}
	else {
		prim->flag &= ~PRIM_UP_VECTOR_SET;
	}
}

/** \} */ // Primitive Widget API


/* -------------------------------------------------------------------- */

void fix_linking_widget_primitive(void)
{
	(void)0;
}
