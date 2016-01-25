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
 * The Original Code is Copyright (C) 2012 Blender Foundation, Joshua Leung
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Original Author: Joshua Leung
 * Contributor(s): Joshua Leung
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 * Implements a brush-based "sculpting" tool for posing rigs
 * in an easier and faster manner.
 */

/** \file blender/editors/armature/pose_sculpt.c
 *  \ingroup edarmature
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_dynstr.h"
#include "BLI_ghash.h"
#include "BLI_threads.h"
#include "BLI_rand.h"

#include "DNA_anim_types.h"
#include "DNA_action_types.h"
#include "DNA_armature_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_armature.h"
#include "BKE_context.h"
#include "BKE_depsgraph.h"
#include "BKE_report.h"

#include "BLT_translation.h"

#include "BIF_gl.h"
#include "BIF_glutil.h"

#include "ED_armature.h"
#include "ED_keyframing.h"
#include "ED_screen.h"
#include "ED_transform.h"
#include "ED_view3d.h"

#include "armature_intern.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "WM_api.h"
#include "WM_types.h"

/* ******************************************************** */
/* General settings */

/* Get Pose Sculpt Settings */
PSculptSettings *psculpt_settings(Scene *scene)
{
	return (scene->toolsettings) ? &scene->toolsettings->psculpt : NULL;
}

/* Get current brush */
PSculptBrushData *psculpt_get_brush(Scene *scene)
{
	PSculptSettings *pset = psculpt_settings(scene);
	
	if ((pset) && (pset->brushtype >= 0) && (pset->brushtype < PSCULPT_TOT_BRUSH))
		return &pset->brush[pset->brushtype];
	else
		return NULL;
}


/* ******************************************************** */
/* Polling Callbacks */

int psculpt_poll(bContext *C)
{
	Scene *scene = CTX_data_scene(C);
	Object *ob = CTX_data_active_object(C);
	
	ScrArea *sa = CTX_wm_area(C);
	ARegion *ar = CTX_wm_region(C);
	
	return ((scene && ob) &&
			((ob->pose) && (ob->mode & OB_MODE_POSE)) && 
			(sa->spacetype == SPACE_VIEW3D) &&
			(ar->regiontype == RGN_TYPE_WINDOW));
}

/* ******************************************************** */
/* Cursor drawing */

/* Helper callback for drawing the cursor itself */
static void psculpt_brush_apply_drawcursor(bContext *C, int x, int y, void *UNUSED(customdata))
{
	PSculptBrushData *brush = psculpt_get_brush(CTX_data_scene(C));
	
	if (brush) {
		glPushMatrix();
		
		glTranslatef((float)x, (float)y, 0.0f);
		
		glColor4ub(255, 255, 255, 128);
		glLineWidth(1.0);
		
		glEnable(GL_LINE_SMOOTH);
		glEnable(GL_BLEND);
		
		glutil_draw_lined_arc(0.0, M_PI * 2.0, brush->size, 40);
		
		glDisable(GL_BLEND);
		glDisable(GL_LINE_SMOOTH);
		
		glPopMatrix();
	}
}

/* Turn brush cursor in 3D view on/off */
static void psculpt_toggle_cursor(bContext *C, bool enable)
{
	PSculptSettings *pset = psculpt_settings(CTX_data_scene(C));
	
	if (pset->paintcursor && !enable) {
		/* clear cursor */
		WM_paint_cursor_end(CTX_wm_manager(C), pset->paintcursor);
		pset->paintcursor = NULL;
	}
	else if (enable) {
		/* enable cursor */
		pset->paintcursor = WM_paint_cursor_activate(CTX_wm_manager(C), 
		                                             psculpt_poll, 
		                                             psculpt_brush_apply_drawcursor, NULL);
	}
}

/* ******************************************************** */
/* Brush Operation Callbacks */

/* Defines ------------------------------------------------ */

/* Struct passed to all callback functions */
typedef struct tPSculptContext {
	/* Relevant context data */
	ViewContext vc;
	
	ARegion *ar;
	View3D *v3d;
	RegionView3D *rv3d;
	
	Scene *scene;
	Object *ob;
	
	/* General Brush Data */
	PSculptBrushData *brush;	/* active brush */
	
	const float *mval;			/* mouse coordinates (pixels) */
	float rad;					/* radius of brush (pixels) */
	float dist;					/* distance from brush to item being sculpted (pixels) */
	float fac;					/* brush strength (factor 0-1) */
	
	short invert;				/* "subtract" mode? */
	bool  is_first;				/* first run through? */
	
	/* Brush Specific Data */
	float dvec[3];				/* mouse travel vector, or something else */
	float rmat[3][3];			/* rotation matrix to apply to all bones (e.g. trackball) */
} tPSculptContext;


/* Affected bones */
typedef struct tAffectedBone {
	struct tAffectedBone *next, *prev;
	
	bPoseChannel *pchan;		/* bone in question */
	float fac;					/* (last) strength factor applied to this bone */
	
	// TODO: original bone values?
	// TODO: bitflag for which channels need keying
} tAffectedBone;

/* Pose Sculpting brush operator data  */
typedef struct tPoseSculptingOp {
	tPSculptContext data;		/* "context" data to pass to brush callbacks later */
	
	Scene *scene;
	Object *ob;
	
	float lastmouse[2];			/* previous mouse position */
	bool is_first;				/* is this the first time we're applying anything? */
	bool is_timer_tick;			/* is the current event being processed due to a timer tick? */
	
	wmTimer *timer;				/* timer for in-place accumulation of brush effect */
	
	GHash *affected_bones;		/* list of bones affected by brush */
	
	KeyingSet *ks;				/* keyingset to use */
	ListBase ks_sources;		/* list of elements to be keyed by the Keying Set */
} tPoseSculptingOp;

/* Callback Function Signature */
typedef void (*PSculptBrushCallback)(tPoseSculptingOp *pso, tPSculptContext *data, bPoseChannel *pchan, float sco1[2], float sco2[2]);

/* Init ------------------------------------------------ */

static void psculpt_init_context_data(bContext *C, tPSculptContext *data)
{
	memset(data, 0, sizeof(*data));
	
	data->scene = CTX_data_scene(C);
	data->ob = CTX_data_active_object(C);
	
	data->brush = psculpt_get_brush(data->scene);
}

static void psculpt_init_view3d_data(bContext *C, tPSculptContext *data)
{
	/* init context data */
	psculpt_init_context_data(C, data);
	
	/* hook up 3D View context */
	view3d_set_viewcontext(C, &data->vc);
}

/* Brush Utilities ---------------------------------------- */

