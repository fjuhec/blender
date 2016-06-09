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
#include "libmv/autotrack/marker.h"
#include "libmv/autotrack/tracks.h"
#include "libmv/numeric/numeric.h"
#include "libmv/logging/logging.h"

using mv::Marker;
using mv::Tracks;

namespace mv {

bool ReconstructTwoFrames(const vector<Marker> &markers,
                          Reconstruction *reconstruction)
{
	if (markers.size() < 16) {
		LG << "Not enough markers to initialize from two frames: " << markers.size();
		return false;
	}

	int image1, image2;
	//GetImagesInMarkers(markers, &image1, &image2);

	//Mat x1, x2;
	//CoordinatesForMarkersInImage(markers, image1, &x1);
	//CoordinatesForMarkersInImage(markers, image2, &x2);

	//Mat3 F;
	//NormalizedEightPointSolver(x1, x2, &F);

	//// The F matrix should be an E matrix, but squash it just to be sure.
	//Mat3 E;
	//FundamentalToEssential(F, &E);

	//// Recover motion between the two images. Since this function assumes a
	//// calibrated camera, use the identity for K.
	//Mat3 R;
	//Vec3 t;
	//Mat3 K = Mat3::Identity();
	//if (!MotionFromEssentialAndCorrespondence(E,
	//                                          K, x1.col(0),
	//                                          K, x2.col(0),
	//                                          &R, &t)) {
	//	LG << "Failed to compute R and t from E and K.";
	//	return false;
	//}

	//// Image 1 gets the reference frame, image 2 gets the relative motion.
	//reconstruction->InsertCamera(image1, Mat3::Identity(), Vec3::Zero());
	//reconstruction->InsertCamera(image2, R, t);

	//LG << "From two frame reconstruction got:\nR:\n" << R
	//   << "\nt:" << t.transpose();
	return true;
}

}  // namespace mv
