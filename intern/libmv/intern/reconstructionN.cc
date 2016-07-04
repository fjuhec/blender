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

	// act in a calibrated fashion
    double normalized_x, normalized_y;
    camera_intrinsics.InvertIntrinsics(marker.center[0], marker.center[1],
                                       &normalized_x, &normalized_y);
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

void finishMultiviewReconstruction(
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

// re-apply camera intrinsics on the normalized 2d points
Marker libmv_projectMarker(const mv::Point& point,
                           const mv::CameraPose& camera,
                           const CameraIntrinsics& intrinsics) {
  libmv::Vec3 projected = camera.R * point.X + camera.t;
  projected /= projected(2);

  mv::Marker reprojected_marker;
  double origin_x, origin_y;
  intrinsics.ApplyIntrinsics(projected(0), projected(1),
                             &origin_x, &origin_y);
  reprojected_marker.center[0] = origin_x;
  reprojected_marker.center[1] = origin_y;

  reprojected_marker.clip = camera.clip;
  reprojected_marker.frame = camera.frame;
  reprojected_marker.track = point.track;
  return reprojected_marker;
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
  //  LG << "Using automatic keyframe selection";

  //  update_callback.invoke(0, "Selecting keyframes");

  //  selectTwoKeyframesBasedOnGRICAndVariance(tracks,
  //                                           normalized_tracks,
  //                                           *camera_intrinsics,
  //                                           keyframe1,
  //                                           keyframe2);

  //  /* so keyframes in the interface would be updated */
  //  libmv_reconstruction_options->keyframe1 = keyframe1;
  //  libmv_reconstruction_options->keyframe2 = keyframe2;
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
  //  printf("mv::EuclideanBundleAll failed\n");
  //  all_libmv_reconstruction[0]->is_valid = false;
  //  return all_libmv_reconstruction;
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
  LG << "[libmv_solveMultiviewReconstruction] Successfully refine camera intrinsics\n";

  ///* Set reconstruction scale to unity. */
  mv::EuclideanScaleToUnity(&reconstruction);

  /* Finish reconstruction. */
  finishMultiviewReconstruction(all_normalized_tracks,
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
  // reprojection is computed as a whole and stored in all_libmv_reconstruction[0]->error
  double error = all_libmv_reconstruction[0]->error;
  return error;
}

libmv_CameraIntrinsics *libmv_reconstructionNExtractIntrinsics(libmv_ReconstructionN *libmv_reconstruction)
{
  return (libmv_CameraIntrinsics *) libmv_reconstruction->intrinsics;
}

int libmv_multiviewPointForTrack(
    const libmv_ReconstructionN *libmv_reconstruction,
    int global_track,
    double pos[3]) {
  const Reconstruction *reconstruction = &libmv_reconstruction->reconstruction;
  const mv::Point *point = reconstruction->PointForTrack(global_track);
  if (point) {
    pos[0] = point->X[0];
    pos[1] = point->X[2];
    pos[2] = point->X[1];
    return 1;
  }
  return 0;
}

double libmv_multiviewReprojectionErrorForTrack(
    const libmv_ReconstructionN *libmv_reconstruction,
    int track) {
  const Reconstruction *reconstruction = &libmv_reconstruction->reconstruction;
  const CameraIntrinsics *intrinsics = libmv_reconstruction->intrinsics;
  mv::vector<Marker> markers;
  libmv_reconstruction->tracks.GetMarkersForTrack(track, &markers);

  int num_reprojected = 0;
  double total_error = 0.0;

  for (int i = 0; i < markers.size(); ++i) {
    double weight = markers[i].weight;
    const mv::CameraPose *camera = reconstruction->CameraPoseForFrame(markers[i].clip, markers[i].frame);
    const mv::Point *point = reconstruction->PointForTrack(markers[i].track);

    if (!camera || !point || weight == 0.0) {
      continue;
    }

    num_reprojected++;

    Marker reprojected_marker = libmv_projectMarker(*point, *camera, *intrinsics);
    double ex = (reprojected_marker.center[0] - markers[i].center[1]) * weight;
    double ey = (reprojected_marker.center[0] - markers[i].center[1]) * weight;

    total_error += sqrt(ex * ex + ey * ey);
  }

  return total_error / num_reprojected;
}

int libmv_multiviewCameraForFrame(
    const libmv_ReconstructionN *libmv_reconstruction,
    int clip,
    int frame,
    double mat[4][4]) {
  const Reconstruction *reconstruction = &libmv_reconstruction->reconstruction;
  const mv::CameraPose *camera = reconstruction->CameraPoseForFrame(clip, frame);

  if (camera) {
    for (int j = 0; j < 3; ++j) {
      for (int k = 0; k < 3; ++k) {
        int l = k;

        if (k == 1) {
          l = 2;
        } else if (k == 2) {
          l = 1;
        }

        if (j == 2) {
          mat[j][l] = -camera->R(j, k);
        } else {
          mat[j][l] = camera->R(j, k);
        }
      }
      mat[j][3] = 0.0;
    }

    libmv::Vec3 optical_center = -camera->R.transpose() * camera->t;

    mat[3][0] = optical_center(0);
    mat[3][1] = optical_center(2);
    mat[3][2] = optical_center(1);

    mat[3][3] = 1.0;

    return 1;
  }

  return 0;
}

double libmv_multiviewReprojectionErrorForFrame(
    const libmv_ReconstructionN *libmv_reconstruction,
    int clip,
    int frame) {
  const Reconstruction *reconstruction = &libmv_reconstruction->reconstruction;
  const CameraIntrinsics *intrinsics = libmv_reconstruction->intrinsics;
  mv::vector<Marker> markers;
  libmv_reconstruction->tracks.GetMarkersInFrame(clip, frame, &markers);
  const mv::CameraPose *camera = reconstruction->CameraPoseForFrame(clip, frame);
  int num_reprojected = 0;
  double total_error = 0.0;

  if (!camera) {
    return 0.0;
  }

  for (int i = 0; i < markers.size(); ++i) {
    const mv::Point *point =
      reconstruction->PointForTrack(markers[i].track);

    if (!point) {
      continue;
    }

    num_reprojected++;

    Marker reprojected_marker =
      libmv_projectMarker(*point, *camera, *intrinsics);
    double ex = (reprojected_marker.center[0] - markers[i].center[0]) * markers[i].weight;
    double ey = (reprojected_marker.center[1] - markers[i].center[1]) * markers[i].weight;

    total_error += sqrt(ex * ex + ey * ey);
  }

  return total_error / num_reprojected;
}
