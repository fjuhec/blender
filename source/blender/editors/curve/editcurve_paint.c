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
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/curve/editcurve_paint.c
 *  \ingroup edcurve
 */

#include "DNA_key_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_anim_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_ghash.h"
#include "BLI_buffer.h"
#include "BLI_mempool.h"

#include "BLT_translation.h"

#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_depsgraph.h"
#include "BKE_displist.h"
#include "BKE_fcurve.h"
#include "BKE_global.h"
#include "BKE_key.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_report.h"
#include "BKE_animsys.h"
#include "BKE_action.h"
#include "BKE_modifier.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_keyframes_edit.h"
#include "ED_space_api.h"
#include "ED_object.h"
#include "ED_screen.h"
#include "ED_transform.h"
#include "ED_types.h"
#include "ED_util.h"
#include "ED_view3d.h"
#include "ED_curve.h"

#include "BIF_gl.h"

#include "curve_intern.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#define USE_SPLINE_FIT

#ifdef USE_SPLINE_FIT
#include "curve_fit_nd.h"
#endif

/* Distance between input samples */
#define STROKE_SAMPLE_DIST_PX 3

/* -------------------------------------------------------------------- */

/** \name StrokeElem / RNA_OperatorStrokeElement Conversion Functions
 * \{ */

struct StrokeElem {
	float mouse[2];
	float location_world[3];
	float location_local[3];
	float pressure;
};

struct CurveDrawData {
	short init_event_type;
	short nurbs_type;

	float    project_plane[4];
	bool use_project_plane;

	float mouse_prev[2];

	Scene *scene;

	struct ScrArea	*sa;
	struct ARegion	*ar;

	/* StrokeElem */
	BLI_mempool *stroke_elem_pool;

	void *draw_handle_view;
};

static void curve_draw_stroke_to_operator_elem(
        wmOperator *op, const struct StrokeElem *selem)
{
	PointerRNA itemptr;
	RNA_collection_add(op->ptr, "stroke", &itemptr);

	RNA_float_set_array(&itemptr, "mouse", selem->mouse);
	RNA_float_set_array(&itemptr, "location", selem->location_world);
	RNA_float_set(&itemptr, "pressure", selem->pressure);
}

static void curve_draw_stroke_from_operator_elem(
        wmOperator *op, PointerRNA *itemptr)
{
	struct CurveDrawData *cdd = op->customdata;

	struct StrokeElem *selem = BLI_mempool_calloc(cdd->stroke_elem_pool);

	RNA_float_get_array(itemptr, "mouse", selem->mouse);
	RNA_float_get_array(itemptr, "location", selem->location_world);
	mul_v3_m4v3(selem->location_local, cdd->scene->obedit->imat, selem->location_world);
	selem->pressure = RNA_float_get(itemptr, "pressure");
}

static void curve_draw_stroke_to_operator(wmOperator *op)
{
	struct CurveDrawData *cdd = op->customdata;

	BLI_mempool_iter iter;
	const struct StrokeElem *selem;

	BLI_mempool_iternew(cdd->stroke_elem_pool, &iter);
	for (selem = BLI_mempool_iterstep(&iter); selem; selem = BLI_mempool_iterstep(&iter)) {
		curve_draw_stroke_to_operator_elem(op, selem);
	}
}

static void curve_draw_stroke_from_operator(wmOperator *op)
{
	RNA_BEGIN (op->ptr, itemptr, "stroke")
	{
		curve_draw_stroke_from_operator_elem(op, &itemptr);
	}
	RNA_END;
}

/** \} */


enum {
	CURVE_PAINT_TYPE_BEZIER = 0,
	CURVE_PAINT_TYPE_POLY,
};

