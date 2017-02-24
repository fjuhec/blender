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

/** \file blender/editors/transform/transform_snap_object.c
 *  \ingroup edtransform
 */

#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <float.h>
#include <stdio.h>

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_kdopbvh.h"
#include "BLI_memarena.h"
#include "BLI_ghash.h"
#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_utildefines.h"

#include "DNA_armature_types.h"
#include "DNA_curve_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"

#include "BKE_DerivedMesh.h"
#include "BKE_object.h"
#include "BKE_anim.h"  /* for duplis */
#include "BKE_editmesh.h"
#include "BKE_main.h"
#include "BKE_tracking.h"

#include "ED_transform.h"
#include "ED_transform_snap_object_context.h"
#include "ED_view3d.h"
#include "ED_armature.h"

#include "transform.h"

enum eViewProj {
	VIEW_PROJ_NONE = -1,
	VIEW_PROJ_ORTHO = 0,
	VIEW_PROJ_PERSP = -1,
};

/* Flags related to occlusion planes */
enum {
	BEHIND_A_PLANE      = 0,
	ISECT_CLIP_PLANE    = 1 << 0,
	IN_FRONT_ALL_PLANES = 1 << 1,
	TEST_RANGE_DEPTH    = 1 << 2,
};

typedef struct SnapData {
	float ray_origin[3];
	float ray_start[3];
	float ray_dir[3];
	float pmat[4][4]; /* perspective matrix */

	float mval[2];
	float win_half[2];/* win x and y */
	float depth_range[2];/* zNear and zFar of Perspective View */

	short snap_to_flag;
	enum eViewProj view_proj;
	bool test_occlusion;

	struct {
		float(*plane)[4];
		short plane_num;
	} clip;
} SnapData;

typedef struct SnapObjectData {
	enum {
		SNAP_MESH = 1,
		SNAP_EDIT_MESH,
	} type;
} SnapObjectData;

typedef struct SnapObjectData_Mesh {
	SnapObjectData sd;
	BVHTree *bvh_trees[2]; /* bvhtrees of loose verts/edges */
	BVHTreeFromMesh treedata;
	bool has_loose_vert;
	bool has_loose_edge;
	bool has_looptris;

} SnapObjectData_Mesh;

typedef struct SnapObjectData_EditMesh {
	SnapObjectData sd;
	BVHTreeFromEditMesh *bvh_trees[3];

} SnapObjectData_EditMesh;

struct SnapObjectContext {
	Main *bmain;
	Scene *scene;
	int flag;

	/* Optional: when performing screen-space projection.
	 * otherwise this doesn't take viewport into account. */
	bool use_v3d;
	struct {
		const struct View3D *v3d;
		const struct ARegion *ar;
	} v3d_data;


	/* Object -> SnapObjectData map */
	struct {
		GHash *object_map;
		MemArena *mem_arena;
	} cache;

	/* Filter data, returns true to check this value */
	struct {
		struct {
			bool (*test_vert_fn)(BMVert *, void *user_data);
			bool (*test_edge_fn)(BMEdge *, void *user_data);
			bool (*test_face_fn)(BMFace *, void *user_data);
			void *user_data;
		} edit_mesh;
	} callbacks;

};


/* -------------------------------------------------------------------- */

/** \name Support for storing all depths, not just the first (raycast 'all')
 *
 * This uses a list of #SnapObjectHitDepth structs.
 *
 * \{ */

/* Store all ray-hits */
struct RayCastAll_Data {
	void *bvhdata;

	/* internal vars for adding depths */
	BVHTree_RayCastCallback raycast_callback;

	float(*obmat)[4];
	float(*timat)[3];

	float len_diff;
	float local_scale;

	Object *ob;
	unsigned int ob_uuid;

	/* output data */
	ListBase *hit_list;
	bool retval;
};

static struct SnapObjectHitDepth *hit_depth_create(
        const float depth, const float co[3], const float no[3], int index,
        Object *ob, const float obmat[4][4], unsigned int ob_uuid)
{
	struct SnapObjectHitDepth *hit = MEM_mallocN(sizeof(*hit), __func__);

	hit->depth = depth;
	copy_v3_v3(hit->co, co);
	copy_v3_v3(hit->no, no);
	hit->index = index;

	hit->ob = ob;
	copy_m4_m4(hit->obmat, (float(*)[4])obmat);
	hit->ob_uuid = ob_uuid;

	return hit;
}

static int hit_depth_cmp_cb(const void *arg1, const void *arg2)
{
	const struct SnapObjectHitDepth *h1 = arg1;
	const struct SnapObjectHitDepth *h2 = arg2;
	int val = 0;

	if (h1->depth < h2->depth) {
		val = -1;
	}
	else if (h1->depth > h2->depth) {
		val = 1;
	}

	return val;
}

static void raycast_all_cb(void *userdata, int index, const BVHTreeRay *ray, BVHTreeRayHit *hit)
{
	struct RayCastAll_Data *data = userdata;
	data->raycast_callback(data->bvhdata, index, ray, hit);
	if (hit->index != -1) {
		/* get all values in worldspace */
		float location[3], normal[3];
		float depth;

		/* worldspace location */
		mul_v3_m4v3(location, data->obmat, hit->co);
		depth = (hit->dist + data->len_diff) / data->local_scale;

		/* worldspace normal */
		copy_v3_v3(normal, hit->no);
		mul_m3_v3(data->timat, normal);
		normalize_v3(normal);

		/* currently unused, and causes issues when looptri's haven't been calculated.
		 * since theres some overhead in ensuring this data is valid, it may need to be optional. */
#if 0
		if (data->dm) {
			hit->index = dm_looptri_to_poly_index(data->dm, &data->dm_looptri[hit->index]);
		}
#endif

		struct SnapObjectHitDepth *hit_item = hit_depth_create(
		        depth, location, normal, hit->index,
		        data->ob, data->obmat, data->ob_uuid);
		BLI_addtail(data->hit_list, hit_item);
	}
}

/** \} */


/* -------------------------------------------------------------------- */

/** \Common utilities
 * \{ */


MINLINE float depth_get(const float co[3], const float ray_start[3], const float ray_dir[3])
{
	float dvec[3];
	sub_v3_v3v3(dvec, co, ray_start);
	return dot_v3v3(dvec, ray_dir);
}

MINLINE void aabb_get_near_far_from_plane(
        const float plane_no[3],
        const float bbmin[3], const float bbmax[3],
        float bb_near[3], float bb_afar[3])
{
	if (plane_no[0] < 0) {
		bb_near[0] = bbmax[0];
		bb_afar[0] = bbmin[0];
	}
	else {
		bb_near[0] = bbmin[0];
		bb_afar[0] = bbmax[0];
	}
	if (plane_no[1] < 0) {
		bb_near[1] = bbmax[1];
		bb_afar[1] = bbmin[1];
	}
	else {
		bb_near[1] = bbmin[1];
		bb_afar[1] = bbmax[1];
	}
	if (plane_no[2] < 0) {
		bb_near[2] = bbmax[2];
		bb_afar[2] = bbmin[2];
	}
	else {
		bb_near[2] = bbmin[2];
		bb_afar[2] = bbmax[2];
	}
}

/**
 * Check if a point is in front all planes.
 * (Similar to `isect_point_planes_v3` but checks the opposite side)
 */
MINLINE bool snp_is_in_front_all_planes(
        const float (*planes)[4], const short totplane, const float p[3])
{
	for (int i = 0; i < totplane; i++) {
		if (plane_point_side_v3(planes[i], p) < 0.0f) {
			return false;
		}
	}

	return true;
}

static void *snp_clipplanes_calc_local(
        const float(*clip)[4], const short clip_num, float obmat[4][4])
{
	BLI_assert(clip != NULL);
	float tobmat[4][4];
	transpose_m4_m4(tobmat, obmat);

	float(*clip_local)[4] = MEM_mallocN(sizeof(*clip_local) * clip_num, __func__);
	for (short i = 0; i < clip_num; i++) {
		mul_v4_m4v4(clip_local[i], tobmat, clip[i]);

//		const float d = len_v3(clip_local[i]);
//		mul_v4_fl(clip_local[i], 1 / d); /* normalize plane */
	}

	return clip_local;
}

/* Relative Snap to Faces */
typedef struct SnapRayCastLocalData {
	float ray_start[3];
	float ray_dir[3];
	float scale; /* local scale in normal direction */
	float depth;
	float len_diff;

	float imat[4][4];
} SnapRayCastLocalData;

/* Relative Snap to Edges or Verts */
typedef struct SnapNearestLocalData {
	float ray_orig[3];
	float ray_dir[3];
	float ray_inv_dir[3];
	float pmat[4][4]; /* perspective matrix */

	float imat[4][4];

	struct {
		float(*plane)[4];
		short plane_num;
	} clip;
} SnapNearestLocalData;

static void snp_raycast_local_data_get(
        SnapRayCastLocalData *localdata, SnapData *snpdt,
        float obmat[4][4], float depth)
{
	copy_v3_v3(localdata->ray_start, snpdt->ray_start);
	copy_v3_v3(localdata->ray_dir, snpdt->ray_dir);

	invert_m4_m4(localdata->imat, obmat);

	mul_m4_v3(localdata->imat, localdata->ray_start);
	mul_mat3_m4_v3(localdata->imat, localdata->ray_dir);

	/* local scale in normal direction */
	localdata->scale = normalize_v3(localdata->ray_dir);
	localdata->depth = depth;
	if (localdata->depth != BVH_RAYCAST_DIST_MAX) {
		localdata->depth *= localdata->scale;
	}

	localdata->len_diff = 0.0f;
}

static void snp_nearest_local_data_get(
        SnapNearestLocalData *localdata, SnapData *snpdt,
        float obmat[4][4])
{
	copy_v3_v3(localdata->ray_orig, snpdt->ray_origin);
	copy_v3_v3(localdata->ray_dir, snpdt->ray_dir);
	mul_m4_m4m4(localdata->pmat, snpdt->pmat, obmat);

	invert_m4_m4(localdata->imat, obmat);

	mul_m4_v3(localdata->imat, localdata->ray_orig);
	mul_mat3_m4_v3(localdata->imat, localdata->ray_dir);

	for (int i = 0; i < 3; i++) {
		localdata->ray_inv_dir[i] =
		        (localdata->ray_dir[i] != 0.0f) ?
		        (1.0f / localdata->ray_dir[i]) : FLT_MAX;
	}

	if (snpdt->clip.plane) {
		localdata->clip.plane = snp_clipplanes_calc_local(
		        snpdt->clip.plane, snpdt->clip.plane_num, obmat);
		localdata->clip.plane_num = snpdt->clip.plane_num;
	}
	else {
		localdata->clip.plane = NULL;
		localdata->clip.plane_num = 0;
	}
}

static void snp_free_nearestdata(SnapNearestLocalData *localdata) {
	if (localdata->clip.plane) {
		MEM_freeN(localdata->clip.plane);
	}
}

/**
 * Generates a struct with the immutable parameters that will be used on all objects.
 *
 * \param snap_to: Element to snap, Vertice, Edge or Face.
 * \param view_proj: ORTHO or PERSP.
 * Currently only works one at a time, but can eventually operate as flag.
 *
 * \param mval: Mouse coords.
 */
static bool snapdata_init_v3d(
        SnapData *snpdt, SnapObjectContext *sctx,
        const unsigned short snap_to_flag, const float mval[2], float *depth)
{
	if (!sctx->use_v3d) {
		return false;
	}

	snpdt->snap_to_flag = snap_to_flag;

	const ARegion *ar = sctx->v3d_data.ar;
	RegionView3D *rv3d = (RegionView3D *)ar->regiondata;

	copy_v2_v2(snpdt->mval, mval);

	ED_view3d_win_to_origin(ar, snpdt->mval, snpdt->ray_origin);
	ED_view3d_win_to_vector(ar, snpdt->mval, snpdt->ray_dir);

	ED_view3d_clip_range_get(
	        sctx->v3d_data.v3d, rv3d, &snpdt->depth_range[0], &snpdt->depth_range[1], false);

	madd_v3_v3v3fl(snpdt->ray_start, snpdt->ray_origin, snpdt->ray_dir, snpdt->depth_range[0]);

	if (rv3d->rflag & RV3D_CLIPPING) {
		/* Referencing or copying? */
		snpdt->clip.plane = MEM_mallocN( 4 * sizeof(*snpdt->clip.plane), __func__);
		memcpy(snpdt->clip.plane, rv3d->clip, 4 * sizeof(*snpdt->clip.plane));
		snpdt->clip.plane_num = 4;

		float dummy_ray_end[3];
		madd_v3_v3v3fl(dummy_ray_end, snpdt->ray_origin, snpdt->ray_dir, snpdt->depth_range[1]);

		if (!clip_segment_v3_plane_n(
		        snpdt->ray_start, dummy_ray_end, snpdt->clip.plane, snpdt->clip.plane_num,
		        snpdt->ray_start, dummy_ray_end))
		{
			return false;
		}

		*depth = depth_get(dummy_ray_end, snpdt->ray_start, snpdt->ray_dir);
	}
	else {
		snpdt->clip.plane = NULL;
		snpdt->clip.plane_num = 0;
	}

	copy_m4_m4(snpdt->pmat, rv3d->persmat);
	snpdt->win_half[0] = ar->winx / 2;
	snpdt->win_half[1] = ar->winy / 2;

	snpdt->view_proj = rv3d->is_persp ? VIEW_PROJ_PERSP : VIEW_PROJ_ORTHO;
	snpdt->test_occlusion = true;

	return true;
}

