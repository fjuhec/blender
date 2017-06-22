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

/** \file blender/python/intern/bpy_rna_manipulator.c
 *  \ingroup pythonintern
 *
 * .
 */

#include <Python.h>
#include <stddef.h>

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"

#include "BKE_main.h"

#include "WM_api.h"
#include "WM_types.h"

#include "bpy_util.h"
#include "bpy_rna_manipulator.h"

#include "../generic/py_capi_utils.h"
#include "../generic/python_utildefines.h"

#include "RNA_access.h"
#include "RNA_types.h"
#include "RNA_enum_types.h"

#include "bpy_rna.h"

enum {
	BPY_MANIPULATOR_FN_SLOT_GET = 0,
	BPY_MANIPULATOR_FN_SLOT_SET,
	BPY_MANIPULATOR_FN_SLOT_RANGE,
};
#define BPY_MANIPULATOR_FN_SLOT_LEN (BPY_MANIPULATOR_FN_SLOT_RANGE + 1)

struct BPyManipulatorHandlerUserData {

	PyObject *fn_slots[BPY_MANIPULATOR_FN_SLOT_LEN];
};

static void py_rna_manipulator_handler_get_cb(
        const wmManipulator *UNUSED(mpr), wmManipulatorProperty *mpr_prop,
        float *value)
{
	PyGILState_STATE gilstate = PyGILState_Ensure();

	struct BPyManipulatorHandlerUserData *data = mpr_prop->custom_func.user_data;
	PyObject *ret = PyObject_CallObject(data->fn_slots[BPY_MANIPULATOR_FN_SLOT_GET], NULL);
	if (ret == NULL) {
		goto fail;
	}

	if (mpr_prop->type->type == PROP_FLOAT) {
		if (mpr_prop->type->array_length == 1) {
			if (((*value = PyFloat_AsDouble(ret)) == -1.0f && PyErr_Occurred()) == 0) {
				goto fail;
			}
		}
		else {
			if (PyC_AsArray(value, ret, mpr_prop->type->array_length, &PyFloat_Type, false,
			                "Manipulator get callback: ") == -1)
			{
				goto fail;
			}
		}
	}
	else {
		PyErr_SetString(PyExc_AttributeError, "internal error, unsupported type");
		goto fail;
	}

	Py_DECREF(ret);

	PyGILState_Release(gilstate);
	return;

fail:
	PyErr_Print();
	PyErr_Clear();

	PyGILState_Release(gilstate);
}

static void py_rna_manipulator_handler_set_cb(
        const wmManipulator *UNUSED(mpr), wmManipulatorProperty *mpr_prop,
        const float *value)
{
	PyGILState_STATE gilstate = PyGILState_Ensure();

	struct BPyManipulatorHandlerUserData *data = mpr_prop->custom_func.user_data;

	PyObject *args = PyTuple_New(1);

	if (mpr_prop->type->type == PROP_FLOAT) {
		PyObject *py_value;
		if (mpr_prop->type->array_length == 1) {
			py_value = PyFloat_FromDouble(*value);
		}
		else {
			py_value =  PyC_FromArray((void *)value, mpr_prop->type->array_length, &PyFloat_Type, false,
			                          "Manipulator set callback: ");
		}
		if (py_value == NULL) {
			goto fail;
		}
		PyTuple_SET_ITEM(args, 0, py_value);
	}
	else {
		PyErr_SetString(PyExc_AttributeError, "internal error, unsupported type");
		goto fail;
	}

	PyObject *ret = PyObject_CallObject(data->fn_slots[BPY_MANIPULATOR_FN_SLOT_SET], args);
	if (ret == NULL) {
		goto fail;
	}
	Py_DECREF(ret);

	PyGILState_Release(gilstate);
	return;

fail:
	PyErr_Print();
	PyErr_Clear();

	Py_DECREF(args);

	PyGILState_Release(gilstate);
}

static void py_rna_manipulator_handler_free_cb(
        const wmManipulator *UNUSED(mpr), wmManipulatorProperty *mpr_prop)
{
	struct BPyManipulatorHandlerUserData *data = mpr_prop->custom_func.user_data;

	PyGILState_STATE gilstate = PyGILState_Ensure();
	for (int i = 0; i < BPY_MANIPULATOR_FN_SLOT_LEN; i++) {
		Py_XDECREF(data->fn_slots[i]);
	}
	PyGILState_Release(gilstate);

	MEM_freeN(data);

}

