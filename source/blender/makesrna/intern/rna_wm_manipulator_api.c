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

/** \file blender/makesrna/intern/rna_wm_manipulator_api.c
 *  \ingroup RNA
 */


#include <stdlib.h>
#include <stdio.h>

#include "BLI_utildefines.h"

#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "DNA_windowmanager_types.h"

#include "WM_api.h"

#include "rna_internal.h"  /* own include */

#ifdef RNA_RUNTIME

#include "UI_interface.h"
#include "BKE_context.h"

#include "ED_manipulator_library.h"

static void rna_manipulator_draw_preset_box(wmManipulator *mpr, float matrix[16], int select_id)
{
	ED_manipulator_draw_preset_box(mpr, (float (*)[4])matrix, select_id);
}

#else

void RNA_api_manipulator(StructRNA *srna)
{
	/* Utility draw functions, since we don't expose new OpenGL drawing wrappers via Python yet.
	 * exactly how these should be exposed isn't totally clear.
	 * However it's probably good to have some high level API's for this anyway.
	 * Just note that this could be re-worked once tests are done.
	 */

	FunctionRNA *func;
	PropertyRNA *parm;

	/* draw */
	func = RNA_def_function(srna, "draw_preset_box", "rna_manipulator_draw_preset_box");
	RNA_def_function_ui_description(func, "Draw a box");
	parm = RNA_def_property(func, "matrix", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_flag(parm, PARM_REQUIRED);
	RNA_def_property_multi_array(parm, 2, rna_matrix_dimsize_4x4);
	RNA_def_property_ui_text(parm, "", "The matrix to transform");
	RNA_def_int(func, "select_id", 0, 0, INT_MAX, "Zero when not selecting", "", 0, INT_MAX);
}


void RNA_api_manipulatorgroup(StructRNA *srna)
{
	FunctionRNA *func;
	PropertyRNA *parm;

#if 0
	/* utility, not for registering */
	func = RNA_def_function(srna, "report", "rna_Operator_report");
	parm = RNA_def_enum_flag(func, "type", wm_report_items, 0, "Type", "");
	RNA_def_property_flag(parm, PROP_REQUIRED);
	parm = RNA_def_string(func, "message", NULL, 0, "Report Message", "");
	RNA_def_property_flag(parm, PROP_REQUIRED);
#endif


	/* Registration */

	/* poll */
	func = RNA_def_function(srna, "poll", NULL);
	RNA_def_function_ui_description(func, "Test if the operator can be called or not");
	RNA_def_function_flag(func, FUNC_NO_SELF | FUNC_REGISTER_OPTIONAL);
	RNA_def_function_return(func, RNA_def_boolean(func, "visible", 1, "", ""));
	parm = RNA_def_pointer(func, "context", "Context", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

	/* keymap_init */
	func = RNA_def_function(srna, "keymap_init", NULL);
	RNA_def_function_ui_description(func, "Initialize keymaps for this manipulator group");
	RNA_def_function_flag(func, FUNC_NO_SELF | FUNC_REGISTER);
	parm = RNA_def_pointer(func, "keyconf", "KeyConfig", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_property(func, "manipulator_group", PROP_STRING, PROP_NONE);
	RNA_def_property_ui_text(parm, "Manipulator Group", "Manipulator Group ID");
	// RNA_def_property_string_default(parm, "");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

	parm = RNA_def_pointer(func, "keymap", "KeyMap", "", "");
	RNA_def_property_flag(parm, PROP_NEVER_NULL);
	RNA_def_function_return(func, parm);

	/* draw */
	func = RNA_def_function(srna, "draw", NULL);
	RNA_def_function_ui_description(func, "Draw function for the operator");
	RNA_def_function_flag(func, FUNC_REGISTER);
	parm = RNA_def_pointer(func, "context", "Context", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
}

#endif
