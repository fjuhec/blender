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
#include "BLT_translation.h"

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
	int *all_refine_flags;					/* refine flags of each clip */
	int **track_global_index;				/* track global index */
	struct libmv_CorrespondencesN *correspondences;			/* libmv correspondence api*/

	bool select_keyframes;
	int keyframe1, keyframe2;		/* the key frames selected from the primary camera */
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

/* Add new correspondence to a specified correspondence base.
 */
MovieTrackingCorrespondence *BKE_tracking_correspondence_add(ListBase *corr_base,
                                                             MovieTrackingTrack *self_track,
                                                             MovieTrackingTrack *other_track,
                                                             MovieClip* self_clip,
                                                             MovieClip* other_clip,
                                                             char *error_msg, int error_size)
{
	MovieTrackingCorrespondence *corr = NULL;
	// check self correspondences
	if (self_track == other_track) {
		BLI_strncpy(error_msg, N_("Cannot link a track to itself"), error_size);
		return NULL;
	}
	// check duplicate correspondences or conflict correspondence
	for (corr = corr_base->first; corr != NULL; corr = corr->next)
	{
		if (corr->self_clip == self_clip && corr->self_track == self_track) {
			// duplicate correspondences
			if (corr->other_clip == other_clip && corr->other_track == other_track) {
				BLI_strncpy(error_msg, N_("This correspondence has been added"), error_size);
				return NULL;
			}
			// conflict correspondence
			else {
				BLI_strncpy(error_msg, N_("Conflict correspondence, consider first deleting the old one"), error_size);
				return NULL;
			}
		}
		if (corr->other_clip == other_clip && corr->other_track == other_track) {
			if (corr->self_clip == self_clip && corr->self_track == self_track) {
				BLI_strncpy(error_msg, N_("This correspondence has been added"), error_size);
				return NULL;
			}
			else {
				BLI_strncpy(error_msg, N_("Conflict correspondence, consider first deleting the old one"), error_size);
				return NULL;
			}
		}
	}

	corr = MEM_callocN(sizeof(MovieTrackingCorrespondence), "add correspondence");
	strcpy(corr->name, "Correspondence");
	corr->self_track = self_track;
	corr->other_track = other_track;
	corr->self_clip = self_clip;
	corr->other_clip = other_clip;

	BLI_addtail(corr_base, corr);
	BKE_tracking_correspondence_unique_name(corr_base, corr);

	return corr;
}

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
static struct libmv_TracksN *libmv_multiview_tracks_new(MovieClip *clip, int clip_id, ListBase *tracksbase,
                                                        int *global_track_index, int width, int height)
{
	int tracknr = 0;
	MovieTrackingTrack *track;
	struct libmv_TracksN *tracks = libmv_tracksNewN();

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

				libmv_Marker libmv_marker;
				libmv_marker.clip = clip_id;
				libmv_marker.frame = marker->framenr;
				libmv_marker.track = global_track_index[tracknr];
				libmv_marker.center[0] = (marker->pos[0] + track->offset[0]) * width;
				libmv_marker.center[1] = (marker->pos[1] + track->offset[1]) * height;
				for (int i = 0; i < 4; i++) {
					libmv_marker.patch[i][0] = marker->pattern_corners[i][0];
					libmv_marker.patch[i][1] = marker->pattern_corners[i][1];
				}
				for (int i = 0; i < 2; i++) {
					libmv_marker.search_region_min[i] = marker->search_min[i];
					libmv_marker.search_region_max[i] = marker->search_max[i];
				}
				libmv_marker.weight = weight;

				// the following the unused in the current pipeline
				// TODO(tianwei): figure out how to fill in reference clip and frame
				if (marker->flag & MARKER_TRACKED) {
					libmv_marker.source = LIBMV_MARKER_SOURCE_TRACKED;
				}
				else {
					libmv_marker.source = LIBMV_MARKER_SOURCE_MANUAL;
				}
				libmv_marker.status = LIBMV_MARKER_STATUS_UNKNOWN;
				libmv_marker.reference_clip = clip_id;
				libmv_marker.reference_frame = -1;

				libmv_marker.model_type = LIBMV_MARKER_MODEL_TYPE_POINT;
				libmv_marker.model_id = 0;
				libmv_marker.disabled_channels =
				        ((track->flag & TRACK_DISABLE_RED)   ? LIBMV_MARKER_CHANNEL_R : 0) |
				        ((track->flag & TRACK_DISABLE_GREEN) ? LIBMV_MARKER_CHANNEL_G : 0) |
				        ((track->flag & TRACK_DISABLE_BLUE)  ? LIBMV_MARKER_CHANNEL_B : 0);

				//TODO(tianwei): why some framenr is negative????
				if (clip_id < 0 || marker->framenr < 0)
					continue;
				libmv_tracksAddMarkerN(tracks, &libmv_marker);
			}
		}

		track = track->next;
		tracknr++;
	}

	return tracks;
}

