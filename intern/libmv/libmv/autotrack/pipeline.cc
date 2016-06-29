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
// Author: Tianwei Shen <shentianweipku@gmail.com>
// This file contains the multi-view reconstruction pipeline, such as camera resection.

#include "libmv/autotrack/pipeline.h"

#include <cstdio>

#include "libmv/logging/logging.h"
#include "libmv/autotrack/bundle.h"
#include "libmv/autotrack/reconstruction.h"
#include "libmv/autotrack/tracks.h"
#include "libmv/autotrack/intersect.h"
#include "libmv/autotrack/resect.h"
#include "libmv/simple_pipeline/camera_intrinsics.h"

#ifdef _MSC_VER
#  define snprintf _snprintf
#endif

namespace mv {
namespace {

// Use this functor-like struct to reuse reconstruction pipeline code
// in the future, in case we will do projective reconstruction
struct EuclideanPipelineRoutines {
	typedef ::mv::Reconstruction Reconstruction;
	typedef CameraPose Camera;
	typedef ::mv::Point Point;

	static void Bundle(const Tracks &tracks,
	                   Reconstruction *reconstruction) {
		EuclideanBundleAll(tracks, reconstruction);
	}

	static bool Resect(const vector<Marker> &markers,
	                   Reconstruction *reconstruction,
	                   bool final_pass,
	                   int intrinsics_index) {
		return EuclideanResect(markers, reconstruction, final_pass, intrinsics_index);
	}

	static bool Intersect(const vector<Marker> &markers,
	                      Reconstruction *reconstruction) {
		return EuclideanIntersect(markers, reconstruction);
	}

