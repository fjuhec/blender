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
 * The Original Code is Copyright (C) 2008, Blender Foundation
 * This is a new part of Blender
 *
 * Contributor(s): Joshua Leung, Antonio Vazquez
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/gpencil/drawgpencil.c
 *  \ingroup edgpencil
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>
#include <float.h>

#include "MEM_guardedalloc.h"

#include "BLI_sys_types.h"

#include "BLI_math.h"
#include "BLI_utildefines.h"
#include "BLI_polyfill2d.h"

#include "BLF_api.h"
#include "BLT_translation.h"

#include "DNA_gpencil_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_view3d_types.h"
#include "DNA_userdef_types.h"
#include "DNA_object_types.h"

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_gpencil.h"
#include "BKE_image.h"

#include "WM_api.h"

#include "BIF_glutil.h"

#include "GPU_immediate.h"
#include "GPU_draw.h"

#include "ED_gpencil.h"

/* set stroke point to vbo */
static void gpencil_set_stroke_point(VertexBuffer *vbo, const bGPDspoint *pt, int idx,
						    unsigned int pos_id, unsigned int color_id,
							unsigned int thickness_id, short thickness,
	                        const float diff_mat[4][4], const float ink[4], bool inverse)
{
	float fpt[3];

	float alpha = ink[3] * pt->strength;
	CLAMP(alpha, GPENCIL_STRENGTH_MIN, 1.0f);
	float col[4] = { ink[0], ink[1], ink[2], alpha };
	VertexBuffer_set_attrib(vbo, color_id, idx, col);

	float thick = max_ff(pt->pressure * thickness, 1.0f);
	VertexBuffer_set_attrib(vbo, thickness_id, idx, &thick);

	mul_v3_m4v3(fpt, diff_mat, &pt->x);
	if (inverse) {
		mul_v3_fl(fpt, -1.0f);
	}
	VertexBuffer_set_attrib(vbo, pos_id, idx, fpt);
}

/* create batch geometry data for stroke shader */
Batch *gpencil_get_stroke_geom(bGPDstroke *gps, short thickness, const float diff_mat[4][4], const float ink[4])
{
	bGPDspoint *points = gps->points;
	int totpoints = gps->totpoints;
	/* if cyclic needs more vertex */
	int cyclic_add = (gps->flag & GP_STROKE_CYCLIC) ? 2 : 0;

	static VertexFormat format = { 0 };
	static unsigned int pos_id, color_id, thickness_id;
	if (format.attrib_ct == 0) {
		pos_id = VertexFormat_add_attrib(&format, "pos", COMP_F32, 3, KEEP_FLOAT);
		color_id = VertexFormat_add_attrib(&format, "color", COMP_F32, 4, KEEP_FLOAT);
		thickness_id = VertexFormat_add_attrib(&format, "thickness", COMP_F32, 1, KEEP_FLOAT);
	}

	VertexBuffer *vbo = VertexBuffer_create_with_format(&format);
	VertexBuffer_allocate_data(vbo, totpoints + cyclic_add + 2);

	/* draw stroke curve */
	const bGPDspoint *pt = points;
	int idx = 0;
	for (int i = 0; i < totpoints; i++, pt++) {
		/* first point for adjacency (not drawn) */
		if (i == 0) {
			gpencil_set_stroke_point(vbo, pt, idx, pos_id, color_id, thickness_id, thickness, diff_mat, ink, true);
			++idx;
		}
		/* set point */
		gpencil_set_stroke_point(vbo, pt, idx, pos_id, color_id, thickness_id, thickness, diff_mat, ink, false);
		++idx;
	}

	if (gps->flag & GP_STROKE_CYCLIC && totpoints > 2) {
		/* draw line to first point to complete the cycle */
		gpencil_set_stroke_point(vbo, &points[0], idx, pos_id, color_id, thickness_id, thickness, diff_mat, ink, false);
		++idx;
		/* now add adjacency points using 2nd & 3rd point to get smooth transition */
		gpencil_set_stroke_point(vbo, &points[1], idx, pos_id, color_id, thickness_id, thickness, diff_mat, ink, false);
		++idx;
		gpencil_set_stroke_point(vbo, &points[2], idx, pos_id, color_id, thickness_id, thickness, diff_mat, ink, false);
		++idx;
	}
	/* last adjacency point (not drawn) */
	else {
		gpencil_set_stroke_point(vbo, &points[totpoints - 1], idx, pos_id, color_id, thickness_id, thickness, diff_mat, ink, true);
	}

	return Batch_create(PRIM_LINE_STRIP_ADJACENCY, vbo, NULL);
}

/* Helper for doing all the checks on whether a stroke can be drawn */
bool gpencil_can_draw_stroke(const bGPDstroke *gps)
{
	/* skip stroke if it doesn't have any valid data */
	if ((gps->points == NULL) || (gps->totpoints < 1))
		return false;

	/* check if the color is visible */
	PaletteColor *palcolor = gps->palcolor;
	if ((palcolor == NULL) ||
		(palcolor->flag & PC_COLOR_HIDE))
	{
		return false;
	}

	/* stroke can be drawn */
	return true;
}
