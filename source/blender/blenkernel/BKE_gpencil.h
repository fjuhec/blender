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
 * Contributor(s): Joshua Leung
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __BKE_GPENCIL_H__
#define __BKE_GPENCIL_H__

/** \file BKE_gpencil.h
 *  \ingroup bke
 *  \author Joshua Leung
 */

struct CurveMapping;
struct bContext;
struct EvaluationContext;
struct ToolSettings;
struct ListBase;
struct bGPdata;
struct bGPDlayer;
struct bGPDframe;
struct bGPDspoint;
struct bGPDstroke;
struct bGPDpaletteref;
struct bGPDpalette;
struct bGPDpalettecolor;
struct Main;
struct PaletteColor;
struct BoundBox;
struct Object;
struct bDeformGroup;
struct GpencilSimplifyModifierData;
struct GpencilArrayModifierData;
struct GpencilLatticeModifierData;

/* ------------ Grease-Pencil API ------------------ */

void BKE_gpencil_free_point_weights(struct bGPDspoint *pt);
void BKE_gpencil_free_stroke_weights(struct bGPDstroke *gps);
void BKE_gpencil_free_stroke(struct bGPDstroke *gps);
bool BKE_gpencil_free_strokes(struct bGPDframe *gpf);
bool BKE_gpencil_free_layer_temp_data(struct bGPDlayer *gpl, struct bGPDframe *derived_gpf);
void BKE_gpencil_free_frames(struct bGPDlayer *gpl);
void BKE_gpencil_free_layers(struct ListBase *list);
void BKE_gpencil_free_derived_frames(struct bGPdata *gpd);
void BKE_gpencil_free_brushes(struct ListBase *list);
void BKE_gpencil_free(struct bGPdata *gpd, bool free_all);

void BKE_gpencil_batch_cache_dirty(struct bGPdata *gpd);
void BKE_gpencil_batch_cache_free(struct bGPdata *gpd);
void BKE_gpencil_batch_cache_alldirty_main(struct Main *bmain);

void BKE_gpencil_stroke_sync_selection(struct bGPDstroke *gps);

struct bGPDframe *BKE_gpencil_frame_addnew(struct bGPDlayer *gpl, int cframe);
struct bGPDframe *BKE_gpencil_frame_addcopy(struct bGPDlayer *gpl, int cframe);
struct bGPDlayer *BKE_gpencil_layer_addnew(struct bGPdata *gpd, const char *name, bool setactive);
struct bGPdata   *BKE_gpencil_data_addnew(struct Main *bmain, const char name[]);

struct bGPDframe *BKE_gpencil_frame_duplicate(const struct bGPDframe *gpf_src);
struct bGPDframe *BKE_gpencil_frame_color_duplicate(const struct bContext *C, const struct bGPDframe *gpf_src);
struct bGPDlayer *BKE_gpencil_layer_duplicate(const struct bGPDlayer *gpl_src);
void BKE_gpencil_frame_copy_strokes(struct bGPDframe *gpf_src, struct bGPDframe *gpf_dst);

void BKE_gpencil_copy_data(struct Main *bmain, struct bGPdata *gpd_dst, const struct bGPdata *gpd_src, const int flag);
struct bGPdata   *BKE_gpencil_copy(struct Main *bmain, const struct bGPdata *gpd);
struct bGPdata   *BKE_gpencil_data_duplicate(struct Main *bmain, const struct bGPdata *gpd, bool internal_copy);
void BKE_gpencil_copy_palette_data(struct bGPdata *gpd_dst, const struct bGPdata *gpd_src);

void BKE_gpencil_make_local(struct Main *bmain, struct bGPdata *gpd, const bool lib_local);

void BKE_gpencil_frame_delete_laststroke(struct bGPDlayer *gpl, struct bGPDframe *gpf);

struct bGPDbrush *BKE_gpencil_brush_addnew(struct ToolSettings *ts, const char *name, bool setactive);
struct bGPDbrush *BKE_gpencil_brush_duplicate(const struct bGPDbrush *brush_src);
void BKE_gpencil_brush_init_presets(struct ToolSettings *ts);


/* Utilities for creating and populating GP strokes */
/* - Number of values defining each point in the built-in data 
 *   buffers for primitives (e.g. 2D Monkey) 
 */
