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
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/viewport/intern/engine_api.c
 *  \ingroup viewport
 *
 * \brief Viewport Render Engine API
 * API for managing viewport engines, internal or external.
 */

#include <stddef.h>

#include "BKE_context.h"

#include "BLI_listbase.h"

#include "BLT_translation.h"

#include "DNA_view3d_types.h"

#include "MEM_guardedalloc.h"

#include "viewport_intern.h"
#include "VP_engine_API.h"


/* -------------------------------------------------------------------- */
/** \name ViewportEngineType
 *
 * \{ */

extern void view3d_main_region_draw_legacy(const ViewportEngine *engine, const bContext *C);

/* TODO Keeping old viewport only during transition */
static ViewportEngineType old_viewport = {
	NULL, NULL,
	"LEGACY_VIEWPORT", N_("Legacy Viewport"),
	NULL,
	NULL,
	view3d_main_region_draw_legacy,
};

ListBase ViewportEngineTypes = {NULL};

static void enginetype_init(ViewportEngineType *engine_type)
{
	if (engine_type->init) {
		engine_type->init(engine_type);
	}
	BLI_addtail(&ViewportEngineTypes, engine_type);
}

void VP_enginetypes_init(void)
{
	enginetype_init(&vp_blender_viewport);
	enginetype_init(&old_viewport);
}

static void drawmode_plates_free(ViewportDrawMode *drawmode)
{
	for (ViewportDrawPlate *drawplate = drawmode->drawplates.first, *drawplate_next;
	     drawplate;
	     drawplate = drawplate_next)
	{
		drawplate_next = drawplate->next;

		BLI_remlink(&drawmode->drawplates, drawplate);
//		MEM_freeN(drawplate); /* TODO currently only static drawplates */
	}
	BLI_assert(BLI_listbase_is_empty(&drawmode->drawplates));
}

static void enginetype_drawmodes_free(ViewportEngineType *engine_type)
{
	for (ViewportDrawMode *drawmode = engine_type->drawmodes.first, *drawmode_next;
	     drawmode;
	     drawmode = drawmode_next)
	{
		drawmode_next = drawmode->next;

		drawmode_plates_free(drawmode);
		BLI_remlink(&engine_type->drawmodes, drawmode);
		MEM_freeN(drawmode);
	}
	BLI_assert(BLI_listbase_is_empty(&engine_type->drawmodes));
}

void VP_enginetypes_exit(void)
{
	for (ViewportEngineType *engine_type = ViewportEngineTypes.first, *engine_type_next;
	     engine_type;
	     engine_type = engine_type_next)
	{
		engine_type_next = engine_type->next;

		enginetype_drawmodes_free(engine_type);
		BLI_remlink(&ViewportEngineTypes, engine_type);
//		MEM_freeN(engine_type); /* only free alloc'ed engine-types */
	}
}

/** \} */ /* ViewportEngineType */


/* -------------------------------------------------------------------- */
/** \name ViewportEngine
 *
 * \{ */

ViewportEngine *VP_engine_create(ViewportEngineType *engine_type)
{
	ViewportEngine *engine = MEM_callocN(sizeof(*engine), __func__);
	engine->type = engine_type;

	return engine;
}

void VP_engine_free(ViewportEngine *engine)
{
	if (engine->render_data) {
		/* XXX Think this doesn't need to be a pointer, at least none that needs
		 * freeing. Need to find out how we'll store and use render data. */
		MEM_freeN(engine->render_data);
	}
	MEM_freeN(engine);
}

static ViewportDrawMode *viewport_active_drawmode_get(ViewportEngineType *engine_type)
{
	return engine_type->drawmodes.first;
}

static void viewport_drawmode_draw(const ViewportEngine *engine, ViewportDrawMode *drawmode, const bContext *C)
{
	BLI_assert(BLI_findindex(&engine->type->drawmodes, drawmode) != -1);
	for (ViewportDrawPlate *drawplate = drawmode->drawplates.first; drawplate; drawplate = drawplate->next) {
		drawplate->draw(engine, C);
	}
}

/**
 * This could run once per view, or even in parallel
 * for each of them. What is a "view"?
 * - a viewport with the camera elsewhere
 * - left/right stereo
 * - panorama / fisheye individual cubemap faces
 */
void VP_engine_render(ViewportEngine *engine, const bContext *C)
{
	if (engine->type->setup_render) {
		engine->type->setup_render(engine, C);
	}

	if (engine->type->render) {
		engine->type->render(engine, C);
	}
	else {
		ViewportDrawMode *active_drawmode = viewport_active_drawmode_get(engine->type);
		viewport_drawmode_draw(engine, active_drawmode, C);
	}
}

/** \} */ /* ViewportEngine */
