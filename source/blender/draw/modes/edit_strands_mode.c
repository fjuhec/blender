/*
 * Copyright 2016, Blender Foundation.
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
 * Contributor(s): Blender Institute
 *
 */

/** \file blender/draw/modes/particle_mode.c
 *  \ingroup draw
 */

#include "DRW_engine.h"
#include "DRW_render.h"

#include "BLI_ghash.h"

#include "DNA_view3d_types.h"

#include "BKE_editstrands.h"

#include "GPU_shader.h"
#include "GPU_texture.h"

#include "draw_common.h"

#include "draw_mode_engines.h"

extern GlobalsUboStorage ts;

extern char datatoc_edit_strands_vert_glsl[];
extern char datatoc_hair_vert_glsl[];
extern char datatoc_hair_frag_glsl[];
extern char datatoc_gpu_shader_point_varying_color_frag_glsl[];
extern char datatoc_gpu_shader_3D_smooth_color_frag_glsl[];

/* *********** LISTS *********** */
/* All lists are per viewport specific datas.
 * They are all free when viewport changes engines
 * or is free itself. Use EDIT_STRANDS_engine_init() to
 * initialize most of them and EDIT_STRANDS_cache_init()
 * for EDIT_STRANDS_PassList */

typedef struct EDIT_STRANDS_PassList {
	/* Declare all passes here and init them in
	 * EDIT_STRANDS_cache_init().
	 * Only contains (DRWPass *) */
	struct DRWPass *wires;
	struct DRWPass *tips;
	struct DRWPass *roots;
	struct DRWPass *points;
} EDIT_STRANDS_PassList;

typedef struct EDIT_STRANDS_StorageList {
	/* Contains any other memory block that the engine needs.
	 * Only directly MEM_(m/c)allocN'ed blocks because they are
	 * free with MEM_freeN() when viewport is freed.
	 * (not per object) */
	struct CustomStruct *block;
	struct EDIT_STRANDS_PrivateData *g_data;
} EDIT_STRANDS_StorageList;

typedef struct EDIT_STRANDS_Data {
	/* Struct returned by DRW_viewport_engine_data_get.
	 * If you don't use one of these, just make it a (void *) */
	// void *fbl;
	void *engine_type; /* Required */
	DRWViewportEmptyList *fbl;
	DRWViewportEmptyList *txl;
	EDIT_STRANDS_PassList *psl;
	EDIT_STRANDS_StorageList *stl;
} EDIT_STRANDS_Data;

/* *********** STATIC *********** */

static struct {
	/* Custom shaders :
	 * Add sources to source/blender/draw/modes/shaders
	 * init in EDIT_STRANDS_engine_init();
	 * free in EDIT_STRANDS_engine_free(); */
	struct GPUShader *edit_point_shader;
	struct GPUShader *edit_wire_shader;
} e_data = {NULL}; /* Engine data */

typedef struct EDIT_STRANDS_PrivateData {
	/* resulting curve as 'wire' for fast editmode drawing */
	DRWShadingGroup *wires_shgrp;
	DRWShadingGroup *tips_shgrp;
	DRWShadingGroup *roots_shgrp;
	DRWShadingGroup *points_shgrp;
} EDIT_STRANDS_PrivateData; /* Transient data */

/* *********** FUNCTIONS *********** */

/* Init Textures, Framebuffers, Storage and Shaders.
 * It is called for every frames.
 * (Optional) */
