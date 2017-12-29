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
 * The Original Code is Copyright (C) 2017, Blender Foundation
 * This is a new part of Blender
 *
 * Contributor(s): Antonio Vazquez
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 * Operators for creating new Grease Pencil primitives (boxes, circles, ...)
 */

/** \file blender/editors/gpencil/gpencil_primitive.c
 *  \ingroup edgpencil
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_math.h"

#include "BLT_translation.h"

#include "DNA_gpencil_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"

#include "BKE_main.h"
#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_gpencil.h"
#include "BKE_library.h"
#include "BKE_paint.h"
#include "BKE_report.h"

#include "UI_interface.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "ED_gpencil.h"
#include "ED_object.h"
#include "ED_screen.h"
#include "ED_view3d.h"
#include "ED_space_api.h"

#include "gpencil_intern.h"

#define MIN_EDGES 3
#define MAX_EDGES 100

#define IDLE 0
#define IN_PROGRESS 1

/* ************************************************ */
/* Core/Shared Utilities */

/* Poll callback for primitive operators */
static int gpencil_view3d_poll(bContext *C)
{
	bGPdata *gpd = CTX_data_gpencil_data(C);
	bGPDlayer *gpl = CTX_data_active_gpencil_layer(C);
	
	/* only 3D view */
	ScrArea *sa = CTX_wm_area(C);
	if (sa && sa->spacetype != SPACE_VIEW3D) {
		return 0;
	}
	
	/* need data to create primitive */
	if (ELEM(NULL, gpd, gpl)) {
		return 0;
	}

	/* only in paint mode */
	if ((gpd->flag & GP_DATA_STROKE_PAINTMODE) == 0) {
		return 0;
	}

	return 1;
}


/* ****************** Primitive Interactive *********************** */

/* Helper: Create internal strokes primitives data */
static void gp_primitive_set_initdata(bContext *C, tGPDprimitive *tgpi)
{
	Scene *scene = CTX_data_scene(C);
	ToolSettings *ts = CTX_data_tool_settings(C);
	bGPDlayer *gpl = CTX_data_active_gpencil_layer(C);
	bGPDbrush *brush;

	/* if not exist, create a new one */
	if (BLI_listbase_is_empty(&ts->gp_brushes)) {
		/* create new brushes */
		BKE_gpencil_brush_init_presets(ts);
		brush = BKE_gpencil_brush_getactive(ts);
	}
	else {
		/* Use the current */
		brush = BKE_gpencil_brush_getactive(ts);
	}
	tgpi->brush = brush;

	tgpi->cframe = CFRA;
	tgpi->gpl = gpl;

	/* create a new temporary frame */
	tgpi->gpf = MEM_callocN(sizeof(bGPDframe), "Temp bGPDframe");
	tgpi->gpf->framenum = tgpi->cframe;

	/* create new temp stroke */
	bGPDstroke *gps = MEM_callocN(sizeof(bGPDstroke), "Temp bGPDstroke");
	gps->thickness = 2.0f;
	gps->inittime = 0.0f;

	/* enable recalculation flag by default */
	gps->flag |= GP_STROKE_RECALC_CACHES;
	/* the polygon must be closed, so enabled cyclic */
	gps->flag |= GP_STROKE_CYCLIC;
	gps->flag |= GP_STROKE_3DSPACE;

	gps->palette = tgpi->palette;
	gps->palcolor = tgpi->palcolor;

	/* allocate memory for storage points, but keep empty */
	gps->totpoints = 0;
	gps->points = MEM_callocN(sizeof(bGPDspoint), "gp_stroke_points");
	/* initialize triangle memory to dummy data */
	gps->tot_triangles = 0;
	gps->triangles = NULL;
	gps->flag |= GP_STROKE_RECALC_CACHES;

	/* add to strokes */
	BLI_addtail(&tgpi->gpf->strokes, gps);
}

/* ----------------------- */
/* Drawing Callbacks */

/* Drawing callback for modal operator in 3d mode */
static void gpencil_primitive_draw_3d(const bContext *C, ARegion *UNUSED(ar), void *arg)
{
	tGPDprimitive *tgpi = (tGPDprimitive *)arg;
	ED_gp_draw_primitives(C, tgpi, REGION_DRAW_POST_VIEW);
}

