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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Aurel Gruber
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include <Eigen/Dense>

#include "slim_c_interface.h"
#include "slim_parametrizer.h"
#include "slim.h"

#include "area_compensation.h"

#include <igl/Timer.h>


using namespace igl;

/*	AUREL THESIS
	Called from the native part during each iteration of interactive parametrisation.
	The blend parameter decides the linear blending between the original UV map and the one
	optained from the accumulated SLIM iterations so far.
 */
void transfer_uvs_blended_C(matrix_transfer *mt, void* slimDataPtr, int uvChartIndex, float blend){
	SLIMData* slimData = (SLIMData*) slimDataPtr;
	Eigen::MatrixXd blendedUvs = getInteractiveResultBlendedWithOriginal(blend, slimData);
	areacomp::correctMapSurfaceAreaIfNecessary(slimData);
	transferUvsBackToNativePart(mt, blendedUvs, uvChartIndex);
}

/*	AUREL THESIS
	Setup call from the native C part. Necessary for interactive parametrisation.
 */
void* setup_slim_C(matrix_transfer *mt, int uvChartIndex, bool borderVerticesArePinned, bool skipInitialization){
	igl::Timer timer;
	timer.start();
	SLIMData* slimData = setup_slim(mt, 0, uvChartIndex, timer, borderVerticesArePinned, skipInitialization);
	return slimData;
}

/*	AUREL THESIS
	Executes a single iteration of SLIM, to be called from the native part. It recasts the pointer to a SLIM object.
 */
void param_slim_single_iteration_C(void* slimDataPtr){
	SLIMData* slimData = (SLIMData*) slimDataPtr;
	param_slim_single_iteration(slimData);
}

void param_slim_C(matrix_transfer *mt, int nIterations, bool borderVerticesArePinned, bool skipInitialization){
	param_slim(mt, nIterations, borderVerticesArePinned, skipInitialization);
}

void free_slim_data_C(void* slimDataPtr){
	free_slim_data((SLIMData*) slimDataPtr);
}
