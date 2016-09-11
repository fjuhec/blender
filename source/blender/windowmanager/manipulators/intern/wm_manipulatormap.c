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
 * The Original Code is Copyright (C) 2014 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/manipulators/intern/wm_manipulatormap.c
 *  \ingroup wm
 */

#include <string.h>

#include "BKE_context.h"

#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_string.h"
#include "BLI_ghash.h"

#include "ED_screen.h"
#include "ED_view3d.h"

#include "GL/glew.h"
#include "GPU_select.h"

#include "MEM_guardedalloc.h"

#include "WM_api.h"
#include "WM_types.h"
#include "wm_event_system.h"

/* own includes */
#include "wm_manipulator_wmapi.h"
#include "wm_manipulator_intern.h"

/**
 * Store all widgetboxmaps here. Anyone who wants to register a widget for a certain
 * area type can query the widget map to do so.
 */
static ListBase widgetmaptypes = {NULL, NULL};

/**
 * List of all visible widgets to avoid unnecessary loops and wmManipulatorGroupType->poll checks.
 * Collected in WM_manipulators_update.
 */
static ListBase draw_widgets = {NULL, NULL};

/**
 * Widget map update/init tagging.
 */
enum eWidgetMapUpdateFlags {
	/* Set to init widget map. Should only be the case on first draw. */
	WIDGETMAP_INIT    = (1 << 0),
	/* Tag widget map for refresh. */
	WIDGETMAP_REFRESH = (1 << 1),
};


/* -------------------------------------------------------------------- */
/** \name wmManipulatorMap
 *
 * \{ */

/**
 * creates a widgetmap with all registered widgets for that type
 */
wmManipulatorMap *WM_manipulatormap_from_type(const struct wmManipulatorMapType_Params *wmap_params)
{
	wmManipulatorMapType *wmaptype = WM_manipulatormaptype_ensure(wmap_params);
	wmManipulatorMap *wmap;

	wmap = MEM_callocN(sizeof(wmManipulatorMap), "WidgetMap");
	wmap->type = wmaptype;
	wmap->update_flag |= (WIDGETMAP_INIT | WIDGETMAP_REFRESH);

	/* create all widgetgroups for this widgetmap. We may create an empty one
	 * too in anticipation of widgets from operators etc */
	for (wmManipulatorGroupType *wgrouptype = wmaptype->widgetgrouptypes.first; wgrouptype; wgrouptype = wgrouptype->next) {
		wmManipulatorGroup *wgroup = MEM_callocN(sizeof(wmManipulatorGroup), "widgetgroup");
		wgroup->type = wgrouptype;
		BLI_addtail(&wmap->widgetgroups, wgroup);
	}

	return wmap;
}

void WM_manipulatormap_selected_delete(wmManipulatorMap *wmap)
{
	MEM_SAFE_FREE(wmap->wmap_context.selected_widgets);
	wmap->wmap_context.tot_selected = 0;
}

void WM_manipulatormap_delete(wmManipulatorMap *wmap)
{
	if (!wmap)
		return;

	for (wmManipulatorGroup *wgroup = wmap->widgetgroups.first, *wgroup_next; wgroup; wgroup = wgroup_next) {
		wgroup_next = wgroup->next;
		WM_manipulatorgroup_free(NULL, wmap, wgroup);
	}
	BLI_assert(BLI_listbase_is_empty(&wmap->widgetgroups));

	WM_manipulatormap_selected_delete(wmap);

	MEM_freeN(wmap);
}

wmManipulatorMap *WM_manipulatormap_find(
        const ARegion *ar, const struct wmManipulatorMapType_Params *wmap_params)
{
	for (wmManipulatorMap *wmap = ar->widgetmaps.first; wmap; wmap = wmap->next) {
		const wmManipulatorMapType *wmaptype = wmap->type;

		if (wmaptype->spaceid == wmap_params->spaceid &&
		    wmaptype->regionid == wmap_params->regionid &&
		    STREQ(wmaptype->idname, wmap_params->idname))
		{
			return wmap;
		}
	}

	return NULL;
}