/**
 * Generates a struct with the immutable parameters that will be used on all objects.
 * Used only in ray_cast (snap to faces)
 * (ray-casting is handled without any projection matrix correction.)
 *
 * \param ray_origin: ray_start before being moved toward the ray_normal at the distance from vew3d clip_min.
 * \param ray_start: ray_origin moved for the start clipping plane (clip_min).
 * \param ray_direction: Unit length direction of the ray.
 * \param depth_range: distances of clipe plane min and clip plane max;
 */
static bool snapdata_init_ray(
        SnapData *snpdt,
        const float ray_start[3], const float ray_normal[3])
{
	snpdt->snap_to_flag = SCE_SELECT_FACE;

	copy_v3_v3(snpdt->ray_origin, ray_start);
	copy_v3_v3(snpdt->ray_start, ray_start);
	copy_v3_v3(snpdt->ray_dir, ray_normal);
//	snpdt->depth_range[0] = 0.0f;
//	snpdt->depth_range[1] = BVH_RAYCAST_DIST_MAX;

	snpdt->view_proj = VIEW_PROJ_NONE;
	snpdt->test_occlusion = false;

	return true;
}

static bool snap_point_v3(
        const float depth_range[2], const float mval[2], const float co[3],
        float pmat[4][4], const float win_half[2], const bool is_persp,
        short flag, float(*planes)[4], int totplane,
        float *dist_px_sq, float r_co[3])
{
	if (flag & ISECT_CLIP_PLANE) {
		if (!snp_is_in_front_all_planes(planes, totplane, co)) {
//			printf("snap_point behind clip plane\n");
			return false;
		}
	}

	float depth;
	if (is_persp) {
		//Z_depth =dot_m4_v3_row_z(pmat, co) / mul_project_m4_v3_zfac(pmat, co);
		depth = mul_project_m4_v3_zfac(pmat, co);
		if (flag & TEST_RANGE_DEPTH) {
			if (depth < depth_range[0] || depth > depth_range[1]) {
//				printf("snap_point out of the depth range\n");
				return false;
			}
		}
	}
	else if (flag & TEST_RANGE_DEPTH) {
		depth = dot_m4_v3_row_z(pmat, co);
		if (fabsf(depth) > 1.0f) {
//			printf("snap_point out of the depth range\n");
			return false;
		}
	}

	float co2d[2] = {
		(dot_m4_v3_row_x(pmat, co) + pmat[3][0]),
		(dot_m4_v3_row_y(pmat, co) + pmat[3][1]),
	};

	if (is_persp) {
		mul_v2_fl(co2d, 1 / depth);
	}

	co2d[0] += 1.0f;
	co2d[1] += 1.0f;
	co2d[0] *= win_half[0];
	co2d[1] *= win_half[1];

	const float dist_sq = len_squared_v2v2(mval, co2d);
	if (dist_sq <= *dist_px_sq) {
		copy_v3_v3(r_co, co);
		*dist_px_sq = dist_sq;
		return true;
	}
	return false;
}

static bool snap_segment_v3v3(
        short snap_to, SnapNearestLocalData *localdata,
        const float depth_range[2], const float mval[2],
        const float win_half[2], const bool is_persp,
        short flag,
        const float va[3], const float vb[3],
        float *dist_px_sq, float r_co[3])
{
	float tmp_co[3], lambda, depth;
	bool ret = false;

	if (snap_to & SCE_SELECT_EDGE) {
		dist_squared_ray_to_seg_v3(localdata->ray_orig, localdata->ray_dir, va, vb, tmp_co, &lambda, &depth);

		if ((snap_to & SCE_SELECT_VERTEX) && (lambda < 0.25f || 0.75f < lambda)) {
			/* Use rv3d clip segments: `rv3d->clip` */
			ret = snap_point_v3(
			        depth_range, mval, lambda < 0.5f ? va : vb, localdata->pmat, win_half, is_persp,
			        flag, localdata->clip.plane, localdata->clip.plane_num, dist_px_sq, r_co);
		}

		if (!ret) {
			ret = snap_point_v3(
		        depth_range, mval, tmp_co, localdata->pmat, win_half, is_persp,
		        flag, localdata->clip.plane, localdata->clip.plane_num, dist_px_sq, r_co);
		}
	}
	else {
		ret = snap_point_v3(
		        depth_range, mval, va, localdata->pmat, win_half, is_persp,
		        flag, localdata->clip.plane, localdata->clip.plane_num, dist_px_sq, r_co);
		ret |= snap_point_v3(
		        depth_range, mval, vb, localdata->pmat, win_half, is_persp,
		        flag, localdata->clip.plane, localdata->clip.plane_num, dist_px_sq, r_co);
	}

	return ret;
}

/**
 * Check if a AABB is:
 * - BEHIND_A_PLANE     (0),
 * - ISECT_CLIP_PLANE   (1),
 * - IN_FRONT_ALL_PLANES(2)
 */
static short snp_isect_aabb_planes_v3(
	float(*planes)[4], int totplane, const float bbmin[3], const float bbmax[3])
{
	short ret = IN_FRONT_ALL_PLANES;

	float bb_near[3], bb_afar[3];
	for (int i = 0; i < totplane; i++) {
		aabb_get_near_far_from_plane(planes[i], bbmin, bbmax, bb_near, bb_afar);
		if (plane_point_side_v3(planes[i], bb_afar) < 0.0f) {
			return BEHIND_A_PLANE;
		}
		else if ((ret != ISECT_CLIP_PLANE) && (plane_point_side_v3(planes[i], bb_near) < 0.0f)) {
			ret = ISECT_CLIP_PLANE;
		}
	}

	return ret;
}

typedef struct SnapNearest2dPrecalc {
	SnapNearestLocalData *local;

	bool is_persp;
	float win_half[2];

	float mval[2];

	float depth_range[2];

} SnapNearest2dPrecalc;

static void snp_dist_squared_to_projected_aabb_precalc(
        struct SnapNearest2dPrecalc *nearest_precalc, SnapNearestLocalData *localdata,
        SnapData *snpdt)
{
	nearest_precalc->local = localdata;
//	memcpy(&nearest_precalc->local, localdata, sizeof(nearest_precalc->local));

	nearest_precalc->is_persp = snpdt->view_proj == VIEW_PROJ_PERSP;
	copy_v2_v2(nearest_precalc->win_half, snpdt->win_half);
	copy_v2_v2(nearest_precalc->depth_range, snpdt->depth_range);

	copy_v2_v2(nearest_precalc->mval, snpdt->mval);
}

/* Returns the distance from a 2d coordinate to a BoundBox (Projected) */
static float snp_dist_squared_to_projected_aabb(
        struct SnapNearest2dPrecalc *data,
        const float bbmin[3], const float bbmax[3],
        short *flag, bool r_axis_closest[3])
{
	float bb_near[3], bb_afar[3];
	aabb_get_near_far_from_plane(
	        data->local->ray_inv_dir, bbmin, bbmax, bb_near, bb_afar);

	/* ISECT_CLIP_PLANE can replace this ? */
	if (*flag & TEST_RANGE_DEPTH) {
		/* Test if the entire AABB is behind us */
		float depth_near;
		float depth_afar;
		if (data->is_persp) {
			depth_near = mul_project_m4_v3_zfac(data->local->pmat, bb_near);
			depth_afar = mul_project_m4_v3_zfac(data->local->pmat, bb_afar);
			if (depth_afar < data->depth_range[0]) {
//				printf("Is behind near clip plane\n");
				return FLT_MAX;
			}
			if (depth_near > data->depth_range[1]) {
//				printf("Is after far clip plane\n");
				return FLT_MAX;
			}
			if (data->depth_range[0] < depth_near && depth_afar < data->depth_range[1]) {
				*flag &= ~TEST_RANGE_DEPTH;
			}
		}
		else {
			depth_near = dot_m4_v3_row_z(data->local->pmat, bb_near);
			depth_afar = dot_m4_v3_row_z(data->local->pmat, bb_afar);
			if (depth_afar < -1.0f) {
//				printf("Is behind near clip plane\n");
				return FLT_MAX;
			}
			if (depth_near > 1.0f) {
//				printf("Is after far clip plane\n");
				return FLT_MAX;
			}
			if (-1.0f < depth_near && depth_afar < 1.0f) {
				*flag &= ~TEST_RANGE_DEPTH;
			}
		}
	}

	const float tmin[3] = {
		(bb_near[0] - data->local->ray_orig[0]) * data->local->ray_inv_dir[0],
		(bb_near[1] - data->local->ray_orig[1]) * data->local->ray_inv_dir[1],
		(bb_near[2] - data->local->ray_orig[2]) * data->local->ray_inv_dir[2],
	};
	const float tmax[3] = {
		(bb_afar[0] - data->local->ray_orig[0]) * data->local->ray_inv_dir[0],
		(bb_afar[1] - data->local->ray_orig[1]) * data->local->ray_inv_dir[1],
		(bb_afar[2] - data->local->ray_orig[2]) * data->local->ray_inv_dir[2],
	};
	/* `va` and `vb` are the coordinates of the AABB edge closest to the ray */
	float va[3], vb[3];
	/* `rtmin` and `rtmax` are the minimum and maximum distances of the ray hits on the AABB */
	float rtmin, rtmax;
	int main_axis;

	if ((tmax[0] <= tmax[1]) && (tmax[0] <= tmax[2])) {
		rtmax = tmax[0];
		va[0] = vb[0] = bb_afar[0];
		main_axis = 3;
		r_axis_closest[0] = data->local->ray_inv_dir[0] < 0;
	}
	else if ((tmax[1] <= tmax[0]) && (tmax[1] <= tmax[2])) {
		rtmax = tmax[1];
		va[1] = vb[1] = bb_afar[1];
		main_axis = 2;
		r_axis_closest[1] = data->local->ray_inv_dir[1] < 0;
	}
	else {
		rtmax = tmax[2];
		va[2] = vb[2] = bb_afar[2];
		main_axis = 1;
		r_axis_closest[2] = data->local->ray_inv_dir[2] < 0;
	}

	if ((tmin[0] >= tmin[1]) && (tmin[0] >= tmin[2])) {
		rtmin = tmin[0];
		va[0] = vb[0] = bb_near[0];
		main_axis -= 3;
		r_axis_closest[0] = data->local->ray_inv_dir[0] >= 0;
	}
	else if ((tmin[1] >= tmin[0]) && (tmin[1] >= tmin[2])) {
		rtmin = tmin[1];
		va[1] = vb[1] = bb_near[1];
		main_axis -= 1;
		r_axis_closest[1] = data->local->ray_inv_dir[1] >= 0;
	}
	else {
		rtmin = tmin[2];
		va[2] = vb[2] = bb_near[2];
		main_axis -= 2;
		r_axis_closest[2] = data->local->ray_inv_dir[2] >= 0;
	}
	if (main_axis < 0) {
		main_axis += 3;
	}

	/* if rtmin < rtmax, ray intersect `AABB` */
	if (rtmin <= rtmax) {
		const float proj = rtmin * data->local->ray_dir[main_axis];
		r_axis_closest[main_axis] = (proj - va[main_axis]) < (vb[main_axis] - proj);
		return 0.0f;
	}
	if (data->local->ray_inv_dir[main_axis] < 0) {
		va[main_axis] = bb_afar[main_axis];
		vb[main_axis] = bb_near[main_axis];
	}
	else {
		va[main_axis] = bb_near[main_axis];
		vb[main_axis] = bb_afar[main_axis];
	}
	float scale = fabsf(bb_afar[main_axis] - bb_near[main_axis]);

	float va2d[2] = {
		(dot_m4_v3_row_x(data->local->pmat, va) + data->local->pmat[3][0]),
		(dot_m4_v3_row_y(data->local->pmat, va) + data->local->pmat[3][1]),
	};
	float vb2d[2] = {
		(va2d[0] + data->local->pmat[main_axis][0] * scale),
		(va2d[1] + data->local->pmat[main_axis][1] * scale),
	};

	if (data->is_persp) {
		float depth_a = mul_project_m4_v3_zfac(data->local->pmat, va);
		float depth_b = depth_a + data->local->pmat[main_axis][3] * scale;
		va2d[0] /= depth_a;
		va2d[1] /= depth_a;
		vb2d[0] /= depth_b;
		vb2d[1] /= depth_b;
	}

	va2d[0] += 1.0f;
	va2d[1] += 1.0f;
	vb2d[0] += 1.0f;
	vb2d[1] += 1.0f;

	va2d[0] *= data->win_half[0];
	va2d[1] *= data->win_half[1];
	vb2d[0] *= data->win_half[0];
	vb2d[1] *= data->win_half[1];

	//float dvec[2], edge[2], rdist;
	//sub_v2_v2v2(dvec, data->mval, va2d);
	//sub_v2_v2v2(edge, vb2d, va2d);
	float rdist;
	short dvec[2] = {data->mval[0] - va2d[0], data->mval[1] - va2d[1]};
	short edge[2] = {vb2d[0] - va2d[0], vb2d[1] - va2d[1]};
	float lambda = dvec[0] * edge[0] + dvec[1] * edge[1];
	if (lambda != 0.0f) {
		lambda /= edge[0] * edge[0] + edge[1] * edge[1];
		if (lambda <= 0.0f) {
			rdist = len_squared_v2v2(data->mval, va2d);
			r_axis_closest[main_axis] = true;
		}
		else if (lambda >= 1.0f) {
			rdist = len_squared_v2v2(data->mval, vb2d);
			r_axis_closest[main_axis] = false;
		}
		else {
			va2d[0] += edge[0] * lambda;
			va2d[1] += edge[1] * lambda;
			rdist = len_squared_v2v2(data->mval, va2d);
			r_axis_closest[main_axis] = lambda < 0.5f;
		}
	}
	else {
		rdist = len_squared_v2v2(data->mval, va2d);
	}
	return rdist;
}

