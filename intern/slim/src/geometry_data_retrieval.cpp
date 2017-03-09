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

#include "geometry_data_retrieval.h"

#include <iostream>

#include "slim.h"
#include <Eigen/Dense>
#include "slim_matrix_transfer.h"

using namespace igl;
using namespace Eigen;

namespace retrieval {

	void create_weights_per_face(SLIMData *slimData){
		if (!slimData->withWeightedParameterization){
			slimData->weightPerFaceMap = Eigen::VectorXf::Ones(slimData->F.rows());
			return;
		}

		std::cout << "weightmap: " << slimData->weightmap << std::endl;

		slimData->weightPerFaceMap = Eigen::VectorXf(slimData->F.rows());

		//the actual weight is maxFactor^(2 * (mean - 0.5))
		int weightInfluenceSign = (slimData->weightInfluence >= 0) ? 1 : -1;
		double maxFactor = std::abs(slimData->weightInfluence) + 1;
		
		for (int fid = 0; fid < slimData->F.rows(); fid++){
			Eigen::RowVector3i row = slimData->F.row(fid);
			float w1, w2, w3, mean, weightFactor, flippedMean;
			w1 = slimData->weightmap(row(0));
			w2 = slimData->weightmap(row(1));
			w3 = slimData->weightmap(row(2));
			mean = (w1 + w2 + w3) / 3;
			flippedMean = 1 - mean;

			weightFactor = std::pow(maxFactor, weightInfluenceSign * 2 * (flippedMean - 0.5));
			slimData->weightPerFaceMap(fid) = weightFactor;
		}
	}

	void setGeometryDataMatrices(GeometryData &gd, SLIMData *slimData){
		slimData->V = gd.vertexPositions3D;
		slimData->F = gd.facesByVertexindices;
		slimData->b = gd.PinnedVertexIndices;
		slimData->bc = gd.positionsOfPinnedVertices2D;
		slimData->V_o = gd.uvPositions2D;
		slimData->oldUVs = gd.uvPositions2D;
		slimData->weightmap = gd.weightsPerVertex;
		create_weights_per_face(slimData);
	}

	bool hasValidPreinitializedMap(GeometryData &gd){
		if (gd.uvPositions2D.rows() == gd.vertexPositions3D.rows() &&
			gd.uvPositions2D.cols() == gd.COLUMNS_2){

			int numberOfFlips = UVInitializer::count_flips(gd.vertexPositions3D, gd.facesByVertexindices, gd.uvPositions2D);
			bool noFlipsPresent = (numberOfFlips == 0);
			return (noFlipsPresent);
		}
		return false;
	}

	/*
	 If we use interactive parametrisation, we usually start form an existing, flip-free unwrapping.
	 Also, pinning of vertices has some issues with initialisation with convex border.
	 We therefore may want to skip initialization. However, to skip initialization we need a preexisting valid starting map.
	 */
	bool canInitializationBeSkipped(GeometryData &gd, bool skipInitialization){
		return (skipInitialization && hasValidPreinitializedMap(gd));
	}

	void constructSlimData(GeometryData &gd, SLIMData *slimData, bool skipInitialization, int slim_reflection_mode){
		slimData->skipInitialization = canInitializationBeSkipped(gd, skipInitialization);
		slimData->weightInfluence = gd.weightInfluence;
		slimData->slim_reflection_mode = slim_reflection_mode;
		slimData->withWeightedParameterization = gd.withWeightedParameteriztion;
		setGeometryDataMatrices(gd, slimData);

		double penaltyForViolatingPinnedPositions = pow(10,9);
		slimData->soft_const_p = penaltyForViolatingPinnedPositions;
		slimData->slim_energy = SLIMData::SYMMETRIC_DIRICHLET;
	}


