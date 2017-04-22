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
 * The Original Code is Copyright (C) 2008, Blender Foundation
 * This is a new part of Blender
 *
 * Contributor(s): Joshua Leung, Antonio Vazquez
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/gpencil/drawgpencil.c
 *  \ingroup edgpencil
 */


#include "BLI_polyfill2d.h"

#include "DNA_gpencil_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"

#include "BKE_gpencil.h"
#include "BKE_action.h"

#include "DRW_render.h"

#include "GPU_immediate.h"
#include "GPU_draw.h"

#include "ED_gpencil.h"
#include "ED_view3d.h"

#include "UI_resources.h"

/* set stroke point to vbo */
static void gpencil_set_stroke_point(VertexBuffer *vbo, const bGPDspoint *pt, int idx,
						    unsigned int pos_id, unsigned int color_id,
							unsigned int thickness_id, short thickness,
	                        const float ink[4], bool inverse)
{
	float fpt[3];

	float alpha = ink[3] * pt->strength;
	CLAMP(alpha, GPENCIL_STRENGTH_MIN, 1.0f);
	float col[4] = { ink[0], ink[1], ink[2], alpha };
	VertexBuffer_set_attrib(vbo, color_id, idx, col);

	float thick = max_ff(pt->pressure * thickness, 1.0f);
	VertexBuffer_set_attrib(vbo, thickness_id, idx, &thick);
	
	copy_v3_v3(fpt, &pt->x);
	if (inverse) {
		mul_v3_fl(fpt, -1.0f);
	}
	VertexBuffer_set_attrib(vbo, pos_id, idx, fpt);
}

/* create batch geometry data for stroke shader */
Batch *gpencil_get_stroke_geom(bGPDstroke *gps, short thickness, const float ink[4])
{
	bGPDspoint *points = gps->points;
	int totpoints = gps->totpoints;
	/* if cyclic needs more vertex */
	int cyclic_add = (gps->flag & GP_STROKE_CYCLIC) ? 2 : 0;

	static VertexFormat format = { 0 };
	static unsigned int pos_id, color_id, thickness_id;
	if (format.attrib_ct == 0) {
		pos_id = VertexFormat_add_attrib(&format, "pos", COMP_F32, 3, KEEP_FLOAT);
		color_id = VertexFormat_add_attrib(&format, "color", COMP_F32, 4, KEEP_FLOAT);
		thickness_id = VertexFormat_add_attrib(&format, "thickness", COMP_F32, 1, KEEP_FLOAT);
	}

	VertexBuffer *vbo = VertexBuffer_create_with_format(&format);
	VertexBuffer_allocate_data(vbo, totpoints + cyclic_add + 2);

	/* draw stroke curve */
	const bGPDspoint *pt = points;
	int idx = 0;
	for (int i = 0; i < totpoints; i++, pt++) {
		/* first point for adjacency (not drawn) */
		if (i == 0) {
			gpencil_set_stroke_point(vbo, pt, idx, pos_id, color_id, thickness_id, thickness, ink, true);
			++idx;
		}
		/* set point */
		gpencil_set_stroke_point(vbo, pt, idx, pos_id, color_id, thickness_id, thickness, ink, false);
		++idx;
	}

	if (gps->flag & GP_STROKE_CYCLIC && totpoints > 2) {
		/* draw line to first point to complete the cycle */
		gpencil_set_stroke_point(vbo, &points[0], idx, pos_id, color_id, thickness_id, thickness, ink, false);
		++idx;
		/* now add adjacency points using 2nd & 3rd point to get smooth transition */
		gpencil_set_stroke_point(vbo, &points[1], idx, pos_id, color_id, thickness_id, thickness, ink, false);
		++idx;
		gpencil_set_stroke_point(vbo, &points[2], idx, pos_id, color_id, thickness_id, thickness, ink, false);
		++idx;
	}
	/* last adjacency point (not drawn) */
	else {
		gpencil_set_stroke_point(vbo, &points[totpoints - 1], idx, pos_id, color_id, thickness_id, thickness, ink, true);
	}

	return Batch_create(PRIM_LINE_STRIP_ADJACENCY, vbo, NULL);
}

