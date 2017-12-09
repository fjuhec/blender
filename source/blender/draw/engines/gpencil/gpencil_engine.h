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
struct ModifierData;
struct GPENCIL_StorageList;

 /* TODO: these could be system parameter in userprefs screen */
#define GPENCIL_MAX_GP_OBJ 256 

#define GPENCIL_CACHE_BLOCK_SIZE 8 
#define GPENCIL_MAX_SHGROUPS 65536
#define GPENCIL_MIN_BATCH_SLOTS_CHUNK 16

#define GPENCIL_COLOR_SOLID   0
#define GPENCIL_COLOR_TEXTURE 1
#define GPENCIL_COLOR_PATTERN 2

#define GP_SIMPLIFY(ts) ((ts->gpencil_simplify & GP_TOOL_FLAG_SIMPLIFY))
#define GP_SIMPLIFY_ONPLAY(playing) (((playing == true) && (ts->gpencil_simplify & GP_TOOL_FLAG_SIMPLIFY_ON_PLAY)) || ((ts->gpencil_simplify & GP_TOOL_FLAG_SIMPLIFY_ON_PLAY) == 0))
#define GP_SIMPLIFY_FILL(ts, playing) ((GP_SIMPLIFY_ONPLAY(playing) && (GP_SIMPLIFY(ts)) && (ts->gpencil_simplify & GP_TOOL_FLAG_SIMPLIFY_VIEW_FILL))) 
#define GP_SIMPLIFY_MODIF(ts, playing) ((GP_SIMPLIFY_ONPLAY(playing) && (GP_SIMPLIFY(ts)) && (ts->gpencil_simplify & GP_TOOL_FLAG_SIMPLIFY_VIEW_MODIF))) 
#define GP_SIMPLIFY_VFX(ts, playing) ((GP_SIMPLIFY_ONPLAY(playing) && (GP_SIMPLIFY(ts)) && (ts->gpencil_simplify & GP_TOOL_FLAG_SIMPLIFY_VIEW_VFX)))

 /* anti aliasing macros using MSAA */
#define MULTISAMPLE_GP_SYNC_ENABLE(dfbl, fbl) { \
	if ((U.ogl_multisamples > 0) && (dfbl->multisample_fb != NULL)) { \
		DRW_stats_query_start("GP Multisample Blit"); \
		DRW_framebuffer_blit(fbl->temp_color_fb, dfbl->multisample_fb, false, false); \
		DRW_framebuffer_blit(fbl->temp_color_fb, dfbl->multisample_fb, true, false); \
		DRW_framebuffer_bind(dfbl->multisample_fb); \
		DRW_stats_query_end(); \
	} \
}

#define MULTISAMPLE_GP_SYNC_DISABLE(dfbl, fbl) { \
	if ((U.ogl_multisamples > 0) && (dfbl->multisample_fb != NULL)) { \
		DRW_stats_query_start("GP Multisample Resolve"); \
		DRW_framebuffer_blit(dfbl->multisample_fb, fbl->temp_color_fb, false, false); \
		DRW_framebuffer_blit(dfbl->multisample_fb, fbl->temp_color_fb, true, false); \
		DRW_framebuffer_bind(fbl->temp_color_fb); \
		DRW_stats_query_end(); \
	} \
}

 /* *********** OBJECTS CACHE *********** */
typedef struct GPencilVFXSwirl {
	float loc[3];
	float radius; 
	float angle;
	int transparent;
} GPencilVFXSwirl;

typedef struct GPencilVFXPixel {
	float size[2];
	float rgba[4];
	int lines;
} GPencilVFXPixel;

typedef struct GPencilVFXBlur {
	float x;
	float y;
} GPencilVFXBlur;

typedef struct GPencilVFXWave {
	int orientation;
	float amplitude;
	float period;
	float phase;
	float wsize[2];
} GPencilVFXWave;

typedef struct GPencilVFXFlip {
	float flipmode[2]; /* use float to pass to shader, but only will be 0 or 1 */
	float wsize[2];
} GPencilVFXFlip;

typedef struct GPencilVFXLight {
	float loc[4];
	float wsize[2];
	float energy;
	float ambient;
	float specular;
} GPencilVFXLight;

