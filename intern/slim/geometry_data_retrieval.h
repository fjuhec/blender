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

#ifndef geometry_data_retrieval_h
#define geometry_data_retrieval_h

#include <stdio.h>
#include <Eigen/Dense>

#include "slim_matrix_transfer.h"
#include "slim.h"
#include "uv_initializer.h"

using namespace Eigen;
using namespace igl;

namespace retrieval {

	struct GeometryData {
		Map<MatrixXd> vertexPositions3D = Map<MatrixXd>(NULL, 0, 0);
		Map<MatrixXd> uvPositions2D = Map<MatrixXd>(NULL, 0, 0);
		MatrixXd positionsOfPinnedVertices2D;
		Map<Matrix<double, Dynamic, Dynamic, RowMajor>> positionsOfExplicitlyPinnedVertices2D = Map<Matrix<double, Dynamic, Dynamic, RowMajor>>(NULL, 0, 0);

		Map<MatrixXi> facesByVertexindices = Map<MatrixXi>(NULL, 0, 0);
		Map<MatrixXi> edgesByVertexindices = Map<MatrixXi>(NULL, 0, 0);
		VectorXi PinnedVertexIndices;
		Map<VectorXi> ExplicitlyPinnedVertexIndices = Map<VectorXi>(NULL, 0);

		Map<VectorXd> edgeLengths = Map<VectorXd>(NULL, 0);
		Map<VectorXi> boundaryVertexIndices = Map<VectorXi>(NULL, 0);
		Map<VectorXf> weightsPerVertex = Map<VectorXf>(NULL, 0);

		int COLUMNS_2 = 2;
		int COLUMNS_3 = 3;
		int numberOfVertices;
		int numberOfFaces;
		int numberOfEdgesTwice;
		int numberOfBoundaryVertices;
		int numberOfPinnedVertices;

		bool withWeightedParameteriztion;
		double weightInfluence;
	};

	void constructSlimData(GeometryData &gd, SLIMData *slimData, bool skipInitialization, int slim_reflection_mode, double relativeScale);

	void retrievePinnedVertices(GeometryData &gd, bool borderVerticesArePinned);

	void retrieveGeometryDataMatrices(const SLIMMatrixTransfer *transferredData,
									  const int uvChartIndex,
									  GeometryData &gd);
}

#endif /* geometry_data_retrieval_h */
