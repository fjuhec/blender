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

/** \file blender/draw/engines/gpencil/gpencil_engine.c
 *  \ingroup draw
 */

#include "DRW_engine.h"
#include "DRW_render.h"

#include "BKE_global.h"
#include "BKE_camera.h"
#include "BKE_object.h"
#include "BKE_paint.h"
#include "BKE_gpencil.h"

#include "DNA_gpencil_types.h"
#include "DNA_view3d_types.h"

#include "DEG_depsgraph_query.h"

#include "draw_mode_engines.h"

#include "UI_resources.h"

#include "RE_pipeline.h"

#include "gpencil_engine.h"

#include "ED_screen.h"
#include "ED_gpencil.h"

extern char datatoc_gpencil_fill_vert_glsl[];
extern char datatoc_gpencil_fill_frag_glsl[];
extern char datatoc_gpencil_stroke_vert_glsl[];
extern char datatoc_gpencil_stroke_geom_glsl[];
extern char datatoc_gpencil_stroke_frag_glsl[];
extern char datatoc_gpencil_zdepth_mix_frag_glsl[];
extern char datatoc_gpencil_front_depth_mix_frag_glsl[];
extern char datatoc_gpencil_point_vert_glsl[];
extern char datatoc_gpencil_point_geom_glsl[];
extern char datatoc_gpencil_point_frag_glsl[];
extern char datatoc_gpencil_gaussian_blur_frag_glsl[];
extern char datatoc_gpencil_wave_frag_glsl[];
extern char datatoc_gpencil_pixel_frag_glsl[];
extern char datatoc_gpencil_swirl_frag_glsl[];
extern char datatoc_gpencil_flip_frag_glsl[];
extern char datatoc_gpencil_light_frag_glsl[];
extern char datatoc_gpencil_painting_frag_glsl[];
extern char datatoc_gpencil_paper_frag_glsl[];
extern char datatoc_gpencil_edit_point_vert_glsl[];
extern char datatoc_gpencil_edit_point_geom_glsl[];
extern char datatoc_gpencil_edit_point_frag_glsl[];

/* *********** STATIC *********** */
static GPENCIL_e_data e_data = {NULL}; /* Engine data */

/* *********** FUNCTIONS *********** */

static void GPENCIL_engine_init(void *vedata)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	GPENCIL_FramebufferList *fbl = ((GPENCIL_Data *)vedata)->fbl;

	/* Go full 32bits for rendering */
	DRWTextureFormat fb_format = DRW_state_is_image_render() ? DRW_TEX_RGBA_32 : DRW_TEX_RGBA_16;

	if (DRW_state_is_fbo()) {
		const float *viewport_size = DRW_viewport_size_get();

		DRWFboTexture tex_color[2] = {
			{&e_data.temp_fbcolor_depth_tx, DRW_TEX_DEPTH_24_STENCIL_8, DRW_TEX_TEMP},
			{&e_data.temp_fbcolor_color_tx, fb_format, DRW_TEX_TEMP}
		};
		/* init temp framebuffer */
		DRW_framebuffer_init(
		        &fbl->temp_color_fb, &draw_engine_gpencil_type,
		        (int)viewport_size[0], (int)viewport_size[1],
		        tex_color, ARRAY_SIZE(tex_color));

		/* vfx */
		DRWFboTexture vfx_color_a[2] = {
			{&e_data.vfx_fbcolor_depth_tx_a, DRW_TEX_DEPTH_24_STENCIL_8, DRW_TEX_TEMP},
			{&e_data.vfx_fbcolor_color_tx_a, fb_format, DRW_TEX_TEMP}
		};
		DRW_framebuffer_init(
		        &fbl->vfx_color_fb_a, &draw_engine_gpencil_type,
		        (int)viewport_size[0], (int)viewport_size[1],
		        vfx_color_a, ARRAY_SIZE(vfx_color_a));

		DRWFboTexture vfx_color_b[2] = {
			{&e_data.vfx_fbcolor_depth_tx_b, DRW_TEX_DEPTH_24_STENCIL_8, DRW_TEX_TEMP},
			{&e_data.vfx_fbcolor_color_tx_b, fb_format, DRW_TEX_TEMP}
		};
		DRW_framebuffer_init(
		        &fbl->vfx_color_fb_b, &draw_engine_gpencil_type,
		        (int)viewport_size[0], (int)viewport_size[1],
		        vfx_color_b, ARRAY_SIZE(vfx_color_b));

		/* painting framebuffer to speed up drawing process (always 16 bits) */
		DRWFboTexture tex_painting[2] = {
			{&e_data.painting_depth_tx, DRW_TEX_DEPTH_24_STENCIL_8, DRW_TEX_TEMP},
			{&e_data.painting_color_tx, DRW_TEX_RGBA_16, DRW_TEX_TEMP}
		};
		DRW_framebuffer_init(
		        &fbl->painting_fb, &draw_engine_gpencil_type,
		        (int)viewport_size[0], (int)viewport_size[1],
		        tex_painting, ARRAY_SIZE(tex_painting));
	}
	/* normal fill shader */
	if (!e_data.gpencil_fill_sh) {
		e_data.gpencil_fill_sh = DRW_shader_create(
		        datatoc_gpencil_fill_vert_glsl, NULL,
		        datatoc_gpencil_fill_frag_glsl, NULL);
	}

	/* normal stroke shader using geometry to display lines */
	if (!e_data.gpencil_stroke_sh) {
		e_data.gpencil_stroke_sh = DRW_shader_create(
		        datatoc_gpencil_stroke_vert_glsl,
		        datatoc_gpencil_stroke_geom_glsl,
		        datatoc_gpencil_stroke_frag_glsl,
		        NULL);
	}
	if (!e_data.gpencil_point_sh) {
		e_data.gpencil_point_sh = DRW_shader_create(
		        datatoc_gpencil_point_vert_glsl,
		        datatoc_gpencil_point_geom_glsl,
		        datatoc_gpencil_point_frag_glsl,
		        NULL);
	}
	/* used for edit points or strokes with one point only */
	if (!e_data.gpencil_edit_point_sh) {
		e_data.gpencil_edit_point_sh = DRW_shader_create(
			datatoc_gpencil_edit_point_vert_glsl,
			datatoc_gpencil_edit_point_geom_glsl,
			datatoc_gpencil_edit_point_frag_glsl, NULL);
	}

	/* used for edit lines for edit modes */
	if (!e_data.gpencil_line_sh) {
		e_data.gpencil_line_sh = GPU_shader_get_builtin_shader(GPU_SHADER_3D_FLAT_COLOR);
	}

	/* used to filling during drawing */
	if (!e_data.gpencil_drawing_fill_sh) {
		e_data.gpencil_drawing_fill_sh = GPU_shader_get_builtin_shader(GPU_SHADER_3D_SMOOTH_COLOR);
	}

	if (!stl->storage) {
		stl->storage = MEM_callocN(sizeof(GPENCIL_Storage), "GPENCIL_Storage");
	}

	unit_m4(stl->storage->unit_matrix);

	/* blank texture used if no texture defined for fill shader */
	if (!e_data.gpencil_blank_texture) {
		e_data.gpencil_blank_texture = DRW_gpencil_create_blank_texture(16, 16);
	}
}

