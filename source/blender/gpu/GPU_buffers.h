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
 * The Original Code is Copyright (C) 2005 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Brecht Van Lommel.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file GPU_buffers.h
 *  \ingroup gpu
 */

#ifndef __GPU_BUFFERS_H__
#define __GPU_BUFFERS_H__

#ifdef DEBUG
/*  #define DEBUG_VBO(X) printf(X)*/
#  define DEBUG_VBO(X)
#else
#  define DEBUG_VBO(X)
#endif

#include <stddef.h>

struct BMesh;
struct CCGElem;
struct CCGKey;
struct DMFlagMat;
struct DerivedMesh;
struct GSet;
struct GPUVertPointLink;
struct GPUDrawObject;
struct PBVH;
struct MVert;

typedef struct GPUBuffer {
	size_t size;        /* in bytes */
	unsigned int id; /* used with vertex buffer objects */
} GPUBuffer;

typedef struct GPUBufferMaterial {
	/* range of points used for this material */
	unsigned int start;
	unsigned int totelements;
	unsigned int totloops;
	unsigned int *polys; /* array of polygons for this material */
	unsigned int totpolys; /* total polygons in polys */
	unsigned int totvisiblepolys; /* total visible polygons */
	unsigned int totvisibleelems; /* total visible elements */

	/* original material index */
	short mat_nr;
} GPUBufferMaterial;

void GPU_buffer_material_finalize(struct GPUDrawObject *gdo, GPUBufferMaterial *matinfo, int totmat);

/* meshes are split up by material since changing materials requires
 * GL state changes that can't occur in the middle of drawing an
 * array.
 *
 * some simplifying assumptions are made:
 * - all quads are treated as two triangles.
 * - no vertex sharing is used; each triangle gets its own copy of the
 *   vertices it uses (this makes it easy to deal with a vertex used
 *   by faces with different properties, such as smooth/solid shading,
 *   different MCols, etc.)
 *
 * to avoid confusion between the original MVert vertices and the
 * arrays of OpenGL vertices, the latter are referred to here and in
 * the source as `points'. similarly, the OpenGL triangles generated
 * for MFaces are referred to as triangles rather than faces.
 */
typedef struct GPUDrawObject {
	/* vertices are kept in a separate buffer to ensure better cache coherence for edge drawing or
	 * for passes not reliant to other formats, such as, for example, depth pass or shadow maps */
	GPUBuffer *vertices;

	/* legacy buffers, do not reuse */
	GPUBuffer *normals;
	GPUBuffer *uv;
	GPUBuffer *uv_tex;
	GPUBuffer *colors;

	/* index buffers */
	GPUBuffer *edges;
	GPUBuffer *uvedges;
	GPUBuffer *triangles; /* triangle index buffer */

	/* material display data needed for the object. The resident data inside the buffer varies depending
	 * on the material that is assigned to each polygon. Vertex stride is the maximum vertex stride needed
	 * to accomodate the most fat material vertex format
	 * NOTE: For future deferred rendering we might want to separate data that are needed for normals as well */
	GPUBuffer *materialData;

	/* These data exist only to display UI helpers for the mesh that are not relevant to materials. Examples
	 * include selection state, weights and mode dependent visual debugging variables, uvs for uveditor */
	GPUBuffer *workflowData;

	/* for each original vertex, the list of related points */
	struct GPUVertPointLink *vert_points;

	/* see: USE_GPU_POINT_LINK define */
#if 0
	/* storage for the vert_points lists */
	struct GPUVertPointLink *vert_points_mem;
	int vert_points_usage;
#endif
	
	int colType;

	GPUBufferMaterial *materials;
	int totmaterial;
	
	unsigned int tot_triangle_point;
	unsigned int tot_loose_point;
	/* different than total loops since ngons get tesselated still */
	unsigned int tot_loop_verts;
	
	/* caches of the original DerivedMesh values */
	unsigned int totvert;
	unsigned int totedge;

	unsigned int loose_edge_offset;
	unsigned int tot_loose_edge_drawn;
	unsigned int tot_edge_drawn;

	/* for subsurf, offset where drawing of interior edges starts */
	unsigned int interior_offset;
	unsigned int totinterior;
} GPUDrawObject;

/* currently unused */
// #define USE_GPU_POINT_LINK

typedef struct GPUVertPointLink {
#ifdef USE_GPU_POINT_LINK
	struct GPUVertPointLink *next;
#endif
	/* -1 means uninitialized */
	int point_index;
} GPUVertPointLink;


/* used for GLSL materials */
typedef struct GPUAttrib {
	int index;
	int size;
	int type;
} GPUAttrib;

/* generic vertex format used for all derivedmeshes
 * customdatatype is enough to get size of format
 * and we can infer the offset by the position in buffer.
 * This corresponds in a single interleaved buffer */
typedef struct GPUMeshVertexAttribute
{
	/* char is sufficient here, we have less than 255 customdata types */
	char customdatatype;
	/* layer number, for layers that need it */
	char layer;
} GPUMeshVertexAttribute;

typedef struct GPUMeshVertexFormat
{
	/* which customdata exist in the current vertex format */
	long customdataflag;

	/* number of customData in format */
	char numData;

	/* actual current data existing in buffer */
	GPUMeshVertexAttribute *layout;
} GPUMeshVertexFormat;

/* create a vertex format with the specified formats */
GPUMeshVertexFormat *GPU_vertex_format_alloc(long iformat);

