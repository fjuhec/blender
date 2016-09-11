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

/** \file blender/windowmanager/manipulators/intern/wm_manipulatorgroup.c
 *  \ingroup wm
 *
 * \name Widget Group
 *
 * WidgetGroups store and manage groups of widgets. They can be
 * attached to modal handlers and have on keymaps.
 */

#include "BKE_context.h"
#include "BKE_main.h"
#include "BKE_report.h"

#include "BLI_listbase.h"
#include "BLI_string.h"

#include "BPY_extern.h"

#include "ED_screen.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"

#include "WM_api.h"
#include "WM_types.h"
#include "wm_event_system.h"

/* own includes */
#include "wm_manipulator_wmapi.h"
#include "wm_manipulator_intern.h"


/* -------------------------------------------------------------------- */
/** \name wmManipulatorGroup
 *
 * \{ */

void WM_manipulatorgroup_free(bContext *C, wmManipulatorMap *wmap, wmManipulatorGroup *wgroup)
{
	for (wmManipulator *widget = wgroup->widgets.first; widget;) {
		wmManipulator *widget_next = widget->next;
		WM_manipulator_delete(&wgroup->widgets, wmap, widget, C);
		widget = widget_next;
	}
	BLI_assert(BLI_listbase_is_empty(&wgroup->widgets));

#ifdef WITH_PYTHON
	if (wgroup->py_instance) {
		/* do this first in case there are any __del__ functions or
		 * similar that use properties */
		BPY_DECREF_RNA_INVALIDATE(wgroup->py_instance);
	}
#endif

	if (wgroup->reports && (wgroup->reports->flag & RPT_FREE)) {
		BKE_reports_clear(wgroup->reports);
		MEM_freeN(wgroup->reports);
	}

	if (wgroup->customdata_free) {
		wgroup->customdata_free(wgroup->customdata);
	}
	else {
		MEM_SAFE_FREE(wgroup->customdata);
	}

	BLI_remlink(&wmap->widgetgroups, wgroup);
	MEM_freeN(wgroup);
}

void WM_manipulatorgroup_attach_to_modal_handler(bContext *C, wmEventHandler *handler,
                                            wmManipulatorGroupType *wgrouptype, wmOperator *op)
{
	/* maybe overly careful, but widgetgrouptype could come from a failed creation */
	if (!wgrouptype) {
		return;
	}

	/* now instantiate the widgetmap */
	wgrouptype->op = op;

	if (handler->op_region && !BLI_listbase_is_empty(&handler->op_region->widgetmaps)) {
		for (wmManipulatorMap *wmap = handler->op_region->widgetmaps.first; wmap; wmap = wmap->next) {
			wmManipulatorMapType *wmaptype = wmap->type;

			if (wmaptype->spaceid == wgrouptype->spaceid && wmaptype->regionid == wgrouptype->regionid) {
				handler->widgetmap = wmap;
			}
		}

		ED_region_tag_redraw(handler->op_region);
	}

	WM_event_add_mousemove(C);
}

/** \name Manipulator operators
 *
 * Basic operators for manipulator interaction with user configurable keymaps.
 *
 * \{ */

static int manipulator_select_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	ARegion *ar = CTX_wm_region(C);

	bool extend = RNA_boolean_get(op->ptr, "extend");
	bool deselect = RNA_boolean_get(op->ptr, "deselect");
	bool toggle = RNA_boolean_get(op->ptr, "toggle");


	for (wmManipulatorMap *wmap = ar->widgetmaps.first; wmap; wmap = wmap->next) {
		wmManipulator ***sel = &wmap->wmap_context.selected_widgets;
		wmManipulator *highlighted = wmap->wmap_context.highlighted_widget;

		/* deselect all first */
		if (extend == false && deselect == false && toggle == false) {
			WM_manipulatormap_deselect_all(wmap, sel);
			BLI_assert(*sel == NULL && wmap->wmap_context.tot_selected == 0);
		}

		if (highlighted) {
			const bool is_selected = (highlighted->flag & WM_MANIPULATOR_SELECTED);
			bool redraw = false;

			if (toggle) {
				/* toggle: deselect if already selected, else select */
				deselect = is_selected;
			}

			if (deselect) {
				if (is_selected && WM_manipulator_deselect(wmap, highlighted)) {
					redraw = true;
				}
			}
			else if (WM_manipulator_select(C, wmap, highlighted)) {
				redraw = true;
			}

			if (redraw) {
				ED_region_tag_redraw(ar);
			}

			return OPERATOR_FINISHED;
		}
		else {
			BLI_assert(0);
			return (OPERATOR_CANCELLED | OPERATOR_PASS_THROUGH);
		}
	}

	return OPERATOR_PASS_THROUGH;
}

