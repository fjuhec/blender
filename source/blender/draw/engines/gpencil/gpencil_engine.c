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
#include "BKE_image.h"
#include "ED_gpencil.h"

#include "DNA_gpencil_types.h"
#include "DNA_view3d_types.h"

 /* If builtin shaders are needed */
#include "GPU_shader.h"
#include "GPU_texture.h"
#include "GPU_glew.h"

#include "draw_common.h"

#include "draw_mode_engines.h"
#include "gpencil_engine.h"

#include "IMB_imbuf_types.h"

extern char datatoc_gpencil_fill_vert_glsl[];
extern char datatoc_gpencil_fill_frag_glsl[];
extern char datatoc_gpencil_stroke_vert_glsl[];
extern char datatoc_gpencil_stroke_geom_glsl[];
extern char datatoc_gpencil_stroke_frag_glsl[];

/* *********** LISTS *********** */
#define MAX_GPENCIL_MAT 512 

typedef struct GPENCIL_Storage {
	int pal_id;
	int t_mix[MAX_GPENCIL_MAT];
	int t_flip[MAX_GPENCIL_MAT];
	int fill_style[MAX_GPENCIL_MAT];
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
typedef struct g_data{
	DRWShadingGroup *shgrps_edit_volumetric;
	DRWShadingGroup *shgrps_point_volumetric;
	DRWShadingGroup *shgrps_drawing_stroke;
	DRWShadingGroup *shgrps_drawing_fill;
	bool scene_draw;
} g_data; /* Transient data */

static struct {
	struct GPUShader *gpencil_fill_sh;
	struct GPUShader *gpencil_stroke_sh;
	struct GPUShader *gpencil_volumetric_sh;
	struct GPUShader *gpencil_drawing_fill_sh;
} e_data = {NULL}; /* Engine data */

/* *********** FUNCTIONS *********** */

static void GPENCIL_engine_init(void *vedata)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

	/* normal fill shader */
	if (!e_data.gpencil_fill_sh) {
		e_data.gpencil_fill_sh = DRW_shader_create(datatoc_gpencil_fill_vert_glsl, NULL,
			datatoc_gpencil_fill_frag_glsl,
			NULL);
	}

	/* normal stroke shader using geometry to display lines */
	if (!e_data.gpencil_stroke_sh) {
		e_data.gpencil_stroke_sh = DRW_shader_create(datatoc_gpencil_stroke_vert_glsl,
			datatoc_gpencil_stroke_geom_glsl,
			datatoc_gpencil_stroke_frag_glsl,
			NULL);
	}

	/* used for edit points or strokes with one point only */
	if (!e_data.gpencil_volumetric_sh) {
		e_data.gpencil_volumetric_sh = GPU_shader_get_builtin_shader(GPU_SHADER_3D_POINT_VARYING_SIZE_VARYING_COLOR);
	}

	/* used to filling during drawing */
	if (!e_data.gpencil_drawing_fill_sh) {
		e_data.gpencil_drawing_fill_sh = GPU_shader_get_builtin_shader(GPU_SHADER_3D_SMOOTH_COLOR);
	}

	if (!stl->storage) {
		stl->storage = MEM_callocN(sizeof(GPENCIL_Storage), "GPENCIL_Storage");
	}
}

static void GPENCIL_engine_free(void)
{
	/* only free custom shaders, builtin shaders are freed in blender close */
	DRW_SHADER_FREE_SAFE(e_data.gpencil_fill_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_stroke_sh);
}

