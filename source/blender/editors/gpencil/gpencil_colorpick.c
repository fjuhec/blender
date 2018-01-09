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
 * The Original Code is Copyright (C) 2017, Blender Foundation, Joshua Leung
 * This is a new part of Blender
 *
 * Contributor(s): Antonio Vazquez
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/gpencil/gpencil_colorpick.c
 *  \ingroup edgpencil
 */

#include <stdio.h>

#include "MEM_guardedalloc.h" 

#include "BLI_utildefines.h"
#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_stack.h"

#include "BLT_translation.h"

#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_windowmanager_types.h"

#include "BKE_main.h" 
#include "BKE_image.h" 
#include "BKE_gpencil.h"
#include "BKE_context.h"
#include "BKE_screen.h"
#include "BKE_paint.h" 
#include "BKE_report.h" 

#include "ED_gpencil.h"
#include "ED_screen.h"
#include "ED_space_api.h" 
#include "ED_view3d.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "GPU_immediate.h"
#include "GPU_draw.h"
#include "GPU_matrix.h"
#include "GPU_framebuffer.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "WM_api.h"
#include "WM_types.h"

#include "gpencil_intern.h"

 /* draw a filled box */
static void gp_draw_fill_box(rcti *box, float ink[4], float fill[4])
{
	Gwn_VertFormat *format = immVertexFormat();
	unsigned pos = GWN_vertformat_attr_add(format, "pos", GWN_COMP_F32, 2, GWN_FETCH_FLOAT);
	unsigned color = GWN_vertformat_attr_add(format, "color", GWN_COMP_F32, 4, GWN_FETCH_FLOAT);

	immBindBuiltinProgram(GPU_SHADER_2D_FLAT_COLOR);
	
	/* draw stroke curve */
	glLineWidth(1.0f);
	immBeginAtMost(GWN_PRIM_TRIS, 6);

	/* First triangle */
	immAttrib4fv(color, ink);
	immVertex2f(pos, box->xmin, box->ymin);

	immAttrib4fv(color, ink);
	immVertex2f(pos, box->xmin, box->ymax);

	immAttrib4fv(color, ink);
	immVertex2f(pos, box->xmax, box->ymax);

	/* Second triangle */
	immAttrib4fv(color, fill);
	immVertex2f(pos, box->xmin, box->ymin);

	immAttrib4fv(color, fill);
	immVertex2f(pos, box->xmax, box->ymax);

	immAttrib4fv(color, fill);
	immVertex2f(pos, box->xmax, box->ymin);

	immEnd();
	immUnbindProgram();
}

/* ----------------------- */
/* Drawing                 */
/* Helper: Draw status message while the user is running the operator */
static void gpencil_colorpick_status_indicators(tGPDpick *tgpk)
{
	Scene *scene = tgpk->scene;
	char status_str[UI_MAX_DRAW_STR];

	BLI_snprintf(status_str, sizeof(status_str), IFACE_("Select: ESC/RMB cancel, LMB Select color"));
	ED_area_headerprint(tgpk->sa, status_str);
}

/* draw a toolbar with all colors of the palette */
static void gpencil_draw_color_table(const bContext *UNUSED(C), tGPDpick *tgpk)
{
	if (!tgpk->palette) {
		return;
	}
	rcti box;
	box.xmin = tgpk->rect.xmax - 100;
	box.ymin = tgpk->rect.ymin;
	box.xmax = tgpk->rect.xmax;
	box.ymax = tgpk->rect.ymax;

	float ink[4];
	UI_GetThemeColor4fv(TH_PANEL_BACK, ink);

	//float ink[4] = { 1.0, 0.0f, 0.0f, 1.0f };
	gp_draw_fill_box(&box, ink, ink);
}

/* Drawing callback for modal operator in 3d mode */
static void gpencil_colorpick_draw_3d(const bContext *C, ARegion *UNUSED(ar), void *arg)
{
	tGPDpick *tgpk = (tGPDpick *)arg;
	gpencil_draw_color_table(C, tgpk); 
}

/* check if context is suitable */
static int gpencil_colorpick_poll(bContext *C)
{
	if (ED_operator_regionactive(C)) {
		ScrArea *sa = CTX_wm_area(C);
		if (sa->spacetype == SPACE_VIEW3D) {
			return 1;
		}
		else {
			CTX_wm_operator_poll_msg_set(C, "Active region not valid for operator");
			return 0;
		}
	}
	else {
		CTX_wm_operator_poll_msg_set(C, "Active region not set");
		return 0;
	}
	return 0;
}

