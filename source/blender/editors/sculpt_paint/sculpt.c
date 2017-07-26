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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2006 by Nicholas Bishop
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Jason Wilkins, Tom Musgrove.
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 * Implements the Sculpt Mode tools
 *
 */

/** \file blender/editors/sculpt_paint/sculpt.c
 *  \ingroup edsculpt
 */


#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_blenlib.h"
#include "BLI_dial.h"
#include "BLI_task.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"
#include "BLI_ghash.h"

#include "BLT_translation.h"

#include "DNA_customdata_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_brush_types.h"

#include "BKE_pbvh.h"
#include "BKE_brush.h"
#include "BKE_ccg.h"
#include "BKE_context.h"
#include "BKE_depsgraph.h"
#include "BKE_global.h"
#include "BKE_image.h"
#include "BKE_key.h"
#include "BKE_library.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"
#include "BKE_modifier.h"
#include "BKE_multires.h"
#include "BKE_paint.h"
#include "BKE_report.h"
#include "BKE_node.h"
#include "BKE_object.h"
#include "BKE_subsurf.h"
#include "BKE_colortools.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_sculpt.h"
#include "ED_object.h"
#include "ED_screen.h"
#include "ED_view3d.h"
#include "paint_intern.h"
#include "sculpt_intern.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "GPU_buffers.h"
#include "GPU_extensions.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "bmesh.h"
#include "bmesh_tools.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*Maybe move silhouette GL Drawing into a seperate file*/
#include "BIF_gl.h"
#include "BIF_glutil.h"
#include "ED_space_api.h"
#include "bmesh.h"
#include "bmesh_tools.h"
#include "BKE_cdderivedmesh.h"
#include "BKE_editmesh.h"
#include "BKE_editmesh_bvh.h"
#include "ED_mesh.h"
#include "BLI_polyfill2d.h"
#include "BLI_polyfill2d_beautify.h"
#include "BLI_memarena.h"
#include "BLI_heap.h"
#include "BLI_edgehash.h"
#include "BLI_linklist.h"
#include "BLI_alloca.h"
#include "BLI_array.h"

/*#define DEBUG_DRAW*/
#ifdef DEBUG_DRAW
/* static void bl_debug_draw(void);*/
/* add these locally when using these functions for testing */
extern void bl_debug_draw_quad_clear(void);
extern void bl_debug_draw_quad_add(const float v0[3], const float v1[3], const float v2[3], const float v3[3]);
extern void bl_debug_draw_edge_add(const float v0[3], const float v1[3]);
extern void bl_debug_color_set(const unsigned int col);

static void bl_debug_draw_medge_add(Mesh *me, int e){
	bl_debug_draw_edge_add(me->mvert[me->medge[e].v1].co, me->mvert[me->medge[e].v2].co);
}

static void bl_debug_draw_point(const float pos[3],const float thickness)
{
	float h = thickness*0.5;
	float v1[] = {pos[0]-h,pos[1]-h,pos[2]};
	float v2[] = {pos[0]-h,pos[1]+h,pos[2]};
	float v3[] = {pos[0]+h,pos[1]-h,pos[2]};
	float v4[] = {pos[0]+h,pos[1]+h,pos[2]};
	bl_debug_draw_quad_add(v1,v2,v3,v4);
}
#endif

/** \name Tool Capabilities
 *
 * Avoid duplicate checks, internal logic only,
 * share logic with #rna_def_sculpt_capabilities where possible.
 *
 * \{ */

/* Check if there are any active modifiers in stack (used for flushing updates at enter/exit sculpt mode) */
static bool sculpt_has_active_modifiers(Scene *scene, Object *ob)
{
	ModifierData *md;
	VirtualModifierData virtualModifierData;

	md = modifiers_getVirtualModifierList(ob, &virtualModifierData);

	/* exception for shape keys because we can edit those */
	for (; md; md = md->next) {
		if (modifier_isEnabled(scene, md, eModifierMode_Realtime))
			return 1;
	}

	return 0;
}

static bool sculpt_tool_needs_original(const char sculpt_tool)
{
	return ELEM(sculpt_tool,
	            SCULPT_TOOL_GRAB,
	            SCULPT_TOOL_ROTATE,
	            SCULPT_TOOL_THUMB,
	            SCULPT_TOOL_LAYER);
}

static bool sculpt_tool_is_proxy_used(const char sculpt_tool)
{
	return ELEM(sculpt_tool,
	            SCULPT_TOOL_SMOOTH,
	            SCULPT_TOOL_LAYER);
}

/**
 * Test whether the #StrokeCache.sculpt_normal needs update in #do_brush_action
 */
static int sculpt_brush_needs_normal(const Brush *brush, float normal_weight)
{
	return ((SCULPT_TOOL_HAS_NORMAL_WEIGHT(brush->sculpt_tool) &&
	         (normal_weight > 0.0f)) ||

	        ELEM(brush->sculpt_tool,
	             SCULPT_TOOL_BLOB,
	             SCULPT_TOOL_CREASE,
	             SCULPT_TOOL_DRAW,
	             SCULPT_TOOL_LAYER,
	             SCULPT_TOOL_NUDGE,
	             SCULPT_TOOL_ROTATE,
	             SCULPT_TOOL_THUMB) ||

	        (brush->mtex.brush_map_mode == MTEX_MAP_MODE_AREA));
}
/** \} */

static bool sculpt_brush_needs_rake_rotation(const Brush *brush)
{
	return SCULPT_TOOL_HAS_RAKE(brush->sculpt_tool) && (brush->rake_factor != 0.0f);
}

/* Factor of brush to have rake point following behind
 * (could be configurable but this is reasonable default). */
#define SCULPT_RAKE_BRUSH_FACTOR 0.25f

struct SculptRakeData {
	float follow_dist;
	float follow_co[3];
} SculptRakeData;

typedef enum StrokeFlags {
	CLIP_X = 1,
	CLIP_Y = 2,
	CLIP_Z = 4
} StrokeFlags;

/* Cache stroke properties. Used because
 * RNA property lookup isn't particularly fast.
 *
 * For descriptions of these settings, check the operator properties.
 */
typedef struct StrokeCache {
	/* Invariants */
	float initial_radius;
	float scale[3];
	int flag;
	float clip_tolerance[3];
	float initial_mouse[2];

	/* Variants */
	float radius;
	float radius_squared;
	float true_location[3];
	float location[3];

	bool pen_flip;
	bool invert;
	float pressure;
	float mouse[2];
	float bstrength;
	float normal_weight;  /* from brush (with optional override) */

	/* The rest is temporary storage that isn't saved as a property */

	bool first_time; /* Beginning of stroke may do some things special */

	/* from ED_view3d_ob_project_mat_get() */
	float projection_mat[4][4];

	/* Clean this up! */
	struct ViewContext *vc;
	struct Brush *brush;

	float special_rotation;
	float grab_delta[3], grab_delta_symmetry[3];
	float old_grab_location[3], orig_grab_location[3];

	/* screen-space rotation defined by mouse motion */
	float   rake_rotation[4], rake_rotation_symmetry[4];
	bool is_rake_rotation_valid;
	struct SculptRakeData rake_data;

	int symmetry; /* Symmetry index between 0 and 7 bit combo 0 is Brush only;
	               * 1 is X mirror; 2 is Y mirror; 3 is XY; 4 is Z; 5 is XZ; 6 is YZ; 7 is XYZ */
	int mirror_symmetry_pass; /* the symmetry pass we are currently on between 0 and 7*/
	float true_view_normal[3];
	float view_normal[3];

	/* sculpt_normal gets calculated by calc_sculpt_normal(), then the
	 * sculpt_normal_symm gets updated quickly with the usual symmetry
	 * transforms */
	float sculpt_normal[3];
	float sculpt_normal_symm[3];

	/* Used for area texture mode, local_mat gets calculated by
	 * calc_brush_local_mat() and used in tex_strength(). */
	float brush_local_mat[4][4];
	
	float plane_offset[3]; /* used to shift the plane around when doing tiled strokes */
	int tile_pass;

	float last_center[3];
	int radial_symmetry_pass;
	float symm_rot_mat[4][4];
	float symm_rot_mat_inv[4][4];
	bool original;
	float anchored_location[3];

	float vertex_rotation; /* amount to rotate the vertices when using rotate brush */
	struct Dial *dial;
	
	char saved_active_brush_name[MAX_ID_NAME];
	char saved_mask_brush_tool;
	int saved_smooth_size; /* smooth tool copies the size of the current tool */
	bool alt_smooth;

	float plane_trim_squared;

	bool supports_gravity;
	float true_gravity_direction[3];
	float gravity_direction[3];

	rcti previous_r; /* previous redraw rectangle */
	rcti current_r; /* current redraw rectangle */
} StrokeCache;

/* reduce brush spacing step size when the geometry curves away from the view */
static void set_adaptive_space_factor(Sculpt *sd)
{
	Brush *brush = BKE_paint_brush(&(sd->paint));

	/*TODO: Reasonable 2D View 3D conversion
	 * Currently somewhere about 1bu / 200px
	 */

	brush->adaptive_space_factor = 1.0f/200.0f;
}


/************** Access to original unmodified vertex data *************/

typedef struct {
	BMLog *bm_log;

	SculptUndoNode *unode;
	float (*coords)[3];
	short (*normals)[3];
	const float *vmasks;

	/* Original coordinate, normal, and mask */
	const float *co;
	const short *no;
	float mask;
} SculptOrigVertData;


/* Initialize a SculptOrigVertData for accessing original vertex data;
 * handles BMesh, mesh, and multires */
static void sculpt_orig_vert_data_unode_init(SculptOrigVertData *data,
                                             Object *ob,
                                             SculptUndoNode *unode)
{
	SculptSession *ss = ob->sculpt;
	BMesh *bm = ss->bm;

	memset(data, 0, sizeof(*data));
	data->unode = unode;

	if (bm) {
		data->bm_log = ss->bm_log;
	}
	else {
		data->coords = data->unode->co;
		data->normals = data->unode->no;
		data->vmasks = data->unode->mask;
	}
}

/* Initialize a SculptOrigVertData for accessing original vertex data;
 * handles BMesh, mesh, and multires */
static void sculpt_orig_vert_data_init(SculptOrigVertData *data,
                                       Object *ob,
                                       PBVHNode *node)
{
	SculptUndoNode *unode;
	unode = sculpt_undo_push_node(ob, node, SCULPT_UNDO_COORDS);
	sculpt_orig_vert_data_unode_init(data, ob, unode);
}

/* Update a SculptOrigVertData for a particular vertex from the PBVH
 * iterator */
static void sculpt_orig_vert_data_update(SculptOrigVertData *orig_data,
                                         PBVHVertexIter *iter)
{
	if (orig_data->unode->type == SCULPT_UNDO_COORDS) {
		if (orig_data->bm_log) {
			BM_log_original_vert_data(
			        orig_data->bm_log, iter->bm_vert,
			        &orig_data->co, &orig_data->no);
		}
		else {
			orig_data->co = orig_data->coords[iter->i];
			orig_data->no = orig_data->normals[iter->i];
		}
	}
	else if (orig_data->unode->type == SCULPT_UNDO_MASK) {
		if (orig_data->bm_log) {
			orig_data->mask = BM_log_original_mask(orig_data->bm_log, iter->bm_vert);
		}
		else {
			orig_data->mask = orig_data->vmasks[iter->i];
		}
	}
}

static void sculpt_rake_data_update(struct SculptRakeData *srd, const float co[3])
{
	float rake_dist = len_v3v3(srd->follow_co, co);
	if (rake_dist > srd->follow_dist) {
		interp_v3_v3v3(srd->follow_co, srd->follow_co, co, rake_dist - srd->follow_dist);
	}
}


static void sculpt_rake_rotate(
        const SculptSession *ss, const float sculpt_co[3], const float v_co[3], float factor, float r_delta[3])
{
	float vec_rot[3];

#if 0
	/* lerp */
	sub_v3_v3v3(vec_rot, v_co, sculpt_co);
	mul_qt_v3(ss->cache->rake_rotation_symmetry, vec_rot);
	add_v3_v3(vec_rot, sculpt_co);
	sub_v3_v3v3(r_delta, vec_rot, v_co);
	mul_v3_fl(r_delta, factor);
#else
	/* slerp */
	float q_interp[4];
	sub_v3_v3v3(vec_rot, v_co, sculpt_co);

	copy_qt_qt(q_interp, ss->cache->rake_rotation_symmetry);
	mul_fac_qt_fl(q_interp, factor);
	mul_qt_v3(q_interp, vec_rot);

	add_v3_v3(vec_rot, sculpt_co);
	sub_v3_v3v3(r_delta, vec_rot, v_co);
#endif

}

/**
 * Align the grab delta to the brush normal.
 *
 * \param grab_delta: Typically from `ss->cache->grab_delta_symmetry`.
 */
static void sculpt_project_v3_normal_align(SculptSession *ss, const float normal_weight, float grab_delta[3])
{
	/* signed to support grabbing in (to make a hole) as well as out. */
	const float len_signed = dot_v3v3(ss->cache->sculpt_normal_symm, grab_delta);

	/* this scale effectively projects the offset so dragging follows the cursor,
	 * as the normal points towards the view, the scale increases. */
	float len_view_scale;
	{
		float view_aligned_normal[3];
		project_plane_v3_v3v3(view_aligned_normal, ss->cache->sculpt_normal_symm, ss->cache->view_normal);
		len_view_scale = fabsf(dot_v3v3(view_aligned_normal, ss->cache->sculpt_normal_symm));
		len_view_scale = (len_view_scale > FLT_EPSILON) ? 1.0f / len_view_scale : 1.0f;
	}

	mul_v3_fl(grab_delta, 1.0f - normal_weight);
	madd_v3_v3fl(grab_delta, ss->cache->sculpt_normal_symm, (len_signed * normal_weight) * len_view_scale);
}


/** \name SculptProjectVector
 *
 * Fast-path for #project_plane_v3_v3v3
 *
 * \{ */

typedef struct SculptProjectVector {
	float plane[3];
	float len_sq;
	float len_sq_inv_neg;
	bool  is_valid;

} SculptProjectVector;

/**
 * \param plane  Direction, can be any length.
 */
static void sculpt_project_v3_cache_init(
        SculptProjectVector *spvc, const float plane[3])
{
	copy_v3_v3(spvc->plane, plane);
	spvc->len_sq = len_squared_v3(spvc->plane);
	spvc->is_valid = (spvc->len_sq > FLT_EPSILON);
	spvc->len_sq_inv_neg = (spvc->is_valid) ? -1.0f / spvc->len_sq : 0.0f;
}

/**
 * Calculate the projection.
 */
static void sculpt_project_v3(
        const SculptProjectVector *spvc, const float vec[3],
        float r_vec[3])
{
#if 0
	project_plane_v3_v3v3(r_vec, vec, spvc->plane);
#else
	/* inline the projection, cache `-1.0 / dot_v3_v3(v_proj, v_proj)` */
	madd_v3_v3fl(r_vec, spvc->plane, dot_v3v3(vec, spvc->plane) * spvc->len_sq_inv_neg);
#endif
}

/** \} */


/**********************************************************************/

/* Returns true if the stroke will use dynamic topology, false
 * otherwise.
 *
 * Factors: some brushes like grab cannot do dynamic topology.
 * Others, like smooth, are better without. Same goes for alt-
 * key smoothing. */
static bool sculpt_stroke_is_dynamic_topology(
        const SculptSession *ss, const Brush *brush)
{
	return ((BKE_pbvh_type(ss->pbvh) == PBVH_BMESH) &&

	        (!ss->cache || (!ss->cache->alt_smooth)) &&

	        /* Requires mesh restore, which doesn't work with
	         * dynamic-topology */
	        !(brush->flag & BRUSH_ANCHORED) &&
	        !(brush->flag & BRUSH_DRAG_DOT) &&

	        SCULPT_TOOL_HAS_DYNTOPO(brush->sculpt_tool));
}

/*** paint mesh ***/

/* Single struct used by all BLI_task threaded callbacks, let's avoid adding 10's of those... */
typedef struct SculptThreadedTaskData {
	Sculpt *sd;
	Object *ob;
	Brush *brush;
	PBVHNode **nodes;
	int totnode;

	/* Data specific to some callbacks. */
	/* Note: even if only one or two of those are used at a time, keeping them separated, names help figuring out
	 *       what it is, and memory overhead is ridiculous anyway... */
	float flippedbstrength;
	float angle;
	float strength;
	bool smooth_mask;
	bool has_bm_orco;

	SculptProjectVector *spvc;
	float *offset;
	float *grab_delta;
	float *cono;
	float *area_no;
	float *area_no_sp;
	float *area_co;
	float (*mat)[4];
	float (*vertCos)[3];

	/* 0=towards view, 1=flipped */
	float (*area_cos)[3];
	float (*area_nos)[3];
	int *count;

	ThreadMutex mutex;
} SculptThreadedTaskData;

static void paint_mesh_restore_co_task_cb(void *userdata, const int n)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;

	SculptUndoNode *unode;
	SculptUndoType type = (data->brush->sculpt_tool == SCULPT_TOOL_MASK ? SCULPT_UNDO_MASK : SCULPT_UNDO_COORDS);

	if (ss->bm) {
		unode = sculpt_undo_push_node(data->ob, data->nodes[n], type);
	}
	else {
		unode = sculpt_undo_get_node(data->nodes[n]);
	}

	if (unode) {
		PBVHVertexIter vd;
		SculptOrigVertData orig_data;

		sculpt_orig_vert_data_unode_init(&orig_data, data->ob, unode);

		BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
		{
			sculpt_orig_vert_data_update(&orig_data, &vd);

			if (orig_data.unode->type == SCULPT_UNDO_COORDS) {
				copy_v3_v3(vd.co, orig_data.co);
				if (vd.no)
					copy_v3_v3_short(vd.no, orig_data.no);
				else
					normal_short_to_float_v3(vd.fno, orig_data.no);
			}
			else if (orig_data.unode->type == SCULPT_UNDO_MASK) {
				*vd.mask = orig_data.mask;
			}

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
		BKE_pbvh_vertex_iter_end;

		BKE_pbvh_node_mark_update(data->nodes[n]);
	}
}

static void paint_mesh_restore_co(Sculpt *sd, Object *ob)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);

	PBVHNode **nodes;
	int totnode;

	BKE_pbvh_search_gather(ss->pbvh, NULL, NULL, &nodes, &totnode);

	/* Disable OpenMP when dynamic-topology is enabled. Otherwise, new entries might be inserted by
	 * sculpt_undo_push_node() into the GHash used internally by BM_log_original_vert_co() by a different thread.
	 * See T33787. */
	SculptThreadedTaskData data = {
		.sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	};

	BLI_task_parallel_range(
	            0, totnode, &data, paint_mesh_restore_co_task_cb,
	            ((sd->flags & SCULPT_USE_OPENMP) && !ss->bm && totnode > SCULPT_THREADED_LIMIT));

	if (nodes)
		MEM_freeN(nodes);
}

/*** BVH Tree ***/

static void sculpt_extend_redraw_rect_previous(Object *ob, rcti *rect)
{
	/* expand redraw rect with redraw rect from previous step to
	 * prevent partial-redraw issues caused by fast strokes. This is
	 * needed here (not in sculpt_flush_update) as it was before
	 * because redraw rectangle should be the same in both of
	 * optimized PBVH draw function and 3d view redraw (if not -- some
	 * mesh parts could disappear from screen (sergey) */
	SculptSession *ss = ob->sculpt;

	if (ss->cache) {
		if (!BLI_rcti_is_empty(&ss->cache->previous_r))
			BLI_rcti_union(rect, &ss->cache->previous_r);
	}
}

/* Get a screen-space rectangle of the modified area */
static bool sculpt_get_redraw_rect(ARegion *ar, RegionView3D *rv3d,
                                   Object *ob, rcti *rect)
{
	PBVH *pbvh = ob->sculpt->pbvh;
	float bb_min[3], bb_max[3];

	if (!pbvh)
		return 0;

	BKE_pbvh_redraw_BB(pbvh, bb_min, bb_max);

	/* convert 3D bounding box to screen space */
	if (!paint_convert_bb_to_rect(rect,
	                              bb_min,
	                              bb_max,
	                              ar,
	                              rv3d,
	                              ob))
	{
		return 0;
	}


	return 1;
}

void ED_sculpt_redraw_planes_get(float planes[4][4], ARegion *ar,
                                 RegionView3D *rv3d, Object *ob)
{
	PBVH *pbvh = ob->sculpt->pbvh;
	/* copy here, original will be used below */
	rcti rect = ob->sculpt->cache->current_r;

	sculpt_extend_redraw_rect_previous(ob, &rect);

	paint_calc_redraw_planes(planes, ar, rv3d, ob, &rect);

	/* we will draw this rect, so now we can set it as the previous partial rect.
	 * Note that we don't update with the union of previous/current (rect), only with
	 * the current. Thus we avoid the rectangle needlessly growing to include
	 * all the stroke area */
	ob->sculpt->cache->previous_r = ob->sculpt->cache->current_r;

	/* clear redraw flag from nodes */
	if (pbvh)
		BKE_pbvh_update(pbvh, PBVH_UpdateRedraw, NULL);
}

/************************ Brush Testing *******************/

typedef struct SculptBrushTest {
	float radius_squared;
	float location[3];
	float dist;
	int mirror_symmetry_pass;

	/* View3d clipping - only set rv3d for clipping */
	RegionView3D *clip_rv3d;
} SculptBrushTest;

static void sculpt_brush_test_init(SculptSession *ss, SculptBrushTest *test)
{
	RegionView3D *rv3d = ss->cache->vc->rv3d;

	test->radius_squared = ss->cache->radius_squared;
	copy_v3_v3(test->location, ss->cache->location);
	test->dist = 0.0f;   /* just for initialize */

	test->mirror_symmetry_pass = ss->cache->mirror_symmetry_pass;

	if (rv3d->rflag & RV3D_CLIPPING) {
		test->clip_rv3d = rv3d;
	}
	else {
		test->clip_rv3d = NULL;
	}
}

BLI_INLINE bool sculpt_brush_test_clipping(const SculptBrushTest *test, const float co[3])
{
	RegionView3D *rv3d = test->clip_rv3d;
	if (!rv3d) {
		return false;
	}
	float symm_co[3];
	flip_v3_v3(symm_co, co, test->mirror_symmetry_pass);
	return ED_view3d_clipping_test(rv3d, symm_co, true);
}

static bool sculpt_brush_test(SculptBrushTest *test, const float co[3])
{
	float distsq = len_squared_v3v3(co, test->location);

	if (distsq <= test->radius_squared) {
		if (sculpt_brush_test_clipping(test, co)) {
			return 0;
		}
		test->dist = sqrtf(distsq);
		return 1;
	}
	else {
		return 0;
	}
}

static bool sculpt_brush_test_sq(SculptBrushTest *test, const float co[3])
{
	float distsq = len_squared_v3v3(co, test->location);

	if (distsq <= test->radius_squared) {
		if (sculpt_brush_test_clipping(test, co)) {
			return 0;
		}
		test->dist = distsq;
		return 1;
	}
	else {
		return 0;
	}
}

static bool sculpt_brush_test_fast(const SculptBrushTest *test, const float co[3])
{
	if (sculpt_brush_test_clipping(test, co)) {
		return 0;
	}
	return len_squared_v3v3(co, test->location) <= test->radius_squared;
}

static bool sculpt_brush_test_cube(SculptBrushTest *test, const float co[3], float local[4][4])
{
	float side = M_SQRT1_2;
	float local_co[3];

	if (sculpt_brush_test_clipping(test, co)) {
		return 0;
	}

	mul_v3_m4v3(local_co, local, co);

	local_co[0] = fabsf(local_co[0]);
	local_co[1] = fabsf(local_co[1]);
	local_co[2] = fabsf(local_co[2]);

	if (local_co[0] <= side && local_co[1] <= side && local_co[2] <= side) {
		float p = 4.0f;
		
		test->dist = ((powf(local_co[0], p) +
		               powf(local_co[1], p) +
		               powf(local_co[2], p)) / powf(side, p));

		return 1;
	}
	else {
		return 0;
	}
}

static float frontface(Brush *br, const float sculpt_normal[3],
                       const short no[3], const float fno[3])
{
	if (br->flag & BRUSH_FRONTFACE) {
		float dot;

		if (no) {
			float tmp[3];

			normal_short_to_float_v3(tmp, no);
			dot = dot_v3v3(tmp, sculpt_normal);
		}
		else {
			dot = dot_v3v3(fno, sculpt_normal);
		}
		return dot > 0 ? dot : 0;
	}
	else {
		return 1;
	}
}

#if 0

static bool sculpt_brush_test_cyl(SculptBrushTest *test, float co[3], float location[3], const float area_no[3])
{
	if (sculpt_brush_test_fast(test, co)) {
		float t1[3], t2[3], t3[3], dist;

		sub_v3_v3v3(t1, location, co);
		sub_v3_v3v3(t2, x2, location);

		cross_v3_v3v3(t3, area_no, t1);

		dist = len_v3(t3) / len_v3(t2);

		test->dist = dist;

		return 1;
	}

	return 0;
}

#endif

/* ===== Sculpting =====
 *
 */
static void flip_v3(float v[3], const char symm)
{
	flip_v3_v3(v, v, symm);
}

static float calc_overlap(StrokeCache *cache, const char symm, const char axis, const float angle)
{
	float mirror[3];
	float distsq;
	
	/* flip_v3_v3(mirror, cache->traced_location, symm); */
	flip_v3_v3(mirror, cache->true_location, symm);

	if (axis != 0) {
		float mat[3][3];
		axis_angle_to_mat3_single(mat, axis, angle);
		mul_m3_v3(mat, mirror);
	}

	/* distsq = len_squared_v3v3(mirror, cache->traced_location); */
	distsq = len_squared_v3v3(mirror, cache->true_location);

	if (distsq <= 4.0f * (cache->radius_squared))
		return (2.0f * (cache->radius) - sqrtf(distsq)) / (2.0f * (cache->radius));
	else
		return 0;
}

static float calc_radial_symmetry_feather(Sculpt *sd, StrokeCache *cache, const char symm, const char axis)
{
	int i;
	float overlap;

	overlap = 0;
	for (i = 1; i < sd->radial_symm[axis - 'X']; ++i) {
		const float angle = 2 * M_PI * i / sd->radial_symm[axis - 'X'];
		overlap += calc_overlap(cache, symm, axis, angle);
	}

	return overlap;
}

static float calc_symmetry_feather(Sculpt *sd, StrokeCache *cache)
{
	if (sd->paint.symmetry_flags & PAINT_SYMMETRY_FEATHER) {
		float overlap;
		int symm = cache->symmetry;
		int i;

		overlap = 0;
		for (i = 0; i <= symm; i++) {
			if (i == 0 || (symm & i && (symm != 5 || i != 3) && (symm != 6 || (i != 3 && i != 5)))) {

				overlap += calc_overlap(cache, i, 0, 0);

				overlap += calc_radial_symmetry_feather(sd, cache, i, 'X');
				overlap += calc_radial_symmetry_feather(sd, cache, i, 'Y');
				overlap += calc_radial_symmetry_feather(sd, cache, i, 'Z');
			}
		}

		return 1 / overlap;
	}
	else {
		return 1;
	}
}

/** \name Calculate Normal and Center
 *
 * Calculate geometry surrounding the brush center.
 * (optionally using original coordinates).
 *
 * Functions are:
 * - #calc_area_center
 * - #calc_area_normal
 * - #calc_area_normal_and_center
 *
 * \note These are all _very_ similar, when changing one, check others.
 * \{ */

static void calc_area_normal_and_center_task_cb(void *userdata, const int n)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	float (*area_nos)[3] = data->area_nos;
	float (*area_cos)[3] = data->area_cos;

	PBVHVertexIter vd;
	SculptBrushTest test;
	SculptUndoNode *unode;

	float private_co[2][3] = {{0.0f}};
	float private_no[2][3] = {{0.0f}};
	int   private_count[2] = {0};
	bool use_original;

	unode = sculpt_undo_push_node(data->ob, data->nodes[n], SCULPT_UNDO_COORDS);
	sculpt_brush_test_init(ss, &test);

	use_original = (ss->cache->original && (unode->co || unode->bm_entry));

	/* when the mesh is edited we can't rely on original coords
	 * (original mesh may not even have verts in brush radius) */
	if (use_original && data->has_bm_orco) {
		float (*orco_coords)[3];
		int   (*orco_tris)[3];
		int     orco_tris_num;
		int i;

		BKE_pbvh_node_get_bm_orco_data(data->nodes[n], &orco_tris, &orco_tris_num, &orco_coords);

		for (i = 0; i < orco_tris_num; i++) {
			const float *co_tri[3] = {
			    orco_coords[orco_tris[i][0]],
			    orco_coords[orco_tris[i][1]],
			    orco_coords[orco_tris[i][2]],
			};
			float co[3];

			closest_on_tri_to_point_v3(co, test.location, UNPACK3(co_tri));

			if (sculpt_brush_test_fast(&test, co)) {
				float no[3];
				int flip_index;

				normal_tri_v3(no, UNPACK3(co_tri));

				flip_index = (dot_v3v3(ss->cache->view_normal, no) <= 0.0f);
				if (area_cos)
					add_v3_v3(private_co[flip_index], co);
				if (area_nos)
					add_v3_v3(private_no[flip_index], no);
				private_count[flip_index] += 1;
			}
		}
	}
	else {
		BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
		{
			const float *co;
			const short *no_s;  /* bm_vert only */

			if (use_original) {
				if (unode->bm_entry) {
					BM_log_original_vert_data(ss->bm_log, vd.bm_vert, &co, &no_s);
				}
				else {
					co = unode->co[vd.i];
					no_s = unode->no[vd.i];
				}
			}
			else {
				co = vd.co;
			}

			if (sculpt_brush_test_fast(&test, co)) {
				float no_buf[3];
				const float *no;
				int flip_index;

				if (use_original) {
					normal_short_to_float_v3(no_buf, no_s);
					no = no_buf;
				}
				else {
					if (vd.no) {
						normal_short_to_float_v3(no_buf, vd.no);
						no = no_buf;
					}
					else {
						no = vd.fno;
					}
				}

				flip_index = (dot_v3v3(ss->cache->view_normal, no) <= 0.0f);
				if (area_cos)
					add_v3_v3(private_co[flip_index], co);
				if (area_nos)
					add_v3_v3(private_no[flip_index], no);
				private_count[flip_index] += 1;
			}
		}
		BKE_pbvh_vertex_iter_end;
	}

	BLI_mutex_lock(&data->mutex);

	/* for flatten center */
	if (area_cos) {
		add_v3_v3(area_cos[0], private_co[0]);
		add_v3_v3(area_cos[1], private_co[1]);
	}

	/* for area normal */
	if (area_nos) {
		add_v3_v3(area_nos[0], private_no[0]);
		add_v3_v3(area_nos[1], private_no[1]);
	}

	/* weights */
	data->count[0] += private_count[0];
	data->count[1] += private_count[1];

	BLI_mutex_unlock(&data->mutex);
}

static void calc_area_center(
        Sculpt *sd, Object *ob,
        PBVHNode **nodes, int totnode,
        float r_area_co[3])
{
	const Brush *brush = BKE_paint_brush(&sd->paint);
	SculptSession *ss = ob->sculpt;
	const bool has_bm_orco = ss->bm && sculpt_stroke_is_dynamic_topology(ss, brush);
	int n;

	/* 0=towards view, 1=flipped */
	float area_cos[2][3] = {{0.0f}};

	int count[2] = {0};

	SculptThreadedTaskData data = {
		.sd = sd, .ob = ob, .nodes = nodes, .totnode = totnode,
		.has_bm_orco = has_bm_orco, .area_cos = area_cos, .area_nos = NULL, .count = count,
	};
	BLI_mutex_init(&data.mutex);

	BLI_task_parallel_range(
	            0, totnode, &data, calc_area_normal_and_center_task_cb,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT));

	BLI_mutex_end(&data.mutex);

	/* for flatten center */
	for (n = 0; n < ARRAY_SIZE(area_cos); n++) {
		if (count[n] != 0) {
			mul_v3_v3fl(r_area_co, area_cos[n], 1.0f / count[n]);
			break;
		}
	}
	if (n == 2) {
		zero_v3(r_area_co);
	}
}


static void calc_area_normal(
        Sculpt *sd, Object *ob,
        PBVHNode **nodes, int totnode,
        float r_area_no[3])
{
	const Brush *brush = BKE_paint_brush(&sd->paint);
	SculptSession *ss = ob->sculpt;
	const bool has_bm_orco = ss->bm && sculpt_stroke_is_dynamic_topology(ss, brush);
	int n;

	/* 0=towards view, 1=flipped */
	float area_nos[2][3] = {{0.0f}};

	int count[2] = {0};

	SculptThreadedTaskData data = {
		.sd = sd, .ob = ob, .nodes = nodes, .totnode = totnode,
		.has_bm_orco = has_bm_orco, .area_cos = NULL, .area_nos = area_nos, .count = count,
	};
	BLI_mutex_init(&data.mutex);

	BLI_task_parallel_range(
	            0, totnode, &data, calc_area_normal_and_center_task_cb,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT));

	BLI_mutex_end(&data.mutex);

	/* for area normal */
	for (n = 0; n < ARRAY_SIZE(area_nos); n++) {
		if (normalize_v3_v3(r_area_no, area_nos[n]) != 0.0f) {
			break;
		}
	}
}

/* this calculates flatten center and area normal together,
 * amortizing the memory bandwidth and loop overhead to calculate both at the same time */