/* get euler rotation value to work with */
static short get_pchan_eul_rotation(float eul[3], short *order, const bPoseChannel *pchan)
{
	if (ELEM(pchan->rotmode, ROT_MODE_QUAT, ROT_MODE_AXISANGLE)) {
		/* ensure that we're not totally locked... */
		if ((pchan->protectflag & OB_LOCK_ROT4D) &&
			(pchan->protectflag & (OB_LOCK_ROTX | OB_LOCK_ROTY | OB_LOCK_ROTZ | OB_LOCK_ROTW)))
		{
			/* if 4D locked, then just a single flag can cause trouble = no go */
			return false;
		}
		
		/* set rotation order - dummy default */
		if (order) {
			*order = ROT_MODE_EUL;
		}
		
		/* convert rotations to eulers */
		switch (pchan->rotmode) {
			case ROT_MODE_QUAT:
				quat_to_eulO(eul, ROT_MODE_EUL, pchan->quat);
				break;
			
			case ROT_MODE_AXISANGLE:
				axis_angle_to_eulO(eul, ROT_MODE_EUL, pchan->rotAxis, pchan->rotAngle);
				break;
				
			default:
				/* this can't happen */
				return false;
		}
	}
	else {
		/* copy pchan rotation to edit-euler */
		copy_v3_v3(eul, pchan->eul);
		
		/* set rotation order to whatever it is */
		if (order) {
			*order = pchan->rotmode;
		}
	}
	
	return true;
}

/* flush euler rotation value */
static void set_pchan_eul_rotation(const float eul[3], bPoseChannel *pchan)
{
	switch (pchan->rotmode) {
		case ROT_MODE_QUAT: /* quaternion */
			eulO_to_quat(pchan->quat, eul, ROT_MODE_EUL);
			break;
		
		case ROT_MODE_AXISANGLE: /* axis angle */
			eulO_to_axis_angle(pchan->rotAxis, &pchan->rotAngle, eul, ROT_MODE_EUL);
			break;
		
		default: /* euler */
			copy_v3_v3(pchan->eul, eul);
			break;
	}
}

/* ........................................................ */

#define TD_PBONE_LOCAL_MTX_C  (1 << 0)
#define TD_PBONE_LOCAL_MTX_P  (1 << 1)

/* Apply given rotation on the given bone
 *
 * Adapted from the transform system code for trackball rotations
 *  - Main method adapted from the T_POSE case for ElementRotation() in transform.c
 *  - All transform/setup math adapted from bPoseChannel -> TransData stuff in transform_conversions.c
 */
