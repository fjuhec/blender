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

struct tGPspoint;

 /* TODO: these could be system parameter in userprefs screen */
#define GPENCIL_CACHE_BLOCK_SIZE 8 
#define GPENCIL_MAX_SHGROUPS 65536
#define GPENCIL_MIN_BATCH_SLOTS_CHUNK 8

#define GPENCIL_COLOR_SOLID   0
#define GPENCIL_COLOR_TEXTURE 1
#define GPENCIL_COLOR_PATTERN 2

 /* *********** OBJECTS CACHE *********** */
 /* used to save gpencil objects */
typedef struct tGPencilObjectCache {
	struct Object *ob;
	int init_grp, end_grp;
	float zdepth;
} tGPencilObjectCache;

  /* *********** LISTS *********** */
typedef struct GPENCIL_shgroup {
	int s_clamp;
	int stroke_style;
	int color_type;
	int t_mix;
	int t_flip;
	int t_clamp;
	int fill_style;
	int keep_size;
	float obj_scale;
	struct DRWShadingGroup *shgrps_fill;
	struct DRWShadingGroup *shgrps_stroke;
} GPENCIL_shgroup;

typedef struct GPENCIL_Storage {
	int shgroup_id; /* total elements */
	float unit_matrix[4][4];
	int stroke_style;
	int color_type;
	int xray;
	int keep_size;
	float obj_scale;
} GPENCIL_Storage;

typedef struct GPENCIL_StorageList {
	struct GPENCIL_Storage *storage;
	struct g_data *g_data;
	struct GPENCIL_shgroup *shgroups;
} GPENCIL_StorageList;

typedef struct GPENCIL_PassList {
	struct DRWPass *stroke_pass;
	struct DRWPass *edit_pass;
	struct DRWPass *drawing_pass;
	struct DRWPass *mix_pass;
} GPENCIL_PassList;

typedef struct GPENCIL_FramebufferList {
	struct GPUFrameBuffer *fb;
	struct GPUFrameBuffer *temp_color_fb;
} GPENCIL_FramebufferList;

typedef struct GPENCIL_TextureList {
	struct GPUTexture *texture;
} GPENCIL_TextureList;

typedef struct GPENCIL_Data {
	void *engine_type; /* Required */
	struct GPENCIL_FramebufferList *fbl;
	struct GPENCIL_TextureList *txl;
	struct GPENCIL_PassList *psl;
	struct GPENCIL_StorageList *stl;
} GPENCIL_Data;

/* *********** STATIC *********** */
typedef struct g_data {
	struct DRWShadingGroup *shgrps_edit_volumetric;
	struct DRWShadingGroup *shgrps_drawing_stroke;
	struct DRWShadingGroup *shgrps_drawing_fill;

	/* for buffer only one batch is nedeed because the drawing is only of one stroke */
	Gwn_Batch *batch_buffer_stroke;
	Gwn_Batch *batch_buffer_fill;

	int gp_cache_used;
	int gp_cache_size;
	struct tGPencilObjectCache *gp_object_cache;

	struct GHash *gpd_in_cache;
} g_data; /* Transient data */

typedef struct GPENCIL_e_data {
	struct GPUShader *gpencil_fill_sh;
	struct GPUShader *gpencil_stroke_sh;
	struct GPUShader *gpencil_point_sh;
	struct GPUShader *gpencil_volumetric_sh;
	struct GPUShader *gpencil_drawing_fill_sh;
	struct GPUShader *gpencil_fullscreen_sh;
	/* temp depth texture */
	struct GPUTexture *temp_fbcolor_depth_tx;
	struct GPUTexture *temp_fbcolor_color_tx;
	struct GPUTexture *gpencil_blank_texture;
} GPENCIL_e_data; /* Engine data */

/* Gwn_Batch Cache */
typedef struct GpencilBatchCache {
	
	/* For normal strokes, a variable number of batch can be needed depending of number of strokes.
	   It could use the stroke number as total size, but when activate the onion skining, the number
	   can change, so the size is changed dinamically.
	 */
	Gwn_Batch **batch_stroke;
	Gwn_Batch **batch_fill;
	Gwn_Batch **batch_edit;

	/* settings to determine if cache is invalid */
	bool is_dirty;
	bool is_editmode;
	int cache_frame;

	/* keep information about the size of the cache */
	int cache_size;  /* total batch slots available */
	int cache_idx;   /* current slot index */
} GpencilBatchCache;

struct DRWShadingGroup *DRW_gpencil_shgroup_stroke_create(struct GPENCIL_e_data *e_data, struct GPENCIL_Data *vedata, struct DRWPass *pass, struct GPUShader *shader, struct Object *ob,
	                                                      struct bGPdata *gpd, struct PaletteColor *palcolor, int id);
struct DRWShadingGroup *DRW_gpencil_shgroup_point_create(struct GPENCIL_e_data *e_data, struct GPENCIL_Data *vedata, struct DRWPass *pass, struct GPUShader *shader, struct Object *ob,
	                                                     struct bGPdata *gpd, struct PaletteColor *palcolor, int id);
struct DRWShadingGroup *DRW_gpencil_shgroup_point_volumetric_create(struct DRWPass *pass, struct GPUShader *shader);
struct DRWShadingGroup *DRW_gpencil_shgroup_edit_volumetric_create(struct DRWPass *pass, struct GPUShader *shader);
struct DRWShadingGroup *DRW_gpencil_shgroup_drawing_fill_create(struct DRWPass *pass, struct GPUShader *shader);

void DRW_gpencil_populate_datablock(struct GPENCIL_e_data *e_data, void *vedata, struct Scene *scene, struct Object *ob, struct ToolSettings *ts, struct bGPdata *gpd);
void DRW_gpencil_populate_buffer_strokes(void *vedata, struct ToolSettings *ts, struct bGPdata *gpd);

struct Gwn_Batch *DRW_gpencil_get_point_geom(struct bGPDstroke *gps, short thickness, const float ink[4]);
struct Gwn_Batch *DRW_gpencil_get_stroke_geom(struct bGPDframe *gpf, struct bGPDstroke *gps, short thickness, const float ink[4]);
struct Gwn_Batch *DRW_gpencil_get_fill_geom(struct bGPDstroke *gps, const float color[4]);
struct Gwn_Batch *DRW_gpencil_get_edit_geom(struct bGPDstroke *gps, float alpha, short dflag);
struct Gwn_Batch *DRW_gpencil_get_buffer_stroke_geom(struct bGPdata *gpd, float matrix[4][4], short thickness);
struct Gwn_Batch *DRW_gpencil_get_buffer_fill_geom(const struct tGPspoint *points, int totpoints, float ink[4]);
struct Gwn_Batch *DRW_gpencil_get_buffer_point_geom(struct bGPdata *gpd, float matrix[4][4], short thickness);

struct GPUTexture *DRW_gpencil_create_blank_texture(int width, int height);

void gpencil_batch_cache_clear(struct bGPdata *gpd);

bool gpencil_can_draw_stroke(const struct bGPDstroke *gps, const bool onion);

struct tGPencilObjectCache *gpencil_object_cache_allocate(struct tGPencilObjectCache *cache, int *gp_cache_size, int *gp_cache_used);
void gpencil_object_cache_add(struct tGPencilObjectCache *cache, struct Object *ob, int *gp_cache_used);

#endif /* __GPENCIL_ENGINE_H__ */
