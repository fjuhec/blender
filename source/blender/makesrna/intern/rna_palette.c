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

/** \file blender/makesrna/intern/rna_palette.c
 *  \ingroup RNA
 */

#include <stdlib.h>

#include "BLI_utildefines.h"

#include "RNA_define.h"
#include "RNA_access.h"

#include "rna_internal.h"

#include "WM_types.h"
#include "ED_gpencil.h"

#include "BLT_translation.h"

#include "DNA_gpencil_types.h"
#include "DNA_brush_types.h"

#ifdef RNA_RUNTIME

#include "WM_api.h"
#include "ED_gpencil.h"

#include "BKE_paint.h"
#include "BKE_report.h"
#include "BKE_gpencil.h"

static void rna_GPencil_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *UNUSED(ptr))
{
	WM_main_add_notifier(NC_GPENCIL | NA_EDITED, NULL);
}

static PaletteColor *rna_Palette_color_new(Palette *palette)
{
	PaletteColor *color = BKE_palette_color_add(palette);
	return color;
}

static void rna_Palette_color_remove(Palette *palette, ReportList *reports, PointerRNA *color_ptr)
{
	PaletteColor *color = color_ptr->data;

	if (BLI_findindex(&palette->colors, color) == -1) {
		BKE_reportf(reports, RPT_ERROR, "Palette '%s' does not contain color given", palette->id.name + 2);
		return;
	}

	BKE_palette_color_remove(palette, color);

	RNA_POINTER_INVALIDATE(color_ptr);
}

static void rna_Palette_color_clear(Palette *palette)
{
	BKE_palette_clear(palette);
}

static PointerRNA rna_Palette_active_color_get(PointerRNA *ptr)
{
	Palette *palette = ptr->data;
	PaletteColor *color;

	color = BLI_findlink(&palette->colors, palette->active_color);

	if (color)
		return rna_pointer_inherit_refine(ptr, &RNA_PaletteColor, color);

	return rna_pointer_inherit_refine(ptr, NULL, NULL);
}

static void rna_Palette_active_color_set(PointerRNA *ptr, PointerRNA value)
{
	Palette *palette = ptr->data;
	PaletteColor *color = value.data;

	/* -1 is ok for an unset index */
	if (color == NULL)
		palette->active_color = -1;
	else
		palette->active_color = BLI_findindex(&palette->colors, color);
}
static char *rna_Palette_color_path(PointerRNA *ptr)
{
	Palette *palette = ptr->data;
	PaletteColor *palcolor = BLI_findlink(&palette->colors, palette->active_color);

	char name_palette[sizeof(palette->id.name) * 2];
	char name_color[sizeof(palcolor->info) * 2];

	BLI_strescape(name_palette, palette->id.name, sizeof(name_palette));
	BLI_strescape(name_color, palcolor->info, sizeof(name_color));

	return BLI_sprintfN("palettes[\"%s\"].colors[\"%s\"]", name_palette, name_color);
}

static void rna_PaletteColor_info_set(PointerRNA *ptr, const char *value)
{
	Palette *palette = ptr->data;
	PaletteColor *palcolor = BLI_findlink(&palette->colors, palette->active_color);

	/* rename all for gp datablocks */
	BKE_gpencil_palettecolor_allnames(palcolor->info, value);

	/* copy the new name into the name slot */
	BLI_strncpy_utf8(palcolor->info, value, sizeof(palcolor->info));
	BLI_uniquename(&palette->colors, palcolor, DATA_("Color"), '.', offsetof(PaletteColor, info),
		sizeof(palcolor->info));
}

static int rna_PaletteColor_is_stroke_visible_get(PointerRNA *ptr)
{
	PaletteColor *pcolor = (PaletteColor *)ptr->data;
	return (pcolor->rgb[3] > GPENCIL_ALPHA_OPACITY_THRESH);
}

static int rna_PaletteColor_is_fill_visible_get(PointerRNA *ptr)
{
	PaletteColor *pcolor = (PaletteColor *)ptr->data;
	return (pcolor->fill[3] > GPENCIL_ALPHA_OPACITY_THRESH);
}

#else

/* palette.colors */
static void rna_def_palettecolors(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;
	PropertyRNA *prop;

	FunctionRNA *func;
	PropertyRNA *parm;

	RNA_def_property_srna(cprop, "PaletteColors");
	srna = RNA_def_struct(brna, "PaletteColors", NULL);
	RNA_def_struct_sdna(srna, "Palette");
	RNA_def_struct_ui_text(srna, "Palette Splines", "Collection of palette colors");

	func = RNA_def_function(srna, "new", "rna_Palette_color_new");
	RNA_def_function_ui_description(func, "Add a new color to the palette");
	parm = RNA_def_pointer(func, "color", "PaletteColor", "", "The newly created color");
	RNA_def_function_return(func, parm);

	func = RNA_def_function(srna, "remove", "rna_Palette_color_remove");
	RNA_def_function_ui_description(func, "Remove a color from the palette");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);
	parm = RNA_def_pointer(func, "color", "PaletteColor", "", "The color to remove");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
	RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);

	func = RNA_def_function(srna, "clear", "rna_Palette_color_clear");
	RNA_def_function_ui_description(func, "Remove all colors from the palette");

	prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "PaletteColor");
	RNA_def_property_pointer_funcs(prop, "rna_Palette_active_color_get", "rna_Palette_active_color_set", NULL, NULL);
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Active Palette Color", "");
}