static void pchan_do_rotate(Object *ob, bPoseChannel *pchan, float mat[3][3])
{
	float mtx[3][3], smtx[3][3], r_mtx[3][3], r_smtx[3][3], l_smtx[3][3];
	//float center[3] = {0}, td_center[3] = {0};
	short locks = pchan->protectflag;
	short td_flag = 0;
	
	float pmtx[3][3], imtx[3][3];
	
	/* ...... transform_conversions.c stuff here ........ */
	// TODO: maybe this stuff can (or maybe should - to prevent errors) be saved off?
	
	// 	copy_v3_v3(td_center, pchan->pose_mat[3]);
	// if (localspace)
	// 	copy_v3_v3(center, td_center);
	// else
	// 	copy_v3_v3(center, bones.center);
	
	/* Compute the transform matrices needed */
	/* New code, using "generic" BKE_pchan_to_pose_mat(). */
	{
		float pmat[3][3], tmat[3][3], cmat[3][3];
		float rotscale_mat[4][4], loc_mat[4][4];
		float rpmat[3][3];
		float omat[3][3];
		
		copy_m3_m4(omat, ob->obmat);
		
		BKE_pchan_to_pose_mat(pchan, rotscale_mat, loc_mat);
		copy_m3_m4(pmat, rotscale_mat);
		
		/* Grrr! Exceptional case: When translating pose bones that are either Hinge or NoLocal,
		 * and want align snapping, we just need both loc_mat and rotscale_mat.
		 * So simply always store rotscale mat in td->ext, and always use it to apply rotations...
		 * Ugly to need such hacks! :/ */
		copy_m3_m4(rpmat, rotscale_mat);
		
		if (false /*constraints_list_needinv(t, &pchan->constraints)*/) {  // XXX...
			copy_m3_m4(tmat, pchan->constinv);
			invert_m3_m3(cmat, tmat);
			mul_m3_series(mtx, cmat, omat, pmat);
			mul_m3_series(r_mtx, cmat, omat, rpmat);
		}
		else {
			mul_m3_series(mtx, omat, pmat);
			mul_m3_series(r_mtx, omat, rpmat);
		}
		invert_m3_m3(r_smtx, r_mtx);
	}
	
	pseudoinverse_m3_m3(smtx, mtx, PSEUDOINVERSE_EPSILON);
	
	/* Exceptional Case: Rotating the pose bone which also applies transformation
	 * when a parentless bone has BONE_NO_LOCAL_LOCATION
	 */
	if (pchan->bone->flag & BONE_NO_LOCAL_LOCATION) {
		if (pchan->parent) {
			/* same as td->smtx but without pchan->bone->bone_mat */
			td_flag |= TD_PBONE_LOCAL_MTX_C;
			mul_m3_m3m3(l_smtx, pchan->bone->bone_mat, smtx);
		}
		else {
			td_flag |= TD_PBONE_LOCAL_MTX_P;
		}
	}
	
	
	/* ....... transform.c stuff begins here .........  */
	
	/* Extract and invert armature object matrix */
	copy_m3_m4(pmtx, ob->obmat);
	invert_m3_m3(imtx, pmtx);
	
	/* Location */
	if ((pchan->parent == NULL) || !(pchan->bone->flag & BONE_CONNECTED)) {
		float vec[3] = {0};
		//sub_v3_v3v3(vec, td->center, center);
		
		mul_m3_v3(pmtx, vec);   /* To Global space */
		mul_m3_v3(mat, vec);    /* (Applying rotation) */
		mul_m3_v3(imtx, vec);   /* To Local space */
		
		//add_v3_v3(vec, center);
		/* vec now is the location where the bone has to be */
		
		//sub_v3_v3v3(vec, vec, td_center); /* Translation needed from the initial location */
		
		/* special exception, see TD_PBONE_LOCAL_MTX definition comments */
		if (td_flag & TD_PBONE_LOCAL_MTX_P) {
			/* do nothing */
		}
		else if (td_flag & TD_PBONE_LOCAL_MTX_C) {
			mul_m3_v3(pmtx, vec);   /* To Global space */
			mul_m3_v3(l_smtx, vec); /* To Pose space (Local Location) */
		}
		else {
			mul_m3_v3(pmtx, vec);  /* To Global space */
			mul_m3_v3(smtx, vec);  /* To Pose space */
		}
		
		if (locks & OB_LOCK_LOCX) vec[0] = 0.0f;
		if (locks & OB_LOCK_LOCY) vec[1] = 0.0f;
		if (locks & OB_LOCK_LOCZ) vec[2] = 0.0f;
		
		add_v3_v3(pchan->loc, vec);
		
		//constraintTransLim(t, td);
	}
	
	/* Rotation */
	/* MORE HACK: as in some cases the matrix to apply location and rot/scale is not the same,
	 * and ElementRotation() might be called in Translation context (with align snapping),
	 * we need to be sure to actually use the *rotation* matrix here...
	 * So no other way than storing it in some dedicated members of td->ext! */
	if (true) {
		/* euler or quaternion/axis-angle? */
		if (pchan->rotmode == ROT_MODE_QUAT) {
			float oldquat[4], quat[4];
			float fmat[3][3];
			
			copy_qt_qt(oldquat, pchan->quat);
			
			mul_m3_series(fmat, r_smtx, mat, r_mtx);
			mat3_to_quat(quat, fmat); /* Actual transform */
			
			mul_qt_qtqt(pchan->quat, quat, oldquat);
			
			/* this function works on end result */
			if (locks & OB_LOCK_ROT4D) {
				if (locks & OB_LOCK_ROTW) pchan->quat[0] = oldquat[0];
				if (locks & OB_LOCK_ROTX) pchan->quat[1] = oldquat[1];
				if (locks & OB_LOCK_ROTX) pchan->quat[2] = oldquat[2];
				if (locks & OB_LOCK_ROTX) pchan->quat[3] = oldquat[3];
			}
			else {
				float eul[3], oldeul[3];
				
				quat_to_eulO(eul, ROT_MODE_EUL, pchan->quat);
				quat_to_eulO(oldeul, ROT_MODE_EUL, oldquat);
				
				if (locks & OB_LOCK_ROTX) eul[0] = oldeul[0];
				if (locks & OB_LOCK_ROTY) eul[1] = oldeul[1];
				if (locks & OB_LOCK_ROTZ) eul[2] = oldeul[2];
				
				eulO_to_quat(pchan->quat, eul, ROT_MODE_EUL);
			}
		}
		else if (pchan->rotmode == ROT_MODE_AXISANGLE) {
			/* calculate effect based on quats */
			float oldAxis[3], oldAngle;
			float iquat[4], tquat[4], quat[4];
			float fmat[3][3];
			
			copy_v3_v3(oldAxis, pchan->rotAxis);
			oldAngle = pchan->rotAngle;
			
			axis_angle_to_quat(iquat, pchan->rotAxis, pchan->rotAngle);
			
			mul_m3_series(fmat, r_smtx, mat, r_mtx);
			mat3_to_quat(quat, fmat); /* Actual transform */
			mul_qt_qtqt(tquat, quat, iquat);
			
			quat_to_axis_angle(pchan->rotAxis, &pchan->rotAngle, tquat);
			
			/* this function works on end result */
			if (locks & OB_LOCK_ROT4D) {
				if (locks & OB_LOCK_ROTW) pchan->rotAngle   = oldAngle;
				if (locks & OB_LOCK_ROTX) pchan->rotAxis[0] = oldAxis[0];
				if (locks & OB_LOCK_ROTX) pchan->rotAxis[1] = oldAxis[1];
				if (locks & OB_LOCK_ROTX) pchan->rotAxis[2] = oldAxis[2];
			}
			else {
				float eul[3], oldeul[3];
				
				axis_angle_to_eulO(eul, ROT_MODE_EUL, pchan->rotAxis, pchan->rotAngle);
				axis_angle_to_eulO(oldeul, ROT_MODE_EUL, oldAxis, oldAngle);
				
				if (locks & OB_LOCK_ROTX) eul[0] = oldeul[0];
				if (locks & OB_LOCK_ROTY) eul[1] = oldeul[1];
				if (locks & OB_LOCK_ROTZ) eul[2] = oldeul[2];
				
				eulO_to_axis_angle(pchan->rotAxis, &pchan->rotAngle, eul, ROT_MODE_EUL);
			}
		}
		else {
			float smat[3][3], fmat[3][3], totmat[3][3];
			float eulmat[3][3];
			float eul[3];
			
			mul_m3_m3m3(totmat, mat, r_mtx);
			mul_m3_m3m3(smat, r_smtx, totmat);
			
			/* calculate the total rotation in eulers */
			copy_v3_v3(eul, pchan->eul);
			eulO_to_mat3(eulmat, eul, pchan->rotmode);
			
			/* mat = transform, obmat = bone rotation */
			mul_m3_m3m3(fmat, smat, eulmat);
			
			mat3_to_compatible_eulO(eul, pchan->eul, pchan->rotmode, fmat);
			
			/* and apply (to end result only) */
			if (locks & OB_LOCK_ROTX) eul[0] = pchan->eul[0];
			if (locks & OB_LOCK_ROTY) eul[1] = pchan->eul[1];
			if (locks & OB_LOCK_ROTZ) eul[2] = pchan->eul[2];
			
			copy_v3_v3(pchan->eul, eul);
		}
		
		//constraintRotLim(t, td);
	}
}

/* ........................................................ */

#if 0  // XXX: Old, slightly buggy code

/* convert pose-space joints of PoseChannel to loc/rot/scale components 
 * <> pchan: (bPoseChannel) pose channel that we're working on
 * < dvec: (vector) vector indicating direction of bone desired
 */
