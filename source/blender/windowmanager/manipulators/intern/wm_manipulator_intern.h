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
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/manipulators/intern/wm_manipulator_intern.h
 *  \ingroup wm
 */


#ifndef __WM_MANIPULATOR_INTERN_H__
#define __WM_MANIPULATOR_INTERN_H__

struct wmKeyConfig;
struct wmManipulator;
struct wmManipulatorMap;

/* -------------------------------------------------------------------- */
/* wmManipulator */

/**
 * \brief Manipulator tweak flag.
 * Bitflag passed to manipulator while tweaking.
 */
enum {
	/* drag with extra precision (shift)
	 * NOTE: Manipulators are responsible for handling this (manipulator->handler callback)! */
	WM_MANIPULATOR_TWEAK_PRECISE = (1 << 0),
};

bool WM_manipulator_register(struct wmManipulatorGroup *mgroup, struct wmManipulator *manipulator, const char *name);

bool WM_manipulator_deselect(struct wmManipulatorMap *mmap, struct wmManipulator *manipulator);
bool WM_manipulator_select(bContext *C, struct wmManipulatorMap *mmap, struct wmManipulator *manipulator);

void WM_manipulator_calculate_scale(struct wmManipulator *manipulator, const bContext *C);
void WM_manipulator_update_prop_data(struct wmManipulator *manipulator);

void fix_linking_manipulator_arrow(void);
void fix_linking_manipulator_arrow2d(void);
void fix_linking_manipulator_cage(void);
void fix_linking_manipulator_dial(void);
void fix_linking_manipulator_facemap(void);
void fix_linking_manipulator_primitive(void);


/* -------------------------------------------------------------------- */
/* wmManipulatorGroup */

enum {
	TWEAK_MODAL_CANCEL = 1,
	TWEAK_MODAL_CONFIRM,
	TWEAK_MODAL_PRECISION_ON,
	TWEAK_MODAL_PRECISION_OFF,
};

struct wmManipulatorGroup *wm_manipulatorgroup_new_from_type(struct wmManipulatorGroupType *mgrouptype);
void WM_manipulatorgroup_free(bContext *C, wmManipulatorMap *mmap, struct wmManipulatorGroup *mgroup);

void WM_manipulatorgrouptype_keymap_init(struct wmManipulatorGroupType *mgrouptype, struct wmKeyConfig *keyconf);


/* -------------------------------------------------------------------- */
/* wmManipulatorMap */

/**
 * This is a container for all manipulator types that can be instantiated in a region.
 * (similar to dropboxes).
 *
 * \note There is only ever one of these for every (area, region) combination.
 */
typedef struct wmManipulatorMapType {
	struct wmManipulatorMapType *next, *prev;
	char idname[64];
	short spaceid, regionid;
	/* eManipulatorMapTypeFlag */
	int flag;
	/* types of manipulator-groups for this manipulator-map type */
	ListBase manipulator_grouptypes;
} wmManipulatorMapType;

void WM_manipulatormap_selected_delete(wmManipulatorMap *mmap);


/* -------------------------------------------------------------------- */
/* Manipulator drawing */

typedef struct ManipulatorDrawInfo {
	int nverts;
	int ntris;
	float (*verts)[3];
	float (*normals)[3];
	unsigned short *indices;
	bool init;
} ManipulatorDrawInfo;

void manipulator_drawinfo_draw(ManipulatorDrawInfo *info, const bool select);

#endif  /* __WM_MANIPULATOR_INTERN_H__ */

