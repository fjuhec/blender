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

#include <stdlib.h>

#include "BIF_gl.h"

#include "BKE_context.h"

#include "BLI_listbase.h"

#include "DNA_manipulator_types.h"
#include "DNA_object_types.h"

#include "ED_screen.h"
#include "ED_view3d.h"
#include "ED_manipulator_library.h"

#include "GPU_select.h"
#include "GPU_matrix.h"

#include "MEM_guardedalloc.h"

/* own includes */
#include "WM_api.h"
#include "WM_types.h"


typedef struct FacemapManipulator {
	struct wmManipulator widget;
	Object *ob;
	int facemap;
	int style;
} FacemapManipulator;


/* -------------------------------------------------------------------- */

static void widget_facemap_draw(const bContext *C, struct wmManipulator *widget)
{
	FacemapManipulator *fmap_widget = (FacemapManipulator *)widget;
	const float *col = (widget->state & WM_MANIPULATOR_STATE_SELECT) ? widget->col_hi : widget->col;

	gpuPushMatrix();
	gpuMultMatrix(fmap_widget->ob->obmat);
	gpuTranslate3fv(widget->offset);
	ED_draw_object_facemap(CTX_data_scene(C), fmap_widget->ob, col, fmap_widget->facemap);
	gpuPopMatrix();
}

static void widget_facemap_render_3d_intersect(const bContext *C, struct wmManipulator *widget, int selectionbase)
{
	GPU_select_load_id(selectionbase);
	widget_facemap_draw(C, widget);
}

#if 0
static int widget_facemap_invoke(bContext *UNUSED(C), const wmEvent *event, struct wmManipulator *widget)
{
	return OPERATOR_PASS_THROUGH;
}

static int widget_facemap_handler(bContext *C, const wmEvent *event, struct wmManipulator *widget)
{
	return OPERATOR_PASS_THROUGH;
}
#endif

/* -------------------------------------------------------------------- */
/** \name Facemap Widget API
 *
 * \{ */

struct wmManipulator *ED_manipulator_facemap_new(
        wmManipulatorGroup *wgroup, const char *name, const int style,
        Object *ob, const int facemap)
{
	const wmManipulatorType *mpt = WM_manipulatortype_find("MANIPULATOR_WT_facemap3d", false);
	FacemapManipulator *fmap_widget = (FacemapManipulator *)WM_manipulator_new(mpt, wgroup, name);

	BLI_assert(facemap > -1);

	fmap_widget->ob = ob;
	fmap_widget->facemap = facemap;
	fmap_widget->style = style;

	return (struct wmManipulator *)fmap_widget;
}

bFaceMap *ED_manipulator_facemap_get_fmap(struct wmManipulator *widget)
{
	FacemapManipulator *fmap_widget = (FacemapManipulator *)widget;
	return BLI_findlink(&fmap_widget->ob->fmaps, fmap_widget->facemap);
}

static void MANIPULATOR_WT_facemap3d(wmManipulatorType *wt)
{
	/* identifiers */
	wt->idname = "MANIPULATOR_WT_facemap3d";

	/* api callbacks */
	wt->draw = widget_facemap_draw;
	wt->draw_select = widget_facemap_render_3d_intersect;

	wt->size = sizeof(FacemapManipulator);
}

void ED_manipulatortypes_facemap_3d(void)
{
	WM_manipulatortype_append(MANIPULATOR_WT_facemap3d);
}

/** \} */ // Facemap Widget API