/* Allocate memory and initialize values */
static tGPDpick *gp_session_init_colorpick(bContext *C, wmOperator *op)
{
	tGPDpick *tgpk = MEM_callocN(sizeof(tGPDpick), __func__);

	/* define initial values */
	ToolSettings *ts = CTX_data_tool_settings(C);
	bGPdata *gpd = CTX_data_gpencil_data(C);
	Main *bmain = CTX_data_main(C);

	/* set current scene and window info */
	tgpk->scene = CTX_data_scene(C);
	tgpk->ob = CTX_data_active_object(C);
	tgpk->sa = CTX_wm_area(C);
	tgpk->ar = CTX_wm_region(C);
	tgpk->eval_ctx = bmain->eval_ctx;
	tgpk->rv3d = tgpk->ar->regiondata;
	tgpk->v3d = tgpk->sa->spacedata.first;
	tgpk->graph = CTX_data_depsgraph(C);
	tgpk->win = CTX_wm_window(C);

	ED_region_visible_rect(tgpk->ar, &tgpk->rect);

	/* get palette */
	bGPDpaletteref *palslot = BKE_gpencil_paletteslot_validate(bmain, gpd);
	tgpk->palette = palslot->palette;

	/* return context data for running operator */
	return tgpk;
}

/* end operator */
static void gpencil_colorpick_exit(bContext *C, wmOperator *op)
{
	Main *bmain = CTX_data_main(C);
	Object *ob = CTX_data_active_object(C);

	/* restore cursor to indicate end of fill */
	WM_cursor_modal_restore(CTX_wm_window(C));

	tGPDpick *tgpk = op->customdata;

	/* don't assume that operator data exists at all */
	if (tgpk) {
		/* clear status message area */
		ED_area_headerprint(tgpk->sa, NULL);

		/* remove drawing handler */
		if (tgpk->draw_handle_3d) {
			ED_region_draw_cb_exit(tgpk->ar->type, tgpk->draw_handle_3d);
		}

		/* finally, free memory used by temp data */
		MEM_freeN(tgpk);
	}

	/* clear pointer */
	op->customdata = NULL;

	WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED, NULL);
}

static void gpencil_colorpick_cancel(bContext *C, wmOperator *op)
{
	/* this is just a wrapper around exit() */
	gpencil_colorpick_exit(C, op);
}

/* Init: Allocate memory and set init values */
static int gpencil_colorpick_init(bContext *C, wmOperator *op)
{
	tGPDpick *tgpk;

	/* check context */
	tgpk = op->customdata = gp_session_init_colorpick(C, op);
	if (tgpk == NULL) {
		/* something wasn't set correctly in context */
		gpencil_colorpick_exit(C, op);
		return 0;
	}

	/* everything is now setup ok */
	return 1;
}

/* start of interactive part of operator */
static int gpencil_colorpick_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	tGPDpick *tgpk = NULL;

	/* try to initialize context data needed */
	if (!gpencil_colorpick_init(C, op)) {
		gpencil_colorpick_exit(C, op);
		if (op->customdata)
			MEM_freeN(op->customdata);
		return OPERATOR_CANCELLED;
	}
	else {
		tgpk = op->customdata;
	}

	/* Enable custom drawing handlers */
	tgpk->draw_handle_3d = ED_region_draw_cb_activate(tgpk->ar->type, gpencil_colorpick_draw_3d, tgpk, REGION_DRAW_POST_PIXEL);

	WM_cursor_modal_set(CTX_wm_window(C), BC_EYEDROPPER_CURSOR);
	
	gpencil_colorpick_status_indicators(tgpk);

	WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED, NULL);

	/* add a modal handler for this operator*/
	WM_event_add_modal_handler(C, op);

	return OPERATOR_RUNNING_MODAL;
}

/* events handling during interactive part of operator */
static int gpencil_colorpick_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
	Scene *scene = CTX_data_scene(C);
	Object *ob = CTX_data_active_object(C);
	tGPDpick *tgpk = op->customdata;

	int estate = OPERATOR_PASS_THROUGH; /* default exit state - pass through */

	switch (event->type) {
		case ESCKEY:
		case RIGHTMOUSE:
			estate = OPERATOR_CANCELLED;
			break;
		case LEFTMOUSE:
			estate = OPERATOR_FINISHED;
			break;
	}
	/* process last operations before exiting */
	switch (estate) {
		case OPERATOR_FINISHED:
			gpencil_colorpick_exit(C, op);
			break;
		
		case OPERATOR_CANCELLED:
			gpencil_colorpick_exit(C, op);
			break;
		
		case OPERATOR_RUNNING_MODAL | OPERATOR_PASS_THROUGH:
			break;
	}
	
	/* return status code */
	return estate;
}

void GPENCIL_OT_colorpick(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Grease Pencil Color Picker";
	ot->idname = "GPENCIL_OT_colorpick";
	ot->description = "Select a color from visual palette";
	
	/* api callbacks */
	ot->invoke = gpencil_colorpick_invoke;
	ot->modal = gpencil_colorpick_modal;
	ot->poll = gpencil_colorpick_poll;
	ot->cancel = gpencil_colorpick_cancel;

	/* flags */
	ot->flag = OPTYPE_BLOCKING;
}
