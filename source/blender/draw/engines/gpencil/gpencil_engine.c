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

#include "BKE_main.h"
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

/* *********** STATIC *********** */
static GPENCIL_e_data e_data = {NULL}; /* Engine data */

/* *********** FUNCTIONS *********** */

static void GPENCIL_engine_init(void *vedata)
{
	GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

	const DRWContextState *draw_ctx = DRW_context_state_get();
	e_data.bmain = CTX_data_main(draw_ctx->evil_C);

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

	unit_m4(stl->storage->unit_matrix);
}

static void GPENCIL_engine_free(void)
{
	/* only free custom shaders, builtin shaders are freed in blender close */
	DRW_SHADER_FREE_SAFE(e_data.gpencil_fill_sh);
	DRW_SHADER_FREE_SAFE(e_data.gpencil_stroke_sh);

	// TODO: When load new file, the previous GPD is not cleared
	/* free all cache data */
	//for (bGPdata *gpd = e_data.bmain->gpencil.first; gpd; gpd = gpd->id.next) {
	//	if (gpd->batch_cache) {
	//		gpencil_batch_cache_clear(gpd);
	//		MEM_SAFE_FREE(gpd->batch_cache);
	//	}
	//}
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
		stl->g_data->shgrps_point_volumetric = gpencil_shgroup_point_volumetric_create(psl->stroke_pass, e_data.gpencil_volumetric_sh);

		/* edit pass */
		psl->edit_pass = DRW_pass_create("Gpencil Edit Pass", state);
		stl->g_data->shgrps_edit_volumetric = gpencil_shgroup_edit_volumetric_create(psl->edit_pass, e_data.gpencil_volumetric_sh);

		/* drawing buffer pass */
		psl->drawing_pass = DRW_pass_create("Gpencil Drawing Pass", state);
		stl->g_data->shgrps_drawing_stroke = gpencil_shgroup_drawing_stroke_create(psl->drawing_pass, e_data.gpencil_stroke_sh);
		stl->g_data->shgrps_drawing_fill = gpencil_shgroup_drawing_fill_create(psl->drawing_pass, e_data.gpencil_drawing_fill_sh);
	}
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
			gpencil_populate_datablock(&e_data, vedata, scene, NULL, ts, scene->gpd);
		}
	}

	/* object datablock */
	if (ob->type == OB_GPENCIL && ob->gpd) {
		gpencil_populate_datablock(&e_data, vedata, scene, ob, ts, ob->gpd);
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
