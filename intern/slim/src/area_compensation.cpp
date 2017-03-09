//
//  areaCompensator.cpp
//  Blender
//
//  Created by Aurel Gruber on 05.12.16.
//
//

#include "slim.h"

#include <doublearea.h>
#include <Eigen/Dense>

using namespace Eigen;
using namespace igl;

namespace areacomp {

	void correctGeometrySize(double surfaceAreaToMapAreaRatio, MatrixXd &VertexPositions, double desiredSurfaceAreaToMapRation){
		assert(surfaceAreaToMapAreaRatio > 0);
		double sqrtOfRatio = sqrt(surfaceAreaToMapAreaRatio/desiredSurfaceAreaToMapRation);
		VertexPositions = VertexPositions / sqrtOfRatio;
	}

	template<typename VertexPositionType, typename FaceIndicesType>
	double computeSurfaceArea(VertexPositionType V, FaceIndicesType F){
		Eigen::VectorXd doubledAreaOfTriangles;
		igl::doublearea(V, F, doubledAreaOfTriangles);
		double areaOfMap = doubledAreaOfTriangles.sum() / 2;
		return areaOfMap;
	}

	void correctMapSurfaceAreaIfNecessary(SLIMData *slimData){

		bool meshSurfaceAreaWasCorrected = (slimData->expectedSurfaceAreaOfResultingMap != 0);
		int numberOfPinnedVertices = slimData->b.rows();
		bool noPinnedVerticesExist = numberOfPinnedVertices == 0;

		bool needsAreaCorrection = meshSurfaceAreaWasCorrected && noPinnedVerticesExist;
		if(!needsAreaCorrection){
			return;
		}

		double areaOfresultingMap = computeSurfaceArea(slimData->V_o, slimData->F);
		if (!areaOfresultingMap){
			return;
		}

		double resultingAreaToExpectedAreaRatio = areaOfresultingMap / slimData->expectedSurfaceAreaOfResultingMap;
		double desiredRatio = 1.0;
		correctGeometrySize(resultingAreaToExpectedAreaRatio, slimData->V_o, desiredRatio);
	}

	void correctMeshSurfaceAreaIfNecessary(SLIMData *slimData, double relative_scale){
		int numberOfPinnedVertices = slimData->b.rows();
		bool pinnedVerticesExist = numberOfPinnedVertices > 0;
		bool needsAreaCorrection = 	slimData->skipInitialization || pinnedVerticesExist;

		if(!needsAreaCorrection){
			return;
		}
		//TODO: should instead compare area of convex hull / size of convex hull of both major and minor axis
		double areaOfPreinitializedMap = computeSurfaceArea(slimData->V_o, slimData->F);
		if (!areaOfPreinitializedMap){
			return;
		}

		if (areaOfPreinitializedMap < 0){
			areaOfPreinitializedMap *= -1;
		}

		slimData->expectedSurfaceAreaOfResultingMap = areaOfPreinitializedMap;
		double surfaceAreaOf3DMesh = computeSurfaceArea(slimData->V, slimData->F);
		double surfaceAreaToMapAreaRatio = surfaceAreaOf3DMesh / areaOfPreinitializedMap;

		double desiredRatio = relative_scale;
		correctGeometrySize(surfaceAreaToMapAreaRatio, slimData->V, desiredRatio);
	}
}
