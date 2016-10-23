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

/** \file blender/windowmanager/manipulators/intern/manipulator_library/manipulator_geometry.h
 *  \ingroup wm
 *
 * \name Manipulator Geometry
 *
 * \brief Prototypes for arrays defining the manipulator geometry. The actual definitions can be found in files usually
 *        called geom_xxx.c
 */


#ifndef __MANIPULATOR_GEOMETRY_H__
#define __MANIPULATOR_GEOMETRY_H__


/* cone */

extern int _MANIPULATOR_nverts_cone;
extern int _MANIPULATOR_ntris_cone;

extern float _MANIPULATOR_verts_cone[][3];
extern float _MANIPULATOR_normals_cone[][3];
extern unsigned short _MANIPULATOR_indices_cone[];


/* cube */

extern int _MANIPULATOR_nverts_cube;
extern int _MANIPULATOR_ntris_cube;

extern float _MANIPULATOR_verts_cube[][3];
extern float _MANIPULATOR_normals_cube[][3];
extern unsigned short _MANIPULATOR_indices_cube[];

#endif  /* __MANIPULATOR_GEOMETRY_H__ */

