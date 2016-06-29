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
#include "libmv/autotrack/bundle.h"
#include "libmv/autotrack/frame_accessor.h"
#include "libmv/autotrack/marker.h"
#include "libmv/autotrack/model.h"
#include "libmv/autotrack/pipeline.h"
#include "libmv/autotrack/predict_tracks.h"
#include "libmv/autotrack/quad.h"
#include "libmv/autotrack/reconstruction.h"
#include "libmv/autotrack/region.h"
#include "libmv/autotrack/tracks.h"

// TODO(tianwei): still rely on simple_pipeline/callback for now, will be removed
#include "libmv/simple_pipeline/callbacks.h"
#include "libmv/simple_pipeline/callbacks.h"

using mv::Tracks;
using mv::Marker;
using mv::Reconstruction;
using mv::Correspondences;

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

// Each clip has a fix camera intrinsics, set frames of a clip to that fixed intrinsics
bool ReconstructionUpdateFixedIntrinsics(libmv_ReconstructionN **all_libmv_reconstruction,
                                         Tracks *tracks,
                                         Reconstruction *reconstruction)
{
	int clip_num = tracks->GetClipNum();
	for(int i = 0; i < clip_num; i++) {
		CameraIntrinsics *camera_intrinsics = all_libmv_reconstruction[i]->intrinsics;
		int cam_intrinsic_index = reconstruction->AddCameraIntrinsics(camera_intrinsics);
		assert(cam_intrinsic_index == i);
	}
	reconstruction->InitIntrinsicsMapFixed(*tracks);

	return true;
}

void libmv_solveRefineIntrinsics(
        const Tracks &tracks,
        const int refine_intrinsics,
        const int bundle_constraints,
        reconstruct_progress_update_cb progress_update_callback,
        void* callback_customdata,
        Reconstruction* reconstruction,
        CameraIntrinsics* intrinsics) {
  /* only a few combinations are supported but trust the caller/ */
  int bundle_intrinsics = 0;

  if (refine_intrinsics & LIBMV_REFINE_FOCAL_LENGTH) {
    bundle_intrinsics |= mv::BUNDLE_FOCAL_LENGTH;
  }
  if (refine_intrinsics & LIBMV_REFINE_PRINCIPAL_POINT) {
    bundle_intrinsics |= mv::BUNDLE_PRINCIPAL_POINT;
  }
  if (refine_intrinsics & LIBMV_REFINE_RADIAL_DISTORTION_K1) {
    bundle_intrinsics |= mv::BUNDLE_RADIAL_K1;
  }
  if (refine_intrinsics & LIBMV_REFINE_RADIAL_DISTORTION_K2) {
    bundle_intrinsics |= mv::BUNDLE_RADIAL_K2;
  }

  progress_update_callback(callback_customdata, 1.0, "Refining solution");

  mv::EuclideanBundleCommonIntrinsics(tracks,
                                      bundle_intrinsics,
                                      bundle_constraints,
                                      reconstruction,
                                      intrinsics);
}

void finishReconstruction(
    const Tracks &tracks,
    const CameraIntrinsics &camera_intrinsics,
    libmv_ReconstructionN *libmv_reconstruction,
    reconstruct_progress_update_cb progress_update_callback,
    void *callback_customdata) {
	Reconstruction &reconstruction = libmv_reconstruction->reconstruction;

	/* Reprojection error calculation. */
	progress_update_callback(callback_customdata, 1.0, "Finishing solution");
	libmv_reconstruction->tracks = tracks;
	libmv_reconstruction->error = mv::EuclideanReprojectionError(tracks,
	                                                             reconstruction,
	                                                             camera_intrinsics);
}

}	// end of anonymous namespace

void libmv_reconstructionNDestroy(libmv_ReconstructionN* libmv_reconstructionN)
{
	LIBMV_OBJECT_DELETE(libmv_reconstructionN->intrinsics, CameraIntrinsics);
	LIBMV_OBJECT_DELETE(libmv_reconstructionN, libmv_ReconstructionN);
}