/**
 * Creates and returns idname hash table for (visible) widgets in \a wmap
 *
 * \param poll  Polling function for excluding widgets.
 * \param data  Custom data passed to \a poll
 */
static GHash *WM_manipulatormap_manipulator_hash_new(
        const bContext *C, wmManipulatorMap *wmap,
        bool (*poll)(const wmManipulator *, void *),
        void *data, const bool include_hidden)
{
	GHash *hash = BLI_ghash_str_new(__func__);

	/* collect widgets */
	for (wmManipulatorGroup *wgroup = wmap->widgetgroups.first; wgroup; wgroup = wgroup->next) {
		if (!wgroup->type->poll || wgroup->type->poll(C, wgroup->type)) {
			for (wmManipulator *widget = wgroup->widgets.first; widget; widget = widget->next) {
				if ((include_hidden || (widget->flag & WM_MANIPULATOR_HIDDEN) == 0) &&
				    (!poll || poll(widget, data)))
				{
					BLI_ghash_insert(hash, widget->idname, widget);
				}
			}
		}
	}

	return hash;
}

void WM_manipulatormap_tag_refresh(wmManipulatorMap *wmap)
{
	if (wmap) {
		wmap->update_flag |= WIDGETMAP_REFRESH;
	}
}

void WM_manipulatormap_widgets_update(const bContext *C, wmManipulatorMap *wmap)
{
	if (!wmap || BLI_listbase_is_empty(&wmap->widgetgroups))
		return;

	/* only active widget needs updating */
	if (wmap->wmap_context.active_widget) {
		WM_manipulator_calculate_scale(wmap->wmap_context.active_widget, C);
		goto done;
	}

	for (wmManipulatorGroup *wgroup = wmap->widgetgroups.first; wgroup; wgroup = wgroup->next) {
		if ((wgroup->type->flag & WM_MANIPULATORGROUPTYPE_OP && !wgroup->type->op) || /* only while operator runs */
		    (wgroup->type->poll && !wgroup->type->poll(C, wgroup->type)))
			continue;

		/* prepare for first draw */
		if (UNLIKELY((wgroup->flag & WM_MANIPULATORGROUP_INITIALIZED) == 0)) {
			wgroup->type->init(C, wgroup);
			wgroup->flag |= WM_MANIPULATORGROUP_INITIALIZED;
		}
		/* update data if needed */
		if (wmap->update_flag & WIDGETMAP_REFRESH && wgroup->type->refresh) {
			wgroup->type->refresh(C, wgroup);
		}
		/* prepare drawing */
		if (wgroup->type->draw_prepare) {
			wgroup->type->draw_prepare(C, wgroup);
		}

		for (wmManipulator *widget = wgroup->widgets.first; widget; widget = widget->next) {
			if (widget->flag & WM_MANIPULATOR_HIDDEN)
				continue;
			if (wmap->update_flag & WIDGETMAP_REFRESH) {
				WM_manipulator_update_prop_data(widget);
			}
			WM_manipulator_calculate_scale(widget, C);
			BLI_addhead(&draw_widgets, BLI_genericNodeN(widget));
		}
	}


done:
	/* done updating */
	wmap->update_flag = 0;
}

/**
 * Draw all visible widgets in \a wmap.
 * Uses global draw_widgets listbase.
 *
 * \param in_scene  draw depth-culled widgets (wmManipulator->flag WM_MANIPULATOR_SCENE_DEPTH) - TODO
 * \param free_drawwidgets  free global draw_widgets listbase (always enable for last draw call in region!).
 */
