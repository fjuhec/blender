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

/** \file blender/windowmanager/manipulators/WM_manipulator_library.h
 *  \ingroup wm
 *
 * \name Generic Manipulator Library
 *
 * Only included in WM_api.h and lower level files.
 */


#ifndef __WM_MANIPULATOR_LIBRARY_H__
#define __WM_MANIPULATOR_LIBRARY_H__

struct wmManipulator;
struct wmManipulatorGroup;


/* -------------------------------------------------------------------- */
/* 3D Arrow Manipulator */

enum ArrowManipulatorStyle {
	MANIPULATOR_ARROW_STYLE_CONE = 0,
	MANIPULATOR_ARROW_STYLE_CUBE = 1,
};

struct wmManipulator *WM_arrow_manipulator_new(
        struct wmManipulatorGroup *mgroup, const char *name, const enum ArrowManipulatorStyle style);
void WM_arrow_manipulator_set_direction(struct wmManipulator *manipulator, const float direction[3]);
void WM_arrow_manipulator_set_length(struct wmManipulator *manipulator, const float length);


/* -------------------------------------------------------------------- */
/* Dial Manipulator */

enum {
	MANIPULATOR_DIAL_STYLE_RING = 0,
	MANIPULATOR_DIAL_STYLE_RING_CLIPPED = 1,
	MANIPULATOR_DIAL_STYLE_RING_FILLED = 2,
};

struct wmManipulator *WM_dial_manipulator_new(struct wmManipulatorGroup *mgroup, const char *name, const int style);
void WM_dial_manipulator_set_up_vector(struct wmManipulator *manipulator, const float direction[3]);
void WM_dial_manipulator_set_value(struct wmManipulator *manipulator, const double value);

#endif  /* __WM_MANIPULATOR_LIBRARY_H__ */