/* create shading group for filling */
static DRWShadingGroup *GPENCIL_shgroup_fill_create(GPENCIL_Data *vedata, DRWPass *pass, PaletteColor *palcolor, int id)
{
	GPENCIL_TextureList *txl = ((GPENCIL_Data *)vedata)->txl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

	DRWShadingGroup *grp = DRW_shgroup_create(e_data.gpencil_fill_sh, pass);
	DRW_shgroup_uniform_vec4(grp, "color2", palcolor->scolor, 1);
	stl->storage->fill_style[id] = palcolor->fill_style;
	DRW_shgroup_uniform_int(grp, "fill_type", &stl->storage->fill_style[id], 1);
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

	stl->storage->t_mix[id] = palcolor->flag & PAC_COLOR_TEX_MIX ? 1 : 0;
	DRW_shgroup_uniform_int(grp, "t_mix", &stl->storage->t_mix[id], 1);

	stl->storage->t_flip[id] = palcolor->flag & PAC_COLOR_FLIP_FILL ? 1 : 0;
	DRW_shgroup_uniform_int(grp, "t_flip", &stl->storage->t_flip[id], 1);

	/* image texture */
	if ((palcolor->fill_style == FILL_STYLE_TEXTURE) || (palcolor->flag & PAC_COLOR_TEX_MIX)) {
		ImBuf *ibuf;
		Image *image = palcolor->ima;
		unsigned int *bind = &image->bindcode[TEXTARGET_TEXTURE_2D];
		UNUSED_VARS(bind);
		ImageUser iuser = { NULL };
		void *lock;

		iuser.ok = true;

		ibuf = BKE_image_acquire_ibuf(image, &iuser, &lock);

		if (ibuf == NULL || ibuf->rect == NULL) {
			BKE_image_release_ibuf(image, ibuf, NULL);
		}
		else {
			txl->texture = GPU_texture_from_blender(palcolor->ima, &iuser, GL_TEXTURE_2D, true, 0.0, 0);
			DRW_shgroup_uniform_texture(grp, "myTexture", txl->texture, 0);

			// TODO: How apply these parameters
			//if (flag & PAC_COLOR_TEX_CLAMP) {
			//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
			//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
			//}
			//else {
			//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			//}

			BKE_image_release_ibuf(image, ibuf, NULL);
		}
	}

	return grp;
}

/* create shading group for volumetric points */
static DRWShadingGroup *GPENCIL_shgroup_point_volumetric_create(GPENCIL_Data *UNUSED(vedata), DRWPass *pass)
{
	DRWShadingGroup *grp = DRW_shgroup_create(e_data.gpencil_volumetric_sh, pass);

	return grp;
}

/* create shading group for strokes */
static DRWShadingGroup *GPENCIL_shgroup_stroke_create(GPENCIL_Data *UNUSED(vedata), DRWPass *pass, PaletteColor *UNUSED(palcolor))
{
	const float *viewport_size = DRW_viewport_size_get();

	DRWShadingGroup *grp = DRW_shgroup_create(e_data.gpencil_stroke_sh, pass);
	DRW_shgroup_uniform_vec2(grp, "Viewport", viewport_size, 1);

	return grp;
}

/* create shading group for edit points using volumetric */
static DRWShadingGroup *GPENCIL_shgroup_edit_volumetric_create(GPENCIL_Data *UNUSED(vedata), DRWPass *pass)
{
	DRWShadingGroup *grp = DRW_shgroup_create(e_data.gpencil_volumetric_sh, pass);

	return grp;
}

/* create shading group for drawing strokes in buffer */
static DRWShadingGroup *GPENCIL_shgroup_drawing_stroke_create(
        GPENCIL_Data *UNUSED(vedata), DRWPass *pass, PaletteColor *UNUSED(palcolor))
{
	DRWShadingGroup *grp = DRW_shgroup_create(e_data.gpencil_stroke_sh, pass);
	DRW_shgroup_uniform_vec2(grp, "Viewport", DRW_viewport_size_get(), 1);
	return grp;
}

/* create shading group for drawing fill in buffer */
static DRWShadingGroup *GPENCIL_shgroup_drawing_fill_create(
        GPENCIL_Data *UNUSED(vedata), DRWPass *pass, PaletteColor *UNUSED(palcolor))
{
	DRWShadingGroup *grp = DRW_shgroup_create(e_data.gpencil_drawing_fill_sh, pass);
	return grp;
}

