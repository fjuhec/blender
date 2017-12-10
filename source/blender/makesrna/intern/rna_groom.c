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


static void rna_Groom_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *UNUSED(ptr))
{
	WM_main_add_notifier(NC_GROOM | NA_EDITED, NULL);
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
}

void RNA_def_groom(BlenderRNA *brna)
{
	rna_def_groom(brna);
}

#endif
