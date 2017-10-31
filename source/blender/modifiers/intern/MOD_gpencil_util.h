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
 * The Original Code is Copyright (C) Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/modifiers/intern/MOD_gpencil_util.h
 *  \ingroup modifiers
 */


#ifndef __MOD_GPENCIL_UTIL_H__
#define __MOD_GPENCIL_UTIL_H__

bool is_stroke_affected_by_modifier(
        char *mlayername, int mpassindex, int minpoints,
        bGPDlayer *gpl, bGPDstroke *gps, bool inv1, bool inv2);

float is_point_affected_by_modifier(bGPDspoint *pt, int inverse, int vindex);

void gp_mod_fill_random_array(float *ar, int count);

#endif  /* __MOD_GPENCIL_UTIL_H__ */
