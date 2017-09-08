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

/** \file blender/makesrna/intern/rna_workspace.c
 *  \ingroup RNA
 */

#include "RNA_define.h"
#include "RNA_enum_types.h"
#include "RNA_types.h"

#include "BKE_workspace.h"

#include "ED_render.h"

#include "RE_engine.h"

#include "WM_api.h"
#include "WM_types.h"

#include "rna_internal.h"

/* Allow accessing private members of DNA_workspace_types.h */
#define DNA_PRIVATE_WORKSPACE_ALLOW
#include "DNA_workspace_types.h"

#ifdef RNA_RUNTIME

#include "BKE_global.h"

#include "BLI_listbase.h"

#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "RNA_access.h"


void rna_workspace_screens_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
	WorkSpace *workspace = ptr->id.data;
	rna_iterator_listbase_begin(iter, BKE_workspace_layouts_get(workspace), NULL);
}

static PointerRNA rna_workspace_screens_item_get(CollectionPropertyIterator *iter)
{
	WorkSpaceLayout *layout = rna_iterator_listbase_get(iter);
	bScreen *screen = BKE_workspace_layout_screen_get(layout);

	return rna_pointer_inherit_refine(&iter->parent, &RNA_Screen, screen);
}

#ifdef USE_WORKSPACE_MODE

static int rna_workspace_object_mode_get(PointerRNA *ptr)
{
	WorkSpace *workspace = ptr->data;
	return (int)BKE_workspace_object_mode_get(workspace);
}

static void rna_workspace_object_mode_set(PointerRNA *ptr, int value)
{
	WorkSpace *workspace = ptr->data;
	BKE_workspace_object_mode_set(workspace, value);
}

#endif /* USE_WORKSPACE_MODE */

void rna_workspace_transform_orientations_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
	WorkSpace *workspace = ptr->id.data;
	rna_iterator_listbase_begin(iter, BKE_workspace_transform_orientations_get(workspace), NULL);
}

static PointerRNA rna_workspace_transform_orientations_item_get(CollectionPropertyIterator *iter)
{
	TransformOrientation *transform_orientation = rna_iterator_listbase_get(iter);
	return rna_pointer_inherit_refine(&iter->parent, &RNA_TransformOrientation, transform_orientation);
}

static PointerRNA rna_workspace_render_layer_get(PointerRNA *ptr)
{
	WorkSpace *workspace = ptr->data;
	SceneLayer *render_layer = BKE_workspace_render_layer_get(workspace);

	/* XXX hmrf... lookup in getter... but how could we avoid it? */
	for (Scene *scene = G.main->scene.first; scene; scene = scene->id.next) {
		if (BLI_findindex(&scene->render_layers, render_layer) != -1) {
			PointerRNA scene_ptr;

			RNA_id_pointer_create(&scene->id, &scene_ptr);
			return rna_pointer_inherit_refine(&scene_ptr, &RNA_SceneLayer, render_layer);
		}
	}

	return PointerRNA_NULL;
}

static void rna_workspace_render_layer_set(PointerRNA *ptr, PointerRNA value)
{
	WorkSpace *workspace = ptr->data;
	BKE_workspace_render_layer_set(workspace, value.data);
}

static void rna_workspace_engine_set(PointerRNA *ptr, int value)
{
	WorkSpace *workspace = (WorkSpace *)ptr->data;
	RenderEngineType *type = BLI_findlink(&R_engines, value);

	if (type != NULL) {
		BKE_workspace_engine_set(workspace, type->idname);
	}
}

static EnumPropertyItem *rna_workspace_engine_itemf(
        bContext *UNUSED(C), PointerRNA *UNUSED(ptr), PropertyRNA *UNUSED(prop), bool *r_free)
{
	RenderEngineType *type;
	EnumPropertyItem *item = NULL;
	EnumPropertyItem tmp = {0, "", 0, "", ""};
	int a = 0, totitem = 0;

	for (type = R_engines.first; type; type = type->next, a++) {
		tmp.value = a;
		tmp.identifier = type->idname;
		tmp.name = type->name;
		RNA_enum_item_add(&item, &totitem, &tmp);
	}

	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
}

static int rna_workspace_engine_get(PointerRNA *ptr)
{
	WorkSpace *workspace = (WorkSpace *)ptr->data;
	RenderEngineType *type;
	int a = 0;

	for (type = R_engines.first; type; type = type->next, a++) {
		if (STREQ(type->idname, workspace->engine)) {
			return a;
		}
	}

	return 0;
}

