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
#include "BLI_math_color.h"
#include "BLI_rand.h"

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_modifier_types.h"

#include "BKE_global.h"
#include "BKE_colortools.h"
#include "BKE_deform.h"
#include "BKE_gpencil.h"
#include "BKE_lattice.h"
#include "BKE_modifier.h"
#include "BKE_object.h"
 
#include "DEG_depsgraph.h"
 
// XXX: temp transitional code
#include "../../modifiers/intern/MOD_gpencil_util.h"

/* used to save temp strokes */
typedef struct tGPencilStrokeCache {
	struct bGPDstroke *gps;
	int idx;
} tGPencilStrokeCache;

/* temp data for simplify modifier */
typedef struct tbGPDspoint {
	float p2d[2];
} tbGPDspoint;

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

/* helper function to sort strokes using qsort */
static int gpencil_stroke_cache_compare(const void *a1, const void *a2)
{
	const tGPencilStrokeCache *ps1 = a1, *ps2 = a2;

	if (ps1->idx < ps2->idx) return -1;
	else if (ps1->idx > ps2->idx) return 1;

	return 0;
}

/* dupli modifier */
void BKE_gpencil_dupli_modifier(
        int id, GpencilDupliModifierData *mmd, Object *UNUSED(ob), bGPDlayer *gpl, bGPDframe *gpf)
{
	bGPDspoint *pt;
	bGPDstroke *gps_dst;
	struct tGPencilStrokeCache *stroke_cache, *p = NULL;
	float offset[3], rot[3], scale[3];
	float mat[4][4];
	float factor;
	int ri;

	/* create cache for sorting */
	int totstrokes = BLI_listbase_count(&gpf->strokes);
	int cachesize =  totstrokes * mmd->count;
	p = MEM_callocN(sizeof(struct tGPencilStrokeCache) * cachesize, "tGPencilStrokeCache");
	if (p) {
		stroke_cache = p;
	}
	else {
		return;
	}

	int stroke = 0;
	int idx = 0;
	for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
		stroke++;
		if (!is_stroke_affected_by_modifier(
		        mmd->layername, mmd->pass_index, 1, gpl, gps,
		        mmd->flag & GP_DUPLI_INVERSE_LAYER, mmd->flag & GP_DUPLI_INVERSE_PASS))
		{
			continue;
		}

		for (int e = 0; e < mmd->count; e++) {
			/* duplicate stroke */
			gps_dst = MEM_dupallocN(gps);
			if (id > -1) {
				gps_dst->palcolor = MEM_dupallocN(gps->palcolor);
			}
			gps_dst->points = MEM_dupallocN(gps->points);
			BKE_gpencil_stroke_weights_duplicate(gps, gps_dst);

			gps_dst->triangles = MEM_dupallocN(gps->triangles);

			/* add to array for sorting later */
			stroke_cache[idx].gps = gps_dst;
			stroke_cache[idx].idx = (e * 100000) + stroke;

			mul_v3_v3fl(offset, mmd->offset, e + 1);
			ri = mmd->rnd[0];
			/* rotation */
			if (mmd->flag & GP_DUPLI_RANDOM_ROT) {
				factor = mmd->rnd_rot * mmd->rnd[ri];
				mul_v3_v3fl(rot, mmd->rot, factor);
				add_v3_v3(rot, mmd->rot);
			}
			else {
				copy_v3_v3(rot, mmd->rot);
			}
			/* scale */
			if (mmd->flag & GP_DUPLI_RANDOM_SIZE) {
				factor = mmd->rnd_size * mmd->rnd[ri];
				mul_v3_v3fl(scale, mmd->scale, factor);
				add_v3_v3(scale, mmd->scale);
			}
			else {
				copy_v3_v3(scale, mmd->scale);
			}
			/* move random index */
			mmd->rnd[0]++;
			if (mmd->rnd[0] > 19) {
				mmd->rnd[0] = 1;
			}

			loc_eul_size_to_mat4(mat, offset, rot, scale);

			/* move points */
			for (int i = 0; i < gps->totpoints; i++) {
				pt = &gps_dst->points[i];
				mul_m4_v3(mat, &pt->x);
			}
			idx++;
		}
	}
	/* sort by idx */
	qsort(stroke_cache, idx, sizeof(tGPencilStrokeCache), gpencil_stroke_cache_compare);
	
	/* add to listbase */
	for (int i = 0; i < idx; i++) {
		BLI_addtail(&gpf->strokes, stroke_cache[i].gps);
	}

	/* free memory */
	MEM_SAFE_FREE(stroke_cache);
}