	void combineMatricesOfPinnedAndBoundaryVertices(GeometryData &gd){


		// over-allocate pessimistically to avoid multiple reallocation
		int upperBoundOnNumberOfPinnedVertices = gd.numberOfBoundaryVertices + gd.numberOfPinnedVertices;
		gd.PinnedVertexIndices = VectorXi(upperBoundOnNumberOfPinnedVertices);
		gd.positionsOfPinnedVertices2D = MatrixXd(upperBoundOnNumberOfPinnedVertices, gd.COLUMNS_2);

		// since border vertices use vertex indices 0 ... #bordervertices we can do:
		gd.PinnedVertexIndices.segment(0, gd.numberOfBoundaryVertices) = gd.boundaryVertexIndices;
		gd.positionsOfPinnedVertices2D.block(0, 0, gd.numberOfBoundaryVertices, gd.COLUMNS_2) =
		gd.uvPositions2D.block(0, 0, gd.numberOfBoundaryVertices, gd.COLUMNS_2);

		int index = gd.numberOfBoundaryVertices;
		int highestVertexIndex = (gd.boundaryVertexIndices)(index - 1);

		for (Map<VectorXi>::InnerIterator it(gd.ExplicitlyPinnedVertexIndices, 0); it; ++it){
			int vertexIndex = it.value();
			if (vertexIndex > highestVertexIndex){
				gd.PinnedVertexIndices(index) = vertexIndex;
				gd.positionsOfPinnedVertices2D.row(index) = gd.uvPositions2D.row(vertexIndex);
				index++;
			}
		}

		int actualNumberOfPinnedVertices = index;
		gd.PinnedVertexIndices.conservativeResize(actualNumberOfPinnedVertices);
		gd.positionsOfPinnedVertices2D.conservativeResize(actualNumberOfPinnedVertices, gd.COLUMNS_2);

		gd.numberOfPinnedVertices = actualNumberOfPinnedVertices;
	}


	/*
	 If the border is fixed, we simply pin the border vertices additionally to other pinned vertices.
	 */
	void retrievePinnedVertices(GeometryData &gd, bool borderVerticesArePinned){
		if (borderVerticesArePinned){
			combineMatricesOfPinnedAndBoundaryVertices(gd);
		} else {
			gd.PinnedVertexIndices = VectorXi(gd.ExplicitlyPinnedVertexIndices);
			gd.positionsOfPinnedVertices2D = MatrixXd(gd.positionsOfExplicitlyPinnedVertices2D);
		}
	}

	void retrieveGeometryDataMatrices(const SLIMMatrixTransfer *transferredData,
									  const int uvChartIndex,
									  GeometryData &gd){

		gd.numberOfVertices = transferredData->n_verts[uvChartIndex];
		gd.numberOfFaces = transferredData->n_faces[uvChartIndex];
		// nEdges in transferredData accounts for boundary edges only once
		gd.numberOfEdgesTwice = transferredData->n_edges[uvChartIndex] + transferredData->n_boundary_vertices[uvChartIndex];
		gd.numberOfBoundaryVertices = transferredData->n_boundary_vertices[uvChartIndex];
		gd.numberOfPinnedVertices = transferredData->n_pinned_vertices[uvChartIndex];

		new (&gd.vertexPositions3D) Map<MatrixXd>(transferredData->v_matrices[uvChartIndex], gd.numberOfVertices, gd.COLUMNS_3);
		new (&gd.uvPositions2D) Map<MatrixXd>(transferredData->uv_matrices[uvChartIndex], gd.numberOfVertices, gd.COLUMNS_2);
		gd.positionsOfPinnedVertices2D = MatrixXd();

		new (&gd.facesByVertexindices) Map<MatrixXi>(transferredData->f_matrices[uvChartIndex], gd.numberOfFaces, gd.COLUMNS_3);
		new (&gd.edgesByVertexindices) Map<MatrixXi>(transferredData->e_matrices[uvChartIndex], gd.numberOfEdgesTwice, gd.COLUMNS_2);
		gd.PinnedVertexIndices = VectorXi();

		new (&gd.edgeLengths) Map<VectorXd>(transferredData->el_vectors[uvChartIndex], gd.numberOfEdgesTwice);
		new (&gd.boundaryVertexIndices) Map<VectorXi>(transferredData->b_vectors[uvChartIndex], gd.numberOfBoundaryVertices);

		gd.withWeightedParameteriztion = transferredData->with_weighted_parameterization;
		new (&gd.weightsPerVertex) Map<VectorXf>(transferredData->w_vectors[uvChartIndex], gd.numberOfVertices);
		gd.weightInfluence = transferredData->weight_influence;

		if (gd.numberOfPinnedVertices != 0){
			new (&gd.ExplicitlyPinnedVertexIndices) Map<VectorXi>(
											transferredData->p_matrices[uvChartIndex], gd.numberOfPinnedVertices);
			new (&gd.positionsOfExplicitlyPinnedVertices2D) Map<Matrix<double, Dynamic, Dynamic, RowMajor>>(
									transferredData->pp_matrices[uvChartIndex], gd.numberOfPinnedVertices, gd.COLUMNS_2);
		}
	}
}
