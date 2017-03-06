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


static void workspace_layout_type_remove(WorkSpace *workspace, WorkSpaceLayoutType *layout_type)
{
	BLI_assert(BLI_findindex(&workspace->layout_types, layout_type) >= 0);
	BLI_freelinkN(&workspace->layout_types, layout_type);
}

static void workspace_layout_remove(WorkSpace *workspace, WorkSpaceLayout *layout, Main *bmain)
{
	bScreen *screen = BKE_workspace_layout_screen_get(layout);

	BLI_assert(BLI_findindex(&workspace->layouts, layout) >= 0);
	BKE_libblock_free(bmain, screen);
	BLI_freelinkN(&workspace->layouts, layout);
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

void BKE_workspace_free(WorkSpace *ws)
{
	BLI_freelistN(&ws->layout_types);
}

void BKE_workspace_remove(WorkSpace *workspace, Main *bmain)
{
	BKE_workspace_layout_iter_begin(layout, workspace->layouts.first)
	{
		workspace_layout_remove(workspace, layout, bmain);
	}
	BKE_workspace_layout_iter_end;
	BKE_workspace_layout_type_iter_begin(layout_type, workspace->layout_types.first)
	{
		workspace_layout_type_remove(workspace, layout_type);
	}
	BKE_workspace_layout_type_iter_end;

	BKE_libblock_free(bmain, workspace);
}


WorkSpaceLayout *BKE_workspace_layout_add_from_type(WorkSpace *workspace, WorkSpaceLayoutType *type, bScreen *screen)
{
	WorkSpaceLayout *layout = MEM_mallocN(sizeof(*layout), __func__);

//	BLI_assert(!workspaces_is_screen_used(G.main, screen));

	layout->type = type;
	layout->screen = screen;
	BLI_addhead(&workspace->layouts, layout);

	return layout;
}

WorkSpaceLayoutType *BKE_workspace_layout_type_add(WorkSpace *workspace, const char *name,
                                                   ListBase *vertbase, ListBase *areabase)
{
	WorkSpaceLayoutType *layout_type = MEM_mallocN(sizeof(*layout_type), __func__);

	layout_type->name = name; /* XXX should probably copy name */
	layout_type->vertbase = vertbase;
	layout_type->areabase = areabase;
	BLI_addhead(&workspace->layout_types, layout_type);

	return layout_type;
}

void BKE_workspace_layout_remove(WorkSpace *workspace, WorkSpaceLayout *layout, Main *bmain)
{
	WorkSpaceLayoutType *layout_type = layout->type;
	workspace_layout_remove(workspace, layout, bmain);
	workspace_layout_type_remove(workspace, layout_type);
}

WorkSpaceHook *BKE_workspace_hook_new(void)
{
	return MEM_callocN(sizeof(WorkSpaceHook), __func__);
}

void BKE_workspace_hook_delete(Main *bmain, WorkSpaceHook *hook)
{
	for (WorkSpaceLayout *layout = hook->layouts.first, *layout_next; layout; layout = layout_next) {
		layout_next = layout->next;

		BKE_libblock_free(bmain, layout->screen);
		BLI_freelinkN(&hook->layouts, layout);
	}
	MEM_freeN(hook);
}


/* -------------------------------------------------------------------- */
/* General Utils */

void BKE_workspace_change_prepare(Main *bmain, WorkSpaceHook *workspace_hook, WorkSpace *workspace_new)
{
	for (WorkSpaceLayoutType *type = workspace_new->layout_types.first; type; type = type->next) {
		bScreen *screen = BKE_screen_create_from_screen_data(bmain, type->vertbase, type->areabase, type->name);
		WorkSpaceLayout *layout = BKE_workspace_layout_add_from_type(workspace_new, type, screen);

		BLI_addtail(&workspace_hook->layouts, layout);

		/* XXX Just setting the active layout matching the active type stored in workspace */
		if (type == workspace_new->act_layout_type) {
			workspace_new->act_layout = layout;
		}
	}
}

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
 * This should only be used directly when it is to be expected that there isn't
 * a layout within \a workspace that wraps \a screen. Usually - especially outside
 * of BKE_workspace - #BKE_workspace_layout_find should be used!
 */
static WorkSpaceLayout *workspace_layout_find(const WorkSpaceHook *hook, const bScreen *screen)
{
	for (WorkSpaceLayout *layout = hook->layouts.first; layout; layout = layout->next) {
		if (layout->screen == screen) {
			return layout;
		}
	}

	return NULL;
}

#if 0
/**
 * Checks if \a screen is already used within any workspace. A screen should never be assigned to multiple
 * WorkSpaceLayouts, but that should be ensured outside of the BKE_workspace module and without such checks.
 * Hence, this should only be used as assert check before assigining a screen to a workflow.
 */
bool workspaces_is_screen_used(const Main *bmain, bScreen *screen)
{
	for (WorkSpace *workspace = bmain->workspaces.first; workspace; workspace = workspace->id.next) {
		if (workspace_layout_find(workspace, screen)) {
			return true;
		}
	}

	return false;
}
#endif

WorkSpaceLayout *BKE_workspace_layout_find(const WorkSpaceHook *hook, const bScreen *screen)
{
	WorkSpaceLayout *layout = workspace_layout_find(hook, screen);
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

ID *BKE_workspace_id_get(WorkSpace *workspace)
{
	return &workspace->id;
}

const char *BKE_workspace_name_get(const WorkSpace *workspace)
{
	return workspace->id.name + 2;
}

WorkSpaceLayout *BKE_workspace_active_layout_get(const WorkSpace *workspace)
{
	return workspace->act_layout;
}
void BKE_workspace_active_layout_set(WorkSpace *workspace, WorkSpaceLayout *layout)
{
	workspace->act_layout = layout;
	workspace->act_layout_type = layout->type;
}

WorkSpaceLayout *BKE_workspace_new_layout_get(const WorkSpace *workspace)
{
	return workspace->new_layout;
}
void BKE_workspace_new_layout_set(WorkSpace *workspace, WorkSpaceLayout *layout)
{
	workspace->new_layout = layout;
}

bScreen *BKE_workspace_active_screen_get(const WorkSpace *ws)
{
	return ws->act_layout->screen;
}
void BKE_workspace_active_screen_set(const WorkSpaceHook *hook, bScreen *screen)
{
	WorkSpace *workspace = hook->act_workspace;
	WorkSpaceLayout *layout = BKE_workspace_layout_find(hook, screen);
	/* we need to find the WorkspaceLayout that wraps this screen */
	workspace->act_layout = layout;
	workspace->act_layout_type = layout->type;
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

WorkSpaceLayoutType *BKE_workspace_active_layout_type_get(const WorkSpace *workspace)
{
	return workspace->act_layout_type;
}
void BKE_workspace_active_layout_type_set(WorkSpace *workspace, WorkSpaceLayoutType *layout_type)
{
	workspace->act_layout_type = layout_type;
}

ListBase *BKE_workspace_layout_types_get(WorkSpace *workspace)
{
	return &workspace->layout_types;
}

const char *BKE_workspace_layout_type_name_get(const WorkSpaceLayoutType *layout_type)
{
	return layout_type->name;
}

ListBase *BKE_workspace_layout_type_vertbase_get(const WorkSpaceLayoutType *type)
{
	return type->vertbase;
}
ListBase *BKE_workspace_layout_type_areabase_get(const WorkSpaceLayoutType *type)
{
	return type->areabase;
}

WorkSpaceLayoutType *BKE_workspace_layout_type_next_get(WorkSpaceLayoutType *layout_type)
{
	return layout_type->next;
}

WorkSpace *BKE_workspace_next_get(const WorkSpace *workspace)
{
	return workspace->id.next;
}
WorkSpace *BKE_workspace_prev_get(const WorkSpace *workspace)
{
	return workspace->id.prev;
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

WorkSpace *BKE_workspace_active_get(const WorkSpaceHook *hook)
{
	return hook->act_workspace;
}
void BKE_workspace_active_set(WorkSpaceHook *hook, WorkSpace *workspace)
{
	hook->act_workspace = workspace;
}

WorkSpace *BKE_workspace_active_delayed_get(const WorkSpaceHook *hook)
{
	return hook->new_workspace;
}
void BKE_workspace_active_delayed_set(WorkSpaceHook *hook, WorkSpace *workspace)
{
	hook->new_workspace = workspace;
}

ListBase *BKE_workspace_hook_layouts_get(WorkSpaceHook *workspace_hook)
{
	return &workspace_hook->layouts;
}
