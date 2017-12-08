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
 * Contributor(s): Joshua Leung, Antonio Vazquez
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/gpencil.c
 *  \ingroup bke
 */

 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_math_vector.h"
#include "BLI_math_color.h"
#include "BLI_string_utils.h"
#include "BLI_rand.h"
#include "BLI_ghash.h"

#include "BLT_translation.h"

#include "DNA_anim_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_userdef_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"

#include "BKE_context.h"
#include "BKE_action.h"
#include "BKE_animsys.h"
#include "BKE_global.h"
#include "BKE_gpencil.h"
#include "BKE_colortools.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_object.h"
#include "BKE_paint.h"

/* ************************************************** */
/* Draw Engine */

void(*BKE_gpencil_batch_cache_dirty_cb)(bGPdata *gpd) = NULL;
void(*BKE_gpencil_batch_cache_free_cb)(bGPdata *gpd) = NULL;

void BKE_gpencil_batch_cache_dirty(bGPdata *gpd)
{
	if (gpd) {
		BKE_gpencil_batch_cache_dirty_cb(gpd);
	}
}

void BKE_gpencil_batch_cache_free(bGPdata *gpd)
{
	if (gpd) {
		BKE_gpencil_batch_cache_free_cb(gpd);
	}
}

/* Change draw manager status in all gpd datablocks */
/* TODO: Remove completely? */
void BKE_gpencil_batch_cache_alldirty_main(Main *bmain)
{
	/* XXX: disabled by default, so we can see how much is broken without this */
	if (G.debug_value == 66) {
		bGPdata *gpd;

		for (gpd = bmain->gpencil.first; gpd; gpd = gpd->id.next) {
			BKE_gpencil_batch_cache_dirty(gpd);
		}
	}
}

/* ************************************************** */
/* Memory Management */

/* clean vertex groups weights */
void BKE_gpencil_free_point_weights(bGPDspoint *pt)
{
	if (pt == NULL) {
		return;
	}
	MEM_SAFE_FREE(pt->weights);
}

void BKE_gpencil_free_stroke_weights(bGPDstroke *gps)
{
	if (gps == NULL) {
		return;
	}
	for (int i = 0; i < gps->totpoints; ++i) {
		bGPDspoint *pt = &gps->points[i];
		BKE_gpencil_free_point_weights(pt);
	}
}

/* free stroke, doesn't unlink from any listbase */
void BKE_gpencil_free_stroke(bGPDstroke *gps)
{
	if (gps == NULL) {
		return;
	}
	/* free stroke memory arrays, then stroke itself */
	if (gps->points) {
		BKE_gpencil_free_stroke_weights(gps);
		MEM_freeN(gps->points);
	}
	if (gps->triangles)
		MEM_freeN(gps->triangles);

	MEM_freeN(gps);
}

/* Free strokes belonging to a gp-frame */
bool BKE_gpencil_free_strokes(bGPDframe *gpf)
{
	bGPDstroke *gps_next;
	bool changed = (BLI_listbase_is_empty(&gpf->strokes) == false);

	/* free strokes */
	for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps_next) {
		gps_next = gps->next;
		BKE_gpencil_free_stroke(gps);
	}
	BLI_listbase_clear(&gpf->strokes);

	return changed;
}

/* Free strokes and colors belonging to a gp-frame */
bool BKE_gpencil_free_layer_temp_data(bGPDlayer *UNUSED(gpl), bGPDframe *derived_gpf)
{
	bGPDstroke *gps_next;
	if (!derived_gpf) {
		return false;
	}

	/* free strokes */
	for (bGPDstroke *gps = derived_gpf->strokes.first; gps; gps = gps_next) {
		gps_next = gps->next;
		MEM_SAFE_FREE(gps->palcolor);
		BKE_gpencil_free_stroke(gps);
	}
	BLI_listbase_clear(&derived_gpf->strokes);

	MEM_SAFE_FREE(derived_gpf);

	return true;
}

/* Free all of a gp-layer's frames */
void BKE_gpencil_free_frames(bGPDlayer *gpl)
{
	bGPDframe *gpf_next;
	
	/* error checking */
	if (gpl == NULL) return;
	
	/* free frames */
	for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf_next) {
		gpf_next = gpf->next;
		
		/* free strokes and their associated memory */
		BKE_gpencil_free_strokes(gpf);
		BLI_freelinkN(&gpl->frames, gpf);
	}
	gpl->actframe = NULL;
}



/* Free all of the gp-brushes for a viewport (list should be &gpd->brushes or so) */
void BKE_gpencil_free_brushes(ListBase *list)
{
	bGPDbrush *brush_next;

	/* error checking */
	if (list == NULL) {
		return;
	}

	/* delete brushes */
	for (bGPDbrush *brush = list->first; brush; brush = brush_next) {
		brush_next = brush->next;
		/* free curves */
		if (brush->cur_sensitivity) {
			curvemapping_free(brush->cur_sensitivity);
		}
		if (brush->cur_strength) {
			curvemapping_free(brush->cur_strength);
		}
		if (brush->cur_jitter) {
			curvemapping_free(brush->cur_jitter);
		}

		MEM_freeN(brush);
	}
	BLI_listbase_clear(list);
}

/* Free all of the gp-layers for a viewport (list should be &gpd->layers or so) */
void BKE_gpencil_free_layers(ListBase *list)
{
	bGPDlayer *gpl_next;

	/* error checking */
	if (list == NULL) return;

	/* delete layers */
	for (bGPDlayer *gpl = list->first; gpl; gpl = gpl_next) {
		gpl_next = gpl->next;
		
		/* free layers and their data */
		BKE_gpencil_free_frames(gpl);
		BLI_freelinkN(list, gpl);
	}
}

/* Free palette slot
 * NOTE: This doesn't unlink the palette from any strokes that may be using it
 */
void BKE_gpencil_palette_slot_free(bGPdata *gpd, bGPDpaletteref *palslot)
{
	if (ELEM(NULL, gpd, palslot))
		return;
	
	/* unlink palette */
	if (palslot->palette) {
		id_us_min(&palslot->palette->id);
	}
	
	/* free slot */
	BLI_freelinkN(&gpd->palette_slots, palslot);
}

/* Free palette slots */
static void BKE_gpencil_free_paletteslots(bGPdata *gpd)
{
	bGPDpaletteref *palslot, *gpr_next;
	
	/* error checking */
	if (gpd == NULL) return;
	
	/* delete palette slots */
	for (palslot = gpd->palette_slots.first; palslot; palslot = gpr_next) {
		gpr_next = palslot->next;
		
		BKE_gpencil_palette_slot_free(gpd, palslot);
	}
}

/* clear all runtime derived data */
static void BKE_gpencil_clear_derived(bGPDlayer *gpl)
{
	GHashIterator gh_iter;
	
	if (gpl->derived_data == NULL) {
		return;
	}
	
	GHASH_ITER(gh_iter, gpl->derived_data) {
		bGPDframe *gpf = (bGPDframe *)BLI_ghashIterator_getValue(&gh_iter);
		if (gpf) {
			BKE_gpencil_free_layer_temp_data(gpl, gpf);
		}
	}
}

/* Free all of the gp-layers temp data*/
static void BKE_gpencil_free_layers_temp_data(ListBase *list)
{
	bGPDlayer *gpl_next;

	/* error checking */
	if (list == NULL) return;
	/* delete layers */
	for (bGPDlayer *gpl = list->first; gpl; gpl = gpl_next) {
		gpl_next = gpl->next;
		BKE_gpencil_clear_derived(gpl);

		if (gpl->derived_data) {
			BLI_ghash_free(gpl->derived_data, NULL, NULL);
			gpl->derived_data = NULL;
		}
	}
}

/* Free temp gpf derived frames */
void BKE_gpencil_free_derived_frames(bGPdata *gpd)
{
	/* error checking */
	if (gpd == NULL) return;
	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		BKE_gpencil_clear_derived(gpl);

		if (gpl->derived_data) {
			BLI_ghash_free(gpl->derived_data, NULL, NULL);
			gpl->derived_data = NULL;
		}
	}
}

/** Free (or release) any data used by this grease pencil (does not free the gpencil itself). */
void BKE_gpencil_free(bGPdata *gpd, bool free_all)
{
	/* clear animation data */
	BKE_animdata_free(&gpd->id, false);

	/* free layers */
	if (free_all) {
		BKE_gpencil_free_layers_temp_data(&gpd->layers);
	}
	BKE_gpencil_free_layers(&gpd->layers);

	/* free palette slots */
	BKE_gpencil_free_paletteslots(gpd);

	/* free all data */
	if (free_all) {
		/* clear cache */
		BKE_gpencil_batch_cache_free(gpd);
		
		/* free palettes (deprecated) */
		BKE_gpencil_free_palettes(&gpd->palettes);
	}
}

/* ************************************************** */
/* Container Creation */

/* add a new gp-frame to the given layer */
bGPDframe *BKE_gpencil_frame_addnew(bGPDlayer *gpl, int cframe)
{
	bGPDframe *gpf = NULL, *gf = NULL;
	short state = 0;
	
	/* error checking */
	if (gpl == NULL)
		return NULL;
		
	/* allocate memory for this frame */
	gpf = MEM_callocN(sizeof(bGPDframe), "bGPDframe");
	gpf->framenum = cframe;
	
	/* find appropriate place to add frame */
	if (gpl->frames.first) {
		for (gf = gpl->frames.first; gf; gf = gf->next) {
			/* check if frame matches one that is supposed to be added */
			if (gf->framenum == cframe) {
				state = -1;
				break;
			}
			
			/* if current frame has already exceeded the frame to add, add before */
			if (gf->framenum > cframe) {
				BLI_insertlinkbefore(&gpl->frames, gf, gpf);
				state = 1;
				break;
			}
		}
	}
	
	/* check whether frame was added successfully */
	if (state == -1) {
		printf("Error: Frame (%d) existed already for this layer. Using existing frame\n", cframe);
		
		/* free the newly created one, and use the old one instead */
		MEM_freeN(gpf);
		
		/* return existing frame instead... */
		BLI_assert(gf != NULL);
		gpf = gf;
	}
	else if (state == 0) {
		/* add to end then! */
		BLI_addtail(&gpl->frames, gpf);
	}
	
	/* return frame */
	return gpf;
}

