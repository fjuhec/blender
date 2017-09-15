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

/* ************************************************** */
/* GENERAL STUFF */

/* --------- Memory Management ------------ */
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
bool BKE_gpencil_free_layer_temp_data(bGPDlayer *gpl, bGPDframe *derived_gpf)
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

/* clear all runtime derived data */
static void BKE_gpencil_clear_derived(bGPDlayer *gpl)
{
	if (gpl->derived_data == NULL) {
		return;
	}
	GHashIterator *ihash = BLI_ghashIterator_new(gpl->derived_data);
	while (!BLI_ghashIterator_done(ihash)) {
		bGPDframe *gpf = (bGPDframe *) BLI_ghashIterator_getValue(ihash);
		if (gpf) {
			BKE_gpencil_free_layer_temp_data(gpl, gpf);
		}
		BLI_ghashIterator_step(ihash);
	}
	BLI_ghashIterator_free(ihash);
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

	/* free all data */
	if (free_all) {
		/* clear cache */
		BKE_gpencil_batch_cache_free(gpd);
		BKE_gpencil_free_palettes(&gpd->palettes);
	}
}

/* -------- Container Creation ---------- */

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
	gpl->flag |= GP_LAYER_ONIONSKIN;
	
	/* auto-name */
	BLI_strncpy(gpl->info, name, sizeof(gpl->info));
	BLI_uniquename(&gpd->layers, gpl, DATA_("GP_Layer"), '.', offsetof(bGPDlayer, info), sizeof(gpl->info));
	
	/* make this one the active one */
	if (setactive)
		BKE_gpencil_layer_setactive(gpd, gpl);
	
	/* return layer */
	return gpl;
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

/* create a set of default drawing brushes with predefined presets */
void BKE_gpencil_brush_init_presets(ToolSettings *ts)
{
	bGPDbrush *brush;
	float curcolor[3];
	ARRAY_SET_ITEMS(curcolor, 1.0f, 1.0f, 1.0f);

	/* Basic brush */
	brush = BKE_gpencil_brush_addnew(ts, "Basic", false);
	brush->thickness = 3.0f;
	brush->flag &= ~GP_BRUSH_USE_RANDOM_PRESSURE;
	brush->draw_sensitivity = 1.0f;
	brush->flag |= GP_BRUSH_USE_PRESSURE;
	brush->flag |= GP_BRUSH_ENABLE_CURSOR;

	brush->flag &= ~GP_BRUSH_USE_RANDOM_STRENGTH;
	brush->draw_strength = 1.0f;
	brush->flag |= ~GP_BRUSH_USE_STENGTH_PRESSURE;

	brush->draw_random_press = 0.0f;

	brush->draw_jitter = 0.0f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	brush->draw_angle = 0.0f;
	brush->draw_angle_factor = 0.0f;

	brush->draw_smoothfac = 0.0f;
	brush->draw_smoothlvl = 1;
	brush->sublevel = 0;
	brush->draw_random_sub = 0.0f;
	copy_v3_v3(brush->curcolor, curcolor);

	/* Pencil brush */
	brush = BKE_gpencil_brush_addnew(ts, "Pencil", true);
	brush->thickness = 15.0f;
	brush->flag &= ~GP_BRUSH_USE_RANDOM_PRESSURE;
	brush->draw_sensitivity = 1.0f;
	brush->flag |= GP_BRUSH_USE_PRESSURE;
	brush->flag |= GP_BRUSH_ENABLE_CURSOR;

	brush->flag &= ~GP_BRUSH_USE_RANDOM_STRENGTH;
	brush->draw_strength = 0.7f;
	brush->flag |= GP_BRUSH_USE_STENGTH_PRESSURE;

	brush->draw_random_press = 0.0f;

	brush->draw_jitter = 0.0f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	brush->draw_angle = 0.0f;
	brush->draw_angle_factor = 0.0f;

	brush->draw_smoothfac = 0.5f;
	brush->draw_smoothlvl = 1;
	brush->sublevel = 1;
	brush->draw_random_sub = 0.0f;
	copy_v3_v3(brush->curcolor, curcolor);

	/* Ink brush */
	brush = BKE_gpencil_brush_addnew(ts, "Ink", false);
	brush->thickness = 7.0f;
	brush->flag &= ~GP_BRUSH_USE_RANDOM_PRESSURE;
	brush->draw_sensitivity = 1.6f;
	brush->flag |= GP_BRUSH_USE_PRESSURE;
	brush->flag |= GP_BRUSH_ENABLE_CURSOR;

	brush->flag &= ~GP_BRUSH_USE_RANDOM_STRENGTH;
	brush->draw_strength = 1.0f;
	brush->flag &= ~GP_BRUSH_USE_STENGTH_PRESSURE;

	brush->draw_random_press = 0.0f;

	brush->draw_jitter = 0.0f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	brush->draw_angle = 0.0f;
	brush->draw_angle_factor = 0.0f;

	brush->draw_smoothfac = 1.1f;
	brush->draw_smoothlvl = 2;
	brush->sublevel = 2;
	brush->draw_random_sub = 0.0f;
	copy_v3_v3(brush->curcolor, curcolor);

	/* Ink Noise brush */
	brush = BKE_gpencil_brush_addnew(ts, "Ink noise", false);
	brush->thickness = 6.0f;
	brush->flag |= GP_BRUSH_USE_RANDOM_PRESSURE;
	brush->draw_sensitivity = 1.611f;
	brush->flag |= GP_BRUSH_USE_PRESSURE;
	brush->flag |= GP_BRUSH_ENABLE_CURSOR;

	brush->flag &= ~GP_BRUSH_USE_RANDOM_STRENGTH;
	brush->draw_strength = 1.0f;
	brush->flag |= GP_BRUSH_USE_STENGTH_PRESSURE;

	brush->draw_random_press = 1.0f;

	brush->draw_jitter = 0.0f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	brush->draw_angle = 0.0f;
	brush->draw_angle_factor = 0.0f;

	brush->draw_smoothfac = 1.1f;
	brush->draw_smoothlvl = 2;
	brush->sublevel = 2;
	brush->draw_random_sub = 0.0f;
	copy_v3_v3(brush->curcolor, curcolor);

	/* Marker brush */
	brush = BKE_gpencil_brush_addnew(ts, "Marker", false);
	brush->thickness = 10.0f;
	brush->flag &= ~GP_BRUSH_USE_RANDOM_PRESSURE;
	brush->draw_sensitivity = 2.0f;
	brush->flag &= ~GP_BRUSH_USE_PRESSURE;

	brush->flag &= ~GP_BRUSH_USE_RANDOM_STRENGTH;
	brush->draw_strength = 1.0f;
	brush->flag &= ~GP_BRUSH_USE_STENGTH_PRESSURE;
	brush->flag |= GP_BRUSH_ENABLE_CURSOR;

	brush->draw_random_press = 0.0f;

	brush->draw_jitter = 0.0f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	brush->draw_angle = M_PI_4; /* 45 degrees */
	brush->draw_angle_factor = 1.0f;

	brush->draw_smoothfac = 1.0f;
	brush->draw_smoothlvl = 2;
	brush->sublevel = 2;
	brush->draw_random_sub = 0.0f;
	copy_v3_v3(brush->curcolor, curcolor);

	/* Crayon brush */
	brush = BKE_gpencil_brush_addnew(ts, "Crayon", false);
	brush->thickness = 10.0f;
	brush->flag &= ~GP_BRUSH_USE_RANDOM_PRESSURE;
	brush->draw_sensitivity = 3.0f;
	brush->flag &= ~GP_BRUSH_USE_PRESSURE;
	brush->flag |= GP_BRUSH_ENABLE_CURSOR;

	brush->flag &= ~GP_BRUSH_USE_RANDOM_STRENGTH;
	brush->draw_strength = 0.140f;
	brush->flag |= GP_BRUSH_USE_STENGTH_PRESSURE;

	brush->draw_random_press = 0.0f;

	brush->draw_jitter = 0.0f;
	brush->flag |= GP_BRUSH_USE_JITTER_PRESSURE;

	brush->draw_angle = 0.0f;
	brush->draw_angle_factor = 0.0f;

	brush->draw_smoothfac = 0.0f;
	brush->draw_smoothlvl = 1;
	brush->sublevel = 2;
	brush->draw_random_sub = 0.5f;
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
	brush->flag |= GP_BRUSH_USE_PRESSURE;
	brush->draw_sensitivity = 1.0f;
	brush->draw_strength = 1.0f;
	brush->flag |= GP_BRUSH_USE_STENGTH_PRESSURE;
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

/* add a new gp-datablock */
bGPdata *BKE_gpencil_data_addnew(const char name[])
{
	bGPdata *gpd;
	
	/* allocate memory for a new block */
	gpd = BKE_libblock_alloc(G.main, ID_GD, name, 0);
	
	/* initial settings */
	gpd->flag = (GP_DATA_DISPINFO | GP_DATA_EXPAND);
	
	/* for now, stick to view is also enabled by default
	 * since this is more useful...
	 */
	gpd->flag |= GP_DATA_VIEWALIGN;
	gpd->xray_mode = GP_XRAY_3DSPACE;
	gpd->batch_cache_data = NULL;
	gpd->pixfactor = GP_DEFAULT_PIX_FACTOR;
	ARRAY_SET_ITEMS(gpd->line_color, 0.6f, 0.6f, 0.6f, 0.3f);
	/* onion-skinning settings */
	gpd->onion_flag |= (GP_ONION_GHOST_PREVCOL | GP_ONION_GHOST_NEXTCOL);
	gpd->onion_flag |= GP_ONION_FADE;
	gpd->onion_factor = 0.5f;
	ARRAY_SET_ITEMS(gpd->gcolor_prev, 0.145098f, 0.419608f, 0.137255f); /* green */
	ARRAY_SET_ITEMS(gpd->gcolor_next, 0.125490f, 0.082353f, 0.529412f); /* blue */

	return gpd;
}

/* -------- Data Duplication ---------- */
/* make a copy of a given gpencil point weights*/
void BKE_gpencil_stroke_weights_duplicate(bGPDstroke *gps_src, bGPDstroke *gps_dst)
{
	if (gps_src == NULL) {
		return;
	}
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

/* make a copy of a given gpencil frame and copy colors too */
bGPDframe *BKE_gpencil_frame_color_duplicate(const bGPDframe *gpf_src)
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
 * You probably nerver want to use that directly, use id_copy or BKE_id_copy_ex for typical needs.
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

	/* copy palettes */
	BLI_listbase_clear(&gpd_dst->palettes);
	for (const bGPDpalette *palette_src = gpd_src->palettes.first; palette_src; palette_src = palette_src->next) {
		bGPDpalette *palette_dst = BKE_gpencil_palette_duplicate(palette_src);  /* TODO here too could add unused flags... */
		BLI_addtail(&gpd_dst->palettes, palette_dst);
	}
}

/* make a copy of a given gpencil datablock */
bGPdata *BKE_gpencil_data_duplicate(Main *bmain, const bGPdata *gpd_src, bool internal_copy)
{
	const bGPDlayer *gpl_src;
	bGPDlayer *gpl_dst;
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
	
	/* copy layers */
	BLI_listbase_clear(&gpd_dst->layers);
	for (gpl_src = gpd_src->layers.first; gpl_src; gpl_src = gpl_src->next) {
		/* make a copy of source layer and its data */
		gpl_dst = BKE_gpencil_layer_duplicate(gpl_src);
		BLI_addtail(&gpd_dst->layers, gpl_dst);
	}
	
	/* return new */
	return gpd_dst;
}

void BKE_gpencil_make_local(Main *bmain, bGPdata *gpd, const bool lib_local)
{
	BKE_id_make_local_generic(bmain, &gpd->id, true, lib_local);
}

/* -------- GP-Stroke API --------- */

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

/* -------- GP-Frame API ---------- */

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

/* -------- GP-Layer API ---------- */

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
	else
		gpl->actframe = NULL;
	
	/* free the frame and its data */
	changed = BKE_gpencil_free_strokes(gpf);
	BLI_freelinkN(&gpl->frames, gpf);
	
	if (changed) {
		BKE_gpencil_batch_cache_alldirty();
	}

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
void BKE_gpencil_palettecolor_delete_allstrokes(bContext *C, PaletteColor *palcolor)
{
	bGPdata *gpd;
	bGPDlayer *gpl;
	bGPDframe *gpf;
	bGPDstroke *gps, *gpsn;

	Main *bmain = CTX_data_main(C);

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

/**
* Helper heuristic for determining if a path is compatible with the basepath
*
* \param path Full RNA-path from some data (usually an F-Curve) to compare
* \param basepath Shorter path fragment to look for
* \return Whether there is a match
*/
static bool UNUSED_FUNCTION(gp_animpath_matches_basepath)(const char path[], const char basepath[])
{
	/* we need start of path to be basepath */
	return (path && basepath) && STRPREFIX(path, basepath);
}

/* Transfer the animation data from bGPDpalette to Palette */
void BKE_gpencil_move_animdata_to_palettes(bContext *C, bGPdata *gpd)
{
	Main *bmain = CTX_data_main(C);
	Palette *palette = NULL;
	AnimData *srcAdt = NULL, *dstAdt = NULL;
	FCurve *fcu = NULL;
	char info[64];

	/* sanity checks */
	if (ELEM(NULL, gpd)) {
		if (G.debug & G_DEBUG)
			printf("ERROR: no source ID to separate AnimData with\n");
		return;
	}
	/* get animdata from src, and create for destination (if needed) */
	srcAdt = BKE_animdata_from_id((ID *)gpd);
	if (ELEM(NULL, srcAdt)) {
		if (G.debug & G_DEBUG)
			printf("ERROR: no source AnimData\n");
		return;
	}

	/* find first palette */
	for (fcu = srcAdt->action->curves.first; fcu; fcu = fcu->next) {
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
	if (srcAdt->action) {
		/* get animdata from destination or create (if needed) */
		dstAdt = BKE_animdata_add_id((ID *) palette);
		if (ELEM(NULL, dstAdt)) {
			if (G.debug & G_DEBUG)
				printf("ERROR: no AnimData for destination palette\n");
			return;
		}

		/* create destination action */
		dstAdt->action = add_empty_action(G.main, srcAdt->action->id.name + 2);
		/* move fcurves */
		action_move_fcurves_by_basepath(srcAdt->action, dstAdt->action, "palettes");

		/* loop over base paths, to fix for each one... */
		for (fcu = dstAdt->action->curves.first; fcu; fcu = fcu->next) {
			if (strncmp("palette", fcu->rna_path, 7) == 0) {
				int x = strcspn(fcu->rna_path, ".") + 1;
				BLI_strncpy(fcu->rna_path, fcu->rna_path + x, strlen(fcu->rna_path));
			}
		}
	}
}

/* Change draw manager status in all gpd datablocks */
void BKE_gpencil_batch_cache_alldirty()
{
	bGPdata *gpd;
	Main *bmain = G.main;

	for (gpd = bmain->gpencil.first; gpd; gpd = gpd->id.next) {
		BKE_gpencil_batch_cache_dirty(gpd);
	}
}

/* get stroke min max values */
void static gpencil_minmax(bGPdata *gpd, float min[3], float max[3])
{
	int i;
	bGPDspoint *pt;
	bGPDframe *gpf;
	INIT_MINMAX(min, max);

	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		gpf= gpl->actframe;
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

	bb = ob->bb;
	gpd= ob->gpd;

	gpencil_minmax(gpd, min, max);
	BKE_boundbox_init_from_minmax(bb, min, max);

	bb->flag &= ~BOUNDBOX_DIRTY;
}

/* get bounding box */
BoundBox *BKE_gpencil_boundbox_get(Object *ob)
{
	if ((!ob) || (ob->gpd == NULL))
		return NULL;

	if ((ob->bb) && ((ob->bb->flag & BOUNDBOX_DIRTY) == 0) && ((ob->gpd->flag & GP_DATA_CACHE_IS_DIRTY) == 0))
		return ob->bb;

	boundbox_gpencil(ob);

	return ob->bb;
}
/********************  Vertex Groups **********************************/
/* remove a vertex group */
void BKE_gpencil_vgroup_remove(Object *ob, bDeformGroup *defgroup)
{
	bGPdata *gpd = ob->gpd;
	bGPDspoint *pt = NULL;
	bGPDweight *gpw = NULL;
	const int def_nr = BLI_findindex(&ob->defbase, defgroup);

	/* Remove points data */
	if (gpd) {
		for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
			for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
				for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
					for (int i = 0; i < gps->totpoints; ++i) {
						pt = &gps->points[i];
						for (int i2 = 0; i2 < pt->totweight; ++i2) {
							gpw = &pt->weights[i2];
							if (gpw->index == def_nr) {
								BKE_gpencil_vgroup_remove_point_weight(pt, def_nr);
							}
							/* if index is greater, must be moved one back */
							if (gpw->index > def_nr) {
								--gpw->index;
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
	for (int i = 0; i < pt->totweight; ++i) {
		tmp_gpw = &pt->weights[i];
		if (tmp_gpw->index == index) {
			tmp_gpw->factor = weight;
			return tmp_gpw;
		}
	}

	++pt->totweight;
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
	for (int i = 0; i < pt->totweight; ++i) {
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

	// realloc weights 
	bGPDweight *tmp = MEM_dupallocN(pt->weights);
	MEM_SAFE_FREE(pt->weights);
	pt->weights = MEM_callocN(sizeof(bGPDweight) * pt->totweight - 1, "gp_weights");

	for (int x = 0; x < pt->totweight; ++x) {
		bGPDweight *gpw = &tmp[e];
		bGPDweight *final_gpw = &pt->weights[e];
		if (gpw->index != index) {
			final_gpw->index = gpw->index;
			final_gpw->factor = gpw->factor;
			++e;
		}
	}
	MEM_SAFE_FREE(tmp);
	--pt->totweight;

	return true;
}

/* helpers to create the monkey */
static void gpencil_add_points(bGPDstroke *gps, float *array, int totpoints)
{
	for (int i = 0; i < totpoints; ++i) {
		bGPDspoint *pt = &gps->points[i];
		int x = 5 * i;
		pt->x = array[x];
		pt->y = array[x + 1];
		pt->z = array[x + 2];
		pt->pressure = array[x + 3];
		pt->strength = array[x + 4];
	}
}

static bGPDstroke *gpencil_add_stroke(bGPDframe *gpf, Palette *palette, PaletteColor *palcolor, int totpoints, 
	char *colorname, short thickness)
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

/* add a 2D Suzanne (original model created by Matias Mendiola) */
void BKE_gpencil_create_monkey(bContext *C, bGPdata *gpd)
{
	bGPDstroke *gps;
	Scene *scene = CTX_data_scene(C);
	/* create palette and colors */
	Palette *palette = BKE_palette_add(G.main, "Palette");

	PaletteColor *color_Black = BKE_palette_color_add_name(palette, "Black");
	ARRAY_SET_ITEMS(color_Black->rgb, 0.0f, 0.0f, 0.0f, 1.0f);
	ARRAY_SET_ITEMS(color_Black->fill, 0.0f, 0.0f, 0.0f, 0.0f);
	PaletteColor *color_Skin = BKE_palette_color_add_name(palette, "Skin");
	ARRAY_SET_ITEMS(color_Skin->rgb, 0.553f, 0.39f, 0.266f, 0.0f);
	ARRAY_SET_ITEMS(color_Skin->fill, 0.733f, 0.567f, 0.359f, 1.0f);
	PaletteColor *color_Skin_Light = BKE_palette_color_add_name(palette, "Skin_Light");
	ARRAY_SET_ITEMS(color_Skin_Light->rgb, 0.553f, 0.39f, 0.266f, 0.0f);
	ARRAY_SET_ITEMS(color_Skin_Light->fill, 0.913f, 0.828f, 0.637f, 1.0f);
	PaletteColor *color_Skin_Shadow = BKE_palette_color_add_name(palette, "Skin_Shadow");
	ARRAY_SET_ITEMS(color_Skin_Shadow->rgb, 0.553f, 0.39f, 0.266f, 0.0f);
	ARRAY_SET_ITEMS(color_Skin_Shadow->fill, 0.32f, 0.29f, 0.223f, 1.0f);
	PaletteColor *color_Eyes = BKE_palette_color_add_name(palette, "Eyes");
	ARRAY_SET_ITEMS(color_Eyes->rgb, 0.553f, 0.39f, 0.266f, 0.0f);
	ARRAY_SET_ITEMS(color_Eyes->fill, 0.773f, 0.762f, 0.73f, 1.0f);
	PaletteColor *color_Pupils = BKE_palette_color_add_name(palette, "Pupils");
	ARRAY_SET_ITEMS(color_Pupils->rgb, 0.107f, 0.075f, 0.051f, 0.0f);
	ARRAY_SET_ITEMS(color_Pupils->fill, 0.153f, 0.057f, 0.063f, 1.0f);

	/* layers */
	bGPDlayer *Colors = BKE_gpencil_layer_addnew(gpd, "Colors", false);
	bGPDlayer *Lines = BKE_gpencil_layer_addnew(gpd, "Lines", true);

	/* frames */
	bGPDframe *frameColor = BKE_gpencil_frame_addnew(Colors, CFRA);
	bGPDframe *frameLines = BKE_gpencil_frame_addnew(Lines, CFRA);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin, 538, "Skin", 3);
	float data0[538 * 5] = { -0.509f, 0.0f, -0.156f, 0.267f, 0.362f, -0.522f, 0.0f, -0.159f, 0.31f, 0.407f, -0.531f, 0.0f, -0.16f, 0.347f, 0.426f, -0.543f, -0.0f, -0.162f, 0.38f, 0.439f,
		-0.554f, -0.0f, -0.163f, 0.409f, 0.448f, -0.566f, -0.0f, -0.165f, 0.433f, 0.458f, -0.578f, -0.0f, -0.167f, 0.454f, 0.478f, -0.591f, -0.0f, -0.168f, 0.471f, 0.5f,
		-0.604f, -0.0f, -0.169f, 0.485f, 0.51f, -0.619f, -0.0f, -0.171f, 0.496f, 0.516f, -0.634f, -0.0f, -0.171f, 0.504f, 0.519f, -0.649f, -0.0f, -0.171f, 0.511f, 0.519f,
		-0.665f, -0.0f, -0.17f, 0.516f, 0.521f, -0.681f, -0.0f, -0.17f, 0.521f, 0.53f, -0.697f, -0.0f, -0.169f, 0.524f, 0.533f, -0.713f, -0.0f, -0.167f, 0.527f, 0.533f,
		-0.729f, 0.0f, -0.165f, 0.53f, 0.534f, -0.745f, 0.0f, -0.161f, 0.531f, 0.534f, -0.761f, 0.0f, -0.157f, 0.533f, 0.535f, -0.777f, 0.0f, -0.153f, 0.534f, 0.535f,
		-0.792f, 0.0f, -0.148f, 0.535f, 0.536f, -0.808f, 0.0f, -0.144f, 0.535f, 0.535f, -0.822f, 0.0f, -0.139f, 0.536f, 0.537f, -0.837f, 0.0f, -0.133f, 0.536f, 0.537f,
		-0.852f, 0.0f, -0.128f, 0.536f, 0.537f, -0.866f, 0.0f, -0.122f, 0.536f, 0.537f, -0.88f, 0.0f, -0.115f, 0.536f, 0.537f, -0.894f, 0.0f, -0.109f, 0.536f, 0.537f,
		-0.908f, 0.0f, -0.101f, 0.535f, 0.535f, -0.922f, 0.0f, -0.092f, 0.535f, 0.535f, -0.936f, 0.0f, -0.082f, 0.534f, 0.534f, -0.949f, 0.0f, -0.072f, 0.534f, 0.534f,
		-0.963f, 0.0f, -0.061f, 0.534f, 0.534f, -0.976f, 0.0f, -0.05f, 0.534f, 0.534f, -0.988f, 0.0f, -0.039f, 0.534f, 0.534f, -1.0f, 0.0f, -0.028f, 0.533f, 0.534f,
		-1.011f, 0.0f, -0.017f, 0.533f, 0.533f, -1.022f, 0.0f, -0.007f, 0.533f, 0.534f, -1.033f, 0.0f, 0.004f, 0.533f, 0.533f, -1.043f, 0.0f, 0.014f, 0.532f, 0.532f,
		-1.053f, 0.0f, 0.025f, 0.532f, 0.532f, -1.062f, 0.0f, 0.036f, 0.531f, 0.531f, -1.071f, 0.0f, 0.046f, 0.531f, 0.531f, -1.078f, 0.0f, 0.057f, 0.531f, 0.531f,
		-1.085f, 0.0f, 0.068f, 0.531f, 0.531f, -1.092f, 0.0f, 0.08f, 0.532f, 0.532f, -1.098f, 0.0f, 0.091f, 0.533f, 0.533f, -1.104f, 0.0f, 0.105f, 0.535f, 0.535f,
		-1.11f, 0.0f, 0.119f, 0.539f, 0.539f, -1.115f, 0.0f, 0.133f, 0.54f, 0.54f, -1.118f, 0.0f, 0.148f, 0.541f, 0.541f, -1.121f, 0.0f, 0.162f, 0.542f, 0.542f,
		-1.123f, 0.0f, 0.177f, 0.542f, 0.542f, -1.125f, 0.0f, 0.193f, 0.543f, 0.543f, -1.125f, 0.0f, 0.208f, 0.543f, 0.543f, -1.125f, 0.0f, 0.225f, 0.543f, 0.543f,
		-1.124f, 0.0f, 0.241f, 0.545f, 0.545f, -1.122f, 0.0f, 0.258f, 0.546f, 0.546f, -1.119f, 0.0f, 0.274f, 0.548f, 0.548f, -1.116f, 0.0f, 0.29f, 0.549f, 0.549f,
		-1.111f, 0.0f, 0.305f, 0.549f, 0.549f, -1.106f, 0.0f, 0.318f, 0.549f, 0.549f, -1.1f, 0.0f, 0.33f, 0.549f, 0.549f, -1.094f, 0.0f, 0.34f, 0.549f, 0.549f,
		-1.087f, 0.0f, 0.349f, 0.55f, 0.55f, -1.08f, 0.0f, 0.357f, 0.549f, 0.549f, -1.072f, 0.0f, 0.365f, 0.55f, 0.55f, -1.063f, 0.0f, 0.372f, 0.551f, 0.551f,
		-1.054f, 0.0f, 0.379f, 0.552f, 0.552f, -1.044f, 0.0f, 0.385f, 0.553f, 0.553f, -1.034f, 0.0f, 0.391f, 0.553f, 0.553f, -1.024f, 0.0f, 0.396f, 0.554f, 0.554f,
		-1.013f, 0.0f, 0.401f, 0.554f, 0.554f, -1.003f, 0.0f, 0.405f, 0.554f, 0.554f, -0.991f, 0.0f, 0.409f, 0.554f, 0.554f, -0.978f, 0.0f, 0.412f, 0.555f, 0.555f,
		-0.964f, -0.0f, 0.414f, 0.555f, 0.555f, -0.949f, -0.0f, 0.414f, 0.556f, 0.556f, -0.934f, -0.0f, 0.413f, 0.556f, 0.556f, -0.919f, -0.0f, 0.412f, 0.557f, 0.557f,
		-0.905f, -0.0f, 0.41f, 0.557f, 0.557f, -0.892f, -0.0f, 0.406f, 0.557f, 0.557f, -0.879f, -0.0f, 0.402f, 0.557f, 0.558f, -0.867f, -0.0f, 0.398f, 0.557f, 0.557f,
		-0.855f, -0.0f, 0.394f, 0.557f, 0.557f, -0.843f, -0.0f, 0.388f, 0.557f, 0.557f, -0.831f, -0.0f, 0.381f, 0.558f, 0.557f, -0.82f, -0.0f, 0.375f, 0.558f, 0.557f,
		-0.81f, -0.0f, 0.368f, 0.558f, 0.558f, -0.801f, -0.0f, 0.362f, 0.558f, 0.558f, -0.793f, -0.0f, 0.357f, 0.557f, 0.559f, -0.784f, 0.0f, 0.353f, 0.557f, 0.559f,
		-0.776f, 0.0f, 0.35f, 0.556f, 0.559f, -0.768f, 0.0f, 0.348f, 0.556f, 0.559f, -0.76f, 0.0f, 0.346f, 0.555f, 0.559f, -0.752f, 0.0f, 0.346f, 0.554f, 0.559f,
		-0.744f, 0.0f, 0.347f, 0.553f, 0.554f, -0.737f, 0.0f, 0.348f, 0.552f, 0.548f, -0.729f, 0.0f, 0.351f, 0.551f, 0.544f, -0.723f, 0.0f, 0.355f, 0.551f, 0.546f,
		-0.716f, 0.0f, 0.36f, 0.55f, 0.546f, -0.709f, 0.0f, 0.366f, 0.55f, 0.547f, -0.702f, 0.0f, 0.372f, 0.549f, 0.547f, -0.696f, 0.0f, 0.379f, 0.549f, 0.547f,
		-0.689f, 0.0f, 0.386f, 0.549f, 0.548f, -0.683f, 0.0f, 0.394f, 0.549f, 0.548f, -0.676f, 0.0f, 0.403f, 0.549f, 0.549f, -0.67f, 0.0f, 0.413f, 0.549f, 0.548f,
		-0.664f, 0.0f, 0.422f, 0.549f, 0.549f, -0.658f, 0.0f, 0.432f, 0.55f, 0.549f, -0.652f, 0.0f, 0.441f, 0.551f, 0.548f, -0.646f, 0.0f, 0.451f, 0.552f, 0.548f,
		-0.639f, 0.0f, 0.46f, 0.554f, 0.548f, -0.632f, 0.0f, 0.469f, 0.556f, 0.549f, -0.624f, 0.0f, 0.478f, 0.559f, 0.549f, -0.616f, 0.0f, 0.487f, 0.563f, 0.549f,
		-0.609f, 0.0f, 0.497f, 0.567f, 0.549f, -0.6f, 0.0f, 0.507f, 0.572f, 0.558f, -0.592f, 0.0f, 0.518f, 0.577f, 0.574f, -0.584f, 0.0f, 0.528f, 0.582f, 0.587f,
		-0.575f, 0.0f, 0.538f, 0.586f, 0.592f, -0.566f, 0.0f, 0.548f, 0.591f, 0.595f, -0.556f, 0.0f, 0.557f, 0.594f, 0.597f, -0.546f, 0.0f, 0.567f, 0.597f, 0.598f,
		-0.536f, 0.0f, 0.577f, 0.6f, 0.6f, -0.525f, 0.0f, 0.586f, 0.602f, 0.603f, -0.514f, 0.0f, 0.596f, 0.604f, 0.605f, -0.503f, 0.0f, 0.606f, 0.605f, 0.606f,
		-0.492f, 0.0f, 0.615f, 0.606f, 0.607f, -0.482f, 0.0f, 0.624f, 0.607f, 0.607f, -0.471f, 0.0f, 0.632f, 0.608f, 0.607f, -0.462f, 0.0f, 0.64f, 0.609f, 0.607f,
		-0.453f, 0.0f, 0.647f, 0.61f, 0.61f, -0.444f, 0.0f, 0.654f, 0.612f, 0.611f, -0.435f, 0.0f, 0.66f, 0.614f, 0.613f, -0.427f, 0.0f, 0.666f, 0.616f, 0.615f,
		-0.418f, 0.0f, 0.672f, 0.617f, 0.618f, -0.409f, 0.0f, 0.677f, 0.619f, 0.621f, -0.399f, 0.0f, 0.683f, 0.621f, 0.622f, -0.389f, 0.0f, 0.69f, 0.623f, 0.623f,
		-0.379f, 0.0f, 0.696f, 0.624f, 0.624f, -0.368f, 0.0f, 0.702f, 0.626f, 0.626f, -0.356f, 0.0f, 0.708f, 0.628f, 0.628f, -0.345f, 0.0f, 0.713f, 0.63f, 0.63f,
		-0.333f, 0.0f, 0.719f, 0.633f, 0.631f, -0.32f, 0.0f, 0.724f, 0.637f, 0.632f, -0.307f, 0.0f, 0.729f, 0.641f, 0.64f, -0.294f, 0.0f, 0.732f, 0.646f, 0.644f,
		-0.281f, 0.0f, 0.736f, 0.65f, 0.655f, -0.268f, 0.0f, 0.739f, 0.654f, 0.657f, -0.255f, 0.0f, 0.742f, 0.657f, 0.658f, -0.243f, 0.0f, 0.745f, 0.659f, 0.661f,
		-0.23f, 0.0f, 0.747f, 0.662f, 0.663f, -0.217f, 0.0f, 0.75f, 0.664f, 0.664f, -0.203f, 0.0f, 0.753f, 0.666f, 0.666f, -0.19f, 0.0f, 0.755f, 0.667f, 0.668f,
		-0.177f, 0.0f, 0.757f, 0.669f, 0.67f, -0.163f, 0.0f, 0.76f, 0.671f, 0.671f, -0.15f, 0.0f, 0.762f, 0.673f, 0.672f, -0.136f, 0.0f, 0.764f, 0.674f, 0.674f,
		-0.122f, 0.0f, 0.767f, 0.676f, 0.676f, -0.108f, 0.0f, 0.769f, 0.677f, 0.678f, -0.093f, 0.0f, 0.771f, 0.678f, 0.68f, -0.079f, 0.0f, 0.773f, 0.678f, 0.68f,
		-0.064f, 0.0f, 0.774f, 0.679f, 0.679f, -0.049f, 0.0f, 0.775f, 0.68f, 0.68f, -0.033f, 0.0f, 0.775f, 0.68f, 0.68f, -0.018f, 0.0f, 0.776f, 0.68f, 0.68f,
		-0.002f, 0.0f, 0.776f, 0.681f, 0.68f, 0.013f, 0.0f, 0.777f, 0.681f, 0.681f, 0.029f, 0.0f, 0.777f, 0.682f, 0.681f, 0.045f, 0.0f, 0.777f, 0.682f, 0.681f,
		0.061f, 0.0f, 0.777f, 0.683f, 0.683f, 0.077f, 0.0f, 0.776f, 0.683f, 0.683f, 0.094f, 0.0f, 0.775f, 0.684f, 0.684f, 0.11f, 0.0f, 0.774f, 0.685f, 0.683f,
		0.126f, 0.0f, 0.773f, 0.685f, 0.685f, 0.142f, 0.0f, 0.771f, 0.687f, 0.685f, 0.158f, 0.0f, 0.769f, 0.688f, 0.685f, 0.174f, 0.0f, 0.767f, 0.69f, 0.686f,
		0.19f, 0.0f, 0.765f, 0.691f, 0.692f, 0.206f, 0.0f, 0.762f, 0.693f, 0.694f, 0.222f, 0.0f, 0.757f, 0.695f, 0.696f, 0.238f, 0.0f, 0.752f, 0.697f, 0.697f,
		0.254f, 0.0f, 0.747f, 0.699f, 0.698f, 0.27f, 0.0f, 0.742f, 0.7f, 0.7f, 0.286f, 0.0f, 0.736f, 0.702f, 0.702f, 0.302f, 0.0f, 0.73f, 0.704f, 0.704f,
		0.318f, 0.0f, 0.724f, 0.705f, 0.71f, 0.335f, 0.0f, 0.717f, 0.707f, 0.71f, 0.351f, 0.0f, 0.709f, 0.708f, 0.71f, 0.367f, 0.0f, 0.701f, 0.709f, 0.711f,
		0.382f, 0.0f, 0.692f, 0.71f, 0.713f, 0.397f, 0.0f, 0.683f, 0.711f, 0.713f, 0.41f, 0.0f, 0.675f, 0.712f, 0.713f, 0.422f, 0.0f, 0.666f, 0.712f, 0.714f,
		0.434f, 0.0f, 0.658f, 0.713f, 0.714f, 0.446f, 0.0f, 0.649f, 0.714f, 0.714f, 0.458f, 0.0f, 0.641f, 0.714f, 0.714f, 0.47f, 0.0f, 0.632f, 0.715f, 0.715f,
		0.483f, 0.0f, 0.622f, 0.715f, 0.716f, 0.496f, 0.0f, 0.611f, 0.715f, 0.716f, 0.51f, 0.0f, 0.6f, 0.716f, 0.717f, 0.523f, 0.0f, 0.588f, 0.716f, 0.716f,
		0.536f, 0.0f, 0.576f, 0.717f, 0.717f, 0.55f, 0.0f, 0.563f, 0.717f, 0.717f, 0.564f, 0.0f, 0.549f, 0.717f, 0.717f, 0.577f, 0.0f, 0.536f, 0.718f, 0.717f,
		0.59f, 0.0f, 0.522f, 0.718f, 0.717f, 0.603f, 0.0f, 0.508f, 0.718f, 0.718f, 0.615f, 0.0f, 0.496f, 0.718f, 0.718f, 0.625f, 0.0f, 0.484f, 0.718f, 0.718f,
		0.635f, 0.0f, 0.473f, 0.719f, 0.718f, 0.645f, 0.0f, 0.461f, 0.719f, 0.718f, 0.654f, 0.0f, 0.45f, 0.719f, 0.718f, 0.662f, 0.0f, 0.44f, 0.719f, 0.719f,
		0.67f, 0.0f, 0.431f, 0.719f, 0.719f, 0.676f, 0.0f, 0.422f, 0.719f, 0.719f, 0.682f, 0.0f, 0.414f, 0.719f, 0.719f, 0.687f, 0.0f, 0.407f, 0.719f, 0.719f,
		0.692f, 0.0f, 0.4f, 0.719f, 0.719f, 0.697f, 0.0f, 0.394f, 0.719f, 0.719f, 0.701f, 0.0f, 0.388f, 0.718f, 0.718f, 0.705f, 0.0f, 0.383f, 0.718f, 0.717f,
		0.708f, 0.0f, 0.378f, 0.718f, 0.717f, 0.711f, 0.0f, 0.374f, 0.717f, 0.717f, 0.714f, 0.0f, 0.37f, 0.717f, 0.717f, 0.717f, 0.0f, 0.366f, 0.717f, 0.717f,
		0.719f, 0.0f, 0.362f, 0.718f, 0.717f, 0.722f, 0.0f, 0.359f, 0.718f, 0.718f, 0.724f, 0.0f, 0.356f, 0.718f, 0.717f, 0.727f, 0.0f, 0.352f, 0.717f, 0.719f,
		0.73f, 0.0f, 0.349f, 0.717f, 0.719f, 0.734f, 0.0f, 0.347f, 0.715f, 0.719f, 0.737f, 0.0f, 0.344f, 0.714f, 0.714f, 0.742f, 0.0f, 0.341f, 0.713f, 0.709f,
		0.746f, 0.0f, 0.339f, 0.714f, 0.707f, 0.751f, 0.0f, 0.336f, 0.718f, 0.704f, 0.757f, 0.0f, 0.334f, 0.724f, 0.705f, 0.763f, 0.0f, 0.332f, 0.732f, 0.705f,
		0.769f, -0.0f, 0.329f, 0.742f, 0.704f, 0.775f, -0.0f, 0.328f, 0.753f, 0.713f, 0.782f, -0.0f, 0.327f, 0.764f, 0.804f, 0.789f, -0.0f, 0.327f, 0.774f, 0.813f,
		0.797f, -0.0f, 0.327f, 0.783f, 0.815f, 0.805f, -0.0f, 0.328f, 0.791f, 0.815f, 0.814f, -0.0f, 0.329f, 0.797f, 0.816f, 0.823f, -0.0f, 0.331f, 0.802f, 0.815f,
		0.832f, 0.0f, 0.335f, 0.806f, 0.816f, 0.841f, 0.0f, 0.341f, 0.809f, 0.816f, 0.851f, 0.0f, 0.346f, 0.811f, 0.816f, 0.861f, 0.0f, 0.351f, 0.812f, 0.816f,
		0.871f, 0.0f, 0.356f, 0.813f, 0.815f, 0.881f, 0.0f, 0.361f, 0.814f, 0.816f, 0.893f, 0.0f, 0.365f, 0.814f, 0.816f, 0.906f, 0.0f, 0.368f, 0.814f, 0.817f,
		0.922f, 0.0f, 0.372f, 0.813f, 0.816f, 0.939f, 0.0f, 0.375f, 0.812f, 0.817f, 0.957f, 0.0f, 0.377f, 0.811f, 0.817f, 0.977f, 0.0f, 0.379f, 0.81f, 0.815f,
		0.995f, 0.0f, 0.38f, 0.808f, 0.813f, 1.012f, 0.0f, 0.379f, 0.806f, 0.807f, 1.028f, 0.0f, 0.377f, 0.803f, 0.803f, 1.042f, 0.0f, 0.374f, 0.8f, 0.801f,
		1.054f, 0.0f, 0.371f, 0.797f, 0.8f, 1.065f, 0.0f, 0.366f, 0.794f, 0.8f, 1.076f, 0.0f, 0.361f, 0.791f, 0.792f, 1.085f, 0.0f, 0.355f, 0.788f, 0.781f,
		1.093f, 0.0f, 0.348f, 0.785f, 0.781f, 1.1f, 0.0f, 0.34f, 0.783f, 0.78f, 1.106f, 0.0f, 0.33f, 0.782f, 0.78f, 1.113f, 0.0f, 0.321f, 0.781f, 0.778f,
		1.117f, 0.0f, 0.31f, 0.78f, 0.777f, 1.122f, -0.0f, 0.299f, 0.779f, 0.777f, 1.125f, -0.0f, 0.286f, 0.778f, 0.776f, 1.129f, -0.0f, 0.274f, 0.778f, 0.777f,
		1.131f, -0.0f, 0.262f, 0.778f, 0.777f, 1.132f, -0.0f, 0.249f, 0.777f, 0.777f, 1.134f, -0.0f, 0.237f, 0.777f, 0.778f, 1.134f, -0.0f, 0.225f, 0.777f, 0.778f,
		1.135f, -0.0f, 0.213f, 0.776f, 0.777f, 1.134f, -0.0f, 0.201f, 0.776f, 0.776f, 1.134f, -0.0f, 0.189f, 0.776f, 0.775f, 1.132f, -0.0f, 0.177f, 0.775f, 0.776f,
		1.13f, -0.0f, 0.164f, 0.775f, 0.775f, 1.129f, -0.0f, 0.152f, 0.774f, 0.774f, 1.126f, -0.0f, 0.141f, 0.774f, 0.773f, 1.122f, -0.0f, 0.13f, 0.774f, 0.772f,
		1.118f, -0.0f, 0.118f, 0.773f, 0.772f, 1.113f, -0.0f, 0.108f, 0.773f, 0.773f, 1.107f, -0.0f, 0.097f, 0.773f, 0.774f, 1.102f, -0.0f, 0.087f, 0.772f, 0.773f,
		1.095f, -0.0f, 0.077f, 0.772f, 0.773f, 1.088f, -0.0f, 0.067f, 0.771f, 0.772f, 1.081f, -0.0f, 0.057f, 0.771f, 0.773f, 1.073f, -0.0f, 0.048f, 0.77f, 0.772f,
		1.066f, -0.0f, 0.038f, 0.769f, 0.767f, 1.058f, -0.0f, 0.029f, 0.768f, 0.766f, 1.05f, -0.0f, 0.019f, 0.768f, 0.765f, 1.041f, -0.0f, 0.011f, 0.767f, 0.765f,
		1.032f, -0.0f, 0.003f, 0.767f, 0.766f, 1.023f, -0.0f, -0.004f, 0.766f, 0.765f, 1.013f, -0.0f, -0.011f, 0.766f, 0.765f, 1.003f, -0.0f, -0.019f, 0.765f, 0.766f,
		0.993f, -0.0f, -0.026f, 0.765f, 0.765f, 0.983f, -0.0f, -0.034f, 0.764f, 0.765f, 0.972f, -0.0f, -0.041f, 0.762f, 0.765f, 0.962f, -0.0f, -0.048f, 0.761f, 0.765f,
		0.951f, -0.0f, -0.055f, 0.759f, 0.762f, 0.94f, -0.0f, -0.063f, 0.756f, 0.761f, 0.929f, -0.0f, -0.07f, 0.754f, 0.755f, 0.918f, -0.0f, -0.078f, 0.751f, 0.751f,
		0.907f, -0.0f, -0.085f, 0.748f, 0.747f, 0.896f, -0.0f, -0.092f, 0.745f, 0.744f, 0.884f, -0.0f, -0.099f, 0.742f, 0.742f, 0.873f, -0.0f, -0.105f, 0.739f, 0.738f,
		0.861f, -0.0f, -0.11f, 0.736f, 0.737f, 0.849f, 0.0f, -0.115f, 0.733f, 0.731f, 0.836f, 0.0f, -0.119f, 0.73f, 0.73f, 0.823f, 0.0f, -0.124f, 0.728f, 0.727f,
		0.81f, 0.0f, -0.128f, 0.725f, 0.725f, 0.796f, 0.0f, -0.132f, 0.723f, 0.723f, 0.783f, 0.0f, -0.136f, 0.72f, 0.719f, 0.77f, 0.0f, -0.141f, 0.718f, 0.717f,
		0.756f, 0.0f, -0.145f, 0.715f, 0.712f, 0.742f, 0.0f, -0.15f, 0.713f, 0.708f, 0.728f, 0.0f, -0.152f, 0.711f, 0.707f, 0.713f, 0.0f, -0.155f, 0.709f, 0.706f,
		0.699f, 0.0f, -0.156f, 0.706f, 0.706f, 0.684f, 0.0f, -0.158f, 0.704f, 0.705f, 0.67f, 0.0f, -0.159f, 0.702f, 0.705f, 0.656f, 0.0f, -0.16f, 0.7f, 0.704f,
		0.642f, 0.0f, -0.161f, 0.698f, 0.702f, 0.628f, 0.0f, -0.161f, 0.695f, 0.698f, 0.614f, 0.0f, -0.162f, 0.693f, 0.695f, 0.6f, 0.0f, -0.162f, 0.691f, 0.691f,
		0.587f, 0.0f, -0.162f, 0.688f, 0.686f, 0.574f, 0.0f, -0.162f, 0.686f, 0.685f, 0.561f, 0.0f, -0.161f, 0.683f, 0.683f, 0.548f, 0.0f, -0.161f, 0.681f, 0.683f,
		0.535f, 0.0f, -0.161f, 0.678f, 0.678f, 0.523f, 0.0f, -0.16f, 0.676f, 0.676f, 0.512f, 0.0f, -0.16f, 0.673f, 0.674f, 0.501f, 0.0f, -0.16f, 0.671f, 0.67f,
		0.49f, 0.0f, -0.16f, 0.668f, 0.668f, 0.48f, 0.0f, -0.161f, 0.666f, 0.663f, 0.469f, 0.0f, -0.162f, 0.665f, 0.66f, 0.458f, 0.0f, -0.165f, 0.663f, 0.66f,
		0.447f, 0.0f, -0.167f, 0.662f, 0.659f, 0.437f, 0.0f, -0.171f, 0.661f, 0.659f, 0.426f, 0.0f, -0.175f, 0.66f, 0.659f, 0.415f, 0.0f, -0.18f, 0.66f, 0.659f,
		0.404f, 0.0f, -0.185f, 0.659f, 0.659f, 0.393f, 0.0f, -0.191f, 0.659f, 0.657f, 0.383f, 0.0f, -0.196f, 0.659f, 0.657f, 0.373f, 0.0f, -0.202f, 0.658f, 0.659f,
		0.363f, -0.0f, -0.208f, 0.658f, 0.658f, 0.353f, -0.0f, -0.215f, 0.658f, 0.659f, 0.344f, -0.0f, -0.223f, 0.658f, 0.659f, 0.336f, -0.0f, -0.23f, 0.658f, 0.659f,
		0.327f, -0.0f, -0.238f, 0.658f, 0.658f, 0.319f, -0.0f, -0.245f, 0.657f, 0.657f, 0.312f, -0.0f, -0.253f, 0.657f, 0.656f, 0.305f, -0.0f, -0.261f, 0.656f, 0.658f,
		0.299f, -0.0f, -0.269f, 0.655f, 0.658f, 0.293f, 0.0f, -0.278f, 0.653f, 0.657f, 0.288f, 0.0f, -0.287f, 0.65f, 0.657f, 0.283f, 0.0f, -0.295f, 0.646f, 0.656f,
		0.279f, 0.0f, -0.304f, 0.642f, 0.655f, 0.275f, 0.0f, -0.313f, 0.637f, 0.642f, 0.271f, 0.0f, -0.322f, 0.633f, 0.637f, 0.268f, 0.0f, -0.331f, 0.628f, 0.609f,
		0.265f, 0.0f, -0.341f, 0.624f, 0.607f, 0.263f, 0.0f, -0.35f, 0.62f, 0.608f, 0.261f, 0.0f, -0.359f, 0.617f, 0.608f, 0.259f, 0.0f, -0.369f, 0.614f, 0.607f,
		0.258f, 0.0f, -0.379f, 0.612f, 0.606f, 0.257f, 0.0f, -0.389f, 0.61f, 0.606f, 0.258f, 0.0f, -0.399f, 0.609f, 0.605f, 0.258f, 0.0f, -0.41f, 0.608f, 0.604f,
		0.26f, 0.0f, -0.421f, 0.608f, 0.606f, 0.263f, 0.0f, -0.431f, 0.607f, 0.606f, 0.266f, 0.0f, -0.441f, 0.607f, 0.606f, 0.27f, 0.0f, -0.452f, 0.606f, 0.607f,
		0.274f, 0.0f, -0.463f, 0.606f, 0.607f, 0.279f, 0.0f, -0.475f, 0.605f, 0.607f, 0.283f, 0.0f, -0.487f, 0.604f, 0.607f, 0.288f, 0.0f, -0.498f, 0.603f, 0.607f,
		0.293f, 0.0f, -0.511f, 0.601f, 0.607f, 0.297f, 0.0f, -0.523f, 0.598f, 0.606f, 0.301f, 0.0f, -0.536f, 0.595f, 0.605f, 0.305f, 0.0f, -0.549f, 0.591f, 0.602f,
		0.309f, 0.0f, -0.562f, 0.588f, 0.597f, 0.312f, 0.0f, -0.576f, 0.583f, 0.585f, 0.315f, 0.0f, -0.59f, 0.579f, 0.577f, 0.318f, 0.0f, -0.604f, 0.574f, 0.576f,
		0.321f, 0.0f, -0.618f, 0.569f, 0.57f, 0.323f, 0.0f, -0.633f, 0.564f, 0.564f, 0.326f, 0.0f, -0.647f, 0.559f, 0.554f, 0.328f, 0.0f, -0.663f, 0.555f, 0.549f,
		0.33f, 0.0f, -0.678f, 0.551f, 0.546f, 0.332f, 0.0f, -0.693f, 0.547f, 0.543f, 0.334f, 0.0f, -0.709f, 0.544f, 0.543f, 0.336f, 0.0f, -0.726f, 0.541f, 0.541f,
		0.338f, 0.0f, -0.742f, 0.538f, 0.54f, 0.338f, 0.0f, -0.758f, 0.536f, 0.538f, 0.338f, 0.0f, -0.773f, 0.534f, 0.53f, 0.337f, 0.0f, -0.787f, 0.532f, 0.528f,
		0.337f, 0.0f, -0.801f, 0.53f, 0.528f, 0.336f, 0.0f, -0.814f, 0.529f, 0.528f, 0.334f, 0.0f, -0.827f, 0.527f, 0.528f, 0.333f, 0.0f, -0.84f, 0.525f, 0.529f,
		0.331f, 0.0f, -0.853f, 0.523f, 0.529f, 0.328f, 0.0f, -0.866f, 0.521f, 0.528f, 0.324f, 0.0f, -0.877f, 0.519f, 0.516f, 0.32f, 0.0f, -0.889f, 0.516f, 0.515f,
		0.315f, 0.0f, -0.9f, 0.513f, 0.515f, 0.31f, 0.0f, -0.91f, 0.51f, 0.514f, 0.304f, 0.0f, -0.921f, 0.507f, 0.513f, 0.297f, 0.0f, -0.931f, 0.505f, 0.507f,
		0.289f, 0.0f, -0.94f, 0.502f, 0.498f, 0.281f, 0.0f, -0.948f, 0.499f, 0.494f, 0.272f, 0.0f, -0.956f, 0.497f, 0.491f, 0.262f, 0.0f, -0.963f, 0.495f, 0.49f,
		0.253f, 0.0f, -0.969f, 0.494f, 0.491f, 0.242f, 0.0f, -0.975f, 0.493f, 0.491f, 0.231f, 0.0f, -0.98f, 0.492f, 0.49f, 0.22f, 0.0f, -0.986f, 0.491f, 0.489f,
		0.208f, 0.0f, -0.99f, 0.491f, 0.49f, 0.195f, 0.0f, -0.994f, 0.491f, 0.491f, 0.181f, 0.0f, -0.998f, 0.491f, 0.491f, 0.168f, 0.0f, -1.001f, 0.491f, 0.492f,
		0.154f, 0.0f, -1.005f, 0.491f, 0.492f, 0.141f, 0.0f, -1.008f, 0.492f, 0.492f, 0.126f, 0.0f, -1.01f, 0.492f, 0.492f, 0.112f, 0.0f, -1.011f, 0.492f, 0.492f,
		0.097f, 0.0f, -1.013f, 0.492f, 0.492f, 0.081f, 0.0f, -1.013f, 0.492f, 0.492f, 0.066f, 0.0f, -1.014f, 0.493f, 0.493f, 0.05f, 0.0f, -1.014f, 0.493f, 0.494f,
		0.035f, 0.0f, -1.014f, 0.493f, 0.494f, 0.019f, 0.0f, -1.013f, 0.493f, 0.494f, 0.004f, 0.0f, -1.012f, 0.493f, 0.494f, -0.011f, 0.0f, -1.011f, 0.493f, 0.493f,
		-0.026f, 0.0f, -1.01f, 0.492f, 0.493f, -0.041f, 0.0f, -1.008f, 0.492f, 0.492f, -0.056f, 0.0f, -1.006f, 0.492f, 0.492f, -0.07f, 0.0f, -1.004f, 0.491f, 0.492f,
		-0.084f, 0.0f, -1.001f, 0.491f, 0.492f, -0.098f, 0.0f, -0.999f, 0.491f, 0.491f, -0.112f, 0.0f, -0.995f, 0.491f, 0.49f, -0.125f, 0.0f, -0.992f, 0.49f, 0.49f,
		-0.138f, 0.0f, -0.987f, 0.49f, 0.491f, -0.15f, 0.0f, -0.983f, 0.49f, 0.49f, -0.162f, 0.0f, -0.978f, 0.49f, 0.49f, -0.174f, 0.0f, -0.973f, 0.489f, 0.489f,
		-0.185f, 0.0f, -0.967f, 0.489f, 0.488f, -0.196f, 0.0f, -0.961f, 0.489f, 0.489f, -0.207f, 0.0f, -0.955f, 0.489f, 0.489f, -0.218f, 0.0f, -0.949f, 0.489f, 0.49f,
		-0.229f, 0.0f, -0.943f, 0.489f, 0.489f, -0.24f, 0.0f, -0.936f, 0.489f, 0.489f, -0.25f, 0.0f, -0.929f, 0.489f, 0.489f, -0.261f, 0.0f, -0.922f, 0.489f, 0.489f,
		-0.271f, 0.0f, -0.914f, 0.489f, 0.49f, -0.28f, 0.0f, -0.907f, 0.49f, 0.49f, -0.289f, 0.0f, -0.898f, 0.49f, 0.489f, -0.298f, 0.0f, -0.89f, 0.49f, 0.489f,
		-0.306f, 0.0f, -0.882f, 0.49f, 0.49f, -0.314f, 0.0f, -0.875f, 0.491f, 0.489f, -0.322f, 0.0f, -0.866f, 0.492f, 0.489f, -0.328f, 0.0f, -0.857f, 0.492f, 0.489f,
		-0.333f, 0.0f, -0.847f, 0.493f, 0.49f, -0.336f, 0.0f, -0.836f, 0.494f, 0.488f, -0.338f, 0.0f, -0.824f, 0.496f, 0.49f, -0.338f, 0.0f, -0.811f, 0.497f, 0.49f,
		-0.338f, 0.0f, -0.798f, 0.499f, 0.491f, -0.337f, 0.0f, -0.785f, 0.501f, 0.497f, -0.337f, 0.0f, -0.772f, 0.503f, 0.5f, -0.337f, 0.0f, -0.759f, 0.505f, 0.504f,
		-0.336f, -0.0f, -0.746f, 0.507f, 0.505f, -0.336f, -0.0f, -0.733f, 0.51f, 0.51f, -0.335f, -0.0f, -0.719f, 0.512f, 0.513f, -0.334f, -0.0f, -0.706f, 0.515f, 0.515f,
		-0.333f, -0.0f, -0.692f, 0.518f, 0.516f, -0.332f, -0.0f, -0.678f, 0.52f, 0.522f, -0.331f, -0.0f, -0.665f, 0.523f, 0.523f, -0.329f, -0.0f, -0.651f, 0.525f, 0.528f,
		-0.327f, -0.0f, -0.637f, 0.528f, 0.53f, -0.325f, -0.0f, -0.624f, 0.53f, 0.532f, -0.322f, -0.0f, -0.61f, 0.532f, 0.534f, -0.319f, -0.0f, -0.597f, 0.535f, 0.535f,
		-0.316f, -0.0f, -0.584f, 0.537f, 0.538f, -0.313f, -0.0f, -0.57f, 0.539f, 0.54f, -0.31f, -0.0f, -0.557f, 0.541f, 0.542f, -0.307f, -0.0f, -0.544f, 0.542f, 0.545f,
		-0.303f, -0.0f, -0.531f, 0.544f, 0.546f, -0.3f, -0.0f, -0.519f, 0.546f, 0.549f, -0.298f, -0.0f, -0.506f, 0.547f, 0.549f, -0.295f, -0.0f, -0.494f, 0.548f, 0.549f,
		-0.292f, -0.0f, -0.482f, 0.549f, 0.55f, -0.29f, -0.0f, -0.47f, 0.55f, 0.552f, -0.287f, -0.0f, -0.459f, 0.551f, 0.552f, -0.285f, -0.0f, -0.447f, 0.551f, 0.552f,
		-0.284f, -0.0f, -0.436f, 0.552f, 0.552f, -0.282f, -0.0f, -0.425f, 0.552f, 0.553f, -0.281f, -0.0f, -0.413f, 0.553f, 0.553f, -0.28f, -0.0f, -0.402f, 0.553f, 0.553f,
		-0.28f, -0.0f, -0.392f, 0.553f, 0.553f, -0.281f, -0.0f, -0.381f, 0.554f, 0.553f, -0.283f, -0.0f, -0.369f, 0.554f, 0.554f, -0.286f, -0.0f, -0.359f, 0.554f, 0.554f,
		-0.289f, -0.0f, -0.348f, 0.555f, 0.554f, -0.294f, -0.0f, -0.337f, 0.555f, 0.555f, -0.299f, -0.0f, -0.327f, 0.555f, 0.554f, -0.305f, -0.0f, -0.317f, 0.556f, 0.555f,
		-0.312f, -0.0f, -0.307f, 0.556f, 0.555f, -0.319f, -0.0f, -0.297f, 0.556f, 0.557f, -0.326f, 0.0f, -0.287f, 0.557f, 0.558f, -0.334f, 0.0f, -0.278f, 0.557f, 0.557f,
		-0.341f, 0.0f, -0.268f, 0.557f, 0.558f, -0.349f, 0.0f, -0.259f, 0.558f, 0.558f, -0.359f, 0.0f, -0.251f, 0.558f, 0.558f, -0.368f, 0.0f, -0.243f, 0.558f, 0.558f,
		-0.378f, 0.0f, -0.235f, 0.558f, 0.559f, -0.388f, 0.0f, -0.228f, 0.558f, 0.558f, -0.398f, 0.0f, -0.221f, 0.559f, 0.559f, -0.408f, 0.0f, -0.214f, 0.559f, 0.559f,
		-0.418f, 0.0f, -0.208f, 0.559f, 0.559f, -0.427f, 0.0f, -0.202f, 0.559f, 0.558f, -0.436f, 0.0f, -0.196f, 0.559f, 0.559f, -0.445f, 0.0f, -0.191f, 0.559f, 0.559f,
		-0.453f, 0.0f, -0.187f, 0.558f, 0.559f, -0.462f, 0.0f, -0.183f, 0.558f, 0.558f, -0.469f, 0.0f, -0.18f, 0.558f, 0.558f, -0.477f, 0.0f, -0.176f, 0.558f, 0.558f,
		-0.484f, 0.0f, -0.174f, 0.557f, 0.558f, -0.493f, 0.0f, -0.17f, 0.555f, 0.559f, };
	gpencil_add_points(gps, data0, 538);

	gps = gpencil_add_stroke(frameColor, palette, color_Eyes, 136, "Eyes", 3);
	float data1[136 * 5] = { -0.369f, 0.0f, -0.048f, 0.065f, 0.065f, -0.378f, 0.0f, -0.046f, 0.239f, 0.293f, -0.383f, 0.0f, -0.044f, 0.316f, 0.339f, -0.39f, 0.0f, -0.041f, 0.348f, 0.355f,
		-0.398f, 0.0f, -0.038f, 0.364f, 0.368f, -0.405f, 0.0f, -0.035f, 0.373f, 0.374f, -0.413f, 0.0f, -0.031f, 0.381f, 0.381f, -0.421f, 0.0f, -0.026f, 0.388f, 0.391f,
		-0.429f, 0.0f, -0.02f, 0.392f, 0.394f, -0.437f, 0.0f, -0.014f, 0.395f, 0.396f, -0.445f, 0.0f, -0.008f, 0.397f, 0.397f, -0.453f, 0.0f, -0.001f, 0.399f, 0.4f,
		-0.461f, 0.0f, 0.007f, 0.401f, 0.401f, -0.468f, -0.0f, 0.016f, 0.404f, 0.404f, -0.474f, 0.0f, 0.023f, 0.406f, 0.407f, -0.479f, 0.0f, 0.03f, 0.409f, 0.409f,
		-0.485f, 0.0f, 0.039f, 0.412f, 0.412f, -0.49f, 0.0f, 0.048f, 0.415f, 0.415f, -0.495f, 0.0f, 0.057f, 0.417f, 0.417f, -0.499f, 0.0f, 0.068f, 0.42f, 0.421f,
		-0.503f, 0.0f, 0.079f, 0.421f, 0.421f, -0.507f, -0.0f, 0.091f, 0.423f, 0.423f, -0.51f, -0.0f, 0.102f, 0.424f, 0.424f, -0.513f, -0.0f, 0.112f, 0.424f, 0.425f,
		-0.515f, -0.0f, 0.123f, 0.425f, 0.425f, -0.517f, -0.0f, 0.135f, 0.425f, 0.425f, -0.518f, -0.0f, 0.146f, 0.426f, 0.425f, -0.519f, -0.0f, 0.158f, 0.426f, 0.425f,
		-0.52f, -0.0f, 0.169f, 0.426f, 0.426f, -0.52f, -0.0f, 0.181f, 0.427f, 0.427f, -0.519f, -0.0f, 0.192f, 0.427f, 0.427f, -0.518f, -0.0f, 0.203f, 0.427f, 0.427f,
		-0.517f, -0.0f, 0.213f, 0.427f, 0.428f, -0.515f, -0.0f, 0.222f, 0.428f, 0.427f, -0.513f, -0.0f, 0.232f, 0.428f, 0.427f, -0.51f, -0.0f, 0.241f, 0.429f, 0.427f,
		-0.508f, -0.0f, 0.25f, 0.43f, 0.428f, -0.505f, -0.0f, 0.259f, 0.431f, 0.431f, -0.501f, -0.0f, 0.267f, 0.431f, 0.432f, -0.497f, -0.0f, 0.276f, 0.432f, 0.433f,
		-0.493f, -0.0f, 0.284f, 0.433f, 0.433f, -0.488f, -0.0f, 0.293f, 0.434f, 0.434f, -0.484f, -0.0f, 0.301f, 0.434f, 0.435f, -0.479f, -0.0f, 0.308f, 0.435f, 0.436f,
		-0.474f, -0.0f, 0.316f, 0.435f, 0.435f, -0.468f, -0.0f, 0.322f, 0.436f, 0.436f, -0.463f, -0.0f, 0.329f, 0.436f, 0.436f, -0.457f, -0.0f, 0.335f, 0.436f, 0.436f,
		-0.451f, -0.0f, 0.341f, 0.437f, 0.436f, -0.445f, -0.0f, 0.347f, 0.438f, 0.437f, -0.438f, -0.0f, 0.352f, 0.44f, 0.437f, -0.432f, -0.0f, 0.357f, 0.442f, 0.441f,
		-0.426f, 0.0f, 0.362f, 0.444f, 0.446f, -0.419f, 0.0f, 0.366f, 0.445f, 0.447f, -0.413f, 0.0f, 0.369f, 0.446f, 0.447f, -0.407f, 0.0f, 0.373f, 0.446f, 0.447f,
		-0.401f, 0.0f, 0.376f, 0.447f, 0.447f, -0.395f, 0.0f, 0.378f, 0.447f, 0.448f, -0.388f, 0.0f, 0.381f, 0.447f, 0.448f, -0.382f, 0.0f, 0.383f, 0.448f, 0.448f,
		-0.375f, 0.0f, 0.384f, 0.448f, 0.448f, -0.369f, 0.0f, 0.386f, 0.448f, 0.448f, -0.362f, 0.0f, 0.387f, 0.448f, 0.448f, -0.355f, 0.0f, 0.388f, 0.448f, 0.448f,
		-0.348f, 0.0f, 0.388f, 0.448f, 0.448f, -0.341f, 0.0f, 0.387f, 0.448f, 0.449f, -0.334f, 0.0f, 0.387f, 0.448f, 0.448f, -0.327f, 0.0f, 0.386f, 0.448f, 0.449f,
		-0.32f, 0.0f, 0.384f, 0.449f, 0.449f, -0.313f, 0.0f, 0.382f, 0.449f, 0.449f, -0.307f, 0.0f, 0.38f, 0.449f, 0.449f, -0.3f, 0.0f, 0.377f, 0.449f, 0.45f,
		-0.294f, 0.0f, 0.375f, 0.45f, 0.45f, -0.288f, -0.0f, 0.372f, 0.45f, 0.45f, -0.282f, -0.0f, 0.368f, 0.45f, 0.451f, -0.276f, -0.0f, 0.365f, 0.45f, 0.451f,
		-0.27f, -0.0f, 0.361f, 0.45f, 0.451f, -0.264f, -0.0f, 0.357f, 0.45f, 0.451f, -0.258f, -0.0f, 0.352f, 0.45f, 0.45f, -0.251f, -0.0f, 0.347f, 0.45f, 0.451f,
		-0.245f, -0.0f, 0.341f, 0.451f, 0.451f, -0.24f, -0.0f, 0.335f, 0.451f, 0.451f, -0.234f, -0.0f, 0.329f, 0.451f, 0.451f, -0.228f, -0.0f, 0.323f, 0.452f, 0.452f,
		-0.223f, -0.0f, 0.316f, 0.452f, 0.453f, -0.218f, -0.0f, 0.309f, 0.452f, 0.453f, -0.213f, -0.0f, 0.301f, 0.453f, 0.453f, -0.208f, -0.0f, 0.294f, 0.453f, 0.453f,
		-0.204f, -0.0f, 0.286f, 0.453f, 0.453f, -0.2f, -0.0f, 0.277f, 0.453f, 0.454f, -0.196f, -0.0f, 0.269f, 0.453f, 0.454f, -0.192f, -0.0f, 0.26f, 0.454f, 0.454f,
		-0.189f, -0.0f, 0.25f, 0.454f, 0.454f, -0.186f, -0.0f, 0.241f, 0.454f, 0.455f, -0.183f, -0.0f, 0.231f, 0.454f, 0.455f, -0.181f, -0.0f, 0.221f, 0.454f, 0.455f,
		-0.179f, -0.0f, 0.209f, 0.455f, 0.455f, -0.177f, -0.0f, 0.197f, 0.455f, 0.455f, -0.176f, -0.0f, 0.184f, 0.455f, 0.455f, -0.176f, -0.0f, 0.171f, 0.455f, 0.456f,
		-0.176f, -0.0f, 0.158f, 0.455f, 0.456f, -0.177f, -0.0f, 0.145f, 0.455f, 0.456f, -0.178f, -0.0f, 0.132f, 0.455f, 0.456f, -0.18f, -0.0f, 0.12f, 0.456f, 0.456f,
		-0.182f, -0.0f, 0.108f, 0.456f, 0.456f, -0.185f, -0.0f, 0.097f, 0.456f, 0.456f, -0.188f, -0.0f, 0.086f, 0.456f, 0.457f, -0.191f, -0.0f, 0.076f, 0.456f, 0.457f,
		-0.194f, -0.0f, 0.067f, 0.457f, 0.457f, -0.198f, -0.0f, 0.058f, 0.457f, 0.457f, -0.202f, -0.0f, 0.05f, 0.457f, 0.457f, -0.206f, -0.0f, 0.042f, 0.457f, 0.457f,
		-0.21f, -0.0f, 0.034f, 0.458f, 0.457f, -0.215f, -0.0f, 0.027f, 0.458f, 0.457f, -0.22f, -0.0f, 0.02f, 0.458f, 0.458f, -0.225f, -0.0f, 0.014f, 0.458f, 0.458f,
		-0.23f, -0.0f, 0.007f, 0.458f, 0.458f, -0.235f, -0.0f, 0.002f, 0.459f, 0.458f, -0.24f, -0.0f, -0.004f, 0.459f, 0.458f, -0.246f, -0.0f, -0.009f, 0.46f, 0.459f,
		-0.251f, 0.0f, -0.013f, 0.464f, 0.463f, -0.257f, 0.0f, -0.018f, 0.467f, 0.468f, -0.262f, 0.0f, -0.022f, 0.469f, 0.469f, -0.268f, 0.0f, -0.026f, 0.471f, 0.47f,
		-0.274f, 0.0f, -0.029f, 0.477f, 0.478f, -0.28f, 0.0f, -0.033f, 0.478f, 0.478f, -0.286f, 0.0f, -0.036f, 0.478f, 0.478f, -0.292f, 0.0f, -0.038f, 0.479f, 0.479f,
		-0.298f, 0.0f, -0.041f, 0.48f, 0.48f, -0.305f, 0.0f, -0.043f, 0.48f, 0.48f, -0.311f, 0.0f, -0.045f, 0.482f, 0.482f, -0.318f, 0.0f, -0.047f, 0.482f, 0.482f,
		-0.324f, 0.0f, -0.048f, 0.482f, 0.482f, -0.331f, 0.0f, -0.049f, 0.48f, 0.482f, -0.336f, 0.0f, -0.05f, 0.457f, 0.485f, -0.344f, 0.0f, -0.05f, 0.32f, 0.32f,
	};
	gpencil_add_points(gps, data1, 136);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin, 2, "Skin", 3);
	float data2[2 * 5] = { -0.512f, 0.0f, -0.168f, 0.545f, 0.557f, -0.521f, 0.0f, -0.167f, 0.535f, 0.558f, };
	gpencil_add_points(gps, data2, 2);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Light, 1, "Skin_Light", 3);
	float data3[1 * 5] = { -1.014f, 0.0f, 0.186f, 0.0f, 0.003f, };
	gpencil_add_points(gps, data3, 1);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Light, 1, "Skin_Light", 3);
	float data4[1 * 5] = { -1.014f, 0.0f, 0.186f, 0.02f, 0.02f, };
	gpencil_add_points(gps, data4, 1);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Light, 48, "Skin_Light", 3);
	float data5[48 * 5] = { -1.014f, 0.0f, 0.187f, 0.066f, 0.066f, -1.013f, 0.0f, 0.2f, 0.222f, 0.356f, -1.01f, 0.0f, 0.208f, 0.295f, 0.404f, -1.006f, 0.0f, 0.218f, 0.354f, 0.431f,
		-1.001f, 0.0f, 0.226f, 0.392f, 0.445f, -0.994f, 0.0f, 0.233f, 0.418f, 0.453f, -0.987f, 0.0f, 0.238f, 0.437f, 0.457f, -0.979f, 0.0f, 0.242f, 0.45f, 0.47f,
		-0.97f, 0.0f, 0.245f, 0.459f, 0.473f, -0.96f, -0.0f, 0.246f, 0.465f, 0.474f, -0.951f, -0.0f, 0.245f, 0.469f, 0.475f, -0.942f, 0.0f, 0.242f, 0.471f, 0.473f,
		-0.932f, 0.0f, 0.239f, 0.472f, 0.474f, -0.924f, 0.0f, 0.234f, 0.471f, 0.474f, -0.915f, 0.0f, 0.228f, 0.469f, 0.474f, -0.906f, 0.0f, 0.22f, 0.464f, 0.47f,
		-0.898f, 0.0f, 0.212f, 0.458f, 0.46f, -0.89f, 0.0f, 0.203f, 0.451f, 0.453f, -0.882f, 0.0f, 0.193f, 0.443f, 0.443f, -0.875f, 0.0f, 0.182f, 0.435f, 0.437f,
		-0.869f, 0.0f, 0.172f, 0.426f, 0.428f, -0.863f, 0.0f, 0.161f, 0.417f, 0.415f, -0.858f, 0.0f, 0.148f, 0.409f, 0.41f, -0.854f, 0.0f, 0.137f, 0.399f, 0.399f,
		-0.85f, 0.0f, 0.126f, 0.39f, 0.392f, -0.847f, 0.0f, 0.116f, 0.379f, 0.386f, -0.846f, 0.0f, 0.109f, 0.369f, 0.371f, -0.846f, 0.0f, 0.104f, 0.361f, 0.357f,
		-0.847f, 0.0f, 0.101f, 0.355f, 0.339f, -0.849f, 0.0f, 0.101f, 0.353f, 0.334f, -0.853f, 0.0f, 0.103f, 0.354f, 0.345f, -0.859f, 0.0f, 0.108f, 0.357f, 0.35f,
		-0.865f, 0.0f, 0.116f, 0.363f, 0.365f, -0.873f, 0.0f, 0.126f, 0.369f, 0.375f, -0.881f, 0.0f, 0.137f, 0.375f, 0.379f, -0.89f, 0.0f, 0.149f, 0.381f, 0.38f,
		-0.899f, 0.0f, 0.159f, 0.387f, 0.385f, -0.908f, 0.0f, 0.168f, 0.394f, 0.394f, -0.919f, 0.0f, 0.177f, 0.401f, 0.398f, -0.932f, 0.0f, 0.184f, 0.409f, 0.404f,
		-0.945f, 0.0f, 0.191f, 0.418f, 0.415f, -0.958f, 0.0f, 0.195f, 0.427f, 0.431f, -0.969f, 0.0f, 0.197f, 0.434f, 0.443f, -0.979f, 0.0f, 0.197f, 0.436f, 0.445f,
		-0.987f, 0.0f, 0.195f, 0.428f, 0.463f, -0.995f, 0.0f, 0.192f, 0.398f, 0.46f, -1.001f, 0.0f, 0.189f, 0.345f, 0.465f, -1.01f, 0.0f, 0.183f, 0.236f, 0.236f,
	};
	gpencil_add_points(gps, data5, 48);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Light, 47, "Skin_Light", 3);
	float data6[47 * 5] = { 0.022f, 0.0f, -0.353f, 0.125f, 0.125f, 0.012f, 0.0f, -0.352f, 0.175f, 0.288f, 0.004f, 0.0f, -0.352f, 0.206f, 0.313f, -0.006f, 0.0f, -0.352f, 0.241f, 0.323f,
		-0.017f, 0.0f, -0.352f, 0.27f, 0.33f, -0.029f, 0.0f, -0.351f, 0.295f, 0.334f, -0.041f, 0.0f, -0.349f, 0.314f, 0.337f, -0.052f, 0.0f, -0.344f, 0.327f, 0.341f,
		-0.063f, 0.0f, -0.337f, 0.336f, 0.344f, -0.072f, 0.0f, -0.329f, 0.341f, 0.345f, -0.081f, 0.0f, -0.32f, 0.345f, 0.345f, -0.088f, 0.0f, -0.311f, 0.348f, 0.345f,
		-0.093f, 0.0f, -0.303f, 0.352f, 0.347f, -0.098f, 0.0f, -0.295f, 0.356f, 0.352f, -0.101f, 0.0f, -0.287f, 0.361f, 0.357f, -0.102f, 0.0f, -0.279f, 0.367f, 0.364f,
		-0.103f, 0.0f, -0.271f, 0.373f, 0.378f, -0.102f, 0.0f, -0.263f, 0.379f, 0.382f, -0.1f, 0.0f, -0.255f, 0.383f, 0.389f, -0.098f, 0.0f, -0.247f, 0.387f, 0.391f,
		-0.094f, 0.0f, -0.24f, 0.389f, 0.393f, -0.09f, 0.0f, -0.233f, 0.391f, 0.393f, -0.086f, 0.0f, -0.227f, 0.392f, 0.393f, -0.082f, 0.0f, -0.222f, 0.393f, 0.393f,
		-0.078f, 0.0f, -0.219f, 0.394f, 0.393f, -0.075f, 0.0f, -0.217f, 0.397f, 0.393f, -0.072f, 0.0f, -0.217f, 0.4f, 0.393f, -0.07f, 0.0f, -0.219f, 0.402f, 0.408f,
		-0.069f, 0.0f, -0.222f, 0.404f, 0.408f, -0.069f, 0.0f, -0.228f, 0.406f, 0.409f, -0.069f, 0.0f, -0.234f, 0.407f, 0.409f, -0.07f, 0.0f, -0.241f, 0.408f, 0.409f,
		-0.07f, 0.0f, -0.248f, 0.408f, 0.409f, -0.07f, 0.0f, -0.256f, 0.409f, 0.409f, -0.07f, 0.0f, -0.263f, 0.409f, 0.41f, -0.069f, 0.0f, -0.271f, 0.41f, 0.411f,
		-0.068f, 0.0f, -0.279f, 0.41f, 0.411f, -0.065f, 0.0f, -0.287f, 0.41f, 0.411f, -0.062f, 0.0f, -0.295f, 0.409f, 0.411f, -0.057f, 0.0f, -0.303f, 0.409f, 0.409f,
		-0.052f, 0.0f, -0.31f, 0.408f, 0.409f, -0.047f, 0.0f, -0.318f, 0.407f, 0.408f, -0.041f, 0.0f, -0.324f, 0.406f, 0.407f, -0.035f, 0.0f, -0.329f, 0.403f, 0.407f,
		-0.027f, 0.0f, -0.333f, 0.4f, 0.408f, -0.021f, 0.0f, -0.336f, 0.398f, 0.403f, -0.012f, 0.0f, -0.339f, 0.393f, 0.393f, };
	gpencil_add_points(gps, data6, 47);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Light, 162, "Skin_Light", 3);
	float data7[162 * 5] = { -0.291f, 0.0f, -0.34f, 0.093f, 0.093f, -0.289f, -0.0f, -0.35f, 0.149f, 0.176f, -0.287f, -0.0f, -0.357f, 0.182f, 0.242f, -0.284f, -0.0f, -0.365f, 0.215f, 0.257f,
		-0.281f, -0.0f, -0.374f, 0.242f, 0.266f, -0.278f, -0.0f, -0.384f, 0.266f, 0.287f, -0.275f, -0.0f, -0.394f, 0.285f, 0.304f, -0.271f, 0.0f, -0.405f, 0.302f, 0.316f,
		-0.267f, 0.0f, -0.417f, 0.317f, 0.326f, -0.263f, 0.0f, -0.429f, 0.33f, 0.337f, -0.259f, 0.0f, -0.442f, 0.342f, 0.346f, -0.256f, 0.0f, -0.454f, 0.354f, 0.351f,
		-0.253f, 0.0f, -0.467f, 0.365f, 0.362f, -0.251f, 0.0f, -0.48f, 0.376f, 0.38f, -0.249f, -0.0f, -0.493f, 0.386f, 0.391f, -0.247f, -0.0f, -0.505f, 0.394f, 0.396f,
		-0.246f, -0.0f, -0.518f, 0.401f, 0.405f, -0.245f, 0.0f, -0.53f, 0.408f, 0.409f, -0.245f, 0.0f, -0.542f, 0.415f, 0.413f, -0.245f, 0.0f, -0.554f, 0.421f, 0.42f,
		-0.245f, 0.0f, -0.565f, 0.426f, 0.43f, -0.246f, 0.0f, -0.575f, 0.43f, 0.433f, -0.246f, -0.0f, -0.585f, 0.432f, 0.435f, -0.247f, -0.0f, -0.594f, 0.434f, 0.436f,
		-0.247f, -0.0f, -0.603f, 0.435f, 0.436f, -0.248f, -0.0f, -0.612f, 0.436f, 0.436f, -0.25f, -0.0f, -0.621f, 0.437f, 0.438f, -0.252f, -0.0f, -0.631f, 0.437f, 0.438f,
		-0.254f, -0.0f, -0.642f, 0.438f, 0.438f, -0.255f, 0.0f, -0.653f, 0.438f, 0.438f, -0.258f, 0.0f, -0.664f, 0.438f, 0.439f, -0.26f, 0.0f, -0.674f, 0.439f, 0.439f,
		-0.261f, 0.0f, -0.685f, 0.439f, 0.439f, -0.262f, 0.0f, -0.696f, 0.439f, 0.439f, -0.264f, 0.0f, -0.706f, 0.439f, 0.439f, -0.265f, 0.0f, -0.717f, 0.439f, 0.439f,
		-0.265f, 0.0f, -0.727f, 0.438f, 0.439f, -0.266f, 0.0f, -0.738f, 0.437f, 0.439f, -0.266f, 0.0f, -0.749f, 0.435f, 0.438f, -0.266f, 0.0f, -0.76f, 0.433f, 0.433f,
		-0.265f, 0.0f, -0.771f, 0.431f, 0.428f, -0.265f, 0.0f, -0.781f, 0.43f, 0.428f, -0.263f, 0.0f, -0.792f, 0.429f, 0.428f, -0.26f, 0.0f, -0.802f, 0.428f, 0.429f,
		-0.257f, 0.0f, -0.812f, 0.426f, 0.427f, -0.254f, 0.0f, -0.821f, 0.423f, 0.426f, -0.25f, 0.0f, -0.829f, 0.421f, 0.42f, -0.247f, 0.0f, -0.837f, 0.418f, 0.416f,
		-0.242f, 0.0f, -0.844f, 0.417f, 0.415f, -0.238f, 0.0f, -0.85f, 0.415f, 0.413f, -0.234f, 0.0f, -0.857f, 0.415f, 0.413f, -0.229f, 0.0f, -0.864f, 0.414f, 0.413f,
		-0.224f, 0.0f, -0.87f, 0.414f, 0.413f, -0.219f, 0.0f, -0.877f, 0.414f, 0.414f, -0.214f, 0.0f, -0.883f, 0.414f, 0.413f, -0.208f, 0.0f, -0.89f, 0.413f, 0.413f,
		-0.203f, 0.0f, -0.897f, 0.413f, 0.413f, -0.197f, 0.0f, -0.903f, 0.413f, 0.413f, -0.191f, 0.0f, -0.909f, 0.413f, 0.413f, -0.186f, 0.0f, -0.914f, 0.413f, 0.413f,
		-0.181f, 0.0f, -0.92f, 0.413f, 0.413f, -0.175f, -0.0f, -0.925f, 0.413f, 0.413f, -0.17f, -0.0f, -0.931f, 0.413f, 0.413f, -0.164f, -0.0f, -0.936f, 0.413f, 0.413f,
		-0.159f, -0.0f, -0.942f, 0.413f, 0.413f, -0.152f, -0.0f, -0.948f, 0.413f, 0.413f, -0.145f, -0.0f, -0.955f, 0.413f, 0.413f, -0.137f, -0.0f, -0.961f, 0.414f, 0.413f,
		-0.13f, -0.0f, -0.967f, 0.414f, 0.413f, -0.122f, -0.0f, -0.974f, 0.414f, 0.414f, -0.114f, -0.0f, -0.979f, 0.414f, 0.413f, -0.106f, -0.0f, -0.985f, 0.414f, 0.413f,
		-0.098f, -0.0f, -0.989f, 0.414f, 0.414f, -0.091f, -0.0f, -0.993f, 0.414f, 0.413f, -0.083f, -0.0f, -0.997f, 0.414f, 0.414f, -0.075f, -0.0f, -0.999f, 0.414f, 0.414f,
		-0.066f, -0.0f, -1.001f, 0.414f, 0.414f, -0.057f, -0.0f, -1.003f, 0.414f, 0.413f, -0.046f, -0.0f, -1.006f, 0.414f, 0.413f, -0.038f, -0.0f, -1.008f, 0.414f, 0.413f,
		-0.031f, -0.0f, -1.009f, 0.421f, 0.413f, -0.036f, -0.0f, -1.008f, 0.423f, 0.424f, -0.045f, -0.0f, -1.006f, 0.425f, 0.425f, -0.054f, -0.0f, -1.005f, 0.425f, 0.425f,
		-0.064f, -0.0f, -1.005f, 0.425f, 0.425f, -0.073f, -0.0f, -1.004f, 0.425f, 0.425f, -0.084f, -0.0f, -1.003f, 0.425f, 0.425f, -0.095f, -0.0f, -1.001f, 0.424f, 0.424f,
		-0.105f, -0.0f, -0.997f, 0.423f, 0.424f, -0.116f, -0.0f, -0.994f, 0.422f, 0.422f, -0.127f, -0.0f, -0.991f, 0.421f, 0.419f, -0.137f, -0.0f, -0.987f, 0.42f, 0.419f,
		-0.148f, -0.0f, -0.983f, 0.42f, 0.419f, -0.158f, -0.0f, -0.98f, 0.42f, 0.419f, -0.167f, -0.0f, -0.976f, 0.419f, 0.419f, -0.176f, -0.0f, -0.973f, 0.419f, 0.419f,
		-0.184f, -0.0f, -0.969f, 0.419f, 0.419f, -0.192f, -0.0f, -0.966f, 0.419f, 0.418f, -0.2f, 0.0f, -0.962f, 0.419f, 0.418f, -0.207f, 0.0f, -0.957f, 0.419f, 0.419f,
		-0.215f, 0.0f, -0.953f, 0.419f, 0.418f, -0.223f, 0.0f, -0.948f, 0.419f, 0.419f, -0.231f, 0.0f, -0.944f, 0.419f, 0.419f, -0.239f, 0.0f, -0.939f, 0.419f, 0.419f,
		-0.247f, 0.0f, -0.934f, 0.419f, 0.419f, -0.255f, 0.0f, -0.929f, 0.419f, 0.419f, -0.262f, 0.0f, -0.924f, 0.419f, 0.419f, -0.269f, 0.0f, -0.919f, 0.419f, 0.418f,
		-0.275f, 0.0f, -0.914f, 0.419f, 0.419f, -0.281f, 0.0f, -0.909f, 0.419f, 0.418f, -0.287f, 0.0f, -0.904f, 0.419f, 0.418f, -0.293f, 0.0f, -0.899f, 0.419f, 0.418f,
		-0.299f, 0.0f, -0.894f, 0.42f, 0.419f, -0.304f, 0.0f, -0.888f, 0.421f, 0.42f, -0.311f, 0.0f, -0.882f, 0.423f, 0.422f, -0.317f, 0.0f, -0.876f, 0.424f, 0.424f,
		-0.322f, 0.0f, -0.869f, 0.426f, 0.426f, -0.328f, 0.0f, -0.861f, 0.427f, 0.427f, -0.332f, 0.0f, -0.853f, 0.429f, 0.429f, -0.336f, 0.0f, -0.843f, 0.43f, 0.429f,
		-0.339f, 0.0f, -0.834f, 0.432f, 0.431f, -0.341f, 0.0f, -0.821f, 0.435f, 0.434f, -0.342f, 0.0f, -0.809f, 0.438f, 0.439f, -0.343f, 0.0f, -0.796f, 0.44f, 0.44f,
		-0.343f, 0.0f, -0.783f, 0.442f, 0.442f, -0.343f, 0.0f, -0.772f, 0.446f, 0.445f, -0.342f, 0.0f, -0.76f, 0.45f, 0.45f, -0.342f, 0.0f, -0.748f, 0.454f, 0.455f,
		-0.34f, 0.0f, -0.735f, 0.457f, 0.457f, -0.339f, 0.0f, -0.723f, 0.46f, 0.46f, -0.338f, 0.0f, -0.711f, 0.463f, 0.464f, -0.336f, 0.0f, -0.7f, 0.465f, 0.465f,
		-0.335f, 0.0f, -0.688f, 0.466f, 0.466f, -0.332f, 0.0f, -0.676f, 0.467f, 0.467f, -0.331f, 0.0f, -0.664f, 0.467f, 0.467f, -0.33f, 0.0f, -0.651f, 0.467f, 0.467f,
		-0.328f, 0.0f, -0.638f, 0.467f, 0.467f, -0.325f, 0.0f, -0.625f, 0.467f, 0.467f, -0.323f, 0.0f, -0.614f, 0.467f, 0.467f, -0.321f, 0.0f, -0.603f, 0.467f, 0.466f,
		-0.318f, 0.0f, -0.592f, 0.467f, 0.466f, -0.315f, 0.0f, -0.581f, 0.467f, 0.466f, -0.313f, 0.0f, -0.569f, 0.467f, 0.467f, -0.311f, -0.0f, -0.557f, 0.467f, 0.467f,
		-0.309f, -0.0f, -0.543f, 0.467f, 0.467f, -0.306f, -0.0f, -0.531f, 0.467f, 0.467f, -0.303f, -0.0f, -0.519f, 0.467f, 0.467f, -0.301f, -0.0f, -0.507f, 0.467f, 0.468f,
		-0.299f, -0.0f, -0.497f, 0.467f, 0.467f, -0.297f, -0.0f, -0.487f, 0.467f, 0.467f, -0.295f, 0.0f, -0.476f, 0.465f, 0.467f, -0.293f, 0.0f, -0.466f, 0.463f, 0.467f,
		-0.292f, 0.0f, -0.456f, 0.46f, 0.466f, -0.291f, 0.0f, -0.445f, 0.455f, 0.459f, -0.29f, 0.0f, -0.435f, 0.449f, 0.457f, -0.29f, 0.0f, -0.424f, 0.44f, 0.448f,
		-0.29f, 0.0f, -0.413f, 0.43f, 0.44f, -0.29f, 0.0f, -0.403f, 0.418f, 0.437f, -0.29f, -0.0f, -0.393f, 0.404f, 0.415f, -0.291f, -0.0f, -0.384f, 0.388f, 0.393f,
		-0.29f, -0.0f, -0.376f, 0.374f, 0.379f, -0.29f, -0.0f, -0.365f, 0.352f, 0.352f, };
	gpencil_add_points(gps, data7, 162);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Light, 55, "Skin_Light", 3);
	float data8[55 * 5] = { 0.781f, 0.0f, 0.098f, 0.109f, 0.109f, 0.784f, 0.0f, 0.105f, 0.202f, 0.338f, 0.785f, 0.0f, 0.108f, 0.254f, 0.369f, 0.787f, 0.0f, 0.113f, 0.306f, 0.382f,
		0.787f, 0.0f, 0.118f, 0.344f, 0.392f, 0.789f, 0.0f, 0.123f, 0.372f, 0.401f, 0.79f, 0.0f, 0.128f, 0.392f, 0.41f, 0.792f, 0.0f, 0.135f, 0.406f, 0.42f,
		0.794f, 0.0f, 0.142f, 0.416f, 0.424f, 0.797f, 0.0f, 0.152f, 0.424f, 0.428f, 0.801f, 0.0f, 0.161f, 0.429f, 0.431f, 0.807f, 0.0f, 0.172f, 0.432f, 0.435f,
		0.814f, 0.0f, 0.182f, 0.435f, 0.438f, 0.821f, 0.0f, 0.19f, 0.437f, 0.439f, 0.828f, 0.0f, 0.197f, 0.439f, 0.44f, 0.836f, 0.0f, 0.204f, 0.44f, 0.441f,
		0.845f, -0.0f, 0.211f, 0.44f, 0.441f, 0.853f, -0.0f, 0.215f, 0.441f, 0.441f, 0.861f, -0.0f, 0.219f, 0.441f, 0.441f, 0.87f, -0.0f, 0.222f, 0.441f, 0.442f,
		0.878f, -0.0f, 0.224f, 0.441f, 0.442f, 0.886f, -0.0f, 0.226f, 0.441f, 0.442f, 0.895f, -0.0f, 0.227f, 0.44f, 0.442f, 0.903f, 0.0f, 0.226f, 0.439f, 0.441f,
		0.911f, 0.0f, 0.225f, 0.436f, 0.441f, 0.919f, 0.0f, 0.224f, 0.432f, 0.441f, 0.927f, 0.0f, 0.221f, 0.425f, 0.436f, 0.934f, 0.0f, 0.218f, 0.415f, 0.429f,
		0.94f, 0.0f, 0.215f, 0.404f, 0.406f, 0.944f, 0.0f, 0.211f, 0.393f, 0.389f, 0.947f, 0.0f, 0.208f, 0.384f, 0.378f, 0.948f, 0.0f, 0.204f, 0.376f, 0.371f,
		0.946f, 0.0f, 0.2f, 0.369f, 0.364f, 0.943f, 0.0f, 0.196f, 0.365f, 0.358f, 0.937f, 0.0f, 0.193f, 0.364f, 0.354f, 0.931f, 0.0f, 0.189f, 0.366f, 0.359f,
		0.925f, 0.0f, 0.186f, 0.37f, 0.367f, 0.917f, 0.0f, 0.182f, 0.374f, 0.375f, 0.908f, 0.0f, 0.177f, 0.378f, 0.382f, 0.899f, 0.0f, 0.172f, 0.381f, 0.384f,
		0.889f, 0.0f, 0.167f, 0.384f, 0.385f, 0.876f, 0.0f, 0.163f, 0.387f, 0.387f, 0.864f, 0.0f, 0.156f, 0.39f, 0.388f, 0.852f, 0.0f, 0.15f, 0.393f, 0.39f,
		0.841f, 0.0f, 0.144f, 0.396f, 0.396f, 0.832f, 0.0f, 0.138f, 0.399f, 0.401f, 0.826f, 0.0f, 0.133f, 0.401f, 0.404f, 0.82f, 0.0f, 0.127f, 0.403f, 0.405f,
		0.816f, 0.0f, 0.122f, 0.403f, 0.407f, 0.812f, 0.0f, 0.119f, 0.399f, 0.406f, 0.808f, 0.0f, 0.115f, 0.39f, 0.405f, 0.805f, 0.0f, 0.113f, 0.371f, 0.407f,
		0.801f, 0.0f, 0.111f, 0.341f, 0.407f, 0.799f, 0.0f, 0.109f, 0.309f, 0.405f, 0.795f, 0.0f, 0.106f, 0.255f, 0.255f, };
	gpencil_add_points(gps, data8, 55);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Light, 70, "Skin_Light", 3);
	float data9[70 * 5] = { 0.819f, -0.0f, 0.325f, 0.109f, 0.109f, 0.829f, -0.0f, 0.328f, 0.258f, 0.403f, 0.835f, -0.0f, 0.329f, 0.327f, 0.428f, 0.843f, -0.0f, 0.331f, 0.383f, 0.452f,
		0.851f, -0.0f, 0.332f, 0.419f, 0.465f, 0.861f, -0.0f, 0.334f, 0.444f, 0.473f, 0.87f, -0.0f, 0.336f, 0.461f, 0.48f, 0.881f, -0.0f, 0.337f, 0.473f, 0.486f,
		0.892f, -0.0f, 0.339f, 0.482f, 0.496f, 0.904f, -0.0f, 0.341f, 0.489f, 0.501f, 0.917f, -0.0f, 0.342f, 0.494f, 0.503f, 0.931f, -0.0f, 0.342f, 0.498f, 0.505f,
		0.945f, -0.0f, 0.342f, 0.501f, 0.505f, 0.958f, -0.0f, 0.342f, 0.503f, 0.506f, 0.971f, -0.0f, 0.341f, 0.505f, 0.506f, 0.984f, -0.0f, 0.341f, 0.506f, 0.506f,
		0.997f, -0.0f, 0.339f, 0.507f, 0.508f, 1.009f, -0.0f, 0.337f, 0.507f, 0.507f, 1.021f, -0.0f, 0.333f, 0.508f, 0.508f, 1.033f, -0.0f, 0.33f, 0.508f, 0.508f,
		1.044f, -0.0f, 0.326f, 0.508f, 0.508f, 1.056f, -0.0f, 0.322f, 0.508f, 0.508f, 1.068f, -0.0f, 0.317f, 0.508f, 0.508f, 1.078f, -0.0f, 0.311f, 0.507f, 0.508f,
		1.089f, -0.0f, 0.304f, 0.506f, 0.508f, 1.099f, 0.0f, 0.294f, 0.503f, 0.506f, 1.107f, 0.0f, 0.287f, 0.498f, 0.506f, 1.113f, 0.0f, 0.28f, 0.49f, 0.505f,
		1.117f, 0.0f, 0.276f, 0.48f, 0.501f, 1.121f, 0.0f, 0.272f, 0.468f, 0.492f, 1.124f, 0.0f, 0.27f, 0.455f, 0.467f, 1.127f, 0.0f, 0.27f, 0.443f, 0.431f,
		1.129f, 0.0f, 0.271f, 0.431f, 0.4f, 1.13f, 0.0f, 0.274f, 0.422f, 0.399f, 1.13f, 0.0f, 0.278f, 0.414f, 0.399f, 1.13f, 0.0f, 0.286f, 0.408f, 0.399f,
		1.128f, 0.0f, 0.295f, 0.404f, 0.399f, 1.124f, 0.0f, 0.305f, 0.402f, 0.399f, 1.119f, 0.0f, 0.316f, 0.403f, 0.4f, 1.113f, -0.0f, 0.327f, 0.405f, 0.401f,
		1.107f, -0.0f, 0.337f, 0.408f, 0.411f, 1.1f, -0.0f, 0.345f, 0.412f, 0.412f, 1.094f, -0.0f, 0.352f, 0.416f, 0.413f, 1.087f, -0.0f, 0.357f, 0.421f, 0.422f,
		1.08f, -0.0f, 0.363f, 0.426f, 0.428f, 1.071f, -0.0f, 0.368f, 0.429f, 0.43f, 1.062f, -0.0f, 0.373f, 0.431f, 0.431f, 1.051f, -0.0f, 0.377f, 0.433f, 0.431f,
		1.039f, -0.0f, 0.381f, 0.436f, 0.437f, 1.026f, -0.0f, 0.383f, 0.438f, 0.44f, 1.013f, -0.0f, 0.384f, 0.44f, 0.44f, 1.0f, -0.0f, 0.385f, 0.441f, 0.443f,
		0.987f, -0.0f, 0.385f, 0.442f, 0.443f, 0.975f, -0.0f, 0.384f, 0.443f, 0.443f, 0.962f, -0.0f, 0.383f, 0.443f, 0.444f, 0.949f, -0.0f, 0.381f, 0.443f, 0.443f,
		0.936f, -0.0f, 0.38f, 0.443f, 0.444f, 0.923f, -0.0f, 0.378f, 0.443f, 0.444f, 0.909f, -0.0f, 0.375f, 0.443f, 0.444f, 0.897f, -0.0f, 0.371f, 0.443f, 0.444f,
		0.886f, -0.0f, 0.367f, 0.443f, 0.443f, 0.876f, -0.0f, 0.363f, 0.443f, 0.444f, 0.868f, -0.0f, 0.359f, 0.443f, 0.442f, 0.86f, -0.0f, 0.355f, 0.442f, 0.443f,
		0.852f, -0.0f, 0.35f, 0.441f, 0.443f, 0.844f, -0.0f, 0.347f, 0.433f, 0.443f, 0.837f, -0.0f, 0.343f, 0.409f, 0.443f, 0.83f, -0.0f, 0.338f, 0.344f, 0.443f,
		0.824f, -0.0f, 0.335f, 0.239f, 0.437f, 0.815f, -0.0f, 0.326f, 0.0f, 0.003f, };
	gpencil_add_points(gps, data9, 70);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Light, 227, "Skin_Light", 3);
	float data10[227 * 5] = { -0.675f, 0.0f, 0.411f, 0.099f, 0.099f, -0.669f, 0.0f, 0.418f, 0.358f, 0.358f, -0.666f, 0.0f, 0.424f, 0.381f, 0.381f, -0.662f, 0.0f, 0.431f, 0.389f, 0.389f,
		-0.658f, 0.0f, 0.438f, 0.393f, 0.393f, -0.649f, 0.0f, 0.448f, 0.404f, 0.404f, -0.641f, 0.0f, 0.458f, 0.419f, 0.419f, -0.632f, 0.0f, 0.468f, 0.431f, 0.434f,
		-0.626f, 0.0f, 0.476f, 0.435f, 0.436f, -0.62f, 0.0f, 0.484f, 0.437f, 0.438f, -0.615f, 0.0f, 0.492f, 0.439f, 0.439f, -0.61f, 0.0f, 0.499f, 0.439f, 0.44f,
		-0.605f, 0.0f, 0.506f, 0.44f, 0.44f, -0.6f, 0.0f, 0.512f, 0.44f, 0.44f, -0.595f, 0.0f, 0.519f, 0.44f, 0.44f, -0.59f, 0.0f, 0.526f, 0.441f, 0.441f,
		-0.584f, 0.0f, 0.532f, 0.441f, 0.441f, -0.579f, 0.0f, 0.539f, 0.441f, 0.441f, -0.573f, 0.0f, 0.545f, 0.442f, 0.442f, -0.566f, 0.0f, 0.551f, 0.443f, 0.443f,
		-0.559f, 0.0f, 0.557f, 0.443f, 0.443f, -0.552f, 0.0f, 0.563f, 0.444f, 0.444f, -0.545f, 0.0f, 0.569f, 0.445f, 0.445f, -0.538f, 0.0f, 0.576f, 0.447f, 0.447f,
		-0.532f, 0.0f, 0.582f, 0.448f, 0.448f, -0.525f, 0.0f, 0.589f, 0.45f, 0.45f, -0.519f, 0.0f, 0.595f, 0.451f, 0.452f, -0.513f, 0.0f, 0.602f, 0.452f, 0.453f,
		-0.506f, 0.0f, 0.608f, 0.453f, 0.453f, -0.5f, 0.0f, 0.613f, 0.453f, 0.454f, -0.493f, 0.0f, 0.619f, 0.453f, 0.454f, -0.486f, 0.0f, 0.625f, 0.453f, 0.454f,
		-0.479f, 0.0f, 0.631f, 0.453f, 0.454f, -0.472f, 0.0f, 0.637f, 0.453f, 0.454f, -0.464f, 0.0f, 0.642f, 0.453f, 0.454f, -0.457f, 0.0f, 0.649f, 0.453f, 0.454f,
		-0.45f, 0.0f, 0.655f, 0.453f, 0.453f, -0.443f, 0.0f, 0.661f, 0.453f, 0.453f, -0.435f, 0.0f, 0.667f, 0.453f, 0.454f, -0.427f, 0.0f, 0.672f, 0.453f, 0.454f,
		-0.419f, 0.0f, 0.677f, 0.453f, 0.454f, -0.411f, 0.0f, 0.682f, 0.453f, 0.453f, -0.403f, 0.0f, 0.688f, 0.453f, 0.453f, -0.395f, 0.0f, 0.692f, 0.453f, 0.454f,
		-0.387f, 0.0f, 0.697f, 0.453f, 0.454f, -0.379f, 0.0f, 0.702f, 0.453f, 0.454f, -0.372f, 0.0f, 0.707f, 0.454f, 0.454f, -0.364f, 0.0f, 0.712f, 0.454f, 0.454f,
		-0.356f, 0.0f, 0.716f, 0.454f, 0.454f, -0.349f, 0.0f, 0.721f, 0.454f, 0.454f, -0.342f, 0.0f, 0.725f, 0.454f, 0.454f, -0.334f, 0.0f, 0.73f, 0.454f, 0.454f,
		-0.326f, 0.0f, 0.733f, 0.454f, 0.454f, -0.318f, 0.0f, 0.737f, 0.454f, 0.454f, -0.31f, 0.0f, 0.74f, 0.454f, 0.454f, -0.301f, 0.0f, 0.743f, 0.454f, 0.454f,
		-0.293f, 0.0f, 0.746f, 0.454f, 0.455f, -0.284f, 0.0f, 0.749f, 0.454f, 0.455f, -0.274f, 0.0f, 0.752f, 0.455f, 0.455f, -0.265f, 0.0f, 0.755f, 0.455f, 0.455f,
		-0.255f, 0.0f, 0.757f, 0.455f, 0.455f, -0.245f, 0.0f, 0.76f, 0.456f, 0.455f, -0.234f, 0.0f, 0.762f, 0.457f, 0.456f, -0.223f, 0.0f, 0.764f, 0.458f, 0.458f,
		-0.212f, 0.0f, 0.766f, 0.459f, 0.46f, -0.201f, 0.0f, 0.769f, 0.461f, 0.46f, -0.189f, 0.0f, 0.771f, 0.462f, 0.461f, -0.177f, 0.0f, 0.773f, 0.464f, 0.463f,
		-0.166f, 0.0f, 0.775f, 0.465f, 0.465f, -0.153f, 0.0f, 0.777f, 0.467f, 0.467f, -0.141f, 0.0f, 0.779f, 0.469f, 0.469f, -0.128f, 0.0f, 0.781f, 0.472f, 0.472f,
		-0.116f, 0.0f, 0.782f, 0.474f, 0.473f, -0.101f, 0.0f, 0.782f, 0.477f, 0.477f, -0.087f, 0.0f, 0.783f, 0.482f, 0.477f, -0.073f, 0.0f, 0.783f, 0.489f, 0.483f,
		-0.059f, 0.0f, 0.783f, 0.497f, 0.5f, -0.046f, 0.0f, 0.784f, 0.503f, 0.509f, -0.033f, 0.0f, 0.784f, 0.508f, 0.51f, -0.022f, 0.0f, 0.784f, 0.51f, 0.512f,
		-0.011f, 0.0f, 0.785f, 0.512f, 0.512f, -0.0f, 0.0f, 0.786f, 0.513f, 0.512f, 0.011f, 0.0f, 0.786f, 0.515f, 0.513f, 0.022f, 0.0f, 0.786f, 0.517f, 0.517f,
		0.032f, 0.0f, 0.786f, 0.52f, 0.519f, 0.044f, 0.0f, 0.786f, 0.522f, 0.524f, 0.055f, 0.0f, 0.785f, 0.525f, 0.525f, 0.066f, 0.0f, 0.785f, 0.527f, 0.525f,
		0.076f, 0.0f, 0.784f, 0.53f, 0.53f, 0.086f, 0.0f, 0.783f, 0.532f, 0.533f, 0.097f, 0.0f, 0.782f, 0.535f, 0.534f, 0.108f, 0.0f, 0.782f, 0.538f, 0.541f,
		0.119f, 0.0f, 0.781f, 0.54f, 0.542f, 0.13f, 0.0f, 0.781f, 0.543f, 0.543f, 0.141f, 0.0f, 0.78f, 0.545f, 0.545f, 0.154f, 0.0f, 0.779f, 0.547f, 0.547f,
		0.165f, 0.0f, 0.777f, 0.549f, 0.548f, 0.177f, 0.0f, 0.775f, 0.55f, 0.552f, 0.188f, 0.0f, 0.772f, 0.552f, 0.552f, 0.199f, 0.0f, 0.77f, 0.553f, 0.553f,
		0.209f, 0.0f, 0.767f, 0.554f, 0.554f, 0.218f, 0.0f, 0.765f, 0.555f, 0.556f, 0.226f, 0.0f, 0.763f, 0.556f, 0.557f, 0.235f, 0.0f, 0.761f, 0.557f, 0.557f,
		0.244f, 0.0f, 0.758f, 0.558f, 0.558f, 0.253f, 0.0f, 0.755f, 0.559f, 0.559f, 0.263f, 0.0f, 0.752f, 0.56f, 0.559f, 0.272f, 0.0f, 0.749f, 0.561f, 0.56f,
		0.285f, 0.0f, 0.745f, 0.562f, 0.56f, 0.299f, 0.0f, 0.741f, 0.563f, 0.563f, 0.316f, 0.0f, 0.736f, 0.564f, 0.564f, 0.331f, 0.0f, 0.728f, 0.565f, 0.567f,
		0.349f, 0.0f, 0.718f, 0.565f, 0.568f, 0.365f, 0.0f, 0.708f, 0.566f, 0.568f, 0.38f, 0.0f, 0.699f, 0.566f, 0.568f, 0.39f, 0.0f, 0.693f, 0.566f, 0.568f,
		0.397f, 0.0f, 0.687f, 0.566f, 0.569f, 0.4f, 0.0f, 0.683f, 0.566f, 0.569f, 0.401f, 0.0f, 0.681f, 0.565f, 0.57f, 0.4f, 0.0f, 0.679f, 0.565f, 0.57f,
		0.397f, 0.0f, 0.678f, 0.564f, 0.57f, 0.393f, 0.0f, 0.678f, 0.564f, 0.565f, 0.387f, 0.0f, 0.678f, 0.563f, 0.559f, 0.379f, 0.0f, 0.679f, 0.562f, 0.558f,
		0.37f, 0.0f, 0.681f, 0.561f, 0.557f, 0.357f, 0.0f, 0.684f, 0.561f, 0.557f, 0.342f, 0.0f, 0.689f, 0.56f, 0.557f, 0.324f, 0.0f, 0.694f, 0.56f, 0.557f,
		0.307f, 0.0f, 0.697f, 0.559f, 0.558f, 0.291f, 0.0f, 0.699f, 0.559f, 0.558f, 0.274f, 0.0f, 0.701f, 0.559f, 0.557f, 0.26f, 0.0f, 0.703f, 0.558f, 0.558f,
		0.246f, 0.0f, 0.705f, 0.558f, 0.558f, 0.235f, 0.0f, 0.707f, 0.558f, 0.558f, 0.224f, 0.0f, 0.709f, 0.558f, 0.558f, 0.214f, 0.0f, 0.711f, 0.558f, 0.558f,
		0.203f, 0.0f, 0.713f, 0.558f, 0.559f, 0.192f, 0.0f, 0.714f, 0.558f, 0.558f, 0.181f, 0.0f, 0.714f, 0.557f, 0.557f, 0.17f, 0.0f, 0.714f, 0.557f, 0.557f,
		0.16f, 0.0f, 0.715f, 0.557f, 0.556f, 0.149f, 0.0f, 0.715f, 0.557f, 0.556f, 0.139f, 0.0f, 0.716f, 0.557f, 0.556f, 0.129f, 0.0f, 0.716f, 0.558f, 0.556f,
		0.119f, 0.0f, 0.717f, 0.558f, 0.556f, 0.109f, 0.0f, 0.717f, 0.558f, 0.557f, 0.099f, 0.0f, 0.718f, 0.558f, 0.557f, 0.089f, 0.0f, 0.718f, 0.559f, 0.557f,
		0.079f, 0.0f, 0.718f, 0.559f, 0.558f, 0.068f, 0.0f, 0.719f, 0.559f, 0.559f, 0.057f, 0.0f, 0.719f, 0.56f, 0.56f, 0.046f, 0.0f, 0.718f, 0.56f, 0.561f,
		0.035f, 0.0f, 0.718f, 0.561f, 0.561f, 0.024f, 0.0f, 0.718f, 0.561f, 0.562f, 0.013f, 0.0f, 0.717f, 0.562f, 0.562f, 0.002f, 0.0f, 0.717f, 0.562f, 0.563f,
		-0.01f, 0.0f, 0.717f, 0.563f, 0.564f, -0.021f, 0.0f, 0.717f, 0.563f, 0.564f, -0.032f, 0.0f, 0.716f, 0.563f, 0.564f, -0.044f, 0.0f, 0.715f, 0.564f, 0.564f,
		-0.055f, 0.0f, 0.714f, 0.564f, 0.565f, -0.066f, 0.0f, 0.713f, 0.564f, 0.565f, -0.078f, 0.0f, 0.712f, 0.564f, 0.564f, -0.089f, 0.0f, 0.711f, 0.564f, 0.564f,
		-0.101f, 0.0f, 0.709f, 0.565f, 0.564f, -0.112f, 0.0f, 0.708f, 0.565f, 0.564f, -0.124f, 0.0f, 0.707f, 0.565f, 0.564f, -0.135f, 0.0f, 0.705f, 0.565f, 0.564f,
		-0.146f, 0.0f, 0.704f, 0.566f, 0.564f, -0.158f, 0.0f, 0.702f, 0.566f, 0.564f, -0.169f, 0.0f, 0.7f, 0.566f, 0.566f, -0.18f, 0.0f, 0.698f, 0.567f, 0.568f,
		-0.191f, 0.0f, 0.696f, 0.567f, 0.568f, -0.203f, 0.0f, 0.693f, 0.567f, 0.568f, -0.215f, 0.0f, 0.69f, 0.567f, 0.568f, -0.227f, 0.0f, 0.687f, 0.567f, 0.568f,
		-0.238f, 0.0f, 0.684f, 0.567f, 0.568f, -0.25f, 0.0f, 0.681f, 0.567f, 0.569f, -0.262f, 0.0f, 0.678f, 0.567f, 0.569f, -0.273f, 0.0f, 0.675f, 0.567f, 0.567f,
		-0.284f, 0.0f, 0.673f, 0.567f, 0.566f, -0.295f, 0.0f, 0.671f, 0.567f, 0.567f, -0.305f, 0.0f, 0.669f, 0.566f, 0.567f, -0.316f, 0.0f, 0.666f, 0.566f, 0.567f,
		-0.326f, 0.0f, 0.663f, 0.565f, 0.566f, -0.337f, 0.0f, 0.66f, 0.565f, 0.566f, -0.348f, 0.0f, 0.655f, 0.564f, 0.564f, -0.359f, 0.0f, 0.652f, 0.563f, 0.564f,
		-0.369f, 0.0f, 0.648f, 0.562f, 0.563f, -0.379f, 0.0f, 0.644f, 0.561f, 0.56f, -0.389f, 0.0f, 0.64f, 0.561f, 0.559f, -0.399f, 0.0f, 0.636f, 0.56f, 0.559f,
		-0.409f, 0.0f, 0.633f, 0.559f, 0.559f, -0.419f, 0.0f, 0.629f, 0.559f, 0.559f, -0.428f, 0.0f, 0.625f, 0.559f, 0.558f, -0.438f, 0.0f, 0.62f, 0.559f, 0.559f,
		-0.447f, 0.0f, 0.615f, 0.559f, 0.559f, -0.457f, 0.0f, 0.61f, 0.559f, 0.559f, -0.466f, 0.0f, 0.605f, 0.559f, 0.559f, -0.474f, 0.0f, 0.6f, 0.559f, 0.559f,
		-0.483f, 0.0f, 0.595f, 0.559f, 0.559f, -0.492f, 0.0f, 0.591f, 0.559f, 0.559f, -0.5f, 0.0f, 0.586f, 0.559f, 0.559f, -0.508f, 0.0f, 0.58f, 0.559f, 0.559f,
		-0.515f, 0.0f, 0.574f, 0.559f, 0.559f, -0.523f, 0.0f, 0.568f, 0.559f, 0.559f, -0.531f, 0.0f, 0.562f, 0.559f, 0.558f, -0.54f, 0.0f, 0.556f, 0.559f, 0.558f,
		-0.548f, 0.0f, 0.549f, 0.559f, 0.559f, -0.556f, 0.0f, 0.543f, 0.559f, 0.559f, -0.562f, 0.0f, 0.537f, 0.559f, 0.559f, -0.568f, 0.0f, 0.531f, 0.559f, 0.559f,
		-0.574f, 0.0f, 0.524f, 0.559f, 0.559f, -0.58f, 0.0f, 0.518f, 0.558f, 0.559f, -0.586f, 0.0f, 0.512f, 0.557f, 0.558f, -0.591f, 0.0f, 0.506f, 0.555f, 0.557f,
		-0.597f, 0.0f, 0.5f, 0.551f, 0.556f, -0.603f, 0.0f, 0.493f, 0.546f, 0.547f, -0.609f, 0.0f, 0.487f, 0.541f, 0.538f, -0.614f, 0.0f, 0.48f, 0.536f, 0.535f,
		-0.621f, 0.0f, 0.473f, 0.534f, 0.534f, -0.628f, 0.0f, 0.467f, 0.534f, 0.534f, -0.637f, 0.0f, 0.459f, 0.534f, 0.534f, -0.642f, 0.0f, 0.452f, 0.532f, 0.532f,
		-0.65f, 0.0f, 0.445f, 0.528f, 0.528f, -0.654f, 0.0f, 0.438f, 0.525f, 0.525f, -0.659f, 0.0f, 0.431f, 0.522f, 0.522f, };
	gpencil_add_points(gps, data10, 227);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Shadow, 1, "Skin_Shadow", 3);
	float data11[1 * 5] = { -0.525f, 0.0f, 0.174f, 0.124f, 0.124f, };
	gpencil_add_points(gps, data11, 1);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Shadow, 123, "Skin_Shadow", 3);
	float data12[123 * 5] = { -0.53f, 0.0f, 0.193f, 0.147f, 0.147f, -0.532f, 0.0f, 0.186f, 0.316f, 0.316f, -0.534f, 0.0f, 0.18f, 0.353f, 0.353f, -0.535f, 0.0f, 0.173f, 0.382f, 0.382f,
		-0.537f, 0.0f, 0.165f, 0.384f, 0.384f, -0.538f, 0.0f, 0.155f, 0.387f, 0.387f, -0.539f, 0.0f, 0.145f, 0.393f, 0.393f, -0.54f, -0.0f, 0.134f, 0.399f, 0.399f,
		-0.541f, -0.0f, 0.123f, 0.4f, 0.4f, -0.542f, -0.0f, 0.11f, 0.401f, 0.401f, -0.542f, 0.0f, 0.094f, 0.402f, 0.402f, -0.54f, 0.0f, 0.078f, 0.403f, 0.403f,
		-0.538f, 0.0f, 0.061f, 0.404f, 0.404f, -0.535f, 0.0f, 0.045f, 0.404f, 0.404f, -0.531f, 0.0f, 0.031f, 0.404f, 0.404f, -0.526f, 0.0f, 0.018f, 0.404f, 0.404f,
		-0.52f, -0.0f, 0.005f, 0.405f, 0.405f, -0.513f, -0.0f, -0.01f, 0.405f, 0.405f, -0.505f, -0.0f, -0.024f, 0.405f, 0.405f, -0.495f, -0.0f, -0.037f, 0.405f, 0.405f,
		-0.485f, 0.0f, -0.051f, 0.405f, 0.405f, -0.474f, 0.0f, -0.064f, 0.406f, 0.406f, -0.462f, 0.0f, -0.076f, 0.405f, 0.405f, -0.451f, 0.0f, -0.086f, 0.406f, 0.406f,
		-0.442f, 0.0f, -0.094f, 0.406f, 0.406f, -0.432f, 0.0f, -0.102f, 0.406f, 0.406f, -0.422f, 0.0f, -0.108f, 0.405f, 0.405f, -0.411f, 0.0f, -0.114f, 0.406f, 0.406f,
		-0.4f, 0.0f, -0.119f, 0.405f, 0.405f, -0.389f, 0.0f, -0.122f, 0.406f, 0.406f, -0.378f, 0.0f, -0.125f, 0.407f, 0.407f, -0.365f, 0.0f, -0.127f, 0.412f, 0.412f,
		-0.354f, 0.0f, -0.129f, 0.418f, 0.418f, -0.342f, 0.0f, -0.131f, 0.44f, 0.44f, -0.33f, 0.0f, -0.131f, 0.448f, 0.448f, -0.317f, 0.0f, -0.131f, 0.469f, 0.469f,
		-0.305f, 0.0f, -0.13f, 0.477f, 0.477f, -0.293f, 0.0f, -0.128f, 0.482f, 0.482f, -0.278f, 0.0f, -0.125f, 0.494f, 0.494f, -0.266f, 0.0f, -0.121f, 0.5f, 0.5f,
		-0.253f, 0.0f, -0.116f, 0.507f, 0.507f, -0.242f, 0.0f, -0.111f, 0.509f, 0.509f, -0.231f, 0.0f, -0.105f, 0.511f, 0.511f, -0.222f, 0.0f, -0.099f, 0.511f, 0.511f,
		-0.213f, 0.0f, -0.092f, 0.512f, 0.512f, -0.206f, 0.0f, -0.084f, 0.513f, 0.513f, -0.199f, 0.0f, -0.076f, 0.514f, 0.514f, -0.192f, 0.0f, -0.067f, 0.515f, 0.515f,
		-0.186f, -0.0f, -0.058f, 0.516f, 0.516f, -0.18f, -0.0f, -0.049f, 0.516f, 0.516f, -0.175f, -0.0f, -0.04f, 0.515f, 0.515f, -0.17f, -0.0f, -0.03f, 0.515f, 0.515f,
		-0.166f, -0.0f, -0.02f, 0.516f, 0.516f, -0.163f, -0.0f, -0.01f, 0.504f, 0.504f, -0.159f, -0.0f, 0.002f, 0.502f, 0.502f, -0.155f, -0.0f, 0.014f, 0.501f, 0.501f,
		-0.152f, -0.0f, 0.027f, 0.502f, 0.502f, -0.149f, -0.0f, 0.043f, 0.5f, 0.5f, -0.148f, -0.0f, 0.058f, 0.49f, 0.49f, -0.147f, -0.0f, 0.075f, 0.47f, 0.47f,
		-0.146f, -0.0f, 0.09f, 0.463f, 0.463f, -0.146f, -0.0f, 0.105f, 0.454f, 0.454f, -0.146f, -0.0f, 0.12f, 0.427f, 0.427f, -0.148f, 0.0f, 0.133f, 0.413f, 0.413f,
		-0.15f, 0.0f, 0.144f, 0.4f, 0.4f, -0.153f, 0.0f, 0.152f, 0.383f, 0.383f, -0.156f, 0.0f, 0.157f, 0.369f, 0.369f, -0.158f, 0.0f, 0.16f, 0.36f, 0.36f,
		-0.16f, 0.0f, 0.158f, 0.349f, 0.349f, -0.162f, 0.0f, 0.154f, 0.364f, 0.364f, -0.164f, 0.0f, 0.147f, 0.37f, 0.37f, -0.166f, 0.0f, 0.139f, 0.378f, 0.378f,
		-0.168f, 0.0f, 0.13f, 0.386f, 0.386f, -0.172f, 0.0f, 0.119f, 0.394f, 0.394f, -0.176f, -0.0f, 0.108f, 0.405f, 0.405f, -0.18f, -0.0f, 0.096f, 0.412f, 0.412f,
		-0.185f, -0.0f, 0.084f, 0.417f, 0.417f, -0.191f, -0.0f, 0.073f, 0.425f, 0.425f, -0.196f, -0.0f, 0.063f, 0.431f, 0.431f, -0.202f, -0.0f, 0.053f, 0.441f, 0.441f,
		-0.208f, -0.0f, 0.043f, 0.444f, 0.444f, -0.214f, -0.0f, 0.034f, 0.451f, 0.451f, -0.22f, 0.0f, 0.026f, 0.46f, 0.46f, -0.226f, 0.0f, 0.018f, 0.463f, 0.463f,
		-0.232f, 0.0f, 0.01f, 0.474f, 0.474f, -0.239f, 0.0f, 0.004f, 0.477f, 0.477f, -0.247f, 0.0f, -0.003f, 0.48f, 0.48f, -0.255f, 0.0f, -0.008f, 0.483f, 0.483f,
		-0.264f, 0.0f, -0.013f, 0.497f, 0.497f, -0.274f, 0.0f, -0.018f, 0.501f, 0.501f, -0.285f, 0.0f, -0.022f, 0.505f, 0.505f, -0.297f, 0.0f, -0.024f, 0.509f, 0.509f,
		-0.311f, 0.0f, -0.025f, 0.51f, 0.51f, -0.325f, 0.0f, -0.024f, 0.512f, 0.512f, -0.339f, 0.0f, -0.023f, 0.512f, 0.512f, -0.354f, 0.0f, -0.022f, 0.513f, 0.513f,
		-0.368f, 0.0f, -0.02f, 0.513f, 0.513f, -0.382f, 0.0f, -0.017f, 0.514f, 0.514f, -0.397f, 0.0f, -0.013f, 0.514f, 0.514f, -0.41f, 0.0f, -0.007f, 0.514f, 0.514f,
		-0.422f, 0.0f, 0.001f, 0.513f, 0.513f, -0.434f, 0.0f, 0.009f, 0.514f, 0.514f, -0.446f, 0.0f, 0.018f, 0.514f, 0.514f, -0.458f, 0.0f, 0.028f, 0.514f, 0.514f,
		-0.47f, -0.0f, 0.039f, 0.514f, 0.514f, -0.48f, 0.0f, 0.048f, 0.514f, 0.514f, -0.487f, 0.0f, 0.057f, 0.514f, 0.514f, -0.493f, 0.0f, 0.068f, 0.514f, 0.514f,
		-0.498f, 0.0f, 0.08f, 0.514f, 0.514f, -0.502f, 0.0f, 0.092f, 0.514f, 0.514f, -0.506f, 0.0f, 0.104f, 0.514f, 0.514f, -0.509f, -0.0f, 0.116f, 0.515f, 0.515f,
		-0.511f, -0.0f, 0.125f, 0.515f, 0.515f, -0.513f, -0.0f, 0.133f, 0.515f, 0.515f, -0.515f, -0.0f, 0.141f, 0.515f, 0.515f, -0.517f, 0.0f, 0.148f, 0.515f, 0.515f,
		-0.519f, 0.0f, 0.155f, 0.514f, 0.514f, -0.52f, 0.0f, 0.161f, 0.514f, 0.514f, -0.522f, 0.0f, 0.168f, 0.514f, 0.514f, -0.523f, 0.0f, 0.174f, 0.514f, 0.514f,
		-0.525f, 0.0f, 0.18f, 0.514f, 0.514f, -0.526f, 0.0f, 0.185f, 0.514f, 0.514f, -0.527f, 0.0f, 0.191f, 0.513f, 0.513f, };
	gpencil_add_points(gps, data12, 123);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Shadow, 125, "Skin_Shadow", 3);
	float data13[125 * 5] = { 0.184f, 0.0f, 0.22f, 0.026f, 0.026f, 0.182f, 0.0f, 0.21f, 0.275f, 0.275f, 0.18f, 0.0f, 0.203f, 0.301f, 0.301f, 0.178f, 0.0f, 0.195f, 0.322f, 0.322f,
		0.176f, 0.0f, 0.186f, 0.343f, 0.343f, 0.173f, 0.0f, 0.176f, 0.36f, 0.36f, 0.17f, -0.0f, 0.166f, 0.367f, 0.367f, 0.168f, -0.0f, 0.156f, 0.38f, 0.38f,
		0.165f, -0.0f, 0.145f, 0.385f, 0.385f, 0.163f, -0.0f, 0.132f, 0.391f, 0.391f, 0.161f, -0.0f, 0.119f, 0.401f, 0.401f, 0.16f, -0.0f, 0.103f, 0.405f, 0.405f,
		0.161f, -0.0f, 0.086f, 0.405f, 0.405f, 0.163f, -0.0f, 0.068f, 0.407f, 0.407f, 0.165f, 0.0f, 0.051f, 0.409f, 0.409f, 0.168f, 0.0f, 0.034f, 0.409f, 0.409f,
		0.172f, 0.0f, 0.018f, 0.409f, 0.409f, 0.177f, 0.0f, 0.004f, 0.409f, 0.409f, 0.183f, 0.0f, -0.008f, 0.411f, 0.411f, 0.19f, 0.0f, -0.022f, 0.411f, 0.411f,
		0.196f, 0.0f, -0.034f, 0.411f, 0.411f, 0.203f, 0.0f, -0.045f, 0.411f, 0.411f, 0.211f, 0.0f, -0.055f, 0.411f, 0.411f, 0.219f, 0.0f, -0.064f, 0.411f, 0.411f,
		0.227f, 0.0f, -0.072f, 0.411f, 0.411f, 0.235f, 0.0f, -0.08f, 0.412f, 0.412f, 0.244f, 0.0f, -0.087f, 0.412f, 0.412f, 0.253f, 0.0f, -0.094f, 0.413f, 0.413f,
		0.262f, 0.0f, -0.1f, 0.413f, 0.413f, 0.273f, 0.0f, -0.105f, 0.413f, 0.413f, 0.284f, 0.0f, -0.11f, 0.413f, 0.413f, 0.295f, 0.0f, -0.114f, 0.419f, 0.419f,
		0.307f, 0.0f, -0.117f, 0.425f, 0.425f, 0.321f, -0.0f, -0.118f, 0.433f, 0.433f, 0.334f, -0.0f, -0.12f, 0.446f, 0.446f, 0.347f, -0.0f, -0.12f, 0.474f, 0.474f,
		0.36f, -0.0f, -0.12f, 0.481f, 0.481f, 0.374f, -0.0f, -0.119f, 0.491f, 0.491f, 0.387f, -0.0f, -0.118f, 0.494f, 0.494f, 0.401f, 0.0f, -0.116f, 0.5f, 0.5f,
		0.414f, 0.0f, -0.112f, 0.505f, 0.505f, 0.426f, -0.0f, -0.107f, 0.51f, 0.51f, 0.438f, -0.0f, -0.101f, 0.513f, 0.513f, 0.449f, -0.0f, -0.094f, 0.515f, 0.515f,
		0.46f, -0.0f, -0.086f, 0.517f, 0.517f, 0.47f, -0.0f, -0.078f, 0.519f, 0.519f, 0.478f, -0.0f, -0.07f, 0.52f, 0.52f, 0.486f, -0.0f, -0.061f, 0.522f, 0.522f,
		0.493f, -0.0f, -0.052f, 0.523f, 0.523f, 0.499f, -0.0f, -0.044f, 0.522f, 0.522f, 0.505f, -0.0f, -0.035f, 0.522f, 0.522f, 0.51f, -0.0f, -0.027f, 0.523f, 0.523f,
		0.514f, -0.0f, -0.018f, 0.523f, 0.523f, 0.517f, -0.0f, -0.009f, 0.523f, 0.523f, 0.52f, -0.0f, -0.001f, 0.524f, 0.524f, 0.522f, -0.0f, 0.008f, 0.523f, 0.523f,
		0.525f, -0.0f, 0.018f, 0.521f, 0.522f, 0.527f, -0.0f, 0.027f, 0.515f, 0.514f, 0.529f, -0.0f, 0.036f, 0.512f, 0.512f, 0.531f, -0.0f, 0.045f, 0.509f, 0.51f,
		0.533f, -0.0f, 0.053f, 0.506f, 0.505f, 0.535f, -0.0f, 0.062f, 0.503f, 0.503f, 0.536f, -0.0f, 0.071f, 0.5f, 0.5f, 0.538f, -0.0f, 0.08f, 0.496f, 0.497f,
		0.538f, -0.0f, 0.09f, 0.491f, 0.492f, 0.539f, -0.0f, 0.1f, 0.485f, 0.486f, 0.539f, 0.0f, 0.11f, 0.475f, 0.476f, 0.539f, 0.0f, 0.12f, 0.46f, 0.459f,
		0.539f, 0.0f, 0.13f, 0.444f, 0.448f, 0.538f, 0.0f, 0.139f, 0.406f, 0.405f, 0.537f, 0.0f, 0.144f, 0.399f, 0.399f, 0.536f, 0.0f, 0.146f, 0.395f, 0.395f,
		0.535f, 0.0f, 0.144f, 0.412f, 0.412f, 0.533f, 0.0f, 0.139f, 0.413f, 0.413f, 0.53f, 0.0f, 0.131f, 0.414f, 0.413f, 0.528f, 0.0f, 0.122f, 0.419f, 0.418f,
		0.525f, 0.0f, 0.112f, 0.425f, 0.424f, 0.521f, 0.0f, 0.102f, 0.444f, 0.444f, 0.518f, 0.0f, 0.094f, 0.451f, 0.452f, 0.514f, 0.0f, 0.085f, 0.457f, 0.457f,
		0.509f, 0.0f, 0.078f, 0.461f, 0.46f, 0.504f, 0.0f, 0.069f, 0.469f, 0.468f, 0.499f, 0.0f, 0.06f, 0.481f, 0.481f, 0.493f, 0.0f, 0.052f, 0.489f, 0.489f,
		0.487f, 0.0f, 0.044f, 0.492f, 0.492f, 0.481f, 0.0f, 0.037f, 0.501f, 0.5f, 0.474f, 0.0f, 0.029f, 0.513f, 0.513f, 0.467f, 0.0f, 0.022f, 0.521f, 0.521f,
		0.458f, 0.0f, 0.015f, 0.524f, 0.524f, 0.449f, 0.0f, 0.008f, 0.525f, 0.525f, 0.439f, 0.0f, 0.001f, 0.528f, 0.528f, 0.427f, 0.0f, -0.005f, 0.532f, 0.532f,
		0.416f, 0.0f, -0.011f, 0.533f, 0.533f, 0.401f, 0.0f, -0.015f, 0.537f, 0.537f, 0.386f, 0.0f, -0.018f, 0.539f, 0.539f, 0.371f, 0.0f, -0.02f, 0.538f, 0.538f,
		0.356f, 0.0f, -0.021f, 0.543f, 0.543f, 0.341f, 0.0f, -0.023f, 0.543f, 0.543f, 0.326f, 0.0f, -0.023f, 0.543f, 0.543f, 0.312f, 0.0f, -0.022f, 0.543f, 0.543f,
		0.298f, 0.0f, -0.018f, 0.543f, 0.543f, 0.286f, 0.0f, -0.014f, 0.543f, 0.543f, 0.273f, 0.0f, -0.006f, 0.543f, 0.543f, 0.26f, 0.0f, 0.004f, 0.543f, 0.543f,
		0.247f, 0.0f, 0.013f, 0.543f, 0.543f, 0.235f, 0.0f, 0.022f, 0.543f, 0.543f, 0.225f, 0.0f, 0.033f, 0.543f, 0.543f, 0.215f, 0.0f, 0.045f, 0.542f, 0.542f,
		0.206f, 0.0f, 0.061f, 0.54f, 0.54f, 0.199f, 0.0f, 0.078f, 0.542f, 0.542f, 0.193f, 0.0f, 0.094f, 0.542f, 0.542f, 0.189f, -0.0f, 0.109f, 0.541f, 0.541f,
		0.186f, -0.0f, 0.119f, 0.542f, 0.542f, 0.185f, -0.0f, 0.127f, 0.542f, 0.542f, 0.184f, -0.0f, 0.135f, 0.542f, 0.542f, 0.184f, -0.0f, 0.142f, 0.542f, 0.542f,
		0.183f, -0.0f, 0.149f, 0.541f, 0.541f, 0.183f, -0.0f, 0.156f, 0.538f, 0.538f, 0.183f, -0.0f, 0.163f, 0.539f, 0.539f, 0.183f, -0.0f, 0.17f, 0.54f, 0.54f,
		0.183f, 0.0f, 0.177f, 0.54f, 0.54f, 0.183f, 0.0f, 0.184f, 0.54f, 0.54f, 0.183f, 0.0f, 0.191f, 0.54f, 0.54f, 0.184f, 0.0f, 0.196f, 0.539f, 0.539f,
		0.184f, 0.0f, 0.204f, 0.518f, 0.518f, };
	gpencil_add_points(gps, data13, 125);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Shadow, 45, "Skin_Shadow", 3);
	float data14[45 * 5] = { -0.096f, -0.0f, -0.305f, 0.01f, 0.01f, -0.09f, -0.0f, -0.313f, 0.121f, 0.362f, -0.086f, -0.0f, -0.318f, 0.179f, 0.368f, -0.081f, -0.0f, -0.325f, 0.234f, 0.37f,
		-0.075f, -0.0f, -0.331f, 0.272f, 0.37f, -0.068f, -0.0f, -0.338f, 0.302f, 0.371f, -0.061f, -0.0f, -0.345f, 0.324f, 0.374f, -0.053f, -0.0f, -0.352f, 0.34f, 0.377f,
		-0.044f, -0.0f, -0.358f, 0.352f, 0.378f, -0.035f, -0.0f, -0.362f, 0.362f, 0.377f, -0.026f, -0.0f, -0.366f, 0.37f, 0.378f, -0.018f, -0.0f, -0.368f, 0.377f, 0.378f,
		-0.009f, -0.0f, -0.369f, 0.383f, 0.376f, -0.001f, -0.0f, -0.369f, 0.389f, 0.369f, 0.007f, -0.0f, -0.368f, 0.395f, 0.364f, 0.015f, -0.0f, -0.367f, 0.4f, 0.388f,
		0.023f, -0.0f, -0.365f, 0.405f, 0.41f, 0.03f, -0.0f, -0.363f, 0.41f, 0.429f, 0.038f, -0.0f, -0.36f, 0.414f, 0.438f, 0.044f, -0.0f, -0.357f, 0.417f, 0.441f,
		0.05f, -0.0f, -0.355f, 0.419f, 0.444f, 0.055f, -0.0f, -0.352f, 0.42f, 0.441f, 0.06f, -0.0f, -0.349f, 0.421f, 0.445f, 0.063f, -0.0f, -0.347f, 0.421f, 0.446f,
		0.065f, -0.0f, -0.344f, 0.42f, 0.443f, 0.065f, -0.0f, -0.342f, 0.42f, 0.437f, 0.065f, -0.0f, -0.341f, 0.419f, 0.413f, 0.063f, -0.0f, -0.339f, 0.418f, 0.404f,
		0.061f, -0.0f, -0.338f, 0.418f, 0.403f, 0.057f, -0.0f, -0.337f, 0.418f, 0.402f, 0.052f, -0.0f, -0.337f, 0.418f, 0.407f, 0.046f, -0.0f, -0.337f, 0.419f, 0.411f,
		0.04f, 0.0f, -0.336f, 0.42f, 0.416f, 0.032f, 0.0f, -0.337f, 0.422f, 0.421f, 0.023f, 0.0f, -0.339f, 0.424f, 0.425f, 0.014f, 0.0f, -0.34f, 0.426f, 0.427f,
		0.003f, 0.0f, -0.341f, 0.428f, 0.427f, -0.007f, 0.0f, -0.341f, 0.43f, 0.433f, -0.018f, 0.0f, -0.339f, 0.432f, 0.437f, -0.027f, 0.0f, -0.335f, 0.434f, 0.438f,
		-0.037f, 0.0f, -0.33f, 0.435f, 0.437f, -0.046f, -0.0f, -0.326f, 0.436f, 0.438f, -0.055f, -0.0f, -0.321f, 0.436f, 0.44f, -0.062f, -0.0f, -0.316f, 0.437f, 0.439f,
		-0.073f, -0.0f, -0.31f, 0.437f, 0.437f, };
	gpencil_add_points(gps, data14, 45);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Shadow, 44, "Skin_Shadow", 3);
	float data15[44 * 5] = { -0.085f, 0.0f, -0.816f, 0.138f, 0.138f, -0.079f, 0.0f, -0.825f, 0.246f, 0.309f, -0.074f, 0.0f, -0.832f, 0.302f, 0.34f, -0.067f, 0.0f, -0.84f, 0.335f, 0.352f,
		-0.059f, 0.0f, -0.848f, 0.357f, 0.374f, -0.05f, 0.0f, -0.855f, 0.371f, 0.378f, -0.041f, 0.0f, -0.861f, 0.382f, 0.383f, -0.031f, 0.0f, -0.866f, 0.391f, 0.396f,
		-0.021f, 0.0f, -0.871f, 0.398f, 0.401f, -0.011f, 0.0f, -0.874f, 0.404f, 0.407f, -0.001f, 0.0f, -0.877f, 0.409f, 0.411f, 0.01f, 0.0f, -0.878f, 0.415f, 0.412f,
		0.02f, 0.0f, -0.878f, 0.422f, 0.417f, 0.031f, 0.0f, -0.878f, 0.43f, 0.421f, 0.042f, 0.0f, -0.876f, 0.438f, 0.437f, 0.052f, 0.0f, -0.873f, 0.445f, 0.451f,
		0.062f, 0.0f, -0.868f, 0.451f, 0.459f, 0.071f, 0.0f, -0.863f, 0.456f, 0.463f, 0.08f, 0.0f, -0.857f, 0.46f, 0.465f, 0.087f, 0.0f, -0.85f, 0.462f, 0.465f,
		0.094f, 0.0f, -0.842f, 0.461f, 0.465f, 0.098f, 0.0f, -0.835f, 0.458f, 0.467f, 0.101f, 0.0f, -0.827f, 0.451f, 0.457f, 0.103f, 0.0f, -0.82f, 0.436f, 0.451f,
		0.102f, 0.0f, -0.815f, 0.422f, 0.418f, 0.1f, 0.0f, -0.811f, 0.419f, 0.378f, 0.096f, 0.0f, -0.814f, 0.436f, 0.447f, 0.089f, 0.0f, -0.817f, 0.454f, 0.465f,
		0.082f, 0.0f, -0.821f, 0.465f, 0.47f, 0.072f, 0.0f, -0.825f, 0.473f, 0.477f, 0.061f, 0.0f, -0.828f, 0.477f, 0.479f, 0.049f, 0.0f, -0.832f, 0.48f, 0.485f,
		0.036f, 0.0f, -0.834f, 0.483f, 0.48f, 0.023f, 0.0f, -0.836f, 0.484f, 0.485f, 0.01f, 0.0f, -0.838f, 0.486f, 0.487f, -0.003f, 0.0f, -0.84f, 0.486f, 0.488f,
		-0.016f, 0.0f, -0.84f, 0.486f, 0.489f, -0.027f, 0.0f, -0.84f, 0.485f, 0.485f, -0.039f, 0.0f, -0.839f, 0.484f, 0.484f, -0.049f, 0.0f, -0.837f, 0.483f, 0.485f,
		-0.058f, 0.0f, -0.834f, 0.48f, 0.481f, -0.066f, 0.0f, -0.83f, 0.473f, 0.479f, -0.072f, 0.0f, -0.827f, 0.462f, 0.472f, -0.081f, 0.0f, -0.823f, 0.442f, 0.442f,
	};
	gpencil_add_points(gps, data15, 44);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Shadow, 84, "Skin_Shadow", 3);
	float data16[84 * 5] = { 0.737f, 0.0f, 0.177f, 0.148f, 0.148f, 0.735f, 0.0f, 0.164f, 0.214f, 0.39f, 0.734f, 0.0f, 0.155f, 0.254f, 0.402f, 0.732f, 0.0f, 0.143f, 0.295f, 0.413f,
		0.73f, 0.0f, 0.132f, 0.328f, 0.415f, 0.728f, 0.0f, 0.121f, 0.355f, 0.415f, 0.726f, 0.0f, 0.109f, 0.375f, 0.416f, 0.724f, 0.0f, 0.097f, 0.39f, 0.417f,
		0.721f, 0.0f, 0.086f, 0.401f, 0.418f, 0.719f, 0.0f, 0.074f, 0.408f, 0.419f, 0.716f, 0.0f, 0.062f, 0.413f, 0.42f, 0.713f, 0.0f, 0.05f, 0.416f, 0.42f,
		0.71f, 0.0f, 0.039f, 0.418f, 0.421f, 0.707f, 0.0f, 0.028f, 0.42f, 0.421f, 0.703f, 0.0f, 0.017f, 0.421f, 0.422f, 0.7f, 0.0f, 0.006f, 0.421f, 0.422f,
		0.696f, 0.0f, -0.005f, 0.422f, 0.422f, 0.693f, 0.0f, -0.015f, 0.422f, 0.422f, 0.689f, 0.0f, -0.025f, 0.423f, 0.423f, 0.685f, 0.0f, -0.034f, 0.423f, 0.423f,
		0.681f, 0.0f, -0.044f, 0.423f, 0.423f, 0.677f, 0.0f, -0.053f, 0.423f, 0.423f, 0.672f, 0.0f, -0.062f, 0.423f, 0.423f, 0.668f, 0.0f, -0.071f, 0.422f, 0.424f,
		0.662f, 0.0f, -0.08f, 0.422f, 0.424f, 0.657f, 0.0f, -0.088f, 0.422f, 0.422f, 0.651f, 0.0f, -0.095f, 0.421f, 0.419f, 0.645f, 0.0f, -0.103f, 0.42f, 0.419f,
		0.638f, 0.0f, -0.109f, 0.42f, 0.419f, 0.631f, 0.0f, -0.115f, 0.419f, 0.419f, 0.624f, 0.0f, -0.12f, 0.419f, 0.419f, 0.617f, 0.0f, -0.125f, 0.419f, 0.419f,
		0.61f, 0.0f, -0.129f, 0.418f, 0.418f, 0.602f, 0.0f, -0.133f, 0.418f, 0.416f, 0.594f, 0.0f, -0.137f, 0.417f, 0.416f, 0.587f, 0.0f, -0.14f, 0.417f, 0.415f,
		0.579f, 0.0f, -0.142f, 0.417f, 0.416f, 0.571f, 0.0f, -0.144f, 0.417f, 0.415f, 0.564f, 0.0f, -0.145f, 0.417f, 0.416f, 0.556f, 0.0f, -0.146f, 0.417f, 0.415f,
		0.549f, 0.0f, -0.146f, 0.417f, 0.415f, 0.541f, 0.0f, -0.146f, 0.417f, 0.415f, 0.535f, 0.0f, -0.145f, 0.417f, 0.416f, 0.53f, 0.0f, -0.143f, 0.418f, 0.418f,
		0.526f, 0.0f, -0.14f, 0.418f, 0.418f, 0.524f, 0.0f, -0.136f, 0.42f, 0.418f, 0.524f, 0.0f, -0.132f, 0.422f, 0.416f, 0.527f, 0.0f, -0.126f, 0.424f, 0.424f,
		0.531f, 0.0f, -0.121f, 0.427f, 0.428f, 0.536f, 0.0f, -0.115f, 0.43f, 0.433f, 0.542f, 0.0f, -0.109f, 0.433f, 0.436f, 0.548f, 0.0f, -0.102f, 0.435f, 0.436f,
		0.555f, 0.0f, -0.095f, 0.436f, 0.437f, 0.562f, 0.0f, -0.088f, 0.437f, 0.438f, 0.568f, 0.0f, -0.081f, 0.437f, 0.438f, 0.575f, 0.0f, -0.073f, 0.438f, 0.438f,
		0.581f, 0.0f, -0.065f, 0.438f, 0.438f, 0.587f, 0.0f, -0.058f, 0.438f, 0.438f, 0.593f, 0.0f, -0.05f, 0.438f, 0.438f, 0.599f, 0.0f, -0.041f, 0.438f, 0.438f,
		0.605f, 0.0f, -0.033f, 0.438f, 0.438f, 0.61f, 0.0f, -0.024f, 0.438f, 0.438f, 0.615f, 0.0f, -0.015f, 0.438f, 0.438f, 0.621f, 0.0f, -0.006f, 0.438f, 0.438f,
		0.626f, 0.0f, 0.004f, 0.438f, 0.438f, 0.631f, 0.0f, 0.013f, 0.437f, 0.438f, 0.636f, 0.0f, 0.023f, 0.436f, 0.438f, 0.641f, 0.0f, 0.032f, 0.434f, 0.438f,
		0.647f, 0.0f, 0.042f, 0.432f, 0.437f, 0.652f, 0.0f, 0.051f, 0.431f, 0.429f, 0.657f, 0.0f, 0.06f, 0.429f, 0.426f, 0.662f, 0.0f, 0.069f, 0.427f, 0.425f,
		0.668f, 0.0f, 0.078f, 0.425f, 0.425f, 0.673f, 0.0f, 0.087f, 0.423f, 0.424f, 0.678f, 0.0f, 0.095f, 0.42f, 0.422f, 0.683f, 0.0f, 0.104f, 0.416f, 0.42f,
		0.688f, 0.0f, 0.112f, 0.411f, 0.421f, 0.693f, 0.0f, 0.12f, 0.403f, 0.417f, 0.698f, 0.0f, 0.128f, 0.394f, 0.411f, 0.702f, 0.0f, 0.135f, 0.382f, 0.404f,
		0.707f, 0.0f, 0.143f, 0.369f, 0.388f, 0.711f, 0.0f, 0.15f, 0.352f, 0.371f, 0.714f, 0.0f, 0.155f, 0.338f, 0.352f, 0.719f, 0.0f, 0.164f, 0.315f, 0.315f,
	};
	gpencil_add_points(gps, data16, 84);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Shadow, 56, "Skin_Shadow", 3);
	float data17[56 * 5] = { -1.007f, -0.0f, 0.183f, 0.022f, 0.022f, -1.003f, -0.0f, 0.181f, 0.192f, 0.436f, -0.998f, -0.0f, 0.18f, 0.28f, 0.451f, -0.99f, -0.0f, 0.178f, 0.355f, 0.459f,
		-0.98f, -0.0f, 0.175f, 0.402f, 0.464f, -0.967f, -0.0f, 0.169f, 0.432f, 0.467f, -0.952f, -0.0f, 0.152f, 0.449f, 0.468f, -0.943f, 0.0f, 0.138f, 0.459f, 0.469f,
		-0.939f, 0.0f, 0.128f, 0.464f, 0.469f, -0.934f, 0.0f, 0.119f, 0.467f, 0.47f, -0.929f, 0.0f, 0.11f, 0.469f, 0.47f, -0.924f, 0.0f, 0.101f, 0.47f, 0.47f,
		-0.919f, 0.0f, 0.092f, 0.47f, 0.471f, -0.913f, 0.0f, 0.082f, 0.471f, 0.471f, -0.908f, 0.0f, 0.072f, 0.471f, 0.471f, -0.903f, 0.0f, 0.063f, 0.472f, 0.472f,
		-0.897f, 0.0f, 0.053f, 0.472f, 0.472f, -0.892f, 0.0f, 0.044f, 0.473f, 0.473f, -0.886f, 0.0f, 0.035f, 0.473f, 0.473f, -0.881f, 0.0f, 0.026f, 0.473f, 0.473f,
		-0.876f, 0.0f, 0.018f, 0.473f, 0.473f, -0.87f, 0.0f, 0.012f, 0.472f, 0.473f, -0.865f, 0.0f, 0.006f, 0.47f, 0.473f, -0.86f, 0.0f, 0.003f, 0.468f, 0.473f,
		-0.855f, 0.0f, 0.001f, 0.466f, 0.469f, -0.85f, 0.0f, 0.001f, 0.463f, 0.469f, -0.846f, 0.0f, 0.003f, 0.46f, 0.45f, -0.843f, 0.0f, 0.008f, 0.458f, 0.454f,
		-0.84f, 0.0f, 0.014f, 0.456f, 0.454f, -0.838f, 0.0f, 0.021f, 0.455f, 0.454f, -0.836f, 0.0f, 0.03f, 0.453f, 0.455f, -0.835f, 0.0f, 0.039f, 0.451f, 0.455f,
		-0.835f, 0.0f, 0.049f, 0.449f, 0.453f, -0.836f, 0.0f, 0.059f, 0.447f, 0.445f, -0.837f, 0.0f, 0.068f, 0.445f, 0.441f, -0.84f, 0.0f, 0.078f, 0.443f, 0.44f,
		-0.843f, 0.0f, 0.087f, 0.442f, 0.44f, -0.846f, 0.0f, 0.095f, 0.442f, 0.44f, -0.851f, -0.0f, 0.103f, 0.441f, 0.441f, -0.855f, -0.0f, 0.111f, 0.441f, 0.44f,
		-0.86f, -0.0f, 0.119f, 0.441f, 0.441f, -0.865f, -0.0f, 0.127f, 0.441f, 0.441f, -0.871f, -0.0f, 0.134f, 0.441f, 0.441f, -0.877f, -0.0f, 0.141f, 0.441f, 0.441f,
		-0.883f, -0.0f, 0.149f, 0.441f, 0.442f, -0.889f, -0.0f, 0.156f, 0.441f, 0.441f, -0.896f, -0.0f, 0.163f, 0.441f, 0.442f, -0.904f, -0.0f, 0.169f, 0.442f, 0.441f,
		-0.913f, -0.0f, 0.176f, 0.442f, 0.441f, -0.925f, -0.0f, 0.183f, 0.443f, 0.441f, -0.941f, -0.0f, 0.19f, 0.444f, 0.442f, -0.956f, -0.0f, 0.195f, 0.446f, 0.443f,
		-0.971f, -0.0f, 0.198f, 0.448f, 0.443f, -0.983f, -0.0f, 0.198f, 0.451f, 0.452f, -0.992f, -0.0f, 0.198f, 0.454f, 0.456f, -1.001f, 0.0f, 0.196f, 0.457f, 0.457f,
	};
	gpencil_add_points(gps, data17, 56);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Shadow, 59, "Skin_Shadow", 3);
	float data18[59 * 5] = { 0.782f, 0.0f, 0.099f, 0.04f, 0.04f, 0.779f, 0.0f, 0.088f, 0.108f, 0.34f, 0.777f, 0.0f, 0.08f, 0.149f, 0.35f, 0.774f, 0.0f, 0.071f, 0.194f, 0.352f,
		0.772f, 0.0f, 0.062f, 0.231f, 0.352f, 0.771f, 0.0f, 0.053f, 0.263f, 0.353f, 0.769f, 0.0f, 0.044f, 0.289f, 0.353f, 0.768f, 0.0f, 0.036f, 0.31f, 0.353f,
		0.767f, 0.0f, 0.029f, 0.327f, 0.353f, 0.767f, 0.0f, 0.023f, 0.341f, 0.353f, 0.767f, 0.0f, 0.017f, 0.353f, 0.353f, 0.768f, 0.0f, 0.013f, 0.363f, 0.353f,
		0.769f, 0.0f, 0.01f, 0.373f, 0.353f, 0.771f, 0.0f, 0.009f, 0.382f, 0.351f, 0.773f, 0.0f, 0.008f, 0.39f, 0.393f, 0.776f, 0.0f, 0.009f, 0.399f, 0.41f,
		0.779f, 0.0f, 0.011f, 0.407f, 0.425f, 0.783f, 0.0f, 0.015f, 0.415f, 0.434f, 0.787f, 0.0f, 0.019f, 0.423f, 0.44f, 0.792f, 0.0f, 0.024f, 0.429f, 0.441f,
		0.797f, 0.0f, 0.03f, 0.435f, 0.444f, 0.802f, 0.0f, 0.037f, 0.441f, 0.447f, 0.807f, 0.0f, 0.044f, 0.445f, 0.453f, 0.813f, 0.0f, 0.051f, 0.449f, 0.457f,
		0.819f, 0.0f, 0.058f, 0.452f, 0.458f, 0.825f, 0.0f, 0.066f, 0.455f, 0.46f, 0.831f, 0.0f, 0.074f, 0.457f, 0.462f, 0.838f, 0.0f, 0.082f, 0.459f, 0.462f,
		0.845f, 0.0f, 0.09f, 0.461f, 0.462f, 0.852f, 0.0f, 0.098f, 0.462f, 0.463f, 0.859f, 0.0f, 0.106f, 0.463f, 0.464f, 0.867f, 0.0f, 0.113f, 0.464f, 0.464f,
		0.874f, 0.0f, 0.121f, 0.465f, 0.465f, 0.882f, 0.0f, 0.129f, 0.465f, 0.465f, 0.889f, 0.0f, 0.136f, 0.466f, 0.466f, 0.897f, 0.0f, 0.143f, 0.466f, 0.467f,
		0.904f, 0.0f, 0.15f, 0.467f, 0.466f, 0.911f, 0.0f, 0.157f, 0.467f, 0.467f, 0.916f, 0.0f, 0.163f, 0.468f, 0.468f, 0.921f, 0.0f, 0.169f, 0.468f, 0.469f,
		0.924f, 0.0f, 0.173f, 0.468f, 0.469f, 0.926f, 0.0f, 0.177f, 0.469f, 0.468f, 0.925f, 0.0f, 0.18f, 0.469f, 0.468f, 0.922f, 0.0f, 0.181f, 0.469f, 0.469f,
		0.918f, 0.0f, 0.181f, 0.469f, 0.469f, 0.912f, 0.0f, 0.18f, 0.469f, 0.469f, 0.905f, 0.0f, 0.178f, 0.468f, 0.47f, 0.898f, 0.0f, 0.175f, 0.466f, 0.471f,
		0.89f, 0.0f, 0.172f, 0.462f, 0.469f, 0.882f, 0.0f, 0.168f, 0.454f, 0.468f, 0.874f, 0.0f, 0.164f, 0.442f, 0.467f, 0.866f, 0.0f, 0.159f, 0.423f, 0.467f,
		0.858f, 0.0f, 0.154f, 0.398f, 0.468f, 0.851f, 0.0f, 0.149f, 0.366f, 0.468f, 0.844f, 0.0f, 0.144f, 0.326f, 0.469f, 0.837f, 0.0f, 0.139f, 0.282f, 0.469f,
		0.83f, 0.0f, 0.134f, 0.231f, 0.467f, 0.824f, 0.0f, 0.13f, 0.184f, 0.415f, 0.816f, 0.0f, 0.124f, 0.111f, 0.111f, };
	gpencil_add_points(gps, data18, 59);

	gps = gpencil_add_stroke(frameColor, palette, color_Skin_Shadow, 100, "Skin_Shadow", 3);
	float data19[100 * 5] = { -0.279f, 0.0f, 0.568f, 0.154f, 0.154f, -0.266f, 0.0f, 0.569f, 0.249f, 0.318f, -0.258f, 0.0f, 0.57f, 0.296f, 0.357f, -0.248f, 0.0f, 0.571f, 0.337f, 0.383f,
		-0.238f, 0.0f, 0.571f, 0.363f, 0.396f, -0.229f, 0.0f, 0.571f, 0.381f, 0.403f, -0.219f, 0.0f, 0.57f, 0.392f, 0.407f, -0.209f, 0.0f, 0.568f, 0.399f, 0.407f,
		-0.2f, 0.0f, 0.566f, 0.403f, 0.408f, -0.19f, 0.0f, 0.563f, 0.406f, 0.41f, -0.181f, 0.0f, 0.559f, 0.407f, 0.41f, -0.171f, 0.0f, 0.555f, 0.409f, 0.41f,
		-0.161f, 0.0f, 0.551f, 0.409f, 0.411f, -0.152f, 0.0f, 0.546f, 0.41f, 0.411f, -0.142f, 0.0f, 0.542f, 0.41f, 0.412f, -0.132f, 0.0f, 0.537f, 0.411f, 0.411f,
		-0.122f, 0.0f, 0.533f, 0.411f, 0.411f, -0.112f, 0.0f, 0.528f, 0.411f, 0.412f, -0.102f, 0.0f, 0.524f, 0.411f, 0.412f, -0.092f, 0.0f, 0.519f, 0.41f, 0.412f,
		-0.081f, 0.0f, 0.515f, 0.407f, 0.411f, -0.071f, 0.0f, 0.511f, 0.403f, 0.408f, -0.061f, 0.0f, 0.507f, 0.399f, 0.401f, -0.051f, 0.0f, 0.503f, 0.394f, 0.395f,
		-0.041f, 0.0f, 0.499f, 0.39f, 0.388f, -0.031f, 0.0f, 0.495f, 0.386f, 0.383f, -0.021f, 0.0f, 0.491f, 0.383f, 0.38f, -0.011f, 0.0f, 0.488f, 0.381f, 0.378f,
		-0.001f, 0.0f, 0.486f, 0.379f, 0.377f, 0.009f, 0.0f, 0.484f, 0.378f, 0.377f, 0.019f, 0.0f, 0.483f, 0.377f, 0.375f, 0.03f, 0.0f, 0.482f, 0.377f, 0.375f,
		0.041f, 0.0f, 0.482f, 0.378f, 0.376f, 0.051f, 0.0f, 0.483f, 0.379f, 0.376f, 0.062f, 0.0f, 0.484f, 0.381f, 0.376f, 0.073f, 0.0f, 0.486f, 0.385f, 0.379f,
		0.085f, 0.0f, 0.488f, 0.389f, 0.382f, 0.096f, 0.0f, 0.491f, 0.395f, 0.392f, 0.108f, 0.0f, 0.494f, 0.402f, 0.4f, 0.12f, 0.0f, 0.497f, 0.409f, 0.409f,
		0.132f, 0.0f, 0.501f, 0.415f, 0.416f, 0.144f, 0.0f, 0.505f, 0.421f, 0.427f, 0.157f, 0.0f, 0.509f, 0.425f, 0.43f, 0.17f, 0.0f, 0.513f, 0.429f, 0.433f,
		0.181f, 0.0f, 0.517f, 0.431f, 0.433f, 0.192f, 0.0f, 0.52f, 0.433f, 0.434f, 0.201f, 0.0f, 0.522f, 0.433f, 0.435f, 0.208f, 0.0f, 0.524f, 0.433f, 0.435f,
		0.213f, 0.0f, 0.524f, 0.432f, 0.436f, 0.216f, 0.0f, 0.523f, 0.431f, 0.435f, 0.217f, 0.0f, 0.521f, 0.43f, 0.426f, 0.215f, 0.0f, 0.518f, 0.429f, 0.427f,
		0.213f, 0.0f, 0.515f, 0.428f, 0.427f, 0.208f, 0.0f, 0.511f, 0.428f, 0.427f, 0.203f, 0.0f, 0.506f, 0.428f, 0.427f, 0.196f, 0.0f, 0.502f, 0.428f, 0.427f,
		0.189f, 0.0f, 0.497f, 0.428f, 0.427f, 0.181f, 0.0f, 0.492f, 0.428f, 0.427f, 0.173f, 0.0f, 0.487f, 0.428f, 0.428f, 0.163f, 0.0f, 0.482f, 0.429f, 0.428f,
		0.154f, 0.0f, 0.477f, 0.429f, 0.429f, 0.145f, 0.0f, 0.472f, 0.43f, 0.43f, 0.135f, 0.0f, 0.467f, 0.431f, 0.431f, 0.125f, 0.0f, 0.462f, 0.432f, 0.43f,
		0.116f, 0.0f, 0.457f, 0.433f, 0.431f, 0.106f, 0.0f, 0.453f, 0.435f, 0.434f, 0.096f, 0.0f, 0.448f, 0.436f, 0.436f, 0.086f, 0.0f, 0.444f, 0.437f, 0.438f,
		0.076f, 0.0f, 0.44f, 0.438f, 0.44f, 0.065f, 0.0f, 0.436f, 0.439f, 0.441f, 0.055f, 0.0f, 0.433f, 0.44f, 0.441f, 0.044f, 0.0f, 0.431f, 0.441f, 0.442f,
		0.033f, 0.0f, 0.429f, 0.441f, 0.442f, 0.022f, 0.0f, 0.427f, 0.441f, 0.442f, 0.011f, 0.0f, 0.426f, 0.442f, 0.443f, -0.0f, 0.0f, 0.426f, 0.442f, 0.442f,
		-0.011f, 0.0f, 0.426f, 0.442f, 0.442f, -0.022f, 0.0f, 0.427f, 0.442f, 0.442f, -0.033f, 0.0f, 0.429f, 0.442f, 0.442f, -0.042f, 0.0f, 0.432f, 0.441f, 0.442f,
		-0.052f, 0.0f, 0.435f, 0.441f, 0.441f, -0.061f, 0.0f, 0.439f, 0.441f, 0.441f, -0.07f, 0.0f, 0.443f, 0.441f, 0.441f, -0.078f, 0.0f, 0.448f, 0.441f, 0.441f,
		-0.087f, 0.0f, 0.453f, 0.441f, 0.442f, -0.095f, 0.0f, 0.458f, 0.441f, 0.441f, -0.104f, 0.0f, 0.463f, 0.44f, 0.44f, -0.113f, 0.0f, 0.468f, 0.44f, 0.44f,
		-0.122f, 0.0f, 0.473f, 0.44f, 0.44f, -0.132f, 0.0f, 0.479f, 0.44f, 0.44f, -0.143f, 0.0f, 0.485f, 0.44f, 0.44f, -0.154f, 0.0f, 0.491f, 0.44f, 0.44f,
		-0.165f, 0.0f, 0.498f, 0.44f, 0.44f, -0.176f, 0.0f, 0.504f, 0.439f, 0.439f, -0.187f, 0.0f, 0.51f, 0.435f, 0.44f, -0.198f, 0.0f, 0.516f, 0.424f, 0.44f,
		-0.209f, 0.0f, 0.522f, 0.393f, 0.44f, -0.219f, 0.0f, 0.527f, 0.324f, 0.44f, -0.228f, 0.0f, 0.532f, 0.222f, 0.404f, -0.241f, 0.0f, 0.538f, 0.037f, 0.037f,
	};
	gpencil_add_points(gps, data19, 100);

	gps = gpencil_add_stroke(frameColor, palette, color_Eyes, 136, "Eyes", 3);
	float data20[136 * 5] = { 0.331f, 0.0f, -0.036f, 0.065f, 0.065f, 0.322f, 0.0f, -0.034f, 0.239f, 0.293f, 0.317f, 0.0f, -0.032f, 0.316f, 0.339f, 0.31f, 0.0f, -0.029f, 0.348f, 0.355f,
		0.302f, 0.0f, -0.027f, 0.364f, 0.368f, 0.295f, 0.0f, -0.023f, 0.373f, 0.374f, 0.287f, 0.0f, -0.02f, 0.381f, 0.381f, 0.279f, 0.0f, -0.015f, 0.388f, 0.391f,
		0.271f, 0.0f, -0.01f, 0.392f, 0.394f, 0.263f, 0.0f, -0.004f, 0.395f, 0.396f, 0.255f, 0.0f, 0.002f, 0.397f, 0.397f, 0.247f, 0.0f, 0.008f, 0.399f, 0.4f,
		0.239f, 0.0f, 0.016f, 0.401f, 0.401f, 0.232f, -0.0f, 0.024f, 0.404f, 0.404f, 0.226f, 0.0f, 0.031f, 0.406f, 0.407f, 0.221f, 0.0f, 0.038f, 0.409f, 0.409f,
		0.215f, 0.0f, 0.045f, 0.412f, 0.412f, 0.21f, 0.0f, 0.054f, 0.415f, 0.415f, 0.205f, 0.0f, 0.063f, 0.417f, 0.417f, 0.201f, 0.0f, 0.073f, 0.42f, 0.421f,
		0.197f, 0.0f, 0.083f, 0.421f, 0.421f, 0.193f, -0.0f, 0.094f, 0.423f, 0.423f, 0.19f, -0.0f, 0.104f, 0.424f, 0.424f, 0.187f, -0.0f, 0.114f, 0.424f, 0.425f,
		0.185f, -0.0f, 0.125f, 0.425f, 0.425f, 0.183f, -0.0f, 0.135f, 0.425f, 0.425f, 0.182f, -0.0f, 0.146f, 0.426f, 0.425f, 0.181f, -0.0f, 0.157f, 0.426f, 0.425f,
		0.18f, -0.0f, 0.168f, 0.426f, 0.426f, 0.18f, -0.0f, 0.179f, 0.427f, 0.427f, 0.181f, -0.0f, 0.189f, 0.427f, 0.427f, 0.182f, -0.0f, 0.199f, 0.427f, 0.427f,
		0.183f, -0.0f, 0.208f, 0.427f, 0.428f, 0.185f, -0.0f, 0.218f, 0.428f, 0.427f, 0.187f, -0.0f, 0.226f, 0.428f, 0.427f, 0.19f, -0.0f, 0.235f, 0.429f, 0.427f,
		0.192f, -0.0f, 0.243f, 0.43f, 0.428f, 0.196f, -0.0f, 0.252f, 0.431f, 0.431f, 0.199f, -0.0f, 0.26f, 0.431f, 0.432f, 0.203f, -0.0f, 0.268f, 0.432f, 0.433f,
		0.207f, -0.0f, 0.276f, 0.433f, 0.433f, 0.212f, -0.0f, 0.283f, 0.434f, 0.434f, 0.216f, -0.0f, 0.291f, 0.434f, 0.435f, 0.221f, -0.0f, 0.298f, 0.435f, 0.436f,
		0.227f, -0.0f, 0.305f, 0.435f, 0.435f, 0.232f, -0.0f, 0.311f, 0.436f, 0.436f, 0.238f, -0.0f, 0.317f, 0.436f, 0.436f, 0.243f, -0.0f, 0.323f, 0.436f, 0.436f,
		0.249f, -0.0f, 0.329f, 0.437f, 0.436f, 0.255f, -0.0f, 0.334f, 0.438f, 0.437f, 0.262f, -0.0f, 0.339f, 0.44f, 0.437f, 0.268f, -0.0f, 0.344f, 0.442f, 0.441f,
		0.274f, 0.0f, 0.348f, 0.444f, 0.446f, 0.281f, 0.0f, 0.352f, 0.445f, 0.447f, 0.287f, 0.0f, 0.355f, 0.446f, 0.447f, 0.293f, 0.0f, 0.358f, 0.446f, 0.447f,
		0.299f, 0.0f, 0.361f, 0.447f, 0.447f, 0.306f, 0.0f, 0.363f, 0.447f, 0.448f, 0.312f, 0.0f, 0.366f, 0.447f, 0.448f, 0.318f, 0.0f, 0.368f, 0.448f, 0.448f,
		0.325f, 0.0f, 0.369f, 0.448f, 0.448f, 0.331f, 0.0f, 0.371f, 0.448f, 0.448f, 0.338f, 0.0f, 0.372f, 0.448f, 0.448f, 0.345f, 0.0f, 0.372f, 0.448f, 0.448f,
		0.352f, 0.0f, 0.372f, 0.448f, 0.448f, 0.359f, 0.0f, 0.372f, 0.448f, 0.449f, 0.366f, 0.0f, 0.371f, 0.448f, 0.448f, 0.373f, 0.0f, 0.37f, 0.448f, 0.449f,
		0.38f, 0.0f, 0.369f, 0.449f, 0.449f, 0.387f, 0.0f, 0.367f, 0.449f, 0.449f, 0.393f, 0.0f, 0.365f, 0.449f, 0.449f, 0.4f, 0.0f, 0.363f, 0.449f, 0.45f,
		0.406f, 0.0f, 0.36f, 0.45f, 0.45f, 0.412f, -0.0f, 0.357f, 0.45f, 0.45f, 0.418f, -0.0f, 0.354f, 0.45f, 0.451f, 0.424f, -0.0f, 0.351f, 0.45f, 0.451f,
		0.43f, -0.0f, 0.347f, 0.45f, 0.451f, 0.436f, -0.0f, 0.343f, 0.45f, 0.451f, 0.443f, -0.0f, 0.339f, 0.45f, 0.45f, 0.449f, -0.0f, 0.334f, 0.45f, 0.451f,
		0.455f, -0.0f, 0.329f, 0.451f, 0.451f, 0.46f, -0.0f, 0.323f, 0.451f, 0.451f, 0.466f, -0.0f, 0.318f, 0.451f, 0.451f, 0.472f, -0.0f, 0.311f, 0.452f, 0.452f,
		0.477f, -0.0f, 0.305f, 0.452f, 0.453f, 0.482f, -0.0f, 0.298f, 0.452f, 0.453f, 0.487f, -0.0f, 0.291f, 0.453f, 0.453f, 0.492f, -0.0f, 0.284f, 0.453f, 0.453f,
		0.496f, -0.0f, 0.277f, 0.453f, 0.453f, 0.5f, -0.0f, 0.269f, 0.453f, 0.454f, 0.504f, -0.0f, 0.261f, 0.453f, 0.454f, 0.508f, -0.0f, 0.252f, 0.454f, 0.454f,
		0.511f, -0.0f, 0.244f, 0.454f, 0.454f, 0.514f, -0.0f, 0.235f, 0.454f, 0.455f, 0.517f, -0.0f, 0.225f, 0.454f, 0.455f, 0.519f, -0.0f, 0.216f, 0.454f, 0.455f,
		0.521f, -0.0f, 0.205f, 0.455f, 0.455f, 0.523f, -0.0f, 0.194f, 0.455f, 0.455f, 0.524f, -0.0f, 0.182f, 0.455f, 0.455f, 0.524f, -0.0f, 0.169f, 0.455f, 0.456f,
		0.524f, -0.0f, 0.157f, 0.455f, 0.456f, 0.523f, -0.0f, 0.145f, 0.455f, 0.456f, 0.522f, -0.0f, 0.133f, 0.455f, 0.456f, 0.52f, -0.0f, 0.122f, 0.456f, 0.456f,
		0.518f, -0.0f, 0.11f, 0.456f, 0.456f, 0.515f, -0.0f, 0.1f, 0.456f, 0.456f, 0.513f, -0.0f, 0.09f, 0.456f, 0.457f, 0.509f, -0.0f, 0.081f, 0.456f, 0.457f,
		0.506f, -0.0f, 0.072f, 0.457f, 0.457f, 0.502f, -0.0f, 0.064f, 0.457f, 0.457f, 0.498f, -0.0f, 0.056f, 0.457f, 0.457f, 0.494f, -0.0f, 0.049f, 0.457f, 0.457f,
		0.49f, -0.0f, 0.041f, 0.458f, 0.457f, 0.485f, -0.0f, 0.034f, 0.458f, 0.457f, 0.48f, -0.0f, 0.028f, 0.458f, 0.458f, 0.475f, -0.0f, 0.022f, 0.458f, 0.458f,
		0.47f, -0.0f, 0.016f, 0.458f, 0.458f, 0.465f, -0.0f, 0.011f, 0.459f, 0.458f, 0.46f, -0.0f, 0.006f, 0.459f, 0.458f, 0.454f, -0.0f, 0.001f, 0.46f, 0.459f,
		0.449f, 0.0f, -0.003f, 0.464f, 0.463f, 0.443f, 0.0f, -0.007f, 0.467f, 0.468f, 0.438f, 0.0f, -0.011f, 0.469f, 0.469f, 0.432f, 0.0f, -0.015f, 0.471f, 0.47f,
		0.426f, 0.0f, -0.018f, 0.477f, 0.478f, 0.42f, 0.0f, -0.021f, 0.478f, 0.478f, 0.414f, 0.0f, -0.024f, 0.478f, 0.478f, 0.408f, 0.0f, -0.027f, 0.479f, 0.479f,
		0.402f, 0.0f, -0.029f, 0.48f, 0.48f, 0.395f, 0.0f, -0.031f, 0.48f, 0.48f, 0.389f, 0.0f, -0.033f, 0.482f, 0.482f, 0.382f, 0.0f, -0.035f, 0.482f, 0.482f,
		0.376f, 0.0f, -0.036f, 0.482f, 0.482f, 0.369f, 0.0f, -0.037f, 0.48f, 0.482f, 0.364f, 0.0f, -0.037f, 0.457f, 0.485f, 0.356f, 0.0f, -0.038f, 0.32f, 0.32f,
	};
	gpencil_add_points(gps, data20, 136);

	gps = gpencil_add_stroke(frameLines, palette, color_Black, 353, "Black", 3);
	float data21[353 * 5] = { -0.382f, 0.0f, 0.397f, 0.0f, 1.0f, -0.386f, 0.0f, 0.394f, 0.0f, 1.0f, -0.389f, 0.0f, 0.392f, 0.0f, 1.0f, -0.392f, 0.0f, 0.39f, 0.0f, 1.0f,
		-0.395f, 0.0f, 0.388f, 0.0f, 1.0f, -0.399f, 0.0f, 0.385f, 0.0f, 1.0f, -0.402f, 0.0f, 0.383f, 0.0f, 1.0f, -0.405f, 0.0f, 0.381f, 0.0f, 1.0f,
		-0.408f, 0.0f, 0.379f, 0.0f, 1.0f, -0.411f, 0.0f, 0.377f, 0.0f, 1.0f, -0.414f, 0.0f, 0.375f, 0.0f, 1.0f, -0.417f, 0.0f, 0.372f, 0.0f, 1.0f,
		-0.42f, 0.0f, 0.37f, 0.0f, 1.0f, -0.423f, 0.0f, 0.368f, 0.0f, 1.0f, -0.425f, 0.0f, 0.366f, 0.0f, 1.0f, -0.428f, 0.0f, 0.364f, 0.0f, 1.0f,
		-0.431f, 0.0f, 0.362f, 0.0f, 1.0f, -0.433f, 0.0f, 0.359f, 0.0f, 1.0f, -0.436f, 0.0f, 0.357f, 0.0f, 1.0f, -0.438f, 0.0f, 0.355f, 0.0f, 1.0f,
		-0.441f, 0.0f, 0.353f, 0.0f, 1.0f, -0.443f, 0.0f, 0.351f, 0.0f, 1.0f, -0.445f, 0.0f, 0.349f, 0.0f, 1.0f, -0.447f, 0.0f, 0.346f, 0.0f, 1.0f,
		-0.45f, 0.0f, 0.344f, 0.0f, 1.0f, -0.452f, 0.0f, 0.342f, 0.0f, 1.0f, -0.454f, 0.0f, 0.34f, 0.0f, 1.0f, -0.456f, 0.0f, 0.337f, 0.0f, 1.0f,
		-0.458f, 0.0f, 0.335f, 0.0f, 1.0f, -0.46f, 0.0f, 0.333f, 0.0f, 1.0f, -0.462f, 0.0f, 0.33f, 0.0f, 1.0f, -0.464f, 0.0f, 0.328f, 0.0f, 1.0f,
		-0.466f, 0.0f, 0.326f, 0.0f, 1.0f, -0.468f, 0.0f, 0.323f, 0.0f, 1.0f, -0.47f, 0.0f, 0.321f, 0.0f, 1.0f, -0.472f, 0.0f, 0.319f, 0.0f, 1.0f,
		-0.474f, 0.0f, 0.316f, 0.0f, 1.0f, -0.475f, 0.0f, 0.314f, 0.0f, 1.0f, -0.477f, 0.0f, 0.311f, 0.0f, 1.0f, -0.479f, 0.0f, 0.309f, 0.0f, 1.0f,
		-0.481f, 0.0f, 0.307f, 0.0f, 1.0f, -0.482f, 0.0f, 0.304f, 0.0f, 1.0f, -0.484f, 0.0f, 0.302f, 0.0f, 1.0f, -0.486f, 0.0f, 0.299f, 0.0f, 1.0f,
		-0.487f, 0.0f, 0.297f, 0.0f, 1.0f, -0.489f, 0.0f, 0.294f, 0.0f, 1.0f, -0.49f, 0.0f, 0.292f, 0.0f, 1.0f, -0.492f, 0.0f, 0.289f, 0.0f, 1.0f,
		-0.494f, 0.0f, 0.286f, 0.0f, 1.0f, -0.495f, 0.0f, 0.284f, 0.0f, 1.0f, -0.497f, 0.0f, 0.281f, 0.0f, 1.0f, -0.498f, 0.0f, 0.279f, 0.001f, 1.0f,
		-0.499f, 0.0f, 0.276f, 0.001f, 1.0f, -0.501f, 0.0f, 0.273f, 0.002f, 1.0f, -0.502f, 0.0f, 0.271f, 0.003f, 1.0f, -0.504f, 0.0f, 0.268f, 0.005f, 1.0f,
		-0.505f, 0.0f, 0.265f, 0.008f, 1.0f, -0.506f, 0.0f, 0.262f, 0.011f, 1.0f, -0.508f, 0.0f, 0.259f, 0.016f, 1.0f, -0.509f, 0.0f, 0.256f, 0.021f, 1.0f,
		-0.51f, 0.0f, 0.254f, 0.027f, 1.0f, -0.512f, 0.0f, 0.251f, 0.035f, 1.0f, -0.513f, 0.0f, 0.248f, 0.043f, 1.0f, -0.514f, 0.0f, 0.245f, 0.053f, 1.0f,
		-0.515f, 0.0f, 0.242f, 0.064f, 1.0f, -0.516f, 0.0f, 0.239f, 0.076f, 1.0f, -0.517f, 0.0f, 0.235f, 0.09f, 1.0f, -0.519f, 0.0f, 0.232f, 0.105f, 1.0f,
		-0.52f, 0.0f, 0.229f, 0.122f, 1.0f, -0.521f, 0.0f, 0.226f, 0.14f, 1.0f, -0.521f, 0.0f, 0.222f, 0.159f, 1.0f, -0.522f, 0.0f, 0.219f, 0.179f, 1.0f,
		-0.523f, 0.0f, 0.216f, 0.2f, 1.0f, -0.524f, 0.0f, 0.212f, 0.221f, 1.0f, -0.525f, 0.0f, 0.209f, 0.243f, 1.0f, -0.526f, 0.0f, 0.205f, 0.265f, 1.0f,
		-0.526f, 0.0f, 0.202f, 0.286f, 1.0f, -0.527f, 0.0f, 0.198f, 0.306f, 1.0f, -0.527f, 0.0f, 0.195f, 0.326f, 1.0f, -0.528f, 0.0f, 0.191f, 0.345f, 1.0f,
		-0.528f, 0.0f, 0.187f, 0.363f, 1.0f, -0.529f, 0.0f, 0.184f, 0.38f, 1.0f, -0.529f, 0.0f, 0.18f, 0.395f, 1.0f, -0.529f, 0.0f, 0.176f, 0.41f, 1.0f,
		-0.53f, 0.0f, 0.173f, 0.424f, 1.0f, -0.53f, 0.0f, 0.169f, 0.438f, 1.0f, -0.53f, 0.0f, 0.165f, 0.452f, 1.0f, -0.53f, 0.0f, 0.161f, 0.465f, 1.0f,
		-0.53f, 0.0f, 0.157f, 0.478f, 1.0f, -0.53f, 0.0f, 0.154f, 0.492f, 1.0f, -0.53f, 0.0f, 0.15f, 0.505f, 1.0f, -0.53f, 0.0f, 0.146f, 0.517f, 1.0f,
		-0.53f, 0.0f, 0.142f, 0.53f, 1.0f, -0.529f, 0.0f, 0.138f, 0.542f, 1.0f, -0.529f, 0.0f, 0.134f, 0.553f, 1.0f, -0.528f, 0.0f, 0.13f, 0.564f, 1.0f,
		-0.528f, 0.0f, 0.127f, 0.574f, 1.0f, -0.527f, 0.0f, 0.123f, 0.583f, 1.0f, -0.527f, 0.0f, 0.119f, 0.592f, 1.0f, -0.526f, 0.0f, 0.115f, 0.6f, 1.0f,
		-0.526f, 0.0f, 0.111f, 0.608f, 1.0f, -0.525f, 0.0f, 0.108f, 0.615f, 1.0f, -0.524f, 0.0f, 0.104f, 0.622f, 1.0f, -0.523f, 0.0f, 0.1f, 0.628f, 1.0f,
		-0.522f, 0.0f, 0.097f, 0.635f, 1.0f, -0.521f, 0.0f, 0.093f, 0.641f, 1.0f, -0.52f, 0.0f, 0.089f, 0.647f, 1.0f, -0.519f, 0.0f, 0.086f, 0.653f, 1.0f,
		-0.518f, 0.0f, 0.082f, 0.659f, 1.0f, -0.517f, 0.0f, 0.079f, 0.664f, 1.0f, -0.515f, 0.0f, 0.075f, 0.67f, 1.0f, -0.514f, 0.0f, 0.072f, 0.675f, 1.0f,
		-0.513f, 0.0f, 0.069f, 0.68f, 1.0f, -0.511f, 0.0f, 0.065f, 0.685f, 1.0f, -0.51f, 0.0f, 0.062f, 0.69f, 1.0f, -0.509f, 0.0f, 0.059f, 0.695f, 1.0f,
		-0.507f, 0.0f, 0.056f, 0.7f, 1.0f, -0.505f, 0.0f, 0.053f, 0.704f, 1.0f, -0.504f, 0.0f, 0.049f, 0.709f, 1.0f, -0.502f, 0.0f, 0.046f, 0.714f, 1.0f,
		-0.5f, 0.0f, 0.043f, 0.719f, 1.0f, -0.499f, 0.0f, 0.04f, 0.724f, 1.0f, -0.497f, 0.0f, 0.038f, 0.73f, 1.0f, -0.495f, 0.0f, 0.035f, 0.735f, 1.0f,
		-0.493f, 0.0f, 0.032f, 0.741f, 1.0f, -0.491f, 0.0f, 0.029f, 0.748f, 1.0f, -0.489f, 0.0f, 0.026f, 0.754f, 1.0f, -0.488f, -0.0f, 0.024f, 0.76f, 1.0f,
		-0.486f, -0.0f, 0.022f, 0.767f, 1.0f, -0.485f, -0.0f, 0.019f, 0.773f, 1.0f, -0.483f, -0.0f, 0.017f, 0.779f, 1.0f, -0.482f, -0.0f, 0.015f, 0.785f, 1.0f,
		-0.48f, -0.0f, 0.013f, 0.79f, 1.0f, -0.478f, -0.0f, 0.01f, 0.795f, 1.0f, -0.476f, -0.0f, 0.008f, 0.8f, 1.0f, -0.474f, -0.0f, 0.006f, 0.804f, 1.0f,
		-0.472f, -0.0f, 0.004f, 0.808f, 1.0f, -0.47f, -0.0f, 0.002f, 0.811f, 1.0f, -0.468f, -0.0f, -0.0f, 0.814f, 1.0f, -0.466f, -0.0f, -0.002f, 0.816f, 1.0f,
		-0.464f, -0.0f, -0.004f, 0.818f, 1.0f, -0.461f, -0.0f, -0.006f, 0.82f, 1.0f, -0.459f, -0.0f, -0.008f, 0.822f, 1.0f, -0.456f, -0.0f, -0.01f, 0.823f, 1.0f,
		-0.454f, -0.0f, -0.012f, 0.825f, 1.0f, -0.451f, -0.0f, -0.014f, 0.826f, 1.0f, -0.448f, -0.0f, -0.016f, 0.827f, 1.0f, -0.445f, -0.0f, -0.018f, 0.828f, 1.0f,
		-0.442f, -0.0f, -0.02f, 0.829f, 1.0f, -0.439f, -0.0f, -0.022f, 0.829f, 1.0f, -0.436f, -0.0f, -0.024f, 0.83f, 1.0f, -0.433f, -0.0f, -0.026f, 0.83f, 1.0f,
		-0.43f, -0.0f, -0.027f, 0.83f, 1.0f, -0.426f, -0.0f, -0.029f, 0.83f, 1.0f, -0.423f, 0.0f, -0.031f, 0.83f, 1.0f, -0.42f, 0.0f, -0.032f, 0.83f, 1.0f,
		-0.417f, 0.0f, -0.033f, 0.831f, 1.0f, -0.414f, 0.0f, -0.034f, 0.831f, 1.0f, -0.411f, 0.0f, -0.035f, 0.831f, 1.0f, -0.408f, 0.0f, -0.037f, 0.831f, 1.0f,
		-0.405f, 0.0f, -0.038f, 0.831f, 1.0f, -0.402f, 0.0f, -0.039f, 0.831f, 1.0f, -0.399f, 0.0f, -0.039f, 0.831f, 1.0f, -0.396f, 0.0f, -0.04f, 0.832f, 1.0f,
		-0.393f, 0.0f, -0.041f, 0.832f, 1.0f, -0.389f, 0.0f, -0.042f, 0.832f, 1.0f, -0.386f, 0.0f, -0.043f, 0.832f, 1.0f, -0.383f, 0.0f, -0.044f, 0.832f, 1.0f,
		-0.379f, 0.0f, -0.044f, 0.832f, 1.0f, -0.376f, 0.0f, -0.045f, 0.832f, 1.0f, -0.372f, 0.0f, -0.045f, 0.832f, 1.0f, -0.369f, 0.0f, -0.046f, 0.832f, 1.0f,
		-0.366f, 0.0f, -0.047f, 0.832f, 1.0f, -0.362f, 0.0f, -0.047f, 0.832f, 1.0f, -0.359f, 0.0f, -0.047f, 0.831f, 1.0f, -0.355f, 0.0f, -0.048f, 0.831f, 1.0f,
		-0.352f, 0.0f, -0.048f, 0.83f, 1.0f, -0.348f, 0.0f, -0.048f, 0.83f, 1.0f, -0.345f, 0.0f, -0.049f, 0.829f, 1.0f, -0.341f, 0.0f, -0.049f, 0.828f, 1.0f,
		-0.338f, 0.0f, -0.049f, 0.827f, 1.0f, -0.334f, 0.0f, -0.049f, 0.826f, 1.0f, -0.331f, 0.0f, -0.049f, 0.823f, 1.0f, -0.327f, 0.0f, -0.049f, 0.82f, 1.0f,
		-0.323f, 0.0f, -0.048f, 0.816f, 1.0f, -0.32f, 0.0f, -0.048f, 0.811f, 1.0f, -0.316f, 0.0f, -0.048f, 0.804f, 1.0f, -0.313f, 0.0f, -0.048f, 0.797f, 1.0f,
		-0.309f, 0.0f, -0.047f, 0.79f, 1.0f, -0.306f, 0.0f, -0.047f, 0.782f, 1.0f, -0.302f, 0.0f, -0.046f, 0.774f, 1.0f, -0.299f, 0.0f, -0.045f, 0.767f, 1.0f,
		-0.295f, 0.0f, -0.044f, 0.76f, 1.0f, -0.292f, 0.0f, -0.044f, 0.753f, 1.0f, -0.288f, 0.0f, -0.043f, 0.748f, 1.0f, -0.285f, 0.0f, -0.042f, 0.742f, 1.0f,
		-0.282f, 0.0f, -0.041f, 0.738f, 1.0f, -0.278f, 0.0f, -0.04f, 0.734f, 1.0f, -0.275f, 0.0f, -0.039f, 0.73f, 1.0f, -0.272f, 0.0f, -0.037f, 0.726f, 1.0f,
		-0.269f, 0.0f, -0.036f, 0.723f, 1.0f, -0.266f, 0.0f, -0.035f, 0.72f, 1.0f, -0.263f, 0.0f, -0.034f, 0.717f, 1.0f, -0.26f, 0.0f, -0.032f, 0.713f, 1.0f,
		-0.257f, 0.0f, -0.031f, 0.71f, 1.0f, -0.255f, 0.0f, -0.029f, 0.706f, 1.0f, -0.252f, 0.0f, -0.028f, 0.702f, 1.0f, -0.249f, 0.0f, -0.026f, 0.698f, 1.0f,
		-0.247f, 0.0f, -0.025f, 0.693f, 1.0f, -0.244f, 0.0f, -0.023f, 0.688f, 1.0f, -0.242f, 0.0f, -0.021f, 0.684f, 1.0f, -0.239f, 0.0f, -0.02f, 0.679f, 1.0f,
		-0.237f, 0.0f, -0.018f, 0.675f, 1.0f, -0.234f, 0.0f, -0.016f, 0.671f, 1.0f, -0.232f, 0.0f, -0.014f, 0.667f, 1.0f, -0.23f, 0.0f, -0.013f, 0.663f, 1.0f,
		-0.228f, 0.0f, -0.011f, 0.66f, 1.0f, -0.225f, 0.0f, -0.009f, 0.657f, 1.0f, -0.223f, 0.0f, -0.007f, 0.654f, 1.0f, -0.221f, 0.0f, -0.005f, 0.651f, 1.0f,
		-0.219f, 0.0f, -0.003f, 0.649f, 1.0f, -0.217f, 0.0f, -0.001f, 0.645f, 1.0f, -0.215f, 0.0f, 0.002f, 0.642f, 1.0f, -0.213f, 0.0f, 0.004f, 0.639f, 1.0f,
		-0.211f, 0.0f, 0.006f, 0.635f, 1.0f, -0.209f, 0.0f, 0.008f, 0.631f, 1.0f, -0.207f, 0.0f, 0.011f, 0.627f, 1.0f, -0.206f, 0.0f, 0.013f, 0.623f, 1.0f,
		-0.204f, 0.0f, 0.016f, 0.619f, 1.0f, -0.202f, 0.0f, 0.018f, 0.615f, 1.0f, -0.2f, 0.0f, 0.021f, 0.61f, 1.0f, -0.199f, 0.0f, 0.023f, 0.606f, 1.0f,
		-0.197f, 0.0f, 0.026f, 0.602f, 1.0f, -0.195f, 0.0f, 0.029f, 0.598f, 1.0f, -0.194f, 0.0f, 0.032f, 0.595f, 1.0f, -0.192f, 0.0f, 0.034f, 0.592f, 1.0f,
		-0.191f, 0.0f, 0.037f, 0.589f, 1.0f, -0.19f, 0.0f, 0.04f, 0.587f, 1.0f, -0.188f, 0.0f, 0.043f, 0.585f, 1.0f, -0.187f, 0.0f, 0.046f, 0.584f, 1.0f,
		-0.186f, 0.0f, 0.05f, 0.583f, 1.0f, -0.185f, 0.0f, 0.053f, 0.582f, 1.0f, -0.183f, 0.0f, 0.056f, 0.581f, 1.0f, -0.182f, 0.0f, 0.059f, 0.581f, 1.0f,
		-0.181f, 0.0f, 0.062f, 0.581f, 1.0f, -0.18f, 0.0f, 0.066f, 0.581f, 1.0f, -0.179f, 0.0f, 0.069f, 0.58f, 1.0f, -0.178f, 0.0f, 0.072f, 0.58f, 1.0f,
		-0.177f, 0.0f, 0.076f, 0.58f, 1.0f, -0.177f, 0.0f, 0.079f, 0.58f, 1.0f, -0.176f, 0.0f, 0.083f, 0.58f, 1.0f, -0.175f, 0.0f, 0.086f, 0.58f, 1.0f,
		-0.174f, 0.0f, 0.09f, 0.58f, 1.0f, -0.174f, 0.0f, 0.093f, 0.58f, 1.0f, -0.173f, 0.0f, 0.097f, 0.58f, 1.0f, -0.172f, 0.0f, 0.1f, 0.58f, 1.0f,
		-0.172f, 0.0f, 0.104f, 0.58f, 1.0f, -0.171f, 0.0f, 0.108f, 0.579f, 1.0f, -0.171f, 0.0f, 0.111f, 0.579f, 1.0f, -0.17f, 0.0f, 0.115f, 0.578f, 1.0f,
		-0.17f, 0.0f, 0.119f, 0.578f, 1.0f, -0.17f, 0.0f, 0.122f, 0.577f, 1.0f, -0.169f, 0.0f, 0.126f, 0.577f, 1.0f, -0.169f, 0.0f, 0.13f, 0.576f, 1.0f,
		-0.169f, 0.0f, 0.134f, 0.576f, 1.0f, -0.169f, 0.0f, 0.137f, 0.575f, 1.0f, -0.169f, 0.0f, 0.141f, 0.575f, 1.0f, -0.169f, 0.0f, 0.145f, 0.574f, 1.0f,
		-0.169f, 0.0f, 0.149f, 0.572f, 1.0f, -0.169f, 0.0f, 0.153f, 0.571f, 1.0f, -0.169f, 0.0f, 0.157f, 0.569f, 1.0f, -0.169f, 0.0f, 0.16f, 0.566f, 1.0f,
		-0.169f, 0.0f, 0.164f, 0.562f, 1.0f, -0.17f, 0.0f, 0.168f, 0.558f, 1.0f, -0.17f, 0.0f, 0.172f, 0.553f, 1.0f, -0.17f, 0.0f, 0.176f, 0.547f, 1.0f,
		-0.171f, 0.0f, 0.18f, 0.539f, 1.0f, -0.171f, 0.0f, 0.183f, 0.531f, 1.0f, -0.172f, 0.0f, 0.187f, 0.522f, 1.0f, -0.172f, 0.0f, 0.191f, 0.513f, 1.0f,
		-0.173f, 0.0f, 0.194f, 0.503f, 1.0f, -0.173f, 0.0f, 0.198f, 0.493f, 1.0f, -0.174f, 0.0f, 0.202f, 0.483f, 1.0f, -0.175f, 0.0f, 0.205f, 0.473f, 1.0f,
		-0.176f, 0.0f, 0.209f, 0.464f, 1.0f, -0.177f, 0.0f, 0.212f, 0.455f, 1.0f, -0.178f, 0.0f, 0.215f, 0.446f, 1.0f, -0.178f, 0.0f, 0.219f, 0.438f, 1.0f,
		-0.179f, 0.0f, 0.222f, 0.428f, 1.0f, -0.18f, 0.0f, 0.226f, 0.418f, 1.0f, -0.182f, 0.0f, 0.229f, 0.407f, 1.0f, -0.183f, 0.0f, 0.232f, 0.394f, 1.0f,
		-0.184f, 0.0f, 0.236f, 0.38f, 1.0f, -0.185f, 0.0f, 0.239f, 0.364f, 1.0f, -0.186f, 0.0f, 0.242f, 0.348f, 1.0f, -0.187f, 0.0f, 0.245f, 0.33f, 1.0f,
		-0.188f, 0.0f, 0.249f, 0.311f, 1.0f, -0.19f, 0.0f, 0.252f, 0.293f, 1.0f, -0.191f, 0.0f, 0.255f, 0.275f, 1.0f, -0.192f, 0.0f, 0.258f, 0.258f, 1.0f,
		-0.194f, 0.0f, 0.261f, 0.242f, 1.0f, -0.195f, 0.0f, 0.264f, 0.228f, 1.0f, -0.196f, 0.0f, 0.267f, 0.214f, 1.0f, -0.198f, 0.0f, 0.27f, 0.202f, 1.0f,
		-0.199f, 0.0f, 0.273f, 0.191f, 1.0f, -0.201f, 0.0f, 0.276f, 0.181f, 1.0f, -0.202f, 0.0f, 0.279f, 0.171f, 1.0f, -0.204f, 0.0f, 0.282f, 0.162f, 1.0f,
		-0.205f, 0.0f, 0.285f, 0.152f, 1.0f, -0.206f, 0.0f, 0.287f, 0.143f, 1.0f, -0.208f, 0.0f, 0.29f, 0.134f, 1.0f, -0.21f, 0.0f, 0.293f, 0.126f, 1.0f,
		-0.211f, 0.0f, 0.295f, 0.117f, 1.0f, -0.213f, 0.0f, 0.298f, 0.109f, 1.0f, -0.214f, 0.0f, 0.301f, 0.101f, 1.0f, -0.216f, 0.0f, 0.303f, 0.094f, 1.0f,
		-0.217f, 0.0f, 0.306f, 0.087f, 1.0f, -0.219f, 0.0f, 0.308f, 0.081f, 1.0f, -0.221f, 0.0f, 0.311f, 0.076f, 1.0f, -0.223f, 0.0f, 0.313f, 0.071f, 1.0f,
		-0.224f, 0.0f, 0.316f, 0.067f, 1.0f, -0.226f, 0.0f, 0.318f, 0.065f, 1.0f, -0.228f, 0.0f, 0.321f, 0.062f, 1.0f, -0.23f, 0.0f, 0.323f, 0.061f, 1.0f,
		-0.232f, 0.0f, 0.326f, 0.06f, 1.0f, -0.233f, 0.0f, 0.328f, 0.06f, 1.0f, -0.235f, 0.0f, 0.331f, 0.061f, 1.0f, -0.237f, 0.0f, 0.334f, 0.061f, 1.0f,
		-0.239f, 0.0f, 0.336f, 0.062f, 1.0f, -0.241f, 0.0f, 0.339f, 0.063f, 1.0f, -0.243f, 0.0f, 0.341f, 0.064f, 1.0f, -0.245f, 0.0f, 0.344f, 0.065f, 1.0f,
		-0.248f, 0.0f, 0.346f, 0.065f, 1.0f, -0.25f, 0.0f, 0.349f, 0.065f, 1.0f, -0.252f, 0.0f, 0.351f, 0.064f, 1.0f, -0.254f, 0.0f, 0.354f, 0.062f, 1.0f,
		-0.256f, 0.0f, 0.356f, 0.06f, 1.0f, -0.258f, 0.0f, 0.359f, 0.058f, 1.0f, -0.261f, 0.0f, 0.361f, 0.055f, 1.0f, -0.263f, 0.0f, 0.364f, 0.051f, 1.0f,
		-0.265f, 0.0f, 0.366f, 0.046f, 1.0f, -0.267f, 0.0f, 0.368f, 0.04f, 1.0f, -0.269f, 0.0f, 0.37f, 0.034f, 1.0f, -0.272f, 0.0f, 0.373f, 0.027f, 1.0f,
		-0.274f, 0.0f, 0.375f, 0.019f, 1.0f, -0.276f, 0.0f, 0.377f, 0.012f, 1.0f, -0.278f, 0.0f, 0.379f, 0.007f, 1.0f, -0.28f, 0.0f, 0.381f, 0.003f, 1.0f,
		-0.282f, 0.0f, 0.383f, 0.001f, 1.0f, -0.284f, 0.0f, 0.385f, 0.0f, 1.0f, -0.286f, 0.0f, 0.387f, 0.0f, 1.0f, -0.287f, 0.0f, 0.388f, 0.0f, 1.0f,
		-0.289f, 0.0f, 0.39f, 0.0f, 1.0f, };
	gpencil_add_points(gps, data21, 353);

	gps = gpencil_add_stroke(frameLines, palette, color_Black, 309, "Black", 3);
	float data22[309 * 5] = { 0.294f, 0.0f, 0.372f, 0.0f, 1.0f, 0.291f, 0.0f, 0.37f, 0.001f, 1.0f, 0.289f, 0.0f, 0.368f, 0.002f, 1.0f, 0.286f, 0.0f, 0.366f, 0.003f, 1.0f,
		0.284f, 0.0f, 0.364f, 0.006f, 1.0f, 0.282f, 0.0f, 0.362f, 0.01f, 1.0f, 0.279f, 0.0f, 0.36f, 0.015f, 1.0f, 0.277f, 0.0f, 0.358f, 0.022f, 1.0f,
		0.274f, 0.0f, 0.356f, 0.03f, 1.0f, 0.272f, 0.0f, 0.353f, 0.04f, 1.0f, 0.269f, 0.0f, 0.351f, 0.051f, 1.0f, 0.267f, 0.0f, 0.349f, 0.062f, 1.0f,
		0.265f, 0.0f, 0.347f, 0.074f, 1.0f, 0.262f, 0.0f, 0.344f, 0.086f, 1.0f, 0.26f, 0.0f, 0.342f, 0.097f, 1.0f, 0.258f, 0.0f, 0.34f, 0.108f, 1.0f,
		0.256f, 0.0f, 0.337f, 0.119f, 1.0f, 0.253f, 0.0f, 0.335f, 0.128f, 1.0f, 0.251f, 0.0f, 0.333f, 0.137f, 1.0f, 0.249f, 0.0f, 0.33f, 0.145f, 1.0f,
		0.247f, 0.0f, 0.328f, 0.153f, 1.0f, 0.246f, 0.0f, 0.325f, 0.161f, 1.0f, 0.244f, 0.0f, 0.323f, 0.168f, 1.0f, 0.242f, 0.0f, 0.321f, 0.176f, 1.0f,
		0.24f, 0.0f, 0.318f, 0.183f, 1.0f, 0.239f, 0.0f, 0.316f, 0.191f, 1.0f, 0.237f, 0.0f, 0.314f, 0.198f, 1.0f, 0.235f, 0.0f, 0.311f, 0.206f, 1.0f,
		0.233f, 0.0f, 0.309f, 0.214f, 1.0f, 0.231f, 0.0f, 0.306f, 0.223f, 1.0f, 0.23f, 0.0f, 0.304f, 0.231f, 1.0f, 0.228f, 0.0f, 0.301f, 0.24f, 1.0f,
		0.226f, 0.0f, 0.299f, 0.248f, 1.0f, 0.224f, 0.0f, 0.296f, 0.256f, 1.0f, 0.223f, 0.0f, 0.294f, 0.264f, 1.0f, 0.221f, 0.0f, 0.291f, 0.272f, 1.0f,
		0.219f, 0.0f, 0.288f, 0.28f, 1.0f, 0.218f, 0.0f, 0.286f, 0.287f, 1.0f, 0.216f, 0.0f, 0.283f, 0.294f, 1.0f, 0.214f, 0.0f, 0.281f, 0.301f, 1.0f,
		0.213f, 0.0f, 0.278f, 0.307f, 1.0f, 0.211f, 0.0f, 0.275f, 0.314f, 1.0f, 0.21f, 0.0f, 0.273f, 0.32f, 1.0f, 0.208f, 0.0f, 0.27f, 0.327f, 1.0f,
		0.206f, 0.0f, 0.267f, 0.333f, 1.0f, 0.205f, 0.0f, 0.265f, 0.339f, 1.0f, 0.204f, 0.0f, 0.262f, 0.345f, 1.0f, 0.202f, 0.0f, 0.259f, 0.351f, 1.0f,
		0.201f, 0.0f, 0.256f, 0.357f, 1.0f, 0.199f, 0.0f, 0.253f, 0.362f, 1.0f, 0.198f, 0.0f, 0.25f, 0.368f, 1.0f, 0.197f, 0.0f, 0.247f, 0.373f, 1.0f,
		0.195f, 0.0f, 0.244f, 0.379f, 1.0f, 0.194f, 0.0f, 0.241f, 0.383f, 1.0f, 0.193f, 0.0f, 0.238f, 0.388f, 1.0f, 0.192f, 0.0f, 0.235f, 0.392f, 1.0f,
		0.191f, 0.0f, 0.232f, 0.396f, 1.0f, 0.19f, 0.0f, 0.229f, 0.399f, 1.0f, 0.189f, 0.0f, 0.226f, 0.402f, 1.0f, 0.188f, 0.0f, 0.222f, 0.405f, 1.0f,
		0.187f, 0.0f, 0.219f, 0.407f, 1.0f, 0.186f, 0.0f, 0.216f, 0.409f, 1.0f, 0.185f, 0.0f, 0.213f, 0.411f, 1.0f, 0.184f, 0.0f, 0.209f, 0.412f, 1.0f,
		0.183f, 0.0f, 0.206f, 0.413f, 1.0f, 0.183f, 0.0f, 0.203f, 0.414f, 1.0f, 0.182f, 0.0f, 0.199f, 0.415f, 1.0f, 0.181f, 0.0f, 0.196f, 0.416f, 1.0f,
		0.181f, 0.0f, 0.193f, 0.417f, 1.0f, 0.18f, 0.0f, 0.189f, 0.417f, 1.0f, 0.18f, 0.0f, 0.186f, 0.418f, 1.0f, 0.179f, 0.0f, 0.182f, 0.419f, 1.0f,
		0.179f, 0.0f, 0.179f, 0.421f, 1.0f, 0.179f, 0.0f, 0.176f, 0.422f, 1.0f, 0.178f, 0.0f, 0.172f, 0.423f, 1.0f, 0.178f, 0.0f, 0.169f, 0.425f, 1.0f,
		0.178f, 0.0f, 0.165f, 0.427f, 1.0f, 0.178f, 0.0f, 0.162f, 0.429f, 1.0f, 0.178f, 0.0f, 0.158f, 0.431f, 1.0f, 0.178f, 0.0f, 0.155f, 0.434f, 1.0f,
		0.178f, 0.0f, 0.152f, 0.436f, 1.0f, 0.178f, 0.0f, 0.148f, 0.439f, 1.0f, 0.178f, 0.0f, 0.145f, 0.442f, 1.0f, 0.178f, 0.0f, 0.141f, 0.446f, 1.0f,
		0.178f, 0.0f, 0.138f, 0.449f, 1.0f, 0.178f, 0.0f, 0.134f, 0.453f, 1.0f, 0.178f, 0.0f, 0.131f, 0.458f, 1.0f, 0.179f, 0.0f, 0.127f, 0.462f, 1.0f,
		0.179f, 0.0f, 0.124f, 0.467f, 1.0f, 0.179f, 0.0f, 0.12f, 0.472f, 1.0f, 0.18f, 0.0f, 0.117f, 0.478f, 1.0f, 0.18f, 0.0f, 0.113f, 0.483f, 1.0f,
		0.181f, 0.0f, 0.11f, 0.489f, 1.0f, 0.182f, 0.0f, 0.106f, 0.494f, 1.0f, 0.182f, 0.0f, 0.103f, 0.5f, 1.0f, 0.183f, 0.0f, 0.099f, 0.505f, 1.0f,
		0.184f, 0.0f, 0.096f, 0.511f, 1.0f, 0.185f, 0.0f, 0.092f, 0.516f, 1.0f, 0.185f, 0.0f, 0.089f, 0.521f, 1.0f, 0.186f, 0.0f, 0.086f, 0.525f, 1.0f,
		0.187f, 0.0f, 0.082f, 0.53f, 1.0f, 0.188f, 0.0f, 0.079f, 0.534f, 1.0f, 0.189f, 0.0f, 0.076f, 0.537f, 1.0f, 0.191f, 0.0f, 0.073f, 0.541f, 1.0f,
		0.192f, 0.0f, 0.069f, 0.544f, 1.0f, 0.193f, 0.0f, 0.066f, 0.547f, 1.0f, 0.194f, 0.0f, 0.063f, 0.55f, 1.0f, 0.196f, 0.0f, 0.061f, 0.553f, 1.0f,
		0.197f, 0.0f, 0.058f, 0.556f, 1.0f, 0.198f, 0.0f, 0.055f, 0.559f, 1.0f, 0.2f, 0.0f, 0.052f, 0.562f, 1.0f, 0.201f, 0.0f, 0.049f, 0.564f, 1.0f,
		0.203f, 0.0f, 0.047f, 0.566f, 1.0f, 0.205f, 0.0f, 0.044f, 0.569f, 1.0f, 0.206f, 0.0f, 0.042f, 0.571f, 1.0f, 0.208f, 0.0f, 0.039f, 0.573f, 1.0f,
		0.21f, 0.0f, 0.037f, 0.575f, 1.0f, 0.212f, 0.0f, 0.035f, 0.576f, 1.0f, 0.214f, 0.0f, 0.032f, 0.578f, 1.0f, 0.215f, 0.0f, 0.03f, 0.579f, 1.0f,
		0.217f, 0.0f, 0.028f, 0.581f, 1.0f, 0.22f, 0.0f, 0.025f, 0.582f, 1.0f, 0.222f, 0.0f, 0.023f, 0.583f, 1.0f, 0.224f, 0.0f, 0.021f, 0.585f, 1.0f,
		0.226f, 0.0f, 0.019f, 0.587f, 1.0f, 0.228f, 0.0f, 0.016f, 0.589f, 1.0f, 0.231f, 0.0f, 0.014f, 0.592f, 1.0f, 0.233f, 0.0f, 0.012f, 0.596f, 1.0f,
		0.236f, 0.0f, 0.01f, 0.599f, 1.0f, 0.238f, 0.0f, 0.008f, 0.604f, 1.0f, 0.241f, 0.0f, 0.006f, 0.608f, 1.0f, 0.243f, 0.0f, 0.004f, 0.612f, 1.0f,
		0.246f, 0.0f, 0.002f, 0.615f, 1.0f, 0.249f, 0.0f, 0.0f, 0.619f, 1.0f, 0.251f, 0.0f, -0.002f, 0.622f, 1.0f, 0.254f, 0.0f, -0.003f, 0.624f, 1.0f,
		0.257f, 0.0f, -0.005f, 0.626f, 1.0f, 0.26f, 0.0f, -0.007f, 0.628f, 1.0f, 0.263f, 0.0f, -0.008f, 0.63f, 1.0f, 0.266f, 0.0f, -0.01f, 0.632f, 1.0f,
		0.269f, 0.0f, -0.011f, 0.634f, 1.0f, 0.272f, 0.0f, -0.013f, 0.636f, 1.0f, 0.275f, 0.0f, -0.014f, 0.638f, 1.0f, 0.278f, 0.0f, -0.015f, 0.64f, 1.0f,
		0.281f, 0.0f, -0.017f, 0.642f, 1.0f, 0.284f, 0.0f, -0.018f, 0.644f, 1.0f, 0.288f, 0.0f, -0.019f, 0.647f, 1.0f, 0.291f, 0.0f, -0.02f, 0.649f, 1.0f,
		0.294f, 0.0f, -0.021f, 0.651f, 1.0f, 0.297f, 0.0f, -0.022f, 0.653f, 1.0f, 0.301f, 0.0f, -0.023f, 0.656f, 1.0f, 0.304f, 0.0f, -0.024f, 0.658f, 1.0f,
		0.307f, 0.0f, -0.025f, 0.659f, 1.0f, 0.31f, 0.0f, -0.026f, 0.661f, 1.0f, 0.314f, 0.0f, -0.027f, 0.662f, 1.0f, 0.317f, 0.0f, -0.027f, 0.664f, 1.0f,
		0.32f, 0.0f, -0.028f, 0.665f, 1.0f, 0.324f, 0.0f, -0.028f, 0.665f, 1.0f, 0.327f, 0.0f, -0.029f, 0.666f, 1.0f, 0.33f, 0.0f, -0.029f, 0.666f, 1.0f,
		0.334f, 0.0f, -0.029f, 0.667f, 1.0f, 0.337f, 0.0f, -0.03f, 0.667f, 1.0f, 0.341f, 0.0f, -0.03f, 0.668f, 1.0f, 0.344f, 0.0f, -0.03f, 0.668f, 1.0f,
		0.348f, 0.0f, -0.03f, 0.668f, 1.0f, 0.351f, 0.0f, -0.03f, 0.668f, 1.0f, 0.354f, 0.0f, -0.03f, 0.668f, 1.0f, 0.358f, 0.0f, -0.029f, 0.668f, 1.0f,
		0.361f, 0.0f, -0.029f, 0.668f, 1.0f, 0.365f, 0.0f, -0.029f, 0.668f, 1.0f, 0.368f, 0.0f, -0.028f, 0.668f, 1.0f, 0.372f, 0.0f, -0.028f, 0.668f, 1.0f,
		0.375f, 0.0f, -0.027f, 0.668f, 1.0f, 0.378f, 0.0f, -0.027f, 0.668f, 1.0f, 0.382f, 0.0f, -0.026f, 0.667f, 1.0f, 0.385f, 0.0f, -0.025f, 0.667f, 1.0f,
		0.388f, 0.0f, -0.025f, 0.666f, 1.0f, 0.392f, 0.0f, -0.024f, 0.666f, 1.0f, 0.395f, 0.0f, -0.023f, 0.665f, 1.0f, 0.398f, 0.0f, -0.022f, 0.664f, 1.0f,
		0.401f, 0.0f, -0.021f, 0.664f, 1.0f, 0.405f, 0.0f, -0.02f, 0.663f, 1.0f, 0.408f, 0.0f, -0.019f, 0.663f, 1.0f, 0.411f, 0.0f, -0.018f, 0.662f, 1.0f,
		0.414f, 0.0f, -0.017f, 0.662f, 1.0f, 0.417f, 0.0f, -0.016f, 0.662f, 1.0f, 0.42f, 0.0f, -0.015f, 0.662f, 1.0f, 0.423f, 0.0f, -0.014f, 0.661f, 1.0f,
		0.426f, 0.0f, -0.012f, 0.661f, 1.0f, 0.429f, 0.0f, -0.011f, 0.661f, 1.0f, 0.432f, 0.0f, -0.01f, 0.661f, 1.0f, 0.434f, 0.0f, -0.009f, 0.66f, 1.0f,
		0.437f, 0.0f, -0.007f, 0.66f, 1.0f, 0.44f, 0.0f, -0.006f, 0.659f, 1.0f, 0.442f, 0.0f, -0.005f, 0.659f, 1.0f, 0.445f, 0.0f, -0.003f, 0.658f, 1.0f,
		0.448f, 0.0f, -0.002f, 0.658f, 1.0f, 0.45f, 0.0f, -0.001f, 0.657f, 1.0f, 0.452f, 0.0f, 0.001f, 0.656f, 1.0f, 0.455f, 0.0f, 0.002f, 0.655f, 1.0f,
		0.457f, 0.0f, 0.004f, 0.654f, 1.0f, 0.459f, 0.0f, 0.005f, 0.653f, 1.0f, 0.462f, 0.0f, 0.007f, 0.652f, 1.0f, 0.464f, 0.0f, 0.009f, 0.651f, 1.0f,
		0.466f, 0.0f, 0.01f, 0.65f, 1.0f, 0.468f, 0.0f, 0.012f, 0.65f, 1.0f, 0.47f, 0.0f, 0.014f, 0.649f, 1.0f, 0.472f, 0.0f, 0.016f, 0.648f, 1.0f,
		0.474f, 0.0f, 0.018f, 0.647f, 1.0f, 0.476f, 0.0f, 0.019f, 0.646f, 1.0f, 0.478f, 0.0f, 0.021f, 0.645f, 1.0f, 0.479f, 0.0f, 0.023f, 0.644f, 1.0f,
		0.481f, 0.0f, 0.025f, 0.643f, 1.0f, 0.483f, 0.0f, 0.027f, 0.642f, 1.0f, 0.485f, 0.0f, 0.03f, 0.642f, 1.0f, 0.486f, 0.0f, 0.032f, 0.641f, 1.0f,
		0.488f, 0.0f, 0.034f, 0.64f, 1.0f, 0.49f, 0.0f, 0.036f, 0.639f, 1.0f, 0.491f, 0.0f, 0.038f, 0.638f, 1.0f, 0.493f, 0.0f, 0.041f, 0.637f, 1.0f,
		0.494f, 0.0f, 0.043f, 0.636f, 1.0f, 0.496f, 0.0f, 0.045f, 0.635f, 1.0f, 0.497f, 0.0f, 0.048f, 0.635f, 1.0f, 0.499f, 0.0f, 0.05f, 0.634f, 1.0f,
		0.5f, 0.0f, 0.053f, 0.633f, 1.0f, 0.502f, 0.0f, 0.055f, 0.632f, 1.0f, 0.503f, 0.0f, 0.058f, 0.631f, 1.0f, 0.505f, 0.0f, 0.06f, 0.63f, 1.0f,
		0.506f, 0.0f, 0.063f, 0.63f, 1.0f, 0.507f, 0.0f, 0.066f, 0.629f, 1.0f, 0.509f, 0.0f, 0.068f, 0.628f, 1.0f, 0.51f, 0.0f, 0.071f, 0.628f, 1.0f,
		0.511f, 0.0f, 0.074f, 0.627f, 1.0f, 0.513f, 0.0f, 0.077f, 0.626f, 1.0f, 0.514f, 0.0f, 0.079f, 0.625f, 1.0f, 0.515f, 0.0f, 0.082f, 0.625f, 1.0f,
		0.516f, 0.0f, 0.085f, 0.624f, 1.0f, 0.518f, 0.0f, 0.088f, 0.623f, 1.0f, 0.519f, 0.0f, 0.091f, 0.622f, 1.0f, 0.52f, 0.0f, 0.094f, 0.62f, 1.0f,
		0.521f, 0.0f, 0.098f, 0.619f, 1.0f, 0.522f, 0.0f, 0.101f, 0.617f, 1.0f, 0.523f, 0.0f, 0.104f, 0.615f, 1.0f, 0.524f, 0.0f, 0.107f, 0.613f, 1.0f,
		0.525f, 0.0f, 0.111f, 0.611f, 1.0f, 0.526f, 0.0f, 0.114f, 0.609f, 1.0f, 0.527f, 0.0f, 0.118f, 0.607f, 1.0f, 0.527f, 0.0f, 0.121f, 0.605f, 1.0f,
		0.528f, 0.0f, 0.124f, 0.603f, 1.0f, 0.529f, 0.0f, 0.128f, 0.602f, 1.0f, 0.529f, 0.0f, 0.132f, 0.6f, 1.0f, 0.53f, 0.0f, 0.135f, 0.599f, 1.0f,
		0.531f, 0.0f, 0.139f, 0.598f, 1.0f, 0.531f, 0.0f, 0.142f, 0.598f, 1.0f, 0.531f, 0.0f, 0.146f, 0.597f, 1.0f, 0.532f, 0.0f, 0.15f, 0.596f, 1.0f,
		0.532f, 0.0f, 0.154f, 0.596f, 1.0f, 0.532f, 0.0f, 0.157f, 0.595f, 1.0f, 0.532f, 0.0f, 0.161f, 0.595f, 1.0f, 0.532f, 0.0f, 0.165f, 0.594f, 1.0f,
		0.532f, 0.0f, 0.169f, 0.593f, 1.0f, 0.532f, 0.0f, 0.173f, 0.592f, 1.0f, 0.532f, 0.0f, 0.177f, 0.591f, 1.0f, 0.532f, 0.0f, 0.181f, 0.59f, 1.0f,
		0.531f, 0.0f, 0.185f, 0.589f, 1.0f, 0.531f, 0.0f, 0.189f, 0.588f, 1.0f, 0.53f, 0.0f, 0.194f, 0.587f, 1.0f, 0.529f, 0.0f, 0.198f, 0.586f, 1.0f,
		0.528f, 0.0f, 0.202f, 0.585f, 1.0f, 0.527f, 0.0f, 0.207f, 0.584f, 1.0f, 0.526f, 0.0f, 0.211f, 0.584f, 1.0f, 0.525f, 0.0f, 0.215f, 0.583f, 1.0f,
		0.523f, 0.0f, 0.22f, 0.583f, 1.0f, 0.522f, 0.0f, 0.224f, 0.583f, 1.0f, 0.52f, 0.0f, 0.229f, 0.582f, 1.0f, 0.518f, 0.0f, 0.234f, 0.582f, 1.0f,
		0.515f, 0.0f, 0.238f, 0.582f, 1.0f, 0.513f, 0.0f, 0.243f, 0.581f, 1.0f, 0.51f, 0.0f, 0.247f, 0.58f, 1.0f, 0.508f, 0.0f, 0.252f, 0.579f, 1.0f,
		0.505f, 0.0f, 0.257f, 0.578f, 1.0f, 0.502f, 0.0f, 0.261f, 0.576f, 1.0f, 0.499f, 0.0f, 0.266f, 0.573f, 1.0f, 0.496f, 0.0f, 0.27f, 0.57f, 1.0f,
		0.492f, 0.0f, 0.275f, 0.566f, 1.0f, 0.489f, 0.0f, 0.279f, 0.561f, 1.0f, 0.485f, 0.0f, 0.284f, 0.555f, 1.0f, 0.481f, 0.0f, 0.288f, 0.548f, 1.0f,
		0.478f, 0.0f, 0.293f, 0.54f, 1.0f, 0.473f, 0.0f, 0.297f, 0.531f, 1.0f, 0.469f, 0.0f, 0.301f, 0.521f, 1.0f, 0.465f, 0.0f, 0.305f, 0.509f, 1.0f,
		0.461f, 0.0f, 0.309f, 0.496f, 1.0f, 0.456f, 0.0f, 0.313f, 0.481f, 1.0f, 0.452f, 0.0f, 0.317f, 0.464f, 1.0f, 0.448f, 0.0f, 0.321f, 0.445f, 1.0f,
		0.443f, 0.0f, 0.324f, 0.424f, 1.0f, 0.438f, 0.0f, 0.328f, 0.401f, 1.0f, 0.434f, 0.0f, 0.331f, 0.374f, 1.0f, 0.429f, 0.0f, 0.334f, 0.346f, 1.0f,
		0.425f, 0.0f, 0.337f, 0.314f, 1.0f, 0.421f, 0.0f, 0.34f, 0.281f, 1.0f, 0.416f, 0.0f, 0.343f, 0.245f, 1.0f, 0.412f, 0.0f, 0.346f, 0.208f, 1.0f,
		0.408f, 0.0f, 0.349f, 0.169f, 1.0f, 0.404f, 0.0f, 0.351f, 0.13f, 1.0f, 0.401f, 0.0f, 0.354f, 0.089f, 1.0f, 0.398f, 0.0f, 0.356f, 0.054f, 1.0f,
		0.394f, 0.0f, 0.359f, 0.0f, 1.0f, };
	gpencil_add_points(gps, data22, 309);

	gps = gpencil_add_stroke(frameLines, palette, color_Black, 209, "Black", 3);
	float data23[209 * 5] = { -0.751f, 0.0f, 0.173f, 0.0f, 1.0f, -0.751f, 0.0f, 0.168f, 0.0f, 1.0f, -0.75f, 0.0f, 0.164f, 0.0f, 1.0f, -0.75f, 0.0f, 0.16f, 0.0f, 1.0f,
		-0.75f, 0.0f, 0.156f, 0.0f, 1.0f, -0.749f, 0.0f, 0.152f, 0.0f, 1.0f, -0.749f, 0.0f, 0.148f, 0.0f, 1.0f, -0.748f, 0.0f, 0.144f, 0.0f, 1.0f,
		-0.747f, 0.0f, 0.14f, 0.001f, 1.0f, -0.747f, 0.0f, 0.137f, 0.002f, 1.0f, -0.746f, 0.0f, 0.133f, 0.005f, 1.0f, -0.745f, 0.0f, 0.129f, 0.008f, 1.0f,
		-0.745f, 0.0f, 0.125f, 0.013f, 1.0f, -0.744f, 0.0f, 0.122f, 0.02f, 1.0f, -0.743f, 0.0f, 0.118f, 0.028f, 1.0f, -0.742f, 0.0f, 0.115f, 0.038f, 1.0f,
		-0.741f, 0.0f, 0.111f, 0.049f, 1.0f, -0.74f, 0.0f, 0.108f, 0.061f, 1.0f, -0.739f, 0.0f, 0.105f, 0.073f, 1.0f, -0.738f, 0.0f, 0.101f, 0.085f, 1.0f,
		-0.736f, 0.0f, 0.098f, 0.097f, 1.0f, -0.735f, 0.0f, 0.095f, 0.109f, 1.0f, -0.734f, 0.0f, 0.091f, 0.119f, 1.0f, -0.732f, 0.0f, 0.088f, 0.129f, 1.0f,
		-0.731f, 0.0f, 0.085f, 0.138f, 1.0f, -0.729f, 0.0f, 0.082f, 0.146f, 1.0f, -0.728f, 0.0f, 0.079f, 0.153f, 1.0f, -0.726f, 0.0f, 0.076f, 0.158f, 1.0f,
		-0.725f, 0.0f, 0.073f, 0.163f, 1.0f, -0.723f, 0.0f, 0.07f, 0.167f, 1.0f, -0.722f, 0.0f, 0.067f, 0.17f, 1.0f, -0.72f, 0.0f, 0.065f, 0.173f, 1.0f,
		-0.718f, 0.0f, 0.062f, 0.174f, 1.0f, -0.717f, 0.0f, 0.059f, 0.175f, 1.0f, -0.715f, 0.0f, 0.057f, 0.176f, 1.0f, -0.714f, 0.0f, 0.054f, 0.176f, 1.0f,
		-0.712f, 0.0f, 0.051f, 0.176f, 1.0f, -0.71f, 0.0f, 0.049f, 0.176f, 1.0f, -0.709f, 0.0f, 0.046f, 0.176f, 1.0f, -0.707f, 0.0f, 0.043f, 0.176f, 1.0f,
		-0.705f, 0.0f, 0.041f, 0.176f, 1.0f, -0.703f, 0.0f, 0.038f, 0.176f, 1.0f, -0.701f, 0.0f, 0.035f, 0.176f, 1.0f, -0.7f, 0.0f, 0.033f, 0.177f, 1.0f,
		-0.698f, 0.0f, 0.03f, 0.177f, 1.0f, -0.696f, 0.0f, 0.027f, 0.178f, 1.0f, -0.694f, 0.0f, 0.024f, 0.179f, 1.0f, -0.692f, 0.0f, 0.022f, 0.18f, 1.0f,
		-0.69f, 0.0f, 0.019f, 0.181f, 1.0f, -0.688f, 0.0f, 0.016f, 0.182f, 1.0f, -0.685f, 0.0f, 0.013f, 0.184f, 1.0f, -0.683f, 0.0f, 0.01f, 0.187f, 1.0f,
		-0.681f, 0.0f, 0.007f, 0.19f, 1.0f, -0.679f, 0.0f, 0.004f, 0.194f, 1.0f, -0.677f, 0.0f, 0.001f, 0.198f, 1.0f, -0.675f, 0.0f, -0.002f, 0.203f, 1.0f,
		-0.673f, 0.0f, -0.005f, 0.209f, 1.0f, -0.67f, 0.0f, -0.008f, 0.215f, 1.0f, -0.668f, 0.0f, -0.011f, 0.222f, 1.0f, -0.666f, 0.0f, -0.014f, 0.229f, 1.0f,
		-0.664f, 0.0f, -0.017f, 0.237f, 1.0f, -0.661f, 0.0f, -0.02f, 0.246f, 1.0f, -0.659f, 0.0f, -0.023f, 0.255f, 1.0f, -0.657f, 0.0f, -0.025f, 0.264f, 1.0f,
		-0.654f, 0.0f, -0.028f, 0.275f, 1.0f, -0.652f, 0.0f, -0.031f, 0.285f, 1.0f, -0.65f, 0.0f, -0.034f, 0.297f, 1.0f, -0.647f, 0.0f, -0.037f, 0.309f, 1.0f,
		-0.644f, 0.0f, -0.04f, 0.322f, 1.0f, -0.642f, 0.0f, -0.043f, 0.335f, 1.0f, -0.639f, 0.0f, -0.046f, 0.348f, 1.0f, -0.636f, 0.0f, -0.049f, 0.361f, 1.0f,
		-0.633f, 0.0f, -0.052f, 0.374f, 1.0f, -0.63f, 0.0f, -0.055f, 0.386f, 1.0f, -0.627f, 0.0f, -0.058f, 0.397f, 1.0f, -0.624f, 0.0f, -0.061f, 0.408f, 1.0f,
		-0.62f, 0.0f, -0.064f, 0.418f, 1.0f, -0.617f, 0.0f, -0.067f, 0.427f, 1.0f, -0.614f, 0.0f, -0.07f, 0.435f, 1.0f, -0.611f, 0.0f, -0.073f, 0.443f, 1.0f,
		-0.607f, 0.0f, -0.075f, 0.451f, 1.0f, -0.604f, 0.0f, -0.078f, 0.458f, 1.0f, -0.6f, 0.0f, -0.081f, 0.465f, 1.0f, -0.597f, 0.0f, -0.084f, 0.472f, 1.0f,
		-0.593f, 0.0f, -0.086f, 0.479f, 1.0f, -0.59f, 0.0f, -0.089f, 0.486f, 1.0f, -0.586f, 0.0f, -0.092f, 0.492f, 1.0f, -0.583f, 0.0f, -0.094f, 0.499f, 1.0f,
		-0.579f, 0.0f, -0.097f, 0.505f, 1.0f, -0.575f, 0.0f, -0.099f, 0.512f, 1.0f, -0.571f, 0.0f, -0.102f, 0.518f, 1.0f, -0.567f, 0.0f, -0.105f, 0.524f, 1.0f,
		-0.563f, 0.0f, -0.107f, 0.53f, 1.0f, -0.559f, 0.0f, -0.11f, 0.536f, 1.0f, -0.555f, 0.0f, -0.112f, 0.541f, 1.0f, -0.551f, 0.0f, -0.115f, 0.546f, 1.0f,
		-0.546f, 0.0f, -0.117f, 0.551f, 1.0f, -0.542f, 0.0f, -0.12f, 0.555f, 1.0f, -0.538f, 0.0f, -0.122f, 0.559f, 1.0f, -0.533f, 0.0f, -0.125f, 0.562f, 1.0f,
		-0.529f, 0.0f, -0.127f, 0.565f, 1.0f, -0.525f, 0.0f, -0.129f, 0.568f, 1.0f, -0.52f, 0.0f, -0.132f, 0.57f, 1.0f, -0.516f, 0.0f, -0.134f, 0.572f, 1.0f,
		-0.512f, 0.0f, -0.137f, 0.574f, 1.0f, -0.508f, 0.0f, -0.139f, 0.576f, 1.0f, -0.503f, 0.0f, -0.141f, 0.577f, 1.0f, -0.499f, 0.0f, -0.144f, 0.578f, 1.0f,
		-0.495f, 0.0f, -0.146f, 0.579f, 1.0f, -0.491f, 0.0f, -0.148f, 0.579f, 1.0f, -0.487f, 0.0f, -0.151f, 0.578f, 1.0f, -0.483f, 0.0f, -0.153f, 0.577f, 1.0f,
		-0.479f, 0.0f, -0.155f, 0.574f, 1.0f, -0.475f, 0.0f, -0.158f, 0.571f, 1.0f, -0.471f, 0.0f, -0.16f, 0.567f, 1.0f, -0.467f, 0.0f, -0.162f, 0.561f, 1.0f,
		-0.463f, 0.0f, -0.165f, 0.555f, 1.0f, -0.459f, 0.0f, -0.167f, 0.548f, 1.0f, -0.456f, 0.0f, -0.169f, 0.54f, 1.0f, -0.452f, 0.0f, -0.172f, 0.532f, 1.0f,
		-0.448f, 0.0f, -0.174f, 0.523f, 1.0f, -0.445f, 0.0f, -0.176f, 0.514f, 1.0f, -0.441f, 0.0f, -0.179f, 0.505f, 1.0f, -0.438f, 0.0f, -0.181f, 0.497f, 1.0f,
		-0.435f, 0.0f, -0.183f, 0.488f, 1.0f, -0.431f, 0.0f, -0.185f, 0.48f, 1.0f, -0.428f, 0.0f, -0.188f, 0.472f, 1.0f, -0.425f, 0.0f, -0.19f, 0.464f, 1.0f,
		-0.422f, 0.0f, -0.192f, 0.457f, 1.0f, -0.419f, 0.0f, -0.194f, 0.451f, 1.0f, -0.416f, 0.0f, -0.196f, 0.444f, 1.0f, -0.413f, 0.0f, -0.198f, 0.439f, 1.0f,
		-0.41f, 0.0f, -0.2f, 0.434f, 1.0f, -0.407f, 0.0f, -0.202f, 0.429f, 1.0f, -0.404f, 0.0f, -0.204f, 0.426f, 1.0f, -0.401f, 0.0f, -0.206f, 0.422f, 1.0f,
		-0.398f, 0.0f, -0.208f, 0.419f, 1.0f, -0.396f, 0.0f, -0.21f, 0.417f, 1.0f, -0.393f, 0.0f, -0.212f, 0.415f, 1.0f, -0.39f, 0.0f, -0.213f, 0.413f, 1.0f,
		-0.388f, 0.0f, -0.215f, 0.412f, 1.0f, -0.385f, 0.0f, -0.217f, 0.411f, 1.0f, -0.382f, 0.0f, -0.219f, 0.41f, 1.0f, -0.38f, 0.0f, -0.221f, 0.41f, 1.0f,
		-0.377f, 0.0f, -0.222f, 0.409f, 1.0f, -0.375f, 0.0f, -0.224f, 0.409f, 1.0f, -0.372f, 0.0f, -0.226f, 0.409f, 1.0f, -0.37f, 0.0f, -0.228f, 0.409f, 1.0f,
		-0.367f, 0.0f, -0.229f, 0.409f, 1.0f, -0.365f, 0.0f, -0.231f, 0.409f, 1.0f, -0.362f, 0.0f, -0.233f, 0.409f, 1.0f, -0.36f, 0.0f, -0.235f, 0.409f, 1.0f,
		-0.357f, 0.0f, -0.236f, 0.409f, 1.0f, -0.355f, 0.0f, -0.238f, 0.409f, 1.0f, -0.352f, 0.0f, -0.24f, 0.408f, 1.0f, -0.35f, 0.0f, -0.242f, 0.408f, 1.0f,
		-0.348f, 0.0f, -0.243f, 0.407f, 1.0f, -0.345f, 0.0f, -0.245f, 0.406f, 1.0f, -0.343f, 0.0f, -0.247f, 0.405f, 1.0f, -0.34f, 0.0f, -0.249f, 0.404f, 1.0f,
		-0.338f, 0.0f, -0.251f, 0.403f, 1.0f, -0.336f, 0.0f, -0.253f, 0.401f, 1.0f, -0.333f, 0.0f, -0.255f, 0.399f, 1.0f, -0.331f, 0.0f, -0.256f, 0.397f, 1.0f,
		-0.329f, 0.0f, -0.258f, 0.394f, 1.0f, -0.327f, 0.0f, -0.26f, 0.391f, 1.0f, -0.324f, 0.0f, -0.262f, 0.387f, 1.0f, -0.322f, 0.0f, -0.264f, 0.383f, 1.0f,
		-0.32f, 0.0f, -0.266f, 0.379f, 1.0f, -0.318f, 0.0f, -0.268f, 0.374f, 1.0f, -0.316f, 0.0f, -0.27f, 0.368f, 1.0f, -0.314f, 0.0f, -0.272f, 0.362f, 1.0f,
		-0.312f, 0.0f, -0.275f, 0.356f, 1.0f, -0.309f, 0.0f, -0.277f, 0.349f, 1.0f, -0.307f, 0.0f, -0.279f, 0.341f, 1.0f, -0.305f, 0.0f, -0.281f, 0.333f, 1.0f,
		-0.303f, 0.0f, -0.283f, 0.325f, 1.0f, -0.301f, 0.0f, -0.286f, 0.316f, 1.0f, -0.299f, 0.0f, -0.288f, 0.307f, 1.0f, -0.297f, 0.0f, -0.29f, 0.298f, 1.0f,
		-0.295f, 0.0f, -0.293f, 0.289f, 1.0f, -0.293f, 0.0f, -0.295f, 0.279f, 1.0f, -0.291f, 0.0f, -0.298f, 0.269f, 1.0f, -0.29f, 0.0f, -0.3f, 0.259f, 1.0f,
		-0.288f, 0.0f, -0.303f, 0.249f, 1.0f, -0.286f, 0.0f, -0.306f, 0.238f, 1.0f, -0.284f, 0.0f, -0.308f, 0.227f, 1.0f, -0.282f, 0.0f, -0.311f, 0.215f, 1.0f,
		-0.28f, 0.0f, -0.314f, 0.203f, 1.0f, -0.278f, 0.0f, -0.317f, 0.191f, 1.0f, -0.277f, 0.0f, -0.32f, 0.178f, 1.0f, -0.275f, 0.0f, -0.323f, 0.165f, 1.0f,
		-0.273f, 0.0f, -0.326f, 0.151f, 1.0f, -0.271f, 0.0f, -0.33f, 0.138f, 1.0f, -0.27f, 0.0f, -0.333f, 0.124f, 1.0f, -0.268f, 0.0f, -0.336f, 0.11f, 1.0f,
		-0.267f, 0.0f, -0.34f, 0.097f, 1.0f, -0.265f, 0.0f, -0.343f, 0.085f, 1.0f, -0.264f, 0.0f, -0.346f, 0.073f, 1.0f, -0.262f, 0.0f, -0.35f, 0.062f, 1.0f,
		-0.261f, 0.0f, -0.353f, 0.052f, 1.0f, -0.259f, 0.0f, -0.357f, 0.043f, 1.0f, -0.258f, 0.0f, -0.36f, 0.035f, 1.0f, -0.257f, 0.0f, -0.363f, 0.028f, 1.0f,
		-0.255f, 0.0f, -0.366f, 0.021f, 1.0f, -0.254f, 0.0f, -0.369f, 0.016f, 1.0f, -0.253f, 0.0f, -0.372f, 0.01f, 1.0f, -0.252f, 0.0f, -0.375f, 0.006f, 1.0f,
		-0.251f, 0.0f, -0.379f, 0.0f, 1.0f, };
	gpencil_add_points(gps, data23, 209);

	gps = gpencil_add_stroke(frameLines, palette, color_Black, 133, "Black", 3);
	float data24[133 * 5] = { 0.233f, 0.0f, -0.376f, 0.021f, 1.0f, 0.234f, 0.0f, -0.372f, 0.08f, 1.0f, 0.234f, 0.0f, -0.369f, 0.116f, 1.0f, 0.234f, 0.0f, -0.366f, 0.156f, 1.0f,
		0.235f, 0.0f, -0.362f, 0.191f, 1.0f, 0.236f, 0.0f, -0.359f, 0.222f, 1.0f, 0.236f, 0.0f, -0.356f, 0.248f, 1.0f, 0.237f, 0.0f, -0.353f, 0.27f, 1.0f,
		0.238f, 0.0f, -0.35f, 0.289f, 1.0f, 0.239f, 0.0f, -0.346f, 0.304f, 1.0f, 0.24f, 0.0f, -0.343f, 0.319f, 1.0f, 0.241f, 0.0f, -0.34f, 0.334f, 1.0f,
		0.242f, 0.0f, -0.337f, 0.35f, 1.0f, 0.243f, 0.0f, -0.335f, 0.367f, 1.0f, 0.244f, 0.0f, -0.332f, 0.385f, 1.0f, 0.245f, 0.0f, -0.329f, 0.401f, 1.0f,
		0.247f, 0.0f, -0.327f, 0.415f, 1.0f, 0.248f, 0.0f, -0.324f, 0.426f, 1.0f, 0.249f, 0.0f, -0.322f, 0.435f, 1.0f, 0.251f, 0.0f, -0.32f, 0.443f, 1.0f,
		0.252f, 0.0f, -0.318f, 0.449f, 1.0f, 0.254f, 0.0f, -0.316f, 0.455f, 1.0f, 0.255f, 0.0f, -0.314f, 0.461f, 1.0f, 0.257f, 0.0f, -0.312f, 0.467f, 1.0f,
		0.258f, 0.0f, -0.311f, 0.474f, 1.0f, 0.26f, 0.0f, -0.309f, 0.48f, 1.0f, 0.262f, 0.0f, -0.307f, 0.487f, 1.0f, 0.263f, 0.0f, -0.305f, 0.493f, 1.0f,
		0.265f, 0.0f, -0.303f, 0.499f, 1.0f, 0.267f, 0.0f, -0.3f, 0.505f, 1.0f, 0.269f, 0.0f, -0.298f, 0.511f, 1.0f, 0.271f, 0.0f, -0.296f, 0.518f, 1.0f,
		0.273f, 0.0f, -0.294f, 0.524f, 1.0f, 0.276f, 0.0f, -0.291f, 0.531f, 1.0f, 0.278f, 0.0f, -0.289f, 0.539f, 1.0f, 0.281f, 0.0f, -0.287f, 0.546f, 1.0f,
		0.283f, 0.0f, -0.284f, 0.552f, 1.0f, 0.286f, 0.0f, -0.281f, 0.557f, 1.0f, 0.289f, 0.0f, -0.279f, 0.561f, 1.0f, 0.292f, 0.0f, -0.276f, 0.565f, 1.0f,
		0.294f, 0.0f, -0.274f, 0.568f, 1.0f, 0.297f, 0.0f, -0.271f, 0.57f, 1.0f, 0.3f, 0.0f, -0.269f, 0.572f, 1.0f, 0.303f, 0.0f, -0.267f, 0.574f, 1.0f,
		0.306f, 0.0f, -0.264f, 0.575f, 1.0f, 0.308f, 0.0f, -0.262f, 0.576f, 1.0f, 0.311f, 0.0f, -0.26f, 0.577f, 1.0f, 0.314f, 0.0f, -0.257f, 0.578f, 1.0f,
		0.316f, 0.0f, -0.255f, 0.578f, 1.0f, 0.319f, 0.0f, -0.253f, 0.579f, 1.0f, 0.322f, 0.0f, -0.25f, 0.579f, 1.0f, 0.325f, 0.0f, -0.248f, 0.58f, 1.0f,
		0.328f, 0.0f, -0.246f, 0.58f, 1.0f, 0.331f, 0.0f, -0.243f, 0.58f, 1.0f, 0.334f, 0.0f, -0.241f, 0.58f, 1.0f, 0.337f, 0.0f, -0.239f, 0.58f, 1.0f,
		0.341f, 0.0f, -0.236f, 0.58f, 1.0f, 0.344f, 0.0f, -0.233f, 0.581f, 1.0f, 0.348f, 0.0f, -0.231f, 0.581f, 1.0f, 0.352f, 0.0f, -0.228f, 0.581f, 1.0f,
		0.356f, 0.0f, -0.225f, 0.582f, 1.0f, 0.36f, 0.0f, -0.222f, 0.582f, 1.0f, 0.365f, 0.0f, -0.219f, 0.582f, 1.0f, 0.369f, 0.0f, -0.216f, 0.582f, 1.0f,
		0.374f, 0.0f, -0.214f, 0.582f, 1.0f, 0.378f, 0.0f, -0.211f, 0.582f, 1.0f, 0.383f, 0.0f, -0.208f, 0.583f, 1.0f, 0.387f, 0.0f, -0.205f, 0.583f, 1.0f,
		0.392f, 0.0f, -0.202f, 0.583f, 1.0f, 0.397f, 0.0f, -0.199f, 0.583f, 1.0f, 0.401f, 0.0f, -0.197f, 0.583f, 1.0f, 0.406f, 0.0f, -0.194f, 0.583f, 1.0f,
		0.411f, 0.0f, -0.191f, 0.583f, 1.0f, 0.416f, 0.0f, -0.188f, 0.583f, 1.0f, 0.42f, 0.0f, -0.186f, 0.583f, 1.0f, 0.425f, 0.0f, -0.183f, 0.583f, 1.0f,
		0.43f, 0.0f, -0.18f, 0.583f, 1.0f, 0.434f, 0.0f, -0.178f, 0.583f, 1.0f, 0.439f, 0.0f, -0.175f, 0.583f, 1.0f, 0.444f, 0.0f, -0.172f, 0.583f, 1.0f,
		0.449f, 0.0f, -0.17f, 0.584f, 1.0f, 0.453f, 0.0f, -0.167f, 0.584f, 1.0f, 0.458f, 0.0f, -0.164f, 0.584f, 1.0f, 0.463f, 0.0f, -0.161f, 0.585f, 1.0f,
		0.468f, 0.0f, -0.158f, 0.585f, 1.0f, 0.473f, 0.0f, -0.155f, 0.585f, 1.0f, 0.478f, 0.0f, -0.152f, 0.585f, 1.0f, 0.483f, 0.0f, -0.149f, 0.585f, 1.0f,
		0.488f, 0.0f, -0.146f, 0.585f, 1.0f, 0.492f, 0.0f, -0.143f, 0.585f, 1.0f, 0.497f, 0.0f, -0.14f, 0.586f, 1.0f, 0.501f, 0.0f, -0.137f, 0.586f, 1.0f,
		0.506f, 0.0f, -0.134f, 0.586f, 1.0f, 0.51f, 0.0f, -0.13f, 0.586f, 1.0f, 0.515f, 0.0f, -0.127f, 0.586f, 1.0f, 0.52f, 0.0f, -0.124f, 0.586f, 1.0f,
		0.524f, 0.0f, -0.12f, 0.586f, 1.0f, 0.529f, 0.0f, -0.117f, 0.586f, 1.0f, 0.534f, 0.0f, -0.113f, 0.586f, 1.0f, 0.539f, 0.0f, -0.109f, 0.586f, 1.0f,
		0.544f, 0.0f, -0.105f, 0.586f, 1.0f, 0.55f, 0.0f, -0.1f, 0.586f, 1.0f, 0.555f, 0.0f, -0.095f, 0.586f, 1.0f, 0.561f, 0.0f, -0.09f, 0.586f, 1.0f,
		0.567f, 0.0f, -0.084f, 0.587f, 1.0f, 0.573f, 0.0f, -0.078f, 0.587f, 1.0f, 0.579f, 0.0f, -0.071f, 0.587f, 1.0f, 0.586f, 0.0f, -0.063f, 0.588f, 1.0f,
		0.593f, 0.0f, -0.055f, 0.588f, 1.0f, 0.6f, 0.0f, -0.047f, 0.588f, 1.0f, 0.607f, 0.0f, -0.038f, 0.589f, 1.0f, 0.614f, 0.0f, -0.028f, 0.589f, 1.0f,
		0.621f, 0.0f, -0.018f, 0.589f, 1.0f, 0.629f, 0.0f, -0.007f, 0.589f, 1.0f, 0.636f, 0.0f, 0.004f, 0.589f, 1.0f, 0.643f, 0.0f, 0.015f, 0.59f, 1.0f,
		0.65f, 0.0f, 0.026f, 0.589f, 1.0f, 0.656f, 0.0f, 0.038f, 0.589f, 1.0f, 0.663f, 0.0f, 0.049f, 0.588f, 1.0f, 0.669f, 0.0f, 0.06f, 0.587f, 1.0f,
		0.676f, 0.0f, 0.072f, 0.584f, 1.0f, 0.682f, 0.0f, 0.084f, 0.579f, 1.0f, 0.688f, 0.0f, 0.096f, 0.571f, 1.0f, 0.694f, 0.0f, 0.108f, 0.558f, 1.0f,
		0.7f, 0.0f, 0.12f, 0.54f, 1.0f, 0.706f, 0.0f, 0.133f, 0.514f, 1.0f, 0.712f, 0.0f, 0.145f, 0.478f, 1.0f, 0.718f, 0.0f, 0.158f, 0.431f, 1.0f,
		0.723f, 0.0f, 0.17f, 0.369f, 1.0f, 0.728f, 0.0f, 0.182f, 0.294f, 1.0f, 0.733f, 0.0f, 0.194f, 0.205f, 1.0f, 0.737f, 0.0f, 0.204f, 0.125f, 1.0f,
		0.743f, 0.0f, 0.218f, 0.0f, 1.0f, };
	gpencil_add_points(gps, data24, 133);

	gps = gpencil_add_stroke(frameLines, palette, color_Black, 389, "Black", 3);
	float data25[389 * 5] = { -0.284f, 0.0f, -0.444f, 0.0f, 1.0f, -0.285f, 0.0f, -0.448f, 0.0f, 1.0f, -0.285f, 0.0f, -0.45f, 0.0f, 1.0f, -0.286f, 0.0f, -0.454f, 0.0f, 1.0f,
		-0.286f, 0.0f, -0.457f, 0.0f, 1.0f, -0.287f, 0.0f, -0.46f, 0.0f, 1.0f, -0.288f, 0.0f, -0.463f, 0.0f, 1.0f, -0.289f, 0.0f, -0.466f, 0.0f, 1.0f,
		-0.289f, 0.0f, -0.47f, 0.0f, 1.0f, -0.29f, 0.0f, -0.473f, 0.0f, 1.0f, -0.291f, 0.0f, -0.476f, 0.0f, 1.0f, -0.292f, 0.0f, -0.48f, 0.0f, 1.0f,
		-0.293f, 0.0f, -0.484f, 0.0f, 1.0f, -0.294f, 0.0f, -0.487f, 0.0f, 1.0f, -0.295f, 0.0f, -0.491f, 0.0f, 1.0f, -0.296f, 0.0f, -0.494f, 0.0f, 1.0f,
		-0.297f, 0.0f, -0.498f, 0.0f, 1.0f, -0.298f, 0.0f, -0.502f, 0.0f, 1.0f, -0.299f, 0.0f, -0.505f, 0.0f, 1.0f, -0.3f, 0.0f, -0.509f, 0.0f, 1.0f,
		-0.301f, 0.0f, -0.513f, 0.0f, 1.0f, -0.302f, 0.0f, -0.517f, 0.0f, 1.0f, -0.303f, 0.0f, -0.52f, 0.0f, 1.0f, -0.304f, 0.0f, -0.524f, 0.0f, 1.0f,
		-0.305f, 0.0f, -0.528f, 0.0f, 1.0f, -0.306f, 0.0f, -0.532f, 0.0f, 1.0f, -0.307f, 0.0f, -0.535f, 0.0f, 1.0f, -0.308f, 0.0f, -0.539f, 0.0f, 1.0f,
		-0.309f, 0.0f, -0.543f, 0.0f, 1.0f, -0.31f, 0.0f, -0.547f, 0.0f, 1.0f, -0.311f, 0.0f, -0.55f, 0.0f, 1.0f, -0.312f, 0.0f, -0.554f, 0.0f, 1.0f,
		-0.313f, 0.0f, -0.558f, 0.0f, 1.0f, -0.314f, 0.0f, -0.562f, 0.0f, 1.0f, -0.315f, 0.0f, -0.565f, 0.0f, 1.0f, -0.316f, 0.0f, -0.569f, 0.0f, 1.0f,
		-0.317f, 0.0f, -0.573f, 0.0f, 1.0f, -0.318f, 0.0f, -0.576f, 0.0f, 1.0f, -0.319f, 0.0f, -0.58f, 0.0f, 1.0f, -0.32f, 0.0f, -0.583f, 0.0f, 1.0f,
		-0.321f, 0.0f, -0.587f, 0.0f, 1.0f, -0.322f, 0.0f, -0.591f, 0.0f, 1.0f, -0.323f, 0.0f, -0.594f, 0.0f, 1.0f, -0.323f, 0.0f, -0.598f, 0.0f, 1.0f,
		-0.324f, 0.0f, -0.601f, 0.0f, 1.0f, -0.325f, 0.0f, -0.605f, 0.0f, 1.0f, -0.326f, 0.0f, -0.608f, 0.0f, 1.0f, -0.326f, 0.0f, -0.612f, 0.0f, 1.0f,
		-0.327f, 0.0f, -0.615f, 0.0f, 1.0f, -0.328f, 0.0f, -0.619f, 0.0f, 1.0f, -0.328f, 0.0f, -0.622f, 0.0f, 1.0f, -0.329f, 0.0f, -0.625f, 0.0f, 1.0f,
		-0.33f, 0.0f, -0.629f, 0.0f, 1.0f, -0.33f, 0.0f, -0.632f, 0.0f, 1.0f, -0.331f, 0.0f, -0.635f, 0.001f, 1.0f, -0.331f, 0.0f, -0.639f, 0.001f, 1.0f,
		-0.332f, 0.0f, -0.642f, 0.002f, 1.0f, -0.332f, 0.0f, -0.645f, 0.002f, 1.0f, -0.333f, 0.0f, -0.649f, 0.003f, 1.0f, -0.333f, 0.0f, -0.652f, 0.005f, 1.0f,
		-0.334f, 0.0f, -0.655f, 0.006f, 1.0f, -0.334f, 0.0f, -0.658f, 0.009f, 1.0f, -0.335f, 0.0f, -0.662f, 0.011f, 1.0f, -0.335f, 0.0f, -0.665f, 0.015f, 1.0f,
		-0.335f, 0.0f, -0.668f, 0.019f, 1.0f, -0.336f, 0.0f, -0.672f, 0.024f, 1.0f, -0.336f, 0.0f, -0.675f, 0.031f, 1.0f, -0.337f, 0.0f, -0.678f, 0.038f, 1.0f,
		-0.337f, 0.0f, -0.682f, 0.046f, 1.0f, -0.337f, 0.0f, -0.685f, 0.056f, 1.0f, -0.338f, 0.0f, -0.689f, 0.067f, 1.0f, -0.338f, 0.0f, -0.692f, 0.079f, 1.0f,
		-0.338f, 0.0f, -0.696f, 0.093f, 1.0f, -0.339f, 0.0f, -0.699f, 0.107f, 1.0f, -0.339f, 0.0f, -0.703f, 0.123f, 1.0f, -0.34f, 0.0f, -0.706f, 0.139f, 1.0f,
		-0.34f, 0.0f, -0.71f, 0.157f, 1.0f, -0.34f, 0.0f, -0.714f, 0.174f, 1.0f, -0.34f, 0.0f, -0.717f, 0.193f, 1.0f, -0.341f, 0.0f, -0.721f, 0.211f, 1.0f,
		-0.341f, 0.0f, -0.725f, 0.23f, 1.0f, -0.341f, 0.0f, -0.729f, 0.248f, 1.0f, -0.342f, 0.0f, -0.732f, 0.266f, 1.0f, -0.342f, 0.0f, -0.736f, 0.284f, 1.0f,
		-0.342f, 0.0f, -0.74f, 0.302f, 1.0f, -0.342f, 0.0f, -0.744f, 0.318f, 1.0f, -0.342f, 0.0f, -0.748f, 0.334f, 1.0f, -0.342f, 0.0f, -0.752f, 0.349f, 1.0f,
		-0.343f, 0.0f, -0.756f, 0.364f, 1.0f, -0.343f, 0.0f, -0.76f, 0.377f, 1.0f, -0.343f, 0.0f, -0.763f, 0.389f, 1.0f, -0.343f, 0.0f, -0.767f, 0.401f, 1.0f,
		-0.343f, 0.0f, -0.771f, 0.411f, 1.0f, -0.343f, 0.0f, -0.775f, 0.421f, 1.0f, -0.342f, 0.0f, -0.779f, 0.429f, 1.0f, -0.342f, 0.0f, -0.783f, 0.437f, 1.0f,
		-0.342f, 0.0f, -0.786f, 0.444f, 1.0f, -0.342f, 0.0f, -0.79f, 0.451f, 1.0f, -0.342f, 0.0f, -0.794f, 0.456f, 1.0f, -0.341f, 0.0f, -0.797f, 0.461f, 1.0f,
		-0.341f, 0.0f, -0.801f, 0.466f, 1.0f, -0.34f, 0.0f, -0.805f, 0.469f, 1.0f, -0.34f, 0.0f, -0.808f, 0.473f, 1.0f, -0.339f, 0.0f, -0.812f, 0.476f, 1.0f,
		-0.339f, 0.0f, -0.815f, 0.478f, 1.0f, -0.338f, 0.0f, -0.818f, 0.48f, 1.0f, -0.338f, 0.0f, -0.822f, 0.482f, 1.0f, -0.337f, 0.0f, -0.825f, 0.483f, 1.0f,
		-0.336f, 0.0f, -0.828f, 0.484f, 1.0f, -0.335f, 0.0f, -0.831f, 0.485f, 1.0f, -0.334f, 0.0f, -0.834f, 0.486f, 1.0f, -0.333f, 0.0f, -0.837f, 0.487f, 1.0f,
		-0.332f, 0.0f, -0.84f, 0.487f, 1.0f, -0.331f, 0.0f, -0.843f, 0.487f, 1.0f, -0.33f, 0.0f, -0.846f, 0.488f, 1.0f, -0.329f, 0.0f, -0.849f, 0.488f, 1.0f,
		-0.328f, 0.0f, -0.852f, 0.488f, 1.0f, -0.326f, 0.0f, -0.855f, 0.488f, 1.0f, -0.325f, 0.0f, -0.857f, 0.488f, 1.0f, -0.324f, 0.0f, -0.86f, 0.488f, 1.0f,
		-0.322f, 0.0f, -0.863f, 0.488f, 1.0f, -0.321f, 0.0f, -0.865f, 0.488f, 1.0f, -0.319f, 0.0f, -0.868f, 0.488f, 1.0f, -0.318f, 0.0f, -0.871f, 0.488f, 1.0f,
		-0.316f, 0.0f, -0.873f, 0.489f, 1.0f, -0.314f, 0.0f, -0.876f, 0.489f, 1.0f, -0.312f, 0.0f, -0.878f, 0.489f, 1.0f, -0.311f, 0.0f, -0.881f, 0.489f, 1.0f,
		-0.309f, 0.0f, -0.883f, 0.489f, 1.0f, -0.307f, 0.0f, -0.885f, 0.489f, 1.0f, -0.305f, 0.0f, -0.888f, 0.49f, 1.0f, -0.303f, 0.0f, -0.89f, 0.491f, 1.0f,
		-0.301f, 0.0f, -0.892f, 0.491f, 1.0f, -0.298f, 0.0f, -0.894f, 0.492f, 1.0f, -0.296f, 0.0f, -0.897f, 0.494f, 1.0f, -0.294f, 0.0f, -0.899f, 0.495f, 1.0f,
		-0.292f, 0.0f, -0.901f, 0.497f, 1.0f, -0.289f, 0.0f, -0.903f, 0.5f, 1.0f, -0.287f, 0.0f, -0.905f, 0.502f, 1.0f, -0.284f, 0.0f, -0.907f, 0.505f, 1.0f,
		-0.282f, 0.0f, -0.909f, 0.509f, 1.0f, -0.279f, 0.0f, -0.912f, 0.512f, 1.0f, -0.277f, 0.0f, -0.914f, 0.517f, 1.0f, -0.274f, 0.0f, -0.916f, 0.521f, 1.0f,
		-0.271f, 0.0f, -0.918f, 0.526f, 1.0f, -0.269f, 0.0f, -0.919f, 0.531f, 1.0f, -0.266f, 0.0f, -0.921f, 0.537f, 1.0f, -0.263f, 0.0f, -0.923f, 0.543f, 1.0f,
		-0.26f, 0.0f, -0.925f, 0.548f, 1.0f, -0.257f, 0.0f, -0.927f, 0.554f, 1.0f, -0.255f, 0.0f, -0.929f, 0.56f, 1.0f, -0.252f, 0.0f, -0.931f, 0.566f, 1.0f,
		-0.249f, 0.0f, -0.932f, 0.571f, 1.0f, -0.246f, 0.0f, -0.934f, 0.577f, 1.0f, -0.243f, 0.0f, -0.936f, 0.582f, 1.0f, -0.24f, 0.0f, -0.938f, 0.587f, 1.0f,
		-0.237f, 0.0f, -0.939f, 0.592f, 1.0f, -0.234f, 0.0f, -0.941f, 0.597f, 1.0f, -0.231f, 0.0f, -0.943f, 0.601f, 1.0f, -0.228f, 0.0f, -0.944f, 0.605f, 1.0f,
		-0.225f, 0.0f, -0.946f, 0.609f, 1.0f, -0.222f, 0.0f, -0.948f, 0.613f, 1.0f, -0.219f, 0.0f, -0.949f, 0.617f, 1.0f, -0.216f, 0.0f, -0.951f, 0.62f, 1.0f,
		-0.213f, 0.0f, -0.953f, 0.624f, 1.0f, -0.21f, 0.0f, -0.954f, 0.627f, 1.0f, -0.207f, 0.0f, -0.956f, 0.63f, 1.0f, -0.204f, 0.0f, -0.958f, 0.633f, 1.0f,
		-0.201f, 0.0f, -0.959f, 0.636f, 1.0f, -0.198f, 0.0f, -0.961f, 0.639f, 1.0f, -0.195f, 0.0f, -0.962f, 0.641f, 1.0f, -0.191f, 0.0f, -0.964f, 0.643f, 1.0f,
		-0.188f, 0.0f, -0.965f, 0.646f, 1.0f, -0.185f, 0.0f, -0.967f, 0.648f, 1.0f, -0.181f, 0.0f, -0.968f, 0.649f, 1.0f, -0.178f, 0.0f, -0.969f, 0.651f, 1.0f,
		-0.175f, 0.0f, -0.971f, 0.653f, 1.0f, -0.171f, 0.0f, -0.972f, 0.654f, 1.0f, -0.168f, 0.0f, -0.973f, 0.655f, 1.0f, -0.165f, 0.0f, -0.974f, 0.657f, 1.0f,
		-0.161f, 0.0f, -0.976f, 0.658f, 1.0f, -0.158f, 0.0f, -0.977f, 0.659f, 1.0f, -0.154f, 0.0f, -0.978f, 0.66f, 1.0f, -0.151f, 0.0f, -0.979f, 0.661f, 1.0f,
		-0.148f, 0.0f, -0.98f, 0.662f, 1.0f, -0.144f, 0.0f, -0.981f, 0.664f, 1.0f, -0.141f, 0.0f, -0.982f, 0.665f, 1.0f, -0.137f, 0.0f, -0.983f, 0.667f, 1.0f,
		-0.134f, 0.0f, -0.984f, 0.669f, 1.0f, -0.13f, 0.0f, -0.985f, 0.671f, 1.0f, -0.127f, 0.0f, -0.986f, 0.673f, 1.0f, -0.124f, 0.0f, -0.987f, 0.675f, 1.0f,
		-0.12f, 0.0f, -0.988f, 0.678f, 1.0f, -0.117f, 0.0f, -0.989f, 0.68f, 1.0f, -0.113f, 0.0f, -0.99f, 0.683f, 1.0f, -0.11f, 0.0f, -0.991f, 0.685f, 1.0f,
		-0.107f, 0.0f, -0.992f, 0.688f, 1.0f, -0.103f, 0.0f, -0.992f, 0.691f, 1.0f, -0.1f, 0.0f, -0.993f, 0.693f, 1.0f, -0.097f, 0.0f, -0.994f, 0.696f, 1.0f,
		-0.093f, 0.0f, -0.995f, 0.698f, 1.0f, -0.09f, 0.0f, -0.996f, 0.701f, 1.0f, -0.087f, 0.0f, -0.997f, 0.703f, 1.0f, -0.084f, 0.0f, -0.997f, 0.705f, 1.0f,
		-0.08f, 0.0f, -0.998f, 0.707f, 1.0f, -0.077f, 0.0f, -0.999f, 0.708f, 1.0f, -0.074f, 0.0f, -1.0f, 0.71f, 1.0f, -0.07f, 0.0f, -1.0f, 0.712f, 1.0f,
		-0.067f, 0.0f, -1.001f, 0.713f, 1.0f, -0.063f, 0.0f, -1.002f, 0.715f, 1.0f, -0.06f, 0.0f, -1.002f, 0.717f, 1.0f, -0.056f, 0.0f, -1.003f, 0.718f, 1.0f,
		-0.053f, 0.0f, -1.003f, 0.72f, 1.0f, -0.049f, 0.0f, -1.004f, 0.723f, 1.0f, -0.045f, 0.0f, -1.004f, 0.725f, 1.0f, -0.041f, 0.0f, -1.005f, 0.728f, 1.0f,
		-0.038f, 0.0f, -1.005f, 0.73f, 1.0f, -0.034f, 0.0f, -1.006f, 0.733f, 1.0f, -0.03f, 0.0f, -1.006f, 0.736f, 1.0f, -0.026f, 0.0f, -1.007f, 0.738f, 1.0f,
		-0.022f, 0.0f, -1.007f, 0.741f, 1.0f, -0.018f, 0.0f, -1.007f, 0.743f, 1.0f, -0.014f, 0.0f, -1.008f, 0.746f, 1.0f, -0.01f, 0.0f, -1.008f, 0.748f, 1.0f,
		-0.006f, 0.0f, -1.009f, 0.75f, 1.0f, -0.001f, 0.0f, -1.009f, 0.752f, 1.0f, 0.003f, 0.0f, -1.009f, 0.754f, 1.0f, 0.007f, 0.0f, -1.01f, 0.755f, 1.0f,
		0.011f, 0.0f, -1.01f, 0.757f, 1.0f, 0.015f, 0.0f, -1.01f, 0.758f, 1.0f, 0.02f, 0.0f, -1.011f, 0.759f, 1.0f, 0.024f, 0.0f, -1.011f, 0.76f, 1.0f,
		0.028f, 0.0f, -1.011f, 0.761f, 1.0f, 0.033f, 0.0f, -1.011f, 0.761f, 1.0f, 0.037f, 0.0f, -1.012f, 0.762f, 1.0f, 0.041f, 0.0f, -1.012f, 0.762f, 1.0f,
		0.045f, 0.0f, -1.012f, 0.763f, 1.0f, 0.05f, 0.0f, -1.012f, 0.763f, 1.0f, 0.054f, 0.0f, -1.012f, 0.764f, 1.0f, 0.058f, 0.0f, -1.013f, 0.764f, 1.0f,
		0.062f, 0.0f, -1.013f, 0.764f, 1.0f, 0.066f, 0.0f, -1.013f, 0.764f, 1.0f, 0.071f, 0.0f, -1.013f, 0.764f, 1.0f, 0.075f, 0.0f, -1.013f, 0.765f, 1.0f,
		0.079f, 0.0f, -1.013f, 0.765f, 1.0f, 0.083f, 0.0f, -1.013f, 0.765f, 1.0f, 0.087f, 0.0f, -1.013f, 0.765f, 1.0f, 0.091f, 0.0f, -1.013f, 0.765f, 1.0f,
		0.095f, 0.0f, -1.013f, 0.765f, 1.0f, 0.099f, 0.0f, -1.013f, 0.766f, 1.0f, 0.103f, 0.0f, -1.013f, 0.766f, 1.0f, 0.108f, 0.0f, -1.012f, 0.766f, 1.0f,
		0.112f, 0.0f, -1.012f, 0.766f, 1.0f, 0.116f, 0.0f, -1.012f, 0.766f, 1.0f, 0.119f, 0.0f, -1.012f, 0.767f, 1.0f, 0.123f, 0.0f, -1.011f, 0.767f, 1.0f,
		0.127f, 0.0f, -1.011f, 0.767f, 1.0f, 0.131f, 0.0f, -1.01f, 0.767f, 1.0f, 0.135f, 0.0f, -1.01f, 0.767f, 1.0f, 0.139f, 0.0f, -1.009f, 0.768f, 1.0f,
		0.143f, 0.0f, -1.009f, 0.768f, 1.0f, 0.147f, 0.0f, -1.008f, 0.768f, 1.0f, 0.151f, 0.0f, -1.007f, 0.769f, 1.0f, 0.154f, 0.0f, -1.007f, 0.769f, 1.0f,
		0.158f, 0.0f, -1.006f, 0.769f, 1.0f, 0.162f, 0.0f, -1.005f, 0.769f, 1.0f, 0.166f, 0.0f, -1.004f, 0.77f, 1.0f, 0.17f, 0.0f, -1.003f, 0.77f, 1.0f,
		0.173f, 0.0f, -1.003f, 0.77f, 1.0f, 0.177f, 0.0f, -1.002f, 0.771f, 1.0f, 0.181f, 0.0f, -1.001f, 0.771f, 1.0f, 0.184f, 0.0f, -1.0f, 0.772f, 1.0f,
		0.188f, 0.0f, -0.999f, 0.772f, 1.0f, 0.192f, 0.0f, -0.998f, 0.773f, 1.0f, 0.195f, 0.0f, -0.997f, 0.773f, 1.0f, 0.199f, 0.0f, -0.996f, 0.774f, 1.0f,
		0.202f, 0.0f, -0.995f, 0.774f, 1.0f, 0.206f, 0.0f, -0.994f, 0.775f, 1.0f, 0.209f, 0.0f, -0.993f, 0.776f, 1.0f, 0.213f, 0.0f, -0.992f, 0.776f, 1.0f,
		0.216f, 0.0f, -0.991f, 0.777f, 1.0f, 0.22f, 0.0f, -0.99f, 0.777f, 1.0f, 0.223f, 0.0f, -0.988f, 0.778f, 1.0f, 0.227f, 0.0f, -0.987f, 0.778f, 1.0f,
		0.23f, 0.0f, -0.986f, 0.778f, 1.0f, 0.233f, 0.0f, -0.985f, 0.779f, 1.0f, 0.237f, 0.0f, -0.983f, 0.779f, 1.0f, 0.24f, 0.0f, -0.982f, 0.779f, 1.0f,
		0.243f, 0.0f, -0.981f, 0.779f, 1.0f, 0.246f, 0.0f, -0.979f, 0.778f, 1.0f, 0.249f, 0.0f, -0.978f, 0.778f, 1.0f, 0.252f, 0.0f, -0.976f, 0.777f, 1.0f,
		0.255f, 0.0f, -0.975f, 0.777f, 1.0f, 0.258f, 0.0f, -0.973f, 0.776f, 1.0f, 0.261f, 0.0f, -0.972f, 0.775f, 1.0f, 0.264f, 0.0f, -0.97f, 0.773f, 1.0f,
		0.267f, 0.0f, -0.968f, 0.772f, 1.0f, 0.269f, 0.0f, -0.967f, 0.77f, 1.0f, 0.272f, 0.0f, -0.965f, 0.769f, 1.0f, 0.275f, 0.0f, -0.963f, 0.767f, 1.0f,
		0.277f, 0.0f, -0.961f, 0.765f, 1.0f, 0.279f, 0.0f, -0.959f, 0.763f, 1.0f, 0.282f, 0.0f, -0.957f, 0.761f, 1.0f, 0.284f, 0.0f, -0.955f, 0.759f, 1.0f,
		0.286f, 0.0f, -0.953f, 0.756f, 1.0f, 0.288f, 0.0f, -0.951f, 0.754f, 1.0f, 0.29f, 0.0f, -0.948f, 0.752f, 1.0f, 0.292f, 0.0f, -0.946f, 0.749f, 1.0f,
		0.294f, 0.0f, -0.944f, 0.746f, 1.0f, 0.296f, 0.0f, -0.941f, 0.744f, 1.0f, 0.298f, 0.0f, -0.939f, 0.741f, 1.0f, 0.3f, 0.0f, -0.937f, 0.738f, 1.0f,
		0.302f, 0.0f, -0.934f, 0.736f, 1.0f, 0.303f, 0.0f, -0.932f, 0.733f, 1.0f, 0.305f, 0.0f, -0.929f, 0.73f, 1.0f, 0.306f, 0.0f, -0.926f, 0.727f, 1.0f,
		0.308f, 0.0f, -0.924f, 0.724f, 1.0f, 0.309f, 0.0f, -0.921f, 0.721f, 1.0f, 0.311f, 0.0f, -0.918f, 0.719f, 1.0f, 0.312f, 0.0f, -0.916f, 0.716f, 1.0f,
		0.313f, 0.0f, -0.913f, 0.713f, 1.0f, 0.315f, 0.0f, -0.91f, 0.71f, 1.0f, 0.316f, 0.0f, -0.907f, 0.707f, 1.0f, 0.317f, 0.0f, -0.904f, 0.704f, 1.0f,
		0.318f, 0.0f, -0.901f, 0.7f, 1.0f, 0.319f, 0.0f, -0.898f, 0.697f, 1.0f, 0.32f, 0.0f, -0.895f, 0.693f, 1.0f, 0.321f, 0.0f, -0.892f, 0.69f, 1.0f,
		0.322f, 0.0f, -0.889f, 0.686f, 1.0f, 0.323f, 0.0f, -0.886f, 0.681f, 1.0f, 0.324f, 0.0f, -0.883f, 0.677f, 1.0f, 0.325f, 0.0f, -0.88f, 0.672f, 1.0f,
		0.326f, 0.0f, -0.876f, 0.667f, 1.0f, 0.326f, 0.0f, -0.873f, 0.661f, 1.0f, 0.327f, 0.0f, -0.87f, 0.655f, 1.0f, 0.328f, 0.0f, -0.867f, 0.649f, 1.0f,
		0.329f, 0.0f, -0.864f, 0.643f, 1.0f, 0.329f, 0.0f, -0.861f, 0.637f, 1.0f, 0.33f, 0.0f, -0.857f, 0.63f, 1.0f, 0.331f, 0.0f, -0.854f, 0.624f, 1.0f,
		0.331f, 0.0f, -0.851f, 0.618f, 1.0f, 0.332f, 0.0f, -0.848f, 0.613f, 1.0f, 0.333f, 0.0f, -0.845f, 0.607f, 1.0f, 0.333f, 0.0f, -0.841f, 0.603f, 1.0f,
		0.334f, 0.0f, -0.838f, 0.598f, 1.0f, 0.334f, 0.0f, -0.835f, 0.594f, 1.0f, 0.335f, 0.0f, -0.832f, 0.591f, 1.0f, 0.335f, 0.0f, -0.828f, 0.588f, 1.0f,
		0.335f, 0.0f, -0.825f, 0.586f, 1.0f, 0.336f, 0.0f, -0.821f, 0.584f, 1.0f, 0.336f, 0.0f, -0.818f, 0.582f, 1.0f, 0.336f, 0.0f, -0.814f, 0.581f, 1.0f,
		0.337f, 0.0f, -0.811f, 0.58f, 1.0f, 0.337f, 0.0f, -0.807f, 0.58f, 1.0f, 0.337f, 0.0f, -0.803f, 0.579f, 1.0f, 0.337f, 0.0f, -0.799f, 0.579f, 1.0f,
		0.337f, 0.0f, -0.795f, 0.578f, 1.0f, 0.337f, 0.0f, -0.79f, 0.578f, 1.0f, 0.337f, 0.0f, -0.786f, 0.578f, 1.0f, 0.338f, 0.0f, -0.782f, 0.577f, 1.0f,
		0.338f, 0.0f, -0.777f, 0.576f, 1.0f, 0.337f, 0.0f, -0.772f, 0.574f, 1.0f, 0.337f, 0.0f, -0.767f, 0.572f, 1.0f, 0.337f, 0.0f, -0.762f, 0.569f, 1.0f,
		0.337f, 0.0f, -0.756f, 0.565f, 1.0f, 0.337f, 0.0f, -0.751f, 0.559f, 1.0f, 0.337f, 0.0f, -0.745f, 0.553f, 1.0f, 0.336f, 0.0f, -0.739f, 0.544f, 1.0f,
		0.336f, 0.0f, -0.732f, 0.534f, 1.0f, 0.335f, 0.0f, -0.725f, 0.521f, 1.0f, 0.334f, 0.0f, -0.718f, 0.505f, 1.0f, 0.333f, 0.0f, -0.711f, 0.487f, 1.0f,
		0.332f, 0.0f, -0.703f, 0.466f, 1.0f, 0.331f, 0.0f, -0.694f, 0.441f, 1.0f, 0.33f, 0.0f, -0.686f, 0.413f, 1.0f, 0.328f, 0.0f, -0.677f, 0.383f, 1.0f,
		0.326f, 0.0f, -0.667f, 0.35f, 1.0f, 0.325f, 0.0f, -0.657f, 0.316f, 1.0f, 0.323f, 0.0f, -0.647f, 0.281f, 1.0f, 0.32f, 0.0f, -0.636f, 0.246f, 1.0f,
		0.318f, 0.0f, -0.625f, 0.212f, 1.0f, 0.316f, 0.0f, -0.614f, 0.18f, 1.0f, 0.313f, 0.0f, -0.603f, 0.149f, 1.0f, 0.311f, 0.0f, -0.592f, 0.12f, 1.0f,
		0.308f, 0.0f, -0.581f, 0.093f, 1.0f, 0.306f, 0.0f, -0.57f, 0.069f, 1.0f, 0.303f, 0.0f, -0.559f, 0.046f, 1.0f, 0.301f, 0.0f, -0.55f, 0.027f, 1.0f,
		0.298f, 0.0f, -0.537f, 0.0f, 1.0f, };
	gpencil_add_points(gps, data25, 389);

	gps = gpencil_add_stroke(frameLines, palette, color_Black, 41, "Black", 3);
	float data26[41 * 5] = { -0.104f, 0.0f, -0.795f, 0.258f, 1.0f, -0.1f, 0.0f, -0.799f, 0.28f, 1.0f, -0.097f, 0.0f, -0.801f, 0.294f, 1.0f, -0.094f, 0.0f, -0.805f, 0.312f, 1.0f,
		-0.09f, 0.0f, -0.808f, 0.328f, 1.0f, -0.086f, 0.0f, -0.811f, 0.345f, 1.0f, -0.082f, 0.0f, -0.815f, 0.361f, 1.0f, -0.078f, 0.0f, -0.818f, 0.377f, 1.0f,
		-0.073f, 0.0f, -0.821f, 0.392f, 1.0f, -0.068f, 0.0f, -0.824f, 0.407f, 1.0f, -0.063f, 0.0f, -0.827f, 0.421f, 1.0f, -0.057f, 0.0f, -0.83f, 0.435f, 1.0f,
		-0.051f, 0.0f, -0.833f, 0.448f, 1.0f, -0.045f, 0.0f, -0.835f, 0.46f, 1.0f, -0.039f, 0.0f, -0.837f, 0.471f, 1.0f, -0.033f, 0.0f, -0.839f, 0.481f, 1.0f,
		-0.026f, 0.0f, -0.841f, 0.491f, 1.0f, -0.019f, 0.0f, -0.842f, 0.5f, 1.0f, -0.012f, 0.0f, -0.843f, 0.508f, 1.0f, -0.005f, 0.0f, -0.843f, 0.515f, 1.0f,
		0.002f, 0.0f, -0.843f, 0.522f, 1.0f, 0.009f, 0.0f, -0.843f, 0.527f, 1.0f, 0.016f, 0.0f, -0.842f, 0.532f, 1.0f, 0.023f, 0.0f, -0.841f, 0.535f, 1.0f,
		0.03f, 0.0f, -0.839f, 0.538f, 1.0f, 0.037f, 0.0f, -0.837f, 0.538f, 1.0f, 0.044f, 0.0f, -0.835f, 0.537f, 1.0f, 0.05f, 0.0f, -0.833f, 0.532f, 1.0f,
		0.056f, 0.0f, -0.83f, 0.524f, 1.0f, 0.062f, 0.0f, -0.827f, 0.513f, 1.0f, 0.068f, 0.0f, -0.823f, 0.496f, 1.0f, 0.074f, 0.0f, -0.82f, 0.474f, 1.0f,
		0.079f, 0.0f, -0.817f, 0.446f, 1.0f, 0.084f, 0.0f, -0.813f, 0.411f, 1.0f, 0.089f, 0.0f, -0.809f, 0.37f, 1.0f, 0.093f, 0.0f, -0.806f, 0.323f, 1.0f,
		0.098f, 0.0f, -0.802f, 0.269f, 1.0f, 0.102f, 0.0f, -0.798f, 0.211f, 1.0f, 0.106f, 0.0f, -0.795f, 0.146f, 1.0f, 0.109f, 0.0f, -0.792f, 0.089f, 1.0f,
		0.114f, 0.0f, -0.787f, 0.0f, 1.0f, };
	gpencil_add_points(gps, data26, 41);

	gps = gpencil_add_stroke(frameLines, palette, color_Black, 77, "Black", 3);
	float data27[77 * 5] = { -0.105f, 0.0f, -0.259f, 0.214f, 1.0f, -0.103f, 0.0f, -0.253f, 0.263f, 1.0f, -0.101f, 0.0f, -0.249f, 0.291f, 1.0f, -0.099f, 0.0f, -0.244f, 0.324f, 1.0f,
		-0.098f, 0.0f, -0.24f, 0.351f, 1.0f, -0.096f, 0.0f, -0.235f, 0.376f, 1.0f, -0.094f, 0.0f, -0.231f, 0.397f, 1.0f, -0.092f, 0.0f, -0.227f, 0.416f, 1.0f,
		-0.09f, 0.0f, -0.222f, 0.432f, 1.0f, -0.088f, 0.0f, -0.218f, 0.446f, 1.0f, -0.086f, 0.0f, -0.215f, 0.458f, 1.0f, -0.084f, 0.0f, -0.211f, 0.469f, 1.0f,
		-0.082f, 0.0f, -0.208f, 0.478f, 1.0f, -0.079f, 0.0f, -0.205f, 0.486f, 1.0f, -0.077f, 0.0f, -0.203f, 0.494f, 1.0f, -0.075f, 0.0f, -0.2f, 0.501f, 1.0f,
		-0.073f, 0.0f, -0.198f, 0.508f, 1.0f, -0.071f, 0.0f, -0.197f, 0.515f, 1.0f, -0.068f, 0.0f, -0.195f, 0.521f, 1.0f, -0.066f, 0.0f, -0.194f, 0.528f, 1.0f,
		-0.064f, 0.0f, -0.194f, 0.534f, 1.0f, -0.061f, 0.0f, -0.194f, 0.54f, 1.0f, -0.059f, 0.0f, -0.194f, 0.546f, 1.0f, -0.056f, 0.0f, -0.194f, 0.551f, 1.0f,
		-0.054f, 0.0f, -0.195f, 0.555f, 1.0f, -0.051f, 0.0f, -0.196f, 0.559f, 1.0f, -0.049f, 0.0f, -0.198f, 0.562f, 1.0f, -0.046f, 0.0f, -0.2f, 0.565f, 1.0f,
		-0.044f, 0.0f, -0.201f, 0.567f, 1.0f, -0.041f, 0.0f, -0.204f, 0.568f, 1.0f, -0.039f, 0.0f, -0.206f, 0.569f, 1.0f, -0.036f, 0.0f, -0.208f, 0.57f, 1.0f,
		-0.034f, 0.0f, -0.21f, 0.571f, 1.0f, -0.032f, 0.0f, -0.213f, 0.571f, 1.0f, -0.029f, 0.0f, -0.215f, 0.571f, 1.0f, -0.027f, 0.0f, -0.217f, 0.572f, 1.0f,
		-0.024f, 0.0f, -0.219f, 0.572f, 1.0f, -0.022f, 0.0f, -0.221f, 0.572f, 1.0f, -0.019f, 0.0f, -0.222f, 0.572f, 1.0f, -0.016f, 0.0f, -0.224f, 0.572f, 1.0f,
		-0.013f, 0.0f, -0.225f, 0.572f, 1.0f, -0.01f, 0.0f, -0.226f, 0.573f, 1.0f, -0.007f, 0.0f, -0.227f, 0.573f, 1.0f, -0.004f, 0.0f, -0.227f, 0.573f, 1.0f,
		-0.001f, 0.0f, -0.227f, 0.574f, 1.0f, 0.002f, 0.0f, -0.227f, 0.575f, 1.0f, 0.005f, 0.0f, -0.227f, 0.576f, 1.0f, 0.008f, 0.0f, -0.226f, 0.577f, 1.0f,
		0.011f, 0.0f, -0.225f, 0.578f, 1.0f, 0.015f, 0.0f, -0.224f, 0.579f, 1.0f, 0.018f, 0.0f, -0.222f, 0.58f, 1.0f, 0.021f, 0.0f, -0.221f, 0.581f, 1.0f,
		0.024f, 0.0f, -0.219f, 0.582f, 1.0f, 0.027f, 0.0f, -0.217f, 0.582f, 1.0f, 0.03f, 0.0f, -0.215f, 0.583f, 1.0f, 0.033f, 0.0f, -0.213f, 0.583f, 1.0f,
		0.036f, 0.0f, -0.212f, 0.583f, 1.0f, 0.039f, 0.0f, -0.21f, 0.583f, 1.0f, 0.042f, 0.0f, -0.208f, 0.583f, 1.0f, 0.045f, 0.0f, -0.207f, 0.583f, 1.0f,
		0.048f, 0.0f, -0.205f, 0.583f, 1.0f, 0.051f, 0.0f, -0.204f, 0.583f, 1.0f, 0.054f, 0.0f, -0.203f, 0.583f, 1.0f, 0.058f, 0.0f, -0.203f, 0.583f, 1.0f,
		0.061f, 0.0f, -0.202f, 0.583f, 1.0f, 0.064f, 0.0f, -0.202f, 0.574f, 1.0f, 0.067f, 0.0f, -0.202f, 0.565f, 1.0f, 0.07f, 0.0f, -0.203f, 0.556f, 1.0f,
		0.073f, 0.0f, -0.203f, 0.547f, 1.0f, 0.075f, 0.0f, -0.204f, 0.515f, 1.0f, 0.078f, 0.0f, -0.204f, 0.483f, 1.0f, 0.08f, 0.0f, -0.205f, 0.451f, 1.0f,
		0.083f, 0.0f, -0.206f, 0.419f, 1.0f, 0.085f, 0.0f, -0.207f, 0.314f, 1.0f, 0.087f, 0.0f, -0.208f, 0.21f, 1.0f, 0.089f, 0.0f, -0.209f, 0.105f, 1.0f,
		0.091f, 0.0f, -0.21f, 0.0f, 1.0f, };
	gpencil_add_points(gps, data27, 77);

	gps = gpencil_add_stroke(frameLines, palette, color_Black, 257, "Black", 3);
	float data28[257 * 5] = { -0.637f, 0.0f, -0.172f, 0.0f, 1.0f, -0.641f, 0.0f, -0.172f, 0.0f, 1.0f, -0.643f, 0.0f, -0.172f, 0.0f, 1.0f, -0.646f, 0.0f, -0.172f, 0.0f, 1.0f,
		-0.65f, 0.0f, -0.172f, 0.0f, 1.0f, -0.653f, 0.0f, -0.172f, 0.0f, 1.0f, -0.657f, 0.0f, -0.172f, 0.0f, 1.0f, -0.66f, 0.0f, -0.172f, 0.0f, 1.0f,
		-0.664f, 0.0f, -0.171f, 0.0f, 1.0f, -0.668f, 0.0f, -0.171f, 0.0f, 1.0f, -0.672f, 0.0f, -0.171f, 0.0f, 1.0f, -0.677f, 0.0f, -0.171f, 0.0f, 1.0f,
		-0.681f, 0.0f, -0.171f, 0.0f, 1.0f, -0.685f, 0.0f, -0.171f, 0.0f, 1.0f, -0.69f, 0.0f, -0.17f, 0.0f, 1.0f, -0.694f, 0.0f, -0.17f, 0.0f, 1.0f,
		-0.699f, 0.0f, -0.17f, 0.0f, 1.0f, -0.704f, 0.0f, -0.169f, 0.0f, 1.0f, -0.708f, 0.0f, -0.169f, 0.0f, 1.0f, -0.713f, 0.0f, -0.168f, 0.0f, 1.0f,
		-0.717f, 0.0f, -0.168f, 0.0f, 1.0f, -0.722f, 0.0f, -0.167f, 0.0f, 1.0f, -0.727f, 0.0f, -0.167f, 0.0f, 1.0f, -0.731f, 0.0f, -0.166f, 0.0f, 1.0f,
		-0.735f, 0.0f, -0.166f, 0.0f, 1.0f, -0.74f, 0.0f, -0.165f, 0.0f, 1.0f, -0.744f, 0.0f, -0.164f, 0.0f, 1.0f, -0.749f, 0.0f, -0.163f, 0.0f, 1.0f,
		-0.753f, 0.0f, -0.163f, 0.0f, 1.0f, -0.757f, 0.0f, -0.162f, 0.0f, 1.0f, -0.761f, 0.0f, -0.161f, 0.0f, 1.0f, -0.765f, 0.0f, -0.16f, 0.0f, 1.0f,
		-0.769f, 0.0f, -0.159f, 0.0f, 1.0f, -0.773f, 0.0f, -0.158f, 0.0f, 1.0f, -0.777f, 0.0f, -0.157f, 0.0f, 1.0f, -0.781f, 0.0f, -0.156f, 0.001f, 1.0f,
		-0.785f, 0.0f, -0.155f, 0.001f, 1.0f, -0.789f, 0.0f, -0.154f, 0.002f, 1.0f, -0.793f, 0.0f, -0.153f, 0.003f, 1.0f, -0.797f, 0.0f, -0.152f, 0.004f, 1.0f,
		-0.801f, 0.0f, -0.15f, 0.005f, 1.0f, -0.805f, 0.0f, -0.149f, 0.006f, 1.0f, -0.81f, 0.0f, -0.147f, 0.008f, 1.0f, -0.814f, 0.0f, -0.146f, 0.009f, 1.0f,
		-0.818f, 0.0f, -0.144f, 0.011f, 1.0f, -0.823f, 0.0f, -0.143f, 0.014f, 1.0f, -0.827f, 0.0f, -0.141f, 0.016f, 1.0f, -0.831f, 0.0f, -0.139f, 0.019f, 1.0f,
		-0.836f, 0.0f, -0.138f, 0.022f, 1.0f, -0.84f, 0.0f, -0.136f, 0.024f, 1.0f, -0.844f, 0.0f, -0.135f, 0.026f, 1.0f, -0.849f, 0.0f, -0.133f, 0.027f, 1.0f,
		-0.853f, 0.0f, -0.131f, 0.027f, 1.0f, -0.857f, 0.0f, -0.13f, 0.027f, 1.0f, -0.861f, 0.0f, -0.128f, 0.027f, 1.0f, -0.865f, 0.0f, -0.126f, 0.027f, 1.0f,
		-0.868f, 0.0f, -0.125f, 0.026f, 1.0f, -0.872f, 0.0f, -0.123f, 0.025f, 1.0f, -0.876f, 0.0f, -0.121f, 0.025f, 1.0f, -0.879f, 0.0f, -0.119f, 0.024f, 1.0f,
		-0.883f, 0.0f, -0.118f, 0.023f, 1.0f, -0.886f, 0.0f, -0.116f, 0.022f, 1.0f, -0.89f, 0.0f, -0.114f, 0.022f, 1.0f, -0.894f, 0.0f, -0.112f, 0.021f, 1.0f,
		-0.898f, 0.0f, -0.11f, 0.022f, 1.0f, -0.901f, 0.0f, -0.107f, 0.022f, 1.0f, -0.905f, 0.0f, -0.105f, 0.024f, 1.0f, -0.909f, 0.0f, -0.103f, 0.026f, 1.0f,
		-0.913f, 0.0f, -0.1f, 0.029f, 1.0f, -0.917f, 0.0f, -0.098f, 0.032f, 1.0f, -0.921f, 0.0f, -0.095f, 0.035f, 1.0f, -0.926f, 0.0f, -0.092f, 0.039f, 1.0f,
		-0.93f, 0.0f, -0.09f, 0.043f, 1.0f, -0.934f, 0.0f, -0.087f, 0.047f, 1.0f, -0.938f, 0.0f, -0.084f, 0.051f, 1.0f, -0.942f, 0.0f, -0.081f, 0.055f, 1.0f,
		-0.946f, 0.0f, -0.078f, 0.06f, 1.0f, -0.95f, 0.0f, -0.075f, 0.065f, 1.0f, -0.954f, 0.0f, -0.073f, 0.07f, 1.0f, -0.958f, 0.0f, -0.07f, 0.075f, 1.0f,
		-0.961f, 0.0f, -0.067f, 0.081f, 1.0f, -0.965f, 0.0f, -0.064f, 0.087f, 1.0f, -0.968f, 0.0f, -0.061f, 0.092f, 1.0f, -0.972f, 0.0f, -0.058f, 0.098f, 1.0f,
		-0.975f, 0.0f, -0.055f, 0.103f, 1.0f, -0.979f, 0.0f, -0.053f, 0.108f, 1.0f, -0.982f, 0.0f, -0.05f, 0.112f, 1.0f, -0.985f, 0.0f, -0.047f, 0.116f, 1.0f,
		-0.988f, 0.0f, -0.045f, 0.12f, 1.0f, -0.991f, 0.0f, -0.042f, 0.123f, 1.0f, -0.994f, 0.0f, -0.039f, 0.126f, 1.0f, -0.997f, 0.0f, -0.037f, 0.129f, 1.0f,
		-1.0f, 0.0f, -0.034f, 0.131f, 1.0f, -1.003f, 0.0f, -0.031f, 0.133f, 1.0f, -1.005f, 0.0f, -0.029f, 0.135f, 1.0f, -1.008f, 0.0f, -0.026f, 0.137f, 1.0f,
		-1.01f, 0.0f, -0.024f, 0.139f, 1.0f, -1.013f, 0.0f, -0.021f, 0.141f, 1.0f, -1.016f, 0.0f, -0.018f, 0.143f, 1.0f, -1.018f, 0.0f, -0.016f, 0.144f, 1.0f,
		-1.02f, 0.0f, -0.013f, 0.146f, 1.0f, -1.023f, 0.0f, -0.011f, 0.148f, 1.0f, -1.025f, 0.0f, -0.008f, 0.149f, 1.0f, -1.027f, 0.0f, -0.006f, 0.151f, 1.0f,
		-1.029f, 0.0f, -0.003f, 0.152f, 1.0f, -1.032f, 0.0f, -0.001f, 0.154f, 1.0f, -1.034f, 0.0f, 0.001f, 0.154f, 1.0f, -1.036f, 0.0f, 0.004f, 0.155f, 1.0f,
		-1.038f, 0.0f, 0.006f, 0.156f, 1.0f, -1.041f, 0.0f, 0.008f, 0.156f, 1.0f, -1.043f, 0.0f, 0.01f, 0.157f, 1.0f, -1.045f, 0.0f, 0.013f, 0.157f, 1.0f,
		-1.047f, 0.0f, 0.015f, 0.157f, 1.0f, -1.049f, 0.0f, 0.018f, 0.158f, 1.0f, -1.051f, 0.0f, 0.02f, 0.158f, 1.0f, -1.053f, 0.0f, 0.023f, 0.158f, 1.0f,
		-1.055f, 0.0f, 0.025f, 0.158f, 1.0f, -1.057f, 0.0f, 0.028f, 0.158f, 1.0f, -1.059f, 0.0f, 0.03f, 0.158f, 1.0f, -1.061f, 0.0f, 0.033f, 0.158f, 1.0f,
		-1.063f, 0.0f, 0.036f, 0.158f, 1.0f, -1.065f, 0.0f, 0.038f, 0.158f, 1.0f, -1.067f, 0.0f, 0.041f, 0.158f, 1.0f, -1.069f, 0.0f, 0.044f, 0.157f, 1.0f,
		-1.071f, 0.0f, 0.047f, 0.157f, 1.0f, -1.073f, 0.0f, 0.049f, 0.156f, 1.0f, -1.074f, 0.0f, 0.052f, 0.155f, 1.0f, -1.076f, 0.0f, 0.055f, 0.154f, 1.0f,
		-1.078f, 0.0f, 0.058f, 0.153f, 1.0f, -1.08f, 0.0f, 0.061f, 0.152f, 1.0f, -1.082f, 0.0f, 0.064f, 0.15f, 1.0f, -1.083f, 0.0f, 0.067f, 0.148f, 1.0f,
		-1.085f, 0.0f, 0.07f, 0.146f, 1.0f, -1.087f, 0.0f, 0.073f, 0.144f, 1.0f, -1.089f, 0.0f, 0.076f, 0.142f, 1.0f, -1.091f, 0.0f, 0.08f, 0.14f, 1.0f,
		-1.092f, 0.0f, 0.083f, 0.138f, 1.0f, -1.094f, 0.0f, 0.086f, 0.136f, 1.0f, -1.096f, 0.0f, 0.09f, 0.135f, 1.0f, -1.097f, 0.0f, 0.093f, 0.134f, 1.0f,
		-1.099f, 0.0f, 0.096f, 0.134f, 1.0f, -1.101f, 0.0f, 0.1f, 0.134f, 1.0f, -1.103f, 0.0f, 0.103f, 0.136f, 1.0f, -1.104f, 0.0f, 0.107f, 0.139f, 1.0f,
		-1.106f, 0.0f, 0.111f, 0.144f, 1.0f, -1.107f, 0.0f, 0.114f, 0.15f, 1.0f, -1.109f, 0.0f, 0.118f, 0.158f, 1.0f, -1.11f, 0.0f, 0.122f, 0.167f, 1.0f,
		-1.111f, 0.0f, 0.126f, 0.178f, 1.0f, -1.113f, 0.0f, 0.13f, 0.191f, 1.0f, -1.114f, 0.0f, 0.134f, 0.205f, 1.0f, -1.115f, 0.0f, 0.138f, 0.22f, 1.0f,
		-1.116f, 0.0f, 0.142f, 0.237f, 1.0f, -1.117f, 0.0f, 0.146f, 0.254f, 1.0f, -1.118f, 0.0f, 0.15f, 0.272f, 1.0f, -1.119f, 0.0f, 0.155f, 0.291f, 1.0f,
		-1.119f, 0.0f, 0.159f, 0.31f, 1.0f, -1.12f, 0.0f, 0.163f, 0.329f, 1.0f, -1.121f, 0.0f, 0.167f, 0.348f, 1.0f, -1.121f, 0.0f, 0.172f, 0.367f, 1.0f,
		-1.122f, 0.0f, 0.176f, 0.386f, 1.0f, -1.122f, 0.0f, 0.18f, 0.405f, 1.0f, -1.123f, 0.0f, 0.184f, 0.423f, 1.0f, -1.123f, 0.0f, 0.189f, 0.441f, 1.0f,
		-1.124f, 0.0f, 0.193f, 0.458f, 1.0f, -1.124f, 0.0f, 0.197f, 0.475f, 1.0f, -1.124f, 0.0f, 0.202f, 0.492f, 1.0f, -1.124f, 0.0f, 0.206f, 0.508f, 1.0f,
		-1.125f, 0.0f, 0.21f, 0.524f, 1.0f, -1.125f, 0.0f, 0.214f, 0.539f, 1.0f, -1.125f, 0.0f, 0.218f, 0.554f, 1.0f, -1.124f, 0.0f, 0.223f, 0.568f, 1.0f,
		-1.124f, 0.0f, 0.227f, 0.581f, 1.0f, -1.124f, 0.0f, 0.231f, 0.593f, 1.0f, -1.124f, 0.0f, 0.235f, 0.604f, 1.0f, -1.123f, 0.0f, 0.239f, 0.614f, 1.0f,
		-1.123f, 0.0f, 0.243f, 0.624f, 1.0f, -1.122f, 0.0f, 0.247f, 0.632f, 1.0f, -1.122f, 0.0f, 0.251f, 0.64f, 1.0f, -1.121f, 0.0f, 0.255f, 0.646f, 1.0f,
		-1.121f, 0.0f, 0.258f, 0.653f, 1.0f, -1.12f, 0.0f, 0.262f, 0.658f, 1.0f, -1.119f, 0.0f, 0.266f, 0.663f, 1.0f, -1.118f, 0.0f, 0.269f, 0.668f, 1.0f,
		-1.117f, 0.0f, 0.272f, 0.673f, 1.0f, -1.117f, 0.0f, 0.276f, 0.678f, 1.0f, -1.116f, 0.0f, 0.279f, 0.682f, 1.0f, -1.115f, 0.0f, 0.282f, 0.687f, 1.0f,
		-1.113f, 0.0f, 0.285f, 0.692f, 1.0f, -1.112f, 0.0f, 0.289f, 0.697f, 1.0f, -1.111f, 0.0f, 0.292f, 0.702f, 1.0f, -1.11f, 0.0f, 0.294f, 0.708f, 1.0f,
		-1.109f, 0.0f, 0.297f, 0.713f, 1.0f, -1.108f, 0.0f, 0.3f, 0.718f, 1.0f, -1.106f, 0.0f, 0.303f, 0.724f, 1.0f, -1.105f, 0.0f, 0.306f, 0.73f, 1.0f,
		-1.104f, 0.0f, 0.309f, 0.735f, 1.0f, -1.102f, 0.0f, 0.312f, 0.741f, 1.0f, -1.101f, 0.0f, 0.315f, 0.746f, 1.0f, -1.099f, 0.0f, 0.318f, 0.751f, 1.0f,
		-1.098f, 0.0f, 0.321f, 0.756f, 1.0f, -1.096f, 0.0f, 0.323f, 0.761f, 1.0f, -1.094f, 0.0f, 0.326f, 0.766f, 1.0f, -1.093f, 0.0f, 0.329f, 0.771f, 1.0f,
		-1.091f, 0.0f, 0.332f, 0.776f, 1.0f, -1.089f, 0.0f, 0.335f, 0.781f, 1.0f, -1.087f, 0.0f, 0.338f, 0.786f, 1.0f, -1.085f, 0.0f, 0.341f, 0.791f, 1.0f,
		-1.082f, 0.0f, 0.344f, 0.797f, 1.0f, -1.08f, 0.0f, 0.347f, 0.802f, 1.0f, -1.078f, 0.0f, 0.349f, 0.808f, 1.0f, -1.075f, 0.0f, 0.352f, 0.814f, 1.0f,
		-1.072f, 0.0f, 0.355f, 0.82f, 1.0f, -1.069f, 0.0f, 0.358f, 0.826f, 1.0f, -1.066f, 0.0f, 0.36f, 0.831f, 1.0f, -1.063f, 0.0f, 0.363f, 0.837f, 1.0f,
		-1.059f, 0.0f, 0.366f, 0.842f, 1.0f, -1.055f, 0.0f, 0.368f, 0.847f, 1.0f, -1.051f, 0.0f, 0.371f, 0.851f, 1.0f, -1.047f, 0.0f, 0.373f, 0.856f, 1.0f,
		-1.042f, 0.0f, 0.375f, 0.86f, 1.0f, -1.037f, 0.0f, 0.378f, 0.863f, 1.0f, -1.031f, 0.0f, 0.38f, 0.866f, 1.0f, -1.026f, 0.0f, 0.382f, 0.869f, 1.0f,
		-1.02f, 0.0f, 0.384f, 0.871f, 1.0f, -1.014f, 0.0f, 0.386f, 0.873f, 1.0f, -1.007f, 0.0f, 0.387f, 0.875f, 1.0f, -1.0f, 0.0f, 0.389f, 0.876f, 1.0f,
		-0.994f, 0.0f, 0.39f, 0.877f, 1.0f, -0.987f, 0.0f, 0.392f, 0.878f, 1.0f, -0.979f, 0.0f, 0.393f, 0.879f, 1.0f, -0.972f, 0.0f, 0.394f, 0.88f, 1.0f,
		-0.964f, 0.0f, 0.395f, 0.881f, 1.0f, -0.956f, 0.0f, 0.395f, 0.881f, 1.0f, -0.948f, 0.0f, 0.395f, 0.882f, 1.0f, -0.94f, 0.0f, 0.395f, 0.882f, 1.0f,
		-0.932f, 0.0f, 0.395f, 0.883f, 1.0f, -0.923f, 0.0f, 0.394f, 0.883f, 1.0f, -0.915f, 0.0f, 0.393f, 0.883f, 1.0f, -0.906f, 0.0f, 0.391f, 0.883f, 1.0f,
		-0.896f, 0.0f, 0.389f, 0.881f, 1.0f, -0.887f, 0.0f, 0.386f, 0.876f, 1.0f, -0.877f, 0.0f, 0.382f, 0.866f, 1.0f, -0.867f, 0.0f, 0.378f, 0.85f, 1.0f,
		-0.857f, 0.0f, 0.373f, 0.828f, 1.0f, -0.848f, 0.0f, 0.368f, 0.799f, 1.0f, -0.838f, 0.0f, 0.363f, 0.764f, 1.0f, -0.829f, 0.0f, 0.357f, 0.723f, 1.0f,
		-0.819f, 0.0f, 0.352f, 0.679f, 1.0f, -0.811f, 0.0f, 0.347f, 0.631f, 1.0f, -0.802f, 0.0f, 0.342f, 0.579f, 1.0f, -0.794f, 0.0f, 0.338f, 0.525f, 1.0f,
		-0.786f, 0.0f, 0.333f, 0.469f, 1.0f, -0.779f, 0.0f, 0.329f, 0.412f, 1.0f, -0.772f, 0.0f, 0.325f, 0.351f, 1.0f, -0.766f, 0.0f, 0.321f, 0.3f, 1.0f,
		-0.757f, 0.0f, 0.317f, 0.219f, 1.0f, };
	gpencil_add_points(gps, data28, 257);

	gps = gpencil_add_stroke(frameLines, palette, color_Black, 205, "Black", 3);
	float data29[205 * 5] = { 0.816f, 0.0f, 0.326f, 0.285f, 1.0f, 0.819f, 0.0f, 0.328f, 0.287f, 1.0f, 0.821f, 0.0f, 0.33f, 0.29f, 1.0f, 0.823f, 0.0f, 0.331f, 0.295f, 1.0f,
		0.825f, 0.0f, 0.333f, 0.304f, 1.0f, 0.828f, 0.0f, 0.335f, 0.315f, 1.0f, 0.83f, 0.0f, 0.337f, 0.328f, 1.0f, 0.833f, 0.0f, 0.339f, 0.341f, 1.0f,
		0.836f, 0.0f, 0.341f, 0.355f, 1.0f, 0.839f, 0.0f, 0.343f, 0.368f, 1.0f, 0.842f, 0.0f, 0.345f, 0.38f, 1.0f, 0.845f, 0.0f, 0.347f, 0.392f, 1.0f,
		0.848f, 0.0f, 0.349f, 0.402f, 1.0f, 0.851f, 0.0f, 0.351f, 0.412f, 1.0f, 0.854f, 0.0f, 0.352f, 0.421f, 1.0f, 0.857f, 0.0f, 0.354f, 0.429f, 1.0f,
		0.861f, 0.0f, 0.356f, 0.437f, 1.0f, 0.865f, 0.0f, 0.357f, 0.444f, 1.0f, 0.869f, 0.0f, 0.359f, 0.452f, 1.0f, 0.872f, 0.0f, 0.36f, 0.46f, 1.0f,
		0.876f, 0.0f, 0.361f, 0.47f, 1.0f, 0.881f, 0.0f, 0.363f, 0.481f, 1.0f, 0.885f, 0.0f, 0.364f, 0.491f, 1.0f, 0.889f, 0.0f, 0.365f, 0.501f, 1.0f,
		0.893f, 0.0f, 0.366f, 0.511f, 1.0f, 0.898f, 0.0f, 0.367f, 0.52f, 1.0f, 0.902f, 0.0f, 0.368f, 0.528f, 1.0f, 0.906f, 0.0f, 0.37f, 0.535f, 1.0f,
		0.911f, 0.0f, 0.371f, 0.542f, 1.0f, 0.915f, 0.0f, 0.372f, 0.548f, 1.0f, 0.92f, 0.0f, 0.373f, 0.554f, 1.0f, 0.924f, 0.0f, 0.374f, 0.559f, 1.0f,
		0.929f, 0.0f, 0.375f, 0.564f, 1.0f, 0.933f, 0.0f, 0.376f, 0.567f, 1.0f, 0.938f, 0.0f, 0.377f, 0.57f, 1.0f, 0.943f, 0.0f, 0.378f, 0.572f, 1.0f,
		0.947f, 0.0f, 0.378f, 0.574f, 1.0f, 0.952f, 0.0f, 0.379f, 0.576f, 1.0f, 0.956f, 0.0f, 0.38f, 0.577f, 1.0f, 0.961f, 0.0f, 0.38f, 0.579f, 1.0f,
		0.966f, 0.0f, 0.381f, 0.581f, 1.0f, 0.971f, 0.0f, 0.381f, 0.585f, 1.0f, 0.975f, 0.0f, 0.382f, 0.588f, 1.0f, 0.98f, 0.0f, 0.382f, 0.591f, 1.0f,
		0.985f, 0.0f, 0.382f, 0.595f, 1.0f, 0.989f, 0.0f, 0.382f, 0.597f, 1.0f, 0.994f, 0.0f, 0.382f, 0.6f, 1.0f, 0.999f, 0.0f, 0.382f, 0.603f, 1.0f,
		1.003f, 0.0f, 0.382f, 0.605f, 1.0f, 1.008f, 0.0f, 0.381f, 0.607f, 1.0f, 1.013f, 0.0f, 0.381f, 0.61f, 1.0f, 1.017f, 0.0f, 0.381f, 0.611f, 1.0f,
		1.021f, 0.0f, 0.381f, 0.613f, 1.0f, 1.025f, 0.0f, 0.38f, 0.613f, 1.0f, 1.029f, 0.0f, 0.38f, 0.614f, 1.0f, 1.033f, 0.0f, 0.379f, 0.614f, 1.0f,
		1.037f, 0.0f, 0.379f, 0.614f, 1.0f, 1.041f, 0.0f, 0.378f, 0.614f, 1.0f, 1.044f, 0.0f, 0.378f, 0.614f, 1.0f, 1.048f, 0.0f, 0.377f, 0.614f, 1.0f,
		1.051f, 0.0f, 0.376f, 0.613f, 1.0f, 1.054f, 0.0f, 0.375f, 0.612f, 1.0f, 1.057f, 0.0f, 0.374f, 0.611f, 1.0f, 1.06f, 0.0f, 0.373f, 0.61f, 1.0f,
		1.063f, 0.0f, 0.372f, 0.609f, 1.0f, 1.066f, 0.0f, 0.371f, 0.609f, 1.0f, 1.068f, 0.0f, 0.37f, 0.608f, 1.0f, 1.071f, 0.0f, 0.368f, 0.608f, 1.0f,
		1.073f, 0.0f, 0.367f, 0.608f, 1.0f, 1.076f, 0.0f, 0.365f, 0.608f, 1.0f, 1.078f, 0.0f, 0.364f, 0.607f, 1.0f, 1.081f, 0.0f, 0.362f, 0.607f, 1.0f,
		1.083f, 0.0f, 0.36f, 0.607f, 1.0f, 1.085f, 0.0f, 0.358f, 0.606f, 1.0f, 1.087f, 0.0f, 0.356f, 0.606f, 1.0f, 1.09f, 0.0f, 0.354f, 0.606f, 1.0f,
		1.092f, 0.0f, 0.352f, 0.606f, 1.0f, 1.094f, 0.0f, 0.35f, 0.606f, 1.0f, 1.096f, 0.0f, 0.348f, 0.606f, 1.0f, 1.097f, 0.0f, 0.346f, 0.606f, 1.0f,
		1.099f, 0.0f, 0.344f, 0.606f, 1.0f, 1.101f, 0.0f, 0.341f, 0.606f, 1.0f, 1.103f, 0.0f, 0.339f, 0.606f, 1.0f, 1.104f, 0.0f, 0.337f, 0.607f, 1.0f,
		1.106f, 0.0f, 0.335f, 0.607f, 1.0f, 1.108f, 0.0f, 0.332f, 0.607f, 1.0f, 1.109f, 0.0f, 0.33f, 0.608f, 1.0f, 1.111f, 0.0f, 0.327f, 0.608f, 1.0f,
		1.113f, 0.0f, 0.324f, 0.608f, 1.0f, 1.114f, 0.0f, 0.322f, 0.609f, 1.0f, 1.116f, 0.0f, 0.319f, 0.609f, 1.0f, 1.117f, 0.0f, 0.316f, 0.609f, 1.0f,
		1.118f, 0.0f, 0.313f, 0.609f, 1.0f, 1.12f, 0.0f, 0.31f, 0.609f, 1.0f, 1.121f, 0.0f, 0.307f, 0.609f, 1.0f, 1.123f, 0.0f, 0.304f, 0.608f, 1.0f,
		1.124f, 0.0f, 0.301f, 0.608f, 1.0f, 1.125f, 0.0f, 0.297f, 0.607f, 1.0f, 1.126f, 0.0f, 0.294f, 0.606f, 1.0f, 1.127f, 0.0f, 0.29f, 0.605f, 1.0f,
		1.129f, 0.0f, 0.287f, 0.603f, 1.0f, 1.13f, 0.0f, 0.283f, 0.601f, 1.0f, 1.131f, 0.0f, 0.279f, 0.599f, 1.0f, 1.132f, 0.0f, 0.276f, 0.597f, 1.0f,
		1.132f, 0.0f, 0.272f, 0.595f, 1.0f, 1.133f, 0.0f, 0.268f, 0.593f, 1.0f, 1.134f, 0.0f, 0.264f, 0.592f, 1.0f, 1.135f, 0.0f, 0.26f, 0.591f, 1.0f,
		1.135f, 0.0f, 0.256f, 0.59f, 1.0f, 1.136f, 0.0f, 0.252f, 0.589f, 1.0f, 1.136f, 0.0f, 0.248f, 0.588f, 1.0f, 1.137f, 0.0f, 0.244f, 0.587f, 1.0f,
		1.137f, 0.0f, 0.24f, 0.586f, 1.0f, 1.138f, 0.0f, 0.236f, 0.585f, 1.0f, 1.138f, 0.0f, 0.232f, 0.584f, 1.0f, 1.138f, 0.0f, 0.228f, 0.582f, 1.0f,
		1.138f, 0.0f, 0.224f, 0.581f, 1.0f, 1.138f, 0.0f, 0.22f, 0.579f, 1.0f, 1.138f, 0.0f, 0.216f, 0.578f, 1.0f, 1.138f, 0.0f, 0.212f, 0.576f, 1.0f,
		1.138f, 0.0f, 0.208f, 0.575f, 1.0f, 1.138f, 0.0f, 0.204f, 0.573f, 1.0f, 1.137f, 0.0f, 0.2f, 0.572f, 1.0f, 1.137f, 0.0f, 0.196f, 0.571f, 1.0f,
		1.137f, 0.0f, 0.192f, 0.569f, 1.0f, 1.136f, 0.0f, 0.188f, 0.568f, 1.0f, 1.136f, 0.0f, 0.184f, 0.567f, 1.0f, 1.135f, 0.0f, 0.18f, 0.566f, 1.0f,
		1.134f, 0.0f, 0.176f, 0.565f, 1.0f, 1.133f, 0.0f, 0.172f, 0.563f, 1.0f, 1.132f, 0.0f, 0.168f, 0.561f, 1.0f, 1.131f, 0.0f, 0.164f, 0.559f, 1.0f,
		1.13f, 0.0f, 0.16f, 0.556f, 1.0f, 1.129f, 0.0f, 0.156f, 0.552f, 1.0f, 1.128f, 0.0f, 0.152f, 0.548f, 1.0f, 1.127f, 0.0f, 0.148f, 0.543f, 1.0f,
		1.126f, 0.0f, 0.144f, 0.537f, 1.0f, 1.124f, 0.0f, 0.14f, 0.53f, 1.0f, 1.123f, 0.0f, 0.136f, 0.522f, 1.0f, 1.122f, 0.0f, 0.132f, 0.514f, 1.0f,
		1.12f, 0.0f, 0.128f, 0.505f, 1.0f, 1.118f, 0.0f, 0.123f, 0.495f, 1.0f, 1.117f, 0.0f, 0.119f, 0.486f, 1.0f, 1.115f, 0.0f, 0.115f, 0.476f, 1.0f,
		1.113f, 0.0f, 0.111f, 0.466f, 1.0f, 1.111f, 0.0f, 0.107f, 0.456f, 1.0f, 1.11f, 0.0f, 0.102f, 0.446f, 1.0f, 1.108f, 0.0f, 0.098f, 0.436f, 1.0f,
		1.105f, 0.0f, 0.094f, 0.425f, 1.0f, 1.103f, 0.0f, 0.09f, 0.414f, 1.0f, 1.101f, 0.0f, 0.085f, 0.402f, 1.0f, 1.099f, 0.0f, 0.081f, 0.389f, 1.0f,
		1.096f, 0.0f, 0.077f, 0.377f, 1.0f, 1.094f, 0.0f, 0.072f, 0.364f, 1.0f, 1.091f, 0.0f, 0.068f, 0.351f, 1.0f, 1.088f, 0.0f, 0.063f, 0.338f, 1.0f,
		1.085f, 0.0f, 0.059f, 0.325f, 1.0f, 1.082f, 0.0f, 0.054f, 0.313f, 1.0f, 1.079f, 0.0f, 0.05f, 0.301f, 1.0f, 1.075f, 0.0f, 0.045f, 0.29f, 1.0f,
		1.071f, 0.0f, 0.04f, 0.281f, 1.0f, 1.067f, 0.0f, 0.035f, 0.272f, 1.0f, 1.063f, 0.0f, 0.031f, 0.266f, 1.0f, 1.059f, 0.0f, 0.026f, 0.261f, 1.0f,
		1.054f, 0.0f, 0.021f, 0.258f, 1.0f, 1.049f, 0.0f, 0.016f, 0.257f, 1.0f, 1.043f, 0.0f, 0.011f, 0.259f, 1.0f, 1.037f, 0.0f, 0.006f, 0.264f, 1.0f,
		1.031f, 0.0f, 0.0f, 0.272f, 1.0f, 1.025f, 0.0f, -0.005f, 0.283f, 1.0f, 1.018f, 0.0f, -0.01f, 0.296f, 1.0f, 1.011f, 0.0f, -0.015f, 0.313f, 1.0f,
		1.003f, 0.0f, -0.021f, 0.33f, 1.0f, 0.996f, 0.0f, -0.026f, 0.348f, 1.0f, 0.988f, 0.0f, -0.032f, 0.365f, 1.0f, 0.979f, 0.0f, -0.038f, 0.379f, 1.0f,
		0.971f, 0.0f, -0.044f, 0.389f, 1.0f, 0.962f, 0.0f, -0.05f, 0.394f, 1.0f, 0.953f, 0.0f, -0.057f, 0.392f, 1.0f, 0.944f, 0.0f, -0.063f, 0.384f, 1.0f,
		0.934f, 0.0f, -0.069f, 0.368f, 1.0f, 0.924f, 0.0f, -0.075f, 0.347f, 1.0f, 0.914f, 0.0f, -0.081f, 0.32f, 1.0f, 0.903f, 0.0f, -0.087f, 0.289f, 1.0f,
		0.893f, 0.0f, -0.092f, 0.256f, 1.0f, 0.882f, 0.0f, -0.098f, 0.223f, 1.0f, 0.871f, 0.0f, -0.103f, 0.191f, 1.0f, 0.86f, 0.0f, -0.108f, 0.162f, 1.0f,
		0.849f, 0.0f, -0.112f, 0.136f, 1.0f, 0.838f, 0.0f, -0.117f, 0.112f, 1.0f, 0.827f, 0.0f, -0.121f, 0.091f, 1.0f, 0.815f, 0.0f, -0.125f, 0.074f, 1.0f,
		0.804f, 0.0f, -0.128f, 0.059f, 1.0f, 0.793f, 0.0f, -0.132f, 0.046f, 1.0f, 0.782f, 0.0f, -0.135f, 0.036f, 1.0f, 0.771f, 0.0f, -0.138f, 0.028f, 1.0f,
		0.76f, 0.0f, -0.141f, 0.021f, 1.0f, 0.749f, 0.0f, -0.144f, 0.016f, 1.0f, 0.738f, 0.0f, -0.147f, 0.012f, 1.0f, 0.728f, 0.0f, -0.149f, 0.009f, 1.0f,
		0.718f, 0.0f, -0.152f, 0.006f, 1.0f, 0.708f, 0.0f, -0.154f, 0.004f, 1.0f, 0.699f, 0.0f, -0.157f, 0.003f, 1.0f, 0.691f, 0.0f, -0.159f, 0.002f, 1.0f,
		0.68f, 0.0f, -0.162f, 0.0f, 1.0f, };
	gpencil_add_points(gps, data29, 205);

	gps = gpencil_add_stroke(frameLines, palette, color_Black, 33, "Black", 3);
	float data30[33 * 5] = { -1.02f, 0.0f, 0.179f, 0.21f, 1.0f, -1.014f, 0.0f, 0.182f, 0.301f, 1.0f, -1.01f, 0.0f, 0.184f, 0.36f, 1.0f, -1.004f, 0.0f, 0.186f, 0.426f, 1.0f,
		-0.999f, 0.0f, 0.188f, 0.479f, 1.0f, -0.993f, 0.0f, 0.19f, 0.519f, 1.0f, -0.987f, 0.0f, 0.191f, 0.545f, 1.0f, -0.981f, 0.0f, 0.192f, 0.562f, 1.0f,
		-0.975f, 0.0f, 0.193f, 0.575f, 1.0f, -0.968f, 0.0f, 0.193f, 0.582f, 1.0f, -0.961f, 0.0f, 0.193f, 0.587f, 1.0f, -0.954f, 0.0f, 0.191f, 0.592f, 1.0f,
		-0.946f, 0.0f, 0.19f, 0.597f, 1.0f, -0.938f, 0.0f, 0.187f, 0.6f, 1.0f, -0.93f, 0.0f, 0.183f, 0.603f, 1.0f, -0.922f, 0.0f, 0.178f, 0.606f, 1.0f,
		-0.913f, 0.0f, 0.173f, 0.608f, 1.0f, -0.905f, 0.0f, 0.168f, 0.61f, 1.0f, -0.898f, 0.0f, 0.162f, 0.612f, 1.0f, -0.89f, 0.0f, 0.156f, 0.613f, 1.0f,
		-0.883f, 0.0f, 0.15f, 0.612f, 1.0f, -0.877f, 0.0f, 0.143f, 0.608f, 1.0f, -0.871f, 0.0f, 0.137f, 0.602f, 1.0f, -0.865f, 0.0f, 0.131f, 0.593f, 1.0f,
		-0.86f, 0.0f, 0.125f, 0.577f, 1.0f, -0.855f, 0.0f, 0.12f, 0.554f, 1.0f, -0.85f, 0.0f, 0.114f, 0.524f, 1.0f, -0.846f, 0.0f, 0.109f, 0.487f, 1.0f,
		-0.842f, 0.0f, 0.104f, 0.443f, 1.0f, -0.838f, 0.0f, 0.1f, 0.394f, 1.0f, -0.835f, 0.0f, 0.095f, 0.339f, 1.0f, -0.832f, 0.0f, 0.091f, 0.295f, 1.0f,
		-0.828f, 0.0f, 0.086f, 0.227f, 1.0f, };
	gpencil_add_points(gps, data30, 33);

	gps = gpencil_add_stroke(frameLines, palette, color_Black, 37, "Black", 3);
	float data31[37 * 5] = { 0.777f, 0.0f, 0.096f, 0.278f, 1.0f, 0.779f, 0.0f, 0.1f, 0.307f, 1.0f, 0.781f, 0.0f, 0.103f, 0.326f, 1.0f, 0.782f, 0.0f, 0.106f, 0.349f, 1.0f,
		0.784f, 0.0f, 0.109f, 0.372f, 1.0f, 0.786f, 0.0f, 0.112f, 0.395f, 1.0f, 0.789f, 0.0f, 0.116f, 0.418f, 1.0f, 0.791f, 0.0f, 0.119f, 0.44f, 1.0f,
		0.794f, 0.0f, 0.123f, 0.462f, 1.0f, 0.798f, 0.0f, 0.127f, 0.484f, 1.0f, 0.801f, 0.0f, 0.13f, 0.504f, 1.0f, 0.806f, 0.0f, 0.134f, 0.522f, 1.0f,
		0.81f, 0.0f, 0.138f, 0.54f, 1.0f, 0.815f, 0.0f, 0.142f, 0.556f, 1.0f, 0.82f, 0.0f, 0.146f, 0.571f, 1.0f, 0.826f, 0.0f, 0.15f, 0.584f, 1.0f,
		0.832f, 0.0f, 0.154f, 0.596f, 1.0f, 0.839f, 0.0f, 0.159f, 0.607f, 1.0f, 0.846f, 0.0f, 0.163f, 0.616f, 1.0f, 0.854f, 0.0f, 0.166f, 0.623f, 1.0f,
		0.862f, 0.0f, 0.17f, 0.628f, 1.0f, 0.87f, 0.0f, 0.174f, 0.632f, 1.0f, 0.878f, 0.0f, 0.177f, 0.632f, 1.0f, 0.887f, 0.0f, 0.18f, 0.63f, 1.0f,
		0.895f, 0.0f, 0.183f, 0.623f, 1.0f, 0.903f, 0.0f, 0.186f, 0.611f, 1.0f, 0.912f, 0.0f, 0.188f, 0.592f, 1.0f, 0.92f, 0.0f, 0.19f, 0.567f, 1.0f,
		0.928f, 0.0f, 0.192f, 0.533f, 1.0f, 0.935f, 0.0f, 0.193f, 0.492f, 1.0f, 0.943f, 0.0f, 0.194f, 0.442f, 1.0f, 0.95f, 0.0f, 0.196f, 0.385f, 1.0f,
		0.957f, 0.0f, 0.197f, 0.321f, 1.0f, 0.963f, 0.0f, 0.197f, 0.253f, 1.0f, 0.97f, 0.0f, 0.198f, 0.175f, 1.0f, 0.975f, 0.0f, 0.199f, 0.107f, 1.0f,
		0.983f, 0.0f, 0.199f, 0.0f, 1.0f, };
	gpencil_add_points(gps, data31, 37);

	gps = gpencil_add_stroke(frameLines, palette, color_Black, 201, "Black", 3);
	float data32[201 * 5] = { -0.437f, 0.0f, 0.508f, 0.0f, 1.0f, -0.435f, 0.0f, 0.51f, 0.0f, 1.0f, -0.434f, 0.0f, 0.511f, 0.0f, 1.0f, -0.432f, 0.0f, 0.512f, 0.0f, 1.0f,
		-0.43f, 0.0f, 0.513f, 0.0f, 1.0f, -0.428f, 0.0f, 0.514f, 0.001f, 1.0f, -0.426f, 0.0f, 0.515f, 0.002f, 1.0f, -0.424f, 0.0f, 0.517f, 0.004f, 1.0f,
		-0.422f, 0.0f, 0.518f, 0.007f, 1.0f, -0.42f, 0.0f, 0.519f, 0.012f, 1.0f, -0.418f, 0.0f, 0.521f, 0.018f, 1.0f, -0.416f, 0.0f, 0.522f, 0.025f, 1.0f,
		-0.414f, 0.0f, 0.523f, 0.034f, 1.0f, -0.411f, 0.0f, 0.525f, 0.043f, 1.0f, -0.409f, 0.0f, 0.526f, 0.053f, 1.0f, -0.407f, 0.0f, 0.528f, 0.063f, 1.0f,
		-0.404f, 0.0f, 0.529f, 0.073f, 1.0f, -0.402f, 0.0f, 0.531f, 0.083f, 1.0f, -0.399f, 0.0f, 0.532f, 0.092f, 1.0f, -0.396f, 0.0f, 0.534f, 0.101f, 1.0f,
		-0.394f, 0.0f, 0.535f, 0.11f, 1.0f, -0.391f, 0.0f, 0.536f, 0.118f, 1.0f, -0.388f, 0.0f, 0.538f, 0.126f, 1.0f, -0.386f, 0.0f, 0.539f, 0.133f, 1.0f,
		-0.383f, 0.0f, 0.54f, 0.14f, 1.0f, -0.38f, 0.0f, 0.542f, 0.147f, 1.0f, -0.377f, 0.0f, 0.543f, 0.153f, 1.0f, -0.374f, 0.0f, 0.544f, 0.159f, 1.0f,
		-0.37f, 0.0f, 0.545f, 0.166f, 1.0f, -0.367f, 0.0f, 0.546f, 0.172f, 1.0f, -0.364f, 0.0f, 0.547f, 0.179f, 1.0f, -0.361f, 0.0f, 0.548f, 0.186f, 1.0f,
		-0.357f, 0.0f, 0.549f, 0.193f, 1.0f, -0.354f, 0.0f, 0.55f, 0.202f, 1.0f, -0.35f, 0.0f, 0.551f, 0.211f, 1.0f, -0.347f, 0.0f, 0.552f, 0.221f, 1.0f,
		-0.343f, 0.0f, 0.552f, 0.233f, 1.0f, -0.339f, 0.0f, 0.553f, 0.245f, 1.0f, -0.336f, 0.0f, 0.553f, 0.258f, 1.0f, -0.332f, 0.0f, 0.554f, 0.272f, 1.0f,
		-0.328f, 0.0f, 0.554f, 0.286f, 1.0f, -0.324f, 0.0f, 0.554f, 0.301f, 1.0f, -0.321f, 0.0f, 0.555f, 0.317f, 1.0f, -0.317f, 0.0f, 0.555f, 0.332f, 1.0f,
		-0.313f, 0.0f, 0.555f, 0.348f, 1.0f, -0.309f, 0.0f, 0.555f, 0.364f, 1.0f, -0.305f, 0.0f, 0.555f, 0.38f, 1.0f, -0.302f, 0.0f, 0.555f, 0.396f, 1.0f,
		-0.298f, 0.0f, 0.555f, 0.411f, 1.0f, -0.294f, 0.0f, 0.555f, 0.426f, 1.0f, -0.29f, 0.0f, 0.554f, 0.44f, 1.0f, -0.287f, 0.0f, 0.554f, 0.454f, 1.0f,
		-0.283f, 0.0f, 0.554f, 0.467f, 1.0f, -0.28f, 0.0f, 0.553f, 0.479f, 1.0f, -0.276f, 0.0f, 0.553f, 0.49f, 1.0f, -0.273f, 0.0f, 0.552f, 0.5f, 1.0f,
		-0.269f, 0.0f, 0.552f, 0.51f, 1.0f, -0.266f, 0.0f, 0.551f, 0.519f, 1.0f, -0.263f, 0.0f, 0.55f, 0.527f, 1.0f, -0.26f, 0.0f, 0.549f, 0.534f, 1.0f,
		-0.256f, 0.0f, 0.549f, 0.541f, 1.0f, -0.253f, 0.0f, 0.548f, 0.547f, 1.0f, -0.25f, 0.0f, 0.547f, 0.552f, 1.0f, -0.247f, 0.0f, 0.546f, 0.557f, 1.0f,
		-0.244f, 0.0f, 0.545f, 0.561f, 1.0f, -0.241f, 0.0f, 0.544f, 0.564f, 1.0f, -0.238f, 0.0f, 0.543f, 0.567f, 1.0f, -0.235f, 0.0f, 0.542f, 0.57f, 1.0f,
		-0.233f, 0.0f, 0.541f, 0.572f, 1.0f, -0.23f, 0.0f, 0.54f, 0.574f, 1.0f, -0.227f, 0.0f, 0.539f, 0.575f, 1.0f, -0.224f, 0.0f, 0.538f, 0.576f, 1.0f,
		-0.221f, 0.0f, 0.537f, 0.577f, 1.0f, -0.219f, 0.0f, 0.535f, 0.578f, 1.0f, -0.216f, 0.0f, 0.534f, 0.578f, 1.0f, -0.213f, 0.0f, 0.533f, 0.579f, 1.0f,
		-0.211f, 0.0f, 0.532f, 0.579f, 1.0f, -0.208f, 0.0f, 0.53f, 0.579f, 1.0f, -0.206f, 0.0f, 0.529f, 0.578f, 1.0f, -0.203f, 0.0f, 0.528f, 0.578f, 1.0f,
		-0.2f, 0.0f, 0.526f, 0.577f, 1.0f, -0.198f, 0.0f, 0.525f, 0.576f, 1.0f, -0.195f, 0.0f, 0.523f, 0.575f, 1.0f, -0.193f, 0.0f, 0.522f, 0.574f, 1.0f,
		-0.19f, 0.0f, 0.52f, 0.572f, 1.0f, -0.188f, 0.0f, 0.518f, 0.571f, 1.0f, -0.185f, 0.0f, 0.517f, 0.569f, 1.0f, -0.182f, 0.0f, 0.515f, 0.568f, 1.0f,
		-0.18f, 0.0f, 0.513f, 0.567f, 1.0f, -0.177f, 0.0f, 0.512f, 0.565f, 1.0f, -0.174f, 0.0f, 0.51f, 0.564f, 1.0f, -0.172f, 0.0f, 0.508f, 0.562f, 1.0f,
		-0.169f, 0.0f, 0.506f, 0.56f, 1.0f, -0.166f, 0.0f, 0.504f, 0.559f, 1.0f, -0.164f, 0.0f, 0.502f, 0.556f, 1.0f, -0.161f, 0.0f, 0.501f, 0.554f, 1.0f,
		-0.158f, 0.0f, 0.499f, 0.552f, 1.0f, -0.155f, 0.0f, 0.497f, 0.55f, 1.0f, -0.153f, 0.0f, 0.495f, 0.547f, 1.0f, -0.15f, 0.0f, 0.493f, 0.545f, 1.0f,
		-0.147f, 0.0f, 0.491f, 0.543f, 1.0f, -0.144f, 0.0f, 0.489f, 0.54f, 1.0f, -0.142f, 0.0f, 0.487f, 0.538f, 1.0f, -0.139f, 0.0f, 0.485f, 0.536f, 1.0f,
		-0.136f, 0.0f, 0.483f, 0.533f, 1.0f, -0.133f, 0.0f, 0.481f, 0.53f, 1.0f, -0.13f, 0.0f, 0.479f, 0.527f, 1.0f, -0.127f, 0.0f, 0.477f, 0.524f, 1.0f,
		-0.124f, 0.0f, 0.475f, 0.521f, 1.0f, -0.121f, 0.0f, 0.473f, 0.519f, 1.0f, -0.118f, 0.0f, 0.471f, 0.516f, 1.0f, -0.115f, 0.0f, 0.469f, 0.514f, 1.0f,
		-0.112f, 0.0f, 0.467f, 0.511f, 1.0f, -0.109f, 0.0f, 0.465f, 0.509f, 1.0f, -0.106f, 0.0f, 0.463f, 0.506f, 1.0f, -0.103f, 0.0f, 0.461f, 0.503f, 1.0f,
		-0.099f, 0.0f, 0.458f, 0.501f, 1.0f, -0.096f, 0.0f, 0.456f, 0.5f, 1.0f, -0.093f, 0.0f, 0.454f, 0.498f, 1.0f, -0.09f, 0.0f, 0.452f, 0.497f, 1.0f,
		-0.086f, 0.0f, 0.45f, 0.496f, 1.0f, -0.083f, 0.0f, 0.448f, 0.496f, 1.0f, -0.079f, 0.0f, 0.446f, 0.495f, 1.0f, -0.076f, 0.0f, 0.444f, 0.495f, 1.0f,
		-0.072f, 0.0f, 0.442f, 0.494f, 1.0f, -0.069f, 0.0f, 0.44f, 0.494f, 1.0f, -0.065f, 0.0f, 0.438f, 0.494f, 1.0f, -0.062f, 0.0f, 0.436f, 0.494f, 1.0f,
		-0.058f, 0.0f, 0.435f, 0.494f, 1.0f, -0.054f, 0.0f, 0.433f, 0.494f, 1.0f, -0.05f, 0.0f, 0.431f, 0.494f, 1.0f, -0.046f, 0.0f, 0.43f, 0.494f, 1.0f,
		-0.042f, 0.0f, 0.428f, 0.494f, 1.0f, -0.038f, 0.0f, 0.427f, 0.494f, 1.0f, -0.033f, 0.0f, 0.426f, 0.494f, 1.0f, -0.029f, 0.0f, 0.425f, 0.494f, 1.0f,
		-0.025f, 0.0f, 0.424f, 0.494f, 1.0f, -0.02f, 0.0f, 0.423f, 0.494f, 1.0f, -0.015f, 0.0f, 0.422f, 0.494f, 1.0f, -0.011f, 0.0f, 0.422f, 0.494f, 1.0f,
		-0.006f, 0.0f, 0.421f, 0.494f, 1.0f, -0.001f, 0.0f, 0.421f, 0.495f, 1.0f, 0.004f, 0.0f, 0.421f, 0.495f, 1.0f, 0.009f, 0.0f, 0.421f, 0.495f, 1.0f,
		0.014f, 0.0f, 0.422f, 0.495f, 1.0f, 0.019f, 0.0f, 0.422f, 0.495f, 1.0f, 0.024f, 0.0f, 0.423f, 0.495f, 1.0f, 0.029f, 0.0f, 0.424f, 0.495f, 1.0f,
		0.034f, 0.0f, 0.426f, 0.495f, 1.0f, 0.039f, 0.0f, 0.427f, 0.495f, 1.0f, 0.044f, 0.0f, 0.429f, 0.496f, 1.0f, 0.049f, 0.0f, 0.43f, 0.497f, 1.0f,
		0.054f, 0.0f, 0.432f, 0.498f, 1.0f, 0.059f, 0.0f, 0.435f, 0.5f, 1.0f, 0.064f, 0.0f, 0.438f, 0.502f, 1.0f, 0.069f, 0.0f, 0.44f, 0.506f, 1.0f,
		0.074f, 0.0f, 0.443f, 0.51f, 1.0f, 0.08f, 0.0f, 0.446f, 0.516f, 1.0f, 0.085f, 0.0f, 0.45f, 0.522f, 1.0f, 0.09f, 0.0f, 0.453f, 0.528f, 1.0f,
		0.095f, 0.0f, 0.456f, 0.533f, 1.0f, 0.101f, 0.0f, 0.46f, 0.537f, 1.0f, 0.107f, 0.0f, 0.463f, 0.539f, 1.0f, 0.112f, 0.0f, 0.467f, 0.542f, 1.0f,
		0.118f, 0.0f, 0.471f, 0.543f, 1.0f, 0.124f, 0.0f, 0.475f, 0.545f, 1.0f, 0.13f, 0.0f, 0.478f, 0.546f, 1.0f, 0.137f, 0.0f, 0.482f, 0.546f, 1.0f,
		0.143f, 0.0f, 0.486f, 0.547f, 1.0f, 0.149f, 0.0f, 0.49f, 0.546f, 1.0f, 0.156f, 0.0f, 0.493f, 0.544f, 1.0f, 0.163f, 0.0f, 0.497f, 0.54f, 1.0f,
		0.17f, 0.0f, 0.5f, 0.533f, 1.0f, 0.176f, 0.0f, 0.503f, 0.525f, 1.0f, 0.183f, 0.0f, 0.507f, 0.515f, 1.0f, 0.191f, 0.0f, 0.509f, 0.503f, 1.0f,
		0.198f, 0.0f, 0.512f, 0.491f, 1.0f, 0.205f, 0.0f, 0.515f, 0.477f, 1.0f, 0.214f, 0.0f, 0.518f, 0.462f, 1.0f, 0.222f, 0.0f, 0.521f, 0.445f, 1.0f,
		0.23f, 0.0f, 0.524f, 0.427f, 1.0f, 0.238f, 0.0f, 0.527f, 0.409f, 1.0f, 0.245f, 0.0f, 0.529f, 0.388f, 1.0f, 0.254f, 0.0f, 0.531f, 0.366f, 1.0f,
		0.262f, 0.0f, 0.532f, 0.343f, 1.0f, 0.272f, 0.0f, 0.533f, 0.317f, 1.0f, 0.282f, 0.0f, 0.534f, 0.289f, 1.0f, 0.292f, 0.0f, 0.535f, 0.258f, 1.0f,
		0.301f, 0.0f, 0.535f, 0.224f, 1.0f, 0.311f, 0.0f, 0.536f, 0.189f, 1.0f, 0.32f, 0.0f, 0.536f, 0.153f, 1.0f, 0.328f, 0.0f, 0.536f, 0.117f, 1.0f,
		0.338f, 0.0f, 0.537f, 0.084f, 1.0f, 0.346f, 0.0f, 0.537f, 0.057f, 1.0f, 0.353f, 0.0f, 0.536f, 0.037f, 1.0f, 0.361f, 0.0f, 0.536f, 0.022f, 1.0f,
		0.37f, 0.0f, 0.537f, 0.013f, 1.0f, 0.376f, 0.0f, 0.536f, 0.007f, 1.0f, 0.384f, 0.0f, 0.536f, 0.004f, 1.0f, 0.39f, 0.0f, 0.536f, 0.002f, 1.0f,
		0.399f, 0.0f, 0.535f, 0.0f, 1.0f, };
	gpencil_add_points(gps, data32, 201);

	gps = gpencil_add_stroke(frameLines, palette, color_Pupils, 69, "Pupils", 3);
	float data33[69 * 5] = { -0.308f, 0.0f, 0.151f, 0.363f, 1.0f, -0.31f, 0.0f, 0.15f, 0.377f, 1.0f, -0.311f, 0.0f, 0.149f, 0.386f, 1.0f, -0.313f, 0.0f, 0.149f, 0.397f, 1.0f,
		-0.314f, 0.0f, 0.149f, 0.408f, 1.0f, -0.316f, 0.0f, 0.148f, 0.42f, 1.0f, -0.318f, 0.0f, 0.148f, 0.431f, 1.0f, -0.32f, 0.0f, 0.148f, 0.443f, 1.0f,
		-0.322f, 0.0f, 0.148f, 0.455f, 1.0f, -0.325f, 0.0f, 0.149f, 0.467f, 1.0f, -0.327f, 0.0f, 0.149f, 0.478f, 1.0f, -0.33f, 0.0f, 0.151f, 0.49f, 1.0f,
		-0.333f, 0.0f, 0.152f, 0.501f, 1.0f, -0.336f, 0.0f, 0.154f, 0.512f, 1.0f, -0.34f, 0.0f, 0.157f, 0.522f, 1.0f, -0.343f, 0.0f, 0.161f, 0.533f, 1.0f,
		-0.346f, 0.0f, 0.166f, 0.543f, 1.0f, -0.349f, 0.0f, 0.171f, 0.553f, 1.0f, -0.351f, 0.0f, 0.178f, 0.563f, 1.0f, -0.352f, 0.0f, 0.186f, 0.572f, 1.0f,
		-0.353f, 0.0f, 0.193f, 0.582f, 1.0f, -0.352f, 0.0f, 0.2f, 0.591f, 1.0f, -0.351f, 0.0f, 0.206f, 0.6f, 1.0f, -0.349f, 0.0f, 0.211f, 0.608f, 1.0f,
		-0.347f, 0.0f, 0.215f, 0.616f, 1.0f, -0.345f, 0.0f, 0.219f, 0.623f, 1.0f, -0.343f, 0.0f, 0.222f, 0.63f, 1.0f, -0.341f, 0.0f, 0.224f, 0.637f, 1.0f,
		-0.339f, 0.0f, 0.226f, 0.642f, 1.0f, -0.337f, 0.0f, 0.228f, 0.647f, 1.0f, -0.335f, 0.0f, 0.229f, 0.652f, 1.0f, -0.333f, 0.0f, 0.23f, 0.656f, 1.0f,
		-0.332f, 0.0f, 0.231f, 0.66f, 1.0f, -0.33f, 0.0f, 0.232f, 0.663f, 1.0f, -0.328f, 0.0f, 0.232f, 0.666f, 1.0f, -0.327f, 0.0f, 0.233f, 0.669f, 1.0f,
		-0.325f, 0.0f, 0.233f, 0.672f, 1.0f, -0.324f, 0.0f, 0.234f, 0.676f, 1.0f, -0.322f, 0.0f, 0.234f, 0.679f, 1.0f, -0.321f, 0.0f, 0.234f, 0.682f, 1.0f,
		-0.319f, 0.0f, 0.234f, 0.686f, 1.0f, -0.317f, 0.0f, 0.234f, 0.689f, 1.0f, -0.316f, 0.0f, 0.234f, 0.693f, 1.0f, -0.314f, 0.0f, 0.234f, 0.697f, 1.0f,
		-0.312f, 0.0f, 0.233f, 0.701f, 1.0f, -0.31f, 0.0f, 0.232f, 0.705f, 1.0f, -0.307f, 0.0f, 0.231f, 0.709f, 1.0f, -0.305f, 0.0f, 0.23f, 0.713f, 1.0f,
		-0.302f, 0.0f, 0.228f, 0.716f, 1.0f, -0.299f, 0.0f, 0.225f, 0.719f, 1.0f, -0.295f, 0.0f, 0.222f, 0.722f, 1.0f, -0.292f, 0.0f, 0.217f, 0.725f, 1.0f,
		-0.289f, 0.0f, 0.21f, 0.727f, 1.0f, -0.287f, 0.0f, 0.202f, 0.728f, 1.0f, -0.285f, 0.0f, 0.194f, 0.729f, 1.0f, -0.286f, 0.0f, 0.185f, 0.729f, 1.0f,
		-0.287f, 0.0f, 0.178f, 0.728f, 1.0f, -0.289f, 0.0f, 0.171f, 0.726f, 1.0f, -0.292f, 0.0f, 0.166f, 0.723f, 1.0f, -0.294f, 0.0f, 0.162f, 0.717f, 1.0f,
		-0.297f, 0.0f, 0.159f, 0.71f, 1.0f, -0.299f, 0.0f, 0.157f, 0.701f, 1.0f, -0.301f, 0.0f, 0.155f, 0.689f, 1.0f, -0.303f, 0.0f, 0.154f, 0.675f, 1.0f,
		-0.305f, 0.0f, 0.152f, 0.659f, 1.0f, -0.306f, 0.0f, 0.151f, 0.641f, 1.0f, -0.308f, 0.0f, 0.151f, 0.62f, 1.0f, -0.309f, 0.0f, 0.15f, 0.602f, 1.0f,
		-0.31f, 0.0f, 0.15f, 0.572f, 1.0f, };
	gpencil_add_points(gps, data33, 69);

	gps = gpencil_add_stroke(frameLines, palette, color_Pupils, 57, "Pupils", 3);
	float data34[57 * 5] = { 0.302f, 0.0f, 0.166f, 0.25f, 1.0f, 0.301f, 0.0f, 0.167f, 0.319f, 1.0f, 0.3f, 0.0f, 0.167f, 0.363f, 1.0f, 0.299f, 0.0f, 0.167f, 0.414f, 1.0f,
		0.298f, 0.0f, 0.167f, 0.459f, 1.0f, 0.296f, 0.0f, 0.168f, 0.501f, 1.0f, 0.295f, 0.0f, 0.168f, 0.539f, 1.0f, 0.293f, 0.0f, 0.169f, 0.573f, 1.0f,
		0.291f, 0.0f, 0.17f, 0.603f, 1.0f, 0.289f, 0.0f, 0.171f, 0.629f, 1.0f, 0.286f, 0.0f, 0.173f, 0.652f, 1.0f, 0.283f, 0.0f, 0.176f, 0.672f, 1.0f,
		0.279f, 0.0f, 0.18f, 0.69f, 1.0f, 0.276f, 0.0f, 0.186f, 0.705f, 1.0f, 0.272f, 0.0f, 0.195f, 0.719f, 1.0f, 0.271f, 0.0f, 0.205f, 0.73f, 1.0f,
		0.272f, 0.0f, 0.217f, 0.741f, 1.0f, 0.275f, 0.0f, 0.227f, 0.75f, 1.0f, 0.279f, 0.0f, 0.234f, 0.758f, 1.0f, 0.283f, 0.0f, 0.24f, 0.765f, 1.0f,
		0.287f, 0.0f, 0.243f, 0.771f, 1.0f, 0.291f, 0.0f, 0.245f, 0.776f, 1.0f, 0.294f, 0.0f, 0.247f, 0.781f, 1.0f, 0.296f, 0.0f, 0.248f, 0.785f, 1.0f,
		0.299f, 0.0f, 0.249f, 0.789f, 1.0f, 0.301f, 0.0f, 0.249f, 0.793f, 1.0f, 0.303f, 0.0f, 0.249f, 0.796f, 1.0f, 0.305f, 0.0f, 0.25f, 0.799f, 1.0f,
		0.306f, 0.0f, 0.25f, 0.802f, 1.0f, 0.308f, 0.0f, 0.249f, 0.805f, 1.0f, 0.31f, 0.0f, 0.249f, 0.808f, 1.0f, 0.311f, 0.0f, 0.249f, 0.81f, 1.0f,
		0.313f, 0.0f, 0.249f, 0.813f, 1.0f, 0.314f, 0.0f, 0.248f, 0.816f, 1.0f, 0.316f, 0.0f, 0.248f, 0.819f, 1.0f, 0.317f, 0.0f, 0.247f, 0.822f, 1.0f,
		0.319f, 0.0f, 0.246f, 0.825f, 1.0f, 0.321f, 0.0f, 0.245f, 0.828f, 1.0f, 0.323f, 0.0f, 0.244f, 0.832f, 1.0f, 0.325f, 0.0f, 0.243f, 0.835f, 1.0f,
		0.328f, 0.0f, 0.24f, 0.838f, 1.0f, 0.33f, 0.0f, 0.237f, 0.841f, 1.0f, 0.333f, 0.0f, 0.233f, 0.844f, 1.0f, 0.337f, 0.0f, 0.228f, 0.847f, 1.0f,
		0.339f, 0.0f, 0.219f, 0.849f, 1.0f, 0.341f, 0.0f, 0.209f, 0.852f, 1.0f, 0.34f, 0.0f, 0.197f, 0.854f, 1.0f, 0.336f, 0.0f, 0.186f, 0.856f, 1.0f,
		0.331f, 0.0f, 0.178f, 0.858f, 1.0f, 0.325f, 0.0f, 0.173f, 0.86f, 1.0f, 0.321f, 0.0f, 0.17f, 0.861f, 1.0f, 0.318f, 0.0f, 0.169f, 0.862f, 1.0f,
		0.315f, 0.0f, 0.168f, 0.864f, 1.0f, 0.312f, 0.0f, 0.167f, 0.865f, 1.0f, 0.311f, 0.0f, 0.167f, 0.866f, 1.0f, 0.309f, 0.0f, 0.166f, 0.867f, 1.0f,
		0.308f, 0.0f, 0.166f, 0.868f, 1.0f, };
	gpencil_add_points(gps, data34, 57);

	gps = gpencil_add_stroke(frameLines, palette, color_Black, 261, "Black", 3);
	float data35[261 * 5] = { -0.685f, 0.0f, 0.408f, 0.0f, 1.0f, -0.683f, 0.0f, 0.41f, 0.023f, 1.0f, -0.681f, 0.0f, 0.412f, 0.051f, 1.0f, -0.679f, 0.0f, 0.414f, 0.092f, 1.0f,
		-0.678f, 0.0f, 0.415f, 0.125f, 1.0f, -0.676f, 0.0f, 0.417f, 0.149f, 1.0f, -0.674f, 0.0f, 0.419f, 0.167f, 1.0f, -0.672f, 0.0f, 0.42f, 0.183f, 1.0f,
		-0.67f, 0.0f, 0.422f, 0.199f, 1.0f, -0.668f, 0.0f, 0.424f, 0.218f, 1.0f, -0.666f, 0.0f, 0.426f, 0.237f, 1.0f, -0.664f, 0.0f, 0.429f, 0.257f, 1.0f,
		-0.661f, 0.0f, 0.431f, 0.275f, 1.0f, -0.659f, 0.0f, 0.434f, 0.291f, 1.0f, -0.657f, 0.0f, 0.436f, 0.305f, 1.0f, -0.655f, 0.0f, 0.439f, 0.315f, 1.0f,
		-0.653f, 0.0f, 0.442f, 0.322f, 1.0f, -0.65f, 0.0f, 0.444f, 0.327f, 1.0f, -0.648f, 0.0f, 0.447f, 0.331f, 1.0f, -0.646f, 0.0f, 0.45f, 0.334f, 1.0f,
		-0.643f, 0.0f, 0.453f, 0.334f, 1.0f, -0.641f, 0.0f, 0.456f, 0.334f, 1.0f, -0.639f, 0.0f, 0.459f, 0.334f, 1.0f, -0.636f, 0.0f, 0.462f, 0.333f, 1.0f,
		-0.634f, 0.0f, 0.466f, 0.332f, 1.0f, -0.631f, 0.0f, 0.469f, 0.332f, 1.0f, -0.628f, 0.0f, 0.473f, 0.332f, 1.0f, -0.625f, 0.0f, 0.476f, 0.333f, 1.0f,
		-0.622f, 0.0f, 0.48f, 0.335f, 1.0f, -0.618f, 0.0f, 0.483f, 0.338f, 1.0f, -0.615f, 0.0f, 0.488f, 0.342f, 1.0f, -0.611f, 0.0f, 0.492f, 0.347f, 1.0f,
		-0.608f, 0.0f, 0.495f, 0.352f, 1.0f, -0.605f, 0.0f, 0.5f, 0.358f, 1.0f, -0.601f, 0.0f, 0.505f, 0.363f, 1.0f, -0.597f, 0.0f, 0.509f, 0.366f, 1.0f,
		-0.593f, 0.0f, 0.514f, 0.367f, 1.0f, -0.589f, 0.0f, 0.518f, 0.367f, 1.0f, -0.585f, 0.0f, 0.522f, 0.369f, 1.0f, -0.582f, 0.0f, 0.526f, 0.372f, 1.0f,
		-0.578f, 0.0f, 0.531f, 0.376f, 1.0f, -0.575f, 0.0f, 0.535f, 0.382f, 1.0f, -0.571f, 0.0f, 0.539f, 0.388f, 1.0f, -0.567f, 0.0f, 0.543f, 0.394f, 1.0f,
		-0.563f, 0.0f, 0.547f, 0.4f, 1.0f, -0.56f, 0.0f, 0.551f, 0.406f, 1.0f, -0.556f, 0.0f, 0.555f, 0.411f, 1.0f, -0.552f, 0.0f, 0.559f, 0.415f, 1.0f,
		-0.548f, 0.0f, 0.563f, 0.418f, 1.0f, -0.544f, 0.0f, 0.566f, 0.419f, 1.0f, -0.54f, 0.0f, 0.569f, 0.42f, 1.0f, -0.537f, 0.0f, 0.572f, 0.421f, 1.0f,
		-0.533f, 0.0f, 0.576f, 0.421f, 1.0f, -0.529f, 0.0f, 0.579f, 0.421f, 1.0f, -0.526f, 0.0f, 0.582f, 0.422f, 1.0f, -0.523f, 0.0f, 0.585f, 0.422f, 1.0f,
		-0.52f, 0.0f, 0.588f, 0.423f, 1.0f, -0.516f, 0.0f, 0.591f, 0.426f, 1.0f, -0.513f, 0.0f, 0.594f, 0.43f, 1.0f, -0.51f, 0.0f, 0.597f, 0.435f, 1.0f,
		-0.507f, 0.0f, 0.6f, 0.441f, 1.0f, -0.504f, 0.0f, 0.603f, 0.447f, 1.0f, -0.501f, 0.0f, 0.606f, 0.453f, 1.0f, -0.498f, 0.0f, 0.609f, 0.458f, 1.0f,
		-0.496f, 0.0f, 0.611f, 0.461f, 1.0f, -0.493f, 0.0f, 0.614f, 0.465f, 1.0f, -0.49f, 0.0f, 0.616f, 0.468f, 1.0f, -0.487f, 0.0f, 0.619f, 0.472f, 1.0f,
		-0.484f, 0.0f, 0.621f, 0.476f, 1.0f, -0.482f, 0.0f, 0.624f, 0.48f, 1.0f, -0.479f, 0.0f, 0.627f, 0.484f, 1.0f, -0.476f, 0.0f, 0.629f, 0.487f, 1.0f,
		-0.473f, 0.0f, 0.632f, 0.491f, 1.0f, -0.471f, 0.0f, 0.634f, 0.495f, 1.0f, -0.468f, 0.0f, 0.637f, 0.499f, 1.0f, -0.465f, 0.0f, 0.639f, 0.504f, 1.0f,
		-0.462f, 0.0f, 0.641f, 0.508f, 1.0f, -0.459f, 0.0f, 0.643f, 0.513f, 1.0f, -0.456f, 0.0f, 0.646f, 0.519f, 1.0f, -0.453f, 0.0f, 0.648f, 0.525f, 1.0f,
		-0.45f, 0.0f, 0.65f, 0.533f, 1.0f, -0.447f, 0.0f, 0.652f, 0.54f, 1.0f, -0.444f, 0.0f, 0.655f, 0.546f, 1.0f, -0.441f, 0.0f, 0.657f, 0.553f, 1.0f,
		-0.438f, 0.0f, 0.659f, 0.56f, 1.0f, -0.435f, 0.0f, 0.662f, 0.567f, 1.0f, -0.432f, 0.0f, 0.664f, 0.574f, 1.0f, -0.429f, 0.0f, 0.666f, 0.58f, 1.0f,
		-0.426f, 0.0f, 0.669f, 0.585f, 1.0f, -0.423f, 0.0f, 0.671f, 0.591f, 1.0f, -0.419f, 0.0f, 0.673f, 0.595f, 1.0f, -0.416f, 0.0f, 0.675f, 0.6f, 1.0f,
		-0.412f, 0.0f, 0.678f, 0.604f, 1.0f, -0.409f, 0.0f, 0.68f, 0.609f, 1.0f, -0.405f, 0.0f, 0.682f, 0.613f, 1.0f, -0.401f, 0.0f, 0.684f, 0.618f, 1.0f,
		-0.398f, 0.0f, 0.687f, 0.622f, 1.0f, -0.394f, 0.0f, 0.689f, 0.627f, 1.0f, -0.39f, 0.0f, 0.692f, 0.632f, 1.0f, -0.386f, 0.0f, 0.694f, 0.638f, 1.0f,
		-0.381f, 0.0f, 0.697f, 0.643f, 1.0f, -0.377f, 0.0f, 0.7f, 0.649f, 1.0f, -0.373f, 0.0f, 0.702f, 0.654f, 1.0f, -0.368f, 0.0f, 0.705f, 0.659f, 1.0f,
		-0.363f, 0.0f, 0.707f, 0.663f, 1.0f, -0.359f, 0.0f, 0.71f, 0.667f, 1.0f, -0.354f, 0.0f, 0.712f, 0.671f, 1.0f, -0.349f, 0.0f, 0.715f, 0.674f, 1.0f,
		-0.345f, 0.0f, 0.717f, 0.677f, 1.0f, -0.34f, 0.0f, 0.72f, 0.68f, 1.0f, -0.335f, 0.0f, 0.722f, 0.683f, 1.0f, -0.33f, 0.0f, 0.725f, 0.685f, 1.0f,
		-0.326f, 0.0f, 0.727f, 0.687f, 1.0f, -0.321f, 0.0f, 0.73f, 0.689f, 1.0f, -0.316f, 0.0f, 0.732f, 0.691f, 1.0f, -0.312f, 0.0f, 0.734f, 0.693f, 1.0f,
		-0.307f, 0.0f, 0.736f, 0.694f, 1.0f, -0.302f, 0.0f, 0.738f, 0.696f, 1.0f, -0.298f, 0.0f, 0.74f, 0.697f, 1.0f, -0.293f, 0.0f, 0.741f, 0.698f, 1.0f,
		-0.288f, 0.0f, 0.743f, 0.699f, 1.0f, -0.284f, 0.0f, 0.745f, 0.699f, 1.0f, -0.279f, 0.0f, 0.746f, 0.7f, 1.0f, -0.275f, 0.0f, 0.748f, 0.701f, 1.0f,
		-0.27f, 0.0f, 0.749f, 0.702f, 1.0f, -0.265f, 0.0f, 0.751f, 0.702f, 1.0f, -0.261f, 0.0f, 0.752f, 0.704f, 1.0f, -0.256f, 0.0f, 0.753f, 0.705f, 1.0f,
		-0.252f, 0.0f, 0.755f, 0.706f, 1.0f, -0.247f, 0.0f, 0.756f, 0.707f, 1.0f, -0.242f, 0.0f, 0.757f, 0.709f, 1.0f, -0.237f, 0.0f, 0.758f, 0.711f, 1.0f,
		-0.233f, 0.0f, 0.759f, 0.713f, 1.0f, -0.228f, 0.0f, 0.761f, 0.715f, 1.0f, -0.223f, 0.0f, 0.762f, 0.717f, 1.0f, -0.218f, 0.0f, 0.763f, 0.719f, 1.0f,
		-0.213f, 0.0f, 0.764f, 0.721f, 1.0f, -0.209f, 0.0f, 0.765f, 0.723f, 1.0f, -0.204f, 0.0f, 0.765f, 0.726f, 1.0f, -0.199f, 0.0f, 0.766f, 0.728f, 1.0f,
		-0.194f, 0.0f, 0.767f, 0.73f, 1.0f, -0.189f, 0.0f, 0.768f, 0.731f, 1.0f, -0.183f, 0.0f, 0.769f, 0.733f, 1.0f, -0.178f, 0.0f, 0.77f, 0.735f, 1.0f,
		-0.173f, 0.0f, 0.77f, 0.736f, 1.0f, -0.168f, 0.0f, 0.771f, 0.738f, 1.0f, -0.163f, 0.0f, 0.772f, 0.739f, 1.0f, -0.158f, 0.0f, 0.772f, 0.741f, 1.0f,
		-0.152f, 0.0f, 0.773f, 0.742f, 1.0f, -0.147f, 0.0f, 0.774f, 0.744f, 1.0f, -0.142f, 0.0f, 0.774f, 0.746f, 1.0f, -0.137f, 0.0f, 0.775f, 0.748f, 1.0f,
		-0.132f, 0.0f, 0.775f, 0.749f, 1.0f, -0.127f, 0.0f, 0.776f, 0.751f, 1.0f, -0.122f, 0.0f, 0.776f, 0.752f, 1.0f, -0.117f, 0.0f, 0.776f, 0.753f, 1.0f,
		-0.112f, 0.0f, 0.777f, 0.754f, 1.0f, -0.108f, 0.0f, 0.777f, 0.755f, 1.0f, -0.103f, 0.0f, 0.777f, 0.755f, 1.0f, -0.099f, 0.0f, 0.777f, 0.756f, 1.0f,
		-0.095f, 0.0f, 0.778f, 0.757f, 1.0f, -0.09f, 0.0f, 0.778f, 0.758f, 1.0f, -0.086f, 0.0f, 0.778f, 0.759f, 1.0f, -0.082f, 0.0f, 0.778f, 0.759f, 1.0f,
		-0.077f, 0.0f, 0.778f, 0.76f, 1.0f, -0.073f, 0.0f, 0.779f, 0.76f, 1.0f, -0.069f, 0.0f, 0.779f, 0.761f, 1.0f, -0.064f, 0.0f, 0.779f, 0.761f, 1.0f,
		-0.06f, 0.0f, 0.779f, 0.761f, 1.0f, -0.055f, 0.0f, 0.78f, 0.762f, 1.0f, -0.051f, 0.0f, 0.78f, 0.762f, 1.0f, -0.046f, 0.0f, 0.78f, 0.762f, 1.0f,
		-0.041f, 0.0f, 0.78f, 0.762f, 1.0f, -0.037f, 0.0f, 0.781f, 0.762f, 1.0f, -0.032f, 0.0f, 0.781f, 0.763f, 1.0f, -0.027f, 0.0f, 0.781f, 0.763f, 1.0f,
		-0.022f, 0.0f, 0.781f, 0.763f, 1.0f, -0.017f, 0.0f, 0.781f, 0.764f, 1.0f, -0.012f, 0.0f, 0.782f, 0.764f, 1.0f, -0.006f, 0.0f, 0.782f, 0.764f, 1.0f,
		-0.001f, 0.0f, 0.782f, 0.765f, 1.0f, 0.004f, 0.0f, 0.782f, 0.766f, 1.0f, 0.009f, 0.0f, 0.782f, 0.766f, 1.0f, 0.015f, 0.0f, 0.782f, 0.767f, 1.0f,
		0.02f, 0.0f, 0.782f, 0.768f, 1.0f, 0.025f, 0.0f, 0.782f, 0.769f, 1.0f, 0.031f, 0.0f, 0.782f, 0.77f, 1.0f, 0.036f, 0.0f, 0.782f, 0.771f, 1.0f,
		0.042f, 0.0f, 0.782f, 0.772f, 1.0f, 0.048f, 0.0f, 0.782f, 0.773f, 1.0f, 0.053f, 0.0f, 0.782f, 0.774f, 1.0f, 0.059f, 0.0f, 0.782f, 0.775f, 1.0f,
		0.065f, 0.0f, 0.782f, 0.775f, 1.0f, 0.07f, 0.0f, 0.782f, 0.776f, 1.0f, 0.076f, 0.0f, 0.782f, 0.776f, 1.0f, 0.082f, 0.0f, 0.782f, 0.776f, 1.0f,
		0.088f, 0.0f, 0.782f, 0.776f, 1.0f, 0.094f, 0.0f, 0.782f, 0.777f, 1.0f, 0.1f, 0.0f, 0.781f, 0.777f, 1.0f, 0.106f, 0.0f, 0.781f, 0.778f, 1.0f,
		0.111f, 0.0f, 0.781f, 0.779f, 1.0f, 0.117f, 0.0f, 0.781f, 0.779f, 1.0f, 0.123f, 0.0f, 0.781f, 0.78f, 1.0f, 0.129f, 0.0f, 0.78f, 0.78f, 1.0f,
		0.135f, 0.0f, 0.78f, 0.781f, 1.0f, 0.141f, 0.0f, 0.779f, 0.781f, 1.0f, 0.147f, 0.0f, 0.779f, 0.782f, 1.0f, 0.153f, 0.0f, 0.778f, 0.783f, 1.0f,
		0.159f, 0.0f, 0.777f, 0.784f, 1.0f, 0.165f, 0.0f, 0.776f, 0.785f, 1.0f, 0.171f, 0.0f, 0.775f, 0.786f, 1.0f, 0.178f, 0.0f, 0.774f, 0.787f, 1.0f,
		0.185f, 0.0f, 0.773f, 0.788f, 1.0f, 0.192f, 0.0f, 0.772f, 0.789f, 1.0f, 0.2f, 0.0f, 0.771f, 0.79f, 1.0f, 0.208f, 0.0f, 0.77f, 0.791f, 1.0f,
		0.218f, 0.0f, 0.768f, 0.793f, 1.0f, 0.228f, 0.0f, 0.766f, 0.796f, 1.0f, 0.239f, 0.0f, 0.764f, 0.799f, 1.0f, 0.25f, 0.0f, 0.762f, 0.802f, 1.0f,
		0.261f, 0.0f, 0.759f, 0.806f, 1.0f, 0.271f, 0.0f, 0.755f, 0.81f, 1.0f, 0.282f, 0.0f, 0.752f, 0.815f, 1.0f, 0.293f, 0.0f, 0.748f, 0.819f, 1.0f,
		0.304f, 0.0f, 0.744f, 0.825f, 1.0f, 0.315f, 0.0f, 0.74f, 0.83f, 1.0f, 0.326f, 0.0f, 0.736f, 0.836f, 1.0f, 0.337f, 0.0f, 0.731f, 0.843f, 1.0f,
		0.349f, 0.0f, 0.727f, 0.85f, 1.0f, 0.361f, 0.0f, 0.722f, 0.858f, 1.0f, 0.372f, 0.0f, 0.718f, 0.866f, 1.0f, 0.384f, 0.0f, 0.712f, 0.874f, 1.0f,
		0.395f, 0.0f, 0.706f, 0.882f, 1.0f, 0.407f, 0.0f, 0.7f, 0.89f, 1.0f, 0.418f, 0.0f, 0.693f, 0.898f, 1.0f, 0.43f, 0.0f, 0.685f, 0.905f, 1.0f,
		0.442f, 0.0f, 0.677f, 0.912f, 1.0f, 0.458f, 0.0f, 0.666f, 0.918f, 1.0f, 0.473f, 0.0f, 0.654f, 0.924f, 1.0f, 0.49f, 0.0f, 0.64f, 0.93f, 1.0f,
		0.506f, 0.0f, 0.625f, 0.935f, 1.0f, 0.522f, 0.0f, 0.611f, 0.939f, 1.0f, 0.538f, 0.0f, 0.596f, 0.941f, 1.0f, 0.554f, 0.0f, 0.58f, 0.942f, 1.0f,
		0.569f, 0.0f, 0.564f, 0.941f, 1.0f, 0.584f, 0.0f, 0.548f, 0.935f, 1.0f, 0.598f, 0.0f, 0.533f, 0.925f, 1.0f, 0.612f, 0.0f, 0.517f, 0.91f, 1.0f,
		0.625f, 0.0f, 0.501f, 0.891f, 1.0f, 0.638f, 0.0f, 0.484f, 0.868f, 1.0f, 0.65f, 0.0f, 0.468f, 0.839f, 1.0f, 0.662f, 0.0f, 0.452f, 0.806f, 1.0f,
		0.671f, 0.0f, 0.437f, 0.766f, 1.0f, 0.679f, 0.0f, 0.423f, 0.718f, 1.0f, 0.685f, 0.0f, 0.412f, 0.661f, 1.0f, 0.691f, 0.0f, 0.403f, 0.595f, 1.0f,
		0.697f, 0.0f, 0.396f, 0.519f, 1.0f, 0.701f, 0.0f, 0.391f, 0.44f, 1.0f, 0.704f, 0.0f, 0.387f, 0.344f, 1.0f, 0.707f, 0.0f, 0.384f, 0.264f, 1.0f,
		0.711f, 0.0f, 0.38f, 0.133f, 1.0f, };
	gpencil_add_points(gps, data35, 261);
}
