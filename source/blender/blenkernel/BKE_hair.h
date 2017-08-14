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
struct HairGroup;
struct DerivedMesh;

static const unsigned int STRAND_INDEX_NONE = 0xFFFFFFFF;

struct HairPattern* BKE_hair_new(void);
struct HairPattern* BKE_hair_copy(struct HairPattern *hair);
void BKE_hair_free(struct HairPattern *hair);

void BKE_hair_set_num_follicles(struct HairPattern *hair, int count);
void BKE_hair_follicles_generate(struct HairPattern *hair, struct DerivedMesh *scalp, int count, unsigned int seed);

struct HairGroup* BKE_hair_group_new(struct HairPattern *hair, int type);
void BKE_hair_group_remove(struct HairPattern *hair, struct HairGroup *group);
struct HairGroup* BKE_hair_group_copy(struct HairPattern *hair, struct HairGroup *group);
void BKE_hair_group_moveto(struct HairPattern *hair, struct HairGroup *group, int position);

void BKE_hair_group_name_set(struct HairPattern *hair, struct HairGroup *group, const char *name);

void BKE_hair_update_groups(struct HairPattern *hair);

/* === Draw Buffer Texture === */

typedef struct HairDrawDataInterface {
	const struct HairGroup *group;
	struct DerivedMesh *scalp;
	
	int (*get_num_strands)(const struct HairDrawDataInterface* hairdata);
	int (*get_num_verts)(const struct HairDrawDataInterface* hairdata);
	
	void (*get_strand_lengths)(const struct HairDrawDataInterface* hairdata, int *r_lengths);
	void (*get_strand_roots)(const struct HairDrawDataInterface* hairdata, struct MeshSample *r_roots);
	void (*get_strand_vertices)(const struct HairDrawDataInterface* hairdata, float (*r_positions)[3]);
} HairDrawDataInterface;

int* BKE_hair_strands_get_fiber_lengths(const struct HairDrawDataInterface *hairdata, int subdiv);
void BKE_hair_strands_get_texture_buffer_size(const struct HairDrawDataInterface *hairdata, int subdiv,
                                              int *r_size, int *r_strand_map_start,
                                              int *r_strand_vertex_start, int *r_fiber_start);
void BKE_hair_strands_get_texture_buffer(const struct HairDrawDataInterface *hairdata, int subdiv, void *texbuffer);

/* === Draw Cache === */

enum {
	BKE_HAIR_BATCH_DIRTY_ALL = 0,
};
void BKE_hair_batch_cache_dirty(struct HairGroup *group, int mode);
void BKE_hair_batch_cache_all_dirty(struct HairPattern *hair, int mode);
void BKE_hair_batch_cache_free(struct HairGroup *group);

int* BKE_hair_group_get_fiber_lengths(struct HairGroup *group, struct DerivedMesh *scalp, int subdiv);
void BKE_hair_group_get_texture_buffer_size(struct HairGroup *group, struct DerivedMesh *scalp, int subdiv,
                                            int *r_size, int *r_strand_map_start, int *r_strand_vertex_start, int *r_fiber_start);
void BKE_hair_group_get_texture_buffer(struct HairGroup *group, struct DerivedMesh *scalp, int subdiv, void *texbuffer);

#endif
