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

#include <iostream>
#include <stdlib.h>

#include "slim.h"
#include "uv_initializer.h"
#include "area_compensation.h"
#include "least_squares_relocator.h"
#include "geometry_data_retrieval.h"

#include "igl/Timer.h"

#include "igl/map_vertices_to_circle.h"
#include "doublearea.h"

#include <string>
#include <vector>

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>

#include "slim_parametrizer.h"

using namespace std;
using namespace igl;
using namespace Eigen;

void transferUvsBackToNativePartLive(SLIMMatrixTransfer *mt,
									 Eigen::MatrixXd &UV,
									 int n_pins,
									 int n_selected_pins,
									 int *selected_pins_indices,
									 int *pin_indices,
									 double *pin_positions,
									 int uvChartIndex){

	double *uvCoordinateArray = mt->uv_matrices[uvChartIndex];
	int numberOfVertices = mt->n_verts[uvChartIndex];

	double u, v;

	for (int i = 0; i < numberOfVertices; i++) {

		while (*selected_pins_indices < i) {
			++selected_pins_indices;
		}

		while (*pin_indices < i) {
			++pin_indices;
			pin_positions += 2;
		}

		if (*selected_pins_indices == i && *pin_indices == i) {
			u = *(pin_positions++);
			v = *(pin_positions++);
			++pin_indices;
		} else {
			u = UV(i,0);
			v = UV(i,1);
		}

		*(uvCoordinateArray++) = u;
		*(uvCoordinateArray++) = v;
	}
}

void transferUvsBackToNativePart(SLIMMatrixTransfer *mt, Eigen::MatrixXd &UV, int uvChartIndex){
	double *uvCoordinateArray;
	uvCoordinateArray = mt->uv_matrices[uvChartIndex];
	int numberOfVertices = mt->n_verts[uvChartIndex];

	for (int i = 0; i < numberOfVertices; i++) {
		for (int j = 0; j < 2; j++) {
			uvCoordinateArray[i * 2 + j] = UV(i, j);
		}
	}
}

Eigen::MatrixXd getInteractiveResultBlendedWithOriginal(float blend, SLIMData *slimData){
	Eigen::MatrixXd originalMapWeighted = blend * slimData->oldUVs;
	Eigen::MatrixXd InteractiveResultMap = (1.0 - blend) * slimData->V_o;
	return originalMapWeighted + InteractiveResultMap;
}

/*
	Executes a single iteration of SLIM, must follow a proper setup & initialisation.
 */
void param_slim_single_iteration(SLIMData *slimData){
	int numberOfIterations = 1;
	slim_solve(*slimData, numberOfIterations);
}

static void adjustPins(SLIMData *slimData, int n_pins, int* pinnedVertexIndices, double *pinnedVertexPositions2D, int n_selected_pins, int *selected_pins){
	Eigen::VectorXi oldPinIndices = slimData->b;
	Eigen::MatrixXd oldPinPositions = slimData->bc;

	slimData->b.resize(n_pins);
	slimData->bc.resize(n_pins, 2);

	int oldPinPointer = 0;
	int newPinPointer = 0;
	int selectedPinPointer = 0;

	while (newPinPointer < n_pins){

		int pinnedVertexIndex = pinnedVertexIndices[newPinPointer];
		slimData->b(newPinPointer) = pinnedVertexIndex;

		while (oldPinIndices(oldPinPointer) < pinnedVertexIndex) {
			++oldPinPointer;
			if (oldPinPointer == oldPinIndices.size()){
				break;
			}
		}

		while (selected_pins[selectedPinPointer] < pinnedVertexIndex) {
			++selectedPinPointer;
			if (selectedPinPointer == n_selected_pins){
				break;
			}
		}

		if (!(pinnedVertexIndex == selected_pins[selectedPinPointer]) && oldPinIndices(oldPinPointer) == pinnedVertexIndex) {
			slimData->bc.row(newPinPointer) = oldPinPositions.row(oldPinPointer);
		} else {
			slimData->bc(newPinPointer, 0) = pinnedVertexPositions2D[2*newPinPointer];
			slimData->bc(newPinPointer, 1) = pinnedVertexPositions2D[2*newPinPointer+1];
		}

		++newPinPointer;
	}
}