/* array modifier */
void BKE_gpencil_array_modifier(
        int UNUSED(id), GpencilArrayModifierData *mmd, Object *UNUSED(ob), int elem_idx[3], float r_mat[4][4])
{
	float offset[3], rot[3], scale[3];
	float factor;
	int ri;

	offset[0] = mmd->offset[0] * elem_idx[0];
	offset[1] = mmd->offset[1] * elem_idx[1];
	offset[2] = mmd->offset[2] * elem_idx[2];

	ri = mmd->rnd[0];
	/* rotation */
	if (mmd->flag & GP_ARRAY_RANDOM_ROT) {
		factor = mmd->rnd_rot * mmd->rnd[ri];
		mul_v3_v3fl(rot, mmd->rot, factor);
		add_v3_v3(rot, mmd->rot);
	}
	else {
		copy_v3_v3(rot, mmd->rot);
	}
	/* scale */
	if (mmd->flag & GP_ARRAY_RANDOM_SIZE) {
		factor = mmd->rnd_size * mmd->rnd[ri];
		mul_v3_v3fl(scale, mmd->scale, factor);
		add_v3_v3(scale, mmd->scale);
	}
	else {
		copy_v3_v3(scale, mmd->scale);
	}
	/* move random index */
	mmd->rnd[0]++;
	if (mmd->rnd[0] > 19) {
		mmd->rnd[0] = 1;
	}
	/* calculate matrix */
	loc_eul_size_to_mat4(r_mat, offset, rot, scale);

}

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

/* apply lattice to stroke */
void BKE_gpencil_lattice_modifier(
        int UNUSED(id), GpencilLatticeModifierData *mmd, Object *ob, bGPDlayer *gpl, bGPDstroke *gps)
{
	bGPDspoint *pt;
	int vindex = defgroup_name_index(ob, mmd->vgname);
	float weight = 1.0f;

	if (!is_stroke_affected_by_modifier(
	        mmd->layername, mmd->pass_index, 3, gpl, gps,
	        mmd->flag & GP_LATTICE_INVERSE_LAYER, mmd->flag & GP_LATTICE_INVERSE_PASS))
	{
		return;
	}

	if (mmd->cache_data == NULL) {
		return;
	}

	for (int i = 0; i < gps->totpoints; i++) {
		pt = &gps->points[i];
		/* verify vertex group */
		weight = is_point_affected_by_modifier(pt, (int)(!(mmd->flag & GP_LATTICE_INVERSE_VGROUP) == 0), vindex);
		if (weight < 0) {
			continue;
		}

		calc_latt_deform((LatticeDeformData *)mmd->cache_data, &pt->x, mmd->strength * weight);
	}
}


/* Get points of stroke always flat to view not affected by camera view or view position */
static void gpencil_stroke_project_2d(const bGPDspoint *points, int totpoints, tbGPDspoint *points2d)
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

		tbGPDspoint *point = &points2d[i];
		point->p2d[0] = dot_v3v3(loc, locx);
		point->p2d[1] = dot_v3v3(loc, locy);
	}

}

