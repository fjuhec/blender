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
 * Contributor(s): Joshua Leung, Antonio Vazquez
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/gpencil/gpencil_fill.c
 *  \ingroup edgpencil
 */

#include <stdio.h>

#include "BLI_utildefines.h"
#include "BLI_blenlib.h"

#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_windowmanager_types.h"

#include "BKE_gpencil.h"
#include "BKE_context.h"
#include "BKE_screen.h"

#include "ED_gpencil.h"
#include "ED_screen.h"

#include "WM_api.h"
#include "WM_types.h"

/* check if context is suitable for filling */
static int gpencil_fill_poll(bContext *C)
{
	if (ED_operator_regionactive(C)) {
		ScrArea *sa = CTX_wm_area(C);
		if (sa->spacetype == SPACE_VIEW3D) {
			return 1;
		}
		else {
			CTX_wm_operator_poll_msg_set(C, "Active region not valid for filling operator");
			return 0;
		}
	}
	else {
		CTX_wm_operator_poll_msg_set(C, "Active region not set");
		return 0;
	}
	return 0;
}

/* start of interactive part of operator */
static int gpencil_fill_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	WM_cursor_modal_set(CTX_wm_window(C), BC_PAINTBRUSHCURSOR);

	/* add a modal handler for this operator*/
	WM_event_add_modal_handler(C, op);

	return OPERATOR_RUNNING_MODAL;
}

/* end operator */
static void gpencil_fill_exit(bContext *C, wmOperator *op)
{
	Object *ob = CTX_data_active_object(C);
	/* restore cursor to indicate end of fill */
	WM_cursor_modal_restore(CTX_wm_window(C));

	/* drawing batch cache is dirty now */
	if ((ob) && (ob->type == OB_GPENCIL) && (ob->data)) {
		bGPdata *gpd = ob->data;
		BKE_gpencil_batch_cache_dirty(gpd);
		gpd->flag |= GP_DATA_CACHE_IS_DIRTY;
	}
}

static void gpencil_fill_cancel(bContext *C, wmOperator *op)
{
	/* this is just a wrapper around exit() */
	gpencil_fill_exit(C, op);
}

/* events handling during interactive part of operator */
static int gpencil_fill_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
	int estate = OPERATOR_PASS_THROUGH; /* default exit state - pass through */
	
	/* we don't pass on key events, GP is used with key-modifiers - prevents Dkey to insert drivers */
	if (ISKEYBOARD(event->type)) {
		if (ELEM(event->type, ESCKEY)) {
			estate = OPERATOR_CANCELLED;
		}
	}
	
	if ELEM(event->type, LEFTMOUSE) {
		ARegion *ar = BKE_area_find_region_xy(CTX_wm_area(C), RGN_TYPE_ANY, event->x, event->y);
		if (ar) {
			rcti region_rect;
			bool in_bounds = false;

			/* Perform bounds check */
			ED_region_visible_rect(ar, &region_rect);
			in_bounds = BLI_rcti_isect_pt_v(&region_rect, event->mval);

			if ((in_bounds) && (ar->regiontype == RGN_TYPE_WINDOW)) {
				/* TODO: Add fill code here */
				printf("(%d, %d) Do all here!\n", event->mval[0], event->mval[1]);
				estate = OPERATOR_FINISHED;
			}
			else {
				estate = OPERATOR_CANCELLED;
			}
		}
		else {
			estate = OPERATOR_CANCELLED;
		}
	}
	
	/* process last operations before exiting */
	switch (estate) {
		case OPERATOR_FINISHED:
			gpencil_fill_exit(C, op);
			WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED, NULL);
			break;
		
		case OPERATOR_CANCELLED:
			gpencil_fill_exit(C, op);
			break;
		
		case OPERATOR_RUNNING_MODAL | OPERATOR_PASS_THROUGH:
			/* event doesn't need to be handled */
			break;
	}
	
	/* return status code */
	return estate;
}

void GPENCIL_OT_fill(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Grease Pencil Fill";
	ot->idname = "GPENCIL_OT_fill";
	ot->description = "Fill with color the shape formed by strokes";
	
	/* api callbacks */
	ot->invoke = gpencil_fill_invoke;
	ot->modal = gpencil_fill_modal;
	ot->poll = gpencil_fill_poll;
	ot->cancel = gpencil_fill_cancel;

	/* flags */
	ot->flag = OPTYPE_BLOCKING;
	
}
