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

/** \file blender/editors/screen/workspace_edit.c
 *  \ingroup edscreen
 */

#include <stdlib.h>

#include "BLI_utildefines.h"
#include "BLI_fileops.h"
#include "BLI_listbase.h"
#include "BLI_path_util.h"
#include "BLI_string.h"

#include "BKE_appdir.h"
#include "BKE_blendfile.h"
#include "BKE_context.h"
#include "BKE_idcode.h"
#include "BKE_main.h"
#include "BKE_library.h"
#include "BKE_report.h"
#include "BKE_screen.h"
#include "BKE_workspace.h"

#include "BLO_readfile.h"

#include "DNA_object_types.h"
#include "DNA_screen_types.h"
#include "DNA_windowmanager_types.h"

#include "ED_object.h"
#include "ED_screen.h"

#include "RNA_access.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "WM_api.h"
#include "WM_types.h"

#include "screen_intern.h"


/** \name Workspace API
 *
 * \brief API for managing workspaces and their data.
 * \{ */

WorkSpace *ED_workspace_add(Main *bmain, const char *name, SceneLayer *act_render_layer)
{
	WorkSpace *workspace = BKE_workspace_add(bmain, name);

	BKE_workspace_object_mode_set(workspace, OB_MODE_OBJECT);
	BKE_workspace_render_layer_set(workspace, act_render_layer);

	return workspace;
}

#ifdef USE_WORKSPACE_MODE
/**
 * Changes the object mode (if needed) to the one set in \a workspace_new.
 * Object mode is still stored on object level. In future it should all be workspace level instead.
 */
static void workspace_change_update_mode(
        const WorkSpace *workspace_old, const WorkSpace *workspace_new,
        bContext *C, Object *ob_act, ReportList *reports)
{
	ObjectMode mode_old = BKE_workspace_object_mode_get(workspace_old);
	ObjectMode mode_new = BKE_workspace_object_mode_get(workspace_new);

	if (mode_old != mode_new) {
		ED_object_mode_compat_set(C, ob_act, mode_new, reports);
		ED_object_toggle_modes(C, mode_new);
	}
}
#endif

static void workspace_change_update_render_layer(
        WorkSpace *workspace_new, const WorkSpace *workspace_old)
{
	if (!BKE_workspace_render_layer_get(workspace_new)) {
		BKE_workspace_render_layer_set(workspace_new, BKE_workspace_render_layer_get(workspace_old));
	}
}

static void workspace_change_update(
        WorkSpace *workspace_new, const WorkSpace *workspace_old,
        bContext *C, wmWindowManager *wm)
{
	/* needs to be done before changing mode! (to ensure right context) */
	workspace_change_update_render_layer(workspace_new, workspace_old);
#ifdef USE_WORKSPACE_MODE
	workspace_change_update_mode(workspace_old, workspace_new, C, CTX_data_active_object(C), &wm->reports);
#else
	UNUSED_VARS(wm);
#endif
}

static bool workspace_change_find_new_layout_cb(const WorkSpaceLayout *layout, void *UNUSED(arg))
{
	/* return false to stop the iterator if we've found a layout that can be activated */
	return workspace_layout_set_poll(layout) ? false : true;
}

static WorkSpaceLayout *workspace_change_get_new_layout(
        WorkSpace *workspace_new, wmWindow *win)
{
	/* ED_workspace_duplicate may have stored a layout to activate once the workspace gets activated. */
	WorkSpaceLayout *layout_temp_store = BKE_workspace_temp_layout_store_get(win->workspace_hook);
	WorkSpaceLayout *layout_new;
	bScreen *screen_new;

	if (layout_temp_store) {
		layout_new = layout_temp_store;
	}
	else {
		layout_new = BKE_workspace_active_layout_get_from_workspace(win->workspace_hook, workspace_new);
		if (!layout_new) {
			layout_new = BKE_workspace_layouts_get(workspace_new)->first;
		}
	}
	screen_new = BKE_workspace_layout_screen_get(layout_new);

	if (screen_new->winid) {
		/* screen is already used, try to find a free one */
		WorkSpaceLayout *layout_temp = BKE_workspace_layout_iter_circular(
		                                   workspace_new, layout_new, workspace_change_find_new_layout_cb,
		                                   NULL, false);
		if (!layout_temp) {
			/* fallback solution: duplicate layout */
			layout_temp = ED_workspace_layout_duplicate(workspace_new, layout_new, win);
		}
		layout_new = layout_temp;
	}

	return layout_new;
}

