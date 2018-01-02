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
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Lukas Toenne
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/makesrna/intern/rna_groom.c
 *  \ingroup RNA
 */

#include <stdlib.h>

#include "DNA_groom_types.h"
#include "DNA_scene_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_string_utils.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "rna_internal.h"

#include "WM_types.h"
#include "DNA_object_types.h"


#ifdef RNA_RUNTIME

#include "WM_api.h"

#include "BKE_groom.h"

#include "DEG_depsgraph.h"

static void UNUSED_FUNCTION(rna_Groom_update)(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *UNUSED(ptr))
{
	WM_main_add_notifier(NC_GROOM | NA_EDITED, NULL);
}

static void rna_Groom_update_data(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	DEG_id_tag_update(ptr->id.data, 0);
	WM_main_add_notifier(NC_GROOM | ND_DATA, ptr->id.data);
}

#else

static void rna_def_groom(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	srna = RNA_def_struct(brna, "Groom", "ID");
	RNA_def_struct_sdna(srna, "Groom");
	RNA_def_struct_ui_text(srna, "Groom", "Guide curve geometry for hair");
	RNA_def_struct_ui_icon(srna, ICON_NONE);
	
	/* Animation Data */
	rna_def_animdata_common(srna);
	
	prop = RNA_def_property(srna, "curve_resolution", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "curve_res");
	RNA_def_property_range(prop, 1, 1024);
	RNA_def_property_ui_range(prop, 1, 64, 1, -1);
	RNA_def_property_ui_text(prop, "Curve Resolution", "Curve subdivisions per segment");
	RNA_def_property_update(prop, 0, "rna_Groom_update_data");
	
	prop = RNA_def_property(srna, "hair_system", PROP_POINTER, PROP_NONE);
	RNA_def_property_ui_text(prop, "Hair", "Hair data");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	
	prop = RNA_def_property(srna, "hair_draw_settings", PROP_POINTER, PROP_NONE);
	RNA_def_property_ui_text(prop, "Hair Draw Settings", "Hair draw settings");
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	
	prop = RNA_def_property(srna, "scalp_object", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "scalp_object");
	RNA_def_property_ui_text(prop, "Scalp Object", "Surface for attaching hairs");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_update(prop, 0, "rna_Groom_update_data");
	
	UNUSED_VARS(prop);
}

void RNA_def_groom(BlenderRNA *brna)
{
	rna_def_groom(brna);
}

#endif
