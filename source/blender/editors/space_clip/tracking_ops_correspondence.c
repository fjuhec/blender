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
 *
 * Contributor(s): Blender Foundation,
 *                 Tianwei Shen
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/space_clip/tracking_ops_correspondence.c
 *  \ingroup spclip
 */

#include "MEM_guardedalloc.h"

#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_camera_types.h"
#include "DNA_object_types.h"

#include "BLI_utildefines.h"
#include "BLI_ghash.h"
#include "BLI_math.h"
#include "BLI_blenlib.h"
#include "BLI_string.h"

#include "BKE_context.h"
#include "BKE_movieclip.h"
#include "BKE_tracking.h"
#include "BKE_depsgraph.h"
#include "BKE_report.h"
#include "BKE_global.h"
#include "BKE_library.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_screen.h"
#include "ED_clip.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "BLT_translation.h"

#include "clip_intern.h"
#include "tracking_ops_intern.h"

// TODO(tianwei): may move these functions to tracking.c in the future
/* Ensure specified correspondence has got unique name,
 * if it's not name of specified correspondence will be changed
 * keeping names of all other correspondence unchanged.
 */
void BKE_tracking_correspondence_unique_name(ListBase *tracksbase, MovieTrackingCorrespondence *corr)
{
	BLI_uniquename(tracksbase, corr, CTX_DATA_(BLT_I18NCONTEXT_ID_MOVIECLIP, "Correspondence"), '.',
	               offsetof(MovieTrackingCorrespondence, name), sizeof(corr->name));
}

/* Add new correspondence to a specified correspondence base.
 */
MovieTrackingCorrespondence *BKE_tracking_correspondence_add(MovieTracking *tracking, ListBase *corr_base,
                                                             MovieTrackingTrack *primary_track,
                                                             MovieTrackingTrack *witness_track)
{
	MovieTrackingCorrespondence *corr;

	corr = MEM_callocN(sizeof(MovieTrackingCorrespondence), "add correspondence");
	strcpy(corr->name, "Correspondence");
	corr->primary_track = primary_track;
	corr->witness_track = witness_track;

	BLI_addtail(corr_base, corr);
	BKE_tracking_correspondence_unique_name(corr_base, corr);

	return corr;
}

/********************** add correspondence operator *********************/

static int add_correspondence_exec(bContext *C, wmOperator *op)
{
	SpaceClip *sc = CTX_wm_space_clip(C);
	MovieClip *clip = ED_space_clip_get_clip(sc);
	MovieTracking *tracking = &clip->tracking;
	ListBase *tracksbase = BKE_tracking_get_active_tracks(tracking);

	// get one track from each clip and link them
	MovieTrackingTrack *primary_track = NULL, *witness_track = NULL;
	MovieTrackingTrack *track;
	int num_primary_selected = 0, num_witness_selected = 0;

	// get number of selected tracks in the primary camera
	for (track = tracksbase->first; track; track = track->next) {
		if (TRACK_VIEW_SELECTED(sc, track)) {
			primary_track = track;
			num_primary_selected++;
		}
	}

	// get number of selected tracks in the witness camera
	// TODO(tianwei): there might be multiple witness cameras, now just work with one witness camera
	wmWindow *window = CTX_wm_window(C);
	for (ScrArea *sa = window->screen->areabase.first; sa != NULL; sa = sa->next) {
		if (sa->spacetype == SPACE_CLIP) {
			SpaceClip *second_sc = sa->spacedata.first;
			if(second_sc != sc) {
				MovieClip *second_clip = ED_space_clip_get_clip(second_sc);
				MovieTracking *second_tracking = &second_clip->tracking;
				ListBase *second_tracksbase = BKE_tracking_get_active_tracks(second_tracking);
				for (track = second_tracksbase->first; track; track = track->next) {
					if (TRACK_VIEW_SELECTED(second_sc, track)) {
						witness_track = track;
						num_witness_selected++;
					}
				}
			}
		}
	}

	if(!primary_track || !witness_track || num_primary_selected != 1 || num_witness_selected != 1) {
		BKE_report(op->reports, RPT_ERROR, "Select exactly one track in each clip");
		return OPERATOR_CANCELLED;
	}

	// TODO(tianwei): link two tracks, mark these two tracks in a different color

	// add these correspondence
	BKE_tracking_correspondence_add(tracking, &(tracking->correspondences), primary_track, witness_track);

	return OPERATOR_FINISHED;
}

