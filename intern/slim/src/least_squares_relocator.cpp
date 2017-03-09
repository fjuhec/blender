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

#include "least_squares_relocator.h"

#include "slim.h"

#include <Eigen/Dense>
#include <iostream>

using namespace std;
using namespace Eigen;
using namespace igl;

namespace relocator {

	void applyTransformation(SLIMData &slimData, Matrix2d &transformationMatrix){
		for (int i = 0; i < slimData.V_o.rows(); i++){
			slimData.V_o.row(i) = transformationMatrix * slimData.V_o.row(i).transpose();
		}
	}

	void applyTranslation(SLIMData &slimData, Vector2d &translationVector){
		for (int i = 0; i < slimData.V_o.rows(); i++){
			slimData.V_o.row(i) = translationVector.transpose() + slimData.V_o.row(i);
		}
	}

	void retrievePositionsOfPinnedVerticesInInitialization(const MatrixXd &allUVPositionsInInitialization,
														   const VectorXi &indicesOfPinnedVertices,
														   MatrixXd &positionOfPinnedVerticesInInitialization){
		int i = 0;
		for (VectorXi::InnerIterator it(indicesOfPinnedVertices, 0); it; ++it, i++){
			int vertexIndex = it.value();
			positionOfPinnedVerticesInInitialization.row(i) = allUVPositionsInInitialization.row(vertexIndex);
		}
	}

	void flipInputGeometry(SLIMData &slimData){
		//slimData.V.col(0) *= -1;

		VectorXi temp = slimData.F.col(0);
		slimData.F.col(0) = slimData.F.col(2);
		slimData.F.col(2) = temp;

	}

	void catchZeroSingularValue(Vector2d &singularValues, const int largerIndex, const int smallerIndex){
		if (singularValues(smallerIndex) == 0){
			cout << "caught zero valued singular value" << endl;
			singularValues(smallerIndex) = singularValues(largerIndex)/20;
		}
	}

	void orderSingularValues(Vector2d &singularValues, int &largerIndex, int &smallerIndex){
		if (abs(singularValues(smallerIndex)) > abs(singularValues(largerIndex))){
			largerIndex = 1 - largerIndex;
			smallerIndex = 1 - smallerIndex;
		}
	}

	/**
	 * The resulting transformation from a generic least squares relocation can lead to 
	 * - zero valued singular values (e.g. if all pins are on the same line)
	 * - extrem difference in singular values
	 * - reflections of the input (may or may not be acceptable)
	 *
	 * This function handles these issues
	 */
	void adjustSVD(SLIMData &slimData, Matrix2d &transformationMatrix){

		JacobiSVD<Matrix2d> svd(transformationMatrix, ComputeFullU | ComputeFullV);
		Vector2d singularValues = svd.singularValues();
		int largerScaleFactor = 0;
		int smallerScaleFactor = 1;

		orderSingularValues(singularValues, largerScaleFactor, smallerScaleFactor);

		catchZeroSingularValue(singularValues, largerScaleFactor, smallerScaleFactor);

		double scaleFactorDifference = abs(singularValues(largerScaleFactor)) / abs(singularValues(smallerScaleFactor));

		if (scaleFactorDifference > 20){
			cout << "limiting SVD scale factor difference" << endl;
			double scaleFactorCompensation = scaleFactorDifference/20;
			singularValues(smallerScaleFactor) *= scaleFactorCompensation;
		}

		transformationMatrix = svd.matrixU() * singularValues.asDiagonal() * svd.matrixV().transpose();
	}

	/*
	 X has the following structure:
	 
	| x_1 y_1  0   0   1   0|
	|  .   .   .   .   .   .|
	|  .   .   .   .   .   .|
	| x_n y_n  0   0   1   0|
	|  0   0  x_1 y_1  0   1|
	|  .   .   .   .   .   .|
	|  .   .   .   .   .   .|
	|  0   0  x_n y_n  0   1|

	 */
	void computeMatrixX(SLIMData &slimData, MatrixXd &X){
		int numberOfPinnedVertices = slimData.b.rows();

		MatrixXd positionOfPinnedVerticesInInitialization(numberOfPinnedVertices, 2);
		VectorXd onesVector = VectorXd::Ones(numberOfPinnedVertices);

		retrievePositionsOfPinnedVerticesInInitialization(slimData.V_o,
														  slimData.b,
														  positionOfPinnedVerticesInInitialization);

		X.block(0, 0, numberOfPinnedVertices, 2) = positionOfPinnedVerticesInInitialization;
		X.block(numberOfPinnedVertices, 2, numberOfPinnedVertices, 2) = positionOfPinnedVerticesInInitialization;

		X.block(0, 4, numberOfPinnedVertices, 1) = onesVector;
		X.block(numberOfPinnedVertices, 5, numberOfPinnedVertices, 1) = onesVector;
	}