static void apply_pchan_joints(bPoseChannel *pchan, float dvec[3])
{
	float poseMat[4][4], poseDeltaMat[4][4];
	short locks = pchan->protectflag;
	
	/* 1) build pose matrix
	 *    Use method used for Spline IK in splineik_evaluate_bone() : steps 3,4
	 */
	{
		float dmat[3][3], rmat[3][3], tmat[3][3];
		float raxis[3], rangle;
		float smat[4][4], size[3];
		
		/* get scale factors */
		mat4_to_size(size, pchan->pose_mat);
		
		/* compute the raw rotation matrix from the bone's current matrix by extracting only the
		 * orientation-relevant axes, and normalising them
		 */
		copy_v3_v3(rmat[0], pchan->pose_mat[0]);
		copy_v3_v3(rmat[1], pchan->pose_mat[1]);
		copy_v3_v3(rmat[2], pchan->pose_mat[2]);
		normalize_m3(rmat);
		
		/* also, normalise the orientation imposed by the bone, now that we've extracted the scale factor */
		normalize_v3(dvec);
		
		/* calculate smallest axis-angle rotation necessary for getting from the
		 * current orientation of the bone, to the spline-imposed direction
		 */
		cross_v3_v3v3(raxis, rmat[1], dvec);
		
		rangle = dot_v3v3(rmat[1], dvec);
		rangle = acos( MAX2(-1.0f, MIN2(1.0f, rangle)) );
		
		/* construct rotation matrix from the axis-angle rotation found above 
		 *	- this call takes care to make sure that the axis provided is a unit vector first
		 */
		axis_angle_to_mat3(dmat, raxis, rangle);
		
		/* combine these rotations so that the y-axis of the bone is now aligned as the spline dictates,
		 * while still maintaining roll control from the existing bone animation
		 */
		mul_m3_m3m3(tmat, dmat, rmat); // m1, m3, m2
		normalize_m3(tmat); /* attempt to reduce shearing, though I doubt this'll really help too much now... */
		
		/* apply scaling back onto this */
		size_to_mat4(smat, size);
		mul_m4_m3m4(poseMat, tmat, smat);
		
		/* apply location too */
		copy_v3_v3(poseMat[3], pchan->pose_head);
	}
	
	/* 2) take away restpose so that matrix is fit for low-level */
	{
		//float imat[4][4];
		
		//invert_m4_m4(imat, pchan->bone->arm_mat);
		//mult_m4_m4m4(poseDeltaMat, imat, poseMat);
		
		BKE_armature_mat_pose_to_bone(pchan, poseMat, poseDeltaMat);
	}
	
	/* 3) apply these joints to low-level transforms */
	//if ((locks & (OB_LOCK_ROTX | OB_LOCK_ROTY | OB_LOCK_ROTZ)) ||
	//	  ((locks & OB_LOCK_ROT4D) && (locks & OB_LOCK_ROTW)) )
	if (locks)
	{
		float dloc[3], dsize[3];
		float rmat[3][3];
		
		float eul[3];
		short rotOrder;
		
		/* decompose to loc, size, and rotation matrix */
		mat4_to_loc_rot_size(dloc, rmat, dsize, poseDeltaMat);
		
		/* only apply location if not locked */
		if ((locks & OB_LOCK_LOCX) == 0) pchan->loc[0] = dloc[0];
		if ((locks & OB_LOCK_LOCY) == 0) pchan->loc[1] = dloc[1];
		if ((locks & OB_LOCK_LOCZ) == 0) pchan->loc[2] = dloc[2];
		
		/* scaling is ignored - it shouldn't have changed for now, so just leave it... */
		
		/* apply rotation matrix if we can */
		if (get_pchan_eul_rotation(eul, &rotOrder, pchan)) {
			float oldeul[3] = {eul[0], eul[1], eul[2]};
			
			/* decompose to euler, then knock out anything bad */
			mat3_to_compatible_eulO(eul, oldeul, rotOrder, rmat);
			
			if (locks & OB_LOCK_ROTX) eul[0] = oldeul[0];
			if (locks & OB_LOCK_ROTY) eul[1] = oldeul[1];
			if (locks & OB_LOCK_ROTZ) eul[2] = oldeul[2];
			
			set_pchan_eul_rotation(eul, pchan);
		}
	}
	else {
		/* no locking - use simpler method */
		BKE_pchan_apply_mat4(pchan, poseDeltaMat, true);
	}
}

#endif // XXX: end of old, buggy and unused code

/* ........................................................ */

/* check if a bone has already been affected by the brush, and add an entry if not */
static tAffectedBone *verify_bone_is_affected(tPoseSculptingOp *pso, tPSculptContext *data, bPoseChannel *pchan, bool add)
{
	/* try to find bone */
	tAffectedBone *tab = BLI_ghash_lookup(pso->affected_bones, pchan);
	
	/* add if not found and we're allowed to */
	if ((tab == NULL) && (add)) {
		tab = MEM_callocN(sizeof(tAffectedBone), "tAffectedBone");
		
		tab->pchan = pchan;
		tab->fac = 0.5f; // placeholder
		
		BLI_ghash_insert(pso->affected_bones, pchan, tab);
	}
	
	return tab;
}

/* free affected bone temp data */
static void free_affected_bone(void *tab_p)
{
	MEM_freeN(tab_p);
}

/* Brushes ------------------------------------------------ */

/* change selection status of bones - used to define masks */
static void psculpt_brush_select_apply(tPoseSculptingOp *pso, tPSculptContext *data, bPoseChannel *pchan, float UNUSED(sco1[2]), float UNUSED(sco2[2]))
{
	if (pchan->bone) {
		if (data->invert)
			pchan->bone->flag &= ~BONE_SELECTED;
		else
			pchan->bone->flag |= BONE_SELECTED;
	}
}

/* .......................... */

/* "Smooth" brush */
static void psculpt_brush_smooth_apply(tPoseSculptingOp *pso, tPSculptContext *data, bPoseChannel *pchan, float sco1[2], float sco2[2])
{
	
}

/* .......................... */

/* "Grab" brush - Translate bone */
static void psculpt_brush_grab_apply(tPoseSculptingOp *pso, tPSculptContext *data, bPoseChannel *pchan, float UNUSED(sco1[2]), float UNUSED(sco2[2]))
{
	PSculptBrushData *brush = data->brush;
	float imat[4][4], mat[4][4];
	float cvec[3];
	float fac;
	
	/* strength of push */
	fac = fabsf(1.0f - data->dist / data->rad) * data->fac;
	if (data->invert) fac = -fac;
	
	if (brush->flag & PSCULPT_BRUSH_FLAG_GRAB_INITIAL) {
		tAffectedBone *tab = verify_bone_is_affected(pso, data, pchan, data->is_first);
		
		/* if one couldn't be found or added, then it didn't exist the first time round,
		 * so we shouldn't proceed (to avoid clobbering additional bones)
		 */
		if (tab == NULL) {
			return;
		}
		else if (data->is_first) {
			/* store factor for later */
			tab->fac = fac;
		}
		else {
			/* don't use falloff - works better for chains */
			fac = 1.0f;
		}
	}
	
	/* compute inverse matrix to convert from screen-space to bone space */
	mul_m4_m4m4(mat, data->ob->obmat, pchan->bone->arm_mat);
	invert_m4_m4(imat, mat);
	
	/* apply deforms to bone locations only based on amount mouse moves */
	copy_v3_v3(cvec, data->dvec);
	mul_mat3_m4_v3(imat, cvec);
	mul_v3_fl(cvec, fac);
	
	/* knock out invalid transforms */
	if ((pchan->parent) && (pchan->bone->flag & BONE_CONNECTED))
		return;
		
	if (pchan->protectflag & OB_LOCK_LOCX)
		cvec[0] = 0.0f;
	if (pchan->protectflag & OB_LOCK_LOCY)
		cvec[1] = 0.0f;
	if (pchan->protectflag & OB_LOCK_LOCZ)
		cvec[2] = 0.0f;
	
	/* apply to bone */
	add_v3_v3(pchan->loc, cvec);
}

/* .......................... */