static bool snp_snap_boundbox_nearest_test(
        SnapData *snpdt, SnapNearestLocalData *localdata, BoundBox *bb, float dist_px)
{
	struct SnapNearest2dPrecalc data;

	snp_dist_squared_to_projected_aabb_precalc(&data, localdata, snpdt);

	if (localdata->clip.plane) {
		if (snp_isect_aabb_planes_v3(
			        data.local->clip.plane,
			        data.local->clip.plane_num,
			        bb->vec[0], bb->vec[6]) == BEHIND_A_PLANE)
		{
			return false;
		}
	}

	short flag = TEST_RANGE_DEPTH;
	bool dummy[3] = {true, true, true};
	return snp_dist_squared_to_projected_aabb(
	        &data, bb->vec[0], bb->vec[6], &flag, dummy) < SQUARE(dist_px);
}

static bool snp_snap_boundbox_raycast_test(
        SnapData *snpdt, SnapRayCastLocalData *localdata, BoundBox *bb) {
{}
	/* was BKE_boundbox_ray_hit_check, see: cf6ca226fa58 */
	return isect_ray_aabb_v3_simple(
	        localdata->ray_start, localdata->ray_dir, bb->vec[0], bb->vec[6], NULL, NULL);
}

static float dist_aabb_to_plane(
        const float bbmin[3], const float bbmax[3],
        const float plane_co[3], const float plane_no[3])
{
	const float bb_near[3] = {
		(plane_no[0] < 0) ? bbmax[0] : bbmin[0],
		(plane_no[1] < 0) ? bbmax[1] : bbmin[1],
		(plane_no[2] < 0) ? bbmax[2] : bbmin[2],
	};
	return depth_get(bb_near, plane_co, plane_no);
}

/** \} */


/* -------------------------------------------------------------------- */

/** \Utilities for DerivedMeshes and EditMeshes
* \{ */

static void object_dm_final_get(Scene *scn, Object *ob, DerivedMesh **dm)
{
	/* in this case we want the mesh from the editmesh, avoids stale data. see: T45978.
	 * still set the 'em' to NULL, since we only want the 'dm'. */
	BMEditMesh *em = BKE_editmesh_from_object(ob);
	if (em) {
		editbmesh_get_derived_cage_and_final(scn, ob, em, CD_MASK_BAREMESH, dm);
	}
	else {
		*dm = mesh_get_derived_final(scn, ob, CD_MASK_BAREMESH);
	}
}

static void dm_get_vert_co_cb(const int index, const float **co, const BVHTreeFromMesh *data)
{
	*co = data->vert[index].co;
}

static void em_get_vert_co_cb(const int index, const float **co, const BMEditMesh *data)
{
	BMVert *eve = BM_vert_at_index(data->bm, index);
	*co = eve->co;
}

#if 0
static void dm_get_edge_co_cb(const int index, const float *co[2], const BVHTreeFromMesh *data)
{
	const MVert *vert = data->vert;
	const MEdge *edge = &data->edge[index];

	co[0] = vert[edge->v1].co;
	co[1] = vert[edge->v2].co;
}

static void em_get_edge_co_cb(const int index, const float *co[2], const BMEditMesh *data)
{
	BMEdge *eed = BM_edge_at_index(data->bm, index);

	co[0] = eed->v1->co;
	co[1] = eed->v2->co;
}
#endif

static void dm_get_edge_verts_cb(const int index, int v_index[2], const BVHTreeFromMesh *data)
{
	const MVert *vert = data->vert;
	const MEdge *edge = &data->edge[index];

	v_index[0] = edge->v1;
	v_index[1] = edge->v2;
}

static void em_get_edge_verts_cb(const int index, int v_index[2], const BMEditMesh *data)
{
	BMEdge *eed = BM_edge_at_index(data->bm, index);

	v_index[0] = eed->v1->head.index;
	v_index[1] = eed->v2->head.index;
}

static void dm_get_tri_verts_cb(const int index, int v_index[3], const BVHTreeFromMesh *data)
{
	const MLoop *loop = data->loop;
	const MLoopTri *looptri = &data->looptri[index];

	v_index[0] = loop[looptri->tri[0]].v;
	v_index[1] = loop[looptri->tri[1]].v;
	v_index[2] = loop[looptri->tri[2]].v;
}

#if 0
static void em_get_tri_verts_cb(const int index, int v_index[3], const BMEditMesh *data)
{
	const BMLoop **ltri = (const BMLoop **)data->looptris[index];

	v_index[0] = ltri[0]->v->head.index;
	v_index[1] = ltri[1]->v->head.index;
	v_index[2] = ltri[2]->v->head.index;
}
#endif

static void dm_get_tri_edges_cb(const int index, int v_index[3], const BVHTreeFromMesh *data)
{
	const MEdge *medge = data->edge;
	const MLoop *mloop = data->loop;
	const MLoopTri *lt = &data->looptri[index];
	for (int j = 2, j_next = 0; j_next < 3; j = j_next++) {
		const MEdge *ed = &medge[mloop[lt->tri[j]].e];
		unsigned int tri_edge[2] = {mloop[lt->tri[j]].v, mloop[lt->tri[j_next]].v};
		if (ELEM(ed->v1, tri_edge[0], tri_edge[1]) &&
			ELEM(ed->v2, tri_edge[0], tri_edge[1]))
		{
			//printf("real edge found\n");
			v_index[j] = mloop[lt->tri[j]].e;
		}
		else
			v_index[j] = -1;
	}
}

#if 0
static void em_get_tri_edges_cb(const int index, int v_index[3], const BMEditMesh *data)
{
	const BMLoop **ltri = (const BMLoop **)data->looptris[index];

	v_index[0] = ltri[0]->e->head.index;
	v_index[1] = ltri[1]->e->head.index;
	v_index[2] = ltri[2]->e->head.index;
	printf("tri: %d, %d, %d\n", v_index[0], v_index[1], v_index[2]);
}
#endif

static void dm_copy_vert_no_cb(const int index, float r_no[3], const BVHTreeFromMesh *data)
{
	const MVert *vert = data->vert + index;

	normal_short_to_float_v3(r_no, vert->no);
}

static void em_copy_vert_no_cb(const int index, float r_no[3], const BMEditMesh *data)
{
	BMVert *eve = BM_vert_at_index(data->bm, index);

	copy_v3_v3(r_no, eve->no);
}

static BVHTree *snp_bvhtree_from_mesh_loose_verts(
        DerivedMesh *dm, const MEdge *medge, const MVert *mvert)
{
	BVHTree *tree = bvhcache_thread_safe_find(dm->bvhCache, BVHTREE_FROM_LOOSE_VERTS);

	if (tree == NULL) { /* Not cached */
		int verts_num = dm->getNumVerts(dm);
		int edges_num = dm->getNumEdges(dm);

		BLI_bitmap *loose_verts_mask = BLI_BITMAP_NEW(verts_num, __func__);
		BLI_BITMAP_SET_ALL(loose_verts_mask, true, verts_num);

		const MEdge *e = medge;
		int numLinkedVerts = 0;
		for (int i = 0; i < edges_num; i++, e++) {
			if (BLI_BITMAP_TEST(loose_verts_mask, e->v1)) {
				BLI_BITMAP_DISABLE(loose_verts_mask, e->v1);
				numLinkedVerts++;
			}
			if (BLI_BITMAP_TEST(loose_verts_mask, e->v2)) {
				BLI_BITMAP_DISABLE(loose_verts_mask, e->v2);
				numLinkedVerts++;
			}
		}

		BVHTreeFromMesh dummy_treedata;
		tree = bvhtree_from_mesh_verts_ex(
		        &dummy_treedata, mvert, verts_num, false,
		        loose_verts_mask, (verts_num - numLinkedVerts), 0.0f, 2, 6);

		if (tree) {
			bvhcache_thread_safe_insert(&dm->bvhCache, tree, BVHTREE_FROM_LOOSE_VERTS);
		}

		MEM_freeN(loose_verts_mask);
	}

	return tree;
}

static BVHTree *snp_bvhtree_from_mesh_loose_edges(
        DerivedMesh *dm, const MEdge *medge, const MVert *mvert)
{
	BVHTree *tree = bvhcache_thread_safe_find(dm->bvhCache, BVHTREE_FROM_LOOSE_EDGES);

	if (tree == NULL) { /* Not cached */
		int edges_num = dm->getNumEdges(dm);

		BLI_bitmap *loose_edges_mask = BLI_BITMAP_NEW(edges_num, __func__);

		/* another option to get the `numLooseEdges` would be
		* `dm->drawObject->tot_loose_edge_drawn` */
		int numLooseEdges = 0;
		const MEdge *e = medge;
		for (int i = 0; i < edges_num; i++, e++) {
			if (e->flag & ME_LOOSEEDGE) {
				BLI_BITMAP_ENABLE(loose_edges_mask, i);
				numLooseEdges++;
			}
			else {
				BLI_BITMAP_DISABLE(loose_edges_mask, i);
			}
		}
		BVHTreeFromMesh dummy_treedata;
		tree = bvhtree_from_mesh_edges_ex(
		        &dummy_treedata, mvert, false,
		        medge, edges_num, false,
		        loose_edges_mask, numLooseEdges, 0.0f, 2, 6);

		if (tree) {
			bvhcache_thread_safe_insert(&dm->bvhCache, tree, BVHTREE_FROM_LOOSE_EDGES);
		}

		MEM_freeN(loose_edges_mask);
	}
	return tree;
}

#if 0
static int dm_looptri_to_poly_index(DerivedMesh *dm, const MLoopTri *lt)
{
	const int *index_mp_to_orig = dm->getPolyDataArray(dm, CD_ORIGINDEX);
	return index_mp_to_orig ? index_mp_to_orig[lt->poly] : lt->poly;
}
#endif

/** \} */


/* -------------------------------------------------------------------- */

/** \Walk DFS
 * \{ */

typedef void (*Nearest2DGetVertCoCallback)(const int index, const float **co, void *data);
//typedef void (*Nearest2DGetEdgeCoCallback)(const int index, const float *co[2], void *data);
typedef void (*Nearest2DGetEdgeVertsCallback)(const int index, int v_index[2], void *data);
typedef void (*Nearest2DGetTriVertsCallback)(const int index, int v_index[3], void *data);
typedef void (*Nearest2DGetTriEdgesCallback)(const int index, int e_index[3], void *data); /* Equal the previous one */
typedef void (*Nearest2DCopyVertNoCallback)(const int index, float r_no[3], void *data);

