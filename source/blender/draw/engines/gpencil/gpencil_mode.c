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

/** \file blender/draw/engines/gpencil/gpencil_mode.c
 *  \ingroup draw
 */

#include "DRW_engine.h"
#include "DRW_render.h"

#include "BKE_gpencil.h"
#include "ED_gpencil.h"

#include "DNA_gpencil_types.h"

 /* If builtin shaders are needed */
#include "GPU_shader.h"

#include "draw_common.h"

#include "draw_mode_engines.h"
#include "gpencil_mode.h"

extern char datatoc_gpencil_fill_vert_glsl[];
extern char datatoc_gpencil_fill_frag_glsl[];
extern char datatoc_gpencil_stroke_vert_glsl[];
extern char datatoc_gpencil_stroke_geom_glsl[];
extern char datatoc_gpencil_stroke_frag_glsl[];

/* *********** LISTS *********** */
#define MAX_GPENCIL_MAT 512 

typedef struct GPENCIL_Storage {
	int pal_id;
	PaletteColor *materials[MAX_GPENCIL_MAT];
	DRWShadingGroup *shgrps_fill[MAX_GPENCIL_MAT];
	DRWShadingGroup *shgrps_stroke[MAX_GPENCIL_MAT];
} GPENCIL_Storage;

/* keep it under MAX_STORAGE */
typedef struct GPENCIL_StorageList {
	struct GPENCIL_Storage *storage;
	struct g_data *g_data;
} GPENCIL_StorageList;