static void GPENCIL_cache_init(void *vedata)
{
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

	const DRWContextState *draw_ctx = DRW_context_state_get();
	PaletteColor *palcolor = CTX_data_active_palettecolor(draw_ctx->evil_C);

	if (!stl->g_data) {
		/* Alloc transient pointers */
		stl->g_data = MEM_mallocN(sizeof(g_data), "g_data");
		stl->g_data->scene_draw = false;
	}

	{
		/* Stroke pass */
		DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND | DRW_STATE_WRITE_DEPTH;
		psl->stroke_pass = DRW_pass_create("Gpencil Stroke Pass", state);
		stl->storage->pal_id = 0;
		memset(stl->storage->shgrps_fill, 0, sizeof(DRWShadingGroup *) * MAX_GPENCIL_MAT);
		memset(stl->storage->shgrps_stroke, 0, sizeof(DRWShadingGroup *) * MAX_GPENCIL_MAT);
		memset(stl->storage->materials, 0, sizeof(PaletteColor *) * MAX_GPENCIL_MAT);
		stl->g_data->shgrps_point_volumetric = GPENCIL_shgroup_point_volumetric_create(vedata, psl->stroke_pass);

		/* edit pass */
		psl->edit_pass = DRW_pass_create("Gpencil Edit Pass", state);
		stl->g_data->shgrps_edit_volumetric = GPENCIL_shgroup_edit_volumetric_create(vedata, psl->edit_pass);

		/* drawing buffer pass */
		psl->drawing_pass = DRW_pass_create("Gpencil Drawing Pass", state);
		stl->g_data->shgrps_drawing_stroke = GPENCIL_shgroup_drawing_stroke_create(vedata, psl->drawing_pass, palcolor);
		stl->g_data->shgrps_drawing_fill = GPENCIL_shgroup_drawing_fill_create(vedata, psl->drawing_pass, palcolor);
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

/* main function to draw strokes */
static void gpencil_draw_strokes(void *vedata, ToolSettings *ts, Object *ob,
	bGPdata *gpd, bGPDlayer *gpl, bGPDframe *gpf,
	const float opacity, const float tintcolor[4], const bool onion, const bool custonion)
{
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	const DRWContextState *draw_ctx = DRW_context_state_get();
	RegionView3D *rv3d = draw_ctx->rv3d;

	DRWShadingGroup *fillgrp;
	DRWShadingGroup *strokegrp;
	float tcolor[4];
	float matrix[4][4];
	float ink[4];

#if 0 // TODO convert xray function
	const int no_xray = (dflag & GP_DRAWDATA_NO_XRAY);
	int mask_orig = 0;

	if (no_xray) {
		glGetIntegerv(GL_DEPTH_WRITEMASK, &mask_orig);
		glDepthMask(0);
		glEnable(GL_DEPTH_TEST);
		/* first arg is normally rv3d->dist, but this isn't
		* available here and seems to work quite well without */
		bglPolygonOffset(1.0f, 1.0f);
	}
#endif

	/* get parent matrix and save as static data */
	ED_gpencil_parent_location(ob, gpd, gpl, matrix);
	copy_m4_m4(gpf->matrix, matrix);

	for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
		/* check if stroke can be drawn */
		if (gpencil_can_draw_stroke(rv3d, gpf, gps) == false) {
			continue;
		}
		/* try to find shader group or create a new one */
		if (gps->totpoints > 1) {
			int id = GPENCIL_shgroup_find(stl->storage, gps->palcolor);
			if (id == -1) {
				id = stl->storage->pal_id;
				stl->storage->materials[id] = gps->palcolor;
				stl->storage->shgrps_fill[id] = GPENCIL_shgroup_fill_create(vedata, psl->stroke_pass, gps->palcolor, id);
				stl->storage->shgrps_stroke[id] = GPENCIL_shgroup_stroke_create(vedata, psl->stroke_pass, gps->palcolor);
				++stl->storage->pal_id;
			}

			fillgrp = stl->storage->shgrps_fill[id];
			strokegrp = stl->storage->shgrps_stroke[id];
		}
		/* fill */
		if (gps->totpoints >= 3) {
			float tfill[4];
			/* set color using palette, tint color and opacity */
			interp_v3_v3v3(tfill, gps->palcolor->fill, tintcolor, tintcolor[3]);
			tfill[3] = gps->palcolor->fill[3] * gpl->opacity;
			if ((tfill[3] > GPENCIL_ALPHA_OPACITY_THRESH) || (gps->palcolor->fill_style > 0)) {
				const float *color;
				if (!onion) {
					color = tfill;
				}
				else {
					if (custonion) {
						color = tintcolor;
					}
					else {
						ARRAY_SET_ITEMS(tfill, UNPACK3(gps->palcolor->fill), tintcolor[3]);
						color = tfill;
					}
				}
				struct Batch *fill_geom = gpencil_get_fill_geom(gps, color);
				DRW_shgroup_call_add(fillgrp, fill_geom, gpf->matrix);
			}
		}

		/* stroke */
		/* set color using palette, tint color and opacity */
		if (!onion) {
			interp_v3_v3v3(tcolor, gps->palcolor->rgb, tintcolor, tintcolor[3]);
			tcolor[3] = gps->palcolor->rgb[3] * opacity;
			copy_v4_v4(ink, tcolor);
		}
		else {
			if (custonion) {
				copy_v4_v4(ink, tintcolor);
			}
			else {
				ARRAY_SET_ITEMS(tcolor, gps->palcolor->rgb[0], gps->palcolor->rgb[1], gps->palcolor->rgb[2], opacity);
				copy_v4_v4(ink, tcolor);
			}
		}
		short sthickness = gps->thickness + gpl->thickness;
		if (sthickness > 0) {
			if (gps->totpoints > 1) {
				struct Batch *stroke_geom = gpencil_get_stroke_geom(gpf, gps, sthickness, ink);
				DRW_shgroup_call_add(strokegrp, stroke_geom, gpf->matrix);
			}
			else if (gps->totpoints == 1) {
				struct Batch *point_geom = gpencil_get_point_geom(gps->points, sthickness, ink);
				DRW_shgroup_call_add(stl->g_data->shgrps_point_volumetric, point_geom, gpf->matrix);
			}
		}
		/* edit points (only in edit mode) */
		if (!onion) {
			if ((gpl->flag & GP_LAYER_LOCKED) == 0 && (gpd->flag & GP_DATA_STROKE_EDITMODE))
			{
				if (gps->flag & GP_STROKE_SELECT) {
					if ((gpl->flag & GP_LAYER_UNLOCK_COLOR) || ((gps->palcolor->flag & PC_COLOR_LOCKED) == 0)) {
						struct Batch *edit_geom = gpencil_get_edit_geom(gps, ts->gp_sculpt.alpha, gpd->flag);
						DRW_shgroup_call_add(stl->g_data->shgrps_edit_volumetric, edit_geom, gpf->matrix);
					}
				}
			}
		}

#if 0 // TODO convert xray function
		if (no_xray) {
			glDepthMask(mask_orig);
			glDisable(GL_DEPTH_TEST);

			bglPolygonOffset(0.0, 0.0);
		}
#endif
	}
}

/* draw stroke in drawing buffer */
static void gpencil_draw_buffer_strokes(void *vedata, ToolSettings *ts, bGPdata *gpd)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	bGPDbrush *brush = BKE_gpencil_brush_getactive(ts);

	/* drawing strokes */
	/* Check if may need to draw the active stroke cache, only if this layer is the active layer
	* that is being edited. (Stroke buffer is currently stored in gp-data)
	*/
	if (ED_gpencil_session_active() && (gpd->sbuffer_size > 0))
	{
		if ((gpd->sbuffer_sflag & GP_STROKE_ERASER) == 0) {
			/* It should also be noted that sbuffer contains temporary point types
			* i.e. tGPspoints NOT bGPDspoints
			*/
			short lthick = brush->thickness;
			float matrix[4][4];
			unit_m4(matrix);

			if (gpd->sbuffer_size == 1) {
				struct Batch *point_geom = gpencil_get_buffer_point_geom(gpd, lthick);
				DRW_shgroup_call_add(stl->g_data->shgrps_point_volumetric, point_geom, matrix);
			}
			else {
				/* use unit matrix because the buffer is in screen space and does not need conversion */
				struct Batch *drawing_stroke_geom = gpencil_get_buffer_stroke_geom(gpd, matrix, lthick);
				DRW_shgroup_call_add(stl->g_data->shgrps_drawing_stroke, drawing_stroke_geom, matrix);

				if ((gpd->sbuffer_size >= 3) && ((gpd->sfill[3] > GPENCIL_ALPHA_OPACITY_THRESH) || (gpd->bfill_style > 0))) {
					/* if not solid, fill is simulated with solid color */
					if (gpd->bfill_style > 0) {
						gpd->sfill[3] = 0.5f;
					}
					struct Batch *drawing_fill_geom = gpencil_get_buffer_fill_geom(gpd->sbuffer, gpd->sbuffer_size, gpd->sfill);
					DRW_shgroup_call_add(stl->g_data->shgrps_drawing_fill, drawing_fill_geom, matrix);
				}
			}
		}
	}
}

