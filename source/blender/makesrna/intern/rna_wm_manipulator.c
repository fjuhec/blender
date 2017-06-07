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
 * Contributor(s): Blender Foundation (2008).
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/makesrna/intern/rna_wm.c
 *  \ingroup RNA
 */

#include <stdlib.h>

#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"
#include "DNA_view3d_types.h"
#include "DNA_windowmanager_types.h"
#include "DNA_manipulator_types.h"

#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "rna_internal.h"

#include "WM_types.h"

#ifdef RNA_RUNTIME
/* enum definitions */
#endif /* RNA_RUNTIME */

#ifdef RNA_RUNTIME

#include <assert.h>

#include "WM_api.h"

#include "DNA_workspace_types.h"

#include "ED_screen.h"

#include "UI_interface.h"

#include "BKE_global.h"
#include "BKE_idprop.h"
#include "BKE_workspace.h"

#include "MEM_guardedalloc.h"

#ifdef WITH_PYTHON
#  include "BPY_extern.h"
#endif

static void rna_manipulator_draw_cb(
        const struct bContext *C, struct wmManipulator *mpr)
{
	extern FunctionRNA rna_ManipulatorGroup_manipulator_draw_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mgroup_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mgroup->type->ext.srna, mgroup, &mgroup_ptr);
	/* RNA_struct_find_function(&mgroup_ptr, "manipulator_draw"); */
	func = &rna_ManipulatorGroup_manipulator_draw_func;
	RNA_parameter_list_create(&list, &mgroup_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	mgroup->type->ext.call((bContext *)C, &mgroup_ptr, func, &list);
	RNA_parameter_list_free(&list);
}

static void rna_manipulator_draw_select_cb(
        const struct bContext *C, struct wmManipulator *mpr, int select_id)
{
	extern FunctionRNA rna_ManipulatorGroup_manipulator_draw_select_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mgroup_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mgroup->type->ext.srna, mgroup, &mgroup_ptr);
	/* RNA_struct_find_function(&mgroup_ptr, "manipulator_draw_select"); */
	func = &rna_ManipulatorGroup_manipulator_draw_select_func;
	RNA_parameter_list_create(&list, &mgroup_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	RNA_parameter_set_lookup(&list, "select_id", &select_id);
	mgroup->type->ext.call((bContext *)C, &mgroup_ptr, func, &list);
	RNA_parameter_list_free(&list);
}

static int rna_manipulator_intersect_cb(
        struct bContext *C, struct wmManipulator *mpr, const struct wmEvent *event)
{
	extern FunctionRNA rna_ManipulatorGroup_manipulator_intersect_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mgroup_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mgroup->type->ext.srna, mgroup, &mgroup_ptr);
	/* RNA_struct_find_function(&mgroup_ptr, "manipulator_draw_select"); */
	func = &rna_ManipulatorGroup_manipulator_intersect_func;
	RNA_parameter_list_create(&list, &mgroup_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	RNA_parameter_set_lookup(&list, "event", &event);
	mgroup->type->ext.call((bContext *)C, &mgroup_ptr, func, &list);

	void *ret;
	RNA_parameter_get_lookup(&list, "intersect_id", &ret);
	int intersect_id = *(int *)ret;

	RNA_parameter_list_free(&list);
	return intersect_id;
}

static void rna_manipulator_handler_cb(
        struct bContext *C, struct wmManipulator *mpr, const struct wmEvent *event, int tweak)
{
	extern FunctionRNA rna_ManipulatorGroup_manipulator_handler_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mgroup_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mgroup->type->ext.srna, mgroup, &mgroup_ptr);
	/* RNA_struct_find_function(&mgroup_ptr, "manipulator_draw_select"); */
	func = &rna_ManipulatorGroup_manipulator_handler_func;
	RNA_parameter_list_create(&list, &mgroup_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	RNA_parameter_set_lookup(&list, "event", &event);
	RNA_parameter_set_lookup(&list, "tweak", &tweak);
	mgroup->type->ext.call((bContext *)C, &mgroup_ptr, func, &list);
	RNA_parameter_list_free(&list);
}