#define GP_PRIM_DATABUF_SIZE  5

void BKE_gpencil_stroke_add_points(struct bGPDstroke *gps, const float *array, const int totpoints);

struct bGPDstroke *BKE_gpencil_add_stroke(
        struct bGPDframe *gpf, struct Palette *palette, struct PaletteColor *palcolor, int totpoints,
        const char *colorname, short thickness);


/* conversion of animation data from bGPDpalette to Palette */
void BKE_gpencil_move_animdata_to_palettes(struct bContext *C, struct bGPdata *gpd);

/* Stroke and Fill - Alpha Visibility Threshold */
#define GPENCIL_ALPHA_OPACITY_THRESH 0.001f
#define GPENCIL_STRENGTH_MIN 0.003f

bool gpencil_layer_is_editable(const struct bGPDlayer *gpl);

/* How gpencil_layer_getframe() should behave when there
 * is no existing GP-Frame on the frame requested.
 */
typedef enum eGP_GetFrame_Mode {
	/* Use the preceeding gp-frame (i.e. don't add anything) */
	GP_GETFRAME_USE_PREV  = 0,
	
	/* Add a new empty/blank frame */
	GP_GETFRAME_ADD_NEW   = 1,
	/* Make a copy of the active frame */
	GP_GETFRAME_ADD_COPY  = 2
} eGP_GetFrame_Mode;

struct bGPDframe *BKE_gpencil_layer_getframe(struct bGPDlayer *gpl, int cframe, eGP_GetFrame_Mode addnew);
struct bGPDframe *BKE_gpencil_layer_find_frame(struct bGPDlayer *gpl, int cframe);
bool BKE_gpencil_layer_delframe(struct bGPDlayer *gpl, struct bGPDframe *gpf);

struct bGPDlayer *BKE_gpencil_layer_getactive(struct bGPdata *gpd);
void BKE_gpencil_layer_setactive(struct bGPdata *gpd, struct bGPDlayer *active);
void BKE_gpencil_layer_delete(struct bGPdata *gpd, struct bGPDlayer *gpl);

struct bGPDbrush *BKE_gpencil_brush_getactive(struct ToolSettings *ts);
void BKE_gpencil_brush_setactive(struct ToolSettings *ts, struct bGPDbrush *active);
void BKE_gpencil_brush_delete(struct ToolSettings *ts, struct bGPDbrush *brush);

/* Palette Slots */
void BKE_gpencil_palette_slot_free(struct bGPdata *gpd, struct bGPDpaletteref *palslot);

struct bGPDpaletteref *BKE_gpencil_paletteslot_find(struct bGPdata *gpd, const struct Palette *palette);
bool BKE_gpencil_paletteslot_has_users(const struct bGPdata *gpd, const struct bGPDpaletteref *palslot);

struct bGPDpaletteref *BKE_gpencil_paletteslot_get_active(const struct bGPdata *gpd);
void BKE_gpencil_paletteslot_set_active(struct bGPdata *gpd, const struct bGPDpaletteref *palslot);
void BKE_gpencil_paletteslot_set_active_palette(struct bGPdata *gpd, const struct Palette *palette);

void BKE_gpencil_paletteslot_set_palette(struct bGPdata *gpd, struct bGPDpaletteref *palslot, struct Palette *palette);

struct bGPDpaletteref *BKE_gpencil_paletteslot_add(struct bGPdata *gpd, struct Palette *palette);
struct bGPDpaletteref *BKE_gpencil_paletteslot_addnew(struct Main *bmain, struct bGPdata *gpd, const char name[]);
struct bGPDpaletteref *BKE_gpencil_paletteslot_validate(struct Main *bmain, struct bGPdata *gpd);

/* Palettes - Deprecated (2.78-2.79) */
void BKE_gpencil_free_palettes(struct ListBase *list);

struct bGPDpalette *BKE_gpencil_palette_addnew(struct bGPdata *gpd, const char *name, bool setactive);
struct bGPDpalette *BKE_gpencil_palette_duplicate(const struct bGPDpalette *palette_src);
struct bGPDpalettecolor *BKE_gpencil_palettecolor_addnew(struct bGPDpalette *palette, const char *name, bool setactive);