static void calc_area_normal_and_center(
        Sculpt *sd, Object *ob,
        PBVHNode **nodes, int totnode,
        float r_area_no[3], float r_area_co[3])
{
	const Brush *brush = BKE_paint_brush(&sd->paint);
	SculptSession *ss = ob->sculpt;
	const bool has_bm_orco = ss->bm && sculpt_stroke_is_dynamic_topology(ss, brush);
	int n;

	/* 0=towards view, 1=flipped */
	float area_cos[2][3] = {{0.0f}};
	float area_nos[2][3] = {{0.0f}};

	int count[2] = {0};

	SculptThreadedTaskData data = {
		.sd = sd, .ob = ob, .nodes = nodes, .totnode = totnode,
		.has_bm_orco = has_bm_orco, .area_cos = area_cos, .area_nos = area_nos, .count = count,
	};
	BLI_mutex_init(&data.mutex);

	BLI_task_parallel_range(
	            0, totnode, &data, calc_area_normal_and_center_task_cb,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT));

	BLI_mutex_end(&data.mutex);

	/* for flatten center */
	for (n = 0; n < ARRAY_SIZE(area_cos); n++) {
		if (count[n] != 0) {
			mul_v3_v3fl(r_area_co, area_cos[n], 1.0f / count[n]);
			break;
		}
	}
	if (n == 2) {
		zero_v3(r_area_co);
	}

	/* for area normal */
	for (n = 0; n < ARRAY_SIZE(area_nos); n++) {
		if (normalize_v3_v3(r_area_no, area_nos[n]) != 0.0f) {
			break;
		}
	}
}

/** \} */


/* Return modified brush strength. Includes the direction of the brush, positive
 * values pull vertices, negative values push. Uses tablet pressure and a
 * special multiplier found experimentally to scale the strength factor. */
static float brush_strength(
        const Sculpt *sd, const StrokeCache *cache,
        const float feather, const UnifiedPaintSettings *ups)
{
	const Scene *scene = cache->vc->scene;
	const Brush *brush = BKE_paint_brush((Paint *)&sd->paint);

	/* Primary strength input; square it to make lower values more sensitive */
	const float root_alpha = BKE_brush_alpha_get(scene, brush);
	float alpha        = root_alpha * root_alpha;
	float dir          = (brush->flag & BRUSH_DIR_IN) ? -1 : 1;
	float pressure     = BKE_brush_use_alpha_pressure(scene, brush) ? cache->pressure : 1;
	float pen_flip     = cache->pen_flip ? -1 : 1;
	float invert       = cache->invert ? -1 : 1;
	float overlap       = ups->overlap_factor;
	/* spacing is integer percentage of radius, divide by 50 to get
	 * normalized diameter */

	float flip         = dir * invert * pen_flip;

	switch (brush->sculpt_tool) {
		case SCULPT_TOOL_CLAY:
		case SCULPT_TOOL_CLAY_STRIPS:
		case SCULPT_TOOL_DRAW:
		case SCULPT_TOOL_LAYER:
			return alpha * flip * pressure * overlap * feather;
			
		case SCULPT_TOOL_MASK:
			overlap = (1 + overlap) / 2;
			switch ((BrushMaskTool)brush->mask_tool) {
				case BRUSH_MASK_DRAW:
					return alpha * flip * pressure * overlap * feather;
				case BRUSH_MASK_SMOOTH:
					return alpha * pressure * feather;
			}

		case SCULPT_TOOL_CREASE:
		case SCULPT_TOOL_BLOB:
			return alpha * flip * pressure * overlap * feather;

		case SCULPT_TOOL_INFLATE:
			if (flip > 0) {
				return 0.250f * alpha * flip * pressure * overlap * feather;
			}
			else {
				return 0.125f * alpha * flip * pressure * overlap * feather;
			}

		case SCULPT_TOOL_FILL:
		case SCULPT_TOOL_SCRAPE:
		case SCULPT_TOOL_FLATTEN:
			if (flip > 0) {
				overlap = (1 + overlap) / 2;
				return alpha * flip * pressure * overlap * feather;
			}
			else {
				/* reduce strength for DEEPEN, PEAKS, and CONTRAST */
				return 0.5f * alpha * flip * pressure * overlap * feather; 
			}

		case SCULPT_TOOL_SMOOTH:
			return alpha * pressure * feather;

		case SCULPT_TOOL_PINCH:
			if (flip > 0) {
				return alpha * flip * pressure * overlap * feather;
			}
			else {
				return 0.25f * alpha * flip * pressure * overlap * feather;
			}

		case SCULPT_TOOL_NUDGE:
			overlap = (1 + overlap) / 2;
			return alpha * pressure * overlap * feather;

		case SCULPT_TOOL_THUMB:
			return alpha * pressure * feather;

		case SCULPT_TOOL_SNAKE_HOOK:
			return root_alpha * feather;

		case SCULPT_TOOL_GRAB:
			return root_alpha * feather;

		case SCULPT_TOOL_ROTATE:
			return alpha * pressure * feather;

		default:
			return 0;
	}
}

/* Return a multiplier for brush strength on a particular vertex. */
static float tex_strength(SculptSession *ss, Brush *br,
                          const float brush_point[3],
                          const float len,
                          const short vno[3],
                          const float fno[3],
                          const float mask,
                          const int thread_id)
{
	StrokeCache *cache = ss->cache;
	const Scene *scene = cache->vc->scene;
	MTex *mtex = &br->mtex;
	float avg = 1;
	float rgba[4];
	float point[3];

	sub_v3_v3v3(point, brush_point, cache->plane_offset);

	if (!mtex->tex) {
		avg = 1;
	}
	else if (mtex->brush_map_mode == MTEX_MAP_MODE_3D) {
		/* Get strength by feeding the vertex 
		 * location directly into a texture */
		avg = BKE_brush_sample_tex_3D(scene, br, point, rgba, 0, ss->tex_pool);
	}
	else if (ss->texcache) {
		float symm_point[3], point_2d[2];
		float x = 0.0f, y = 0.0f; /* Quite warnings */

		/* if the active area is being applied for symmetry, flip it
		 * across the symmetry axis and rotate it back to the original
		 * position in order to project it. This insures that the 
		 * brush texture will be oriented correctly. */

		flip_v3_v3(symm_point, point, cache->mirror_symmetry_pass);

		if (cache->radial_symmetry_pass)
			mul_m4_v3(cache->symm_rot_mat_inv, symm_point);

		ED_view3d_project_float_v2_m4(cache->vc->ar, symm_point, point_2d, cache->projection_mat);

		/* still no symmetry supported for other paint modes.
		 * Sculpt does it DIY */
		if (mtex->brush_map_mode == MTEX_MAP_MODE_AREA) {
			/* Similar to fixed mode, but projects from brush angle
			 * rather than view direction */

			mul_m4_v3(cache->brush_local_mat, symm_point);

			x = symm_point[0];
			y = symm_point[1];

			x *= br->mtex.size[0];
			y *= br->mtex.size[1];

			x += br->mtex.ofs[0];
			y += br->mtex.ofs[1];

			avg = paint_get_tex_pixel(&br->mtex, x, y, ss->tex_pool, thread_id);

			avg += br->texture_sample_bias;
		}
		else {
			const float point_3d[3] = {point_2d[0], point_2d[1], 0.0f};
			avg = BKE_brush_sample_tex_3D(scene, br, point_3d, rgba, 0, ss->tex_pool);
		}
	}

	/* Falloff curve */
	avg *= BKE_brush_curve_strength(br, len, cache->radius);

	avg *= frontface(br, cache->view_normal, vno, fno);

	/* Paint mask */
	avg *= 1.0f - mask;

	return avg;
}

typedef struct {
	Sculpt *sd;
	SculptSession *ss;
	float radius_squared;
	bool original;
} SculptSearchSphereData;

/* Test AABB against sphere */
static bool sculpt_search_sphere_cb(PBVHNode *node, void *data_v)
{
	SculptSearchSphereData *data = data_v;
	float *center = data->ss->cache->location, nearest[3];
	float t[3], bb_min[3], bb_max[3];
	int i;

	if (data->original)
		BKE_pbvh_node_get_original_BB(node, bb_min, bb_max);
	else
		BKE_pbvh_node_get_BB(node, bb_min, bb_max);
	
	for (i = 0; i < 3; ++i) {
		if (bb_min[i] > center[i])
			nearest[i] = bb_min[i];
		else if (bb_max[i] < center[i])
			nearest[i] = bb_max[i];
		else
			nearest[i] = center[i]; 
	}
	
	sub_v3_v3v3(t, center, nearest);

	return len_squared_v3(t) < data->radius_squared;
}

/* Handles clipping against a mirror modifier and SCULPT_LOCK axis flags */
static void sculpt_clip(Sculpt *sd, SculptSession *ss, float co[3], const float val[3])
{
	int i;

	for (i = 0; i < 3; ++i) {
		if (sd->flags & (SCULPT_LOCK_X << i))
			continue;

		if ((ss->cache->flag & (CLIP_X << i)) && (fabsf(co[i]) <= ss->cache->clip_tolerance[i]))
			co[i] = 0.0f;
		else
			co[i] = val[i];
	}
}

/* Calculate primary direction of movement for many brushes */
static void calc_sculpt_normal(
        Sculpt *sd, Object *ob,
        PBVHNode **nodes, int totnode,
        float r_area_no[3])
{
	const Brush *brush = BKE_paint_brush(&sd->paint);
	const SculptSession *ss = ob->sculpt;

	switch (brush->sculpt_plane) {
		case SCULPT_DISP_DIR_VIEW:
			copy_v3_v3(r_area_no, ss->cache->true_view_normal);
			break;

		case SCULPT_DISP_DIR_X:
			ARRAY_SET_ITEMS(r_area_no, 1, 0, 0);
			break;

		case SCULPT_DISP_DIR_Y:
			ARRAY_SET_ITEMS(r_area_no, 0, 1, 0);
			break;

		case SCULPT_DISP_DIR_Z:
			ARRAY_SET_ITEMS(r_area_no, 0, 0, 1);
			break;

		case SCULPT_DISP_DIR_AREA:
			calc_area_normal(sd, ob, nodes, totnode, r_area_no);
			break;

		default:
			break;
	}
}

static void update_sculpt_normal(Sculpt *sd, Object *ob,
                                 PBVHNode **nodes, int totnode)
{
	const Brush *brush = BKE_paint_brush(&sd->paint);
	StrokeCache *cache = ob->sculpt->cache;
	
	if (cache->mirror_symmetry_pass == 0 &&
	    cache->radial_symmetry_pass == 0 &&
	    (cache->first_time || !(brush->flag & BRUSH_ORIGINAL_NORMAL)))
	{
		calc_sculpt_normal(sd, ob, nodes, totnode, cache->sculpt_normal);
		copy_v3_v3(cache->sculpt_normal_symm, cache->sculpt_normal);
	}
	else {
		copy_v3_v3(cache->sculpt_normal_symm, cache->sculpt_normal);
		flip_v3(cache->sculpt_normal_symm, cache->mirror_symmetry_pass);
		mul_m4_v3(cache->symm_rot_mat, cache->sculpt_normal_symm);
	}
}

static void calc_local_y(ViewContext *vc, const float center[3], float y[3])
{
	Object *ob = vc->obact;
	float loc[3], mval_f[2] = {0.0f, 1.0f};
	float zfac;

	mul_v3_m4v3(loc, ob->imat, center);
	zfac = ED_view3d_calc_zfac(vc->rv3d, loc, NULL);

	ED_view3d_win_to_delta(vc->ar, mval_f, y, zfac);
	normalize_v3(y);

	add_v3_v3(y, ob->loc);
	mul_m4_v3(ob->imat, y);
}

static void calc_brush_local_mat(const Brush *brush, Object *ob,
                                 float local_mat[4][4])
{
	const StrokeCache *cache = ob->sculpt->cache;
	float tmat[4][4];
	float mat[4][4];
	float scale[4][4];
	float angle, v[3];
	float up[3];

	/* Ensure ob->imat is up to date */
	invert_m4_m4(ob->imat, ob->obmat);

	/* Initialize last column of matrix */
	mat[0][3] = 0;
	mat[1][3] = 0;
	mat[2][3] = 0;
	mat[3][3] = 1;

	/* Get view's up vector in object-space */
	calc_local_y(cache->vc, cache->location, up);

	/* Calculate the X axis of the local matrix */
	cross_v3_v3v3(v, up, cache->sculpt_normal);
	/* Apply rotation (user angle, rake, etc.) to X axis */
	angle = brush->mtex.rot - cache->special_rotation;
	rotate_v3_v3v3fl(mat[0], v, cache->sculpt_normal, angle);

	/* Get other axes */
	cross_v3_v3v3(mat[1], cache->sculpt_normal, mat[0]);
	copy_v3_v3(mat[2], cache->sculpt_normal);

	/* Set location */
	copy_v3_v3(mat[3], cache->location);

	/* Scale by brush radius */
	normalize_m4(mat);
	scale_m4_fl(scale, cache->radius);
	mul_m4_m4m4(tmat, mat, scale);

	/* Return inverse (for converting from modelspace coords to local
	 * area coords) */
	invert_m4_m4(local_mat, tmat);
}

static void update_brush_local_mat(Sculpt *sd, Object *ob)
{
	StrokeCache *cache = ob->sculpt->cache;

	if (cache->mirror_symmetry_pass == 0 &&
	    cache->radial_symmetry_pass == 0)
	{
		calc_brush_local_mat(BKE_paint_brush(&sd->paint), ob,
		                     cache->brush_local_mat);
	}
}

/* For the smooth brush, uses the neighboring vertices around vert to calculate
 * a smoothed location for vert. Skips corner vertices (used by only one
 * polygon.) */
static void neighbor_average(SculptSession *ss, float avg[3], unsigned vert)
{
	const MeshElemMap *vert_map = &ss->pmap[vert];
	const MVert *mvert = ss->mvert;
	float (*deform_co)[3] = ss->deform_cos;

	/* Don't modify corner vertices */
	if (vert_map->count > 1) {
		int i, total = 0;

		zero_v3(avg);

		for (i = 0; i < vert_map->count; i++) {
			const MPoly *p = &ss->mpoly[vert_map->indices[i]];
			unsigned f_adj_v[2];

			if (poly_get_adj_loops_from_vert(p, ss->mloop, vert, f_adj_v) != -1) {
				int j;
				for (j = 0; j < ARRAY_SIZE(f_adj_v); j += 1) {
					if (vert_map->count != 2 || ss->pmap[f_adj_v[j]].count <= 2) {
						add_v3_v3(avg, deform_co ? deform_co[f_adj_v[j]] :
						          mvert[f_adj_v[j]].co);

						total++;
					}
				}
			}
		}

		if (total > 0) {
			mul_v3_fl(avg, 1.0f / total);
			return;
		}
	}

	copy_v3_v3(avg, deform_co ? deform_co[vert] : mvert[vert].co);
}

/* Similar to neighbor_average(), but returns an averaged mask value
 * instead of coordinate. Also does not restrict based on border or
 * corner vertices. */
static float neighbor_average_mask(SculptSession *ss, unsigned vert)
{
	const float *vmask = ss->vmask;
	float avg = 0;
	int i, total = 0;

	for (i = 0; i < ss->pmap[vert].count; i++) {
		const MPoly *p = &ss->mpoly[ss->pmap[vert].indices[i]];
		unsigned f_adj_v[2];

		if (poly_get_adj_loops_from_vert(p, ss->mloop, vert, f_adj_v) != -1) {
			int j;
			for (j = 0; j < ARRAY_SIZE(f_adj_v); j += 1) {
				avg += vmask[f_adj_v[j]];
				total++;
			}
		}
	}

	if (total > 0)
		return avg / (float)total;
	else
		return vmask[vert];
}

/* Same logic as neighbor_average(), but for bmesh rather than mesh */
static void bmesh_neighbor_average(float avg[3], BMVert *v)
{
	/* logic for 3 or more is identical */
	const int vfcount = BM_vert_face_count_ex(v, 3);

	/* Don't modify corner vertices */
	if (vfcount > 1) {
		BMIter liter;
		BMLoop *l;
		int i, total = 0;

		zero_v3(avg);

		BM_ITER_ELEM (l, &liter, v, BM_LOOPS_OF_VERT) {
			const BMVert *adj_v[2] = {l->prev->v, l->next->v};

			for (i = 0; i < ARRAY_SIZE(adj_v); i++) {
				const BMVert *v_other = adj_v[i];
				if (vfcount != 2 || BM_vert_face_count_ex(v_other, 2) <= 2) {
					add_v3_v3(avg, v_other->co);
					total++;
				}
			}
		}

		if (total > 0) {
			mul_v3_fl(avg, 1.0f / total);
			return;
		}
	}

	copy_v3_v3(avg, v->co);
}

/* Same logic as neighbor_average_mask(), but for bmesh rather than mesh */
static float bmesh_neighbor_average_mask(BMVert *v, const int cd_vert_mask_offset)
{
	BMIter liter;
	BMLoop *l;
	float avg = 0;
	int i, total = 0;

	BM_ITER_ELEM (l, &liter, v, BM_LOOPS_OF_VERT) {
		/* skip this vertex */
		const BMVert *adj_v[2] = {l->prev->v, l->next->v};

		for (i = 0; i < ARRAY_SIZE(adj_v); i++) {
			const BMVert *v_other = adj_v[i];
			const float *vmask = BM_ELEM_CD_GET_VOID_P(v_other, cd_vert_mask_offset);
			avg += (*vmask);
			total++;
		}
	}

	if (total > 0) {
		return avg / (float)total;
	}
	else {
		const float *vmask = BM_ELEM_CD_GET_VOID_P(v, cd_vert_mask_offset);
		return (*vmask);
	}
}

/* Note: uses after-struct allocated mem to store actual cache... */
typedef struct SculptDoBrushSmoothGridDataChunk {
	size_t tmpgrid_size;
} SculptDoBrushSmoothGridDataChunk;

static void do_smooth_brush_mesh_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Sculpt *sd = data->sd;
	Brush *brush = data->brush;
	const bool smooth_mask = data->smooth_mask;
	float bstrength = data->strength;

	PBVHVertexIter vd;
	SculptBrushTest test;

	CLAMP(bstrength, 0.0f, 1.0f);

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test(&test, vd.co)) {
			const float fade = bstrength * tex_strength(
			                       ss, brush, vd.co, test.dist, vd.no, vd.fno,
			                       smooth_mask ? 0.0f : (vd.mask ? *vd.mask : 0.0f),
			                       thread_id);
			if (smooth_mask) {
				float val = neighbor_average_mask(ss, vd.vert_indices[vd.i]) - *vd.mask;
				val *= fade * bstrength;
				*vd.mask += val;
				CLAMP(*vd.mask, 0.0f, 1.0f);
			}
			else {
				float avg[3], val[3];

				neighbor_average(ss, avg, vd.vert_indices[vd.i]);
				sub_v3_v3v3(val, avg, vd.co);

				madd_v3_v3v3fl(val, vd.co, val, fade);

				sculpt_clip(sd, ss, vd.co, val);
			}

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_smooth_brush_bmesh_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Sculpt *sd = data->sd;
	Brush *brush = data->brush;
	const bool smooth_mask = data->smooth_mask;
	float bstrength = data->strength;

	PBVHVertexIter vd;
	SculptBrushTest test;

	CLAMP(bstrength, 0.0f, 1.0f);

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test(&test, vd.co)) {
			const float fade = bstrength * tex_strength(
			                       ss, brush, vd.co, test.dist, vd.no, vd.fno, smooth_mask ? 0.0f : *vd.mask,
			                       thread_id);
			if (smooth_mask) {
				float val = bmesh_neighbor_average_mask(vd.bm_vert, vd.cd_vert_mask_offset) - *vd.mask;
				val *= fade * bstrength;
				*vd.mask += val;
				CLAMP(*vd.mask, 0.0f, 1.0f);
			}
			else {
				float avg[3], val[3];

				bmesh_neighbor_average(avg, vd.bm_vert);
				sub_v3_v3v3(val, avg, vd.co);

				madd_v3_v3v3fl(val, vd.co, val, fade);

				sculpt_clip(sd, ss, vd.co, val);
			}

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_smooth_brush_multires_task_cb_ex(
        void *userdata, void *userdata_chunk, const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptDoBrushSmoothGridDataChunk *data_chunk = userdata_chunk;
	SculptSession *ss = data->ob->sculpt;
	Sculpt *sd = data->sd;
	Brush *brush = data->brush;
	const bool smooth_mask = data->smooth_mask;
	float bstrength = data->strength;

	SculptBrushTest test;
	CCGElem **griddata, *gddata;
	CCGKey key;

	float (*tmpgrid_co)[3] = NULL;
	float tmprow_co[2][3];
	float *tmpgrid_mask = NULL;
	float tmprow_mask[2];

	BLI_bitmap * const *grid_hidden;
	int *grid_indices, totgrid, gridsize;
	int i, x, y;

	sculpt_brush_test_init(ss, &test);

	CLAMP(bstrength, 0.0f, 1.0f);

	BKE_pbvh_node_get_grids(ss->pbvh, data->nodes[n], &grid_indices, &totgrid, NULL, &gridsize, &griddata);
	BKE_pbvh_get_grid_key(ss->pbvh, &key);

	grid_hidden = BKE_pbvh_grid_hidden(ss->pbvh);

	if (smooth_mask)
		tmpgrid_mask = (void *)(data_chunk + 1);
	else
		tmpgrid_co = (void *)(data_chunk + 1);

	for (i = 0; i < totgrid; i++) {
		int gi = grid_indices[i];
		const BLI_bitmap *gh = grid_hidden[gi];
		gddata = griddata[gi];

		if (smooth_mask)
			memset(tmpgrid_mask, 0, data_chunk->tmpgrid_size);
		else
			memset(tmpgrid_co, 0, data_chunk->tmpgrid_size);

		for (y = 0; y < gridsize - 1; y++) {
			const int v = y * gridsize;
			if (smooth_mask) {
				tmprow_mask[0] = (*CCG_elem_offset_mask(&key, gddata, v) +
				                  *CCG_elem_offset_mask(&key, gddata, v + gridsize));
			}
			else {
				add_v3_v3v3(tmprow_co[0],
				            CCG_elem_offset_co(&key, gddata, v),
				            CCG_elem_offset_co(&key, gddata, v + gridsize));
			}

			for (x = 0; x < gridsize - 1; x++) {
				const int v1 = x + y * gridsize;
				const int v2 = v1 + 1;
				const int v3 = v1 + gridsize;
				const int v4 = v3 + 1;

				if (smooth_mask) {
					float tmp;

					tmprow_mask[(x + 1) % 2] = (*CCG_elem_offset_mask(&key, gddata, v2) +
					                            *CCG_elem_offset_mask(&key, gddata, v4));
					tmp = tmprow_mask[(x + 1) % 2] + tmprow_mask[x % 2];

					tmpgrid_mask[v1] += tmp;
					tmpgrid_mask[v2] += tmp;
					tmpgrid_mask[v3] += tmp;
					tmpgrid_mask[v4] += tmp;
				}
				else {
					float tmp[3];

					add_v3_v3v3(tmprow_co[(x + 1) % 2],
					            CCG_elem_offset_co(&key, gddata, v2),
					            CCG_elem_offset_co(&key, gddata, v4));
					add_v3_v3v3(tmp, tmprow_co[(x + 1) % 2], tmprow_co[x % 2]);

					add_v3_v3(tmpgrid_co[v1], tmp);
					add_v3_v3(tmpgrid_co[v2], tmp);
					add_v3_v3(tmpgrid_co[v3], tmp);
					add_v3_v3(tmpgrid_co[v4], tmp);
				}
			}
		}

		/* blend with existing coordinates */
		for (y = 0; y < gridsize; y++) {
			for (x = 0; x < gridsize; x++) {
				float *co;
				const float *fno;
				float *mask;
				const int index = y * gridsize + x;

				if (gh) {
					if (BLI_BITMAP_TEST(gh, index))
						continue;
				}

				co = CCG_elem_offset_co(&key, gddata, index);
				fno = CCG_elem_offset_no(&key, gddata, index);
				mask = CCG_elem_offset_mask(&key, gddata, index);

				if (sculpt_brush_test(&test, co)) {
					const float strength_mask = (smooth_mask ? 0.0f : *mask);
					const float fade = bstrength * tex_strength(
					                       ss, brush, co, test.dist, NULL, fno, strength_mask, thread_id);
					float f = 1.0f / 16.0f;

					if (x == 0 || x == gridsize - 1)
						f *= 2.0f;

					if (y == 0 || y == gridsize - 1)
						f *= 2.0f;

					if (smooth_mask) {
						*mask += ((tmpgrid_mask[index] * f) - *mask) * fade;
					}
					else {
						float *avg = tmpgrid_co[index];
						float val[3];

						mul_v3_fl(avg, f);
						sub_v3_v3v3(val, avg, co);
						madd_v3_v3v3fl(val, co, val, fade);

						sculpt_clip(sd, ss, co, val);
					}
				}
			}
		}
	}
}

static void smooth(
        Sculpt *sd, Object *ob, PBVHNode **nodes, const int totnode, float bstrength, const bool smooth_mask)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);

	const int max_iterations = 4;
	const float fract = 1.0f / max_iterations;
	PBVHType type = BKE_pbvh_type(ss->pbvh);
	int iteration, count;
	float last;

	CLAMP(bstrength, 0.0f, 1.0f);

	count = (int)(bstrength * max_iterations);
	last  = max_iterations * (bstrength - count * fract);

	if (type == PBVH_FACES && !ss->pmap) {
		BLI_assert(!"sculpt smooth: pmap missing");
		return;
	}

	set_adaptive_space_factor(sd);

	for (iteration = 0; iteration <= count; ++iteration) {
		const float strength = (iteration != count) ? 1.0f : last;

		SculptThreadedTaskData data = {
		    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
		    .smooth_mask = smooth_mask, .strength = strength,
		};

		switch (type) {
			case PBVH_GRIDS:
			{
				int gridsize;
				size_t size;
				SculptDoBrushSmoothGridDataChunk *data_chunk;

				BKE_pbvh_node_get_grids(ss->pbvh, NULL, NULL, NULL, NULL, &gridsize, NULL);
				size = (size_t)gridsize;
				size = sizeof(float) * size * size * (smooth_mask ? 1 : 3);
				data_chunk = MEM_mallocN(sizeof(*data_chunk) + size, __func__);
				data_chunk->tmpgrid_size = size;
				size += sizeof(*data_chunk);

				BLI_task_parallel_range_ex(
				            0, totnode, &data, data_chunk, size, do_smooth_brush_multires_task_cb_ex,
				            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);

				MEM_freeN(data_chunk);
				break;
			}
			case PBVH_FACES:
				BLI_task_parallel_range_ex(
				            0, totnode, &data, NULL, 0, do_smooth_brush_mesh_task_cb_ex,
				            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
				break;
			case PBVH_BMESH:
				BLI_task_parallel_range_ex(
				            0, totnode, &data, NULL, 0, do_smooth_brush_bmesh_task_cb_ex,
				            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
				break;
		}

		if (ss->multires)
			multires_stitch_grids(ob);
	}
}

static void do_smooth_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	smooth(sd, ob, nodes, totnode, ss->cache->bstrength, false);
}

static void do_mask_brush_draw_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	const float bstrength = ss->cache->bstrength;

	PBVHVertexIter vd;
	SculptBrushTest test;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test(&test, vd.co)) {
			const float fade = tex_strength(ss, brush, vd.co, test.dist, vd.no, vd.fno, 0.0f, thread_id);

			(*vd.mask) += fade * bstrength;
			CLAMP(*vd.mask, 0, 1);

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
		BKE_pbvh_vertex_iter_end;
	}
}

static void do_mask_brush_draw(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	Brush *brush = BKE_paint_brush(&sd->paint);

	set_adaptive_space_factor(sd);

	/* threaded loop over nodes */
	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_mask_brush_draw_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_mask_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);
	
	switch ((BrushMaskTool)brush->mask_tool) {
		case BRUSH_MASK_DRAW:
			do_mask_brush_draw(sd, ob, nodes, totnode);
			break;
		case BRUSH_MASK_SMOOTH:
			smooth(sd, ob, nodes, totnode, ss->cache->bstrength, true);
			break;
	}
}

