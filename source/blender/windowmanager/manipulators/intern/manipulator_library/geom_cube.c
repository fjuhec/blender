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

/** \file blender/windowmanager/manipulators/intern/manipulator_library/geom_cube.c
 *  \ingroup wm
 */

int _MANIPULATOR_nverts_cube = 8;
int _MANIPULATOR_ntris_cube = 12;

float _MANIPULATOR_verts_cube[][3] = {
	{1.000000, 1.000000, -1.000000},
	{1.000000, -1.000000, -1.000000},
	{-1.000000, -1.000000, -1.000000},
	{-1.000000, 1.000000, -1.000000},
	{1.000000, 1.000000, 1.000000},
	{0.999999, -1.000001, 1.000000},
	{-1.000000, -1.000000, 1.000000},
	{-1.000000, 1.000000, 1.000000},
};

float _MANIPULATOR_normals_cube[][3] = {
	{0.577349, 0.577349, -0.577349},
	{0.577349, -0.577349, -0.577349},
	{-0.577349, -0.577349, -0.577349},
	{-0.577349, 0.577349, -0.577349},
	{0.577349, 0.577349, 0.577349},
	{0.577349, -0.577349, 0.577349},
	{-0.577349, -0.577349, 0.577349},
	{-0.577349, 0.577349, 0.577349},
};

unsigned short _MANIPULATOR_indices_cube[] = {
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