/* "Adjust" Brush - Compute transform to apply to all bones inside the brush */
static void psculpt_brush_calc_trackball(tPoseSculptingOp *pso, tPSculptContext *data)
{
	PSculptBrushData *brush = data->brush;
	RegionView3D *rv3d = data->rv3d;
	
	float smat[3][3], totmat[3][3];
	float mat[3][3], refmat[3][3];
	float axis1[3], axis2[3];
	float angles[2];
	
	
	/* Compute screenspace movements for trackball transform
	 * Adapted from applyTrackball() in transform.c
	 */
	copy_v3_v3(axis1, rv3d->persinv[0]);
	copy_v3_v3(axis2, rv3d->persinv[1]);
	normalize_v3(axis1);
	normalize_v3(axis2);
	
	/* From InputTrackBall() in transform_input.c */
	angles[0] = (float)(pso->lastmouse[1] - data->mval[1]);
	angles[1] = (float)(data->mval[0] - pso->lastmouse[0]);
	
	mul_v2_fl(angles, 0.01f); /* (mi->factor = 0.01f) */
	
	/* Adapted from applyTrackballValue() in transform.c */
	axis_angle_normalized_to_mat3(smat, axis1, angles[0]);
	axis_angle_normalized_to_mat3(totmat, axis2, angles[1]);
	
	mul_m3_m3m3(mat, smat, totmat);
	
	/* Adjust strength of effect */
	unit_m3(refmat);
	interp_m3_m3m3(data->rmat, refmat, mat, brush->strength);
}

/* "Adjust" Brush - i.e. a simple trackball transform */
// TODO: on root bones, don't do trackball... do grab instead?
static void psculpt_brush_adjust_apply(tPoseSculptingOp *pso, tPSculptContext *data, bPoseChannel *pchan, float UNUSED(sco1[2]), float UNUSED(sco2[2]))
{
	pchan_do_rotate(data->ob, pchan, data->rmat);
}

/* .......................... */

/* "Curl" brush - Rotate bone around its non-primary axes */
static void psculpt_brush_curl_apply(tPoseSculptingOp *pso, tPSculptContext *data, bPoseChannel *pchan, float UNUSED(sco1[2]), float UNUSED(sco2[2]))
{
	PSculptBrushData *brush = data->brush;
	short locks = pchan->protectflag;
	float eul[3] = {0.0f};
	float angle = 0.0f;
	
	/* get temp euler tuple to work on */
	if (get_pchan_eul_rotation(eul, NULL, pchan) == false)
		return;
	
	/* Amount to rotate depends on the strength of the brush 
	 * - The current calculation results in 0.xy degree values. Multiplying by even 2.5
	 *   however is much too strong for controllability. So, leaving it as-is.
	 * - Rotations are internally represented using radians, which are very sensitive
	 */
	angle = fabsf(1.0f - data->dist / data->rad) * data->fac;	//printf("%f ", angle);
	angle = DEG2RAD(angle);                                     //printf("%f \n", angle);
	
	if (data->invert) angle = -angle;
	
	/* rotate on x/z axes, whichever isn't locked */
	if (ELEM(brush->xzMode, PSCULPT_BRUSH_DO_XZ, PSCULPT_BRUSH_DO_X) && 
		(locks & OB_LOCK_ROTX)==0)
	{
		/* apply to x axis */
		eul[0] += angle;
	}
	
	if (ELEM(brush->xzMode, PSCULPT_BRUSH_DO_XZ, PSCULPT_BRUSH_DO_Z) && 
		(locks & OB_LOCK_ROTZ)==0)
	{
		/* apply to z axis */
		eul[2] += angle;
	}
	
	/* flush values */
	set_pchan_eul_rotation(eul, pchan);
}

/* .......................... */

/* "Twist" brush - Rotate bone around its primary axis */
static void psculpt_brush_twist_apply(tPoseSculptingOp *pso, tPSculptContext *data, bPoseChannel *pchan, float UNUSED(sco1[2]), float UNUSED(sco2[2]))
{
	short locks = pchan->protectflag;
	float eul[3] = {0.0f};
	float angle = 0.0f;
	
	/* get temp euler tuple to work on */
	if (get_pchan_eul_rotation(eul, NULL, pchan) == false)
		return;
	
	/* Amount to rotate depends on the strength of the brush 
	 * - The current calculation results in 0.xy degree values. Multiplying by even 2.5
	 *   however is much too strong for controllability. So, leaving it as-is.
	 * - Rotations are internally represented using radians, which are very sensitive
	 */
	angle = fabsf(1.0f - data->dist / data->rad) * data->fac;	//printf("%f ", angle);
	angle = DEG2RAD(angle);                                     //printf("%f \n", angle);
	
	if (data->invert) angle = -angle;
	
	/* just rotate on y, unless locked */
	if ((locks & OB_LOCK_ROTY) == 0) {
		eul[1] += angle;
	}
	
	/* flush values */
	set_pchan_eul_rotation(eul, pchan);
}

/* .......................... */

/* "Stretch" brush - Scale bone along its primary axis */
static void psculpt_brush_stretch_apply(tPoseSculptingOp *pso, tPSculptContext *data, bPoseChannel *pchan, float UNUSED(sco1[2]), float UNUSED(sco2[2]))
{
	PSculptBrushData *brush = data->brush;
	const float DAMP_FAC = 0.1f; /* damping factor - to be configurable? */
	float fac;
	
	/* scale factor must be greater than 1 for add, and less for subtract */
	fac = fabsf(1.0f - data->dist / data->rad) * data->fac * DAMP_FAC;
	
	if (data->invert)
		fac = 1.0f - fac;
	else
		fac = 1.0f + fac;
	
	/* perform scaling on y-axis - that's what "stretching" is! */
	pchan->size[1] *= fac;
	
	/* scale on x/z axes, whichever isn't locked */
	// TODO: investigate volume preserving stuff?
	if (ELEM(brush->xzMode, PSCULPT_BRUSH_DO_XZ, PSCULPT_BRUSH_DO_X) && 
		(pchan->protectflag & OB_LOCK_SCALEX) == 0)
	{
		/* apply to x axis */
		pchan->size[0] *= fac;
	}
	
	if (ELEM(brush->xzMode, PSCULPT_BRUSH_DO_XZ, PSCULPT_BRUSH_DO_Z) && 
		(pchan->protectflag & OB_LOCK_SCALEZ) == 0)
	{
		/* apply to z axis */
		pchan->size[2] *= fac;
	}
}

/* .......................... */

/* Clear transforms
 *
 * This brush doesn't immediately set values back to the rest pose.
 * Instead, it blends between the current value and the rest pose,
 * making it possible to "relax" the pose somewhat (if they are similar)
 */