void WM_manipulatormap_widgets_draw(
        const bContext *C, const wmManipulatorMap *wmap,
        const bool in_scene, const bool free_drawwidgets)
{
	BLI_assert(!BLI_listbase_is_empty(&wmap->widgetgroups));
	if (!wmap)
		return;

	const bool draw_multisample = (U.ogl_multisamples != USER_MULTISAMPLE_NONE);
	const bool use_lighting = (U.widget_flag & V3D_SHADED_WIDGETS) != 0;

	/* enable multisampling */
	if (draw_multisample) {
		glEnable(GL_MULTISAMPLE);
	}
	if (use_lighting) {
		const float lightpos[4] = {0.0, 0.0, 1.0, 0.0};
		const float diffuse[4] = {1.0, 1.0, 1.0, 0.0};

		glPushAttrib(GL_LIGHTING_BIT | GL_ENABLE_BIT);

		glEnable(GL_LIGHTING);
		glEnable(GL_LIGHT0);
		glEnable(GL_COLOR_MATERIAL);
		glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);
		glPushMatrix();
		glLoadIdentity();
		glLightfv(GL_LIGHT0, GL_POSITION, lightpos);
		glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
		glPopMatrix();
	}


	wmManipulator *widget = wmap->wmap_context.active_widget;

	/* draw active widget */
	if (widget && in_scene == (widget->flag & WM_MANIPULATOR_SCENE_DEPTH)) {
		if (widget->flag & (WM_MANIPULATOR_DRAW_ACTIVE | WM_MANIPULATOR_DRAW_VALUE)) {
			/* notice that we don't update the widgetgroup, widget is now on
			 * its own, it should have all relevant data to update itself */
			widget->draw(C, widget);
		}
	}

	/* draw selected widgets */
	if (wmap->wmap_context.selected_widgets) {
		for (int i = 0; i < wmap->wmap_context.tot_selected; i++) {
			widget = wmap->wmap_context.selected_widgets[i];
			if ((widget != NULL) &&
			    (widget->flag & WM_MANIPULATOR_HIDDEN) == 0 &&
			    (in_scene == (widget->flag & WM_MANIPULATOR_SCENE_DEPTH)))
			{
				/* notice that we don't update the widgetgroup, widget is now on
				 * its own, it should have all relevant data to update itself */
				widget->draw(C, widget);
			}
		}
	}

	/* draw other widgets */
	if (!wmap->wmap_context.active_widget) {
		/* draw_widgets excludes hidden widgets */
		for (LinkData *link = draw_widgets.first; link; link = link->next) {
			widget = link->data;
			if ((in_scene == (widget->flag & WM_MANIPULATOR_SCENE_DEPTH)) &&
				((widget->flag & WM_MANIPULATOR_SELECTED) == 0) && /* selected were drawn already */
				((widget->flag & WM_MANIPULATOR_DRAW_HOVER) == 0 || (widget->flag & WM_MANIPULATOR_HIGHLIGHT)))
			{
				widget->draw(C, widget);
			}
		}
	}


	if (draw_multisample)
		glDisable(GL_MULTISAMPLE);
	if (use_lighting)
		glPopAttrib();

	if (free_drawwidgets) {
		BLI_freelistN(&draw_widgets);
	}
}

static void manipulator_find_active_3D_loop(const bContext *C, ListBase *visible_widgets)
{
	int selectionbase = 0;
	wmManipulator *widget;

	for (LinkData *link = visible_widgets->first; link; link = link->next) {
		widget = link->data;
		/* pass the selection id shifted by 8 bits. Last 8 bits are used for selected widget part id */
		widget->render_3d_intersection(C, widget, selectionbase << 8);

		selectionbase++;
	}
}