static void curve_draw_stroke_3d(const struct bContext *UNUSED(C), ARegion *UNUSED(ar), void *arg)
{
	wmOperator *op = arg;
	struct CurveDrawData *cdd = op->customdata;

	const int stroke_len = BLI_mempool_count(cdd->stroke_elem_pool);

	if (stroke_len == 0) {
		return;
	}

	View3D *v3d = cdd->sa->spacedata.first;
	Object *obedit = cdd->scene->obedit;
	Curve *cu = obedit->data;

	UI_ThemeColor(TH_WIRE);

	if (cu->ext2 > 0.0f) {
		GLUquadricObj *qobj = gluNewQuadric();

		gluQuadricDrawStyle(qobj, GLU_FILL);

		BLI_mempool_iter iter;
		const struct StrokeElem *selem;

		const float  location_zero[3] = {0};
		const float *location_prev = location_zero;

		/* scale to edit-mode space */
		glPushMatrix();
		glMultMatrixf(obedit->obmat);

		BLI_mempool_iternew(cdd->stroke_elem_pool, &iter);
		for (selem = BLI_mempool_iterstep(&iter); selem; selem = BLI_mempool_iterstep(&iter)) {
			glTranslatef(
			        selem->location_local[0] - location_prev[0],
			        selem->location_local[1] - location_prev[1],
			        selem->location_local[2] - location_prev[2]);
			location_prev = selem->location_local;
			gluSphere(qobj, (selem->pressure * cu->ext2), 16, 12);

			location_prev = selem->location_local;
		}

		glPopMatrix();

		gluDeleteQuadric(qobj);
	}

	if (stroke_len > 1) {
		float (*coord_array)[3] = MEM_mallocN(sizeof(*coord_array) * stroke_len, __func__);

		{
			BLI_mempool_iter iter;
			const struct StrokeElem *selem;
			int i;
			BLI_mempool_iternew(cdd->stroke_elem_pool, &iter);
			for (selem = BLI_mempool_iterstep(&iter), i = 0; selem; selem = BLI_mempool_iterstep(&iter), i++) {
				copy_v3_v3(coord_array[i], selem->location_world);
			}
		}

		{
			glEnable(GL_BLEND);
			glEnable(GL_LINE_SMOOTH);

			glEnableClientState(GL_VERTEX_ARRAY);
			glVertexPointer(3, GL_FLOAT, 0, coord_array);

			cpack(0x0);
			glLineWidth(3.0f);
			glDrawArrays(GL_LINE_STRIP, 0, stroke_len);

			if (v3d->zbuf)
				glDisable(GL_DEPTH_TEST);

			cpack(0xffffffff);
			glLineWidth(1.0f);
			glDrawArrays(GL_LINE_STRIP, 0, stroke_len);

			if (v3d->zbuf)
				glEnable(GL_DEPTH_TEST);

			glDisableClientState(GL_VERTEX_ARRAY);

			glDisable(GL_BLEND);
			glDisable(GL_LINE_SMOOTH);
		}

		MEM_freeN(coord_array);
	}
}


static void curve_draw_event_add(wmOperator *op, const wmEvent *event)
{
	struct CurveDrawData *cdd = op->customdata;
//	RegionView3D *rv3d = cdd->ar->regiondata;
	Object *obedit = cdd->scene->obedit;
	const float mval_fl[2] = {UNPACK2(event->mval)};
	View3D *v3d = cdd->sa->spacedata.first;

	invert_m4_m4(obedit->imat, obedit->obmat);

	struct StrokeElem *selem = BLI_mempool_calloc(cdd->stroke_elem_pool);

	/* project to 'location_world' */
//	if (cdd->use_project_plane)
	{
		/* get the view vector to 'location' */
		float ray_origin[3], ray_direction[3];
		ED_view3d_win_to_ray(cdd->ar, v3d, mval_fl, ray_origin, ray_direction, false);

		float lambda;
		if (isect_ray_plane_v3(ray_origin, ray_direction, cdd->project_plane, &lambda, false)) {
			madd_v3_v3v3fl(selem->location_world, ray_origin, ray_direction, lambda);
		}
	}
//	else {
////		ED_view3d_win_to_3d(cdd->ar, cursor, mval_fl, selem->location_world);
//	}

	// ED_view3d_win_to_3d(cdd->ar, cursor, mval_fl, selem->location_world);

	copy_v2_v2(selem->mouse, mval_fl);
	mul_v3_m4v3(selem->location_local, obedit->imat, selem->location_world);

	/* handle pressure sensitivity (which is supplied by tablets) */
	if (event->tablet_data) {
		const wmTabletData *wmtab = event->tablet_data;
		selem->pressure = wmtab->Pressure;
	}
	else {
		selem->pressure = 1.0f;
	}

	copy_v2_v2(cdd->mouse_prev, mval_fl);

	ED_region_tag_redraw(cdd->ar);
}


static void curve_draw_exit(wmOperator *op)
{
	struct CurveDrawData *cdd = op->customdata;
	if (cdd) {
		if (cdd->draw_handle_view) {
			ED_region_draw_cb_exit(cdd->ar->type, cdd->draw_handle_view);
		}

		if (cdd->stroke_elem_pool) {
			BLI_mempool_destroy(cdd->stroke_elem_pool);
		}

		MEM_freeN(cdd);
		op->customdata = NULL;
	}
}