	static Marker ProjectMarker(const Point &point,
	                            const CameraPose &camera,
	                            const CameraIntrinsics &intrinsics) {
		Vec3 projected = camera.R * point.X + camera.t;
		projected /= projected(2);

		Marker reprojected_marker;
		double normalized_x, normalized_y;
		intrinsics.ApplyIntrinsics(projected(0),
		                           projected(1),
		                           &normalized_x,
		                           &normalized_y);
		reprojected_marker.center[0] = normalized_x;
		reprojected_marker.center[1] = normalized_y;

		reprojected_marker.clip = camera.clip;
		reprojected_marker.frame = camera.frame;
		reprojected_marker.track = point.track;
		return reprojected_marker;
	}
};

}  // namespace

static void CompleteReconstructionLogProgress(
    libmv::ProgressUpdateCallback *update_callback,
    double progress,
    const char *step = NULL) {
  if (update_callback) {
    char message[256];

    if (step)
      snprintf(message, sizeof(message), "Completing solution %d%% | %s",
               (int)(progress*100), step);
    else
      snprintf(message, sizeof(message), "Completing solution %d%%",
               (int)(progress*100));

    update_callback->invoke(progress, message);
  }
}

template<typename PipelineRoutines>
bool InternalCompleteReconstruction(
    const Tracks &tracks,
    typename PipelineRoutines::Reconstruction *reconstruction,
    libmv::ProgressUpdateCallback *update_callback = NULL) {

	int clip_num = tracks.GetClipNum();
	int num_frames = 0;
	for(int i = 0; i < clip_num; i++) {
		num_frames += tracks.MaxFrame(i) + 1;
	}

	int max_track = tracks.MaxTrack();
	int num_resects = -1;
	int num_intersects = -1;
	int total_resects = 0;
	std::cout << "Max track: " << max_track << "\n";
	std::cout << "Number of total frames: " << num_frames << "\n";
	std::cout << "Number of markers: " << tracks.NumMarkers() << "\n";
	while (num_resects != 0 || num_intersects != 0) {
		// Do all possible intersections.
		num_intersects = 0;
		for (int track = 0; track <= max_track; ++track) {
			if (reconstruction->PointForTrack(track)) {		// track has already been added
				std::cout << "Skipping point: " << track << "\n";
				continue;
			}
			vector<Marker> all_markers;
			tracks.GetMarkersForTrack(track, &all_markers);
			std::cout << "Got " << all_markers.size() << " markers for track " << track << "\n";

			vector<Marker> reconstructed_markers;
			for (int i = 0; i < all_markers.size(); ++i) {
				if (reconstruction->CameraPoseForFrame(all_markers[i].clip, all_markers[i].frame)) {
					reconstructed_markers.push_back(all_markers[i]);
				}
			}
			std::cout << "Got " << reconstructed_markers.size() << " reconstructed markers for track " << track << "\n";
			if (reconstructed_markers.size() >= 2) {
				CompleteReconstructionLogProgress(update_callback,
				                                  (double)total_resects/(num_frames));
				if (PipelineRoutines::Intersect(reconstructed_markers,
				                                reconstruction)) {
					num_intersects++;
					std::cout << "Ran Intersect() for track " << track << "\n";
				} else {
					std::cout << "Failed Intersect() for track " << track << "\n";
				}
			}
		}
		// bundle the newly added points
		if (num_intersects) {
			CompleteReconstructionLogProgress(update_callback,
			                                  (double)total_resects/(num_frames),
			                                  "Bundling...");
			PipelineRoutines::Bundle(tracks, reconstruction);
			std::cout << "Ran Bundle() after intersections.\n";
		}
		std::cout << "Did " << num_intersects << " intersects.\n";

		// Do all possible resections.
		num_resects = 0;
		for(int clip = 0; clip < clip_num; clip++) {
			const int max_image = tracks.MaxFrame(clip);
			for (int image = 0; image <= max_image; ++image) {
				if (reconstruction->CameraPoseForFrame(clip, image)) {	// this camera pose has been added
					LG << "Skipping frame: " << clip << " " << image << "\n";
					continue;
				}
				vector<Marker> all_markers;
				tracks.GetMarkersInFrame(clip, image, &all_markers);
				std::cout << "Got " << all_markers.size() << " markers for frame " << clip << ", " << image << "\n";

				vector<Marker> reconstructed_markers;
				for (int i = 0; i < all_markers.size(); ++i) {
					if (reconstruction->PointForTrack(all_markers[i].track)) {	// 3d points have been added
						reconstructed_markers.push_back(all_markers[i]);
					}
				}
				std::cout << "Got " << reconstructed_markers.size() << " reconstructed markers for frame "
				          << clip << " " << image << "\n";
				if (reconstructed_markers.size() >= 5) {
					CompleteReconstructionLogProgress(update_callback,
					                                  (double)total_resects/(num_frames));
					if (PipelineRoutines::Resect(reconstructed_markers,
					                             reconstruction, false,
					                             reconstruction->GetIntrinsicsMap(clip, image))) {
						num_resects++;
						total_resects++;
						std::cout << "Ran Resect() for frame (" << clip << ", " << image << ")\n";
					} else {
						std::cout << "Failed Resect() for frame (" << clip << ", " << image << ")\n";
					}
				}
			}
		}
		if (num_resects) {
			CompleteReconstructionLogProgress(update_callback,
			                                  (double)total_resects/(num_frames),
			                                  "Bundling...");
			PipelineRoutines::Bundle(tracks, reconstruction);
		}
		std::cout << "Did " << num_resects << " resects.\n";
	}

	// One last pass...
	std::cout << "[InternalCompleteReconstruction] Ran last pass\n";
	num_resects = 0;
	for(int clip = 0; clip < clip_num; clip++) {
		int max_image = tracks.MaxFrame(clip);
		for (int image = 0; image <= max_image; ++image) {
			if (reconstruction->CameraPoseForFrame(clip, image)) {
				LG << "Skipping frame: " << clip << " " << image << "\n";
				continue;
			}
			vector<Marker> all_markers;
			tracks.GetMarkersInFrame(clip, image, &all_markers);

			vector<Marker> reconstructed_markers;
			for (int i = 0; i < all_markers.size(); ++i) {
				if (reconstruction->PointForTrack(all_markers[i].track)) {
					reconstructed_markers.push_back(all_markers[i]);
				}
			}
			if (reconstructed_markers.size() >= 5) {
				CompleteReconstructionLogProgress(update_callback,
				                                  (double)total_resects/(max_image));
				if (PipelineRoutines::Resect(reconstructed_markers,
				                             reconstruction, true,
				                             reconstruction->GetIntrinsicsMap(clip, image))) {
					num_resects++;
					std::cout << "Ran final Resect() for image " << image;
				} else {
					std::cout << "Failed final Resect() for image " << image;
				}
			}
		}
	}
	if (num_resects) {
		CompleteReconstructionLogProgress(update_callback,
		                                  (double)total_resects/(num_frames),
		                                  "Bundling...");
		PipelineRoutines::Bundle(tracks, reconstruction);
	}
	return true;
}

template<typename PipelineRoutines>
double InternalReprojectionError(
        const Tracks &image_tracks,
        const typename PipelineRoutines::Reconstruction &reconstruction,
        const CameraIntrinsics &intrinsics) {
	int num_skipped = 0;
	int num_reprojected = 0;
	double total_error = 0.0;
	vector<Marker> markers;
	image_tracks.GetAllMarkers(&markers);
	for (int i = 0; i < markers.size(); ++i) {
		double weight = markers[i].weight;
		const typename PipelineRoutines::Camera *camera =
		        reconstruction.CameraPoseForFrame(markers[i].clip, markers[i].frame);
		const typename PipelineRoutines::Point *point =
		        reconstruction.PointForTrack(markers[i].track);
		if (!camera || !point || weight == 0.0) {
			num_skipped++;
			continue;
		}
		num_reprojected++;

		Marker reprojected_marker =
		        PipelineRoutines::ProjectMarker(*point, *camera, intrinsics);
		double ex = (reprojected_marker.center[0] - markers[i].center[0]) * weight;
		double ey = (reprojected_marker.center[1] - markers[i].center[1]) * weight;

		const int N = 100;
		char line[N];
		//snprintf(line, N,
		//       "image %-3d track %-3d "
		//       "x %7.1f y %7.1f "
		//       "rx %7.1f ry %7.1f "
		//       "ex %7.1f ey %7.1f"
		//       "    e %7.1f",
		//       markers[i].image,
		//       markers[i].track,
		//       markers[i].center[0],
		//       markers[i].center[1],
		//       reprojected_marker.center[0],
		//       reprojected_marker.center[1],
		//       ex,
		//       ey,
		//       sqrt(ex*ex + ey*ey));
		VLOG(1) << line;

		total_error += sqrt(ex*ex + ey*ey);
	}
	LG << "Skipped " << num_skipped << " markers.";
	LG << "Reprojected " << num_reprojected << " markers.";
	LG << "Total error: " << total_error;
	LG << "Average error: " << (total_error / num_reprojected) << " [pixels].";
	return total_error / num_reprojected;
}

double EuclideanReprojectionError(const Tracks &tracks,
                                  const Reconstruction &reconstruction,
                                  const CameraIntrinsics &intrinsics) {
	return InternalReprojectionError<EuclideanPipelineRoutines>(tracks,
	                                                            reconstruction,
	                                                            intrinsics);
}

bool EuclideanCompleteMultiviewReconstruction(const Tracks &tracks,
                                              Reconstruction *reconstruction,
                                              libmv::ProgressUpdateCallback *update_callback) {
	return InternalCompleteReconstruction<EuclideanPipelineRoutines>(tracks,
	                                                                 reconstruction,
	                                                                 update_callback);
}

void InvertIntrinsicsForTracks(const Tracks &raw_tracks,
                               const CameraIntrinsics &camera_intrinsics,
                               Tracks *calibrated_tracks) {
	vector<Marker> markers;
	raw_tracks.GetAllMarkers(&markers);
	for (int i = 0; i < markers.size(); ++i) {
		double normalized_x, normalized_y;
		camera_intrinsics.InvertIntrinsics(markers[i].center[0], markers[i].center[1],
		                                   &normalized_x, &normalized_y);
		markers[i].center[0] = normalized_x, markers[i].center[1] = normalized_y;
	}
	*calibrated_tracks = Tracks(markers);
}

void EuclideanScaleToUnity(Reconstruction *reconstruction) {
	int clip_num = reconstruction->GetClipNum();
	const vector<vector<CameraPose> >& all_cameras = reconstruction->camera_poses();

	// Calculate center of the mass of all cameras.
	int total_valid_cameras = 0;
	Vec3 cameras_mass_center = Vec3::Zero();
	for(int i = 0; i < clip_num; i++) {
		for (int j = 0; j < all_cameras[i].size(); ++j) {
			if(all_cameras[i][j].clip > 0 && all_cameras[i][j].frame > 0) {
				cameras_mass_center += all_cameras[i][j].t;
				total_valid_cameras++;
			}
		}
	}
	cameras_mass_center /= total_valid_cameras;

	// Find the most distant camera from the mass center.
	double max_distance = 0.0;
	for(int i = 0; i < clip_num; i++) {
		for (int j = 0; j < all_cameras[i].size(); ++j) {
			double distance = (all_cameras[i][j].t - cameras_mass_center).squaredNorm();
			if (distance > max_distance) {
				max_distance = distance;
			}
		}
	}

	if (max_distance == 0.0) {
		std::cout << "Cameras position variance is too small, can not rescale\n";
		return;
	}

	double scale_factor = 1.0 / sqrt(max_distance);

	// Rescale cameras positions.
	for(int i = 0; i < clip_num; i++) {
		for (int j = 0; j < all_cameras[i].size(); ++j) {
			int image = all_cameras[i][j].frame;
			CameraPose *camera = reconstruction->CameraPoseForFrame(i, image);
			camera->t = camera->t * scale_factor;
		}
	}

	// Rescale points positions.
	vector<Point> all_points = reconstruction->AllPoints();
	for (int i = 0; i < all_points.size(); ++i) {
		int track = all_points[i].track;
		Point *point = reconstruction->PointForTrack(track);
		point->X = point->X * scale_factor;
	}
}

}  // namespace mv
