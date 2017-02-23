//
//  UVInitializer.cpp
//  Blender
//
//  Created by Aurel Gruber on 28.11.16.
//
//

#include "UVInitializer.h"

#include <Eigen/Sparse>
#include "igl/Timer.h"
#include "igl/harmonic.h"


/*	AUREL THESIS
	My own implementation of unfirom laplacian parameterization, for speed comparison and curiosity. Mine is faster then the libigl implementation.
	Unfortunatly it is a very bad initialisation for our use.
 */
void UVInitializer::uniform_laplacian(const Eigen::MatrixXi &E, const Eigen::VectorXd &EL, const Eigen::VectorXi &bnd, Eigen::MatrixXd &bnd_uv, Eigen::MatrixXd &UV){

	igl::Timer timer;
	timer.start();

	int nVerts = UV.rows();
	int nEdges = E.rows();

	int nUnknowns = nVerts - bnd.size();
	int nKnowns = bnd.size();

	Eigen::SparseMatrix<double> Aint(2 * nUnknowns, 2 * nUnknowns);
	Eigen::SparseMatrix<double> Abnd(2 * nUnknowns, 2 * nKnowns);
	Eigen::VectorXd	z(2 * nKnowns);

	std::vector<Eigen::Triplet<double>> intTripletVector;
	std::vector<Eigen::Triplet<double>> bndTripletVector;

	int rowindex;
	int columnindex;
	int i;
	int e;
	double edgeWeight;


	int first, second;

	//cout << "		" << timer.getElapsedTime() << " edge-iteration started" << endl;

	for (e = 0; e < nEdges; e++){
		Eigen::RowVector2i r = E.row(e);

		for (i = 0; i < 2; i++){

			first = r(i);
			second = r((i + 1) % 2);

			if (first >= nKnowns){//into Aint
				rowindex = first - nKnowns;
				edgeWeight = 1; //EL(e);

				intTripletVector.push_back(Eigen::Triplet<double>(rowindex, rowindex, edgeWeight));
				intTripletVector.push_back(Eigen::Triplet<double>(rowindex + nUnknowns, rowindex + nUnknowns, edgeWeight));

				//cout << "inserted into INT cc: " << rowindex << ", " << rowindex << endl;

				if (second >= nKnowns){
					columnindex = second - nKnowns;

					intTripletVector.push_back(Eigen::Triplet<double>(rowindex, columnindex, -edgeWeight));
					intTripletVector.push_back(Eigen::Triplet<double>(rowindex + nUnknowns, columnindex + nUnknowns, -edgeWeight));

					//cout << "inserted into INT rc: " << rowindex << ", " << columnindex << endl;
				} else {
					columnindex = second;
					bndTripletVector.push_back(Eigen::Triplet<double>(rowindex, columnindex, -edgeWeight));
					bndTripletVector.push_back(Eigen::Triplet<double>(rowindex + nUnknowns, columnindex + nKnowns, -edgeWeight));

					//cout << "inserted into BND rc: " << rowindex << ", " << columnindex << endl;
				}
			}
		}
	}


	//cout << "		" << timer.getElapsedTime() << " edge-iteration ended" << endl;

	Aint.setFromTriplets(intTripletVector.begin(), intTripletVector.end());
	Aint.makeCompressed();

	//cout << "		" << timer.getElapsedTime() << " Aint set & compressed" << endl;

	Abnd.setFromTriplets(bndTripletVector.begin(), bndTripletVector.end());
	Abnd.makeCompressed();

	//cout << "		" << timer.getElapsedTime() << " Abnd set & compressed" << endl;

	//cout << " bnd " << endl << bnd << endl;

	//cout << " bnd_uv " << endl << bnd_uv << endl;

	for (int zindex = 0; zindex < nKnowns; zindex++){
		z(zindex) = bnd_uv(bnd(zindex), 0);
		z(zindex + nKnowns) = bnd_uv(bnd(zindex), 1);
	}

	//cout << "		" << timer.getElapsedTime() << " z created" << endl;

	Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
	solver.compute(Aint);


	//cout << "		" << timer.getElapsedTime() << " solver precomputed" << endl;

	Eigen::VectorXd b = Abnd*z;


	//cout << "		" << timer.getElapsedTime() << " b cast" << endl;

	Eigen::VectorXd uvs;
	uvs = solver.solve(-b);

	//cout << "		" << timer.getElapsedTime() << " solved!" << endl;

	Eigen::VectorXd bnd_x = bnd_uv.col(0);
	Eigen::VectorXd bnd_y = bnd_uv.col(1);
	Eigen::VectorXd int_x = uvs.head(uvs.size() / 2);
	Eigen::VectorXd int_y = uvs.tail(uvs.size() / 2);

	if (int_x.rows() > 0){
		UV.col(0) << bnd_x, int_x;
		UV.col(1) << bnd_y, int_y;
	} else {
		UV.col(0) << bnd_x;
		UV.col(1) << bnd_y;
	}

	//cout << "		" << timer.getElapsedTime() << " UV generated" << endl;
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