static void do_draw_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	const float *offset = data->offset;

	PBVHVertexIter vd;
	SculptBrushTest test;
	float (*proxy)[3];

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test(&test, vd.co)) {
			/* offset vertex */
			const float fade = tex_strength(
			                       ss, brush, vd.co, test.dist, vd.no, vd.fno, vd.mask ? *vd.mask : 0.0f, thread_id);

			mul_v3_v3fl(proxy[vd.i], offset, fade);

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_draw_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);
	float offset[3];
	const float bstrength = ss->cache->bstrength;

	/* offset with as much as possible factored in already */
	mul_v3_v3fl(offset, ss->cache->sculpt_normal_symm, ss->cache->radius);
	mul_v3_v3(offset, ss->cache->scale);
	mul_v3_fl(offset, bstrength);

	set_adaptive_space_factor(sd);

	/* threaded loop over nodes */
	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .offset = offset,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_draw_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_crease_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	SculptProjectVector *spvc = data->spvc;
	const float flippedbstrength = data->flippedbstrength;
	const float *offset = data->offset;

	PBVHVertexIter vd;
	SculptBrushTest test;
	float (*proxy)[3];

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test(&test, vd.co)) {
			/* offset vertex */
			const float fade = tex_strength(
			                       ss, brush, vd.co, test.dist, vd.no, vd.fno, vd.mask ? *vd.mask : 0.0f, thread_id);
			float val1[3];
			float val2[3];

			/* first we pinch */
			sub_v3_v3v3(val1, test.location, vd.co);
			mul_v3_fl(val1, fade * flippedbstrength);

			sculpt_project_v3(spvc, val1, val1);

			/* then we draw */
			mul_v3_v3fl(val2, offset, fade);

			add_v3_v3v3(proxy[vd.i], val1, val2);

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_crease_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	const Scene *scene = ss->cache->vc->scene;
	Brush *brush = BKE_paint_brush(&sd->paint);
	float offset[3];
	float bstrength = ss->cache->bstrength;
	float flippedbstrength, crease_correction;
	float brush_alpha;

	SculptProjectVector spvc;

	/* offset with as much as possible factored in already */
	mul_v3_v3fl(offset, ss->cache->sculpt_normal_symm, ss->cache->radius);
	mul_v3_v3(offset, ss->cache->scale);
	mul_v3_fl(offset, bstrength);
	
	/* we divide out the squared alpha and multiply by the squared crease to give us the pinch strength */
	crease_correction = brush->crease_pinch_factor * brush->crease_pinch_factor;
	brush_alpha = BKE_brush_alpha_get(scene, brush);
	if (brush_alpha > 0.0f)
		crease_correction /= brush_alpha * brush_alpha;

	/* we always want crease to pinch or blob to relax even when draw is negative */
	flippedbstrength = (bstrength < 0) ? -crease_correction * bstrength : crease_correction * bstrength;

	if (brush->sculpt_tool == SCULPT_TOOL_BLOB) flippedbstrength *= -1.0f;

	/* Use surface normal for 'spvc', so the vertices are pinched towards a line instead of a single point.
	 * Without this we get a 'flat' surface surrounding the pinch */
	sculpt_project_v3_cache_init(&spvc, ss->cache->sculpt_normal_symm);

	set_adaptive_space_factor(sd);

	/* threaded loop over nodes */
	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .spvc = &spvc, .offset = offset, .flippedbstrength = flippedbstrength,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_crease_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_pinch_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;

	PBVHVertexIter vd;
	SculptBrushTest test;
	float (*proxy)[3];
	const float bstrength = ss->cache->bstrength;

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test(&test, vd.co)) {
			const float fade = bstrength * tex_strength(
			                       ss, brush, vd.co, test.dist, vd.no, vd.fno, vd.mask ? *vd.mask : 0.0f, thread_id);
			float val[3];

			sub_v3_v3v3(val, test.location, vd.co);
			mul_v3_v3fl(proxy[vd.i], val, fade);

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_pinch_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	Brush *brush = BKE_paint_brush(&sd->paint);

	set_adaptive_space_factor(sd);

	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_pinch_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_grab_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	const float *grab_delta = data->grab_delta;

	PBVHVertexIter vd;
	SculptBrushTest test;
	SculptOrigVertData orig_data;
	float (*proxy)[3];
	const float bstrength = ss->cache->bstrength;

	sculpt_orig_vert_data_init(&orig_data, data->ob, data->nodes[n]);

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		sculpt_orig_vert_data_update(&orig_data, &vd);

		if (sculpt_brush_test(&test, orig_data.co)) {
			const float fade = bstrength * tex_strength(
			                       ss, brush, orig_data.co, test.dist, orig_data.no, NULL, vd.mask ? *vd.mask : 0.0f,
			                       thread_id);

			mul_v3_v3fl(proxy[vd.i], grab_delta, fade);

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_grab_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);
	float grab_delta[3];

	copy_v3_v3(grab_delta, ss->cache->grab_delta_symmetry);

	if (ss->cache->normal_weight > 0.0f) {
		sculpt_project_v3_normal_align(ss, ss->cache->normal_weight, grab_delta);
	}

	set_adaptive_space_factor(sd);

	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .grab_delta = grab_delta,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_grab_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_nudge_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	const float *cono = data->cono;

	PBVHVertexIter vd;
	SculptBrushTest test;
	float (*proxy)[3];
	const float bstrength = ss->cache->bstrength;

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test(&test, vd.co)) {
			const float fade = bstrength * tex_strength(
			                       ss, brush, vd.co, test.dist, vd.no, vd.fno, vd.mask ? *vd.mask : 0.0f, thread_id);

			mul_v3_v3fl(proxy[vd.i], cono, fade);

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_nudge_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);
	float grab_delta[3];
	float tmp[3], cono[3];

	copy_v3_v3(grab_delta, ss->cache->grab_delta_symmetry);

	cross_v3_v3v3(tmp, ss->cache->sculpt_normal_symm, grab_delta);
	cross_v3_v3v3(cono, tmp, ss->cache->sculpt_normal_symm);

	set_adaptive_space_factor(sd);

	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .cono = cono,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_nudge_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_snake_hook_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	SculptProjectVector *spvc = data->spvc;
	const float *grab_delta = data->grab_delta;

	PBVHVertexIter vd;
	SculptBrushTest test;
	float (*proxy)[3];
	const float bstrength = ss->cache->bstrength;
	const bool do_rake_rotation = ss->cache->is_rake_rotation_valid;
	const bool do_pinch = (brush->crease_pinch_factor != 0.5f);
	const float pinch = do_pinch ?
	        (2.0f * (0.5f - brush->crease_pinch_factor) * (len_v3(grab_delta) / ss->cache->radius)) : 0.0f;

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test(&test, vd.co)) {
			const float fade = bstrength * tex_strength(
			                       ss, brush, vd.co, test.dist, vd.no, vd.fno, vd.mask ? *vd.mask : 0.0f, thread_id);

			mul_v3_v3fl(proxy[vd.i], grab_delta, fade);

			/* negative pinch will inflate, helps maintain volume */
			if (do_pinch) {
				float delta_pinch_init[3], delta_pinch[3];

				sub_v3_v3v3(delta_pinch, vd.co, test.location);

				/* important to calculate based on the grabbed location (intentionally ignore fade here). */
				add_v3_v3(delta_pinch, grab_delta);

				sculpt_project_v3(spvc, delta_pinch, delta_pinch);

				copy_v3_v3(delta_pinch_init, delta_pinch);

				float pinch_fade = pinch * fade;
				/* when reducing, scale reduction back by how close to the center we are,
				 * so we don't pinch into nothingness */
				if (pinch > 0.0f) {
					/* square to have even less impact for close vertices */
					pinch_fade *= pow2f(min_ff(1.0f, len_v3(delta_pinch) / ss->cache->radius));
				}
				mul_v3_fl(delta_pinch, 1.0f + pinch_fade);
				sub_v3_v3v3(delta_pinch, delta_pinch_init, delta_pinch);
				add_v3_v3(proxy[vd.i], delta_pinch);
			}

			if (do_rake_rotation) {
				float delta_rotate[3];
				sculpt_rake_rotate(ss, test.location, vd.co, fade, delta_rotate);
				add_v3_v3(proxy[vd.i], delta_rotate);
			}

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_snake_hook_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);
	const float bstrength = ss->cache->bstrength;
	float grab_delta[3];

	SculptProjectVector spvc;

	copy_v3_v3(grab_delta, ss->cache->grab_delta_symmetry);

	if (bstrength < 0)
		negate_v3(grab_delta);

	if (ss->cache->normal_weight > 0.0f) {
		sculpt_project_v3_normal_align(ss, ss->cache->normal_weight, grab_delta);
	}

	/* optionally pinch while painting */
	if (brush->crease_pinch_factor != 0.5f) {
		sculpt_project_v3_cache_init(&spvc, grab_delta);
	}


	set_adaptive_space_factor(sd);

	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .spvc = &spvc, .grab_delta = grab_delta,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_snake_hook_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_thumb_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	const float *cono = data->cono;

	PBVHVertexIter vd;
	SculptBrushTest test;
	SculptOrigVertData orig_data;
	float (*proxy)[3];
	const float bstrength = ss->cache->bstrength;

	sculpt_orig_vert_data_init(&orig_data, data->ob, data->nodes[n]);

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		sculpt_orig_vert_data_update(&orig_data, &vd);

		if (sculpt_brush_test(&test, orig_data.co)) {
			const float fade = bstrength * tex_strength(
			                       ss, brush, orig_data.co, test.dist, orig_data.no, NULL, vd.mask ? *vd.mask : 0.0f,
			                       thread_id);

			mul_v3_v3fl(proxy[vd.i], cono, fade);

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_thumb_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);
	float grab_delta[3];
	float tmp[3], cono[3];

	copy_v3_v3(grab_delta, ss->cache->grab_delta_symmetry);

	cross_v3_v3v3(tmp, ss->cache->sculpt_normal_symm, grab_delta);
	cross_v3_v3v3(cono, tmp, ss->cache->sculpt_normal_symm);

	set_adaptive_space_factor(sd);

	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .cono = cono,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_thumb_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_rotate_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	const float angle = data->angle;

	PBVHVertexIter vd;
	SculptBrushTest test;
	SculptOrigVertData orig_data;
	float (*proxy)[3];
	const float bstrength = ss->cache->bstrength;

	sculpt_orig_vert_data_init(&orig_data, data->ob, data->nodes[n]);

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		sculpt_orig_vert_data_update(&orig_data, &vd);

		if (sculpt_brush_test(&test, orig_data.co)) {
			float vec[3], rot[3][3];
			const float fade = bstrength * tex_strength(
			                       ss, brush, orig_data.co, test.dist, orig_data.no, NULL, vd.mask ? *vd.mask : 0.0f,
			                       thread_id);

			sub_v3_v3v3(vec, orig_data.co, ss->cache->location);
			axis_angle_normalized_to_mat3(rot, ss->cache->sculpt_normal_symm, angle * fade);
			mul_v3_m3v3(proxy[vd.i], rot, vec);
			add_v3_v3(proxy[vd.i], ss->cache->location);
			sub_v3_v3(proxy[vd.i], orig_data.co);

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_rotate_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);

	static const int flip[8] = { 1, -1, -1, 1, -1, 1, 1, -1 };
	const float angle = ss->cache->vertex_rotation * flip[ss->cache->mirror_symmetry_pass];

	set_adaptive_space_factor(sd);

	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .angle = angle,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_rotate_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_layer_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Sculpt *sd = data->sd;
	Brush *brush = data->brush;
	const float *offset = data->offset;

	PBVHVertexIter vd;
	SculptBrushTest test;
	SculptOrigVertData orig_data;
	float *layer_disp;
	const float bstrength = ss->cache->bstrength;
	const float lim = (bstrength < 0) ? -data->brush->height : data->brush->height;
	/* XXX: layer brush needs conversion to proxy but its more complicated */
	/* proxy = BKE_pbvh_node_add_proxy(ss->pbvh, nodes[n])->co; */

	sculpt_orig_vert_data_init(&orig_data, data->ob, data->nodes[n]);

	/* Why does this have to be thread-protected? */
	BLI_mutex_lock(&data->mutex);
	layer_disp = BKE_pbvh_node_layer_disp_get(ss->pbvh, data->nodes[n]);
	BLI_mutex_unlock(&data->mutex);

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		sculpt_orig_vert_data_update(&orig_data, &vd);

		if (sculpt_brush_test(&test, orig_data.co)) {
			const float fade = bstrength * tex_strength(
			                       ss, brush, vd.co, test.dist, vd.no, vd.fno, vd.mask ? *vd.mask : 0.0f, thread_id);
			float *disp = &layer_disp[vd.i];
			float val[3];

			*disp += fade;

			/* Don't let the displacement go past the limit */
			if ((lim < 0.0f && *disp < lim) || (lim >= 0.0f && *disp > lim))
				*disp = lim;

			mul_v3_v3fl(val, offset, *disp);

			if (!ss->multires && !ss->bm && ss->layer_co && (brush->flag & BRUSH_PERSISTENT)) {
				int index = vd.vert_indices[vd.i];

				/* persistent base */
				add_v3_v3(val, ss->layer_co[index]);
			}
			else {
				add_v3_v3(val, orig_data.co);
			}

			sculpt_clip(sd, ss, vd.co, val);

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_layer_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);
	float offset[3];

	mul_v3_v3v3(offset, ss->cache->scale, ss->cache->sculpt_normal_symm);

	set_adaptive_space_factor(sd);

	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .offset = offset,
	};
	BLI_mutex_init(&data.mutex);

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_layer_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);

	BLI_mutex_end(&data.mutex);
}

static void do_inflate_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;

	PBVHVertexIter vd;
	SculptBrushTest test;
	float (*proxy)[3];
	const float bstrength = ss->cache->bstrength;

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test(&test, vd.co)) {
			const float fade = bstrength * tex_strength(
			                       ss, brush, vd.co, test.dist, vd.no, vd.fno, vd.mask ? *vd.mask : 0.0f, thread_id);
			float val[3];

			if (vd.fno)
				copy_v3_v3(val, vd.fno);
			else
				normal_short_to_float_v3(val, vd.no);

			mul_v3_fl(val, fade * ss->cache->radius);
			mul_v3_v3v3(proxy[vd.i], val, ss->cache->scale);

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_inflate_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	Brush *brush = BKE_paint_brush(&sd->paint);

	set_adaptive_space_factor(sd);

	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_inflate_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void calc_sculpt_plane(
        Sculpt *sd, Object *ob,
        PBVHNode **nodes, int totnode,
        float r_area_no[3], float r_area_co[3])
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);

	if (ss->cache->mirror_symmetry_pass == 0 &&
	    ss->cache->radial_symmetry_pass == 0 &&
	    ss->cache->tile_pass == 0 &&
	    (ss->cache->first_time || !(brush->flag & BRUSH_ORIGINAL_NORMAL)))
	{
		switch (brush->sculpt_plane) {
			case SCULPT_DISP_DIR_VIEW:
				copy_v3_v3(r_area_no, ss->cache->true_view_normal);
				break;

			case SCULPT_DISP_DIR_X:
				ARRAY_SET_ITEMS(r_area_no, 1, 0, 0);
				break;

			case SCULPT_DISP_DIR_Y:
				ARRAY_SET_ITEMS(r_area_no, 0, 1, 0);
				break;

			case SCULPT_DISP_DIR_Z:
				ARRAY_SET_ITEMS(r_area_no, 0, 0, 1);
				break;

			case SCULPT_DISP_DIR_AREA:
				calc_area_normal_and_center(sd, ob, nodes, totnode, r_area_no, r_area_co);
				break;

			default:
				break;
		}

		/* for flatten center */
		/* flatten center has not been calculated yet if we are not using the area normal */
		if (brush->sculpt_plane != SCULPT_DISP_DIR_AREA)
			calc_area_center(sd, ob, nodes, totnode, r_area_co);

		/* for area normal */
		copy_v3_v3(ss->cache->sculpt_normal, r_area_no);

		/* for flatten center */
		copy_v3_v3(ss->cache->last_center, r_area_co);
	}
	else {
		/* for area normal */
		copy_v3_v3(r_area_no, ss->cache->sculpt_normal);

		/* for flatten center */
		copy_v3_v3(r_area_co, ss->cache->last_center);

		/* for area normal */
		flip_v3(r_area_no, ss->cache->mirror_symmetry_pass);

		/* for flatten center */
		flip_v3(r_area_co, ss->cache->mirror_symmetry_pass);

		/* for area normal */
		mul_m4_v3(ss->cache->symm_rot_mat, r_area_no);

		/* for flatten center */
		mul_m4_v3(ss->cache->symm_rot_mat, r_area_co);

		/* shift the plane for the current tile */
		add_v3_v3(r_area_co, ss->cache->plane_offset);
	}
}

/* Projects a point onto a plane along the plane's normal */
static void point_plane_project(
        float intr[3],
        const float co[3], const float plane_normal[3], const float plane_center[3])
{
	sub_v3_v3v3(intr, co, plane_center);
	mul_v3_v3fl(intr, plane_normal, dot_v3v3(plane_normal, intr));
	sub_v3_v3v3(intr, co, intr);
}

static int plane_trim(const StrokeCache *cache, const Brush *brush, const float val[3])
{
	return (!(brush->flag & BRUSH_PLANE_TRIM) ||
	        ((dot_v3v3(val, val) <= cache->radius_squared * cache->plane_trim_squared)));
}

static bool plane_point_side_flip(
        const float co[3], const float plane_normal[3], const float plane_center[3],
        const bool flip)
{
	float delta[3];
	float d;

	sub_v3_v3v3(delta, co, plane_center);
	d = dot_v3v3(plane_normal, delta);

	if (flip) d = -d;

	return d <= 0.0f;
}

static int plane_point_side(const float co[3], const float plane_normal[3], const float plane_center[3])
{
	float delta[3];

	sub_v3_v3v3(delta, co, plane_center);
	return dot_v3v3(plane_normal, delta) <= 0.0f;
}

static float get_offset(Sculpt *sd, SculptSession *ss)
{
	Brush *brush = BKE_paint_brush(&sd->paint);

	float rv = brush->plane_offset;

	if (brush->flag & BRUSH_OFFSET_PRESSURE) {
		rv *= ss->cache->pressure;
	}

	return rv;
}

static void do_flatten_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	const float *area_no = data->area_no;
	const float *area_co = data->area_co;

	PBVHVertexIter vd;
	SculptBrushTest test;
	float (*proxy)[3];
	const float bstrength = ss->cache->bstrength;

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test_sq(&test, vd.co)) {
			float intr[3];
			float val[3];

			point_plane_project(intr, vd.co, area_no, area_co);

			sub_v3_v3v3(val, intr, vd.co);

			if (plane_trim(ss->cache, brush, val)) {
				const float fade = bstrength * tex_strength(
				                       ss, brush, vd.co, sqrtf(test.dist), vd.no, vd.fno, vd.mask ? *vd.mask : 0.0f,
				                       thread_id);

				mul_v3_v3fl(proxy[vd.i], val, fade);

				if (vd.mvert)
					vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
			}
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_flatten_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);

	const float radius = ss->cache->radius;

	float area_no[3];
	float area_co[3];

	float offset = get_offset(sd, ss);
	float displace;
	float temp[3];

	calc_sculpt_plane(sd, ob, nodes, totnode, area_no, area_co);

	displace = radius * offset;

	mul_v3_v3v3(temp, area_no, ss->cache->scale);
	mul_v3_fl(temp, displace);
	add_v3_v3(area_co, temp);

	set_adaptive_space_factor(sd);

	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .area_no = area_no, .area_co = area_co,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_flatten_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_clay_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	const float *area_no = data->area_no;
	const float *area_co = data->area_co;

	PBVHVertexIter vd;
	SculptBrushTest test;
	float (*proxy)[3];
	const bool flip = (ss->cache->bstrength < 0);
	const float bstrength = flip ? -ss->cache->bstrength : ss->cache->bstrength;

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test_sq(&test, vd.co)) {
			if (plane_point_side_flip(vd.co, area_no, area_co, flip)) {
				float intr[3];
				float val[3];

				point_plane_project(intr, vd.co, area_no, area_co);

				sub_v3_v3v3(val, intr, vd.co);

				if (plane_trim(ss->cache, brush, val)) {
					/* note, the normal from the vertices is ignored,
					 * causes glitch with planes, see: T44390 */
					const float fade = bstrength * tex_strength(
					                       ss, brush, vd.co, sqrtf(test.dist), vd.no, vd.fno, vd.mask ? *vd.mask : 0.0f,
					                       thread_id);

					mul_v3_v3fl(proxy[vd.i], val, fade);

					if (vd.mvert)
						vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
				}
			}
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_clay_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);

	const bool flip = (ss->cache->bstrength < 0);
	const float radius = flip ? -ss->cache->radius : ss->cache->radius;

	float offset = get_offset(sd, ss);
	float displace;

	float area_no[3];
	float area_co[3];
	float temp[3];

	calc_sculpt_plane(sd, ob, nodes, totnode, area_no, area_co);

	displace = radius * (0.25f + offset);

	mul_v3_v3v3(temp, area_no, ss->cache->scale);
	mul_v3_fl(temp, displace);
	add_v3_v3(area_co, temp);

	/* add_v3_v3v3(p, ss->cache->location, area_no); */

	set_adaptive_space_factor(sd);

	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .area_no = area_no, .area_co = area_co,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_clay_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_clay_strips_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	float (*mat)[4] = data->mat;
	const float *area_no_sp = data->area_no_sp;
	const float *area_co = data->area_co;

	PBVHVertexIter vd;
	SculptBrushTest test;
	float (*proxy)[3];
	const bool flip = (ss->cache->bstrength < 0);
	const float bstrength = flip ? -ss->cache->bstrength : ss->cache->bstrength;

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test_cube(&test, vd.co, mat)) {
			if (plane_point_side_flip(vd.co, area_no_sp, area_co, flip)) {
				float intr[3];
				float val[3];

				point_plane_project(intr, vd.co, area_no_sp, area_co);

				sub_v3_v3v3(val, intr, vd.co);

				if (plane_trim(ss->cache, brush, val)) {
					/* note, the normal from the vertices is ignored,
					 * causes glitch with planes, see: T44390 */
					const float fade = bstrength * tex_strength(
					                       ss, brush, vd.co, ss->cache->radius * test.dist,
					                       vd.no, vd.fno, vd.mask ? *vd.mask : 0.0f, thread_id);

					mul_v3_v3fl(proxy[vd.i], val, fade);

					if (vd.mvert)
						vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
				}
			}
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_clay_strips_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);

	const bool flip = (ss->cache->bstrength < 0);
	const float radius    = flip ? -ss->cache->radius : ss->cache->radius;
	const float offset    = get_offset(sd, ss);
	const float displace  = radius * (0.25f + offset);

	float area_no_sp[3];  /* the sculpt-plane normal (whatever its set to) */
	float area_no[3];     /* geometry normal */
	float area_co[3];

	float temp[3];
	float mat[4][4];
	float scale[4][4];
	float tmat[4][4];

	calc_sculpt_plane(sd, ob, nodes, totnode, area_no_sp, area_co);

	if (brush->sculpt_plane != SCULPT_DISP_DIR_AREA || (brush->flag & BRUSH_ORIGINAL_NORMAL))
		calc_area_normal(sd, ob, nodes, totnode, area_no);
	else
		copy_v3_v3(area_no, area_no_sp);

	/* delay the first daub because grab delta is not setup */
	if (ss->cache->first_time)
		return;

	mul_v3_v3v3(temp, area_no_sp, ss->cache->scale);
	mul_v3_fl(temp, displace);
	add_v3_v3(area_co, temp);

	/* init mat */
	cross_v3_v3v3(mat[0], area_no, ss->cache->grab_delta_symmetry);
	mat[0][3] = 0;
	cross_v3_v3v3(mat[1], area_no, mat[0]);
	mat[1][3] = 0;
	copy_v3_v3(mat[2], area_no);
	mat[2][3] = 0;
	copy_v3_v3(mat[3], ss->cache->location);
	mat[3][3] = 1;
	normalize_m4(mat);

	/* scale mat */
	scale_m4_fl(scale, ss->cache->radius);
	mul_m4_m4m4(tmat, mat, scale);
	invert_m4_m4(mat, tmat);

	set_adaptive_space_factor(sd);

	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .area_no_sp = area_no_sp, .area_co = area_co, .mat = mat,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_clay_strips_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_fill_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	const float *area_no = data->area_no;
	const float *area_co = data->area_co;

	PBVHVertexIter vd;
	SculptBrushTest test;
	float (*proxy)[3];
	const float bstrength = ss->cache->bstrength;

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test_sq(&test, vd.co)) {
			if (plane_point_side(vd.co, area_no, area_co)) {
				float intr[3];
				float val[3];

				point_plane_project(intr, vd.co, area_no, area_co);

				sub_v3_v3v3(val, intr, vd.co);

				if (plane_trim(ss->cache, brush, val)) {
					const float fade = bstrength * tex_strength(
					                       ss, brush, vd.co, sqrtf(test.dist),
					                       vd.no, vd.fno, vd.mask ? *vd.mask : 0.0f, thread_id);

					mul_v3_v3fl(proxy[vd.i], val, fade);

					if (vd.mvert)
						vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
				}
			}
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_fill_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);

	const float radius = ss->cache->radius;

	float area_no[3];
	float area_co[3];
	float offset = get_offset(sd, ss);

	float displace;

	float temp[3];

	calc_sculpt_plane(sd, ob, nodes, totnode, area_no, area_co);

	displace = radius * offset;

	mul_v3_v3v3(temp, area_no, ss->cache->scale);
	mul_v3_fl(temp, displace);
	add_v3_v3(area_co, temp);

	set_adaptive_space_factor(sd);

	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .area_no = area_no, .area_co = area_co,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_fill_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_scrape_brush_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	const float *area_no = data->area_no;
	const float *area_co = data->area_co;

	PBVHVertexIter vd;
	SculptBrushTest test;
	float (*proxy)[3];
	const float bstrength = ss->cache->bstrength;

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		if (sculpt_brush_test_sq(&test, vd.co)) {
			if (!plane_point_side(vd.co, area_no, area_co)) {
				float intr[3];
				float val[3];

				point_plane_project(intr, vd.co, area_no, area_co);

				sub_v3_v3v3(val, intr, vd.co);

				if (plane_trim(ss->cache, brush, val)) {
					const float fade = bstrength * tex_strength(
					                       ss, brush, vd.co, sqrtf(test.dist), vd.no, vd.fno, vd.mask ? *vd.mask : 0.0f,
					                       thread_id);

					mul_v3_v3fl(proxy[vd.i], val, fade);

					if (vd.mvert)
						vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
				}
			}
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_scrape_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);

	const float radius = ss->cache->radius;

	float area_no[3];
	float area_co[3];
	float offset = get_offset(sd, ss);

	float displace;

	float temp[3];

	calc_sculpt_plane(sd, ob, nodes, totnode, area_no, area_co);

	displace = -radius * offset;

	mul_v3_v3v3(temp, area_no, ss->cache->scale);
	mul_v3_fl(temp, displace);
	add_v3_v3(area_co, temp);

	set_adaptive_space_factor(sd);

	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .area_no = area_no, .area_co = area_co,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_scrape_brush_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}

static void do_gravity_task_cb_ex(
        void *userdata, void *UNUSED(userdata_chunk), const int n, const int thread_id)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Brush *brush = data->brush;
	float *offset = data->offset;

	PBVHVertexIter vd;
	SculptBrushTest test;
	float (*proxy)[3];

	proxy = BKE_pbvh_node_add_proxy(ss->pbvh, data->nodes[n])->co;

	sculpt_brush_test_init(ss, &test);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE) {
		if (sculpt_brush_test_sq(&test, vd.co)) {
			const float fade = tex_strength(
			                       ss, brush, vd.co, sqrtf(test.dist), vd.no, vd.fno, vd.mask ? *vd.mask : 0.0f,
			                       thread_id);

			mul_v3_v3fl(proxy[vd.i], offset, fade);

			if (vd.mvert)
				vd.mvert->flag |= ME_VERT_PBVH_UPDATE;
		}
	}
	BKE_pbvh_vertex_iter_end;
}

static void do_gravity(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode, float bstrength)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);

	float offset[3]/*, area_no[3]*/;
	float gravity_vector[3];

	mul_v3_v3fl(gravity_vector, ss->cache->gravity_direction, -ss->cache->radius_squared);

	/* offset with as much as possible factored in already */
	mul_v3_v3v3(offset, gravity_vector, ss->cache->scale);
	mul_v3_fl(offset, bstrength);

	set_adaptive_space_factor(sd);

	/* threaded loop over nodes */
	SculptThreadedTaskData data = {
	    .sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
	    .offset = offset,
	};

	BLI_task_parallel_range_ex(
	            0, totnode, &data, NULL, 0, do_gravity_task_cb_ex,
	            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT), false);
}


void sculpt_vertcos_to_key(Object *ob, KeyBlock *kb, float (*vertCos)[3])
{
	Mesh *me = (Mesh *)ob->data;
	float (*ofs)[3] = NULL;
	int a;
	const int kb_act_idx = ob->shapenr - 1;
	KeyBlock *currkey;

	/* for relative keys editing of base should update other keys */
	if (BKE_keyblock_is_basis(me->key, kb_act_idx)) {
		ofs = BKE_keyblock_convert_to_vertcos(ob, kb);

		/* calculate key coord offsets (from previous location) */
		for (a = 0; a < me->totvert; a++) {
			sub_v3_v3v3(ofs[a], vertCos[a], ofs[a]);
		}

		/* apply offsets on other keys */
		for (currkey = me->key->block.first; currkey; currkey = currkey->next) {
			if ((currkey != kb) && (currkey->relative == kb_act_idx)) {
				BKE_keyblock_update_from_offset(ob, currkey, ofs);
			}
		}

		MEM_freeN(ofs);
	}

	/* modifying of basis key should update mesh */
	if (kb == me->key->refkey) {
		MVert *mvert = me->mvert;

		for (a = 0; a < me->totvert; a++, mvert++)
			copy_v3_v3(mvert->co, vertCos[a]);

		BKE_mesh_calc_normals(me);
	}

	/* apply new coords on active key block, no need to re-allocate kb->data here! */
	BKE_keyblock_update_from_vertcos(ob, kb, vertCos);
}

/* Note: we do the topology update before any brush actions to avoid
 * issues with the proxies. The size of the proxy can't change, so
 * topology must be updated first. */
static void sculpt_topology_update(Sculpt *sd, Object *ob, Brush *brush, UnifiedPaintSettings *UNUSED(ups))
{
	SculptSession *ss = ob->sculpt;
	SculptSearchSphereData data;
	PBVHNode **nodes = NULL;
	float radius;
	int n, totnode;

	/* Build a list of all nodes that are potentially within the
	 * brush's area of influence */
	data.ss = ss;
	data.sd = sd;

	radius = ss->cache->radius * 1.25f;

	data.radius_squared = radius * radius;
	data.original = sculpt_tool_needs_original(brush->sculpt_tool) ? true : ss->cache->original;

	BKE_pbvh_search_gather(ss->pbvh, sculpt_search_sphere_cb, &data, &nodes, &totnode);

	/* Only act if some verts are inside the brush area */
	if (totnode) {
		PBVHTopologyUpdateMode mode = 0;
		float location[3];

		if (sd->flags & SCULPT_DYNTOPO_SUBDIVIDE)
			mode |= PBVH_Subdivide;

		if ((sd->flags & SCULPT_DYNTOPO_COLLAPSE) ||
		    (brush->sculpt_tool == SCULPT_TOOL_SIMPLIFY))
		{
			mode |= PBVH_Collapse;
		}

		for (n = 0; n < totnode; n++) {
			sculpt_undo_push_node(ob, nodes[n],
			                      brush->sculpt_tool == SCULPT_TOOL_MASK ?
			                      SCULPT_UNDO_MASK : SCULPT_UNDO_COORDS);
			BKE_pbvh_node_mark_update(nodes[n]);

			if (BKE_pbvh_type(ss->pbvh) == PBVH_BMESH) {
				BKE_pbvh_node_mark_topology_update(nodes[n]);
				BKE_pbvh_bmesh_node_save_orig(nodes[n]);
			}
		}

		if (BKE_pbvh_type(ss->pbvh) == PBVH_BMESH) {
			BKE_pbvh_bmesh_update_topology(
			        ss->pbvh, mode,
			        ss->cache->location,
			        (brush->flag & BRUSH_FRONTFACE) ? ss->cache->view_normal : NULL,
			        ss->cache->radius);
		}

		MEM_freeN(nodes);

		/* update average stroke position */
		copy_v3_v3(location, ss->cache->true_location);
		mul_m4_v3(ob->obmat, location);
	}
}

static void do_brush_action_task_cb(void *userdata, const int n)
{
	SculptThreadedTaskData *data = userdata;

	sculpt_undo_push_node(data->ob, data->nodes[n],
	                      data->brush->sculpt_tool == SCULPT_TOOL_MASK ? SCULPT_UNDO_MASK : SCULPT_UNDO_COORDS);
	BKE_pbvh_node_mark_update(data->nodes[n]);
}

static void do_brush_action(Sculpt *sd, Object *ob, Brush *brush, UnifiedPaintSettings *ups)
{
	SculptSession *ss = ob->sculpt;
	SculptSearchSphereData data;
	PBVHNode **nodes = NULL;
	int totnode;

	/* Build a list of all nodes that are potentially within the brush's area of influence */
	data.ss = ss;
	data.sd = sd;
	data.radius_squared = ss->cache->radius_squared;
	data.original = sculpt_tool_needs_original(brush->sculpt_tool) ? true : ss->cache->original;
	BKE_pbvh_search_gather(ss->pbvh, sculpt_search_sphere_cb, &data, &nodes, &totnode);

	/* Only act if some verts are inside the brush area */
	if (totnode) {
		float location[3];

		SculptThreadedTaskData task_data = {
			.sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
		};

		BLI_task_parallel_range(
		            0, totnode, &task_data, do_brush_action_task_cb,
		            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT));

		if (sculpt_brush_needs_normal(brush, ss->cache->normal_weight))
			update_sculpt_normal(sd, ob, nodes, totnode);

		if (brush->mtex.brush_map_mode == MTEX_MAP_MODE_AREA)
			update_brush_local_mat(sd, ob);

		/* Apply one type of brush action */
		switch (brush->sculpt_tool) {
			case SCULPT_TOOL_DRAW:
				do_draw_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_SMOOTH:
				do_smooth_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_CREASE:
				do_crease_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_BLOB:
				do_crease_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_PINCH:
				do_pinch_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_INFLATE:
				do_inflate_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_GRAB:
				do_grab_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_ROTATE:
				do_rotate_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_SNAKE_HOOK:
				do_snake_hook_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_NUDGE:
				do_nudge_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_THUMB:
				do_thumb_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_LAYER:
				do_layer_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_FLATTEN:
				do_flatten_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_CLAY:
				do_clay_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_CLAY_STRIPS:
				do_clay_strips_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_FILL:
				do_fill_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_SCRAPE:
				do_scrape_brush(sd, ob, nodes, totnode);
				break;
			case SCULPT_TOOL_MASK:
				do_mask_brush(sd, ob, nodes, totnode);
				break;
		}

		if (!ELEM(brush->sculpt_tool, SCULPT_TOOL_SMOOTH, SCULPT_TOOL_MASK) &&
		    brush->autosmooth_factor > 0)
		{
			if (brush->flag & BRUSH_INVERSE_SMOOTH_PRESSURE) {
				smooth(sd, ob, nodes, totnode, brush->autosmooth_factor * (1 - ss->cache->pressure), false);
			}
			else {
				smooth(sd, ob, nodes, totnode, brush->autosmooth_factor, false);
			}
		}

		if (ss->cache->supports_gravity)
			do_gravity(sd, ob, nodes, totnode, sd->gravity_factor);

		MEM_freeN(nodes);

		/* update average stroke position */
		copy_v3_v3(location, ss->cache->true_location);
		mul_m4_v3(ob->obmat, location);

		add_v3_v3(ups->average_stroke_accum, location);
		ups->average_stroke_counter++;
		/* update last stroke position */
		ups->last_stroke_valid = true;
	}
}

/* flush displacement from deformed PBVH vertex to original mesh */
static void sculpt_flush_pbvhvert_deform(Object *ob, PBVHVertexIter *vd)
{
	SculptSession *ss = ob->sculpt;
	Mesh *me = ob->data;
	float disp[3], newco[3];
	int index = vd->vert_indices[vd->i];

	sub_v3_v3v3(disp, vd->co, ss->deform_cos[index]);
	mul_m3_v3(ss->deform_imats[index], disp);
	add_v3_v3v3(newco, disp, ss->orig_cos[index]);

	copy_v3_v3(ss->deform_cos[index], vd->co);
	copy_v3_v3(ss->orig_cos[index], newco);

	if (!ss->kb)
		copy_v3_v3(me->mvert[index].co, newco);
}

static void sculpt_combine_proxies_task_cb(void *userdata, const int n)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Sculpt *sd = data->sd;
	Object *ob = data->ob;

	/* these brushes start from original coordinates */
	const bool use_orco = ELEM(data->brush->sculpt_tool, SCULPT_TOOL_GRAB, SCULPT_TOOL_ROTATE, SCULPT_TOOL_THUMB);

	PBVHVertexIter vd;
	PBVHProxyNode *proxies;
	int proxy_count;
	float (*orco)[3] = NULL;

	if (use_orco && !ss->bm)
		orco = sculpt_undo_push_node(data->ob, data->nodes[n], SCULPT_UNDO_COORDS)->co;

	BKE_pbvh_node_get_proxies(data->nodes[n], &proxies, &proxy_count);

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		float val[3];
		int p;

		if (use_orco) {
			if (ss->bm) {
				copy_v3_v3(val, BM_log_original_vert_co(ss->bm_log, vd.bm_vert));
			}
			else {
				copy_v3_v3(val, orco[vd.i]);
			}
		}
		else {
			copy_v3_v3(val, vd.co);
		}

		for (p = 0; p < proxy_count; p++)
			add_v3_v3(val, proxies[p].co[vd.i]);

		sculpt_clip(sd, ss, vd.co, val);

		if (ss->modifiers_active)
			sculpt_flush_pbvhvert_deform(ob, &vd);
	}
	BKE_pbvh_vertex_iter_end;

	BKE_pbvh_node_free_proxies(data->nodes[n]);
}

static void sculpt_combine_proxies(Sculpt *sd, Object *ob)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);
	PBVHNode **nodes;
	int totnode;

	BKE_pbvh_gather_proxies(ss->pbvh, &nodes, &totnode);

	/* first line is tools that don't support proxies */
	if (ss->cache->supports_gravity ||
	    (sculpt_tool_is_proxy_used(brush->sculpt_tool) == false))
	{
		SculptThreadedTaskData data = {
			.sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
		};

		BLI_task_parallel_range(
		            0, totnode, &data, sculpt_combine_proxies_task_cb,
		            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT));
	}

	if (nodes)
		MEM_freeN(nodes);
}