static void rna_workspace_engine_update(Main *bmain, Scene *UNUSED(unused), PointerRNA *UNUSED(ptr))
{
	ED_render_engine_changed(bmain);
}

static int rna_workspace_multiple_engines_get(PointerRNA *UNUSED(ptr))
{
	return (BLI_listbase_count(&R_engines) > 1);
}

#else /* RNA_RUNTIME */

static void rna_def_workspace(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static EnumPropertyItem engine_items[] = {
	    {0, "BLENDER_RENDER", 0, "Blender Render", "Use the Blender internal rendering engine for rendering"},
	    {0, NULL, 0, NULL, NULL}
	};

	srna = RNA_def_struct(brna, "WorkSpace", "ID");
	RNA_def_struct_sdna(srna, "WorkSpace");
	RNA_def_struct_ui_text(srna, "Workspace", "Workspace data-block, defining the working environment for the user");
	/* TODO: real icon, just to show something */
	RNA_def_struct_ui_icon(srna, ICON_RENDER_RESULT);

	prop = RNA_def_property(srna, "screens", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "layouts", NULL);
	RNA_def_property_struct_type(prop, "Screen");
	RNA_def_property_collection_funcs(prop, "rna_workspace_screens_begin", NULL, NULL,
	                                  "rna_workspace_screens_item_get", NULL, NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "Screens", "Screen layouts of a workspace");

#ifdef USE_WORKSPACE_MODE
	prop = RNA_def_property(srna, "object_mode", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, rna_enum_object_mode_items);
	RNA_def_property_enum_funcs(prop, "rna_workspace_object_mode_get", "rna_workspace_object_mode_set", NULL);
	RNA_def_property_ui_text(prop, "Mode", "Object interaction mode");
#endif

	prop = RNA_def_property(srna, "orientations", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "transform_orientations", NULL);
	RNA_def_property_struct_type(prop, "TransformOrientation");
	RNA_def_property_collection_funcs(prop, "rna_workspace_transform_orientations_begin", NULL, NULL,
	                                  "rna_workspace_transform_orientations_item_get", NULL, NULL, NULL, NULL);
	RNA_def_property_ui_text(prop, "Transform Orientations", "");

	prop = RNA_def_property(srna, "render_layer", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "SceneLayer");
	RNA_def_property_pointer_funcs(prop, "rna_workspace_render_layer_get", "rna_workspace_render_layer_set",
	                               NULL, NULL);
	RNA_def_property_ui_text(prop, "Active Render Layer", "The active render layer used in this workspace");
	RNA_def_property_flag(prop, PROP_EDITABLE | PROP_NEVER_NULL);
	RNA_def_property_update(prop, NC_SCREEN | ND_LAYER, NULL);

	/* Engine. */
	prop = RNA_def_property(srna, "engine", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, engine_items);
	RNA_def_property_enum_funcs(prop, "rna_workspace_engine_get", "rna_workspace_engine_set",
	                            "rna_workspace_engine_itemf");
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_ui_text(prop, "Engine", "Engine to use for viewport drawing");
	RNA_def_property_update(prop, NC_WINDOW, "rna_workspace_engine_update");

	prop = RNA_def_property(srna, "has_multiple_engines", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_funcs(prop, "rna_workspace_multiple_engines_get", NULL);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Multiple Engines", "More than one rendering engine is available");

	/* Flags */
	prop = RNA_def_property(srna, "use_scene_settings", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
	RNA_def_property_boolean_sdna(prop, NULL, "flags", WORKSPACE_USE_SCENE_SETTINGS);
	RNA_def_property_ui_text(prop, "Scene Settings",
	                         "Use scene settings instead of workspace settings");
	RNA_def_property_update(prop, NC_SCREEN | ND_LAYER, NULL);
}

static void rna_def_transform_orientation(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "TransformOrientation", NULL);

	prop = RNA_def_property(srna, "matrix", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_float_sdna(prop, NULL, "mat");
	RNA_def_property_multi_array(prop, 2, rna_matrix_dimsize_3x3);
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);

	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_struct_name_property(srna, prop);
	RNA_def_property_ui_text(prop, "Name", "Name of the custom transform orientation");
	RNA_def_property_update(prop, NC_SPACE | ND_SPACE_VIEW3D, NULL);
}

void RNA_def_workspace(BlenderRNA *brna)
{
	rna_def_workspace(brna);
	rna_def_transform_orientation(brna);
}

#endif /* RNA_RUNTIME */