typedef struct Nearest2dUserData {
	struct SnapNearest2dPrecalc data_precalc;

	float dist_px_sq;

	bool r_axis_closest[3];

	short snap_to;
	void *userdata;
	Nearest2DGetVertCoCallback get_vert_co;
//	Nearest2DGetEdgeCoCallback get_edge_co;
	Nearest2DGetEdgeVertsCallback get_edge_verts_index;
	Nearest2DGetTriVertsCallback get_tri_verts_index;
	Nearest2DGetTriEdgesCallback get_tri_edges_index;
	Nearest2DCopyVertNoCallback copy_vert_no;

	int edge_index;
	int vert_index;
	float co[3];
	float no[3];
} Nearest2dUserData;


static bool cb_walk_parent_snap_project(
        const BVHTreeAxisRange *bounds, short *parent_flag, void *user_data)
{
	Nearest2dUserData *data = user_data;
	float bbmin[3] = {bounds[0].min, bounds[1].min, bounds[2].min};
	float bbmax[3] = {bounds[0].max, bounds[1].max, bounds[2].max};

	/* Use rv3d clip segments: `rv3d->clip` */
	if (*parent_flag & ISECT_CLIP_PLANE) {
		short ret_flag = snp_isect_aabb_planes_v3(
		        data->data_precalc.local->clip.plane,
		        data->data_precalc.local->clip.plane_num,
		        bbmin, bbmax);

		if (ret_flag != BEHIND_A_PLANE) {
			if (ret_flag == IN_FRONT_ALL_PLANES) {
				*parent_flag &= ~ISECT_CLIP_PLANE;
			}
		}
		else {
			return false;
		}
	}
	float rdist = snp_dist_squared_to_projected_aabb(
	        &data->data_precalc, bbmin, bbmax, parent_flag, data->r_axis_closest);

	return rdist < data->dist_px_sq;
}

static bool cb_walk_leaf_snap_vert(
        const BVHTreeAxisRange *UNUSED(bounds), int index, short *parent_flag, void *userdata)
{
	struct Nearest2dUserData *data = userdata;
	struct SnapNearest2dPrecalc *local_data = &data->data_precalc;
	if (index == data->vert_index) {
		return true;
	}
	float *co;
	data->get_vert_co(index, &co, data->userdata);

	/* Use rv3d clip segments: `rv3d->clip` */
	if (snap_point_v3(
		        local_data->depth_range,
		        local_data->mval, co,
		        local_data->local->pmat,
		        local_data->win_half,
		        local_data->is_persp,
		        *parent_flag,
		        local_data->local->clip.plane,
		        local_data->local->clip.plane_num,
		        &data->dist_px_sq, data->co))
	{
		data->copy_vert_no(index, data->no, data->userdata);
		data->vert_index = index;
	}

	return true;
}

static bool cb_walk_leaf_snap_edge(
        const BVHTreeAxisRange *UNUSED(bounds), int index, short *parent_flag, void *userdata)
{
	struct Nearest2dUserData *data = userdata;
	struct SnapNearest2dPrecalc *local_data = &data->data_precalc;
	if (index == data->edge_index) {
		return true;
	}

	int vindex[2];
	data->get_edge_verts_index(index, vindex, data->userdata);

	if (data->snap_to & SCE_SELECT_EDGE) {
		bool vert_snapped = false;
		const float *co[2];
		data->get_vert_co(vindex[0], &co[0], data->userdata);
		data->get_vert_co(vindex[1], &co[1], data->userdata);
//		data->get_edge_co(index, co, data->userdata);

		float r_co[3], lambda, depth;
		dist_squared_ray_to_seg_v3(
		        local_data->local->ray_orig,
		        local_data->local->ray_dir,
		        co[0], co[1], r_co, &lambda, &depth);

		if ((data->snap_to & SCE_SELECT_VERTEX) && ((lambda < 0.25f) || (0.75f < lambda))) {
			int r_index = vindex[lambda > 0.5f];
			cb_walk_leaf_snap_vert(NULL, r_index, parent_flag, userdata);
			vert_snapped = data->vert_index == r_index;
			if (vert_snapped) {
				data->edge_index = index; /* Avoid recalculating edge */
			}
		}

		if (!vert_snapped) {
			if (snap_point_v3(
				        local_data->depth_range,
				        local_data->mval, r_co,
				        local_data->local->pmat,
				        local_data->win_half,
				        local_data->is_persp,
				        *parent_flag,
				        local_data->local->clip.plane,
				        local_data->local->clip.plane_num,
				        &data->dist_px_sq, data->co))
			{
				sub_v3_v3v3(data->no, co[0], co[1]);
				data->edge_index = index;
			}
		}
	}
	else {
		for (int i = 0; i < 2; i++) {
			cb_walk_leaf_snap_vert(NULL, vindex[i], parent_flag, userdata);
		}
	}

	return true;
}

static bool cb_walk_leaf_snap_tri(
        const BVHTreeAxisRange *UNUSED(bounds), int index, short *parent_flag, void *userdata)
{
	struct Nearest2dUserData *data = userdata;

	if (data->snap_to & SCE_SELECT_EDGE) {
		int eindex[3];
		data->get_tri_edges_index(index, eindex, data->userdata);
		for (int i = 0; i < 3; i++) {
			if (eindex[i] != -1) {
				cb_walk_leaf_snap_edge(NULL, eindex[i], parent_flag, userdata);
			}
		}
	}
	else {
		int vindex[3];
		data->get_tri_verts_index(index, vindex, data->userdata);
		for (int i = 0; i < 3; i++) {
			cb_walk_leaf_snap_vert(NULL, vindex[i], parent_flag, userdata);
		}
	}
	return true;
}

static bool cb_nearest_walk_order(
        const BVHTreeAxisRange *UNUSED(bounds), char axis, short *UNUSED(parent_flag), void *userdata)
{
	const bool *r_axis_closest = ((struct Nearest2dUserData *)userdata)->r_axis_closest;
	return r_axis_closest[axis];
}

/** \} */

/* -------------------------------------------------------------------- */

/** \name Internal Object Snapping API
 * \{ */

static bool snapArmature(
        SnapData *snpdt,
        Object *ob, bArmature *arm, float obmat[4][4],
        /* read/write args */
        float *ray_depth, float *dist_px,
        /* return args */
        float r_loc[3], float *UNUSED(r_no))
{
	bool retval = false;

	if (snpdt->snap_to_flag == SCE_SELECT_FACE) { /* Currently only edge and vert */
		return retval;
	}

	SnapNearestLocalData nearestlocaldata;
	snp_nearest_local_data_get(&nearestlocaldata, snpdt, obmat);

	short flag = nearestlocaldata.clip.plane ? (ISECT_CLIP_PLANE | TEST_RANGE_DEPTH) : TEST_RANGE_DEPTH;

	bool is_persp = snpdt->view_proj == VIEW_PROJ_PERSP;
	float dist_px_sq = SQUARE(*dist_px);

	if (arm->edbo) {
		for (EditBone *eBone = arm->edbo->first; eBone; eBone = eBone->next) {
			if (eBone->layer & arm->layer) {
				/* skip hidden or moving (selected) bones */
				if ((eBone->flag & (BONE_HIDDEN_A | BONE_ROOTSEL | BONE_TIPSEL)) == 0) {
					retval |= snap_segment_v3v3(
					        snpdt->snap_to_flag, &nearestlocaldata,
					        snpdt->depth_range, snpdt->mval,
					        snpdt->win_half, is_persp, flag,
					        eBone->head, eBone->tail,
					        &dist_px_sq, r_loc);
				}
			}
		}
	}
	else if (ob->pose && ob->pose->chanbase.first) {
		for (bPoseChannel *pchan = ob->pose->chanbase.first; pchan; pchan = pchan->next) {
			Bone *bone = pchan->bone;
			/* skip hidden bones */
			if (bone && !(bone->flag & (BONE_HIDDEN_P | BONE_HIDDEN_PG))) {
				const float *head_vec = pchan->pose_head;
				const float *tail_vec = pchan->pose_tail;

				retval |= snap_segment_v3v3(
				        snpdt->snap_to_flag, &nearestlocaldata,
				        snpdt->depth_range, snpdt->mval,
				        snpdt->win_half, is_persp, flag,
				        head_vec, tail_vec,
				        &dist_px_sq, r_loc);
			}
		}
	}

	snp_free_nearestdata(&nearestlocaldata);

	if (retval) {
		*dist_px = sqrtf(dist_px_sq);
		mul_m4_v3(obmat, r_loc);
		*ray_depth = depth_get(r_loc, snpdt->ray_start, snpdt->ray_dir);
		return true;
	}

	return false;
}

static bool snapCurve(
        SnapData *snpdt,
        Object *ob, Curve *cu, float obmat[4][4],
        /* read/write args */
        float *ray_depth, float *dist_px,
        /* return args */
        float r_loc[3], float *UNUSED(r_no))
{
	bool retval = false;

	/* only vertex snapping mode (eg control points and handles) supported for now) */
	if ((snpdt->snap_to_flag & SCE_SELECT_VERTEX) == 0) {
		return retval;
	}

	bool is_persp = snpdt->view_proj == VIEW_PROJ_PERSP;
	float lpmat[4][4], dist_px_sq = SQUARE(*dist_px);
	mul_m4_m4m4(lpmat, snpdt->pmat, obmat);
	float imat[4][4];
	invert_m4_m4(imat, obmat);

	short flag = TEST_RANGE_DEPTH;

	float(*local_clip_planes)[4] = NULL;
	if (snpdt->clip.plane) {
		flag |= ISECT_CLIP_PLANE;
		local_clip_planes = snp_clipplanes_calc_local(
			snpdt->clip.plane, snpdt->clip.plane_num, obmat);
	}

	for (Nurb *nu = (ob->mode == OB_MODE_EDIT ? cu->editnurb->nurbs.first : cu->nurb.first); nu; nu = nu->next) {
		for (int u = 0; u < nu->pntsu; u++) {
			if (ob->mode == OB_MODE_EDIT) {
				if (nu->bezt) {
					/* don't snap to selected (moving) or hidden */
					if (nu->bezt[u].f2 & SELECT || nu->bezt[u].hide != 0) {
						break;
					}
					retval |= snap_point_v3(
					        snpdt->depth_range, snpdt->mval, nu->bezt[u].vec[1],
					        lpmat, snpdt->win_half, is_persp,
					        flag, local_clip_planes, snpdt->clip.plane_num,
					        &dist_px_sq, r_loc);
					/* don't snap if handle is selected (moving), or if it is aligning to a moving handle */
					if (!(nu->bezt[u].f1 & SELECT) &&
					    !(nu->bezt[u].h1 & HD_ALIGN && nu->bezt[u].f3 & SELECT))
					{
						retval |= snap_point_v3(
						        snpdt->depth_range, snpdt->mval, nu->bezt[u].vec[0],
						        lpmat, snpdt->win_half, is_persp,
						        flag, local_clip_planes, snpdt->clip.plane_num,
						        &dist_px_sq, r_loc);
					}
					if (!(nu->bezt[u].f3 & SELECT) &&
					    !(nu->bezt[u].h2 & HD_ALIGN && nu->bezt[u].f1 & SELECT))
					{
						retval |= snap_point_v3(
						        snpdt->depth_range, snpdt->mval, nu->bezt[u].vec[2],
						        lpmat, snpdt->win_half, is_persp,
						        flag, local_clip_planes, snpdt->clip.plane_num,
						        &dist_px_sq, r_loc);
					}
				}
				else {
					/* don't snap to selected (moving) or hidden */
					if (nu->bp[u].f1 & SELECT || nu->bp[u].hide != 0) {
						break;
					}
					retval |= snap_point_v3(
					        snpdt->depth_range, snpdt->mval, nu->bp[u].vec,
					        lpmat, snpdt->win_half, is_persp,
					        flag, local_clip_planes, snpdt->clip.plane_num,
					        &dist_px_sq, r_loc);
				}
			}
			else {
				/* curve is not visible outside editmode if nurb length less than two */
				if (nu->pntsu > 1) {
					if (nu->bezt) {
						retval |= snap_point_v3(
						        snpdt->depth_range, snpdt->mval, nu->bezt[u].vec[1],
						        lpmat, snpdt->win_half, is_persp,
						        flag, local_clip_planes, snpdt->clip.plane_num,
						        &dist_px_sq, r_loc);
					}
					else {
						retval |= snap_point_v3(
						        snpdt->depth_range, snpdt->mval, nu->bp[u].vec,
						        lpmat, snpdt->win_half, is_persp,
						        flag, local_clip_planes, snpdt->clip.plane_num,
						        &dist_px_sq, r_loc);
					}
				}
			}
		}
	}

	if (local_clip_planes) {
		MEM_freeN(local_clip_planes);
	}

	if (retval) {
		*dist_px = sqrtf(dist_px_sq);
		mul_m4_v3(obmat, r_loc);
		*ray_depth = depth_get(r_loc, snpdt->ray_start, snpdt->ray_dir);
		return true;
	}
	return false;
}