/* add a copy of the active gp-frame to the given layer */
bGPDframe *BKE_gpencil_frame_addcopy(bGPDlayer *gpl, int cframe)
{
	bGPDframe *new_frame;
	bool found = false;
	
	/* Error checking/handling */
	if (gpl == NULL) {
		/* no layer */
		return NULL;
	}
	else if (gpl->actframe == NULL) {
		/* no active frame, so just create a new one from scratch */
		return BKE_gpencil_frame_addnew(gpl, cframe);
	}
	
	/* Create a copy of the frame */
	new_frame = BKE_gpencil_frame_duplicate(gpl->actframe);
	
	/* Find frame to insert it before */
	for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
		if (gpf->framenum > cframe) {
			/* Add it here */
			BLI_insertlinkbefore(&gpl->frames, gpf, new_frame);
			
			found = true;
			break;
		}
		else if (gpf->framenum == cframe) {
			/* This only happens when we're editing with framelock on...
			 * - Delete the new frame and don't do anything else here...
			 */
			BKE_gpencil_free_strokes(new_frame);
			MEM_freeN(new_frame);
			new_frame = NULL;
			
			found = true;
			break;
		}
	}
	
	if (found == false) {
		/* Add new frame to the end */
		BLI_addtail(&gpl->frames, new_frame);
	}
	
	/* Ensure that frame is set up correctly, and return it */
	if (new_frame) {
		new_frame->framenum = cframe;
		gpl->actframe = new_frame;
	}
	
	return new_frame;
}

/* add a new gp-layer and make it the active layer */
bGPDlayer *BKE_gpencil_layer_addnew(bGPdata *gpd, const char *name, bool setactive)
{
	bGPDlayer *gpl;
	
	/* check that list is ok */
	if (gpd == NULL)
		return NULL;
		
	/* allocate memory for frame and add to end of list */
	gpl = MEM_callocN(sizeof(bGPDlayer), "bGPDlayer");
	
	/* add to datablock */
	BLI_addtail(&gpd->layers, gpl);
	
	/* set basic settings */
	copy_v4_v4(gpl->color, U.gpencil_new_layer_col);
	/* Since GPv2 thickness must be 0 */
	gpl->thickness = 0;

	gpl->opacity = 1.0f;

	/* onion-skinning settings */
	gpl->onion_flag |= GP_LAYER_ONIONSKIN;
	gpl->onion_flag |= (GP_LAYER_GHOST_PREVCOL | GP_LAYER_GHOST_NEXTCOL);
	gpl->onion_flag |= GP_LAYER_ONION_FADE;
	gpl->onion_factor = 0.5f;
	gpl->gstep = 1;
	gpl->gstep_next = 1;

	ARRAY_SET_ITEMS(gpl->gcolor_prev, 0.145098f, 0.419608f, 0.137255f); /* green */
	ARRAY_SET_ITEMS(gpl->gcolor_next, 0.125490f, 0.082353f, 0.529412f); /* blue */

	/* auto-name */
	BLI_strncpy(gpl->info, name, sizeof(gpl->info));
	BLI_uniquename(&gpd->layers, gpl, DATA_("GP_Layer"), '.', offsetof(bGPDlayer, info), sizeof(gpl->info));
	
	/* make this one the active one */
	if (setactive)
		BKE_gpencil_layer_setactive(gpd, gpl);
	
	/* return layer */
	return gpl;
}

/* cumapping->preset */
typedef enum eGPCurveMappingPreset {
	GPCURVE_PRESET_PENCIL = 0,
	GPCURVE_PRESET_INK = 1,
	GPCURVE_PRESET_INKNOISE = 2,
} eGPCurveMappingPreset;

static void gp_brush_curvemap_reset(CurveMap *cuma, int preset)
{
	if (cuma->curve)
		MEM_freeN(cuma->curve);

	cuma->totpoint = 3;
	cuma->curve = MEM_callocN(cuma->totpoint * sizeof(CurveMapPoint), "curve points");

	switch (preset) {
		case GPCURVE_PRESET_PENCIL:
			cuma->curve[0].x = 0.0f;
			cuma->curve[0].y = 0.0f;
			cuma->curve[1].x = 0.75115f;
			cuma->curve[1].y = 0.25f;
			cuma->curve[2].x = 1.0f;
			cuma->curve[2].y = 1.0f;
			break;
		case GPCURVE_PRESET_INK:
			cuma->curve[0].x = 0.0f;
			cuma->curve[0].y = 0.0f;
			cuma->curve[1].x = 0.63448f;
			cuma->curve[1].y = 0.375f;
			cuma->curve[2].x = 1.0f;
			cuma->curve[2].y = 1.0f;
			break;
		case GPCURVE_PRESET_INKNOISE:
			cuma->curve[0].x = 0.0f;
			cuma->curve[0].y = 0.0f;
			cuma->curve[1].x = 0.63134f;
			cuma->curve[1].y = 0.3625f;
			cuma->curve[2].x = 1.0f;
			cuma->curve[2].y = 1.0f;
			break;
	}

	if (cuma->table) {
		MEM_freeN(cuma->table);
		cuma->table = NULL;
	}
}

/* create a set of default drawing brushes with predefined presets */
void BKE_gpencil_brush_init_presets(ToolSettings *ts)
{
	bGPDbrush *brush;
	CurveMapping *custom_curve;
	float curcolor[3] = { 1.0f, 1.0f, 1.0f };

	/* Pencil brush */
	brush = BKE_gpencil_brush_addnew(ts, "Pencil", false);
	brush->thickness = 25.0f;
	brush->flag |= (GP_BRUSH_USE_RANDOM_PRESSURE | GP_BRUSH_USE_PRESSURE | GP_BRUSH_ENABLE_CURSOR);
	brush->draw_sensitivity = 1.0f;

	brush->flag |= GP_BRUSH_USE_RANDOM_STRENGTH;
	brush->draw_strength = 0.6f;
	brush->flag |= GP_BRUSH_USE_STENGTH_PRESSURE;

	brush->draw_random_press = 0.0f;

	brush->draw_jitter = 0.0f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	brush->draw_angle = 0.0f;
	brush->draw_angle_factor = 0.0f;

	brush->draw_smoothfac = 1.0f;
	brush->draw_smoothlvl = 3;
	brush->thick_smoothfac = 1.0f;
	brush->thick_smoothlvl = 3;
	brush->sublevel = 1;
	brush->draw_random_sub = 0.0f;
	copy_v3_v3(brush->curcolor, curcolor);
	
	/* Curve */
	custom_curve = brush->cur_strength;
	curvemapping_set_defaults(custom_curve, 1, 0.0f, 0.0f, 1.0f, 1.0f);
	curvemapping_initialize(custom_curve);
	gp_brush_curvemap_reset(custom_curve->cm, GPCURVE_PRESET_PENCIL);

	/* Pen brush */
	brush = BKE_gpencil_brush_addnew(ts, "Pen", true);
	brush->thickness = 30.0f;
	brush->flag |= (GP_BRUSH_USE_RANDOM_PRESSURE | GP_BRUSH_USE_PRESSURE | GP_BRUSH_ENABLE_CURSOR);
	brush->draw_sensitivity = 1.0f;

	brush->flag |= GP_BRUSH_USE_RANDOM_STRENGTH;
	brush->draw_strength = 1.0f;
	brush->flag |= GP_BRUSH_USE_STENGTH_PRESSURE;

	brush->draw_random_press = 0.0f;

	brush->draw_jitter = 0.0f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	brush->draw_angle = 0.0f;
	brush->draw_angle_factor = 0.0f;

	brush->draw_smoothfac = 1.0f;
	brush->draw_smoothlvl = 3;
	brush->sublevel = 1;
	brush->thick_smoothfac = 1.0f;
	brush->thick_smoothlvl = 3;
	brush->draw_random_sub = 0.0f;
	copy_v3_v3(brush->curcolor, curcolor);

	/* Ink brush */
	brush = BKE_gpencil_brush_addnew(ts, "Ink", true);
	brush->thickness = 60.0f;
	brush->flag |= (GP_BRUSH_USE_PRESSURE | GP_BRUSH_ENABLE_CURSOR);
	brush->draw_sensitivity = 1.6f;

	brush->draw_strength = 1.0f;

	brush->draw_random_press = 0.0f;

	brush->draw_jitter = 0.0f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	brush->draw_angle = 0.0f;
	brush->draw_angle_factor = 0.0f;

	brush->draw_smoothfac = 1.0f;
	brush->draw_smoothlvl = 3;
	brush->thick_smoothfac = 1.0f;
	brush->thick_smoothlvl = 3;
	brush->sublevel = 1;
	brush->draw_random_sub = 0.0f;
	copy_v3_v3(brush->curcolor, curcolor);

	/* Curve */
	custom_curve = brush->cur_sensitivity;
	curvemapping_set_defaults(custom_curve, 1, 0.0f, 0.0f, 1.0f, 1.0f);
	curvemapping_initialize(custom_curve);
	gp_brush_curvemap_reset(custom_curve->cm, GPCURVE_PRESET_INK);

	/* Ink Noise brush */
	brush = BKE_gpencil_brush_addnew(ts, "Ink noise", false);
	brush->thickness = 60.0f;
	brush->flag |= (GP_BRUSH_USE_RANDOM_PRESSURE | GP_BRUSH_USE_PRESSURE | GP_BRUSH_ENABLE_CURSOR);
	brush->draw_sensitivity = 1.0f;

	brush->draw_strength = 1.0f;

	brush->draw_random_press = 0.7f;

	brush->draw_jitter = 0.0f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	brush->draw_angle = 0.0f;
	brush->draw_angle_factor = 0.0f;

	brush->draw_smoothfac = 1.0f;
	brush->draw_smoothlvl = 2;
	brush->thick_smoothfac = 0.5f;
	brush->thick_smoothlvl = 2;
	brush->sublevel = 1;
	brush->draw_random_sub = 0.0f;
	copy_v3_v3(brush->curcolor, curcolor);

	/* Curve */
	custom_curve = brush->cur_sensitivity;
	curvemapping_set_defaults(custom_curve, 1, 0.0f, 0.0f, 1.0f, 1.0f);
	curvemapping_initialize(custom_curve);
	gp_brush_curvemap_reset(custom_curve->cm, GPCURVE_PRESET_INKNOISE);

	/* Block Basic brush */
	brush = BKE_gpencil_brush_addnew(ts, "Block Basic", false);
	brush->thickness = 150.0f;
	brush->flag |= (GP_BRUSH_USE_PRESSURE | GP_BRUSH_ENABLE_CURSOR);
	brush->draw_sensitivity = 1.0f;

	brush->draw_strength = 0.7f;
	brush->flag |= GP_BRUSH_USE_STENGTH_PRESSURE;

	brush->draw_random_press = 0.0f;

	brush->draw_jitter = 0.0f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	brush->draw_angle = 0.0f;
	brush->draw_angle_factor = 0.0f;

	brush->draw_smoothfac = 0.0f;
	brush->draw_smoothlvl = 1;
	brush->thick_smoothfac = 1.0f;
	brush->thick_smoothlvl = 3;
	brush->sublevel = 0;
	brush->draw_random_sub = 0;
	copy_v3_v3(brush->curcolor, curcolor);

	/* Marker brush */
	brush = BKE_gpencil_brush_addnew(ts, "Marker", false);
	brush->thickness = 80.0f;
	brush->flag |= (GP_BRUSH_USE_RANDOM_PRESSURE | GP_BRUSH_USE_PRESSURE | GP_BRUSH_ENABLE_CURSOR);
	brush->draw_sensitivity = 1.0f;

	brush->draw_strength = 1.0f;

	brush->draw_random_press = 0.374f;

	brush->draw_jitter = 0.0f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	brush->draw_angle = M_PI_4; /* 45 degrees */
	brush->draw_angle_factor = 1.0f;

	brush->draw_smoothfac = 1.0f;
	brush->draw_smoothlvl = 3;
	brush->thick_smoothfac = 1.0f;
	brush->thick_smoothlvl = 3;
	brush->sublevel = 1;
	brush->draw_random_sub = 0.0f;
	copy_v3_v3(brush->curcolor, curcolor);

	/* Experimental brush */
	brush = BKE_gpencil_brush_addnew(ts, "Experimental", false);
	brush->thickness = 80.0f;
	brush->flag |= (GP_BRUSH_USE_PRESSURE | GP_BRUSH_ENABLE_CURSOR);
	brush->draw_sensitivity = 1.0f;

	brush->draw_strength = 0.485f;
	brush->flag |= GP_BRUSH_USE_STENGTH_PRESSURE;

	brush->draw_random_press = 0.0f;

	brush->draw_jitter = 0.10f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	brush->draw_angle = 0.0f;
	brush->draw_angle_factor = 0.0f;

	brush->draw_smoothfac = 1.3f;
	brush->draw_smoothlvl = 1;
	brush->thick_smoothfac = 0.0f;
	brush->thick_smoothlvl = 0;
	brush->sublevel = 3;
	brush->draw_random_sub = 0;
	copy_v3_v3(brush->curcolor, curcolor);
}

