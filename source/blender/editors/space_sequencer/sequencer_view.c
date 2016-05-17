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
 * The Original Code is Copyright (C) 2012 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Blender Foundation,
 *                 Sergey Sharybin
 *
 * ***** END GPL LICENSE BLOCK *****
 */


/** \file blender/editors/space_sequencer/sequencer_view.c
 *  \ingroup spseq
 */

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_utildefines.h"
#include "BLI_rect.h"

#include "DNA_scene_types.h"
#include "DNA_widget_types.h"

#include "BKE_context.h"
#include "BKE_main.h"
#include "BKE_sequencer.h"
#include "BKE_screen.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_image.h"
#include "ED_screen.h"
#include "ED_space_api.h"
#include "ED_sequencer.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"
#include "IMB_colormanagement.h"

#include "UI_view2d.h"

#include "RNA_define.h"

/* own include */
#include "sequencer_intern.h"

/******************** sample backdrop operator ********************/

typedef struct ImageSampleInfo {
	ARegionType *art;
	void *draw_handle;
	int x, y;
	int channels;

	unsigned char col[4];
	float colf[4];
	float linearcol[4];

	unsigned char *colp;
	const float *colfp;

	int draw;
int color_manage;
} ImageSampleInfo;

static void sample_draw(const bContext *C, ARegion *ar, void *arg_info)
{
	Scene *scene = CTX_data_scene(C);
	ImageSampleInfo *info = arg_info;

	if (info->draw) {
		ED_image_draw_info(scene, ar, info->color_manage, false, info->channels,
		                   info->x, info->y, info->colp, info->colfp,
		                   info->linearcol, NULL, NULL);
	}
}

static void sample_apply(bContext *C, wmOperator *op, const wmEvent *event)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	SpaceSeq *sseq = (SpaceSeq *) CTX_wm_space_data(C);
	ARegion *ar = CTX_wm_region(C);
	ImBuf *ibuf = sequencer_ibuf_get(bmain, scene, sseq, CFRA, 0, NULL);
	ImageSampleInfo *info = op->customdata;
	float fx, fy;
	
	if (ibuf == NULL) {
		IMB_freeImBuf(ibuf);
		info->draw = 0;
		return;
	}

	UI_view2d_region_to_view(&ar->v2d, event->mval[0], event->mval[1], &fx, &fy);

	fx += (float) ibuf->x / 2.0f;
	fy += (float) ibuf->y / 2.0f;

	if (fx >= 0.0f && fy >= 0.0f && fx < ibuf->x && fy < ibuf->y) {
		const float *fp;
		unsigned char *cp;
		int x = (int) fx, y = (int) fy;

		info->x = x;
		info->y = y;
		info->draw = 1;
		info->channels = ibuf->channels;

		info->colp = NULL;
		info->colfp = NULL;
		
		if (ibuf->rect) {
			cp = (unsigned char *)(ibuf->rect + y * ibuf->x + x);

			info->col[0] = cp[0];
			info->col[1] = cp[1];
			info->col[2] = cp[2];
			info->col[3] = cp[3];
			info->colp = info->col;

			info->colf[0] = (float)cp[0] / 255.0f;
			info->colf[1] = (float)cp[1] / 255.0f;
			info->colf[2] = (float)cp[2] / 255.0f;
			info->colf[3] = (float)cp[3] / 255.0f;
			info->colfp = info->colf;

			copy_v4_v4(info->linearcol, info->colf);
			IMB_colormanagement_colorspace_to_scene_linear_v4(info->linearcol, false, ibuf->rect_colorspace);

			info->color_manage = true;
		}
		if (ibuf->rect_float) {
			fp = (ibuf->rect_float + (ibuf->channels) * (y * ibuf->x + x));

			info->colf[0] = fp[0];
			info->colf[1] = fp[1];
			info->colf[2] = fp[2];
			info->colf[3] = fp[3];
			info->colfp = info->colf;

			/* sequencer's image buffers are in non-linear space, need to make them linear */
			copy_v4_v4(info->linearcol, info->colf);
			BKE_sequencer_pixel_from_sequencer_space_v4(scene, info->linearcol);

			info->color_manage = true;
		}
	}
	else {
		info->draw = 0;
	}

	IMB_freeImBuf(ibuf);
	ED_area_tag_redraw(CTX_wm_area(C));
}

