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
 * The Original Code is Copyright (C) 2016 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/manipulators/WM_manipulator_types.h
 *  \ingroup wm
 *
 * \name Manipulator Types
 * \brief Manipulator defines for external use.
 *
 * Only included in WM_types.h and lower level files.
 */


#ifndef __WM_MANIPULATOR_TYPES_H__
#define __WM_MANIPULATOR_TYPES_H__

#include "BLI_compiler_attrs.h"

struct wmManipulatorGroupType;
struct wmManipulatorGroup;
struct wmKeyConfig;

typedef bool (*wmManipulatorGroupPollFunc)(const struct bContext *, struct wmManipulatorGroupType *) ATTR_WARN_UNUSED_RESULT;
typedef void (*wmManipulatorGroupInitFunc)(const struct bContext *, struct wmManipulatorGroup *);
typedef void (*wmManipulatorGroupRefreshFunc)(const struct bContext *, struct wmManipulatorGroup *);
typedef void (*wmManipulatorGroupDrawPrepareFunc)(const struct bContext *, struct wmManipulatorGroup *);


/* -------------------------------------------------------------------- */

/* factory class for a manipulator-group type, gets called every time a new area is spawned */
typedef struct wmManipulatorGroupType {
	struct wmManipulatorGroupType *next, *prev;

	char idname[64];  /* MAX_NAME */
	const char *name; /* manipulator-group name - displayed in UI (keymap editor) */

	/* poll if manipulator-map should be visible */
	wmManipulatorGroupPollFunc poll;
	/* initially create manipulators and set permanent data - stuff you only need to do once */
	wmManipulatorGroupInitFunc init;
	/* refresh data, only called if recreate flag is set (WM_manipulatormap_tag_refresh) */
	wmManipulatorGroupRefreshFunc refresh;
	/* refresh data for drawing, called before each redraw */
	wmManipulatorGroupDrawPrepareFunc draw_prepare;

	/* keymap init callback for this manipulator-group */
	struct wmKeyMap *(*keymap_init)(const struct wmManipulatorGroupType *, struct wmKeyConfig *);
	/* keymap created with callback from above */
	struct wmKeyMap *keymap;

	/* rna for properties */
	struct StructRNA *srna;

	/* RNA integration */
	ExtensionRNA ext;

	/* manipulatorTypeflags (includes copy of wmManipulatorMapType.flag - used for comparisons) */
	int flag;

	/* if type is spawned from operator this is set here */
	void *op;

	/* same as manipulator-maps, so registering/unregistering goes to the correct region */
	short spaceid, regionid;
	char mapidname[64];
} wmManipulatorGroupType;

struct wmManipulatorMapType_Params {
	const char *idname;
	const int spaceid;
	const int regionid;
	const int flag;
};

/**
 * Simple utility wrapper for storing a single manipulator as wmManipulatorGroup.customdata (which gets freed).
 */
typedef struct wmManipulatorWrapper {
	struct wmManipulator *manipulator;
} wmManipulatorWrapper;


/* -------------------------------------------------------------------- */

/* wmManipulator->flag */
enum eManipulatorFlag {
	/* states */
	WM_MANIPULATOR_HIGHLIGHT   = (1 << 0),
	WM_MANIPULATOR_ACTIVE      = (1 << 1),
	WM_MANIPULATOR_SELECTED    = (1 << 2),
	/* settings */
	WM_MANIPULATOR_DRAW_HOVER  = (1 << 3),
	WM_MANIPULATOR_DRAW_ACTIVE = (1 << 4), /* draw while dragging */
	WM_MANIPULATOR_DRAW_VALUE  = (1 << 5), /* draw a indicator for the current value while dragging */
	WM_MANIPULATOR_SCALE_3D    = (1 << 6),
	WM_MANIPULATOR_SCENE_DEPTH = (1 << 7), /* manipulator is depth culled with scene objects*/
	WM_MANIPULATOR_HIDDEN      = (1 << 8),
	WM_MANIPULATOR_SELECTABLE  = (1 << 9),
};

/* wmManipulatorMapType->flag */
enum eManipulatorMapTypeFlag {
	/**
	 * Check if manipulator-map does 3D drawing
	 * (uses a different kind of interaction),
	 * - 3d: use glSelect buffer.
	 * - 2d: use simple cursor position intersection test. */
	WM_MANIPULATORMAPTYPE_3D           = (1 << 0),
};

#endif  /* __WM_MANIPULATOR_TYPES_H__ */

