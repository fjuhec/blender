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

/** \file blender/python/intern/bpy_widgetgroup_wrap.c
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
#include "bpy_widgetgroup_wrap.h"  /* own include */

/* we may want to add, but not now */
// #define USE_SRNA

static void widgetgroup_properties_init(wmManipulatorGroupType *mgrouptype)
{
#ifdef USE_SRNA
	PyTypeObject *py_class = mgrouptype->ext.data;
#endif
	RNA_struct_blender_type_set(mgrouptype->ext.srna, mgrouptype);

#ifdef USE_SRNA
	/* only call this so pyrna_deferred_register_class gives a useful error
	 * WM_operatortype_append_ptr will call RNA_def_struct_identifier
	 * later */
	RNA_def_struct_identifier(mgrouptype->srna, mgrouptype->idname);

	if (pyrna_deferred_register_class(mgrouptype->srna, py_class) != 0) {
		PyErr_Print(); /* failed to register operator props */
		PyErr_Clear();
	}
#endif
}

void widgetgroup_wrapper(wmManipulatorGroupType *mgrouptype, void *userdata)
{
	/* take care not to overwrite anything set in
	 * WM_manipulatorgrouptype_append_ptr before opfunc() is called */
#ifdef USE_SRNA
	StructRNA *srna = mgrouptype->srna;
#endif
	*mgrouptype = *((wmManipulatorGroupType *)userdata);
#ifdef USE_SRNA
	mgrouptype->srna = srna; /* restore */
#endif

#ifdef USE_SRNA
	/* Use i18n context from ext.srna if possible (py widgetgroups). */
	if (mgrouptype->ext.srna) {
		RNA_def_struct_translation_context(mgrouptype->srna, RNA_struct_translation_context(mgrouptype->ext.srna));
	}
#endif

	widgetgroup_properties_init(mgrouptype);
}