static void rna_def_palettecolor(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "PaletteColor", NULL);
	RNA_def_struct_ui_text(srna, "Palette Color", "");

	prop = RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_range(prop, 0.0, 1.0);
	RNA_def_property_float_sdna(prop, NULL, "rgb");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Color", "");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS | ND_DATA | NC_GPENCIL, "rna_GPencil_update");

	prop = RNA_def_property(srna, "strength", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1.0);
	RNA_def_property_float_sdna(prop, NULL, "value");
	RNA_def_property_ui_text(prop, "Value", "");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

	prop = RNA_def_property(srna, "weight", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1.0);
	RNA_def_property_float_sdna(prop, NULL, "value");
	RNA_def_property_ui_text(prop, "Weight", "");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

	prop = RNA_def_property(srna, "alpha", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "rgb[3]");
	RNA_def_property_range(prop, 0.0, 1.0f);
	RNA_def_property_ui_text(prop, "Opacity", "Color Opacity");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS | ND_DATA | NC_GPENCIL, "rna_GPencil_update");

	/* Name */
	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "info");
	RNA_def_property_ui_text(prop, "Name", "Color name");
	RNA_def_property_string_funcs(prop, NULL, NULL, "rna_PaletteColor_info_set");
	RNA_def_struct_name_property(srna, prop);
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS | ND_DATA | NC_GPENCIL, "rna_GPencil_update");

	/* Fill Drawing Color */
	prop = RNA_def_property(srna, "fill_color", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "fill");
	RNA_def_property_array(prop, 3);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Fill Color", "Color for filling region bounded by each stroke");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS | ND_DATA | NC_GPENCIL, "rna_GPencil_update");

	/* Fill alpha */
	prop = RNA_def_property(srna, "fill_alpha", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "fill[3]");
	RNA_def_property_range(prop, 0.0, 1.0f);
	RNA_def_property_ui_text(prop, "Fill Opacity", "Opacity for filling region bounded by each stroke");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS | ND_DATA | NC_GPENCIL, "rna_GPencil_update");

	/* Flags */
	prop = RNA_def_property(srna, "hide", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", PAC_COLOR_HIDE);
	RNA_def_property_ui_icon(prop, ICON_RESTRICT_VIEW_OFF, 1);
	RNA_def_property_ui_text(prop, "Hide", "Set color Visibility");
	RNA_def_property_update(prop, NC_GPENCIL | ND_DATA, "rna_GPencil_update");

	prop = RNA_def_property(srna, "lock", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", PAC_COLOR_LOCKED);
	RNA_def_property_ui_icon(prop, ICON_UNLOCKED, 1);
	RNA_def_property_ui_text(prop, "Locked", "Protect color from further editing and/or frame changes");
	RNA_def_property_update(prop, NC_GPENCIL | ND_DATA, "rna_GPencil_update");

	prop = RNA_def_property(srna, "ghost", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", PAC_COLOR_ONIONSKIN);
	RNA_def_property_ui_icon(prop, ICON_GHOST_ENABLED, 0);
	RNA_def_property_ui_text(prop, "Show in Ghosts", "Display strokes using this color when showing onion skins");
	RNA_def_property_update(prop, NC_GPENCIL | ND_DATA, "rna_GPencil_update");

	/* Draw Style */
	prop = RNA_def_property(srna, "use_volumetric_strokes", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", PAC_COLOR_VOLUMETRIC);
	RNA_def_property_ui_text(prop, "Volumetric Strokes", "Draw strokes as a series of circular blobs, resulting in "
		"a volumetric effect");
	RNA_def_property_update(prop, NC_GPENCIL | ND_DATA, "rna_GPencil_update");

	/* Read-only state props (for simpler UI code) */
	prop = RNA_def_property(srna, "is_stroke_visible", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_funcs(prop, "rna_PaletteColor_is_stroke_visible_get", NULL);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Is Stroke Visible", "True when opacity of stroke is set high enough to be visible");

	prop = RNA_def_property(srna, "is_fill_visible", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_funcs(prop, "rna_PaletteColor_is_fill_visible_get", NULL);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Is Fill Visible", "True when opacity of fill is set high enough to be visible");

}

static void rna_def_palette(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "Palette", "ID");
	RNA_def_struct_ui_text(srna, "Palette", "");
	RNA_def_struct_ui_icon(srna, ICON_COLOR);

	prop = RNA_def_property(srna, "colors", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_struct_type(prop, "PaletteColor");
	rna_def_palettecolors(brna, prop);
}

void RNA_def_palette(BlenderRNA *brna)
{
	/* *** Non-Animated *** */
	RNA_define_animate_sdna(false);
	rna_def_palettecolor(brna);
	rna_def_palette(brna);
	RNA_define_animate_sdna(true);
}

#endif