/* add a new gp-brush and make it the active */
bGPDbrush *BKE_gpencil_brush_addnew(ToolSettings *ts, const char *name, bool setactive)
{
	bGPDbrush *brush;

	/* check that list is ok */
	if (ts == NULL) {
		return NULL;
	}

	/* allocate memory and add to end of list */
	brush = MEM_callocN(sizeof(bGPDbrush), "bGPDbrush");

	/* add to datablock */
	BLI_addtail(&ts->gp_brushes, brush);

	/* set basic settings */
	brush->thickness = 3;
	brush->draw_smoothlvl = 1;
	brush->flag = 0;
	brush->flag |= GP_BRUSH_USE_PRESSURE;
	brush->draw_sensitivity = 1.0f;
	brush->draw_strength = 1.0f;
	brush->draw_jitter = 0.0f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	/* curves */
	brush->cur_sensitivity = curvemapping_add(1, 0.0f, 0.0f, 1.0f, 1.0f);
	brush->cur_strength = curvemapping_add(1, 0.0f, 0.0f, 1.0f, 1.0f);
	brush->cur_jitter = curvemapping_add(1, 0.0f, 0.0f, 1.0f, 1.0f);

	/* auto-name */
	BLI_strncpy(brush->info, name, sizeof(brush->info));
	BLI_uniquename(&ts->gp_brushes, brush, DATA_("GP_Brush"), '.', offsetof(bGPDbrush, info), sizeof(brush->info));

	/* make this one the active one */
	if (setactive) {
		BKE_gpencil_brush_setactive(ts, brush);
	}

	/* return brush */
	return brush;
}

/* add a new gp-datablock */
bGPdata *BKE_gpencil_data_addnew(Main *bmain, const char name[])
{
	bGPdata *gpd;
	
	/* allocate memory for a new block */
	gpd = BKE_libblock_alloc(bmain, ID_GD, name, 0);
	
	/* initial settings */
	gpd->flag = (GP_DATA_DISPINFO | GP_DATA_EXPAND);
	
	/* general flags */
	gpd->flag |= GP_DATA_VIEWALIGN;
	
	/* GP object specific settings */
	gpd->flag |= GP_DATA_STROKE_SHOW_EDIT_LINES;
	ARRAY_SET_ITEMS(gpd->line_color, 0.6f, 0.6f, 0.6f, 0.5f);
	
	gpd->xray_mode = GP_XRAY_3DSPACE;
	gpd->batch_cache_data = NULL;
	gpd->pixfactor = GP_DEFAULT_PIX_FACTOR;
	
	/* onion-skinning settings (datablock level) */
	gpd->onion_flag |= (GP_ONION_GHOST_PREVCOL | GP_ONION_GHOST_NEXTCOL);
	gpd->onion_flag |= GP_ONION_FADE;
	gpd->onion_factor = 0.5f;
	ARRAY_SET_ITEMS(gpd->gcolor_prev, 0.145098f, 0.419608f, 0.137255f); /* green */
	ARRAY_SET_ITEMS(gpd->gcolor_next, 0.125490f, 0.082353f, 0.529412f); /* blue */
	gpd->gstep = 1;
	gpd->gstep_next = 1;

	return gpd;
}


/* ************************************************** */
/* Primitive Creation */
/* Utilities for easier bulk-creation of geometry */

/** 
 * Populate stroke with point data from data buffers
 *
 * \param array Flat array of point data values. Each entry has GP_PRIM_DATABUF_SIZE values
 */
void BKE_gpencil_stroke_add_points(bGPDstroke *gps, const float *array, const int totpoints)
{
	for (int i = 0; i < totpoints; i++) {
		bGPDspoint *pt = &gps->points[i];
		const int x = GP_PRIM_DATABUF_SIZE * i;
		
		pt->x = array[x];
		pt->y = array[x + 1];
		pt->z = array[x + 2];
		pt->pressure = array[x + 3];
		pt->strength = array[x + 4];
	}
}

/* Create a new stroke, with pre-allocated data buffers */
bGPDstroke *BKE_gpencil_add_stroke(
        bGPDframe *gpf, Palette *palette, PaletteColor *palcolor, int totpoints,
        const char *colorname, short thickness)
{
	/* allocate memory for a new stroke */
	bGPDstroke *gps = MEM_callocN(sizeof(bGPDstroke), "gp_stroke");
	
	gps->thickness = thickness * (GP_DEFAULT_PIX_FACTOR / 40);
	gps->inittime = 0;
	
	/* enable recalculation flag by default */
	gps->flag = GP_STROKE_RECALC_CACHES | GP_STROKE_3DSPACE;
	
	gps->totpoints = totpoints;
	gps->points = MEM_callocN(sizeof(bGPDspoint) * gps->totpoints, "gp_stroke_points");
	
	/* initialize triangle memory to dummy data */
	gps->triangles = MEM_callocN(sizeof(bGPDtriangle), "GP Stroke triangulation");
	gps->flag |= GP_STROKE_RECALC_CACHES;
	gps->tot_triangles = 0;
	
	gps->palette = palette;
	gps->palcolor = palcolor;
	BLI_strncpy(gps->colorname, colorname, sizeof(gps->colorname));
	
	/* add to frame */
	BLI_addtail(&gpf->strokes, gps);
	
	return gps;
}


/* ************************************************** */
/* Data Duplication */

/* make a copy of a given gpencil point weights */
void BKE_gpencil_stroke_weights_duplicate(bGPDstroke *gps_src, bGPDstroke *gps_dst)
{
	if (gps_src == NULL) {
		return;
	}
	BLI_assert(gps_src->totpoints == gps_dst->totpoints);
	for (int i = 0; i < gps_src->totpoints; ++i) {
		bGPDspoint *pt_dst = &gps_dst->points[i];
		bGPDspoint *pt_src = &gps_src->points[i];
		pt_dst->weights = MEM_dupallocN(pt_src->weights);
	}
}


/* make a copy of a given gpencil frame */
bGPDframe *BKE_gpencil_frame_duplicate(const bGPDframe *gpf_src)
{
	bGPDstroke *gps_dst;
	bGPDframe *gpf_dst;
	
	/* error checking */
	if (gpf_src == NULL) {
		return NULL;
	}
		
	/* make a copy of the source frame */
	gpf_dst = MEM_dupallocN(gpf_src);
	gpf_dst->prev = gpf_dst->next = NULL;
	
	/* copy strokes */
	BLI_listbase_clear(&gpf_dst->strokes);
	for (bGPDstroke *gps_src = gpf_src->strokes.first; gps_src; gps_src = gps_src->next) {
		/* make copy of source stroke, then adjust pointer to points too */
		gps_dst = MEM_dupallocN(gps_src);
		gps_dst->points = MEM_dupallocN(gps_src->points);
		BKE_gpencil_stroke_weights_duplicate(gps_src, gps_dst);

		gps_dst->triangles = MEM_dupallocN(gps_src->triangles);
		gps_dst->flag |= GP_STROKE_RECALC_CACHES;
		BLI_addtail(&gpf_dst->strokes, gps_dst);
	}
	
	/* return new frame */
	return gpf_dst;
}