void CLIP_OT_add_correspondence(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Correspondence";
	ot->idname = "CLIP_OT_add_correspondence";
	ot->description = "Add correspondence between primary camera and witness camera";

	/* api callbacks */
	ot->exec = add_correspondence_exec;
	ot->poll = ED_space_clip_tracking_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/********************** delete correspondence operator *********************/

static int delete_correspondence_exec(bContext *C, wmOperator *UNUSED(op))
{
	SpaceClip *sc = CTX_wm_space_clip(C);
	MovieClip *clip = ED_space_clip_get_clip(sc);
	MovieTracking *tracking = &clip->tracking;
	bool changed = false;

	/* Delete selected plane tracks. */
	ListBase *plane_tracks_base = BKE_tracking_get_active_plane_tracks(tracking);
	for (MovieTrackingPlaneTrack *plane_track = plane_tracks_base->first,
	                             *next_plane_track;
	     plane_track != NULL;
	     plane_track = next_plane_track)
	{
		next_plane_track = plane_track->next;

		if (PLANE_TRACK_VIEW_SELECTED(plane_track)) {
			BKE_tracking_plane_track_free(plane_track);
			BLI_freelinkN(plane_tracks_base, plane_track);
			changed = true;
		}
	}

	/* Remove selected point tracks (they'll also be removed from planes which
	 * uses them).
	 */
	ListBase *tracksbase = BKE_tracking_get_active_tracks(tracking);
	for (MovieTrackingTrack *track = tracksbase->first, *next_track;
	     track != NULL;
	     track = next_track)
	{
		next_track = track->next;
		if (TRACK_VIEW_SELECTED(sc, track)) {
			clip_delete_track(C, clip, track);
			changed = true;
		}
	}

	/* Nothing selected now, unlock view so it can be scrolled nice again. */
	sc->flag &= ~SC_LOCK_SELECTION;

	if (changed) {
		WM_event_add_notifier(C, NC_MOVIECLIP | NA_EDITED, clip);
	}

	return OPERATOR_FINISHED;
}

void CLIP_OT_delete_correspondence(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Delete Correspondence";
	ot->idname = "CLIP_OT_delete_correspondence";
	ot->description = "Delete selected tracker correspondene between primary and witness camera";

	/* api callbacks */
	ot->invoke = WM_operator_confirm;
	ot->exec = delete_correspondence_exec;
	ot->poll = ED_space_clip_tracking_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/********************** solve multiview operator *********************/
typedef struct {
	Scene *scene;
	MovieClip *clip;
	MovieClipUser user;

	ReportList *reports;

	char stats_message[256];

	struct MovieReconstructContext *context;
} SolveMultiviewJob;

static bool solve_multiview_initjob(bContext *C,
                                 SolveMultiviewJob *scj,
                                 wmOperator *op,
                                 char *error_msg,
                                 int max_error)
{
	SpaceClip *sc = CTX_wm_space_clip(C);
	MovieClip *clip = ED_space_clip_get_clip(sc);
	Scene *scene = CTX_data_scene(C);
	MovieTracking *tracking = &clip->tracking;
	MovieTrackingObject *object = BKE_tracking_object_get_active(tracking);
	int width, height;

	if (!BKE_tracking_reconstruction_check(tracking,
	                                       object,
	                                       error_msg,
	                                       max_error))
	{
		return false;
	}

	/* Could fail if footage uses images with different sizes. */
	BKE_movieclip_get_size(clip, &sc->user, &width, &height);

	scj->clip = clip;
	scj->scene = scene;
	scj->reports = op->reports;
	scj->user = sc->user;

	scj->context = BKE_tracking_reconstruction_context_new(clip,
	                                                       object,
	                                                       object->keyframe1,
	                                                       object->keyframe2,
	                                                       width,
	                                                       height);

	tracking->stats = MEM_callocN(sizeof(MovieTrackingStats), "solve multiview stats");

	return true;
}

static void solve_multiview_updatejob(void *scv)
{
	SolveMultiviewJob *scj = (SolveMultiviewJob *)scv;
	MovieTracking *tracking = &scj->clip->tracking;

	BLI_strncpy(tracking->stats->message,
	            scj->stats_message,
	            sizeof(tracking->stats->message));
}

static void solve_multiview_startjob(void *scv, short *stop, short *do_update, float *progress)
{
	SolveMultiviewJob *scj = (SolveMultiviewJob *)scv;
	BKE_tracking_reconstruction_solve(scj->context,
	                                  stop,
	                                  do_update,
	                                  progress,
	                                  scj->stats_message,
	                                  sizeof(scj->stats_message));
}

static void solve_multiview_freejob(void *scv)
{
	SolveMultiviewJob *scj = (SolveMultiviewJob *)scv;
	MovieTracking *tracking = &scj->clip->tracking;
	Scene *scene = scj->scene;
	MovieClip *clip = scj->clip;
	int solved;

	if (!scj->context) {
		/* job weren't fully initialized due to some error */
		MEM_freeN(scj);
		return;
	}

	solved = BKE_tracking_reconstruction_finish(scj->context, tracking);
	if (!solved) {
		BKE_report(scj->reports,
		           RPT_WARNING,
		           "Some data failed to reconstruct (see console for details)");
	}
	else {
		BKE_reportf(scj->reports,
		            RPT_INFO,
		            "Average re-projection error: %.3f",
		            tracking->reconstruction.error);
	}

	/* Set currently solved clip as active for scene. */
	if (scene->clip != NULL) {
		id_us_min(&clip->id);
	}
	scene->clip = clip;
	id_us_plus(&clip->id);

	/* Set blender camera focal length so result would look fine there. */
	if (scene->camera != NULL &&
	    scene->camera->data &&
	    GS(((ID *) scene->camera->data)->name) == ID_CA)
	{
		Camera *camera = (Camera *)scene->camera->data;
		int width, height;
		BKE_movieclip_get_size(clip, &scj->user, &width, &height);
		BKE_tracking_camera_to_blender(tracking, scene, camera, width, height);
		WM_main_add_notifier(NC_OBJECT, camera);
	}

	MEM_freeN(tracking->stats);
	tracking->stats = NULL;

	DAG_id_tag_update(&clip->id, 0);

	WM_main_add_notifier(NC_MOVIECLIP | NA_EVALUATED, clip);
	WM_main_add_notifier(NC_OBJECT | ND_TRANSFORM, NULL);

	/* Update active clip displayed in scene buttons. */
	WM_main_add_notifier(NC_SCENE, scene);

	BKE_tracking_reconstruction_context_free(scj->context);
	MEM_freeN(scj);
}

static int solve_multiview_exec(bContext *C, wmOperator *op)
{
	SolveMultiviewJob *scj;
	char error_msg[256] = "\0";
	scj = MEM_callocN(sizeof(SolveMultiviewJob), "SolveMultiviewJob data");
	if (!solve_multiview_initjob(C, scj, op, error_msg, sizeof(error_msg))) {
		if (error_msg[0]) {
			BKE_report(op->reports, RPT_ERROR, error_msg);
		}
		solve_multiview_freejob(scj);
		return OPERATOR_CANCELLED;
	}
	solve_multiview_startjob(scj, NULL, NULL, NULL);
	solve_multiview_freejob(scj);
	return OPERATOR_FINISHED;
}

static int solve_multiview_invoke(bContext *C,
                                  wmOperator *op,
                                  const wmEvent *UNUSED(event))
{
	SolveMultiviewJob *scj;
	ScrArea *sa = CTX_wm_area(C);
	SpaceClip *sc = CTX_wm_space_clip(C);
	MovieClip *clip = ED_space_clip_get_clip(sc);
	MovieTracking *tracking = &clip->tracking;
	MovieTrackingReconstruction *reconstruction =
	        BKE_tracking_get_active_reconstruction(tracking);
	wmJob *wm_job;
	char error_msg[256] = "\0";

	if (WM_jobs_test(CTX_wm_manager(C), sa, WM_JOB_TYPE_ANY)) {
		/* only one solve is allowed at a time */
		return OPERATOR_CANCELLED;
	}

	scj = MEM_callocN(sizeof(SolveMultiviewJob), "SolveCameraJob data");
	if (!solve_multiview_initjob(C, scj, op, error_msg, sizeof(error_msg))) {
		if (error_msg[0]) {
			BKE_report(op->reports, RPT_ERROR, error_msg);
		}
		solve_multiview_freejob(scj);
		return OPERATOR_CANCELLED;
	}

	BLI_strncpy(tracking->stats->message,
	            "Solving multiview| Preparing solve",
	            sizeof(tracking->stats->message));

	/* Hide reconstruction statistics from previous solve. */
	reconstruction->flag &= ~TRACKING_RECONSTRUCTED;
	WM_event_add_notifier(C, NC_MOVIECLIP | NA_EVALUATED, clip);

	/* Setup job. */
	wm_job = WM_jobs_get(CTX_wm_manager(C), CTX_wm_window(C), sa, "Solve Camera",
	                     WM_JOB_PROGRESS, WM_JOB_TYPE_CLIP_SOLVE_CAMERA);
	WM_jobs_customdata_set(wm_job, scj, solve_multiview_freejob);
	WM_jobs_timer(wm_job, 0.1, NC_MOVIECLIP | NA_EVALUATED, 0);
	WM_jobs_callbacks(wm_job,
	                  solve_multiview_startjob,
	                  NULL,
	                  solve_multiview_updatejob,
	                  NULL);

	G.is_break = false;

	WM_jobs_start(CTX_wm_manager(C), wm_job);
	WM_cursor_wait(0);

	/* add modal handler for ESC */
	WM_event_add_modal_handler(C, op);

	return OPERATOR_RUNNING_MODAL;
}

static int solve_multiview_modal(bContext *C,
                                 wmOperator *UNUSED(op),
                                 const wmEvent *event)
{
	/* No running solver, remove handler and pass through. */
	if (0 == WM_jobs_test(CTX_wm_manager(C), CTX_wm_area(C), WM_JOB_TYPE_ANY))
		return OPERATOR_FINISHED | OPERATOR_PASS_THROUGH;

	/* Running solver. */
	switch (event->type) {
		case ESCKEY:
			return OPERATOR_RUNNING_MODAL;
	}

	return OPERATOR_PASS_THROUGH;
}

void CLIP_OT_solve_multiview(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Solve multi-view reconstruction";
	ot->idname = "CLIP_OT_solve_multiview";
	ot->description = "Solve multiview reconstruction";

	/* api callbacks */
	ot->exec = solve_multiview_exec;
	ot->invoke = solve_multiview_invoke;
	ot->modal = solve_multiview_modal;
	ot->poll = ED_space_clip_tracking_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