/* copy the modified vertices from bvh to the active key */
static void sculpt_update_keyblock(Object *ob)
{
	SculptSession *ss = ob->sculpt;
	float (*vertCos)[3];

	/* Keyblock update happens after handling deformation caused by modifiers,
	 * so ss->orig_cos would be updated with new stroke */
	if (ss->orig_cos) vertCos = ss->orig_cos;
	else vertCos = BKE_pbvh_get_vertCos(ss->pbvh);

	if (vertCos) {
		sculpt_vertcos_to_key(ob, ss->kb, vertCos);

		if (vertCos != ss->orig_cos)
			MEM_freeN(vertCos);
	}
}

static void sculpt_flush_stroke_deform_task_cb(void *userdata, const int n)
{
	SculptThreadedTaskData *data = userdata;
	SculptSession *ss = data->ob->sculpt;
	Object *ob = data->ob;
	float (*vertCos)[3] = data->vertCos;

	PBVHVertexIter vd;

	BKE_pbvh_vertex_iter_begin(ss->pbvh, data->nodes[n], vd, PBVH_ITER_UNIQUE)
	{
		sculpt_flush_pbvhvert_deform(ob, &vd);

		if (vertCos) {
			int index = vd.vert_indices[vd.i];
			copy_v3_v3(vertCos[index], ss->orig_cos[index]);
		}
	}
	BKE_pbvh_vertex_iter_end;
}

/* flush displacement from deformed PBVH to original layer */
static void sculpt_flush_stroke_deform(Sculpt *sd, Object *ob)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);

	if (sculpt_tool_is_proxy_used(brush->sculpt_tool)) {
		/* this brushes aren't using proxies, so sculpt_combine_proxies() wouldn't
		 * propagate needed deformation to original base */

		int totnode;
		Mesh *me = (Mesh *)ob->data;
		PBVHNode **nodes;
		float (*vertCos)[3] = NULL;

		if (ss->kb) {
			vertCos = MEM_mallocN(sizeof(*vertCos) * me->totvert, "flushStrokeDeofrm keyVerts");

			/* mesh could have isolated verts which wouldn't be in BVH,
			 * to deal with this we copy old coordinates over new ones
			 * and then update coordinates for all vertices from BVH
			 */
			memcpy(vertCos, ss->orig_cos, sizeof(*vertCos) * me->totvert);
		}

		BKE_pbvh_search_gather(ss->pbvh, NULL, NULL, &nodes, &totnode);

		SculptThreadedTaskData data = {
			.sd = sd, .ob = ob, .brush = brush, .nodes = nodes,
			.vertCos = vertCos,
		};

		BLI_task_parallel_range(
		            0, totnode, &data, sculpt_flush_stroke_deform_task_cb,
		            ((sd->flags & SCULPT_USE_OPENMP) && totnode > SCULPT_THREADED_LIMIT));

		if (vertCos) {
			sculpt_vertcos_to_key(ob, ss->kb, vertCos);
			MEM_freeN(vertCos);
		}

		MEM_freeN(nodes);

		/* Modifiers could depend on mesh normals, so we should update them/
		 * Note, then if sculpting happens on locked key, normals should be re-calculated
		 * after applying coords from keyblock on base mesh */
		BKE_mesh_calc_normals(me);
	}
	else if (ss->kb) {
		sculpt_update_keyblock(ob);
	}
}

/* Flip all the editdata across the axis/axes specified by symm. Used to
 * calculate multiple modifications to the mesh when symmetry is enabled. */
static void calc_brushdata_symm(Sculpt *sd, StrokeCache *cache, const char symm,
                                const char axis, const float angle,
                                const float UNUSED(feather))
{
	(void)sd; /* unused */

	flip_v3_v3(cache->location, cache->true_location, symm);
	flip_v3_v3(cache->grab_delta_symmetry, cache->grab_delta, symm);
	flip_v3_v3(cache->view_normal, cache->true_view_normal, symm);

	/* XXX This reduces the length of the grab delta if it approaches the line of symmetry
	 * XXX However, a different approach appears to be needed */
#if 0
	if (sd->paint.symmetry_flags & SCULPT_SYMMETRY_FEATHER) {
		float frac = 1.0f / max_overlap_count(sd);
		float reduce = (feather - frac) / (1 - frac);

		printf("feather: %f frac: %f reduce: %f\n", feather, frac, reduce);

		if (frac < 1)
			mul_v3_fl(cache->grab_delta_symmetry, reduce);
	}
#endif

	unit_m4(cache->symm_rot_mat);
	unit_m4(cache->symm_rot_mat_inv);
	zero_v3(cache->plane_offset);

	if (axis) { /* expects XYZ */
		rotate_m4(cache->symm_rot_mat, axis, angle);
		rotate_m4(cache->symm_rot_mat_inv, axis, -angle);
	}

	mul_m4_v3(cache->symm_rot_mat, cache->location);
	mul_m4_v3(cache->symm_rot_mat, cache->grab_delta_symmetry);

	if (cache->supports_gravity) {
		flip_v3_v3(cache->gravity_direction, cache->true_gravity_direction, symm);
		mul_m4_v3(cache->symm_rot_mat, cache->gravity_direction);
	}

	if (cache->is_rake_rotation_valid) {
		flip_qt_qt(cache->rake_rotation_symmetry, cache->rake_rotation, symm);
	}
}

typedef void (*BrushActionFunc)(Sculpt *sd, Object *ob, Brush *brush, UnifiedPaintSettings *ups);

static void do_tiled(Sculpt *sd, Object *ob, Brush *brush, UnifiedPaintSettings *ups, BrushActionFunc action)
{
	SculptSession *ss = ob->sculpt;
	StrokeCache *cache = ss->cache;
	const float radius = cache->radius;
	const float *bbMin = ob->bb->vec[0];
	const float *bbMax = ob->bb->vec[6];
	const float *step = sd->paint.tile_offset;
	int dim;

	/* These are integer locations, for real location: multiply with step and add orgLoc. So 0,0,0 is at orgLoc. */
	int start[3];
	int end[3];
	int cur[3];

	float orgLoc[3]; /* position of the "prototype" stroke for tiling */
	copy_v3_v3(orgLoc, cache->location);

	for (dim = 0; dim < 3; ++dim) {
		if ((sd->paint.symmetry_flags & (PAINT_TILE_X << dim)) && step[dim] > 0) {
			start[dim] = (bbMin[dim] - orgLoc[dim] - radius) / step[dim];
			end[dim] = (bbMax[dim] - orgLoc[dim] + radius) / step[dim];
		}
		else
			start[dim] = end[dim] = 0;
	}

	/* first do the "untiled" position to initialize the stroke for this location */
	cache->tile_pass = 0;
	action(sd, ob, brush, ups);

	/* now do it for all the tiles */
	copy_v3_v3_int(cur, start);
	for (cur[0] = start[0]; cur[0] <= end[0]; ++cur[0]) {
		for (cur[1] = start[1]; cur[1] <= end[1]; ++cur[1]) {
			for (cur[2] = start[2]; cur[2] <= end[2]; ++cur[2]) {
				if (!cur[0] && !cur[1] && !cur[2])
					continue; /* skip tile at orgLoc, this was already handled before all others */

				++cache->tile_pass;

				for (dim = 0; dim < 3; ++dim) {
					cache->location[dim] = cur[dim] * step[dim] + orgLoc[dim];
					cache->plane_offset[dim] = cur[dim] * step[dim];
				}
				action(sd, ob, brush, ups);
			}
		}
	}
}


static void do_radial_symmetry(Sculpt *sd, Object *ob, Brush *brush, UnifiedPaintSettings *ups,
                               BrushActionFunc action,
                               const char symm, const int axis,
                               const float feather)
{
	SculptSession *ss = ob->sculpt;
	int i;

	for (i = 1; i < sd->radial_symm[axis - 'X']; ++i) {
		const float angle = 2 * M_PI * i / sd->radial_symm[axis - 'X'];
		ss->cache->radial_symmetry_pass = i;
		calc_brushdata_symm(sd, ss->cache, symm, axis, angle, feather);
		do_tiled(sd, ob, brush, ups, action);
	}
}

/* noise texture gives different values for the same input coord; this
 * can tear a multires mesh during sculpting so do a stitch in this
 * case */
static void sculpt_fix_noise_tear(Sculpt *sd, Object *ob)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);
	MTex *mtex = &brush->mtex;

	if (ss->multires && mtex->tex && mtex->tex->type == TEX_NOISE)
		multires_stitch_grids(ob);
}

static void do_symmetrical_brush_actions(Sculpt *sd, Object *ob,
                                         BrushActionFunc action, UnifiedPaintSettings *ups)
{
	Brush *brush = BKE_paint_brush(&sd->paint);
	SculptSession *ss = ob->sculpt;
	StrokeCache *cache = ss->cache;
	const char symm = sd->paint.symmetry_flags & PAINT_SYMM_AXIS_ALL;
	int i;

	float feather = calc_symmetry_feather(sd, ss->cache);

	cache->bstrength = brush_strength(sd, cache, feather, ups);
	cache->symmetry = symm;

	/* symm is a bit combination of XYZ - 1 is mirror X; 2 is Y; 3 is XY; 4 is Z; 5 is XZ; 6 is YZ; 7 is XYZ */ 
	for (i = 0; i <= symm; ++i) {
		if (i == 0 || (symm & i && (symm != 5 || i != 3) && (symm != 6 || (i != 3 && i != 5)))) {
			cache->mirror_symmetry_pass = i;
			cache->radial_symmetry_pass = 0;

			calc_brushdata_symm(sd, cache, i, 0, 0, feather);
			do_tiled(sd, ob, brush, ups, action);

			do_radial_symmetry(sd, ob, brush, ups, action, i, 'X', feather);
			do_radial_symmetry(sd, ob, brush, ups, action, i, 'Y', feather);
			do_radial_symmetry(sd, ob, brush, ups, action, i, 'Z', feather);
		}
	}
}

static void sculpt_update_tex(const Scene *scene, Sculpt *sd, SculptSession *ss)
{
	Brush *brush = BKE_paint_brush(&sd->paint);
	const int radius = BKE_brush_size_get(scene, brush);

	if (ss->texcache) {
		MEM_freeN(ss->texcache);
		ss->texcache = NULL;
	}

	if (ss->tex_pool) {
		BKE_image_pool_free(ss->tex_pool);
		ss->tex_pool = NULL;
	}

	/* Need to allocate a bigger buffer for bigger brush size */
	ss->texcache_side = 2 * radius;
	if (!ss->texcache || ss->texcache_side > ss->texcache_actual) {
		ss->texcache = BKE_brush_gen_texture_cache(brush, radius, false);
		ss->texcache_actual = ss->texcache_side;
		ss->tex_pool = BKE_image_pool_new();
	}
}



int sculpt_mode_poll(bContext *C)
{
	Object *ob = CTX_data_active_object(C);
	return ob && ob->mode & OB_MODE_SCULPT;
}

int sculpt_mode_poll_view3d(bContext *C)
{
	return (sculpt_mode_poll(C) &&
	        CTX_wm_region_view3d(C));
}

int sculpt_poll_view3d(bContext *C)
{
	return (sculpt_poll(C) &&
	        CTX_wm_region_view3d(C));
}

int sculpt_poll(bContext *C)
{
	return sculpt_mode_poll(C) && paint_poll(C);
}

static const char *sculpt_tool_name(Sculpt *sd)
{
	Brush *brush = BKE_paint_brush(&sd->paint);

	switch ((BrushSculptTool)brush->sculpt_tool) {
		case SCULPT_TOOL_DRAW:
			return "Draw Brush";
		case SCULPT_TOOL_SMOOTH:
			return "Smooth Brush";
		case SCULPT_TOOL_CREASE:
			return "Crease Brush";
		case SCULPT_TOOL_BLOB:
			return "Blob Brush";
		case SCULPT_TOOL_PINCH:
			return "Pinch Brush";
		case SCULPT_TOOL_INFLATE:
			return "Inflate Brush";
		case SCULPT_TOOL_GRAB:
			return "Grab Brush";
		case SCULPT_TOOL_NUDGE:
			return "Nudge Brush";
		case SCULPT_TOOL_THUMB:
			return "Thumb Brush";
		case SCULPT_TOOL_LAYER:
			return "Layer Brush";
		case SCULPT_TOOL_FLATTEN:
			return "Flatten Brush";
		case SCULPT_TOOL_CLAY:
			return "Clay Brush";
		case SCULPT_TOOL_CLAY_STRIPS:
			return "Clay Strips Brush";
		case SCULPT_TOOL_FILL:
			return "Fill Brush";
		case SCULPT_TOOL_SCRAPE:
			return "Scrape Brush";
		case SCULPT_TOOL_SNAKE_HOOK:
			return "Snake Hook Brush";
		case SCULPT_TOOL_ROTATE:
			return "Rotate Brush";
		case SCULPT_TOOL_MASK:
			return "Mask Brush";
		case SCULPT_TOOL_SIMPLIFY:
			return "Simplify Brush";
	}

	return "Sculpting";
}

/**
 * Operator for applying a stroke (various attributes including mouse path)
 * using the current brush. */

static void sculpt_cache_free(StrokeCache *cache)
{
	if (cache->dial)
		MEM_freeN(cache->dial);
	MEM_freeN(cache);
}

/* Initialize mirror modifier clipping */
static void sculpt_init_mirror_clipping(Object *ob, SculptSession *ss)
{
	ModifierData *md;
	int i;

	for (md = ob->modifiers.first; md; md = md->next) {
		if (md->type == eModifierType_Mirror &&
		    (md->mode & eModifierMode_Realtime))
		{
			MirrorModifierData *mmd = (MirrorModifierData *)md;
			
			if (mmd->flag & MOD_MIR_CLIPPING) {
				/* check each axis for mirroring */
				for (i = 0; i < 3; ++i) {
					if (mmd->flag & (MOD_MIR_AXIS_X << i)) {
						/* enable sculpt clipping */
						ss->cache->flag |= CLIP_X << i;
						
						/* update the clip tolerance */
						if (mmd->tolerance >
						    ss->cache->clip_tolerance[i])
						{
							ss->cache->clip_tolerance[i] =
							    mmd->tolerance;
						}
					}
				}
			}
		}
	}
}

/* Initialize the stroke cache invariants from operator properties */
static void sculpt_update_cache_invariants(
        bContext *C, Sculpt *sd, SculptSession *ss,
        wmOperator *op, const float mouse[2])
{
	StrokeCache *cache = MEM_callocN(sizeof(StrokeCache), "stroke cache");
	Scene *scene = CTX_data_scene(C);
	UnifiedPaintSettings *ups = &CTX_data_tool_settings(C)->unified_paint_settings;
	Brush *brush = BKE_paint_brush(&sd->paint);
	ViewContext *vc = paint_stroke_view_context(op->customdata);
	Object *ob = CTX_data_active_object(C);
	float mat[3][3];
	float viewDir[3] = {0.0f, 0.0f, 1.0f};
	float max_scale;
	int i;
	int mode;

	ss->cache = cache;

	/* Set scaling adjustment */
	if (brush->sculpt_tool == SCULPT_TOOL_LAYER) {
		max_scale = 1.0f;
	}
	else {
		max_scale = 0.0f;
		for (i = 0; i < 3; i ++) {
			max_scale = max_ff(max_scale, fabsf(ob->size[i]));
		}
	}
	cache->scale[0] = max_scale / ob->size[0];
	cache->scale[1] = max_scale / ob->size[1];
	cache->scale[2] = max_scale / ob->size[2];

	cache->plane_trim_squared = brush->plane_trim * brush->plane_trim;

	cache->flag = 0;

	sculpt_init_mirror_clipping(ob, ss);

	/* Initial mouse location */
	if (mouse)
		copy_v2_v2(cache->initial_mouse, mouse);
	else
		zero_v2(cache->initial_mouse);

	mode = RNA_enum_get(op->ptr, "mode");
	cache->invert = mode == BRUSH_STROKE_INVERT;
	cache->alt_smooth = mode == BRUSH_STROKE_SMOOTH;
	cache->normal_weight = brush->normal_weight;

	/* interpret invert as following normal, for grab brushes */
	if (SCULPT_TOOL_HAS_NORMAL_WEIGHT(brush->sculpt_tool)) {
		if (cache->invert) {
			cache->invert = false;
			cache->normal_weight = (cache->normal_weight == 0.0f);
		}
	}

	/* not very nice, but with current events system implementation
	 * we can't handle brush appearance inversion hotkey separately (sergey) */
	if (cache->invert) ups->draw_inverted = true;
	else ups->draw_inverted = false;

	/* Alt-Smooth */
	if (cache->alt_smooth) {
		if (brush->sculpt_tool == SCULPT_TOOL_MASK) {
			cache->saved_mask_brush_tool = brush->mask_tool;
			brush->mask_tool = BRUSH_MASK_SMOOTH;
		}
		else {
			Paint *p = &sd->paint;
			Brush *br;
			int size = BKE_brush_size_get(scene, brush);
		
			BLI_strncpy(cache->saved_active_brush_name, brush->id.name + 2,
			            sizeof(cache->saved_active_brush_name));

			br = (Brush *)BKE_libblock_find_name(ID_BR, "Smooth");
			if (br) {
				BKE_paint_brush_set(p, br);
				brush = br;
				cache->saved_smooth_size = BKE_brush_size_get(scene, brush);
				BKE_brush_size_set(scene, brush, size);
				curvemapping_initialize(brush->curve);
			}
		}
	}

	copy_v2_v2(cache->mouse, cache->initial_mouse);
	copy_v2_v2(ups->tex_mouse, cache->initial_mouse);

	/* Truly temporary data that isn't stored in properties */

	cache->vc = vc;

	cache->brush = brush;

	/* cache projection matrix */
	ED_view3d_ob_project_mat_get(cache->vc->rv3d, ob, cache->projection_mat);

	invert_m4_m4(ob->imat, ob->obmat);
	copy_m3_m4(mat, cache->vc->rv3d->viewinv);
	mul_m3_v3(mat, viewDir);
	copy_m3_m4(mat, ob->imat);
	mul_m3_v3(mat, viewDir);
	normalize_v3_v3(cache->true_view_normal, viewDir);

	cache->supports_gravity = (!ELEM(brush->sculpt_tool, SCULPT_TOOL_MASK, SCULPT_TOOL_SMOOTH, SCULPT_TOOL_SIMPLIFY) &&
	                           (sd->gravity_factor > 0.0f));
	/* get gravity vector in world space */
	if (cache->supports_gravity) {
		if (sd->gravity_object) {
			Object *gravity_object = sd->gravity_object;

			copy_v3_v3(cache->true_gravity_direction, gravity_object->obmat[2]);
		}
		else {
			cache->true_gravity_direction[0] = cache->true_gravity_direction[1] = 0.0;
			cache->true_gravity_direction[2] = 1.0;
		}

		/* transform to sculpted object space */
		mul_m3_v3(mat, cache->true_gravity_direction);
		normalize_v3(cache->true_gravity_direction);
	}

	/* Initialize layer brush displacements and persistent coords */
	if (brush->sculpt_tool == SCULPT_TOOL_LAYER) {
		/* not supported yet for multires or dynamic topology */
		if (!ss->multires && !ss->bm && !ss->layer_co &&
		    (brush->flag & BRUSH_PERSISTENT))
		{
			if (!ss->layer_co)
				ss->layer_co = MEM_mallocN(sizeof(float) * 3 * ss->totvert,
				                           "sculpt mesh vertices copy");

			if (ss->deform_cos) {
				memcpy(ss->layer_co, ss->deform_cos, ss->totvert);
			}
			else {
				for (i = 0; i < ss->totvert; ++i) {
					copy_v3_v3(ss->layer_co[i], ss->mvert[i].co);
				}
			}
		}

		if (ss->bm) {
			/* Free any remaining layer displacements from nodes. If not and topology changes
			 * from using another tool, then next layer toolstroke can access past disp array bounds */
			BKE_pbvh_free_layer_disp(ss->pbvh);
		}
	}

	/* Make copies of the mesh vertex locations and normals for some tools */
	if (brush->flag & BRUSH_ANCHORED) {
		cache->original = 1;
	}

	if (SCULPT_TOOL_HAS_ACCUMULATE(brush->sculpt_tool)) {
		if (!(brush->flag & BRUSH_ACCUMULATE)) {
			cache->original = 1;
		}
	}

	cache->first_time = 1;

#define PIXEL_INPUT_THRESHHOLD 5
	if (brush->sculpt_tool == SCULPT_TOOL_ROTATE)
		cache->dial = BLI_dial_initialize(cache->initial_mouse, PIXEL_INPUT_THRESHHOLD);
		
#undef PIXEL_INPUT_THRESHHOLD
}

static void sculpt_update_brush_delta(UnifiedPaintSettings *ups, Object *ob, Brush *brush)
{
	SculptSession *ss = ob->sculpt;
	StrokeCache *cache = ss->cache;
	const float mouse[2] = {
		cache->mouse[0],
		cache->mouse[1]
	};
	int tool = brush->sculpt_tool;

	if (ELEM(tool,
	         SCULPT_TOOL_GRAB, SCULPT_TOOL_NUDGE,
	         SCULPT_TOOL_CLAY_STRIPS, SCULPT_TOOL_SNAKE_HOOK,
	         SCULPT_TOOL_THUMB))
	{
		float grab_location[3], imat[4][4], delta[3], loc[3];

		if (cache->first_time) {
			copy_v3_v3(cache->orig_grab_location,
			           cache->true_location);
		}
		else if (tool == SCULPT_TOOL_SNAKE_HOOK)
			add_v3_v3(cache->true_location, cache->grab_delta);

		/* compute 3d coordinate at same z from original location + mouse */
		mul_v3_m4v3(loc, ob->obmat, cache->orig_grab_location);
		ED_view3d_win_to_3d(cache->vc->v3d, cache->vc->ar, loc, mouse, grab_location);

		/* compute delta to move verts by */
		if (!cache->first_time) {
			switch (tool) {
				case SCULPT_TOOL_GRAB:
				case SCULPT_TOOL_THUMB:
					sub_v3_v3v3(delta, grab_location, cache->old_grab_location);
					invert_m4_m4(imat, ob->obmat);
					mul_mat3_m4_v3(imat, delta);
					add_v3_v3(cache->grab_delta, delta);
					break;
				case SCULPT_TOOL_CLAY_STRIPS:
				case SCULPT_TOOL_NUDGE:
				case SCULPT_TOOL_SNAKE_HOOK:
					if (brush->flag & BRUSH_ANCHORED) {
						float orig[3];
						mul_v3_m4v3(orig, ob->obmat, cache->orig_grab_location);
						sub_v3_v3v3(cache->grab_delta, grab_location, orig);
					}
					else {
						sub_v3_v3v3(cache->grab_delta, grab_location,
						            cache->old_grab_location);
					}
				
					invert_m4_m4(imat, ob->obmat);
					mul_mat3_m4_v3(imat, cache->grab_delta);
					break;
			}
		}
		else {
			zero_v3(cache->grab_delta);
		}

		copy_v3_v3(cache->old_grab_location, grab_location);

		if (tool == SCULPT_TOOL_GRAB)
			copy_v3_v3(cache->anchored_location, cache->true_location);
		else if (tool == SCULPT_TOOL_THUMB)
			copy_v3_v3(cache->anchored_location, cache->orig_grab_location);

		if (ELEM(tool, SCULPT_TOOL_GRAB, SCULPT_TOOL_THUMB)) {
			/* location stays the same for finding vertices in brush radius */
			copy_v3_v3(cache->true_location, cache->orig_grab_location);

			ups->draw_anchored = true;
			copy_v2_v2(ups->anchored_initial_mouse, cache->initial_mouse);
			ups->anchored_size = ups->pixel_radius;
		}


		/* handle 'rake' */
		cache->is_rake_rotation_valid = false;

		if (cache->first_time) {
			copy_v3_v3(cache->rake_data.follow_co, grab_location);
		}

		if (sculpt_brush_needs_rake_rotation(brush)) {
			cache->rake_data.follow_dist = cache->radius * SCULPT_RAKE_BRUSH_FACTOR;

			if (!is_zero_v3(cache->grab_delta)) {
				const float eps = 0.00001f;

				float v1[3], v2[3];

				copy_v3_v3(v1, cache->rake_data.follow_co);
				copy_v3_v3(v2, cache->rake_data.follow_co);
				sub_v3_v3(v2, cache->grab_delta);

				sub_v3_v3(v1, grab_location);
				sub_v3_v3(v2, grab_location);

				if ((normalize_v3(v2) > eps) &&
				    (normalize_v3(v1) > eps) &&
				    (len_squared_v3v3(v1, v2) > eps))
				{
					const float rake_dist_sq = len_squared_v3v3(cache->rake_data.follow_co, grab_location);
					const float rake_fade = (rake_dist_sq > SQUARE(cache->rake_data.follow_dist)) ?
					        1.0f : sqrtf(rake_dist_sq) / cache->rake_data.follow_dist;

					float axis[3], angle;
					float tquat[4];

					rotation_between_vecs_to_quat(tquat, v1, v2);

					/* use axis-angle to scale rotation since the factor may be above 1 */
					quat_to_axis_angle(axis, &angle, tquat);
					normalize_v3(axis);

					angle *= brush->rake_factor * rake_fade;
					axis_angle_normalized_to_quat(cache->rake_rotation, axis, angle);
					cache->is_rake_rotation_valid = true;
				}
			}
			sculpt_rake_data_update(&cache->rake_data, grab_location);
		}
	}
}

/* Initialize the stroke cache variants from operator properties */
static void sculpt_update_cache_variants(bContext *C, Sculpt *sd, Object *ob,
                                         PointerRNA *ptr)
{
	Scene *scene = CTX_data_scene(C);
	UnifiedPaintSettings *ups = &scene->toolsettings->unified_paint_settings;
	SculptSession *ss = ob->sculpt;
	StrokeCache *cache = ss->cache;
	Brush *brush = BKE_paint_brush(&sd->paint);

	/* RNA_float_get_array(ptr, "location", cache->traced_location); */

	if (cache->first_time ||
	    !((brush->flag & BRUSH_ANCHORED) ||
	      (brush->sculpt_tool == SCULPT_TOOL_SNAKE_HOOK) ||
	      (brush->sculpt_tool == SCULPT_TOOL_ROTATE))
	    )
	{
		RNA_float_get_array(ptr, "location", cache->true_location);
	}

	cache->pen_flip = RNA_boolean_get(ptr, "pen_flip");
	RNA_float_get_array(ptr, "mouse", cache->mouse);

	/* XXX: Use pressure value from first brush step for brushes which don't
	 *      support strokes (grab, thumb). They depends on initial state and
	 *      brush coord/pressure/etc.
	 *      It's more an events design issue, which doesn't split coordinate/pressure/angle
	 *      changing events. We should avoid this after events system re-design */
	if (paint_supports_dynamic_size(brush, ePaintSculpt) || cache->first_time) {
		cache->pressure = RNA_float_get(ptr, "pressure");
	}

	/* Truly temporary data that isn't stored in properties */
	if (cache->first_time) {
		if (!BKE_brush_use_locked_size(scene, brush)) {
			cache->initial_radius = paint_calc_object_space_radius(cache->vc,
			                                                       cache->true_location,
			                                                       BKE_brush_size_get(scene, brush));
			BKE_brush_unprojected_radius_set(scene, brush, cache->initial_radius);
		}
		else {
			cache->initial_radius = BKE_brush_unprojected_radius_get(scene, brush);
		}
	}

	if (BKE_brush_use_size_pressure(scene, brush) && paint_supports_dynamic_size(brush, ePaintSculpt)) {
		cache->radius = cache->initial_radius * cache->pressure;
	}
	else {
		cache->radius = cache->initial_radius;
	}

	cache->radius_squared = cache->radius * cache->radius;

	if (brush->flag & BRUSH_ANCHORED) {
		/* true location has been calculated as part of the stroke system already here */
		if (brush->flag & BRUSH_EDGE_TO_EDGE) {
			RNA_float_get_array(ptr, "location", cache->true_location);
		}

		cache->radius = paint_calc_object_space_radius(cache->vc,
		                                               cache->true_location,
		                                               ups->pixel_radius);
		cache->radius_squared = cache->radius * cache->radius;

		copy_v3_v3(cache->anchored_location, cache->true_location);
	}

	sculpt_update_brush_delta(ups, ob, brush);

	if (brush->sculpt_tool == SCULPT_TOOL_ROTATE) {
		cache->vertex_rotation = -BLI_dial_angle(cache->dial, cache->mouse) * cache->bstrength;

		ups->draw_anchored = true;
		copy_v2_v2(ups->anchored_initial_mouse, cache->initial_mouse);
		copy_v3_v3(cache->anchored_location, cache->true_location);
		ups->anchored_size = ups->pixel_radius;
	}

	cache->special_rotation = ups->brush_rotation;
}

/* Returns true if any of the smoothing modes are active (currently
 * one of smooth brush, autosmooth, mask smooth, or shift-key
 * smooth) */
static bool sculpt_any_smooth_mode(const Brush *brush,
                                   StrokeCache *cache,
                                   int stroke_mode)
{
	return ((stroke_mode == BRUSH_STROKE_SMOOTH) ||
	        (cache && cache->alt_smooth) ||
	        (brush->sculpt_tool == SCULPT_TOOL_SMOOTH) ||
	        (brush->autosmooth_factor > 0) ||
	        ((brush->sculpt_tool == SCULPT_TOOL_MASK) &&
	         (brush->mask_tool == BRUSH_MASK_SMOOTH)));
}

static void sculpt_stroke_modifiers_check(const bContext *C, Object *ob)
{
	SculptSession *ss = ob->sculpt;

	if (ss->kb || ss->modifiers_active) {
		Sculpt *sd = CTX_data_tool_settings(C)->sculpt;
		Brush *brush = BKE_paint_brush(&sd->paint);

		BKE_sculpt_update_mesh_elements(CTX_data_scene(C), sd, ob,
		                            sculpt_any_smooth_mode(brush, ss->cache, 0), false);
	}
}

typedef struct {
	SculptSession *ss;
	const float *ray_start, *ray_normal;
	bool hit;
	float dist;
	bool original;
} SculptRaycastData;

typedef struct {
	const float *ray_start, *ray_normal;
	bool hit;
	float dist;
	float detail;
} SculptDetailRaycastData;

static void sculpt_raycast_cb(PBVHNode *node, void *data_v, float *tmin)
{
	if (BKE_pbvh_node_get_tmin(node) < *tmin) {
		SculptRaycastData *srd = data_v;
		float (*origco)[3] = NULL;
		bool use_origco = false;

		if (srd->original && srd->ss->cache) {
			if (BKE_pbvh_type(srd->ss->pbvh) == PBVH_BMESH) {
				use_origco = true;
			}
			else {
				/* intersect with coordinates from before we started stroke */
				SculptUndoNode *unode = sculpt_undo_get_node(node);
				origco = (unode) ? unode->co : NULL;
				use_origco = origco ? true : false;
			}
		}

		if (BKE_pbvh_node_raycast(srd->ss->pbvh, node, origco, use_origco,
		                          srd->ray_start, srd->ray_normal, &srd->dist))
		{
			srd->hit = 1;
			*tmin = srd->dist;
		}
	}
}

static void sculpt_raycast_detail_cb(PBVHNode *node, void *data_v, float *tmin)
{
	if (BKE_pbvh_node_get_tmin(node) < *tmin) {
		SculptDetailRaycastData *srd = data_v;
		if (BKE_pbvh_bmesh_node_raycast_detail(node, srd->ray_start, srd->ray_normal,
		                                       &srd->dist, &srd->detail))
		{
			srd->hit = 1;
			*tmin = srd->dist;
		}
	}
}

static float sculpt_raycast_init(
        ViewContext *vc, const float mouse[2],
        float ray_start[3], float ray_end[3], float ray_normal[3], bool original)
{
	float obimat[4][4];
	float dist;
	Object *ob = vc->obact;
	RegionView3D *rv3d = vc->ar->regiondata;

	/* TODO: what if the segment is totally clipped? (return == 0) */
	ED_view3d_win_to_segment(vc->ar, vc->v3d, mouse, ray_start, ray_end, true);

	invert_m4_m4(obimat, ob->obmat);
	mul_m4_v3(obimat, ray_start);
	mul_m4_v3(obimat, ray_end);

	sub_v3_v3v3(ray_normal, ray_end, ray_start);
	dist = normalize_v3(ray_normal);

	if ((rv3d->is_persp == false) &&
	    /* if the ray is clipped, don't adjust its start/end */
	    ((rv3d->rflag & RV3D_CLIPPING) == 0))
	{
		BKE_pbvh_raycast_project_ray_root(ob->sculpt->pbvh, original, ray_start, ray_end, ray_normal);

		/* recalculate the normal */
		sub_v3_v3v3(ray_normal, ray_end, ray_start);
		dist = normalize_v3(ray_normal);
	}

	return dist;
}

/* Do a raycast in the tree to find the 3d brush location
 * (This allows us to ignore the GL depth buffer)
 * Returns 0 if the ray doesn't hit the mesh, non-zero otherwise
 */
bool sculpt_stroke_get_location(bContext *C, float out[3], const float mouse[2])
{
	Object *ob;
	SculptSession *ss;
	StrokeCache *cache;
	float ray_start[3], ray_end[3], ray_normal[3], dist;
	SculptRaycastData srd;
	bool original;
	ViewContext vc;

	view3d_set_viewcontext(C, &vc);

	ob = vc.obact;

	ss = ob->sculpt;
	cache = ss->cache;
	original = (cache) ? cache->original : 0;

	sculpt_stroke_modifiers_check(C, ob);

	dist = sculpt_raycast_init(&vc, mouse, ray_start, ray_end, ray_normal, original);

	srd.original = original;
	srd.ss = ob->sculpt;
	srd.hit = 0;
	srd.ray_start = ray_start;
	srd.ray_normal = ray_normal;
	srd.dist = dist;

	BKE_pbvh_raycast(ss->pbvh, sculpt_raycast_cb, &srd,
	                 ray_start, ray_normal, srd.original);

	copy_v3_v3(out, ray_normal);
	mul_v3_fl(out, srd.dist);
	add_v3_v3(out, ray_start);

	return srd.hit;
}

