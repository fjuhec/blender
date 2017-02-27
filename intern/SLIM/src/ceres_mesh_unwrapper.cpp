//
//  ceres_mesh_unwrapper.cpp
//  Blender
//
//  Created by Aurel Gruber on 24.02.17.
//
//

#include "ceres_mesh_unwrapper.h"
#include "ceres/ceres.h"
#include <iostream>
#include <math.h>       /* cos */


using namespace std;

template <typename T>
T cross_2D(const Eigen::Matrix<T, 1, 2> &a, const Eigen::Matrix<T, 1, 2> &b){
	return -T(a(0) * b(1) - a(1) * b(0));
}

/**
 *find SVD according to http://scicomp.stackexchange.com/questions/8899/robust-algorithm-for-2x2-svd
 *	-> Alex Eftimiades' answer
 */
template <typename T>
void computeSingularValues(T j11, T j12, T j21, T j22, T& s1, T& s2) {

	T y1 = j12 + j21;
	T x1 = j11 - j22;
	T y2 = j12 - j21;
	T x2 = j11 + j22;

	T h1 = T(sqrt(y1*y1 + x1*x1));
	T h2 = T(sqrt(y2*y2 + x2*x2));

	s1 = (h1 + h2) / T(2.0);
	s2 = (h1 - h2) / T(2.0);

	//cout << "s1: " << s1 << endl;
	//cout << "s2: " << s2 << endl;

}

template <typename T>
void findJacobian(const double* orig_v2D_A, const double* orig_v2D_B, const double* orig_v2D_C,
				  T* new_v2D_A, T* new_v2D_B, T* new_v2D_C,
				  T& j11, T& j12, T& j21, T& j22) {
	/*

	 To find J we can use the points B and C:

	 J = j11	j12
	 j21	j22

	 |j11  j12|   |b1|   |b'1|
	 JB = B'	 ->  |		  | * |  | = |   |
	 |j21  j22|   |b2|   |b'2|

	 -->

	 |j11  j12|   |c1|   |c'1|
	 JC = C'	 ->  |        | * |  | = |   |
	 |j21  j22|   |c2|   |c'2|


	 We know, that c2 is zero, due to the nature of the embedding of the original triangle in 2d

	 j11 * b1 + j12 * b2 = b'1
	 j21 * b1 + j22 * b2 = b'2
	 j11 * c1 = c'1
	 j21 * c1 = c'2

	 Thereofore:

	 j11 = c'1 / c1
	 j21 = c'2 / c1

	 j12 = ( b'1 - j11*b1 ) / b2
	 j22 = ( b'2 - j21*b1 ) / b2

	 */

	j11 = new_v2D_C[0] / T(orig_v2D_C[0]);
	j21 = new_v2D_C[1] / T(orig_v2D_C[0]);

	j12 = ( new_v2D_B[0] - j11 * T(orig_v2D_B[0]) ) / T(orig_v2D_B[1]);
	j22 = ( new_v2D_B[1] - j21 * T(orig_v2D_B[0]) ) / T(orig_v2D_B[1]);
}

void map_3D_triangles_to_2D_undistorded(
										const Eigen::Vector3d &v3DA,
										const Eigen::Vector3d &v3DB,
										const Eigen::Vector3d &v3DC,
										Eigen::Vector2d &v2DAembedded,
										Eigen::Vector2d &v2DBembedded,
										Eigen::Vector2d &v2DCembedded
										) {
	/*
	     b
	 A ______ B
	   \    /
	  a \  / c
	     \/
	     C

	 We place A on 0,0
	 A = 0,0

	 We place C on the x axis
	 C = ||C-A||, 0

	 We compute B via angle

	 b = B - A
	 a = C - A

	 phi =  arccos(a*b)/|a|*|b|

	 B = cos(phi)*b, sin(phi)*b
	 */

	Eigen::Vector3d xUnitVector = Eigen::Vector3d::UnitX();
	Eigen::Vector3d yUnitVector = Eigen::Vector3d::UnitY();

	//translate to origin
	Eigen::Vector3d v3DAembedded;
	v3DAembedded<< 0,0,0;

	Eigen::Vector3d v3DBembedded = v3DB - v3DA;
	Eigen::Vector3d v3DCembedded = v3DC - v3DA;

	// rotate triangle, s.t. C is on x axis
	double angleC_X = std::acos( (v3DCembedded.dot(xUnitVector)) / v3DCembedded.norm() );
	Eigen::Vector3d axis = v3DCembedded.cross(xUnitVector);

	if (axis.norm() > 0) {
		axis.normalize();
		Eigen::AngleAxisd rotationY(angleC_X, axis);

		v3DCembedded = rotationY * v3DCembedded;
		v3DBembedded = rotationY * v3DBembedded;
	}


	// rotate triangle, such that B is in x-y plane
	Eigen::Vector3d v3DBembeddedProjectedZYPlane;
	v3DBembeddedProjectedZYPlane << 0, v3DBembedded(1), v3DBembedded(2);
	double angleB_XY = std::acos( (v3DBembeddedProjectedZYPlane.dot(yUnitVector)) / v3DBembeddedProjectedZYPlane.norm() );
	axis = v3DBembeddedProjectedZYPlane.cross(yUnitVector);

	if (axis.norm() > 0) {

		axis.normalize();
		Eigen::AngleAxisd rotationX(angleB_XY, axis);

		v3DBembedded = rotationX * v3DBembedded;
	}

	v2DAembedded << v3DAembedded(0), v3DAembedded(1);
	v2DBembedded << v3DBembedded(0), v3DBembedded(1);
	v2DCembedded << v3DCembedded(0), v3DCembedded(1);
}