PyDoc_STRVAR(bpy_manipulator_target_set_handler_doc,
".. method:: target_set_handler():\n"
"\n"
"   TODO.\n"
);
static PyObject *bpy_manipulator_target_set_handler(PyObject *UNUSED(self), PyObject *args, PyObject *kwds)
{
	/* Note: this is a counter-part to RNA function:
	 * 'Manipulator.target_set_prop' (see: rna_wm_manipulator_api.c). conventions should match. */
	static const char * const _keywords[] = {"self", "target", "get", "set", "range", NULL};
	static _PyArg_Parser _parser = {"Os|$OOO:target_set_handler", _keywords, 0};

	PyGILState_STATE gilstate = PyGILState_Ensure();

	struct {
		PyObject *self;
		char *target;
		PyObject *py_fn_slots[BPY_MANIPULATOR_FN_SLOT_LEN];
	} params = {
		.self = NULL,
		.target = NULL,
		.py_fn_slots = {NULL},
	};

	if (!_PyArg_ParseTupleAndKeywordsFast(
	        args, kwds,
	        &_parser,
	        &params.self,
	        &params.target,
	        &params.py_fn_slots[BPY_MANIPULATOR_FN_SLOT_GET],
	        &params.py_fn_slots[BPY_MANIPULATOR_FN_SLOT_SET],
	        &params.py_fn_slots[BPY_MANIPULATOR_FN_SLOT_RANGE]))
	{
		goto fail;
	}

	wmManipulator *mpr = ((BPy_StructRNA *)params.self)->ptr.data;

	const wmManipulatorPropertyType *mpr_prop_type =
	        WM_manipulatortype_target_property_find(mpr->type, params.target);
	if (mpr_prop_type == NULL) {
		PyErr_Format(PyExc_ValueError,
		             "Manipulator target property '%s.%s' not found",
		             mpr->type->idname, params.target);
		goto fail;
	}

	if ((!PyCallable_Check(params.py_fn_slots[BPY_MANIPULATOR_FN_SLOT_GET])) ||
	    (!PyCallable_Check(params.py_fn_slots[BPY_MANIPULATOR_FN_SLOT_SET])) ||
	    ((params.py_fn_slots[BPY_MANIPULATOR_FN_SLOT_RANGE] != NULL) &&
	     !PyCallable_Check(params.py_fn_slots[BPY_MANIPULATOR_FN_SLOT_RANGE])))
	{
		PyErr_SetString(PyExc_RuntimeError, "Non callable passed as handler");
		goto fail;
	}

	struct BPyManipulatorHandlerUserData *data = MEM_callocN(sizeof(*data), __func__);

	for (int i = 0; i < BPY_MANIPULATOR_FN_SLOT_LEN; i++) {
		data->fn_slots[i] = params.py_fn_slots[i];
		Py_XINCREF(params.py_fn_slots[i]);
	}

	WM_manipulator_target_property_def_func_ptr(
	        mpr, mpr_prop_type,
	        &(const struct wmManipulatorPropertyFnParams) {
	            .value_get_fn = py_rna_manipulator_handler_get_cb,
	            .value_set_fn = py_rna_manipulator_handler_set_cb,
	            .range_get_fn = NULL,
	            .free_fn = py_rna_manipulator_handler_free_cb,
	            .user_data = data,
	        });

	PyGILState_Release(gilstate);

	Py_RETURN_NONE;

fail:
	PyGILState_Release(gilstate);
	return NULL;
}

int BPY_rna_manipulator_module(PyObject *mod_par)
{
	static PyMethodDef method_def = {
	    "target_set_handler", (PyCFunction)bpy_manipulator_target_set_handler, METH_VARARGS | METH_KEYWORDS,
	    bpy_manipulator_target_set_handler_doc};

	PyObject *func = PyCFunction_New(&method_def, NULL);
	PyObject *func_inst = PyInstanceMethod_New(func);


	/* TODO, return a type that binds nearly to a method. */
	PyModule_AddObject(mod_par, "_rna_manipulator_target_set_handler", func_inst);

	return 0;
}