/* fix any null value in palettes (this must be removed in the future) */
static void gpencil_fix_null_palette(const bContext *C, bGPDstroke *gps_src)
{
	bGPdata *gpd = CTX_data_gpencil_data(C);
	Palette *tmp_palette = NULL;

	tmp_palette = BKE_palette_get_active_from_context(C);
	if (!tmp_palette) {
		bGPDpaletteref *palslot;
		
		palslot = BKE_gpencil_paletteslot_addnew(CTX_data_main(C), gpd,
			                                     "Auto-Generated Palette");
		tmp_palette = palslot->palette;
	}

	gps_src->palette = tmp_palette;
	if (BKE_palette_is_empty(tmp_palette)) {
		gps_src->palcolor = BKE_palette_color_add_name(tmp_palette, gps_src->colorname);
	}
	else {
		gps_src->palcolor = BKE_palette_color_getbyname(tmp_palette, gps_src->colorname);
		if (gps_src->palcolor == NULL) {
			gps_src->palcolor = BKE_palette_color_get_active(tmp_palette);
		}
	}
}

/* make a copy of a given gpencil frame and copy colors too */
bGPDframe *BKE_gpencil_frame_color_duplicate(const bContext *C, const bGPDframe *gpf_src)
{
	bGPDstroke *gps_dst;
	bGPDframe *gpf_dst;
	
	/* error checking */
	if (gpf_src == NULL) {
		return NULL;
	}
	
	/* make a copy of the source frame */
	gpf_dst = MEM_dupallocN(gpf_src);

	/* copy strokes */
	BLI_listbase_clear(&gpf_dst->strokes);
	for (bGPDstroke *gps_src = gpf_src->strokes.first; gps_src; gps_src = gps_src->next) {
		/* make copy of source stroke */
		gps_dst = MEM_dupallocN(gps_src);
		gps_dst->points = MEM_dupallocN(gps_src->points);
		BKE_gpencil_stroke_weights_duplicate(gps_src, gps_dst);

		gps_dst->triangles = MEM_dupallocN(gps_src->triangles);
		/* antoniov XXX: The palette never must be null, but this avoid crash after open if NULL */
		if (gps_src->palette == NULL) {
			gpencil_fix_null_palette(C, gps_src);
		}

		gps_dst->palcolor = MEM_dupallocN(gps_src->palcolor);

		BLI_addtail(&gpf_dst->strokes, gps_dst);
	}
	/* return new frame */
	return gpf_dst;
}

/* make a copy of a given gpencil brush */
bGPDbrush *BKE_gpencil_brush_duplicate(const bGPDbrush *brush_src)
{
	bGPDbrush *brush_dst;

	/* error checking */
	if (brush_src == NULL) {
		return NULL;
	}

	/* make a copy of source brush */
	brush_dst = MEM_dupallocN(brush_src);
	brush_dst->prev = brush_dst->next = NULL;
	/* make a copy of curves */
	brush_dst->cur_sensitivity = curvemapping_copy(brush_src->cur_sensitivity);
	brush_dst->cur_strength = curvemapping_copy(brush_src->cur_strength);
	brush_dst->cur_jitter = curvemapping_copy(brush_src->cur_jitter);

	/* return new brush */
	return brush_dst;
}


/* make a copy of a given gpencil layer */
bGPDlayer *BKE_gpencil_layer_duplicate(const bGPDlayer *gpl_src)
{
	const bGPDframe *gpf_src;
	bGPDframe *gpf_dst;
	bGPDlayer *gpl_dst;
	
	/* error checking */
	if (gpl_src == NULL) {
		return NULL;
	}
		
	/* make a copy of source layer */
	gpl_dst = MEM_dupallocN(gpl_src);
	gpl_dst->prev = gpl_dst->next = NULL;
	gpl_dst->derived_data = NULL;
	
	/* copy frames */
	BLI_listbase_clear(&gpl_dst->frames);
	for (gpf_src = gpl_src->frames.first; gpf_src; gpf_src = gpf_src->next) {
		/* make a copy of source frame */
		gpf_dst = BKE_gpencil_frame_duplicate(gpf_src);
		BLI_addtail(&gpl_dst->frames, gpf_dst);
		
		/* if source frame was the current layer's 'active' frame, reassign that too */
		if (gpf_src == gpl_dst->actframe)
			gpl_dst->actframe = gpf_dst;
	}
	
	/* return new layer */
	return gpl_dst;
}

/**
 * Only copy internal data of GreasePencil ID from source to already allocated/initialized destination.
 * You probably never want to use that directly, use id_copy or BKE_id_copy_ex for typical needs.
 *
 * WARNING! This function will not handle ID user count!
 *
 * \param flag  Copying options (see BKE_library.h's LIB_ID_COPY_... flags for more).
 */
void BKE_gpencil_copy_data(Main *UNUSED(bmain), bGPdata *gpd_dst, const bGPdata *gpd_src, const int UNUSED(flag))
{
	/* copy layers */
	BLI_listbase_clear(&gpd_dst->layers);
	for (const bGPDlayer *gpl_src = gpd_src->layers.first; gpl_src; gpl_src = gpl_src->next) {
		/* make a copy of source layer and its data */
		bGPDlayer *gpl_dst = BKE_gpencil_layer_duplicate(gpl_src);  /* TODO here too could add unused flags... */
		BLI_addtail(&gpd_dst->layers, gpl_dst);
	}
	
	/* copy palette slots */
	BLI_listbase_clear(&gpd_dst->palette_slots);
	for (const bGPDpaletteref *palslot_src = gpd_src->palette_slots.first; palslot_src; palslot_src = palslot_src->next) {
		bGPDpaletteref *palslot_dst = MEM_dupallocN(palslot_src);
		
		/* TODO: Separate out into separate function, and make use of LIB_ID_COPY flags? */
		if (palslot_dst->palette) {
			/* XXX: Better safe than sorry... at worst, usercount won't go down,
			 * and the user has to reload?
			 */
			id_us_plus(&palslot_dst->palette->id);
		}
		palslot_dst->next = palslot_dst->prev = NULL;
		
		BLI_addtail(&gpd_dst->palette_slots, palslot_dst);
	}

	/* copy palettes */
	BLI_listbase_clear(&gpd_dst->palettes);
	for (const bGPDpalette *palette_src = gpd_src->palettes.first; palette_src; palette_src = palette_src->next) {
		bGPDpalette *palette_dst = BKE_gpencil_palette_duplicate(palette_src);  /* TODO here too could add unused flags... */
		BLI_addtail(&gpd_dst->palettes, palette_dst);
	}
}

/* Standard API to make a copy of GP datablock, separate from copying its data */
bGPdata *BKE_gpencil_copy(Main *bmain, const bGPdata *gpd)
{
	bGPdata *gpd_copy;
	BKE_id_copy_ex(bmain, &gpd->id, (ID **)&gpd_copy, 0, false);
	return gpd_copy;
}

/* make a copy of a given gpencil datablock */
// XXX: Should this be deprecated?
bGPdata *BKE_gpencil_data_duplicate(Main *bmain, const bGPdata *gpd_src, bool internal_copy)
{
	bGPdata *gpd_dst;

	/* Yuck and super-uber-hyper yuck!!!
	 * Should be replaceable with a no-main copy (LIB_ID_COPY_NO_MAIN etc.), but not sure about it,
	 * so for now keep old code for that one. */
	 
	/* error checking */
	if (gpd_src == NULL) {
		return NULL;
	}

	if (internal_copy) {
		/* make a straight copy for undo buffers used during stroke drawing */
		gpd_dst = MEM_dupallocN(gpd_src);
	}
	else {
		/* make a copy when others use this */
		gpd_dst = BKE_libblock_copy(bmain, &gpd_src->id);
		gpd_dst->batch_cache_data = NULL;
	}
	
	/* Copy internal data (layers, etc.) */
	BKE_gpencil_copy_data(bmain, gpd_dst, gpd_src, 0);
	
	/* return new */
	return gpd_dst;
}

void BKE_gpencil_make_local(Main *bmain, bGPdata *gpd, const bool lib_local)
{
	BKE_id_make_local_generic(bmain, &gpd->id, true, lib_local);
}

/* ************************************************** */
/* GP Stroke API */

/* ensure selection status of stroke is in sync with its points */
void BKE_gpencil_stroke_sync_selection(bGPDstroke *gps)
{
	bGPDspoint *pt;
	int i;
	
	/* error checking */
	if (gps == NULL)
		return;
	
	/* we'll stop when we find the first selected point,
	 * so initially, we must deselect
	 */
	gps->flag &= ~GP_STROKE_SELECT;
	
	for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
		if (pt->flag & GP_SPOINT_SELECT) {
			gps->flag |= GP_STROKE_SELECT;
			break;
		}
	}
}

/* ************************************************** */
/* GP Frame API */

/* delete the last stroke of the given frame */
void BKE_gpencil_frame_delete_laststroke(bGPDlayer *gpl, bGPDframe *gpf)
{
	bGPDstroke *gps = (gpf) ? gpf->strokes.last : NULL;
	int cfra = (gpf) ? gpf->framenum : 0; /* assume that the current frame was not locked */
	
	/* error checking */
	if (ELEM(NULL, gpf, gps))
		return;
	
	/* free the stroke and its data */
	if (gps->points) {
		BKE_gpencil_free_stroke_weights(gps);
		MEM_freeN(gps->points);
	}
	MEM_freeN(gps->triangles);
	BLI_freelinkN(&gpf->strokes, gps);
	
	/* if frame has no strokes after this, delete it */
	if (BLI_listbase_is_empty(&gpf->strokes)) {
		BKE_gpencil_layer_delframe(gpl, gpf);
		BKE_gpencil_layer_getframe(gpl, cfra, 0);
	}
}

/* ************************************************** */
/* GP Layer API */

/* Check if the given layer is able to be edited or not */
bool gpencil_layer_is_editable(const bGPDlayer *gpl)
{
	/* Sanity check */
	if (gpl == NULL)
		return false;
	
	/* Layer must be: Visible + Editable */
	if ((gpl->flag & (GP_LAYER_HIDE | GP_LAYER_LOCKED)) == 0) {
		/* Opacity must be sufficiently high that it is still "visible"
		 * Otherwise, it's not really "visible" to the user, so no point editing...
		 */
		if (gpl->opacity > GPENCIL_ALPHA_OPACITY_THRESH) {
			return true;
		}
	}
	
	/* Something failed */
	return false;
}

