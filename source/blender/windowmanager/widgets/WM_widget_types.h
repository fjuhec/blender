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
 * The Original Code is Copyright (C) 2016 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/widgets/WM_widget_types.h
 *  \ingroup wm
 *
 * \name Widget Types
 * \brief Widget defines for external use.
 *
 * Only included in WM_types.h and lower level files.
 */


#ifndef __WM_WIDGET_TYPES_H__
#define __WM_WIDGET_TYPES_H__

#include "BLI_compiler_attrs.h"

struct wmWidgetGroupType;
struct wmWidgetGroup;
struct wmKeyConfig;

typedef int (*wmWidgetGroupPollFunc)(const struct bContext *, struct wmWidgetGroupType *) ATTR_WARN_UNUSED_RESULT; /* TODO use bool */
typedef void (*wmWidgetGroupInitFunc)(const struct bContext *, struct wmWidgetGroup *);


/* -------------------------------------------------------------------- */

/* factory class for a widgetgroup type, gets called every time a new area is spawned */
typedef struct wmWidgetGroupType {
	struct wmWidgetGroupType *next, *prev;

	char idname[64]; /* MAX_NAME */
	char name[64]; /* widget group name - displayed in UI (keymap editor) */

	/* poll if widgetmap should be active */
	wmWidgetGroupPollFunc poll;

	/* initially create widgets, set permanent data stuff you only need to do once */
	wmWidgetGroupInitFunc init;
	/* refresh data, only called if recreate flag is set (WM_widgetmap_tag_refresh) */
	void (*refresh)(const struct bContext *C, struct wmWidgetGroup *wgroup);
	/* refresh data for drawing, called before each redraw */
	void (*draw_prepare)(const struct bContext *C, struct wmWidgetGroup *wgroup);

	/* keymap init callback for this widgetgroup */
	struct wmKeyMap *(*keymap_init)(const struct wmWidgetGroupType *, struct wmKeyConfig *);
	/* keymap created with callback from above */
	struct wmKeyMap *keymap;

	/* rna for properties */
	struct StructRNA *srna;

	/* RNA integration */
	ExtensionRNA ext;

	/* widgetTypeflags (includes copy of wmWidgetMapType.flag - used for comparisons) */
	int flag;

	/* if type is spawned from operator this is set here */
	void *op;

	/* same as widgetmaps, so registering/unregistering goes to the correct region */
	short spaceid, regionid;
	char mapidname[64];
} wmWidgetGroupType;


typedef struct wmWidgetMap {
	struct wmWidgetMap *next, *prev;

	struct wmWidgetMapType *type;
	ListBase widgetgroups;

	short update_flag; /* private, update tagging */

	/**
	 * \brief Widget map runtime context
	 *
	 * Contains information about this widget map. Currently
	 * highlighted widget, currently selected widgets, ...
	 */
	struct {
		/* we redraw the widgetmap when this changes */
		struct wmWidget *highlighted_widget;
		/* user has clicked this widget and it gets all input */
		struct wmWidget *active_widget;
		/* array for all selected widgets
		 * TODO  check on using BLI_array */
		struct wmWidget **selected_widgets;
		int tot_selected;

		/* set while widget is highlighted/active */
		struct wmWidgetGroup *activegroup;
	} wmap_context;
} wmWidgetMap;


struct wmWidgetMapType_Params {
	const char *idname;
	const int spaceid;
	const int regionid;
	const int flag;
};

/**
 * Simple utility wrapper for storing a single widget as wmWidgetGroup.customdata
 */
typedef struct wmWidgetWrapper {
	struct wmWidget *widget;
} wmWidgetWrapper;


/* -------------------------------------------------------------------- */

/* wmWidget->flag */
enum eWidgetFlag {
	/* states */
	WM_WIDGET_HIGHLIGHT   = (1 << 0),
	WM_WIDGET_ACTIVE      = (1 << 1),
	WM_WIDGET_SELECTED    = (1 << 2),
	/* settings */
	WM_WIDGET_DRAW_HOVER  = (1 << 3),
	WM_WIDGET_DRAW_ACTIVE = (1 << 4), /* draw while dragging */
	WM_WIDGET_DRAW_VALUE  = (1 << 5), /* draw a indicator for the current value while dragging */
	WM_WIDGET_SCALE_3D    = (1 << 6),
	WM_WIDGET_SCENE_DEPTH = (1 << 7), /* widget is depth culled with scene objects*/
	WM_WIDGET_HIDDEN      = (1 << 8),
	WM_WIDGET_SELECTABLE  = (1 << 9),
};

/* wmWidgetMapType->flag */
enum eWidgetMapTypeFlag {
	/**
	 * Check if widgetmap does 3D drawing
	 * (uses a different kind of interaction),
	 * - 3d: use glSelect buffer.
	 * - 2d: use simple cursor position intersection test. */
	WM_WIDGETMAPTYPE_3D           = (1 << 0),
};

#endif  /* __WM_WIDGET_TYPES_H__ */