/* ----------------------- */

/* Helper: Draw status message while the user is running the operator */
static void gpencil_primitive_status_indicators(tGPDprimitive *tgpi)
{
	Scene *scene = tgpi->scene;
	char status_str[UI_MAX_DRAW_STR];
	char msg_str[UI_MAX_DRAW_STR];
	
	if (tgpi->type == GP_STROKE_BOX) {
		BLI_strncpy(msg_str, IFACE_("GP Primitive: ESC/RMB to cancel, LMB set origin, Enter/LMB to confirm, Shift to square"), UI_MAX_DRAW_STR);
	}
	else {
		BLI_strncpy(msg_str, IFACE_("Circle: ESC/RMB to cancel, Enter/LMB to confirm, WHEEL to adjust edge number, Shift to square"), UI_MAX_DRAW_STR);
	}

	if (tgpi->type == GP_STROKE_CIRCLE) {
		if (hasNumInput(&tgpi->num)) {
			char str_offs[NUM_STR_REP_LEN];

			outputNumInput(&tgpi->num, str_offs, &scene->unit);
			BLI_snprintf(status_str, sizeof(status_str), "%s: %s", msg_str, str_offs);
		}
		else {
			if (tgpi->flag == IN_PROGRESS) {
				BLI_snprintf(status_str, sizeof(status_str), "%s: %d (%d, %d) (%d, %d)", msg_str, (int)tgpi->tot_edges, 
							 tgpi->top[0], tgpi->top[1], tgpi->bottom[0], tgpi->bottom[1]);
			}
			else {
				BLI_snprintf(status_str, sizeof(status_str), "%s: %d (%d, %d)", msg_str, (int)tgpi->tot_edges, 
							 tgpi->bottom[0], tgpi->bottom[1]);
			}
		}
	}
	else {
		if (tgpi->flag == IN_PROGRESS) {
			BLI_snprintf(status_str, sizeof(status_str), "%s: (%d, %d) (%d, %d)", msg_str, 
						 tgpi->top[0], tgpi->top[1], tgpi->bottom[0], tgpi->bottom[1]);
		}
		else {
			BLI_snprintf(status_str, sizeof(status_str), "%s: (%d, %d)", msg_str, 
				         tgpi->bottom[0], tgpi->bottom[1]);
		}
	}
	ED_area_headerprint(tgpi->sa, status_str);

}

/* create a rectangle */
static void gp_primitive_rectangle(tGPDprimitive *tgpi, bGPDstroke *gps)
{
	ToolSettings *ts = tgpi->scene->toolsettings;
	tGPspoint point2D;
	bGPDspoint *pt;
	float r_out[3];
	int x[4], y[4];
	int totpoints = 4;

	ARRAY_SET_ITEMS(x, tgpi->top[0], tgpi->bottom[0], tgpi->bottom[0], tgpi->top[0]);
	ARRAY_SET_ITEMS(y, tgpi->top[1], tgpi->top[1], tgpi->bottom[1], tgpi->bottom[1]);

	for (int i = 0; i < totpoints; i++) {
		point2D.x = x[i];
		point2D.y = y[i];

		pt = &gps->points[i];
		/* convert screen-coordinates to 3D coordinates */
		gp_stroke_convertcoords_tpoint(tgpi->scene, tgpi->ar, tgpi->v3d, &point2D, r_out);
		copy_v3_v3(&pt->x, r_out);
		/* if parented change position relative to parent object */
		gp_apply_parent_point(tgpi->ob, tgpi->gpd, tgpi->gpl, pt);

		pt->pressure = 1.0f;
		pt->strength = tgpi->brush->draw_strength;
		pt->time = 0.0f;
		pt->totweight = 0;
		pt->weights = NULL;
	}

	gps->totpoints = totpoints;
	
	/* if axis locked, reproject to plane locked */
	if (tgpi->lock_axis > GP_LOCKAXIS_NONE) {
		float origin[3];
		bGPDspoint *tpt = gps->points;
		ED_gp_get_drawing_reference(tgpi->v3d, tgpi->scene, tgpi->ob, tgpi->gpl, 
			                        ts->gpencil_v3d_align, origin);

		ED_gp_project_stroke_to_plane(tgpi->ob, tgpi->rv3d, gps, origin,
			tgpi->lock_axis - 1, ts->gpencil_src);
	}

	/* force fill recalc */
	gps->flag |= GP_STROKE_RECALC_CACHES;
}