/*
	Executes several iterations of SLIM when used with LiveUnwrap
 */

void param_slim_live_unwrap(SLIMData *slimData, int n_pins, int* pinnedVertexIndices, double *pinnedVertexPositions2D, int n_selected_pins, int *selected_pins) {
	int numberOfIterations = 3;
	adjustPins(slimData, n_pins, pinnedVertexIndices, pinnedVertexPositions2D, n_selected_pins, selected_pins);
	// recompute current energy
	//recompute_energy(*slimData);
	slim_solve(*slimData, numberOfIterations);
}


void param_slim(SLIMMatrixTransfer *mt, int nIterations, bool borderVerticesArePinned, bool skipInitialization){

	igl::Timer timer;
	timer.start();

	for (int uvChartIndex = 0; uvChartIndex < mt->n_charts; uvChartIndex++) {

		SLIMData *slimData = setup_slim(mt, nIterations, uvChartIndex, timer, borderVerticesArePinned, skipInitialization);

		slim_solve(*slimData, nIterations);

		areacomp::correctMapSurfaceAreaIfNecessary(slimData);

		transferUvsBackToNativePart(mt, slimData->V_o, uvChartIndex);

		free_slim_data(slimData);
	}

};

void initializeUvs(retrieval::GeometryData &gd, SLIMData *slimData){

	MatrixXd vertexPositions2D = slimData->V;
	MatrixXi facesByVertexIndices = slimData->F;
	VectorXi boundaryVertexIndices = gd.boundaryVertexIndices;
	MatrixXd uvPositions2D = slimData->V_o;

	Eigen::MatrixXd uvPositionsOfBoundary(boundaryVertexIndices.rows(), 2);
	UVInitializer::mapVerticesToConvexBorder(uvPositionsOfBoundary);

	bool allVerticesOnBoundary = (slimData->V_o.rows() == uvPositionsOfBoundary.rows());
	if (allVerticesOnBoundary){
		slimData->V_o = uvPositionsOfBoundary;
		return;
	}

	 UVInitializer::mvc(
		gd.facesByVertexindices,
		gd.vertexPositions3D,
		gd.edgesByVertexindices,
	 	gd.edgeLengths,
	 	boundaryVertexIndices,
	 	uvPositionsOfBoundary,
	 	slimData->V_o);
}

void initializeIfNeeded(retrieval::GeometryData &gd, SLIMData *slimData){
	if (!slimData->skipInitialization){
		initializeUvs(gd, slimData);
	}
}

/*
	Transfers all the matrices from the native part and initialises slim.
 */
SLIMData* setup_slim(SLIMMatrixTransfer *transferredData,
					 int nIterations,
					 int uvChartIndex,
					 igl::Timer &timer,
					 bool borderVerticesArePinned,
					 bool skipInitialization){

	retrieval::GeometryData geometryData;
	retrieval::retrieveGeometryDataMatrices(transferredData, uvChartIndex, geometryData);

	retrieval::retrievePinnedVertices(geometryData, borderVerticesArePinned);
	transferredData->n_pinned_vertices[uvChartIndex] = geometryData.numberOfPinnedVertices;

	SLIMData *slimData = new SLIMData();
	retrieval::constructSlimData(geometryData, slimData, skipInitialization, transferredData->reflection_mode, transferredData->relative_scale);
	slimData->nIterations = nIterations;

	initializeIfNeeded(geometryData, slimData);
	relocator::transformInitializationIfNecessary(*slimData);

	areacomp::correctMeshSurfaceAreaIfNecessary(slimData);

	slim_precompute(slimData->V,
					slimData->F,
					slimData->V_o,
					*slimData,
					slimData->slim_energy,
					slimData->b,
					slimData->bc,
					slimData->soft_const_p);

	return slimData;
}

void free_slim_data(SLIMData *slimData){
	delete slimData;
};
