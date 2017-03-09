#ifndef slim_parametrizer_h
#define slim_parametrizer_h


#include "matrix_transfer.h"
#include "slim.h"
#include <igl/Timer.h>

using namespace igl;

/*	AUREL THESIS
	The header file that exposes the C++ functions to the native C part of Blender, see thesis.
*/

Eigen::MatrixXd getInteractiveResultBlendedWithOriginal(float blend, SLIMData *slimData);
SLIMData* setup_slim(matrix_transfer *transferredData,
					 	 int nIterations,
						 int uvChartIndex,
						 igl::Timer &timer,
						 bool borderVerticesArePinned,
						 bool skipInitialization);
void transferUvsBackToNativePart(matrix_transfer *mt, Eigen::MatrixXd &UV, int uvChartIndex);
void param_slim_single_iteration(SLIMData *slimData);
void param_slim(matrix_transfer *mt, int n_iterations, bool fixBorder, bool skipInitialization);
void free_slim_data(SLIMData *slimData);
#endif // !slim_parametrizer_h
