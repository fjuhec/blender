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

/** \file blender/windowmanager/manipulators/WM_manipulator_api.h
 *  \ingroup wm
 *
 * \name Manipulator API
 * \brief API for external use of wmManipulator types.
 *
 * Only included in WM_api.h
 */


#ifndef __WM_MANIPULATOR_API_H__
#define __WM_MANIPULATOR_API_H__

struct ARegion;
struct Main;
struct wmEventHandler;
struct wmKeyConfig;
struct wmKeyMap;
struct wmOperator;
struct wmManipulatorGroup;
struct wmManipulatorGroupType;
struct wmManipulatorMap;
struct wmManipulatorMapType;
struct wmManipulatorMapType_Params;

/* -------------------------------------------------------------------- */
/* wmManipulator */

struct wmManipulator *WM_widget_new(
        void (*draw)(const struct bContext *, struct wmManipulator *),
        void (*render_3d_intersection)(const struct bContext *, struct wmManipulator *, int),
        int  (*intersect)(struct bContext *, const struct wmEvent *, struct wmManipulator *),
        int  (*handler)(struct bContext *, const struct wmEvent *, struct wmManipulator *, const int));
void WM_widget_delete(ListBase *widgetlist, struct wmManipulatorMap *wmap, struct wmManipulator *widget, struct bContext *C);

void WM_widget_set_property(struct wmManipulator *, int slot, struct PointerRNA *ptr, const char *propname);
struct PointerRNA *WM_widget_set_operator(struct wmManipulator *, const char *opname);
void WM_widget_set_func_select(
        struct wmManipulator *widget,
        void (*select)(struct bContext *, struct wmManipulator *, const int action)); /* wmManipulatorSelectFunc */
void WM_widget_set_origin(struct wmManipulator *widget, const float origin[3]);
void WM_widget_set_offset(struct wmManipulator *widget, const float offset[3]);
void WM_widget_set_flag(struct wmManipulator *widget, const int flag, const bool enable);
void WM_widget_set_scale(struct wmManipulator *widget, float scale);
void WM_widget_set_line_width(struct wmManipulator *widget, const float line_width);
void WM_widget_set_colors(struct wmManipulator *widget, const float col[4], const float col_hi[4]);


/* -------------------------------------------------------------------- */
/* wmManipulatorGroup */

struct wmManipulatorGroupType *WM_widgetgrouptype_append(
        struct wmManipulatorMapType *wmaptype,
        void (*wgrouptype_func)(struct wmManipulatorGroupType *));
struct wmManipulatorGroupType *WM_widgetgrouptype_append_runtime(
        const struct Main *main, struct wmManipulatorMapType *wmaptype,
        void (*wgrouptype_func)(struct wmManipulatorGroupType *));
void WM_widgetgrouptype_init_runtime(
        const struct Main *bmain, struct wmManipulatorMapType *wmaptype,
        struct wmManipulatorGroupType *wgrouptype);
void WM_widgetgrouptype_unregister(struct bContext *C, struct Main *bmain, struct wmManipulatorGroupType *wgroup);

struct wmKeyMap *WM_widgetgroup_keymap_common(
        const struct wmManipulatorGroupType *wgrouptype, struct wmKeyConfig *config);
struct wmKeyMap *WM_widgetgroup_keymap_common_sel(
        const struct wmManipulatorGroupType *wgrouptype, struct wmKeyConfig *config);


/* -------------------------------------------------------------------- */
/* wmManipulatorMap */

struct wmManipulatorMapType *WM_widgetmaptype_find(const struct wmManipulatorMapType_Params *wmap_params);
struct wmManipulatorMapType *WM_widgetmaptype_ensure(const struct wmManipulatorMapType_Params *wmap_params);
struct wmManipulatorMap *WM_widgetmap_from_type(const struct wmManipulatorMapType_Params *wmap_params);
struct wmManipulatorMap *WM_widgetmap_find(const struct ARegion *ar, const struct wmManipulatorMapType_Params *wmap_params);

void WM_widgetmap_delete(struct wmManipulatorMap *wmap);
void WM_widgetmaptypes_free(void);

void WM_widgetmap_tag_refresh(struct wmManipulatorMap *wmap);
void WM_widgetmap_widgets_update(const struct bContext *C, struct wmManipulatorMap *wmap);
void WM_widgetmap_widgets_draw(
        const struct bContext *C, const struct wmManipulatorMap *wmap,
        const bool in_scene, const bool free_drawwidgets);

void WM_widgetmaps_add_handlers(struct ARegion *ar);

bool WM_widgetmap_select_all(struct bContext *C, struct wmManipulatorMap *wmap, const int action);

bool WM_widgetmap_cursor_set(const struct wmManipulatorMap *wmap, struct wmWindow *win);

#endif  /* __WM_MANIPULATOR_API_H__ */

