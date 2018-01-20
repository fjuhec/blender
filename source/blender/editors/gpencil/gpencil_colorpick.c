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

#define GP_BOX_SIZE (40 * U.ui_scale)
#define GP_BOX_GAP (18 * U.ui_scale)

/* draw color name using default font */
static void gp_draw_color_name(tGPDpick *tgpk, tGPDpickColor *col, const uiFontStyle *fstyle, bool focus)
{
	char drawstr[UI_MAX_DRAW_STR];
	const float okwidth = tgpk->boxsize[0];
	const size_t max_len = sizeof(drawstr);
	const float minwidth = (float)(UI_DPI_ICON_SIZE);

	unsigned char text_col[4];
	if (focus) {
		UI_GetThemeColor4ubv(TH_TEXT_HI, text_col);
	}
	else {
		UI_GetThemeColor4ubv(TH_TEXT, text_col);
	}

	/* color name */
	BLI_strncpy(drawstr, col->name, sizeof(drawstr));
	UI_text_clip_middle_ex(fstyle, drawstr, okwidth, minwidth, max_len, '\0');
	UI_fontstyle_draw_simple(fstyle, col->rect.xmin, col->rect.ymin - (GP_BOX_GAP / 2) - 3, 
							 drawstr, text_col);
}

/* draw a pattern for alpha display */
static void gp_draw_pattern_box(int xmin, int ymin, int xmax, int ymax)
{
	rcti box;
	box.xmin = xmin;
	box.ymin = ymin;
	box.xmax = xmax;
	box.ymax = ymax;

	rcti rect;
	const int lvl = 3;
	const int size = (box.xmax - box.xmin) / lvl;
	float gcolor[4] = { 0.6f, 0.6f, 0.6f, 0.5f };

	/* draw a pattern of boxes */
	int i = 1;
	for (int a = 0; a < lvl; a++) {
		for (int b = 0; b < lvl; b++) {
			rect.xmin = box.xmin + (size * a);
			rect.xmax = rect.xmin + size;

			rect.ymin = box.ymin + (size * b);
			rect.ymax = rect.ymin + size;

			if (i % 2 == 0) {
				UI_draw_roundbox_4fv(true, rect.xmin, rect.ymin, rect.xmax, rect.ymax,
									0.0f, gcolor);
			}
			i++;
		}
	}
}

