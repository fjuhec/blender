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

/** \file blender/blenkernel/intern/gpencil_modifier.c
 *  \ingroup bke
 */

 
#include <stdio.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_math_vector.h"

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_modifier_types.h"
#include "DNA_vec_types.h"

#include "BKE_global.h"
#include "BKE_gpencil.h"
#include "BKE_lattice.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
 
#include "DEG_depsgraph.h"

/* *************************************************** */
/* Geometry Utilities */

/* calculate stroke normal using some points */
void BKE_gpencil_stroke_normal(const bGPDstroke *gps, float r_normal[3])
{
	if (gps->totpoints < 3) {
		zero_v3(r_normal);
		return;
	}

	bGPDspoint *points = gps->points;
	int totpoints = gps->totpoints;

	const bGPDspoint *pt0 = &points[0];
	const bGPDspoint *pt1 = &points[1];
	const bGPDspoint *pt3 = &points[(int)(totpoints * 0.75)];

	float vec1[3];
	float vec2[3];

	/* initial vector (p0 -> p1) */
	sub_v3_v3v3(vec1, &pt1->x, &pt0->x);

	/* point vector at 3/4 */
	sub_v3_v3v3(vec2, &pt3->x, &pt0->x);

	/* vector orthogonal to polygon plane */
	cross_v3_v3v3(r_normal, vec1, vec2);

	/* Normalize vector */
	normalize_v3(r_normal);
}

/* Get points of stroke always flat to view not affected by camera view or view position */
static void gpencil_stroke_project_2d(const bGPDspoint *points, int totpoints, vec2f *points2d)
{
	const bGPDspoint *pt0 = &points[0];
	const bGPDspoint *pt1 = &points[1];
	const bGPDspoint *pt3 = &points[(int)(totpoints * 0.75)];

	float locx[3];
	float locy[3];
	float loc3[3];
	float normal[3];

	/* local X axis (p0 -> p1) */
	sub_v3_v3v3(locx, &pt1->x, &pt0->x);

	/* point vector at 3/4 */
	sub_v3_v3v3(loc3, &pt3->x, &pt0->x);

	/* vector orthogonal to polygon plane */
	cross_v3_v3v3(normal, locx, loc3);

	/* local Y axis (cross to normal/x axis) */
	cross_v3_v3v3(locy, normal, locx);

	/* Normalize vectors */
	normalize_v3(locx);
	normalize_v3(locy);

	/* Get all points in local space */
	for (int i = 0; i < totpoints; i++) {
		const bGPDspoint *pt = &points[i];
		float loc[3];

		/* Get local space using first point as origin */
		sub_v3_v3v3(loc, &pt->x, &pt0->x);

		vec2f *point = &points2d[i];
		point->x = dot_v3v3(loc, locx);
		point->y = dot_v3v3(loc, locy);
	}

}

/* Stroke Simplify ------------------------------------- */

/* Reduce a series of points to a simplified version, but
 * maintains the general shape of the series
 *
 * Ramer - Douglas - Peucker algorithm
 * by http ://en.wikipedia.org/wiki/Ramer-Douglas-Peucker_algorithm
 */