/* may extend later (for now just snaps to empty center) */
static bool snapEmpty(
        SnapData *snpdt,
        Object *ob, float obmat[4][4],
        /* read/write args */
        float *ray_depth, float *dist_px,
        /* return args */
        float r_loc[3], float *UNUSED(r_no))
{
	bool retval = false;

	if (ob->transflag & OB_DUPLI) {
		return retval;
	}

	short flag = snpdt->clip.plane ? (TEST_RANGE_DEPTH | ISECT_CLIP_PLANE) : TEST_RANGE_DEPTH;

	/* for now only vertex supported */
	if (snpdt->snap_to_flag & SCE_SELECT_VERTEX) {
		bool is_persp = snpdt->view_proj == VIEW_PROJ_PERSP;
		float dist_px_sq = SQUARE(*dist_px);
		float tmp_co[3];
		copy_v3_v3(tmp_co, obmat[3]);
		if (snap_point_v3(
			        snpdt->depth_range, snpdt->mval, tmp_co,
			        snpdt->pmat, snpdt->win_half, is_persp,
			        flag, snpdt->clip.plane, snpdt->clip.plane_num,
			        &dist_px_sq, r_loc))
		{
			*dist_px = sqrtf(dist_px_sq);
			*ray_depth = depth_get(r_loc, snpdt->ray_start, snpdt->ray_dir);
			retval = true;
		}
	}

	return retval;
}

static bool snapCamera(
        const SnapObjectContext *sctx, SnapData *snpdt,
        Object *object, float obmat[4][4],
        /* read/write args */
        float *ray_depth, float *dist_px,
        /* return args */
        float r_loc[3], float *UNUSED(r_no))
{
	bool retval = false;

	if  (snpdt->snap_to_flag & SCE_SELECT_VERTEX) {
		Scene *scene = sctx->scene;

		MovieClip *clip = BKE_object_movieclip_get(scene, object, false);

		if (clip == NULL) {
			return retval;
		}
		if (object->transflag & OB_DUPLI) {
			return retval;
		}

		float orig_camera_mat[4][4], orig_camera_imat[4][4], imat[4][4];

		bool is_persp = snpdt->view_proj == VIEW_PROJ_PERSP;
		float dist_px_sq = SQUARE(*dist_px);

		BKE_tracking_get_camera_object_matrix(scene, object, orig_camera_mat);

		invert_m4_m4(orig_camera_imat, orig_camera_mat);
		invert_m4_m4(imat, obmat);
		short flag = snpdt->clip.plane ? (TEST_RANGE_DEPTH | ISECT_CLIP_PLANE) : TEST_RANGE_DEPTH;

		MovieTracking *tracking = &clip->tracking;

		MovieTrackingObject *tracking_object;

		for (tracking_object = tracking->objects.first;
		     tracking_object;
		     tracking_object = tracking_object->next)
		{
			ListBase *tracksbase = BKE_tracking_object_get_tracks(tracking, tracking_object);
			MovieTrackingTrack *track;
			float reconstructed_camera_mat[4][4],
			      reconstructed_camera_imat[4][4];
			float (*vertex_obmat)[4];

			if ((tracking_object->flag & TRACKING_OBJECT_CAMERA) == 0) {
				BKE_tracking_camera_get_reconstructed_interpolate(tracking, tracking_object,
				                                                  CFRA, reconstructed_camera_mat);

				invert_m4_m4(reconstructed_camera_imat, reconstructed_camera_mat);
			}

			for (track = tracksbase->first; track; track = track->next) {
				float bundle_pos[3];

				if ((track->flag & TRACK_HAS_BUNDLE) == 0) {
					continue;
				}

				copy_v3_v3(bundle_pos, track->bundle_pos);
				if (tracking_object->flag & TRACKING_OBJECT_CAMERA) {
					vertex_obmat = orig_camera_mat;
				}
				else {
					mul_m4_v3(reconstructed_camera_imat, bundle_pos);
					vertex_obmat = obmat;
				}

				/* Use local values */
				mul_m4_v3(vertex_obmat, bundle_pos);
				retval |= snap_point_v3(
				        snpdt->depth_range, snpdt->mval, bundle_pos,
				        snpdt->pmat, snpdt->win_half, is_persp,
				        flag, snpdt->clip.plane, snpdt->clip.plane_num,
				        &dist_px_sq, r_loc);
			}
		}

		if (retval) {
			*dist_px = sqrtf(dist_px_sq);
			*ray_depth = depth_get(r_loc, snpdt->ray_start, snpdt->ray_dir);
			return true;
		}
	}
	return retval;
}

static bool snapDerivedMesh(
        SnapObjectContext *sctx, SnapData *snpdt,
        Object *ob, float obmat[4][4], const unsigned int ob_index,
        /* read/write args */
        float *ray_depth, float *dist_px,
        /* return args */
        float r_loc[3], float r_no[3], int *r_index,
        ListBase *r_hit_list)
{
	bool retval = false;

	union {
		SnapNearestLocalData nearest;
		SnapRayCastLocalData raycast;
	} localdata;

	BoundBox *bb = BKE_object_boundbox_get(ob);

	if (snpdt->snap_to_flag & SCE_SELECT_FACE) {
		snp_raycast_local_data_get(&localdata.raycast, snpdt, obmat, *ray_depth);
		if (bb && !snp_snap_boundbox_raycast_test(snpdt, &localdata.raycast, bb)) {
			return retval;
		}
	}
	else {
		/* In vertex and edges you need to get the pixel distance from mval to BoundBox, see T46816. */
		snp_nearest_local_data_get(&localdata.nearest, snpdt, obmat);
		if (bb && !snp_snap_boundbox_nearest_test(snpdt, &localdata.nearest, bb, *dist_px)) {
			snp_free_nearestdata(&localdata.nearest);
			return retval;
		}
	}

	SnapObjectData_Mesh *sod = NULL;

	void **sod_p;
	if (BLI_ghash_ensure_p(sctx->cache.object_map, ob, &sod_p)) {
		sod = *sod_p;
	}
	else {
		sod = *sod_p = BLI_memarena_calloc(sctx->cache.mem_arena, sizeof(*sod));
		sod->sd.type = SNAP_MESH;
		sod->has_loose_vert = sod->has_loose_edge = sod->has_looptris = true;
	}

	DerivedMesh *dm;
	object_dm_final_get(sctx->scene, ob, &dm);

	if (dm->getNumVerts(dm) == 0) {
		return retval;
	}

	BVHTreeFromMesh *treedata_lt = &sod->treedata;

	/* Adds data to cache */

	/* For any snap_to, the BVHTree of looptris will always be used */
	/* the tree is owned by the DM and may have been freed since we last used! */
	if (sod->has_looptris) {
		if (treedata_lt->cached && !bvhcache_has_tree(dm->bvhCache, treedata_lt->tree)) {
			//printf("dm has no tree\n");
			free_bvhtree_from_mesh(treedata_lt);
		}

		/* Better than using `treedata_lt->tree` because it may continue NULL */
		if (treedata_lt->tree == NULL) {
			bvhtree_from_mesh_looptri(treedata_lt, dm, 0.0f, 4, 6);

			sod->has_looptris = treedata_lt->tree != NULL;
		}
	}

	if ((snpdt->snap_to_flag & SCE_SELECT_FACE) && sod->has_looptris) {
		/* Only use closer ray_start in case of ortho view! In perspective one, ray_start may already
		 * been *inside* boundbox, leading to snap failures (see T38409).
		 * Note also ar might be null (see T38435), in this case we assume ray_start is ok!
		 */
		if (snpdt->view_proj == VIEW_PROJ_ORTHO) {  /* do_ray_start_correction */
			if (bb) {
				localdata.raycast.len_diff = dist_aabb_to_plane(
				        bb->vec[0], bb->vec[6],
				        localdata.raycast.ray_start,
				        localdata.raycast.ray_dir);

				if (localdata.raycast.len_diff < 0) {
					localdata.raycast.len_diff = 0.0f;
				}
			}
			else {
				/* TODO (Germano): Do with root */
			}

			/* You need to make sure that ray_start is really far away,
			 * because even in the Orthografic view, in some cases,
			 * the ray can start inside the object (see T50486) */
			if (localdata.raycast.len_diff > 400.0f) {
				/* We pass a temp ray_start, set from object's boundbox, to avoid precision issues with
				 * very far away ray_start values (as returned in case of ortho view3d), see T38358.
				 */
				float ray_org_local[3];
				copy_v3_v3(ray_org_local, snpdt->ray_origin);
				mul_m4_v3(localdata.raycast.imat, ray_org_local);

				localdata.raycast.len_diff -= localdata.raycast.scale; /* make temp start point a bit away from bbox hit point. */
				madd_v3_v3v3fl(
				        localdata.raycast.ray_start, ray_org_local, localdata.raycast.ray_dir,
				        localdata.raycast.len_diff + snpdt->depth_range[0] * localdata.raycast.scale);
				localdata.raycast.depth -= localdata.raycast.len_diff;
			}
			else localdata.raycast.len_diff = 0.0f;
		}
		if (r_hit_list) {
			float timat[3][3];
			transpose_m3_m4(timat, localdata.raycast.imat);

			struct RayCastAll_Data data;

			data.bvhdata = treedata_lt;
			data.raycast_callback = treedata_lt->raycast_callback;
			data.obmat = obmat;
			data.timat = timat;
			data.len_diff = localdata.raycast.len_diff;
			data.local_scale = localdata.raycast.scale;
			data.ob = ob;
			data.ob_uuid = ob_index;
			data.hit_list = r_hit_list;
			data.retval = retval;

			BLI_bvhtree_ray_cast_all(
			        treedata_lt->tree,
			        localdata.raycast.ray_start, localdata.raycast.ray_dir,
			        0.0f, *ray_depth, raycast_all_cb, &data);

			retval = data.retval;
		}
		else {
			BVHTreeRayHit hit = {.index = -1, .dist = localdata.raycast.depth};

			if (BLI_bvhtree_ray_cast(
			        treedata_lt->tree,
			        localdata.raycast.ray_start, localdata.raycast.ray_dir,
			        0.0f, &hit, treedata_lt->raycast_callback, treedata_lt) != -1)
			{
				hit.dist += localdata.raycast.len_diff;
				hit.dist /= localdata.raycast.scale;
				if (hit.dist <= *ray_depth) {
					*ray_depth = hit.dist;
					copy_v3_v3(r_loc, hit.co);

					/* back to worldspace */
					mul_m4_v3(obmat, r_loc);

					if (r_no) {
						float timat[3][3];
						transpose_m3_m4(timat, localdata.raycast.imat);
						copy_v3_v3(r_no, hit.no);
						mul_m3_v3(timat, r_no);
						normalize_v3(r_no);
					}

					retval = true;

					if (r_index) {
						*r_index = treedata_lt->looptri[hit.index].poly;
					}
				}
			}
		}
	}
	else { /* TODO (Germano): separate raycast from nearest */

		/* If the tree continues NULL, probably there are no looptris :\
		 * In this case, at least take the vertices */
		if (treedata_lt->vert == NULL) {
			/* Get the vert array again, to use in the snap to verts or edges */
			treedata_lt->vert = DM_get_vert_array(dm, &treedata_lt->vert_allocated);
		}

		if (snpdt->snap_to_flag & SCE_SELECT_VERTEX) {
			if (!treedata_lt->edge_allocated) { /* Snap to edges may already have been used before */
				treedata_lt->edge = DM_get_edge_array(dm, &treedata_lt->edge_allocated);
			}
			if (sod->has_loose_vert) {
				/* the tree is owned by the DM and may have been freed since we last used! */
				if (sod->bvh_trees[0] && !bvhcache_has_tree(dm->bvhCache, sod->bvh_trees[0])) {
					BLI_bvhtree_free(sod->bvh_trees[0]);
					sod->bvh_trees[0] = NULL;
				}
				if (sod->bvh_trees[0] == NULL) {
					sod->bvh_trees[0] = snp_bvhtree_from_mesh_loose_verts(
					        dm, treedata_lt->edge, treedata_lt->vert);

					sod->has_loose_vert = sod->bvh_trees[0] != NULL;
				}
			}
		}
		if (snpdt->snap_to_flag & SCE_SELECT_EDGE) {
			if (!treedata_lt->edge_allocated) {
				treedata_lt->edge = DM_get_edge_array(dm, &treedata_lt->edge_allocated);
			}
			if (sod->has_loose_edge) {
				/* the tree is owned by the DM and may have been freed since we last used! */
				if (sod->bvh_trees[1] && !bvhcache_has_tree(dm->bvhCache, sod->bvh_trees[1])) {
					BLI_bvhtree_free(sod->bvh_trees[1]);
					sod->bvh_trees[1] = NULL;
				}
				if (sod->bvh_trees[1] == NULL) {
					sod->bvh_trees[1] = snp_bvhtree_from_mesh_loose_edges(
					        dm, treedata_lt->edge, treedata_lt->vert);

					sod->has_loose_edge = sod->bvh_trees[1] != NULL;
				}
			}
		}

		/* Warning: the depth_max is currently being used only in perspective view.
		 * It is not correct to limit the maximum depth for elements obtained with nearest
		 * since this limitation will depend on the normal and the size of the face.
		 * And more... ray_depth here is being confused with Z-depth */
		const float ray_depth_max_global = *ray_depth + snpdt->depth_range[0];

		Nearest2dUserData neasrest2d = {
		    .dist_px_sq = SQUARE(*dist_px),
		    .r_axis_closest = {1.0f, 1.0f, 1.0f},
		    .snap_to = snpdt->snap_to_flag,
		    .userdata = treedata_lt,
		    .get_vert_co = (Nearest2DGetVertCoCallback)dm_get_vert_co_cb,
//		    .get_edge_co = (Nearest2DGetEdgeCoCallback)dm_get_edge_co,
		    .get_edge_verts_index = (Nearest2DGetEdgeVertsCallback)dm_get_edge_verts_cb,
		    .get_tri_verts_index = (Nearest2DGetTriVertsCallback)dm_get_tri_verts_cb,
		    .get_tri_edges_index = (Nearest2DGetTriEdgesCallback)dm_get_tri_edges_cb,
		    .copy_vert_no = (Nearest2DCopyVertNoCallback)dm_copy_vert_no_cb,
		    .vert_index = -1,
		    .edge_index = -1,
		};

		snp_dist_squared_to_projected_aabb_precalc(
		        &neasrest2d.data_precalc, &localdata.nearest, snpdt);

		short flag = TEST_RANGE_DEPTH;
		if (snpdt->clip.plane) {
			flag |= ISECT_CLIP_PLANE;
		}

		if (sod->bvh_trees[0]) { /* VERTS */
			BLI_bvhtree_walk_dfs(
			        sod->bvh_trees[0],
			        cb_walk_parent_snap_project, cb_walk_leaf_snap_vert,
			        cb_nearest_walk_order, flag, &neasrest2d);
		}

		if (sod->bvh_trees[1]) { /* EDGES */
			BLI_bvhtree_walk_dfs(
			        sod->bvh_trees[1],
			        cb_walk_parent_snap_project, cb_walk_leaf_snap_edge,
			        cb_nearest_walk_order, flag, &neasrest2d);
		}

		if (treedata_lt->tree) { /* LOOPTRIS */
			BLI_bvhtree_walk_dfs(
			        treedata_lt->tree,
			        cb_walk_parent_snap_project, cb_walk_leaf_snap_tri,
			        cb_nearest_walk_order, flag, &neasrest2d);
		}

		snp_free_nearestdata(&localdata.nearest);

		if ((neasrest2d.vert_index != -1) || (neasrest2d.edge_index != -1)) {
			copy_v3_v3(r_loc, neasrest2d.co);
			mul_m4_v3(obmat, r_loc);
			if (r_no) {
				float timat[3][3];
				transpose_m3_m4(timat, localdata.nearest.imat);
				copy_v3_v3(r_no, neasrest2d.no);
				mul_m3_v3(timat, r_no);
				normalize_v3(r_no);
			}
			*dist_px = sqrtf(neasrest2d.dist_px_sq);
			*ray_depth = depth_get(r_loc, snpdt->ray_start, snpdt->ray_dir);

			retval = true;
		}
	}

	dm->release(dm);

	return retval;
}

