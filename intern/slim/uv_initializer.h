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