	/*
	 Finds transformation matrix

	 T = |a b|
	 	 |c d|

	 and translation vector

	 s = |e|
	     |f|

	 s.t. if to each point p in the inizialized map the following is applied

	 T*p + s

	 we get the closest transformation of the positions of the vertices in the initialized map to the pinned vertices
	 in a least squares sense. We find them by solving

	 argmin_{t}	Xt = p

	 i.e.:

	 | x_1 y_1  0   0   1   0|           |u_1|
	 |  .   .   .   .   .   .|   |T11|   | . |
	 |  .   .   .   .   .   .|   |T12|   | . |
	 | x_n y_n  0   0   1   0|   |T21|   |u_n|
	 |  0   0  x_1 y_1  0   1| * |T22| = |v_1|
	 |  .   .   .   .   .   .|   |s_1|   | . |
	 |  .   .   .   .   .   .|   |s_2|   | . |
	 |  0   0  x_n y_n  0   1|           |v_n|

	 t is of dimension 1 x 6 and p of dimension 2*numberOfPinnedVertices x 1 
	 is the vector holding the uv positions of the pinned vertices.
	 */

	void computeLeastSquaresTransformation(SLIMData &slimData,
										   Vector2d &translationVector,
										   Matrix2d &transformationMatrix){
		int numberOfPinnedVertices = slimData.b.rows();

		MatrixXd X = MatrixXd::Zero(numberOfPinnedVertices * 2, 6);
		computeMatrixX(slimData, X);

		VectorXd p(2 * numberOfPinnedVertices);
		p.head(numberOfPinnedVertices) = slimData.bc.block(0,0,numberOfPinnedVertices,1);
		p.tail(numberOfPinnedVertices) = slimData.bc.block(0,1,numberOfPinnedVertices,1);;

		VectorXd t = X.colPivHouseholderQr().solve(p);
		transformationMatrix.row(0) = t.head(2);
		transformationMatrix.row(1) = t.segment(2,2);

		translationVector = t.tail(2);

		adjustSVD(slimData, transformationMatrix);

		bool containsReflection = transformationMatrix.determinant() < 0;
		if (containsReflection){
			flipInputGeometry(slimData);
		}
	}

	void computeCentroid(MatrixXd &pointCloud, Vector2d &centroid){
		centroid << pointCloud.col(0).sum(), pointCloud.col(1).sum();
		centroid /= pointCloud.rows();
	};


	/*
	 Finds scaling matrix

	 T = |a 0|
	     |0 a|


	 s.t. if to each point p in the inizialized map the following is applied

	 T*p

	 we get the closest scaling of the positions of the vertices in the initialized map to the pinned vertices
	 in a least squares sense. We find them by solving

	 argmin_{t}	At = p

	 i.e.:

	 | x_1 |           |u_1|
	 |  .  |           | . |
	 |  .  |           | . |
	 | x_n |           |u_n|
	 | y_1 | * | a | = |v_1|
	 |  .  |           | . |
	 |  .  |           | . |
	 | y_n |           |v_n|

	 t is of dimension 1 x 1 and p of dimension 2*numberOfPinnedVertices x 1
	 is the vector holding the uv positions of the pinned vertices.
	 */
	void computeLeastSquaresScaling(MatrixXd centeredPins,
									MatrixXd centeredInitializedPins,
									Matrix2d &transformationMatrix){

		int numberOfPinnedVertices = centeredPins.rows();

		MatrixXd A = MatrixXd::Zero(numberOfPinnedVertices * 2, 1);
		A << centeredInitializedPins.col(0), centeredInitializedPins.col(1);

		VectorXd p(2 * numberOfPinnedVertices);
		p << centeredPins.col(0), centeredPins.col(1);

		VectorXd t = A.colPivHouseholderQr().solve(p);
		t(0) = abs(t(0));
		transformationMatrix << t(0), 0, 0, t(0);
	}