static int curve_draw_exec(bContext *C, wmOperator *op)
{
	struct CurveDrawData *cdd = op->customdata;
	const CurvePaintSettings *cps = &cdd->scene->toolsettings->curve_paint_settings;
	Object *obedit = cdd->scene->obedit;
	Curve *cu = obedit->data;
	ListBase *nurblist = object_editcurve_get(obedit);

	int stroke_len = BLI_mempool_count(cdd->stroke_elem_pool);

	const bool is_3d = (cu->flag & CU_3D) != 0;
	const bool use_pressure_radius = (cps->flag & CURVE_PAINT_FLAG_PRESSURE_RADIUS) != 0;
	invert_m4_m4(obedit->imat, obedit->obmat);

	if (BLI_mempool_count(cdd->stroke_elem_pool) == 0) {
		curve_draw_stroke_from_operator(op);
		stroke_len = BLI_mempool_count(cdd->stroke_elem_pool);
	}

	ED_curve_deselect_all(cu->editnurb);

	const double radius_min = cps->radius_min;
	const double radius_max = cps->radius_max;
	const double radius_range = cps->radius_max - cps->radius_min;

	Nurb *nu = MEM_callocN(sizeof(Nurb), __func__);
	nu->pntsv = 1;
	nu->resolu = cu->resolu;
	nu->resolv = cu->resolv;
	nu->flag |= CU_SMOOTH;


	if (cdd->nurbs_type == CU_BEZIER) {
		nu->type = CU_BEZIER;

#ifdef USE_SPLINE_FIT

#define DIMS 4

		float (*coords)[DIMS] = MEM_mallocN(sizeof(*coords) * stroke_len, __func__);

		float       *cubic_spline = NULL;
		unsigned int cubic_spline_len = 0;

		PropertyRNA *prop_error = RNA_struct_find_property(op->ptr, "error");
		float error_threshold;  /* error in object local space */


		if (RNA_property_is_set(op->ptr, prop_error)) {
			error_threshold = RNA_property_float_get(op->ptr, prop_error);
		}
		else {
			/* error isnt set so we'll have to calculate it from the */
			BLI_mempool_iter iter;
			const struct StrokeElem *selem, *selem_prev;

			float len_3d = 0.0f, len_2d = 0.0f;
			float scale_px;  /* pixel to local space scale */

			int i = 0;
			BLI_mempool_iternew(cdd->stroke_elem_pool, &iter);
			selem_prev = BLI_mempool_iterstep(&iter);
			for (selem = BLI_mempool_iterstep(&iter); selem; selem = BLI_mempool_iterstep(&iter), i++) {
				len_3d += len_v3v3(selem->location_local, selem_prev->location_local);
				len_2d += len_v2v2(selem->mouse, selem_prev->mouse);
				selem_prev = selem;
			}
			scale_px = len_3d / len_2d;
			error_threshold = (float)cps->error_threshold * scale_px;
			RNA_property_float_set(op->ptr, prop_error, error_threshold);
			printf("%.6f ~ %.6f ~ %.6f\n", scale_px, len_3d, len_2d);
		}

		{
			BLI_mempool_iter iter;
			const struct StrokeElem *selem;

			int i = 0;
			BLI_mempool_iternew(cdd->stroke_elem_pool, &iter);
			for (selem = BLI_mempool_iterstep(&iter); selem; selem = BLI_mempool_iterstep(&iter), i++) {
				copy_v3_v3(coords[i], selem->location_local);
				coords[i][3] = selem->pressure;
			}
		}

		unsigned int *corners = NULL;
		unsigned int  corners_len = 0;

		if (cps->flag & CURVE_PAINT_FLAG_CORNERS_DETECT) {
			/* this could be configurable... */
			const float corner_radius_min = error_threshold;
			const float corner_radius_max = error_threshold * 3;
			const unsigned int samples_max = 16;
			spline_fit_corners_detect_fl(
			        (const float *)coords, stroke_len, DIMS,
			        corner_radius_min, corner_radius_max,
			        samples_max, cps->corner_angle,
			        &corners, &corners_len);
		}

		const int result = spline_fit_cubic_to_points_fl(
		        (const float *)coords, stroke_len, DIMS, error_threshold,
		        corners, corners_len,
		        &cubic_spline, &cubic_spline_len);
		MEM_freeN(coords);
		if (corners) {
			free(corners);
		}

		if (result == 0) {
			nu->pntsu = cubic_spline_len + 1;
			nu->bezt = MEM_callocN(sizeof(BezTriple) * nu->pntsu, __func__);

			float *fl = cubic_spline;
			for (int j = 0; j < cubic_spline_len; j++, fl += (DIMS * 4)) {
				const float *pt_l     = fl + (DIMS * 0);
				const float *handle_l = fl + (DIMS * 1);
				const float *handle_r = fl + (DIMS * 2);
				const float *pt_r     = fl + (DIMS * 3);

				zero_v3(nu->bezt[j + 0].vec[1]);
				zero_v3(nu->bezt[j + 0].vec[2]);
				zero_v3(nu->bezt[j + 1].vec[0]);
				zero_v3(nu->bezt[j + 1].vec[1]);

				copy_v3_v3(nu->bezt[j + 0].vec[1], pt_l);
				copy_v3_v3(nu->bezt[j + 0].vec[2], handle_l);
				copy_v3_v3(nu->bezt[j + 1].vec[0], handle_r);
				copy_v3_v3(nu->bezt[j + 1].vec[1], pt_r);

				if (use_pressure_radius) {
					nu->bezt[j + 0].radius = (pt_l[3] * radius_range) + radius_min;
					nu->bezt[j + 1].radius = (pt_r[3] * radius_range) + radius_min;
				}
				else {
					nu->bezt[j + 0].radius = radius_max;
					nu->bezt[j + 1].radius = radius_max;
				}
			}

			{
				BezTriple *bezt;
				bezt = &nu->bezt[0];
				flip_v3_v3v3(bezt->vec[0], bezt->vec[1], bezt->vec[2]);

				bezt = &nu->bezt[nu->pntsu - 1];
				flip_v3_v3v3(bezt->vec[2], bezt->vec[1], bezt->vec[0]);
			}

			for (int j = 0; j < nu->pntsu; j++) {
				BezTriple *bezt = &nu->bezt[j];
				float tan[2][3];

				sub_v3_v3v3(tan[0], bezt->vec[0], bezt->vec[1]);
				sub_v3_v3v3(tan[1], bezt->vec[2], bezt->vec[1]);
				float cross[3];

				cross_v3_v3v3(cross, tan[0], tan[1]);
				if (len_squared_v3(cross) < 1e-4f) {
					bezt->h1 = bezt->h2 = HD_ALIGN;
				}
				else {
					bezt->h1 = bezt->h2 = HD_FREE;
				}

				bezt->f1 = bezt->f2 = bezt->f3 = SELECT;
			}

		}
		if (cubic_spline) {
			free(cubic_spline);
		}

#undef DIMS

#else
		nu->pntsu = stroke_len;
		nu->bezt = MEM_callocN(nu->pntsu * sizeof(BezTriple), __func__);

		BezTriple *bezt = nu->bezt;

		{
			BLI_mempool_iter iter;
			const struct StrokeElem *selem;

			BLI_mempool_iternew(cdd->stroke_elem_pool, &iter);
			for (selem = BLI_mempool_iterstep(&iter); selem; selem = BLI_mempool_iterstep(&iter)) {
				copy_v3_v3(bezt->vec[1], selem->location_local);
				if (!is_3d) {
					bezt->vec[1][2] = 0.0f;
				}

				if (use_pressure_radius) {
					bezt->radius = selem->pressure;
				}
				else {
					bezt->radius = cps->radius_max;
				}


				bezt->h1 = bezt->h2 = HD_AUTO;

				bezt->f1 |= SELECT;
				bezt->f2 |= SELECT;
				bezt->f3 |= SELECT;

				bezt++;
			}
		}
#endif

		BKE_nurb_handles_calc(nu);
	}
	else {  /* CU_POLY */
		BLI_mempool_iter iter;
		const struct StrokeElem *selem;

		nu->pntsu = stroke_len;
		nu->type = CU_POLY;
		nu->bp = MEM_callocN(nu->pntsu * sizeof(BPoint), __func__);

		BPoint *bp = nu->bp;

		BLI_mempool_iternew(cdd->stroke_elem_pool, &iter);
		for (selem = BLI_mempool_iterstep(&iter); selem; selem = BLI_mempool_iterstep(&iter)) {
			copy_v3_v3(bp->vec, selem->location_local);
			if (!is_3d) {
				bp->vec[2] = 0.0f;
			}

			if (use_pressure_radius) {
				bp->radius = (selem->pressure * radius_range) + radius_min;
			}
			else {
				bp->radius = cps->radius_max;
			}
			bp->f1 = SELECT;
			bp->vec[3] = 1.0f;

			bp++;
		}

		BKE_nurb_knot_calc_u(nu);
	}

	BLI_addtail(nurblist, nu);

	BKE_curve_nurb_active_set(cu, nu);
	cu->actvert = nu->pntsu - 1;

	WM_event_add_notifier(C, NC_GEOM | ND_DATA, obedit->data);
	DAG_id_tag_update(obedit->data, 0);

	return OPERATOR_FINISHED;
}

