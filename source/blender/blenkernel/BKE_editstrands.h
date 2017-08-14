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

#ifndef __BKE_EDITSTRANDS_H__
#define __BKE_EDITSTRANDS_H__

/** \file blender/blenkernel/BKE_editstrands.h
 *  \ingroup bke
 */

#include "BLI_utildefines.h"

#include "DNA_customdata_types.h"

#include "BKE_customdata.h"
#include "BKE_editmesh.h"

#include "bmesh.h"

struct BMesh;
struct DerivedMesh;
struct Mesh;
struct Object;

typedef struct BMEditStrands {
	BMEditMesh base;
	
	/* Scalp mesh for fixing root vertices */
	struct DerivedMesh *root_dm;
	struct HairPattern *hair_pattern;
	struct HairGroup *hair_group;
	
	int flag;
	
	unsigned int vertex_glbuf; // legacy gpu code
	unsigned int elem_glbuf; // legacy gpu code
	unsigned int dot_glbuf; // legacy gpu code
	void *batch_cache;
	void *texture;
} BMEditStrands;

/* BMEditStrands->flag */
typedef enum BMEditStrandsFlag {
	BM_STRANDS_DIRTY_SEGLEN     = (1 << 0),
	BM_STRANDS_DIRTY_ROOTS      = (1 << 1),
} BMEditStrandsFlag;

struct BMEditStrands *BKE_editstrands_create(struct BMesh *bm, struct DerivedMesh *root_dm);
struct BMEditStrands *BKE_editstrands_copy(struct BMEditStrands *es);
struct BMEditStrands *BKE_editstrands_from_object_particles(struct Object *ob, struct ParticleSystem **r_psys);
struct BMEditStrands *BKE_editstrands_from_object(struct Object *ob);
void BKE_editstrands_update_linked_customdata(struct BMEditStrands *es);
void BKE_editstrands_free(struct BMEditStrands *es);

/* === Hair Fibers === */

int* BKE_editstrands_hair_get_fiber_lengths(struct BMEditStrands *es, int subdiv);
void BKE_editstrands_hair_get_texture_buffer_size(struct BMEditStrands *es, int subdiv, int *r_size,
                                                  int *r_strand_map_start, int *r_strand_vertex_start, int *r_fiber_start);
void BKE_editstrands_hair_get_texture_buffer(struct BMEditStrands *es, int subdiv, void *texbuffer);

/* === Constraints === */

/* Stores vertex locations for temporary reference:
 * Vertex locations get modified by tools, but then need to be corrected
 * by calculating a smooth solution based on the difference to original pre-tool locations.
 */
typedef float (*BMEditStrandsLocations)[3];
BMEditStrandsLocations BKE_editstrands_get_locations(struct BMEditStrands *edit);
void BKE_editstrands_free_locations(BMEditStrandsLocations locs);

void BKE_editstrands_solve_constraints(struct Object *ob, struct BMEditStrands *es, BMEditStrandsLocations orig);
void BKE_editstrands_ensure(struct BMEditStrands *es);

/* === Particle Conversion === */

struct BMesh *BKE_editstrands_particles_to_bmesh(struct Object *ob, struct ParticleSystem *psys);
void BKE_editstrands_particles_from_bmesh(struct Object *ob, struct ParticleSystem *psys);

/* === Mesh Conversion === */
struct BMesh *BKE_editstrands_mesh_to_bmesh(struct Object *ob, struct Mesh *me);
void BKE_editstrands_mesh_from_bmesh(struct Object *ob);

/* === Draw Cache === */
enum {
	BKE_STRANDS_BATCH_DIRTY_ALL = 0,
	BKE_STRANDS_BATCH_DIRTY_SELECT = 1,
};
void BKE_editstrands_batch_cache_dirty(struct BMEditStrands *es, int mode);
void BKE_editstrands_batch_cache_free(struct BMEditStrands *es);

#endif