// TODO: Use mouse pressure here to modulate factor too?
static void psculpt_brush_reset_apply(tPoseSculptingOp *UNUSED(pso), tPSculptContext *data, bPoseChannel *pchan, float UNUSED(sco1[2]), float UNUSED(sco2[2]))
{
	const short locks = pchan->protectflag;
	const float fac = data->fac;
	float eul[3] = {0.0f};
	
	/* location locks */
	if ((locks & OB_LOCK_LOCX) == 0)
		pchan->loc[0] = interpf(0.0f, pchan->loc[0], fac);
	if ((locks & OB_LOCK_LOCY) == 0)
		pchan->loc[1] = interpf(0.0f, pchan->loc[1], fac);
	if ((locks & OB_LOCK_LOCZ) == 0)
		pchan->loc[2] = interpf(0.0f, pchan->loc[2], fac);
		
	/* rotation locks */
	if (get_pchan_eul_rotation(eul, NULL, pchan)) {
		if ((locks & OB_LOCK_ROTX) == 0)
			eul[0] = interpf(0.0f, eul[0], fac);
		if ((locks & OB_LOCK_ROTY) == 0)
			eul[1] = interpf(0.0f, eul[1], fac);
		if ((locks & OB_LOCK_ROTZ) == 0)
			eul[2] = interpf(0.0f, eul[2], fac);
		
		// do compat euler?
		set_pchan_eul_rotation(eul, pchan);
	}
	
	/* scaling locks */
	if ((locks & OB_LOCK_SCALEX) == 0)
		pchan->size[0] = interpf(1.0f, pchan->size[0], fac);
	if ((locks & OB_LOCK_SCALEY) == 0)
		pchan->size[1] = interpf(1.0f, pchan->size[1], fac);
	if ((locks & OB_LOCK_SCALEZ) == 0)
		pchan->size[2] = interpf(1.0f, pchan->size[2], fac);
}

/* .......................... */

/* "radial" brush */
static void psculpt_brush_radial_apply(tPoseSculptingOp *pso, tPSculptContext *data, bPoseChannel *pchan, float UNUSED(sco1[2]), float UNUSED(sco2[2]))
{
	
}

/* "wrap" brush */
static void psculpt_brush_wrap_apply(tPoseSculptingOp *pso, tPSculptContext *data, bPoseChannel *pchan, float UNUSED(sco1[2]), float UNUSED(sco2[2]))
{
	
}

/* ******************************************************** */
/* Pose Sculpt - Painting Operator */

/* Init/Exit ----------------------------------------------- */

static int psculpt_brush_init(bContext *C, wmOperator *op)
{
	Scene *scene = CTX_data_scene(C);
	Object *ob = CTX_data_active_object(C);
	tPoseSculptingOp *pso;
	tPSculptContext *data;
	PSculptBrushData *brush;
	
	/* setup operator data */
	pso = MEM_callocN(sizeof(tPoseSculptingOp), "tPoseSculptingOp");
	op->customdata = pso;
	
	pso->is_first = true;
	
	pso->scene = scene;
	pso->ob = ob;
	
	pso->affected_bones = BLI_ghash_new(BLI_ghashutil_ptrhash, BLI_ghashutil_ptrcmp, "psculpt affected bones gh");
	
	/* ensure that object's inverse matrix is set and valid */
	invert_m4_m4(ob->imat, ob->obmat);
	
	/* setup callback data */
	data = &pso->data;
	psculpt_init_view3d_data(C, data);
	
	brush = data->brush;
	data->invert = (brush && (brush->flag & PSCULPT_BRUSH_FLAG_INV)) || 
	                (RNA_boolean_get(op->ptr, "invert"));
				  
	data->is_first = true;
	
	/* init data needed for handling autokeying
	 * - If autokeying is not applicable here, the keyingset will be NULL,
	 *   and therefore no autokeying stuff will need to happen later...
	 */
	if (autokeyframe_cfra_can_key(scene, &ob->id)) {
		pso->ks = ANIM_get_keyingset_for_autokeying(scene, ANIM_KS_WHOLE_CHARACTER_ID);
	}
	
	/* setup cursor and header drawing */
	ED_area_headerprint(CTX_wm_area(C), IFACE_("Pose Sculpting in progress..."));
	
	WM_cursor_modal_set(CTX_wm_window(C), BC_CROSSCURSOR);
	psculpt_toggle_cursor(C, true);
	
	return true;
}

static void psculpt_brush_exit(bContext *C, wmOperator *op)
{
	tPoseSculptingOp *pso = op->customdata;
	wmWindow *win = CTX_wm_window(C);
	
	/* unregister timer (only used for realtime) */
	if (pso->timer) {
		WM_event_remove_timer(CTX_wm_manager(C), win, pso->timer);
	}
	
	/* clear affected bones hash - second arg is provided to free allocated data */
	BLI_ghash_free(pso->affected_bones, NULL, free_affected_bone);
	
	/* disable cursor and headerprints */
	ED_area_headerprint(CTX_wm_area(C), NULL);
	
	WM_cursor_modal_restore(win);
	psculpt_toggle_cursor(C, false);
	
	/* free operator data */
	MEM_freeN(pso);
	op->customdata = NULL;
}

/* Apply ----------------------------------------------- */

/* Perform auto-keyframing */
static void psculpt_brush_do_autokey(bContext *C, tPoseSculptingOp *pso)
{
	BLI_assert(pso->ks != NULL);
	
	if (pso->ks && pso->ks_sources.first) {
		Scene *scene = pso->scene;
		Object *ob = pso->ob;
		
		/* insert keyframes for all relevant bones in one go */
		ANIM_apply_keyingset(C, &pso->ks_sources, NULL, pso->ks, MODIFYKEY_MODE_INSERT, CFRA);
		BLI_freelistN(&pso->ks_sources);
		
		/* do the bone paths
		 *	- only do this if keyframes should have been added
		 *	- do not calculate unless there are paths already to update...
		 */
		if (ob->pose->avs.path_bakeflag & MOTIONPATH_BAKE_HAS_PATHS) {
			//ED_pose_clear_paths(C, ob); // XXX for now, don't need to clear
			ED_pose_recalculate_paths(scene, ob);
		}
	}
}

/* Apply brush callback on bones which fall within the brush region 
 * Based on method pose_circle_select() in view3d_select.c
 */