/* keep it under MAX_PASSES */
typedef struct GPENCIL_PassList {
	struct DRWPass *pass;
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

typedef struct g_data{
	int t_flip;
	int t_mix;
	int fill_style;
} g_data; /* Transient data */

static struct {
	struct GPUShader *gpencil_fill_sh;
	struct GPUShader *gpencil_stroke_sh;
	struct GPUShader *gpencil_point_sh;
	struct GPUShader *gpencil_volumetric_sh;
} e_data = {NULL}; /* Engine data */

/* *********** FUNCTIONS *********** */

static void GPENCIL_engine_init(void *vedata)
{
	GPENCIL_TextureList *txl = ((GPENCIL_Data *)vedata)->txl;
	GPENCIL_FramebufferList *fbl = ((GPENCIL_Data *)vedata)->fbl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

	e_data.gpencil_fill_sh = DRW_shader_create(datatoc_gpencil_fill_vert_glsl, NULL,
											   datatoc_gpencil_fill_frag_glsl,
											   NULL);
	e_data.gpencil_stroke_sh = DRW_shader_create(datatoc_gpencil_stroke_vert_glsl, 
												 datatoc_gpencil_stroke_geom_glsl,
												 datatoc_gpencil_stroke_frag_glsl,
												 NULL);
	e_data.gpencil_point_sh = GPU_shader_get_builtin_shader(GPU_SHADER_3D_POINT_UNIFORM_SIZE_UNIFORM_COLOR_AA);

	e_data.gpencil_volumetric_sh = GPU_shader_get_builtin_shader(GPU_SHADER_3D_POINT_VARYING_SIZE_VARYING_COLOR);

	if (!stl->storage) {
		stl->storage = MEM_callocN(sizeof(GPENCIL_Storage), "GPENCIL_Storage");
	}

}

static void GPENCIL_engine_free(void)
{
	if (e_data.gpencil_fill_sh)
		DRW_shader_free(e_data.gpencil_fill_sh);
	if (e_data.gpencil_stroke_sh)
		DRW_shader_free(e_data.gpencil_stroke_sh);
}

/* create shading group for filling */
static DRWShadingGroup *GPENCIL_shgroup_fill_create(GPENCIL_Data *vedata, DRWPass *pass, PaletteColor *palcolor)
{
	GPENCIL_TextureList *txl = ((GPENCIL_Data *)vedata)->txl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

	DRWShadingGroup *grp = DRW_shgroup_create(e_data.gpencil_fill_sh, pass);
	DRW_shgroup_uniform_vec4(grp, "color", palcolor->fill, 1);
	DRW_shgroup_uniform_vec4(grp, "color2", palcolor->scolor, 1);
	stl->g_data->fill_style = palcolor->fill_style;
	DRW_shgroup_uniform_int(grp, "fill_type", &stl->g_data->fill_style, 1);
	DRW_shgroup_uniform_float(grp, "mix_factor", &palcolor->mix_factor, 1);

	DRW_shgroup_uniform_float(grp, "g_angle", &palcolor->g_angle, 1);
	DRW_shgroup_uniform_float(grp, "g_radius", &palcolor->g_radius, 1);
	DRW_shgroup_uniform_float(grp, "g_boxsize", &palcolor->g_boxsize, 1);
	DRW_shgroup_uniform_vec2(grp, "g_scale", palcolor->g_scale, 1);
	DRW_shgroup_uniform_vec2(grp, "g_shift", palcolor->g_shift, 1);

	DRW_shgroup_uniform_float(grp, "t_angle", &palcolor->t_angle, 1);
	DRW_shgroup_uniform_vec2(grp, "t_scale", palcolor->t_scale, 1);
	DRW_shgroup_uniform_vec2(grp, "t_shift", palcolor->t_shift, 1);
	DRW_shgroup_uniform_float(grp, "t_opacity", &palcolor->t_opacity, 1);

	stl->g_data->t_mix = palcolor->flag & PAC_COLOR_TEX_MIX ? 1 : 0;
	DRW_shgroup_uniform_int(grp, "t_mix", &stl->g_data->t_mix, 1);

	stl->g_data->t_flip = palcolor->flag & PAC_COLOR_FLIP_FILL ? 1 : 0;
	DRW_shgroup_uniform_int(grp, "t_flip", &stl->g_data->t_flip, 1);

	/* TODO: image texture */
	if ((palcolor->fill_style == FILL_STYLE_TEXTURE) || (palcolor->flag & PAC_COLOR_TEX_MIX)) {
		//gp_set_filling_texture(palcolor->ima, palcolor->flag);
		DRW_shgroup_uniform_buffer(grp, "myTexture", &txl->texture, 0);
	}

	return grp;
}

/* create shading group for strokes */
static DRWShadingGroup *GPENCIL_shgroup_stroke_create(GPENCIL_Data *vedata, DRWPass *pass, PaletteColor *palcolor)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

	DRWShadingGroup *grp = DRW_shgroup_create(e_data.gpencil_stroke_sh, pass);
	DRW_shgroup_uniform_vec2(grp, "Viewport", DRW_viewport_size_get(), 1);

	return grp;
}


static void GPENCIL_cache_init(void *vedata)
{
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	GPENCIL_TextureList *txl = ((GPENCIL_Data *)vedata)->txl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	DefaultTextureList *dtxl = DRW_viewport_texture_list_get();

	const struct bContext *C = DRW_get_context();
	Scene *scene = CTX_data_scene(C);
	SceneLayer *sl = CTX_data_scene_layer(C);

	if (!stl->g_data) {
		/* Alloc transient pointers */
		stl->g_data = MEM_mallocN(sizeof(g_data), "g_data");
	}

	{
		/* Create a pass */
		DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH; 
		psl->pass = DRW_pass_create("Gpencil Pass", state);
		stl->storage->pal_id = 0;
		memset(stl->storage->shgrps_fill, 0, sizeof(DRWShadingGroup *) * MAX_GPENCIL_MAT);
		memset(stl->storage->shgrps_stroke, 0, sizeof(DRWShadingGroup *) * MAX_GPENCIL_MAT);
		memset(stl->storage->materials, 0, sizeof(PaletteColor *) * MAX_GPENCIL_MAT);
	}
}

/* find shader group */
static int GPENCIL_shgroup_find(GPENCIL_Storage *storage, PaletteColor *palcolor)
{
	for (int i = 0; i < storage->pal_id; ++i) {
		if (storage->materials[i] == palcolor) {
			return i;
		}
	}

	/* not found */
	return -1;
}