static int manipulator_find_highlighted_3D_intern(
        ListBase *visible_widgets, const bContext *C, const wmEvent *event, const float hotspot)
{
	ScrArea *sa = CTX_wm_area(C);
	ARegion *ar = CTX_wm_region(C);
	View3D *v3d = sa->spacedata.first;
	RegionView3D *rv3d = ar->regiondata;
	rctf rect, selrect;
	GLuint buffer[64];      // max 4 items per select, so large enuf
	short hits;
	const bool do_passes = GPU_select_query_check_active();

	extern void view3d_winmatrix_set(ARegion *ar, View3D *v3d, rctf *rect);


	rect.xmin = event->mval[0] - hotspot;
	rect.xmax = event->mval[0] + hotspot;
	rect.ymin = event->mval[1] - hotspot;
	rect.ymax = event->mval[1] + hotspot;

	selrect = rect;

	view3d_winmatrix_set(ar, v3d, &rect);
	mul_m4_m4m4(rv3d->persmat, rv3d->winmat, rv3d->viewmat);

	if (do_passes)
		GPU_select_begin(buffer, ARRAY_SIZE(buffer), &selrect, GPU_SELECT_NEAREST_FIRST_PASS, 0);
	else
		GPU_select_begin(buffer, ARRAY_SIZE(buffer), &selrect, GPU_SELECT_ALL, 0);
	/* do the drawing */
	manipulator_find_active_3D_loop(C, visible_widgets);

	hits = GPU_select_end();

	if (do_passes) {
		GPU_select_begin(buffer, ARRAY_SIZE(buffer), &selrect, GPU_SELECT_NEAREST_SECOND_PASS, hits);
		manipulator_find_active_3D_loop(C, visible_widgets);
		GPU_select_end();
	}

	view3d_winmatrix_set(ar, v3d, NULL);
	mul_m4_m4m4(rv3d->persmat, rv3d->winmat, rv3d->viewmat);

	return hits > 0 ? buffer[3] : -1;
}

static void widgets_prepare_visible_3D(wmManipulatorMap *wmap, ListBase *visible_widgets, bContext *C)
{
	wmManipulator *widget;

	for (wmManipulatorGroup *wgroup = wmap->widgetgroups.first; wgroup; wgroup = wgroup->next) {
		if (!wgroup->type->poll || wgroup->type->poll(C, wgroup->type)) {
			for (widget = wgroup->widgets.first; widget; widget = widget->next) {
				if (widget->render_3d_intersection && (widget->flag & WM_MANIPULATOR_HIDDEN) == 0) {
					BLI_addhead(visible_widgets, BLI_genericNodeN(widget));
				}
			}
		}
	}
}

wmManipulator *WM_manipulatormap_find_highlighted_3D(wmManipulatorMap *wmap, bContext *C, const wmEvent *event, unsigned char *part)
{
	wmManipulator *result = NULL;
	ListBase visible_widgets = {0};
	const float hotspot = 14.0f;
	int ret;

	widgets_prepare_visible_3D(wmap, &visible_widgets, C);

	*part = 0;
	/* set up view matrices */
	view3d_operator_needs_opengl(C);

	ret = manipulator_find_highlighted_3D_intern(&visible_widgets, C, event, 0.5f * hotspot);

	if (ret != -1) {
		LinkData *link;
		int retsec;
		retsec = manipulator_find_highlighted_3D_intern(&visible_widgets, C, event, 0.2f * hotspot);

		if (retsec != -1)
			ret = retsec;

		link = BLI_findlink(&visible_widgets, ret >> 8);
		*part = ret & 255;
		result = link->data;
	}

	BLI_freelistN(&visible_widgets);

	return result;
}

void WM_manipulatormaps_add_handlers(ARegion *ar)
{
	for (wmManipulatorMap *wmap = ar->widgetmaps.first; wmap; wmap = wmap->next) {
		wmEventHandler *handler = MEM_callocN(sizeof(wmEventHandler), "widget handler");

		handler->widgetmap = wmap;
		BLI_addtail(&ar->handlers, handler);
	}

}

void WM_manipulatormaps_handled_modal_update(
        bContext *C, wmEvent *event, wmEventHandler *handler,
        const wmOperatorType *ot)
{
	const bool modal_running = (handler->op != NULL);

	/* happens on render */
	if (!handler->op_region)
		return;

	/* hide operator widgets */
	if (!modal_running && ot->wgrouptype) {
		ot->wgrouptype->op = NULL;
	}

	for (wmManipulatorMap *wmap = handler->op_region->widgetmaps.first; wmap; wmap = wmap->next) {
		wmManipulator *widget = WM_manipulatormap_get_active_widget(wmap);
		ScrArea *area = CTX_wm_area(C);
		ARegion *region = CTX_wm_region(C);

		WM_manipulatormap_handler_context(C, handler);

		/* regular update for running operator */
		if (modal_running) {
			if (widget && widget->handler && widget->opname && STREQ(widget->opname, handler->op->idname)) {
				widget->handler(C, event, widget, 0);
			}
		}
		/* operator not running anymore */
		else {
			WM_manipulatormap_set_highlighted_widget(wmap, C, NULL, 0);
			WM_manipulatormap_set_active_widget(wmap, C, event, NULL);
		}

		/* restore the area */
		CTX_wm_area_set(C, area);
		CTX_wm_region_set(C, region);
	}
}

