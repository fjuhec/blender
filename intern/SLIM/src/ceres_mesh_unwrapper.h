//
//  ceres_mesh_unwrapper.hpp
//  Blender
//
//  Created by Aurel Gruber on 24.02.17.
//
//

#pragma once
#include <Eigen/Dense>
bool solve_map_with_ceres(const Eigen::MatrixXd &Vertex3D, const Eigen::MatrixXi &FaceIndices, Eigen::MatrixXd &UV, int nIterations);
