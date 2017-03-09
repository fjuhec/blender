//
//  matrix_transfer.h
//  Blender
//
//  Created by Aurel Gruber on 21/04/16.
//
//


#ifndef matrix_transfer_h
#define matrix_transfer_h

#include <stdio.h>

/*	AUREL THESIS
	Struct that holds all the information and data matrices to be transfered from the native Blender part to SLIM, named as follows:

	Matrix/Vector	|	contains pointers to arrays of:
	________________|_____________________________________________
	Vmatrices		|	vertex positions
	UVmatrices		|	UV positions of vertices
	PPmatrice		|	positions of pinned vertices
	ELvectors		|	Edge lengths
	Wvectors		|	weights pre vertex
	Fmatrices		|	vertexindex-triplets making up the faces
	Pmatrices		|	indices of pinned vertices
	Ematrix			|	vertexindex-tuples making up edges
	Bvector			|	vertexindices of boundary vertices
	---------------------------------------------------------------
*/
typedef struct {
	int nCharts;
	int *nVerts, *nFaces, *nPinnedVertices, *nBoundaryVertices, *nEdges;

	double **Vmatrices, **UVmatrices, **PPmatrices;
	double **ELvectors;
	float **Wvectors;

	int **Fmatrices, **Pmatrices, **Ematrices;
	int **Bvectors;

	bool fixed_boundary;
	bool pinned_vertices;
	bool with_weighted_parameterization;
	double weight_influence;
	bool transform_islands;
	int slim_reflection_mode;
	double relative_scale;

} matrix_transfer;

#endif /* matrix_transfer_h */