struct DistortionResidual {
	DistortionResidual(const double* v2D_A, const double* v2D_B, const double* v2D_C)
	: orig_v2D_A(v2D_A), orig_v2D_B(v2D_B), orig_v2D_C(v2D_C) {}
	template <typename T>
	bool operator()(
					const T* const new_v2D_A_offset,
					const T* const new_v2D_B_offset,
					const T* const new_v2D_C_offset,
					T* residuals) const {


		/*
		 The Jacobian J represents the mapping from the embedded, undistorted triangles to the distorted triangles of the current iterate:

            A ______ B         A'__________ B'
              \    /	  J		|	    .
               \  /		-->     |.  ^
                \/				C'
                 C

		 Given an ortogonal frame per triangle, we can express any point in this local coordinatesystem. We first place the triangle of the current iterate at the origin:
		 */

		T new_v2D_A[2];
		T new_v2D_B[2];
		T new_v2D_C[2];

		new_v2D_A[0] = T(0);
		new_v2D_A[1] = T(0);
		new_v2D_B[0] = new_v2D_B_offset[0] - new_v2D_A_offset[0];
		new_v2D_B[1] = new_v2D_B_offset[1] - new_v2D_A_offset[1];
		new_v2D_C[0] = new_v2D_C_offset[0] - new_v2D_A_offset[0];
		new_v2D_C[1] = new_v2D_C_offset[1] - new_v2D_A_offset[1];

		/*
		 The unwrapping can be characterised as a linear function w.r.t. the undistorted triangle. Given a point p1 "on" the undistorted triangle, it must hold that

		 J*p1 = q1

		 where q1 is the position of the point after the mapping. Ideally, no distortion is induced. This means J == R, that is some rotation matrix.

		 That means, the singular values of J should all be 1.
		 */

		T j11, j12, j21, j22;
		findJacobian(orig_v2D_A, orig_v2D_B, orig_v2D_C,
					 new_v2D_A, new_v2D_B, new_v2D_C,
					 j11, j12, j21, j22);

		/*
			Given J, we can find the singular values s1, s2:
		 */

		T s1, s2;
		computeSingularValues(j11, j12, j21, j22, s1, s2);


		const Eigen::Map<const Eigen::Matrix<T, 1, 2> > ev1(new_v2D_A_offset);
		const Eigen::Map<const Eigen::Matrix<T, 1, 2> > ev2(new_v2D_B_offset);
		const Eigen::Map<const Eigen::Matrix<T, 1, 2> > ev3(new_v2D_C_offset);

		Eigen::Matrix<T, 1, 2> e21 = ev1 - ev2;
		Eigen::Matrix<T, 1, 2> e23 = ev3 - ev2;

		T area_2 = cross_2D(e21, e23);

		if (area_2 < T(0.0)){
			cout << "area negative! " << endl;
			return false;
		}

		residuals[0] = s1;
		residuals[1] = s2;
		residuals[2] = T(1.0) / s1;
		residuals[3] = T(1.0) / s2;

		return true;
	}
	const double* orig_v2D_A;
	const double* orig_v2D_B;
	const double* orig_v2D_C;
};

bool solve_map_with_ceres(const Eigen::MatrixXd &Vertex3D, const Eigen::MatrixXi &FaceIndices, Eigen::MatrixXd &UV, int nIterations){
	int num_faces = FaceIndices.rows();

	Eigen::Matrix<double, Eigen::Dynamic, 2, Eigen::RowMajor> vertices2d = UV;
	Eigen::Matrix<double, Eigen::Dynamic, 3, Eigen::RowMajor> vertices3d = Vertex3D;
	Eigen::Matrix<double, Eigen::Dynamic, 2, Eigen::RowMajor> vertices2DEmbedded(num_faces*3, 2);

	ceres::Problem problem;
	for (int i = 0; i < num_faces; ++i) {

		// computed undistorted embedding
		Eigen::Vector2d vertex2DAEmbedded;
		Eigen::Vector2d vertex2DBEmbedded;
		Eigen::Vector2d vertex2DCEmbedded;

		map_3D_triangles_to_2D_undistorded(vertices3d.row(FaceIndices(i, 0)),
										   vertices3d.row(FaceIndices(i, 1)),
										   vertices3d.row(FaceIndices(i, 2)),
										   vertex2DAEmbedded,
										   vertex2DBEmbedded,
										   vertex2DCEmbedded);

		vertices2DEmbedded.row(3*i) = vertex2DAEmbedded;
		vertices2DEmbedded.row(3*i+1) = vertex2DBEmbedded;
		vertices2DEmbedded.row(3*i+2) = vertex2DCEmbedded;

		problem.AddResidualBlock(
			new ceres::AutoDiffCostFunction<DistortionResidual, 4, 2, 2, 2>(
				new DistortionResidual(
						&vertices2DEmbedded(3*i, 0),
						&vertices2DEmbedded(3*i+1, 0),
						&vertices2DEmbedded(3*i+2, 0)
				)
			),
			NULL,
			&vertices2d(FaceIndices(i, 0), 0),
			&vertices2d(FaceIndices(i, 1), 0),
			&vertices2d(FaceIndices(i, 2), 0));
	}

	// Make Ceres automatically detect the bundle structure. Note that the
	// standard solver, SPARSE_NORMAL_CHOLESKY, also works fine but it is slower
	// for standard bundle adjustment problems.
	ceres::Solver::Options options;
	options.max_num_iterations = nIterations;
	options.linear_solver_type = ceres::CGNR;
	options.sparse_linear_algebra_library_type = ceres::EIGEN_SPARSE;
	options.minimizer_progress_to_stdout = true;
	ceres::Solver::Summary summary;
	ceres::Solve(options, &problem, &summary);
	std::cout << summary.FullReport() << "\n";
	
	UV = vertices2d;
	return true;
}