static void sample_exit(bContext *C, wmOperator *op)
{
	ImageSampleInfo *info = op->customdata;

	ED_region_draw_cb_exit(info->art, info->draw_handle);
	ED_area_tag_redraw(CTX_wm_area(C));
	MEM_freeN(info);
}

static int sample_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	ARegion *ar = CTX_wm_region(C);
	SpaceSeq *sseq = CTX_wm_space_seq(C);
	ImageSampleInfo *info;

	if (sseq->mainb != SEQ_DRAW_IMG_IMBUF)
		return OPERATOR_CANCELLED;

	info = MEM_callocN(sizeof(ImageSampleInfo), "ImageSampleInfo");
	info->art = ar->type;
	info->draw_handle = ED_region_draw_cb_activate(ar->type, sample_draw, info, REGION_DRAW_POST_PIXEL);
	op->customdata = info;

	sample_apply(C, op, event);

	WM_event_add_modal_handler(C, op);

	return OPERATOR_RUNNING_MODAL;
}

static int sample_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
	switch (event->type) {
		case LEFTMOUSE:
		case RIGHTMOUSE: /* XXX hardcoded */
			if (event->val == KM_RELEASE) {
				sample_exit(C, op);
				return OPERATOR_CANCELLED;
			}
			break;
		case MOUSEMOVE:
			sample_apply(C, op, event);
			break;
	}

	return OPERATOR_RUNNING_MODAL;
}

static void sample_cancel(bContext *C, wmOperator *op)
{
	sample_exit(C, op);
}

static int sample_poll(bContext *C)
{
	SpaceSeq *sseq = CTX_wm_space_seq(C);
	return sseq && BKE_sequencer_editing_get(CTX_data_scene(C), false) != NULL;
}

void SEQUENCER_OT_sample(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Sample Color";
	ot->idname = "SEQUENCER_OT_sample";
	ot->description = "Use mouse to sample color in current frame";

	/* api callbacks */
	ot->invoke = sample_invoke;
	ot->modal = sample_modal;
	ot->cancel = sample_cancel;
	ot->poll = sample_poll;

	/* flags */
	ot->flag = OPTYPE_BLOCKING;
}

/******** Backdrop Transform *******/

typedef struct OverDropTransformData {
	ImBuf *ibuf; /* image to be transformed (preview image transformation widget) */
	int init_size[2];
	float init_zoom;
	float init_offset[2];
	int event_type;
} OverDropTransformData;

static int sequencer_overdrop_transform_poll(bContext *C)
{
	SpaceSeq *sseq = CTX_wm_space_seq(C);
	ARegion *ar = CTX_wm_region(C);

	return (sseq && ar && ar->type->regionid == RGN_TYPE_WINDOW && (sseq->draw_flag & SEQ_DRAW_OVERDROP));
}

static void widgetgroup_overdrop_init(const bContext *UNUSED(C), wmWidgetGroup *wgroup)
{
	wmWidgetWrapper *wwrapper = MEM_mallocN(sizeof(wmWidgetWrapper), __func__);
	wgroup->customdata = wwrapper;

	wwrapper->widget = WIDGET_rect_transform_new(
	                       wgroup, "overdrop_cage",
	                       WIDGET_RECT_TRANSFORM_STYLE_SCALE_UNIFORM | WIDGET_RECT_TRANSFORM_STYLE_TRANSLATE);
}

static void widgetgroup_overdrop_refresh(const bContext *C, wmWidgetGroup *wgroup)
{
	wmWidget *cage = ((wmWidgetWrapper *)wgroup->customdata)->widget;
	const Scene *sce = CTX_data_scene(C);
	const ARegion *ar = CTX_wm_region(C);
	const float origin[3] = {BLI_rcti_size_x(&ar->winrct) / 2.0f, BLI_rcti_size_y(&ar->winrct)/2.0f};
	const int sizex = (sce->r.size * sce->r.xsch) / 100;
	const int sizey = (sce->r.size * sce->r.ysch) / 100;

	/* XXX hmmm, can't we do this in _init somehow? Issue is op->ptr is freed after OP is done. */
	wmOperator *op = wgroup->type->op;
	WM_widget_set_property(cage, RECT_TRANSFORM_SLOT_OFFSET, op->ptr, "offset");
	WM_widget_set_property(cage, RECT_TRANSFORM_SLOT_SCALE, op->ptr, "scale");

	WM_widget_set_origin(cage, origin);
	WIDGET_rect_transform_set_dimensions(cage, sizex, sizey);
}

