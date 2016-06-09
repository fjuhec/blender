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

#include "intern/reconstructionN.h"
#include "intern/camera_intrinsics.h"
#include "intern/tracksN.h"
#include "intern/utildefines.h"

#include "libmv/logging/logging.h"
#include "libmv/autotrack/autotrack.h"
#include "libmv/autotrack/frame_accessor.h"
#include "libmv/autotrack/marker.h"
#include "libmv/autotrack/model.h"
#include "libmv/autotrack/predict_tracks.h"
#include "libmv/autotrack/quad.h"
#include "libmv/autotrack/reconstruction.h"
#include "libmv/autotrack/region.h"
#include "libmv/autotrack/tracks.h"

// TODO(tianwei): still rely on simple_pipeline/callback for now, will be removed
#include "libmv/simple_pipeline/callbacks.h"

using mv::Tracks;
using mv::Marker;
using mv::Reconstruction;

using libmv::CameraIntrinsics;
using libmv::ProgressUpdateCallback;

struct libmv_ReconstructionN {
	mv::Reconstruction reconstruction;

	/* Used for per-track average error calculation after reconstruction */
	mv::Tracks tracks;
	libmv::CameraIntrinsics *intrinsics;

	double error;
	bool is_valid;
};

namespace {
class MultiviewReconstructUpdateCallback : public ProgressUpdateCallback {
public:
	MultiviewReconstructUpdateCallback(
	        multiview_reconstruct_progress_update_cb progress_update_callback,
	        void *callback_customdata) {
		progress_update_callback_ = progress_update_callback;
		callback_customdata_ = callback_customdata;
	}

	void invoke(double progress, const char* message) {
		if (progress_update_callback_) {
			progress_update_callback_(callback_customdata_, progress, message);
		}
	}
protected:
	multiview_reconstruct_progress_update_cb progress_update_callback_;
	void* callback_customdata_;
};

void mv_getNormalizedTracks(const Tracks &tracks,
                            const CameraIntrinsics &camera_intrinsics,
                            Tracks *normalized_tracks)
{
	libmv::vector<Marker> markers = tracks.markers();
	for (int i = 0; i < markers.size(); ++i) {
		Marker &marker = markers[i];
		double normalized_x, normalized_y;
		camera_intrinsics.InvertIntrinsics(marker.center[0], marker.center[1],
		                                   &normalized_x, &normalized_y);
		// TODO(tianwei): put the normalized value in marker instead? is this supposed to be like this?
		marker.center[0] = normalized_x, marker.center[1] = normalized_y;
		normalized_tracks->AddMarker(marker);
	}
}

}	// end of anonymous namespace

void libmv_reconstructionNDestroy(libmv_ReconstructionN* libmv_reconstructionN)
{
	LIBMV_OBJECT_DELETE(libmv_reconstructionN->intrinsics, CameraIntrinsics);
	LIBMV_OBJECT_DELETE(libmv_reconstructionN, libmv_ReconstructionN);
}