static bool psculpt_brush_do_apply(tPoseSculptingOp *pso, tPSculptContext *data, PSculptBrushCallback brush_cb)
{
	PSculptSettings *pset = psculpt_settings(pso->scene);
	ViewContext *vc = &data->vc;
	Object *ob = data->ob;
	bArmature *arm = ob->data;
	bPose *pose = ob->pose;
	bPoseChannel *pchan;
	bool changed = false;
	
	ED_view3d_init_mats_rv3d(vc->obact, vc->rv3d); /* for foreach's screen/vert projection */
	
	/* check each PoseChannel... */
	// TODO: could be optimised at some point
	for (pchan = pose->chanbase.first; pchan; pchan = pchan->next) {
		eV3DProjStatus ps1, ps2;
		float sco1[2], sco2[2];
		float vec[3];
		bool ok = false;
		
		/* skip invisible bones */
		if (PBONE_VISIBLE(arm, pchan->bone) == 0)
			continue;
			
		/* only affect selected bones? */
		if ((pset->flag & PSCULPT_FLAG_SELECT_MASK) && 
		    (pset->brushtype != PSCULPT_BRUSH_SELECT)) 
		{
			if ((pchan->bone) && !(pchan->bone->flag & BONE_SELECTED))
				continue;
		}
		
		/* project head location to screenspace */
		mul_v3_m4v3(vec, vc->obact->obmat, pchan->pose_head);
		ps1 = ED_view3d_project_float_global(vc->ar, vec, sco1, V3D_PROJ_TEST_CLIP_BB | V3D_PROJ_TEST_CLIP_WIN);
		
		/* project tail location to screenspace */
		mul_v3_m4v3(vec, vc->obact->obmat, pchan->pose_tail);
		ps2 = ED_view3d_project_float_global(vc->ar, vec, sco2, V3D_PROJ_TEST_CLIP_BB | V3D_PROJ_TEST_CLIP_WIN);
		
		/* outright skip any joints which occur off-screen
		 * NOTE: edge_inside_circle doesn't check for these cases, and ends up
		 * making mirror-bones partially out of view getting activated
		 */
		if ((ps1 != V3D_PROJ_RET_OK) || (ps2 != V3D_PROJ_RET_OK)) {
			continue;
		}
		
		/* Check if this is already in the cache for a brush that just wants to affect those initially captured;
		 * If that's the case, we should just continue to affect it
		 */
		else if ((data->brush->flag & PSCULPT_BRUSH_FLAG_GRAB_INITIAL) && 
				 (data->is_first == false) && 
				 (verify_bone_is_affected(pso, data, pchan, false) != NULL))
		{
			ok = true;
		}
		/* Otherwise, check if the head and/or tail is in the circle 
		 *	- the call to check also does the selection already
		 */
		// FIXME: this method FAILS on custom bones shapes. Can be quite bad sometimes with production rigs!
		else if (edge_inside_circle(data->mval, data->rad, sco1, sco2)) {
			ok = true;
		}
		
		/* act on bone? */
		if (ok) {
			float mid[2];
			
			/* set distance from cursor to bone - taken as midpoint of bone */
			mid_v2_v2v2(mid, sco1, sco2);
			data->dist = len_v2v2(mid, data->mval);
			
			/* apply callback to this bone */
			brush_cb(pso, data, pchan, sco1, sco2);
			
			/* schedule this bone up for being keyframed (if autokeying is enabled) */
			if (pso->ks) {
				ANIM_relative_keyingset_add_source(&pso->ks_sources, &ob->id, &RNA_PoseBone, pchan); 
				if (pchan->bone) pchan->bone->flag &= ~BONE_UNKEYED;
			}
			else {
				if (pchan->bone) pchan->bone->flag |= BONE_UNKEYED;
			}
			
			/* tag as changed */
			// TODO: add to autokeying cache...
			changed |= true;
		}
	}
	
	return changed;
}

/* Calculate settings for applying brush */
static void psculpt_brush_apply(bContext *C, wmOperator *op, PointerRNA *itemptr)
{
	tPoseSculptingOp *pso = op->customdata;
	
	Scene *scene = pso->scene;
	Object *ob = pso->ob;
	
	int mouse[2];
	float mousef[2];
	float dx, dy;
	
	/* get latest mouse coordinates */
	RNA_float_get_array(itemptr, "mouse", mousef);
	mouse[0] = (int)mousef[0];
	mouse[1] = (int)mousef[1];
	
	if (RNA_boolean_get(itemptr, "pen_flip"))
		pso->data.invert = true;
	
	/* store coordinates as reference, if operator just started running */
	if (pso->is_first) {
		pso->lastmouse[0] = mouse[0];
		pso->lastmouse[1] = mouse[1];
	}
	
	/* get distance moved */
	dx = (float)(mouse[0] - pso->lastmouse[0]);
	dy = (float)(mouse[1] - pso->lastmouse[1]);
	
	/* only apply brush if mouse moved, or if this is the first run, or if the timer ticked */
	if (((dx != 0.0f) || (dy != 0.0f)) || (pso->is_first) || (pso->is_timer_tick)) 
	{
		PSculptSettings *pset = psculpt_settings(scene);
		PSculptBrushData *brush = psculpt_get_brush(scene);
		ARegion *ar = CTX_wm_region(C);
		
		View3D *v3d = CTX_wm_view3d(C);
		RegionView3D *rv3d = CTX_wm_region_view3d(C);
		float *rvec, zfac;
		
		tPSculptContext data = pso->data;
		bool changed = false;
		
		/* init view3D depth buffer stuff, used for finding bones to affect */
		view3d_operator_needs_opengl(C);
		view3d_set_viewcontext(C, &pso->data.vc);
		
		rvec = ED_view3d_cursor3d_get(scene, v3d);
		zfac = ED_view3d_calc_zfac(rv3d, rvec, NULL);
		
		/* precompute object dependencies */
		invert_m4_m4(ob->imat, ob->obmat);
		
		/* set generic mouse parameters */
		// XXX: this doesn't need to happen everytime!
		data.ar = ar;
		data.v3d = v3d;
		data.rv3d = rv3d;
		
		data.mval = mousef;
		data.rad = (float)brush->size;
		data.fac = brush->strength;
		data.is_first = pso->is_first;
		
		/* apply brushes */
		switch (pset->brushtype) {
			case PSCULPT_BRUSH_DRAW: // XXX: placeholder... we need a proper "draw" brush
			case PSCULPT_BRUSH_ADJUST:
			{
				if (data.invert) {
					/* Shift = Hardcoded convenience shortcut to perform Grab */
					float delta[2] = {dx, dy};
					ED_view3d_win_to_delta(ar, delta, data.dvec, zfac);
					
					/* Hack: Clear invert flag, or else translate behaves wrong */
					data.invert = false;
					
					changed = psculpt_brush_do_apply(pso, &data, psculpt_brush_grab_apply);
				}
				else {
					/* Compute trackball effect */
					psculpt_brush_calc_trackball(pso, &data);
					
					/* Apply trackball transform to bones... */
					// TODO: if no bones affected, fall back to the ones last affected (as we may have slipped off into space)
					changed = psculpt_brush_do_apply(pso, &data, psculpt_brush_adjust_apply);
				}
				
				break;
			}
				
			case PSCULPT_BRUSH_SMOOTH:
			{
				// XXX: placeholder
				changed = psculpt_brush_do_apply(pso, &data, psculpt_brush_smooth_apply);
				
				break;
			}
				
			case PSCULPT_BRUSH_GRAB:
			{
				float delta[2] = {dx, dy};
				ED_view3d_win_to_delta(ar, delta, data.dvec, zfac);
				
				changed = psculpt_brush_do_apply(pso, &data, psculpt_brush_grab_apply);
				
				break;
			}
			
			case PSCULPT_BRUSH_CURL:
			{
				changed = psculpt_brush_do_apply(pso, &data, psculpt_brush_curl_apply);
				break;
			}
			
			case PSCULPT_BRUSH_STRETCH:
			{
				changed = psculpt_brush_do_apply(pso, &data, psculpt_brush_stretch_apply);
				break;
			}
			
			case PSCULPT_BRUSH_TWIST:
			{
				changed = psculpt_brush_do_apply(pso, &data, psculpt_brush_twist_apply);
				break;
			}
			
			case PSCULPT_BRUSH_RADIAL:
			{
				// XXX: placeholder
				changed = psculpt_brush_do_apply(pso, &data, psculpt_brush_radial_apply);
				
				break;
			}
			
			case PSCULPT_BRUSH_WRAP:
			{
				// XXX: placeholder
				changed = psculpt_brush_do_apply(pso, &data, psculpt_brush_wrap_apply);
				
				break;
			}
			
			case PSCULPT_BRUSH_RESET:
			{
				changed = psculpt_brush_do_apply(pso, &data, psculpt_brush_reset_apply);
				break;
			}
			
			case PSCULPT_BRUSH_SELECT:
			{
				bArmature *arm = (bArmature *)ob->data;
				bool sel_changed = false;
				
				/* no need for recalc, unless some visualisation tools depend on this 
				 * (i.e. mask modifier in 'armature' mode) 
				 */
				sel_changed = psculpt_brush_do_apply(pso, &data, psculpt_brush_select_apply);
				changed = ((sel_changed) && (arm->flag & ARM_HAS_VIZ_DEPS));
				
				break;
			}
			
			default:
				printf("Pose Sculpt: Unknown brush type %d\n", pset->brushtype);
				break;
		}
		
		/* flush updates */
		if (changed) {
			bArmature *arm = (bArmature *)ob->data;
			
			/* perform autokeying first */
			// XXX: order?
			psculpt_brush_do_autokey(C, pso);
			
			/* old optimize trick... this enforces to bypass the depgraph 
			 *	- note: code copied from transform_generics.c -> recalcData()
			 */
			// FIXME: shouldn't this use the builtin stuff?
			if ((arm->flag & ARM_DELAYDEFORM) == 0)
				DAG_id_tag_update(&ob->id, OB_RECALC_DATA);  /* sets recalc flags */
			else
				BKE_pose_where_is(scene, ob);
		}
		
		/* cleanup and send updates */
		WM_event_add_notifier(C, NC_OBJECT | ND_POSE | NA_EDITED, ob);
		
		pso->lastmouse[0] = mouse[0];
		pso->lastmouse[1] = mouse[1];
		pso->is_first = false;
	}
}

