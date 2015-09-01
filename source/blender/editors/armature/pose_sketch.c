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
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software Foundation,
* Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
* The Original Code is Copyright (C) 2014 Blender Foundation, Joshua Leung
* 
* Original Author: Joshua Leung
* Contributor(s): Joshua Leung
*
* ***** END GPL LICENSE BLOCK *****
*
* Sketch-based posing tools for armatures
*
*/
/** \file blender/editors/armature/pose_sketch.c
* \ingroup edarmature
*/
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "BLI_blenlib.h"
#include "BLI_ghash.h"
#include "BLI_math.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "MEM_guardedalloc.h"

#include "DNA_action_types.h"
#include "DNA_armature_types.h"
//#include "DNA_constraint_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_action.h"
#include "BKE_armature.h"
#include "BKE_constraint.h"
#include "BKE_context.h"
#include "BKE_depsgraph.h"
#include "BKE_global.h"
#include "BKE_gpencil.h"
#include "BKE_report.h"

#include "ED_armature.h"
#include "ED_gpencil.h"
#include "ED_screen.h"

#include "armature_intern.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "WM_api.h"
#include "WM_types.h"

/* ***************************************************** */
/* Simple "Direct-Sketch" operator:
 * This operator assumes that the sketched line directly corresponds to
 * a bone chain, allowing us to directly map the bones to the sketched
 * line (using parametric positions).
 *
 * For now, this just uses Grease Pencil to provide the sketching functionality,
 * letting us focus on testing out the deformations-side of things.
 *
 * To simplify things further for the initial prototype, the actual keymapped tool
 * will just be a macro binding together Grease Pencil paint (one sketch) + this operator
 */

/* ---------------------------------------------------------------- */

/* Helper - Logic for which bones to include */
static bool psketch_direct_bone_can_include(bPoseChannel *pchan, bPoseChannel *prev_pchan)
{
	// XXX: potential bug with non-connected bones - we don't want to skip across that boundary for now...
	return ((prev_pchan == NULL) || (pchan->parent == prev_pchan));
}


/* Simplified GPencil stroke point, ready for pose matching */
typedef struct tGPStrokePosePoint {
	float co[3];	/* pose-space coordinates of this point */
	int index;		/* original index of this point in the stroke */
} tGPStrokePosePoint;

/* Figure out where each joint should fit along the stroke 
 *
 * The algorithm used here is roughly based on the technique
 * used in anim.c : calc_curvepath()
 */
static tGPStrokePosePoint *psketch_stroke_to_points(Object *ob, bGPDstroke *stroke, 
                                                    float *joint_dists, size_t num_joints, 
                                                    bool reversed)
{
	tGPStrokePosePoint *result = MEM_callocN(sizeof(tGPStrokePosePoint) * num_joints, "tGPStrokePosePoints");
	tGPStrokePosePoint *pt;
	
	float *distances = MEM_callocN(sizeof(float) * stroke->totpoints, "psketch stroke distances"); // XXX: wrong length
	float totlen = 0.0f;
	int i;
	
	/* 1) Compute total length of stroke, and the cumulative distance that each point sits at */
	/* NOTE: We start at i = 1, as distances[0] = 0 = totlen */
	for (i = 1; i < stroke->totpoints; i++) {		
		bGPDspoint *p2 = &stroke->points[i];
		bGPDspoint *p1 = &stroke->points[i - 1];
		
		totlen += len_v3v3(&p1->x, &p2->x);
		distances[i] = totlen;
		
		// xxx: debug: unselect all points so that the only selected ones are the ones we want
		p1->flag &= ~GP_SPOINT_SELECT;
		p2->flag &= ~GP_SPOINT_SELECT;
	}
	
	// XXX: prevent divbyzero
	printf("totlen = %f\n", totlen);
	if (totlen < 0.00001f)
		totlen = 1.0f;
	
	/* 2) Compute each stroke point */
	// XXX: assume ob->imat is initialised already
	for (i = 0, pt = result; i < num_joints; i++, pt++) {
		bGPDspoint *sp, *prev;
		float dist, dist_prev;
		float fac1, fac2;
		float d;
		int j;
		bool found = false;
		
		/* Get the distance that this stroke point is supposed to represent */
		/* NOTE: Multiplying the target distance out may lead to precision issues,
		 * but at least we don't need to do O(n) divides - one per poitn!
		 */
		if (reversed) {
			/* Reverse Order - ith joint from end/tail of chain */
			d = joint_dists[num_joints - i - 1] * totlen;
		}
		else {
			/* Forward Order - ith joint from start/head of chain, normal order */
			d = joint_dists[i] * totlen;
		}
		
		/* Go through stroke points searching for the one where the distance
		 * is greater than the current joint requires.
		 *
		 * NOTE: We start from the second point, so that we can calc the difference
		 *       between those and interpolate coordinates as required...
		 */
		for (j = 1, sp = stroke->points + 1;
		     j < stroke->totpoints;
		     j++, sp++)
		{
			if (d < distances[j]) {
				/* we just passed the point we need to use */
				found = true;
				break;
			}
		}
		
		if (found == false) {
			/* assume this is the last point */
			j = stroke->totpoints - 1;
			sp = &stroke->points[j];
		}

		dist_prev = distances[j - 1];
		prev = sp - 1;
		
		if (fabsf(dist - dist_prev) > 0.00001f) {
			fac1 = (dist - d) / (dist - dist_prev);
			fac2 = 1.0f - fac1;
		}
		else {
			// XXX?
			fac1 = 1.0f;
			fac2 = 0.0f;
		}
		
		/* Convert stroke coordinate to pose-space */
		interp_v3_v3v3(pt->co, &prev->x, &sp->x, fac2);
		mul_m4_v3(ob->imat, pt->co);
		
		/* Store index of point(s) from stroke that correspond to this */
		pt->index = j; // XXX
		
		// XXX: debug... for seeing which points were involved
		sp->flag |= GP_SPOINT_SELECT;
		prev->flag |= GP_SPOINT_SELECT;
	}
	
	// XXX: debugging...
	stroke->flag |= GP_STROKE_SELECT;
	
	/* Free temp data and return the stroke points array */
	MEM_freeN(distances);
	return result;
}


