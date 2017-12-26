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

#include "MEM_guardedalloc.h" 

#include "BLI_utildefines.h"
#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_stack.h"

#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_windowmanager_types.h"

#include "BKE_main.h" 
#include "BKE_image.h" 
#include "BKE_gpencil.h"
#include "BKE_camera.h" 
#include "BKE_context.h"
#include "BKE_screen.h"
#include "BKE_paint.h" 
#include "BKE_report.h" 

#include "ED_gpencil.h"
#include "ED_screen.h"
#include "ED_space_api.h" 
#include "ED_view3d.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "GPU_immediate.h"
#include "GPU_draw.h"
#include "GPU_matrix.h"
#include "GPU_framebuffer.h"

#include "WM_api.h"
#include "WM_types.h"

 /* draw a given stroke in offscreen */
static void gp_draw_offscreen_stroke(const bGPDspoint *points, int totpoints, 
	const float diff_mat[4][4], bool cyclic, float ink[4])
{
	float fpt[3];

	/* if cyclic needs more vertex */
	int cyclic_add = (cyclic) ? 1 : 0;

	Gwn_VertFormat *format = immVertexFormat();
	unsigned pos = GWN_vertformat_attr_add(format, "pos", GWN_COMP_F32, 3, GWN_FETCH_FLOAT);
	unsigned color = GWN_vertformat_attr_add(format, "color", GWN_COMP_F32, 4, GWN_FETCH_FLOAT);

	immBindBuiltinProgram(GPU_SHADER_3D_FLAT_COLOR);
	
	/* draw stroke curve */
	glLineWidth(2.0f);
	immBeginAtMost(GWN_PRIM_LINE_STRIP, totpoints + cyclic_add);
	const bGPDspoint *pt = points;

	for (int i = 0; i < totpoints; i++, pt++) {
		/* set point */
		immAttrib4fv(color, ink);
		mul_v3_m4v3(fpt, diff_mat, &pt->x);
		immVertex3fv(pos, fpt);
	}

	if (cyclic && totpoints > 2) {
		/* draw line to first point to complete the cycle */
		immAttrib4fv(color, ink);
		mul_v3_m4v3(fpt, diff_mat, &points->x);
		immVertex3fv(pos, fpt);

	}

	immEnd();
	immUnbindProgram();
}

/* loop all layers */
static void gp_draw_datablock(Scene *scene, Object *ob, bGPdata *gpd, float ink[4])
{
	float diff_mat[4][4];
	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		/* calculate parent position */
		ED_gpencil_parent_location(ob, gpd, gpl, diff_mat);

		/* don't draw layer if hidden */
		if (gpl->flag & GP_LAYER_HIDE)
			continue;

		/* get frame to draw */
		bGPDframe *gpf = BKE_gpencil_layer_getframe(gpl, CFRA, 0);
		if (gpf == NULL)
			continue;

		for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
			/* check if stroke can be drawn */
			if ((gps->points == NULL) || (gps->totpoints < 2)) {
				continue;
			}
			/* check if the color is visible */
			PaletteColor *palcolor = gps->palcolor;
			if ((palcolor == NULL) || (palcolor->flag & PC_COLOR_HIDE))
			{
				continue;
			}

			/* 3D Lines - OpenGL primitives-based */
			gp_draw_offscreen_stroke(gps->points, gps->totpoints,
				diff_mat, gps->flag & GP_STROKE_CYCLIC, ink);
		}
	}
}

 /* draw strokes in offscreen buffer */
