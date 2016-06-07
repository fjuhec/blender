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
 *                 Sergey Sharybin
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