static void sculpt_brush_init_tex(const Scene *scene, Sculpt *sd, SculptSession *ss)
{
	Brush *brush = BKE_paint_brush(&sd->paint);
	MTex *mtex = &brush->mtex;

	/* init mtex nodes */
	if (mtex->tex && mtex->tex->nodetree)
		ntreeTexBeginExecTree(mtex->tex->nodetree);  /* has internal flag to detect it only does it once */

	/* TODO: Shouldn't really have to do this at the start of every
	 * stroke, but sculpt would need some sort of notification when
	 * changes are made to the texture. */
	sculpt_update_tex(scene, sd, ss);
}

static bool sculpt_brush_stroke_init(bContext *C, wmOperator *op)
{
	Scene *scene = CTX_data_scene(C);
	Object *ob = CTX_data_active_object(C);
	Sculpt *sd = CTX_data_tool_settings(C)->sculpt;
	SculptSession *ss = CTX_data_active_object(C)->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);
	int mode = RNA_enum_get(op->ptr, "mode");
	bool is_smooth;
	bool need_mask = false;

	if (brush->sculpt_tool == SCULPT_TOOL_MASK) {
		need_mask = true;
	}

	view3d_operator_needs_opengl(C);
	sculpt_brush_init_tex(scene, sd, ss);

	is_smooth = sculpt_any_smooth_mode(brush, NULL, mode);
	BKE_sculpt_update_mesh_elements(scene, sd, ob, is_smooth, need_mask);

	return 1;
}

static void sculpt_restore_mesh(Sculpt *sd, Object *ob)
{
	SculptSession *ss = ob->sculpt;
	Brush *brush = BKE_paint_brush(&sd->paint);

	/* Restore the mesh before continuing with anchored stroke */
	if ((brush->flag & BRUSH_ANCHORED) ||
	    (brush->sculpt_tool == SCULPT_TOOL_GRAB &&
	     BKE_brush_use_size_pressure(ss->cache->vc->scene, brush)) ||
	    (brush->flag & BRUSH_DRAG_DOT))
	{
		paint_mesh_restore_co(sd, ob);
	}
}

/* Copy the PBVH bounding box into the object's bounding box */
void sculpt_update_object_bounding_box(Object *ob)
{
	if (ob->bb) {
		float bb_min[3], bb_max[3];

		BKE_pbvh_bounding_box(ob->sculpt->pbvh, bb_min, bb_max);
		BKE_boundbox_init_from_minmax(ob->bb, bb_min, bb_max);
	}
}

static void sculpt_flush_update(bContext *C)
{
	Object *ob = CTX_data_active_object(C);
	SculptSession *ss = ob->sculpt;
	ARegion *ar = CTX_wm_region(C);
	MultiresModifierData *mmd = ss->multires;

	if (mmd)
		multires_mark_as_modified(ob, MULTIRES_COORDS_MODIFIED);
	if (ob->derivedFinal) /* VBO no longer valid */
		GPU_drawobject_free(ob->derivedFinal);

	if (ss->kb || ss->modifiers_active) {
		DAG_id_tag_update(&ob->id, OB_RECALC_DATA);
		ED_region_tag_redraw(ar);
	}
	else {
		rcti r;

		BKE_pbvh_update(ss->pbvh, PBVH_UpdateBB, NULL);
		/* Update the object's bounding box too so that the object
		 * doesn't get incorrectly clipped during drawing in
		 * draw_mesh_object(). [#33790] */
		sculpt_update_object_bounding_box(ob);

		if (sculpt_get_redraw_rect(ar, CTX_wm_region_view3d(C), ob, &r)) {
			if (ss->cache) {
				ss->cache->current_r = r;
			}

			/* previous is not set in the current cache else
			 * the partial rect will always grow */
			sculpt_extend_redraw_rect_previous(ob, &r);

			r.xmin += ar->winrct.xmin - 2;
			r.xmax += ar->winrct.xmin + 2;
			r.ymin += ar->winrct.ymin - 2;
			r.ymax += ar->winrct.ymin + 2;

			ss->partial_redraw = 1;
			ED_region_tag_redraw_partial(ar, &r);
		}
	}
}

/* Returns whether the mouse/stylus is over the mesh (1)
 * or over the background (0) */
static bool over_mesh(bContext *C, struct wmOperator *UNUSED(op), float x, float y)
{
	float mouse[2], co[3];

	mouse[0] = x;
	mouse[1] = y;

	return sculpt_stroke_get_location(C, co, mouse);
}

static bool sculpt_stroke_test_start(bContext *C, struct wmOperator *op,
                                    const float mouse[2])
{
	/* Don't start the stroke until mouse goes over the mesh.
	 * note: mouse will only be null when re-executing the saved stroke. */
	if (!mouse || over_mesh(C, op, mouse[0], mouse[1])) {
		Object *ob = CTX_data_active_object(C);
		SculptSession *ss = ob->sculpt;
		Sculpt *sd = CTX_data_tool_settings(C)->sculpt;

		ED_view3d_init_mats_rv3d(ob, CTX_wm_region_view3d(C));

		sculpt_update_cache_invariants(C, sd, ss, op, mouse);

		sculpt_undo_push_begin(sculpt_tool_name(sd));

		return 1;
	}
	else
		return 0;
}

static void sculpt_stroke_update_step(bContext *C, struct PaintStroke *UNUSED(stroke), PointerRNA *itemptr)
{
	UnifiedPaintSettings *ups = &CTX_data_tool_settings(C)->unified_paint_settings;
	Sculpt *sd = CTX_data_tool_settings(C)->sculpt;
	Object *ob = CTX_data_active_object(C);
	SculptSession *ss = ob->sculpt;
	const Brush *brush = BKE_paint_brush(&sd->paint);
	
	sculpt_stroke_modifiers_check(C, ob);
	sculpt_update_cache_variants(C, sd, ob, itemptr);
	sculpt_restore_mesh(sd, ob);

	if (sd->flags & SCULPT_DYNTOPO_DETAIL_CONSTANT) {
		BKE_pbvh_bmesh_detail_size_set(ss->pbvh, 1.0f / sd->constant_detail);
	}
	else if (sd->flags & SCULPT_DYNTOPO_DETAIL_BRUSH) {
		BKE_pbvh_bmesh_detail_size_set(ss->pbvh, ss->cache->radius * sd->detail_percent / 100.0f);
	}
	else {
		BKE_pbvh_bmesh_detail_size_set(
		        ss->pbvh,
		        (ss->cache->radius /
		        (float)ups->pixel_radius) *
		        (float)(sd->detail_size * U.pixelsize) / 0.4f);
	}

	if (sculpt_stroke_is_dynamic_topology(ss, brush)) {
		do_symmetrical_brush_actions(sd, ob, sculpt_topology_update, ups);
	}

	do_symmetrical_brush_actions(sd, ob, do_brush_action, ups);

	sculpt_combine_proxies(sd, ob);

	/* hack to fix noise texture tearing mesh */
	sculpt_fix_noise_tear(sd, ob);

	/* TODO(sergey): This is not really needed for the solid shading,
	 * which does use pBVH drawing anyway, but texture and wireframe
	 * requires this.
	 *
	 * Could be optimized later, but currently don't think it's so
	 * much common scenario.
	 *
	 * Same applies to the DAG_id_tag_update() invoked from
	 * sculpt_flush_update().
	 */
	if (ss->modifiers_active) {
		sculpt_flush_stroke_deform(sd, ob);
	}
	else if (ss->kb) {
		sculpt_update_keyblock(ob);
	}

	ss->cache->first_time = false;

	/* Cleanup */
	sculpt_flush_update(C);
}

static void sculpt_brush_exit_tex(Sculpt *sd)
{
	Brush *brush = BKE_paint_brush(&sd->paint);
	MTex *mtex = &brush->mtex;

	if (mtex->tex && mtex->tex->nodetree)
		ntreeTexEndExecTree(mtex->tex->nodetree->execdata);
}

static void sculpt_stroke_done(const bContext *C, struct PaintStroke *UNUSED(stroke))
{
	Object *ob = CTX_data_active_object(C);
	Scene *scene = CTX_data_scene(C);
	SculptSession *ss = ob->sculpt;
	Sculpt *sd = CTX_data_tool_settings(C)->sculpt;

	/* Finished */
	if (ss->cache) {
		UnifiedPaintSettings *ups = &CTX_data_tool_settings(C)->unified_paint_settings;
		Brush *brush = BKE_paint_brush(&sd->paint);
		ups->draw_inverted = false;

		sculpt_stroke_modifiers_check(C, ob);

		/* Alt-Smooth */
		if (ss->cache->alt_smooth) {
			if (brush->sculpt_tool == SCULPT_TOOL_MASK) {
				brush->mask_tool = ss->cache->saved_mask_brush_tool;
			}
			else {
				Paint *p = &sd->paint;
				BKE_brush_size_set(scene, ss->cache->brush, ss->cache->saved_smooth_size);
				brush = (Brush *)BKE_libblock_find_name(ID_BR, ss->cache->saved_active_brush_name);
				if (brush) {
					BKE_paint_brush_set(p, brush);
				}
			}
		}

		sculpt_cache_free(ss->cache);
		ss->cache = NULL;

		sculpt_undo_push_end(C);

		BKE_pbvh_update(ss->pbvh, PBVH_UpdateOriginalBB, NULL);
		
		if (BKE_pbvh_type(ss->pbvh) == PBVH_BMESH)
			BKE_pbvh_bmesh_after_stroke(ss->pbvh);

		/* optimization: if there is locked key and active modifiers present in */
		/* the stack, keyblock is updating at each step. otherwise we could update */
		/* keyblock only when stroke is finished */
		if (ss->kb && !ss->modifiers_active) sculpt_update_keyblock(ob);

		ss->partial_redraw = 0;

		/* try to avoid calling this, only for e.g. linked duplicates now */
		if (((Mesh *)ob->data)->id.us > 1)
			DAG_id_tag_update(&ob->id, OB_RECALC_DATA);

		WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, ob);
	}

	sculpt_brush_exit_tex(sd);
}

static int sculpt_brush_stroke_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	struct PaintStroke *stroke;
	int ignore_background_click;
	int retval;

	if (!sculpt_brush_stroke_init(C, op))
		return OPERATOR_CANCELLED;

	stroke = paint_stroke_new(C, op, sculpt_stroke_get_location,
	                          sculpt_stroke_test_start,
	                          sculpt_stroke_update_step, NULL,
	                          sculpt_stroke_done, event->type);

	op->customdata = stroke;

	/* For tablet rotation */
	ignore_background_click = RNA_boolean_get(op->ptr, "ignore_background_click");

	if (ignore_background_click && !over_mesh(C, op, event->x, event->y)) {
		paint_stroke_data_free(op);
		return OPERATOR_PASS_THROUGH;
	}
	
	if ((retval = op->type->modal(C, op, event)) == OPERATOR_FINISHED) {
		paint_stroke_data_free(op);
		return OPERATOR_FINISHED;
	}
	/* add modal handler */
	WM_event_add_modal_handler(C, op);

	OPERATOR_RETVAL_CHECK(retval);
	BLI_assert(retval == OPERATOR_RUNNING_MODAL);
	
	return OPERATOR_RUNNING_MODAL;
}

static int sculpt_brush_stroke_exec(bContext *C, wmOperator *op)
{
	if (!sculpt_brush_stroke_init(C, op))
		return OPERATOR_CANCELLED;

	op->customdata = paint_stroke_new(C, op, sculpt_stroke_get_location, sculpt_stroke_test_start,
	                                  sculpt_stroke_update_step, NULL, sculpt_stroke_done, 0);

	/* frees op->customdata */
	paint_stroke_exec(C, op);

	return OPERATOR_FINISHED;
}

static void sculpt_brush_stroke_cancel(bContext *C, wmOperator *op)
{
	Object *ob = CTX_data_active_object(C);
	SculptSession *ss = ob->sculpt;
	Sculpt *sd = CTX_data_tool_settings(C)->sculpt;
	const Brush *brush = BKE_paint_brush(&sd->paint);

	/* XXX Canceling strokes that way does not work with dynamic topology, user will have to do real undo for now.
	 *     See T46456. */
	if (ss->cache && !sculpt_stroke_is_dynamic_topology(ss, brush)) {
		paint_mesh_restore_co(sd, ob);
	}

	paint_stroke_cancel(C, op);

	if (ss->cache) {
		sculpt_cache_free(ss->cache);
		ss->cache = NULL;
	}

	sculpt_brush_exit_tex(sd);
}

static void SCULPT_OT_brush_stroke(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Sculpt";
	ot->idname = "SCULPT_OT_brush_stroke";
	ot->description = "Sculpt a stroke into the geometry";
	
	/* api callbacks */
	ot->invoke = sculpt_brush_stroke_invoke;
	ot->modal = paint_stroke_modal;
	ot->exec = sculpt_brush_stroke_exec;
	ot->poll = sculpt_poll;
	ot->cancel = sculpt_brush_stroke_cancel;

	/* flags (sculpt does own undo? (ton) */
	ot->flag = OPTYPE_BLOCKING;

	/* properties */

	paint_stroke_operator_properties(ot);

	RNA_def_boolean(ot->srna, "ignore_background_click", 0,
	                "Ignore Background Click",
	                "Clicks on the background do not start the stroke");	
}

/**** Reset the copy of the mesh that is being sculpted on (currently just for the layer brush) ****/

static int sculpt_set_persistent_base_exec(bContext *C, wmOperator *UNUSED(op))
{
	SculptSession *ss = CTX_data_active_object(C)->sculpt;

	if (ss) {
		if (ss->layer_co)
			MEM_freeN(ss->layer_co);
		ss->layer_co = NULL;
	}

	return OPERATOR_FINISHED;
}