static void gp_render_offscreen(tGPDfill *tgpf)
{
	Scene *scene = tgpf->scene;
	Object *ob = tgpf->ob;
	bGPdata *gpd = (bGPdata *)ob->data;
	const char *viewname = "GP";
	bool is_ortho = false;
	float winmat[4][4];

	if (!gpd) {
		return;
	}

	char err_out[256] = "unknown";
	GPUOffScreen *offscreen = GPU_offscreen_create(tgpf->sizex, tgpf->sizey, 0, err_out);
	GPU_offscreen_bind(offscreen, true);
	unsigned int flag = IB_rect | IB_rectfloat;
	ImBuf *ibuf = IMB_allocImBuf(tgpf->sizex, tgpf->sizey, 32, flag);

	/* render 3d view */
	if (tgpf->rv3d->persp == RV3D_CAMOB && tgpf->v3d->camera) {
		CameraParams params;
		Object *camera = BKE_camera_multiview_render(scene, tgpf->v3d->camera, viewname);

		BKE_camera_params_init(&params);
		/* fallback for non camera objects */
		params.clipsta = tgpf->v3d->near;
		params.clipend = tgpf->v3d->far;
		BKE_camera_params_from_object(&params, camera);
		BKE_camera_multiview_params(&scene->r, &params, camera, viewname);
		BKE_camera_params_compute_viewplane(&params, tgpf->sizex, tgpf->sizey, scene->r.xasp, scene->r.yasp);
		BKE_camera_params_compute_matrix(&params);

		is_ortho = params.is_ortho;
		copy_m4_m4(winmat, params.winmat);
	}
	else {
		rctf viewplane;
		float clipsta, clipend;

		is_ortho = ED_view3d_viewplane_get(tgpf->v3d, tgpf->rv3d, tgpf->sizex, tgpf->sizey, &viewplane, &clipsta, &clipend, NULL);
		if (is_ortho) {
			orthographic_m4(winmat, viewplane.xmin, viewplane.xmax, viewplane.ymin, viewplane.ymax, -clipend, clipend);
		}
		else {
			perspective_m4(winmat, viewplane.xmin, viewplane.xmax, viewplane.ymin, viewplane.ymax, clipsta, clipend);
		}
	}

	/* set temporary new size */
	int bwinx = tgpf->ar->winx;
	int bwiny = tgpf->ar->winy;
	rcti brect = tgpf->ar->winrct;

	tgpf->ar->winx = tgpf->sizex;
	tgpf->ar->winy = tgpf->sizey;
	tgpf->ar->winrct.xmin = 0;
	tgpf->ar->winrct.ymin = 0;
	tgpf->ar->winrct.xmax = tgpf->sizex;
	tgpf->ar->winrct.ymax = tgpf->sizey;

	gpuPushProjectionMatrix();
	gpuLoadIdentity();
	gpuPushMatrix();
	gpuLoadIdentity();

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	ED_view3d_update_viewmat(tgpf->eval_ctx, tgpf->scene, tgpf->v3d, tgpf->ar,
							NULL, winmat, NULL);
	/* set for opengl */
	gpuLoadProjectionMatrix(tgpf->rv3d->winmat);
	gpuLoadMatrix(tgpf->rv3d->viewmat);

	/* draw strokes */
	float ink[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
	gp_draw_datablock(scene, ob, gpd, ink);

	/* restore size */
	tgpf->ar->winx = bwinx;
	tgpf->ar->winy = bwiny;
	tgpf->ar->winrct = brect;

	gpuPopProjectionMatrix();
	gpuPopMatrix();

	/* read result pixels */
	//unsigned char *pixeldata = MEM_mallocN(tgpf->sizex * tgpf->sizey * sizeof(unsigned char) * 4, "gpencil offscreen fill");
	//GPU_offscreen_read_pixels(offscreen, GL_UNSIGNED_BYTE, pixeldata);

	/* create a image to see result of template */
	if (ibuf->rect_float) {
		GPU_offscreen_read_pixels(offscreen, GL_FLOAT, ibuf->rect_float);
	}
	else if (ibuf->rect) {
		GPU_offscreen_read_pixels(offscreen, GL_UNSIGNED_BYTE, ibuf->rect);
	}
	if (ibuf->rect_float && ibuf->rect) {
		IMB_rect_from_float(ibuf);
	}

	tgpf->ima = BKE_image_add_from_imbuf(ibuf, "GP_fill");
	BKE_image_release_ibuf(tgpf->ima, ibuf, NULL);

	/* switch back to window-system-provided framebuffer */
	GPU_offscreen_unbind(offscreen, true);
	GPU_offscreen_free(offscreen);
}

/* return pixel data (rgba) at index */
static unsigned int *get_pixel(unsigned int *pixeldata, int idx)
{
	if (idx >= 0) {
		return &pixeldata[idx];
	}
	else {
		return NULL;
	}
}

/* set pixel data (rgba) at index */
static void set_pixel(unsigned int *pixeldata, int idx)
{
	if (idx >= 0) {
		/* enable Green & Alpha channel */
		pixeldata[idx] = 0; /* red 0% */
		pixeldata[idx + 1] = 255; /* green 100% */
		pixeldata[idx + 3] = 255; /* alpha 100% */
	}
}

/* Boundary fill inside strokes 
 * Fills the space created by a set of strokes using the stroke color as the boundary
 * of the shape to fill.
 *
 * \param tgpf       Temporary fill data
 * \param pixeldata  Array of pixels from offscreen render
 */
static void gpencil_fill_area(tGPDfill *tgpf)
{
	ImBuf *ibuf;
	unsigned int *rgba;
	unsigned int *pixeldata;
	void *lock;

	ibuf = BKE_image_acquire_ibuf(tgpf->ima, NULL, &lock);
	const int maxpixel = (ibuf->x * ibuf->y) - 4;
	pixeldata = ibuf->rect;

	BLI_Stack *stack = BLI_stack_new(sizeof(int), __func__);

	/* calculate index of the seed point using the position of the mouse */
	int index = ((tgpf->sizex * tgpf->center[0]) + tgpf->center[1]) * 4;
	if ((index >= 0) && (index < maxpixel)) {
		BLI_stack_push(stack, &index);
	}

	/* the fill use a stack to save the pixel list instead of the common recursive
	* 4-contact point method.
	* The problem with recursive calls is that for big fill areas, we can get max limit
	* of recursive calls and STACK_OVERFLOW error.
	*
	* The 4-contact point analyze the pixels to the left, right, bottom and top
	*      -----------
	*      |    X    |
	*      |   XoX   |
	*      |    X    |
	*      -----------
	*/
#if 0
	while (!BLI_stack_is_empty(stack)) {
		int v;
		BLI_stack_pop(stack, &v);

		rgba = get_pixel(pixeldata, v);
		if (rgba) {
			/* check if no border(red) or already filled color(green) */
			if ((rgba[0] != 255) && (rgba[1] != 255))
			{
				/* fill current pixel */
				set_pixel(pixeldata, v);

				/* add contact pixels */
				/* pixel left */
				if (v - 4 >= 0) {
					index = v - 4;
					BLI_stack_push(stack, &index);
				}
				/* pixel right */
				if (v + 4 < maxpixel) {
					index = v + 4;
					BLI_stack_push(stack, &index);
				}
				/* pixel top */
				if (v + tgpf->sizex < maxpixel) {
					index = v + tgpf->sizex;
					BLI_stack_push(stack, &index);
				}
				/* pixel bottom */
				if (v - tgpf->sizex >= 0) {
					index = v - tgpf->sizex;
					BLI_stack_push(stack, &index);
				}
			}
		}

	}
#endif

//#if 0 /* debug code */
	for (int x = 0; x < ibuf->x * ibuf->y; x++) {
		rgba = get_pixel(pixeldata, x);
		if (rgba[3] > 0) {
			printf("%d->RGBA(%d, %d, %d, %d)\n", x, rgba[0], rgba[1], rgba[2], rgba[3]);
			//set_pixel(pixeldata, x);
		}
	}
	printf("\n");
//#endif

	if (ibuf) {
		BKE_image_release_ibuf(tgpf->ima, ibuf, lock);
	}

	/* free temp stack data*/
	BLI_stack_free(stack);
}

/* ----------------------- */
/* Drawing Callbacks */

/* draw boundary lines to see fill limits */
static void gpencil_draw_boundary_lines(const bContext *UNUSED(C), tGPDfill *tgpf)
{
	Scene *scene = tgpf->scene;
	Object *ob = tgpf->ob;
	bGPdata *gpd = (bGPdata *)ob->data;
	if (!gpd) {
		return;
	}
	float ink[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
	gp_draw_datablock(scene, ob, gpd, ink);
}

/* Drawing callback for modal operator in 3d mode */
static void gpencil_fill_draw_3d(const bContext *C, ARegion *UNUSED(ar), void *arg)
{
	tGPDfill *tgpf = (tGPDfill *)arg;
	gpencil_draw_boundary_lines(C, tgpf); 
}

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

/* Allocate memory and initialize values */
static tGPDfill *gp_session_init_fill(bContext *C, wmOperator *op)
{
	tGPDfill *tgpf = MEM_callocN(sizeof(tGPDfill), "GPencil Fill Data");

	/* define initial values */
	ToolSettings *ts = CTX_data_tool_settings(C);
	bGPdata *gpd = CTX_data_gpencil_data(C);
	Main *bmain = CTX_data_main(C);

	/* set current scene and window info */
	tgpf->scene = CTX_data_scene(C);
	tgpf->ob = CTX_data_active_object(C);
	tgpf->sa = CTX_wm_area(C);
	tgpf->ar = CTX_wm_region(C);
	tgpf->eval_ctx = bmain->eval_ctx;
	tgpf->rv3d = tgpf->ar->regiondata;
	tgpf->v3d = tgpf->sa->spacedata.first;

	/* set GP datablock */
	tgpf->gpd = gpd;

	/* get palette and color info */
	bGPDpaletteref *palslot = BKE_gpencil_paletteslot_validate(bmain, gpd);
	tgpf->palette = palslot->palette;
	tgpf->palcolor = BKE_palette_color_get_active(tgpf->palette);

	tgpf->lock_axis = ts->gp_sculpt.lock_axis;

	/* return context data for running operator */
	return tgpf;
}

/* end operator */
static void gpencil_fill_exit(bContext *C, wmOperator *op)
{
	Object *ob = CTX_data_active_object(C);
	/* restore cursor to indicate end of fill */
	WM_cursor_modal_restore(CTX_wm_window(C));

	tGPDfill *tgpf = op->customdata;
	bGPdata *gpd = tgpf->gpd;

	/* don't assume that operator data exists at all */
	if (tgpf) {
		/* remove drawing handler */
		if (tgpf->draw_handle_3d) {
			ED_region_draw_cb_exit(tgpf->ar->type, tgpf->draw_handle_3d);
		}

		/* finally, free memory used by temp data */
		MEM_freeN(tgpf);
	}

	/* clear pointer */
	op->customdata = NULL;

	/* drawing batch cache is dirty now */
	if ((ob) && (ob->type == OB_GPENCIL) && (ob->data)) {
		bGPdata *gpd = ob->data;
		BKE_gpencil_batch_cache_dirty(gpd);
		gpd->flag |= GP_DATA_CACHE_IS_DIRTY;
	}

	WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED, NULL);
}

static void gpencil_fill_cancel(bContext *C, wmOperator *op)
{
	/* this is just a wrapper around exit() */
	gpencil_fill_exit(C, op);
}

/* Init: Allocate memory and set init values */
static int gpencil_fill_init(bContext *C, wmOperator *op)
{
	tGPDfill *tgpf;

	/* check context */
	tgpf = op->customdata = gp_session_init_fill(C, op);
	if (tgpf == NULL) {
		/* something wasn't set correctly in context */
		gpencil_fill_exit(C, op);
		return 0;
	}

	/* everything is now setup ok */
	return 1;
}

/* start of interactive part of operator */
static int gpencil_fill_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	BKE_report(op->reports, RPT_WARNING, "This operator is not implemented yet");

	tGPDfill *tgpf = NULL;

	/* try to initialize context data needed */
	if (!gpencil_fill_init(C, op)) {
		if (op->customdata)
			MEM_freeN(op->customdata);
		return OPERATOR_CANCELLED;
	}
	else {
		tgpf = op->customdata;
	}

	/* Enable custom drawing handlers */
	tgpf->draw_handle_3d = ED_region_draw_cb_activate(tgpf->ar->type, gpencil_fill_draw_3d, tgpf, REGION_DRAW_POST_VIEW);

	WM_cursor_modal_set(CTX_wm_window(C), BC_PAINTBRUSHCURSOR);
	BKE_gpencil_batch_cache_dirty(tgpf->gpd);
	WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED, NULL);

	/* add a modal handler for this operator*/
	WM_event_add_modal_handler(C, op);

	return OPERATOR_RUNNING_MODAL;
}

/* events handling during interactive part of operator */
static int gpencil_fill_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
	Scene *scene = CTX_data_scene(C);
	Object *ob = CTX_data_active_object(C);
	tGPDfill *tgpf = op->customdata;

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
				tgpf->center[0] = event->x;
				tgpf->center[1] = event->y;

				tgpf->sizex = region_rect.xmax - region_rect.xmin;
				tgpf->sizey = region_rect.ymax - region_rect.ymin;

				/* TODO: Add fill code here */
				printf("(%d, %d) in (%d, %d) -> (%d, %d) Do all here!\n", event->mval[0], event->mval[1],
						region_rect.xmin, region_rect.ymin, region_rect.xmax, region_rect.ymax);

				gp_render_offscreen(tgpf);
				gpencil_fill_area(tgpf);

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
			/* TODO: Removed for debug: WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED, NULL); */
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