static wmWidgetGroupType *sequencer_overdrop_widgets(void)
{
	/* no poll, lives always for the duration of the operator */
	return WM_widgetgrouptype_register_update(
	        NULL,
	        &(const struct wmWidgetMapType_Params) {"Seq_Canvas", SPACE_SEQ, RGN_TYPE_WINDOW, 0},
	        NULL,
	        widgetgroup_overdrop_init,
	        widgetgroup_overdrop_refresh,
	        NULL,
	        WM_widgetgroup_keymap_common,
	        "Backdrop Transform Widgets");
}

static int sequencer_overdrop_transform_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	ScrArea *sa = CTX_wm_area(C);
	SpaceSeq *sseq = CTX_wm_space_seq(C);
	OverDropTransformData *data = MEM_mallocN(sizeof(OverDropTransformData), "overdrop transform data");
	
	RNA_float_set_array(op->ptr, "offset", sseq->overdrop_offset);
	RNA_float_set(op->ptr, "scale", sseq->overdrop_zoom);

	copy_v2_v2(data->init_offset, sseq->overdrop_offset);
	data->init_zoom = sseq->overdrop_zoom;
	data->event_type = event->type;

	op->customdata = data;
	WM_event_add_modal_handler(C, op);

	ED_area_headerprint(sa, "Drag to place, and scale, Space/Enter/Caller key to confirm, R to recenter, RClick/Esc to cancel");
	
	return OPERATOR_RUNNING_MODAL;
}

static void sequencer_overdrop_finish(bContext *C, OverDropTransformData *data)
{
	ScrArea *sa = CTX_wm_area(C);
	ED_area_headerprint(sa, NULL);
	MEM_freeN(data);
}

static void sequencer_overdrop_cancel(struct bContext *C, struct wmOperator *op)
{
	OverDropTransformData *data = op->customdata;
	sequencer_overdrop_finish(C, data);
}

static int sequencer_overdrop_transform_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
	OverDropTransformData *data = op->customdata;
	ARegion *ar = CTX_wm_region(C);
	wmWidgetMap *wmap = ar->widgetmaps.first;

	if (event->type == data->event_type && event->val == KM_PRESS) {
		sequencer_overdrop_finish(C, data);
		return OPERATOR_FINISHED;
	}
	
	switch (event->type) {
		case EVT_WIDGET_UPDATE:
		{
			SpaceSeq *sseq = CTX_wm_space_seq(C);
			RNA_float_get_array(op->ptr, "offset", sseq->overdrop_offset);
			sseq->overdrop_zoom = RNA_float_get(op->ptr, "scale");
			break;
		}
		case RKEY:
		{
			SpaceSeq *sseq = CTX_wm_space_seq(C);
			float zero[2] = {0.0f};
			RNA_float_set_array(op->ptr, "offset", zero);
			RNA_float_set(op->ptr, "scale", 1.0f);
			copy_v2_v2(sseq->overdrop_offset, zero);
			sseq->overdrop_zoom = 1.0;
			ED_region_tag_redraw(ar);
			/* add a mousemove to refresh the widget */
			WM_event_add_mousemove(C);
			break;
		}
		case RETKEY:
		case PADENTER:
		case SPACEKEY:
		{
			sequencer_overdrop_finish(C, data);
			return OPERATOR_FINISHED;
		}
			
		case ESCKEY:
		case RIGHTMOUSE:
		{
			SpaceSeq *sseq = CTX_wm_space_seq(C);

			/* only end modal if we're not dragging a widget */
			if (!wmap->wmap_context.active_widget && event->val == KM_PRESS) {
				copy_v2_v2(sseq->overdrop_offset, data->init_offset);
				sseq->overdrop_zoom = data->init_zoom;

				sequencer_overdrop_finish(C, data);
				return OPERATOR_CANCELLED;
			}
		}
	}
	WM_widgetmap_tag_refresh(wmap);

	return OPERATOR_RUNNING_MODAL;
}

