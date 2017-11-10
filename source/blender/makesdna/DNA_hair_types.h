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
	int pad;
} HairPattern;

typedef struct HairGuideCurve {
	/* Sample on the scalp mesh for the root vertex */
	MeshSample mesh_sample;
	int vertstart;
	int totvert;
} HairGuideCurve;

typedef struct HairGuideVertex {
	int flag;
	float co[3];
} HairGuideVertex;

typedef struct HairGuideData {
	struct HairGuideCurve *guides;
	struct HairGuideVertex *verts;
	int num_guides;
	int totvert;
} HairGuideData;

typedef struct HairSystem {
	struct HairGuide *guide;
	struct Object *guide_object;
	
	struct HairPattern *pattern;
	
	void *draw_batch_cache;
	void *draw_texture_cache;
} HairSystem;

#ifdef __cplusplus
}
#endif

#endif /* __DNA_HAIR_TYPES_H__ */
