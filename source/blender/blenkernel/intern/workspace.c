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

/** \file blender/blenkernel/intern/workspace.c
 *  \ingroup bke
 */

#define NAMESPACE_WORKSPACE /* allow including specially guarded dna_workspace_types.h */

#include <stdlib.h>

#include "BLI_string.h"
#include "BLI_string_utils.h"
#include "BLI_utildefines.h"
#include "BLI_listbase.h"

#include "BKE_global.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_screen.h"
#include "BKE_workspace.h"

#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "dna_workspace_types.h"

#include "MEM_guardedalloc.h"


bool workspaces_is_screen_used(const Main *bmain, bScreen *screen);

/* -------------------------------------------------------------------- */
/* Internal utils */

static void workspace_name_set(WorkSpace *workspace, WorkSpaceLayout *layout, const char *new_name)
{
	BLI_strncpy(layout->name, new_name, sizeof(layout->name));
	BLI_uniquename(&workspace->layouts, layout, "Layout", '.', offsetof(WorkSpaceLayout, name), sizeof(layout->name));
}

static void workspace_relation_add(ListBase *relation_list, void *parent, void *data)
{
	WorkSpaceDataRelation *relation = MEM_mallocN(sizeof(*relation), __func__);
	relation->parent = parent;
	relation->value = data;
	/* add to head, if we switch back to it soon we find it faster. */
	BLI_addhead(relation_list, relation);
}
static void workspace_relation_remove(ListBase *relation_list, WorkSpaceDataRelation *relation)
{
	BLI_remlink(relation_list, relation);
	MEM_freeN(relation);
}

static void workspace_ensure_updated_relation(ListBase *relation_list, void *parent, void *data)
{
	for (WorkSpaceDataRelation *relation = relation_list->first; relation; relation = relation->next) {
		if (relation->parent == parent) {
			relation->value = data;
			/* reinsert at the head of the list, so that more commonly used relations are found faster. */
			BLI_remlink(relation_list, relation);
			BLI_addhead(relation_list, relation);
			return;
		}
	}

	/* no matching relation found, add new one */
	workspace_relation_add(relation_list, parent, data);
}

static void *workspace_relation_get_data_matching_parent(const ListBase *relation_list, const void *parent)
{
	for (WorkSpaceDataRelation *relation = relation_list->first; relation; relation = relation->next) {
		if (relation->parent == parent) {
			return relation->value;
		}
	}

	return NULL;
}


/* -------------------------------------------------------------------- */
/* Create, delete, init */

/**
 * Only to be called by #BKE_libblock_alloc_notest! Always use BKE_workspace_add to add a new workspace.
 */
WorkSpace *workspace_alloc(void)
{
	return MEM_callocN(sizeof(WorkSpace), __func__);
}

WorkSpace *BKE_workspace_add(Main *bmain, const char *name)
{
	WorkSpace *new_ws = BKE_libblock_alloc(bmain, ID_WS, name);
	return new_ws;
}

void BKE_workspace_free(WorkSpace *workspace)
{
	for (WorkSpaceDataRelation *relation = workspace->hook_layout_relations.first, *relation_next;
	     relation;
	     relation = relation_next)
	{
		relation_next = relation->next;
		workspace_relation_remove(&workspace->hook_layout_relations, relation);
	}
	BLI_freelistN(&workspace->layouts);
}

void BKE_workspace_remove(WorkSpace *workspace, Main *bmain)
{
	BKE_workspace_layout_iter_begin(layout, workspace->layouts.first)
	{
		BKE_workspace_layout_remove(workspace, layout, bmain);
	}
	BKE_workspace_layout_iter_end;

	BKE_libblock_free(bmain, workspace);
}

WorkSpaceInstanceHook *BKE_workspace_instance_hook_create(const Main *bmain)
{
	WorkSpaceInstanceHook *hook = MEM_callocN(sizeof(WorkSpaceInstanceHook), __func__);

	/* set an active screen-layout for each possible window/workspace combination */
	BKE_workspace_iter_begin(workspace_iter, bmain->workspaces.first)
	{
		BKE_workspace_active_layout_set_for_workspace(hook, workspace_iter, workspace_iter->layouts.first);
	}
	BKE_workspace_iter_end;

	return hook;
}
void BKE_workspace_instance_hook_free(WorkSpaceInstanceHook *hook, const Main *bmain)
{
	/* workspaces should never be freed before wm (during which we call this function) */
	BLI_assert(!BLI_listbase_is_empty(&bmain->workspaces));

	/* Free relations for this hook */
	BKE_workspace_iter_begin(workspace, bmain->workspaces.first)
	{
		for (WorkSpaceDataRelation *relation = workspace->hook_layout_relations.first, *relation_next;
		     relation;
		     relation = relation_next)
		{
			relation_next = relation->next;
			if (relation->parent == hook) {
				workspace_relation_remove(&workspace->hook_layout_relations, relation);
			}
		}
	}
	BKE_workspace_iter_end;

	MEM_freeN(hook);
}

