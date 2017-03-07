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

/** \file BKE_workspace.h
 *  \ingroup bke
 */

#ifndef __BKE_WORKSPACE_H__
#define __BKE_WORKSPACE_H__

#include "BLI_compiler_attrs.h"
#include "BLI_utildefines.h"

struct bScreen;
struct ListBase;
struct Main;
struct ScreenLayoutData;
struct TransformOrientation;
struct WorkSpace;

typedef struct WorkSpace WorkSpace;
typedef struct WorkSpaceHook WorkSpaceHook;
typedef struct WorkSpaceLayout WorkSpaceLayout;
typedef struct WorkSpaceLayoutType WorkSpaceLayoutType;

/**
 * Plan is to store the object-mode per workspace, not per object anymore.
 * However, there's quite some work to be done for that, so for now, there is just a basic
 * implementation of an object <-> workspace object-mode syncing for testing, with some known
 * problems. Main problem being that the modes can get out of sync when changing object selection.
 * Would require a pile of temporary changes to always sync modes when changing selection. So just
 * leaving this here for some testing until object-mode is really a workspace level setting.
 */
#define USE_WORKSPACE_MODE


/* -------------------------------------------------------------------- */
/* Create, delete, init */

WorkSpace *BKE_workspace_add(struct Main *bmain, const char *name);
void BKE_workspace_free(WorkSpace *ws);
void BKE_workspace_remove(WorkSpace *workspace, struct Main *bmain);

WorkSpaceLayout *BKE_workspace_layout_add_from_type(WorkSpace *workspace, WorkSpaceLayoutType *type,
                                                    struct bScreen *screen) ATTR_NONNULL() ATTR_WARN_UNUSED_RESULT;
WorkSpaceLayoutType *BKE_workspace_layout_type_add(WorkSpace *workspace, const char *name,
                                                   struct ScreenLayoutData layout_blueprint) ATTR_NONNULL();
void BKE_workspace_layout_remove(WorkSpace *workspace, WorkSpaceLayout *layout, struct Main *bmain) ATTR_NONNULL();
WorkSpaceHook *BKE_workspace_hook_new(void) ATTR_WARN_UNUSED_RESULT;
void BKE_workspace_hook_delete(struct Main *bmain, WorkSpaceHook *hook) ATTR_NONNULL();


/* -------------------------------------------------------------------- */
/* General Utils */

