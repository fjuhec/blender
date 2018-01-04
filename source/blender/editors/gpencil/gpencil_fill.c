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
 * Contributor(s): Antonio Vazquez, Joshua Leung
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

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "GPU_immediate.h"
#include "GPU_draw.h"
#include "GPU_matrix.h"
#include "GPU_framebuffer.h"

#include "UI_interface.h"

#include "WM_api.h"
#include "WM_types.h"

#include "gpencil_intern.h"

#define LEAK_HORZ 0
#define LEAK_VERT 1

 /* draw a given stroke using same thickness and color for all points */
static void gp_draw_basic_stroke(bGPDstroke *gps, const float diff_mat[4][4], 
								bool cyclic, float ink[4], int flag, float thershold)
{
	bGPDspoint *points = gps->points;
	int totpoints = gps->totpoints;
	float fpt[3];
	float col[4];

	copy_v4_v4(col, ink);

	/* if cyclic needs more vertex */
	int cyclic_add = (cyclic) ? 1 : 0;

	Gwn_VertFormat *format = immVertexFormat();
	unsigned pos = GWN_vertformat_attr_add(format, "pos", GWN_COMP_F32, 3, GWN_FETCH_FLOAT);
	unsigned color = GWN_vertformat_attr_add(format, "color", GWN_COMP_F32, 4, GWN_FETCH_FLOAT);

	immBindBuiltinProgram(GPU_SHADER_3D_FLAT_COLOR);
	
	/* draw stroke curve */
	glLineWidth(1.0f);
	immBeginAtMost(GWN_PRIM_LINE_STRIP, totpoints + cyclic_add);
	const bGPDspoint *pt = points;

	for (int i = 0; i < totpoints; i++, pt++) {

		if (flag & GP_BRUSH_FILL_HIDE) {
			float alpha = gps->palcolor->rgb[3] * pt->strength;
			CLAMP(alpha, 0.0f, 1.0f);
			col[3] = alpha <= thershold ? 0.0f : 1.0f;
		}
		else {
			col[3] = 1.0f;
		}
		/* set point */
		immAttrib4fv(color, col);
		mul_v3_m4v3(fpt, diff_mat, &pt->x);
		immVertex3fv(pos, fpt);
	}

	if (cyclic && totpoints > 2) {
		/* draw line to first point to complete the cycle */
		immAttrib4fv(color, col);
		mul_v3_m4v3(fpt, diff_mat, &points->x);
		immVertex3fv(pos, fpt);
	}

	immEnd();
	immUnbindProgram();
}

/* loop all layers */
static void gp_draw_datablock(tGPDfill *tgpf, float ink[4])
{
	Scene *scene = tgpf->scene;
	Object *ob = tgpf->ob;
	bGPdata *gpd = tgpf->gpd;

	glEnable(GL_BLEND);

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
			gp_draw_basic_stroke(gps, diff_mat, gps->flag & GP_STROKE_CYCLIC, ink, 
								tgpf->flag, tgpf->fill_threshold);
		}
	}

	glDisable(GL_BLEND);
}

 /* draw strokes in offscreen buffer */