/* check if reusing the vertex format is possible */
bool GPU_vertex_format_reuse(GPUMeshVertexFormat *vformat, long iformat);

/* bind the vertex format existing in the currently bound buffer object,
 * according to the format specified here (should be a subset of the format of the buffer) */
void GPU_vertex_format_bind(GPUMeshVertexFormat *vformat, long iformat);

/* get the size of the vertex format */
int GPU_vertex_format_size(GPUMeshVertexFormat *vformat);

void GPU_global_buffer_pool_free(void);
void GPU_global_buffer_pool_free_unused(void);

GPUBuffer *GPU_buffer_alloc(size_t size);
void GPU_buffer_free(GPUBuffer *buffer);

void GPU_drawobject_free(struct DerivedMesh *dm);

/* free special global multires grid buffer */
void GPU_buffer_multires_free(bool force);

/* flag that controls data type to fill buffer with, a modifier will prepare. */
typedef enum {
	GPU_BUFFER_VERTEX = 0,
	GPU_BUFFER_NORMAL,
	GPU_BUFFER_COLOR,
	GPU_BUFFER_UV,
	GPU_BUFFER_UV_TEXPAINT,
	GPU_BUFFER_EDGE,
	GPU_BUFFER_UVEDGE,
	GPU_BUFFER_TRIANGLES,
} GPUBufferType;

typedef enum {
	GPU_BINDING_ARRAY = 0,
	GPU_BINDING_INDEX = 1,
} GPUBindingType;

/* called before drawing */
void GPU_vertex_setup(struct DerivedMesh *dm);
void GPU_normal_setup(struct DerivedMesh *dm);
void GPU_uv_setup(struct DerivedMesh *dm);
void GPU_texpaint_uv_setup(struct DerivedMesh *dm);
/* colType is the cddata MCol type to use! */
void GPU_color_setup(struct DerivedMesh *dm, int colType);
void GPU_buffer_bind_as_color(GPUBuffer *buffer);
void GPU_edge_setup(struct DerivedMesh *dm); /* does not mix with other data */
void GPU_uvedge_setup(struct DerivedMesh *dm);

void GPU_triangle_setup(struct DerivedMesh *dm);

int GPU_attrib_element_size(GPUAttrib data[], int numdata);
void GPU_interleaved_attrib_setup(GPUBuffer *buffer, GPUAttrib data[], int numdata, int element_size);

void GPU_buffer_bind(GPUBuffer *buffer, GPUBindingType binding);
void GPU_buffer_unbind(GPUBuffer *buffer, GPUBindingType binding);

/* can't lock more than one buffer at once */
void *GPU_buffer_lock(GPUBuffer *buffer, GPUBindingType binding);
void *GPU_buffer_lock_stream(GPUBuffer *buffer, GPUBindingType binding);
void GPU_buffer_unlock(GPUBuffer *buffer, GPUBindingType binding);

/* switch color rendering on=1/off=0 */
void GPU_color_switch(int mode);

/* used for drawing edges */
void GPU_buffer_draw_elements(GPUBuffer *elements, unsigned int mode, int start, int count);

/* called after drawing */
void GPU_buffers_unbind(void);

/* only unbind interleaved data */
void GPU_interleaved_attrib_unbind(void);

/* Buffers for non-DerivedMesh drawing */
typedef struct GPU_PBVH_Buffers GPU_PBVH_Buffers;

/* build */
GPU_PBVH_Buffers *GPU_build_mesh_pbvh_buffers(
        const int (*face_vert_indices)[4],
        const struct MPoly *mpoly, const struct MLoop *mloop, const struct MLoopTri *looptri,
        const struct MVert *verts,
        const int *face_indices,
        const int  face_indices_len);

GPU_PBVH_Buffers *GPU_build_grid_pbvh_buffers(int *grid_indices, int totgrid,
                                    unsigned int **grid_hidden, int gridsize, const struct CCGKey *key);

GPU_PBVH_Buffers *GPU_build_bmesh_pbvh_buffers(bool smooth_shading);

/* update */

void GPU_update_mesh_pbvh_buffers(
        GPU_PBVH_Buffers *buffers, const struct MVert *mvert,
        const int *vert_indices, int totvert, const float *vmask,
        const int (*face_vert_indices)[4], bool show_diffuse_color);

void GPU_update_bmesh_pbvh_buffers(GPU_PBVH_Buffers *buffers,
                              struct BMesh *bm,
                              struct GSet *bm_faces,
                              struct GSet *bm_unique_verts,
                              struct GSet *bm_other_verts,
                              bool show_diffuse_color);

void GPU_update_grid_pbvh_buffers(GPU_PBVH_Buffers *buffers, struct CCGElem **grids,
                             const struct DMFlagMat *grid_flag_mats,
                             int *grid_indices, int totgrid, const struct CCGKey *key,
                             bool show_diffuse_color);

/* draw */
void GPU_draw_pbvh_buffers(GPU_PBVH_Buffers *buffers, DMSetMaterial setMaterial,
                           bool wireframe, bool fast);

/* debug PBVH draw*/
void GPU_draw_pbvh_BB(float min[3], float max[3], bool leaf);
void GPU_end_draw_pbvh_BB(void);
void GPU_init_draw_pbvh_BB(void);

bool GPU_pbvh_buffers_diffuse_changed(GPU_PBVH_Buffers *buffers, struct GSet *bm_faces, bool show_diffuse_color);

void GPU_free_pbvh_buffers(GPU_PBVH_Buffers *buffers);

#endif