static void EDIT_STRANDS_engine_init(void *vedata)
{
	EDIT_STRANDS_StorageList *stl = ((EDIT_STRANDS_Data *)vedata)->stl;

	UNUSED_VARS(stl);

	/* Init Framebuffers like this: order is attachment order (for color texs) */
	/*
	 * DRWFboTexture tex[2] = {{&txl->depth, DRW_TEX_DEPTH_24, 0},
	 *                         {&txl->color, DRW_TEX_RGBA_8, DRW_TEX_FILTER}};
	 */

	/* DRW_framebuffer_init takes care of checking if
	 * the framebuffer is valid and has the right size*/
	/*
	 * float *viewport_size = DRW_viewport_size_get();
	 * DRW_framebuffer_init(&fbl->occlude_wire_fb,
	 *                     (int)viewport_size[0], (int)viewport_size[1],
	 *                     tex, 2);
	 */

	if (!e_data.edit_point_shader) {
		e_data.edit_point_shader = DRW_shader_create(
		                               datatoc_edit_strands_vert_glsl,
		                               NULL,
		                               datatoc_gpu_shader_point_varying_color_frag_glsl,
		                               NULL);
	}
	
	if (!e_data.edit_wire_shader) {
		e_data.edit_wire_shader = DRW_shader_create(
		                              datatoc_edit_strands_vert_glsl,
		                              NULL,
		                              datatoc_gpu_shader_3D_smooth_color_frag_glsl,
		                              NULL);
	}
}

/* Cleanup when destroying the engine.
 * This is not per viewport ! only when quitting blender.
 * Mostly used for freeing shaders */
static void EDIT_STRANDS_engine_free(void)
{
	DRW_SHADER_FREE_SAFE(e_data.edit_point_shader);
	DRW_SHADER_FREE_SAFE(e_data.edit_wire_shader);
}

/* Here init all passes and shading groups
 * Assume that all Passes are NULL */
static void EDIT_STRANDS_cache_init(void *vedata)
{
	EDIT_STRANDS_PassList *psl = ((EDIT_STRANDS_Data *)vedata)->psl;
	EDIT_STRANDS_StorageList *stl = ((EDIT_STRANDS_Data *)vedata)->stl;
	
	if (!stl->g_data) {
		/* Alloc transient pointers */
		stl->g_data = MEM_mallocN(sizeof(*stl->g_data), __func__);
	}
	
	{
		/* Strand wires */
		DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS | DRW_STATE_BLEND;
		psl->wires = DRW_pass_create("Strand Wire Verts Pass", state);
		
		stl->g_data->wires_shgrp = DRW_shgroup_create(e_data.edit_wire_shader, psl->wires);
		DRW_shgroup_uniform_vec4(stl->g_data->wires_shgrp, "color", ts.colorWireEdit, 1);
		DRW_shgroup_uniform_vec4(stl->g_data->wires_shgrp, "colorSelect", ts.colorEdgeSelect, 1);
	}
	
	{
		/* Tip vertices */
		DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS | DRW_STATE_BLEND;
		psl->tips = DRW_pass_create("Strand Tip Verts Pass", state);
		
		stl->g_data->tips_shgrp = DRW_shgroup_create(e_data.edit_point_shader, psl->tips);
		DRW_shgroup_uniform_vec4(stl->g_data->tips_shgrp, "color", ts.colorVertex, 1);
		DRW_shgroup_uniform_vec4(stl->g_data->tips_shgrp, "colorSelect", ts.colorVertexSelect, 1);
		DRW_shgroup_uniform_float(stl->g_data->tips_shgrp, "sizeVertex", &ts.sizeVertex, 1);
	}
	
	{
		/* Root vertices */
		DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS | DRW_STATE_BLEND;
		psl->roots = DRW_pass_create("Strand Root Verts Pass", state);
		
		stl->g_data->roots_shgrp = DRW_shgroup_create(e_data.edit_point_shader, psl->roots);
		DRW_shgroup_uniform_vec4(stl->g_data->roots_shgrp, "color", ts.colorVertex, 1);
		DRW_shgroup_uniform_vec4(stl->g_data->roots_shgrp, "colorSelect", ts.colorVertexSelect, 1);
		DRW_shgroup_uniform_float(stl->g_data->roots_shgrp, "sizeVertex", &ts.sizeVertex, 1);
	}
	
	{
		/* Interior vertices */
		DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS | DRW_STATE_BLEND;
		psl->points = DRW_pass_create("Strand Interior Verts Pass", state);
		
		stl->g_data->points_shgrp = DRW_shgroup_create(e_data.edit_point_shader, psl->points);
		DRW_shgroup_uniform_vec4(stl->g_data->points_shgrp, "color", ts.colorVertex, 1);
		DRW_shgroup_uniform_vec4(stl->g_data->points_shgrp, "colorSelect", ts.colorVertexSelect, 1);
		DRW_shgroup_uniform_float(stl->g_data->points_shgrp, "sizeVertex", &ts.sizeVertex, 1);
	}
}