static void GPENCIL_engine_free(void)
{
	/* only free custom shaders, builtin shaders are freed in blender close */
	DRW_SHADER_FREE_SAFE(e_data.gpencil_fill_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_stroke_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_point_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_edit_point_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_fullscreen_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_vfx_blur_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_vfx_wave_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_vfx_pixel_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_vfx_swirl_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_vfx_flip_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_vfx_light_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_painting_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_front_depth_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_paper_sh);

	DRW_TEXTURE_FREE_SAFE(e_data.gpencil_blank_texture);
}

static void GPENCIL_cache_init(void *vedata)
{
	if (G.debug_value == 665) {
		printf("GPENCIL_cache_init\n");
	}

	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	Object *ob = NULL;
	bGPdata *gpd = NULL;
	const DRWContextState *draw_ctx = DRW_context_state_get();
	Scene *scene = draw_ctx->scene;
	ToolSettings *ts = scene->toolsettings;
	View3D *v3d = draw_ctx->v3d;
	Object *obact = draw_ctx->obact;

	if (!stl->g_data) {
		/* Alloc transient pointers */
		stl->g_data = MEM_mallocN(sizeof(g_data), "g_data");
		stl->storage->xray = GP_XRAY_FRONT; /* used for drawing */
		stl->storage->stroke_style = STROKE_STYLE_SOLID; /* used for drawing */
	}
	/* reset total shading groups */
	stl->g_data->tot_sh = 0;
	stl->g_data->tot_sh_stroke = 0;
	stl->g_data->tot_sh_fill = 0;
	stl->g_data->tot_sh_point = 0;

	stl->g_data->shgrps_edit_line = NULL;
	stl->g_data->shgrps_edit_point = NULL;
	
	if (!stl->shgroups) {
		/* Alloc maximum size because count strokes is very slow and can be very complex due onion skinning.
		   I tried to allocate only one block and using realloc, increasing the size when read a new strokes
		   in cache_finish, but the realloc produce weird things on screen, so we keep as is while we found
		   a better solution
		 */
		stl->shgroups = MEM_mallocN(sizeof(GPENCIL_shgroup) * GPENCIL_MAX_SHGROUPS, "GPENCIL_shgroup");
	}

	if (!stl->vfx) {
		stl->vfx = MEM_mallocN(sizeof(GPENCIL_vfx) * GPENCIL_MAX_GP_OBJ, "GPENCIL_vfx");
	}
	
	/* init gp objects cache */
	stl->g_data->gp_cache_used = 0;
	stl->g_data->gp_cache_size = 0;
	stl->g_data->gp_object_cache = NULL;

	/* full screen for mix zdepth*/
	if (!e_data.gpencil_fullscreen_sh) {
		e_data.gpencil_fullscreen_sh = DRW_shader_create_fullscreen(datatoc_gpencil_zdepth_mix_frag_glsl, NULL);
	}
	if (!e_data.gpencil_vfx_blur_sh) {
		e_data.gpencil_vfx_blur_sh = DRW_shader_create_fullscreen(datatoc_gpencil_gaussian_blur_frag_glsl, NULL);
	}
	if (!e_data.gpencil_vfx_wave_sh) {
		e_data.gpencil_vfx_wave_sh = DRW_shader_create_fullscreen(datatoc_gpencil_wave_frag_glsl, NULL);
	}
	if (!e_data.gpencil_vfx_pixel_sh) {
		e_data.gpencil_vfx_pixel_sh = DRW_shader_create_fullscreen(datatoc_gpencil_pixel_frag_glsl, NULL);
	}
	if (!e_data.gpencil_vfx_swirl_sh) {
		e_data.gpencil_vfx_swirl_sh = DRW_shader_create_fullscreen(datatoc_gpencil_swirl_frag_glsl, NULL);
	}
	if (!e_data.gpencil_vfx_flip_sh) {
		e_data.gpencil_vfx_flip_sh = DRW_shader_create_fullscreen(datatoc_gpencil_flip_frag_glsl, NULL);
	}
	if (!e_data.gpencil_vfx_light_sh) {
		e_data.gpencil_vfx_light_sh = DRW_shader_create_fullscreen(datatoc_gpencil_light_frag_glsl, NULL);
	}
	if (!e_data.gpencil_painting_sh) {
		e_data.gpencil_painting_sh = DRW_shader_create_fullscreen(datatoc_gpencil_painting_frag_glsl, NULL);
	}
	if (!e_data.gpencil_front_depth_sh) {
		e_data.gpencil_front_depth_sh = DRW_shader_create_fullscreen(datatoc_gpencil_front_depth_mix_frag_glsl, NULL);
	}
	if (!e_data.gpencil_paper_sh) {
		e_data.gpencil_paper_sh = DRW_shader_create_fullscreen(datatoc_gpencil_paper_frag_glsl, NULL);
	}

	{
		/* Stroke pass */
		psl->stroke_pass = DRW_pass_create("GPencil Stroke Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_ALWAYS | DRW_STATE_BLEND);
		stl->storage->shgroup_id = 0;

		/* edit pass */
		psl->edit_pass = DRW_pass_create("GPencil Edit Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND);

		/* detect if playing animation */
		stl->storage->playing = 0;
		if (draw_ctx->evil_C) {
			stl->storage->playing = ED_screen_animation_playing(CTX_wm_manager(draw_ctx->evil_C)) != NULL ? 1 : 0;
		}
		/* save render state */
		stl->storage->is_render = DRW_state_is_image_render();

		/* save pixsize */
		if (!stl->storage->is_render) {
			stl->storage->pixsize = DRW_viewport_pixelsize_get();
		}
		else {
			stl->storage->pixsize = &stl->storage->render_pixsize;
		}

		/* detect if painting session */
		bGPdata *obact_gpd = NULL;
		if ((obact) && (obact->type == OB_GPENCIL) && (obact->data))
			obact_gpd = obact->data;

		if ((obact_gpd) && (obact_gpd->flag & GP_DATA_STROKE_PAINTMODE) &&
			(stl->storage->playing == 0) &&
			((ts->gpencil_simplify & GP_TOOL_FLAG_DISABLE_FAST_DRAWING) == 0))
		{
			if (((obact_gpd->sbuffer_sflag & GP_STROKE_ERASER) == 0) && (obact_gpd->sbuffer_size > 1)) {
				stl->g_data->session_flag = GP_DRW_PAINT_PAINTING;
			}
			else {
				stl->g_data->session_flag = GP_DRW_PAINT_IDLE;
			}
		}
		else {
			/* if not drawing mode */
			stl->g_data->session_flag = GP_DRW_PAINT_HOLD;
		}

		ob = draw_ctx->obact;
		if ((ob) && (ob->type == OB_GPENCIL)) {
			gpd = ob->data;
			/* for some reason, when press play there is a delay in the animation flag check
			* and this produces errors. To be sure, we set cache as dirty because the frame
			* is changing.
			*/
			if (stl->storage->playing == 1) {
				gpd->flag |= GP_DATA_CACHE_IS_DIRTY;
			}
			/* if render, set as dirty to update all data */
			else if (stl->storage->is_render == true) {
				gpd->flag |= GP_DATA_CACHE_IS_DIRTY;
			}
		}
		bGPDpaletteref *palslot = BKE_gpencil_paletteslot_get_active(gpd);
		PaletteColor *palcolor = BKE_palette_color_get_active((palslot) ? palslot->palette : NULL);
		if (palcolor) {
			stl->storage->stroke_style = palcolor->stroke_style;
			stl->storage->color_type = GPENCIL_COLOR_SOLID;
			if (palcolor->stroke_style == STROKE_STYLE_TEXTURE) {
				stl->storage->color_type = GPENCIL_COLOR_TEXTURE;
				if (palcolor->flag & PAC_COLOR_PATTERN) {
					stl->storage->color_type = GPENCIL_COLOR_PATTERN;
				}
			}
		}
		else {
			stl->storage->stroke_style = STROKE_STYLE_SOLID;
			stl->storage->color_type = GPENCIL_COLOR_SOLID;
		}

		/* drawing buffer pass */
		psl->drawing_pass = DRW_pass_create("GPencil Drawing Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND);

		/* full screen pass to combine the result */
		struct Gwn_Batch *quad = DRW_cache_fullscreen_quad_get();
		psl->mix_pass = DRW_pass_create("GPencil Mix Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);
		DRWShadingGroup *mix_shgrp = DRW_shgroup_create(e_data.gpencil_fullscreen_sh, psl->mix_pass);
		stl->g_data->tot_sh++;
		DRW_shgroup_call_add(mix_shgrp, quad, NULL);
		DRW_shgroup_uniform_buffer(mix_shgrp, "strokeColor", &e_data.input_color_tx);
		DRW_shgroup_uniform_buffer(mix_shgrp, "strokeDepth", &e_data.input_depth_tx);

		/* mix pass no blend */
		struct Gwn_Batch *quad_noblend = DRW_cache_fullscreen_quad_get();
		psl->mix_pass_noblend = DRW_pass_create("GPencil Mix Pass no blend", DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);
		DRWShadingGroup *mix_shgrp_noblend = DRW_shgroup_create(e_data.gpencil_fullscreen_sh, psl->mix_pass_noblend);
		stl->g_data->tot_sh++;
		DRW_shgroup_call_add(mix_shgrp_noblend, quad_noblend, NULL);
		DRW_shgroup_uniform_buffer(mix_shgrp_noblend, "strokeColor", &e_data.input_color_tx);
		DRW_shgroup_uniform_buffer(mix_shgrp_noblend, "strokeDepth", &e_data.input_depth_tx);

		/* vfx copy pass from txtb to txta */
		struct Gwn_Batch *vfxquad = DRW_cache_fullscreen_quad_get();
		psl->vfx_copy_pass = DRW_pass_create("GPencil VFX Copy b to a Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);
		DRWShadingGroup *vfx_copy_shgrp = DRW_shgroup_create(e_data.gpencil_fullscreen_sh, psl->vfx_copy_pass);
		stl->g_data->tot_sh++;
		DRW_shgroup_call_add(vfx_copy_shgrp, vfxquad, NULL);
		DRW_shgroup_uniform_buffer(vfx_copy_shgrp, "strokeColor", &e_data.vfx_fbcolor_color_tx_b);
		DRW_shgroup_uniform_buffer(vfx_copy_shgrp, "strokeDepth", &e_data.vfx_fbcolor_depth_tx_b);

		/* VFX pass */
		psl->vfx_wave_pass = DRW_pass_create("GPencil VFX Wave Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);

		psl->vfx_blur_pass_1 = DRW_pass_create("GPencil VFX Blur Pass 1", DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);
		psl->vfx_blur_pass_2 = DRW_pass_create("GPencil VFX Blur Pass 2", DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);
		psl->vfx_blur_pass_3 = DRW_pass_create("GPencil VFX Blur Pass 3", DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);
		psl->vfx_blur_pass_4 = DRW_pass_create("GPencil VFX Blur Pass 4", DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);

		psl->vfx_pixel_pass = DRW_pass_create("GPencil VFX Pixel Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);

		psl->vfx_swirl_pass = DRW_pass_create("GPencil VFX Swirl Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);

		psl->vfx_flip_pass = DRW_pass_create("GPencil VFX Flip Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);

		psl->vfx_light_pass = DRW_pass_create("GPencil VFX Light Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);

		/* Painting session pass (used only to speedup while the user is drawing ) */
		struct Gwn_Batch *paintquad = DRW_cache_fullscreen_quad_get();
		psl->painting_pass = DRW_pass_create("GPencil Painting Session Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);
		DRWShadingGroup *painting_shgrp = DRW_shgroup_create(e_data.gpencil_painting_sh, psl->painting_pass);
		stl->g_data->tot_sh++;
		DRW_shgroup_call_add(painting_shgrp, paintquad, NULL);
		DRW_shgroup_uniform_buffer(painting_shgrp, "strokeColor", &e_data.painting_color_tx);
		DRW_shgroup_uniform_buffer(painting_shgrp, "strokeDepth", &e_data.painting_depth_tx);

		/* pass for current stroke drawing in front of all */
		struct Gwn_Batch *frontquad = DRW_cache_fullscreen_quad_get();
		psl->mix_pass_front = DRW_pass_create("GPencil Mix Front Depth Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);
		DRWShadingGroup *mix_front_shgrp = DRW_shgroup_create(e_data.gpencil_front_depth_sh, psl->mix_pass_front);
		stl->g_data->tot_sh++;
		DRW_shgroup_call_add(mix_front_shgrp, frontquad, NULL);
		DRW_shgroup_uniform_buffer(mix_front_shgrp, "strokeColor", &e_data.temp_fbcolor_color_tx);

		/* pass for drawing paper (only if viewport)
		 * In render, the v3d is null
		 */
		if (v3d) {
			struct Gwn_Batch *paperquad = DRW_cache_fullscreen_quad_get();
			psl->paper_pass = DRW_pass_create("GPencil Paper Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND);
			DRWShadingGroup *paper_shgrp = DRW_shgroup_create(e_data.gpencil_paper_sh, psl->paper_pass);
			DRW_shgroup_call_add(paper_shgrp, paperquad, NULL);
			DRW_shgroup_uniform_vec4(paper_shgrp, "color", v3d->gpencil_paper_color, 1);

			UI_GetThemeColor3fv(TH_GRID, stl->storage->gridcolor);
			DRW_shgroup_uniform_vec3(paper_shgrp, "gridcolor", &stl->storage->gridcolor[0], 1);

			stl->storage->gridsize[0] = (float)v3d->gpencil_grid_size[0];
			stl->storage->gridsize[1] = (float)v3d->gpencil_grid_size[1];
			DRW_shgroup_uniform_vec2(paper_shgrp, "size", &stl->storage->gridsize[0], 1);
			if (v3d->flag3 & V3D_GP_SHOW_GRID) {
				stl->storage->uselines = 1;
			}
			else {
				stl->storage->uselines = 0;
			}
			DRW_shgroup_uniform_int(paper_shgrp, "uselines", &stl->storage->uselines, 1);
		}
	}
}

static void GPENCIL_cache_populate(void *vedata, Object *ob)
{
	/* object must be visisible */
	if (!DRW_check_object_visible_within_active_context(ob)) {
		return;
	}

	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	const DRWContextState *draw_ctx = DRW_context_state_get();
	Scene *scene = draw_ctx->scene;
	ToolSettings *ts = scene->toolsettings;
	bool playing = (bool)stl->storage->playing;

	/* object datablock (this is not draw now) */
	if (ob->type == OB_GPENCIL && ob->data) {
		if ((stl->g_data->session_flag & GP_DRW_PAINT_READY) == 0) {
			if (G.debug_value == 665) {
				printf("GPENCIL_cache_populate: %s\n", ob->id.name);
			}

			/* if render set as dirty */
	        if (stl->storage->is_render == true) {
				bGPdata *gpd = (bGPdata *)ob->data;
				gpd->flag |= GP_DATA_CACHE_IS_DIRTY;
			}

			/* allocate memory for saving gp objects */
			stl->g_data->gp_object_cache = gpencil_object_cache_allocate(stl->g_data->gp_object_cache, &stl->g_data->gp_cache_size, &stl->g_data->gp_cache_used);
			
			/* add for drawing later */
			gpencil_object_cache_add(stl->g_data->gp_object_cache, ob, &stl->g_data->gp_cache_used);
			
			/* generate instances as separate cache objects for array modifiers 
			 * with the "Make as Objects" option enabled
			 */
			if (!GP_SIMPLIFY_MODIF(ts, playing)) {
				gpencil_array_modifiers(stl, ob);
			}
		}
		/* draw current painting strokes */
		DRW_gpencil_populate_buffer_strokes(&e_data, vedata, ts, ob);
	}
}

static void GPENCIL_cache_finish(void *vedata)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
	const DRWContextState *draw_ctx = DRW_context_state_get();
	Scene *scene = draw_ctx->scene;
	ToolSettings *ts = scene->toolsettings;
	tGPencilObjectCache *cache;
	bool is_multiedit = false; 
	bool playing = (bool)stl->storage->playing;

	/* if painting session, don't need to do more */
	if (stl->g_data->session_flag & GP_DRW_PAINT_PAINTING) {
		return;
	}

	/* Draw all pending objects */
	if (stl->g_data->gp_cache_used > 0) {
		for (int i = 0; i < stl->g_data->gp_cache_used; i++) {
			Object *ob = stl->g_data->gp_object_cache[i].ob;
			bGPdata *gpd = ob->data;
			
			/* save init shading group */
			stl->g_data->gp_object_cache[i].init_grp = stl->storage->shgroup_id;

			/* fill shading groups */
			is_multiedit = (bool)GPENCIL_MULTIEDIT_SESSIONS_ON(gpd);
			if (!is_multiedit) {
				DRW_gpencil_populate_datablock(&e_data, vedata, scene, ob, ts, gpd);
			}
			else {
				DRW_gpencil_populate_multiedit(&e_data, vedata, scene, ob, ts, gpd);
			}

			/* save end shading group */
			stl->g_data->gp_object_cache[i].end_grp = stl->storage->shgroup_id - 1;
			if (G.debug_value == 665) {
				printf("GPENCIL_cache_finish: %s %d->%d\n", ob->id.name, 
					stl->g_data->gp_object_cache[i].init_grp, stl->g_data->gp_object_cache[i].end_grp);
			}
			/* VFX pass */
			if (!GP_SIMPLIFY_VFX(ts, playing)) {
				cache = &stl->g_data->gp_object_cache[i];
				if ((!is_multiedit) && (ob->modifiers.first)) {
					DRW_gpencil_vfx_modifiers(i, &e_data, vedata, ob, cache);
				}
			}
		}
	}
}

/* helper function to sort inverse gpencil objects using qsort */
static int gpencil_object_cache_compare_zdepth(const void *a1, const void *a2)
{
	const tGPencilObjectCache *ps1 = a1, *ps2 = a2;

	if (ps1->zdepth < ps2->zdepth) return 1;
	else if (ps1->zdepth > ps2->zdepth) return -1;

	return 0;
}

/* helper to draw VFX pass */
static void gpencil_draw_vfx_pass(DRWPass *vfxpass, DRWPass *copypass, 
	GPENCIL_FramebufferList *fbl, DRWShadingGroup *shgrp)
{
	float clearcol[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	DRW_framebuffer_bind(fbl->vfx_color_fb_b);
	DRW_framebuffer_clear(true, true, false, clearcol, 1.0f);
	/* flip pass */
	DRW_draw_pass_subset(vfxpass, shgrp, shgrp);
	/* copy pass from b to a */
	DRW_framebuffer_bind(fbl->vfx_color_fb_a);
	DRW_framebuffer_clear(true, true, false, clearcol, 1.0f);
	DRW_draw_pass(copypass);
}

/* Draw all passes related to VFX modifiers 
 * the passes are created using two framebuffers and use a ping-pong selection
 * to alternate use. By default all vfx modifiers start with tx_a as input
 * and the final output must put the result in tx_a again to be used by next
 * vfx modifier. This use one pass more but allows to create a stack of vfx
 * modifiers and add more modifiers in the future using the same structure.
*/
static void gpencil_vfx_passes(void *vedata, tGPencilObjectCache *cache)
{
	float clearcol[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	GPENCIL_FramebufferList *fbl = ((GPENCIL_Data *)vedata)->fbl;

	DRW_framebuffer_bind(fbl->vfx_color_fb_a);
	DRW_framebuffer_clear(true, true, false, clearcol, 1.0f);

	/* create a wave pass or if this modifier is not used, copy the original texture
	* to tx_a to be used by all following vfx modifiers.
	* At the end of this pass, we can be sure the vfx_fbcolor_color_tx_a texture has 
	* the final image.
	*
	* Wave pass is always evaluated first.
	*/
	DRW_draw_pass_subset(psl->vfx_wave_pass,
		cache->vfx_wave_sh,
		cache->vfx_wave_sh);
	/* --------------
	 * Blur passes (use several passes to get better quality)
	 * --------------*/
	if (cache->vfx_blur_sh_1) {
		DRW_framebuffer_bind(fbl->vfx_color_fb_b);
		DRW_framebuffer_clear(true, true, false, clearcol, 1.0f);
		/* pass 1 */
		DRW_draw_pass_subset(psl->vfx_blur_pass_1,
			cache->vfx_blur_sh_1,
			cache->vfx_blur_sh_1);
		/* pass 2 */
		DRW_framebuffer_bind(fbl->vfx_color_fb_a);
		DRW_draw_pass_subset(psl->vfx_blur_pass_2,
			cache->vfx_blur_sh_2,
			cache->vfx_blur_sh_2);
		/* pass 3 */
		DRW_framebuffer_bind(fbl->vfx_color_fb_b);
		DRW_draw_pass_subset(psl->vfx_blur_pass_3,
			cache->vfx_blur_sh_3,
			cache->vfx_blur_sh_3);
		/* pass 4 */
		DRW_framebuffer_bind(fbl->vfx_color_fb_a);
		DRW_draw_pass_subset(psl->vfx_blur_pass_4,
			cache->vfx_blur_sh_4,
			cache->vfx_blur_sh_4);
	}
	/* --------------
	 * Pixelate pass 
	 * --------------*/
	if (cache->vfx_pixel_sh) {
		gpencil_draw_vfx_pass(psl->vfx_pixel_pass, psl->vfx_copy_pass,
			fbl, cache->vfx_pixel_sh);
	}
	/* --------------
	 * Swirl pass 
	 * --------------*/
	if (cache->vfx_swirl_sh) {
		gpencil_draw_vfx_pass(psl->vfx_swirl_pass, psl->vfx_copy_pass,
			fbl, cache->vfx_swirl_sh);
	}
	/* --------------
	* Flip pass
	* --------------*/
	if (cache->vfx_flip_sh) {
		gpencil_draw_vfx_pass(psl->vfx_flip_pass, psl->vfx_copy_pass,
			fbl, cache->vfx_flip_sh);
	}
	/* --------------
	* Light pass
	* --------------*/
	if (cache->vfx_light_sh) {
		gpencil_draw_vfx_pass(psl->vfx_light_pass, psl->vfx_copy_pass,
			fbl, cache->vfx_light_sh);
	}
}

/* prepare a texture with full viewport for fast drawing */
static void gpencil_prepare_fast_drawing(GPENCIL_StorageList *stl, DefaultFramebufferList *dfbl, GPENCIL_FramebufferList *fbl, DRWPass *pass, float clearcol[4])
{
	if (stl->g_data->session_flag & (GP_DRW_PAINT_IDLE | GP_DRW_PAINT_FILLING)) {
		DRW_framebuffer_bind(fbl->painting_fb);
		/* clean only in first loop cycle */
		if (stl->g_data->session_flag & GP_DRW_PAINT_IDLE) {
			DRW_framebuffer_clear(true, true, false, clearcol, 1.0f);
			stl->g_data->session_flag = GP_DRW_PAINT_FILLING;
		}
		/* repeat pass to fill temp texture */
		DRW_draw_pass(pass);
		/* set default framebuffer again */
		DRW_framebuffer_bind(dfbl->default_fb);
	}
}

static void gpencil_free_obj_list(GPENCIL_StorageList *stl)
{
	/* free memory */
	/* clear temp objects created for display only */
	for (int i = 0; i < stl->g_data->gp_cache_used; i++) {
		Object *ob = stl->g_data->gp_object_cache[i].ob;
		if (ob->id.tag & LIB_TAG_NO_MAIN) {
			MEM_SAFE_FREE(ob);
		}
	}
	MEM_SAFE_FREE(stl->g_data->gp_object_cache);
}

/* draw scene */
static void GPENCIL_draw_scene(void *vedata)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

	GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
	GPENCIL_FramebufferList *fbl = ((GPENCIL_Data *)vedata)->fbl;
	DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();

	int init_grp, end_grp;
	tGPencilObjectCache *cache;
	float clearcol[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	const DRWContextState *draw_ctx = DRW_context_state_get();
	View3D *v3d = draw_ctx->v3d;
	Scene *scene = draw_ctx->scene;
	ToolSettings *ts = scene->toolsettings;
	Object *obact = draw_ctx->obact;
	bool playing = (bool)stl->storage->playing;
	bool is_render = stl->storage->is_render;

	/* paper pass to display a confortable area to draw over complex scenes with geometry */
	if ((!is_render) && (obact) && (obact->type == OB_GPENCIL)) {
		if ((v3d->flag3 & V3D_GP_SHOW_PAPER) && (stl->g_data->gp_cache_used > 0)) {
			DRW_draw_pass(psl->paper_pass);
		}
	}

	/* if we have a painting session, we use fast viewport drawing method */
	if ((!is_render) && (stl->g_data->session_flag & GP_DRW_PAINT_PAINTING)) {
		DRW_framebuffer_bind(dfbl->default_fb);

		MULTISAMPLE_SYNC_ENABLE(dfbl);
		
		DRW_draw_pass(psl->painting_pass);
		
		MULTISAMPLE_SYNC_DISABLE(dfbl);

		/* Current stroke must pass through the temp framebuffer to get same alpha values in blend */
		DRW_framebuffer_texture_attach(fbl->temp_color_fb, e_data.temp_fbcolor_depth_tx, 0, 0);
		DRW_framebuffer_texture_attach(fbl->temp_color_fb, e_data.temp_fbcolor_color_tx, 0, 0);

		DRW_framebuffer_bind(fbl->temp_color_fb);
		DRW_framebuffer_clear(true, true, false, clearcol, 1.0f);
		MULTISAMPLE_GP_SYNC_ENABLE(dfbl, fbl);

		DRW_draw_pass(psl->drawing_pass);

		MULTISAMPLE_GP_SYNC_DISABLE(dfbl, fbl);

		/* send to default framebuffer */
		DRW_framebuffer_bind(dfbl->default_fb);
		DRW_draw_pass(psl->mix_pass_front);

		DRW_framebuffer_texture_detach(e_data.temp_fbcolor_depth_tx);
		DRW_framebuffer_texture_detach(e_data.temp_fbcolor_color_tx);

		/* attach again default framebuffer after detach textures */
		DRW_framebuffer_bind(dfbl->default_fb);

		/* free memory */
		gpencil_free_obj_list(stl);
		return;
	}

	if (DRW_state_is_fbo()) {
		/* attach temp textures */
		DRW_framebuffer_texture_attach(fbl->temp_color_fb, e_data.temp_fbcolor_depth_tx, 0, 0);
		DRW_framebuffer_texture_attach(fbl->temp_color_fb, e_data.temp_fbcolor_color_tx, 0, 0);

		DRW_framebuffer_texture_attach(fbl->vfx_color_fb_a, e_data.vfx_fbcolor_depth_tx_a, 0, 0);
		DRW_framebuffer_texture_attach(fbl->vfx_color_fb_a, e_data.vfx_fbcolor_color_tx_a, 0, 0);

		DRW_framebuffer_texture_attach(fbl->vfx_color_fb_b, e_data.vfx_fbcolor_depth_tx_b, 0, 0);
		DRW_framebuffer_texture_attach(fbl->vfx_color_fb_b, e_data.vfx_fbcolor_color_tx_b, 0, 0);

		DRW_framebuffer_texture_attach(fbl->painting_fb, e_data.painting_depth_tx, 0, 0);
		DRW_framebuffer_texture_attach(fbl->painting_fb, e_data.painting_color_tx, 0, 0);

		/* Draw all pending objects */
		if (stl->g_data->gp_cache_used > 0) {

			/* sort by zdepth */
			qsort(stl->g_data->gp_object_cache, stl->g_data->gp_cache_used,
				sizeof(tGPencilObjectCache), gpencil_object_cache_compare_zdepth);

			for (int i = 0; i < stl->g_data->gp_cache_used; i++) {
				cache = &stl->g_data->gp_object_cache[i];
				Object *ob = cache->ob;
				bGPdata *gpd = ob->data;
				init_grp = cache->init_grp;
				end_grp = cache->end_grp;
				/* Render stroke in separated framebuffer */
				DRW_framebuffer_bind(fbl->temp_color_fb);
				DRW_framebuffer_clear(true, true, false, clearcol, 1.0f);

				/* Stroke Pass: DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND | DRW_STATE_WRITE_DEPTH
				 * draw only a subset that usually start with a fill and end with stroke because the
				 * shading groups are created by pairs */
				if (G.debug_value == 665) {
					printf("GPENCIL_draw_scene: %s %d->%d\n", ob->id.name, init_grp, end_grp);
				}

				if (end_grp >= init_grp) {
					MULTISAMPLE_GP_SYNC_ENABLE(dfbl, fbl);

					DRW_draw_pass_subset(psl->stroke_pass,
						stl->shgroups[init_grp].shgrps_fill != NULL ? stl->shgroups[init_grp].shgrps_fill : stl->shgroups[init_grp].shgrps_stroke,
						stl->shgroups[end_grp].shgrps_stroke);

					MULTISAMPLE_GP_SYNC_DISABLE(dfbl, fbl);
				}
				/* Current buffer drawing */
				if ((!is_render) && (gpd->sbuffer_size > 0)) {
					DRW_draw_pass(psl->drawing_pass);
				}

				/* vfx modifiers passes
				 * if any vfx modifier exist, the init_vfx_wave_sh will be not NULL.
				 */
				if ((cache->vfx_wave_sh) && (!GP_SIMPLIFY_VFX(ts, playing))) {
					/* add vfx passes */
					gpencil_vfx_passes(vedata, cache);

					e_data.input_depth_tx = e_data.vfx_fbcolor_depth_tx_a;
					e_data.input_color_tx = e_data.vfx_fbcolor_color_tx_a;
				}
				else {
					e_data.input_depth_tx = e_data.temp_fbcolor_depth_tx;
					e_data.input_color_tx = e_data.temp_fbcolor_color_tx;
				}
				/* Combine with scene buffer */
				if ((!is_render) || (fbl->main == NULL)) {
					DRW_framebuffer_bind(dfbl->default_fb);
				}
				else {
					DRW_framebuffer_bind(fbl->main);
				}
				DRW_draw_pass(psl->mix_pass);
				/* prepare for fast drawing */
				if (!is_render) {
					gpencil_prepare_fast_drawing(stl, dfbl, fbl, psl->mix_pass_noblend, clearcol);
				}
			}
			/* edit points */
			if ((!is_render) && (!playing)) {
				DRW_draw_pass(psl->edit_pass);
			}
		}
	}
	/* free memory */
	gpencil_free_obj_list(stl);

	/* detach temp textures */
	if (DRW_state_is_fbo()) {
		DRW_framebuffer_texture_detach(e_data.temp_fbcolor_depth_tx);
		DRW_framebuffer_texture_detach(e_data.temp_fbcolor_color_tx);

		DRW_framebuffer_texture_detach(e_data.vfx_fbcolor_depth_tx_a);
		DRW_framebuffer_texture_detach(e_data.vfx_fbcolor_color_tx_a);

		DRW_framebuffer_texture_detach(e_data.vfx_fbcolor_depth_tx_b);
		DRW_framebuffer_texture_detach(e_data.vfx_fbcolor_color_tx_b);

		DRW_framebuffer_texture_detach(e_data.painting_depth_tx);
		DRW_framebuffer_texture_detach(e_data.painting_color_tx);

		/* attach again default framebuffer after detach textures */
		if (!is_render) {
			DRW_framebuffer_bind(dfbl->default_fb);
		}

		/* the temp texture is ready. Now we can use fast screen drawing */
		if (stl->g_data->session_flag & GP_DRW_PAINT_FILLING) {
			stl->g_data->session_flag = GP_DRW_PAINT_READY;
		}
	}
}

/* Get pixel size for render 
 * This function uses the same calculation used for viewport, because if use
 * camera pixelsize, the result is not correct.
 */
static float get_render_pixelsize(float persmat[4][4], int winx, int winy)
{
	float v1[3], v2[3];
	float len_px, len_sc;

	v1[0] = persmat[0][0];
	v1[1] = persmat[1][0];
	v1[2] = persmat[2][0];

	v2[0] = persmat[0][1];
	v2[1] = persmat[1][1];
	v2[2] = persmat[2][1];

	len_px = 2.0f / sqrtf(min_ff(len_squared_v3(v1), len_squared_v3(v2)));
	len_sc = (float)MAX2(winx, winy);

	return len_px / len_sc;
}

/* init render data */
void GPENCIL_render_init(GPENCIL_Data *ved, RenderEngine *engine, struct Depsgraph *depsgraph)
{
	GPENCIL_Data *vedata = (GPENCIL_Data *)ved;
	GPENCIL_StorageList *stl = vedata->stl;
	GPENCIL_FramebufferList *fbl = vedata->fbl;

	Scene *scene = DEG_get_evaluated_scene(depsgraph);
	const float *viewport_size = DRW_viewport_size_get();

	/* In render mode the default framebuffer is not generated
	* because there is no viewport. So we need to manually create one 
	* NOTE : use 32 bit format for precision in render mode. 
	*/
	DRWFboTexture tex_color[2] = {
		{ &e_data.render_depth_tx, DRW_TEX_DEPTH_24_STENCIL_8, DRW_TEX_TEMP },
		{ &e_data.render_color_tx, DRW_TEX_RGBA_32, DRW_TEX_TEMP }
	};
	/* init main framebuffer */
	DRW_framebuffer_init(
		&fbl->main, &draw_engine_gpencil_type,
		(int)viewport_size[0], (int)viewport_size[1],
		tex_color, ARRAY_SIZE(tex_color));

	/* Alloc transient data. */
	if (!stl->g_data) {
		stl->g_data = MEM_callocN(sizeof(*stl->g_data), __func__);
	}

	/* Set the pers & view matrix. */
	struct Object *camera = RE_GetCamera(engine->re);
	float frame = BKE_scene_frame_get(scene);
	RE_GetCameraWindow(engine->re, camera, frame, stl->storage->winmat);
	RE_GetCameraModelMatrix(engine->re, camera, stl->storage->viewinv);

	invert_m4_m4(stl->storage->viewmat, stl->storage->viewinv);
	mul_m4_m4m4(stl->storage->persmat, stl->storage->winmat, stl->storage->viewmat);
	invert_m4_m4(stl->storage->persinv, stl->storage->persmat);
	invert_m4_m4(stl->storage->wininv, stl->storage->winmat);

	DRW_viewport_matrix_override_set(stl->storage->persmat, DRW_MAT_PERS);
	DRW_viewport_matrix_override_set(stl->storage->persinv, DRW_MAT_PERSINV);
	DRW_viewport_matrix_override_set(stl->storage->winmat, DRW_MAT_WIN);
	DRW_viewport_matrix_override_set(stl->storage->wininv, DRW_MAT_WININV);
	DRW_viewport_matrix_override_set(stl->storage->viewmat, DRW_MAT_VIEW);
	DRW_viewport_matrix_override_set(stl->storage->viewinv, DRW_MAT_VIEWINV);

	/* calculate pixel size for render */
	stl->storage->render_pixsize = get_render_pixelsize(stl->storage->persmat, viewport_size[0], viewport_size[1]);
	/* INIT CACHE */
	GPENCIL_cache_init(vedata);
}

/* render all objects and select only grease pencil */
void GPENCIL_render_cache(
	void *vedata, struct Object *ob,
	struct RenderEngine *UNUSED(engine), struct Depsgraph *UNUSED(depsgraph))
{
	if ((ob == NULL) || (DRW_check_object_visible_within_active_context(ob) == false)) {
		return;
	}

	if (ob->type == OB_GPENCIL) {
		GPENCIL_cache_populate(vedata, ob);
	}
}

/* TODO: Reuse Eevee code in shared module instead to duplicate here */
static void GPENCIL_update_viewvecs(float invproj[4][4], float winmat[4][4], float(*r_viewvecs)[4])
{
	/* view vectors for the corners of the view frustum.
	* Can be used to recreate the world space position easily */
	float view_vecs[4][4] = {
		{ -1.0f, -1.0f, -1.0f, 1.0f },
	{ 1.0f, -1.0f, -1.0f, 1.0f },
	{ -1.0f,  1.0f, -1.0f, 1.0f },
	{ -1.0f, -1.0f,  1.0f, 1.0f }
	};

	/* convert the view vectors to view space */
	const bool is_persp = (winmat[3][3] == 0.0f);
	for (int i = 0; i < 4; i++) {
		mul_project_m4_v3(invproj, view_vecs[i]);
		/* normalized trick see:
		* http://www.derschmale.com/2014/01/26/reconstructing-positions-from-the-depth-buffer */
		if (is_persp) {
			/* Divide XY by Z. */
			mul_v2_fl(view_vecs[i], 1.0f / view_vecs[i][2]);
		}
	}

	/**
	* If ortho : view_vecs[0] is the near-bottom-left corner of the frustum and
	*            view_vecs[1] is the vector going from the near-bottom-left corner to
	*            the far-top-right corner.
	* If Persp : view_vecs[0].xy and view_vecs[1].xy are respectively the bottom-left corner
	*            when Z = 1, and top-left corner if Z = 1.
	*            view_vecs[0].z the near clip distance and view_vecs[1].z is the (signed)
	*            distance from the near plane to the far clip plane.
	**/
	copy_v4_v4(r_viewvecs[0], view_vecs[0]);

	/* we need to store the differences */
	r_viewvecs[1][0] = view_vecs[1][0] - view_vecs[0][0];
	r_viewvecs[1][1] = view_vecs[2][1] - view_vecs[0][1];
	r_viewvecs[1][2] = view_vecs[3][2] - view_vecs[0][2];
}

/* Update view_vecs */
static void GPENCIL_update_vecs(GPENCIL_Data *vedata)
{
	GPENCIL_StorageList *stl = vedata->stl;

	float invproj[4][4], winmat[4][4];
	DRW_viewport_matrix_get(winmat, DRW_MAT_WIN);
	DRW_viewport_matrix_get(invproj, DRW_MAT_WININV);

	/* this is separated to keep function equal to Eevee for future reuse of same code */
	GPENCIL_update_viewvecs(invproj, winmat, stl->storage->view_vecs);
}

/* read z-depth render result */
static void GPENCIL_render_result_z(RenderResult *rr, const char *viewname,	GPENCIL_Data *vedata)
{
	const DRWContextState *draw_ctx = DRW_context_state_get();
	ViewLayer *view_layer = draw_ctx->view_layer;
	GPENCIL_StorageList *stl = vedata->stl;

	if ((view_layer->passflag & SCE_PASS_Z) != 0) {
		RenderLayer *rl = rr->layers.first;
		RenderPass *rp = RE_pass_find_by_name(rl, RE_PASSNAME_Z, viewname);

		DRW_framebuffer_read_depth(rr->xof, rr->yof, rr->rectx, rr->recty, rp->rect);

		bool is_persp = DRW_viewport_is_persp_get();

		GPENCIL_update_vecs(vedata);

		/* Convert ogl depth [0..1] to view Z [near..far] */
		for (int i = 0; i < rr->rectx * rr->recty; ++i) {
			if (rp->rect[i] == 1.0f) {
				rp->rect[i] = 1e10f; /* Background */
			}
			else {
				if (is_persp) {
					rp->rect[i] = rp->rect[i] * 2.0f - 1.0f;
					rp->rect[i] = stl->storage->winmat[3][2] / (rp->rect[i] + stl->storage->winmat[2][2]);
				}
				else {
					rp->rect[i] = -stl->storage->view_vecs[0][2] + rp->rect[i] * -stl->storage->view_vecs[1][2];
				}
			}
		}
	}
}

/* read combined render result */
static void GPENCIL_render_result_combined(RenderResult *rr, const char *viewname, GPENCIL_Data *vedata)
{
	RenderLayer *rl = rr->layers.first;
	RenderPass *rp = RE_pass_find_by_name(rl, RE_PASSNAME_COMBINED, viewname);
	GPENCIL_FramebufferList *fbl = ((GPENCIL_Data *)vedata)->fbl;

	DRW_framebuffer_bind(fbl->main);
	DRW_framebuffer_read_data(rr->xof, rr->yof, rr->rectx, rr->recty, 4, 0, rp->rect);
}

/* render grease pencil to image */
static void GPENCIL_render_to_image(void *vedata, struct RenderEngine *engine, struct Depsgraph *depsgraph)
{
	const char *viewname = RE_GetActiveRenderView(engine->re);
	const float *render_size = DRW_viewport_size_get();
	RenderResult *rr = RE_engine_begin_result(engine, 0, 0, (int)render_size[0], (int)render_size[1], NULL, viewname);

	GPENCIL_engine_init(vedata);
	GPENCIL_render_init(vedata, engine, depsgraph);

	GPENCIL_FramebufferList *fbl = ((GPENCIL_Data *)vedata)->fbl;
	if (fbl->main) {
		DRW_framebuffer_texture_attach(fbl->main, e_data.render_depth_tx, 0, 0);
		DRW_framebuffer_texture_attach(fbl->main, e_data.render_color_tx, 0, 0);
		/* clean first time the buffer */
		float clearcol[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		DRW_framebuffer_bind(fbl->main);
		DRW_framebuffer_clear(true, true, false, clearcol, 1.0f);
	}

	/* loop all objects and draw */
	DRW_render_object_iter(vedata, engine, depsgraph, GPENCIL_render_cache);

	GPENCIL_cache_finish(vedata);
	GPENCIL_draw_scene(vedata);
	
	/* combined data */
	GPENCIL_render_result_combined(rr, viewname, vedata);
	/* z-depth data */
	GPENCIL_render_result_z(rr, viewname, vedata);
	
	/* detach textures */
	if (fbl->main) {
		DRW_framebuffer_texture_detach(e_data.render_depth_tx);
		DRW_framebuffer_texture_detach(e_data.render_color_tx);
	}

	RE_engine_end_result(engine, rr, false, false, false);
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
	&GPENCIL_draw_scene,
	NULL,
	NULL,
	&GPENCIL_render_to_image,
};