void SEQUENCER_OT_overdrop_transform(struct wmOperatorType *ot)
{
	float default_offset[2] = {0.0f, 0.0f};
	
	/* identifiers */
	ot->name = "Change Data/Files";
	ot->idname = "SEQUENCER_OT_overdrop_transform";
	ot->description = "";

	/* api callbacks */
	ot->invoke = sequencer_overdrop_transform_invoke;
	ot->modal = sequencer_overdrop_transform_modal;
	ot->poll = sequencer_overdrop_transform_poll;
	ot->cancel = sequencer_overdrop_cancel;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	ot->wgrouptype = sequencer_overdrop_widgets();

	RNA_def_float_array(ot->srna, "offset", 2, default_offset, FLT_MIN, FLT_MAX, "Offset", "Offset of the backdrop", FLT_MIN, FLT_MAX);
	RNA_def_float(ot->srna, "scale", 1.0f, 0.0f, FLT_MAX, "Scale", "Scale of the backdrop", 0.0f, FLT_MAX);
}

/******** transform widget (preview area) *******/

typedef struct ImageTransformData {
	ImBuf *ibuf; /* image to be transformed (preview image transformation widget) */
	int init_size[2];
	int event_type;
} ImageTransformData;

static int sequencer_image_transform_widget_poll(bContext *C)
{
	SpaceSeq *sseq = CTX_wm_space_seq(C);
	ARegion *ar = CTX_wm_region(C);

	return (sseq && ar && ar->type->regionid == RGN_TYPE_PREVIEW);
}

static void widgetgroup_image_transform_init(const bContext *UNUSED(C), wmWidgetGroup *wgroup)
{
	wmWidgetWrapper *wwrapper = MEM_mallocN(sizeof(wmWidgetWrapper), __func__);
	wgroup->customdata = wwrapper;

	wwrapper->widget = WIDGET_rect_transform_new(
	                       wgroup, "image_cage",
	                       WIDGET_RECT_TRANSFORM_STYLE_SCALE_UNIFORM | WIDGET_RECT_TRANSFORM_STYLE_TRANSLATE);
}

static void widgetgroup_image_transform_refresh(const bContext *C, wmWidgetGroup *wgroup)
{
	wmWidget *cage = ((wmWidgetWrapper *)wgroup->customdata)->widget;
	ARegion *ar = CTX_wm_region(C);
	View2D *v2d = &ar->v2d;
	float viewrect[2];
	float scale[2];

	sequencer_display_size(CTX_data_scene(C), CTX_wm_space_seq(C), viewrect);
	UI_view2d_scale_get(v2d, &scale[0], &scale[1]);

	wmOperator *op = wgroup->type->op;
	WM_widget_set_property(cage, RECT_TRANSFORM_SLOT_SCALE, op->ptr, "scale");

	const float origin[3] = {-(v2d->cur.xmin * scale[0]), -(v2d->cur.ymin * scale[1])};
	WM_widget_set_origin(cage, origin);
	WIDGET_rect_transform_set_dimensions(cage, viewrect[0] * scale[0], viewrect[1] * scale[1]);
}

static wmWidgetGroupType *sequencer_image_transform_widgets(void)
{
	/* no poll, lives always for the duration of the operator */
	return WM_widgetgrouptype_register_update(
	        NULL,
	        &(const struct wmWidgetMapType_Params) {"Seq_Canvas", SPACE_SEQ, RGN_TYPE_PREVIEW, 0},
	        NULL,
	        widgetgroup_image_transform_init,
	        widgetgroup_image_transform_refresh,
	        NULL,
	        WM_widgetgroup_keymap_common,
	        "Image Transform Widgets");
}