/* --------------------------------------------------------------------------
* Reduces a series of points to a simplified version, but
* maintains the general shape of the series
*
* Ramer - Douglas - Peucker algorithm
* by http ://en.wikipedia.org/wiki/Ramer-Douglas-Peucker_algorithm
* -------------------------------------------------------------------------- */
static void gpencil_rdp_stroke(bGPDstroke *gps, tbGPDspoint *points2d, float epsilon)
{
	tbGPDspoint *old_points2d = points2d;
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
			v1[1] = old_points2d[le].p2d[0] - old_points2d[ls].p2d[0];
			v1[0] = old_points2d[ls].p2d[1] - old_points2d[le].p2d[1];

			for (int i = ls + 1; i < le; i++) {
				float mul;
				float dist;
				float v2[2];

				v2[0] = old_points2d[i].p2d[0] - old_points2d[ls].p2d[0];
				v2[1] = old_points2d[i].p2d[1] - old_points2d[ls].p2d[1];

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

	int x = 0;
	for (int i = 0; i < totpoints; i++) {
		bGPDspoint *old_pt = &old_points[i];
		bGPDspoint *pt = &gps->points[x];
		if ((marked[i]) || (i == 0) || (i == totpoints - 1)) {
			memcpy(pt, old_pt, sizeof(bGPDspoint));
			x++;
		}
		else {
			BKE_gpencil_free_point_weights(old_pt);
		}
	}

	gps->totpoints = x;

	MEM_SAFE_FREE(old_points);
	MEM_SAFE_FREE(marked);
}

/* (wrapper api) simplify stroke using Ramer-Douglas-Peucker algorithm */
void BKE_gpencil_simplify_stroke(bGPDlayer *UNUSED(gpl), bGPDstroke *gps, float factor)
{
	/* first create temp data and convert points to 2D */
	tbGPDspoint *points2d = MEM_mallocN(sizeof(tbGPDspoint) * gps->totpoints, "GP Stroke temp 2d points");

	gpencil_stroke_project_2d(gps->points, gps->totpoints, points2d);

	gpencil_rdp_stroke(gps, points2d, factor);

	MEM_SAFE_FREE(points2d);
}


/* simplify stroke using Ramer-Douglas-Peucker algorithm */
void BKE_gpencil_simplify_modifier(int UNUSED(id), GpencilSimplifyModifierData *mmd, Object *UNUSED(ob), bGPDlayer *gpl, bGPDstroke *gps)
{
	if (!is_stroke_affected_by_modifier(
	        mmd->layername, mmd->pass_index, 4, gpl, gps,
	        mmd->flag & GP_SIMPLIFY_INVERSE_LAYER, mmd->flag & GP_SIMPLIFY_INVERSE_PASS))
	{
		return;
	}

	/* first create temp data and convert points to 2D */
	tbGPDspoint *points2d = MEM_mallocN(sizeof(tbGPDspoint) * gps->totpoints, "GP Stroke temp 2d points");

	gpencil_stroke_project_2d(gps->points, gps->totpoints, points2d);

	gpencil_rdp_stroke(gps, points2d, mmd->factor);

	MEM_SAFE_FREE(points2d);
}

/* reset modifiers */
void BKE_gpencil_reset_modifiers(Object *ob)
{
	ModifierData *md;
	GpencilDupliModifierData *arr;

	for (md = ob->modifiers.first; md; md = md->next) {
		switch (md->type) {
			case eModifierType_GpencilDupli:
				arr = (GpencilDupliModifierData *) md;
				arr->rnd[0] = 1;
				break;
		}
	}
}

/* verify if exist geometry modifiers */
bool BKE_gpencil_has_geometry_modifiers(Object *ob)
{
	ModifierData *md;
	for (md = ob->modifiers.first; md; md = md->next) {
		const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
		
		if (mti->generateStrokes) {
			return true;
		}
			
		// XXX: Remove
		if (md->type == eModifierType_GpencilDupli) {
			return true;
		}
	}
	return false;
}

/* apply stroke modifiers */
void BKE_gpencil_stroke_modifiers(Object *ob, bGPDlayer *gpl, bGPDframe *UNUSED(gpf), bGPDstroke *gps)
{
	ModifierData *md;
	bGPdata *gpd = ob->data;
	bool is_edit = GPENCIL_ANY_EDIT_MODE(gpd);

	int id = 0; // XXX: Remove
	for (md = ob->modifiers.first; md; md = md->next) {
		if (((md->mode & eModifierMode_Realtime) && ((G.f & G_RENDER_OGL) == 0)) ||
		    ((md->mode & eModifierMode_Render) && (G.f & G_RENDER_OGL)))
		{
			const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
			
			if (((md->mode & eModifierMode_Editmode) == 0) && (is_edit)) {
				continue;
			}
			
			if (mti->deformStroke) {
				EvaluationContext eval_ctx = {0}; /* XXX */
				mti->deformStroke(md, &eval_ctx, ob, gpl, gps);
			}

			// XXX: The following lines need to all be converted to modifier callbacks...
			switch (md->type) {
					// Simplify Modifier
				case eModifierType_GpencilSimplify:
					BKE_gpencil_simplify_modifier(id, (GpencilSimplifyModifierData *)md, ob, gpl, gps);
					break;
					// Lattice
				case eModifierType_GpencilLattice:
					BKE_gpencil_lattice_modifier(id, (GpencilLatticeModifierData *)md, ob, gpl, gps);
					break;
			}
		}
		id++;
	}
}

/* apply stroke geometry modifiers */
void BKE_gpencil_geometry_modifiers(Object *ob, bGPDlayer *gpl, bGPDframe *gpf)
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
				EvaluationContext eval_ctx = {0}; /* XXX */
				mti->generateStrokes(md, &eval_ctx, ob, gpl, gpf, id);
			}

			// XXX: The following lines need to all be converted to modifier callbacks...
			switch (md->type) {
				// Array
				case eModifierType_GpencilDupli:
					BKE_gpencil_dupli_modifier(id, (GpencilDupliModifierData *)md, ob, gpl, gpf);
					break;
			}
		}
		id++;
	}
}
