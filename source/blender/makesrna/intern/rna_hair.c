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
 * Contributor(s): Blender Foundation.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/makesrna/intern/rna_hair.c
 *  \ingroup RNA
 */

#include <stdlib.h>

#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "rna_internal.h"

#include "DNA_hair_types.h"

#include "WM_types.h"

#ifdef RNA_RUNTIME

#include "MEM_guardedalloc.h"

#include "DNA_object_types.h"

#include "BKE_hair.h"
#include "BKE_main.h"
#include "DEG_depsgraph.h"

#include "WM_api.h"
#include "WM_types.h"

static void rna_HairPattern_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	HairPattern *hair = ptr->data;
	UNUSED_VARS(hair);
}

static void rna_HairPattern_num_follicles_set(PointerRNA *ptr, int value)
{
	HairPattern *hair = ptr->data;
	BKE_hair_set_num_follicles(hair, value);
}

#else

static void rna_def_hair_follicle(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	srna = RNA_def_struct(brna, "HairFollicle", NULL);
	RNA_def_struct_ui_text(srna, "Hair Follicle", "Single follicle on a surface");
	RNA_def_struct_sdna(srna, "HairFollicle");
	
	prop = RNA_def_property(srna, "mesh_sample", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "MeshSample");
}

static void rna_def_hair_pattern(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	srna = RNA_def_struct(brna, "HairPattern", NULL);
	RNA_def_struct_ui_text(srna, "Hair Pattern", "Set of hair follicles distributed on a surface");
	RNA_def_struct_sdna(srna, "HairPattern");
	RNA_def_struct_ui_icon(srna, ICON_STRANDS);
	
	prop = RNA_def_property(srna, "num_follicles", PROP_INT, PROP_UNSIGNED);
	RNA_def_property_int_sdna(prop, NULL, "num_follicles");
	RNA_def_property_int_funcs(prop, NULL, "rna_HairPattern_num_follicles_set", NULL);
	RNA_def_property_flag(prop, PROP_HIDDEN);
	RNA_def_property_range(prop, 0, INT_MAX);
	RNA_def_property_ui_range(prop, 0, 1000000, 1, -1);
	RNA_def_property_ui_text(prop, "Amount", "Number of hair follicles");
	RNA_def_property_update(prop, 0, "rna_HairPattern_update");
}

void RNA_def_hair(BlenderRNA *brna)
{
	rna_def_hair_follicle(brna);
	rna_def_hair_pattern(brna);
}

#endif