/* helper to convert 2d to 3d for simple drawing buffer */
static void gpencil_stroke_convertcoords(Scene *scene, ARegion *ar, ScrArea *sa, tGPspoint *point2D, float out[3], float *depth)
{
	float mval_f[2] = { point2D->x, point2D->y };
	float mval_prj[2];
	float rvec[3], dvec[3];
	float zfac;

	/* Current method just converts each point in screen-coordinates to
	* 3D-coordinates using the 3D-cursor as reference.
	*/
	View3D *v3d = sa->spacedata.first;
	const float *cursor = ED_view3d_cursor3d_get(scene, v3d);
	copy_v3_v3(rvec, cursor);

	zfac = ED_view3d_calc_zfac(ar->regiondata, rvec, NULL);

	if (ED_view3d_project_float_global(ar, rvec, mval_prj, V3D_PROJ_TEST_NOP) == V3D_PROJ_RET_OK) {
		sub_v2_v2v2(mval_f, mval_prj, mval_f);
		ED_view3d_win_to_delta(ar, mval_f, dvec, zfac);
		sub_v3_v3v3(out, rvec, dvec);
	}
	else {
		zero_v3(out);
	}
}

/* create batch geometry data for current buffer stroke shader */
Batch *gpencil_get_buffer_stroke_geom(bGPdata *gpd, short thickness)
{
	const struct bContext *C = DRW_get_context();
	Scene *scene = CTX_data_scene(C);
	ScrArea *sa = CTX_wm_area(C);
	ARegion *ar = CTX_wm_region(C);

	tGPspoint *points = gpd->sbuffer;
	int totpoints = gpd->sbuffer_size;

	static VertexFormat format = { 0 };
	static unsigned int pos_id, color_id, thickness_id;
	if (format.attrib_ct == 0) {
		pos_id = VertexFormat_add_attrib(&format, "pos", COMP_F32, 3, KEEP_FLOAT);
		color_id = VertexFormat_add_attrib(&format, "color", COMP_F32, 4, KEEP_FLOAT);
		thickness_id = VertexFormat_add_attrib(&format, "thickness", COMP_F32, 1, KEEP_FLOAT);
	}

	VertexBuffer *vbo = VertexBuffer_create_with_format(&format);
	VertexBuffer_allocate_data(vbo, totpoints + 2);

	/* draw stroke curve */
	const tGPspoint *tpt = points;
	bGPDspoint pt;
	int idx = 0;
	float p3d[3];

	float viewinvmat[4][4];
	DRW_viewport_matrix_get(viewinvmat, DRW_MAT_VIEWINV);

	for (int i = 0; i < totpoints; i++, tpt++) {
		/* need conversion to 3d format */
		gpencil_stroke_convertcoords(scene, ar, sa, tpt, p3d, NULL);
		copy_v3_v3(&pt.x, p3d);
		pt.pressure = tpt->pressure;
		pt.strength = tpt->strength;

		/* first point for adjacency (not drawn) */
		if (i == 0) {
			gpencil_set_stroke_point(vbo, &pt, idx, pos_id, color_id, thickness_id, thickness, gpd->scolor, true);
			++idx;
		}
		/* set point */
		gpencil_set_stroke_point(vbo, &pt, idx, pos_id, color_id, thickness_id, thickness, gpd->scolor, false);
		++idx;
	}

	/* last adjacency point (not drawn) */
	gpencil_set_stroke_point(vbo, &pt, idx, pos_id, color_id, thickness_id, thickness, gpd->scolor, true);

	return Batch_create(PRIM_LINE_STRIP_ADJACENCY, vbo, NULL);
}

