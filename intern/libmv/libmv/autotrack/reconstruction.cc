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
#include "libmv/simple_pipeline/camera_intrinsics.h"

using mv::Marker;
using mv::Tracks;
using libmv::Mat;
using libmv::Mat2;
using libmv::Mat3;
using libmv::Vec;
using libmv::Vec2;

namespace mv {

static void GetFramesInMarkers(const vector<Marker> &markers,
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

static void CoordinatesForMarkersInFrame(const vector<Marker> &markers,
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
  CameraPose pose1(clip, frame1, reconstruction->GetIntrinsicsMap(clip, frame1), Mat3::Identity(), Vec3::Zero());
  CameraPose pose2(clip, frame2, reconstruction->GetIntrinsicsMap(clip, frame2), R, t);
  reconstruction->AddCameraPose(pose1);
  reconstruction->AddCameraPose(pose2);

  LG << "From two frame reconstruction got:\nR:\n" << R << "\nt:" << t.transpose();
  return true;
}

//	==================  mv::Reconstruction implementation ===================
// push a new cameraIntrinsics and return the index
int Reconstruction::AddCameraIntrinsics(CameraIntrinsics *intrinsics_ptr) {
  camera_intrinsics_.push_back(intrinsics_ptr);
  return camera_intrinsics_.size()-1;
}

void Reconstruction::AddCameraPose(const CameraPose& pose) {
  if(camera_poses_.size() < pose.clip + 1) {
    camera_poses_.resize(pose.clip+1);
  }
  if(camera_poses_[pose.clip].size() < pose.frame + 1) {
    camera_poses_[pose.clip].resize(pose.frame+1);
  }
  // copy form pose to camera_poses_
  camera_poses_[pose.clip][pose.frame].clip = pose.clip;
  camera_poses_[pose.clip][pose.frame].frame = pose.frame;
  camera_poses_[pose.clip][pose.frame].intrinsics = pose.intrinsics;
  camera_poses_[pose.clip][pose.frame].R = pose.R;
  camera_poses_[pose.clip][pose.frame].t = pose.t;
}

int Reconstruction::GetClipNum() const {
  return camera_poses_.size();
}

int Reconstruction::GetAllPoseNum() const {
  int all_pose = 0;
  for(int i = 0; i < camera_poses_.size(); ++i) {
    all_pose += camera_poses_[i].size();
  }
  return all_pose;
}

CameraPose* Reconstruction::CameraPoseForFrame(int clip, int frame) {
  if (camera_poses_.size() <= clip)
    return NULL;
  if (camera_poses_[clip].size() <= frame)
    return NULL;
  if (camera_poses_[clip][frame].clip == -1)	// this CameraPose is uninitilized
      return NULL;
  return &(camera_poses_[clip][frame]);
}

const CameraPose* Reconstruction::CameraPoseForFrame(int clip, int frame) const {
  if (camera_poses_.size() <= clip)
    return NULL;
  if (camera_poses_[clip].size() <= frame)
    return NULL;
  if (camera_poses_[clip][frame].clip == -1)	// this CameraPose is uninitilized
    return NULL;
  return (const CameraPose*) &(camera_poses_[clip][frame]);
}

Point* Reconstruction::PointForTrack(int track) {
  if (track < 0 || track >= points_.size()) {
    return NULL;
  }
  Point *point = &points_[track];
  if (point->track == -1) {
    return NULL;
  }
  return point;
}

const Point* Reconstruction::PointForTrack(int track) const {
  if (track < 0 || track >= points_.size()) {
    return NULL;
  }
  const Point *point = &points_[track];
  if (point->track == -1) {	// initialized but not set, return NULL
    return NULL;
  }
  return point;
}

int Reconstruction::AddPoint(const Point& point) {
  LG << "InsertPoint " << point.track << ":\n" << point.X;
  if (point.track >= points_.size()) {
    points_.resize(point.track + 1);
  }
  points_[point.track].track = point.track;
  points_[point.track].X = point.X;
  return point.track;
}

const vector<vector<CameraPose> >& Reconstruction::camera_poses() const {
  return camera_poses_;
}

const vector<Point>& Reconstruction::AllPoints() const {
  return points_;
}

int Reconstruction::GetReconstructedCameraNum() const {
  int reconstructed_num = 0;
  for(int i = 0; i < camera_poses_.size(); i++) {
    for(int j = 0; j < camera_poses_[i].size(); j++) {
      if(camera_poses_[i][j].clip != -1 && camera_poses_[i][j].frame != -1)
        reconstructed_num++;
    }
  }
  return reconstructed_num;
}

void Reconstruction::InitIntrinsicsMap(Tracks &tracks) {
  int clip_num = tracks.GetClipNum();
  intrinsics_map.resize(clip_num);
  for(int i = 0; i < clip_num; i++) {
    intrinsics_map.resize(tracks.MaxFrame(i)+1);
    for(int j = 0; j < intrinsics_map.size(); j++) {
    	intrinsics_map[i][j] = -1;
    }
  }
}

void Reconstruction::InitIntrinsicsMapFixed(Tracks &tracks) {
  int clip_num = tracks.GetClipNum();
  intrinsics_map.resize(clip_num);
  for(int i = 0; i < clip_num; i++) {
    intrinsics_map[i].resize(tracks.MaxFrame(i)+1);
    for(int j = 0; j < intrinsics_map[i].size(); j++) {
      intrinsics_map[i][j] = i;
    }
  }
}

bool Reconstruction::SetIntrinsicsMap(int clip, int frame, int intrinsics) {
  if(intrinsics_map.size() <= clip)
    return false;
  if(intrinsics_map[clip].size() <= frame)
    return false;
  intrinsics_map[clip][frame] = intrinsics;
  return true;
}

int Reconstruction::GetIntrinsicsMap(int clip, int frame) const {
  if(intrinsics_map.size() <= clip)
    return -1;
  if(intrinsics_map[clip].size() <= frame)
    return -1;
  return intrinsics_map[clip][frame];
}

}  // namespace mv
