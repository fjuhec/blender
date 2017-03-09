//
//  UVInitializer.hpp
//  Blender
//
//  Created by Aurel Gruber on 28.11.16.
//
//

#ifndef UVInitializer_h
#define UVInitializer_h

#include <stdio.h>

#include <Eigen/Dense>
#include <Eigen/Sparse>


enum Method{TUTTE, HARMONIC, MVC};

namespace UVInitializer {


	void convex_border_parameterization(const Eigen::MatrixXi &F,
										const Eigen::MatrixXd &V,
										const Eigen::MatrixXi &E,
										const Eigen::VectorXd &EL,
										const Eigen::VectorXi &bnd,
										const Eigen::MatrixXd &bnd_uv,
										Eigen::MatrixXd &UV,
										Method method);

	void mvc(
			 const Eigen::MatrixXi &F,
			 const Eigen::MatrixXd &V,
			 const Eigen::MatrixXi &E,
			 const Eigen::VectorXd &EL,
			 const Eigen::VectorXi &bnd,
			 const Eigen::MatrixXd &bnd_uv,
			 Eigen::MatrixXd &UV);

	void harmonic(
				  const Eigen::MatrixXi &F,
				  const Eigen::MatrixXd &V,
				  const Eigen::MatrixXi &E,
				  const Eigen::VectorXd &EL,
				  const Eigen::VectorXi &bnd,
				  const Eigen::MatrixXd &bnd_uv,
				  Eigen::MatrixXd &UV);

	void tutte(
			   const Eigen::MatrixXi &F,
			   const Eigen::MatrixXd &V,
			   const Eigen::MatrixXi &E,
			   const Eigen::VectorXd &EL,
			   const Eigen::VectorXi &bnd,
			   const Eigen::MatrixXd &bnd_uv,
			   Eigen::MatrixXd &UV);

	void harmonic(const Eigen::MatrixXd &V,
				  const Eigen::MatrixXi &F,
				  const Eigen::MatrixXi &B,
				  const Eigen::MatrixXd &bnd_uv,
				  int powerOfHarmonicOperaton,
				  Eigen::MatrixXd &UV);

	int count_flips(const Eigen::MatrixXd& V,
									const Eigen::MatrixXi& F,
									const Eigen::MatrixXd& uv);
}

#endif /* UVInitializer_h */
