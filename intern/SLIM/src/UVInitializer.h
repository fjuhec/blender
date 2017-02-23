//
//  UVInitializer.hpp
//  Blender
//
//  Created by Aurel Gruber on 28.11.16.
//
//

#ifndef UVInitializer_hpp
#define UVInitializer_hpp

#include <stdio.h>

#include <Eigen/Dense>

#endif /* UVInitializer_hpp */

namespace UVInitializer {

	void uniform_laplacian(const Eigen::MatrixXi &E,
						   const Eigen::VectorXd &EL,
						   const Eigen::VectorXi &bnd,
						   Eigen::MatrixXd &bnd_uv,
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