/* draw a toolbar with all colors of the palette */
static void gpencil_draw_color_table(const bContext *UNUSED(C), tGPDpick *tgpk)
{
	if (!tgpk->palette) {
		return;
	}
	const uiFontStyle *fstyle = UI_FSTYLE_WIDGET;
	float ink[4];
	float line[4];
	float radius = (0.4f * U.widget_unit);
	float wcolor[4] = { 0.9f, 0.9f, 0.9f, 0.8f };

	/* boxes for stroke and fill color */
	rcti sbox;
	rcti fbox;

	UI_GetThemeColor3fv(TH_TAB_OUTLINE, line);
	line[3] = 1.0f;

	/* draw panel background */
	UI_GetThemeColor4fv(TH_PANEL_BACK, ink);
	ink[3] = 0.8f;
	glEnable(GL_BLEND);
	UI_draw_roundbox_corner_set(UI_CNR_ALL);
	UI_draw_roundbox_4fv(true, tgpk->panel.xmin, tgpk->panel.ymin,
						tgpk->panel.xmax, tgpk->panel.ymax,
						radius, ink);
	glDisable(GL_BLEND);

	/* draw color boxes */
	tGPDpickColor *col = tgpk->colors;
	for (int i = 0; i < tgpk->totcolor; i++, col++) {
		bool focus = false;
		glEnable(GL_BLEND);
		glEnable(GL_LINE_SMOOTH);

		int scalex = (col->rect.xmax - col->rect.xmin) / 3;
		int scaley = (col->rect.ymax - col->rect.ymin) / 3;
		sbox.xmin = col->rect.xmin;
		sbox.ymin = col->rect.ymin + scaley;
		sbox.xmax = col->rect.xmax - scalex;
		sbox.ymax = col->rect.ymax;

		fbox.xmin = col->rect.xmin + scalex;
		fbox.ymin = col->rect.ymin;
		fbox.xmax = col->rect.xmax;
		fbox.ymax = col->rect.ymax - scaley;

		/* focus to current color */
		if (tgpk->palette->active_color == col->index) {
			focus = true;
		}
		/* fill box */
		UI_draw_roundbox_4fv(true, fbox.xmin, fbox.ymin, fbox.xmax, fbox.ymax, radius, wcolor);
		gp_draw_pattern_box(fbox.xmin + 3, fbox.ymin + 3, fbox.xmax - 2, fbox.ymax - 2);
		UI_draw_roundbox_4fv(true, fbox.xmin, fbox.ymin, fbox.xmax, fbox.ymax,
			radius, col->fill);
		UI_draw_roundbox_4fv(false, fbox.xmin, fbox.ymin, fbox.xmax, fbox.ymax,
			radius, line);

		/* stroke box */
		UI_draw_roundbox_4fv(true, sbox.xmin, sbox.ymin, sbox.xmax, sbox.ymax, radius, wcolor);
		gp_draw_pattern_box(sbox.xmin + 3, sbox.ymin + 3, sbox.xmax - 2, sbox.ymax - 2);
		UI_draw_roundbox_4fv(true, sbox.xmin, sbox.ymin, sbox.xmax, sbox.ymax,
			radius, col->rgba);
		UI_draw_roundbox_4fv(false, sbox.xmin, sbox.ymin, sbox.xmax, sbox.ymax,
			radius, line);

		glDisable(GL_BLEND);
		glDisable(GL_LINE_SMOOTH);

		/* draw color name */
		gp_draw_color_name(tgpk, col, fstyle, focus);
	}
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

/* get total number of available colors for brush */
static int get_tot_colors(tGPDpick *tgpk)
{
	int tot = 0;
	for (PaletteColor *palcol = tgpk->palette->colors.first; palcol; palcol = palcol->next) {

		/* Must use a color with fill with fill brushes */
		if (tgpk->brush->flag & GP_BRUSH_FILL_ONLY) {
			if ((palcol->fill[3] < GPENCIL_ALPHA_OPACITY_THRESH) &&
				((tgpk->brush->flag & GP_BRUSH_FILL_ALLOW_STROKEONLY) == 0))
			{
				continue;
			}
		}
		tot++;
	}
	return tot;
}

/* Allocate memory and initialize values */
static tGPDpick *gp_session_init_colorpick(bContext *C, wmOperator *op, const wmEvent *event)
{
	tGPDpick *tgpk = MEM_callocN(sizeof(tGPDpick), __func__);

	/* define initial values */
	ToolSettings *ts = CTX_data_tool_settings(C);
	bGPdata *gpd = CTX_data_gpencil_data(C);
	Main *bmain = CTX_data_main(C);

	/* set current scene and window info */
	tgpk->win = CTX_wm_window(C);
	tgpk->scene = CTX_data_scene(C);
	tgpk->ts = CTX_data_tool_settings(C);
	tgpk->ob = CTX_data_active_object(C);
	tgpk->sa = CTX_wm_area(C);
	tgpk->ar = CTX_wm_region(C);
	tgpk->brush = BKE_gpencil_brush_getactive(ts);
	tgpk->bflag = tgpk->brush->flag;
	/* disable cursor for brush */
	tgpk->brush->flag &= ~GP_BRUSH_ENABLE_CURSOR;

	tgpk->center[0] = event->mval[0];
	tgpk->center[1] = event->mval[1];

	ED_region_visible_rect(tgpk->ar, &tgpk->rect);

	/* get current palette */
	bGPDpaletteref *palslot = BKE_gpencil_paletteslot_validate(bmain, gpd);
	tgpk->palette = palslot->palette;

	/* allocate color table */
	tgpk->totcolor = get_tot_colors(tgpk);
	if (tgpk->totcolor > 0) {
		tgpk->colors = MEM_callocN(sizeof(tGPDpickColor) * tgpk->totcolor, "gp_colorpicker");
	}

	/* set size of color box */
	tgpk->boxsize[0] = GP_BOX_SIZE;
	tgpk->boxsize[1] = GP_BOX_SIZE;

	/* get number of rows and columns */
	tgpk->row = (tgpk->rect.ymax - tgpk->rect.ymin - GP_BOX_GAP) / (tgpk->boxsize[1] + GP_BOX_GAP);
	if (tgpk->row > tgpk->totcolor) {
		tgpk->row = tgpk->totcolor;
	}
	/* if there are too many colors, use more rows */
	if (tgpk->totcolor < 72) {
		CLAMP(tgpk->row, 1, 6);
	}
	else {
		CLAMP(tgpk->row, 1, 9);
	}
	tgpk->col = tgpk->totcolor / tgpk->row;
	if (tgpk->totcolor % tgpk->row > 0) {
		tgpk->col++;
	}
	CLAMP_MIN(tgpk->col, 1);

	/* define panel size */
	int width = (GP_BOX_SIZE * tgpk->col) + (GP_BOX_GAP * (tgpk->col + 1));
	int height = (GP_BOX_SIZE * tgpk->row) + (GP_BOX_GAP * (tgpk->row + 1));
	tgpk->panel.xmin = tgpk->center[0] - (width / 2) + tgpk->rect.xmin;
	tgpk->panel.ymin = tgpk->center[1] - (height / 2) + tgpk->rect.ymin;

	/* center in visible area. If panel is outside visible area, move panel to make all visible */
	if (tgpk->panel.xmin < tgpk->rect.xmin) {
		tgpk->panel.xmin = tgpk->rect.xmin;
	}
	if (tgpk->panel.ymin < tgpk->rect.ymin) {
		tgpk->panel.ymin = tgpk->rect.ymin;
	}

	tgpk->panel.xmax = tgpk->panel.xmin + width;
	tgpk->panel.ymax = tgpk->panel.ymin + height;

	if (tgpk->panel.xmax > tgpk->rect.xmax) {
		tgpk->panel.xmin = tgpk->rect.xmax - width;
		tgpk->panel.xmax = tgpk->panel.xmin + width;
	}
	if (tgpk->panel.ymax > tgpk->rect.ymax) {
		tgpk->panel.ymin = tgpk->rect.ymax - height;
		tgpk->panel.ymax = tgpk->panel.ymin + height;
	}

	/* load color table in temp data */
	tGPDpickColor *tcolor = tgpk->colors;
	Palette *palette = tgpk->palette;
	int idx = 0;
	int row = 0;
	int col = 0;
	int t = 0;
	for (PaletteColor *palcol = palette->colors.first; palcol; palcol = palcol->next) {
		/* Must use a color with fill with fill brushes */
		if (tgpk->brush->flag & GP_BRUSH_FILL_ONLY) {
			if ((palcol->fill[3] < GPENCIL_ALPHA_OPACITY_THRESH) &&
				((tgpk->brush->flag & GP_BRUSH_FILL_ALLOW_STROKEONLY) == 0))
			{
				idx++;
				continue;
			}
		}

		BLI_strncpy(tcolor->name, palcol->info, sizeof(tcolor->name));
		tcolor->index = idx;
		copy_v4_v4(tcolor->rgba, palcol->rgb);
		copy_v4_v4(tcolor->fill, palcol->fill);
		if (palcol->fill[3] > 0.0f) {
			tcolor->fillmode = true;
		}
		else {
			tcolor->fillmode = false;
		}

		/* box position */
		tcolor->rect.xmin = tgpk->panel.xmin + (tgpk->boxsize[0] * col) + (GP_BOX_GAP * (col + 1));
		tcolor->rect.xmax = tcolor->rect.xmin + tgpk->boxsize[0];

		tcolor->rect.ymax = tgpk->panel.ymax - (tgpk->boxsize[1] * row) - (GP_BOX_GAP * row) - (GP_BOX_GAP / 2);
		tcolor->rect.ymin = tcolor->rect.ymax - tgpk->boxsize[0];

		idx++;
		row++;
		tcolor++;

		if (row > tgpk->row - 1) {
			row = 0;
			col++;
		}

		t++;
	}
	tgpk->totcolor = t;

	/* return context data for running operator */
	return tgpk;
}

/* end operator */
static void gpencil_colorpick_exit(bContext *C, wmOperator *op)
{
	Main *bmain = CTX_data_main(C);
	Object *ob = CTX_data_active_object(C);

	/* restore cursor to indicate end */
	WM_cursor_modal_restore(CTX_wm_window(C));

	tGPDpick *tgpk = op->customdata;

	/* don't assume that operator data exists at all */
	if (tgpk) {
		/* remove drawing handler */
		if (tgpk->draw_handle_3d) {
			ED_region_draw_cb_exit(tgpk->ar->type, tgpk->draw_handle_3d);
		}
		/* free color table */
		MEM_SAFE_FREE(tgpk->colors);

		/* rest brush flags */
		tgpk->brush->flag = tgpk->bflag;

		/* finally, free memory used by temp data */
		MEM_freeN(tgpk);
	}

	/* clear pointer */
	op->customdata = NULL;

	WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED, NULL);
}