/* get correspondences from blender tracking to libmv correspondences
 * return the number of correspondences converted
 */
static int libmv_CorrespondencesFromTracking(ListBase *tracking_correspondences,
                                             MovieClip **clips,
                                             const int clip_num,
                                             struct libmv_CorrespondencesN *libmv_correspondences,
                                             int **global_track_index)
{
	int num_valid_corrs = 0;
	MovieTrackingCorrespondence *corr;
	corr = tracking_correspondences->first;
	while (corr) {
		printf("enter corr\n");
		int clip1 = -1, clip2 = -1, track1 = -1, track2 = -1;
		MovieClip *self_clip = corr->self_clip;
		MovieClip *other_clip = corr->other_clip;
		// iterate through all the clips to get the local clip id
		for (int i = 0; i < clip_num; i++) {
			MovieTracking *tracking = &clips[i]->tracking;
			ListBase *tracksbase = &tracking->tracks;
			MovieTrackingTrack *track = tracksbase->first;
			int tracknr = 0;
			// check primary clip
			if (self_clip == clips[i]) {
				clip1 = i;
				while (track) {
					if (corr->self_track == track) {
						track1 = tracknr;
						break;
					}
					track = track->next;
					tracknr++;
				}
			}
			// check witness clip
			if (other_clip == clips[i]) {
				clip2 = i;
				while (track) {
					if (corr->other_track == track) {
						track2 = tracknr;
						break;
					}
					track = track->next;
					tracknr++;
				}
			}
		}
		if (clip1 != -1 && clip2 != -1 && track1 != -1 && track2 != -1 && clip1 != clip2) {
			libmv_AddCorrespondenceN(libmv_correspondences, clip1, clip2, track1, track2);
			num_valid_corrs++;
		}
		// change the global index of clip2-track2 to clip1-track1
		global_track_index[clip2][track2] = global_track_index[clip1][track1];
		corr = corr->next;
	}
	return num_valid_corrs;
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
	context->all_reconstruction = MEM_callocN(num_clips * sizeof(struct libmv_ReconstructionN*), "MRC libmv reconstructions");
	context->correspondences = libmv_correspondencesNewN();
	context->all_tracks_map = MEM_callocN(num_clips * sizeof(TracksMap*), "MRC TracksMap");
	context->all_camera_intrinsics_options = MEM_callocN(num_clips * sizeof(libmv_CameraIntrinsicsOptions), "MRC camera intrinsics");
	context->all_sfra = MEM_callocN(num_clips * sizeof(int), "MRC start frames");
	context->all_efra = MEM_callocN(num_clips * sizeof(int), "MRC end frames");
	context->all_refine_flags = MEM_callocN(num_clips * sizeof(int), "MRC refine flags");
	context->keyframe1 = keyframe1;
	context->keyframe2 = keyframe2;
	context->clip_num = num_clips;

	// initial global track index to [0,..., N1 - 1], [N1,..., N1+N2-1], so on and so forth
	context->track_global_index = MEM_callocN(num_clips * sizeof(int*), "global track index of each clip");
	int global_index = 0;
	for (int i = 0; i < num_clips; i++) {
		MovieClip *clip = clips[i];
		MovieTracking *tracking = &clip->tracking;
		ListBase *tracksbase = BKE_tracking_object_get_tracks(tracking, object);
		int num_tracks = BLI_listbase_count(tracksbase);
		context->track_global_index[i] = MEM_callocN(num_tracks * sizeof(int), "global track index for clip i");
		for (int j = 0; j < num_tracks; j++) {
			context->track_global_index[i][j] = global_index++;
		}
	}

	for (int i = 0; i < num_clips; i++) {
		MovieClip *clip = clips[i];
		MovieTracking *tracking = &clip->tracking;
		ListBase *tracksbase = BKE_tracking_object_get_tracks(tracking, object);
		float aspy = 1.0f / tracking->camera.pixel_aspect;
		int num_tracks = BLI_listbase_count(tracksbase);
		if (i == 0)
			printf("%d tracks in the primary clip 0\n", num_tracks);
		else
			printf("%d tracks in the witness camera %d\n", num_tracks, i);
		int sfra = INT_MAX, efra = INT_MIN;
		MovieTrackingTrack *track;

		// setting context only from information in the primary clip
		if(i == 0) {
			// correspondences are recorded in the primary clip (0), convert local track id to global track id
			int num_valid_corrs = libmv_CorrespondencesFromTracking(&tracking->correspondences, clips,
			                                                        num_clips, context->correspondences,
			                                                        context->track_global_index);
			printf("num valid corrs: %d\n", num_valid_corrs);
			BLI_assert(num_valid_corrs == BLI_listbase_count(&tracking->correspondences));

			BLI_strncpy(context->object_name, object->name, sizeof(context->object_name));
			context->is_camera = object->flag & TRACKING_OBJECT_CAMERA;
			context->motion_flag = tracking->settings.motion_flag;
			context->select_keyframes =
			        (tracking->settings.reconstruction_flag & TRACKING_USE_KEYFRAME_SELECTION) != 0;
		}

		tracking_cameraIntrinscisOptionsFromTracking(tracking,
		                                             width, height,
		                                             &context->all_camera_intrinsics_options[i]);

		context->all_tracks_map[i] = tracks_map_new(context->object_name, context->is_camera, num_tracks, 0);
		context->all_refine_flags[i] = multiview_refine_intrinsics_get_flags(tracking, object);

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
		context->all_tracks[i] = libmv_multiview_tracks_new(clip, i, tracksbase, context->track_global_index[i],
		                                                    width, height * aspy);
	}

	return context;
}