void WIDGETGROUP_OT_widget_select(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Widget Select";
	ot->description = "Select the currently highlighted widget";
	ot->idname = "WIDGETGROUP_OT_widget_select";

	/* api callbacks */
	ot->invoke = manipulator_select_invoke;

	ot->flag = OPTYPE_UNDO;

	WM_operator_properties_mouse_select(ot);
}

typedef struct WidgetTweakData {
	wmManipulatorMap *wmap;
	wmManipulator *active;

	int init_event; /* initial event type */
	int flag;       /* tweak flags */
} WidgetTweakData;

static void manipulator_tweak_finish(bContext *C, wmOperator *op, const bool cancel)
{
	WidgetTweakData *wtweak = op->customdata;
	if (wtweak->active->exit) {
		wtweak->active->exit(C, wtweak->active, cancel);
	}
	WM_manipulatormap_set_active_widget(wtweak->wmap, C, NULL, NULL);
	MEM_freeN(wtweak);
}

static int manipulator_tweak_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
	WidgetTweakData *wtweak = op->customdata;
	wmManipulator *widget = wtweak->active;

	if (!widget) {
		BLI_assert(0);
		return (OPERATOR_CANCELLED | OPERATOR_PASS_THROUGH);
	}

	if (event->type == wtweak->init_event && event->val == KM_RELEASE) {
		manipulator_tweak_finish(C, op, false);
		return OPERATOR_FINISHED;
	}


	if (event->type == EVT_MODAL_MAP) {
		switch (event->val) {
			case TWEAK_MODAL_CANCEL:
				manipulator_tweak_finish(C, op, true);
				return OPERATOR_CANCELLED;
			case TWEAK_MODAL_CONFIRM:
				manipulator_tweak_finish(C, op, false);
				return OPERATOR_FINISHED;
			case TWEAK_MODAL_PRECISION_ON:
				wtweak->flag |= WM_MANIPULATOR_TWEAK_PRECISE;
				break;
			case TWEAK_MODAL_PRECISION_OFF:
				wtweak->flag &= ~WM_MANIPULATOR_TWEAK_PRECISE;
				break;
		}
	}

	/* handle widget */
	if (widget->handler) {
		widget->handler(C, event, widget, wtweak->flag);
	}

	/* Ugly hack to send widget events */
	((wmEvent *)event)->type = EVT_WIDGET_UPDATE;

	/* always return PASS_THROUGH so modal handlers
	 * with widgets attached can update */
	return OPERATOR_PASS_THROUGH;
}

static int manipulator_tweak_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	ARegion *ar = CTX_wm_region(C);
	wmManipulatorMap *wmap;
	wmManipulator *widget;

	for (wmap = ar->widgetmaps.first; wmap; wmap = wmap->next)
		if ((widget = wmap->wmap_context.highlighted_widget))
			break;

	if (!widget) {
		/* wm_handlers_do_intern shouldn't let this happen */
		BLI_assert(0);
		return (OPERATOR_CANCELLED | OPERATOR_PASS_THROUGH);
	}


	/* activate highlighted widget */
	WM_manipulatormap_set_active_widget(wmap, C, event, widget);

	/* XXX temporary workaround for modal widget operator
	 * conflicting with modal operator attached to widget */
	if (widget->opname) {
		wmOperatorType *ot = WM_operatortype_find(widget->opname, true);
		if (ot->modal) {
			return OPERATOR_FINISHED;
		}
	}


	WidgetTweakData *wtweak = MEM_mallocN(sizeof(WidgetTweakData), __func__);

	wtweak->init_event = event->type;
	wtweak->active = wmap->wmap_context.highlighted_widget;
	wtweak->wmap = wmap;
	wtweak->flag = 0;

	op->customdata = wtweak;

	WM_event_add_modal_handler(C, op);

	return OPERATOR_RUNNING_MODAL;
}

void WIDGETGROUP_OT_widget_tweak(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Widget Tweak";
	ot->description = "Tweak the active widget";
	ot->idname = "WIDGETGROUP_OT_widget_tweak";

	/* api callbacks */
	ot->invoke = manipulator_tweak_invoke;
	ot->modal = manipulator_tweak_modal;

	ot->flag = OPTYPE_UNDO;
}

/** \} */ // Widget operators


