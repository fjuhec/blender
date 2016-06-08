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
 * The Original Code is Copyright (C) 2011 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Blender Foundation,
 *                 Tianwei Shen
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/tracking_correspondence.c
 *  \ingroup bke
 *
 * This file contains blender-side correspondence functions for witness camera support
 */

#include <limits.h>

#include "MEM_guardedalloc.h"
#include "DNA_movieclip_types.h"
#include "DNA_object_types.h"   /* SELECT */
#include "DNA_anim_types.h"

#include "BLI_utildefines.h"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_math.h"
#include "BLI_ghash.h"

#include "BKE_tracking.h"
#include "BKE_fcurve.h"
#include "BKE_movieclip.h"

#include "RNA_access.h"

#include "IMB_imbuf_types.h"

#include "libmv-capi.h"
#include "tracking_private.h"

struct ReconstructProgressData;

typedef struct MovieMultiviewReconstructContext {
	int clip_num;							/* number of clips in this reconstruction */
	struct libmv_TracksN **all_tracks;		/* set of tracks from all clips (API in autotrack) */
	// TODO(tianwei): might be proper to make it libmv_multiview_Reconstruction
	struct libmv_ReconstructionN **all_reconstruction;	/* reconstruction for each clip (API in autotrack) */
	libmv_CameraIntrinsicsOptions *all_camera_intrinsics_options;	/* camera intrinsic of each camera */
	TracksMap **all_tracks_map;				/* tracks_map of each clip */
	int *all_sfra, *all_efra;				/* start and end frame of each clip */
	ListBase *corr_base;			/* a set of correspondence across clips */

	bool select_keyframes;
	int keyframe1, keyframe2;		/* the key frames selected from the primary camera */
	int refine_flags;
	char object_name[MAX_NAME];
	bool is_camera;
	short motion_flag;
	float reprojection_error;		/* average reprojection error for all clips and tracks */
} MovieMultiviewReconstructContext;

typedef struct MultiviewReconstructProgressData {
	short *stop;
	short *do_update;
	float *progress;
	char *stats_message;
	int message_size;
} MultiviewReconstructProgressData;

/* Convert blender's multiview refinement flags to libmv's.
 * These refined flags would be the same as the single view version
 */
static int multiview_refine_intrinsics_get_flags(MovieTracking *tracking, MovieTrackingObject *object)
{
	int refine = tracking->settings.refine_camera_intrinsics;
	int flags = 0;

	if ((object->flag & TRACKING_OBJECT_CAMERA) == 0)
		return 0;

	if (refine & REFINE_FOCAL_LENGTH)
		flags |= LIBMV_REFINE_FOCAL_LENGTH;

	if (refine & REFINE_PRINCIPAL_POINT)
		flags |= LIBMV_REFINE_PRINCIPAL_POINT;

	if (refine & REFINE_RADIAL_DISTORTION_K1)
		flags |= LIBMV_REFINE_RADIAL_DISTORTION_K1;

	if (refine & REFINE_RADIAL_DISTORTION_K2)
		flags |= LIBMV_REFINE_RADIAL_DISTORTION_K2;

	return flags;
}

/* Create new libmv Tracks structure from blender's tracks list. */
static struct libmv_Tracks *libmv_multiview_tracks_new(MovieClip *clip, ListBase *tracksbase, int width, int height)
{
	int tracknr = 0;
	MovieTrackingTrack *track;
	struct libmv_Tracks *tracks = libmv_tracksNew();

	track = tracksbase->first;
	while (track) {
		FCurve *weight_fcurve;
		int a = 0;

		weight_fcurve = id_data_find_fcurve(&clip->id, track, &RNA_MovieTrackingTrack,
		                                    "weight", 0, NULL);

		for (a = 0; a < track->markersnr; a++) {
			MovieTrackingMarker *marker = &track->markers[a];

			if ((marker->flag & MARKER_DISABLED) == 0) {
				float weight = track->weight;

				if (weight_fcurve) {
					int scene_framenr =
						BKE_movieclip_remap_clip_to_scene_frame(clip, marker->framenr);
					weight = evaluate_fcurve(weight_fcurve, scene_framenr);
				}

				libmv_tracksInsert(tracks, marker->framenr, tracknr,
				                   (marker->pos[0] + track->offset[0]) * width,
				                   (marker->pos[1] + track->offset[1]) * height,
				                   weight);
			}
		}

		track = track->next;
		tracknr++;
	}

	return tracks;
}