struct bGPDpalette *BKE_gpencil_palette_getactive(struct bGPdata *gpd);
void BKE_gpencil_palette_setactive(struct bGPdata *gpd, struct bGPDpalette *active);
void BKE_gpencil_palette_delete(struct bGPdata *gpd, struct bGPDpalette *palette);
void BKE_gpencil_palette_change_strokes(struct bGPdata *gpd);

struct bGPDpalettecolor *BKE_gpencil_palettecolor_getactive(struct bGPDpalette *palette);
void BKE_gpencil_palettecolor_setactive(struct bGPDpalette *palette, struct bGPDpalettecolor *active);
void BKE_gpencil_palettecolor_delete(struct bGPDpalette *palette, struct bGPDpalettecolor *palcolor);
struct bGPDpalettecolor *BKE_gpencil_palettecolor_getbyname(struct bGPDpalette *palette, char *name);
void BKE_gpencil_palettecolor_allnames(struct PaletteColor *palcolor, const char *newname);
void BKE_gpencil_palettecolor_changename(struct PaletteColor *palcolor, struct bGPdata *gpd, const char *newname);
void BKE_gpencil_palettecolor_delete_allstrokes(struct Main *bmain, struct PaletteColor *palcolor);

/* object boundbox */
bool BKE_gpencil_stroke_minmax(
        const struct bGPDstroke *gps, const bool use_select,
        float r_min[3], float r_max[3]);

struct BoundBox *BKE_gpencil_boundbox_get(struct Object *ob);
void BKE_gpencil_centroid_3D(struct bGPdata *gpd, float r_centroid[3]);

/* vertex groups */
float BKE_gpencil_vgroup_use_index(struct bGPDspoint *pt, int index);
void BKE_gpencil_vgroup_remove(struct Object *ob, struct bDeformGroup *defgroup);
struct bGPDweight *BKE_gpencil_vgroup_add_point_weight(struct bGPDspoint *pt, int index, float weight);
bool BKE_gpencil_vgroup_remove_point_weight(struct bGPDspoint *pt, int index);
void BKE_gpencil_stroke_weights_duplicate(struct bGPDstroke *gps_src, struct bGPDstroke *gps_dst);

/* GPencil geometry evaluation */
void BKE_gpencil_eval_geometry(const struct EvaluationContext *eval_ctx, struct bGPdata *gpd);

/* modifiers */
bool BKE_gpencil_has_geometry_modifiers(struct Object *ob);

void BKE_gpencil_stroke_modifiers(
		struct EvaluationContext *eval_ctx, struct Object *ob, 
		struct bGPDlayer *gpl, struct bGPDframe *gpf, struct bGPDstroke *gps, bool is_render);
void BKE_gpencil_geometry_modifiers(
		struct EvaluationContext *eval_ctx, struct Object *ob, 
		struct bGPDlayer *gpl, struct bGPDframe *gpf, bool is_render);

void BKE_gpencil_array_modifier_instance_tfm(struct GpencilArrayModifierData *mmd, const int elem_idx[3], float r_mat[4][4]);

void BKE_gpencil_lattice_init(struct Object *ob);
void BKE_gpencil_lattice_clear(struct Object *ob);

/* stroke geometry utilities */
void BKE_gpencil_stroke_normal(const struct bGPDstroke *gps, float r_normal[3]);
void BKE_gpencil_simplify_stroke(struct bGPDlayer *gpl, struct bGPDstroke *gps, float factor);
void BKE_gpencil_simplify_fixed(struct bGPDlayer *gpl, struct bGPDstroke *gps);

void BKE_gpencil_transform(struct bGPdata *gpd, float mat[4][4]);

bool BKE_gp_smooth_stroke(struct bGPDstroke *gps, int i, float inf, bool affect_pressure);
bool BKE_gp_smooth_stroke_strength(struct bGPDstroke *gps, int i, float inf);
bool BKE_gp_smooth_stroke_thickness(struct bGPDstroke *gps, int i, float inf);

void BKE_gp_get_range_selected(struct bGPDlayer *gpl, int *r_initframe, int *r_endframe);
float BKE_gpencil_multiframe_falloff_calc(struct bGPDframe *gpf, int actnum, int f_init, int f_end, struct CurveMapping *cur_falloff);

#endif /*  __BKE_GPENCIL_H__ */