static void rna_manipulator_invoke_cb(
        struct bContext *C, struct wmManipulator *mpr, const struct wmEvent *event)
{
	extern FunctionRNA rna_ManipulatorGroup_manipulator_invoke_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mgroup_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mgroup->type->ext.srna, mgroup, &mgroup_ptr);
	/* RNA_struct_find_function(&mgroup_ptr, "manipulator_draw_select"); */
	func = &rna_ManipulatorGroup_manipulator_invoke_func;
	RNA_parameter_list_create(&list, &mgroup_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	RNA_parameter_set_lookup(&list, "event", &event);
	mgroup->type->ext.call((bContext *)C, &mgroup_ptr, func, &list);
	RNA_parameter_list_free(&list);
}

static void rna_manipulator_exit_cb(
        struct bContext *C, struct wmManipulator *mpr, bool cancel)
{
	extern FunctionRNA rna_ManipulatorGroup_manipulator_exit_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mgroup_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mgroup->type->ext.srna, mgroup, &mgroup_ptr);
	/* RNA_struct_find_function(&mgroup_ptr, "manipulator_draw_select"); */
	func = &rna_ManipulatorGroup_manipulator_exit_func;
	RNA_parameter_list_create(&list, &mgroup_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	{
		int cancel_i = cancel;
		RNA_parameter_set_lookup(&list, "cancel", &cancel_i);
	}
	mgroup->type->ext.call((bContext *)C, &mgroup_ptr, func, &list);
	RNA_parameter_list_free(&list);
}

static void rna_manipulator_select_cb(
        struct bContext *C, struct wmManipulator *mpr, int action)
{
	extern FunctionRNA rna_ManipulatorGroup_manipulator_select_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mgroup_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mgroup->type->ext.srna, mgroup, &mgroup_ptr);
	/* RNA_struct_find_function(&mgroup_ptr, "manipulator_draw_select"); */
	func = &rna_ManipulatorGroup_manipulator_select_func;
	RNA_parameter_list_create(&list, &mgroup_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	RNA_parameter_set_lookup(&list, "action", &action);
	mgroup->type->ext.call((bContext *)C, &mgroup_ptr, func, &list);
	RNA_parameter_list_free(&list);
}

/* Order must match definitions from 'RNA_api_manipulatorgroup'. */
enum {
	MANIPULATOR_FN_DRAW					= (1 << 0),
	MANIPULATOR_FN_DRAW_SELECT			= (1 << 1),
	MANIPULATOR_FN_INTERSECT			= (1 << 2),
	MANIPULATOR_FN_HANDLER				= (1 << 3),
	MANIPULATOR_FN_PROP_DATA_UPDATE		= (1 << 4),
	MANIPULATOR_FN_FINAL_POSITION_GET	= (1 << 5),
	MANIPULATOR_FN_INVOKE				= (1 << 6),
	MANIPULATOR_FN_EXIT					= (1 << 7),
	MANIPULATOR_FN_CURSOR_GET			= (1 << 8),
	MANIPULATOR_FN_SELECT				= (1 << 9),
};

static wmManipulator *rna_ManipulatorGroup_manipulator_new(wmManipulatorGroup *mgroup, const char *name)
{
	wmManipulator *mpr = WM_manipulator_new(mgroup, name);

	if (mgroup->type->rna_func_flag & MANIPULATOR_FN_DRAW) {
		WM_manipulator_set_fn_draw(mpr, rna_manipulator_draw_cb);
	}
	if (mgroup->type->rna_func_flag & MANIPULATOR_FN_DRAW_SELECT) {
		WM_manipulator_set_fn_draw_select(mpr, rna_manipulator_draw_select_cb);
	}
	if (mgroup->type->rna_func_flag & MANIPULATOR_FN_INTERSECT) {
		WM_manipulator_set_fn_intersect(mpr, rna_manipulator_intersect_cb);
	}
	if (mgroup->type->rna_func_flag & MANIPULATOR_FN_HANDLER) {
		WM_manipulator_set_fn_handler(mpr, rna_manipulator_handler_cb);
	}
	if (mgroup->type->rna_func_flag & MANIPULATOR_FN_PROP_DATA_UPDATE) {
		WM_manipulator_set_fn_prop_data_update(mpr, NULL /* TODO */ );
	}
	if (mgroup->type->rna_func_flag & MANIPULATOR_FN_FINAL_POSITION_GET) {
		WM_manipulator_set_fn_final_position_get(mpr, NULL /* TODO */ );
	}
	if (mgroup->type->rna_func_flag & MANIPULATOR_FN_INVOKE) {
		WM_manipulator_set_fn_invoke(mpr, rna_manipulator_invoke_cb);
	}
	if (mgroup->type->rna_func_flag & MANIPULATOR_FN_EXIT) {
		WM_manipulator_set_fn_exit(mpr, rna_manipulator_exit_cb);
	}
	if (mgroup->type->rna_func_flag & MANIPULATOR_FN_CURSOR_GET) {
		WM_manipulator_set_fn_cursor_get(mpr, NULL /* TODO */ );
	}
	if (mgroup->type->rna_func_flag & MANIPULATOR_FN_SELECT) {
		WM_manipulator_set_fn_select(mpr, rna_manipulator_select_cb);
	}

	return mpr;
}