/**
 * Deselect all selected widgets in \a wmap.
 * \return if selection has changed.
 */
bool WM_manipulatormap_deselect_all(wmManipulatorMap *wmap, wmManipulator ***sel)
{
	if (*sel == NULL || wmap->wmap_context.tot_selected == 0)
		return false;

	for (int i = 0; i < wmap->wmap_context.tot_selected; i++) {
		(*sel)[i]->flag &= ~WM_MANIPULATOR_SELECTED;
		(*sel)[i] = NULL;
	}
	WM_manipulatormap_selected_delete(wmap);

	/* always return true, we already checked
	 * if there's anything to deselect */
	return true;
}

BLI_INLINE bool manipulator_selectable_poll(const wmManipulator *widget, void *UNUSED(data))
{
	return (widget->flag & WM_MANIPULATOR_SELECTABLE);
}

/**
 * Select all selectable widgets in \a wmap.
 * \return if selection has changed.
 */
static bool WM_manipulatormap_select_all_intern(bContext *C, wmManipulatorMap *wmap, wmManipulator ***sel, const int action)
{
	/* GHash is used here to avoid having to loop over all widgets twice (once to
	 * get tot_sel for allocating, once for actually selecting). Instead we collect
	 * selectable widgets in hash table and use this to get tot_sel and do selection */

	GHash *hash = WM_manipulatormap_manipulator_hash_new(C, wmap, manipulator_selectable_poll, NULL, true);
	GHashIterator gh_iter;
	int i, *tot_sel = &wmap->wmap_context.tot_selected;
	bool changed = false;

	*tot_sel = BLI_ghash_size(hash);
	*sel = MEM_reallocN(*sel, sizeof(**sel) * (*tot_sel));

	GHASH_ITER_INDEX (gh_iter, hash, i) {
		wmManipulator *widget_iter = BLI_ghashIterator_getValue(&gh_iter);

		if ((widget_iter->flag & WM_MANIPULATOR_SELECTED) == 0) {
			changed = true;
		}
		widget_iter->flag |= WM_MANIPULATOR_SELECTED;
		if (widget_iter->select) {
			widget_iter->select(C, widget_iter, action);
		}
		(*sel)[i] = widget_iter;
		BLI_assert(i < (*tot_sel));
	}
	/* highlight first widget */
	WM_manipulatormap_set_highlighted_widget(wmap, C, (*sel)[0], (*sel)[0]->highlighted_part);

	BLI_ghash_free(hash, NULL, NULL);
	return changed;
}

/**
 * Select/Deselect all selectable widgets in \a wmap.
 * \return if selection has changed.
 *
 * TODO select all by type
 */
bool WM_manipulatormap_select_all(bContext *C, wmManipulatorMap *wmap, const int action)
{
	wmManipulator ***sel = &wmap->wmap_context.selected_widgets;
	bool changed = false;

	switch (action) {
		case SEL_SELECT:
			changed = WM_manipulatormap_select_all_intern(C, wmap, sel, action);
			break;
		case SEL_DESELECT:
			changed = WM_manipulatormap_deselect_all(wmap, sel);
			break;
		default:
			BLI_assert(0);
			break;
	}

	if (changed)
		WM_event_add_mousemove(C);

	return changed;
}

bool WM_manipulatormap_is_3d(const wmManipulatorMap *wmap)
{
	return (wmap->type->flag & WM_MANIPULATORMAPTYPE_3D) != 0;
}

