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

/** \file blender/windowmanager/intern/wm_hmd.c
 *  \ingroup wm
 *  \name Head Mounted Displays
 */

#include "BKE_context.h"
#include "BKE_screen.h"

#include "BLI_math.h"

#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"
#include "DNA_view3d_types.h"

#include "ED_screen.h"
#include "ED_view3d.h"

#include "GHOST_C-api.h"

#include "GPU_glew.h"

#include "MEM_guardedalloc.h"

#include "UI_resources.h"

#include "WM_api.h"
#include "WM_types.h"
#include "wm.h"
#include "wm_window.h"


/* -------------------------------------------------------------------- */
/* Device utilities (GHOST wrappers) */

int WM_HMD_num_devices_get(void)
{
	const int tot_devices = GHOST_HMDgetNumDevices();

#ifdef NDEBUG
	/* OpenHMD always places the dummy device last, we don't want to display it in non-debug builds. */
	return tot_devices - 1;
#else
	return tot_devices;
#endif
}

/**
 * Get index of currently open device.
 */
int WM_HMD_device_active_get(void)
{
	return GHOST_HMDgetOpenDeviceIndex();
}

const char *WM_HMD_device_name_get(int index)
{
	BLI_assert(index < MAX_HMD_DEVICES);
	return GHOST_HMDgetDeviceName(index);
}

const char *WM_HMD_device_vendor_get(int index)
{
	BLI_assert(index < MAX_HMD_DEVICES);
	return GHOST_HMDgetVendorName(index);
}

/**
 * Get IPD from currently opened HMD.
 */
float WM_HMD_device_IPD_get(void)
{
	return GHOST_HMDgetDeviceIPD();
}
void WM_HMD_device_IPD_set(float value)
{
	GHOST_HMDsetDeviceIPD(value);
}

float WM_HMD_device_lens_horizontal_separation_get(void)
{
	return GHOST_HMDgetLensHorizontalSeparation();
}

float WM_HMD_device_projection_z_near_get(void)
{
	return GHOST_HMDgetProjectionZNear();
}

float WM_HMD_device_projection_z_far_get(void)
{
	return GHOST_HMDgetProjectionZFar();
}

float WM_HMD_device_screen_horizontal_size_get(void)
{
	return GHOST_HMDgetScreenHorizontalSize();
}

/**
 * Enable or disable an HMD.
 */
void WM_HMD_device_state_set(const int device, const bool enable)
{
	BLI_assert(device < MAX_HMD_DEVICES);
	if (enable && (device >= 0)) {
		/* GHOST closes previously opened device if needed */
		GHOST_HMDopenDevice(device);
	}
	else {
		GHOST_HMDcloseDevice();
	}
}

void WM_HMD_device_modelview_matrix_get(const bool is_left, float r_modelviewmat[4][4])
{
	if (U.hmd_settings.device == -1) {
		unit_m4(r_modelviewmat);
	}
	else if (is_left) {
		GHOST_HMDgetLeftModelviewMatrix(r_modelviewmat);
	}
	else {
		GHOST_HMDgetRightModelviewMatrix(r_modelviewmat);
	}
}

float WM_HMD_device_FOV_get(const bool is_left)
{
	if (U.hmd_settings.device == -1) {
		return -1.0f;
	}
	else if (is_left) {
		return GHOST_HMDgetLeftEyeFOV();
	}
	else {
		return GHOST_HMDgetRightEyeFOV();
	}
}

void *WM_HMD_device_distortion_parameters_get(void)
{
	return GHOST_HMDgetDistortionParameters();
}


/* -------------------------------------------------------------------- */
/* Operators */