static void GPENCIL_cache_populate(void *vedata, Object *ob)
{
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	DRWShadingGroup *fillgrp;
	DRWShadingGroup *strokegrp;
	const bContext *C = DRW_get_context();
	Scene *scene = CTX_data_scene(C);
	float diff_mat[4][4];
	float ink[4];
	float tcolor[4];

	UNUSED_VARS(psl, stl);

	if (ob->type == OB_GPENCIL && ob->gpd)  {
		for (bGPDlayer *gpl = ob->gpd->layers.first; gpl; gpl = gpl->next) {
			/* don't draw layer if hidden */
			if (gpl->flag & GP_LAYER_HIDE)
				continue;

			bGPDframe *gpf = BKE_gpencil_layer_getframe(gpl, CFRA, 0);
			if (gpf == NULL)
				continue;

			/* calculate parent position */
			ED_gpencil_parent_location(ob, ob->gpd, gpl, diff_mat);
			for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
				/* check if stroke can be drawn */
				if (gpencil_can_draw_stroke(gps) == false) {
					continue;
				}
				/* try to find shader group or create a new one */
				int id = GPENCIL_shgroup_find(stl->storage, gps->palcolor);
				if (id == -1) {
					id = stl->storage->pal_id;
					stl->storage->materials[id] = gps->palcolor;
					stl->storage->shgrps_fill[id] = GPENCIL_shgroup_fill_create(vedata, psl->pass, gps->palcolor);
					stl->storage->shgrps_stroke[id] = GPENCIL_shgroup_stroke_create(vedata, psl->pass, gps->palcolor);
					++stl->storage->pal_id;
				}

				fillgrp = stl->storage->shgrps_fill[id];
				strokegrp = stl->storage->shgrps_stroke[id];

				/* fill */
				if (gps->totpoints >= 3) {
					float tfill[4];
					interp_v3_v3v3(tfill, gps->palcolor->fill, gpl->tintcolor, gpl->tintcolor[3]);
					tfill[3] = gps->palcolor->fill[3] * gpl->opacity;
					if ((tfill[3] > GPENCIL_ALPHA_OPACITY_THRESH) || (gps->palcolor->fill_style > 0)) {
						struct Batch *fill_geom = gpencil_get_fill_geom(gps, diff_mat, tfill);
						DRW_shgroup_call_add(fillgrp, fill_geom, ob->obmat);
					}
				}

				/* stroke */
				interp_v3_v3v3(tcolor, gps->palcolor->rgb, gpl->tintcolor, gpl->tintcolor[3]);
				tcolor[3] = gps->palcolor->rgb[3] * gpl->opacity;
				copy_v4_v4(ink, tcolor);

				short sthickness = gps->thickness + gpl->thickness;
				if (sthickness > 0) {
					struct Batch *stroke_geom = gpencil_get_stroke_geom(gps, sthickness, diff_mat, ink);
					DRW_shgroup_call_add(strokegrp, stroke_geom, ob->obmat);
				}
			}
		}
	}
}

static void GPENCIL_draw_scene(void *vedata)
{
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	GPENCIL_FramebufferList *fbl = ((GPENCIL_Data *)vedata)->fbl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	/* Default framebuffer and texture */
	DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();
	DefaultTextureList *dtxl = DRW_viewport_texture_list_get();

	UNUSED_VARS(fbl, dfbl, dtxl);
	if (stl->storage->pal_id > 0) {
		DRW_draw_pass(psl->pass);
	}
}

static const DrawEngineDataSize GPENCIL_data_size = DRW_VIEWPORT_DATA_SIZE(GPENCIL_Data);

DrawEngineType draw_engine_gpencil_type = {
	NULL, NULL,
	N_("GpencilMode"),
	&GPENCIL_data_size,
	&GPENCIL_engine_init,
	&GPENCIL_engine_free,
	&GPENCIL_cache_init,
	&GPENCIL_cache_populate,
	NULL,
	NULL,
	&GPENCIL_draw_scene
};
