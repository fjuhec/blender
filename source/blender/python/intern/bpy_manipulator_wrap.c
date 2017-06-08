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

/** \file blender/python/intern/bpy_manipulator_wrap.c
 *  \ingroup pythonintern
 *
 * This file is so Python can define widget-group's that C can call into.
 * The generic callback functions for Python widget-group are defines in
 * 'rna_wm.c', some calling into functions here to do python specific
 * functionality.
 *
 * \note This follows 'bpy_operator_wrap.c' very closely.
 * Keep in sync unless there is good reason not to!
 */

#include <Python.h>

#include "BLI_utildefines.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "bpy_rna.h"
#include "bpy_intern_string.h"
#include "bpy_manipulator_wrap.h"  /* own include */

/* we may want to add, but not now */
// #define USE_SRNA



/* -------------------------------------------------------------------- */

/** \name Manipulator
 * \{ */


static void manipulator_properties_init(wmManipulatorType *wt)
{
#ifdef USE_SRNA
	PyTypeObject *py_class = wt->ext.data;
#endif
	RNA_struct_blender_type_set(wt->ext.srna, wt);

#ifdef USE_SRNA
	/* only call this so pyrna_deferred_register_class gives a useful error
	 * WM_operatortype_append_ptr will call RNA_def_struct_identifier
	 * later */
	RNA_def_struct_identifier(wt->srna, wt->idname);

	if (pyrna_deferred_register_class(wt->srna, py_class) != 0) {
		PyErr_Print(); /* failed to register operator props */
		PyErr_Clear();
	}
#endif
}

void BPY_RNA_manipulator_wrapper(wmManipulatorType *wt, void *userdata)
{
	/* take care not to overwrite anything set in
	 * WM_manipulatorgrouptype_append_ptr before opfunc() is called */
#ifdef USE_SRNA
	StructRNA *srna = wt->srna;
#endif
	*wt = *((wmManipulatorType *)userdata);
#ifdef USE_SRNA
	wt->srna = srna; /* restore */
#endif

#ifdef USE_SRNA
	/* Use i18n context from ext.srna if possible (py manipulatorgroups). */
	if (wt->ext.srna) {
		RNA_def_struct_translation_context(wt->srna, RNA_struct_translation_context(wt->ext.srna));
	}
#endif

	wt->struct_size = sizeof(wmManipulator);

	manipulator_properties_init(wt);
}

/** \} */


/* -------------------------------------------------------------------- */

/** \name Manipulator Group
 * \{ */

static void manipulatorgroup_properties_init(wmManipulatorGroupType *wgt)
{
#ifdef USE_SRNA
	PyTypeObject *py_class = wgt->ext.data;
#endif
	RNA_struct_blender_type_set(wgt->ext.srna, wgt);

#ifdef USE_SRNA
	/* only call this so pyrna_deferred_register_class gives a useful error
	 * WM_operatortype_append_ptr will call RNA_def_struct_identifier
	 * later */
	RNA_def_struct_identifier(wgt->srna, wgt->idname);

	if (pyrna_deferred_register_class(wgt->srna, py_class) != 0) {
		PyErr_Print(); /* failed to register operator props */
		PyErr_Clear();
	}
#endif
}

void BPY_RNA_manipulatorgroup_wrapper(wmManipulatorGroupType *wgt, void *userdata)
{
	/* take care not to overwrite anything set in
	 * WM_manipulatorgrouptype_append_ptr before opfunc() is called */
#ifdef USE_SRNA
	StructRNA *srna = wgt->srna;
#endif
	*wgt = *((wmManipulatorGroupType *)userdata);
#ifdef USE_SRNA
	wgt->srna = srna; /* restore */
#endif

#ifdef USE_SRNA
	/* Use i18n context from ext.srna if possible (py manipulatorgroups). */
	if (wgt->ext.srna) {
		RNA_def_struct_translation_context(wgt->srna, RNA_struct_translation_context(wgt->ext.srna));
	}
#endif

	manipulatorgroup_properties_init(wgt);
}

/** \} */