/* Look up the gp-frame on the requested frame number, but don't add a new one */
bGPDframe *BKE_gpencil_layer_find_frame(bGPDlayer *gpl, int cframe)
{
	bGPDframe *gpf;
	
	/* Search in reverse order, since this is often used for playback/adding,
	 * where it's less likely that we're interested in the earlier frames
	 */
	for (gpf = gpl->frames.last; gpf; gpf = gpf->prev) {
		if (gpf->framenum == cframe) {
			return gpf;
		}
	}
	
	return NULL;
}

/* get the appropriate gp-frame from a given layer
 *	- this sets the layer's actframe var (if allowed to)
 *	- extension beyond range (if first gp-frame is after all frame in interest and cannot add)
 */
bGPDframe *BKE_gpencil_layer_getframe(bGPDlayer *gpl, int cframe, eGP_GetFrame_Mode addnew)
{
	bGPDframe *gpf = NULL;
	short found = 0;
	
	/* error checking */
	if (gpl == NULL) return NULL;
	
	/* check if there is already an active frame */
	if (gpl->actframe) {
		gpf = gpl->actframe;
		
		/* do not allow any changes to layer's active frame if layer is locked from changes
		 * or if the layer has been set to stay on the current frame
		 */
		if (gpl->flag & GP_LAYER_FRAMELOCK)
			return gpf;
		/* do not allow any changes to actframe if frame has painting tag attached to it */
		if (gpf->flag & GP_FRAME_PAINT) 
			return gpf;
		
		/* try to find matching frame */
		if (gpf->framenum < cframe) {
			for (; gpf; gpf = gpf->next) {
				if (gpf->framenum == cframe) {
					found = 1;
					break;
				}
				else if ((gpf->next) && (gpf->next->framenum > cframe)) {
					found = 1;
					break;
				}
			}
			
			/* set the appropriate frame */
			if (addnew) {
				if ((found) && (gpf->framenum == cframe))
					gpl->actframe = gpf;
				else if (addnew == GP_GETFRAME_ADD_COPY)
					gpl->actframe = BKE_gpencil_frame_addcopy(gpl, cframe);
				else
					gpl->actframe = BKE_gpencil_frame_addnew(gpl, cframe);
			}
			else if (found)
				gpl->actframe = gpf;
			else
				gpl->actframe = gpl->frames.last;
		}
		else {
			for (; gpf; gpf = gpf->prev) {
				if (gpf->framenum <= cframe) {
					found = 1;
					break;
				}
			}
			
			/* set the appropriate frame */
			if (addnew) {
				if ((found) && (gpf->framenum == cframe))
					gpl->actframe = gpf;
				else if (addnew == GP_GETFRAME_ADD_COPY)
					gpl->actframe = BKE_gpencil_frame_addcopy(gpl, cframe);
				else
					gpl->actframe = BKE_gpencil_frame_addnew(gpl, cframe);
			}
			else if (found)
				gpl->actframe = gpf;
			else
				gpl->actframe = gpl->frames.first;
		}
	}
	else if (gpl->frames.first) {
		/* check which of the ends to start checking from */
		const int first = ((bGPDframe *)(gpl->frames.first))->framenum;
		const int last = ((bGPDframe *)(gpl->frames.last))->framenum;
		
		if (abs(cframe - first) > abs(cframe - last)) {
			/* find gp-frame which is less than or equal to cframe */
			for (gpf = gpl->frames.last; gpf; gpf = gpf->prev) {
				if (gpf->framenum <= cframe) {
					found = 1;
					break;
				}
			}
		}
		else {
			/* find gp-frame which is less than or equal to cframe */
			for (gpf = gpl->frames.first; gpf; gpf = gpf->next) {
				if (gpf->framenum <= cframe) {
					found = 1;
					break;
				}
			}
		}
		
		/* set the appropriate frame */
		if (addnew) {
			if ((found) && (gpf->framenum == cframe))
				gpl->actframe = gpf;
			else
				gpl->actframe = BKE_gpencil_frame_addnew(gpl, cframe);
		}
		else if (found)
			gpl->actframe = gpf;
		else {
			/* unresolved errogenous situation! */
			printf("Error: cannot find appropriate gp-frame\n");
			/* gpl->actframe should still be NULL */
		}
	}
	else {
		/* currently no frames (add if allowed to) */
		if (addnew)
			gpl->actframe = BKE_gpencil_frame_addnew(gpl, cframe);
		else {
			/* don't do anything... this may be when no frames yet! */
			/* gpl->actframe should still be NULL */
		}
	}
	
	/* return */
	return gpl->actframe;
}

/* delete the given frame from a layer */
bool BKE_gpencil_layer_delframe(bGPDlayer *gpl, bGPDframe *gpf)
{
	bool changed = false;
	
	/* error checking */
	if (ELEM(NULL, gpl, gpf))
		return false;
	
	/* if this frame was active, make the previous frame active instead 
	 * since it's tricky to set active frame otherwise
	 */
	if (gpl->actframe == gpf)
		gpl->actframe = gpf->prev;
	
	/* free the frame and its data */
	changed = BKE_gpencil_free_strokes(gpf);
	BLI_freelinkN(&gpl->frames, gpf);
	
	return changed;
}

/* get the active gp-layer for editing */
bGPDlayer *BKE_gpencil_layer_getactive(bGPdata *gpd)
{
	bGPDlayer *gpl;
	
	/* error checking */
	if (ELEM(NULL, gpd, gpd->layers.first))
		return NULL;
		
	/* loop over layers until found (assume only one active) */
	for (gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		if (gpl->flag & GP_LAYER_ACTIVE)
			return gpl;
	}
	
	/* no active layer found */
	return NULL;
}

/* set the active gp-layer */
void BKE_gpencil_layer_setactive(bGPdata *gpd, bGPDlayer *active)
{
	bGPDlayer *gpl;
	
	/* error checking */
	if (ELEM(NULL, gpd, gpd->layers.first, active))
		return;
		
	/* loop over layers deactivating all */
	for (gpl = gpd->layers.first; gpl; gpl = gpl->next)
		gpl->flag &= ~GP_LAYER_ACTIVE;
	
	/* set as active one */
	active->flag |= GP_LAYER_ACTIVE;
}

/* delete the active gp-layer */
void BKE_gpencil_layer_delete(bGPdata *gpd, bGPDlayer *gpl)
{
	/* error checking */
	if (ELEM(NULL, gpd, gpl)) 
		return;
	
	/* free layer */
	BKE_gpencil_free_frames(gpl);
	
	/* free derived data */
	BKE_gpencil_clear_derived(gpl);
	if (gpl->derived_data) {
		BLI_ghash_free(gpl->derived_data, NULL, NULL);
		gpl->derived_data = NULL;
	}

	BLI_freelinkN(&gpd->layers, gpl);
}

/* ************************************************** */
/* GP Brush API */

/* get the active gp-brush for editing */
bGPDbrush *BKE_gpencil_brush_getactive(ToolSettings *ts)
{
	bGPDbrush *brush;

	/* error checking */
	if (ELEM(NULL, ts, ts->gp_brushes.first)) {
		return NULL;
	}

	/* loop over brushes until found (assume only one active) */
	for (brush = ts->gp_brushes.first; brush; brush = brush->next) {
		if (brush->flag & GP_BRUSH_ACTIVE) {
			return brush;
		}
	}

	/* no active brush found */
	return NULL;
}

/* set the active gp-brush */
void BKE_gpencil_brush_setactive(ToolSettings *ts, bGPDbrush *active)
{
	bGPDbrush *brush;

	/* error checking */
	if (ELEM(NULL, ts, ts->gp_brushes.first, active)) {
		return;
	}

	/* loop over brushes deactivating all */
	for (brush = ts->gp_brushes.first; brush; brush = brush->next) {
		brush->flag &= ~GP_BRUSH_ACTIVE;
	}

	/* set as active one */
	active->flag |= GP_BRUSH_ACTIVE;
}

/* delete the active gp-brush */
void BKE_gpencil_brush_delete(ToolSettings *ts, bGPDbrush *brush)
{
	/* error checking */
	if (ELEM(NULL, ts, brush)) {
		return;
	}

	/* free curves */
	if (brush->cur_sensitivity) {
		curvemapping_free(brush->cur_sensitivity);
	}
	if (brush->cur_strength) {
		curvemapping_free(brush->cur_strength);
	}
	if (brush->cur_jitter) {
		curvemapping_free(brush->cur_jitter);
	}

	/* free */
	BLI_freelinkN(&ts->gp_brushes, brush);
}

/* ************************************************** */
/* GP Palette Slots API */

/* Active Palette Slot ------------------------------ */

/* Get active palette slot */
bGPDpaletteref *BKE_gpencil_paletteslot_get_active(const bGPdata *gpd)
{
	/* sanity checks */
	if (ELEM(NULL, gpd, gpd->palette_slots.first))
		return NULL;
	
	/* use active_index value to find the relevant slot */
	return BLI_findlink(&gpd->palette_slots, gpd->active_palette_slot);
}

/* Set active palette slot */
void BKE_gpencil_paletteslot_set_active(bGPdata *gpd, const bGPDpaletteref *palslot)
{
	/* sanity checks */
	if (ELEM(NULL, gpd, palslot))
		return;
	
	/* try to find index of this item, assuming it exists in the list */
	gpd->active_palette_slot = BLI_findindex(&gpd->palette_slots, palslot);
}

/* Make the slot using this palette active */
void BKE_gpencil_paletteslot_set_active_palette(bGPdata *gpd, const Palette *palette)
{
	bGPDpaletteref *palslot = BKE_gpencil_paletteslot_find(gpd, palette);
	BKE_gpencil_paletteslot_set_active(gpd, palslot);
}

/* Slot Lookup ------------------------------------- */

/* Get palette slot that uses this Palette */
bGPDpaletteref *BKE_gpencil_paletteslot_find(bGPdata *gpd, const Palette *palette)
{
	bGPDpaletteref *palslot;
	
	/* sanity checks */
	if (ELEM(NULL, gpd, palette))
		return NULL;
	
	/* search for the palette */
	for (palslot = gpd->palette_slots.first; palslot; palslot = palslot->next) {
		if (palslot->palette == palette)
			return palslot;
	}
	
	/* not found */
	return NULL;
}

