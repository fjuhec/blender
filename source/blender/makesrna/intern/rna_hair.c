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

#include "DNA_object_types.h"

#include "BKE_context.h"
#include "BKE_DerivedMesh.h"
#include "BKE_hair.h"
#include "BKE_main.h"
#include "BKE_material.h"

#include "DEG_depsgraph.h"

#include "RNA_access.h"

#include "WM_api.h"
#include "WM_types.h"

static void rna_HairSystem_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	DEG_id_tag_update(ptr->id.data, OB_RECALC_DATA);
}

static void rna_HairSystem_generate_follicles(
        HairSystem *hsys,
        struct bContext *C,
        Object *scalp,
        int seed,
        int count)
{
	if (!scalp)
	{
		return;
	}
	
	struct Scene *scene = CTX_data_scene(C);
	EvaluationContext eval_ctx;
	CTX_data_eval_ctx(C, &eval_ctx);
	
	CustomDataMask datamask = CD_MASK_BAREMESH;
	DerivedMesh *dm = mesh_get_derived_final(&eval_ctx, scene, scalp, datamask);
	
	BKE_hair_generate_follicles(hsys, dm, (unsigned int)seed, count);
}

static const EnumPropertyItem *rna_HairSystem_material_slot_itemf(
        bContext *C,
        PointerRNA *UNUSED(ptr),
        PropertyRNA *UNUSED(prop),
        bool *r_free)
{
	Object *ob = CTX_data_pointer_get(C, "object").data;
	Material *ma;
	EnumPropertyItem *item = NULL;
	EnumPropertyItem tmp = {0, "", 0, "", ""};
	int totitem = 0;
	int i;

	if (ob && ob->totcol > 0) {
		for (i = 1; i <= ob->totcol; i++) {
			ma = give_current_material(ob, i);
			tmp.value = i;
			tmp.icon = ICON_MATERIAL_DATA;
			if (ma) {
				tmp.name = ma->id.name + 2;
				tmp.identifier = tmp.name;
			}
			else {
				tmp.name = "Default Material";
				tmp.identifier = tmp.name;
			}
			RNA_enum_item_add(&item, &totitem, &tmp);
		}
	}
	else {
		tmp.value = 1;
		tmp.icon = ICON_MATERIAL_DATA;
		tmp.name = "Default Material";
		tmp.identifier = tmp.name;
		RNA_enum_item_add(&item, &totitem, &tmp);
	}

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
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
	
	prop = RNA_def_property(srna, "follicles", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "follicles", "num_follicles");
	RNA_def_property_struct_type(prop, "HairFollicle");
	RNA_def_property_ui_text(prop, "Follicles", "Hair fiber follicles");
}

static void rna_def_hair_system(BlenderRNA *brna)
{
	StructRNA *srna;
	FunctionRNA *func;
	PropertyRNA *prop, *parm;
	
	static const EnumPropertyItem material_slot_items[] = {
		{0, "DUMMY", 0, "Dummy", ""},
		{0, NULL, 0, NULL, NULL}
	};
	
	srna = RNA_def_struct(brna, "HairSystem", NULL);
	RNA_def_struct_ui_text(srna, "Hair System", "Hair rendering and deformation data");
	RNA_def_struct_sdna(srna, "HairSystem");
	RNA_def_struct_ui_icon(srna, ICON_STRANDS);
	
	prop = RNA_def_property(srna, "pattern", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "HairPattern");
	RNA_def_property_ui_text(prop, "Pattern", "Hair pattern");
	
	prop = RNA_def_property(srna, "material_index", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "material_index");
	RNA_def_property_range(prop, 1, 32767);
	RNA_def_property_ui_text(prop, "Material Index", "Index of material slot used for rendering hair fibers");
	RNA_def_property_update(prop, 0, "rna_HairSystem_update");

	prop = RNA_def_property(srna, "material_slot", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "material_index");
	RNA_def_property_enum_items(prop, material_slot_items);
	RNA_def_property_enum_funcs(prop, NULL, NULL, "rna_HairSystem_material_slot_itemf");
	RNA_def_property_ui_text(prop, "Material Slot", "Material slot used for rendering particles");
	RNA_def_property_update(prop, 0, "rna_HairSystem_update");
	
	func = RNA_def_function(srna, "generate_follicles", "rna_HairSystem_generate_follicles");
	RNA_def_function_flag(func, FUNC_USE_CONTEXT);
	parm = RNA_def_pointer(func, "scalp", "Object", "Scalp", "Scalp object on which to place hair follicles");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	parm = RNA_def_int(func, "seed", 0, 0, INT_MAX, "Seed", "Seed value for random numbers", 0, INT_MAX);
	parm = RNA_def_int(func, "count", 0, 0, INT_MAX, "Count", "Maximum number of follicles to generate", 1, 1e5);
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
}

static void rna_def_hair_draw_settings(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	static const EnumPropertyItem follicle_mode_items[] = {
	    {HAIR_DRAW_FOLLICLE_NONE, "NONE", 0, "None", ""},
	    {HAIR_DRAW_FOLLICLE_POINTS, "POINTS", 0, "Points", "Draw a point for each follicle"},
	    {HAIR_DRAW_FOLLICLE_AXES, "AXES", 0, "Axes", "Draw direction of hair for each follicle"},
		{0, NULL, 0, NULL, NULL}
	};
	
	srna = RNA_def_struct(brna, "HairDrawSettings", NULL);
	RNA_def_struct_ui_text(srna, "Hair Draw Settings", "Settings for drawing hair systems");
	RNA_def_struct_sdna(srna, "HairDrawSettings");
	
	prop = RNA_def_property(srna, "follicle_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, follicle_mode_items);
	RNA_def_property_ui_text(prop, "Follicle Mode", "Draw follicles on the scalp surface");
}

void RNA_def_hair(BlenderRNA *brna)
{
	rna_def_hair_follicle(brna);
	rna_def_hair_pattern(brna);
	rna_def_hair_system(brna);
	rna_def_hair_draw_settings(brna);
}

#endif
