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
 * The Original Code is Copyright (C) 2016 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Kevin Dietrich.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include "DNA_cachefile_types.h"

#include "RNA_define.h"
#include "RNA_access.h"

#include "rna_internal.h"

#ifdef RNA_RUNTIME
#else

static void rna_def_cachefile(BlenderRNA *brna)
{
	StructRNA *srna = RNA_def_struct(brna, "CacheFile", "ID");
	RNA_def_struct_sdna(srna, "CacheFile");
	RNA_def_struct_ui_text(srna, "CacheFile", "");
	RNA_def_struct_ui_icon(srna, ICON_FILE);

	PropertyRNA *prop = RNA_def_property(srna, "filepath", PROP_STRING, PROP_FILEPATH);
	RNA_def_property_ui_text(prop, "File Path", "Path to external displacements file");
	RNA_def_property_update(prop, 0, NULL);

	prop = RNA_def_property(srna, "is_sequence", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_ui_text(prop, "Sequence", "Whether the cache is separated in a series of files");
	RNA_def_property_update(prop, 0, NULL);
}

void RNA_def_cachefile(BlenderRNA *brna)
{
	rna_def_cachefile(brna);
}

#endif
