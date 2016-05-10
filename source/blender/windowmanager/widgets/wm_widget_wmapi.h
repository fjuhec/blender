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

/** \file blender/windowmanager/widgets/wm_widget_wmapi.h
 *  \ingroup wm
 *
 * \name Widgets Window Manager API
 * \brief API for usage in window manager code only.
 *
 * Only included in wm.h
 */


#ifndef __WM_WIDGET_WMAPI_H__
#define __WM_WIDGET_WMAPI_H__

struct wmEventHandler;
struct wmOperatorType;
struct wmOperator;


/* -------------------------------------------------------------------- */
/* wmWidget */

typedef void (*wmWidgetSelectFunc)(struct bContext *, struct wmWidget *, const int);


/* widgets are set per region by registering them on widgetmaps */
typedef struct wmWidget {
	struct wmWidget *next, *prev;

	char idname[MAX_NAME + 4]; /* + 4 for unique '.001', '.002', etc suffix */

	/* pointer back to parent widget group */
	struct wmWidgetGroup *wgroup;

	/* could become wmWidgetType */
	/* draw widget */
	void (*draw)(const struct bContext *C, struct wmWidget *widget);

	/* determine if the mouse intersects with the widget. The calculation should be done in the callback itself */
	int  (*intersect)(struct bContext *C, const struct wmEvent *event, struct wmWidget *widget);

	/* determines 3d intersection by rendering the widget in a selection routine. */
	void (*render_3d_intersection)(const struct bContext *C, struct wmWidget *widget, int selectionbase);

	/* handler used by the widget. Usually handles interaction tied to a widget type */
	int  (*handler)(struct bContext *C, const struct wmEvent *event, struct wmWidget *widget, const int flag);

	/* widget-specific handler to update widget attributes when a property is bound */
	void (*bind_to_prop)(struct wmWidget *widget, int slot);

	/* returns the final position which may be different from the origin, depending on the widget.
	 * used in calculations of scale */
	void (*get_final_position)(struct wmWidget *widget, float vec[3]);

	/* activate a widget state when the user clicks on it */
	int (*invoke)(struct bContext *C, const struct wmEvent *event, struct wmWidget *widget);

	/* called when widget tweaking is done - used to free data and reset property when cancelling */
	void (*exit)(bContext *C, struct wmWidget *widget, const bool cancel);

	int (*get_cursor)(struct wmWidget *widget);

	/* called when widget selection state changes */
	wmWidgetSelectFunc select;

	int flag; /* flags set by drawing and interaction, such as highlighting */

	unsigned char highlighted_part;

	/* center of widget in space, 2d or 3d */
	float origin[3];
	/* custom offset from origin */
	float offset[3];
	/* runtime property, set the scale while drawing on the viewport */
	float scale;
	/* user defined scale, in addition to the original one */
	float user_scale;
	/* user defined width for line drawing */
	float line_width;
	/* widget colors (uses default fallbacks if not defined) */
	float col[4], col_hi[4];

	/* data used during interaction */
	void *interaction_data;

	/* name of operator to spawn when activating the widget */
	const char *opname;
	/* operator properties if widget spawns and controls an operator, or owner pointer if widget spawns and controls a property */
	PointerRNA opptr;

	/* maximum number of properties attached to the widget */
	int max_prop;
	/* arrays of properties attached to various widget parameters. As the widget is interacted with, those properties get updated */
	PointerRNA *ptr;
	PropertyRNA **props;
} wmWidget;


/* -------------------------------------------------------------------- */
/* wmWidgetGroup */

void WIDGETGROUP_OT_widget_select(struct wmOperatorType *ot);
void WIDGETGROUP_OT_widget_tweak(struct wmOperatorType *ot);

void  wm_widgetgroup_attach_to_modal_handler(struct bContext *C, struct wmEventHandler *handler,
                                             struct wmWidgetGroupType *wgrouptype, struct wmOperator *op);

/* wmWidgetGroupType->flag */
enum {
	WM_WIDGETGROUPTYPE_3D      = (1 << 0), /* WARNING: Don't change this! Bit used for wmWidgetMapType comparisons! */
	/* widget group is attached to operator, and is only accessible as long as this runs */
	WM_WIDGETGROUPTYPE_OP      = (1 << 10),
};


/* -------------------------------------------------------------------- */
/* wmWidgetMap */

void wm_widgets_keymap(struct wmKeyConfig *keyconf);

bool wm_widgetmap_is_3d(const struct wmWidgetMap *wmap);

void wm_widgetmaps_handled_modal_update(
        bContext *C, struct wmEvent *event, struct wmEventHandler *handler,
        const struct wmOperatorType *ot);
void wm_widgetmap_handler_context(bContext *C, struct wmEventHandler *handler);

wmWidget *wm_widgetmap_find_highlighted_3D(struct wmWidgetMap *wmap, bContext *C,
                                        const struct wmEvent *event, unsigned char *part);
wmWidget *wm_widgetmap_find_highlighted_widget(struct wmWidgetMap *wmap, bContext *C,
                                     const struct wmEvent *event, unsigned char *part);
void      wm_widgetmap_set_highlighted_widget(struct wmWidgetMap *wmap, bContext *C,
                                              wmWidget *widget, unsigned char part);
wmWidget *wm_widgetmap_get_highlighted_widget(struct wmWidgetMap *wmap);
void      wm_widgetmap_set_active_widget(struct wmWidgetMap *wmap, bContext *C,
                                         const struct wmEvent *event, wmWidget *widget);
wmWidget *wm_widgetmap_get_active_widget(struct wmWidgetMap *wmap);

bool wm_widgetmap_deselect_all(struct wmWidgetMap *wmap, wmWidget ***sel);

#endif  /* __WM_WIDGET_WMAPI_H__ */

