
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

/** \file blender/windowmanager/widgets/intern/widget_library/arrow2d_widget.c
 *  \ingroup wm
 *
 * \name 2D Arrow Widget
 *
 * 2D Widget
 *
 * \brief Simple arrow widget which is dragged into a certain direction.
 */

#include "BIF_gl.h"

#include "BKE_context.h"

#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_rect.h"

#include "DNA_widget_types.h"
#include "DNA_windowmanager_types.h"

#include "ED_screen.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"

#include "WM_types.h"

/* own includes */
#include "WM_widget_types.h"
#include "wm_widget_wmapi.h"
#include "WM_widget_library.h"
#include "wm_widget_intern.h"
#include "widget_library_intern.h"


typedef struct ArrowWidget2D {
	wmWidget widget;

	float angle;
	float line_len;
} ArrowWidget2D;


static void arrow2d_draw_geom(ArrowWidget2D *arrow, const float origin[2])
{
	const float size = 0.11f;
	const float size_h = size / 2.0f;
	const float len = arrow->line_len;
	const float draw_line_ofs = (arrow->widget.line_width * 0.5f) / arrow->widget.scale;

	glPushMatrix();
	glTranslate2fv(origin);
	glScalef(arrow->widget.scale, arrow->widget.scale, 0.0f);
	glRotatef(RAD2DEGF(arrow->angle), 0.0f, 0.0f, 1.0f);
	/* local offset */
	glTranslatef(arrow->widget.offset[0] + draw_line_ofs, arrow->widget.offset[1], 0.0f);

	/* TODO get rid of immediate mode */
	glBegin(GL_LINES);
	glVertex2f(0.0f, 0.0f);
	glVertex2f(0.0f, len);
	glEnd();
	glBegin(GL_TRIANGLES);
	glVertex2f(size_h, len);
	glVertex2f(-size_h, len);
	glVertex2f(0.0f, len + size * 1.7f);
	glEnd();

	glPopMatrix();
}

static void widget_arrow2d_draw(const bContext *UNUSED(C), wmWidget *widget)
{
	ArrowWidget2D *arrow = (ArrowWidget2D *)widget;
	const float *col = widget_color_get(widget, widget->flag & WM_WIDGET_HIGHLIGHT);

	glColor4fv(col);
	glLineWidth(widget->line_width);
	glEnable(GL_BLEND);
	arrow2d_draw_geom(arrow, widget->origin);
	glDisable(GL_BLEND);

	if (arrow->widget.interaction_data) {
		WidgetInteraction *inter = arrow->widget.interaction_data;

		glColor4f(0.5f, 0.5f, 0.5f, 0.5f);
		glEnable(GL_BLEND);
		arrow2d_draw_geom(arrow, inter->init_origin);
		glDisable(GL_BLEND);
	}
}

static int widget_arrow2d_invoke(bContext *UNUSED(C), const wmEvent *UNUSED(event), wmWidget *widget)
{
	WidgetInteraction *inter = MEM_callocN(sizeof(WidgetInteraction), __func__);

	copy_v2_v2(inter->init_origin, widget->origin);
	widget->interaction_data = inter;

	return OPERATOR_RUNNING_MODAL;
}

static int widget_arrow2d_intersect(bContext *UNUSED(C), const wmEvent *event, wmWidget *widget)
{
	ArrowWidget2D *arrow = (ArrowWidget2D *)widget;
	const float mval[2] = {event->mval[0], event->mval[1]};
	const float line_len = arrow->line_len * widget->scale;
	float mval_local[2];

	copy_v2_v2(mval_local, mval);
	sub_v2_v2(mval_local, widget->origin);

	float line[2][2];
	line[0][0] = line[0][1] = line[1][0] = 0.0f;
	line[1][1] = line_len;

	/* rotate only if needed */
	if (arrow->angle != 0.0f) {
		float rot_point[2];
		copy_v2_v2(rot_point, line[1]);
		rotate_v2_v2fl(line[1], rot_point, arrow->angle);
	}

	/* arrow line intersection check */
	float isect_1[2], isect_2[2];
	const int isect = isect_line_sphere_v2(
	        line[0], line[1], mval_local, WIDGET_HOTSPOT + widget->line_width * 0.5f,
	        isect_1, isect_2);

	if (isect > 0) {
		float line_ext[2][2]; /* extended line for segment check including hotspot */
		copy_v2_v2(line_ext[0], line[0]);
		line_ext[1][0] = line[1][0] + WIDGET_HOTSPOT * ((line[1][0] - line[0][0]) / line_len);
		line_ext[1][1] = line[1][1] + WIDGET_HOTSPOT * ((line[1][1] - line[0][1]) / line_len);

		const float lambda_1 = line_point_factor_v2(isect_1, line_ext[0], line_ext[1]);
		if (isect == 1) {
			return IN_RANGE_INCL(lambda_1, 0.0f, 1.0f);
		}
		else {
			BLI_assert(isect == 2);
			const float lambda_2 = line_point_factor_v2(isect_2, line_ext[0], line_ext[1]);
			return IN_RANGE_INCL(lambda_1, 0.0f, 1.0f) && IN_RANGE_INCL(lambda_2, 0.0f, 1.0f);
		}
	}

	return 0;
}

/* -------------------------------------------------------------------- */
/** \name 2D Arrow Widget API
 *
 * \{ */

wmWidget *WIDGET_arrow2d_new(wmWidgetGroup *wgroup, const char *name)
{
	ArrowWidget2D *arrow = MEM_callocN(sizeof(ArrowWidget2D), __func__);

	arrow->widget.draw = widget_arrow2d_draw;
	arrow->widget.invoke = widget_arrow2d_invoke;
//	arrow->widget.bind_to_prop = widget_arrow2d_bind_to_prop;
//	arrow->widget.handler = widget_arrow2d_handler;
	arrow->widget.intersect = widget_arrow2d_intersect;
//	arrow->widget.exit = widget_arrow2d_exit;
	arrow->widget.flag |= WM_WIDGET_DRAW_ACTIVE;

	arrow->line_len = 1.0f;

	wm_widget_register(wgroup, &arrow->widget, name);

	return (wmWidget *)arrow;
}

void WIDGET_arrow2d_set_angle(wmWidget *widget, const float angle)
{
	ArrowWidget2D *arrow = (ArrowWidget2D *)widget;
	arrow->angle = angle;
}

void WIDGET_arrow2d_set_line_len(wmWidget *widget, const float len)
{
	ArrowWidget2D *arrow = (ArrowWidget2D *)widget;
	arrow->line_len = len;
}

/** \} */ /* Arrow Widget API */


/* -------------------------------------------------------------------- */

void fix_linking_widget_arrow2d(void)
{
	(void)0;
}