/* Check if palette is used by any strokes... */
bool BKE_gpencil_paletteslot_has_users(const bGPdata *gpd, const bGPDpaletteref *palslot)
{
	Palette *palette;
	bGPDlayer *gpl;
	bGPDframe *gpf;
	bGPDstroke *gps;
	
	/* quick-exit checks */
	if (ELEM(NULL, gpd, palslot, palslot->palette))
		return false;
	
	palette = palslot->palette;
	
	/* check if any strokes use this */
	for (gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			for (gps = gpf->strokes.first; gps; gps = gps->next) {
				if (gps->palette == palette)
					return true;
			}
		}
	}
	
	/* nothing found */
	return false;
}

/* Add Slots --------------------------------------- */

/* Create a new palette slot (and optionally assign a palette to it) */
bGPDpaletteref *BKE_gpencil_paletteslot_add(bGPdata *gpd, Palette *palette)
{
	bGPDpaletteref *palslot;
	
	/* sanity checks */
	if (gpd == NULL) {
		return NULL;
	}
	if (palette) {
		/* check if it's used already - don't allow for duplicates */
		palslot = BKE_gpencil_paletteslot_find(gpd, palette);
		if (palslot) {
			/* just return existing? */
			return palslot;
		}
	}
	
	/* allocate a new slot, and assigned palette as user */
	palslot = MEM_callocN(sizeof(bGPDpaletteref), "bGPDpaletteref");
	BLI_addtail(&gpd->palette_slots, palslot);
	
	/* assign palette */
	if (palette) {
		palslot->palette = palette;
		id_us_plus(&palette->id);
	}
	
	/* update active palette */
	gpd->active_palette_slot = BLI_listbase_count(&gpd->palette_slots) - 1;
	
	/* return new slot */
	return palslot;
}

/* Wrapper for BKE_gpencil_paletteslot_add() to add a new Palette + slot, 
 * and set all the usercounts correctly
 */
bGPDpaletteref *BKE_gpencil_paletteslot_addnew(Main *bmain, bGPdata *gpd, const char name[])
{
	/* create the palette first */
	Palette *palette = BKE_palette_add(bmain, name);
	
	/* lower the usercount, as assigning to the slot will add its own instead */
	id_us_min(&palette->id);
	
	/* create and return the new slot */
	return BKE_gpencil_paletteslot_add(gpd, palette);
}

/* Get active palette slot, and add all default settings if we don't find anything */
bGPDpaletteref *BKE_gpencil_paletteslot_validate(Main *bmain, bGPdata *gpd)
{
	bGPDpaletteref *palslot;
	Palette *palette;
	
	/* sanity checks */
	if (ELEM(NULL, bmain, gpd))
		return NULL;
	
	/* ensure a palette slot exists */
	palslot = BKE_gpencil_paletteslot_get_active(gpd);
	if (palslot == NULL) {
		palslot = BKE_gpencil_paletteslot_add(gpd, NULL);
	}
	
	/* ensure a palette exists */
	/* TODO: use "active palette" instead of making a new one each time?
	 *      (or use active one on "gp_object" in scene?)
	 */
	if (palslot->palette == NULL) {
		/* NOTE: no need to increment user count when setting this one here,
		 * as the db already has 2 users (1 from "Fake User", and the other
		 * is for whoever uses it now - i.e. the palette slot!)
		 */
		palslot->palette = BKE_palette_add(bmain, "Palette");
	}
	
	/* ensure the palette has colors, and that the active one is usable */
	palette = palslot->palette;
	if (BKE_palette_is_empty(palette)) {
		/* init the default set if none exist */
		BKE_palette_color_add_default_set(palette);
	}
	else if (BKE_palette_color_get_active(palette) == NULL) {
		/* sometimes the "active" color is unreachable
		 * (e.g. after deleting the previously active one)
		 */
		palette->active_color = 0;
	}
	
	/* return the slot (this is more useful, as we can still get the palette from it) */
	return palslot;
}

/* Palette Assignment ------------------------------- */

/**
 * Helper for BKE_gpencil_paletteslot_set_palette()
 *
 * Change all strokes using 'old_palette' to use 'new_palette' instead.
 */
/* XXX: This overlaps with the operators, but this one just does bulk replacement */
/* TODO: Optimise with hash lookups? */
static void gpencil_strokes_palette_change_all(bGPdata *gpd, Palette *old_palette, Palette *new_palette)
{
	if (BKE_palette_is_empty(old_palette))
		return;
	
	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
				/* check if this stroke uses the old palette */
				if (gps->palette == old_palette) {
					/* change palette */
					gps->palette = new_palette;
					
					/* find new matching color */
					// XXX: optimise?
					// XXX: what to do if not set?
					gps->palcolor = BKE_palette_color_getbyname(new_palette, gps->colorname);
				}
			}
		}
	}
}

/* Set the palette used by this slot */
void BKE_gpencil_paletteslot_set_palette(bGPdata *gpd, bGPDpaletteref *palslot, Palette *palette)
{
	/* sanity checks */
	if (ELEM(NULL, gpd, palslot))
		return;
	
	/* Save effort if nothing changes... */
	/* XXX: This does mean that "flushing" the property to fix any errors won't work... */
	if (palslot->palette == palette)
		return;
	
	/* Unset existing palette */
	if (palslot->palette) {
		/* Unbind all strokes using this, as the slots *must*
		 * reflect all the palettes used by the strokes
		 *
		 * XXX: What happens if the new Palette doesn't support this new color?
		 */
		gpencil_strokes_palette_change_all(gpd, palslot->palette, palette);
		
		/* Now clear the old user... */
		id_us_min(&palslot->palette->id);
		palslot->palette = NULL;
	}
	
	/* Set new palette */
	palslot->palette = palette;
	if (palette) {
		id_us_plus(&palette->id);
	}
}

/* ************************************************** */
/* GP Palettes API (Deprecated) */

/* Free all of a gp-colors */
static void free_gpencil_colors(bGPDpalette *palette)
{
	/* error checking */
	if (palette == NULL) {
		return;
	}

	/* free colors */
	BLI_freelistN(&palette->colors);
}

/* Free all of the gp-palettes and colors */
void BKE_gpencil_free_palettes(ListBase *list)
{
	bGPDpalette *palette_next;

	/* error checking */
	if (list == NULL) {
		return;
	}

	/* delete palettes */
	for (bGPDpalette *palette = list->first; palette; palette = palette_next) {
		palette_next = palette->next;
		/* free palette colors */
		free_gpencil_colors(palette);

		MEM_freeN(palette);
	}
	BLI_listbase_clear(list);
}


/* add a new gp-palette and make it the active */
bGPDpalette *BKE_gpencil_palette_addnew(bGPdata *gpd, const char *name, bool setactive)
{
	bGPDpalette *palette;

	/* check that list is ok */
	if (gpd == NULL) {
		return NULL;
	}

	/* allocate memory and add to end of list */
	palette = MEM_callocN(sizeof(bGPDpalette), "bGPDpalette");

	/* add to datablock */
	BLI_addtail(&gpd->palettes, palette);

	/* set basic settings */
	/* auto-name */
	BLI_strncpy(palette->info, name, sizeof(palette->info));
	BLI_uniquename(&gpd->palettes, palette, DATA_("GP_Palette"), '.', offsetof(bGPDpalette, info),
	               sizeof(palette->info));

	/* make this one the active one */
	/* NOTE: Always make this active if there's nothing else yet (T50123) */
	if ((setactive) || (gpd->palettes.first == gpd->palettes.last)) {
		BKE_gpencil_palette_setactive(gpd, palette);
	}

	/* return palette */
	return palette;
}


/* get the active gp-palette for editing */
bGPDpalette *BKE_gpencil_palette_getactive(bGPdata *gpd)
{
	bGPDpalette *palette;

	/* error checking */
	if (ELEM(NULL, gpd, gpd->palettes.first)) {
		return NULL;
	}

	/* loop over palettes until found (assume only one active) */
	for (palette = gpd->palettes.first; palette; palette = palette->next) {
		if (palette->flag & PL_PALETTE_ACTIVE)
			return palette;
	}

	/* no active palette found */
	return NULL;
}

/* set the active gp-palette */
void BKE_gpencil_palette_setactive(bGPdata *gpd, bGPDpalette *active)
{
	bGPDpalette *palette;

	/* error checking */
	if (ELEM(NULL, gpd, gpd->palettes.first, active)) {
		return;
	}

	/* loop over palettes deactivating all */
	for (palette = gpd->palettes.first; palette; palette = palette->next) {
		palette->flag &= ~PL_PALETTE_ACTIVE;
	}

	/* set as active one */
	active->flag |= PL_PALETTE_ACTIVE;
	/* force color recalc */
	BKE_gpencil_palette_change_strokes(gpd);
}

/* delete the active gp-palette */
void BKE_gpencil_palette_delete(bGPdata *gpd, bGPDpalette *palette)
{
	/* error checking */
	if (ELEM(NULL, gpd, palette)) {
		return;
	}

	/* free colors */
	free_gpencil_colors(palette);
	BLI_freelinkN(&gpd->palettes, palette);
	/* force color recalc */
	BKE_gpencil_palette_change_strokes(gpd);
}

/* make a copy of a given gpencil palette */
bGPDpalette *BKE_gpencil_palette_duplicate(const bGPDpalette *palette_src)
{
	bGPDpalette *palette_dst;
	const bGPDpalettecolor *palcolor_src;
	bGPDpalettecolor *palcolord_dst;

	/* error checking */
	if (palette_src == NULL) {
		return NULL;
	}

	/* make a copy of source palette */
	palette_dst = MEM_dupallocN(palette_src);
	palette_dst->prev = palette_dst->next = NULL;

	/* copy colors */
	BLI_listbase_clear(&palette_dst->colors);
	for (palcolor_src = palette_src->colors.first; palcolor_src; palcolor_src = palcolor_src->next) {
		/* make a copy of source */
		palcolord_dst = MEM_dupallocN(palcolor_src);
		BLI_addtail(&palette_dst->colors, palcolord_dst);
	}

	/* return new palette */
	return palette_dst;
}

/* Set all strokes to recalc the palette color */
void BKE_gpencil_palette_change_strokes(bGPdata *gpd)
{
	bGPDlayer *gpl;
	bGPDframe *gpf;
	bGPDstroke *gps;

	for (gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			for (gps = gpf->strokes.first; gps; gps = gps->next) {
				gps->flag |= GP_STROKE_RECALC_COLOR;
			}
		}
	}
}