/* create a circle */
static void gp_primitive_circle(tGPDprimitive *tgpi, bGPDstroke *gps)
{
	ToolSettings *ts = tgpi->scene->toolsettings;
	tGPspoint point2D;
	bGPDspoint *pt;
	float r_out[3];
	float center[2];
	const int totpoints = tgpi->tot_edges;
	const float step = DEG2RADF(360.0f / (float)(totpoints));
	float a = 0.0f;

	center[0] = tgpi->top[0] + ((tgpi->bottom[0] - tgpi->top[0]) / 2.0);
	center[1] = tgpi->top[1] + ((tgpi->bottom[1] - tgpi->top[1]) / 2.0);
	float r_x = fabs(((tgpi->bottom[0] - tgpi->top[0]) / 2.0));
	float r_y = fabs(((tgpi->bottom[1] - tgpi->top[1]) / 2.0));

	for (int i = 0; i < totpoints; i++) {
		point2D.x = (int)(center[0] + cos(a) * r_x);
		point2D.y = (int)(center[1] + sin(a) * r_y);
		a += step;

		pt = &gps->points[i];
		/* convert screen-coordinates to 3D coordinates */
		gp_stroke_convertcoords_tpoint(tgpi->scene, tgpi->ar, tgpi->v3d, &point2D, r_out);
		copy_v3_v3(&pt->x, r_out);
		/* if parented change position relative to parent object */
		gp_apply_parent_point(tgpi->ob, tgpi->gpd, tgpi->gpl, pt);

		pt->pressure = 1.0f;
		pt->strength = tgpi->brush->draw_strength;
		pt->time = 0.0f;
		pt->totweight = 0;
		pt->weights = NULL;
	}

	gps->totpoints = totpoints;

	/* if axis locked, reproject to plane locked */
	if (tgpi->lock_axis > GP_LOCKAXIS_NONE) {
		float origin[3];
		bGPDspoint *tpt = gps->points;
		ED_gp_get_drawing_reference(tgpi->v3d, tgpi->scene, tgpi->ob, tgpi->gpl,
		                            ts->gpencil_v3d_align, origin);

		for (int i = 0; i < gps->totpoints; i++, tpt++) {
			ED_gp_project_point_to_plane(tgpi->ob, tgpi->rv3d, origin,
			                             ts->gp_sculpt.lock_axis - 1,
			                             ts->gpencil_src, tpt);
		}
	}

	/* force fill recalc */
	gps->flag |= GP_STROKE_RECALC_CACHES;
}

/* Helper: Update shape of the stroke */
static void gp_primitive_update_strokes(bContext *C, tGPDprimitive *tgpi)
{
	bGPdata *gpd = tgpi->gpd;
	bGPDstroke *gps = tgpi->gpf->strokes.first;

	/* realloc points to new size */
	gps->points = MEM_reallocN(gps->points, sizeof(bGPDspoint) * tgpi->tot_edges);
	gps->totpoints = tgpi->tot_edges;

	/* update points position creating figure */
	switch (tgpi->type) {
		case GP_STROKE_BOX:
			gp_primitive_rectangle(tgpi, gps);
			break;
		case GP_STROKE_CIRCLE:
			gp_primitive_circle(tgpi, gps);
			break;
		default:
			break;
	}

	BKE_gpencil_batch_cache_dirty(gpd);
	WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED, NULL);
}

