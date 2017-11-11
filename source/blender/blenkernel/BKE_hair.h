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

static const unsigned int HAIR_STRAND_INDEX_NONE = 0xFFFFFFFF;

struct HairFollicle;
struct HairPattern;
struct HairSystem;
struct DerivedMesh;
struct EvaluationContext;
struct Scene;

struct HairSystem* BKE_hair_new(void);
struct HairSystem* BKE_hair_copy(struct HairSystem *hsys);
void BKE_hair_free(struct HairSystem *hsys);

void BKE_hair_generate_follicles(struct HairSystem* hsys, unsigned int seed);

/* === Guide Strands === */

struct DerivedMesh* BKE_hair_get_scalp(const struct HairSystem *hsys, struct Scene *scene, const struct EvaluationContext *eval_ctx);
int BKE_hair_get_num_strands(const struct HairSystem *hsys);
int BKE_hair_get_num_strands_verts(const struct HairSystem *hsys);
void BKE_hair_get_strand_lengths(const struct HairSystem *hsys, int *r_lengths);
void BKE_hair_get_strand_roots(const struct HairSystem *hsys, struct MeshSample *r_roots);
void BKE_hair_get_strand_vertices(const struct HairSystem *hsys, float (*r_positions)[3]);
void BKE_hair_get_follicle_weights(const struct HairSystem *hsys, unsigned int (*r_parents)[4], float (*r_weights)[4]);

/* === Draw Cache === */

enum {
	BKE_HAIR_BATCH_DIRTY_ALL = 0,
};
void BKE_hair_batch_cache_dirty(struct HairSystem* hsys, int mode);
void BKE_hair_batch_cache_free(struct HairSystem* hsys);

int* BKE_hair_get_fiber_lengths(const struct HairSystem* hsys, int subdiv);
void BKE_hair_get_texture_buffer_size(const struct HairSystem* hsys, int subdiv,
                                              int *r_size, int *r_strand_map_start, int *r_strand_vertex_start, int *r_fiber_start);
void BKE_hair_get_texture_buffer(const struct HairSystem* hsys, struct Scene *scene,
                                 struct EvaluationContext *eval_ctx, int subdiv, void *texbuffer);

#endif