static void rna_ManipulatorGroup_manipulator_remove(
        wmManipulatorGroup *mgroup, bContext *C, wmManipulator *manipulator)
{
	WM_manipulator_delete(&mgroup->manipulators, mgroup->parent_mmap, manipulator, C);
}

static void rna_ManipulatorGroup_manipulator_clear(
        wmManipulatorGroup *mgroup, bContext *C)
{
	while (mgroup->manipulators.first) {
		WM_manipulator_delete(&mgroup->manipulators, mgroup->parent_mmap, mgroup->manipulators.first, C);
	}
}

static void rna_ManipulatorGroup_name_get(PointerRNA *ptr, char *value)
{
	wmManipulatorGroup *wgroup = ptr->data;
	strcpy(value, wgroup->type->name);
	(void)wgroup;
}

static int rna_ManipulatorGroup_name_length(PointerRNA *ptr)
{
	wmManipulatorGroup *wgroup = ptr->data;
	return strlen(wgroup->type->name);
	(void)wgroup;
}

static void rna_ManipulatorGroup_bl_label_set(PointerRNA *ptr, const char *value)
{
	wmManipulatorGroup *data = ptr->data;
	const char *str = data->type->name;
	if (!str)
		str = value;
	else
		assert(!"setting the bl_label on a non-builtin operator");
}

static int rna_ManipulatorGroup_has_reports_get(PointerRNA *ptr)
{
	wmManipulatorGroup *wgroup = ptr->data;
	return (wgroup->reports && wgroup->reports->list.first);
}

#ifdef WITH_PYTHON
static void rna_ManipulatorGroup_unregister(struct Main *bmain, StructRNA *type)
{
	//const char *idname;
	wmManipulatorGroupType *wgrouptype = RNA_struct_blender_type_get(type);
	//wmWindowManager *wm;
	//wmManipulatorMapType *wmap = NULL;

	if (!wgrouptype)
		return;

	WM_main_add_notifier(NC_SCREEN | NA_EDITED, NULL);

	RNA_struct_free_extension(type, &wgrouptype->ext);

	WM_manipulatorgrouptype_unregister(NULL, bmain, wgrouptype);
	//WM_operatortype_remove_ptr(ot);

	RNA_struct_free(&BLENDER_RNA, type);
}