/* Adaptation of "Direct Mode" technique from Oztireli et al. (2013) */
static int psketch_direct_exec(bContext *C, wmOperator *op)
{
	Scene *scene = CTX_data_scene(C);
	Object *ob = CTX_data_active_object(C);
	
	bGPdata *gpd = ED_gpencil_data_get_active(C);
	bGPDlayer *gpl = gpencil_layer_getactive(gpd); // XXX: this assumes that any layer will do, as long as user drew in it recently
	bGPDframe *gpf = (gpl) ? gpl->actframe : NULL;
	bGPDstroke *stroke = (gpf) ? gpf->strokes.last : NULL;
	
	float chain_len = 0.0f;
	size_t num_items = 0;
	
	bPoseChannel *first_bone = NULL, *last_bone = NULL;
	bPoseChannel *prev_pchan = NULL;
	
	bPoseChannel **chain = NULL; /* chain of bones to affect */
	float *joint_dists = NULL;   /* for each joint, the parametric position [0.0, 1.0] that it occupies */
	bool reversed = NULL;
	
	tGPStrokePosePoint *spoints = NULL;
	bool use_stretch = RNA_boolean_get(op->ptr, "use_stretch");
	
	
	/* Abort if we don't have a reference stroke */
	// XXX: assume that the stroke is in 3D space
	if (stroke == NULL) {
		BKE_report(op->reports, RPT_ERROR, "No Grease Pencil stroke to use for posing the selected chain of bones");
		return OPERATOR_CANCELLED;
	}
	else if (stroke->totpoints == 1) {
		BKE_report(op->reports, RPT_ERROR, "Stroke is unusable (i.e. it is juts a dot)");
		return OPERATOR_CANCELLED;
	}
	
	/* 1) Find the chain of bones to include - start/end, number of bones, and length of the chain */
	CTX_DATA_BEGIN(C, bPoseChannel *, pchan, selected_pose_bones)
	{
		if (psketch_direct_bone_can_include(pchan, prev_pchan)) {
			/* adjust total length of chain, including any current scaling applied to the bone
			 * (as the user sketched the curve taking that into account)
			 */
			chain_len += len_v3v3(pchan->pose_head, pchan->pose_tail);
			num_items++;
			
			/* adjust start/end of chain */
			if (first_bone == NULL) {
				first_bone = pchan;
			}
			
			last_bone = pchan;
			prev_pchan = pchan;
		}
		else {
			/* skip bone - in the case where a bone has 2+ children, and more than one of these
			 * is selected, the direct child we're interested in may still show up...
			 */
			if (G.debug & G_DEBUG) printf("psketch_direct_init_bchain(): Skipping bone '%s'\n", pchan->name);
		}
	}
	CTX_DATA_END;
	
	if (num_items < 2) {
		BKE_report(op->reports, RPT_ERROR, "Select a chain of two or more bones first");
		return OPERATOR_CANCELLED;
	}
	
	if (IS_EQ(chain_len, 0.0f)) {
		BKE_report(op->reports, RPT_ERROR, "Zero length bone chain");
		return OPERATOR_CANCELLED;
	}
	
	
	// XXX: debug
	printf(
		"PSketch %d bones, total len = %f, first = %s, last = %s\n",
		num_items, chain_len, 
		(first_bone) ? first_bone->name : "<None>",
		(last_bone) ? last_bone->name : "<None>");
		
	/* 2) Find which end of the chain is closer to the start of the stroke.
	 *    This joint will be mapped to the first point in the stroke, etc.
	 */
	{
		bGPDspoint *sp = stroke->points;
		bGPDspoint *ep = sp + (stroke->totpoints - 1);
		float head[3], tail[3];
		float hdist, tdist;
		float hdist2, tdist2;
		
		/* convert pose-space coordinates to global space to be in same space as the GPencil strokes */
		mul_v3_m4v3(head, ob->obmat, first_bone->pose_head);
		mul_v3_m4v3(tail, ob->obmat, last_bone->pose_tail);
		
		/* which one is closer? */
		hdist = len_v3v3(&sp->x, head);
		tdist = len_v3v3(&sp->x, tail);
		
		hdist2 = len_v3v3(&ep->x, head);
		tdist2 = len_v3v3(&ep->x, tail);
		
		printf("sp = %p, ep = %p, count = %d\n", sp, ep, stroke->totpoints);
		
		/* We assume here that there should be a bias towards users drawing strokes in the
		 * direction that the bones flow. Therefore, only reverse the direction if strictly
		 * necessary...
		 */
		if (tdist < hdist) {
			/* Special Case: Watch out for C-shaped chains/curves
			 * We shouldn't reverse if the stroke ends closer to
			 * the endpoint, even if the tail is closer to the start
			 * of the stroke. This should prevent reversal when the
			 * head gets close to the tail, but the stroke also ends
			 * near the tail.
			 */
			if (hdist2 < tdist2) {
				printf("reversed - %f %f | %f %f\n", hdist, tdist, hdist2, tdist2);
				reversed = true;
			}
			else {
				printf("not reversed (c) - %f %f | %f %f\n", hdist, tdist, hdist2, tdist2);
				reversed = true;
			}
		}
		else {
			printf("not reversed - %f %f | %f %f\n", hdist, tdist, hdist2, tdist2);
			reversed = false;
		}
	}
	
	/* 3) Compute the relative positions of the joints, as well as the sequence of bones */
	chain = MEM_callocN(sizeof(bPoseChannel *) * num_items, "psketch bone chain");
	joint_dists = MEM_callocN(sizeof(float) * (num_items + 1), "psketch joints");
	
	{
		float len = 0.0f;
		size_t i = 0;
		
		prev_pchan = NULL;
		
		CTX_DATA_BEGIN(C, bPoseChannel *, pchan, selected_pose_bones)
		{
			if (psketch_direct_bone_can_include(pchan, prev_pchan)) {
				/* If this is the first bone, initialise first joint's distance */
				if (i == 0) {
					joint_dists[0] = 0.0f;
				}
				
				/* Set this joint's distance */
				len += len_v3v3(pchan->pose_head, pchan->pose_tail);
				joint_dists[i + 1] = len / chain_len;
				
				/* Store this bone */
				chain[i] = pchan;
				
				/* Increment for next step */
				i++;
			}
		}
		CTX_DATA_END;
	}
	
	/* 4) Create a simplified version of the stroke
	 *    - Sampled down to have 1 pt per joint
	 *    - Coordinates in the pose space (not global)
	 */
	spoints = psketch_stroke_to_points(ob, stroke, joint_dists, num_items + 1, reversed);
	
	
	/* 5) Adjust each bone */
	{
		size_t i;
		
		for (i = 0; i < num_items; i++) {
			bPoseChannel *pchan = chain[i];
			tGPStrokePosePoint *p1 = &spoints[i];
			tGPStrokePosePoint *p2 = &spoints[i + 1];
			
			float old_vec[3], new_vec[3];
			float old_len, new_len, sfac;
			float dmat[3][3];
			
			/* Compute old and new vectors for the bone direction */
			sub_v3_v3v3(old_vec, pchan->pose_tail, pchan->pose_head);
			sub_v3_v3v3(new_vec, p2->co, p1->co);
			
			/* Compute transform needed to rotate old to new,
			 * as well as the scaling factor needed to stretch
			 * the old bone to match the new one
			 */
			old_len = normalize_v3(old_vec);
			new_len = normalize_v3(new_vec);
			sfac    = new_len / old_len;
			
			rotation_between_vecs_to_mat3(dmat, old_vec, new_vec);
			
			printf("%s: old vec = %f %f %f,  new vec = %f %f %f\n",
					pchan->name,
					old_vec[0], old_vec[1], old_vec[2],
					new_vec[0], new_vec[1], new_vec[2]);
			{
				float rot[3];
				mat3_to_eul(rot, dmat);
				printf("   r = %f %f %f\n", rot[0], rot[1], rot[2]);
			}
			
			/* Apply the rotation */
			{
				float tmat[3][3], rmat[3][3];
				float scale[3];
				
				/* Separate out the scaling and rotation components,
				 * so that we can operate on the rotation component
				 * separately without skewing the matrix
				 */
				copy_m3_m4(tmat, pchan->pose_mat);
				
				scale[0] = normalize_v3(tmat[0]);
				scale[1] = normalize_v3(tmat[1]);
				scale[2] = normalize_v3(tmat[2]);
				
				/* Apply extra rotation to rotate the bone */
				mul_m3_m3m3(rmat, dmat, tmat);
				
				/* Reapply scaling */
				if (use_stretch) {
					/* Apply the scaling factor to all axes,
					 * not just on the y-axis needed to make
					 * things fit.
					 *
					 * TODO: XZ scaling modes could be introduced
					 * here as an alternative.
					 */
					mul_v3_fl(rmat[0], scale[0] * sfac);
					mul_v3_fl(rmat[1], scale[1] * sfac);
					mul_v3_fl(rmat[2], scale[2] * sfac);

				}
				else {
					/* Just reapply scaling normally */
					mul_v3_fl(rmat[0], scale[0]);
					mul_v3_fl(rmat[1], scale[1]);
					mul_v3_fl(rmat[2], scale[2]);
				}
				
				/* Copy new transforms back to the bone */
				copy_m4_m3(pchan->pose_mat, rmat);
			}
			
			/* Compute the new joints */
			if ((pchan->parent == NULL) || (pchan->bone->flag & BONE_CONNECTED)) {
				/* head -> start of chain */
				copy_v3_v3(pchan->pose_mat[3], p1->co);
				copy_v3_v3(pchan->pose_head, p1->co);
			}
			
			if (use_stretch) {
				/* Scaled Tail - Reapply stretched length to new-vector, and add that to the bone's current position */
				float vec[3];
				
				mul_v3_v3fl(vec, new_vec, new_len);
				add_v3_v3v3(pchan->pose_tail, pchan->pose_head, vec);
			}
			else {
				/* Direction-Only Tail - Use new rotation but old length */
				float vec[3];
				
				mul_v3_v3fl(vec, new_vec, old_len);
				add_v3_v3v3(pchan->pose_tail, pchan->pose_head, vec);
			}
		}
	}
	
	/* free temp data */
	MEM_freeN(chain);
	MEM_freeN(joint_dists);
	MEM_freeN(spoints);
	
	/* updates */
	WM_event_add_notifier(C, NC_OBJECT | ND_POSE, ob);
	
	return OPERATOR_FINISHED;
}

void POSE_OT_sketch_direct(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Sketch Chain Pose";
	ot->idname = "POSE_OT_sketch_direct";
	ot->description = "Simple sketch-based posing tool, where a selected chain of bones is made to match the stroke drawn";
	
	/* callbacks */
	ot->exec = psketch_direct_exec;
	ot->poll = ED_operator_posemode;
	
	/* properties */
	RNA_def_boolean(ot->srna, "use_stretch", true, "Stretch to Fit", "Stretch bones to match the stroke exactly");
}

/* ***************************************************** */

/* ***************************************************** */

