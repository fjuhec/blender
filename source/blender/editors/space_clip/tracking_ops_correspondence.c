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

#include "BLI_utildefines.h"
#include "BLI_ghash.h"
#include "BLI_math.h"
#include "BLI_blenlib.h"

#include "BKE_context.h"
#include "BKE_movieclip.h"
#include "BKE_tracking.h"
#include "BKE_depsgraph.h"
#include "BKE_report.h"
#include "BKE_sound.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_screen.h"
#include "ED_clip.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "BLT_translation.h"

#include "clip_intern.h"
#include "tracking_ops_intern.h"

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

	// link two tracks

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
