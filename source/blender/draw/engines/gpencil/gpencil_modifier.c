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
 * Contributor(s): Antonio Vazquez
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/gpencil/gpencil_modifier.c
 *  \ingroup edgpencil
 */


#include "DNA_gpencil_types.h"
#include "DNA_object_types.h"
#include "DNA_modifier_types.h"
#include "DNA_view3d_types.h"

#include "BLI_math.h"
#include "BLI_rand.h"

#include "BKE_gpencil.h"
#include "ED_gpencil.h"

/* calculate a noise base on stroke direction */
static void ED_gpencil_noise_modifier(Object *ob, GpencilNoiseModifierData *mmd, bGPDstroke *gps)
{
	float normal[3];
	float vec1[3], vec2[3];

	/* Need three points or more */
	if (gps->totpoints < 3) {
		return;
	}
	/* calculate stroke normal*/
	ED_gpencil_stroke_normal(gps, normal);

	/* move points (starting in point 2) */
	bGPDspoint *pt0, *pt1; 
	float shift;
	for (int i = 1; i < gps->totpoints; i++) {
		pt0 = &gps->points[i - 1];
		pt1 = &gps->points[i];
		/* initial vector (p0 -> p1) */
		sub_v3_v3v3(vec1, &pt1->x, &pt0->x);
		/* vector orthogonal to normal */
		cross_v3_v3v3(vec2, vec1, normal);
		normalize_v3(vec2);
		shift = BLI_frand() * (mmd->seed / 10.0f) * mmd->factor;
		if (BLI_frand() > 0.5f) {
			mul_v3_fl(vec2, shift);
		}
		else {
			mul_v3_fl(vec2, shift * -1.0f);
		}
		add_v3_v3(&pt1->x, vec2);
	}
}

/* apply all modifiers */
void ED_gpencil_apply_modifiers(Object *ob, bGPDstroke *gps)
{
	ModifierData *md;

	for (md = ob->modifiers.first; md; md = md->next) {
		if (md->mode & (eModifierMode_Realtime | eModifierMode_Render)) {
			if (md->type == eModifierType_GpencilNoise) {
				GpencilNoiseModifierData *mmd = (GpencilNoiseModifierData *)md;
				ED_gpencil_noise_modifier(ob, mmd, gps);
			}
		}
	}

}