libmv_ReconstructionN** libmv_solveMultiviewReconstruction(
        const int clip_num,
        const libmv_TracksN **all_libmv_tracks,
        const libmv_CameraIntrinsicsOptions *all_libmv_camera_intrinsics_options,
        const libmv_CorrespondencesN * libmv_correspondences,
        libmv_MultiviewReconstructionOptions *libmv_reconstruction_options,
        multiview_reconstruct_progress_update_cb progress_update_callback,
        void* callback_customdata)
{
	libmv_ReconstructionN **all_libmv_reconstruction = LIBMV_STRUCT_NEW(libmv_ReconstructionN*, clip_num);
	libmv::vector<Marker> keyframe_markers;
	int keyframe1, keyframe2;

	Tracks all_normalized_tracks;	// normalized tracks of all clips
	all_normalized_tracks.SetClipNum(clip_num);
	for(int i = 0; i < clip_num; i++)
	{
		all_libmv_reconstruction[i] = LIBMV_OBJECT_NEW(libmv_ReconstructionN);
		Tracks &tracks = *((Tracks *) all_libmv_tracks[i]);		// Tracks are just a bunch of markers

		///* Retrieve reconstruction options from C-API to libmv API. */
		CameraIntrinsics *camera_intrinsics;
		camera_intrinsics = all_libmv_reconstruction[i]->intrinsics =
		        libmv_cameraIntrinsicsCreateFromOptions(&all_libmv_camera_intrinsics_options[i]);

		///* Invert the camera intrinsics/ */
		Tracks normalized_tracks;
		mv_getNormalizedTracks(tracks, *camera_intrinsics, &normalized_tracks);
		all_normalized_tracks.AddTracks(normalized_tracks);

		if(i == 0)		// key frame from primary camera
		{
			///* keyframe selection. */
			keyframe1 = libmv_reconstruction_options->keyframe1, keyframe2 = libmv_reconstruction_options->keyframe2;
			normalized_tracks.GetMarkersForTracksInBothImages(i, keyframe1, i, keyframe2, &keyframe_markers);
		}
	}
	// make reconstrution on the primary clip reconstruction
	Reconstruction &reconstruction = all_libmv_reconstruction[0]->reconstruction;

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

	// update intrinsics mapping from (clip, frame) -> intrinsics
	// TODO(tianwei): in the future we may support varing focal length,
	// thus each (clip, frame) should have a unique intrinsics index
	ReconstructionUpdateFixedIntrinsics(all_libmv_reconstruction, &all_normalized_tracks, &reconstruction);

	// reconstruct two views from the main clip
	if(!mv::ReconstructTwoFrames(keyframe_markers, 0, *(all_libmv_reconstruction[0]->intrinsics), &reconstruction)) {
		LG << "mv::ReconstrucTwoFrames failed\n";
		all_libmv_reconstruction[0]->is_valid = false;
		return all_libmv_reconstruction;
	}
	// bundle the two-view initial reconstruction
	// (it is redundant for now since now 3d point is added at this stage)
	//if(!mv::EuclideanBundleAll(all_normalized_tracks, &reconstruction)) {
	//	printf("mv::EuclideanBundleAll failed\n");
	//	all_libmv_reconstruction[0]->is_valid = false;
	//	return all_libmv_reconstruction;
	//}
	if(!mv::EuclideanCompleteMultiviewReconstruction(all_normalized_tracks, &reconstruction, &update_callback)) {
		LG << "mv::EuclideanReconstructionComplete failed\n";
		all_libmv_reconstruction[0]->is_valid = false;
		return all_libmv_reconstruction;
	}
	std::cout << "[libmv_solveMultiviewReconstruction] Successfully do track intersection and camera resection\n";

	/* Refinement/ */
	//TODO(Tianwei): current api allows only one camera refine intrinsics
	if (libmv_reconstruction_options->all_refine_intrinsics[0]) {
		libmv_solveRefineIntrinsics(
		            all_normalized_tracks,
		            libmv_reconstruction_options->all_refine_intrinsics[0],
		            mv::BUNDLE_NO_CONSTRAINTS,
		            progress_update_callback,
		            callback_customdata,
		            &reconstruction,
		            all_libmv_reconstruction[0]->intrinsics);
	}
	std::cout << "[libmv_solveMultiviewReconstruction] Successfully refine camera intrinsics\n";

	///* Set reconstruction scale to unity. */
	mv::EuclideanScaleToUnity(&reconstruction);

	/* Finish reconstruction. */
	finishReconstruction(all_normalized_tracks,
	                     *(all_libmv_reconstruction[0]->intrinsics),
	                     all_libmv_reconstruction[0],
	                     progress_update_callback,
	                     callback_customdata);

	// a multi-view reconstruction is succesfuly iff all reconstruction falgs are set to true
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