/* Free memory used by a reconstruction process. */
void BKE_tracking_multiview_reconstruction_context_free(MovieMultiviewReconstructContext *context)
{
	for (int i = 0; i < context->clip_num; i++) {
		libmv_tracksDestroyN(context->all_tracks[i]);
		if (context->all_reconstruction[i])
			libmv_reconstructionNDestroy(context->all_reconstruction[i]);
		if (context->track_global_index[i])
			MEM_freeN(context->track_global_index[i]);
		tracks_map_free(context->all_tracks_map[i], NULL);
	}
	printf("free per clip context");
	libmv_CorrespondencesDestroyN(context->correspondences);
	MEM_freeN(context->all_tracks);
	MEM_freeN(context->all_reconstruction);
	MEM_freeN(context->all_camera_intrinsics_options);
	MEM_freeN(context->all_tracks_map);
	MEM_freeN(context->all_sfra);
	MEM_freeN(context->all_efra);
	MEM_freeN(context->all_refine_flags);
	MEM_freeN(context->track_global_index);

	MEM_freeN(context);
}


/* Fill in multiview reconstruction options structure from reconstruction context. */
static void multiviewReconstructionOptionsFromContext(libmv_MultiviewReconstructionOptions *reconstruction_options,
                                                      MovieMultiviewReconstructContext *context)
{
	reconstruction_options->select_keyframes = context->select_keyframes;

	reconstruction_options->keyframe1 = context->keyframe1;
	reconstruction_options->keyframe2 = context->keyframe2;

	reconstruction_options->all_refine_intrinsics = context->all_refine_flags;
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

/* stop is not actually used at this moment, so reconstruction
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

	libmv_MultiviewReconstructionOptions reconstruction_options;

	progressdata.stop = stop;
	progressdata.do_update = do_update;
	progressdata.progress = progress;
	progressdata.stats_message = stats_message;
	progressdata.message_size = message_size;

	multiviewReconstructionOptionsFromContext(&reconstruction_options, context);

	if (context->motion_flag & TRACKING_MOTION_MODAL) {
		// TODO(tianwei): leave tracking solve object for now
		//context->reconstruction = libmv_solveModal(context->tracks,
		//                                           &context->camera_intrinsics_options,
		//                                           &reconstruction_options,
		//                                           multiview_reconstruct_update_solve_cb, &progressdata);
	}
	else {
		context->all_reconstruction = libmv_solveMultiviewReconstruction(
		                                  context->clip_num,
		                                  (const libmv_TracksN **) context->all_tracks,
		                                  (const libmv_CameraIntrinsicsOptions *) context->all_camera_intrinsics_options,
		                                  (const libmv_CorrespondencesN *) context->correspondences,
		                                  &reconstruction_options,
		                                  multiview_reconstruct_update_solve_cb, &progressdata);

		if (context->select_keyframes) {
			/* store actual keyframes used for reconstruction to update them in the interface later */
			context->keyframe1 = reconstruction_options.keyframe1;
			context->keyframe2 = reconstruction_options.keyframe2;
		}
	}

	error = libmv_multiviewReprojectionError(context->clip_num,
	                                         (const libmv_ReconstructionN**)context->all_reconstruction);

	context->reprojection_error = error;
}

