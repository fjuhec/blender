/*
 * Copyright 2017, Blender Foundation.
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
 * Contributor(s): Blender Institute
 *
 */

/** \file gpencil_mode.h
 *  \ingroup DNA
 */

#ifndef __GPENCIL_MODE_H__
#define __GPENCIL_MODE_H__

struct Batch;

struct Batch *gpencil_get_point_geom(struct bGPDspoint *pt, short thickness, const float ink[4]);
struct Batch *gpencil_get_stroke_geom(struct bGPDstroke *gps, short thickness, const float ink[4]);
struct Batch *gpencil_get_fill_geom(struct bGPDstroke *gps, const float color[4]);
struct Batch *gpencil_get_edit_geom(struct bGPDstroke *gps, float alpha, short dflag);
struct Batch *gpencil_get_buffer_stroke_geom(struct bGPdata *gpd, short thickness);
struct Batch *gpencil_get_buffer_fill_geom(const struct tGPspoint *points, int totpoints, float ink[4]);
struct Batch *gpencil_get_buffer_point_geom(struct bGPdata *gpd, short thickness);

bool gpencil_can_draw_stroke(const struct bGPDstroke *gps);

#endif /* __GPENCIL_MODE_H__ */