/* create batch geometry data for current buffer fill shader */
Batch *gpencil_get_buffer_fill_geom(const tGPspoint *points, int totpoints, float ink[4])
{
	if (totpoints < 3) {
		return NULL;
	}

	const struct bContext *C = DRW_get_context();
	Scene *scene = CTX_data_scene(C);
	ScrArea *sa = CTX_wm_area(C);
	ARegion *ar = CTX_wm_region(C);

	int tot_triangles = totpoints - 2;
	/* allocate memory for temporary areas */
	unsigned int(*tmp_triangles)[3] = MEM_mallocN(sizeof(*tmp_triangles) * tot_triangles, "GP Stroke buffer temp triangulation");
	float(*points2d)[2] = MEM_mallocN(sizeof(*points2d) * totpoints, "GP Stroke buffer temp 2d points");

	/* Convert points to array and triangulate
	* Here a cache is not used because while drawing the information changes all the time, so the cache
	* would be recalculated constantly, so it is better to do direct calculation for each function call
	*/
	for (int i = 0; i < totpoints; i++) {
		const tGPspoint *pt = &points[i];
		points2d[i][0] = pt->x;
		points2d[i][1] = pt->y;
	}
	BLI_polyfill_calc((const float(*)[2])points2d, (unsigned int)totpoints, 0, (unsigned int(*)[3])tmp_triangles);

	static VertexFormat format = { 0 };
	static unsigned int pos_id, color_id;
	if (format.attrib_ct == 0) {
		pos_id = VertexFormat_add_attrib(&format, "pos", COMP_F32, 3, KEEP_FLOAT);
		color_id = VertexFormat_add_attrib(&format, "color", COMP_F32, 4, KEEP_FLOAT);
	}

	VertexBuffer *vbo = VertexBuffer_create_with_format(&format);

	/* draw triangulation data */
	if (tot_triangles > 0) {
		VertexBuffer_allocate_data(vbo, tot_triangles * 3);

		const tGPspoint *tpt;
		bGPDspoint pt;

		int idx = 0;
		float p3d[3];
		for (int i = 0; i < tot_triangles; i++) {
			/* vertex 1 */
			tpt = &points[tmp_triangles[i][0]];
			/* need conversion to 3d format */
			gpencil_stroke_convertcoords(scene, ar, sa, tpt, p3d, NULL);
			copy_v3_v3(&pt.x, p3d);
			pt.pressure = tpt->pressure;
			pt.strength = tpt->strength;

			VertexBuffer_set_attrib(vbo, pos_id, idx, &pt.x);
			VertexBuffer_set_attrib(vbo, color_id, idx, ink);
			++idx;
			/* vertex 2 */
			tpt = &points[tmp_triangles[i][1]];
			/* need conversion to 3d format */
			gpencil_stroke_convertcoords(scene, ar, sa, tpt, p3d, NULL);
			copy_v3_v3(&pt.x, p3d);
			pt.pressure = tpt->pressure;
			pt.strength = tpt->strength;

			VertexBuffer_set_attrib(vbo, pos_id, idx, &pt.x);
			VertexBuffer_set_attrib(vbo, color_id, idx, ink);
			++idx;
			/* vertex 3 */
			tpt = &points[tmp_triangles[i][2]];
			/* need conversion to 3d format */
			gpencil_stroke_convertcoords(scene, ar, sa, tpt, p3d, NULL);
			copy_v3_v3(&pt.x, p3d);
			pt.pressure = tpt->pressure;
			pt.strength = tpt->strength;

			VertexBuffer_set_attrib(vbo, pos_id, idx, &pt.x);
			VertexBuffer_set_attrib(vbo, color_id, idx, ink);
			++idx;
		}
	}

	/* clear memory */
	if (tmp_triangles) {
		MEM_freeN(tmp_triangles);
	}
	if (points2d) {
		MEM_freeN(points2d);
	}

	return Batch_create(PRIM_TRIANGLES, vbo, NULL);
}


/* Helper for doing all the checks on whether a stroke can be drawn */
bool gpencil_can_draw_stroke(const bGPDstroke *gps)
{
	/* skip stroke if it doesn't have any valid data */
	if ((gps->points == NULL) || (gps->totpoints < 1))
		return false;

	/* check if the color is visible */
	PaletteColor *palcolor = gps->palcolor;
	if ((palcolor == NULL) ||
		(palcolor->flag & PC_COLOR_HIDE))
	{
		return false;
	}

	/* stroke can be drawn */
	return true;
}