/* add a new gp-palettecolor and make it the active */
bGPDpalettecolor *BKE_gpencil_palettecolor_addnew(bGPDpalette *palette, const char *name, bool setactive)
{
	bGPDpalettecolor *palcolor;

	/* check that list is ok */
	if (palette == NULL) {
		return NULL;
	}

	/* allocate memory and add to end of list */
	palcolor = MEM_callocN(sizeof(bGPDpalettecolor), "bGPDpalettecolor");

	/* add to datablock */
	BLI_addtail(&palette->colors, palcolor);

	/* set basic settings */
	copy_v4_v4(palcolor->color, U.gpencil_new_layer_col);
	ARRAY_SET_ITEMS(palcolor->fill, 1.0f, 1.0f, 1.0f);

	/* auto-name */
	BLI_strncpy(palcolor->info, name, sizeof(palcolor->info));
	BLI_uniquename(&palette->colors, palcolor, DATA_("Color"), '.', offsetof(bGPDpalettecolor, info),
	               sizeof(palcolor->info));

	/* make this one the active one */
	if (setactive) {
		BKE_gpencil_palettecolor_setactive(palette, palcolor);
	}

	/* return palette color */
	return palcolor;
}


/* get the active gp-palettecolor for editing */
bGPDpalettecolor *BKE_gpencil_palettecolor_getactive(bGPDpalette *palette)
{
	bGPDpalettecolor *palcolor;

	/* error checking */
	if (ELEM(NULL, palette, palette->colors.first)) {
		return NULL;
	}

	/* loop over colors until found (assume only one active) */
	for (palcolor = palette->colors.first; palcolor; palcolor = palcolor->next) {
		if (palcolor->flag & PC_COLOR_ACTIVE) {
			return palcolor;
		}
	}

	/* no active color found */
	return NULL;
}


/* get the gp-palettecolor looking for name */
bGPDpalettecolor *BKE_gpencil_palettecolor_getbyname(bGPDpalette *palette, char *name)
{
	/* error checking */
	if (ELEM(NULL, palette, name)) {
		return NULL;
	}

	return BLI_findstring(&palette->colors, name, offsetof(bGPDpalettecolor, info));
}

/* Change color name in all gpd datablocks */
void BKE_gpencil_palettecolor_allnames(PaletteColor *palcolor, const char *newname)
{
	bGPdata *gpd;
	Main *bmain = G.main;

	for (gpd = bmain->gpencil.first; gpd; gpd = gpd->id.next) {
		BKE_gpencil_palettecolor_changename(palcolor, gpd, newname);
	}
}

/* Change color name in all strokes */
void BKE_gpencil_palettecolor_changename(PaletteColor *palcolor, bGPdata *gpd, const char *newname)
{
	bGPDlayer *gpl;
	bGPDframe *gpf;
	bGPDstroke *gps;
	
	/* Sanity checks (gpd may not be set in the RNA pointers sometimes) */
	if (ELEM(NULL, gpd, newname))
		return;
	
	for (gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			for (gps = gpf->strokes.first; gps; gps = gps->next) {
				if (gps->palcolor == palcolor) {
					BLI_strncpy(gps->colorname, newname, sizeof(gps->colorname));
				}
			}
		}
	}
		
}

/* Delete all strokes of the color for all gpd datablocks */
void BKE_gpencil_palettecolor_delete_allstrokes(Main *bmain, PaletteColor *palcolor)
{
	bGPdata *gpd;
	bGPDlayer *gpl;
	bGPDframe *gpf;
	bGPDstroke *gps, *gpsn;

	/* TODO: Optimise this by only checking GP datablocks that reference the palette this comes from */
	for (gpd = bmain->gpencil.first; gpd; gpd = gpd->id.next) {
		for (gpl = gpd->layers.first; gpl; gpl = gpl->next) {
			for (gpf = gpl->frames.first; gpf; gpf = gpf->next) {
				for (gps = gpf->strokes.first; gps; gps = gpsn) {
					gpsn = gps->next;

					if (gps->palcolor == palcolor) {
						if (gps->points) {
							BKE_gpencil_free_stroke_weights(gps);
							MEM_freeN(gps->points);
						}
						if (gps->triangles) MEM_freeN(gps->triangles);
						BLI_freelinkN(&gpf->strokes, gps);
					}
				}
			}
		}
		BKE_gpencil_batch_cache_dirty(gpd);
	}
}

/* set the active gp-palettecolor */
void BKE_gpencil_palettecolor_setactive(bGPDpalette *palette, bGPDpalettecolor *active)
{
	bGPDpalettecolor *palcolor;

	/* error checking */
	if (ELEM(NULL, palette, palette->colors.first, active)) {
		return;
	}

	/* loop over colors deactivating all */
	for (palcolor = palette->colors.first; palcolor; palcolor = palcolor->next) {
		palcolor->flag &= ~PC_COLOR_ACTIVE;
	}

	/* set as active one */
	active->flag |= PC_COLOR_ACTIVE;
}

/* delete the active gp-palettecolor */
void BKE_gpencil_palettecolor_delete(bGPDpalette *palette, bGPDpalettecolor *palcolor)
{
	/* error checking */
	if (ELEM(NULL, palette, palcolor)) {
		return;
	}

	/* free */
	BLI_freelinkN(&palette->colors, palcolor);
}

/* ************************************************** */
/* Palette Data Conversion */

/* Transfer the animation data from bGPDpalette to Palette */
void BKE_gpencil_move_animdata_to_palettes(bContext *C, bGPdata *gpd)
{
	Main *bmain = CTX_data_main(C);
	Palette *palette = NULL;
	AnimData *src_adt = NULL, *dst_adt = NULL;
	FCurve *fcu = NULL;
	char info[64];

	/* sanity checks */
	if (ELEM(NULL, gpd)) {
		if (G.debug & G_DEBUG)
			printf("ERROR: no source ID to separate AnimData with\n");
		return;
	}
	/* get animdata from src, and create for destination (if needed) */
	src_adt = BKE_animdata_from_id((ID *)gpd);
	if (ELEM(NULL, src_adt)) {
		if (G.debug & G_DEBUG)
			printf("ERROR: no source AnimData\n");
		return;
	}

	/* find first palette */
	for (fcu = src_adt->action->curves.first; fcu; fcu = fcu->next) {
		if (strncmp("palette", fcu->rna_path, 7) == 0) {
			int x = strcspn(fcu->rna_path, "[") + 2;
			int y = strcspn(fcu->rna_path, "]");
			BLI_strncpy(info, fcu->rna_path + x, y - x);
			palette = BLI_findstring(&bmain->palettes, info, offsetof(ID, name) + 2);
			break;
		}
	}
	if (ELEM(NULL, palette)) {
		if (G.debug & G_DEBUG)
			printf("ERROR: Palette %s not found\n", info);
		return;
	}

	/* active action */
	if (src_adt->action) {
		/* get animdata from destination or create (if needed) */
		dst_adt = BKE_animdata_add_id((ID *) palette);
		if (ELEM(NULL, dst_adt)) {
			if (G.debug & G_DEBUG)
				printf("ERROR: no AnimData for destination palette\n");
			return;
		}

		/* create destination action */
		dst_adt->action = add_empty_action(G.main, src_adt->action->id.name + 2);
		/* move fcurves */
		action_move_fcurves_by_basepath(src_adt->action, dst_adt->action, "palettes");

		/* loop over base paths, to fix for each one... */
		for (fcu = dst_adt->action->curves.first; fcu; fcu = fcu->next) {
			if (strncmp("palette", fcu->rna_path, 7) == 0) {
				int x = strcspn(fcu->rna_path, ".") + 1;
				BLI_strncpy(fcu->rna_path, fcu->rna_path + x, strlen(fcu->rna_path));
			}
		}
	}
}

/* ************************************************** */
/* GP Object - Boundbox Support */

/* get stroke min max values */
static void gpencil_minmax(bGPdata *gpd, float min[3], float max[3])
{
	int i;
	bGPDspoint *pt;
	bGPDframe *gpf;
	INIT_MINMAX(min, max);

	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		gpf = gpl->actframe;
		if (!gpf) {
			continue;
		}
		for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
			for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
				minmax_v3v3_v3(min, max, &pt->x);
			}
		}
	}
}

/* compute center of bounding box */
void BKE_gpencil_centroid_3D(bGPdata *gpd, float r_centroid[3])
{
	float min[3], max[3], tot[3];
	
	gpencil_minmax(gpd, min, max);
	
	add_v3_v3v3(tot, min, max);
	mul_v3_v3fl(r_centroid, tot, 0.5f);
}


/* create bounding box values */
static void boundbox_gpencil(Object *ob)
{
	BoundBox *bb;
	bGPdata *gpd;
	float min[3], max[3];

	if (ob->bb == NULL) {
		ob->bb = MEM_callocN(sizeof(BoundBox), "GPencil boundbox");
	}

	bb  = ob->bb;
	gpd = ob->data;

	gpencil_minmax(gpd, min, max);
	BKE_boundbox_init_from_minmax(bb, min, max);

	bb->flag &= ~BOUNDBOX_DIRTY;
}

/* get bounding box */
BoundBox *BKE_gpencil_boundbox_get(Object *ob)
{
	bGPdata *gpd;
	
	if (ELEM(NULL, ob, ob->data))
		return NULL;
	
	gpd = ob->data;
	if ((ob->bb) && ((ob->bb->flag & BOUNDBOX_DIRTY) == 0) && 
	    ((gpd->flag & GP_DATA_CACHE_IS_DIRTY) == 0))
	{
		return ob->bb;
	}

	boundbox_gpencil(ob);

	return ob->bb;
}

/* ************************************************** */
/* Apply Transforms */

void BKE_gpencil_transform(bGPdata *gpd, float mat[4][4])
{
	if (gpd == NULL)
		return;
	
	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		/* FIXME: For now, we just skip parented layers.
		 * Otherwise, we have to update each frame to find
		 * the current parent position/effects.
		 */
		if (gpl->parent) {
			continue;
		}
		
		for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
				bGPDspoint *pt;
				int i;
				
				for (pt = gps->points, i = 0; i < gps->totpoints; pt++, i++) {
					mul_m4_v3(mat, &pt->x);
				}
				
				/* TODO: Do we need to do this? distortion may mean we need to re-triangulate */
				gps->flag |= GP_STROKE_RECALC_CACHES;
				gps->tot_triangles = 0;
			}
		}
	}
	
	
	BKE_gpencil_batch_cache_dirty(gpd);
}