/* Retrieve multiview refined camera intrinsics from libmv to blender. */
static void multiview_reconstruct_retrieve_libmv_intrinsics(MovieMultiviewReconstructContext *context,
                                                            int clip_id,
                                                            MovieTracking *tracking)
{
	struct libmv_ReconstructionN *libmv_reconstruction = context->all_reconstruction[clip_id];
	struct libmv_CameraIntrinsics *libmv_intrinsics = libmv_reconstructionNExtractIntrinsics(libmv_reconstruction);

	libmv_CameraIntrinsicsOptions camera_intrinsics_options;
	libmv_cameraIntrinsicsExtractOptions(libmv_intrinsics, &camera_intrinsics_options);

	tracking_trackingCameraFromIntrinscisOptions(tracking,
	                                             &camera_intrinsics_options);
}

/* Retrieve multiview reconstructed tracks from libmv to blender.
 * and also copies reconstructed cameras from libmv to movie clip datablock.
 */
static bool multiview_reconstruct_retrieve_libmv_info(MovieMultiviewReconstructContext *context,
                                                      int clip_id,
                                                      MovieTracking *tracking)
{
	// libmv reconstruction results in saved in context->all_reconstruction[0]
	struct libmv_ReconstructionN *libmv_reconstruction = context->all_reconstruction[0];
	MovieTrackingReconstruction *reconstruction = NULL;
	MovieReconstructedCamera *reconstructed;
	MovieTrackingTrack *track;
	ListBase *tracksbase =  NULL;
	int tracknr = 0, a;
	bool ok = true;
	bool origin_set = false;
	int sfra = context->all_sfra[clip_id], efra = context->all_efra[clip_id];
	float imat[4][4];

	if (context->is_camera) {
		tracksbase = &tracking->tracks;
		reconstruction = &tracking->reconstruction;
	}
	else {
		MovieTrackingObject *object = BKE_tracking_object_get_named(tracking, context->object_name);
		tracksbase = &object->tracks;
		reconstruction = &object->reconstruction;
	}

	unit_m4(imat);

	int *track_index_map = context->track_global_index[clip_id];	// this saves the global track index mapping
	track = tracksbase->first;
	while (track) {
		double pos[3];

		if (libmv_multiviewPointForTrack(libmv_reconstruction, track_index_map[tracknr], pos)) {
			track->bundle_pos[0] = pos[0];
			track->bundle_pos[1] = pos[1];
			track->bundle_pos[2] = pos[2];

			track->flag |= TRACK_HAS_BUNDLE;
			track->error = libmv_multiviewReprojectionErrorForTrack(libmv_reconstruction, track_index_map[tracknr]);
		}
		else {
			track->flag &= ~TRACK_HAS_BUNDLE;
			ok = false;

			printf("Unable to reconstruct position for track #%d '%s'\n", tracknr, track->name);
		}

		track = track->next;
		tracknr++;
	}

	if (reconstruction->cameras)
		MEM_freeN(reconstruction->cameras);

	reconstruction->camnr = 0;
	reconstruction->cameras = NULL;
	reconstructed = MEM_callocN((efra - sfra + 1) * sizeof(MovieReconstructedCamera),
	                            "temp reconstructed camera");

	for (a = sfra; a <= efra; a++) {
		double matd[4][4];

		if (libmv_multiviewCameraForFrame(libmv_reconstruction, clip_id, a, matd)) {
			int i, j;
			float mat[4][4];
			float error = libmv_multiviewReprojectionErrorForFrame(libmv_reconstruction, clip_id, a);

			for (i = 0; i < 4; i++) {
				for (j = 0; j < 4; j++)
					mat[i][j] = matd[i][j];
			}

			/* Ensure first camera has got zero rotation and transform.
			 * This is essential for object tracking to work -- this way
			 * we'll always know object and environment are properly
			 * oriented.
			 *
			 * There's one weak part tho, which is requirement object
			 * motion starts at the same frame as camera motion does,
			 * otherwise that;' be a russian roulette whether object is
			 * aligned correct or not.
			 */
			if (!origin_set) {
				invert_m4_m4(imat, mat);
				unit_m4(mat);
				origin_set = true;
			}
			else {
				mul_m4_m4m4(mat, imat, mat);
			}

			copy_m4_m4(reconstructed[reconstruction->camnr].mat, mat);
			reconstructed[reconstruction->camnr].framenr = a;
			reconstructed[reconstruction->camnr].error = error;
			reconstruction->camnr++;
		}
		else {
			ok = false;
			printf("No camera for frame %d %d\n", clip_id, a);
		}
	}

	if (reconstruction->camnr) {
		int size = reconstruction->camnr * sizeof(MovieReconstructedCamera);
		reconstruction->cameras = MEM_callocN(size, "reconstructed camera");
		memcpy(reconstruction->cameras, reconstructed, size);
	}

	if (origin_set) {
		track = tracksbase->first;
		while (track) {
			if (track->flag & TRACK_HAS_BUNDLE)
				mul_v3_m4v3(track->bundle_pos, imat, track->bundle_pos);

			track = track->next;
		}
	}

	MEM_freeN(reconstructed);

	return ok;
}