	void computLeastSquaresRotationScaleOnly(SLIMData &slimData,
								   Vector2d &translationVector,
								   Matrix2d &transformationMatrix){

		MatrixXd positionOfInitializedPins(slimData.b.rows(), 2);
		retrievePositionsOfPinnedVerticesInInitialization(slimData.V_o,
														  slimData.b,
														  positionOfInitializedPins);

		Vector2d centroidOfInitialized;
		computeCentroid(positionOfInitializedPins, centroidOfInitialized);

		Vector2d centroidOfPins;
		computeCentroid(slimData.bc, centroidOfPins);

		MatrixXd centeredInitializedPins = positionOfInitializedPins.rowwise().operator-(centroidOfInitialized.transpose());
		MatrixXd centeredpins = slimData.bc.rowwise().operator-(centroidOfPins.transpose());

		MatrixXd S = centeredInitializedPins.transpose() * centeredpins;

		JacobiSVD<MatrixXd> svd(S, ComputeFullU | ComputeFullV);

		Matrix2d VU_T = svd.matrixV() * svd.matrixU().transpose();

		Matrix2d singularValues = Matrix2d::Identity();
		singularValues(1,1) = VU_T.determinant();

		computeLeastSquaresScaling(centeredpins,
									   centeredInitializedPins,
									   transformationMatrix);

		transformationMatrix = transformationMatrix * svd.matrixV() * singularValues * svd.matrixU().transpose();

		translationVector = centroidOfPins - transformationMatrix*centroidOfInitialized;
	}

	void computeTransformationMatrix2Pins(SLIMData &slimData, Matrix2d &transformationMatrix){
		Vector2d pinnedPositionDifferenceVector = slimData.bc.row(0) - slimData.bc.row(1);
		Vector2d initializedPositionDifferenceVector = slimData.V_o.row(slimData.b(0)) - slimData.V_o.row(slimData.b(1));

		double scale = pinnedPositionDifferenceVector.norm()/initializedPositionDifferenceVector.norm();

		pinnedPositionDifferenceVector.normalize();
		initializedPositionDifferenceVector.normalize();

		//TODO: sometimes rotates in wrong direction
		double cosAngle = pinnedPositionDifferenceVector.dot(initializedPositionDifferenceVector);
		double sinAngle = sqrt(1 - pow(cosAngle, 2));

		transformationMatrix << cosAngle, -sinAngle, sinAngle, cosAngle;
		transformationMatrix = (Matrix2d::Identity()*scale) * transformationMatrix;
	}

	void computeTranslation1Pin(SLIMData &slimData, Vector2d &translationVector){
			translationVector = slimData.bc.row(0) - slimData.V_o.row(slimData.b(0));
	}

	void transformInitializedMap(SLIMData &slimData){
		Matrix2d transformationMatrix;
		Vector2d translationVector;

		int numberOfPinnedVertices = slimData.b.rows();

		switch (numberOfPinnedVertices) {
			case 0:
    			cout << "No transformation possible because no pinned vertices exist." << endl;
				return;
			case 1:	// only translation is needed with one pin
				computeTranslation1Pin(slimData, translationVector);
				applyTranslation(slimData, translationVector);
				break;
			case 2:
				computeTransformationMatrix2Pins(slimData, transformationMatrix);
				applyTransformation(slimData, transformationMatrix);
				computeTranslation1Pin(slimData, translationVector);
				applyTranslation(slimData, translationVector);
				break;
			default:

				if (slimData.slim_reflection_mode == 0){
					computeLeastSquaresTransformation(slimData,
													  translationVector,
													  transformationMatrix);
				} else {
					computLeastSquaresRotationScaleOnly(slimData,
											   			translationVector,
											   			transformationMatrix);
				}

				applyTransformation(slimData, transformationMatrix);
				applyTranslation(slimData, translationVector);

    			break;
		}
	}

	bool isTranslationNeeded(SLIMData &slimData){
		bool pinnedVerticesExist = (slimData.b.rows() > 0);
		bool wasInitialized = !slimData.skipInitialization;
		return wasInitialized && pinnedVerticesExist;
	}

	void transformInitializationIfNecessary(SLIMData &slimData){
		if (!isTranslationNeeded(slimData)){
			return;
		}

		transformInitializedMap(slimData);
	}
}