/* calc bounding box in 2d using flat projection data */
static void gpencil_calc_2d_bounding_box(const float(*points2d)[2], int totpoints, float minv[2], float maxv[2], bool expand)
{
	minv[0] = points2d[0][0];
	minv[1] = points2d[0][1];
	maxv[0] = points2d[0][0];
	maxv[1] = points2d[0][1];

	for (int i = 1; i < totpoints; i++)
	{
		/* min */
		if (points2d[i][0] < minv[0]) {
			minv[0] = points2d[i][0];
		}
		if (points2d[i][1] < minv[1]) {
			minv[1] = points2d[i][1];
		}
		/* max */
		if (points2d[i][0] > maxv[0]) {
			maxv[0] = points2d[i][0];
		}
		if (points2d[i][1] > maxv[1]) {
			maxv[1] = points2d[i][1];
		}
	}
	/* If not expanded, use a perfect square */
	if (expand == false) {
		if (maxv[0] > maxv[1]) {
			maxv[1] = maxv[0];
		}
		else {
			maxv[0] = maxv[1];
		}
	}
}

/* calc texture coordinates using flat projected points */
static void gpencil_calc_stroke_uv(const float(*points2d)[2], int totpoints, float minv[2], float maxv[2], float(*r_uv)[2])
{
	float d[2];
	d[0] = maxv[0] - minv[0];
	d[1] = maxv[1] - minv[1];
	for (int i = 0; i < totpoints; i++)
	{
		r_uv[i][0] = (points2d[i][0] - minv[0]) / d[0];
		r_uv[i][1] = (points2d[i][1] - minv[1]) / d[1];
	}
}

/* Get points of stroke always flat to view not affected by camera view or view position */
static void gpencil_stroke_2d_flat(const bGPDspoint *points, int totpoints, float(*points2d)[2], int *r_direction)
{
	const bGPDspoint *pt0 = &points[0];
	const bGPDspoint *pt1 = &points[1];
	const bGPDspoint *pt3 = &points[(int)(totpoints * 0.75)];

	float locx[3];
	float locy[3];
	float loc3[3];
	float normal[3];

	/* local X axis (p0 -> p1) */
	sub_v3_v3v3(locx, &pt1->x, &pt0->x);

	/* point vector at 3/4 */
	sub_v3_v3v3(loc3, &pt3->x, &pt0->x);

	/* vector orthogonal to polygon plane */
	cross_v3_v3v3(normal, locx, loc3);

	/* local Y axis (cross to normal/x axis) */
	cross_v3_v3v3(locy, normal, locx);

	/* Normalize vectors */
	normalize_v3(locx);
	normalize_v3(locy);

	/* Get all points in local space */
	for (int i = 0; i < totpoints; i++) {
		const bGPDspoint *pt = &points[i];
		float loc[3];

		/* Get local space using first point as origin */
		sub_v3_v3v3(loc, &pt->x, &pt0->x);

		points2d[i][0] = dot_v3v3(loc, locx);
		points2d[i][1] = dot_v3v3(loc, locy);
	}

	/* Concave (-1), Convex (1), or Autodetect (0)? */
	*r_direction = (int)locy[2];
}