/* Create context for camera/object motion reconstruction.
 * Copies all data needed for reconstruction from movie
 * clip datablock, so editing this clip is safe during
 * reconstruction job is in progress.
 */
MovieMultiviewReconstructContext *
BKE_tracking_multiview_reconstruction_context_new(MovieClip **clips,
                                                  int num_clips,
                                                  MovieTrackingObject *object,
                                                  int keyframe1, int keyframe2,
                                                  int width, int height)
{
	MovieMultiviewReconstructContext *context = MEM_callocN(sizeof(MovieMultiviewReconstructContext),
	                                                        "MRC data");
	// alloc memory for the field members
	context->all_tracks = MEM_callocN(num_clips * sizeof(libmv_TracksN*), "MRC libmv_Tracks");
	context->all_reconstruction = MEM_callocN(num_clips * sizeof(struct libmv_ReconstructionN*), "MRC libmv_Reconstruction");
	context->all_tracks_map = MEM_callocN(num_clips * sizeof(TracksMap*), "MRC TracksMap");
	context->all_camera_intrinsics_options = MEM_callocN(num_clips * sizeof(libmv_CameraIntrinsicsOptions), "MRC camera intrinsics");
	context->all_sfra = MEM_callocN(num_clips * sizeof(int), "MRC start frames");
	context->all_efra = MEM_callocN(num_clips * sizeof(int), "MRC end frames");
	context->keyframe1 = keyframe1;
	context->keyframe2 = keyframe2;
	context->clip_num = num_clips;

	for(int i = 0; i < num_clips; i++)
	{
		MovieClip *clip = clips[i];
		MovieTracking *tracking = &clip->tracking;
		ListBase *tracksbase = BKE_tracking_object_get_tracks(tracking, object);
		float aspy = 1.0f / tracking->camera.pixel_aspect;
		int num_tracks = BLI_listbase_count(tracksbase);
		if(i == 0)
			printf("%d tracks in the primary clip\n", num_tracks);
		else
			printf("%d tracks in the witness camera %d", num_tracks, i);
		int sfra = INT_MAX, efra = INT_MIN;
		MovieTrackingTrack *track;

		// setting context only from information in the primary clip
		if(i == 0) {
			// TODO(tianwei): make a proper correspondence DS in the libmv side
			context->corr_base = &tracking->correspondences;	// correspondences are recorded in the primary clip
			BLI_strncpy(context->object_name, object->name, sizeof(context->object_name));
			context->is_camera = object->flag & TRACKING_OBJECT_CAMERA;
			context->motion_flag = tracking->settings.motion_flag;
			context->select_keyframes =
			        (tracking->settings.reconstruction_flag & TRACKING_USE_KEYFRAME_SELECTION) != 0;
			context->refine_flags = multiview_refine_intrinsics_get_flags(tracking, object);
		}

		tracking_cameraIntrinscisOptionsFromTracking(tracking,
		                                             width, height,
		                                             &context->all_camera_intrinsics_options[i]);

		context->all_tracks_map[i] = tracks_map_new(context->object_name, context->is_camera, num_tracks, 0);

		track = tracksbase->first;
		while (track) {
			int first = 0, last = track->markersnr - 1;
			MovieTrackingMarker *first_marker = &track->markers[0];
			MovieTrackingMarker *last_marker = &track->markers[track->markersnr - 1];

			/* find first not-disabled marker */
			while (first <= track->markersnr - 1 && first_marker->flag & MARKER_DISABLED) {
				first++;
				first_marker++;
			}

			/* find last not-disabled marker */
			while (last >= 0 && last_marker->flag & MARKER_DISABLED) {
				last--;
				last_marker--;
			}

			if (first <= track->markersnr - 1)
				sfra = min_ii(sfra, first_marker->framenr);

			if (last >= 0)
				efra = max_ii(efra, last_marker->framenr);

			tracks_map_insert(context->all_tracks_map[i], track, NULL);

			track = track->next;
		}
		context->all_sfra[i] = sfra;
		context->all_efra[i] = efra;
		context->all_tracks[i] = libmv_multiview_tracks_new(clip, tracksbase, width, height * aspy);
	}

	return context;
}

/* Free memory used by a reconstruction process. */
void BKE_tracking_multiview_reconstruction_context_free(MovieMultiviewReconstructContext *context)
{
	for(int i = 0; i < context->clip_num; i++)
	{
		libmv_tracksDestroy(context->all_tracks[i]);
		if (context->all_reconstruction[i])
			libmv_reconstructionDestroy(context->all_reconstruction[i]);
		tracks_map_free(context->all_tracks_map[i], NULL);
	}
	MEM_freeN(context->all_tracks);
	MEM_freeN(context->all_reconstruction);
	MEM_freeN(context->all_camera_intrinsics_options);
	MEM_freeN(context->all_tracks_map);
	MEM_freeN(context->all_sfra);
	MEM_freeN(context->all_efra);

	MEM_freeN(context);
}