/* draw onion-skinning for a layer */
static void gpencil_draw_onionskins(void *vedata, ToolSettings *ts, Object *ob,bGPdata *gpd, bGPDlayer *gpl, bGPDframe *gpf)
{
	const float default_color[3] = { UNPACK3(U.gpencil_new_layer_col) };
	const float alpha = 1.0f;
	float color[4];

	/* 1) Draw Previous Frames First */
	if (gpl->flag & GP_LAYER_GHOST_PREVCOL) {
		copy_v3_v3(color, gpl->gcolor_prev);
	}
	else {
		copy_v3_v3(color, default_color);
	}

	if (gpl->gstep > 0) {
		/* draw previous frames first */
		for (bGPDframe *gf = gpf->prev; gf; gf = gf->prev) {
			/* check if frame is drawable */
			if ((gpf->framenum - gf->framenum) <= gpl->gstep) {
				/* alpha decreases with distance from curframe index */
				float fac = 1.0f - ((float)(gpf->framenum - gf->framenum) / (float)(gpl->gstep + 1));
				color[3] = alpha * fac * 0.66f;
				gpencil_draw_strokes(vedata, ts, ob, gpd, gpl, gf, 1.0f, color, true, gpl->flag & GP_LAYER_GHOST_PREVCOL);
			}
			else
				break;
		}
	}
	else if (gpl->gstep == 0) {
		/* draw the strokes for the ghost frames (at half of the alpha set by user) */
		if (gpf->prev) {
			color[3] = (alpha / 7);
			gpencil_draw_strokes(vedata, ts, ob, gpd, gpl, gpf->prev, 1.0f, color, true, gpl->flag & GP_LAYER_GHOST_PREVCOL);
		}
	}
	else {
		/* don't draw - disabled */
	}

	/* 2) Now draw next frames */
	if (gpl->flag & GP_LAYER_GHOST_NEXTCOL) {
		copy_v3_v3(color, gpl->gcolor_next);
	}
	else {
		copy_v3_v3(color, default_color);
	}

	if (gpl->gstep_next > 0) {
		/* now draw next frames */
		for (bGPDframe *gf = gpf->next; gf; gf = gf->next) {
			/* check if frame is drawable */
			if ((gf->framenum - gpf->framenum) <= gpl->gstep_next) {
				/* alpha decreases with distance from curframe index */
				float fac = 1.0f - ((float)(gf->framenum - gpf->framenum) / (float)(gpl->gstep_next + 1));
				color[3] = alpha * fac * 0.66f;
				gpencil_draw_strokes(vedata, ts, ob, gpd, gpl, gf, 1.0f, color, true, gpl->flag & GP_LAYER_GHOST_NEXTCOL);
			}
			else
				break;
		}
	}
	else if (gpl->gstep_next == 0) {
		/* draw the strokes for the ghost frames (at half of the alpha set by user) */
		if (gpf->next) {
			color[3] = (alpha / 4);
			gpencil_draw_strokes(vedata, ts, ob, gpd, gpl, gpf->next, 1.0f, color, true, gpl->flag & GP_LAYER_GHOST_NEXTCOL);
		}
	}
	else {
		/* don't draw - disabled */
	}
}