libmv_ReconstructionN** libmv_solveMultiviewReconstruction(const int clip_num,
        const libmv_TracksN **all_libmv_tracks,
        const libmv_CameraIntrinsicsOptions *all_libmv_camera_intrinsics_options,
        libmv_MultiviewReconstructionOptions *libmv_reconstruction_options,
        multiview_reconstruct_progress_update_cb progress_update_callback,
        void* callback_customdata)
{
	libmv_ReconstructionN **all_libmv_reconstruction = LIBMV_STRUCT_NEW(libmv_ReconstructionN*, clip_num);
	libmv::vector<Marker> keyframe_markers;
	int keyframe1, keyframe2;

	// make reconstrution on the primary clip reconstruction
	Reconstruction &reconstruction = all_libmv_reconstruction[0]->reconstruction;
	for(int i = 0; i < clip_num; i++)
	{
		all_libmv_reconstruction[i] = LIBMV_OBJECT_NEW(libmv_ReconstructionN);
		Tracks &tracks = *((Tracks *) all_libmv_tracks[i]);

		///* Retrieve reconstruction options from C-API to libmv API. */
		CameraIntrinsics *camera_intrinsics;
		camera_intrinsics = all_libmv_reconstruction[i]->intrinsics =
		        libmv_cameraIntrinsicsCreateFromOptions(&all_libmv_camera_intrinsics_options[i]);
		printf("camera %d size: %d, %d\n", i, camera_intrinsics->image_width(), camera_intrinsics->image_height());

		///* Invert the camera intrinsics/ */
		Tracks normalized_tracks;
		mv_getNormalizedTracks(tracks, *camera_intrinsics, &normalized_tracks);

		if(i == 0)		// key frame from primary camera
		{
			///* keyframe selection. */
			keyframe1 = libmv_reconstruction_options->keyframe1, keyframe2 = libmv_reconstruction_options->keyframe2;
			normalized_tracks.GetMarkersForTracksInBothImages(i, keyframe1, i, keyframe2, &keyframe_markers);
		}
	}

	printf("frames to init from: %d %d\n", keyframe1, keyframe2);
	printf("number of markers for init: %d\n", keyframe_markers.size());
	if (keyframe_markers.size() < 8) {
		LG << "No enough markers to initialize from";
		for(int i = 0; i < clip_num; i++)
			all_libmv_reconstruction[i]->is_valid = false;
		return all_libmv_reconstruction;
	}

	// create multiview reconstruct update callback
	MultiviewReconstructUpdateCallback update_callback =
	        MultiviewReconstructUpdateCallback(progress_update_callback,
	                                           callback_customdata);

	// TODO(tianwei): skip the automatic keyframe selection for now
	//if (libmv_reconstruction_options->select_keyframes) {
	//	LG << "Using automatic keyframe selection";

	//	update_callback.invoke(0, "Selecting keyframes");

	//	selectTwoKeyframesBasedOnGRICAndVariance(tracks,
	//	                                         normalized_tracks,
	//	                                         *camera_intrinsics,
	//	                                         keyframe1,
	//	                                         keyframe2);

	//	/* so keyframes in the interface would be updated */
	//	libmv_reconstruction_options->keyframe1 = keyframe1;
	//	libmv_reconstruction_options->keyframe2 = keyframe2;
	//}

	///* Actual reconstruction. */
	update_callback.invoke(0, "Initial reconstruction");

	///* TODO(tianwei): chain the tracks and correspondences */

	if(!mv::ReconstructTwoFrames(keyframe_markers, &reconstruction)) {
		printf("mv::ReconstrucTwoFrames failed\n");
		for(int i = 0; i < clip_num; i++)
			all_libmv_reconstruction[i]->is_valid = false;
		return all_libmv_reconstruction;
	}
	//EuclideanBundle(normalized_tracks, &reconstruction);
	//EuclideanCompleteReconstruction(normalized_tracks,
	//                                &reconstruction,
	//                                &update_callback);

	///* Refinement/ */
	//if (libmv_reconstruction_options->refine_intrinsics) {
	//	libmv_solveRefineIntrinsics(
	//	            tracks,
	//	            libmv_reconstruction_options->refine_intrinsics,
	//	            libmv::BUNDLE_NO_CONSTRAINTS,
	//	            progress_update_callback,
	//	            callback_customdata,
	//	            &reconstruction,
	//	            camera_intrinsics);
	//}

	///* Set reconstruction scale to unity. */
	//EuclideanScaleToUnity(&reconstruction);

	///* Finish reconstruction. */
	//finishReconstruction(tracks,
	//                     *camera_intrinsics,
	//                     libmv_reconstruction,
	//                     progress_update_callback,
	//                     callback_customdata);

	for(int i = 0; i < clip_num; i++)
		all_libmv_reconstruction[i]->is_valid = true;

	return all_libmv_reconstruction;
}

bool libmv_multiviewReconstructionIsValid(const int clip_num,
                                          const libmv_ReconstructionN **all_libmv_reconstruction)
{
	bool valid_flag = true;
	for(int i = 0; i < clip_num; i++)
		valid_flag &= all_libmv_reconstruction[i]->is_valid;
	return valid_flag;
}

double libmv_multiviewReprojectionError(const int clip_num,
                                        const libmv_ReconstructionN **all_libmv_reconstruction)
{
	double error = 0.0;
	for(int i = 0; i < clip_num; i++)
		error += all_libmv_reconstruction[i]->error;
	error /= clip_num;

	return error;
}

libmv_CameraIntrinsics *libmv_reconstructionNExtractIntrinsics(libmv_ReconstructionN *libmv_reconstruction)
{
	return (libmv_CameraIntrinsics *) libmv_reconstruction->intrinsics;
}