static bool snapEditMesh(
        SnapObjectContext *sctx, SnapData *snpdt,
        Object *ob, float obmat[4][4], const unsigned int ob_index,
        /* read/write args */
        float *ray_depth, float *dist_px,
        /* return args */
        float r_loc[3], float r_no[3], int *r_index,
        ListBase *r_hit_list)
{
	bool retval = false;

	BMEditMesh *em = BKE_editmesh_from_object(ob);

	if (em->bm->totvert == 0) {
		return retval;
	}

	SnapObjectData_EditMesh *sod = NULL;


	void **sod_p;
	if (BLI_ghash_ensure_p(sctx->cache.object_map, ob, &sod_p)) {
		sod = *sod_p;
	}
	else {
		sod = *sod_p = BLI_memarena_calloc(sctx->cache.mem_arena, sizeof(*sod));
		sod->sd.type = SNAP_EDIT_MESH;
	}

	if (snpdt->snap_to_flag & SCE_SELECT_FACE) {
		BVHTreeFromEditMesh *treedata = NULL;

		if (sod->bvh_trees[0] == NULL) {
			sod->bvh_trees[0] = BLI_memarena_calloc(sctx->cache.mem_arena, sizeof(*treedata));

			int looptri_num_active = -1;
			BLI_bitmap *face_mask = NULL;
			if (sctx->callbacks.edit_mesh.test_face_fn) {
				face_mask = BLI_BITMAP_NEW(em->tottri, __func__);
				looptri_num_active = BM_iter_mesh_bitmap_from_filter_tessface(
				        em->bm, face_mask,
				        sctx->callbacks.edit_mesh.test_face_fn, sctx->callbacks.edit_mesh.user_data);
			}
			bvhtree_from_editmesh_looptri_ex(
			        sod->bvh_trees[0], em, face_mask, looptri_num_active, 0.0f, 4, 6, NULL);

			if (face_mask) {
				MEM_freeN(face_mask);
			}
		}
		treedata = sod->bvh_trees[0];

		SnapRayCastLocalData raycastlocaldata;
		snp_raycast_local_data_get(&raycastlocaldata, snpdt, obmat, *ray_depth);

		/* Only use closer ray_start in case of ortho view! In perspective one, ray_start
		 * may already been *inside* boundbox, leading to snap failures (see T38409).
		 * Note also ar might be null (see T38435), in this case we assume ray_start is ok!
		 */
		if (snpdt->view_proj == VIEW_PROJ_ORTHO) {  /* do_ray_start_correction */
			/* We *need* a reasonably valid len_diff in this case.
			 * Use BHVTree to find the closest face from ray_start_local.
			 */
			BVHTreeNearest nearest;
			nearest.index = -1;
			nearest.dist_sq = FLT_MAX;
			/* Compute and store result. */
			if (BLI_bvhtree_find_nearest(
			        treedata->tree, raycastlocaldata.ray_start, &nearest, NULL, NULL) != -1)
			{
				float dvec[3];
				sub_v3_v3v3(dvec, nearest.co, raycastlocaldata.ray_dir);
				raycastlocaldata.len_diff = dot_v3v3(dvec, raycastlocaldata.ray_dir);
				/* You need to make sure that ray_start is really far away,
				 * because even in the Orthografic view, in some cases,
				 * the ray can start inside the object (see T50486) */
				if (raycastlocaldata.len_diff > 400.0f) {
					float ray_org_local[3];

					copy_v3_v3(ray_org_local, snpdt->ray_origin);
					mul_m4_v3(raycastlocaldata.imat, ray_org_local);

					/* We pass a temp ray_start, set from object's boundbox,
					 * to avoid precision issues with very far away ray_start values
					 * (as returned in case of ortho view3d), see T38358.
					 */
					raycastlocaldata.len_diff -= raycastlocaldata.scale; /* make temp start point a bit away from bbox hit point. */
					madd_v3_v3v3fl(
					        raycastlocaldata.ray_start, ray_org_local, raycastlocaldata.ray_dir,
					        raycastlocaldata.len_diff + snpdt->depth_range[0] * raycastlocaldata.scale);
					raycastlocaldata.depth -= raycastlocaldata.len_diff;
				}
				else raycastlocaldata.len_diff = 0.0f;
			}
		}
		if (r_hit_list) {
			float timat[3][3];
			transpose_m3_m4(timat, raycastlocaldata.imat);

			struct RayCastAll_Data data;

			data.bvhdata = treedata;
			data.raycast_callback = treedata->raycast_callback;
			data.obmat = obmat;
			data.timat = timat;
			data.len_diff = raycastlocaldata.len_diff;
			data.local_scale = raycastlocaldata.scale;
			data.ob = ob;
			data.ob_uuid = ob_index;
			data.hit_list = r_hit_list;
			data.retval = retval;

			BLI_bvhtree_ray_cast_all(
			        treedata->tree, raycastlocaldata.ray_start, raycastlocaldata.ray_dir, 0.0f,
			        *ray_depth, raycast_all_cb, &data);

			retval = data.retval;
		}
		else {
			BVHTreeRayHit hit = {.index = -1, .dist = raycastlocaldata.depth};

			if (BLI_bvhtree_ray_cast(
			        treedata->tree, raycastlocaldata.ray_start, raycastlocaldata.ray_dir, 0.0f,
			        &hit, treedata->raycast_callback, treedata) != -1)
			{
				hit.dist += raycastlocaldata.len_diff;
				hit.dist /= raycastlocaldata.scale;
				if (hit.dist <= *ray_depth) {
					*ray_depth = hit.dist;
					copy_v3_v3(r_loc, hit.co);

					/* back to worldspace */
					mul_m4_v3(obmat, r_loc);

					if (r_no) {
						float timat[3][3];
						transpose_m3_m4(timat, raycastlocaldata.imat);

						copy_v3_v3(r_no, hit.no);
						mul_m3_v3(timat, r_no);
						normalize_v3(r_no);
					}

					retval = true;

					if (r_index) {
						*r_index = em->looptris[hit.index][0]->f->head.index;
					}
				}
			}
		}
	}
	else {
		BVHTreeFromEditMesh *treedata_edge = NULL, *treedata_vert = NULL;

		if (snpdt->snap_to_flag &  SCE_SELECT_EDGE) {
			if (sod->bvh_trees[1] == NULL) {
				sod->bvh_trees[1] = BLI_memarena_calloc(sctx->cache.mem_arena, sizeof(*treedata_edge));

				BLI_bitmap *edge_mask = NULL;
				int edges_num_active = -1;
				if (sctx->callbacks.edit_mesh.test_edge_fn) {
					edge_mask = BLI_BITMAP_NEW(em->bm->totedge, __func__);
					edges_num_active = BM_iter_mesh_bitmap_from_filter(
					        BM_EDGES_OF_MESH, em->bm, edge_mask,
					        (bool (*)(BMElem *, void *))sctx->callbacks.edit_mesh.test_edge_fn,
					        sctx->callbacks.edit_mesh.user_data);
				}
				bvhtree_from_editmesh_edges_ex(
				        sod->bvh_trees[1], em, edge_mask, edges_num_active, 0.0f, 2, 6);

				if (edge_mask) {
					MEM_freeN(edge_mask);
				}
			}
			treedata_edge = sod->bvh_trees[1];
		}
		if (snpdt->snap_to_flag & SCE_SELECT_VERTEX) {
			if (sod->bvh_trees[2] == NULL) {
				sod->bvh_trees[2] = BLI_memarena_calloc(sctx->cache.mem_arena, sizeof(*treedata_vert));

				BLI_bitmap *vert_mask = NULL;
				int verts_num_active = -1;
				if (sctx->callbacks.edit_mesh.test_vert_fn) {
					vert_mask = BLI_BITMAP_NEW(em->bm->totvert, __func__);
					verts_num_active = BM_iter_mesh_bitmap_from_filter(
					        BM_VERTS_OF_MESH, em->bm, vert_mask,
					        (bool (*)(BMElem *, void *))sctx->callbacks.edit_mesh.test_vert_fn,
					        sctx->callbacks.edit_mesh.user_data);
				}
				bvhtree_from_editmesh_verts_ex(
				        sod->bvh_trees[2], em, vert_mask, verts_num_active, 0.0f, 2, 6);

				if (vert_mask) {
					MEM_freeN(vert_mask);
				}
			}
			treedata_vert = sod->bvh_trees[2];
		}

		SnapNearestLocalData nearestlocaldata;
		snp_nearest_local_data_get(&nearestlocaldata, snpdt, obmat);

		Nearest2dUserData neasrest2d = {
		    .dist_px_sq = SQUARE(*dist_px),
		    .r_axis_closest = {1.0f, 1.0f, 1.0f},
		    .snap_to = snpdt->snap_to_flag,
		    .userdata = em,
		    .get_vert_co = (Nearest2DGetVertCoCallback)em_get_vert_co_cb,
//		    .get_edge_co = (Nearest2DGetEdgeCoCallback)em_get_edge_co,
		    .get_edge_verts_index = (Nearest2DGetEdgeVertsCallback)em_get_edge_verts_cb,
//		    .get_tri_verts_index = (Nearest2DGetTriVertsCallback)em_get_tri_verts_cb,
//		    .get_tri_edges_index = (Nearest2DGetTriEdgesCallback)em_get_tri_edges_cb,
		    .copy_vert_no = (Nearest2DCopyVertNoCallback)em_copy_vert_no_cb,
		    .vert_index = -1,
		    .edge_index = -1,
		};

		snp_dist_squared_to_projected_aabb_precalc(
		        &neasrest2d.data_precalc, &nearestlocaldata, snpdt);

		short flag = TEST_RANGE_DEPTH;
		if (snpdt->clip.plane) {
			flag |= ISECT_CLIP_PLANE;
		}

		if (treedata_vert && treedata_vert->tree) {
			BLI_bvhtree_walk_dfs(
			        treedata_vert->tree,
			        cb_walk_parent_snap_project, cb_walk_leaf_snap_vert,
			        cb_nearest_walk_order, flag, &neasrest2d);
		}
		if (neasrest2d.vert_index == -1 && treedata_edge && treedata_edge->tree) {
			neasrest2d.snap_to &= ~SCE_SELECT_VERTEX;
			BLI_bvhtree_walk_dfs(
			        treedata_edge->tree,
			        cb_walk_parent_snap_project, cb_walk_leaf_snap_edge,
			        cb_nearest_walk_order, flag, &neasrest2d);
		}

		snp_free_nearestdata(&nearestlocaldata);

		if ((neasrest2d.vert_index != -1) || (neasrest2d.edge_index != -1)) {
			copy_v3_v3(r_loc, neasrest2d.co);
			mul_m4_v3(obmat, r_loc);
			if (r_no) {
				float timat[3][3];
				transpose_m3_m4(timat, nearestlocaldata.imat);

				copy_v3_v3(r_no, neasrest2d.no);
				mul_m3_v3(timat, r_no);
				normalize_v3(r_no);
			}
			*dist_px = sqrtf(neasrest2d.dist_px_sq);
			*ray_depth = depth_get(r_loc, snpdt->ray_start, snpdt->ray_dir);

			retval = true;
		}
	}

	return retval;
}