static void SCULPT_OT_set_persistent_base(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Set Persistent Base";
	ot->idname = "SCULPT_OT_set_persistent_base";
	ot->description = "Reset the copy of the mesh that is being sculpted on";
	
	/* api callbacks */
	ot->exec = sculpt_set_persistent_base_exec;
	ot->poll = sculpt_mode_poll;
	
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/****************** Topology tools Silhouette ******************/

/*	init data:
 * 	Silhouette Data */
typedef struct SilhouetteStroke {
	float *points;
	int totvert;
	int max_verts;
}SilhouetteStroke;

typedef enum {
	SIL_INIT = 0,
	SIL_DRAWING = 1,
	SIL_OP = 2
} SilhouetteState;

typedef struct SilhouetteData {
	ARegion *ar;        	/* region that Silhouette started drawn in */
	void *draw_handle;  	/* for drawing preview loop */
	ViewContext vc;
	SilhouetteStroke *current_stroke;
	Object *ob;
	BMEditMesh *em;			/*Triangulated stroke for spine generation*/
	Scene *scene;

	float add_col[3];		/* preview color */
	float last_mouse_pos[2];

	SilhouetteState state;	/* Operator state */

	float depth;			/* Depth or thickness of the generated shape */
	float smoothness;		/* Smoothness of the generated shape */
	int resolution;			/* Subdivision of the shape*/
	float anchor[3];		/* Origin point of the reference plane */
	float z_vec[3];			/* Orientation of the reference plane */
} SilhouetteData;

static void silhouette_stroke_free(SilhouetteStroke *stroke)
{
	if (stroke) {
		if (stroke->points) {
			MEM_SAFE_FREE(stroke->points);
		}
		MEM_SAFE_FREE(stroke);
	}
}

static SilhouetteStroke *silhouette_stroke_new(int max_verts)
{
	SilhouetteStroke *stroke = MEM_callocN(sizeof(SilhouetteStroke), "SilhouetteStroke");
	stroke->points = 0;
	stroke->points = MEM_callocN(sizeof(float) * 3 * max_verts,"SilhouetteStrokePoints");/* TODO: Dynamic length */
	stroke->totvert = 0;
	stroke->max_verts = max_verts;
	return stroke;
}

static SilhouetteData *silhouette_data_new(bContext *C)
{
	SilhouetteData *sil = MEM_callocN(sizeof(SilhouetteData), "SilhouetteData");
	Object *obedit = CTX_data_edit_object(C);
	Scene *scene = CTX_data_scene(C);
	Sculpt *sd = CTX_data_tool_settings(C)->sculpt;
	View3D *v3d = CTX_wm_view3d(C);
	const float *fp = ED_view3d_cursor3d_get(scene, v3d);

	sil->ar = CTX_wm_region(C);
	sil->current_stroke = silhouette_stroke_new(1024);
	view3d_set_viewcontext(C, &sil->vc);

	sil->add_col[0] = 1.00; /* add mode color is light red */
	sil->add_col[1] = 0.39;
	sil->add_col[2] = 0.39;

	/*Load RNA Data if present */
	sil->smoothness = sd->silhouette_smoothness / 100.0f;
	sil->depth = sd->silhouette_depth;
	sil->resolution = sd->silhouette_resolution;

	copy_v3_v3(sil->anchor, fp);

	sil->scene = scene;
	sil->ob = obedit;
	sil->state = SIL_INIT;
	return sil;
}

static void silhouette_data_free(struct wmOperator *op)
{
	SilhouetteData *data;
	data = op->customdata;
	if (data) {
		silhouette_stroke_free(data->current_stroke);
		MEM_SAFE_FREE(data);
	}
}

static void silhoute_stroke_point_to_3d(SilhouetteData *sil, int point, float r_v[3])
{
	copy_v3_v3(r_v, &sil->current_stroke->points[point]);
	/*ED_view3d_win_to_3d(sil->vc.v3d, sil->ar, sil->anchor, &sil->current_stroke->points[point], r_v);*/
}

/* TODO: Add dynamic memory allocation */
static void silhouette_stroke_add_3Dpoint(SilhouetteStroke *stroke, float point[3])
{
	if (stroke->totvert < stroke->max_verts) {
		copy_v3_v3(&stroke->points[stroke->totvert * 3], point);
		stroke->totvert ++;
	} else {
		printf("Stroke reached maximum vert count.\n");
	}
}

static void silhouette_stroke_add_point(SilhouetteData *sil, SilhouetteStroke *stroke, float point[2])
{
	if (stroke->totvert < stroke->max_verts) {
		ED_view3d_win_to_3d(sil->vc.v3d, sil->ar, sil->anchor, point, &sil->current_stroke->points[stroke->totvert * 3]);
		stroke->totvert ++;
	} else {
		printf("Stroke reached maximum vert count.\n");
	}
}

/* Set reference plane, 3D plane which is drawn on in 2D */
static void silhouette_set_ref_plane(SilhouetteData *sil)
{
	ED_view3d_global_to_vector(sil->ar->regiondata, sil->anchor, sil->z_vec);
}

static void sculpt_silhouette_stroke_update(float mouse[2], SilhouetteData *sil)
{
	float anchor[3];
	silhouette_set_ref_plane(sil);
	SilhouetteStroke *stroke = sil->current_stroke;

	sil->last_mouse_pos[0] = mouse[0];
	sil->last_mouse_pos[1] = mouse[1];
	silhouette_stroke_add_point(sil, stroke, sil->last_mouse_pos);
	interp_v3_v3v3(anchor, sil->anchor, &stroke->points[stroke->totvert * 3 - 3], 1 / stroke->totvert);
	copy_v3_v3(sil->anchor, anchor);
	ED_region_tag_redraw(sil->ar);
	copy_v2_v2(sil->last_mouse_pos,mouse);
}

typedef enum {
	BRANCH_INIT = 0,
	BRANCH_EDGE_GEN = 1	/* Edges on the ends are stored (used primarly for bridging) */
} BranchState;


/* Spine Memory 
 * The Spine is a treelike representation of the drawn shape.
 * It is used to determine the topology of the shape.
 * The Spine is generated in 4 steps
 * 1. Triangulate the hull drawn by the sculptor. This uses a BMesh triangulation method
 * 2. Traverse all triangles, starting from the first, marching from one tri to its neighbouring tris.
 * 3. Every step gets stored into a branch. Two new branches are created when a tri with 3 neighbours is hit. Traversing ends when a tri with one neighbour is hit.
 * 4. Small, stubby, branches are dissolved into the parent branches.
 * This results in a tree with 3 types of branches. One end, the Caps. Two ends, the Tubes. Three ends, the t-intersections.*/

typedef struct SpineBranch{
	int totpoints;			/* Number of points of the spine. */
	int totforks;			/* Number of connected adjacent branches */
	float *points;			/* Points of the spine. Generated from tri-centers */
	int tot_hull_points;
	int *hull_points;		/* Hullpoints, pointer to stroke points */
	int idx;				/* Index in the Spine branches array */
	int *terminal_points;	/* Description of the connected branches. Per fork 2 ints (point,branch_idx) */
	BranchState flag;
	int *e_start_arr;		/* Edges on the ends are stored (used primarly for bridging) */
	int fs_bs_offset;		/* Frontside edge offset to backside*/
	int *e_flip_side_ends;	/* Front and backside connecting edges of each part*/
}SpineBranch;

/* Main Tree Container */
typedef struct Spine{
	int totbranches;
	SpineBranch **branches; /* All branches. Can contain Null pointers if branches got removed*/
}Spine;

static int get_adjacent_faces(BMFace *f, BMFace **ad_f, BMFace *last_f)
{
	int ad_faces = 0;
	/* There should only be tris in a triangulated mesh. */
	BLI_assert(f->len == 3);

	/* Loop edges in faces */
	BMLoop *f_t_l = f->l_first;
	for (int i = 0; i < 3; i++) {
		/* Loop faces connected to this edge */
		BMLoop *t_l = f_t_l;
		while (t_l != f_t_l->radial_prev && ad_faces < 3) {
			if (t_l->f != f && t_l->f != last_f) {
				ad_f[ad_faces] = t_l->f;
				ad_faces++;
			}
			t_l = t_l->radial_next;
		}
		if (t_l->f != f && t_l->f != last_f) {
			ad_f[ad_faces] = t_l->f;
			ad_faces++;
		}
		f_t_l = f_t_l->next;
	}
	return ad_faces;
}

#ifdef DEBUG_DRAW
static void debug_branch(SpineBranch *sb, unsigned int color)
{
	float v1[3], v2[3];

	if (sb->totpoints > 1) {
		bl_debug_color_set(color);
		for (int j = 1; j < sb->totpoints; j++) {
			copy_v3_v3(v1,&sb->points[j*3-3]);
			copy_v3_v3(v2,&sb->points[j*3  ]);
			bl_debug_draw_edge_add(v1,v2);
		}
	}
	bl_debug_color_set(color);

	/*for(int j = 0; j < sb->tot_hull_points; j++){
		silhoute_stroke_point_to_3d(sil, sb->hull_points[j]*2, v1);
		bl_debug_draw_point(v1, 0.2f);
	 }*/

	/*SpineBranch *rsb;*/
	/*unsigned int forkcol = 0xFFFF00;
	 for(int j = 1; j < sb->totforks; j++){
		printf("Branch fork %i, stroke ids: %i, %i\n", j, sb->terminal_points[j*4], sb->terminal_points[j*4+3]);
		if (spine->branches[sb->terminal_points[j*4+2]] && spine->branches[sb->terminal_points[j*4+2]]->totforks > 0) {
	 bl_debug_color_set(forkcol);
	 silhoute_stroke_point_to_3d(sil, sb->terminal_points[j*4]*2, v1);
	 bl_debug_draw_point(v1, 0.2f);
	 silhoute_stroke_point_to_3d(sil, sb->terminal_points[j*4+3]*2, v1);
	 bl_debug_draw_point(v1, 0.2f);
	 bl_debug_color_set(color);
		}
	 }*/
}

static void debug_spine(Spine *spine)
{
	char color = 0;

	for (int i = 0; i < spine->totbranches; i++) {
		if (spine->branches[i]) {
			bl_debug_color_set(color);
			SpineBranch *sb = spine->branches[i];
			debug_branch(sb, color);
			color += 90;
		}
	}

	bl_debug_color_set(0x000000);
}
#endif

static void free_spine_branch(SpineBranch *branch)
{
	if (branch->points) {
		MEM_freeN(branch->points);
	}
	if (branch->hull_points) {
		MEM_freeN(branch->hull_points);
	}
	if (branch->terminal_points) {
		MEM_freeN(branch->terminal_points);
	}
	if (branch->flag & BRANCH_EDGE_GEN) {
		MEM_freeN(branch->e_start_arr);
		MEM_freeN(branch->e_flip_side_ends);
	}
	MEM_freeN(branch);
}

static void detach_branch(SpineBranch *b, SpineBranch *db)
{
	bool clear = false;
	/* Find the branch to be disconnected in the fork array and shift all following forward */
	for (int i = 0; i < b->totforks; i++) {
		if (b->terminal_points[i * 2 + 1] == db->idx) {
			clear = true;
		} else if (clear) {
			b->terminal_points[i * 2 - 2] = b->terminal_points[i * 2    ];
			b->terminal_points[i * 2 - 1] = b->terminal_points[i * 2 + 1];
		}
	}
	if (clear) {
		b->totforks --;
	}
}

static void dissolve_branch(Spine *spine, SpineBranch *branch, SpineBranch *t_branch)
{
	/* Dissolve all connected branches recursively which arent from the target subbranch */
	for (int i = 0; i < branch->totforks; i++) {
		if (branch->terminal_points[i * 2 + 1] != t_branch->idx) {
			dissolve_branch(spine, spine->branches[branch->terminal_points[i * 2 + 1]], branch);
		}
	}

	detach_branch(t_branch, branch);

	/* Copy Hullpoints from the dissolving branch to the target branch */
	for (int i = 0; i < branch->tot_hull_points; i++) {
		t_branch->hull_points[t_branch->tot_hull_points] = branch->hull_points[i];
		t_branch->tot_hull_points ++;
	}

	spine->branches[branch->idx] = NULL;
	free_spine_branch(branch);
}

static SpineBranch *new_spine_branch(int idx, int max_alloc, int hull_max)
{
	SpineBranch *branch = MEM_callocN(sizeof(SpineBranch), "Spine Branch");
	branch->totpoints = 0;
	branch->totforks = 0;

	/*TODO: way to big, maybe shrink if done creating or dynamic arrays?*/
	branch->points = MEM_callocN(sizeof(float) * 3 * max_alloc, __func__);
	branch->hull_points = MEM_callocN(sizeof(int) * hull_max * 2 * 3, "Spine Hull");
	branch->terminal_points = MEM_callocN(sizeof(int) * 3 * 2, "Spine terminal");

	branch->idx = idx;
	return branch;
}

static Spine *new_spine(int max_alloc, int hull_max)
{
	Spine *s = MEM_callocN(sizeof(Spine),"Silhouette Spine");
	s->branches = MEM_callocN(sizeof(SpineBranch *) * max_alloc,"branches");
	s->totbranches = 1;
	s->branches[0] = new_spine_branch(0, max_alloc, hull_max);
	return s;
}

static void free_spine(Spine *spine)
{
	for (int i = 0; i < spine->totbranches; i++) {
		if (spine->branches[i]) {
			free_spine_branch(spine->branches[i]);
		}
	}
	MEM_freeN(spine->branches);
}

static SpineBranch *spine_branchoff(Spine *spine, SpineBranch *current_branch, int max_alloc, int hull_max)
{
	SpineBranch *new_branch = new_spine_branch(spine->totbranches, max_alloc, hull_max);
	spine->branches[spine->totbranches] = new_branch;

	current_branch->terminal_points[current_branch->totforks * 2    ] = current_branch->totpoints - 1;
	current_branch->terminal_points[current_branch->totforks * 2 + 1] = spine->totbranches;

	copy_v3_v3(&new_branch->points[0], &current_branch->points[current_branch->totpoints * 3 - 3]);
	new_branch->totpoints = 1;

	new_branch->terminal_points[0] = 0;
	new_branch->terminal_points[1] = current_branch->idx;

	current_branch->totforks ++;
	new_branch->totforks = 1;

	spine->totbranches ++;

	return new_branch;
}

static void add_face_to_branch(SpineBranch *branch, BMFace *f)
{
	float center[3];
	BM_face_calc_center_mean_weighted(f, center);
	copy_v3_v3(branch->points + branch->totpoints * 3, center);
	branch->totpoints++;

	branch->hull_points[branch->tot_hull_points    ] = BM_elem_index_get(f->l_first->v);
	branch->hull_points[branch->tot_hull_points + 1] = BM_elem_index_get(f->l_first->next->v);
	branch->hull_points[branch->tot_hull_points + 2] = BM_elem_index_get(f->l_first->prev->v);
	branch->tot_hull_points += 3;
}

static int calc_mid_spine_rec(BMFace *f, Spine *spine, SpineBranch *active_branch, BMFace *last_f, int max_alloc, int hull_max)
{
	BMFace **ad_f = MEM_callocN(sizeof(BMFace *) * 3,__func__);
	int adjacent_faces = get_adjacent_faces(f, ad_f, last_f);
	int added_points = 0;		/* Points added by the current branch */
	int sub_added_points = 0;	/* Points added by child branches */

	/* TODO Maybe not duplicates? */
	add_face_to_branch(active_branch, f);
	added_points ++;

	SpineBranch *new_branch;

	if (adjacent_faces == 1) {
		added_points += calc_mid_spine_rec(ad_f[0], spine, active_branch, f, max_alloc, hull_max);
	} else if (adjacent_faces == 2) {
		/*
		 Longer Branches?

		 if(last_f != NULL){
			float v_tmp[3], v_origin[3], v_branch1[3], v_branch2[3];
			BM_face_calc_center_mean_weighted(f, v_tmp);
			BM_face_calc_center_mean_weighted(last_f, v_origin);
			sub_v3_v3(v_origin, v_tmp);
			BM_face_calc_center_mean_weighted(ad_f[0], v_branch1);
			sub_v3_v3(v_branch1, v_tmp);
			normalize_v3(v_branch1);
			BM_face_calc_center_mean_weighted(ad_f[1], v_branch2);
			sub_v3_v3(v_branch2, v_tmp);
			normalize_v3(v_branch2);

			//switch branches depending on angle
			if(dot_v3v3(v_origin, v_branch1) < dot_v3v3(v_origin, v_branch2)){
				ad_f[2] = ad_f[0];
				ad_f[0] = ad_f[1];
				ad_f[1] = ad_f[2];
			}
		}*/

		new_branch = spine_branchoff(spine, active_branch, max_alloc, hull_max);
		sub_added_points = calc_mid_spine_rec(ad_f[0], spine, new_branch, f, max_alloc, hull_max);
		/* Controls when to remove small/stubby branches */
		if (sub_added_points < 20 && new_branch->totforks < 3) {
			dissolve_branch(spine, new_branch, active_branch);
		}
		added_points += sub_added_points;

		new_branch = spine_branchoff(spine, active_branch, max_alloc, hull_max);
		sub_added_points = calc_mid_spine_rec(ad_f[1], spine, new_branch, f, max_alloc, hull_max);
		if (sub_added_points < 20 && new_branch->totforks < 3) {
			dissolve_branch(spine, new_branch, active_branch);
		}
		added_points += sub_added_points;
	}

	MEM_freeN(ad_f);
	return added_points;
}

static Spine *silhouette_generate_spine(SilhouetteData *sil, SilhouetteStroke *stroke)
{
	BMesh *bm;
	const struct BMeshCreateParams bm_create_params = {0};
	BMVert **vert_arr = MEM_mallocN(sizeof(BMVert *) * stroke->totvert, __func__);
	BMFace *f;
	BMVert *v;
	float v_co[3];

	/* Generate a BMesh from the drawn hull */
	bm = BM_mesh_create(&bm_mesh_allocsize_default,
						&bm_create_params);

	for (int i = 0; i < stroke->totvert; i++) {
		silhoute_stroke_point_to_3d(sil, i * 3, v_co);
		v = BM_vert_create( bm, v_co, NULL, BM_CREATE_NOP);
		BM_elem_index_set(v,i);
		vert_arr[i] = v;
	}

	f = BM_face_create_ngon_verts(bm, vert_arr, stroke->totvert,
								  NULL, BM_CREATE_NO_DOUBLE,
								  true, true);
	BM_face_normal_update(f);

	int save_max = (2 * stroke->totvert - 2) * 2;
	int faces_array_tot = save_max;/* Upper limit -convexhull verts for precise calc */
	BMFace **faces_array = BLI_array_alloca(faces_array, faces_array_tot);
	struct MemArena *pf_arena;
	struct Heap *pf_heap;
	struct EdgeHash *pf_ehash;

	pf_arena = BLI_memarena_new(BLI_POLYFILL_ARENA_SIZE, __func__);

	pf_heap = BLI_heap_new_ex(BLI_POLYFILL_ALLOC_NGON_RESERVE);
	pf_ehash = BLI_edgehash_new_ex(__func__, BLI_POLYFILL_ALLOC_NGON_RESERVE);

	/* Triangulate to traverse faces */
	BM_face_triangulate(bm, f,
						faces_array,
						&faces_array_tot,
						NULL,
						NULL,
						NULL,
						MOD_TRIANGULATE_QUAD_BEAUTY,
						MOD_TRIANGULATE_NGON_BEAUTY,
						false, pf_arena, pf_heap, pf_ehash);

	/*
	 Debug triangulated face:
	 for (int i = 0; i < faces_array_tot; i++){
		f = faces_array[i];
		bl_debug_draw_edge_add(f->l_first->e->v1->co,f->l_first->e->v2->co);
		bl_debug_draw_edge_add(f->l_first->next->e->v1->co,f->l_first->next->e->v2->co);
		bl_debug_draw_edge_add(f->l_first->next->next->e->v1->co,f->l_first->next->next->e->v2->co);
	}*/

	/* start traversing at the first face */
	f = faces_array[0];
	Spine *spine = new_spine(save_max, stroke->totvert);
	printf("Verts in stroke: %i\n", stroke->totvert);
	/* traverse recursively */
	calc_mid_spine_rec(f, spine, spine->branches[0], NULL, save_max, stroke->totvert);
	printf("Spine generated with %i Branches.\n", spine->totbranches);
#ifdef DEBUG_DRAW
	/*debug_spine(spine);*/
#endif
	return spine;
}

/*
 *  d      ** | ******c
 *      *     |       |
 *    *       |       |
 *   *        |       |
 *  *         |       |
 *  ---------- --------
 *  *         |       |
 *  *      smooth     |
 * a _________|______ b
 * Interpolate between the three points resulting in a vertex line between a and c.
 * Smoothness regulates the cutoff to start a circular interpolation
 */
static void calc_vert_quarter(Mesh *me, float a[3], float b[3], float c[3], int v_steps, int w_h_steps, float smoothness, bool flip, bool flip_side){
	int v_start = me->totvert;
	int v_pos = flip ? v_start + v_steps + w_h_steps - 1 - (flip_side ? 1 : 0): v_start;
	float inv_smooth = 1.0f - smoothness;
	float v1[3], v2[3], v3[3], v4[3], v5[3], up[3], side[3], d[3];
	float f_sin, f_cos;
	int s_steps_w = inv_smooth * w_h_steps;
	int s_steps_v = inv_smooth * v_steps;
	int s_steps_c = v_steps - s_steps_v + w_h_steps - s_steps_w;

	ED_mesh_vertices_add(me, NULL, v_steps + w_h_steps - (flip_side ? 1 : 0));

	sub_v3_v3v3(up, c, b);
	add_v3_v3v3(d, a, up);
	mul_v3_fl(up, 1.0f / (float)v_steps);
	sub_v3_v3v3(side, a, b);
	if (w_h_steps > 0) {
		mul_v3_fl(side, 1.0f / (float)(w_h_steps));
	}
	mul_v3_v3fl(v2, side, s_steps_w);
	add_v3_v3(v2, c);

	copy_v3_v3(v1, a);
	for (int v = 0; v < s_steps_v; v++) {
		if (!flip_side || v > 0) {
			copy_v3_v3(me->mvert[v_pos].co, v1);
			me->mvert[v_pos].flag = 0;
			me->mvert[v_pos].bweight = 0;
			v_pos += flip ? - 1 : 1;
		}
		if (v < s_steps_v - 1) {
			add_v3_v3(v1, up);
		}
	}

	sub_v3_v3v3(v3, d, v1);
	sub_v3_v3v3(v4, d, v2);
	sub_v3_v3(v1, v4);

	for (int c = 1; c < s_steps_c + 1; c++) {
		f_sin = sin(((float)c / (float)(s_steps_c + 1)) * M_PI / 2);
		f_cos = cos(((float)c / (float)(s_steps_c + 1)) * M_PI / 2);

		mul_v3_v3fl(v2, v4, f_cos);
		add_v3_v3v3(v5, v2, v1);
		mul_v3_v3fl(v2, v3, f_sin);
		add_v3_v3(v5, v2);

		copy_v3_v3(me->mvert[v_pos].co, v5);
		me->mvert[v_pos].flag = 0;
		me->mvert[v_pos].bweight = 0;
		v_pos += flip ? - 1 : 1;
	}

	add_v3_v3(v1, v3);
	for (int w = 0; w < s_steps_w; w++) {
		copy_v3_v3(me->mvert[v_pos].co, v1);
		me->mvert[v_pos].flag = 0;
		me->mvert[v_pos].bweight = 0;
		sub_v3_v3(v1, side);
		v_pos += flip ? - 1 : 1;
	}
}

/* W_steps needs to be uneven! */
static void calc_vert_half(Mesh *me, float left[3], float right[3], float center[3], float upper_center[3], int v_steps, int w_steps, float smoothness, bool flip_side)
{
	int half_w = w_steps / 2;
	calc_vert_quarter(me, left, center, upper_center, v_steps, half_w, smoothness, false, flip_side);
	ED_mesh_vertices_add(me, NULL, 1);
	copy_v3_v3(me->mvert[me->totvert - 1].co, upper_center);
	me->mvert[me->totvert - 1].flag = 0;
	me->mvert[me->totvert - 1].bweight = 0;
	calc_vert_quarter(me, right, center, upper_center, v_steps, half_w, smoothness, true, flip_side);
}

/* Create standardised Mesh. Simple UxV rectangular grid. (Edges, Loops, Polys):*/
static void generate_mesh_grid_f_e(Mesh *me, int u_steps, int v_steps, int v_start, bool n_flip)
{
	int face_count = (u_steps - 1) * (v_steps - 1);
	int e_start = me->totedge;
	int l_start = me->totloop;
	int p_start = me->totpoly;
	int p_pos = 0, l_pos = 0, e_pos = 0, v_pos = 0;
	int edges_per_side = face_count * 2 + u_steps + v_steps - 2;
	/*TODO: drag out of branch loop*/
	ED_mesh_edges_add(me, NULL, edges_per_side);
	ED_mesh_loops_add(me, NULL, face_count * 4);
	ED_mesh_polys_add(me, NULL, face_count);

	for (int u = 0; u < u_steps; u++) {
		for (int v = 0; v < v_steps; v++) {
			/* add edges */
			if (v < v_steps - 1 && u < u_steps - 1) {
				e_pos = e_start + u * (v_steps * 2 - 1) + v * 2;
				me->medge[e_pos].crease = 0;
				me->medge[e_pos].bweight = 0;
				me->medge[e_pos].flag = 0;
				me->medge[e_pos].v1 = v_start + u * v_steps + v;
				me->medge[e_pos].v2 = v_start + u * v_steps + v + 1;

				me->medge[e_pos + 1].crease = 0;
				me->medge[e_pos + 1].bweight = 0;
				me->medge[e_pos + 1].flag = 0;
				me->medge[e_pos + 1].v1 = v_start + u * v_steps + v;
				me->medge[e_pos + 1].v2 = v_start + (u + 1) * v_steps + v;

				/* add loops */
				l_pos = l_start + u * 4 * (v_steps - 1) + v * 4;
				v_pos = v_start + u * v_steps + v;
				me->mloop[l_pos].v = v_pos;
				me->mloop[l_pos].e = e_pos;

				if (n_flip) {
					/* clockwise */
					me->mloop[l_pos + 1].v = v_pos + 1;
					me->mloop[l_pos + 1].e = e_pos + 3;

					me->mloop[l_pos + 2].v = v_pos + v_steps + 1;
					me->mloop[l_pos + 2].e = e_pos + v_steps * 2 - 1;

					me->mloop[l_pos + 3].v = v_pos + v_steps;
					me->mloop[l_pos + 3].e = e_pos + 1;
				} else {
					/* anti clockwise */
					me->mloop[l_pos + 1].v = v_pos + v_steps;
					me->mloop[l_pos + 1].e = e_pos + 1;

					me->mloop[l_pos + 2].v = v_pos + v_steps + 1;
					me->mloop[l_pos + 2].e = e_pos + v_steps * 2 - 1;

					me->mloop[l_pos + 3].v = v_pos + 1;
					me->mloop[l_pos + 3].e = e_pos + 3;
				}

				/* add Polys */
				p_pos = p_start + u * (v_steps - 1) + v;
				me->mpoly[p_pos].totloop = 4;
				me->mpoly[p_pos].loopstart = l_pos;
				me->mpoly[p_pos].mat_nr = 0;
				me->mpoly[p_pos].flag = 0;
				me->mpoly[p_pos].pad = 0;


			} else if (v == v_steps - 1 && u != u_steps - 1){
				e_pos = e_start + u * (v_steps * 2 - 1) + v * 2;
				me->medge[e_pos].crease = 0;
				me->medge[e_pos].bweight = 0;
				me->medge[e_pos].flag = 0;
				me->medge[e_pos].v1 = v_start + u * v_steps + v;
				me->medge[e_pos].v2 = v_start + (u + 1) * v_steps + v;
			} else if (u == u_steps - 1 && v != v_steps - 1) {
				e_pos = e_start + u * (v_steps * 2 - 1) + v;
				me->medge[e_pos].crease = 0;
				me->medge[e_pos].bweight = 0;
				me->medge[e_pos].flag = 0;
				me->medge[e_pos].v1 = v_start + u * v_steps + v;
				me->medge[e_pos].v2 = v_start + u * v_steps + v + 1;
			}
		}
	}
}

static void bridge_loops(Mesh *me, int e_start_a, int e_start_b, int totvert, bool flip, int a_stride, int b_stride, bool n_flip)
{
	int e_start = me->totedge;
	int l_start = me->totloop;
	int p_start = me->totpoly;
	ED_mesh_edges_add(me, NULL, totvert);
	ED_mesh_loops_add(me, NULL, 4 * totvert - 4);
	ED_mesh_polys_add(me, NULL, totvert - 1);

	for (int i = 0; i < totvert; i++) {
		me->medge[e_start + i].crease = 0;
		me->medge[e_start + i].bweight = 0;
		me->medge[e_start + i].flag = 0;

		if (i < totvert - 1) {
			if (flip) {
				me->medge[e_start + i].v1 = me->medge[e_start_a + i * a_stride].v1;
				me->medge[e_start + i].v2 = me->medge[e_start_b - i * b_stride].v2;
			} else {
				me->medge[e_start + i].v1 = me->medge[e_start_a + i * a_stride].v1;
				me->medge[e_start + i].v2 = me->medge[e_start_b + i * b_stride].v1;
			}

			me->mloop[l_start + i * 4 + 0].v = me->medge[e_start_a + i * a_stride].v1;
			me->mloop[l_start + i * 4 + 0].e = e_start_a + i * a_stride;

			me->mloop[l_start + i * 4 + (n_flip? 3 : 1)].v = me->medge[e_start_a + i * a_stride].v2;
			me->mloop[l_start + i * 4 + (n_flip? 3 : 1)].e = e_start + i + 1;

			if (flip) {
				me->mloop[l_start + i * 4 + 2].v = me->medge[e_start_b - i * b_stride].v1;
				me->mloop[l_start + i * 4 + 2].e = e_start_b - i * b_stride;
				me->mloop[l_start + i * 4 + (n_flip? 1 : 3)].v = me->medge[e_start_b - i * b_stride].v2;
			} else {
				me->mloop[l_start + i * 4 + 2].v = me->medge[e_start_b + i * b_stride].v2;
				me->mloop[l_start + i * 4 + 2].e = e_start_b + i * b_stride;
				me->mloop[l_start + i * 4 + (n_flip? 1 : 3)].v = me->medge[e_start_b + i * b_stride].v1;
			}

			me->mloop[l_start + i * 4 + (n_flip? 1 : 3)].e = e_start + i;

			me->mpoly[p_start + i].loopstart = l_start + i * 4;
			me->mpoly[p_start + i].totloop = 4;
			me->mpoly[p_start + i].mat_nr = 0;
			me->mpoly[p_start + i].flag = 0;
			me->mpoly[p_start + i].pad = 0;
		} else {
			if (flip) {
				me->medge[e_start + i].v1 = me->medge[e_start_a + (i - 1) * a_stride].v2;
				me->medge[e_start + i].v2 = me->medge[e_start_b - (i - 1) * b_stride].v1;
			} else {
				me->medge[e_start + i].v1 = me->medge[e_start_a + (i - 1) * a_stride].v2;
				me->medge[e_start + i].v2 = me->medge[e_start_b + (i - 1) * b_stride].v2;
			}
		}
	}
}

/*
 * Generate a quad from three edges. Returning the newly created edge.
 *  ___a___
 *  |      |
 *  b     new
 *  |      |
 *  ___c___
 */
static int add_quad(Mesh *me, int edge_b, int edge_a, int edge_c, bool flip)
{
	int v_a, v_c;
	MEdge e_a, e_b, e_c;
	int e_start = me->totedge;
	int l_start = me->totloop;
	int p_start = me->totpoly;
	bool b_flip = false;

	e_a = me->medge[edge_a];
	e_b = me->medge[edge_b];
	e_c = me->medge[edge_c];

	if (e_a.v1 == e_b.v1 || e_a.v1 == e_b.v2){
		b_flip = (e_a.v1 == e_b.v1);
		v_a = e_a.v2;
	} else {
		b_flip = (e_a.v2 == e_b.v1);
		v_a = e_a.v1;
	}
	if (e_c.v1 == e_b.v1 || e_c.v1 == e_b.v2){
		v_c = e_c.v2;
	} else {
		v_c = e_c.v1;
	}

	ED_mesh_edges_add(me, NULL, 1);

	me->medge[e_start].v2 = v_a;
	me->medge[e_start].v1 = v_c;
	me->medge[e_start].crease = 0;
	me->medge[e_start].bweight = 0;
	me->medge[e_start].flag = 0;

	ED_mesh_loops_add(me, NULL, 4);
	me->mloop[l_start].v = b_flip ? e_b.v1 : e_b.v2;
	me->mloop[l_start].e = edge_a;

	me->mloop[l_start + (flip ? 1 : 3)].v = v_a;
	me->mloop[l_start + (flip ? 1 : 3)].e = e_start;

	me->mloop[l_start + 2].v = v_c;
	me->mloop[l_start + 2].e = edge_c;

	me->mloop[l_start + (flip ? 3 : 1)].v = b_flip ? e_b.v2 : e_b.v1;
	me->mloop[l_start + (flip ? 3 : 1)].e = edge_b;

	ED_mesh_polys_add(me, NULL, 1);
	me->mpoly[p_start].loopstart = l_start;
	me->mpoly[p_start].totloop = 4;
	me->mpoly[p_start].mat_nr = 0;
	me->mpoly[p_start].flag = 0;
	me->mpoly[p_start].pad = 0;

	return e_start;
}

static void add_face(Mesh *me, int e1, int e2, int e3, int e4, int l_start, int p_start, bool flip){
	int comp_v1;

	if (me->medge[e1].v1 == me->medge[e2].v1 || me->medge[e1].v1 == me->medge[e2].v2) {
		comp_v1 = me->medge[e1].v1;
		me->mloop[l_start].v = me->medge[e1].v2;
		me->mloop[l_start].e = e1;
	} else {
		comp_v1 = me->medge[e1].v2;
		me->mloop[l_start].v = me->medge[e1].v1;
		me->mloop[l_start].e = e1;
	}
	if (me->medge[e2].v1 == comp_v1) {
		me->mloop[l_start + (flip ? 3 : 1)].v = me->medge[e2].v1;
		comp_v1 = me->medge[e2].v2;
	} else {
		me->mloop[l_start + (flip ? 3 : 1)].v = me->medge[e2].v2;
		comp_v1 = me->medge[e2].v1;
	}
	me->mloop[l_start + (flip ? 3 : 1)].e = e2;

	if (me->medge[e3].v1 == comp_v1) {
		me->mloop[l_start + 2].v = me->medge[e3].v1;
		comp_v1 = me->medge[e3].v2;
	} else {
		me->mloop[l_start + 2].v = me->medge[e3].v2;
		comp_v1 = me->medge[e3].v1;
	}
	me->mloop[l_start + 2].e = e3;

	if (me->medge[e4].v1 == comp_v1) {
		me->mloop[l_start + (flip ? 1 : 3)].v = me->medge[e4].v1;
		comp_v1 = me->medge[e4].v2;
	} else {
		me->mloop[l_start + (flip ? 1 : 3)].v = me->medge[e4].v2;
		comp_v1 = me->medge[e4].v1;
	}
	me->mloop[l_start + (flip ? 1 : 3)].e = e4;

	me->mpoly[p_start].totloop = 4;
	me->mpoly[p_start].loopstart = l_start;
	me->mpoly[p_start].mat_nr = 0;
	me->mpoly[p_start].flag = 0;
	me->mpoly[p_start].pad = 0;
}

/* TODO: is there a sort function already?*/
static int cmpfunc (const void * a, const void * b)
{
	return ( *(int*)a - *(int*)b );
}

/* Generate the Tube shape for branches with two ends. */
static void fill_tube(Mesh *me, float *left, float *right, int totl, int totr, int u_steps, float z_vec[3], int v_steps, int w_steps, float smoothness, int *r_edge_loop_ends, bool n_g_flip, bool flip_side)
{
	float step_l = left[totl * 4 - 1] / (float)u_steps;
	float step_r = right[totr * 4 - 1] / (float)u_steps;
	float a, b, f;
	float v1[3], v2[3], v3[3], v4[3];
	int l_u_pos_i = 1, r_u_pos_i = totr - 2;

	const int v_start = me->totvert;
	const int e_start = me->totedge;

	for (int u = 0; u < u_steps; u++) {
		while (l_u_pos_i < totl - 1 && left[l_u_pos_i * 4 + 3] <= step_l * (float)u) {
			l_u_pos_i ++;
		}

		while (r_u_pos_i > 0 && right[r_u_pos_i * 4 + 3] > step_r * (float)(u_steps - u - 1)) {
			r_u_pos_i --;
		}

		/* Interpolate over the points of each side. Interpolate an even point distribution along the line */
		if (totl > 1) {
			a = left[l_u_pos_i * 4 - 1];
			b = left[l_u_pos_i * 4 + 3];
			if (a != b) {
				f = (step_l * (float)u - a) / (b - a);
			} else {
				f = 0.0f;
			}
			interp_v3_v3v3(v1, &left[l_u_pos_i * 4 - 4], &left[l_u_pos_i * 4], f);
		} else {
			copy_v3_v3(v1, &left[0]);
		}

		if (totr > 1) {
			a = right[r_u_pos_i * 4 + 7];
			b = right[r_u_pos_i * 4 + 3];
			if (a != b) {
				f = (step_r * (float)(u_steps - u - 1) - a) / (b - a);
			} else {
				f = 0.0f;
			}
			interp_v3_v3v3(v2, &right[r_u_pos_i * 4 + 4], &right[r_u_pos_i * 4], f);
		} else {
			copy_v3_v3(v2, &right[0]);
		}

		add_v3_v3v3(v3, v1, v2);
		mul_v3_fl(v3, 0.5f);
		add_v3_v3v3(v4, v3, z_vec);

		/* v1 left, v2 right, v3 center bottom, v4 center top */
		calc_vert_half(me, v1, v2, v3, v4, v_steps, w_steps, smoothness, flip_side);
	}
	generate_mesh_grid_f_e(me, u_steps, v_steps * 2 + w_steps - (flip_side ? 2 : 0), v_start, n_g_flip);
	if (!flip_side) {
		r_edge_loop_ends[0] = e_start;
		r_edge_loop_ends[1] = 2;
		r_edge_loop_ends[2] = me->totedge - v_steps * 2 - w_steps + 1;
		r_edge_loop_ends[3] = 1;
	} else {
		r_edge_loop_ends[4] = e_start;
		r_edge_loop_ends[5] = 2;
		r_edge_loop_ends[6] = me->totedge - v_steps * 2 - w_steps + 3;
		r_edge_loop_ends[7] = 1;
	}
}

static int get_cyclic_offset(SpineBranch *branch)
{
	int cyclic_offset = 0, n_i = 0;
	if (branch->hull_points[0] == 0) {
		for (int i = 0; i < branch->tot_hull_points; i++) {
			if (n_i > 0) {
				cyclic_offset ++;
			}
			if (branch->hull_points[i] + 1 != branch->hull_points[i + 1] &&
				branch->hull_points[i] != branch->hull_points[i + 1] &&
				i < branch->tot_hull_points - 1)
			{
				n_i ++;
				cyclic_offset = 0;
			}
		}
	}
	return cyclic_offset;
}

/* Generate the Cap for branches with one ends. */
static void add_ss_cap(SilhouetteData *sil, SpineBranch *branch, Mesh *me, float z_vec[3], float depth, int v_steps, int w_steps, float smoothness, bool n_g_flip, bool flip_side)
{
	float *cap_p = NULL;
	float v1[3], m_center[3], m_center_up[3], left_ref[3], right_ref[3];
	float totlength = 0.0f;
	float step_size = depth / (float)w_steps;
	int cyclic_offset = 0, n_i = 0;
	int e_cap_start_a, e_cap_start_b, e_cap_start_c;
	int e_corner_a, e_corner_b;
	int cap_end_flip_e_start, cap_end_flip_start_a_l, cap_end_flip_start_b_l;
	int cap_end_flip_start_a_r, cap_end_flip_start_b_r, e_cap_tube_start, e_cap_tube_end;
	int e_flip_tube_end[2];
	BLI_array_declare(cap_p);

	if (!flip_side) {
		branch->fs_bs_offset = me->totedge;
	}

	/* calc and sort hullpoints for the three sides */
	qsort (branch->hull_points, branch->tot_hull_points, sizeof(int), cmpfunc);

	cyclic_offset = get_cyclic_offset(branch);

	for (int i = 0; i < branch->tot_hull_points; i++) {
		n_i = branch->tot_hull_points + i - cyclic_offset;
		BLI_array_grow_items(cap_p, 4);
		silhoute_stroke_point_to_3d(sil, branch->hull_points[n_i % branch->tot_hull_points] * 3, &cap_p[i * 4]);
		if (i > 0) {
			cap_p[i * 4 + 3] = len_v3v3(&cap_p[i * 4], v1) + cap_p[i * 4 - 1];
		} else {
			cap_p[i * 4 + 3] = 0.0f;
		}
		copy_v3_v3(v1, &cap_p[i * 4]);
	}

	if (!(branch->flag & BRANCH_EDGE_GEN)) {
		branch->e_start_arr = MEM_callocN(sizeof(int) * 4, "edge startposition array");
		branch->e_flip_side_ends = MEM_callocN(sizeof(int) * 2, "fs bs edges");
		branch->flag |= BRANCH_EDGE_GEN;
	}

	totlength = cap_p[branch->tot_hull_points * 4 - 1];

	float cap_length = fmin(totlength, (w_steps * step_size));
	step_size = cap_length / (float)w_steps;
	float cap_pos = (totlength - cap_length) * 0.5f;
	float a, b, f;
	int u_pos_i = 0;
	int v_start = me->totvert;
	int u_steps;
	int e_start_tube[8];
	int totl = 0, totr = 0;
	int e_flip_start;
	int e_flip_offset = 0;	/* carry edgecount difference in both sides to refrence opposing edges by subtracting totedge - flip offset. Only valid if flip_side = true*/

	/* If the cap is big enough a tube is added between the cap and the last branch. */
	if (totlength > step_size * w_steps) {
		int side_l = cap_pos;
		float *left = NULL, *right = NULL;
		float n_off_right;
		BLI_array_declare(left);
		BLI_array_declare(right);

		while (cap_p[u_pos_i * 4 + 3] < side_l) {
			BLI_array_grow_items(left, 4);
			left[totl * 4    ] = cap_p[u_pos_i * 4    ];
			left[totl * 4 + 1] = cap_p[u_pos_i * 4 + 1];
			left[totl * 4 + 2] = cap_p[u_pos_i * 4 + 2];
			left[totl * 4 + 3] = cap_p[u_pos_i * 4 + 3];
			totl ++;
			u_pos_i ++;
		}
		while (cap_p[u_pos_i * 4 + 3] < totlength - side_l) {
			u_pos_i ++;
		}
		while (u_pos_i < branch->tot_hull_points) {
			if (totr == 0) {
				n_off_right = cap_p[u_pos_i * 4 + 3];
			}
			BLI_array_grow_items(right, 4);
			right[totr * 4    ] = cap_p[u_pos_i * 4    ];
			right[totr * 4 + 1] = cap_p[u_pos_i * 4 + 1];
			right[totr * 4 + 2] = cap_p[u_pos_i * 4 + 2];
			right[totr * 4 + 3] = cap_p[u_pos_i * 4 + 3] - n_off_right;
			totr ++;
			u_pos_i ++;
		}

		if (totl >= 1 && totr >= 1) {
			u_steps = fmax(2.0f, fmax(left[totl * 4 - 1], right[totr * 4 - 1]) / (float)(2 * depth / v_steps));
			e_flip_start = me->totedge;
			fill_tube(me, left, right, totl, totr, u_steps, z_vec, v_steps, w_steps, smoothness, e_start_tube, n_g_flip, flip_side);
			copy_v3_v3(left_ref, &left[totl * 4 - 4]);
			copy_v3_v3(right_ref, &right[0]);
			cap_length = totlength - left[totl * 4 - 1] - right[totr * 4 - 1];

			if (flip_side) {
				branch->e_flip_side_ends[0] = me->totedge;
				bridge_loops(me,
							 e_flip_start + 1,
							 e_flip_start - branch->fs_bs_offset + 1,
							 u_steps,
							 false,
							 ((v_steps - 1) * 2 + w_steps) * 2 - 1,
							 (v_steps * 2 + w_steps) * 2 - 1,
							 !n_g_flip);
				branch->e_flip_side_ends[1] = me->totedge;
				e_flip_tube_end[0] = me->totedge - 1;
				bridge_loops(me,
							 e_flip_start + ((v_steps - 1) * 2 + w_steps) * 2 - 2,
							 e_flip_start - branch->fs_bs_offset + (v_steps * 2 + w_steps) * 2 - 2,
							 u_steps,
							 false,
							 ((v_steps - 1) * 2 + w_steps) * 2 - 1,
							 (v_steps * 2 + w_steps) * 2 - 1,
							 n_g_flip);
				e_flip_tube_end[1] = me->totedge - 1;
				e_flip_offset += u_steps * 2 - 2;
			}
		}
		BLI_array_free(left);
		BLI_array_free(right);
	}

	if (totlength <= step_size * w_steps || totl == 0 || totr == 0) {
		copy_v3_v3(left_ref, &cap_p[0]);
		copy_v3_v3(right_ref, &cap_p[branch->tot_hull_points * 4 - 4]);
	}

	cap_pos = (totlength - cap_length) / 2.0f;
	step_size = cap_length / (w_steps + 2);

	interp_v3_v3v3(m_center, left_ref, right_ref, 0.5f);
	add_v3_v3v3(m_center_up, m_center, z_vec);

	/* Add connecting edge */
	v_start = me->totvert;
	e_cap_start_a = me->totedge;
	calc_vert_half(me,
				   left_ref,
				   right_ref,
				   m_center,
				   m_center_up,
				   v_steps,
				   w_steps,
				   smoothness,
				   flip_side);

	/*TODO connect to flipside */
	ED_mesh_edges_add(me, NULL, v_steps * 2 + w_steps - 1 - (flip_side ? 2 : 0));
	for(int v = 0; v < v_steps * 2 + w_steps - 1 - (flip_side ? 2 : 0); v++){
		me->medge[e_cap_start_a + v].v1 = v_start + v;
		me->medge[e_cap_start_a + v].v2 = v_start + v + 1;
		me->medge[e_cap_start_a + v].crease = 0;
		me->medge[e_cap_start_a + v].bweight = 0;
		me->medge[e_cap_start_a + v].flag = 0;
	}

	e_flip_offset += 2;

	cap_pos += step_size;

	u_pos_i = 1;
	v_start = me->totvert;

	for (int u = 0; u < w_steps; u++) {
		while (u_pos_i < branch->tot_hull_points && cap_p[u_pos_i * 4 + 3] <= cap_pos) {
			u_pos_i ++;
		}

		interp_v3_v3v3(m_center, left_ref, right_ref, smoothness * 0.5f + (1.0f - smoothness) * ((float)u / w_steps));
		add_v3_v3v3(m_center_up, m_center, z_vec);

		a = cap_p[u_pos_i * 4 - 1];
		b = cap_p[u_pos_i * 4 + 3];
		if (a != b) {
			f = (cap_pos - a) / (b - a);
		} else {
			f = 0.0f;
		}
		interp_v3_v3v3(v1, &cap_p[u_pos_i * 4 - 4], &cap_p[u_pos_i * 4], f);

		calc_vert_quarter(me,
						  v1,
						  m_center,
						  m_center_up,
						  v_steps,
						  0,
						  smoothness,
						  false,
						  flip_side);

		cap_pos += step_size;
	}
	e_cap_start_b = me->totedge;
	generate_mesh_grid_f_e(me, w_steps, v_steps - (flip_side ? 1 : 0), v_start, n_g_flip);
	e_cap_start_c = me->totedge;

	if (flip_side) {
		cap_end_flip_e_start = me->totedge;
		bridge_loops(me,
					 e_cap_start_b + 1,
					 e_cap_start_b - branch->fs_bs_offset + e_flip_offset + 1,
					 w_steps,
					 false,
					 v_steps * 2 - 3,
					 v_steps * 2 - 1,
					 !n_g_flip);
		e_flip_offset += w_steps - 1;
	}

	bridge_loops(me,
				 e_cap_start_a,
				 e_cap_start_b,
				 v_steps - (flip_side ? 1 : 0),
				 false,
				 1,
				 2,
				 !n_g_flip);

	e_corner_a = me->totedge - 1;
	cap_end_flip_start_a_l = me->totedge - v_steps + 1;
	cap_end_flip_start_b_l = me->totedge - v_steps + 1 - branch->fs_bs_offset + e_flip_offset;

	bridge_loops(me,
				 e_cap_start_a + v_steps - (flip_side ? 1 : 0),
				 e_cap_start_b + 2 * v_steps - 2 - (flip_side ? 2 : 0),
				 w_steps,
				 false,
				 1,
				 2 * v_steps - 1 - (flip_side ? 2 : 0),
				 !n_g_flip);

	e_corner_b = me->totedge - 1;

	bridge_loops(me,
				 e_cap_start_a + v_steps - (flip_side ? 1 : 0) + w_steps,
				 e_cap_start_c - 1,
				 v_steps - (flip_side ? 1 : 0),
				 true,
				 1,
				 1,
				 !n_g_flip);
	cap_end_flip_start_a_r = me->totedge - 1;
	cap_end_flip_start_b_r = me->totedge - branch->fs_bs_offset + e_flip_offset + 1;

	ED_mesh_loops_add(me, NULL, 6);
	ED_mesh_polys_add(me, NULL, 2);
	/* corner a */
	me->mloop[me->totloop - 6].v = me->medge[e_cap_start_a + v_steps -(flip_side ? 1 : 0) - 1].v1;
	me->mloop[me->totloop - 6].e = e_cap_start_a + v_steps -(flip_side ? 1 : 0) - 1;

	me->mloop[me->totloop - (n_g_flip ? 5 : 4)].v = me->medge[e_corner_a + 1].v1;
	me->mloop[me->totloop - (n_g_flip ? 5 : 4)].e = e_corner_a + 1;

	me->mloop[me->totloop - (n_g_flip ? 4 : 5)].v = me->medge[e_corner_a].v2;
	me->mloop[me->totloop - (n_g_flip ? 4 : 5)].e = e_corner_a;

	me->mpoly[me->totpoly - 2].loopstart = me->totloop - 6;
	me->mpoly[me->totpoly - 2].totloop = 3;
	me->mpoly[me->totpoly - 2].mat_nr = 0;
	me->mpoly[me->totpoly - 2].flag = 0;
	me->mpoly[me->totpoly - 2].pad = 0;

	/* corner b*/
	me->mloop[me->totloop - 3].v = me->medge[e_cap_start_a + v_steps -(flip_side ? 1 : 0) + w_steps - 1].v1;
	me->mloop[me->totloop - 3].e = e_cap_start_a + v_steps -(flip_side ? 1 : 0) + w_steps - 1;

	me->mloop[me->totloop - (n_g_flip ? 2 : 1)].v = me->medge[e_corner_b + 1].v1;
	me->mloop[me->totloop - (n_g_flip ? 2 : 1)].e = e_corner_b + 1;

	me->mloop[me->totloop - (n_g_flip ? 1 : 2)].v = me->medge[e_corner_b].v2;
	me->mloop[me->totloop - (n_g_flip ? 1 : 2)].e = e_corner_b;

	me->mpoly[me->totpoly - 1].loopstart = me->totloop - 3;
	me->mpoly[me->totpoly - 1].totloop = 3;
	me->mpoly[me->totpoly - 1].mat_nr = 0;
	me->mpoly[me->totpoly - 1].flag = 0;
	me->mpoly[me->totpoly - 1].pad = 0;

	if (totlength <= step_size * w_steps || totl == 0 || totr == 0) {
		branch->e_start_arr[flip_side ? 2 : 0] = e_cap_start_a;
		branch->e_start_arr[flip_side ? 3 : 1] = 1;
	} else {
		e_cap_tube_start = me->totedge;
		bridge_loops(me,
					 e_cap_start_a,
					 e_start_tube[flip_side ? 6 : 2],
					 v_steps * 2 + w_steps - (flip_side ? 2 : 0),
					 false,
					 1,
					 1,
					 n_g_flip);
		e_cap_tube_end = me->totedge - 1;
		e_flip_offset += 2;
		branch->e_start_arr[flip_side ? 2 : 0] = e_start_tube[flip_side ? 4 : 0];
		branch->e_start_arr[flip_side ? 3 : 1] = e_start_tube[flip_side ? 5 : 1];
	}

	if (!flip_side) {
		branch->fs_bs_offset = me->totedge - branch->fs_bs_offset;
	} else {
		add_quad(me, cap_end_flip_e_start, cap_end_flip_start_a_l, cap_end_flip_start_b_l, !n_g_flip);
		add_quad(me, cap_end_flip_e_start + w_steps - 1, cap_end_flip_start_a_r, cap_end_flip_start_b_r, n_g_flip);
		if (totlength <= step_size * w_steps || totl == 0 || totr == 0) {
			branch->e_flip_side_ends[0] = me->totedge - 2;
			branch->e_flip_side_ends[1] = me->totedge - 1;
		} else {
			ED_mesh_loops_add(me, NULL, 8);
			ED_mesh_polys_add(me, NULL, 2);
			add_face(me,
					 e_flip_tube_end[0],
					 e_cap_tube_start - branch->fs_bs_offset + e_flip_offset,
					 me->totedge - 2,
					 e_cap_tube_start,
					 me->totloop - 8,
					 me->totpoly - 2,
					 n_g_flip);

			add_face(me,
					 e_flip_tube_end[1],
					 e_cap_tube_end - branch->fs_bs_offset + e_flip_offset + 2,
					 me->totedge - 1,
					 e_cap_tube_end,
					 me->totloop - 4,
					 me->totpoly - 1,
					 !n_g_flip);
		}
	}

	BLI_array_free(cap_p);
}

static int calc_branch_orientation(Spine *spine, SpineBranch *branch, float point[3], int f_s)
{
	int s = 0;
	float last_dist = len_v3v3(&spine->branches[branch->terminal_points[1]]->points[spine->branches[branch->terminal_points[1]]->totpoints * 3 - 3], point);
	if (spine->branches[branch->terminal_points[3]]->totpoints < 2 || spine->branches[branch->terminal_points[5]]->totpoints < 2) {
		return f_s;
	}
	float curr_dist = len_v3v3(&spine->branches[branch->terminal_points[3]]->points[3], point);
	if(last_dist > curr_dist){
		last_dist = curr_dist;
		s = 1;
	}
	curr_dist = len_v3v3(&spine->branches[branch->terminal_points[5]]->points[3], point);
	if(last_dist > curr_dist){
		last_dist = curr_dist;
		s = 2;
	}
	return s;
}

/* Generate a T-Intersection for branches with three ends. */
static void add_ss_tinter(SilhouetteData *sil, Spine *spine, SpineBranch *branch, Mesh *me, float z_vec[3], float depth, int v_steps, int w_steps, float smoothness, bool n_g_flip, bool flip_side)
{
	float *sa = NULL;
	int b_start[3] = {0,0,0}, b_tot[3] = {0,0,0};
	int filler = 0;
	int cyclic_offset = 0, n_i = 0;
	int pos_i_sa, u_steps;
	float step_length, a, b, f;
	float v1[3], v2[3], v3[3], v4[3], center[3], center_up[3];
	float center_s[3 * 3];
	int w_h_steps = w_steps / 2;
	int v_start, v_start_center;
	int e_start[3], e_start_center, e_start_inner[3], e_t_sign[6];
	int stride_le;
	int ori[3];
	int e_flip_offset = 0;	/* carry edgecount difference in both sides to refrence opposing edges by subtracting totedge - flip offset. Only valid if flip_side = true*/
	int e_flip_start[3];
	int e_flip_q_l[3], e_flip_q_r[3];
	BLI_array_declare(sa);

	if (!flip_side) {
		branch->fs_bs_offset = me->totedge;
	}

	/* calc and sort hullpoints for the three sides */
	qsort (branch->hull_points, branch->tot_hull_points, sizeof(int), cmpfunc);

	if (branch->hull_points[0] == 0) {
		for (int i = 0; i < branch->tot_hull_points; i++) {
			if (n_i > 2) {
				cyclic_offset ++;
			}
			if (branch->hull_points[i] + 1 != branch->hull_points[i + 1] &&
				branch->hull_points[i] != branch->hull_points[i + 1] &&
				i < branch->tot_hull_points - 1)
			{
				n_i ++;
				cyclic_offset = 0;
			}
		}
	}

	b_start[0] = 0;
	filler = 0;
	for (int i = 0; i < branch->tot_hull_points; i++) {
		n_i = branch->tot_hull_points + i - cyclic_offset;
		BLI_array_grow_items(sa, 4);
		silhoute_stroke_point_to_3d(sil, branch->hull_points[n_i % branch->tot_hull_points] * 3, &sa[b_start[filler] + b_tot[filler] * 4]);
		if(b_tot[filler] == 0){
			sa[b_start[filler] + b_tot[filler] * 4 + 3] = 0.0f;
		}else{
			sa[b_start[filler] + b_tot[filler] * 4 + 3] = len_v3v3(&sa[b_start[filler] + b_tot[filler] * 4 - 4], &sa[b_start[filler] + b_tot[filler] * 4]) + sa[b_start[filler] + b_tot[filler] * 4 - 1];
		}
		b_tot[filler] ++;
		if (branch->hull_points[n_i % branch->tot_hull_points] + 1 != branch->hull_points[(n_i + 1) % branch->tot_hull_points] &&
		   branch->hull_points[n_i % branch->tot_hull_points] != branch->hull_points[(n_i + 1) % branch->tot_hull_points] &&
		   !(cyclic_offset != 0 && b_tot[0] <= cyclic_offset))
		{
			if ((filler + 1) % 3 > 0){
				b_start[(filler + 1) % 3] = b_start[filler] + b_tot[filler] * 4;
			}
			filler = (filler + 1) % 3;
		}
	}

	u_steps = 5;
	zero_v3(center);
	for (int s = 0; s < 3; s++) {
		copy_v3_v3(&center_s[s * 3], &sa[b_start[s]]);
		add_v3_v3(&center_s[s * 3], &sa[b_start[(s + 2) % 3] + b_tot[(s + 2) % 3] * 4 - 4]);
		mul_v3_fl(&center_s[s * 3], 0.5f);
		add_v3_v3(center, &center_s[s * 3]);
	}
	mul_v3_fl(center, 1.0f/3.0f);
	add_v3_v3v3(center_up, center, z_vec);

	for (int s = 0; s < 3; s++) {
		/*TODO: Better max funktion (int)*/
		u_steps = fmax(u_steps, len_v3v3(&center_s[(s + 1) % 3 * 3], &center_s[s * 3]) / (float)(2 * depth / v_steps));
	}

	/*needs to be uneven*/
	u_steps |= 1;

	if (!(branch->flag & BRANCH_EDGE_GEN)) {
		branch->e_start_arr = MEM_callocN(sizeof(int) * 12,"edge startposition array");
		branch->e_flip_side_ends = MEM_callocN(sizeof(int) * 6, "fs bs edges");
		branch->flag |= BRANCH_EDGE_GEN;
	}

	v_start_center = me->totvert;
	ED_mesh_vertices_add(me, NULL, u_steps + u_steps / 2);
	e_start_center = me->totedge;
	ED_mesh_edges_add(me, NULL, (u_steps / 2) * 3);

	for (int s = 0; s < 3; s++) {
		step_length = sa[b_start[s] + b_tot[s] * 4 - 1] / (float)u_steps;

		add_v3_v3v3(v3, &center_s[s * 3], z_vec);

		v_start = me->totvert;

		ori[s] = calc_branch_orientation(spine, branch, &center_s[s * 3], s);
		branch->e_start_arr[(flip_side ? 6 : 0) + ori[s] * 2] = me->totedge;
		branch->e_start_arr[(flip_side ? 6 : 0) + ori[s] * 2 + 1] = 1;

		calc_vert_half(me,
					   &sa[b_start[s]],
					   &sa[b_start[(s + 2) % 3] + b_tot[(s + 2) % 3] * 4 - 4],
					   &center_s[s * 3],
					   v3,
					   v_steps,
					   w_steps,
					   smoothness,
					   flip_side);

		e_start[s] = me->totedge;

		ED_mesh_edges_add(me, NULL, v_steps * 2 + w_steps - 1 - (flip_side ? 2 : 0));
		for(int v = 0; v < v_steps * 2 + w_steps - 1 - (flip_side ? 2 : 0); v++){
			me->medge[e_start[s] + v].v1 = v_start + v;
			me->medge[e_start[s] + v].v2 = v_start + v + 1;
			me->medge[e_start[s] + v].crease = 0;
			me->medge[e_start[s] + v].bweight = 0;
			me->medge[e_start[s] + v].flag = 0;
		}

		e_flip_offset += 2;

		v_start = me->totvert;

		for (int u = 1; u < u_steps - 1; u++) {
			pos_i_sa = 1;
			while (pos_i_sa < b_tot[s] && sa[b_start[s] + pos_i_sa * 4 + 3] <= step_length * (float)u) {
				pos_i_sa ++;
			}

			/* Interpolate over the points of each side. Interpolate an even point distribution along the line */
			if (b_tot[s] < 2) {
				copy_v3_v3(v1, &sa[b_start[s]]);
			} else {
				a = sa[b_start[s] + pos_i_sa * 4 - 1];
				b = sa[b_start[s] + pos_i_sa * 4 + 3];
				if (a != b) {
					f = (step_length * (float)u - a) / (b - a);
				} else {
					f = 0.0f;
				}
				interp_v3_v3v3(v1, &sa[b_start[s] + pos_i_sa * 4 - 4], &sa[b_start[s] + pos_i_sa * 4], f);
			}

			f = fabs((float)(u_steps / 2 - u) / ((float)u_steps / 2.0f));
			if (u < u_steps / 2){
				interp_v3_v3v3(v4, center, &center_s[s * 3], f);
				add_v3_v3v3(v2, v4, z_vec);

				copy_v3_v3(me->mvert[v_start_center + s * (u_steps / 2) + u].co, v2);
				me->mvert[v_start_center + s * (u_steps / 2) + u].flag = 0;
				me->mvert[v_start_center + s * (u_steps / 2) + u].bweight = 0;
			} else if (u == u_steps / 2) {
				copy_v3_v3(v4, center);
				add_v3_v3v3(v2, v4, z_vec);
				if (s == 0) {
					/* Add center at v2 */
					copy_v3_v3(me->mvert[v_start_center].co, v2);
					me->mvert[v_start_center].flag = 0;
					me->mvert[v_start_center].bweight = 0;
				}
			} else {
				interp_v3_v3v3(v4, center, &center_s[(s + 1) % 3 * 3], f);
				add_v3_v3v3(v2, v4, z_vec);
			}

			calc_vert_quarter(me, v1, v4, v2, v_steps, w_h_steps, smoothness, false, flip_side);
		}

		me->medge[e_start_center + s * (u_steps / 2)].v1 = me->medge[e_start[s] + v_steps - (flip_side ? 1 : 0) + w_steps / 2].v1;
		me->medge[e_start_center + s * (u_steps / 2)].v2 = v_start_center + s * (u_steps / 2) + 1;
		me->medge[e_start_center + s * (u_steps / 2)].crease = 0;
		me->medge[e_start_center + s * (u_steps / 2)].bweight = 0;
		me->medge[e_start_center + s * (u_steps / 2)].flag = 0;

		for (int u = 1; u < u_steps / 2 - 1; u++) {
			me->medge[e_start_center + s * (u_steps / 2) + u].v1 = v_start_center + s * (u_steps / 2) + u;
			me->medge[e_start_center + s * (u_steps / 2) + u].v2 = v_start_center + s * (u_steps / 2) + 1 + u;
			me->medge[e_start_center + s * (u_steps / 2) + u].crease = 0;
			me->medge[e_start_center + s * (u_steps / 2) + u].bweight = 0;
			me->medge[e_start_center + s * (u_steps / 2) + u].flag = 0;
		}

		me->medge[e_start_center + (s + 1) * (u_steps / 2) - 1].v1 = v_start_center + s * (u_steps / 2) + 1 + (u_steps / 2 - 2);
		me->medge[e_start_center + (s + 1) * (u_steps / 2) - 1].v2 = v_start_center;
		me->medge[e_start_center + (s + 1) * (u_steps / 2) - 1].crease = 0;
		me->medge[e_start_center + (s + 1) * (u_steps / 2) - 1].bweight = 0;
		me->medge[e_start_center + (s + 1) * (u_steps / 2) - 1].flag = 0;

		e_start_inner[s] = me->totedge;
		generate_mesh_grid_f_e(me, u_steps - 2, v_steps - (flip_side ? 1 : 0) + w_steps / 2, v_start, n_g_flip);

		if (flip_side) {
			e_flip_start[s] = me->totedge;
			bridge_loops(me,
						 e_start_inner[s] + 1,
						 e_start_inner[s] - branch->fs_bs_offset + e_flip_offset + 1,
						 u_steps - 2,
						 false,
						 (v_steps - 1 + w_steps / 2) * 2 - 1,
						 (v_steps + w_steps / 2) * 2 - 1,
						 !n_g_flip);

			e_flip_offset += u_steps - 3;
		}
	}

	for(int s = 0; s < 3; s++){
		int e_end_a, e_end_b;
		bridge_loops(me,
					 e_start_inner[s],
					 e_start[s],
					 v_steps -(flip_side ? 1 : 0) + w_steps / 2,
					 false,
					 2,
					 1,
					 n_g_flip);
		e_flip_offset += 1;

		if (flip_side) {
			e_flip_q_l[0] = e_flip_start[s];
			e_flip_q_l[1] = me->totedge - (v_steps + w_steps / 2) + 1;
			e_flip_q_l[2] = me->totedge - (v_steps + w_steps / 2) - branch->fs_bs_offset + e_flip_offset;
		}

		e_end_a = me->totedge;
		bridge_loops(me,
					 e_start_inner[(s + 2) % 3] + (u_steps - 3) * (2 * (v_steps -(flip_side ? 1 : 0) + w_steps / 2) - 1),
					 e_start[s] + v_steps * 2 -(flip_side ? 2 : 0) + w_steps - 2,
					 v_steps -(flip_side ? 1 : 0) + w_steps / 2,
					 true,
					 1,
					 1,
					 !n_g_flip);

		if (flip_side) {
			e_flip_q_r[0] = e_flip_start[(s + 2) % 3] + u_steps - 3;
			e_flip_q_r[1] = me->totedge - (v_steps + w_steps / 2) + 1;
			e_flip_q_r[2] = me->totedge - (v_steps + w_steps / 2) + 1 - branch->fs_bs_offset + e_flip_offset;
		}

		e_end_b = me->totedge;

		int e_side_a, e_side_b;
		e_side_a = add_quad(me, e_start[s] + v_steps -(flip_side ? 1 : 0) + w_steps / 2, e_start_center + s * (u_steps / 2), e_end_b - 1, !n_g_flip);
		e_side_b = add_quad(me, e_start[s] + v_steps -(flip_side ? 1 : 0) + w_steps / 2 - 1, e_end_a - 1, e_start_center + s * (u_steps / 2), !n_g_flip);
		stride_le = (2 * (v_steps -(flip_side ? 1 : 0) + w_steps / 2) - 1);

		for (int u = 1; u < u_steps / 2 - 1; u++) {
			e_side_b = add_quad(me,
								e_side_b,
								e_start_inner[s] - 1 + stride_le * u,
								e_start_center + s * (u_steps / 2) + u,
								!n_g_flip);
			e_side_a = add_quad(me,
								e_side_a,
								e_start_inner[(s + 2) % 3] - 1 + stride_le * (u_steps - 2 - u),
								e_start_center + s * (u_steps / 2) + u,
								n_g_flip);
		}

		e_t_sign[s * 2] = add_quad(me,
				 e_side_b,
				 e_start_inner[s] - 1 + stride_le * ((u_steps / 2) - 1),
				 e_start_center + s * (u_steps / 2) + u_steps / 2 - 1,
				 !n_g_flip);

		e_t_sign[s * 2 + 1] = e_side_a;

		if (flip_side) {
			branch->e_flip_side_ends[ori[s] * 2] = me->totedge;
			add_quad(me,
					 e_flip_q_l[0],
					 e_flip_q_l[1],
					 e_flip_q_l[2],
					 !n_g_flip);
			branch->e_flip_side_ends[ori[s] * 2 + 1] = me->totedge;
			add_quad(me,
					 e_flip_q_r[0],
					 e_flip_q_r[1],
					 e_flip_q_r[2],
					 n_g_flip);
			e_flip_offset -= 1;
		}
	}

	for(int s = 0; s < 3; s++){
		ED_mesh_loops_add(me, NULL, 4);
		me->mloop[me->totloop - 4].v = me->medge[e_t_sign[(s + 2) % 3 * 2]].v1;
		me->mloop[me->totloop - 4].e = e_t_sign[(s + 2) % 3 * 2];

		me->mloop[me->totloop - (n_g_flip ? 3 : 1)].v = me->medge[e_start_center + s * (u_steps / 2) + u_steps / 2 - 1].v1;
		me->mloop[me->totloop - (n_g_flip ? 3 : 1)].e = e_start_center + s * (u_steps / 2) + u_steps / 2 - 1;

		me->mloop[me->totloop - 2].v = me->medge[e_start_inner[(s + 2) % 3] - 1 + stride_le * ((u_steps / 2))].v2;
		me->mloop[me->totloop - 2].e = e_t_sign[s * 2 + 1];

		me->mloop[me->totloop - (n_g_flip ? 1 : 3)].v = me->medge[e_start_inner[(s + 2) % 3] - 1 + stride_le * ((u_steps / 2))].v1;
		me->mloop[me->totloop - (n_g_flip ? 1 : 3)].e = e_start_inner[(s + 2) % 3] - 1 + stride_le * ((u_steps / 2));

		ED_mesh_polys_add(me, NULL, 1);
		me->mpoly[me->totpoly - 1].loopstart = me->totloop - 4;
		me->mpoly[me->totpoly - 1].totloop = 4;
		me->mpoly[me->totpoly - 1].mat_nr = 0;
		me->mpoly[me->totpoly - 1].flag = 0;
		me->mpoly[me->totpoly - 1].pad = 0;

	}

	if (!flip_side) {
		branch->fs_bs_offset = me->totedge - branch->fs_bs_offset;
	}

	BLI_array_free(sa);
}

static void add_ss_tube(SilhouetteData *sil, SpineBranch *branch, Mesh *me, float z_vec[3], float depth, int v_steps, int w_steps, float w_fact, bool n_g_flip, bool flip_side)
{
	/* x y z l (accumulative length)*/
	float *left = NULL, *right = NULL;
	int totl = 0, totr = 0;
	int u_steps = 0;
	bool f_swap = false;
	int cyclic_offset = 0, n_i = 0;
	int e_start = 0;
	BLI_array_declare(left);
	BLI_array_declare(right);

	if (!flip_side) {
		branch->fs_bs_offset = me->totedge;
	}
	/* Calc and sort Hullpoints to left and right side */
	qsort (branch->hull_points, branch->tot_hull_points, sizeof(int), cmpfunc);

	if (branch->hull_points[0] == 0) {
		for (int i = 0; i < branch->tot_hull_points; i++) {
			if (n_i > 1) {
				cyclic_offset ++;
			}
			if (branch->hull_points[i] + 1 != branch->hull_points[i + 1] &&
				branch->hull_points[i] != branch->hull_points[i + 1] &&
				i < branch->tot_hull_points - 1)
			{
				n_i ++;
				cyclic_offset = 0;
			}
		}
	}

	/* fill left and right arrays with hull points. */
	for (int i = 0; i < branch->tot_hull_points; i++){
		n_i = branch->tot_hull_points + (i - cyclic_offset);
		if (!f_swap) {
			BLI_array_grow_items(left, 4);
			silhoute_stroke_point_to_3d(sil, branch->hull_points[n_i % branch->tot_hull_points] * 3, &left[totl * 4]);
			if (totl > 0) {
				left[totl * 4 + 3] = len_v3v3(&left[totl * 4],&left[totl * 4 - 4]) + left[totl * 4 - 1];
			} else {
				left[totl * 4 + 3] = 0.0f;
			}
			totl ++;
		} else {
			BLI_array_grow_items(right, 4);
			silhoute_stroke_point_to_3d(sil, branch->hull_points[n_i % branch->tot_hull_points] * 3, &right[totr*4]);
			if (totr > 0) {
				right[totr * 4 + 3] = len_v3v3(&right[totr * 4],&right[totr * 4 - 4]) + right[totr * 4 - 1];
			} else {
				right[totr * 4 + 3] = 0.0f;
			}
			totr ++;
		}
		if (branch->hull_points[n_i % branch->tot_hull_points] + 1 != branch->hull_points[(n_i + 1) % branch->tot_hull_points] &&
			branch->hull_points[n_i % branch->tot_hull_points] != branch->hull_points[(n_i + 1) % branch->tot_hull_points] &&
			!(cyclic_offset != 0 && totl <= cyclic_offset))
		{
			f_swap = !f_swap;
		}
	}

	if (totl < 1 && totr < 1) {
		return;
	}

	u_steps = fmax(2.0f, fmax(left[totl * 4 - 1], right[totr * 4 - 1]) / (float)(2 * depth / v_steps));

	if (!(branch->flag & BRANCH_EDGE_GEN)) {
		branch->e_start_arr = MEM_callocN(sizeof(int) * 8, "edge startposition array");
		branch->e_flip_side_ends = MEM_callocN(sizeof(int) * 4, "fs bs edges");
		branch->flag |= BRANCH_EDGE_GEN;
	}

	e_start = me->totedge;

	fill_tube(me, left, right, totl, totr, u_steps, z_vec, v_steps, w_steps, w_fact, branch->e_start_arr, n_g_flip, flip_side);

	if (flip_side) {
		branch->e_flip_side_ends[0] = me->totedge;
		bridge_loops(me,
					 e_start + 1,
					 e_start - branch->fs_bs_offset + 1,
					 u_steps,
					 false,
					 ((v_steps - 1) * 2 + w_steps) * 2 - 1,
					 (v_steps * 2 + w_steps) * 2 - 1,
					 !n_g_flip);
		branch->e_flip_side_ends[1] = me->totedge;
		branch->e_flip_side_ends[2] = me->totedge - 1;
		bridge_loops(me,
					 e_start + ((v_steps - 1) * 2 + w_steps) * 2 - 2,
					 e_start - branch->fs_bs_offset + (v_steps * 2 + w_steps) * 2 - 2,
					 u_steps,
					 false,
					 ((v_steps - 1) * 2 + w_steps) * 2 - 1,
					 (v_steps * 2 + w_steps) * 2 - 1,
					 n_g_flip);
		branch->e_flip_side_ends[3] = me->totedge - 1;
	} else {
		branch->fs_bs_offset = me->totedge - branch->fs_bs_offset;
	}

	BLI_array_free(left);
	BLI_array_free(right);
}

/* TODO: Fix for incomplete Spine Generation. Delete? It calculates the real amount of adjacent branches not counting empty ones.*/
static int r_branch_count(Spine *spine, SpineBranch *b)
{
	int r_forks = 0;
	for (int i = 0; i < b->totforks; i++) {
		if (spine->branches[b->terminal_points[i * 2 + 1]]) {
			r_forks ++;
		}
	}
	return r_forks;
}

/* TODO: T-Intersections are sometimes misordered! Connects the different Branches if they have the BRANCH_EDGE_GEN flag set. */
static void bridge_all_parts_rec(Mesh *me, Spine *spine, SpineBranch *active_branch, SpineBranch *prev_branch, int verts_per_loop, bool n_g_flip)
{
	int b_fork_off, a_fork_off, a_fork_off_inv, b_fork_off_inv, comp_e_start, comp_e_start_flip;
	float dist_a, dist_b;
	a_fork_off = 0;
	for (int i = 0; i < active_branch->totforks; i++) {
		SpineBranch *comp = spine->branches[active_branch->terminal_points[i * 2 + 1]];
		if (comp && comp != prev_branch) {
			if (active_branch->flag & BRANCH_EDGE_GEN && comp->flag & BRANCH_EDGE_GEN) {
				b_fork_off = 0;
				for (int sb = 0; sb < comp->totforks; sb++) {
					if (spine->branches[comp->terminal_points[sb * 2 + 1]]) {
						if (comp->terminal_points[sb * 2 + 1] == active_branch->idx) {
							break;
						}
						b_fork_off ++;
					}
				}

				/* TODO: Might fail with thin geometry */
				dist_a = len_v3v3(me->mvert[me->medge[active_branch->e_start_arr[a_fork_off * 2]].v1].co, me->mvert[me->medge[comp->e_start_arr[b_fork_off * 2]].v1].co);
				dist_b = len_v3v3(me->mvert[me->medge[active_branch->e_start_arr[a_fork_off * 2]].v1].co, me->mvert[me->medge[comp->e_start_arr[b_fork_off * 2] + (verts_per_loop - 2 - 0) * comp->e_start_arr[b_fork_off * 2 + 1]].v2].co);

				/* TODO: Tube bug inverse?
				if (comp->hull_points[0] == 0) {
					bl_debug_color_set(0x0000ff);
					bl_debug_draw_medge_add(me, comp->e_start_arr[b_fork_off * 2] + comp->e_start_arr[b_fork_off * 2 + 1] * (verts_per_loop - 2));
					bl_debug_color_set(0x000000);
				}*/
				a_fork_off_inv = a_fork_off * 2 + active_branch->totforks * 2;
				b_fork_off_inv = b_fork_off * 2 + comp->totforks * 2;
				if (dist_a > dist_b) {
					comp_e_start = comp->e_start_arr[b_fork_off * 2] + comp->e_start_arr[b_fork_off * 2 + 1] * (verts_per_loop - 2);
					comp_e_start_flip = comp->e_start_arr[b_fork_off_inv] + comp->e_start_arr[b_fork_off_inv + 1] * (verts_per_loop - 4);
				} else {
					comp_e_start = comp->e_start_arr[b_fork_off * 2];
					comp_e_start_flip = comp->e_start_arr[b_fork_off_inv];
				}
				bridge_loops(me,
							 active_branch->e_start_arr[a_fork_off * 2],
							 comp_e_start,
							 verts_per_loop,
							 (dist_a > dist_b),
							 active_branch->e_start_arr[a_fork_off * 2 + 1],
							 comp->e_start_arr[b_fork_off * 2 + 1],
							 !n_g_flip ^ (dist_a > dist_b));

				bridge_loops(me,
							 active_branch->e_start_arr[a_fork_off_inv],
							 comp_e_start_flip,
							 verts_per_loop - 2,
							 (dist_a > dist_b),
							 active_branch->e_start_arr[a_fork_off_inv + 1],
							 comp->e_start_arr[b_fork_off_inv + 1],
							 n_g_flip ^ (dist_a > dist_b));

				ED_mesh_loops_add(me, NULL, 8);
				ED_mesh_polys_add(me, NULL, 2);
				add_face(me,
						 me->totedge - 1,
						 active_branch->e_flip_side_ends[a_fork_off * 2 + 1],
						 me->totedge - verts_per_loop + 1,
						 comp->e_flip_side_ends[b_fork_off * 2 + ((dist_a > dist_b) ? 0 : 1)],
						 me->totloop - 8,
						 me->totpoly - 2,
						 n_g_flip ^ (dist_a > dist_b));

				add_face(me,
						 me->totedge - verts_per_loop + 2,
						 active_branch->e_flip_side_ends[a_fork_off * 2],
						 me->totedge - verts_per_loop * 2 + 2,
						 comp->e_flip_side_ends[b_fork_off * 2 + ((dist_a > dist_b) ? 1 : 0)],
						 me->totloop - 4,
						 me->totpoly - 1,
						 !n_g_flip ^ (dist_a > dist_b));
			}
			bridge_all_parts_rec(me, spine, comp, active_branch, verts_per_loop, n_g_flip);
		}
		if (comp) {
			a_fork_off ++;
		}
	}
}

static void bridge_all_parts(Mesh *me, Spine *spine, int verts_per_loop, bool n_g_flip)
{
	SpineBranch *active_branch = NULL;
	for (int i = 0; i < spine->totbranches; i++) {
		if (!active_branch) {
			active_branch = spine->branches[i];
		} else {
			break;
		}
	}
	if (!active_branch) {
		/* No Branches in the spine. Should not happen. */
		return;
	}
	bridge_all_parts_rec(me, spine, active_branch, NULL, verts_per_loop, n_g_flip);
}

static bool calc_stroke_normal_ori(SilhouetteStroke *stroke, float z_vec[3]) {
	float n[3];
	/*TODO: stroke points to multidimensional array*/
	cross_poly_v3(n, (float (*)[3])stroke->points, stroke->totvert);
	return dot_v3v3(n, z_vec) <= 0;
}

/* Generates a 3D shape from a stroke. */
static void silhouette_create_shape_mesh(bContext *C, Mesh *me, SilhouetteData *sil, SilhouetteStroke *stroke)
{
	float z_vec[3] = {0.0f,0.0f,1.0f};
	float inv_z_vec[3];
	float depth = sil->depth;
	int ss_level = sil->resolution;
	int v_steps = (1 << ss_level) + 2;
	bool n_ori = false;
	/* TODO: RNA Init*/

	copy_v3_v3(z_vec, sil->z_vec);
	normalize_v3(z_vec);

	n_ori = calc_stroke_normal_ori(stroke, z_vec);

	/* Generate spine */
	Spine *spine = silhouette_generate_spine(sil, stroke);
	SpineBranch *a_branch;

	mul_v3_fl(z_vec, depth);
	mul_v3_v3fl(inv_z_vec, z_vec, -1.0f);

	int w_steps = v_steps / 2 + 2;
	float smoothness = sil->smoothness;

	for (int i = 0; i < spine->totbranches; i++) {
		a_branch = spine->branches[i];
		if (a_branch) {
			int r_forks = r_branch_count(spine, a_branch);
			switch (r_forks) {
				case 1:
					add_ss_cap(sil, a_branch, me, z_vec, depth, v_steps, w_steps, smoothness, n_ori, false);
					add_ss_cap(sil, a_branch, me, inv_z_vec, depth, v_steps, w_steps, smoothness, !n_ori, true);
#ifdef DEBUG_DRAW
					debug_branch(a_branch, 0x00ff00);
#endif
					break;
				case 2:
					add_ss_tube(sil, a_branch, me, z_vec, depth, v_steps, w_steps, smoothness, n_ori, false);
					add_ss_tube(sil, a_branch, me, inv_z_vec, depth, v_steps, w_steps, smoothness, !n_ori, true);
#ifdef DEBUG_DRAW
					debug_branch(a_branch, 0xff0000);
#endif
					break;
				case 3:
					add_ss_tinter(sil, spine, a_branch, me, z_vec, depth, v_steps, w_steps, smoothness, n_ori, false);
					add_ss_tinter(sil, spine, a_branch, me, inv_z_vec, depth, v_steps, w_steps, smoothness, !n_ori, true);
#ifdef DEBUG_DRAW
					debug_branch(a_branch, 0x0000ff);
#endif
					break;
			}
		}
	}

	bridge_all_parts(me, spine, v_steps * 2 + w_steps, n_ori);

	free_spine(spine);

	ED_mesh_update(me, C, 1, 1);
}

/* Adds additional points to the stroke if start and end are far apart. */
static void stroke_smooth_cap(SilhouetteStroke *stroke, float max_dist)
{
	float v1[3], v2[3], dv[3];
	float length = 0;
	copy_v3_v3(v1, &stroke->points[0]);
	copy_v3_v3(v2, &stroke->points[stroke->totvert * 3 - 3]);

	sub_v3_v3v3(dv,v1,v2);
	length = len_v3(dv);

	if (length > max_dist) {
		int steps = floorf(length / (float)max_dist);
		mul_v3_fl(dv, 1.0f / (float)steps);
		for (int i = 1; i < steps; i++) {
			mul_v3_v3fl(v1, dv, i);
			add_v3_v3(v1, v2);
			silhouette_stroke_add_3Dpoint(stroke, v1);
		}
	}
}

static void sculpt_silhouette_calc_mesh(bContext *C, wmOperator *op)
{
	/*finalize stroke*/
	Object *ob = CTX_data_active_object(C);
	SilhouetteData *sil = op->customdata;
	Mesh *me = ob->data;

	SilhouetteStroke *stroke = sil->current_stroke;

	stroke_smooth_cap(stroke, 0.3f);

	silhouette_create_shape_mesh(C, me, sil, stroke);

	/* Rebuild mesh caches
	 * TODO: Proper PBVH etc. */
	BKE_object_free_derived_caches(ob);
}

static void sculpt_silhouette_stroke_done(bContext *UNUSED(C), wmOperator *op)
{
#ifdef DEBUG_DRAW
	SilhouetteData *sil = op->customdata;

	for (int i = 1; i < sil->current_stroke->totvert; i ++) {
		float v1[3], v2[3];
		copy_v3_v3(v1, &sil->current_stroke->points[i * 3 - 3]);
		copy_v3_v3(v2, &sil->current_stroke->points[i * 3]);
		/*bl_debug_draw_edge_add(v1,v2);*/
	}
#endif
	
	/*cleanup*/
	silhouette_data_free(op);
	op->customdata = NULL;
}

/* Stop drawing the UI*/
static void sculpt_silhouette_clean_draw(bContext *C, wmOperator *op)
{
	SilhouetteData *sil = op->customdata;
	WM_cursor_modal_restore(CTX_wm_window(C));
	ED_region_draw_cb_exit(sil->ar->type,sil->draw_handle);
	ED_region_tag_redraw(sil->ar);
}

static int sculpt_silhouette_exec(bContext *C, wmOperator *op)
{
	int v_start, e_start, l_start, p_start;
	SilhouetteData *sil = op->customdata;
	Object *ob = CTX_data_active_object(C);
	Mesh *me = ob->data;

	if (!sil) {
		sil = silhouette_data_new(C);
		silhouette_set_ref_plane(sil);
		op->customdata = sil;
	}

	if (sil->current_stroke->totvert > 3) {
		sculpt_undo_push_begin("draw Silhouette");
		v_start = me->totvert;
		e_start = me->totedge;
		l_start = me->totloop;
		p_start = me->totpoly;
		sculpt_silhouette_calc_mesh(C, op);
		sculpt_silhouette_stroke_done(C, op);

		sculpt_undo_silhouette_push(ob, v_start, e_start, l_start, p_start);
		sculpt_undo_push_end(C);
	}

	return OPERATOR_FINISHED;
}

static int sculpt_silhouette_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
	float mouse[2];
	SilhouetteData *sil = op->customdata;
	copy_v2_fl2(mouse, event->mval[0], event->mval[1]);
	printf(".");
	if (event->val == KM_RELEASE) {
		sculpt_silhouette_clean_draw(C, op);
		if (sil->state == SIL_DRAWING) {
			silhouette_set_ref_plane(sil);
			return sculpt_silhouette_exec(C, op);
		}
		return OPERATOR_FINISHED;
	} else {
		if (sil->state == SIL_DRAWING) {
			sculpt_silhouette_stroke_update(mouse, op->customdata);
		}
		return OPERATOR_RUNNING_MODAL;
	}
}

