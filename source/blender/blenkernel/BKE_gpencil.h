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

struct ToolSettings;
struct ListBase;
struct bGPdata;
struct bGPDlayer;
struct bGPDframe;
struct bGPDstroke;
struct bGPDpalette;
struct bGPDpalettecolor;
struct Main;
struct PaletteColor;
struct BoundBox;
struct Object;
struct bDeformGroup;
struct GpencilNoiseModifierData;
struct GpencilSubdivModifierData;
struct GpencilThickModifierData;
struct GpencilTintModifierData;
struct GpencilColorModifierData;
struct GpencilArrayModifierData;
struct GpencilDupliModifierData;
struct GpencilOpacityModifierData;
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
void BKE_gpencil_free_palettes(struct ListBase *list);
void BKE_gpencil_free(struct bGPdata *gpd, bool free_all);

void BKE_gpencil_batch_cache_dirty(struct bGPdata *gpd);
void BKE_gpencil_batch_cache_free(struct bGPdata *gpd);
void BKE_gpencil_batch_cache_alldirty(void);

void BKE_gpencil_stroke_sync_selection(struct bGPDstroke *gps);

struct bGPDframe *BKE_gpencil_frame_addnew(struct bGPDlayer *gpl, int cframe);
struct bGPDframe *BKE_gpencil_frame_addcopy(struct bGPDlayer *gpl, int cframe);
struct bGPDlayer *BKE_gpencil_layer_addnew(struct bGPdata *gpd, const char *name, bool setactive);
struct bGPdata   *BKE_gpencil_data_addnew(const char name[]);

struct bGPDframe *BKE_gpencil_frame_duplicate(const struct bGPDframe *gpf_src);
struct bGPDframe *BKE_gpencil_frame_color_duplicate(const struct bGPDframe *gpf_src);
struct bGPDlayer *BKE_gpencil_layer_duplicate(const struct bGPDlayer *gpl_src);
struct bGPdata   *BKE_gpencil_data_duplicate(struct Main *bmain, const struct bGPdata *gpd, bool internal_copy);

void BKE_gpencil_make_local(struct Main *bmain, struct bGPdata *gpd, const bool lib_local);

void BKE_gpencil_frame_delete_laststroke(struct bGPDlayer *gpl, struct bGPDframe *gpf);

struct bGPDpalette *BKE_gpencil_palette_addnew(struct bGPdata *gpd, const char *name, bool setactive);
struct bGPDpalette *BKE_gpencil_palette_duplicate(const struct bGPDpalette *palette_src);
struct bGPDpalettecolor *BKE_gpencil_palettecolor_addnew(struct bGPDpalette *palette, const char *name, bool setactive);

struct bGPDbrush *BKE_gpencil_brush_addnew(struct ToolSettings *ts, const char *name, bool setactive);
struct bGPDbrush *BKE_gpencil_brush_duplicate(const struct bGPDbrush *brush_src);
void BKE_gpencil_brush_init_presets(struct ToolSettings *ts);

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
void BKE_gpencil_palettecolor_delete_allstrokes(struct bContext *C, struct PaletteColor *palcolor);

/* object boundbox */
struct BoundBox *BKE_gpencil_boundbox_get(struct Object *ob);
void BKE_gpencil_centroid_3D(struct bGPdata *gpd, float r_centroid[3]);

/* vertex groups */
float BKE_gpencil_vgroup_use_index(struct bGPDspoint *pt, int index);
void BKE_gpencil_vgroup_remove(struct Object *ob, struct bDeformGroup *defgroup);
struct bGPDweight *BKE_gpencil_vgroup_add_point_weight(struct bGPDspoint *pt, int index, float weight);
bool BKE_gpencil_vgroup_remove_point_weight(struct bGPDspoint *pt, int index);
void BKE_gpencil_stroke_weights_duplicate(struct bGPDstroke *gps_src, struct bGPDstroke *gps_dst);

/* modifiers */
void ED_gpencil_reset_modifiers(struct Object *ob);
bool ED_gpencil_has_geometry_modifiers(struct Object *ob);
void ED_gpencil_stroke_modifiers(struct Object *ob, struct bGPDlayer *gpl, struct bGPDframe *gpf, struct bGPDstroke *gps);
void ED_gpencil_geometry_modifiers(struct Object *ob, struct bGPDlayer *gpl, struct bGPDframe *gpf);
void ED_gpencil_fill_random_array(float *ar, int count);
void ED_gpencil_stroke_normal(const struct bGPDstroke *gps, float r_normal[3]);
void ED_gpencil_noise_modifier(int id, struct GpencilNoiseModifierData *mmd, struct Object *ob, struct bGPDlayer *gpl, struct bGPDstroke *gps);
void ED_gpencil_subdiv_modifier(int id, struct GpencilSubdivModifierData *mmd, struct Object *ob, struct bGPDlayer *gpl, struct bGPDstroke *gps);
void ED_gpencil_thick_modifier(int id, struct GpencilThickModifierData *mmd, struct Object *ob, struct bGPDlayer *gpl, struct bGPDstroke *gps);
void ED_gpencil_tint_modifier(int id, struct GpencilTintModifierData *mmd, struct Object *ob, struct bGPDlayer *gpl, struct bGPDstroke *gps);
void ED_gpencil_color_modifier(int id, struct GpencilColorModifierData *mmd, struct Object *ob, struct bGPDlayer *gpl, struct bGPDstroke *gps);
void ED_gpencil_array_modifier(int id, struct GpencilArrayModifierData *mmd, struct Object *ob, int elem_idx[3], float r_mat[4][4]);
void ED_gpencil_dupli_modifier(int id, struct GpencilDupliModifierData *mmd, struct Object *ob, struct bGPDlayer *gpl, struct bGPDframe *gpf);
void ED_gpencil_opacity_modifier(int id, struct GpencilOpacityModifierData *mmd, struct Object *ob, struct bGPDlayer *gpl, struct bGPDstroke *gps);

bool ED_gpencil_use_this_lattice(struct Object *ob, struct Object *lattice);
void ED_gpencil_lattice_init(struct Object *ob);
void ED_gpencil_lattice_modifier(int id, struct GpencilLatticeModifierData *mmd, struct Object *ob, struct bGPDlayer *gpl, struct bGPDstroke *gps);

#endif /*  __BKE_GPENCIL_H__ */