/* ************************************************** */
/* GP Object - Vertex Groups */

/* remove a vertex group */
void BKE_gpencil_vgroup_remove(Object *ob, bDeformGroup *defgroup)
{
	bGPdata *gpd = ob->data;
	bGPDspoint *pt = NULL;
	bGPDweight *gpw = NULL;
	const int def_nr = BLI_findindex(&ob->defbase, defgroup);

	/* Remove points data */
	if (gpd) {
		for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
			for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
				for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
					for (int i = 0; i < gps->totpoints; i++) {
						pt = &gps->points[i];
						for (int i2 = 0; i2 < pt->totweight; i2++) {
							gpw = &pt->weights[i2];
							if (gpw->index == def_nr) {
								BKE_gpencil_vgroup_remove_point_weight(pt, def_nr);
							}
							/* if index is greater, must be moved one back */
							if (gpw->index > def_nr) {
								gpw->index--;
							}
						}
					}
				}
			}
		}
	}

	/* Remove the group */
	BLI_freelinkN(&ob->defbase, defgroup);
}

/* add a new weight */
bGPDweight *BKE_gpencil_vgroup_add_point_weight(bGPDspoint *pt, int index, float weight)
{
	bGPDweight *new_gpw = NULL;
	bGPDweight *tmp_gpw;

	/* need to verify if was used before to update */
	for (int i = 0; i < pt->totweight; i++) {
		tmp_gpw = &pt->weights[i];
		if (tmp_gpw->index == index) {
			tmp_gpw->factor = weight;
			return tmp_gpw;
		}
	}

	pt->totweight++;
	if (pt->totweight == 1) {
		pt->weights = MEM_callocN(sizeof(bGPDweight), "gp_weight");
	}
	else {
		pt->weights = MEM_reallocN(pt->weights, sizeof(bGPDweight) * pt->totweight);
	}
	new_gpw = &pt->weights[pt->totweight - 1];
	new_gpw->index = index;
	new_gpw->factor = weight;

	return new_gpw;
}

/* return the weight if use index  or -1*/
float BKE_gpencil_vgroup_use_index(bGPDspoint *pt, int index)
{
	bGPDweight *gpw;
	for (int i = 0; i < pt->totweight; i++) {
		gpw = &pt->weights[i];
		if (gpw->index == index) {
			return gpw->factor;
		}
	}
	return -1.0f;
}

/* add a new weight */
bool BKE_gpencil_vgroup_remove_point_weight(bGPDspoint *pt, int index)
{
	int e = 0;

	if (BKE_gpencil_vgroup_use_index(pt, index) < 0.0f) {
		return false;
	}

	/* if the array get empty, exit */
	if (pt->totweight == 1) {
		pt->totweight = 0;
		MEM_SAFE_FREE(pt->weights);
		return true;
	}

	/* realloc weights */
	bGPDweight *tmp = MEM_dupallocN(pt->weights);
	MEM_SAFE_FREE(pt->weights);
	pt->weights = MEM_callocN(sizeof(bGPDweight) * pt->totweight - 1, "gp_weights");

	for (int x = 0; x < pt->totweight; x++) {
		bGPDweight *gpw = &tmp[e];
		bGPDweight *final_gpw = &pt->weights[e];
		if (gpw->index != index) {
			final_gpw->index = gpw->index;
			final_gpw->factor = gpw->factor;
			e++;
		}
	}
	MEM_SAFE_FREE(tmp);
	pt->totweight--;

	return true;
}


/* ************************************************** */
/**
* Apply smooth to stroke point
* \param gps              Stroke to smooth
* \param i                Point index
* \param inf              Amount of smoothing to apply
* \param affect_pressure  Apply smoothing to pressure values too?
*/
bool BKE_gp_smooth_stroke(bGPDstroke *gps, int i, float inf, bool affect_pressure)
{
	bGPDspoint *pt = &gps->points[i];
	float pressure = 0.0f;
	float sco[3] = { 0.0f };

	/* Do nothing if not enough points to smooth out */
	if (gps->totpoints <= 2) {
		return false;
	}

	/* Only affect endpoints by a fraction of the normal strength,
	* to prevent the stroke from shrinking too much
	*/
	if ((i == 0) || (i == gps->totpoints - 1)) {
		inf *= 0.1f;
	}

	/* Compute smoothed coordinate by taking the ones nearby */
	/* XXX: This is potentially slow, and suffers from accumulation error as earlier points are handled before later ones */
	{
		// XXX: this is hardcoded to look at 2 points on either side of the current one (i.e. 5 items total)
		const int   steps = 2;
		const float average_fac = 1.0f / (float)(steps * 2 + 1);
		int step;

		/* add the point itself */
		madd_v3_v3fl(sco, &pt->x, average_fac);

		if (affect_pressure) {
			pressure += pt->pressure * average_fac;
		}

		/* n-steps before/after current point */
		// XXX: review how the endpoints are treated by this algorithm
		// XXX: falloff measures should also introduce some weighting variations, so that further-out points get less weight
		for (step = 1; step <= steps; step++) {
			bGPDspoint *pt1, *pt2;
			int before = i - step;
			int after = i + step;

			CLAMP_MIN(before, 0);
			CLAMP_MAX(after, gps->totpoints - 1);

			pt1 = &gps->points[before];
			pt2 = &gps->points[after];

			/* add both these points to the average-sum (s += p[i]/n) */
			madd_v3_v3fl(sco, &pt1->x, average_fac);
			madd_v3_v3fl(sco, &pt2->x, average_fac);

#if 0
			/* XXX: Disabled because get weird result */
			/* do pressure too? */
			if (affect_pressure) {
				pressure += pt1->pressure * average_fac;
				pressure += pt2->pressure * average_fac;
			}
#endif
		}
	}

	/* Based on influence factor, blend between original and optimal smoothed coordinate */
	interp_v3_v3v3(&pt->x, &pt->x, sco, inf);

#if 0
	/* XXX: Disabled because get weird result */
	if (affect_pressure) {
		pt->pressure = pressure;
	}
#endif

	return true;
}

/**
* Apply smooth for strength to stroke point
* \param gps              Stroke to smooth
* \param i                Point index
* \param inf              Amount of smoothing to apply
*/
bool BKE_gp_smooth_stroke_strength(bGPDstroke *gps, int i, float inf)
{
	bGPDspoint *ptb = &gps->points[i];

	/* Do nothing if not enough points */
	if (gps->totpoints <= 2) {
		return false;
	}

	/* Compute theoretical optimal value using distances */
	bGPDspoint *pta, *ptc;
	int before = i - 1;
	int after = i + 1;

	CLAMP_MIN(before, 0);
	CLAMP_MAX(after, gps->totpoints - 1);

	pta = &gps->points[before];
	ptc = &gps->points[after];

	/* the optimal value is the corresponding to the interpolation of the strength
	*  at the distance of point b
	*/
	const float fac = line_point_factor_v3(&ptb->x, &pta->x, &ptc->x);
	const float optimal = (1.0f - fac) * pta->strength + fac * ptc->strength;

	/* Based on influence factor, blend between original and optimal */
	ptb->strength = (1.0f - inf) * ptb->strength + inf * optimal;

	return true;
}

/**
* Apply smooth for thickness to stroke point (use pressure)
* \param gps              Stroke to smooth
* \param i                Point index
* \param inf              Amount of smoothing to apply
*/
bool BKE_gp_smooth_stroke_thickness(bGPDstroke *gps, int i, float inf)
{
	bGPDspoint *ptb = &gps->points[i];

	/* Do nothing if not enough points */
	if (gps->totpoints <= 2) {
		return false;
	}

	/* Compute theoretical optimal value using distances */
	bGPDspoint *pta, *ptc;
	int before = i - 1;
	int after = i + 1;

	CLAMP_MIN(before, 0);
	CLAMP_MAX(after, gps->totpoints - 1);

	pta = &gps->points[before];
	ptc = &gps->points[after];

	/* the optimal value is the corresponding to the interpolation of the pressure
	*  at the distance of point b
	*/
	float fac = line_point_factor_v3(&ptb->x, &pta->x, &ptc->x);
	float optimal = (1.0f - fac) * pta->pressure + fac * ptc->pressure;

	/* Based on influence factor, blend between original and optimal */
	ptb->pressure = (1.0f - inf) * ptb->pressure + inf * optimal;

	return true;
}

/**
* Get range of selected frames in layer.
* Always the active frame is considered as selected, so if no more selected the range
* will be equal to the current active frame.
* \param gpl              Layer
* \param r_initframe      Number of first selected frame
* \param r_endframe       Number of last selected frame
*/
void BKE_gp_get_range_selected(bGPDlayer *gpl, int *r_initframe, int *r_endframe)
{
	*r_initframe = gpl->actframe->framenum;
	*r_endframe = gpl->actframe->framenum;

	for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
		if (gpf->flag & GP_FRAME_SELECT) {
			if (gpf->framenum < *r_initframe) {
				*r_initframe = gpf->framenum;
			}
			if (gpf->framenum > *r_endframe) {
				*r_endframe = gpf->framenum;
			}
		}
	}
}
/**
* Get Falloff factor base on frame range
* \param gpf          Frame
* \param actnum       Number of active frame in layer
* \param f_init       Number of first selected frame
* \param f_end        Number of last selected frame
* \param cur_falloff  Curve with falloff factors
*/
void BKE_get_falloff_factor(bGPDframe *gpf, int actnum, int f_init, int f_end, CurveMapping *cur_falloff, float *r_value)
{
	float fnum = 0.5f; /* default mid curve */
	/* frames to the right of the active frame */
	if (gpf->framenum < actnum) {
		fnum = (float)(gpf->framenum - f_init) / (actnum - f_init);
		fnum *= 0.5f;
		*r_value = curvemapping_evaluateF(cur_falloff, 0, fnum);
	}
	/* frames to the left of the active frame */
	else if (gpf->framenum > actnum) {
		fnum = (float)(gpf->framenum - actnum) / (f_end - actnum);
		fnum *= 0.5f;
		*r_value = curvemapping_evaluateF(cur_falloff, 0, fnum + 0.5f);
	}
	else {
		*r_value = 1.0f;
	}
}