/* Fill in multiview reconstruction options structure from reconstruction context. */
static void reconstructionOptionsFromContext(libmv_ReconstructionOptions *reconstruction_options,
                                             MovieMultiviewReconstructContext *context)
{
	reconstruction_options->select_keyframes = context->select_keyframes;

	reconstruction_options->keyframe1 = context->keyframe1;
	reconstruction_options->keyframe2 = context->keyframe2;

	reconstruction_options->refine_intrinsics = context->refine_flags;
}

/* Callback which is called from libmv side to update progress in the interface. */
static void multiview_reconstruct_update_solve_cb(void *customdata, double progress, const char *message)
{
	MultiviewReconstructProgressData *progressdata = customdata;

	if (progressdata->progress) {
		*progressdata->progress = progress;
		*progressdata->do_update = true;
	}

	BLI_snprintf(progressdata->stats_message, progressdata->message_size, "Solving cameras | %s", message);
}

/* TODO(tianwei): Solve camera/object motion and reconstruct 3D markers position
 * from a prepared reconstruction context from multiple views.
 *
 * stop is not actually used at this moment, so reconstruction
 * job could not be stopped.
 *
 * do_update, progress and stat_message are set by reconstruction
 * callback in libmv side and passing to an interface.
 */
void BKE_tracking_multiview_reconstruction_solve(MovieMultiviewReconstructContext *context, short *stop, short *do_update,
                                                 float *progress, char *stats_message, int message_size)
{
	float error;

	MultiviewReconstructProgressData progressdata;

	libmv_ReconstructionOptions reconstruction_options;

	progressdata.stop = stop;
	progressdata.do_update = do_update;
	progressdata.progress = progress;
	progressdata.stats_message = stats_message;
	progressdata.message_size = message_size;

	//reconstructionOptionsFromContext(&reconstruction_options, context);

	//if (context->motion_flag & TRACKING_MOTION_MODAL) {
	//	context->reconstruction = libmv_solveModal(context->tracks,
	//	                                           &context->camera_intrinsics_options,
	//	                                           &reconstruction_options,
	//	                                           multiview_reconstruct_update_solve_cb, &progressdata);
	//}
	//else {
	//	context->reconstruction = libmv_solveReconstruction(context->tracks,
	//	                                                    &context->camera_intrinsics_options,
	//	                                                    &reconstruction_options,
	//	                                                    multiview_reconstruct_update_solve_cb, &progressdata);

	//	if (context->select_keyframes) {
	//		/* store actual keyframes used for reconstruction to update them in the interface later */
	//		context->keyframe1 = reconstruction_options.keyframe1;
	//		context->keyframe2 = reconstruction_options.keyframe2;
	//	}
	//}

	//error = libmv_reprojectionError(context->reconstruction);

	//context->reprojection_error = error;
	context->reprojection_error = 0;
}

/* TODO(tianwei): Finish multiview reconstruction process by copying reconstructed data
 * to the primary movie clip datablock.
 */
bool BKE_tracking_multiview_reconstruction_finish(MovieMultiviewReconstructContext *context, MovieTracking *tracking)
{
	MovieTrackingReconstruction *reconstruction;
	MovieTrackingObject *object;

	return false;
	//if (!libmv_reconstructionIsValid(context->all_reconstruction)) {
	//	printf("Failed solve the motion: most likely there are no good keyframes\n");
	//	return false;
	//}

	//tracks_map_merge(context->all_tracks_map, tracking);
	//BKE_tracking_dopesheet_tag_update(tracking);

	//object = BKE_tracking_object_get_named(tracking, context->object_name);

	//if (context->is_camera)
	//	reconstruction = &tracking->reconstruction;
	//else
	//	reconstruction = &object->reconstruction;

	///* update keyframe in the interface */
	//if (context->select_keyframes) {
	//	object->keyframe1 = context->keyframe1;
	//	object->keyframe2 = context->keyframe2;
	//}

	//reconstruction->error = context->reprojection_error;
	//reconstruction->flag |= TRACKING_RECONSTRUCTED;

	//if (!reconstruct_retrieve_libmv(context, tracking))
	//	return false;

	//return true;
}