/**
 * \brief Change the active workspace.
 *
 * Operator call, WM + Window + screen already existed before
 * Pretty similar to #ED_screen_change since changing workspace also changes screen.
 *
 * \warning Do NOT call in area/region queues!
 * \returns if workspace changing was successful.
 */
bool ED_workspace_change(
        bContext *C, wmWindowManager *wm, wmWindow *win, WorkSpace *workspace_new)
{
	Main *bmain = CTX_data_main(C);
	WorkSpace *workspace_old = WM_window_get_active_workspace(win);
	WorkSpaceLayout *layout_new = workspace_change_get_new_layout(workspace_new, win);
	bScreen *screen_new = BKE_workspace_layout_screen_get(layout_new);
	bScreen *screen_old = BKE_workspace_active_screen_get(win->workspace_hook);

	BKE_workspace_temp_layout_store_set(win->workspace_hook, NULL);
	if (workspace_old == workspace_new) {
		/* Could also return true, everything that needs to be done was done (nothing :P), but nothing changed */
		return false;
	}

	screen_new = screen_change_prepare(screen_old, screen_new, bmain, C, win);

	if (screen_new) {
		WM_window_set_active_layout(win, workspace_new, layout_new);
		WM_window_set_active_workspace(win, workspace_new);

		/* update screen *after* changing workspace - which also causes the actual screen change */
		screen_changed_update(C, win, screen_new);
		workspace_change_update(workspace_new, workspace_old, C, wm);

		BLI_assert(BKE_workspace_render_layer_get(workspace_new) != NULL);
		BLI_assert(CTX_wm_workspace(C) == workspace_new);

		return true;
	}

	return false;
}

/**
 * Duplicate a workspace including its layouts. Does not activate the workspace, but
 * it stores the screen-layout to be activated (BKE_workspace_temp_layout_store)
 */
WorkSpace *ED_workspace_duplicate(WorkSpace *workspace_old, Main *bmain, wmWindow *win)
{
	WorkSpaceLayout *layout_active_old = BKE_workspace_active_layout_get(win->workspace_hook);
	ListBase *layouts_old = BKE_workspace_layouts_get(workspace_old);
	WorkSpace *workspace_new = ED_workspace_add(bmain, BKE_workspace_name_get(workspace_old),
	                                            BKE_workspace_render_layer_get(workspace_old));

	BKE_workspace_object_mode_set(workspace_new, BKE_workspace_object_mode_get(workspace_old));

	BKE_workspace_layout_iter_begin(layout_old, layouts_old->first)
	{
		WorkSpaceLayout *layout_new = ED_workspace_layout_duplicate(workspace_new, layout_old, win);

		if (layout_active_old == layout_old) {
			BKE_workspace_temp_layout_store_set(win->workspace_hook, layout_new);
		}
	}
	BKE_workspace_layout_iter_end;

	return workspace_new;
}

/**
 * \return if succeeded.
 */
bool ED_workspace_delete(Main *bmain, bContext *C, wmWindowManager *wm, wmWindow *win, WorkSpace *ws)
{
	if (BLI_listbase_is_single(&bmain->workspaces)) {
		return false;
	}

	if (WM_window_get_active_workspace(win) == ws) {
		WorkSpace *prev = BKE_workspace_prev_get(ws);
		WorkSpace *next = BKE_workspace_next_get(ws);

		ED_workspace_change(C, wm, win, (prev != NULL) ? prev : next);
	}
	BKE_libblock_free(bmain, BKE_workspace_id_get(ws));

	return true;
}

/**
 * Some editor data may need to be synced with scene data (3D View camera and layers).
 * This function ensures data is synced for editors in active layout of \a workspace.
 */
void ED_workspace_scene_data_sync(WorkSpaceInstanceHook *hook, Scene *scene)
{
	bScreen *screen = BKE_workspace_active_screen_get(hook);
	BKE_screen_view3d_scene_sync(screen, scene);
}

/** \} Workspace API */


/** \name Workspace Operators
 *
 * \{ */

static int workspace_new_exec(bContext *C, wmOperator *UNUSED(op))
{
	Main *bmain = CTX_data_main(C);
	wmWindow *win = CTX_wm_window(C);
	WorkSpace *workspace = ED_workspace_duplicate(WM_window_get_active_workspace(win), bmain, win);

	WM_event_add_notifier(C, NC_WORKSPACE | ND_WORKSPACE_SET, workspace);

	return OPERATOR_FINISHED;
}