/* Triangulate stroke for high quality fill (this is done only if cache is null or stroke was modified) */
static void gp_triangulate_stroke_fill(bGPDstroke *gps)
{
	BLI_assert(gps->totpoints >= 3);

	/* allocate memory for temporary areas */
	gps->tot_triangles = gps->totpoints - 2;
	unsigned int(*tmp_triangles)[3] = MEM_mallocN(sizeof(*tmp_triangles) * gps->tot_triangles, "GP Stroke temp triangulation");
	float(*points2d)[2] = MEM_mallocN(sizeof(*points2d) * gps->totpoints, "GP Stroke temp 2d points");
	float(*uv)[2] = MEM_mallocN(sizeof(*uv) * gps->totpoints, "GP Stroke temp 2d uv data");

	int direction = 0;

	/* convert to 2d and triangulate */
	gpencil_stroke_2d_flat(gps->points, gps->totpoints, points2d, &direction);
	BLI_polyfill_calc((const float(*)[2])points2d, (unsigned int)gps->totpoints, direction, (unsigned int(*)[3])tmp_triangles);

	/* calc texture coordinates automatically */
	float minv[2];
	float maxv[2];
	/* first needs bounding box data */
	gpencil_calc_2d_bounding_box((const float(*)[2])points2d, gps->totpoints, minv, maxv, false);
	/* calc uv data */
	gpencil_calc_stroke_uv((const float(*)[2])points2d, gps->totpoints, minv, maxv, uv);

	/* Number of triangles */
	gps->tot_triangles = gps->totpoints - 2;
	/* save triangulation data in stroke cache */
	if (gps->tot_triangles > 0) {
		if (gps->triangles == NULL) {
			gps->triangles = MEM_callocN(sizeof(*gps->triangles) * gps->tot_triangles, "GP Stroke triangulation");
		}
		else {
			gps->triangles = MEM_recallocN(gps->triangles, sizeof(*gps->triangles) * gps->tot_triangles);
		}

		for (int i = 0; i < gps->tot_triangles; i++) {
			bGPDtriangle *stroke_triangle = &gps->triangles[i];
			stroke_triangle->v1 = tmp_triangles[i][0];
			stroke_triangle->v2 = tmp_triangles[i][1];
			stroke_triangle->v3 = tmp_triangles[i][2];
			/* copy texture coordinates */
			copy_v2_v2(stroke_triangle->uv1, uv[tmp_triangles[i][0]]);
			copy_v2_v2(stroke_triangle->uv2, uv[tmp_triangles[i][1]]);
			copy_v2_v2(stroke_triangle->uv3, uv[tmp_triangles[i][2]]);
		}
	}
	else {
		/* No triangles needed - Free anything allocated previously */
		if (gps->triangles)
			MEM_freeN(gps->triangles);

		gps->triangles = NULL;
	}

	/* disable recalculation flag */
	if (gps->flag & GP_STROKE_RECALC_CACHES) {
		gps->flag &= ~GP_STROKE_RECALC_CACHES;
	}

	/* clear memory */
	if (tmp_triangles) MEM_freeN(tmp_triangles);
	if (points2d) MEM_freeN(points2d);
	if (uv) MEM_freeN(uv);
}

/* add a new fill point and texture coordinates to vertex buffer */
static void gpencil_set_fill_point(VertexBuffer *vbo, int idx, bGPDspoint *pt, float fcolor[4], float uv[2],
	unsigned int pos_id, unsigned int color_id, unsigned int text_id,
	short UNUSED(flag),	int UNUSED(offsx), int UNUSED(offsy), int UNUSED(winx), int UNUSED(winy))
{
#if 0
	/* if 2d, need conversion */
	if (!flag & GP_STROKE_3DSPACE) {
		float co[2];
		gp_calc_2d_stroke_fxy(fpt, flag, offsx, offsy, winx, winy, co);
		copy_v2_v2(fpt, co);
		fpt[2] = 0.0f; /* 2d always is z=0.0f */
	}
#endif

	VertexBuffer_set_attrib(vbo, pos_id, idx, &pt->x);
	VertexBuffer_set_attrib(vbo, color_id, idx, fcolor);
	VertexBuffer_set_attrib(vbo, text_id, idx, uv);
}

/* create batch geometry data for stroke shader */
Batch *gpencil_get_fill_geom(bGPDstroke *gps, float color[4])
{
	BLI_assert(gps->totpoints >= 3);
	int offsx = 0;
	int offsy = 0;
	const float *viewport = DRW_viewport_size_get();
	int winx = (int)viewport[0];
	int winy = (int)viewport[1];

	/* Calculate triangles cache for filling area (must be done only after changes) */
	if ((gps->flag & GP_STROKE_RECALC_CACHES) || (gps->tot_triangles == 0) || (gps->triangles == NULL)) {
		gp_triangulate_stroke_fill(gps);
	}
	BLI_assert(gps->tot_triangles >= 1);

	static VertexFormat format = { 0 };
	static unsigned int pos_id, color_id, text_id;
	if (format.attrib_ct == 0) {
		pos_id = VertexFormat_add_attrib(&format, "pos", COMP_F32, 3, KEEP_FLOAT);
		color_id = VertexFormat_add_attrib(&format, "color", COMP_F32, 4, KEEP_FLOAT);
		text_id = VertexFormat_add_attrib(&format, "texCoord", COMP_F32, 2, KEEP_FLOAT);
	}

	VertexBuffer *vbo = VertexBuffer_create_with_format(&format);
	VertexBuffer_allocate_data(vbo, gps->tot_triangles * 3);

	/* Draw all triangles for filling the polygon (cache must be calculated before) */
	bGPDtriangle *stroke_triangle = gps->triangles;
	int idx = 0;
	for (int i = 0; i < gps->tot_triangles; i++, stroke_triangle++) {
		gpencil_set_fill_point(vbo, idx, &gps->points[stroke_triangle->v1], color, stroke_triangle->uv1,
			pos_id, color_id, text_id, gps->flag,
			offsx, offsy, winx, winy);
		++idx;
		gpencil_set_fill_point(vbo, idx, &gps->points[stroke_triangle->v2], color, stroke_triangle->uv2,
			pos_id, color_id, text_id, gps->flag,
			offsx, offsy, winx, winy);
		++idx;
		gpencil_set_fill_point(vbo, idx, &gps->points[stroke_triangle->v3], color, stroke_triangle->uv3,
			pos_id, color_id, text_id, gps->flag,
			offsx, offsy, winx, winy);
		++idx;
	}

	return Batch_create(PRIM_TRIANGLES, vbo, NULL);
}

