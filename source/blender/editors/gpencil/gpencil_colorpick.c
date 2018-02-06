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
#include "BLI_rect.h"
#include "BLI_stack.h"

#include "BLT_translation.h"

#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_windowmanager_types.h"
#include "DNA_userdef_types.h"

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

#define GP_BOX_SIZE (32 * U.ui_scale)
#define GP_BOX_GAP (24 * U.ui_scale)

/* Representation of a color displayed in the picker */
typedef struct tGPDpickColor {
	char name[64];   /* color name. Must be unique. */
	rcti full_rect;  /* full size of region occupied by color box (for event/highlight handling) */
	rcti rect;       /* box position */
	int index;       /* index of color in palette */
	float rgba[4];   /* color */
	float fill[4];   /* fill color */
	bool fillmode;   /* flag fill is not enabled */
} tGPDpickColor;

/* Temporary color picker operation data (op->customdata) */
typedef struct tGPDpick {
	struct wmWindow *win;               /* window */
	struct Scene *scene;                /* current scene from context */
	struct ToolSettings *ts;            /* current toolsettings from context */
	struct Object *ob;                  /* current active gp object */
	struct ScrArea *sa;                 /* area where painting originated */
	struct ARegion *ar;                 /* region where painting originated */
	struct Palette *palette;            /* current palette */
	struct bGPDbrush *brush;            /* current brush */
	short bflag;                        /* previous brush flag */

	int center[2];                      /* mouse center position */
	rcti rect;                          /* visible area */
	rcti panel;                         /* panel area */
	int row, col;                       /* number of rows and columns */ 
	int boxsize[2];                     /* size of each box color */

	int totcolor;                       /* number of colors */
	int curindex;                       /* index of color under cursor */
	tGPDpickColor *colors;              /* colors of palette */

	void *draw_handle_3d;               /* handle for drawing strokes while operator is running */
} tGPDpick;


/* draw color name using default font */
static void gp_draw_color_name(tGPDpick *tgpk, tGPDpickColor *col, const uiFontStyle *fstyle, bool focus)
{
	bTheme *btheme = UI_GetTheme();
	uiWidgetColors menuBack = btheme->tui.wcol_menu_back;

	char drawstr[UI_MAX_DRAW_STR];
	const float okwidth = tgpk->boxsize[0];
	const size_t max_len = sizeof(drawstr);
	const float minwidth = (float)(UI_DPI_ICON_SIZE);

	unsigned char text_col[4];
	if (focus) {
		copy_v4_v4_char(text_col, menuBack.text_sel);
	}
	else {
		copy_v4_v4_char(text_col, menuBack.text);
	}

	/* color name */
	BLI_strncpy(drawstr, col->name, sizeof(drawstr));
	UI_text_clip_middle_ex((uiFontStyle *)fstyle, drawstr, okwidth, minwidth, max_len, '\0');
	UI_fontstyle_draw_simple(fstyle, col->rect.xmin, col->rect.ymin - (GP_BOX_GAP / 2) - (3 * U.ui_scale), 
							 drawstr, text_col);
}

/* draw a pattern for alpha display */
static void gp_draw_pattern_box(int xmin, int ymin, int xmax, int ymax)
{
	unsigned int position;

	Gwn_VertFormat *format = immVertexFormat();
	position = GWN_vertformat_attr_add(format, "pos", GWN_COMP_F32, 2, GWN_FETCH_FLOAT);
	immBindBuiltinProgram(GPU_SHADER_2D_CHECKER);

	/* Drawing the checkerboard. */
	immUniform4f("color1", UI_ALPHA_CHECKER_DARK / 255.0f, UI_ALPHA_CHECKER_DARK / 255.0f, UI_ALPHA_CHECKER_DARK / 255.0f, 1.0f);
	immUniform4f("color2", UI_ALPHA_CHECKER_LIGHT / 255.0f, UI_ALPHA_CHECKER_LIGHT / 255.0f, UI_ALPHA_CHECKER_LIGHT / 255.0f, 1.0f);
	immUniform1i("size", 8);
	immRectf(position, xmin, ymin, xmax, ymax);
	immUnbindProgram();
}