/* Retrieve all the multiview reconstruction libmv data from context to blender's side data blocks. */
static int multiview_reconstruct_retrieve_libmv(MovieMultiviewReconstructContext *context,
                                                int clip_id,
                                                MovieTracking *tracking)
{
	/* take the intrinsics back from libmv */
	multiview_reconstruct_retrieve_libmv_intrinsics(context, clip_id, tracking);

	/* take reconstructed camera and tracks info back from libmv */
	return multiview_reconstruct_retrieve_libmv_info(context, clip_id, tracking);
}

/* Finish multiview reconstruction process by copying reconstructed data
 * to the each movie clip datablock.
 */
bool BKE_tracking_multiview_reconstruction_finish(MovieMultiviewReconstructContext *context, MovieClip** clips)
{
	if (!libmv_multiviewReconstructionIsValid(context->clip_num,
	                                          (const libmv_ReconstructionN**) context->all_reconstruction)) {
		printf("Failed solve the multiview motion: at least one clip failed\n");
		return false;
	}

	for (int i = 0; i < context->clip_num; i++) {
		MovieTrackingReconstruction *reconstruction;
		MovieTrackingObject *object;

		MovieClip *clip = clips[i];
		MovieTracking *tracking = &clip->tracking;
		tracks_map_merge(context->all_tracks_map[i], tracking);
		BKE_tracking_dopesheet_tag_update(tracking);

		object = BKE_tracking_object_get_named(tracking, context->object_name);
		if (context->is_camera)
			reconstruction = &tracking->reconstruction;
		else
			reconstruction = &object->reconstruction;
		///* update keyframe in the interface */
		if (context->select_keyframes) {
			object->keyframe1 = context->keyframe1;
			object->keyframe2 = context->keyframe2;
		}
		reconstruction->error = context->reprojection_error;
		reconstruction->flag |= TRACKING_RECONSTRUCTED;

		if (!multiview_reconstruct_retrieve_libmv(context, i, tracking))
			return false;
	}

	return true;
}
