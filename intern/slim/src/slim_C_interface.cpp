//
//  slim_C_interface.cpp
//  Blender
//
//  Created by Aurel Gruber on 30.11.16.
//
//

#include <Eigen/Dense>

#include "slim_C_interface.h"
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