/* UI Indicator for the tool. TODO: beautify */
static void sculpt_silhouette_draw(const bContext *UNUSED(C),struct ARegion *UNUSED(ar), void *arg)
{
	SilhouetteData *sil = arg;

	if (!sil) {
		return;
	}
	const SilhouetteStroke *stroke = sil->current_stroke;
	if (!stroke) {
		return;
	}

	glLineWidth(1.0f);
	glEnable(GL_BLEND);
	glEnable(GL_LINE_SMOOTH);

	/* set brush color */
	glColor4f(sil->add_col[0],sil->add_col[1],sil->add_col[2], 0.2f);

	if (stroke->points) {
		glBegin(GL_POLYGON);
		for (int i = 0; i < stroke->totvert; i++) {
			glVertex3f(stroke->points[3 * i], stroke->points[3 * i + 1], stroke->points[3 * i + 2]);
		}
		glEnd();
	}

	/* restore GL state */
	glDisable(GL_BLEND);
	glDisable(GL_LINE_SMOOTH);
}

static int sculpt_silhouette_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	printf("Drawing Silhouette\n");

	SilhouetteData *sil_data;

	sil_data = silhouette_data_new(C);

	op->customdata = sil_data;

	/* Tag for UI to be drawn */
	ED_region_tag_redraw(sil_data->ar);

	sil_data->draw_handle = ED_region_draw_cb_activate(sil_data->ar->type, sculpt_silhouette_draw, op->customdata, REGION_DRAW_PRE_VIEW);
	sil_data->state = SIL_DRAWING;

	/* add modal handler */
	WM_event_add_modal_handler(C, op);

	return OPERATOR_RUNNING_MODAL;
}