/**
 * \param use_obedit: Uses the coordinates of BMesh (if any) to do the snapping;
 *
 * \note Duplicate args here are documented at #snapObjectsRay
 */
static bool snapObject(
        SnapObjectContext *sctx, SnapData *snpdt,
        Object *ob, float obmat[4][4], const unsigned int ob_index,
        bool use_obedit,
        /* read/write args */
        float *ray_depth, float *dist_px,
        /* return args */
        float r_loc[3], float r_no[3], int *r_index,
        Object **r_ob, float r_obmat[4][4],
        ListBase *r_hit_list)
{
	bool retval = false;

	if (snpdt->test_occlusion &&
		snpdt->snap_to_flag == SCE_SELECT_FACE &&
		sctx->v3d_data.v3d->drawtype < OB_SOLID ||
		ob->dt < OB_SOLID)
	{
		return retval;
	}

	switch (ob->type) {
		case OB_MESH:
		{
			if (use_obedit) {
				retval = snapEditMesh(
				        sctx, snpdt, ob, obmat, ob_index,
				        ray_depth, dist_px,
				        r_loc, r_no, r_index,
				        r_hit_list);
			}
			else {
				retval = snapDerivedMesh(
				        sctx, snpdt, ob, obmat, ob_index,
				        ray_depth, dist_px,
				        r_loc, r_no,
				        r_index, r_hit_list);
			}
			break;
		}
		case OB_ARMATURE:
			retval = snapArmature(
			        snpdt,
			        ob, ob->data, obmat,
			        ray_depth, dist_px,
			        r_loc, r_no);
			break;
		case OB_CURVE:
			retval = snapCurve(
			        snpdt,
			        ob, ob->data, obmat,
			        ray_depth, dist_px,
			        r_loc, r_no);
			break;
		case OB_EMPTY:
			retval = snapEmpty(
			        snpdt,
			        ob, obmat,
			        ray_depth, dist_px,
			        r_loc, r_no);
			break;
		case OB_CAMERA:
			retval = snapCamera(
			        sctx, snpdt, ob, obmat,
			        ray_depth, dist_px,
			        r_loc, r_no);
			break;
	}

	if (retval) {
		if (r_ob) {
			*r_ob = ob;
			copy_m4_m4(r_obmat, obmat);
		}
	}

	return retval;
}

/**
 * Main Snapping Function
 * ======================
 *
 * Walks through all objects in the scene to find the closest snap element ray.
 *
 * \param sctx: Snap context to store data.
 * \param snpdt: struct generated in `snapdata_init_ray/v3d`.
 * \param snap_select: from enum SnapSelect.
 * \param use_object_edit_cage: Uses the coordinates of BMesh (if any) to do the snapping.
 *
 * Read/Write Args
 * ---------------
 *
 * \param ray_depth: maximum depth allowed for r_co, elements deeper than this value will be ignored.
 * \param dist_px: Maximum threshold distance (in pixels).
 *
 * Output Args
 * -----------
 *
 * \param r_loc: Hit location.
 * \param r_no: Hit normal (optional).
 * \param r_index: Hit index or -1 when no valid index is found.
 * (currently only set to the polygon index when when using ``snap_to == SCE_SNAP_MODE_FACE``).
 * \param r_ob: Hit object.
 * \param r_obmat: Object matrix (may not be #Object.obmat with dupli-instances).
 * \param r_hit_list: List of #SnapObjectHitDepth (caller must free).
 *
 */
static bool snapObjectsRay(
        SnapObjectContext *sctx, SnapData *snpdt,
        const SnapSelect snap_select,
        const bool use_object_edit_cage,
        /* read/write args */
        float *ray_depth, float *dist_px,
        /* return args */
        float r_loc[3], float r_no[3], int *r_index,
        Object **r_ob, float r_obmat[4][4],
        ListBase *r_hit_list)
{
	bool retval = false;

	unsigned int ob_index = 0;
	Object *obedit = use_object_edit_cage ? sctx->scene->obedit : NULL;

	/* Need an exception for particle edit because the base is flagged with BA_HAS_RECALC_DATA
	 * which makes the loop skip it, even the derived mesh will never change
	 *
	 * To solve that problem, we do it first as an exception.
	 * */
	Base *base_act = sctx->scene->basact;
	if (base_act && base_act->object && base_act->object->mode & OB_MODE_PARTICLE_EDIT) {
		Object *ob = base_act->object;

		retval |= snapObject(
		        sctx, snpdt, ob, ob->obmat, ob_index++, false,
		        ray_depth, dist_px,
		        r_loc, r_no, r_index, r_ob, r_obmat, r_hit_list);
	}

	bool ignore_object_selected = false, ignore_object_active = false;
	switch (snap_select) {
		case SNAP_ALL:
			break;
		case SNAP_NOT_SELECTED:
			ignore_object_selected = true;
			break;
		case SNAP_NOT_ACTIVE:
			ignore_object_active = true;
			break;
	}
	for (Base *base = sctx->scene->base.first; base != NULL; base = base->next) {
		if ((BASE_VISIBLE_BGMODE(sctx->v3d_data.v3d, sctx->scene, base)) &&
		    (base->flag & (BA_HAS_RECALC_OB | BA_HAS_RECALC_DATA)) == 0 &&

		    !((ignore_object_selected && (base->flag & (SELECT | BA_WAS_SEL))) ||
		      (ignore_object_active && base == base_act)))
		{
			Object *ob = base->object;

			if (ob->transflag & OB_DUPLI) {
				DupliObject *dupli_ob;
				ListBase *lb = object_duplilist(sctx->bmain->eval_ctx, sctx->scene, ob);

				for (dupli_ob = lb->first; dupli_ob; dupli_ob = dupli_ob->next) {
					bool use_obedit_dupli = (obedit && dupli_ob->ob->data == obedit->data);
					Object *dupli_snap = (use_obedit_dupli) ? obedit : dupli_ob->ob;

					retval |= snapObject(
					        sctx, snpdt, dupli_snap, dupli_ob->mat,
					        ob_index++, use_obedit_dupli,
					        ray_depth, dist_px,
					        r_loc, r_no, r_index, r_ob, r_obmat, r_hit_list);
				}

				free_object_duplilist(lb);
			}

			bool use_obedit = (obedit != NULL) && (ob->data == obedit->data);
			Object *ob_snap = use_obedit ? obedit : ob;

			retval |= snapObject(
			        sctx, snpdt, ob_snap, ob->obmat, ob_index++, use_obedit,
			        ray_depth, dist_px,
			        r_loc, r_no, r_index, r_ob, r_obmat, r_hit_list);
		}
	}

	return retval;
}

/** \} */


/* -------------------------------------------------------------------- */

/** \name Public Object Snapping API
 * \{ */

SnapObjectContext *ED_transform_snap_object_context_create(
        Main *bmain, Scene *scene, int flag)
{
	SnapObjectContext *sctx = MEM_callocN(sizeof(*sctx), __func__);

	sctx->flag = flag;

	sctx->bmain = bmain;
	sctx->scene = scene;

	sctx->cache.object_map = BLI_ghash_ptr_new(__func__);
	sctx->cache.mem_arena = BLI_memarena_new(BLI_MEMARENA_STD_BUFSIZE, __func__);

	return sctx;
}

SnapObjectContext *ED_transform_snap_object_context_create_view3d(
        Main *bmain, Scene *scene, int flag,
        /* extra args for view3d */
        const ARegion *ar, const View3D *v3d)
{
	SnapObjectContext *sctx = ED_transform_snap_object_context_create(bmain, scene, flag);

	sctx->use_v3d = true;
	sctx->v3d_data.ar = ar;
	sctx->v3d_data.v3d = v3d;

	return sctx;
}

static void snap_object_data_free(void *sod_v)
{
	switch (((SnapObjectData *)sod_v)->type) {
		case SNAP_MESH:
		{
			SnapObjectData_Mesh *sod = sod_v;
#if NO_DM_CACHE
			if (sod->bvh_trees[0]) {
				BLI_bvhtree_free(sod->bvh_trees[0]);
			}
			if (sod->bvh_trees[1]) {
				BLI_bvhtree_free(sod->bvh_trees[1]);
			}
#endif
			free_bvhtree_from_mesh(&sod->treedata);

			break;
		}
		case SNAP_EDIT_MESH:
		{
			SnapObjectData_EditMesh *sod = sod_v;
			for (int i = 0; i < ARRAY_SIZE(sod->bvh_trees); i++) {
				if (sod->bvh_trees[i]) {
					free_bvhtree_from_editmesh(sod->bvh_trees[i]);
				}
			}
			break;
		}
	}
}

void ED_transform_snap_object_context_destroy(SnapObjectContext *sctx)
{
	BLI_ghash_free(sctx->cache.object_map, NULL, snap_object_data_free);
	BLI_memarena_free(sctx->cache.mem_arena);

	MEM_freeN(sctx);
}

void ED_transform_snap_object_context_set_editmesh_callbacks(
        SnapObjectContext *sctx,
        bool (*test_vert_fn)(BMVert *, void *user_data),
        bool (*test_edge_fn)(BMEdge *, void *user_data),
        bool (*test_face_fn)(BMFace *, void *user_data),
        void *user_data)
{
	sctx->callbacks.edit_mesh.test_vert_fn = test_vert_fn;
	sctx->callbacks.edit_mesh.test_edge_fn = test_edge_fn;
	sctx->callbacks.edit_mesh.test_face_fn = test_face_fn;

	sctx->callbacks.edit_mesh.user_data = user_data;
}

bool ED_transform_snap_object_project_ray_ex(
        SnapObjectContext *sctx,
        const struct SnapObjectParams *params,
        const float ray_start[3], const float ray_normal[3],
        float *ray_depth,
        float r_loc[3], float r_no[3], int *r_index,
        Object **r_ob, float r_obmat[4][4])
{
	SnapData snpdt;
	snapdata_init_ray(&snpdt, ray_start, ray_normal);

	Object *ob_fallback = NULL;
	if (r_index && !r_ob) {
		*r_ob = ob_fallback;
	}

	if (snapObjectsRay(
	        sctx, &snpdt,
	        params->snap_select, params->use_object_edit_cage,
	        ray_depth, NULL,
	        r_loc, r_no, r_index, r_ob, r_obmat, NULL))
	{
		if (r_index) {
			/* Restore index exposed by polys in in bpy */
			SnapObjectData *sod;
			void **sod_p;
			BLI_ghash_ensure_p(sctx->cache.object_map, *r_ob, &sod_p);
			sod = *sod_p;
			if (sod->type == SNAP_MESH) {

				DerivedMesh *dm;
				object_dm_final_get(sctx->scene, *r_ob, &dm);

				const int *index_mp_to_orig = dm->getPolyDataArray(dm, CD_ORIGINDEX);
				*r_index = index_mp_to_orig ? index_mp_to_orig[*r_index] : *r_index;

				dm->release(dm);
			}
		}

		return true;
	}

	return false;
}

