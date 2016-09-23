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

/** \file blender/windowmanager/widgets/intern/widget_library/facemap_manipulator.c
 *  \ingroup wm
 *
 * \name Facemap Manipulator
 *
 * 3D Manipulator
 *
 * \brief Manipulator representing shape of a face map.
 * Currently no own handling, use with operator only.
 */

#include "BIF_gl.h"

#include "BKE_context.h"

#include "BLI_listbase.h"

#include "DNA_manipulator_types.h"
#include "DNA_object_types.h"

#include "ED_screen.h"
#include "ED_view3d.h"

#include "GPU_select.h"

#include "MEM_guardedalloc.h"

/* own includes */
#include "WM_manipulator_types.h"
#include "WM_manipulator_library.h"
#include "wm_manipulator_wmapi.h"
#include "wm_manipulator_intern.h"


typedef struct FacemapManipulator {
	wmManipulator widget;
	Object *ob;
	int facemap;
	int style;
} FacemapManipulator;


/* -------------------------------------------------------------------- */

static void widget_facemap_draw(const bContext *C, wmManipulator *widget)
{
	FacemapManipulator *fmap_widget = (FacemapManipulator *)widget;
	const float *col = (widget->flag & WM_MANIPULATOR_SELECTED) ? widget->col_hi : widget->col;

	glPushMatrix();
	glMultMatrixf(fmap_widget->ob->obmat);
	glTranslate3fv(widget->offset);
	ED_draw_object_facemap(CTX_data_scene(C), fmap_widget->ob, col, fmap_widget->facemap);
	glPopMatrix();
}

static void widget_facemap_render_3d_intersect(const bContext *C, wmManipulator *widget, int selectionbase)
{
	GPU_select_load_id(selectionbase);
	widget_facemap_draw(C, widget);
}

#if 0
static int widget_facemap_invoke(bContext *UNUSED(C), const wmEvent *event, wmManipulator *widget)
{
	return OPERATOR_PASS_THROUGH;
}

static int widget_facemap_handler(bContext *C, const wmEvent *event, wmManipulator *widget)
{
	return OPERATOR_PASS_THROUGH;
}
#endif

/* -------------------------------------------------------------------- */
/** \name Facemap Widget API
 *
 * \{ */

wmManipulator *MANIPULATOR_facemap_new(
        wmManipulatorGroup *wgroup, const char *name, const int style,
        Object *ob, const int facemap)
{
	FacemapManipulator *fmap_widget = MEM_callocN(sizeof(FacemapManipulator), name);

	BLI_assert(facemap > -1);

	fmap_widget->widget.draw = widget_facemap_draw;
//	fmap_widget->widget.invoke = widget_facemap_invoke;
//	fmap_widget->widget.bind_to_prop = NULL;
//	fmap_widget->widget.handler = widget_facemap_handler;
	fmap_widget->widget.render_3d_intersection = widget_facemap_render_3d_intersect;
	fmap_widget->ob = ob;
	fmap_widget->facemap = facemap;
	fmap_widget->style = style;

	wm_manipulator_register(wgroup, &fmap_widget->widget, name);

	return (wmManipulator *)fmap_widget;
}

bFaceMap *MANIPULATOR_facemap_get_fmap(wmManipulator *widget)
{
	FacemapManipulator *fmap_widget = (FacemapManipulator *)widget;
	return BLI_findlink(&fmap_widget->ob->fmaps, fmap_widget->facemap);
}

/** \} */ // Facemap Widget API


/* -------------------------------------------------------------------- */

void fix_linking_manipulator_facemap(void)
{
	(void)0;
}