/**
 * Add a new layout to \a workspace for \a screen.
 */
WorkSpaceLayout *BKE_workspace_layout_add(WorkSpace *workspace, bScreen *screen, const char *name)
{
	WorkSpaceLayout *layout = MEM_mallocN(sizeof(*layout), __func__);

	BLI_assert(!workspaces_is_screen_used(G.main, screen));
	layout->screen = screen;
	workspace_name_set(workspace, layout, name);
	BLI_addhead(&workspace->layouts, layout);

	return layout;
}

void BKE_workspace_layout_remove(WorkSpace *workspace, WorkSpaceLayout *layout, Main *bmain)
{
	BKE_libblock_free(bmain, BKE_workspace_layout_screen_get(layout));
	BLI_freelinkN(&workspace->layouts, layout);
}


/* -------------------------------------------------------------------- */
/* General Utils */

void BKE_workspaces_transform_orientation_remove(const ListBase *workspaces, const TransformOrientation *orientation)
{
	BKE_workspace_iter_begin(workspace, workspaces->first)
	{
		BKE_workspace_layout_iter_begin(layout, workspace->layouts.first)
		{
			BKE_screen_transform_orientation_remove(BKE_workspace_layout_screen_get(layout), orientation);
		}
		BKE_workspace_layout_iter_end;
	}
	BKE_workspace_iter_end;
}

/**
 * Checks if \a screen is already used within any workspace. A screen should never be assigned to multiple
 * WorkSpaceLayouts, but that should be ensured outside of the BKE_workspace module and without such checks.
 * Hence, this should only be used as assert check before assigining a screen to a workflow.
 */
bool workspaces_is_screen_used(const Main *bmain, bScreen *screen)
{
	for (WorkSpace *workspace = bmain->workspaces.first; workspace; workspace = workspace->id.next) {
		if (BKE_workspace_layout_find_exec(workspace, screen)) {
			return true;
		}
	}

	return false;
}

/**
 * This should only be used directly when it is to be expected that there isn't
 * a layout within \a workspace that wraps \a screen. Usually - especially outside
 * of BKE_workspace - #BKE_workspace_layout_find should be used!
 */
WorkSpaceLayout *BKE_workspace_layout_find_exec(const WorkSpace *ws, const bScreen *screen)
{
	for (WorkSpaceLayout *layout = ws->layouts.first; layout; layout = layout->next) {
		if (layout->screen == screen) {
			return layout;
		}
	}

	return NULL;
}

WorkSpaceLayout *BKE_workspace_layout_find(const WorkSpace *ws, const bScreen *screen)
{
	WorkSpaceLayout *layout = BKE_workspace_layout_find_exec(ws, screen);
	if (layout) {
		return layout;
	}

	BLI_assert(!"Couldn't find layout in this workspace. This should not happen!");
	return NULL;
}

WorkSpaceLayout *BKE_workspace_layout_iter_circular(const WorkSpace *workspace, WorkSpaceLayout *start,
                                                    bool (*callback)(const WorkSpaceLayout *layout, void *arg),
                                                    void *arg, const bool iter_backward)
{
	WorkSpaceLayout *iter_layout;

	if (iter_backward) {
		BLI_LISTBASE_CIRCULAR_BACKWARD_BEGIN(&workspace->layouts, iter_layout, start)
		{
			if (!callback(iter_layout, arg)) {
				return iter_layout;
			}
		}
		BLI_LISTBASE_CIRCULAR_BACKWARD_END(&workspace->layouts, iter_layout, start);
	}
	else {
		BLI_LISTBASE_CIRCULAR_FORWARD_BEGIN(&workspace->layouts, iter_layout, start)
		{
			if (!callback(iter_layout, arg)) {
				return iter_layout;
			}
		}
		BLI_LISTBASE_CIRCULAR_FORWARD_END(&workspace->layouts, iter_layout, start)
	}

	return NULL;
}


/* -------------------------------------------------------------------- */
/* Getters/Setters */

/**
 * Needed because we can't switch workspaces during handlers, it would break context.
 */
WorkSpace *BKE_workspace_temp_store_get(WorkSpaceInstanceHook *hook)
{
	return hook->temp_store;
}
void BKE_workspace_temp_store_set(WorkSpaceInstanceHook *hook, WorkSpace *workspace)
{
	hook->temp_store = workspace;
}