/* Update screen and stroke */
static void gpencil_primitive_update(bContext *C, wmOperator *op, tGPDprimitive *tgpi)
{
	/* update indicator in header */
	gpencil_primitive_status_indicators(tgpi);
	/* apply... */
	tgpi->type = RNA_enum_get(op->ptr, "type");
	tgpi->tot_edges = RNA_int_get(op->ptr, "edges");
	/* update points position */
	gp_primitive_update_strokes(C, tgpi);
}

/* ----------------------- */

/* Exit and free memory */
static void gpencil_primitive_exit(bContext *C, wmOperator *op)
{
	tGPDprimitive *tgpi = op->customdata;
	bGPdata *gpd = tgpi->gpd;

	/* don't assume that operator data exists at all */
	if (tgpi) {
		/* remove drawing handler */
		if (tgpi->draw_handle_3d) {
			ED_region_draw_cb_exit(tgpi->ar->type, tgpi->draw_handle_3d);
		}
		
		/* clear status message area */
		ED_area_headerprint(tgpi->sa, NULL);
		
		/* finally, free memory used by temp data */
		BKE_gpencil_free_strokes(tgpi->gpf);
		MEM_freeN(tgpi->gpf);
		MEM_freeN(tgpi);
	}
	BKE_gpencil_batch_cache_dirty(gpd);
	WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED, NULL);
	
	/* clear pointer */
	op->customdata = NULL;
}

/* Init new temporary primitive data */
static bool gp_primitive_set_init_values(bContext *C, wmOperator *op, tGPDprimitive *tgpi)
{
	ToolSettings *ts = CTX_data_tool_settings(C);
	bGPdata *gpd = CTX_data_gpencil_data(C);
	Main *bmain = CTX_data_main(C);
	
	/* set current scene and window info */
	tgpi->scene = CTX_data_scene(C);
	tgpi->ob = CTX_data_active_object(C);
	tgpi->sa = CTX_wm_area(C);
	tgpi->ar = CTX_wm_region(C);
	tgpi->rv3d = tgpi->ar->regiondata;
	tgpi->v3d = tgpi->sa->spacedata.first;
	
	/* set current frame number */
	tgpi->cframe = tgpi->scene->r.cfra;
	
	/* set GP datablock */
	tgpi->gpd = gpd;
	
	/* get palette and color info */
	bGPDpaletteref *palslot = BKE_gpencil_paletteslot_validate(bmain, gpd);
	tgpi->palette = palslot->palette;
	tgpi->palcolor = BKE_palette_color_get_active(tgpi->palette);

	/* set parameters */
	tgpi->type = RNA_enum_get(op->ptr, "type");
	/* if circle set default to 32 */
	if (tgpi->type == GP_STROKE_CIRCLE) {
		RNA_int_set(op->ptr, "edges", 32);
	}

	tgpi->tot_edges = RNA_int_get(op->ptr, "edges");
	tgpi->flag = IDLE;
	tgpi->oldevent = EVENT_NONE;

	tgpi->lock_axis = ts->gp_sculpt.lock_axis;

	/* set temp layer, frame and stroke */
	gp_primitive_set_initdata(C, tgpi);
	
	return 1;
}

/* Allocate memory and initialize values */
static tGPDprimitive *gp_session_init_primitives(bContext *C, wmOperator *op)
{
	tGPDprimitive *tgpi = MEM_callocN(sizeof(tGPDprimitive), "GPencil Primitive Data");
	
	/* define initial values */
	gp_primitive_set_init_values(C, op, tgpi);
	
	/* return context data for running operator */
	return tgpi;
}

/* Init interpolation: Allocate memory and set init values */
static int gpencil_primitive_init(bContext *C, wmOperator *op)
{
	tGPDprimitive *tgpi;
	
	/* check context */
	tgpi = op->customdata = gp_session_init_primitives(C, op);
	if (tgpi == NULL) {
		/* something wasn't set correctly in context */
		gpencil_primitive_exit(C, op);
		return 0;
	}
	
	/* everything is now setup ok */
	return 1;
}

/* ----------------------- */