/* Running --------------------------------------------- */

/* helper - a record stroke, and apply paint event */
static void psculpt_brush_apply_event(bContext *C, wmOperator *op, const wmEvent *event)
{
	PointerRNA itemptr;
	float mouse[2];
	
	/* add a new entry in the stroke-elements collection */
	RNA_collection_add(op->ptr, "stroke", &itemptr);
	
	/* fill in current mouse coordinates */
	VECCOPY2D(mouse, event->mval);
	RNA_float_set_array(&itemptr, "mouse", mouse);
	
	/* handle pressure sensitivity (which is supplied by tablets) */
	if (event->tablet_data) {
		const wmTabletData *wmtab = event->tablet_data;
		float pressure = wmtab->Pressure;
		bool tablet = (wmtab->Active != EVT_TABLET_NONE);
		
		/* special exception here for too high pressure values on first touch in
		 * windows for some tablets: clamp the values to be sane
		 */
		if (tablet && (pressure >= 0.99f)) {
			pressure = 1.0f;
		}		
		RNA_float_set(&itemptr, "pressure", pressure);
		
		/* "pen_flip" is meant to be attached to the eraser */
		if (wmtab->Active == EVT_TABLET_ERASER)
			RNA_boolean_set(&itemptr, "pen_flip", event->shift == false);
		else
			RNA_boolean_set(&itemptr, "pen_flip", event->shift != false);
	}
	else {
		RNA_float_set(&itemptr, "pressure", 1.0f);
		RNA_boolean_set(&itemptr, "pen_flip", event->shift != false);
	}
	
	/* apply */
	psculpt_brush_apply(C, op, &itemptr);
}

/* reapply */
static int psculpt_brush_exec(bContext *C, wmOperator *op)
{
	if (!psculpt_brush_init(C, op))
		return OPERATOR_CANCELLED;
	
	RNA_BEGIN(op->ptr, itemptr, "stroke") 
	{
		psculpt_brush_apply(C, op, &itemptr);
	}
	RNA_END;
	
	psculpt_brush_exit(C, op);
	
	return OPERATOR_FINISHED;
}


/* start modal painting */
static int psculpt_brush_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{	
	Scene *scene = CTX_data_scene(C);
	
	PSculptSettings *pset = psculpt_settings(scene);
	tPoseSculptingOp *pso = NULL;
	
	/* init painting data */
	if (!psculpt_brush_init(C, op))
		return OPERATOR_CANCELLED;
	
	pso = op->customdata;
	
	/* do initial "click" apply */
	psculpt_brush_apply_event(C, op, event);
	
	/* register timer for increasing influence by hovering over an area */
	if (ELEM(pset->brushtype, PSCULPT_BRUSH_CURL, PSCULPT_BRUSH_STRETCH))
	{
		PSculptBrushData *brush = psculpt_get_brush(scene);
		pso->timer = WM_event_add_timer(CTX_wm_manager(C), CTX_wm_window(C), TIMER, brush->rate);
	}
	
	/* register modal handler */
	WM_event_add_modal_handler(C, op);
	
	return OPERATOR_RUNNING_MODAL;
}

/* painting - handle events */
static int psculpt_brush_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
	tPoseSculptingOp *pso = op->customdata;
	
	switch (event->type) {
		/* mouse release or some other mbut click = abort! */
		case LEFTMOUSE:
		case MIDDLEMOUSE:
		case RIGHTMOUSE:
			psculpt_brush_exit(C, op);
			return OPERATOR_FINISHED;
			
		/* timer tick - only if this was our own timer */
		case TIMER:
			if (event->customdata == pso->timer) {
				pso->is_timer_tick = true;
				psculpt_brush_apply_event(C, op, event);
				pso->is_timer_tick = false;
			}
			break;
			
		/* mouse move = apply somewhere else */
		case MOUSEMOVE:
		case INBETWEEN_MOUSEMOVE:
			psculpt_brush_apply_event(C, op, event);
			break;
	}
	
	return OPERATOR_RUNNING_MODAL;
}

/* Operator --------------------------------------------- */

void POSE_OT_brush_paint(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Pose Sculpt";
	ot->idname = "POSE_OT_brush_paint";
	ot->description = "Pose sculpting paint brush";
	
	/* api callbacks */
	ot->exec = psculpt_brush_exec;
	ot->invoke = psculpt_brush_invoke;
	ot->modal = psculpt_brush_modal;
	ot->cancel = psculpt_brush_exit;
	ot->poll = psculpt_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO | OPTYPE_BLOCKING;

	/* properties */
	RNA_def_collection_runtime(ot->srna, "stroke", &RNA_OperatorStrokeElement, "Stroke", "");
	RNA_def_boolean(ot->srna, "invert", false, "Invert Brush Action", "Override brush direction to apply inverse operation");
}

/* ******************************************************** */