/* used to save gpencil objects */
typedef struct tGPencilObjectCache {
	struct Object *ob;
	int init_grp, end_grp;
	DRWShadingGroup *init_vfx_wave_sh;
	DRWShadingGroup *end_vfx_wave_sh;

	DRWShadingGroup *init_vfx_blur_sh_1;
	DRWShadingGroup *end_vfx_blur_sh_1;
	DRWShadingGroup *init_vfx_blur_sh_2;
	DRWShadingGroup *end_vfx_blur_sh_2;
	DRWShadingGroup *init_vfx_blur_sh_3;
	DRWShadingGroup *end_vfx_blur_sh_3;
	DRWShadingGroup *init_vfx_blur_sh_4;
	DRWShadingGroup *end_vfx_blur_sh_4;

	DRWShadingGroup *init_vfx_pixel_sh;
	DRWShadingGroup *end_vfx_pixel_sh;

	DRWShadingGroup *init_vfx_swirl_sh;
	DRWShadingGroup *end_vfx_swirl_sh;

	DRWShadingGroup *init_vfx_flip_sh;
	DRWShadingGroup *end_vfx_flip_sh;

	DRWShadingGroup *init_vfx_light_sh;
	DRWShadingGroup *end_vfx_light_sh;
	float zdepth;
} tGPencilObjectCache;

  /* *********** LISTS *********** */
typedef struct GPENCIL_vfx {
	GPencilVFXBlur vfx_blur;
	GPencilVFXWave vfx_wave;
	GPencilVFXPixel vfx_pixel;
	GPencilVFXSwirl vfx_swirl;
	GPencilVFXFlip vfx_flip;
	GPencilVFXLight vfx_light;
} GPENCIL_vfx;

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
	int pixfactor;
	int playing;
	int uselines;
	float gridsize[2];
	float gridcolor[3];
} GPENCIL_Storage;

typedef struct GPENCIL_StorageList {
	struct GPENCIL_Storage *storage;
	struct g_data *g_data;
	struct GPENCIL_shgroup *shgroups;
	struct GPENCIL_vfx *vfx;
} GPENCIL_StorageList;

typedef struct GPENCIL_PassList {
	struct DRWPass *stroke_pass;
	struct DRWPass *edit_pass;
	struct DRWPass *drawing_pass;
	struct DRWPass *mix_pass;
	struct DRWPass *mix_vfx_pass;
	struct DRWPass *mix_pass_noblend;
	struct DRWPass *mix_vfx_pass_noblend;
	struct DRWPass *mix_pass_front;
	struct DRWPass *vfx_copy_pass;
	struct DRWPass *vfx_wave_pass;
	struct DRWPass *vfx_blur_pass_1;
	struct DRWPass *vfx_blur_pass_2;
	struct DRWPass *vfx_blur_pass_3;
	struct DRWPass *vfx_blur_pass_4;
	struct DRWPass *vfx_pixel_pass;
	struct DRWPass *vfx_swirl_pass;
	struct DRWPass *vfx_flip_pass;
	struct DRWPass *vfx_light_pass;
	struct DRWPass *painting_pass;
	struct DRWPass *paper_pass;
} GPENCIL_PassList;

typedef struct GPENCIL_FramebufferList {
	struct GPUFrameBuffer *fb;
	struct GPUFrameBuffer *temp_color_fb;
	struct GPUFrameBuffer *vfx_color_fb_a;
	struct GPUFrameBuffer *vfx_color_fb_b;
	struct GPUFrameBuffer *painting_fb;
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
	struct DRWShadingGroup *shgrps_edit_point;
	struct DRWShadingGroup *shgrps_edit_line;
	struct DRWShadingGroup *shgrps_drawing_stroke;
	struct DRWShadingGroup *shgrps_drawing_fill;

	/* for buffer only one batch is nedeed because the drawing is only of one stroke */
	Gwn_Batch *batch_buffer_stroke;
	Gwn_Batch *batch_buffer_fill;

	int gp_cache_used;
	int gp_cache_size;
	struct tGPencilObjectCache *gp_object_cache;

	int session_flag;

	/* number of shading groups */
	int tot_sh;
	int tot_sh_stroke;
	int tot_sh_fill;
	int tot_sh_point;
} g_data; /* Transient data */