/* Invoke handler: Initialize the operator */
static int gpencil_primitive_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	wmWindow *win = CTX_wm_window(C);
	bGPdata *gpd = CTX_data_gpencil_data(C);
	bGPDlayer *gpl = CTX_data_active_gpencil_layer(C);
	tGPDprimitive *tgpi = NULL;

	/* cannot primitive if not active frame */
	if (ELEM(NULL, gpd, gpl)) {
		BKE_report(op->reports, RPT_ERROR, "Cannot add primitive. Need an active layer");
		return OPERATOR_CANCELLED;
	}
	
	/* try to initialize context data needed */
	if (!gpencil_primitive_init(C, op)) {
		if (op->customdata)
			MEM_freeN(op->customdata);
		return OPERATOR_CANCELLED;
	}
	else {
		tgpi = op->customdata;
	}
	
	/* Enable custom drawing handlers */
	tgpi->draw_handle_3d = ED_region_draw_cb_activate(tgpi->ar->type, gpencil_primitive_draw_3d, tgpi, REGION_DRAW_POST_VIEW);
	
	/* set cursor to indicate modal */
	WM_cursor_modal_set(win, BC_CROSSCURSOR);
	
	/* update sindicator in header */
	gpencil_primitive_status_indicators(tgpi);
	BKE_gpencil_batch_cache_dirty(gpd);
	WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED, NULL);
	
	/* add a modal handler for this operator */
	WM_event_add_modal_handler(C, op);
	
	return OPERATOR_RUNNING_MODAL;
}

/* Helper to complete a primitive */
static void gpencil_primitive_done(bContext *C, wmOperator *op, wmWindow *win, tGPDprimitive *tgpi)
{
	bGPDframe *gpf;
	bGPDstroke *gps_src, *gps_dst;

	/* return to normal cursor and header status */
	ED_area_headerprint(tgpi->sa, NULL);
	WM_cursor_modal_restore(win);

	/* insert keyframes as required... */
	gpf = BKE_gpencil_layer_getframe(tgpi->gpl, tgpi->cframe, GP_GETFRAME_ADD_NEW);

	/* make copy of source stroke, then adjust pointer to points too */
	gps_src = tgpi->gpf->strokes.first;
	gps_dst = MEM_dupallocN(gps_src);
	/* set thickness */
	gps_dst->thickness = tgpi->brush->thickness;
	gps_dst->points = MEM_dupallocN(gps_src->points);
	BKE_gpencil_stroke_weights_duplicate(gps_src, gps_dst);
	gps_dst->triangles = MEM_dupallocN(gps_src->triangles);
	gps_dst->flag |= GP_STROKE_RECALC_CACHES;
	BLI_addtail(&gpf->strokes, gps_dst);

	/* clean up temp data */
	gpencil_primitive_exit(C, op);

}