static int sculpt_silhouette_poll(bContext *UNUSED(C))
{
	/* TODO; */
	return 1;
}

static void SCULPT_OT_silhouette_draw(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Draw Silhouette";
	ot->idname = "SCULPT_OT_silhouette_draw";
	ot->description = "Draw a new silhoutte for the sculpt";

	/* api callbacks */
	ot->invoke = sculpt_silhouette_invoke;
	ot->modal = sculpt_silhouette_modal;
	ot->exec = sculpt_silhouette_exec;
	ot->poll = sculpt_silhouette_poll;
	ot->cancel = sculpt_silhouette_stroke_done;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
/* end Silhouette */

/************************** Dynamic Topology **************************/

static void sculpt_dynamic_topology_triangulate(BMesh *bm)
{
	if (bm->totloop != bm->totface * 3) {
		BM_mesh_triangulate(bm, MOD_TRIANGULATE_QUAD_BEAUTY, MOD_TRIANGULATE_NGON_EARCLIP, false, NULL, NULL, NULL);
	}
}

void sculpt_pbvh_clear(Object *ob)
{
	SculptSession *ss = ob->sculpt;
	DerivedMesh *dm = ob->derivedFinal;

	/* Clear out any existing DM and PBVH */
	if (ss->pbvh)
		BKE_pbvh_free(ss->pbvh);
	ss->pbvh = NULL;
	if (dm)
		dm->getPBVH(NULL, dm);
	BKE_object_free_derived_caches(ob);
}

void sculpt_dyntopo_node_layers_add(SculptSession *ss)
{
	int cd_node_layer_index;

	char layer_id[] = "_dyntopo_node_id";

	cd_node_layer_index = CustomData_get_named_layer_index(&ss->bm->vdata, CD_PROP_INT, layer_id);
	if (cd_node_layer_index == -1) {
		BM_data_layer_add_named(ss->bm, &ss->bm->vdata, CD_PROP_INT, layer_id);
		cd_node_layer_index = CustomData_get_named_layer_index(&ss->bm->vdata, CD_PROP_INT, layer_id);
	}

	ss->cd_vert_node_offset = CustomData_get_n_offset(
	        &ss->bm->vdata, CD_PROP_INT,
	        cd_node_layer_index - CustomData_get_layer_index(&ss->bm->vdata, CD_PROP_INT));

	ss->bm->vdata.layers[cd_node_layer_index].flag |= CD_FLAG_TEMPORARY;

	cd_node_layer_index = CustomData_get_named_layer_index(&ss->bm->pdata, CD_PROP_INT, layer_id);
	if (cd_node_layer_index == -1) {
		BM_data_layer_add_named(ss->bm, &ss->bm->pdata, CD_PROP_INT, layer_id);
		cd_node_layer_index = CustomData_get_named_layer_index(&ss->bm->pdata, CD_PROP_INT, layer_id);
	}

	ss->cd_face_node_offset = CustomData_get_n_offset(
	        &ss->bm->pdata, CD_PROP_INT,
	        cd_node_layer_index - CustomData_get_layer_index(&ss->bm->pdata, CD_PROP_INT));

	ss->bm->pdata.layers[cd_node_layer_index].flag |= CD_FLAG_TEMPORARY;
}


void sculpt_update_after_dynamic_topology_toggle(bContext *C)
{
	Scene *scene = CTX_data_scene(C);
	Object *ob = CTX_data_active_object(C);
	Sculpt *sd = scene->toolsettings->sculpt;

	/* Create the PBVH */
	BKE_sculpt_update_mesh_elements(scene, sd, ob, false, false);
	WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, ob);
}

void sculpt_dynamic_topology_enable(bContext *C)
{
	Scene *scene = CTX_data_scene(C);
	Object *ob = CTX_data_active_object(C);
	SculptSession *ss = ob->sculpt;
	Mesh *me = ob->data;
	const BMAllocTemplate allocsize = BMALLOC_TEMPLATE_FROM_ME(me);

	sculpt_pbvh_clear(ob);

	ss->bm_smooth_shading = (scene->toolsettings->sculpt->flags & SCULPT_DYNTOPO_SMOOTH_SHADING) != 0;

	/* Dynamic topology doesn't ensure selection state is valid, so remove [#36280] */
	BKE_mesh_mselect_clear(me);

	/* Create triangles-only BMesh */
	ss->bm = BM_mesh_create(
	        &allocsize,
	        &((struct BMeshCreateParams){.use_toolflags = false,}));

	BM_mesh_bm_from_me(
	        ss->bm, me, (&(struct BMeshFromMeshParams){
	            .calc_face_normal = true, .use_shapekey = true, .active_shapekey = ob->shapenr,
	        }));
	sculpt_dynamic_topology_triangulate(ss->bm);
	BM_data_layer_add(ss->bm, &ss->bm->vdata, CD_PAINT_MASK);
	sculpt_dyntopo_node_layers_add(ss);
	/* make sure the data for existing faces are initialized */
	if (me->totpoly != ss->bm->totface) {
		BM_mesh_normals_update(ss->bm);
	}

	/* Enable dynamic topology */
	me->flag |= ME_SCULPT_DYNAMIC_TOPOLOGY;
	
	/* Enable logging for undo/redo */
	ss->bm_log = BM_log_create(ss->bm);

	/* Refresh */
	sculpt_update_after_dynamic_topology_toggle(C);
}

/* Free the sculpt BMesh and BMLog
 *
 * If 'unode' is given, the BMesh's data is copied out to the unode
 * before the BMesh is deleted so that it can be restored from */
void sculpt_dynamic_topology_disable(bContext *C,
                                     SculptUndoNode *unode)
{
	Object *ob = CTX_data_active_object(C);
	SculptSession *ss = ob->sculpt;
	Mesh *me = ob->data;

	sculpt_pbvh_clear(ob);

	if (unode) {
		/* Free all existing custom data */
		CustomData_free(&me->vdata, me->totvert);
		CustomData_free(&me->edata, me->totedge);
		CustomData_free(&me->fdata, me->totface);
		CustomData_free(&me->ldata, me->totloop);
		CustomData_free(&me->pdata, me->totpoly);

		/* Copy over stored custom data */
		me->totvert = unode->bm_enter_totvert;
		me->totloop = unode->bm_enter_totloop;
		me->totpoly = unode->bm_enter_totpoly;
		me->totedge = unode->bm_enter_totedge;
		me->totface = 0;
		CustomData_copy(&unode->bm_enter_vdata, &me->vdata, CD_MASK_MESH,
		                CD_DUPLICATE, unode->bm_enter_totvert);
		CustomData_copy(&unode->bm_enter_edata, &me->edata, CD_MASK_MESH,
		                CD_DUPLICATE, unode->bm_enter_totedge);
		CustomData_copy(&unode->bm_enter_ldata, &me->ldata, CD_MASK_MESH,
		                CD_DUPLICATE, unode->bm_enter_totloop);
		CustomData_copy(&unode->bm_enter_pdata, &me->pdata, CD_MASK_MESH,
		                CD_DUPLICATE, unode->bm_enter_totpoly);

		BKE_mesh_update_customdata_pointers(me, false);
	}
	else {
		BKE_sculptsession_bm_to_me(ob, true);
	}

	/* Clear data */
	me->flag &= ~ME_SCULPT_DYNAMIC_TOPOLOGY;

	/* typically valid but with global-undo they can be NULL, [#36234] */
	if (ss->bm) {
		BM_mesh_free(ss->bm);
		ss->bm = NULL;
	}
	if (ss->bm_log) {
		BM_log_free(ss->bm_log);
		ss->bm_log = NULL;
	}

	/* Refresh */
	sculpt_update_after_dynamic_topology_toggle(C);
}


static int sculpt_dynamic_topology_toggle_exec(bContext *C, wmOperator *UNUSED(op))
{
	Object *ob = CTX_data_active_object(C);
	SculptSession *ss = ob->sculpt;

	WM_cursor_wait(1);

	if (ss->bm) {
		sculpt_undo_push_begin("Dynamic topology disable");
		sculpt_undo_push_node(ob, NULL, SCULPT_UNDO_DYNTOPO_END);
		sculpt_dynamic_topology_disable(C, NULL);
	}
	else {
		sculpt_undo_push_begin("Dynamic topology enable");
		sculpt_dynamic_topology_enable(C);
		sculpt_undo_push_node(ob, NULL, SCULPT_UNDO_DYNTOPO_BEGIN);
	}
	sculpt_undo_push_end(C);

	WM_cursor_wait(0);

	return OPERATOR_FINISHED;
}

enum eDynTopoWarnFlag {
	DYNTOPO_WARN_VDATA = (1 << 0),
	DYNTOPO_WARN_EDATA = (1 << 1),
	DYNTOPO_WARN_LDATA = (1 << 2),
	DYNTOPO_WARN_MODIFIER = (1 << 3),
};

static int dyntopo_warning_popup(bContext *C, wmOperatorType *ot, enum eDynTopoWarnFlag flag)
{
	uiPopupMenu *pup = UI_popup_menu_begin(C, IFACE_("Warning!"), ICON_ERROR);
	uiLayout *layout = UI_popup_menu_layout(pup);

	if (flag & (DYNTOPO_WARN_VDATA | DYNTOPO_WARN_EDATA | DYNTOPO_WARN_LDATA)) {
		const char *msg_error = TIP_("Vertex Data Detected!");
		const char *msg = TIP_("Dyntopo will not preserve vertex colors, UVs, or other customdata");
		uiItemL(layout, msg_error, ICON_INFO);
		uiItemL(layout, msg, ICON_NONE);
		uiItemS(layout);
	}

	if (flag & DYNTOPO_WARN_MODIFIER) {
		const char *msg_error = TIP_("Generative Modifiers Detected!");
		const char *msg = TIP_("Keeping the modifiers will increase polycount when returning to object mode");

		uiItemL(layout, msg_error, ICON_INFO);
		uiItemL(layout, msg, ICON_NONE);
		uiItemS(layout);
	}

	uiItemFullO_ptr(layout, ot, IFACE_("OK"), ICON_NONE, NULL, WM_OP_EXEC_DEFAULT, 0);

	UI_popup_menu_end(C, pup);

	return OPERATOR_INTERFACE;
}

static enum eDynTopoWarnFlag sculpt_dynamic_topology_check(bContext *C)
{
	Object *ob = CTX_data_active_object(C);
	Mesh *me = ob->data;
	SculptSession *ss = ob->sculpt;

	Scene *scene = CTX_data_scene(C);
	enum eDynTopoWarnFlag flag = 0;

	BLI_assert(ss->bm == NULL);
	UNUSED_VARS_NDEBUG(ss);

	for (int i = 0; i < CD_NUMTYPES; i++) {
		if (!ELEM(i, CD_MVERT, CD_MEDGE, CD_MFACE, CD_MLOOP, CD_MPOLY, CD_PAINT_MASK, CD_ORIGINDEX)) {
			if (CustomData_has_layer(&me->vdata, i)) {
				flag |= DYNTOPO_WARN_VDATA;
			}
			if (CustomData_has_layer(&me->edata, i)) {
				flag |= DYNTOPO_WARN_EDATA;
			}
			if (CustomData_has_layer(&me->ldata, i)) {
				flag |= DYNTOPO_WARN_LDATA;
			}
		}
	}

	{
		VirtualModifierData virtualModifierData;
		ModifierData *md = modifiers_getVirtualModifierList(ob, &virtualModifierData);

		/* exception for shape keys because we can edit those */
		for (; md; md = md->next) {
			const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
			if (!modifier_isEnabled(scene, md, eModifierMode_Realtime)) continue;

			if (mti->type == eModifierTypeType_Constructive) {
				flag |= DYNTOPO_WARN_MODIFIER;
				break;
			}
		}
	}

	return flag;
}

static int sculpt_dynamic_topology_toggle_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	Object *ob = CTX_data_active_object(C);
	SculptSession *ss = ob->sculpt;

	if (!ss->bm) {
		enum eDynTopoWarnFlag flag = sculpt_dynamic_topology_check(C);

		if (flag) {
			/* The mesh has customdata that will be lost, let the user confirm this is OK */
			return dyntopo_warning_popup(C, op->type, flag);
		}
	}

	return sculpt_dynamic_topology_toggle_exec(C, op);
}

static void SCULPT_OT_dynamic_topology_toggle(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Dynamic Topology Toggle";
	ot->idname = "SCULPT_OT_dynamic_topology_toggle";
	ot->description = "Dynamic topology alters the mesh topology while sculpting";
	
	/* api callbacks */
	ot->invoke = sculpt_dynamic_topology_toggle_invoke;
	ot->exec = sculpt_dynamic_topology_toggle_exec;
	ot->poll = sculpt_mode_poll;
	
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/************************* SCULPT_OT_optimize *************************/

static int sculpt_optimize_exec(bContext *C, wmOperator *UNUSED(op))
{
	Object *ob = CTX_data_active_object(C);

	sculpt_pbvh_clear(ob);
	WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, ob);

	return OPERATOR_FINISHED;
}

static int sculpt_and_dynamic_topology_poll(bContext *C)
{
	Object *ob = CTX_data_active_object(C);

	return sculpt_mode_poll(C) && ob->sculpt->bm;
}

/* The BVH gets less optimal more quickly with dynamic topology than
 * regular sculpting. There is no doubt more clever stuff we can do to
 * optimize it on the fly, but for now this gives the user a nicer way
 * to recalculate it than toggling modes. */
static void SCULPT_OT_optimize(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Optimize";
	ot->idname = "SCULPT_OT_optimize";
	ot->description = "Recalculate the sculpt BVH to improve performance";
	
	/* api callbacks */
	ot->exec = sculpt_optimize_exec;
	ot->poll = sculpt_and_dynamic_topology_poll;
	
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/********************* Dynamic topology symmetrize ********************/

static int sculpt_symmetrize_exec(bContext *C, wmOperator *UNUSED(op))
{
	Object *ob = CTX_data_active_object(C);
	const Sculpt *sd = CTX_data_tool_settings(C)->sculpt;
	SculptSession *ss = ob->sculpt;

	/* To simplify undo for symmetrize, all BMesh elements are logged
	 * as deleted, then after symmetrize operation all BMesh elements
	 * are logged as added (as opposed to attempting to store just the
	 * parts that symmetrize modifies) */
	sculpt_undo_push_begin("Dynamic topology symmetrize");
	sculpt_undo_push_node(ob, NULL, SCULPT_UNDO_DYNTOPO_SYMMETRIZE);
	BM_log_before_all_removed(ss->bm, ss->bm_log);

	BM_mesh_toolflags_set(ss->bm, true);

	/* Symmetrize and re-triangulate */
	BMO_op_callf(ss->bm, BMO_FLAG_DEFAULTS,
	             "symmetrize input=%avef direction=%i  dist=%f",
	             sd->symmetrize_direction, 0.00001f);
	sculpt_dynamic_topology_triangulate(ss->bm);

	/* bisect operator flags edges (keep tags clean for edge queue) */
	BM_mesh_elem_hflag_disable_all(ss->bm, BM_EDGE, BM_ELEM_TAG, false);

	BM_mesh_toolflags_set(ss->bm, false);

	/* Finish undo */
	BM_log_all_added(ss->bm, ss->bm_log);
	sculpt_undo_push_end(C);

	/* Redraw */
	sculpt_pbvh_clear(ob);
	WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, ob);

	return OPERATOR_FINISHED;
}

static void SCULPT_OT_symmetrize(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Symmetrize";
	ot->idname = "SCULPT_OT_symmetrize";
	ot->description = "Symmetrize the topology modifications";
	
	/* api callbacks */
	ot->exec = sculpt_symmetrize_exec;
	ot->poll = sculpt_and_dynamic_topology_poll;
}

/**** Toggle operator for turning sculpt mode on or off ****/

static void sculpt_init_session(Scene *scene, Object *ob)
{
	ob->sculpt = MEM_callocN(sizeof(SculptSession), "sculpt session");

	BKE_sculpt_update_mesh_elements(scene, scene->toolsettings->sculpt, ob, 0, false);
}


static int sculpt_mode_toggle_exec(bContext *C, wmOperator *op)
{
	Scene *scene = CTX_data_scene(C);
	ToolSettings *ts = CTX_data_tool_settings(C);
	Object *ob = CTX_data_active_object(C);
	const int mode_flag = OB_MODE_SCULPT;
	const bool is_mode_set = (ob->mode & mode_flag) != 0;
	Mesh *me;
	MultiresModifierData *mmd = BKE_sculpt_multires_active(scene, ob);
	int flush_recalc = 0;

	if (!is_mode_set) {
		if (!ED_object_mode_compat_set(C, ob, mode_flag, op->reports)) {
			return OPERATOR_CANCELLED;
		}
	}

	me = BKE_mesh_from_object(ob);

	/* multires in sculpt mode could have different from object mode subdivision level */
	flush_recalc |= mmd && mmd->sculptlvl != mmd->lvl;
	/* if object has got active modifiers, it's dm could be different in sculpt mode  */
	flush_recalc |= sculpt_has_active_modifiers(scene, ob);

	if (is_mode_set) {
		if (mmd)
			multires_force_update(ob);

		/* Always for now, so leaving sculpt mode always ensures scene is in
		 * a consistent state.
		 */
		if (true || flush_recalc || (ob->sculpt && ob->sculpt->bm)) {
			DAG_id_tag_update(&ob->id, OB_RECALC_DATA);
		}

		if (me->flag & ME_SCULPT_DYNAMIC_TOPOLOGY) {
			/* Dynamic topology must be disabled before exiting sculpt
			 * mode to ensure the undo stack stays in a consistent
			 * state */
			sculpt_dynamic_topology_toggle_exec(C, NULL);

			/* store so we know to re-enable when entering sculpt mode */
			me->flag |= ME_SCULPT_DYNAMIC_TOPOLOGY;
		}

		/* Leave sculptmode */
		ob->mode &= ~mode_flag;

		BKE_sculptsession_free(ob);

		paint_cursor_delete_textures();
	}
	else {
		/* Enter sculptmode */
		ob->mode |= mode_flag;

		if (flush_recalc)
			DAG_id_tag_update(&ob->id, OB_RECALC_DATA);

		/* Create persistent sculpt mode data */
		if (!ts->sculpt) {
			ts->sculpt = MEM_callocN(sizeof(Sculpt), "sculpt mode data");

			/* Turn on X plane mirror symmetry by default */
			ts->sculpt->paint.symmetry_flags |= PAINT_SYMM_X;
			ts->sculpt->paint.flags |= PAINT_SHOW_BRUSH;

			/* Make sure at least dyntopo subdivision is enabled */
			ts->sculpt->flags |= SCULPT_DYNTOPO_SUBDIVIDE | SCULPT_DYNTOPO_COLLAPSE;
		}

		if (!ts->sculpt->detail_size)
			ts->sculpt->detail_size = 12;
		if (!ts->sculpt->detail_percent)
			ts->sculpt->detail_percent = 25;
		if (ts->sculpt->constant_detail == 0.0f)
			ts->sculpt->constant_detail = 3.0f;

		/* Set sane default tiling offsets */
		if (!ts->sculpt->paint.tile_offset[0]) ts->sculpt->paint.tile_offset[0] = 1.0f;
		if (!ts->sculpt->paint.tile_offset[1]) ts->sculpt->paint.tile_offset[1] = 1.0f;
		if (!ts->sculpt->paint.tile_offset[2]) ts->sculpt->paint.tile_offset[2] = 1.0f;


		/* Create sculpt mode session data */
		if (ob->sculpt)
			BKE_sculptsession_free(ob);

		sculpt_init_session(scene, ob);

		/* Mask layer is required */
		if (mmd) {
			/* XXX, we could attempt to support adding mask data mid-sculpt mode (with multi-res)
			 * but this ends up being quite tricky (and slow) */
			BKE_sculpt_mask_layers_ensure(ob, mmd);
		}

		if (!(fabsf(ob->size[0] - ob->size[1]) < 1e-4f && fabsf(ob->size[1] - ob->size[2]) < 1e-4f)) {
			BKE_report(op->reports, RPT_WARNING,
			           "Object has non-uniform scale, sculpting may be unpredictable");
		}
		else if (is_negative_m4(ob->obmat)) {
			BKE_report(op->reports, RPT_WARNING,
			           "Object has negative scale, sculpting may be unpredictable");
		}

		BKE_paint_init(scene, ePaintSculpt, PAINT_CURSOR_SCULPT);

		paint_cursor_start(C, sculpt_poll_view3d);

		/* Check dynamic-topology flag; re-enter dynamic-topology mode when changing modes,
		 * As long as no data was added that is not supported. */
		if (me->flag & ME_SCULPT_DYNAMIC_TOPOLOGY) {
			const char *message_unsupported = NULL;
			if (me->totloop != me->totpoly * 3) {
				message_unsupported = TIP_("non-triangle face");
			}
			else if (mmd != NULL) {
				message_unsupported = TIP_("multi-res modifier");
			}
			else {
				enum eDynTopoWarnFlag flag = sculpt_dynamic_topology_check(C);
				if (flag == 0) {
					/* pass */
				}
				else if (flag & DYNTOPO_WARN_VDATA) {
					message_unsupported = TIP_("vertex data");
				}
				else if (flag & DYNTOPO_WARN_EDATA) {
					message_unsupported = TIP_("edge data");
				}
				else if (flag & DYNTOPO_WARN_LDATA) {
					message_unsupported = TIP_("face data");
				}
				else if (flag & DYNTOPO_WARN_MODIFIER) {
					message_unsupported = TIP_("constructive modifier");
				}
				else {
					BLI_assert(0);
				}
			}

			if (message_unsupported == NULL) {
				/* undo push is needed to prevent memory leak */
				sculpt_undo_push_begin("Dynamic topology enable");
				sculpt_dynamic_topology_enable(C);
				sculpt_undo_push_node(ob, NULL, SCULPT_UNDO_DYNTOPO_BEGIN);
			}
			else {
				BKE_reportf(op->reports, RPT_WARNING,
				            "Dynamic Topology found: %s, disabled",
				            message_unsupported);
				me->flag &= ~ME_SCULPT_DYNAMIC_TOPOLOGY;
			}
		}
	}

	if (ob->derivedFinal) /* VBO no longer valid */
		GPU_drawobject_free(ob->derivedFinal);

	WM_event_add_notifier(C, NC_SCENE | ND_MODE, scene);

	return OPERATOR_FINISHED;
}

static void SCULPT_OT_sculptmode_toggle(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Sculpt Mode";
	ot->idname = "SCULPT_OT_sculptmode_toggle";
	ot->description = "Toggle sculpt mode in 3D view";
	
	/* api callbacks */
	ot->exec = sculpt_mode_toggle_exec;
	ot->poll = ED_operator_object_active_editable_mesh;
	
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}


static int sculpt_and_dynamic_topology_constant_detail_poll(bContext *C)
{
	Object *ob = CTX_data_active_object(C);
	Sculpt *sd = CTX_data_tool_settings(C)->sculpt;

	return sculpt_mode_poll(C) && ob->sculpt->bm && (sd->flags & SCULPT_DYNTOPO_DETAIL_CONSTANT);
}

static int sculpt_detail_flood_fill_exec(bContext *C, wmOperator *UNUSED(op))
{
	Sculpt *sd = CTX_data_tool_settings(C)->sculpt;
	Object *ob = CTX_data_active_object(C);
	SculptSession *ss = ob->sculpt;
	float size;
	float bb_min[3], bb_max[3];
	int i, totnodes;
	PBVHNode **nodes;

	BKE_pbvh_search_gather(ss->pbvh, NULL, NULL, &nodes, &totnodes);

	if (!totnodes)
		return OPERATOR_CANCELLED;

	for (i = 0; i < totnodes; i++) {
		BKE_pbvh_node_mark_topology_update(nodes[i]);
	}
	/* get the bounding box, store the size to bb_max and center (zero) to bb_min */
	BKE_pbvh_bounding_box(ob->sculpt->pbvh, bb_min, bb_max);
	sub_v3_v3(bb_max, bb_min);
	zero_v3(bb_min);
	size = max_fff(bb_max[0], bb_max[1], bb_max[2]);

	/* update topology size */
	BKE_pbvh_bmesh_detail_size_set(ss->pbvh, 1.0f / sd->constant_detail);

	sculpt_undo_push_begin("Dynamic topology flood fill");
	sculpt_undo_push_node(ob, NULL, SCULPT_UNDO_COORDS);

	while (BKE_pbvh_bmesh_update_topology(
	               ss->pbvh, PBVH_Collapse | PBVH_Subdivide,
	               bb_min, NULL, size))
	{
		for (i = 0; i < totnodes; i++)
			BKE_pbvh_node_mark_topology_update(nodes[i]);
	}

	MEM_freeN(nodes);
	sculpt_undo_push_end(C);

	/* force rebuild of pbvh for better BB placement */
	sculpt_pbvh_clear(ob);
	/* Redraw */
	WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, ob);

	return OPERATOR_FINISHED;
}

static void SCULPT_OT_detail_flood_fill(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Detail Flood Fill";
	ot->idname = "SCULPT_OT_detail_flood_fill";
	ot->description = "Flood fill the mesh with the selected detail setting";

	/* api callbacks */
	ot->exec = sculpt_detail_flood_fill_exec;
	ot->poll = sculpt_and_dynamic_topology_constant_detail_poll;

	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static void sample_detail(bContext *C, int ss_co[2])
{
	ViewContext vc;
	Object *ob;
	Sculpt *sd;
	float ray_start[3], ray_end[3], ray_normal[3], dist;
	SculptDetailRaycastData srd;
	float mouse[2] = {ss_co[0], ss_co[1]};
	view3d_set_viewcontext(C, &vc);

	sd = CTX_data_tool_settings(C)->sculpt;
	ob = vc.obact;

	sculpt_stroke_modifiers_check(C, ob);

	dist = sculpt_raycast_init(&vc, mouse, ray_start, ray_end, ray_normal, false);

	srd.hit = 0;
	srd.ray_start = ray_start;
	srd.ray_normal = ray_normal;
	srd.dist = dist;
	srd.detail = sd->constant_detail;

	BKE_pbvh_raycast(ob->sculpt->pbvh, sculpt_raycast_detail_cb, &srd,
	                 ray_start, ray_normal, false);

	if (srd.hit) {
		/* convert edge length to detail resolution */
		sd->constant_detail = 1.0f / srd.detail;
	}
}

static int sculpt_sample_detail_size_exec(bContext *C, wmOperator *op)
{
	int ss_co[2];
	RNA_int_get_array(op->ptr, "location", ss_co);
	sample_detail(C, ss_co);
	return OPERATOR_FINISHED;
}


static int sculpt_sample_detail_size_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(e))
{
	ScrArea *sa = CTX_wm_area(C);
	ED_area_headerprint(sa, "Click on the mesh to set the detail");
	WM_cursor_modal_set(CTX_wm_window(C), BC_EYEDROPPER_CURSOR);
	WM_event_add_modal_handler(C, op);
	return OPERATOR_RUNNING_MODAL;
}

static int sculpt_sample_detail_size_modal(bContext *C, wmOperator *op, const wmEvent *e)
{
	switch (e->type) {
		case LEFTMOUSE:
			if (e->val == KM_PRESS) {
				ScrArea *sa = CTX_wm_area(C);
				int ss_co[2] = {e->mval[0], e->mval[1]};

				sample_detail(C, ss_co);

				RNA_int_set_array(op->ptr, "location", ss_co);
				WM_cursor_modal_restore(CTX_wm_window(C));
				ED_area_headerprint(sa, NULL);
				WM_main_add_notifier(NC_SCENE | ND_TOOLSETTINGS, NULL);

				return OPERATOR_FINISHED;
			}
			break;

		case RIGHTMOUSE:
		{
			ScrArea *sa = CTX_wm_area(C);
			WM_cursor_modal_restore(CTX_wm_window(C));
			ED_area_headerprint(sa, NULL);

			return OPERATOR_CANCELLED;
		}
	}

	return OPERATOR_RUNNING_MODAL;
}


static void SCULPT_OT_sample_detail_size(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Sample Detail Size";
	ot->idname = "SCULPT_OT_sample_detail_size";
	ot->description = "Sample the mesh detail on clicked point";

	/* api callbacks */
	ot->invoke = sculpt_sample_detail_size_invoke;
	ot->exec = sculpt_sample_detail_size_exec;
	ot->modal = sculpt_sample_detail_size_modal;
	ot->poll = sculpt_and_dynamic_topology_constant_detail_poll;

	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	RNA_def_int_array(ot->srna, "location", 2, NULL, 0, SHRT_MAX,
	                  "Location", "Screen Coordinates of sampling", 0, SHRT_MAX);
}


static int sculpt_set_detail_size_exec(bContext *C, wmOperator *UNUSED(op))
{
	Sculpt *sd = CTX_data_tool_settings(C)->sculpt;

	PointerRNA props_ptr;
	wmOperatorType *ot = WM_operatortype_find("WM_OT_radial_control", true);

	WM_operator_properties_create_ptr(&props_ptr, ot);

	if (sd->flags & SCULPT_DYNTOPO_DETAIL_CONSTANT) {
		set_brush_rc_props(&props_ptr, "sculpt", "constant_detail_resolution", NULL, 0);
		RNA_string_set(&props_ptr, "data_path_primary", "tool_settings.sculpt.constant_detail_resolution");
	}
	else if (sd->flags & SCULPT_DYNTOPO_DETAIL_BRUSH) {
		set_brush_rc_props(&props_ptr, "sculpt", "constant_detail_resolution", NULL, 0);
		RNA_string_set(&props_ptr, "data_path_primary", "tool_settings.sculpt.detail_percent");
	}
	else {
		set_brush_rc_props(&props_ptr, "sculpt", "detail_size", NULL, 0);
		RNA_string_set(&props_ptr, "data_path_primary", "tool_settings.sculpt.detail_size");
	}

	WM_operator_name_call_ptr(C, ot, WM_OP_INVOKE_DEFAULT, &props_ptr);

	WM_operator_properties_free(&props_ptr);

	return OPERATOR_FINISHED;
}

static void SCULPT_OT_set_detail_size(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Set Detail Size";
	ot->idname = "SCULPT_OT_set_detail_size";
	ot->description = "Set the mesh detail (either relative or constant one, depending on current dyntopo mode)";

	/* api callbacks */
	ot->exec = sculpt_set_detail_size_exec;
	ot->poll = sculpt_and_dynamic_topology_poll;

	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

void ED_operatortypes_sculpt(void)
{
	WM_operatortype_append(SCULPT_OT_brush_stroke);
	WM_operatortype_append(SCULPT_OT_sculptmode_toggle);
	WM_operatortype_append(SCULPT_OT_set_persistent_base);
	WM_operatortype_append(SCULPT_OT_silhouette_draw);
	WM_operatortype_append(SCULPT_OT_dynamic_topology_toggle);
	WM_operatortype_append(SCULPT_OT_optimize);
	WM_operatortype_append(SCULPT_OT_symmetrize);
	WM_operatortype_append(SCULPT_OT_detail_flood_fill);
	WM_operatortype_append(SCULPT_OT_sample_detail_size);
	WM_operatortype_append(SCULPT_OT_set_detail_size);
}
