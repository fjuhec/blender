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

/** \file blender/windowmanager/manipulators/intern/manipulator_library/geom_cone.c
 *  \ingroup wm
 */

int _MANIPULATOR_nverts_Cone = 10;
int _MANIPULATOR_ntris_Cone = 16;

float _MANIPULATOR_verts_Cone[][3] = {
	{0.000000, 0.000000, 0.000000},
	{0.000000, 0.000000, 1.000000},
	{0.000000, 0.270000, 0.000000},
	{0.190919, 0.190919, 0.000000},
	{0.270000, -0.000000, 0.000000},
	{0.190919, -0.190919, 0.000000},
	{-0.000000, -0.270000, 0.000000},
	{-0.190919, -0.190919, 0.000000},
	{-0.270000, 0.000000, 0.000000},
	{-0.190919, 0.190919, 0.000000},
};

float _MANIPULATOR_normals_Cone[][3] = {
	{0.000000, 0.000000, -1.000000},
	{0.000000, 0.000000, 0.999969},
	{0.000000, 0.848567, -0.529069},
	{0.600024, 0.600024, -0.529069},
	{0.848567, 0.000000, -0.529069},
	{0.600024, -0.600024, -0.529069},
	{0.000000, -0.848567, -0.529069},
	{-0.600024, -0.600024, -0.529069},
	{-0.848567, 0.000000, -0.529069},
	{-0.600024, 0.600024, -0.529069},
};

unsigned short _MANIPULATOR_indices_Cone[] = {
	0, 2, 3,
	2, 1, 3,
	0, 3, 4,
	3, 1, 4,
	0, 4, 5,
	4, 1, 5,
	0, 5, 6,
	5, 1, 6,
	0, 6, 7,
	6, 1, 7,
	0, 7, 8,
	7, 1, 8,
	0, 8, 9,
	8, 1, 9,
	0, 9, 2,
	9, 1, 2,
};
