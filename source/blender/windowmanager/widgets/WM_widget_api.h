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

/** \file blender/windowmanager/widgets/WM_widget_api.h
 *  \ingroup wm
 *
 * \name Widget API
 * \brief API for external use of wmWidget types.
 *
 * Only included in WM_api.h
 */


#ifndef __WM_WIDGET_API_H__
#define __WM_WIDGET_API_H__

struct ARegion;
struct Main;
struct wmEventHandler;
struct wmKeyConfig;
struct wmKeyMap;
struct wmOperator;
struct wmWidgetGroup;
struct wmWidgetGroupType;
struct wmWidgetMap;
struct wmWidgetMapType;
struct wmWidgetMapType_Params;

/* -------------------------------------------------------------------- */
/* wmWidget */

struct wmWidget *WM_widget_new(
        void (*draw)(const struct bContext *, struct wmWidget *),
        void (*render_3d_intersection)(const struct bContext *, struct wmWidget *, int),
        int  (*intersect)(struct bContext *, const struct wmEvent *, struct wmWidget *),
        int  (*handler)(struct bContext *, const struct wmEvent *, struct wmWidget *, const int));

void WM_widget_set_property(struct wmWidget *, int slot, struct PointerRNA *ptr, const char *propname);
struct PointerRNA *WM_widget_set_operator(struct wmWidget *, const char *opname);
void WM_widget_set_func_select(
        struct wmWidget *widget,
        void (*select)(struct bContext *, struct wmWidget *, const int action)); /* wmWidgetSelectFunc */
void WM_widget_set_origin(struct wmWidget *widget, const float origin[3]);
void WM_widget_set_offset(struct wmWidget *widget, const float offset[3]);
void WM_widget_set_flag(struct wmWidget *widget, const int flag, const bool enable);
void WM_widget_set_scale(struct wmWidget *widget, float scale);
void WM_widget_set_line_width(struct wmWidget *widget, const float line_width);
void WM_widget_set_colors(struct wmWidget *widget, const float col[4], const float col_hi[4]);


/* -------------------------------------------------------------------- */
/* wmWidgetGroup */

struct wmWidgetGroupType *WM_widgetgrouptype_register_ptr(
        const struct Main *bmain, struct wmWidgetMapType *wmaptype,
        int (*poll)(const struct bContext *, struct wmWidgetGroupType *), /* wmWidgetGroupPollFunc */
        void (*create)(const struct bContext *, struct wmWidgetGroup *),  /* wmWidgetGroupCreateFunc */
        struct wmKeyMap *(*keymap_init)(const struct wmWidgetGroupType *wgrouptype, struct wmKeyConfig *config),
        const char *name);
struct wmWidgetGroupType *WM_widgetgrouptype_register(
        const struct Main *bmain, const struct wmWidgetMapType_Params *wmap_params,
        int (*poll)(const struct bContext *, struct wmWidgetGroupType *), /* wmWidgetGroupPollFunc */
        void (*create)(const struct bContext *, struct wmWidgetGroup *),  /* wmWidgetGroupCreateFunc */
        struct wmKeyMap *(*keymap_init)(const struct wmWidgetGroupType *wgrouptype, struct wmKeyConfig *config),
        const char *name);
void WM_widgetgrouptype_init_runtime(
        const struct Main *bmain, struct wmWidgetMapType *wmaptype,
        struct wmWidgetGroupType *wgrouptype);
void WM_widgetgrouptype_unregister(struct bContext *C, struct Main *bmain, struct wmWidgetGroupType *wgroup);

struct wmKeyMap *WM_widgetgroup_keymap_common(
        const struct wmWidgetGroupType *wgrouptype, struct wmKeyConfig *config);
struct wmKeyMap *WM_widgetgroup_keymap_common_sel(
        const struct wmWidgetGroupType *wgrouptype, struct wmKeyConfig *config);


/* -------------------------------------------------------------------- */
/* wmWidgetMap */

struct wmWidgetMapType *WM_widgetmaptype_find(const struct wmWidgetMapType_Params *wmap_params);
struct wmWidgetMapType *WM_widgetmaptype_ensure(const struct wmWidgetMapType_Params *wmap_params);
struct wmWidgetMap *WM_widgetmap_from_type(const struct wmWidgetMapType_Params *wmap_params);

void WM_widgetmap_delete(struct wmWidgetMap *wmap);
void WM_widgetmaptypes_free(void);

void WM_widgetmap_widgets_update(const struct bContext *C, struct wmWidgetMap *wmap);
void WM_widgetmap_widgets_draw(const struct bContext *C, const struct wmWidgetMap *wmap,
                               const bool in_scene, const bool free_drawwidgets);

void WM_widgetmaps_add_handlers(struct ARegion *ar);

bool WM_widgetmap_select_all(struct bContext *C, struct wmWidgetMap *wmap, const int action);

bool WM_widgetmap_cursor_set(const struct wmWidgetMap *wmap, struct wmWindow *win);

#endif  /* __WM_WIDGET_API_H__ */