/* draw a toolbar with all colors of the palette */
static void gpencil_draw_color_table(const bContext *UNUSED(C), tGPDpick *tgpk)
{
	if (!tgpk->palette) {
		return;
	}
	const uiFontStyle *fstyle = UI_FSTYLE_WIDGET;
	float background[4];
	float line[4];
	float selcolor[4];
	float wcolor[4] = { 0.9f, 0.9f, 0.9f, 0.8f };
	float radius = (0.2f * U.widget_unit);

	/* boxes for stroke and fill color */
	rcti sbox;
	rcti fbox;

	bTheme *btheme = UI_GetTheme();
	uiWidgetColors menuBack = btheme->tui.wcol_menu_back;
	uiWidgetColors menuItem = btheme->tui.wcol_menu_item;

	rgba_uchar_to_float(line, menuBack.outline);
	rgba_uchar_to_float(background, menuBack.inner);
	rgba_uchar_to_float(selcolor, menuItem.inner_sel);

	/* draw panel background */
	/* TODO: Draw soft drop shadow behind this (like standard menus)? */
	glEnable(GL_BLEND);
	UI_draw_roundbox_corner_set(UI_CNR_ALL);
	UI_draw_roundbox_4fv(true, tgpk->panel.xmin, tgpk->panel.ymin,
						tgpk->panel.xmax, tgpk->panel.ymax,
						radius, background);
	glDisable(GL_BLEND);

	/* draw color boxes */
	tGPDpickColor *col = tgpk->colors;
	glLineWidth(1.0);
	for (int i = 0; i < tgpk->totcolor; i++, col++) {
		const bool focus = (tgpk->curindex == i);

		const int scalex = (col->rect.xmax - col->rect.xmin) / 3;
		const int scaley = (col->rect.ymax - col->rect.ymin) / 3;

		sbox.xmin = col->rect.xmin;
		sbox.ymin = col->rect.ymin + scaley;
		sbox.xmax = col->rect.xmax - scalex;
		sbox.ymax = col->rect.ymax;

		fbox.xmin = col->rect.xmin + scalex;
		fbox.ymin = col->rect.ymin;
		fbox.xmax = col->rect.xmax;
		fbox.ymax = col->rect.ymax - scaley;

		glEnable(GL_BLEND);
		glEnable(GL_LINE_SMOOTH);


		/* highlight background of item under mouse */
		if (i == tgpk->curindex) {
			/* TODO: How to get the menu gradient shading? */
			rcti *cbox = &col->full_rect;
			UI_draw_roundbox_4fv(true,
			                     cbox->xmin, cbox->ymin,
			                     cbox->xmax, cbox->ymax,
			                     0, selcolor);
		}

		/* fill box */
		UI_draw_roundbox_4fv(true, fbox.xmin, fbox.ymin, fbox.xmax, fbox.ymax, radius, wcolor);
		gp_draw_pattern_box(fbox.xmin + 2, fbox.ymin + 2, fbox.xmax - 2, fbox.ymax - 2);
		UI_draw_roundbox_4fv(true, fbox.xmin, fbox.ymin, fbox.xmax, fbox.ymax,
			radius, col->fill);
		UI_draw_roundbox_4fv(false, fbox.xmin, fbox.ymin, fbox.xmax, fbox.ymax,
			radius, line);

		/* stroke box */
		UI_draw_roundbox_4fv(true, sbox.xmin, sbox.ymin, sbox.xmax, sbox.ymax, radius, wcolor);
		gp_draw_pattern_box(sbox.xmin + 2, sbox.ymin + 2, sbox.xmax - 2, sbox.ymax - 2);
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
			CTX_wm_operator_poll_msg_set(C, "Operator only works in the 3D view");
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
static tGPDpick *gpencil_colorpick_init(bContext *C, wmOperator *op, const wmEvent *event)
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

	/* disable brush cursor
	 * (so it doesn't distract when moving between colors)
	 */
	tgpk->brush = BKE_gpencil_brush_getactive(ts);
	tgpk->bflag = tgpk->brush->flag;
	tgpk->brush->flag &= ~GP_BRUSH_ENABLE_CURSOR;

	tgpk->center[0] = event->mval[0];
	tgpk->center[1] = event->mval[1];

	ED_region_visible_rect(tgpk->ar, &tgpk->rect);

	/* get current palette */
	bGPDpaletteref *palslot = BKE_gpencil_paletteslot_validate(bmain, gpd);
	tgpk->palette = palslot->palette;

	/* allocate color table */
	tgpk->totcolor = BLI_listbase_count(&tgpk->palette->colors);
	tgpk->curindex = tgpk->palette->active_color;
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
	if (tgpk->totcolor < 25) {
		CLAMP(tgpk->row, 1, 3);
	}
	else if (tgpk->totcolor < 72) {
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
	int width = (GP_BOX_SIZE * tgpk->col) + (GP_BOX_GAP * tgpk->col);
	int height = (GP_BOX_SIZE * tgpk->row) + (GP_BOX_GAP * (tgpk->row + 1)) - (GP_BOX_GAP / 2);
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
		tcolor->index = idx;

		BLI_strncpy(tcolor->name, palcol->info, sizeof(tcolor->name));
		copy_v4_v4(tcolor->rgba, palcol->rgb);
		copy_v4_v4(tcolor->fill, palcol->fill);
		tcolor->fillmode = (palcol->fill[3] > 0.0f);

		/* box position */
		tcolor->rect.xmin = tgpk->panel.xmin + (tgpk->boxsize[0] * col) + (GP_BOX_GAP * (col + 1)) - (GP_BOX_GAP / 2);
		tcolor->rect.xmax = tcolor->rect.xmin + tgpk->boxsize[0];

		tcolor->rect.ymax = tgpk->panel.ymax - (tgpk->boxsize[1] * row) - (GP_BOX_GAP * row) - (GP_BOX_GAP / 2);
		tcolor->rect.ymin = tcolor->rect.ymax - tgpk->boxsize[0];

		/* "full" hit region  (used for UI highlight and event testing) */
		// XXX: It would be nice to have these larger, to allow for a less laggy feel (due the hit-region misses)
		tcolor->full_rect.xmin = tcolor->rect.xmin - (GP_BOX_GAP / 4);
		tcolor->full_rect.xmax = tcolor->rect.xmax + (GP_BOX_GAP / 4);
		tcolor->full_rect.ymin = tcolor->rect.ymin - (GP_BOX_GAP / 4);
		tcolor->full_rect.ymax = tcolor->rect.ymax + (GP_BOX_GAP / 4);

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
	op->customdata = tgpk;
	return tgpk;
}

/* end operator */
static void gpencil_colorpick_exit(bContext *C, wmOperator *op)
{
	tGPDpick *tgpk = op->customdata;

	/* don't assume that operator data exists at all */
	if (tgpk) {
		/* remove drawing handler */
		if (tgpk->draw_handle_3d) {
			ED_region_draw_cb_exit(tgpk->ar->type, tgpk->draw_handle_3d);
		}
		/* free color table */
		MEM_SAFE_FREE(tgpk->colors);

		/* reset brush flags */
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

/* start of interactive part of operator */
static int gpencil_colorpick_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	tGPDpick *tgpk = gpencil_colorpick_init(C, op, event);

	/* Enable custom drawing handlers */
	tgpk->draw_handle_3d = ED_region_draw_cb_activate(tgpk->ar->type, gpencil_colorpick_draw_3d, tgpk, REGION_DRAW_POST_PIXEL);

	WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED, NULL);

	/* add a modal handler for this operator*/
	WM_event_add_modal_handler(C, op);

	return OPERATOR_RUNNING_MODAL;
}

/* get active color under the cursor */
/* FIXME: Can we do this without looping? */
static int gpencil_colorpick_index_from_mouse(const tGPDpick *tgpk, const wmEvent *event)
{
	tGPDpickColor *tcol = tgpk->colors;

	for (int i = 0; i < tgpk->totcolor; i++, tcol++) {
		if (BLI_rcti_isect_pt_v(&tcol->full_rect, event->mval)) {
			return tcol->index;
		}
	}

	return -1;
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

		case LEFTMOUSE:
			if (!BLI_rcti_isect_pt_v(&tgpk->panel, event->mval)) {
				/* if click out of panel, end */
				estate = OPERATOR_CANCELLED;
			}
			else {
				int index = gpencil_colorpick_index_from_mouse(tgpk, event);
				if (index != -1) {
					tgpk->palette->active_color = index;
					estate = OPERATOR_FINISHED;
				}
			}
			break;

		case MOUSEMOVE:
			if (BLI_rcti_isect_pt_v(&tgpk->panel, event->mval)) {
				int index = gpencil_colorpick_index_from_mouse(tgpk, event);
				if (index != -1) {
					/* don't update active color if we move outside the grid */
					tgpk->curindex = index;
					ED_region_tag_redraw(CTX_wm_region(C));
				}
			}
			break;
	}
	/* process last operations before exiting */
	switch (estate) {
		case OPERATOR_FINISHED:
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
	ot->flag = OPTYPE_UNDO | OPTYPE_BLOCKING;
}