typedef enum eGPsession_Flag {
	GP_DRW_PAINT_HOLD     = (1 << 0),
	GP_DRW_PAINT_IDLE     = (1 << 1),
	GP_DRW_PAINT_FILLING  = (1 << 2),
	GP_DRW_PAINT_READY    = (1 << 3),
	GP_DRW_PAINT_PAINTING = (1 << 4),
} eGPsession_Flag;

typedef struct GPENCIL_e_data {
	struct GPUShader *gpencil_fill_sh;
	struct GPUShader *gpencil_stroke_sh;
	struct GPUShader *gpencil_point_sh;
	struct GPUShader *gpencil_edit_point_sh;
	struct GPUShader *gpencil_line_sh;
	struct GPUShader *gpencil_drawing_fill_sh;
	struct GPUShader *gpencil_fullscreen_sh;
	struct GPUShader *gpencil_vfx_blur_sh;
	struct GPUShader *gpencil_vfx_wave_sh;
	struct GPUShader *gpencil_vfx_pixel_sh;
	struct GPUShader *gpencil_vfx_swirl_sh;
	struct GPUShader *gpencil_vfx_flip_sh;
	struct GPUShader *gpencil_vfx_light_sh;
	struct GPUShader *gpencil_painting_sh;
	struct GPUShader *gpencil_front_depth_sh;
	struct GPUShader *gpencil_paper_sh;
	/* temp depth texture */
	struct GPUTexture *temp_fbcolor_depth_tx;
	struct GPUTexture *temp_fbcolor_color_tx;
	
	struct GPUTexture *vfx_fbcolor_depth_tx_a;
	struct GPUTexture *vfx_fbcolor_color_tx_a;
	struct GPUTexture *vfx_fbcolor_depth_tx_b;
	struct GPUTexture *vfx_fbcolor_color_tx_b;

	struct GPUTexture *painting_depth_tx;
	struct GPUTexture *painting_color_tx;

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
	Gwn_Batch **batch_edlin;

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

void DRW_gpencil_populate_datablock(struct GPENCIL_e_data *e_data, void *vedata, struct Scene *scene, struct Object *ob, struct ToolSettings *ts, struct bGPdata *gpd);
void DRW_gpencil_populate_buffer_strokes(struct GPENCIL_e_data *e_data, void *vedata, struct ToolSettings *ts, struct Object *ob);
void DRW_gpencil_populate_multiedit(struct GPENCIL_e_data *e_data, void *vedata, struct Scene *scene, struct Object *ob, struct ToolSettings *ts, struct bGPdata *gpd);

struct Gwn_Batch *DRW_gpencil_get_point_geom(struct bGPDstroke *gps, short thickness, const float ink[4]);
struct Gwn_Batch *DRW_gpencil_get_stroke_geom(struct bGPDframe *gpf, struct bGPDstroke *gps, short thickness, const float ink[4]);
struct Gwn_Batch *DRW_gpencil_get_fill_geom(struct bGPDstroke *gps, const float color[4]);
struct Gwn_Batch *DRW_gpencil_get_edit_geom(struct bGPDstroke *gps, float alpha, short dflag);
struct Gwn_Batch *DRW_gpencil_get_edlin_geom(struct bGPDstroke *gps, float alpha, short dflag);
struct Gwn_Batch *DRW_gpencil_get_buffer_stroke_geom(struct bGPdata *gpd, float matrix[4][4], short thickness);
struct Gwn_Batch *DRW_gpencil_get_buffer_fill_geom(const struct tGPspoint *points, int totpoints, float ink[4]);
struct Gwn_Batch *DRW_gpencil_get_buffer_point_geom(struct bGPdata *gpd, float matrix[4][4], short thickness);

struct GPUTexture *DRW_gpencil_create_blank_texture(int width, int height);

bool gpencil_can_draw_stroke(const struct bGPDstroke *gps, const bool onion);

struct tGPencilObjectCache *gpencil_object_cache_allocate(struct tGPencilObjectCache *cache, int *gp_cache_size, int *gp_cache_used);
void gpencil_object_cache_add(struct tGPencilObjectCache *cache, struct Object *ob, int *gp_cache_used);

void gpencil_array_modifiers(struct GPENCIL_StorageList *stl, struct Object *ob);

void DRW_gpencil_vfx_modifiers(int ob_idx, struct GPENCIL_e_data *e_data, struct GPENCIL_Data *vedata, struct Object *ob, struct tGPencilObjectCache *cache);

#endif /* __GPENCIL_ENGINE_H__ */