static void WORKSPACE_OT_workspace_duplicate(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "New Workspace";
	ot->description = "Add a new workspace";
	ot->idname = "WORKSPACE_OT_workspace_duplicate";

	/* api callbacks */
	ot->exec = workspace_new_exec;
	ot->poll = WM_operator_winactive;
}

static int workspace_delete_exec(bContext *C, wmOperator *UNUSED(op))
{
	Main *bmain = CTX_data_main(C);
	wmWindowManager *wm = CTX_wm_manager(C);
	wmWindow *win = CTX_wm_window(C);

	ED_workspace_delete(bmain, C, wm, win, WM_window_get_active_workspace(win));

	return OPERATOR_FINISHED;
}

static void WORKSPACE_OT_workspace_delete(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Delete Workspace";
	ot->description = "Delete the active workspace";
	ot->idname = "WORKSPACE_OT_workspace_delete";

	/* api callbacks */
	ot->exec = workspace_delete_exec;
}

ATTR_NONNULL(1)
static WorkflowFileData *workspace_workflow_file_read(const Main *bmain, ReportList *reports)
{
	char filepath_workflow[FILE_MAX];
	const char * const cfgdir = BKE_appdir_folder_id(BLENDER_USER_CONFIG, NULL);

	if (cfgdir) {
		BLI_make_file_string(bmain->name, filepath_workflow, cfgdir, BLENDER_WORKFLOW_FILE);
	}
	else {
		filepath_workflow[0] = '\0';
	}

	if (BLI_exists(filepath_workflow)) {
		/* may still return NULL */
		return BKE_blendfile_workflow_read(filepath_workflow, reports);
	}
	else if (reports) {
		BKE_reportf(reports, RPT_WARNING, "Couldn't find workflow file in %s", filepath_workflow);
	}

	return NULL;
}

ATTR_NONNULL(1, 2)
static void workspace_workflow_file_append_buttons(
        uiLayout *layout, const Main *bmain, ReportList *reports)
{
	WorkflowFileData *workflow_file = workspace_workflow_file_read(bmain, reports);

	if (workflow_file) {
		wmOperatorType *ot_append = WM_operatortype_find("WM_OT_append", true);
		PointerRNA opptr;
		char lib_path[FILE_MAX_LIBEXTRA];

		BKE_workspace_iter_begin(workspace, workflow_file->workspaces.first)
		{
			ID *id = BKE_workspace_id_get(workspace);

			BLI_snprintf(
			            lib_path, sizeof(lib_path), "%s%c%s", workflow_file->main->name,
			            SEP, BKE_idcode_to_name(GS(id->name)));

			opptr = uiItemFullO_ptr(
			            layout, ot_append, BKE_workspace_name_get(workspace), ICON_NONE, NULL,
			            WM_OP_EXEC_DEFAULT, UI_ITEM_O_RETURN_PROPS);
			RNA_string_set(&opptr, "directory", lib_path);
			RNA_string_set(&opptr, "filename", id->name + 2);
		}
		BKE_workspace_iter_end;

		BKE_blendfile_workflow_data_free(workflow_file);
	}
}

static int workspace_add_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	const Main *bmain = CTX_data_main(C);

	uiPopupMenu *pup = UI_popup_menu_begin(C, op->type->name, ICON_NONE);
	uiLayout *layout = UI_popup_menu_layout(pup);

	uiItemO(layout, "Duplicate Current", ICON_NONE, "WORKSPACE_OT_workspace_duplicate");
	uiItemS(layout);
	workspace_workflow_file_append_buttons(layout, bmain, op->reports);

	UI_popup_menu_end(C, pup);

	return OPERATOR_INTERFACE;
}

static void WORKSPACE_OT_workspace_add_menu(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Workspace";
	ot->description = "Add a new workspace by duplicating the current one or appending one "
	                  "from the workflow configuration";
	ot->idname = "WORKSPACE_OT_workspace_add_menu";

	/* api callbacks */
	ot->invoke = workspace_add_invoke;
}

void ED_operatortypes_workspace(void)
{
	WM_operatortype_append(WORKSPACE_OT_workspace_duplicate);
	WM_operatortype_append(WORKSPACE_OT_workspace_delete);
	WM_operatortype_append(WORKSPACE_OT_workspace_add_menu);
}

/** \} Workspace Operators */