static wmKeyMap *widgetgroup_tweak_modal_keymap(wmKeyConfig *keyconf, const char *wgroupname)
{
	wmKeyMap *keymap;
	char name[KMAP_MAX_NAME];

	static EnumPropertyItem modal_items[] = {
		{TWEAK_MODAL_CANCEL, "CANCEL", 0, "Cancel", ""},
		{TWEAK_MODAL_CONFIRM, "CONFIRM", 0, "Confirm", ""},
		{TWEAK_MODAL_PRECISION_ON, "PRECISION_ON", 0, "Enable Precision", ""},
		{TWEAK_MODAL_PRECISION_OFF, "PRECISION_OFF", 0, "Disable Precision", ""},
		{0, NULL, 0, NULL, NULL}
	};


	BLI_snprintf(name, sizeof(name), "%s Tweak Modal Map", wgroupname);
	keymap = WM_modalkeymap_get(keyconf, name);

	/* this function is called for each spacetype, only needs to add map once */
	if (keymap && keymap->modal_items)
		return NULL;

	keymap = WM_modalkeymap_add(keyconf, name, modal_items);


	/* items for modal map */
	WM_modalkeymap_add_item(keymap, ESCKEY, KM_PRESS, KM_ANY, 0, TWEAK_MODAL_CANCEL);
	WM_modalkeymap_add_item(keymap, RIGHTMOUSE, KM_PRESS, KM_ANY, 0, TWEAK_MODAL_CANCEL);

	WM_modalkeymap_add_item(keymap, RETKEY, KM_PRESS, KM_ANY, 0, TWEAK_MODAL_CONFIRM);
	WM_modalkeymap_add_item(keymap, PADENTER, KM_PRESS, KM_ANY, 0, TWEAK_MODAL_CONFIRM);

	WM_modalkeymap_add_item(keymap, RIGHTSHIFTKEY, KM_PRESS, KM_ANY, 0, TWEAK_MODAL_PRECISION_ON);
	WM_modalkeymap_add_item(keymap, RIGHTSHIFTKEY, KM_RELEASE, KM_ANY, 0, TWEAK_MODAL_PRECISION_OFF);
	WM_modalkeymap_add_item(keymap, LEFTSHIFTKEY, KM_PRESS, KM_ANY, 0, TWEAK_MODAL_PRECISION_ON);
	WM_modalkeymap_add_item(keymap, LEFTSHIFTKEY, KM_RELEASE, KM_ANY, 0, TWEAK_MODAL_PRECISION_OFF);


	WM_modalkeymap_assign(keymap, "WIDGETGROUP_OT_widget_tweak");

	return keymap;
}

/**
 * Common default keymap for widget groups
 */
wmKeyMap *WM_manipulatorgroup_keymap_common(const struct wmManipulatorGroupType *wgrouptype, wmKeyConfig *config)
{
	/* Use area and region id since we might have multiple widgets with the same name in different areas/regions */
	wmKeyMap *km = WM_keymap_find(config, wgrouptype->name, wgrouptype->spaceid, wgrouptype->regionid);

	WM_keymap_add_item(km, "WIDGETGROUP_OT_widget_tweak", ACTIONMOUSE, KM_PRESS, KM_ANY, 0);
	widgetgroup_tweak_modal_keymap(config, wgrouptype->name);

	return km;
}

/**
 * Variation of #WM_manipulatorgroup_keymap_common but with keymap items for selection
 */
wmKeyMap *WM_manipulatorgroup_keymap_common_sel(const struct wmManipulatorGroupType *wgrouptype, wmKeyConfig *config)
{
	/* Use area and region id since we might have multiple widgets with the same name in different areas/regions */
	wmKeyMap *km = WM_keymap_find(config, wgrouptype->name, wgrouptype->spaceid, wgrouptype->regionid);

	WM_keymap_add_item(km, "WIDGETGROUP_OT_widget_tweak", ACTIONMOUSE, KM_PRESS, KM_ANY, 0);
	widgetgroup_tweak_modal_keymap(config, wgrouptype->name);

	wmKeyMapItem *kmi = WM_keymap_add_item(km, "WIDGETGROUP_OT_widget_select", SELECTMOUSE, KM_PRESS, 0, 0);
	RNA_boolean_set(kmi->ptr, "extend", false);
	RNA_boolean_set(kmi->ptr, "deselect", false);
	RNA_boolean_set(kmi->ptr, "toggle", false);
	kmi = WM_keymap_add_item(km, "WIDGETGROUP_OT_widget_select", SELECTMOUSE, KM_PRESS, KM_SHIFT, 0);
	RNA_boolean_set(kmi->ptr, "extend", false);
	RNA_boolean_set(kmi->ptr, "deselect", false);
	RNA_boolean_set(kmi->ptr, "toggle", true);

	return km;
}

/** \} */ /* wmManipulatorGroup */

/* -------------------------------------------------------------------- */
/** \name wmManipulatorGroupType
 *
 * \{ */

/**
 * Use this for registering widgets on startup. For runtime, use #WM_manipulatorgrouptype_append_runtime.
 */
