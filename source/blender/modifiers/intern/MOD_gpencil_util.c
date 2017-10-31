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
 * The Original Code is Copyright (C) 2017, Blender Foundation
 * This is a new part of Blender
 *
 * Contributor(s): Antonio Vazquez
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/modifiers/intern/MOD_gpencil_util.c
 *  \ingroup bke
 */

 
#include <stdio.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_math_vector.h"
#include "BLI_math_color.h"
#include "BLI_rand.h"

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_modifier_types.h"

#include "BKE_global.h"
#include "BKE_object.h"
#include "BKE_lattice.h"
#include "BKE_gpencil.h"
#include "BKE_modifier.h"
#include "BKE_colortools.h"


/* fill an array with random numbers */
void gp_mod_fill_random_array(float *ar, int count)
{
	for (int i = 0; i < count; i++) {
		ar[i] = BLI_frand();
	}
} 

/* verify if valid layer and pass index */
bool is_stroke_affected_by_modifier(
        char *mlayername, int mpassindex, int minpoints,
        bGPDlayer *gpl, bGPDstroke *gps, bool inv1, bool inv2)
{
	/* omit if filter by layer */
	if (mlayername[0] != '\0') {
		if (inv1 == false) {
			if (!STREQ(mlayername, gpl->info)) {
				return false;
			}
		}
		else {
			if (STREQ(mlayername, gpl->info)) {
				return false;
			}
		}
	}
	/* verify pass */
	if (mpassindex > 0) {
		if (inv2 == false) {
			if (gps->palcolor->index != mpassindex) {
				return false;
			}
		}
		else {
			if (gps->palcolor->index == mpassindex) {
				return false;
			}
		}
	}
	/* need to have a minimum number of points */
	if ((minpoints > 0) && (gps->totpoints < minpoints)) {
		return false;
	}

	return true;
}

/* verify if valid vertex group *and return weight */
float is_point_affected_by_modifier(bGPDspoint *pt, int inverse, int vindex)
{
	float weight = 1.0f;

	if (vindex >= 0) {
		weight = BKE_gpencil_vgroup_use_index(pt, vindex);
		if ((weight >= 0.0f) && (inverse == 1)) {
			return -1.0f;
		}

		if ((weight < 0.0f) && (inverse == 0)) {
			return -1.0f;
		}

		/* if inverse, weight is always 1 */
		if ((weight < 0.0f) && (inverse == 1)) {
			return 1.0f;
		}

	}

	return weight;
}

