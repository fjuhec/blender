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

/** \file blender/editors/space_view3d/view3d_bvh.c
 *  \ingroup spview3d
 */

#include "BKE_DerivedMesh.h"
#include "BKE_object.h"

#include "BLI_kdopbvh.h"
#include "BLI_listbase.h"
#include "BLI_math.h"

#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"

#include "GPU_buffers.h"

#include "MEM_guardedalloc.h"

#include "view3d_intern.h" /* own include */


static void bvh_objects_insert(View3D *v3d, const RegionView3D *rv3d, const Scene *scene)
{
	Object *ob;
	int i = 0;
	for (Base *base = scene->base.first; base; base = base->next) {
		ob = base->object;
		if (BASE_SELECTABLE(v3d, base)) {
			bool needs_freeing;
			BoundBox *bb = BKE_object_drawboundbox_get(scene, ob, &needs_freeing);
			if (bb) {
				BoundBox bb_local = *bb;
				if (needs_freeing) {
					MEM_freeN(bb);
				}

				for (int j = 0; j < 8; j++) {
					if (ob->type == OB_LAMP) {
						/* for lamps, only use location and zoom independent size */
						const float pixelsize = ED_view3d_pixel_size(rv3d, ob->obmat[3]);
						mul_v3_fl(bb_local.vec[j], pixelsize);
						add_v3_v3(bb_local.vec[j], ob->obmat[3]);
					}
					else {
						mul_m4_v3(ob->obmat, bb_local.vec[j]);
					}
				}

				BLI_bvhtree_insert(v3d->bvhtree, i++, &bb_local.vec[0][0], 8);
			}
			else {
//				printf("No BB: %s\n", base->object->id.name + 2);
			}
		}
		else {
//			printf("Not selectable: %s\n", base->object->id.name + 2);
		}
	}
}

void view3d_objectbvh_rebuild(View3D *v3d, const RegionView3D *rv3d, const Scene *scene)
{
	BVHTree *bvhtree = v3d->bvhtree;
	if (bvhtree) {
		view3d_objectbvh_free(v3d);
	}
	v3d->bvhtree = bvhtree = BLI_bvhtree_new(BLI_listbase_count(&scene->base), FLT_EPSILON, 2, 8);
	bvh_objects_insert(v3d, rv3d, scene);
	BLI_bvhtree_balance(bvhtree);
}

void view3d_objectbvh_free(View3D *v3d)
{
	BLI_bvhtree_free(v3d->bvhtree);
}

Base *view3d_objectbvh_raycast(Scene *scene, View3D *v3d, ARegion *ar, const int mval[2])
{
	RegionView3D *rv3d = ar->regiondata;
	BVHTreeNearest nearest = {.index = -1, .dist_sq = FLT_MAX};
	const float mval_fl[2] = {mval[0], mval[1]};
	float ray_start[3] = {0}, ray_normal[3] = {0};

	ED_view3d_win_to_ray(ar, v3d, mval_fl, ray_start, ray_normal, true);

	BLI_bvhtree_find_nearest_to_ray(v3d->bvhtree, ray_start, ray_normal, true, NULL, &nearest, NULL, NULL);
	/* TODO more refined geometry check */

	/* distance threshold */
	const float dist_px = sqrtf(nearest.dist_sq) / ED_view3d_pixel_size(rv3d, nearest.co);
	if (dist_px > SELECT_DIST_THRESHOLD) {
		return NULL;
	}

	int i = 0;
	Base *base;
	for (base = scene->base.first; base; base = base->next) {
		if (BASE_SELECTABLE(v3d, base)) {
			bool needs_freeing;
			BoundBox *bb = BKE_object_drawboundbox_get(scene, base->object, &needs_freeing);
			if (bb) {
				if (needs_freeing) {
					MEM_freeN(bb);
				}
				if (i == nearest.index) {
					break;
				}
				i++;
			}
		}
	}
	if (base) {
//		printf("Select: %s\n", base->object->id.name + 2);
	}
	return base;
}

static void bvh_draw_boundbox(const BVHTreeAxisRange *bounds, const bool is_leaf)
{
	float min[3] = {bounds[0].min, bounds[1].min, bounds[2].min};
	float max[3] = {bounds[0].max, bounds[1].max, bounds[2].max};
	GPU_draw_boundbox(min, max, is_leaf);
}

static bool bvh_draw_boundbox_parent_cb(const BVHTreeAxisRange *bounds, void *UNUSED(userdata))
{
	bvh_draw_boundbox(bounds, false);
	return true;
}

static bool bvh_draw_boundbox_leaf_cb(const BVHTreeAxisRange *bounds, int UNUSED(index), void *UNUSED(userdata))
{
	bvh_draw_boundbox(bounds, true);
	return true;
}

static bool bvh_walk_order_cb(const BVHTreeAxisRange *UNUSED(bounds), char UNUSED(axis), void *UNUSED(userdata))
{
	return true;
}

void view3d_bvh_draw_boundboxes(const View3D *v3d)
{
	GPU_init_draw_boundbox();
	BLI_bvhtree_walk_dfs(v3d->bvhtree, bvh_draw_boundbox_parent_cb, bvh_draw_boundbox_leaf_cb, bvh_walk_order_cb, NULL);
	GPU_end_draw_boundbox();
}