static void gpencil_rdp_stroke(bGPDstroke *gps, vec2f *points2d, float epsilon)
{
	vec2f *old_points2d = points2d;
	int totpoints = gps->totpoints;
	char *marked = NULL;
	char work;

	int start = 1;
	int end = gps->totpoints - 2;

	marked = MEM_callocN(totpoints, "GP marked array");
	marked[start] = 1;
	marked[end] = 1;

	work = 1;
	int totmarked = 0;
	/* while still reducing */
	while (work) {
		int ls, le;
		work = 0;

		ls = start;
		le = start + 1;

		/* while not over interval */
		while (ls < end) {
			int max_i = 0;
			float v1[2];
			/* divided to get more control */
			float max_dist = epsilon / 10.0f;

			/* find the next marked point */
			while (marked[le] == 0) {
				le++;
			}

			/* perpendicular vector to ls-le */
			v1[1] = old_points2d[le].x - old_points2d[ls].x;
			v1[0] = old_points2d[ls].y - old_points2d[le].y;

			for (int i = ls + 1; i < le; i++) {
				float mul;
				float dist;
				float v2[2];

				v2[0] = old_points2d[i].x - old_points2d[ls].x;
				v2[1] = old_points2d[i].y - old_points2d[ls].y;

				if (v2[0] == 0 && v2[1] == 0) {
					continue;
				}

				mul = (float)(v1[0] * v2[0] + v1[1] * v2[1]) / (float)(v2[0] * v2[0] + v2[1] * v2[1]);

				dist = mul * mul * (v2[0] * v2[0] + v2[1] * v2[1]);

				if (dist > max_dist) {
					max_dist = dist;
					max_i = i;
				}
			}

			if (max_i != 0) {
				work = 1;
				marked[max_i] = 1;
				totmarked++;
			}

			ls = le;
			le = ls + 1;
		}
	}

	/* adding points marked */
	bGPDspoint *old_points = MEM_dupallocN(gps->points);

	/* resize gps */
	gps->flag |= GP_STROKE_RECALC_CACHES;
	gps->tot_triangles = 0;

	int j = 0;
	for (int i = 0; i < totpoints; i++) {
		bGPDspoint *old_pt = &old_points[i];
		bGPDspoint *pt = &gps->points[j];
		if ((marked[i]) || (i == 0) || (i == totpoints - 1)) {
			memcpy(pt, old_pt, sizeof(bGPDspoint));
			j++;
		}
		else {
			BKE_gpencil_free_point_weights(old_pt);
		}
	}

	gps->totpoints = j;

	MEM_SAFE_FREE(old_points);
	MEM_SAFE_FREE(marked);
}

/* Simplify stroke using Ramer-Douglas-Peucker algorithm */
void BKE_gpencil_simplify_stroke(bGPDlayer *UNUSED(gpl), bGPDstroke *gps, float factor)
{
	/* first create temp data and convert points to 2D */
	vec2f *points2d = MEM_mallocN(sizeof(vec2f) * gps->totpoints, "GP Stroke temp 2d points");

	gpencil_stroke_project_2d(gps->points, gps->totpoints, points2d);

	gpencil_rdp_stroke(gps, points2d, factor);

	MEM_SAFE_FREE(points2d);
}

/* Simplify alternate vertex of stroke except extrems */
void BKE_gpencil_simplify_alternate(bGPDlayer *UNUSED(gpl), bGPDstroke *gps, float factor)
{
	if (gps->totpoints < 5) {
		return;
	}

	/* save points */
	bGPDspoint *old_points = MEM_dupallocN(gps->points);

	/* resize gps */
	int newtot = (gps->totpoints - 2) / 2;
	if (((gps->totpoints - 2) % 2) > 0) {
		++newtot;
	}
	newtot += 2;

	gps->points = MEM_recallocN(gps->points, sizeof(*gps->points) * newtot);
	gps->flag |= GP_STROKE_RECALC_CACHES;
	gps->tot_triangles = 0;

	int j = 0;
	for (int i = 0; i < gps->totpoints; i++) {
		bGPDspoint *old_pt = &old_points[i];
		bGPDspoint *pt = &gps->points[j];

		if ((i == 0) || (i == gps->totpoints - 1) || ((i % 2) > 0.0)) {
			memcpy(pt, old_pt, sizeof(bGPDspoint));
			j++;
		}
		else {
			BKE_gpencil_free_point_weights(old_pt);
		}
	}

	gps->totpoints = j;

	MEM_SAFE_FREE(old_points);
}

/* *************************************************** */
/* Modifier Utilities */

/* Lattice Modifier ---------------------------------- */
/* Usually, evaluation of the lattice modifier is self-contained.
 * However, since GP's modifiers operate on a per-stroke basis,
 * we need to these two extra functions that called before/after
 * each loop over all the geometry being evaluated.
 */