WorkSpace *BKE_workspace_active_get(WorkSpaceInstanceHook *hook)
{
	return hook->active;
}
void BKE_workspace_active_set(WorkSpaceInstanceHook *hook, WorkSpace *workspace)
{
	hook->active = workspace;
	if (workspace) {
		WorkSpaceLayout *layout;
		if ((layout = workspace_relation_get_data_matching_parent(&workspace->hook_layout_relations, hook))) {
			hook->act_layout = layout;
		}
	}
}

ID *BKE_workspace_id_get(WorkSpace *workspace)
{
	return &workspace->id;
}

const char *BKE_workspace_name_get(const WorkSpace *workspace)
{
	return workspace->id.name + 2;
}

WorkSpaceLayout *BKE_workspace_active_layout_get_from_workspace(
        const WorkSpaceInstanceHook *hook, const WorkSpace *workspace)
{
	return workspace_relation_get_data_matching_parent(&workspace->hook_layout_relations, hook);
}
WorkSpaceLayout *BKE_workspace_active_layout_get(const WorkSpaceInstanceHook *hook)
{
	return hook->act_layout;
}
void BKE_workspace_active_layout_set(WorkSpaceInstanceHook *hook, WorkSpaceLayout *layout)
{
	hook->act_layout = layout;
}

void BKE_workspace_active_layout_set_for_workspace(
        WorkSpaceInstanceHook *hook, WorkSpace *workspace, WorkSpaceLayout *layout)
{
	hook->act_layout = layout;
	workspace_ensure_updated_relation(&workspace->hook_layout_relations, hook, layout);
}

WorkSpaceLayout *BKE_workspace_temp_layout_store_get(const WorkSpaceInstanceHook *hook)
{
	return hook->temp_layout_store;
}
void BKE_workspace_temp_layout_store_set(WorkSpaceInstanceHook *hook, WorkSpaceLayout *layout)
{
	hook->temp_layout_store = layout;
}

bScreen *BKE_workspace_active_screen_get(const WorkSpaceInstanceHook *hook)
{
	return hook->act_layout->screen;
}
void BKE_workspace_active_screen_set(WorkSpaceInstanceHook *hook, WorkSpace *workspace, bScreen *screen)
{
	/* we need to find the WorkspaceLayout that wraps this screen */
	WorkSpaceLayout *layout = BKE_workspace_layout_find(hook->active, screen);
	BKE_workspace_active_layout_set_for_workspace(hook, workspace, layout);
}

#ifdef USE_WORKSPACE_MODE
ObjectMode BKE_workspace_object_mode_get(const WorkSpace *workspace)
{
	return workspace->object_mode;
}
void BKE_workspace_object_mode_set(WorkSpace *workspace, const ObjectMode mode)
{
	workspace->object_mode = mode;
}
#endif

SceneLayer *BKE_workspace_render_layer_get(const WorkSpace *workspace)
{
	return workspace->render_layer;
}
void BKE_workspace_render_layer_set(WorkSpace *workspace, SceneLayer *layer)
{
	workspace->render_layer = layer;
}

ListBase *BKE_workspace_layouts_get(WorkSpace *workspace)
{
	return &workspace->layouts;
}

WorkSpace *BKE_workspace_next_get(const WorkSpace *workspace)
{
	return workspace->id.next;
}
WorkSpace *BKE_workspace_prev_get(const WorkSpace *workspace)
{
	return workspace->id.prev;
}


const char *BKE_workspace_layout_name_get(const WorkSpaceLayout *layout)
{
	return layout->name;
}
void BKE_workspace_layout_name_set(WorkSpace *workspace, WorkSpaceLayout *layout, const char *new_name)
{
	workspace_name_set(workspace, layout, new_name);
}

bScreen *BKE_workspace_layout_screen_get(const WorkSpaceLayout *layout)
{
	return layout->screen;
}
void BKE_workspace_layout_screen_set(WorkSpaceLayout *layout, bScreen *screen)
{
	layout->screen = screen;
}

WorkSpaceLayout *BKE_workspace_layout_next_get(const WorkSpaceLayout *layout)
{
	return layout->next;
}
WorkSpaceLayout *BKE_workspace_layout_prev_get(const WorkSpaceLayout *layout)
{
	return layout->prev;
}

ListBase *BKE_workspace_hook_layout_relations_get(WorkSpace *workspace)
{
	return &workspace->hook_layout_relations;
}

WorkSpaceDataRelation *BKE_workspace_relation_next_get(const WorkSpaceDataRelation *relation)
{
	return relation->next;
}

void BKE_workspace_relation_data_get(const WorkSpaceDataRelation *relation, void **parent, void **data)
{
	*parent = relation->parent;
	*data = relation->value;
}
void BKE_workspace_relation_data_set(WorkSpaceDataRelation *relation, void *parent, void *data)
{
	relation->parent = parent;
	relation->value = data;
}
