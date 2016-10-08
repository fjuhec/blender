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

#include "BLI_listbase.h"

#include "BLT_translation.h"

#include "DNA_view3d_types.h"

#include "MEM_guardedalloc.h"

#include "VP_engine_API.h"


/* -------------------------------------------------------------------- */
/** \name ViewportEngineType
 *
 * \{ */

static ViewportEngineType internal_viewport = {
	NULL, NULL,
	"BLENDER_VIEWPORT", N_("Modern Viewport"), /* TODO temp name */
};

/* TODO Keeping old viewport only during transition */
static ViewportEngineType old_viewport = {
	NULL, NULL,
	"OLD_VIEWPORT", N_("Old Viewport"),
};

ListBase ViewportEngineTypes = {NULL};

void VP_enginetypes_init(void)
{
	BLI_addtail(&ViewportEngineTypes, &internal_viewport);
	BLI_addtail(&ViewportEngineTypes, &old_viewport);
}

void VP_enginetypes_exit(void)
{
	for (ViewportEngineType *engine_type = ViewportEngineTypes.first, *engine_type_next;
	     engine_type;
	     engine_type = engine_type_next)
	{
		engine_type_next = engine_type->next;

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
	MEM_freeN(engine);
}

/** \} */ /* ViewportEngine */