#define HMD_ITER_MIRRORED_3D_VIEW_REGIONS_START(wm, name_suffix) \
	for (wmWindow *win##name_suffix = wm->windows.first; \
	     win##name_suffix; \
	     win##name_suffix = win##name_suffix->next) \
	{ \
		for (ScrArea *sa##name_suffix = win##name_suffix->screen->areabase.first; \
		     sa##name_suffix; \
		     sa##name_suffix = sa##name_suffix->next) \
		{ \
			if (sa##name_suffix->spacetype == SPACE_VIEW3D) { \
				View3D *v3d##name_suffix = sa##name_suffix->spacedata.first; \
				if (v3d##name_suffix->flag3 & V3D_SHOW_HMD_MIRROR) { \
					for (ARegion *ar##name_suffix = sa##name_suffix->regionbase.first; \
					     ar##name_suffix; \
					     ar##name_suffix = ar##name_suffix->next) \
					{ \
						if (ar##name_suffix->regiontype == RGN_TYPE_WINDOW) { \
							RegionView3D *rv3d##name_suffix = ar##name_suffix->regiondata;
#define HMD_ITER_MIRRORED_3D_VIEW_REGIONS_END \
						} \
					} \
				} \
			} \
		} \
	} (void)0


static void hmd_session_enable_mirrored_viewlocks(const wmWindowManager *wm)
{
	HMD_ITER_MIRRORED_3D_VIEW_REGIONS_START(wm, )
	{
		rv3d->viewlock |= RV3D_LOCKED_SHARED;
		ED_region_tag_redraw(ar);
	}
	HMD_ITER_MIRRORED_3D_VIEW_REGIONS_END;
}
static void hmd_session_disable_mirrored_viewlocks(const wmWindowManager *wm)
{
	HMD_ITER_MIRRORED_3D_VIEW_REGIONS_START(wm, )
	{
		if (RV3D_IS_LOCKED_SHARED(rv3d)) {
			rv3d->viewlock &= ~RV3D_LOCKED_SHARED;
			ED_region_tag_redraw(ar);
		}
	}
	HMD_ITER_MIRRORED_3D_VIEW_REGIONS_END;
}

static void hmd_view_prepare_screen(wmWindowManager *wm, wmWindow *win, const RegionView3D *rv3d_current)
{
	ScrArea *sa_hmd = win->screen->areabase.first;
	View3D *v3d_hmd = sa_hmd->spacedata.first;
	RegionView3D *rv3d_hmd = BKE_area_find_region_type(sa_hmd, RGN_TYPE_WINDOW)->regiondata;

	BLI_assert(sa_hmd->spacetype == SPACE_VIEW3D);

	/* sync view options */
	v3d_hmd->drawtype = wm->hmd_view.view_shade;
	/* copy view orientation from current 3D view to newly opened HMD view */
	ED_view3d_copy_region_view_data(rv3d_current, rv3d_hmd);
}

static void hmd_session_prepare_screen(const wmWindowManager *wm, wmWindow *hmd_win)
{
	ScrArea *sa = hmd_win->screen->areabase.first;
	View3D *v3d = sa->spacedata.first;
	ARegion *ar = BKE_area_find_region_type(sa, RGN_TYPE_WINDOW);
	RegionView3D *rv3d = ar->regiondata;
	BLI_assert(ar && sa->spacetype == SPACE_VIEW3D);

	v3d->fx_settings.fx_flag |= GPU_FX_FLAG_LensDist;
	/* Set distortion type for 3D View but first we need to validate fx settings. */
	BKE_screen_gpu_fx_validate(&v3d->fx_settings);

	if (rv3d->persp == RV3D_ORTHO) {
		rv3d->persp = RV3D_PERSP;
	}
	rv3d->viewlock |= RV3D_LOCK_PERSP_VIEW;
	hmd_session_enable_mirrored_viewlocks(wm);

	hmd_win->screen->is_hmd_running = true;
}

static void hmd_session_cursor_draw(bContext *C, int mx, int my, void *UNUSED(customdata))
{
	wmWindow *win = CTX_wm_window(C);
	GLUquadricObj *qobj;

	if (!WM_window_is_running_hmd_view(win)) {
		/* only hmd window */
		return;
	}
	WM_cursor_modal_set(win, CURSOR_NONE);

	qobj = gluNewQuadric();

	UI_ThemeColor(TH_TEXT_HI);

	glPushMatrix();
	glTranslatef(mx, my, 0.0f);

	gluQuadricDrawStyle(qobj, GLU_FILL);
	gluDisk(qobj, 0.0f, 4.0f, 16, 1);

	glPopMatrix();
	gluDeleteQuadric(qobj);
}

static void hmd_session_start(wmWindowManager *wm)
{
	wmWindow *hmd_win = wm->hmd_view.hmd_win;

	/* device setup */
	WM_HMD_device_state_set(U.hmd_settings.device, true);
	if ((U.hmd_settings.flag & USER_HMD_USE_DEVICE_IPD) == 0) {
		U.hmd_settings.init_ipd = WM_HMD_device_IPD_get();
		WM_HMD_device_IPD_set(U.hmd_settings.custom_ipd);
	}

	hmd_session_prepare_screen(wm, hmd_win);
	WM_window_fullscreen_toggle(hmd_win, true, false);

	wm->hmd_view.cursor = WM_paint_cursor_activate(wm, NULL, hmd_session_cursor_draw, NULL);
}
static void hmd_session_exit(wmWindowManager *wm, const bool skip_window_unset)
{
	wmWindow *hmd_win = wm->hmd_view.hmd_win;
	ScrArea *sa = hmd_win->screen->areabase.first;
	View3D *v3d = sa->spacedata.first;
	BLI_assert(sa->spacetype == SPACE_VIEW3D);

	/* screen */
	hmd_win->screen->is_hmd_running = false;
	if (!skip_window_unset) {
		v3d->fx_settings.fx_flag &= ~GPU_FX_FLAG_LensDist;
		MEM_SAFE_FREE(v3d->fx_settings.lensdist);
		WM_window_fullscreen_toggle(hmd_win, false, true);
	}
	hmd_session_disable_mirrored_viewlocks(wm);

	/* cursor */
	WM_cursor_modal_restore(hmd_win);
	WM_paint_cursor_end(wm, wm->hmd_view.cursor);

	/* deactivate HMD */
	WM_HMD_device_state_set(U.hmd_settings.device, false);
}

void wm_hmd_view_close(wmWindowManager *wm)
{
	hmd_session_exit(wm, true);
	wm->hmd_view.hmd_win = NULL;
}

static int wm_hmd_view_toggle_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *UNUSED(event))
{
	wmWindow *prevwin = CTX_wm_window(C);
	wmWindowManager *wm = CTX_wm_manager(C);
	wmWindow *hmd_win = wm->hmd_view.hmd_win;

	/* close */
	if (hmd_win) {
		/* calls wm_hmd_view_close */
		wm_window_close(C, wm, hmd_win);
	}
	/* open */
	else {
		ARegion *ar_current;
		View3D *v3d_current;
		RegionView3D *rv3d_current;

		rcti rect = {prevwin->posx, prevwin->posx + (int)(prevwin->sizex * 0.9f),
		             prevwin->posy, prevwin->posy + (int)(prevwin->sizey * 0.9f)};

		/* WM_window_open_restricted changes context, so get current context data first */
		ED_view3d_context_user_region(C, &v3d_current, &ar_current);
		rv3d_current = ar_current->regiondata;
		BLI_assert(v3d_current && ar_current && rv3d_current);

		hmd_win = WM_window_open_restricted(C, &rect, WM_WINDOW_HMD);
		wm->hmd_view.hmd_win = hmd_win;

		hmd_view_prepare_screen(wm, hmd_win, rv3d_current);
	}

	return OPERATOR_FINISHED;
}

void WM_OT_hmd_view_toggle(wmOperatorType *ot)
{
	ot->name = "Open/Close HMD View Window";
	ot->idname = "WM_OT_hmd_view_toggle";
	ot->description = "Open/Close a separate window for a head mounted display";

	ot->invoke = wm_hmd_view_toggle_invoke;
}

static int hmd_session_toggle_poll(bContext *C)
{
	wmWindowManager *wm = CTX_wm_manager(C);

	if (!wm->hmd_view.hmd_win) {
		CTX_wm_operator_poll_msg_set(C, "Open a HMD window first");
		return false;
	}

	return true;
}

static int hmd_session_toggle_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *UNUSED(event))
{
	wmWindowManager *wm = CTX_wm_manager(C);
	wmWindow *hmd_win = wm->hmd_view.hmd_win;

	if (!hmd_win) {
		return (OPERATOR_CANCELLED | OPERATOR_PASS_THROUGH);
	}

	if (WM_window_is_running_hmd_view(hmd_win)) {
		hmd_session_exit(wm, false);
	}
	else {
		hmd_session_start(wm);
	}

	return OPERATOR_FINISHED;
}

void WM_OT_hmd_session_toggle(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Run HMD Session";
	ot->description = "Start/Stop a head mounted display session";
	ot->idname = "WM_OT_hmd_session_run";

	/* api callbacks */
	ot->invoke = hmd_session_toggle_invoke;
	ot->poll = hmd_session_toggle_poll;
}

static int hmd_session_refresh_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *UNUSED(event))
{
	wmWindowManager *wm = CTX_wm_manager(C);
	wmWindow *hmd_win = wm->hmd_view.hmd_win;
	ScrArea *sa;

	if (!(hmd_win && WM_window_is_running_hmd_view(hmd_win))) {
		return OPERATOR_CANCELLED;
	}

	sa = hmd_win->screen->areabase.first;
	BLI_assert(sa->spacetype == SPACE_VIEW3D);
	/* Actually the only thing we have to do is ensuring a redraw, we'll then
	 * get the modelview/projection matrices from HMD device when drawing */
	ED_area_tag_redraw(sa);
	/* Make sure running modal operators can update their drawing for changed
	 * view (without having to listen to HMD transform event themselves) */
	WM_event_add_mousemove(C);

	/* Tag mirrored 3D views for redraw too */
	HMD_ITER_MIRRORED_3D_VIEW_REGIONS_START(wm, _iter)
	{
		if (RV3D_IS_LOCKED_SHARED(rv3d_iter)) {
			/* this rv3d shares data with the HMD view */
			ED_region_tag_redraw(ar_iter);
		}
	}
	HMD_ITER_MIRRORED_3D_VIEW_REGIONS_END;

	return OPERATOR_FINISHED;
}

void WM_OT_hmd_session_refresh(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Refresh HMD Session";
	ot->description = "Refresh data for a head mounted display (virtual reality) session";
	ot->idname = "WM_OT_hmd_session_refresh";

	/* api callbacks */
	ot->invoke = hmd_session_refresh_invoke;

	/* flags */
	ot->flag = OPTYPE_INTERNAL;
}

#undef HMD_ITER_MIRRORED_3D_VIEW_REGIONS_START
#undef HMD_ITER_MIRRORED_3D_VIEW_REGIONS_END