/* init lattice deform data */
void BKE_gpencil_lattice_init(Object *ob)
{
	ModifierData *md;
	for (md = ob->modifiers.first; md; md = md->next) {
		if (md->type == eModifierType_GpencilLattice) {
			GpencilLatticeModifierData *mmd = (GpencilLatticeModifierData *)md;
			Object *latob = NULL;

			latob = mmd->object;
			if ((!latob) || (latob->type != OB_LATTICE)) {
				return;
			}
			if (mmd->cache_data) {
				end_latt_deform((LatticeDeformData *)mmd->cache_data);
			}

			/* init deform data */
			mmd->cache_data = (LatticeDeformData *)init_latt_deform(latob, ob);
		}
	}
}

/* clear lattice deform data */
void BKE_gpencil_lattice_clear(Object *ob)
{
	ModifierData *md;
	for (md = ob->modifiers.first; md; md = md->next) {
		if (md->type == eModifierType_GpencilLattice) {
			GpencilLatticeModifierData *mmd = (GpencilLatticeModifierData *)md;
			if ((mmd) && (mmd->cache_data)) {
				end_latt_deform((LatticeDeformData *)mmd->cache_data);
				mmd->cache_data = NULL;
			}
		}
	}
}

/* *************************************************** */
/* Modifier Methods - Evaluation Loops, etc. */

/* verify if exist geometry modifiers */
bool BKE_gpencil_has_geometry_modifiers(Object *ob)
{
	ModifierData *md;
	for (md = ob->modifiers.first; md; md = md->next) {
		const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
		
		if (mti && mti->generateStrokes) {
			return true;
		}
	}
	return false;
}

/* apply stroke modifiers */
void BKE_gpencil_stroke_modifiers(EvaluationContext *eval_ctx, Object *ob, bGPDlayer *gpl, bGPDframe *UNUSED(gpf), bGPDstroke *gps)
{
	ModifierData *md;
	bGPdata *gpd = ob->data;
	bool is_edit = GPENCIL_ANY_EDIT_MODE(gpd);
	
	for (md = ob->modifiers.first; md; md = md->next) {
		if (((md->mode & eModifierMode_Realtime) && ((G.f & G_RENDER_OGL) == 0)) ||
		    ((md->mode & eModifierMode_Render) && (G.f & G_RENDER_OGL)))
		{
			const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
			
			if (((md->mode & eModifierMode_Editmode) == 0) && (is_edit)) {
				continue;
			}
			
			if (mti && mti->deformStroke) {
				mti->deformStroke(md, eval_ctx, ob, gpl, gps);
			}
		}
	}
}

/* apply stroke geometry modifiers */
void BKE_gpencil_geometry_modifiers(EvaluationContext *eval_ctx, Object *ob, bGPDlayer *gpl, bGPDframe *gpf)
{
	ModifierData *md;
	bGPdata *gpd = ob->data;
	bool is_edit = GPENCIL_ANY_EDIT_MODE(gpd);

	int id = 0;
	for (md = ob->modifiers.first; md; md = md->next) {
		if (((md->mode & eModifierMode_Realtime) && ((G.f & G_RENDER_OGL) == 0)) ||
		    ((md->mode & eModifierMode_Render) && (G.f & G_RENDER_OGL)))
		{
			const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
			
			if (((md->mode & eModifierMode_Editmode) == 0) && (is_edit)) {
				continue;
			}

			if (mti->generateStrokes) {
				mti->generateStrokes(md, eval_ctx, ob, gpl, gpf, id);
			}
		}
		id++;
	}
}

/* *************************************************** */

void BKE_gpencil_eval_geometry(const EvaluationContext *UNUSED(eval_ctx),
                               bGPdata *UNUSED(gpd))
{
	/* TODO: Move "derived_gpf" logic here from DRW_gpencil_populate_datablock()?
	 * This way, we wouldn't have to mess around trying to figure out how to pass
	 * an EvaluationContext to each of the modifiers.
	 */
}


/* *************************************************** */