wmManipulatorGroupType *WM_manipulatorgrouptype_append(wmManipulatorMapType *wmaptype, void (*wgrouptype_func)(wmManipulatorGroupType *))
{
	wmManipulatorGroupType *wgrouptype = MEM_callocN(sizeof(wmManipulatorGroupType), "widgetgroup");

	wgrouptype_func(wgrouptype);
	wgrouptype->spaceid = wmaptype->spaceid;
	wgrouptype->regionid = wmaptype->regionid;
	wgrouptype->flag = wmaptype->flag;
	BLI_strncpy(wgrouptype->mapidname, wmaptype->idname, MAX_NAME);
	/* if not set, use default */
	if (!wgrouptype->keymap_init) {
		wgrouptype->keymap_init = WM_manipulatorgroup_keymap_common;
	}

	/* add the type for future created areas of the same type  */
	BLI_addtail(&wmaptype->widgetgrouptypes, wgrouptype);
	return wgrouptype;
}

/**
 * Use this for registering widgets on runtime.
 */
wmManipulatorGroupType *WM_manipulatorgrouptype_append_runtime(
        const Main *main, wmManipulatorMapType *wmaptype,
        void (*wgrouptype_func)(wmManipulatorGroupType *))
{
	wmManipulatorGroupType *wgrouptype = WM_manipulatorgrouptype_append(wmaptype, wgrouptype_func);

	/* Main is missing on startup when we create new areas.
	 * So this is only called for widgets initialized on runtime */
	WM_manipulatorgrouptype_init_runtime(main, wmaptype, wgrouptype);

	return wgrouptype;
}

void WM_manipulatorgrouptype_init_runtime(
        const Main *bmain, wmManipulatorMapType *wmaptype,
        wmManipulatorGroupType *wgrouptype)
{
	/* init keymap - on startup there's an extra call to init keymaps for 'permanent' widget-groups */
	WM_manipulatorgrouptype_keymap_init(wgrouptype, ((wmWindowManager *)bmain->wm.first)->defaultconf);

	/* now create a widget for all existing areas */
	for (bScreen *sc = bmain->screen.first; sc; sc = sc->id.next) {
		for (ScrArea *sa = sc->areabase.first; sa; sa = sa->next) {
			for (SpaceLink *sl = sa->spacedata.first; sl; sl = sl->next) {
				ListBase *lb = (sl == sa->spacedata.first) ? &sa->regionbase : &sl->regionbase;
				for (ARegion *ar = lb->first; ar; ar = ar->next) {
					for (wmManipulatorMap *wmap = ar->widgetmaps.first; wmap; wmap = wmap->next) {
						if (wmap->type == wmaptype) {
							wmManipulatorGroup *wgroup = MEM_callocN(sizeof(wmManipulatorGroup), "widgetgroup");

							wgroup->type = wgrouptype;

							/* just add here, drawing will occur on next update */
							BLI_addtail(&wmap->widgetgroups, wgroup);
							WM_manipulatormap_set_highlighted_widget(wmap, NULL, NULL, 0);
							ED_region_tag_redraw(ar);
						}
					}
				}
			}
		}
	}
}

void WM_manipulatorgrouptype_unregister(bContext *C, Main *bmain, wmManipulatorGroupType *wgrouptype)
{
	for (bScreen *sc = bmain->screen.first; sc; sc = sc->id.next) {
		for (ScrArea *sa = sc->areabase.first; sa; sa = sa->next) {
			for (SpaceLink *sl = sa->spacedata.first; sl; sl = sl->next) {
				ListBase *lb = (sl == sa->spacedata.first) ? &sa->regionbase : &sl->regionbase;
				for (ARegion *ar = lb->first; ar; ar = ar->next) {
					for (wmManipulatorMap *wmap = ar->widgetmaps.first; wmap; wmap = wmap->next) {
						wmManipulatorGroup *wgroup, *wgroup_next;

						for (wgroup = wmap->widgetgroups.first; wgroup; wgroup = wgroup_next) {
							wgroup_next = wgroup->next;
							if (wgroup->type == wgrouptype) {
								WM_manipulatorgroup_free(C, wmap, wgroup);
								ED_region_tag_redraw(ar);
							}
						}
					}
				}
			}
		}
	}

	wmManipulatorMapType *wmaptype = WM_manipulatormaptype_find(&(const struct wmManipulatorMapType_Params) {
	        wgrouptype->mapidname, wgrouptype->spaceid,
	        wgrouptype->regionid, wgrouptype->flag});

	BLI_remlink(&wmaptype->widgetgrouptypes, wgrouptype);
	wgrouptype->prev = wgrouptype->next = NULL;

	MEM_freeN(wgrouptype);
}

void WM_manipulatorgrouptype_keymap_init(wmManipulatorGroupType *wgrouptype, wmKeyConfig *keyconf)
{
	wgrouptype->keymap = wgrouptype->keymap_init(wgrouptype, keyconf);
}

/** \} */ /* wmManipulatorGroupType */
