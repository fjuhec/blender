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
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Lukas Toenne
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/bmesh/operators/bmo_face_island.c
 *  \ingroup bmesh
 *
 * Face island search.
 */

#include "MEM_guardedalloc.h"

#include "BLI_array.h"
#include "BLI_math.h"

#include "bmesh.h"

#include "intern/bmesh_operators_private.h"


#define FACE_MARK 1

static BMLoop* bmo_face_island_find_start_loop(BMesh *bm, BMOperator *op)
{
	BMFace *f;
	BMOIter oiter;
	BMO_ITER (f, &oiter, op->slots_in, "faces", BM_FACE)
	{
		BMLoop *l;
		BMIter l_iter;
		BM_ITER_ELEM(l, &l_iter, f, BM_LOOPS_OF_FACE)
		{
			BMLoop *lr = l;
			do
			{
				if (!BM_loop_is_manifold(lr)) {
					/* treat non-manifold edges as boundaries */
					return lr;
				}
				if (!BMO_face_flag_test(bm, lr->f, FACE_MARK))
				{
					return lr;
				}
				lr = lr->radial_next;
			}
			while (lr != l);
		}
	}
	return NULL;
}

void bmo_face_island_boundary_exec(BMesh *bm, BMOperator *op)
{
	BMO_slot_buffer_flag_enable(bm, op->slots_in, "faces", BM_FACE, FACE_MARK);
	
	BMLoop *l_start = bmo_face_island_find_start_loop(bm, op);
	if (!l_start)
	{
		return;
	}
	
	BMLoop **boundary = NULL;
	BLI_array_declare(boundary);
	
	BMWalker walker;
	BMW_init(&walker, bm, BMW_ISLANDBOUND,
	         BMW_MASK_NOP, BMW_MASK_NOP, FACE_MARK,
	         BMW_FLAG_NOP, /* no need to check BMW_FLAG_TEST_HIDDEN, faces are already marked by the bmo */
	         BMW_NIL_LAY);
	
	for (BMLoop *l_iter = BMW_begin(&walker, l_start); l_iter; l_iter = BMW_step(&walker))
	{
		BLI_array_append(boundary, l_iter);
	}
	BMW_end(&walker);
	
	{
		BMOpSlot *slot = BMO_slot_get(op->slots_out, "boundary");
		BMO_slot_buffer_from_array(op, slot, (BMHeader **)boundary, BLI_array_count(boundary));
		BLI_array_free(boundary);
	}
	
#if 0
	BMOIter oiter;
	BMFace *f;
	BMFace ***regions = NULL;
	BMFace **faces = NULL;
	BLI_array_declare(regions);
	BLI_array_declare(faces);
	BMFace *act_face = bm->act_face;
	BMWalker regwalker;
	int i;

	const bool use_verts = BMO_slot_bool_get(op->slots_in, "use_verts");

	if (use_verts) {
		/* tag verts that start out with only 2 edges,
		 * don't remove these later */
		BMIter viter;
		BMVert *v;

		BM_ITER_MESH (v, &viter, bm, BM_VERTS_OF_MESH) {
			BMO_vert_flag_set(bm, v, VERT_MARK, !BM_vert_is_edge_pair(v));
		}
	}

	BMO_slot_buffer_flag_enable(bm, op->slots_in, "faces", BM_FACE, FACE_MARK | FACE_TAG);
	
	/* collect region */
	BMO_ITER (f, &oiter, op->slots_in, "faces", BM_FACE) {
		BMFace *f_iter;
		if (!BMO_face_flag_test(bm, f, FACE_TAG)) {
			continue;
		}

		BLI_array_empty(faces);
		faces = NULL; /* forces different allocatio */

		BMW_init(&regwalker, bm, BMW_ISLAND_MANIFOLD,
		         BMW_MASK_NOP, BMW_MASK_NOP, FACE_MARK,
		         BMW_FLAG_NOP, /* no need to check BMW_FLAG_TEST_HIDDEN, faces are already marked by the bmo */
		         BMW_NIL_LAY);

		for (f_iter = BMW_begin(&regwalker, f); f_iter; f_iter = BMW_step(&regwalker)) {
			BLI_array_append(faces, f_iter);
		}
		BMW_end(&regwalker);
		
		for (i = 0; i < BLI_array_count(faces); i++) {
			f_iter = faces[i];
			BMO_face_flag_disable(bm, f_iter, FACE_TAG);
			BMO_face_flag_enable(bm, f_iter, FACE_ORIG);
		}

		if (BMO_error_occurred(bm)) {
			BMO_error_clear(bm);
			BMO_error_raise(bm, op, BMERR_DISSOLVEFACES_FAILED, NULL);
			goto cleanup;
		}
		
		BLI_array_append(faces, NULL);
		BLI_array_append(regions, faces);
	}

	/* track how many faces we should end up with */
	int totface_target = bm->totface;

	for (i = 0; i < BLI_array_count(regions); i++) {
		BMFace *f_new;
		int tot = 0;
		
		faces = regions[i];
		if (!faces[0]) {
			BMO_error_raise(bm, op, BMERR_DISSOLVEFACES_FAILED,
			                "Could not find boundary of dissolve region");
			goto cleanup;
		}
		
		while (faces[tot])
			tot++;
		
		f_new = BM_faces_join(bm, faces, tot, true);

		if (f_new) {
			/* maintain active face */
			if (act_face && bm->act_face == NULL) {
				bm->act_face = f_new;
			}
			totface_target -= tot - 1;
		}
		else {
			BMO_error_raise(bm, op, BMERR_DISSOLVEFACES_FAILED,
			                "Could not create merged face");
			goto cleanup;
		}

		/* if making the new face failed (e.g. overlapping test)
		 * unmark the original faces for deletion */
		BMO_face_flag_disable(bm, f_new, FACE_ORIG);
		BMO_face_flag_enable(bm, f_new, FACE_NEW);
	}

	/* Typically no faces need to be deleted */
	if (totface_target != bm->totface) {
		BMO_op_callf(bm, op->flag, "delete geom=%ff context=%i", FACE_ORIG, DEL_FACES);
	}

	if (use_verts) {
		BMIter viter;
		BMVert *v, *v_next;

		BM_ITER_MESH_MUTABLE (v, v_next, &viter, bm, BM_VERTS_OF_MESH) {
			if (BMO_vert_flag_test(bm, v, VERT_MARK)) {
				if (BM_vert_is_edge_pair(v)) {
					BM_vert_collapse_edge(bm, v->e, v, true, true);
				}
			}
		}
	}

	if (BMO_error_occurred(bm)) {
		goto cleanup;
	}

	BMO_slot_buffer_from_enabled_flag(bm, op, op->slots_out, "region.out", BM_FACE, FACE_NEW);

cleanup:
	/* free/cleanup */
	for (i = 0; i < BLI_array_count(regions); i++) {
		if (regions[i]) MEM_freeN(regions[i]);
	}

	BLI_array_free(regions);
#endif
}
