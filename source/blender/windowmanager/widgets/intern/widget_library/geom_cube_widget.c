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

/** \file blender/windowmanager/widgets/intern/widget_library/geom_cube_widget.c
 *  \ingroup wm
 */

int _WIDGET_nverts_cube = 8;
int _WIDGET_ntris_cube = 12;

float _WIDGET_verts_cube[][3] = {
    {1.000000, 1.000000, -1.000000},
    {1.000000, -1.000000, -1.000000},
    {-1.000000, -1.000000, -1.000000},
    {-1.000000, 1.000000, -1.000000},
    {1.000000, 1.000000, 1.000000},
    {0.999999, -1.000001, 1.000000},
    {-1.000000, -1.000000, 1.000000},
    {-1.000000, 1.000000, 1.000000},
};

float _WIDGET_normals_cube[][3] = {
    {0.577349, 0.577349, -0.577349},
    {0.577349, -0.577349, -0.577349},
    {-0.577349, -0.577349, -0.577349},
    {-0.577349, 0.577349, -0.577349},
    {0.577349, 0.577349, 0.577349},
    {0.577349, -0.577349, 0.577349},
    {-0.577349, -0.577349, 0.577349},
    {-0.577349, 0.577349, 0.577349},
};

unsigned short _WIDGET_indices_cube[] = {
    1, 2, 3,
    7, 6, 5,
    4, 5, 1,
    5, 6, 2,
    2, 6, 7,
    0, 3, 7,
    0, 1, 3,
    4, 7, 5,
    0, 4, 1,
    1, 5, 2,
    3, 2, 7,
    4, 0, 7,
};