/**
 * Fill in a list of all hits.
 *
 * \param ray_depth: Only depths in this range are considered, -1.0 for maximum.
 * \param sort: Optionally sort the hits by depth.
 * \param r_hit_list: List of #SnapObjectHitDepth (caller must free).
 */
bool ED_transform_snap_object_project_ray_all(
        SnapObjectContext *sctx,
        const struct SnapObjectParams *params,
        const float ray_start[3], const float ray_normal[3],
        float ray_depth, bool sort,
        ListBase *r_hit_list)
{
	const float depth_range[2] = {0.0f, FLT_MAX};
	if (ray_depth == -1.0f) {
		ray_depth = BVH_RAYCAST_DIST_MAX;
	}

#ifdef DEBUG
	float ray_depth_prev = ray_depth;
#endif

	SnapData snpdt;
	snapdata_init_ray(&snpdt, ray_start, ray_normal);

	bool retval = snapObjectsRay(
	        sctx, &snpdt,
	        params->snap_select, params->use_object_edit_cage,
	        &ray_depth, NULL,
	        NULL, NULL, NULL, NULL, NULL,
	        r_hit_list);

	/* meant to be readonly for 'all' hits, ensure it is */
#ifdef DEBUG
	BLI_assert(ray_depth_prev == ray_depth);
#endif

	if (sort) {
		BLI_listbase_sort(r_hit_list, hit_depth_cmp_cb);
	}

	return retval;
}

/**
 * Convenience function for snap ray-casting.
 *
 * Given a ray, cast it into the scene (snapping to faces).
 *
 * \return Snap success
 */
static bool transform_snap_context_project_ray_impl(
        SnapObjectContext *sctx,
        const struct SnapObjectParams *params,
        const float ray_start[3], const float ray_normal[3], float *ray_depth,
        float r_co[3], float r_no[3])
{
	bool ret;

	/* try snap edge, then face if it fails */
	ret = ED_transform_snap_object_project_ray_ex(
	        sctx,
	        params,
	        ray_start, ray_normal, ray_depth,
	        r_co, r_no, NULL,
	        NULL, NULL);

	return ret;
}

bool ED_transform_snap_object_project_ray(
        SnapObjectContext *sctx,
        const struct SnapObjectParams *params,
        const float ray_origin[3], const float ray_direction[3], float *ray_depth,
        float r_co[3], float r_no[3])
{
	float ray_depth_fallback;
	if (ray_depth == NULL) {
		ray_depth_fallback = BVH_RAYCAST_DIST_MAX;
		ray_depth = &ray_depth_fallback;
	}

	return transform_snap_context_project_ray_impl(
	        sctx,
	        params,
	        ray_origin, ray_direction, ray_depth,
	        r_co, r_no);
}

static bool transform_snap_context_project_view3d_mixed_impl(
        SnapObjectContext *sctx,
        const struct SnapObjectParams *params,
        SnapData *snpdt, float *dist_px, float *ray_depth,
        float r_co[3], float r_no[3], int *r_index)
{
	BLI_assert(snpdt->snap_to_flag != 0);
	BLI_assert((snpdt->snap_to_flag & ~(1 | 2 | 4)) == 0);

	bool is_hit = false;
	int t_index;
	Object *obj;
	float obmat[4][4];
	float t_no[3];

	if (snpdt->test_occlusion || snpdt->snap_to_flag & SCE_SELECT_FACE) {
		unsigned short tmp_snap_to_flag = snpdt->snap_to_flag;
		snpdt->snap_to_flag = SCE_SELECT_FACE;
		if (snapObjectsRay(
		        sctx, snpdt,
		        params->snap_select, params->use_object_edit_cage,
		        ray_depth, dist_px,
		        r_co, t_no, &t_index, &obj, obmat, NULL))
		{
			is_hit = (tmp_snap_to_flag & SCE_SELECT_FACE) != 0;

			/* Get new clip plane to simule occlusion */
			if (tmp_snap_to_flag & (SCE_SELECT_EDGE | SCE_SELECT_VERTEX)) {

				float imat[4][4];
				invert_m4_m4(imat, obmat);

				float normal_local[3], plane_no[3], far_vert[3];
				copy_v3_v3(plane_no, t_no);
				if (dot_v3v3(plane_no, snpdt->ray_dir) > 0) {
					negate_v3(plane_no);
				}

				copy_v3_v3(normal_local, plane_no);
				mul_m4_v3(imat, normal_local);

				if (snpdt->clip.plane) {
#define PREPEND_PLANE
#ifdef PREPEND_PLANE
					/* Alloc the new plane at the beginning of the array */
					float (*temp_plane)[4] = MEM_mallocN(
					        sizeof(*temp_plane) * (snpdt->clip.plane_num + 1), __func__);

					memcpy(&temp_plane[1], snpdt->clip.plane,
					        sizeof(*temp_plane) * snpdt->clip.plane_num);

					MEM_freeN(snpdt->clip.plane);

					snpdt->clip.plane = temp_plane;
#else
					snpdt->clip.plane = MEM_reallocN(
					        snpdt->clip.plane, sizeof(*snpdt->clip.plane) * (snpdt->clip.plane_num + 1));
#endif
				}
				else {
					snpdt->clip.plane = MEM_mallocN(sizeof(*snpdt->clip.plane), __func__);
				}

				snpdt->clip.plane_num += 1;

				SnapObjectData *sod;
				void **sod_p;
				BLI_ghash_ensure_p(sctx->cache.object_map, obj, &sod_p);
				sod = *sod_p;
				if (sod->type == SNAP_MESH) {
					const MLoop *mloop = ((SnapObjectData_Mesh *)sod)->treedata.loop;
					const MVert *mvert = ((SnapObjectData_Mesh *)sod)->treedata.vert;

					DerivedMesh *dm;
					object_dm_final_get(sctx->scene, obj, &dm);

					bool poly_allocated;
					MPoly *mpoly = DM_get_poly_array(dm, &poly_allocated);
					MPoly *mp = &mpoly[t_index];

					int loopstart = mp->loopstart;
					int totloop = mp->totloop;

					if (poly_allocated) {
						MEM_freeN(mpoly);
					}

					copy_v3_v3(far_vert, mvert[mloop[loopstart + totloop - 1].v].co);
					float iter_dist, far_dist = dot_v3v3(far_vert, normal_local);

					const MLoop *ml = &mloop[loopstart];
					for (int i = 1; i < totloop; i++, ml++) {
						iter_dist = dot_v3v3(mvert[ml->v].co, normal_local);
						if (iter_dist < far_dist) {
							far_dist = iter_dist;
							copy_v3_v3(far_vert, mvert[ml->v].co);
						}
					}

					dm->release(dm);
				}
				else { //if (sod->type == SNAP_EDIT_MESH)
					BMEditMesh *em = BKE_editmesh_from_object(obj);
					BMFace *f = BM_face_at_index(em->bm, t_index);

					BMLoop *l_iter, *l_first;
					l_first = BM_FACE_FIRST_LOOP(f);

					copy_v3_v3(far_vert, l_first->v->co);
					float iter_dist, far_dist = dot_v3v3(far_vert, normal_local);
					l_iter = l_first->next;

					do {
						iter_dist = dot_v3v3(l_iter->v->co, normal_local);
						if (iter_dist < far_dist) {
							far_dist = iter_dist;
							copy_v3_v3(far_vert, l_iter->v->co);
						}
					} while ((l_iter = l_iter->next) != l_first);
				}

				mul_m4_v3(obmat, far_vert);

#ifdef PREPEND_PLANE
				plane_from_point_normal_v3(
				        snpdt->clip.plane[0], far_vert, plane_no);

				/* Slightly move the clip plane away since there was no snap in the polygon (TODO) */
				snpdt->clip.plane[0][3] += 0.000005;
#else
				plane_from_point_normal_v3(
				        snpdt->clip.plane[snpdt->clip.plane_num -1], far_vert, plane_no);

				/* Slightly move the clip plane away since there was no snap in the polygon (TODO) */
				snpdt->clip.plane[snpdt->clip.plane_num - 1][3] += 0.000005;

#endif
//				print_v4("new_clip_plane", snpdt->clip.plane[0]);
			}
		}

		snpdt->snap_to_flag = tmp_snap_to_flag & ~SCE_SELECT_FACE;
	}


	if (snpdt->snap_to_flag) {
		BLI_assert(dist_px != NULL);
		if (snapObjectsRay(
			        sctx, snpdt,
			        params->snap_select,
			        params->use_object_edit_cage,
			        ray_depth, dist_px,
			        r_co, t_no, NULL, NULL, NULL, NULL))
		{
			is_hit = true;
		}
	}

	if (r_no) {
		copy_v3_v3(r_no, t_no);
	}
	if (r_index) {
		*r_index = t_index;
	}

	return is_hit;
}

/**
 * Convenience function for performing snapping.
 *
 * Given a 2D region value, snap to vert/edge/face.
 *
 * \param sctx: Snap context.
 * \param mval_fl: Screenspace coordinate.
 * \param dist_px: Maximum distance to snap (in pixels).
 * \param use_depth: Snap to the closest element, use when using more than one snap type.
 * \param r_co: hit location.
 * \param r_no: hit normal (optional).
 * \return Snap success
 */
bool ED_transform_snap_object_project_view3d_mixed(
        SnapObjectContext *sctx,
        const unsigned short snap_to_flag,
        const struct SnapObjectParams *params,
        const float mval_fl[2], float *dist_px,
        bool use_depth,
        float r_co[3], float r_no[3])
{
	float ray_depth = BVH_RAYCAST_DIST_MAX;

	SnapData snpdt;
	if (!snapdata_init_v3d(&snpdt, sctx, snap_to_flag, mval_fl, &ray_depth)) {
		return false;
	}

	snpdt.snap_to_flag = snap_to_flag;
	snpdt.test_occlusion = use_depth;

	bool ret = transform_snap_context_project_view3d_mixed_impl(
	        sctx, params,
	        &snpdt, dist_px, &ray_depth,
	        r_co, r_no, NULL);

	if (snpdt.clip.plane) {
		MEM_freeN(snpdt.clip.plane);
	}

	return ret;
}

bool ED_transform_snap_object_project_view3d_ex(
        SnapObjectContext *sctx,
        const unsigned short snap_to,
        const struct SnapObjectParams *params,
        const float mval[2], float *dist_px,
        float *ray_depth,
        float r_loc[3], float r_no[3], int *r_index)
{
	unsigned short snap_to_flag;
	switch (snap_to) {
		case SCE_SNAP_MODE_FACE:
			snap_to_flag = SCE_SELECT_FACE;
			break;
		case SCE_SNAP_MODE_VERTEX:
			snap_to_flag = SCE_SELECT_VERTEX;
			break;
		case SCE_SNAP_MODE_EDGE:
			snap_to_flag = SCE_SELECT_EDGE;
			break;
		default:
			return false;
	}

	float ray_depth_fallback = BVH_RAYCAST_DIST_MAX;
	if (ray_depth == NULL) {
		ray_depth = &ray_depth_fallback;
	}

	SnapData snpdt;
	if (!snapdata_init_v3d(&snpdt, sctx, snap_to_flag, mval, ray_depth)) {
		return false;
	}

	bool ret = transform_snap_context_project_view3d_mixed_impl(
	        sctx, params,
	        &snpdt, dist_px, ray_depth,
	        r_loc, r_no, r_index);

	if (snpdt.clip.plane) {
		MEM_freeN(snpdt.clip.plane);
	}

	return ret;
}

bool ED_transform_snap_object_project_view3d(
        SnapObjectContext *sctx,
        const unsigned short snap_to,
        const struct SnapObjectParams *params,
        const float mval[2], float *dist_px,
        float *ray_depth,
        float r_loc[3], float r_no[3])
{
	return ED_transform_snap_object_project_view3d_ex(
	        sctx,
	        snap_to,
	        params,
	        mval, dist_px,
	        ray_depth,
	        r_loc, r_no, NULL);
}

/**
 * see: #ED_transform_snap_object_project_ray_all
 */
bool ED_transform_snap_object_project_all_view3d_ex(
        SnapObjectContext *sctx,
        const struct SnapObjectParams *params,
        const float mval[2],
        float ray_depth, bool sort,
        ListBase *r_hit_list)
{
	float ray_start[3], ray_normal[3];

	if (!ED_view3d_win_to_ray_ex(
	        sctx->v3d_data.ar, sctx->v3d_data.v3d,
	        mval, NULL, ray_normal, ray_start, true))
	{
		return false;
	}

	return ED_transform_snap_object_project_ray_all(
	        sctx,
	        params,
	        ray_start, ray_normal, ray_depth, sort,
	        r_hit_list);
}

/** \} */