static int curve_draw_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	UNUSED_VARS(event);
	struct CurveDrawData *cdd;

	cdd = MEM_mallocN(sizeof(*cdd), __func__);
	op->customdata = cdd;

	cdd->init_event_type = event->type;

	cdd->nurbs_type = RNA_enum_get(op->ptr, "type");

	cdd->scene = CTX_data_scene(C);
	cdd->sa = CTX_wm_area(C);
	cdd->ar = CTX_wm_region(C);

	cdd->stroke_elem_pool = BLI_mempool_create(
	        sizeof(struct StrokeElem), 0, 512, BLI_MEMPOOL_ALLOW_ITER);

	cdd->draw_handle_view = ED_region_draw_cb_activate(
	        cdd->ar->type, curve_draw_stroke_3d, op, REGION_DRAW_POST_VIEW);


	{
		View3D *v3d = cdd->sa->spacedata.first;
		RegionView3D *rv3d = cdd->ar->regiondata;
		Object *obedit = cdd->scene->obedit;
		Curve *cu = obedit->data;

		if ((cu->flag & CU_3D) == 0) {
			/* 2D overrides other options */
			normalize_v3_v3(cdd->project_plane, obedit->obmat[2]);
			cdd->project_plane[3] = -dot_v3v3(cdd->project_plane, obedit->obmat[3]);
		}
		else {
			const float *cursor = ED_view3d_cursor3d_get(cdd->scene, v3d);
			normalize_v3_v3(cdd->project_plane, rv3d->viewinv[2]);
			cdd->project_plane[3] = -dot_v3v3(cdd->project_plane, cursor);
		}
	}

	/* add temp handler */
	WM_event_add_modal_handler(C, op);

	/* add first point */
	curve_draw_event_add(op, event);

	return OPERATOR_RUNNING_MODAL;
}

