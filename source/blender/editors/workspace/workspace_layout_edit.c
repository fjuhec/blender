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

/** \file blender/editors/workspace/workspace_layout_edit.c
 *  \ingroup edworkspace
 */

#include <stdlib.h>

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_screen.h"
#include "BKE_workspace.h"

#include "BLI_utildefines.h"
#include "BLI_listbase.h"

#include "DNA_screen_types.h"

#include "ED_screen.h"

#include "WM_api.h"

#include "screen_intern.h"


/**
 * Creates and activates an layout type for \a workspace and layout instances for all windows showing this workspace.
 * Layout instances get an empty screen, with 1 dummy area without spacedata. Uses window size.
 */
void ED_workspace_layout_add(WorkSpace *workspace, ListBase *windows, const char *name,
                             ScreenLayoutData layout_blueprint)
{
	WorkSpaceLayoutType *layout_type = BKE_workspace_layout_type_add(workspace, name, layout_blueprint);

	for (wmWindow *win = windows->first; win; win = win->next) {
		if (BKE_workspace_active_get(win->workspace_hook) == workspace) {
			bScreen *screen = screen_add_from_layout_type(layout_type, win->winid);
			ListBase *layouts = BKE_workspace_hook_layouts_get(win->workspace_hook);
			WorkSpaceLayout *layout = BKE_workspace_layout_add_from_type(workspace, layout_type, screen);

			BLI_addhead(layouts, layout);
			BKE_workspace_hook_active_layout_set(win->workspace_hook, layout);
		}
	}
}

WorkSpaceLayout *ED_workspace_layout_duplicate(WorkSpace *workspace, const WorkSpaceLayout *layout_old,
                                               wmWindowManager *wm)
{
	bScreen *screen_old = BKE_workspace_layout_screen_get(layout_old);
	ScreenLayoutData layout_data = BKE_screen_layout_data_get(screen_old);
	bScreen *screen_new;
	WorkSpaceLayout *layout_new;

	if (BKE_screen_is_fullscreen_area(screen_old)) {
		return NULL; /* XXX handle this case! */
	}

	ED_workspace_layout_add(workspace, &wm->windows, screen_old->id.name + 2, layout_data);
	for (wmWindow *win = wm->windows.first; win; win = win->next) {
		if (BKE_workspace_active_get(win->workspace_hook) == workspace) {
			layout_new = BKE_workspace_hook_active_layout_get(win->workspace_hook);
			screen_new = BKE_workspace_layout_screen_get(layout_new);
			screen_data_copy(screen_new, screen_old);
		}
	}

	return layout_new;
}

static bool workspace_layout_delete_doit(
        bContext *C, WorkSpace *workspace, WorkSpaceLayoutType *layout_type_old, WorkSpaceLayoutType *layout_type_new)
{
	wmWindowManager *wm = CTX_wm_manager(C);
	Main *bmain = CTX_data_main(C);

	for (wmWindow *win = wm->windows.first; win; win = win->next) {
		if (BKE_workspace_active_get(win->workspace_hook) == workspace &&
		    BKE_workspace_active_layout_type_get(workspace) == layout_type_old)
		{
			wmWindow *win_ctx = CTX_wm_window(C);
			bScreen *screen_new = BKE_workspace_layout_screen_find_from_type(win->workspace_hook, layout_type_new);

			CTX_wm_window_set(C, win);
			ED_screen_change(C, screen_new);
			CTX_wm_window_set(C, win_ctx);
		}
	}

	if (BKE_workspace_active_layout_type_get(workspace) != layout_type_new) {
		BKE_workspace_layout_type_remove(workspace, layout_type_old, bmain);
		return true;
	}

	return false;
}

#if 0
static bool workspace_layout_set_poll(const WorkSpaceLayout *layout)
{
	const bScreen *screen = BKE_workspace_layout_screen_get(layout);

	return ((BKE_screen_is_used(screen) == false) &&
	        /* in typical usage temp screens should have a nonzero winid
	         * (all temp screens should be used, or closed & freed). */
	        (screen->temp == false) &
	        (BKE_screen_is_fullscreen_area(screen) == false) &&
	        (screen->id.name[2] != '.' || !(U.uiflag & USER_HIDE_DOT)));
}