#define BKE_workspace_iter_begin(_workspace, _start_workspace) \
	for (WorkSpace *_workspace = _start_workspace, *_workspace##_next; _workspace; _workspace = _workspace##_next) { \
		_workspace##_next = BKE_workspace_next_get(_workspace); /* support removing workspace from list */
#define BKE_workspace_iter_end } (void)0

void BKE_workspace_change_prepare(struct Main *bmain, WorkSpaceHook *workspace_hook, WorkSpace *workspace_new) ATTR_NONNULL();

void BKE_workspaces_transform_orientation_remove(const struct ListBase *workspaces,
                                                 const struct TransformOrientation *orientation) ATTR_NONNULL();

WorkSpaceLayout *BKE_workspace_layout_find(const WorkSpaceHook *hook, const struct bScreen *screen) ATTR_NONNULL() ATTR_WARN_UNUSED_RESULT;

#define BKE_workspace_layout_iter_begin(_layout, _start_layout) \
	for (WorkSpaceLayout *_layout = _start_layout, *_layout##_next; _layout; _layout = _layout##_next) { \
		_layout##_next = BKE_workspace_layout_next_get(_layout); /* support removing layout from list */
#define BKE_workspace_layout_iter_backwards_begin(_layout, _start_layout) \
	for (WorkSpaceLayout *_layout = _start_layout, *_layout##_prev; _layout; _layout = _layout##_prev) {\
		_layout##_prev = BKE_workspace_layout_prev_get(_layout); /* support removing layout from list */
#define BKE_workspace_layout_iter_end } (void)0

WorkSpaceLayout *BKE_workspace_layout_iter_circular(const WorkSpace *workspace, WorkSpaceLayout *start,
                                                    bool (*callback)(const WorkSpaceLayout *layout, void *arg),
                                                    void *arg, const bool iter_backward);

#define BKE_workspace_layout_type_iter_begin(_layout_type, _start_layout_type) \
	for (WorkSpaceLayoutType *_layout_type = _start_layout_type, *_layout_type##_next; \
	     _layout_type; \
	     _layout_type = _layout_type##_next) \
	{ \
		_layout_type##_next = BKE_workspace_layout_type_next_get(_layout_type); /* support removing layout-type from list */
#define BKE_workspace_layout_type_iter_end } (void)0



/* -------------------------------------------------------------------- */
/* Getters/Setters */

#define GETTER_ATTRS ATTR_NONNULL() ATTR_WARN_UNUSED_RESULT
#define SETTER_ATTRS ATTR_NONNULL(1)

struct ID *BKE_workspace_id_get(WorkSpace *workspace) GETTER_ATTRS;
const char *BKE_workspace_name_get(const WorkSpace *workspace) GETTER_ATTRS;
WorkSpaceLayout *BKE_workspace_active_layout_get(const struct WorkSpace *ws) GETTER_ATTRS;
void             BKE_workspace_active_layout_set(WorkSpace *ws, WorkSpaceLayout *layout) SETTER_ATTRS;
struct bScreen *BKE_workspace_active_screen_get(const WorkSpace *ws) GETTER_ATTRS;
void            BKE_workspace_active_screen_set(const WorkSpaceHook *hook, struct bScreen *screen) SETTER_ATTRS;
struct bScreen *BKE_workspace_hook_active_screen_get(const WorkSpaceHook *hook) GETTER_ATTRS;
enum ObjectMode BKE_workspace_object_mode_get(const WorkSpace *workspace) GETTER_ATTRS;
#ifdef USE_WORKSPACE_MODE
void            BKE_workspace_object_mode_set(WorkSpace *workspace, const enum ObjectMode mode) SETTER_ATTRS;
#endif
struct SceneLayer *BKE_workspace_render_layer_get(const WorkSpace *workspace) GETTER_ATTRS;
void               BKE_workspace_render_layer_set(WorkSpace *workspace, struct SceneLayer *layer) SETTER_ATTRS;
struct ListBase *BKE_workspace_layouts_get(WorkSpace *workspace) GETTER_ATTRS;
WorkSpaceLayoutType *BKE_workspace_active_layout_type_get(const WorkSpace *workspace) GETTER_ATTRS;
void                 BKE_workspace_active_layout_type_set(WorkSpace *workspace, WorkSpaceLayoutType *layout_type) SETTER_ATTRS;
struct ListBase *BKE_workspace_layout_types_get(WorkSpace *workspace) GETTER_ATTRS;
const char      *BKE_workspace_layout_type_name_get(const WorkSpaceLayoutType *layout_type) GETTER_ATTRS;
struct ScreenLayoutData BKE_workspace_layout_type_blueprint_get(WorkSpaceLayoutType *type) GETTER_ATTRS;
struct ListBase *BKE_workspace_layout_type_vertbase_get(WorkSpaceLayoutType *type) GETTER_ATTRS;
WorkSpaceLayoutType *BKE_workspace_layout_type_next_get(WorkSpaceLayoutType *layout_type) GETTER_ATTRS;
WorkSpaceLayout *BKE_workspace_new_layout_get(const WorkSpace *workspace) GETTER_ATTRS;
void             BKE_workspace_new_layout_set(WorkSpace *workspace, WorkSpaceLayout *layout) SETTER_ATTRS;

WorkSpace *BKE_workspace_next_get(const WorkSpace *workspace) GETTER_ATTRS;
WorkSpace *BKE_workspace_prev_get(const WorkSpace *workspace) GETTER_ATTRS;

struct bScreen *BKE_workspace_layout_screen_get(const WorkSpaceLayout *layout) GETTER_ATTRS;
void            BKE_workspace_layout_screen_set(WorkSpaceLayout *layout, struct bScreen *screen) SETTER_ATTRS;

WorkSpaceLayout *BKE_workspace_layout_next_get(const WorkSpaceLayout *layout) GETTER_ATTRS;
WorkSpaceLayout *BKE_workspace_layout_prev_get(const WorkSpaceLayout *layout) GETTER_ATTRS;

WorkSpace *BKE_workspace_active_get(const WorkSpaceHook *hook) GETTER_ATTRS;
void BKE_workspace_active_set(WorkSpaceHook *hook, WorkSpace *workspace) SETTER_ATTRS;
WorkSpace *BKE_workspace_active_delayed_get(const WorkSpaceHook *hook) GETTER_ATTRS;
void BKE_workspace_active_delayed_set(WorkSpaceHook *hook, WorkSpace *workspace) SETTER_ATTRS;
struct ListBase *BKE_workspace_hook_layouts_get(WorkSpaceHook *workspace_hook) GETTER_ATTRS;

/* -------------------------------------------------------------------- */
/* Don't use outside of BKE! */

WorkSpace *workspace_alloc(void) ATTR_WARN_UNUSED_RESULT;

#endif /* __BKE_WORKSPACE_H__ */