static bool manipulatorgroup_poll(const bContext *C, wmManipulatorGroupType *wgrouptype)
{

	extern FunctionRNA rna_ManipulatorGroup_poll_func;

	PointerRNA ptr;
	ParameterList list;
	FunctionRNA *func;
	void *ret;
	int visible;

	RNA_pointer_create(NULL, wgrouptype->ext.srna, NULL, &ptr); /* dummy */
	func = &rna_ManipulatorGroup_poll_func; /* RNA_struct_find_function(&ptr, "poll"); */

	RNA_parameter_list_create(&list, &ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	wgrouptype->ext.call((bContext *)C, &ptr, func, &list);

	RNA_parameter_get_lookup(&list, "visible", &ret);
	visible = *(int *)ret;

	RNA_parameter_list_free(&list);

	return visible;
}

static void manipulatorgroup_draw(const bContext *C, wmManipulatorGroup *wgroup)
{
	extern FunctionRNA rna_ManipulatorGroup_draw_func;

	PointerRNA wgroup_ptr;
	ParameterList list;
	FunctionRNA *func;

	RNA_pointer_create(NULL, wgroup->type->ext.srna, wgroup, &wgroup_ptr);
	func = &rna_ManipulatorGroup_draw_func; /* RNA_struct_find_function(&wgroupr, "draw"); */

	RNA_parameter_list_create(&list, &wgroup_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	wgroup->type->ext.call((bContext *)C, &wgroup_ptr, func, &list);

	RNA_parameter_list_free(&list);
}

static wmKeyMap *manipulatorgroup_keymap_init(const wmManipulatorGroupType *wgrouptype, wmKeyConfig *config)
{
	extern FunctionRNA rna_ManipulatorGroup_keymap_init_func;
	const char *wgroupname = wgrouptype->name;
	void *ret;

	PointerRNA ptr;
	ParameterList list;
	FunctionRNA *func;

	RNA_pointer_create(NULL, wgrouptype->ext.srna, NULL, &ptr); /* dummy */
	func = &rna_ManipulatorGroup_keymap_init_func; /* RNA_struct_find_function(&wgroupr, "keymap_init"); */

	RNA_parameter_list_create(&list, &ptr, func);
	RNA_parameter_set_lookup(&list, "keyconfig", &config);
	RNA_parameter_set_lookup(&list, "group_name", &wgroupname);
	wgrouptype->ext.call(NULL, &ptr, func, &list);

	RNA_parameter_get_lookup(&list, "keymap", &ret);
	wmKeyMap *keymap = *(wmKeyMap **)ret;

	RNA_parameter_list_free(&list);

	return keymap;
}

#if 0

/* same as exec(), but call cancel */
static void operator_cancel(bContext *C, wmManipulatorGroup *op)
{
	extern FunctionRNA rna_ManipulatorGroup_cancel_func;

	PointerRNA opr;
	ParameterList list;
	FunctionRNA *func;

	RNA_pointer_create(NULL, op->type->ext.srna, op, &opr);
	func = &rna_ManipulatorGroup_cancel_func; /* RNA_struct_find_function(&opr, "cancel"); */

	RNA_parameter_list_create(&list, &opr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	op->type->ext.call(C, &opr, func, &list);

	RNA_parameter_list_free(&list);
}
#endif

static void rna_Manipulator_color_get(PointerRNA *ptr, float *values)
{
	const wmManipulator *mnp = ptr->data;
	WM_manipulator_get_color(mnp, values);
}
static void rna_Manipulator_color_set(PointerRNA *ptr, const float *values)
{
	wmManipulator *mnp = ptr->data;
	WM_manipulator_set_color(mnp, values);
}

static void rna_Manipulator_color_hi_get(PointerRNA *ptr, float *values)
{
	const wmManipulator *mnp = ptr->data;
	WM_manipulator_get_color_highlight(mnp, values);
}
static void rna_Manipulator_color_hi_set(PointerRNA *ptr, const float *values)
{
	wmManipulator *mnp = ptr->data;
	WM_manipulator_set_color_highlight(mnp, values);
}

void manipulatorgroup_wrapper(wmManipulatorGroupType *mgrouptype, void *userdata);

static StructRNA *rna_ManipulatorGroup_register(
        Main *bmain, ReportList *reports, void *data, const char *identifier,
        StructValidateFunc validate, StructCallbackFunc call, StructFreeFunc free)
{
	wmManipulatorGroupType dummywgt = {NULL};
	wmManipulatorGroup dummywg = {NULL};
	PointerRNA wgptr;

	/* Two sets of functions. */
#define GROUP_FN_LEN 3
#define MANIP_FN_LEN 7
	int have_function[GROUP_FN_LEN + MANIP_FN_LEN];

	/* setup dummy manipulatorgroup & manipulatorgroup type to store static properties in */
	dummywg.type = &dummywgt;
	RNA_pointer_create(NULL, &RNA_ManipulatorGroup, &dummywg, &wgptr);

	/* validate the python class */
	if (validate(&wgptr, data, have_function) != 0)
		return NULL;

	if (strlen(identifier) >= sizeof(dummywgt.idname)) {
		BKE_reportf(reports, RPT_ERROR, "Registering manipulatorgroup class: '%s' is too long, maximum length is %d",
		            identifier, (int)sizeof(dummywgt.idname));
		return NULL;
	}

	/* check if the area supports widgets */
	const struct wmManipulatorMapType_Params wmap_params = {
		.idname = dummywgt.idname,
		.spaceid = dummywgt.spaceid,
		.regionid = dummywgt.regionid,
	};

	wmManipulatorMapType *wmaptype = WM_manipulatormaptype_ensure(&wmap_params);
	if (wmaptype == NULL) {
		BKE_reportf(reports, RPT_ERROR, "Area type does not support manipulators");
		return NULL;
	}

	/* check if we have registered this manipulatorgroup type before, and remove it */
	{
		wmManipulatorGroupType *wgrouptype = WM_manipulatorgrouptype_find(wmaptype, dummywgt.idname);
		if (wgrouptype && wgrouptype->ext.srna) {
			WM_main_add_notifier(NC_SCREEN | NA_EDITED, NULL);
			WM_manipulatorgrouptype_unregister(NULL, bmain, wgrouptype);
		}
	}

	/* create a new manipulatorgroup type */
	dummywgt.ext.srna = RNA_def_struct_ptr(&BLENDER_RNA, dummywgt.idname, &RNA_ManipulatorGroup);
	RNA_def_struct_flag(dummywgt.ext.srna, STRUCT_NO_IDPROPERTIES); /* manipulatorgroup properties are registered separately */
	dummywgt.ext.data = data;
	dummywgt.ext.call = call;
	dummywgt.ext.free = free;

	/* We used to register widget group types like this, now we do it similar to
	 * operator types. Thus we should be able to do the same as operator types now. */
	dummywgt.poll = (have_function[0]) ? manipulatorgroup_poll : NULL;
	dummywgt.keymap_init = (have_function[1]) ? manipulatorgroup_keymap_init : NULL;
	dummywgt.init = (have_function[2]) ? manipulatorgroup_draw : NULL;
	/* XXX, expose */
	dummywgt.flag = WM_MANIPULATORGROUPTYPE_IS_3D;

	{
		const int *have_function_manipulator = &have_function[GROUP_FN_LEN];
		for (int i = 0; i < MANIP_FN_LEN; i++) {
			if (have_function_manipulator[i]) {
				dummywgt.rna_func_flag |= (1 << i);
			}
		}
	}

	WM_manipulatorgrouptype_append_ptr(wmaptype, manipulatorgroup_wrapper, (void *)&dummywgt);

	/* TODO: WM_manipulatorgrouptype_init_runtime */

	RNA_def_struct_duplicate_pointers(dummywgt.ext.srna);

	/* update while blender is running */
	WM_main_add_notifier(NC_SCREEN | NA_EDITED, NULL);

	return dummywgt.ext.srna;

#undef GROUP_FN_LEN
#undef MANIP_FN_LEN
}

static void **rna_ManipulatorGroup_instance(PointerRNA *ptr)
{
	wmManipulatorGroup *wgroup = ptr->data;
	return &wgroup->py_instance;
}

static StructRNA *rna_ManipulatorGroup_refine(PointerRNA *wgroup_ptr)
{
	wmManipulatorGroup *wgroup = wgroup_ptr->data;
	return (wgroup->type && wgroup->type->ext.srna) ? wgroup->type->ext.srna : &RNA_ManipulatorGroup;
}

#endif

#else /* RNA_RUNTIME */

static void rna_def_manipulator(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;
	PropertyRNA *prop;

	RNA_def_property_srna(cprop, "Manipulator");
	srna = RNA_def_struct(brna, "Manipulator", NULL);
	RNA_def_struct_sdna(srna, "wmManipulator");
	RNA_def_struct_ui_text(srna, "Manipulator", "Collection of manipulators");

	prop = RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_array(prop, 4);
	RNA_def_property_float_funcs(prop, "rna_Manipulator_color_get", "rna_Manipulator_color_set", NULL);

	prop = RNA_def_property(srna, "color_highlight", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_array(prop, 4);
	RNA_def_property_float_funcs(prop, "rna_Manipulator_color_hi_get", "rna_Manipulator_color_hi_set", NULL);

	RNA_def_property_ui_text(prop, "Color", "");

	RNA_api_manipulator(srna);
}

/* ManipulatorGroup.manipulators */
static void rna_def_manipulators(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;

	FunctionRNA *func;
	PropertyRNA *parm;

	RNA_def_property_srna(cprop, "Manipulators");
	srna = RNA_def_struct(brna, "Manipulators", NULL);
	RNA_def_struct_sdna(srna, "wmManipulatorGroup");
	RNA_def_struct_ui_text(srna, "Manipulators", "Collection of manipulators");

	func = RNA_def_function(srna, "new", "rna_ManipulatorGroup_manipulator_new");
	RNA_def_function_ui_description(func, "Add manipulator");
	RNA_def_string(func, "name", "Manipulator", 0, "", "Manipulator name"); /* optional */
	parm = RNA_def_pointer(func, "manipulator", "Manipulator", "", "New manipulator");
	RNA_def_function_return(func, parm);

	func = RNA_def_function(srna, "remove", "rna_ManipulatorGroup_manipulator_remove");
	RNA_def_function_flag(func, FUNC_USE_CONTEXT);
	RNA_def_function_ui_description(func, "Delete manipulator");
	parm = RNA_def_pointer(func, "manipulator", "Manipulator", "", "New manipulator");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);

	func = RNA_def_function(srna, "clear", "rna_ManipulatorGroup_manipulator_clear");
	RNA_def_function_flag(func, FUNC_USE_CONTEXT);
	RNA_def_function_ui_description(func, "Delete all manipulators");
}

static void rna_def_manipulatorgroup(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "ManipulatorGroup", NULL);
	RNA_def_struct_ui_text(srna, "ManipulatorGroup", "Storage of an operator being executed, or registered after execution");
	RNA_def_struct_sdna(srna, "wmManipulatorGroup");
	RNA_def_struct_refine_func(srna, "rna_ManipulatorGroup_refine");
#ifdef WITH_PYTHON
	RNA_def_struct_register_funcs(
	        srna,
	        "rna_ManipulatorGroup_register",
	        "rna_ManipulatorGroup_unregister",
	        "rna_ManipulatorGroup_instance");
#endif
	RNA_def_struct_translation_context(srna, BLT_I18NCONTEXT_OPERATOR_DEFAULT);

	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_string_funcs(prop, "rna_ManipulatorGroup_name_get", "rna_ManipulatorGroup_name_length", NULL);
	RNA_def_property_ui_text(prop, "Name", "");

	prop = RNA_def_property(srna, "has_reports", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE); /* this is 'virtual' property */
	RNA_def_property_boolean_funcs(prop, "rna_ManipulatorGroup_has_reports_get", NULL);
	RNA_def_property_ui_text(prop, "Has Reports",
	                         "ManipulatorGroup has a set of reports (warnings and errors) from last execution");

	/* Registration */
	prop = RNA_def_property(srna, "bl_idname", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "type->idname");
	/* RNA_def_property_clear_flag(prop, PROP_EDITABLE); */
	RNA_def_property_flag(prop, PROP_REGISTER);
	RNA_def_struct_name_property(srna, prop);

	prop = RNA_def_property(srna, "bl_label", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "type->name");
	RNA_def_property_string_maxlength(prop, 64); /* else it uses the pointer size! */
	RNA_def_property_string_funcs(prop, NULL, NULL, "rna_ManipulatorGroup_bl_label_set");
	/* RNA_def_property_clear_flag(prop, PROP_EDITABLE); */
	RNA_def_property_flag(prop, PROP_REGISTER);

	prop = RNA_def_property(srna, "bl_space_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "type->spaceid");
	RNA_def_property_enum_items(prop, rna_enum_space_type_items);
	RNA_def_property_flag(prop, PROP_REGISTER);
	RNA_def_property_ui_text(prop, "Space type", "The space where the panel is going to be used in");

	prop = RNA_def_property(srna, "bl_region_type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "type->regionid");
	RNA_def_property_enum_items(prop, rna_enum_region_type_items);
	RNA_def_property_flag(prop, PROP_REGISTER);
	RNA_def_property_ui_text(prop, "Region Type", "The region where the panel is going to be used in");

#if 0
	prop = RNA_def_property(srna, "bl_options", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "type->flag");
	RNA_def_property_enum_items(prop, manipulator_flag_items);
	RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL | PROP_ENUM_FLAG);
	RNA_def_property_ui_text(prop, "Options",  "Options for this manipulator type");
#endif

	prop = RNA_def_property(srna, "manipulators", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "manipulators", NULL);
	RNA_def_property_struct_type(prop, "Manipulator");
	RNA_def_property_ui_text(prop, "Manipulators", "List of manipulators in the Manipulator Map");
	rna_def_manipulator(brna, prop);
	rna_def_manipulators(brna, prop);

	RNA_api_manipulatorgroup(srna);
}

void RNA_def_wm_manipulator(BlenderRNA *brna)
{
	rna_def_manipulatorgroup(brna);
}

#endif /* RNA_RUNTIME */
