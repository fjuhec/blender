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

#include "BLI_listbase.h"

#include "DNA_modifier_types.h"
#include "DNA_object_types.h"

#include "BKE_hair.h"
#include "BKE_main.h"
#include "DEG_depsgraph.h"

#include "RNA_access.h"

#include "WM_api.h"
#include "WM_types.h"

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
	
	prop = RNA_def_property(srna, "follicles", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "follicles", "num_follicles");
	RNA_def_property_struct_type(prop, "HairFollicle");
	RNA_def_property_ui_text(prop, "Follicles", "Hair fiber follicles");
}

static void rna_def_hair_system(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	srna = RNA_def_struct(brna, "HairSystem", NULL);
	RNA_def_struct_ui_text(srna, "Hair System", "Hair rendering and deformation data");
	RNA_def_struct_sdna(srna, "HairSystem");
	RNA_def_struct_ui_icon(srna, ICON_STRANDS);
	
	prop = RNA_def_property(srna, "pattern", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "HairPattern");
	RNA_def_property_ui_text(prop, "Pattern", "Hair pattern");
}

void RNA_def_hair(BlenderRNA *brna)
{
	rna_def_hair_follicle(brna);
	rna_def_hair_pattern(brna);
	rna_def_hair_system(brna);
}

#endif