/* helper for complete grease pencil datablock */
static void gpencil_draw_datablock(void *vedata, Scene *scene, Object *ob, ToolSettings *ts, bGPdata *gpd)
{
	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		/* don't draw layer if hidden */
		if (gpl->flag & GP_LAYER_HIDE)
			continue;

		bGPDframe *gpf = BKE_gpencil_layer_getframe(gpl, CFRA, 0);
		if (gpf == NULL)
			continue;
		/* draw onion skins */
		if ((gpl->flag & GP_LAYER_ONIONSKIN) || (gpl->flag & GP_LAYER_GHOST_ALWAYS))
		{
			gpencil_draw_onionskins(vedata, ts, ob, gpd, gpl, gpf);
		}
		/* draw normal strokes */
		gpencil_draw_strokes(vedata, ts, ob, gpd, gpl, gpf, gpl->opacity, gpl->tintcolor, false, false);
	}
	/* draw current painting strokes */
	gpencil_draw_buffer_strokes(vedata, ts, gpd);
}

static void GPENCIL_cache_populate(void *vedata, Object *ob)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	const DRWContextState *draw_ctx = DRW_context_state_get();
	Scene *scene = draw_ctx->scene;
	ToolSettings *ts = scene->toolsettings;

	/* scene datablock (only once) */
	if (stl->g_data->scene_draw == false) {
		stl->g_data->scene_draw = true;
		if (scene && scene->gpd) {
			gpencil_draw_datablock(vedata, scene, NULL, ts, scene->gpd);
		}
	}

	/* object datablock */
	if (ob->type == OB_GPENCIL && ob->gpd) {
		gpencil_draw_datablock(vedata, scene, ob, ts, ob->gpd);
	}
}

static void GPENCIL_cache_finish(void *vedata)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	stl->g_data->scene_draw = false;
}

static void GPENCIL_draw_scene(void *vedata)
{
	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;

	DRW_draw_pass(psl->stroke_pass);
	DRW_draw_pass(psl->edit_pass);
	/* current buffer */
	DRW_draw_pass(psl->drawing_pass);
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
	&GPENCIL_cache_finish,
	NULL,
	&GPENCIL_draw_scene
};
