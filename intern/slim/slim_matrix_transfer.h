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

#ifndef slim_matrix_transfer_h
#define slim_matrix_transfer_h

#include <stdio.h>

/*	Struct that holds all the information and data matrices to be transfered from the native Blender part to SLIM, named as follows:

	Matrix/Vector	|	contains pointers to arrays of:
	________________|_____________________________________________
	v_matrices		|	vertex positions
	uv_matrices		|	UV positions of vertices
	PPmatrice		|	positions of pinned vertices
	el_vectors		|	Edge lengths
	w_vectors		|	weights pre vertex
	f_matrices		|	vertexindex-triplets making up the faces
	p_matrices		|	indices of pinned vertices
	Ematrix			|	vertexindex-tuples making up edges
	Bvector			|	vertexindices of boundary vertices
	---------------------------------------------------------------
*/

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int n_charts;
	int *n_verts, *n_faces, *n_pinned_vertices, *n_boundary_vertices, *n_edges;

	double **v_matrices, **uv_matrices, **pp_matrices;
	double **el_vectors;
	float **w_vectors;

	int **f_matrices, **p_matrices, **e_matrices;
	int **b_vectors;

	bool fixed_boundary;
	bool pinned_vertices;
	bool with_weighted_parameterization;
	double weight_influence;
	bool transform_islands;
	int slim_reflection_mode;
	double relative_scale;

} SLIMMatrixTransfer;

#ifdef __cplusplus
}
#endif

#endif /* slim_matrix_transfer_h */

