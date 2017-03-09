#include <iostream>
#include <stdlib.h>

#include "slim.h"
#include "uv_initializer.h"
#include "area_compensation.h"
#include "least_squares_relocator.h"
#include "geometry_data_retrieval.h"

#include "igl/Timer.h"

#include "igl/map_vertices_to_circle.h"
#include "igl/harmonic.h"
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


void transferUvsBackToNativePart(matrix_transfer *mt, Eigen::MatrixXd &UV, int uvChartIndex){
	double *uvCoordinateArray;
	uvCoordinateArray = mt->UVmatrices[uvChartIndex];
	int numberOfVertices = mt->nVerts[uvChartIndex];

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

void param_slim(matrix_transfer *mt, int nIterations, bool borderVerticesArePinned, bool skipInitialization){

	igl::Timer timer;
	timer.start();

	for (int uvChartIndex = 0; uvChartIndex < mt->nCharts; uvChartIndex++) {

		SLIMData *slimData = setup_slim(mt, nIterations, uvChartIndex, timer, borderVerticesArePinned, skipInitialization);

		//slim_solve(*slimData, nIterations);

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

	Eigen::MatrixXd uvPositionsOfBoundary;
	igl::map_vertices_to_circle(vertexPositions2D, boundaryVertexIndices, uvPositionsOfBoundary);

	bool allVerticesOnBoundary = (slimData->V_o.rows() == uvPositionsOfBoundary.rows());
	if (allVerticesOnBoundary){
		slimData->V_o = uvPositionsOfBoundary;
		return;
	}


	Eigen::MatrixXd CotMatrix;
	igl::cotmatrix_entries(Eigen::MatrixXd(gd.vertexPositions3D), Eigen::MatrixXi(gd.facesByVertexindices), CotMatrix);

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
SLIMData* setup_slim(matrix_transfer *transferredData,
					 int nIterations,
					 int uvChartIndex,
					 igl::Timer &timer,
					 bool borderVerticesArePinned,
					 bool skipInitialization){

	retrieval::GeometryData geometryData;
	retrieval::retrieveGeometryDataMatrices(transferredData, uvChartIndex, geometryData);

	retrieval::retrievePinnedVertices(geometryData, borderVerticesArePinned);
	transferredData->nPinnedVertices[uvChartIndex] = geometryData.numberOfPinnedVertices;

	SLIMData *slimData = new SLIMData();
	retrieval::constructSlimData(geometryData, slimData, skipInitialization, transferredData->slim_reflection_mode);
	slimData->nIterations = nIterations;

	initializeIfNeeded(geometryData, slimData);
	relocator::transformInitializationIfNecessary(*slimData);

	areacomp::correctMeshSurfaceAreaIfNecessary(slimData, transferredData->relative_scale);

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
