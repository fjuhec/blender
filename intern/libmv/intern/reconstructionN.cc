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

using mv::Tracks;
using mv::Reconstruction;

using libmv::CameraIntrinsics;

struct libmv_ReconstructionN {
	mv::Reconstruction reconstruction;

	/* Used for per-track average error calculation after reconstruction */
	mv::Tracks tracks;
	libmv::CameraIntrinsics *intrinsics;

	double error;
	bool is_valid;
};

libmv_ReconstructionN** libmv_solveMultiviewReconstruction(const int clip_num,
        const libmv_TracksN **all_libmv_tracks,
        const libmv_CameraIntrinsicsOptions *libmv_camera_intrinsics_options,
        libmv_MultiviewReconstructionOptions *libmv_reconstruction_options,
        reconstruct_progress_update_cb progress_update_callback,
        void* callback_customdata)
{
	//libmv_ReconstructionN *libmv_reconstruction =
	//        LIBMV_OBJECT_NEW(libmv_ReconstructionN);

	//Tracks &tracks = *((Tracks *) libmv_tracks);
	//EuclideanReconstruction &reconstruction =
	//        libmv_reconstruction->reconstruction;

	//ReconstructUpdateCallback update_callback =
	//        ReconstructUpdateCallback(progress_update_callback,
	//                                  callback_customdata);

	///* Retrieve reconstruction options from C-API to libmv API. */
	//CameraIntrinsics *camera_intrinsics;
	//camera_intrinsics = libmv_reconstruction->intrinsics =
	//        libmv_cameraIntrinsicsCreateFromOptions(libmv_camera_intrinsics_options);

	///* Invert the camera intrinsics/ */
	//Tracks normalized_tracks;
	//libmv_getNormalizedTracks(tracks, *camera_intrinsics, &normalized_tracks);

	///* keyframe selection. */
	//int keyframe1 = libmv_reconstruction_options->keyframe1,
	//        keyframe2 = libmv_reconstruction_options->keyframe2;

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
	//LG << "frames to init from: " << keyframe1 << " " << keyframe2;

	//libmv::vector<Marker> keyframe_markers =
	//        normalized_tracks.MarkersForTracksInBothImages(keyframe1, keyframe2);

	//LG << "number of markers for init: " << keyframe_markers.size();

	//if (keyframe_markers.size() < 8) {
	//	LG << "No enough markers to initialize from";
	//	libmv_reconstruction->is_valid = false;
	//	return libmv_reconstruction;
	//}

	//update_callback.invoke(0, "Initial reconstruction");

	//EuclideanReconstructTwoFrames(keyframe_markers, &reconstruction);
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

	//libmv_reconstruction->is_valid = true;

	//return (libmv_Reconstruction *) libmv_reconstruction;
	return (libmv_ReconstructionN**) NULL;
}