static void gp_render_offscreen(tGPDfill *tgpf)
{
	const char *viewname = "GP";
	bool is_ortho = false;
	float winmat[4][4];

	if (!tgpf->gpd) {
		return;
	}

	char err_out[256] = "unknown";
	GPUOffScreen *offscreen = GPU_offscreen_create(tgpf->sizex, tgpf->sizey, 0, false, err_out);
	GPU_offscreen_bind(offscreen, true);
	unsigned int flag = IB_rect | IB_rectfloat;
	ImBuf *ibuf = IMB_allocImBuf(tgpf->sizex, tgpf->sizey, 32, flag);

	rctf viewplane;
	float clipsta, clipend;

	is_ortho = ED_view3d_viewplane_get(tgpf->v3d, tgpf->rv3d, tgpf->sizex, tgpf->sizey, &viewplane, &clipsta, &clipend, NULL);
	if (is_ortho) {
		orthographic_m4(winmat, viewplane.xmin, viewplane.xmax, viewplane.ymin, viewplane.ymax, -clipend, clipend);
	}
	else {
		perspective_m4(winmat, viewplane.xmin, viewplane.xmax, viewplane.ymin, viewplane.ymax, clipsta, clipend);
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
	gp_draw_datablock(tgpf, ink);

	/* restore size */
	tgpf->ar->winx = bwinx;
	tgpf->ar->winy = bwiny;
	tgpf->ar->winrct = brect;

	gpuPopProjectionMatrix();
	gpuPopMatrix();

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
	tgpf->ima->id.tag |= LIB_TAG_DOIT;

	BKE_image_release_ibuf(tgpf->ima, ibuf, NULL);

	/* switch back to window-system-provided framebuffer */
	GPU_offscreen_unbind(offscreen, true);
	GPU_offscreen_free(offscreen);
}
/* return pixel data (rgba) at index */
static void get_pixel(ImBuf *ibuf, int idx, float r_col[4])
{
	if (ibuf->rect_float) {
		float *frgba = &ibuf->rect_float[idx * 4];
		r_col[0] = *frgba++;
		r_col[1] = *frgba++;
		r_col[2] = *frgba++;
		r_col[3] = *frgba;
	}
}

/* set pixel data (rgba) at index */
static void set_pixel(ImBuf *ibuf, int idx, const float col[4])
{
	if (ibuf->rect) {
		unsigned int *rrect = &ibuf->rect[idx];
		char ccol[4];

		ccol[0] = (int)(col[0] * 255);
		ccol[1] = (int)(col[1] * 255);
		ccol[2] = (int)(col[2] * 255);
		ccol[3] = (int)(col[3] * 255);

		*rrect = *((unsigned int *)ccol);
	}

	if (ibuf->rect_float) {
		float *rrectf = &ibuf->rect_float[idx * 4];

		*rrectf++ = col[0];
		*rrectf++ = col[1];
		*rrectf++ = col[2];
		*rrectf = col[3];
	}
}

/* check if the size of the leak is narrow to determine if the stroke is closed
 * this is used for strokes with small gaps between them to get a full fill
 * and don't get a full screen fill.
 *
 * \param ibuf      Image pixel data
 * \param maxpixel  Maximum index
 * \param limit     Limit of pixels to analize
 * \param index     Index of current pixel
 * \param type      0-Horizontal 1-Verical
 */
static bool is_leak_narrow(ImBuf *ibuf, const int maxpixel, int limit, int index, int type)
{
	float rgba[4];
	int i;
	int pt;
	bool t_a = false;
	bool t_b = false;

	/* Horizontal leak (check vertical pixels) 
	 *     X
	 *	   X
	 *	==>·
	 *	   X
	 *	   X
	 */
	if (type == LEAK_HORZ) {
		/* pixels on top */
		for (i = 1; i <= limit; i++) {
			pt = index + (ibuf->x * i);
			if (pt <= maxpixel) {
				get_pixel(ibuf, pt, rgba);
				if (rgba[0] == 1.0f) {
					t_a =  true;
					break;
				}
			}
			else {
				t_a = true; /* edge of image*/
				break;
			}
		}
		/* pixels on bottom */
		for (i = 1; i <= limit; i++) {
			pt = index - (ibuf->x * i);
			if (pt >= 0) {
				get_pixel(ibuf, pt, rgba);
				if (rgba[0] == 1.0f) {
					t_b = true;
					break;
				}
			}
			else {
				t_b = true; /* edge of image*/
				break;
			}
		}
	}

	/* Vertical leak (check horizontal pixels 
	 *
	 *  XXX·XXX
	 *     ^
	 *     |
	 *
	 */
	if (type == LEAK_VERT) {
		/* get pixel range of the row */
		int row = index / ibuf->x;
		int lowpix = row * ibuf->x;
		int higpix = lowpix + ibuf->x - 1;

		/* pixels to right */
		for (i = 0; i < limit; i++) {
			pt = index - (limit - i);
			if (pt >= lowpix) {
				get_pixel(ibuf, pt, rgba);
				if (rgba[0] == 1.0f) {
					t_a = true;
					break;
				}
			}
			else {
				t_a = true; /* edge of image*/
				break;
			}
		}
		/* pixels to left */
		for (i = 0; i < limit; i++) {
			pt = index + (limit - i);
			if (pt <= higpix) {
				get_pixel(ibuf, pt, rgba);
				if (rgba[0] == 1.0f) {
					t_b = true;
					break;
				}
			}
			else {
				t_b = true; /* edge of image*/
				break;
			}
		}
	}
	return (bool)(t_a && t_b);
}

/* Boundary fill inside strokes 
 * Fills the space created by a set of strokes using the stroke color as the boundary
 * of the shape to fill.
 *
 * \param tgpf       Temporary fill data
 */
static void gpencil_boundaryfill_area(tGPDfill *tgpf)
{
	ImBuf *ibuf;
	float rgba[4];
	void *lock;
	const float fill_col[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	ibuf = BKE_image_acquire_ibuf(tgpf->ima, NULL, &lock);
	const int maxpixel = (ibuf->x * ibuf->y) - 1;

	BLI_Stack *stack = BLI_stack_new(sizeof(int), __func__);

	/* calculate index of the seed point using the position of the mouse */
	int index = (tgpf->sizex * tgpf->center[1]) + tgpf->center[0];
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
	while (!BLI_stack_is_empty(stack)) {
		int v;
		BLI_stack_pop(stack, &v);

		get_pixel(ibuf, v, rgba);

		if (rgba) {
			/* check if no border(red) or already filled color(green) */
			if ((rgba[0] != 1.0f) && (rgba[1] != 1.0f))
			{
				/* fill current pixel */
				set_pixel(ibuf, v, fill_col);

				/* add contact pixels */
				/* pixel left */
				if (v - 1 >= 0) {
					index = v - 1;
					if (!is_leak_narrow(ibuf, maxpixel, tgpf->fill_leak, v, LEAK_HORZ)) {
						BLI_stack_push(stack, &index);
					}
				}
				/* pixel right */
				if (v + 1 < maxpixel) {
					index = v + 1;
					if (!is_leak_narrow(ibuf, maxpixel, tgpf->fill_leak, v, LEAK_HORZ)) {
						BLI_stack_push(stack, &index);
					}
				}
				/* pixel top */
				if (v + tgpf->sizex < maxpixel) {
					index = v + tgpf->sizex;
					if (!is_leak_narrow(ibuf, maxpixel, tgpf->fill_leak, v, LEAK_VERT)) {
						BLI_stack_push(stack, &index);
					}
				}
				/* pixel bottom */
				if (v - tgpf->sizex >= 0) {
					index = v - tgpf->sizex;
					if (!is_leak_narrow(ibuf, maxpixel, tgpf->fill_leak, v, LEAK_VERT)) {
						BLI_stack_push(stack, &index);
					}
				}
			}
		}
	}

	/* release ibuf */
	if (ibuf) {
		BKE_image_release_ibuf(tgpf->ima, ibuf, lock);
	}

	tgpf->ima->id.tag |= LIB_TAG_DOIT;
	/* free temp stack data*/
	BLI_stack_free(stack);
}

/* clean external border of image to avoid infinite loops */
static void gpencil_clean_borders(tGPDfill *tgpf)
{
	ImBuf *ibuf;
	void *lock;
	const float fill_col[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	ibuf = BKE_image_acquire_ibuf(tgpf->ima, NULL, &lock);
	int idx;

	/* horizontal lines */
	for (idx = 0; idx < ibuf->x; idx++) {
		/* bottom line */
		set_pixel(ibuf, idx, fill_col);
		/* top line */
		set_pixel(ibuf, idx + (ibuf->x * (ibuf->y - 1)), fill_col);
	}
	/* vertical lines */
	for (idx = 0; idx < ibuf->y; idx++) {
		/* left line */
		set_pixel(ibuf, ibuf->x * idx, fill_col);
		/* right line */
		set_pixel(ibuf, ibuf->x * idx + (ibuf->x - 1), fill_col);
	}

	/* release ibuf */
	if (ibuf) {
		BKE_image_release_ibuf(tgpf->ima, ibuf, lock);
	}

	tgpf->ima->id.tag |= LIB_TAG_DOIT;
}

/* helper to copy points 2D */
static void copyint_v2_v2(int r[2], const int a[2])
{
	r[0] = a[0];
	r[1] = a[1];
}

/* Get the outline points of a shape using Moore Neighborhood algorithm 
 *
 * This is a Blender customized version of the general algorithm described 
 * in https://en.wikipedia.org/wiki/Moore_neighborhood
 */
static  void gpencil_get_outline_points(tGPDfill *tgpf)
{
	ImBuf *ibuf;
	float rgba[4];
	void *lock;
	int v[2];
	int boundary_co[2];
	int start_co[2];
	int backtracked_co[2];
	int current_check_co[2];
	int prev_check_co[2];
	int backtracked_offset[1][2] = { { 0,0 } };
	bool boundary_found = false;
	bool start_found = false;
	const int NEIGHBOR_COUNT = 8;

	int offset[8][2] = {
		{ -1, -1 },
		{ 0, -1 },
		{ 1, -1 },
		{ 1, 0 },
		{ 1, 1 },
		{ 0, 1 },
		{ -1, 1 },
		{ -1, 0 }
	};

	tgpf->stack = BLI_stack_new(sizeof(int[2]), __func__);

	ibuf = BKE_image_acquire_ibuf(tgpf->ima, NULL, &lock);
	int imagesize = ibuf->x * ibuf->y;

	/* find the initial point to start outline analysis */
	for (int idx = imagesize; idx >= 0; idx--) {
		get_pixel(ibuf, idx, rgba);
		if (rgba[1] == 1.0f) {
			boundary_co[0] = idx % ibuf->x;
			boundary_co[1] = idx / ibuf->x;
			copyint_v2_v2(start_co, boundary_co);
			backtracked_co[0] = (idx - 1) % ibuf->x;
			backtracked_co[1] = (idx - 1) / ibuf->x;
			backtracked_offset[0][0] = backtracked_co[0] - boundary_co[0];
			backtracked_offset[0][1] = backtracked_co[1] - boundary_co[1];
			copyint_v2_v2(prev_check_co, start_co);

			BLI_stack_push(tgpf->stack, &boundary_co);
			start_found = true;
			break;
		}
	}

	while (true && start_found)
	{
		int cur_back_offset = -1;
		for (int i = 0; i < NEIGHBOR_COUNT; i++) {
			if (backtracked_offset[0][0] == offset[i][0] &&
				backtracked_offset[0][1] == offset[i][1])
			{
				/* Finding the bracktracked pixel's offset index */
				cur_back_offset = i;
				break;
			}
		}

		int loop = 0;
		while (loop < (NEIGHBOR_COUNT - 1) && cur_back_offset != -1) {
			int offset_idx = (cur_back_offset + 1) % NEIGHBOR_COUNT;
			current_check_co[0] = boundary_co[0] + offset[offset_idx][0];
			current_check_co[1] = boundary_co[1] + offset[offset_idx][1];
			
			int image_idx = ibuf->x * current_check_co[1] + current_check_co[0];
			get_pixel(ibuf, image_idx, rgba);

			/* find next boundary pixel */
			if (rgba[1] == 1.0f) {
				copyint_v2_v2(boundary_co, current_check_co);
				copyint_v2_v2(backtracked_co, prev_check_co);
				backtracked_offset[0][0] = backtracked_co[0] - boundary_co[0];
				backtracked_offset[0][1] = backtracked_co[1] - boundary_co[1];

				BLI_stack_push(tgpf->stack, &boundary_co);

				break;
			}
			copyint_v2_v2(prev_check_co, current_check_co);
			cur_back_offset++;
			loop++;
		}
		/* current pixel is equal to starting pixel */
		if (boundary_co[0] == start_co[0] &&
			boundary_co[1] == start_co[1])
		{
			BLI_stack_pop(tgpf->stack, &v);
			boundary_found = true;
			break;
		}
	}

	/* release ibuf */
	if (ibuf) {
		BKE_image_release_ibuf(tgpf->ima, ibuf, lock);
	}
}

/* create a grease pencil stroke using points in stack */
static void gpencil_stroke_from_stack(tGPDfill *tgpf)
{
	Scene *scene = tgpf->scene;
	ToolSettings *ts = tgpf->scene->toolsettings;
	bGPDspoint *pt;
	tGPspoint point2D;
	float r_out[3];
	int totpoints = BLI_stack_count(tgpf->stack);
	if (totpoints == 0) {
		return;
	}

	/* get frame or create a new one */
	tgpf->gpf = BKE_gpencil_layer_getframe(tgpf->gpl, CFRA, GP_GETFRAME_ADD_NEW);

	/* create new stroke */
	bGPDstroke *gps = MEM_callocN(sizeof(bGPDstroke), "bGPDstroke");
	gps->thickness = 1.0f;
	gps->inittime = 0.0f;

	/* the polygon must be closed, so enabled cyclic */
	gps->flag |= GP_STROKE_CYCLIC;
	gps->flag |= GP_STROKE_3DSPACE;

	gps->palette = tgpf->palette;
	gps->palcolor = tgpf->palcolor;
	if (tgpf->palcolor)
		BLI_strncpy(gps->colorname, tgpf->palcolor->info, sizeof(gps->colorname));

	/* allocate memory for storage points */
	gps->totpoints = totpoints;
	gps->points = MEM_callocN(sizeof(bGPDspoint) * totpoints, "gp_stroke_points");
	
	/* initialize triangle memory to dummy data */
	gps->tot_triangles = 0;
	gps->triangles = NULL;
	gps->flag |= GP_STROKE_RECALC_CACHES;

	/* add stroke to frame */
	BLI_addtail(&tgpf->gpf->strokes, gps);

	/* add points */
	pt = gps->points;
	int i = 0;
	while (!BLI_stack_is_empty(tgpf->stack)) {
		int v[2];
		BLI_stack_pop(tgpf->stack, &v);
		point2D.x = v[0];
		point2D.y = v[1];

		/* convert screen-coordinates to 3D coordinates */
		gp_stroke_convertcoords_tpoint(tgpf->scene, tgpf->ar, tgpf->v3d, tgpf->ob, tgpf->gpl, &point2D, r_out);
		copy_v3_v3(&pt->x, r_out);

		pt->pressure = 1.0f;
		pt->strength = 1.0f;;
		pt->time = 0.0f;
		pt->totweight = 0;
		pt->weights = NULL;

		pt++;
	}

	/* smooth stroke */
	float reduce = 0.0f;
	float smoothfac = 1.0f;
	for (int r = 0; r < 1; ++r) {
		for (i = 0; i < gps->totpoints; i++) {
			BKE_gp_smooth_stroke(gps, i, smoothfac - reduce, false);
		}
		reduce += 0.25f;  // reduce the factor
	}

	/* if axis locked, reproject to plane locked */
	if (tgpf->lock_axis > GP_LOCKAXIS_NONE) {
		float origin[3];
		bGPDspoint *tpt = gps->points;
		ED_gp_get_drawing_reference(tgpf->v3d, tgpf->scene, tgpf->ob, tgpf->gpl,
			ts->gpencil_v3d_align, origin);
		ED_gp_project_stroke_to_plane(tgpf->ob, tgpf->rv3d, gps, origin, 
			tgpf->lock_axis - 1, ts->gpencil_src);
	}

	/* if parented change position relative to parent object */
	for (int a = 0; a < totpoints; a++) {
		pt = &gps->points[a];
		gp_apply_parent_point(tgpf->ob, tgpf->gpd, tgpf->gpl, pt);
	}

	/* simplify stroke */
	for (int b = 0; b < tgpf->fill_simplylvl; b++) {
		BKE_gpencil_simplify_fixed(tgpf->gpl, gps);
	}
}

/* ----------------------- */
/* Drawing                 */
/* Helper: Draw status message while the user is running the operator */
static void gpencil_fill_status_indicators(tGPDfill *tgpf)
{
	Scene *scene = tgpf->scene;
	char status_str[UI_MAX_DRAW_STR];

	BLI_snprintf(status_str, sizeof(status_str), IFACE_("Fill: ESC/RMB cancel, LMB Fill"));
	ED_area_headerprint(tgpf->sa, status_str);
}

/* draw boundary lines to see fill limits */
static void gpencil_draw_boundary_lines(const bContext *UNUSED(C), tGPDfill *tgpf)
{
	if (!tgpf->gpd) {
		return;
	}
	float ink[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
	gp_draw_datablock(tgpf, ink);
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
	tgpf->graph = CTX_data_depsgraph(C);
	tgpf->win = CTX_wm_window(C);

	/* set GP datablock */
	tgpf->gpd = gpd;
	tgpf->gpl = BKE_gpencil_layer_getactive(gpd);

	/* get palette and color info */
	bGPDpaletteref *palslot = BKE_gpencil_paletteslot_validate(bmain, gpd);
	tgpf->palette = palslot->palette;
	tgpf->palcolor = BKE_palette_color_get_active(tgpf->palette);

	tgpf->lock_axis = ts->gp_sculpt.lock_axis;
	
	tgpf->oldkey = -1;

	/* save filling parameters */
	bGPDbrush *brush = BKE_gpencil_brush_getactive(ts);
	tgpf->flag = brush->flag;
	tgpf->fill_leak = brush->fill_leak;
	tgpf->fill_threshold = brush->fill_threshold;
	tgpf->fill_simplylvl = brush->fill_simplylvl;

	/* init undo */
	gpencil_undo_init(tgpf->gpd);

	/* return context data for running operator */
	return tgpf;
}

/* end operator */
static void gpencil_fill_exit(bContext *C, wmOperator *op)
{
	Main *bmain = CTX_data_main(C);
	Object *ob = CTX_data_active_object(C);

	/* clear undo stack */
	gpencil_undo_finish();

	/* restore cursor to indicate end of fill */
	WM_cursor_modal_restore(CTX_wm_window(C));

	tGPDfill *tgpf = op->customdata;
	bGPdata *gpd = tgpf->gpd;

	/* don't assume that operator data exists at all */
	if (tgpf) {
		/* clear status message area */
		ED_area_headerprint(tgpf->sa, NULL);

		/* remove drawing handler */
		if (tgpf->draw_handle_3d) {
			ED_region_draw_cb_exit(tgpf->ar->type, tgpf->draw_handle_3d);
		}

		/* delete temp image */
		if (tgpf->ima) {
			for (Image *ima = bmain->image.first; ima; ima = ima->id.next) {
				if (ima == tgpf->ima) {
					BLI_remlink(&bmain->image, ima);
					BKE_image_free(tgpf->ima);
					MEM_SAFE_FREE(tgpf->ima);
					break;
				}
			}
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
	tGPDfill *tgpf = NULL;

	/* try to initialize context data needed */
	if (!gpencil_fill_init(C, op)) {
		gpencil_fill_exit(C, op);
		if (op->customdata)
			MEM_freeN(op->customdata);
		return OPERATOR_CANCELLED;
	}
	else {
		tgpf = op->customdata;
	}

	/* Must use a color with fill */
	if ((tgpf->palcolor->fill[3] < GPENCIL_ALPHA_OPACITY_THRESH) && 
		((tgpf->flag & GP_BRUSH_FILL_ALLOW_STROKEONLY) == 0)) 
	{
		BKE_report(op->reports, RPT_ERROR, "The current color must have fill enabled");
		gpencil_fill_exit(C, op);
		return OPERATOR_CANCELLED;
	}

	/* Enable custom drawing handlers */
	if (tgpf->flag & GP_BRUSH_FILL_SHOW_BOUNDARY) {
		tgpf->draw_handle_3d = ED_region_draw_cb_activate(tgpf->ar->type, gpencil_fill_draw_3d, tgpf, REGION_DRAW_POST_VIEW);
	}

	WM_cursor_modal_set(CTX_wm_window(C), BC_PAINTBRUSHCURSOR);
	
	gpencil_fill_status_indicators(tgpf);

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
	if ELEM(event->type, RIGHTMOUSE) {
		estate = OPERATOR_CANCELLED;
	}
	if ELEM(event->type, LEFTMOUSE) {
		/* first time the event is not enabled to show help lines */
		if ((tgpf->oldkey != -1) || ((tgpf->flag & GP_BRUSH_FILL_SHOW_BOUNDARY) == 0)) {
			ARegion *ar = BKE_area_find_region_xy(CTX_wm_area(C), RGN_TYPE_ANY, event->x, event->y);
			if (ar) {
				rcti region_rect;
				bool in_bounds = false;

				/* Perform bounds check */
				ED_region_visible_rect(ar, &region_rect);
				in_bounds = BLI_rcti_isect_pt_v(&region_rect, event->mval);

				if ((in_bounds) && (ar->regiontype == RGN_TYPE_WINDOW)) {
					tgpf->center[0] = event->mval[0];
					tgpf->center[1] = event->mval[1];

					/* save size (don't sub minsize data to get right mouse click position) */
					tgpf->sizex = region_rect.xmax;
					tgpf->sizey = region_rect.ymax;

					/* render screen to temp image */
					gp_render_offscreen(tgpf);

					/* apply boundary fill */
					gpencil_boundaryfill_area(tgpf);

					/* clean borders to avoid infinite loops */
					gpencil_clean_borders(tgpf);

					/* analyze outline */
					gpencil_get_outline_points(tgpf);

					/* create stroke and reproject */
					gpencil_stroke_from_stack(tgpf);

					/* free temp stack data */
					if (tgpf->stack) {
						BLI_stack_free(tgpf->stack);
					}

					/* push undo data */
					gpencil_undo_push(tgpf->gpd);

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
		tgpf->oldkey = event->type;
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
	ot->flag = OPTYPE_UNDO | OPTYPE_BLOCKING;
}
