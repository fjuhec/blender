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
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Lukas Toenne
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __BKE_HAIR_H__
#define __BKE_HAIR_H__

/** \file blender/blenkernel/BKE_hair.h
 *  \ingroup bke
 */

#include "BLI_utildefines.h"

struct HairFollicle;
struct HairPattern;
struct DerivedMesh;

static const unsigned int STRAND_INDEX_NONE = 0xFFFFFFFF;

struct HairPattern* BKE_hair_new(void);
struct HairPattern* BKE_hair_copy(struct HairPattern *hair);
void BKE_hair_free(struct HairPattern *hair);

void BKE_hair_set_num_follicles(struct HairPattern *hair, int count);
void BKE_hair_follicles_generate(struct HairPattern *hair, struct DerivedMesh *scalp, int count, unsigned int seed);

/* ================================= */

typedef struct HairFiber {
	/* Sample on the scalp mesh for the root vertex */
	MeshSample root;
	/* Indices of control strands for interpolation */
	unsigned int parent_index[4];
	/* Weights of control strands for interpolation */
	float parent_weight[4];
	/* Parametric distance to the primary control strand */
	float root_distance[2];
} HairFiber;

bool BKE_hair_fiber_get_location(const struct HairFiber *fiber, struct DerivedMesh *root_dm, float loc[3]);
bool BKE_hair_fiber_get_vectors(const struct HairFiber *fiber, struct DerivedMesh *root_dm,
                                   float loc[3], float nor[3], float tang[3]);
bool BKE_hair_fiber_get_matrix(const struct HairFiber *fiber, struct DerivedMesh *root_dm, float mat[4][4]);

typedef struct StrandsView {
	int (*get_num_strands)(const struct StrandsView* strands);
	int (*get_num_verts)(const struct StrandsView* strands);
	
	void (*get_strand_lengths)(const struct StrandsView* strands, int *r_lengths);
	void (*get_strand_roots)(const struct StrandsView* strands, struct MeshSample *r_roots);
	void (*get_strand_vertices)(const struct StrandsView* strands, float (*positions)[3]);
} StrandsView;

struct HairFiber* BKE_hair_fibers_create(const struct StrandsView *strands,
                                         struct DerivedMesh *scalp, unsigned int amount,
                                         unsigned int seed);

int* BKE_hair_get_fiber_lengths(const struct HairFiber *fibers, int totfibers,
                                 const struct StrandsView *strands, int subdiv);

void BKE_hair_get_texture_buffer_size(const struct StrandsView *strands, int totfibers, int subdiv,
                                      int *r_size, int *r_strand_map_start,
                                      int *r_strand_vertex_start, int *r_fiber_start);
void BKE_hair_get_texture_buffer(const struct StrandsView *strands, struct DerivedMesh *scalp,
                                 const struct HairFiber *fibers, int totfibers, int subdiv,
                                 void *texbuffer);

#endif
