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

static void rna_HairGroup_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	HairGroup *group = ptr->data;
	UNUSED_VARS(group);
}

static HairPattern* find_hair_group_pattern(ID *id, HairGroup *group)
{
	Object *ob = (Object *)id;
	for (ModifierData *md = ob->modifiers.first; md; md = md->next) {
		if (md->type == eModifierType_Hair) {
			HairModifierData *hmd = (HairModifierData *)md;
			for (HairGroup *g = hmd->hair->groups.first; g; g = g->next) {
				if (g == group) {
					return hmd->hair;
				}
			}
		}
	}
	return NULL;
}

static void rna_HairGroup_name_set(PointerRNA *ptr, const char *value)
{
	HairGroup *group = ptr->data;
	HairPattern *hair = find_hair_group_pattern(ptr->id.data, group);
	BLI_assert(hair != NULL);
	
	BKE_hair_group_name_set(hair, group, value);
}

static void rna_HairPattern_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	HairPattern *hair = ptr->data;
	UNUSED_VARS(hair);
}

PointerRNA rna_HairPattern_active_group_get(PointerRNA *ptr)
{
	HairPattern *hair = ptr->data;
	PointerRNA result;
	RNA_pointer_create(ptr->id.data, &RNA_HairGroup, BLI_findlink(&hair->groups, hair->active_group), &result);
	return result;
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

static void rna_def_hair_group(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	static EnumPropertyItem type_items[] = {
	    {HAIR_GROUP_TYPE_NORMALS, "NORMALS", 0, "Normals", "Hair grows straight along surface normals"},
	    {HAIR_GROUP_TYPE_STRANDS, "STRANDS", 0, "Strands", "Hair is interpolated between control strands"},
	    {0, NULL, 0, NULL, NULL}
	};
	
	srna = RNA_def_struct(brna, "HairGroup", NULL);
	RNA_def_struct_ui_text(srna, "Hair Group", "Group of hairs that are generated in the same way");
	RNA_def_struct_sdna(srna, "HairGroup");
	
	prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, type_items);
	RNA_def_property_enum_default(prop, HAIR_GROUP_TYPE_NORMALS);
	RNA_def_property_ui_text(prop, "Type", "Generator type");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_HairGroup_update");
	
	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_property_string_funcs(prop, NULL, NULL, "rna_HairGroup_name_set");
	RNA_def_property_ui_text(prop, "Name", "");
	RNA_def_struct_name_property(srna, prop);
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW | NA_RENAME, NULL);
	
	prop = RNA_def_property(srna, "max_length", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0f, FLT_MAX);
	RNA_def_property_ui_range(prop, 0.0, 10.0, 0.1, 4);
	RNA_def_property_ui_text(prop, "Maximum Length", "Maximum length of hair fibers in this group");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_HairGroup_update");
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
	
	prop = RNA_def_property(srna, "groups", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "groups", NULL);
	RNA_def_property_struct_type(prop, "HairGroup");
	RNA_def_property_ui_text(prop, "Groups", "Hair group using a the same generator method");
	
	prop = RNA_def_property(srna, "active_group", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_funcs(prop, "rna_HairPattern_active_group_get", NULL, NULL, NULL);
	RNA_def_property_struct_type(prop, "HairGroup");
	RNA_def_property_ui_text(prop, "Active Group", "");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_HairPattern_update");
	
	prop = RNA_def_property(srna, "active_group_index", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "active_group");
	RNA_def_property_ui_text(prop, "Active Group Index", "");
	RNA_def_property_update(prop, NC_OBJECT | ND_DRAW, "rna_HairPattern_update");
}

void RNA_def_hair(BlenderRNA *brna)
{
	rna_def_hair_follicle(brna);
	rna_def_hair_group(brna);
	rna_def_hair_pattern(brna);
}

#endif
