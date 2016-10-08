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

/** \file VP_engine_API.h
 *  \ingroup viewport
 */

#ifndef __VP_ENGINE_H__
#define __VP_ENGINE_H__

#include "BLI_compiler_attrs.h"

#include "DNA_defs.h"
#include "DNA_listBase.h"

struct bContext;
struct ViewportEngine;

extern ListBase ViewportEngineTypes;

typedef struct ViewportDrawPlate {
	struct ViewportDrawPlate *next, *prev;

	const char *idname; /* We may not need this, but useful for debugging */

	/* Do the actual drawing */
	void (*draw)(const struct ViewportEngine *, const struct bContext *);
} ViewportDrawPlate;

/**
 * Each viewport-type can have a number of draw modes which are mostly a container for a list of draw plates.
 */
typedef struct ViewportDrawMode {
	struct ViewportDrawMode *next, *prev;

	ListBase drawplates;
} ViewportDrawMode;

typedef struct ViewportEngineType {
	struct ViewportEngineType *next, *prev;

	char idname[MAX_NAME]; /* Unused (TODO do we need this?) */
	/* Displayed in UI */
	char name[MAX_NAME];

	/* Initialize engine, set defaults, especially default draw modes. */
	void (*init)(struct ViewportEngineType *);
	/* Set up data and view, executed before actual render callback */
	void (*setup_render)(struct ViewportEngine *, const struct bContext *);
	/* Can be used instead of using drawmodes & plates. Used for legacy viewport
	 * right now, could likely be removed after that's removed too (TODO). */
	void (*render)(const struct ViewportEngine *, const struct bContext *);

	/* First item is active one */
	ListBase drawmodes; /* ViewportDrawMode */
} ViewportEngineType;

/* Engine Types */
void VP_enginetypes_init(void);
void VP_enginetypes_exit(void);

/* Engines */
struct ViewportEngine *VP_engine_create(ViewportEngineType *engine_type) ATTR_NONNULL();
void VP_engine_free(struct ViewportEngine *engine) ATTR_NONNULL();

void VP_engine_render(struct ViewportEngine *engine, const struct bContext *C);

#endif /* __VP_ENGINE_H__ */