void WM_manipulatormap_handler_context(bContext *C, wmEventHandler *handler)
{
	bScreen *screen = CTX_wm_screen(C);

	if (screen) {
		if (handler->op_area == NULL) {
			/* do nothing in this context */
		}
		else {
			ScrArea *sa;

			for (sa = screen->areabase.first; sa; sa = sa->next)
				if (sa == handler->op_area)
					break;
			if (sa == NULL) {
				/* when changing screen layouts with running modal handlers (like render display), this
				 * is not an error to print */
				if (handler->widgetmap == NULL)
					printf("internal error: modal widgetmap handler has invalid area\n");
			}
			else {
				ARegion *ar;
				CTX_wm_area_set(C, sa);
				for (ar = sa->regionbase.first; ar; ar = ar->next)
					if (ar == handler->op_region)
						break;
				/* XXX no warning print here, after full-area and back regions are remade */
				if (ar)
					CTX_wm_region_set(C, ar);
			}
		}
	}
}


wmManipulator *WM_manipulatormap_find_highlighted_widget(wmManipulatorMap *wmap, bContext *C, const wmEvent *event, unsigned char *part)
{
	wmManipulator *widget;

	for (wmManipulatorGroup *wgroup = wmap->widgetgroups.first; wgroup; wgroup = wgroup->next) {
		if (!wgroup->type->poll || wgroup->type->poll(C, wgroup->type)) {
			for (widget = wgroup->widgets.first; widget; widget = widget->next) {
				if (widget->intersect) {
					if ((*part = widget->intersect(C, event, widget)))
						return widget;
				}
			}
		}
	}

	return NULL;
}

bool WM_manipulatormap_cursor_set(const wmManipulatorMap *wmap, wmWindow *win)
{
	for (; wmap; wmap = wmap->next) {
		wmManipulator *widget = wmap->wmap_context.highlighted_widget;
		if (widget && widget->get_cursor) {
			WM_cursor_set(win, widget->get_cursor(widget));
			return true;
		}
	}

	return false;
}

void WM_manipulatormap_set_highlighted_widget(wmManipulatorMap *wmap, const bContext *C, wmManipulator *widget, unsigned char part)
{
	if ((widget != wmap->wmap_context.highlighted_widget) || (widget && part != widget->highlighted_part)) {
		if (wmap->wmap_context.highlighted_widget) {
			wmap->wmap_context.highlighted_widget->flag &= ~WM_MANIPULATOR_HIGHLIGHT;
			wmap->wmap_context.highlighted_widget->highlighted_part = 0;
		}

		wmap->wmap_context.highlighted_widget = widget;

		if (widget) {
			widget->flag |= WM_MANIPULATOR_HIGHLIGHT;
			widget->highlighted_part = part;
			wmap->wmap_context.activegroup = widget->wgroup;

			if (C && widget->get_cursor) {
				wmWindow *win = CTX_wm_window(C);
				WM_cursor_set(win, widget->get_cursor(widget));
			}
		}
		else {
			wmap->wmap_context.activegroup = NULL;
			if (C) {
				wmWindow *win = CTX_wm_window(C);
				WM_cursor_set(win, CURSOR_STD);
			}
		}

		/* tag the region for redraw */
		if (C) {
			ARegion *ar = CTX_wm_region(C);
			ED_region_tag_redraw(ar);
		}
	}
}

wmManipulator *WM_manipulatormap_get_highlighted_widget(wmManipulatorMap *wmap)
{
	return wmap->wmap_context.highlighted_widget;
}