/* Draw selected verts for strokes being edited */
Batch *gpencil_get_edit_geom(bGPDstroke *gps, float alpha, short dflag)
{
	/* Get size of verts:
	* - The selected state needs to be larger than the unselected state so that
	*   they stand out more.
	* - We use the theme setting for size of the unselected verts
	*/
	float bsize = UI_GetThemeValuef(TH_GP_VERTEX_SIZE);
	float vsize;
	if ((int)bsize > 8) {
		vsize = 10.0f;
		bsize = 8.0f;
	}
	else {
		vsize = bsize + 2;
	}

	/* for now, we assume that the base color of the points is not too close to the real color */
	/* set color using palette */
	PaletteColor *palcolor = gps->palcolor;

	float selectColor[4];
	UI_GetThemeColor3fv(TH_GP_VERTEX_SELECT, selectColor);
	selectColor[3] = alpha;

	static VertexFormat format = { 0 };
	static unsigned int pos_id, color_id, size_id;
	if (format.attrib_ct == 0) {
		pos_id = VertexFormat_add_attrib(&format, "pos", COMP_F32, 3, KEEP_FLOAT);
		color_id = VertexFormat_add_attrib(&format, "color", COMP_F32, 4, KEEP_FLOAT);
		size_id = VertexFormat_add_attrib(&format, "size", COMP_F32, 1, KEEP_FLOAT);
	}

	VertexBuffer *vbo = VertexBuffer_create_with_format(&format);
	VertexBuffer_allocate_data(vbo, gps->totpoints);

	/* Draw start and end point differently if enabled stroke direction hint */
	bool show_direction_hint = (dflag & GP_DATA_SHOW_DIRECTION) && (gps->totpoints > 1);

	/* Draw all the stroke points (selected or not) */
	bGPDspoint *pt = gps->points;
	int idx = 0;
	float fcolor[4];
	float fsize = 0;

	for (int i = 0; i < gps->totpoints; i++, pt++) {
		if (show_direction_hint && i == 0) {
			/* start point in green bigger */
			ARRAY_SET_ITEMS(fcolor, 0.0f, 1.0f, 0.0f, 1.0f);
			fsize = vsize + 4;
		}
		else if (show_direction_hint && (i == gps->totpoints - 1)) {
			/* end point in red smaller */
			ARRAY_SET_ITEMS(fcolor, 1.0f, 0.0f, 0.0f, 1.0f);
			fsize = vsize + 1;
		}
		else if (pt->flag & GP_SPOINT_SELECT) {
			copy_v4_v4(fcolor, selectColor);
			fsize = vsize;
		}
		else {
			copy_v4_v4(fcolor, palcolor->rgb);
			fsize = bsize;
		}

		VertexBuffer_set_attrib(vbo, color_id, idx, fcolor);
		VertexBuffer_set_attrib(vbo, size_id, idx, &fsize);
		VertexBuffer_set_attrib(vbo, pos_id, idx, &pt->x);
		++idx;
	}

	return Batch_create(PRIM_POINTS, vbo, NULL);
}
