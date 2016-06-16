// Copyright (c) 2016 libmv authors.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//
// Author: shentianweipku@gmail.com (Tianwei Shen)

#include "libmv/autotrack/reconstruction.h"
#include "libmv/multiview/fundamental.h"
#include "libmv/autotrack/marker.h"
#include "libmv/autotrack/tracks.h"
#include "libmv/numeric/numeric.h"
#include "libmv/logging/logging.h"

using mv::Marker;
using mv::Tracks;
using libmv::Mat;
using libmv::Mat2;
using libmv::Mat3;
using libmv::Vec;
using libmv::Vec2;

namespace mv {

void GetFramesInMarkers(const vector<Marker> &markers,
                        int *image1, int *image2) {
	if (markers.size() < 2) {
		return;
	}
	*image1 = markers[0].frame;
	for (int i = 1; i < markers.size(); ++i) {
		if (markers[i].frame != *image1) {
			*image2 = markers[i].frame;
			return;
		}
	}
	*image2 = -1;
	LOG(FATAL) << "Only one image in the markers.";
}

void CoordinatesForMarkersInFrame(const vector<Marker> &markers,
                                  int clip, int frame,
                                  Mat *coordinates) {
	vector<Vec2> coords;
	for (int i = 0; i < markers.size(); ++i) {
		const Marker &marker = markers[i];
		if (markers[i].clip == clip && markers[i].frame == frame) {
			coords.push_back(Vec2(marker.center[0], marker.center[1]));
		}
	}
	coordinates->resize(2, coords.size());
	for (int i = 0; i < coords.size(); i++) {
		coordinates->col(i) = coords[i];
	}
}

/* markers come from two views in the same clip,
 * reconstruction should be new and empty
 */
bool ReconstructTwoFrames(const vector<Marker> &markers,
                          const int clip,
                          libmv::CameraIntrinsics &cam_intrinsics,
                          Reconstruction *reconstruction)
{
	if (markers.size() < 16) {
		LG << "Not enough markers to initialize from two frames: " << markers.size();
		return false;
	}

	int frame1, frame2;
	GetFramesInMarkers(markers, &frame1, &frame2);

	Mat x1, x2;
	CoordinatesForMarkersInFrame(markers, clip, frame1, &x1);
	CoordinatesForMarkersInFrame(markers, clip, frame2, &x2);

	Mat3 F;
	libmv::NormalizedEightPointSolver(x1, x2, &F);

	// The F matrix should be an E matrix, but squash it just to be sure.
	Mat3 E;
	libmv::FundamentalToEssential(F, &E);

	// Recover motion between the two images. Since this function assumes a
	// calibrated camera, use the identity for K.
	Mat3 R;
	Vec3 t;
	Mat3 K = Mat3::Identity();
	if (!libmv::MotionFromEssentialAndCorrespondence(E, K, x1.col(0), K, x2.col(0), &R, &t))
	{
		LG << "Failed to compute R and t from E and K.";
		return false;
	}

	// frame 1 gets the reference frame, frame 2 gets the relative motion.
	int cam_intrinsic_index = reconstruction->AddCameraIntrinsics(&cam_intrinsics);
	CameraPose pose1(clip, frame1, cam_intrinsic_index, Mat3::Identity(), Vec3::Zero());
	CameraPose pose2(clip, frame2, cam_intrinsic_index, R, t);
	reconstruction->AddCameraPose(pose1);
	reconstruction->AddCameraPose(pose2);

	LG << "From two frame reconstruction got:\nR:\n" << R
	   << "\nt:" << t.transpose();
	return true;
}

/**
 * @brief EuclideanBundleAll: bundle all the clips and frames
 * @param all_normalized_tracks: markers from all clips
 * @param reconstruction: Reconstruction data structure
 * @return
 */
bool EuclideanBundleAll(const Tracks &all_normalized_tracks,
                        Reconstruction *reconstruction)
{
	return true;
}

bool EuclideanReconstructionComplete(const Tracks &tracks,
                                     Reconstruction *reconstruction,
                                     libmv::ProgressUpdateCallback *update_callback)
{
	//InternalCompleteReconstruction<EuclideanPipelineRoutines>(tracks, reconstruction, update_callback);
	return true;
}

//	==================  mv::Reconstruction implementation ===================
// push a new cameraIntrinsics and return the index
int Reconstruction::AddCameraIntrinsics(CameraIntrinsics *intrinsics_ptr)
{
	camera_intrinsics_.push_back(intrinsics_ptr);
	return camera_intrinsics_.size()-1;
}

void Reconstruction::AddCameraPose(const CameraPose& pose)
{
	if(camera_poses_.size() < pose.clip + 1)
		camera_poses_.resize(pose.clip+1);
	camera_poses_[pose.clip].push_back(pose);
}

int Reconstruction::GetClipNum() const {
	return camera_poses_.size();
}

int Reconstruction::GetAllPoseNum() const {
	int all_pose = 0;
	for(int i = 0; i < camera_poses_.size(); ++i) {
		all_pose += camera_poses_[i].size();
	}
}

}  // namespace mv
