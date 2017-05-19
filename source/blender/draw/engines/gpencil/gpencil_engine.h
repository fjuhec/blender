/*
 * Copyright 2017, Blender Foundation.
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
 * Contributor(s): Antonio Vazquez
 *
 */

/** \file blender/draw/engines/gpencil/gpencil_engine.h
 *  \ingroup edgpencil
 */

#ifndef __GPENCIL_ENGINE_H__
#define __GPENCIL_ENGINE_H__

#include "GPU_batch.h"

#define MAX_GPENCIL_MAT 512 
#define GPENCIL_MIN_BATCH_SLOTS_CHUNK 8

 /* *********** LISTS *********** */
typedef struct GPENCIL_Storage {
	int pal_id; /* total elements */
	int t_mix[MAX_GPENCIL_MAT];
	int t_flip[MAX_GPENCIL_MAT];
	int t_clamp[MAX_GPENCIL_MAT];
	int fill_style[MAX_GPENCIL_MAT];
	PaletteColor *materials[MAX_GPENCIL_MAT];
	DRWShadingGroup *shgrps_fill[MAX_GPENCIL_MAT];
	DRWShadingGroup *shgrps_stroke[MAX_GPENCIL_MAT];
	float unit_matrix[4][4];
	int is_persp;   /* rv3d->is_persp (1-yes) */
	int xray;
} GPENCIL_Storage;

/* keep it under MAX_STORAGE */
typedef struct GPENCIL_StorageList {
	struct GPENCIL_Storage *storage;
	struct g_data *g_data;
} GPENCIL_StorageList;

/* keep it under MAX_PASSES */
typedef struct GPENCIL_PassList {
	struct DRWPass *stroke_pass;
	struct DRWPass *edit_pass;
	struct DRWPass *drawing_pass;
} GPENCIL_PassList;

/* keep it under MAX_BUFFERS */
typedef struct GPENCIL_FramebufferList {
	struct GPUFrameBuffer *fb;
} GPENCIL_FramebufferList;

/* keep it under MAX_TEXTURES */
typedef struct GPENCIL_TextureList {
	struct GPUTexture *texture;
} GPENCIL_TextureList;

typedef struct GPENCIL_Data {
	void *engine_type; /* Required */
	GPENCIL_FramebufferList *fbl;
	GPENCIL_TextureList *txl;
	GPENCIL_PassList *psl;
	GPENCIL_StorageList *stl;
} GPENCIL_Data;

/* *********** STATIC *********** */
typedef struct g_data {
	DRWShadingGroup *shgrps_edit_volumetric;
	DRWShadingGroup *shgrps_point_volumetric;
	DRWShadingGroup *shgrps_drawing_stroke;
	DRWShadingGroup *shgrps_drawing_fill;
	bool scene_draw;
} g_data; /* Transient data */

typedef struct GPENCIL_e_data {
	struct GPUShader *gpencil_fill_sh;
	struct GPUShader *gpencil_stroke_sh;
	struct GPUShader *gpencil_volumetric_sh;
	struct GPUShader *gpencil_drawing_fill_sh;
	struct Main *bmain;
} GPENCIL_e_data; /* Engine data */

/* Batch Cache */
typedef struct GpencilBatchCache {
	
	/* For normal strokes, a variable number of batch can be needed depending of number of strokes.
	   It could use the stroke number as total size, but when activate the onion skining, the number
	   can change, so the size is changed dinamically.
	 */
	Batch **batch_stroke;
	Batch **batch_fill;
	Batch **batch_edit;

	/* for buffer only one batch is nedeed because the drawing is only of one stroke */
	Batch *batch_buffer_stroke;
	Batch *batch_buffer_fill;

	/* settings to determine if cache is invalid */
	bool is_dirty;
	bool is_editmode;
	int cache_frame;

	/* keep information about the size of the cache */
	int cache_size;  /* total batch slots available */
	int cache_idx;   /* current slot index */
} GpencilBatchCache;

struct DRWShadingGroup *DRW_gpencil_shgroup_stroke_create(struct GPENCIL_Data *vedata, struct DRWPass *pass, struct GPUShader *shader, bGPdata *gpd);
struct DRWShadingGroup *DRW_gpencil_shgroup_point_volumetric_create(struct DRWPass *pass, struct GPUShader *shader);
struct DRWShadingGroup *DRW_gpencil_shgroup_edit_volumetric_create(struct DRWPass *pass, struct GPUShader *shader);
struct DRWShadingGroup *DRW_gpencil_shgroup_drawing_fill_create(struct DRWPass *pass, struct GPUShader *shader);

void DRW_gpencil_populate_datablock(struct GPENCIL_e_data *e_data, void *vedata, struct Scene *scene, struct Object *ob, struct ToolSettings *ts, struct bGPdata *gpd);

struct Batch *DRW_gpencil_get_point_geom(struct bGPDspoint *pt, short thickness, const float ink[4]);
struct Batch *DRW_gpencil_get_stroke_geom(struct bGPDframe *gpf, struct bGPDstroke *gps, short thickness, const float ink[4]);
struct Batch *DRW_gpencil_get_fill_geom(struct bGPDstroke *gps, const float color[4]);
struct Batch *DRW_gpencil_get_edit_geom(struct bGPDstroke *gps, float alpha, short dflag);
struct Batch *DRW_gpencil_get_buffer_stroke_geom(struct bGPdata *gpd, float matrix[4][4], short thickness);
struct Batch *DRW_gpencil_get_buffer_fill_geom(const struct tGPspoint *points, int totpoints, float ink[4]);
struct Batch *DRW_gpencil_get_buffer_point_geom(struct bGPdata *gpd, short thickness);

void gpencil_batch_cache_clear(struct bGPdata *gpd);

bool gpencil_can_draw_stroke(struct RegionView3D *rv3d, const struct bGPDframe *gpf, const struct bGPDstroke *gps);

#endif /* __GPENCIL_ENGINE_H__ */
