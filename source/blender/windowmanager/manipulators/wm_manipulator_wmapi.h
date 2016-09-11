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

/** \file blender/windowmanager/manipulators/wm_manipulator_wmapi.h
 *  \ingroup wm
 *
 * \name Manipulators Window Manager API
 * \brief API for usage in window manager code only.
 *
 * Only included in wm.h and lower level files.
 */


#ifndef __WM_MANIPULATOR_WMAPI_H__
#define __WM_MANIPULATOR_WMAPI_H__

struct wmEventHandler;
struct wmOperatorType;
struct wmOperator;


/* -------------------------------------------------------------------- */
/* wmManipulator */

typedef void (*wmManipulatorSelectFunc)(struct bContext *, struct wmManipulator *, const int);


/* widgets are set per region by registering them on widgetmaps */
typedef struct wmManipulator {
	struct wmManipulator *next, *prev;

	char idname[MAX_NAME + 4]; /* + 4 for unique '.001', '.002', etc suffix */

	/* pointer back to parent widget group */
	struct wmManipulatorGroup *wgroup;

	/* could become wmManipulatorType */
	/* draw widget */
	void (*draw)(const struct bContext *C, struct wmManipulator *widget);

	/* determine if the mouse intersects with the widget. The calculation should be done in the callback itself */
	int  (*intersect)(struct bContext *C, const struct wmEvent *event, struct wmManipulator *widget);

	/* determines 3d intersection by rendering the widget in a selection routine. */
	void (*render_3d_intersection)(const struct bContext *C, struct wmManipulator *widget, int selectionbase);

	/* handler used by the widget. Usually handles interaction tied to a widget type */
	int  (*handler)(struct bContext *C, const struct wmEvent *event, struct wmManipulator *widget, const int flag);

	/* widget-specific handler to update widget attributes based on the property value */
	void (*prop_data_update)(struct wmManipulator *widget, int slot);

	/* returns the final position which may be different from the origin, depending on the widget.
	 * used in calculations of scale */
	void (*get_final_position)(struct wmManipulator *widget, float vec[3]);

	/* activate a widget state when the user clicks on it */
	int (*invoke)(struct bContext *C, const struct wmEvent *event, struct wmManipulator *widget);

	/* called when widget tweaking is done - used to free data and reset property when cancelling */
	void (*exit)(bContext *C, struct wmManipulator *widget, const bool cancel);

	int (*get_cursor)(struct wmManipulator *widget);

	/* called when manipulator selection state changes */
	wmManipulatorSelectFunc select;

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
} wmManipulator;


/* -------------------------------------------------------------------- */
/* wmManipulatorGroup */

void WIDGETGROUP_OT_widget_select(struct wmOperatorType *ot);
void WIDGETGROUP_OT_widget_tweak(struct wmOperatorType *ot);

void  WM_manipulatorgroup_attach_to_modal_handler(struct bContext *C, struct wmEventHandler *handler,
                                             struct wmManipulatorGroupType *wgrouptype, struct wmOperator *op);

/* wmManipulatorGroupType->flag */
enum {
	WM_MANIPULATORGROUPTYPE_3D      = (1 << 0), /* WARNING: Don't change this! Bit used for wmManipulatorMapType comparisons! */
	/* widget group is attached to operator, and is only accessible as long as this runs */
	WM_MANIPULATORGROUPTYPE_OP      = (1 << 10),
	WM_MANIPULATORGROUP_INITIALIZED = (1 << 11), /* wgroup has been initialized */
};


/* -------------------------------------------------------------------- */
/* wmManipulatorMap */

void WM_manipulators_keymap(struct wmKeyConfig *keyconf);

bool WM_manipulatormap_is_3d(const struct wmManipulatorMap *wmap);

void WM_manipulatormaps_handled_modal_update(
        bContext *C, struct wmEvent *event, struct wmEventHandler *handler,
        const struct wmOperatorType *ot);
void WM_manipulatormap_handler_context(bContext *C, struct wmEventHandler *handler);

wmManipulator *WM_manipulatormap_find_highlighted_3D(
        struct wmManipulatorMap *wmap, bContext *C,
        const struct wmEvent *event, unsigned char *part);
wmManipulator *WM_manipulatormap_find_highlighted_widget(
        struct wmManipulatorMap *wmap, bContext *C,
        const struct wmEvent *event, unsigned char *part);
void WM_manipulatormap_set_highlighted_widget(
        struct wmManipulatorMap *wmap, const bContext *C,
        wmManipulator *widget, unsigned char part);
wmManipulator *WM_manipulatormap_get_highlighted_widget(struct wmManipulatorMap *wmap);
void WM_manipulatormap_set_active_widget(
        struct wmManipulatorMap *wmap, bContext *C,
        const struct wmEvent *event, wmManipulator *widget);
wmManipulator *WM_manipulatormap_get_active_widget(struct wmManipulatorMap *wmap);

bool WM_manipulatormap_deselect_all(struct wmManipulatorMap *wmap, wmManipulator ***sel);

#endif  /* __WM_MANIPULATOR_WMAPI_H__ */

