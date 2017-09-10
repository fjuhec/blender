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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 */

/** \file DNA_hair_types.h
 *  \ingroup DNA
 */

#ifndef __DNA_HAIR_TYPES_H__
#define __DNA_HAIR_TYPES_H__

#include "DNA_defs.h"
#include "DNA_listBase.h"
#include "DNA_ID.h"
#include "DNA_meshdata_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Root point (follicle) of a hair on a surface */
typedef struct HairFollicle {
	/* Sample on the scalp mesh for the root vertex */
	MeshSample mesh_sample;
} HairFollicle;

/* Collection of hair roots on a surface */
typedef struct HairPattern {
	struct HairFollicle *follicles;
	int num_follicles;
	
	int active_group;
	ListBase groups;
} HairPattern;

typedef struct HairGroup {
	struct HairGroup *next, *prev;
	
	char name[64]; /* MAX_NAME */
	int type;
	int flag;
	
	struct HairFollicle *follicles;
	int num_follicles;
	int pad;
	
	struct HairGuideData *guide_data;
	
	void *draw_batch_cache;
	void *draw_texture_cache;
	
	/* NORMALS */
	float normals_max_length;
	int pad2;
	
	/* STRANDS */
	int (*strands_parent_index)[4];
	float (*strands_parent_weight)[4];
} HairGroup;

typedef enum HairGroup_Type {
	HAIR_GROUP_TYPE_NORMALS    = 1,
	HAIR_GROUP_TYPE_STRANDS    = 2,
} HairGroup_Type;

typedef enum HairGroup_Flag {
	HAIR_GROUP_FLAG_GUIDESDIRTY    = 1,
} HairGroup_Flag;

typedef struct HairGuide {
	/* Sample on the scalp mesh for the root vertex */
	MeshSample mesh_sample;
	int vertstart;
	int totvert;
} HairGuide;

typedef struct HairGuideVertex {
	int flag;
	float co[3];
} HairGuideVertex;

typedef struct HairGuideData {
	struct HairGuide *guides;
	int num_guides;
	
	struct HairGuideVertex *verts;
	int totvert;
} HairGuideData;

#ifdef __cplusplus
}
#endif

#endif /* __DNA_HAIR_TYPES_H__ */