void WM_manipulatormap_set_active_widget(wmManipulatorMap *wmap, bContext *C, const wmEvent *event, wmManipulator *widget)
{
	if (widget && C) {
		widget->flag |= WM_MANIPULATOR_ACTIVE;
		wmap->wmap_context.active_widget = widget;

		if (widget->opname) {
			wmOperatorType *ot = WM_operatortype_find(widget->opname, 0);

			if (ot) {
				/* first activate the widget itself */
				if (widget->invoke && widget->handler) {
					widget->invoke(C, event, widget);
				}

				WM_operator_name_call_ptr(C, ot, WM_OP_INVOKE_DEFAULT, &widget->opptr);

				/* we failed to hook the widget to the operator handler or operator was cancelled, return */
				if (!wmap->wmap_context.active_widget) {
					widget->flag &= ~WM_MANIPULATOR_ACTIVE;
					/* first activate the widget itself */
					if (widget->interaction_data) {
						MEM_freeN(widget->interaction_data);
						widget->interaction_data = NULL;
					}
				}
				return;
			}
			else {
				printf("Widget error: operator not found");
				wmap->wmap_context.active_widget = NULL;
				return;
			}
		}
		else {
			if (widget->invoke && widget->handler) {
				widget->invoke(C, event, widget);
			}
		}
		WM_cursor_grab_enable(CTX_wm_window(C), true, true, NULL);
	}
	else {
		widget = wmap->wmap_context.active_widget;


		/* deactivate, widget but first take care of some stuff */
		if (widget) {
			widget->flag &= ~WM_MANIPULATOR_ACTIVE;
			/* first activate the widget itself */
			if (widget->interaction_data) {
				MEM_freeN(widget->interaction_data);
				widget->interaction_data = NULL;
			}
		}
		wmap->wmap_context.active_widget = NULL;

		if (C) {
			WM_cursor_grab_disable(CTX_wm_window(C), NULL);
			ED_region_tag_redraw(CTX_wm_region(C));
			WM_event_add_mousemove(C);
		}
	}
}

wmManipulator *WM_manipulatormap_get_active_widget(wmManipulatorMap *wmap)
{
	return wmap->wmap_context.active_widget;
}

/** \} */ /* wmManipulatorMap */


/* -------------------------------------------------------------------- */
/** \name wmManipulatorMapType
 *
 * \{ */

wmManipulatorMapType *WM_manipulatormaptype_find(
        const struct wmManipulatorMapType_Params *wmap_params)
{
	wmManipulatorMapType *wmaptype;
	/* flags which differentiates widget groups */
	const int flag_cmp = WM_MANIPULATORMAPTYPE_3D;
	const int flag_test = wmap_params->flag & flag_cmp;

	for (wmaptype = widgetmaptypes.first; wmaptype; wmaptype = wmaptype->next) {
		if (wmaptype->spaceid == wmap_params->spaceid &&
		    wmaptype->regionid == wmap_params->regionid &&
		    ((wmaptype->flag & flag_cmp) == flag_test) &&
		    STREQ(wmaptype->idname, wmap_params->idname))
		{
			return wmaptype;
		}
	}

	return NULL;
}

wmManipulatorMapType *WM_manipulatormaptype_ensure(
        const struct wmManipulatorMapType_Params *wmap_params)
{
	wmManipulatorMapType *wmaptype = WM_manipulatormaptype_find(wmap_params);

	if (wmaptype) {
		return wmaptype;
	}

	wmaptype = MEM_callocN(sizeof(wmManipulatorMapType), "widgettype list");
	wmaptype->spaceid = wmap_params->spaceid;
	wmaptype->regionid = wmap_params->regionid;
	wmaptype->flag = wmap_params->flag;
	BLI_strncpy(wmaptype->idname, wmap_params->idname, sizeof(wmaptype->idname));
	BLI_addhead(&widgetmaptypes, wmaptype);

	return wmaptype;
}

void WM_manipulatormaptypes_free(void)
{
	for (wmManipulatorMapType *wmaptype = widgetmaptypes.first; wmaptype; wmaptype = wmaptype->next) {
		BLI_freelistN(&wmaptype->widgetgrouptypes);
	}
	BLI_freelistN(&widgetmaptypes);
}

/**
 * Initialize keymaps for all existing widget-groups
 */
void WM_manipulators_keymap(wmKeyConfig *keyconf)
{
	wmManipulatorMapType *wmaptype;
	wmManipulatorGroupType *wgrouptype;

	/* we add this item-less keymap once and use it to group widgetgroup keymaps into it */
	WM_keymap_find(keyconf, "Widgets", 0, 0);

	for (wmaptype = widgetmaptypes.first; wmaptype; wmaptype = wmaptype->next) {
		for (wgrouptype = wmaptype->widgetgrouptypes.first; wgrouptype; wgrouptype = wgrouptype->next) {
			WM_manipulatorgrouptype_keymap_init(wgrouptype, keyconf);
		}
	}
}

/** \} */ /* wmManipulatorMapType */