/* cancel operator */
static void gpencil_colorpick_cancel(bContext *C, wmOperator *op)
{
	/* this is just a wrapper around exit() */
	gpencil_colorpick_exit(C, op);
}

/* Init: Allocate memory and set init values */
static int gpencil_colorpick_init(bContext *C, wmOperator *op, const wmEvent *event)
{
	tGPDpick *tgpk;

	/* check context */
	tgpk = op->customdata = gp_session_init_colorpick(C, op, event);
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
	if (!gpencil_colorpick_init(C, op, event)) {
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

	WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED, NULL);

	/* add a modal handler for this operator*/
	WM_event_add_modal_handler(C, op);

	return OPERATOR_RUNNING_MODAL;
}

/* set active color when user select one box of the toolbar */
static bool set_color(const wmEvent *event, tGPDpick *tgpk)
{
	tGPDpickColor *tcol = tgpk->colors;
	/* if click out of panel end */
	if ((event->mval[0] <= tgpk->panel.xmin) || (event->mval[0] >= tgpk->panel.xmax) ||
		(event->mval[1] <= tgpk->panel.ymin) || (event->mval[1] >= tgpk->panel.ymax))
	{
		return true;
	}

	for (int i = 0; i < tgpk->totcolor; i++, tcol++) {
		if ((event->mval[0] >= tcol->rect.xmin) && (event->mval[0] <= tcol->rect.xmax) &&
			(event->mval[1] >= tcol->rect.ymin) && (event->mval[1] <= tcol->rect.ymax))
		{
			tgpk->palette->active_color = tcol->index;
			return true;
		}
	}

	return false;
}

/* events handling during interactive part of operator */
static int gpencil_colorpick_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
	Scene *scene = CTX_data_scene(C);
	Object *ob = CTX_data_active_object(C);
	tGPDpick *tgpk = op->customdata;

	int estate = OPERATOR_RUNNING_MODAL; 

	switch (event->type) {
		case ESCKEY:
		case RIGHTMOUSE:
			estate = OPERATOR_CANCELLED;
			break;
		case MOUSEMOVE:
			if ((event->mval[0] >= tgpk->panel.xmin) && (event->mval[0] <= tgpk->panel.xmax) &&
				(event->mval[1] >= tgpk->panel.ymin) && (event->mval[1] <= tgpk->panel.ymax)) 
			{
				WM_cursor_modal_set(tgpk->win, BC_EYEDROPPER_CURSOR);
			}
			else {
				WM_cursor_modal_set(tgpk->win, CURSOR_STD);
			}
			break;
		case LEFTMOUSE:
			if (set_color(event, tgpk) == true) {
				estate = OPERATOR_FINISHED;
			}
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
		
		case OPERATOR_RUNNING_MODAL:
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