static void edit_strands_add_ob_to_pass(
        Scene *scene, Object *ob, BMEditStrands *edit,
        DRWShadingGroup *tips_shgrp,
        DRWShadingGroup *roots_shgrp,
        DRWShadingGroup *points_shgrp,
        DRWShadingGroup *wires_shgrp)
{
	HairEditSettings *tsettings = &scene->toolsettings->hair_edit;
	
	{
		struct Gwn_Batch *geom = DRW_cache_editstrands_get_wires(edit);
		DRW_shgroup_call_add(wires_shgrp, geom, ob->obmat);
	}

	switch (tsettings->select_mode) {
		case HAIR_SELECT_TIP: {
			struct Gwn_Batch *geom = DRW_cache_editstrands_get_tips(edit);
			DRW_shgroup_call_add(tips_shgrp, geom, ob->obmat);
			break;
		}
		
		case HAIR_SELECT_STRAND: {
#if 0
			struct Gwn_Batch *geom = DRW_cache_editstrands_get_roots(edit);
			DRW_shgroup_call_add(roots_shgrp, geom, ob->obmat);
#else
			UNUSED_VARS(roots_shgrp);
#endif
			break;
		}
		
		case HAIR_SELECT_VERTEX: {
			struct Gwn_Batch *geom = DRW_cache_editstrands_get_points(edit);
			DRW_shgroup_call_add(points_shgrp, geom, ob->obmat);
			break;
		}
	}
}

/* Add geometry to shadingGroups. Execute for each objects */
static void EDIT_STRANDS_cache_populate(void *vedata, Object *ob)
{
	EDIT_STRANDS_StorageList *stl = ((EDIT_STRANDS_Data *)vedata)->stl;

	BMEditStrands *edit = BKE_editstrands_from_object(ob);
	const DRWContextState *draw_ctx = DRW_context_state_get();
	Scene *scene = draw_ctx->scene;

	// Don't draw strands while editing the object itself
	if (ob == scene->obedit)
		return;

	if (edit) {
		edit_strands_add_ob_to_pass(scene, ob, edit,
		            stl->g_data->tips_shgrp, stl->g_data->roots_shgrp,
		            stl->g_data->points_shgrp, stl->g_data->wires_shgrp);
	}
}

/* Optional: Post-cache_populate callback */
static void EDIT_STRANDS_cache_finish(void *vedata)
{
	EDIT_STRANDS_PassList *psl = ((EDIT_STRANDS_Data *)vedata)->psl;
	EDIT_STRANDS_StorageList *stl = ((EDIT_STRANDS_Data *)vedata)->stl;

	/* Do something here! dependant on the objects gathered */
	UNUSED_VARS(psl, stl);
}

/* Draw time ! Control rendering pipeline from here */
static void EDIT_STRANDS_draw_scene(void *vedata)
{
	EDIT_STRANDS_PassList *psl = ((EDIT_STRANDS_Data *)vedata)->psl;

	DRW_draw_pass(psl->wires);
	DRW_draw_pass(psl->points);
	DRW_draw_pass(psl->roots);
	DRW_draw_pass(psl->tips);
	
	/* If you changed framebuffer, double check you rebind
	 * the default one with its textures attached before finishing */
}

static const DrawEngineDataSize STRANDS_data_size = DRW_VIEWPORT_DATA_SIZE(EDIT_STRANDS_Data);

DrawEngineType draw_engine_edit_strands_type = {
	NULL, NULL,
	N_("EditStrandsMode"),
	&STRANDS_data_size,
	EDIT_STRANDS_engine_init,
	EDIT_STRANDS_engine_free,
	&EDIT_STRANDS_cache_init,
	&EDIT_STRANDS_cache_populate,
	&EDIT_STRANDS_cache_finish,
	NULL, /* draw_background but not needed by mode engines */
	&EDIT_STRANDS_draw_scene
};