static void curve_draw_cancel(bContext *UNUSED(C), wmOperator *op)
{
	curve_draw_exit(op);
}


/* Modal event handling of frame changing */
static int curve_draw_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
	int ret = OPERATOR_RUNNING_MODAL;
	struct CurveDrawData *cdd = op->customdata;

	UNUSED_VARS(C, op);

	if (event->type == cdd->init_event_type) {
		if (event->val == KM_RELEASE) {
			curve_draw_stroke_to_operator(op);

			curve_draw_exec(C, op);

			ED_region_tag_redraw(cdd->ar);
			curve_draw_exit(op);
			return OPERATOR_FINISHED;
		}
	}
	else if (ELEM(event->type, MOUSEMOVE, INBETWEEN_MOUSEMOVE)) {
		const float mval_fl[2] = {UNPACK2(event->mval)};
		if (len_squared_v2v2(mval_fl, cdd->mouse_prev) > SQUARE(STROKE_SAMPLE_DIST_PX)) {
			curve_draw_event_add(op, event);
		}
	}

	return ret;
}

static EnumPropertyItem prop_curve_draw_types[] = {
	{CU_POLY, "POLY", 0, "Polygon", ""},
	{CU_BEZIER, "BEZIER", 0, "Bezier", ""},
	{0, NULL, 0, NULL, NULL}
};

void CURVE_OT_draw(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Draw Curve";
	ot->idname = "CURVE_OT_draw";
	ot->description = "Draw a freehand spline";

	/* api callbacks */
	ot->exec = curve_draw_exec;
	ot->invoke = curve_draw_invoke;
	ot->cancel = curve_draw_cancel;
	ot->modal = curve_draw_modal;
	ot->poll = ED_operator_editcurve;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* properties */
	ot->prop = RNA_def_enum(ot->srna, "type", prop_curve_draw_types, CU_BEZIER, "Type", "");

	RNA_def_float(ot->srna, "error", 0.0f, 0.0f, 10.0f, "Error", "", 0.0001f, 10.0f);

	RNA_def_collection_runtime(ot->srna, "stroke", &RNA_OperatorStrokeElement, "Stroke", "");
}