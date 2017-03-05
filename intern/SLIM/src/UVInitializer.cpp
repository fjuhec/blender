//
//  UVInitializer.cpp
//  Blender
//
//  Created by Aurel Gruber on 28.11.16.
//
//

#include "UVInitializer.h"
#include<Eigen/SparseLU>

#include "igl/Timer.h"
#include "igl/harmonic.h"
#include "cotmatrix.h"
#include <iostream>

using namespace std;

double computeAngle(const Eigen::Vector3d &a,const Eigen::Vector3d &b){
	cout << "points for angle " << a << "   and    " << b << endl;
	double angle = acos(a.dot(b) / (a.norm() * b.norm()));
	return angle;
}

void findVertexToOppositeAnglesCorrespondence(const Eigen::MatrixXi &F,const Eigen::MatrixXd &V, Eigen::SparseMatrix<double> &vertexToFaceIndices){

	typedef Eigen::Triplet<double> T;
	std::vector<T> coefficients;

	for (int i = 0; i < F.rows(); i++){

		int vertexIndex1 = F(i,0); // retrieve vertex index that is part of face
		int vertexIndex2 = F(i,1); // retrieve vertex index that is part of face
		int vertexIndex3 = F(i,2); // retrieve vertex index that is part of face

		double angle1 = computeAngle(V.row(vertexIndex2) - V.row(vertexIndex1), V.row(vertexIndex3) - V.row(vertexIndex1));
		double angle2 = computeAngle(V.row(vertexIndex3) - V.row(vertexIndex2), V.row(vertexIndex1) - V.row(vertexIndex2));
		double angle3 = computeAngle(V.row(vertexIndex1) - V.row(vertexIndex3), V.row(vertexIndex2) - V.row(vertexIndex3));

		cout << "vertex " << V.row(vertexIndex1) << " has angle " << angle1 << "for neighbors " << V.row(vertexIndex2) << " and" << V.row(vertexIndex3) << endl << endl;
		cout << "vertex " << V.row(vertexIndex2) << " has angle " << angle2 << "for neighbors " << V.row(vertexIndex3) << " and" << V.row(vertexIndex1) << endl << endl;
		cout << "vertex " << V.row(vertexIndex3) << " has angle " << angle3 << "for neighbors " << V.row(vertexIndex1) << " and" << V.row(vertexIndex2) << endl << endl;

		coefficients.push_back(T(vertexIndex1, 2*vertexIndex2, angle3));
		coefficients.push_back(T(vertexIndex1, 2*vertexIndex3 + 1, angle2));

		coefficients.push_back(T(vertexIndex2, 2*vertexIndex1 + 1, angle3));
		coefficients.push_back(T(vertexIndex2, 2*vertexIndex3, angle1));

		coefficients.push_back(T(vertexIndex3, 2*vertexIndex1, angle2));
		coefficients.push_back(T(vertexIndex3, 2*vertexIndex2 + 1, angle1));

	}

	vertexToFaceIndices.setFromTriplets(coefficients.begin(), coefficients.end());
}

void findVertexToItsAnglesCorrespondence(const Eigen::MatrixXi &F,const Eigen::MatrixXd &V, Eigen::SparseMatrix<double> &vertexToFaceIndices){

	typedef Eigen::Triplet<double> T;
	std::vector<T> coefficients;

	for (int i = 0; i < F.rows(); i++){

		int vertexIndex1 = F(i,0); // retrieve vertex index that is part of face
		int vertexIndex2 = F(i,1); // retrieve vertex index that is part of face
		int vertexIndex3 = F(i,2); // retrieve vertex index that is part of face

		double angle1 = computeAngle(V.row(vertexIndex2) - V.row(vertexIndex1), V.row(vertexIndex3) - V.row(vertexIndex1));
		double angle2 = computeAngle(V.row(vertexIndex3) - V.row(vertexIndex2), V.row(vertexIndex1) - V.row(vertexIndex2));
		double angle3 = computeAngle(V.row(vertexIndex1) - V.row(vertexIndex3), V.row(vertexIndex2) - V.row(vertexIndex3));

		coefficients.push_back(T(vertexIndex1, 2*vertexIndex2, angle1));
		coefficients.push_back(T(vertexIndex1, 2*vertexIndex3 + 1, angle1));

		coefficients.push_back(T(vertexIndex2, 2*vertexIndex1 + 1, angle2));
		coefficients.push_back(T(vertexIndex2, 2*vertexIndex3, angle2));

		coefficients.push_back(T(vertexIndex3, 2*vertexIndex1, angle3));
		coefficients.push_back(T(vertexIndex3, 2*vertexIndex2 + 1, angle3));

	}

	vertexToFaceIndices.setFromTriplets(coefficients.begin(), coefficients.end());
}

/*	AUREL THESIS
	My own implementation of unfirom laplacian parameterization, for speed comparison and curiosity. Mine is faster then the libigl implementation.
	Unfortunatly it is a very bad initialisation for our use.
 */