/* Modal handler: Events handling during interactive part */
static int gpencil_primitive_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
	tGPDprimitive *tgpi = op->customdata;
	wmWindow *win = CTX_wm_window(C);
	const bool has_numinput = hasNumInput(&tgpi->num);
	
	switch (event->type) {
		case LEFTMOUSE: 
			/* avoid fast double clicks done accidentally by user */
			if (tgpi->oldevent == event->type) {
				tgpi->oldevent = EVENT_NONE;
				break;
			}
			tgpi->oldevent = event->type;

			if (tgpi->flag == IDLE) {
				tgpi->top[0] = event->mval[0];
				tgpi->top[1] = event->mval[1];
			}

			tgpi->bottom[0] = event->mval[0];
			tgpi->bottom[1] = event->mval[1];

			if (tgpi->flag == IDLE) {
				tgpi->flag = IN_PROGRESS;
				break;
			}
			else {
				tgpi->flag = IDLE;
				gpencil_primitive_done(C, op, win, tgpi);
				/* done! */
				return OPERATOR_FINISHED;
			}
			break;
		case RETKEY:  /* confirm */
		{
			tgpi->flag = IDLE;
			gpencil_primitive_done(C, op, win, tgpi);
			/* done! */
			return OPERATOR_FINISHED;
		}

		case ESCKEY:    /* cancel */
		case RIGHTMOUSE:
		{
			/* return to normal cursor and header status */
			ED_area_headerprint(tgpi->sa, NULL);
			WM_cursor_modal_restore(win);
			
			/* clean up temp data */
			gpencil_primitive_exit(C, op);
			
			/* canceled! */
			return OPERATOR_CANCELLED;
		}
		
		case WHEELUPMOUSE:
		{
			if (tgpi->type == GP_STROKE_CIRCLE) {
				tgpi->tot_edges = tgpi->tot_edges + 1;
				CLAMP(tgpi->tot_edges, MIN_EDGES, MAX_EDGES);
				RNA_int_set(op->ptr, "edges", tgpi->tot_edges);

				/* update screen */
				gpencil_primitive_update(C, op, tgpi);
			}
			break;
		}
		case WHEELDOWNMOUSE:
		{
			if (tgpi->type == GP_STROKE_CIRCLE) {
				tgpi->tot_edges = tgpi->tot_edges - 1;
				CLAMP(tgpi->tot_edges, MIN_EDGES, MAX_EDGES);
				RNA_int_set(op->ptr, "edges", tgpi->tot_edges);

				/* update screen */
				gpencil_primitive_update(C, op, tgpi);
			}
			break;
		}
		case MOUSEMOVE: /* calculate new position */
		{
			/* only handle mousemove if not doing numinput */
			if (has_numinput == false) {
				/* update position of mouse */
				tgpi->bottom[0] = event->mval[0];
				tgpi->bottom[1] = event->mval[1];
				if (tgpi->flag == IDLE) {
					tgpi->top[0] = event->mval[0];
					tgpi->top[1] = event->mval[1];
				}
				/* Keep square if shift key */
				if (event->shift) {
					tgpi->bottom[1] = tgpi->top[1] - (tgpi->bottom[0] - tgpi->top[0]);
				}
				/* update screen */
				gpencil_primitive_update(C, op, tgpi);
			}
			break;
		}
		default:
		{
			if ((event->val == KM_PRESS) && handleNumInput(C, &tgpi->num, event)) {
				float value;
				
				/* Grab data from numeric input, and store this new value (the user see an int) */
				value = tgpi->tot_edges;
				applyNumInput(&tgpi->num, &value);
				tgpi->tot_edges = value;
				
				CLAMP(tgpi->tot_edges, MIN_EDGES, MAX_EDGES);
				RNA_int_set(op->ptr, "edges", tgpi->tot_edges);
				
				/* update screen */
				gpencil_primitive_update(C, op, tgpi);
				
				break;
			}
			else {
				/* unhandled event - allow to pass through */
				return OPERATOR_RUNNING_MODAL | OPERATOR_PASS_THROUGH;
			}
		}
	}
	
	/* still running... */
	return OPERATOR_RUNNING_MODAL;
}

/* Cancel handler */
static void gpencil_primitive_cancel(bContext *C, wmOperator *op)
{
	/* this is just a wrapper around exit() */
	gpencil_primitive_exit(C, op);
}

void GPENCIL_OT_primitive(wmOperatorType *ot)
{
	static EnumPropertyItem primitive_type[] = {
		{ GP_STROKE_BOX, "BOX", 0, "Box", "" },
		{ GP_STROKE_CIRCLE, "CIRCLE", 0, "Circle", "" },
		{ 0, NULL, 0, NULL, NULL }
	};

	/* identifiers */
	ot->name = "Grease Pencil Shapes";
	ot->idname = "GPENCIL_OT_primitive";
	ot->description = "Create predefined grease pencil stroke shapes";
	
	/* callbacks */
	ot->invoke = gpencil_primitive_invoke;
	ot->modal = gpencil_primitive_modal;
	ot->cancel = gpencil_primitive_cancel;
	ot->poll = gpencil_view3d_poll;
	
	/* flags */
	ot->flag = OPTYPE_UNDO | OPTYPE_BLOCKING;
	
	/* properties */
	RNA_def_int(ot->srna, "edges", 4, MIN_EDGES, MAX_EDGES, "Edges", "Number of polygon edges", MIN_EDGES, MAX_EDGES);
	RNA_def_enum(ot->srna, "type", primitive_type, GP_STROKE_BOX, "Type", "Type of shape");
}

/* *************************************************************** */
