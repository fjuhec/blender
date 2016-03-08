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
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/widgets/intern/wm_widget_intern.h
 *  \ingroup wm
 */


#ifndef __WM_WIDGET_INTERN_H__
#define __WM_WIDGET_INTERN_H__

struct wmKeyConfig;
struct wmWidget;
struct wmWidgetMap;

/* -------------------------------------------------------------------- */
/* wmWidget */

/**
 * \brief Widget tweak flag.
 * Bitflag passed to widget while tweaking.
 */
enum {
	/* drag with extra precision (shift)
	 * NOTE: Widgets are responsible for handling this (widget->handler callback)! */
	WM_WIDGET_TWEAK_PRECISE = (1 << 0),
};

bool wm_widget_register(wmWidgetGroup *wgroup, struct wmWidget *widget, const char *name);
void wm_widget_data_free(struct wmWidget *widget);
void wm_widget_delete(ListBase *widgetlist, struct wmWidget *widget);

void wm_widget_deselect(const bContext *C, struct wmWidgetMap *wmap, struct wmWidget *widget);
void wm_widget_select(bContext *C, struct wmWidgetMap *wmap, struct wmWidget *widget);

bool wm_widget_compare(const struct wmWidget *a, const struct wmWidget *b);
void wm_widget_calculate_scale(struct wmWidget *widget, const bContext *C);

void fix_linking_widget_arrow(void);
void fix_linking_widget_arrow2d(void);
void fix_linking_widget_cage(void);
void fix_linking_widget_dial(void);
void fix_linking_widget_facemap(void);
void fix_linking_widget_primitive(void);


/* -------------------------------------------------------------------- */
/* wmWidgetGroup */

enum {
	TWEAK_MODAL_CANCEL = 1,
	TWEAK_MODAL_CONFIRM,
	TWEAK_MODAL_PRECISION_ON,
	TWEAK_MODAL_PRECISION_OFF,
};

void wm_widgetgroup_free(bContext *C, wmWidgetMap *wmap, struct wmWidgetGroup *wgroup);

void wm_widgetgrouptype_keymap_init(struct wmWidgetGroupType *wgrouptype, struct wmKeyConfig *keyconf);


/* -------------------------------------------------------------------- */
/* wmWidgetMap */

/**
 * This is a container for all widget types that can be instantiated in a region.
 * (similar to dropboxes).
 *
 * \note There is only ever one of these for every (area, region) combination.
 */
typedef struct wmWidgetMapType {
	struct wmWidgetMapType *next, *prev;
	char idname[64];
	short spaceid, regionid;
	/* widgetTypeflags */
	int flag;
	/* types of widgetgroups for this widgetmap type */
	ListBase widgetgrouptypes;
} wmWidgetMapType;

void wm_widgetmap_selected_delete(wmWidgetMap *wmap);


/* -------------------------------------------------------------------- */
/* Widget drawing */

typedef struct WidgetDrawInfo {
	int nverts;
	int ntris;
	float (*verts)[3];
	float (*normals)[3];
	unsigned short *indices;
	bool init;
} WidgetDrawInfo;

void widget_draw_intern(WidgetDrawInfo *info, const bool select);

#endif  /* __WM_WIDGET_INTERN_H__ */

