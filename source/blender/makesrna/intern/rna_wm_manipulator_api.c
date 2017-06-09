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

static void rna_manipulator_draw_preset_box(
        wmManipulator *mpr, float matrix[16], int select_id)
{
	ED_manipulator_draw_preset_box(mpr, (float (*)[4])matrix, select_id);
}

static void rna_manipulator_draw_preset_arrow(
        wmManipulator *mpr, float matrix[16], int axis, int select_id)
{
	ED_manipulator_draw_preset_arrow(mpr, (float (*)[4])matrix, axis, select_id);
}

static void rna_manipulator_draw_preset_circle(
        wmManipulator *mpr, float matrix[16], int axis, int select_id)
{
	ED_manipulator_draw_preset_circle(mpr, (float (*)[4])matrix, axis, select_id);
}

static void rna_manipulator_draw_preset_facemap(
        wmManipulator *mpr, struct bContext *C, struct Object *ob, int facemap, int select_id)
{
	struct Scene *scene = CTX_data_scene(C);
	ED_manipulator_draw_preset_facemap(mpr, scene, ob, facemap, select_id);
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

	/* -------------------------------------------------------------------- */
	/* Primitive Shapes */

	/* draw_preset_box */
	func = RNA_def_function(srna, "draw_preset_box", "rna_manipulator_draw_preset_box");
	RNA_def_function_ui_description(func, "Draw a box");
	parm = RNA_def_property(func, "matrix", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_flag(parm, PARM_REQUIRED);
	RNA_def_property_multi_array(parm, 2, rna_matrix_dimsize_4x4);
	RNA_def_property_ui_text(parm, "", "The matrix to transform");
	RNA_def_int(func, "select_id", -1, -1, INT_MAX, "Zero when not selecting", "", -1, INT_MAX);

	/* draw_preset_box */
	func = RNA_def_function(srna, "draw_preset_arrow", "rna_manipulator_draw_preset_arrow");
	RNA_def_function_ui_description(func, "Draw a box");
	parm = RNA_def_property(func, "matrix", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_flag(parm, PARM_REQUIRED);
	RNA_def_property_multi_array(parm, 2, rna_matrix_dimsize_4x4);
	RNA_def_property_ui_text(parm, "", "The matrix to transform");
	RNA_def_enum(func, "axis", rna_enum_object_axis_items, 2, "", "Arrow Orientation");
	RNA_def_int(func, "select_id", -1, -1, INT_MAX, "Zero when not selecting", "", -1, INT_MAX);

	func = RNA_def_function(srna, "draw_preset_circle", "rna_manipulator_draw_preset_circle");
	RNA_def_function_ui_description(func, "Draw a box");
	parm = RNA_def_property(func, "matrix", PROP_FLOAT, PROP_MATRIX);
	RNA_def_property_flag(parm, PARM_REQUIRED);
	RNA_def_property_multi_array(parm, 2, rna_matrix_dimsize_4x4);
	RNA_def_property_ui_text(parm, "", "The matrix to transform");
	RNA_def_enum(func, "axis", rna_enum_object_axis_items, 2, "", "Arrow Orientation");
	RNA_def_int(func, "select_id", -1, -1, INT_MAX, "Zero when not selecting", "", -1, INT_MAX);

	/* -------------------------------------------------------------------- */
	/* Other Shapes */

	/* draw_preset_facemap */
	func = RNA_def_function(srna, "draw_preset_facemap", "rna_manipulator_draw_preset_facemap");
	RNA_def_function_ui_description(func, "Draw the face-map of a mesh object");
	RNA_def_function_flag(func, FUNC_USE_CONTEXT);
	parm = RNA_def_pointer(func, "object", "Object", "", "Object");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	RNA_def_int(func, "facemap", 0, 0, INT_MAX, "Face map index", "", 0, INT_MAX);
	RNA_def_int(func, "select_id", -1, -1, INT_MAX, "Zero when not selecting", "", -1, INT_MAX);
}


void RNA_api_manipulatorgroup(StructRNA *UNUSED(srna))
{
//	FunctionRNA *func;
//	PropertyRNA *parm;

#if 0
	/* utility, not for registering */
	func = RNA_def_function(srna, "report", "rna_Operator_report");
	parm = RNA_def_enum_flag(func, "type", wm_report_items, 0, "Type", "");
	RNA_def_property_flag(parm, PROP_REQUIRED);
	parm = RNA_def_string(func, "message", NULL, 0, "Report Message", "");
	RNA_def_property_flag(parm, PROP_REQUIRED);
#endif

}

#endif