void UVInitializer::uniform_laplacian(const Eigen::MatrixXi &F, const Eigen::MatrixXd &V, const Eigen::MatrixXi &E, const Eigen::VectorXd &EL, const Eigen::VectorXi &bnd, Eigen::MatrixXd &bnd_uv, Eigen::MatrixXd &UV, Eigen::MatrixXd &CotMatrix){

	enum Method{TUTTE, HARMONIC, MVC};
	Method method = HARMONIC;

	int nVerts = UV.rows();
	int nEdges = E.rows();

	Eigen::SparseMatrix<double> vertexToAngles(nVerts, nVerts*2);

	switch (method){
		case HARMONIC:
			findVertexToOppositeAnglesCorrespondence(F, V, vertexToAngles);
			break;
		case MVC:
			findVertexToItsAnglesCorrespondence(F, V, vertexToAngles);
			break;
		case TUTTE:
			break;
	}

	int nUnknowns = nVerts - bnd.size();
	int nKnowns = bnd.size();

	Eigen::SparseMatrix<double> Aint(nUnknowns, nUnknowns);
	Eigen::SparseMatrix<double> Abnd(nUnknowns, nKnowns);
	Eigen::VectorXd	z(nKnowns);

	std::vector<Eigen::Triplet<double>> intTripletVector;
	std::vector<Eigen::Triplet<double>> bndTripletVector;

	int rowindex;
	int columnindex;
	double edgeWeight, edgeLength;
	Eigen::RowVector2i edge;

	int firstVertex, secondVertex;

	for (int e = 0; e < nEdges; e++){
		edge = E.row(e);
		edgeLength = EL(e);
		firstVertex = edge(0);
		secondVertex = edge(1);

		if (firstVertex >= nKnowns){//into Aint

			rowindex = firstVertex - nKnowns;

			double angle1 = vertexToAngles.coeff(firstVertex, 2*secondVertex);
			double angle2 = vertexToAngles.coeff(firstVertex, 2*secondVertex+1);

			switch (method){
				case HARMONIC:
					edgeWeight = 1/tan(angle1) + 1/tan(angle2);
					break;
				case MVC:
					edgeWeight = tan(angle1/2) + tan(angle2/2);
					edgeWeight /= edgeLength;
					break;
				case TUTTE:
					edgeWeight = 1;
					break;
			}

			cout << "edgeWeight " << edgeWeight << "r.norm: "<< edgeLength << endl;

			intTripletVector.push_back(Eigen::Triplet<double>(rowindex, rowindex, edgeWeight));

			if (secondVertex >= nKnowns){ // also an unknown point in the interior
				columnindex = secondVertex - nKnowns;

				intTripletVector.push_back(Eigen::Triplet<double>(rowindex, columnindex, -edgeWeight));

			} else { // known point on the border
				columnindex = secondVertex;
				bndTripletVector.push_back(Eigen::Triplet<double>(rowindex, columnindex, edgeWeight));
			}

		}
	}


	Aint.setFromTriplets(intTripletVector.begin(), intTripletVector.end());
	Aint.makeCompressed();

	Abnd.setFromTriplets(bndTripletVector.begin(), bndTripletVector.end());
	Abnd.makeCompressed();

	//cout << "Aint before: " << endl << Aint.toDense() << endl;
	//cout << "Abnd before: " << endl << Abnd.toDense() << endl;

	for (int i = 0; i < nUnknowns; i++){
		double factor = Aint.coeff(i, i);
		Aint.row(i) /= factor;
		Abnd.row(i) /= factor;
	}

	//cout << "Aint: " << endl << Aint.toDense() << endl;
	//cout << "Abnd: " << endl << Abnd.toDense() << endl;

	Eigen::SparseLU<Eigen::SparseMatrix<double>> solver;
	solver.compute(Aint);

	for (int i = 0; i<2; i++){// solve for u and v coordinates

		for (int zindex = 0; zindex < nKnowns; zindex++){
			z(zindex) = bnd_uv(bnd(zindex), i);
		}

		Eigen::VectorXd b = Abnd*z;

		Eigen::VectorXd uvs;
		uvs = solver.solve(b);

		Eigen::VectorXd boundary = bnd_uv.col(i);
		Eigen::VectorXd interior = uvs;

		UV.col(i) << boundary, interior;
	}
}

void UVInitializer::harmonic(const Eigen::MatrixXd &V,
							 const Eigen::MatrixXi &F,
							 const Eigen::MatrixXi &B,
							 const Eigen::MatrixXd &bnd_uv,
							 int powerOfHarmonicOperaton,
							 Eigen::MatrixXd &UV){

	igl::harmonic(V, F, B, bnd_uv, powerOfHarmonicOperaton, UV);
}

void get_flips(const Eigen::MatrixXd& V,
							  const Eigen::MatrixXi& F,
							  const Eigen::MatrixXd& uv,
							  std::vector<int>& flip_idx) {
	flip_idx.resize(0);
	for (int i = 0; i < F.rows(); i++) {

		Eigen::Vector2d v1_n = uv.row(F(i,0)); Eigen::Vector2d v2_n = uv.row(F(i,1)); Eigen::Vector2d v3_n = uv.row(F(i,2));

		Eigen::MatrixXd T2_Homo(3,3);
		T2_Homo.col(0) << v1_n(0),v1_n(1),1;
		T2_Homo.col(1) << v2_n(0),v2_n(1),1;
		T2_Homo.col(2) << v3_n(0),v3_n(1),1;
		double det = T2_Homo.determinant();
		assert (det == det);
		if (det < 0) {
			//cout << "flip at face #" << i << " det = " << T2_Homo.determinant() << endl;
			flip_idx.push_back(i);
		}
	}
}

int UVInitializer::count_flips(const Eigen::MatrixXd& V,
				const Eigen::MatrixXi& F,
				const Eigen::MatrixXd& uv) {

	std::vector<int> flip_idx;
	get_flips(V,F,uv,flip_idx);

	return flip_idx.size();
}