static WorkSpaceLayout *workspace_layout_delete_find_new(const WorkSpaceLayout *layout_old)
{
	WorkSpaceLayout *prev = BKE_workspace_layout_prev_get(layout_old);
	WorkSpaceLayout *next = BKE_workspace_layout_next_get(layout_old);

	BKE_workspace_layout_iter_backwards_begin(layout_new, prev)
	{
		if (workspace_layout_set_poll(layout_new)) {
			return layout_new;
		}
	}
	BKE_workspace_layout_iter_end;
	BKE_workspace_layout_iter_begin(layout_new, next)
	{
		if (workspace_layout_set_poll(layout_new)) {
			return layout_new;
		}
	}
	BKE_workspace_layout_iter_end;

	return NULL;
}
#endif

/**
 * Delete all layout variations based on the layout-type of \a layout_old.
 * \warning Only call outside of area/region loops!
 * \return true if succeeded.
 */
bool ED_workspace_layout_delete(bContext *C, wmWindow *win, WorkSpace *workspace, WorkSpaceLayout *layout_old)
{
	WorkSpaceLayoutType *layout_type_old = BKE_workspace_layout_type_get(layout_old);
	WorkSpaceLayoutType *layout_type_new = BKE_workspace_layout_type_next_get(layout_type_old);

	BLI_assert(BLI_findindex(BKE_workspace_hook_layouts_get(win->workspace_hook), layout_old) != -1);

	/* don't allow deleting temp fullscreens for now */
	/* XXX */
//	if (BKE_screen_is_fullscreen_area(screen_old)) {
//		return false;
//	}

	/* A layout/screen can only be in use by one window at a time, so as
	 * long as we are able to find a layout/screen that is unused, we
	 * can safely assume ours is not in use anywhere an delete it. */

	/* XXX */
//	layout_new = workspace_layout_delete_find_new(layout_old);

	if (layout_type_new && (layout_type_new != layout_type_old)) {
		return workspace_layout_delete_doit(C, workspace, layout_type_old, layout_type_new);
	}

	return false;
}

#if 0
static bool workspace_layout_cycle_iter_cb(const WorkSpaceLayout *layout, void *UNUSED(arg))
{
	return workspace_layout_set_poll(layout);
}
#endif

bool ED_workspace_layout_cycle(bContext *C, WorkSpace *workspace, const short direction)
{
	wmWindowManager *wm = CTX_wm_manager(C);
	WorkSpaceLayoutType *old_type = BKE_workspace_active_layout_type_get(workspace);
	WorkSpaceLayoutType *new_type;
	WorkSpaceLayout *old_layout = BKE_workspace_active_layout_get(workspace);
	const bScreen *old_screen = BKE_workspace_layout_screen_get(old_layout);
	ScrArea *sa = CTX_wm_area(C);
	bool changed = false;

	if (old_screen->temp || (sa && sa->full && sa->full->temp)) {
		return false;
	}

	/* XXX new_type isn't necessarily usable */
	if (direction == 1) {
		new_type = BKE_workspace_layout_type_next_get(old_type);
	}
	else if (direction == -1) {
		new_type = BKE_workspace_layout_type_prev_get(old_type);
	}
	else {
		BLI_assert(0);
	}

	if (new_type && (new_type != old_type)) {
		for (wmWindow *win = wm->windows.first; win; win = win->next) {
			if (BKE_workspace_active_get(win->workspace_hook) == workspace) {
				WorkSpaceLayout *layout = BKE_workspace_layout_find_from_type(win->workspace_hook, new_type);
				BLI_assert(layout != NULL);
				bScreen *new_screen = BKE_workspace_layout_screen_get(layout);

				BKE_workspace_hook_active_layout_set(win->workspace_hook, layout);

				if (sa && sa->full) {
					/* return to previous state before switching screens */
					ED_screen_full_restore(C, sa); /* may free screen of old_layout */
				}

				ED_screen_change(C, new_screen);
				changed = true;
			}
		}

	}

	return changed;
}