static int sequencer_image_transform_widget_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	ScrArea *sa = CTX_wm_area(C);
	SpaceSeq *sseq = CTX_wm_space_seq(C);
	Scene *scene = CTX_data_scene(C);
	ImageTransformData *data = MEM_mallocN(sizeof(ImageTransformData), "overdrop transform data");
	ImBuf *ibuf = sequencer_ibuf_get(CTX_data_main(C), scene, sseq, CFRA, 0, NULL);

	if (!ibuf || !ED_space_sequencer_check_show_imbuf(sseq)) {
		return OPERATOR_CANCELLED;
	}

	copy_v2_v2_int(data->init_size, &ibuf->x);
	data->event_type = event->type;
	data->ibuf = ibuf;

	op->customdata = data;
	WM_event_add_modal_handler(C, op);

	ED_area_headerprint(sa, "Drag to place, and scale, Space/Enter/Caller key to confirm, R to recenter, RClick/Esc to cancel");

	return OPERATOR_RUNNING_MODAL;
}

static void sequencer_image_transform_widget_finish(bContext *C, ImageTransformData *data)
{
	ScrArea *sa = CTX_wm_area(C);
	ED_area_headerprint(sa, NULL);
	MEM_freeN(data);
}

static void sequencer_image_transform_widget_cancel(struct bContext *C, struct wmOperator *op)
{
	ImageTransformData *data = op->customdata;
	sequencer_image_transform_widget_finish(C, data);
}

static int sequencer_image_transform_widget_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
	ImageTransformData *data = op->customdata;
	ARegion *ar = CTX_wm_region(C);
	wmWidgetMap *wmap = ar->widgetmaps.first;

	if (event->type == data->event_type && event->val == KM_PRESS) {
		sequencer_image_transform_widget_finish(C, data);
		return OPERATOR_FINISHED;
	}

	switch (event->type) {
		case EVT_WIDGET_UPDATE:
		{
			Scene *scene = CTX_data_scene(C);
			float scale_fac = RNA_float_get(op->ptr, "scale");
			float new_size[2];
			float offset[2];

			new_size[0] = (float)data->init_size[0] * scale_fac;
			new_size[1] = (float)data->init_size[1] * scale_fac;

			/* sale image */
			IMB_scalefastImBuf(data->ibuf, (unsigned int)new_size[0], (unsigned int)new_size[1]);

			/* update view */
			scene->r.xsch = (int)(new_size[0] / ((float)scene->r.size / 100));
			scene->r.ysch = (int)(new_size[1] / ((float)scene->r.size / 100));

			/* no offset needed in this case */
			offset[0] = offset[1] = 0;
			WM_widget_set_offset(wmap->wmap_context.active_widget, offset);
			break;
		}

		case RKEY:
		{
//			SpaceSeq *sseq = CTX_wm_space_seq(C);
//			float zero[2] = {0.0f};
//			RNA_float_set_array(op->ptr, "offset", zero);
//			RNA_float_set(op->ptr, "scale", 1.0f);
//			copy_v2_v2(sseq->overdrop_offset, zero);
//			sseq->overdrop_zoom = 1.0;
			ED_region_tag_redraw(ar);
			/* add a mousemove to refresh the widget */
			WM_event_add_mousemove(C);
			break;
		}
		case RETKEY:
		case PADENTER:
		case SPACEKEY:
		{
			sequencer_image_transform_widget_finish(C, data);
			return OPERATOR_FINISHED;
		}

		case ESCKEY:
		case RIGHTMOUSE:
		{
//			SpaceSeq *sseq = CTX_wm_space_seq(C);
//			copy_v2_v2(sseq->overdrop_offset, data->init_offset);
//			sseq->overdrop_zoom = data->init_zoom;

			sequencer_image_transform_widget_finish(C, data);
			return OPERATOR_CANCELLED;
		}
	}
	WM_widgetmap_tag_refresh(wmap);

	return OPERATOR_RUNNING_MODAL;
}

void SEQUENCER_OT_image_transform_widget(struct wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Image Transform";
	ot->idname = "SEQUENCER_OT_image_transform_widget";
	ot->description = "Transform the image using a widget";

	/* api callbacks */
	ot->invoke = sequencer_image_transform_widget_invoke;
	ot->modal = sequencer_image_transform_widget_modal;
	ot->poll = sequencer_image_transform_widget_poll;
	ot->cancel = sequencer_image_transform_widget_cancel;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	ot->wgrouptype = sequencer_image_transform_widgets();

	RNA_def_float(ot->srna, "scale", 1.0f, 0.0f, FLT_MAX, "Scale", "Scale of the backdrop", 0.0f, FLT_MAX);
}

