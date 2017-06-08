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

/** \file blender/makesrna/intern/rna_wm_manipulator.c
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

#include "WM_api.h"
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

/* -------------------------------------------------------------------- */

/** \name Manipulator API
 * \{ */

static void rna_manipulator_draw_cb(
        const struct bContext *C, struct wmManipulator *mpr)
{
	extern FunctionRNA rna_Manipulator_draw_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mpr_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mpr->type->ext.srna, mpr, &mpr_ptr);
	/* RNA_struct_find_function(&mpr_ptr, "draw"); */
	func = &rna_Manipulator_draw_func;
	RNA_parameter_list_create(&list, &mpr_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	mgroup->type->ext.call((bContext *)C, &mpr_ptr, func, &list);
	RNA_parameter_list_free(&list);
}

static void rna_manipulator_draw_select_cb(
        const struct bContext *C, struct wmManipulator *mpr, int select_id)
{
	extern FunctionRNA rna_Manipulator_draw_select_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mpr_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mpr->type->ext.srna, mpr, &mpr_ptr);
	/* RNA_struct_find_function(&mpr_ptr, "draw_select"); */
	func = &rna_Manipulator_draw_select_func;
	RNA_parameter_list_create(&list, &mpr_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	RNA_parameter_set_lookup(&list, "select_id", &select_id);
	mgroup->type->ext.call((bContext *)C, &mpr_ptr, func, &list);
	RNA_parameter_list_free(&list);
}

static int rna_manipulator_intersect_cb(
        struct bContext *C, struct wmManipulator *mpr, const struct wmEvent *event)
{
	extern FunctionRNA rna_Manipulator_intersect_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mpr_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mpr->type->ext.srna, mpr, &mpr_ptr);
	/* RNA_struct_find_function(&mpr_ptr, "intersect"); */
	func = &rna_Manipulator_intersect_func;
	RNA_parameter_list_create(&list, &mpr_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	RNA_parameter_set_lookup(&list, "event", &event);
	mgroup->type->ext.call((bContext *)C, &mpr_ptr, func, &list);

	void *ret;
	RNA_parameter_get_lookup(&list, "intersect_id", &ret);
	int intersect_id = *(int *)ret;

	RNA_parameter_list_free(&list);
	return intersect_id;
}

static void rna_manipulator_modal_cb(
        struct bContext *C, struct wmManipulator *mpr, const struct wmEvent *event, int tweak)
{
	extern FunctionRNA rna_Manipulator_modal_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mpr_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mpr->type->ext.srna, mpr, &mpr_ptr);
	/* RNA_struct_find_function(&mpr_ptr, "modal"); */
	func = &rna_Manipulator_modal_func;
	RNA_parameter_list_create(&list, &mpr_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	RNA_parameter_set_lookup(&list, "event", &event);
	RNA_parameter_set_lookup(&list, "tweak", &tweak);
	mgroup->type->ext.call((bContext *)C, &mpr_ptr, func, &list);
	RNA_parameter_list_free(&list);
}

static void rna_manipulator_invoke_cb(
        struct bContext *C, struct wmManipulator *mpr, const struct wmEvent *event)
{
	extern FunctionRNA rna_Manipulator_invoke_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mpr_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mpr->type->ext.srna, mpr, &mpr_ptr);
	/* RNA_struct_find_function(&mpr_ptr, "invoke"); */
	func = &rna_Manipulator_invoke_func;
	RNA_parameter_list_create(&list, &mpr_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	RNA_parameter_set_lookup(&list, "event", &event);
	mgroup->type->ext.call((bContext *)C, &mpr_ptr, func, &list);
	RNA_parameter_list_free(&list);
}

static void rna_manipulator_exit_cb(
        struct bContext *C, struct wmManipulator *mpr, bool cancel)
{
	extern FunctionRNA rna_Manipulator_exit_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mpr_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mpr->type->ext.srna, mpr, &mpr_ptr);
	/* RNA_struct_find_function(&mpr_ptr, "exit"); */
	func = &rna_Manipulator_exit_func;
	RNA_parameter_list_create(&list, &mpr_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	{
		int cancel_i = cancel;
		RNA_parameter_set_lookup(&list, "cancel", &cancel_i);
	}
	mgroup->type->ext.call((bContext *)C, &mpr_ptr, func, &list);
	RNA_parameter_list_free(&list);
}

static void rna_manipulator_select_cb(
        struct bContext *C, struct wmManipulator *mpr, int action)
{
	extern FunctionRNA rna_Manipulator_select_func;
	wmManipulatorGroup *mgroup = WM_manipulator_get_parent_group(mpr);
	PointerRNA mpr_ptr;
	ParameterList list;
	FunctionRNA *func;
	RNA_pointer_create(NULL, mpr->type->ext.srna, mpr, &mpr_ptr);
	/* RNA_struct_find_function(&mpr_ptr, "select"); */
	func = &rna_Manipulator_select_func;
	RNA_parameter_list_create(&list, &mpr_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	RNA_parameter_set_lookup(&list, "manipulator", &mpr);
	RNA_parameter_set_lookup(&list, "action", &action);
	mgroup->type->ext.call((bContext *)C, &mpr_ptr, func, &list);
	RNA_parameter_list_free(&list);
}

/* just to work around 'const char *' warning and to ensure this is a python op */
static void rna_Manipulator_bl_idname_set(PointerRNA *ptr, const char *value)
{
	wmManipulator *data = ptr->data;
	char *str = (char *)data->type->idname;
	if (!str[0]) {
		BLI_strncpy(str, value, MAX_NAME);    /* utf8 already ensured */
	}
	else {
		assert(!"setting the bl_idname on a non-builtin operator");
	}
}

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

static void rna_Manipulator_unregister(struct Main *bmain, StructRNA *type);
void manipulator_wrapper(wmManipulatorType *wgt, void *userdata);

static char _manipulator_idname[OP_MAX_TYPENAME];

static StructRNA *rna_Manipulator_register(
        Main *bmain, ReportList *reports, void *data, const char *identifier,
        StructValidateFunc validate, StructCallbackFunc call, StructFreeFunc free)
{
	wmManipulatorType dummywt = {NULL};
	wmManipulator dummymnp = {NULL};
	PointerRNA mnp_ptr;

	/* Two sets of functions. */
	int have_function[7];

	/* setup dummy manipulator & manipulator type to store static properties in */
	dummymnp.type = &dummywt;
	dummywt.idname = _manipulator_idname;
	RNA_pointer_create(NULL, &RNA_Manipulator, &dummymnp, &mnp_ptr);

	/* clear in case they are left unset */
	_manipulator_idname[0] = '\0';

	/* validate the python class */
	if (validate(&mnp_ptr, data, have_function) != 0)
		return NULL;

	if (strlen(identifier) >= sizeof(_manipulator_idname)) {
		BKE_reportf(reports, RPT_ERROR, "Registering manipulator class: '%s' is too long, maximum length is %d",
		            identifier, (int)sizeof(_manipulator_idname));
		return NULL;
	}

	/* check if we have registered this manipulator type before, and remove it */
	{
		const wmManipulatorType *wt = WM_manipulatortype_find(dummywt.idname, true);
		if (wt && wt->ext.srna) {
			rna_Manipulator_unregister(bmain, wt->ext.srna);
		}
	}

	/* create a new manipulator type */
	dummywt.ext.srna = RNA_def_struct_ptr(&BLENDER_RNA, dummywt.idname, &RNA_Manipulator);
	/* manipulator properties are registered separately */
	RNA_def_struct_flag(dummywt.ext.srna, STRUCT_NO_IDPROPERTIES);
	dummywt.ext.data = data;
	dummywt.ext.call = call;
	dummywt.ext.free = free;

	{
		int i = 0;
		dummywt.draw = (have_function[i++]) ? rna_manipulator_draw_cb : NULL;
		dummywt.draw_select = (have_function[i++]) ? rna_manipulator_draw_select_cb : NULL;
		dummywt.intersect = (have_function[i++]) ? rna_manipulator_intersect_cb : NULL;
		dummywt.modal = (have_function[i++]) ? rna_manipulator_modal_cb : NULL;
//		dummywt.prop_data_update = (have_function[i++]) ? rna_manipulator_prop_data_update : NULL;
//		dummywt.position_get = (have_function[i++]) ? rna_manipulator_position_get : NULL;
		dummywt.invoke = (have_function[i++]) ? rna_manipulator_invoke_cb : NULL;
		dummywt.exit = (have_function[i++]) ? rna_manipulator_exit_cb : NULL;
		dummywt.select = (have_function[i++]) ? rna_manipulator_select_cb : NULL;

		BLI_assert(i == ARRAY_SIZE(have_function));
	}

	WM_manipulatortype_append_ptr(manipulator_wrapper, (void *)&dummywt);

	RNA_def_struct_duplicate_pointers(dummywt.ext.srna);

	/* update while blender is running */
	WM_main_add_notifier(NC_SCREEN | NA_EDITED, NULL);

	return dummywt.ext.srna;
}

static void rna_Manipulator_unregister(struct Main *UNUSED(bmain), StructRNA *type)
{
	wmManipulatorType *wt = RNA_struct_blender_type_get(type);

	/* TODO, remove widgets from interface! */

	if (!wt)
		return;

	WM_main_add_notifier(NC_SCREEN | NA_EDITED, NULL);

	RNA_struct_free_extension(type, &wt->ext);

	WM_manipulatortype_remove_ptr(wt);

	RNA_struct_free(&BLENDER_RNA, type);
}

static void **rna_Manipulator_instance(PointerRNA *ptr)
{
	wmManipulator *mnp = ptr->data;
	return &mnp->py_instance;
}

static StructRNA *rna_Manipulator_refine(PointerRNA *mnp_ptr)
{
	wmManipulator *mnp = mnp_ptr->data;
	return (mnp->type && mnp->type->ext.srna) ? mnp->type->ext.srna : &RNA_Manipulator;
}

/** \} */

/** \name Manipulator Group API
 * \{ */

static wmManipulator *rna_ManipulatorGroup_manipulator_new(
        wmManipulatorGroup *mgroup, ReportList *reports, const char *idname, const char *name)
{
	const wmManipulatorType *wt = WM_manipulatortype_find(idname, true);
	if (wt == NULL) {
		BKE_reportf(reports, RPT_ERROR, "ManipulatorType '%s' not known", idname);
		return NULL;
	}
	wmManipulator *mpr = WM_manipulator_new_ptr(wt, mgroup, name);
	return mpr;
}

static void rna_ManipulatorGroup_manipulator_remove(
        wmManipulatorGroup *mgroup, bContext *C, wmManipulator *manipulator)
{
	WM_manipulator_free(&mgroup->manipulators, mgroup->parent_mmap, manipulator, C);
}

static void rna_ManipulatorGroup_manipulator_clear(
        wmManipulatorGroup *mgroup, bContext *C)
{
	while (mgroup->manipulators.first) {
		WM_manipulator_free(&mgroup->manipulators, mgroup->parent_mmap, mgroup->manipulators.first, C);
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

/* just to work around 'const char *' warning and to ensure this is a python op */
static void rna_ManipulatorGroup_bl_idname_set(PointerRNA *ptr, const char *value)
{
	wmManipulatorGroup *data = ptr->data;
	char *str = (char *)data->type->idname;
	if (!str[0])
		BLI_strncpy(str, value, MAX_NAME);    /* utf8 already ensured */
	else
		assert(!"setting the bl_idname on a non-builtin operator");
}

static void rna_ManipulatorGroup_bl_label_set(PointerRNA *ptr, const char *value)
{
	wmManipulatorGroup *data = ptr->data;
	char *str = (char *)data->type->name;
	if (!str[0])
		BLI_strncpy(str, value, MAX_NAME);    /* utf8 already ensured */
	else
		assert(!"setting the bl_label on a non-builtin operator");
}

static int rna_ManipulatorGroup_has_reports_get(PointerRNA *ptr)
{
	wmManipulatorGroup *wgroup = ptr->data;
	return (wgroup->reports && wgroup->reports->list.first);
}

#ifdef WITH_PYTHON

static bool manipulatorgroup_poll(const bContext *C, wmManipulatorGroupType *wgt)
{

	extern FunctionRNA rna_ManipulatorGroup_poll_func;

	PointerRNA ptr;
	ParameterList list;
	FunctionRNA *func;
	void *ret;
	int visible;

	RNA_pointer_create(NULL, wgt->ext.srna, NULL, &ptr); /* dummy */
	func = &rna_ManipulatorGroup_poll_func; /* RNA_struct_find_function(&ptr, "poll"); */

	RNA_parameter_list_create(&list, &ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	wgt->ext.call((bContext *)C, &ptr, func, &list);

	RNA_parameter_get_lookup(&list, "visible", &ret);
	visible = *(int *)ret;

	RNA_parameter_list_free(&list);

	return visible;
}

static void manipulatorgroup_setup(const bContext *C, wmManipulatorGroup *wgroup)
{
	extern FunctionRNA rna_ManipulatorGroup_setup_func;

	PointerRNA wgroup_ptr;
	ParameterList list;
	FunctionRNA *func;

	RNA_pointer_create(NULL, wgroup->type->ext.srna, wgroup, &wgroup_ptr);
	func = &rna_ManipulatorGroup_setup_func; /* RNA_struct_find_function(&wgroupr, "setup"); */

	RNA_parameter_list_create(&list, &wgroup_ptr, func);
	RNA_parameter_set_lookup(&list, "context", &C);
	wgroup->type->ext.call((bContext *)C, &wgroup_ptr, func, &list);

	RNA_parameter_list_free(&list);
}

static wmKeyMap *manipulatorgroup_setup_keymap(const wmManipulatorGroupType *wgt, wmKeyConfig *config)
{
	extern FunctionRNA rna_ManipulatorGroup_setup_keymap_func;
	const char *wgroupname = wgt->name;
	void *ret;

	PointerRNA ptr;
	ParameterList list;
	FunctionRNA *func;

	RNA_pointer_create(NULL, wgt->ext.srna, NULL, &ptr); /* dummy */
	func = &rna_ManipulatorGroup_setup_keymap_func; /* RNA_struct_find_function(&wgroupr, "setup_keymap"); */

	RNA_parameter_list_create(&list, &ptr, func);
	RNA_parameter_set_lookup(&list, "keyconfig", &config);
	RNA_parameter_set_lookup(&list, "manipulator_group", &wgroupname);
	wgt->ext.call(NULL, &ptr, func, &list);

	RNA_parameter_get_lookup(&list, "keymap", &ret);
	wmKeyMap *keymap = *(wmKeyMap **)ret;

	RNA_parameter_list_free(&list);

	return keymap;
}

void manipulatorgroup_wrapper(wmManipulatorGroupType *wgt, void *userdata);

static char _manipulatorgroup_name[MAX_NAME];
static char _manipulatorgroup_idname[MAX_NAME];

static StructRNA *rna_ManipulatorGroup_register(
        Main *bmain, ReportList *reports, void *data, const char *identifier,
        StructValidateFunc validate, StructCallbackFunc call, StructFreeFunc free)
{
	wmManipulatorGroupType dummywgt = {NULL};
	wmManipulatorGroup dummywg = {NULL};
	PointerRNA wgptr;

	/* Two sets of functions. */
	int have_function[3];

	/* setup dummy manipulatorgroup & manipulatorgroup type to store static properties in */
	dummywg.type = &dummywgt;
	dummywgt.name = _manipulatorgroup_name;
	dummywgt.idname = _manipulatorgroup_idname;

	RNA_pointer_create(NULL, &RNA_ManipulatorGroup, &dummywg, &wgptr);

	/* validate the python class */
	if (validate(&wgptr, data, have_function) != 0)
		return NULL;

	if (strlen(identifier) >= sizeof(_manipulatorgroup_idname)) {
		BKE_reportf(reports, RPT_ERROR, "Registering manipulatorgroup class: '%s' is too long, maximum length is %d",
		            identifier, (int)sizeof(_manipulatorgroup_idname));
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

	{
		int idlen = strlen(_manipulatorgroup_idname) + 1;
		int namelen = strlen(_manipulatorgroup_name) + 1;

		char *ch;
		ch = MEM_callocN(sizeof(char) * (idlen + namelen), "_manipulatorgroup_idname");
		dummywgt.idname = ch;
		memcpy(ch, _manipulatorgroup_idname, idlen);
		ch += idlen;
		dummywgt.name = ch;
		memcpy(ch, _manipulatorgroup_name, namelen);
	}

	/* check if we have registered this manipulatorgroup type before, and remove it */
	{
		wmManipulatorGroupType *wgt = WM_manipulatorgrouptype_find(wmaptype, dummywgt.idname);
		if (wgt && wgt->ext.srna) {
			WM_main_add_notifier(NC_SCREEN | NA_EDITED, NULL);
			WM_manipulatorgrouptype_remove_ptr(NULL, bmain, wgt);
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
	dummywgt.setup_keymap = (have_function[1]) ? manipulatorgroup_setup_keymap : NULL;
	dummywgt.setup = (have_function[2]) ? manipulatorgroup_setup : NULL;
	/* XXX, expose */
	dummywgt.flag = WM_MANIPULATORGROUPTYPE_IS_3D;

	WM_manipulatorgrouptype_append_ptr(wmaptype, manipulatorgroup_wrapper, (void *)&dummywgt);

	/* TODO: WM_manipulatorgrouptype_init_runtime */

	RNA_def_struct_duplicate_pointers(dummywgt.ext.srna);

	/* update while blender is running */
	WM_main_add_notifier(NC_SCREEN | NA_EDITED, NULL);

	return dummywgt.ext.srna;
}

static void rna_ManipulatorGroup_unregister(struct Main *bmain, StructRNA *type)
{
	wmManipulatorGroupType *wgt = RNA_struct_blender_type_get(type);

	if (!wgt)
		return;

	WM_main_add_notifier(NC_SCREEN | NA_EDITED, NULL);

	RNA_struct_free_extension(type, &wgt->ext);

	WM_manipulatorgrouptype_remove_ptr(NULL, bmain, wgt);

	RNA_struct_free(&BLENDER_RNA, type);
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

/** \} */


#else /* RNA_RUNTIME */


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
	RNA_def_function_flag(func, FUNC_USE_REPORTS);
	RNA_def_string(func, "type", "Type", 0, "", "Manipulator identifier"); /* optional */
	RNA_def_string(func, "name", "Name", 0, "", "Manipulator name"); /* optional */
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


static void rna_def_manipulator(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;
	PropertyRNA *prop;

	FunctionRNA *func;
	PropertyRNA *parm;

	RNA_def_property_srna(cprop, "Manipulator");
	srna = RNA_def_struct(brna, "Manipulator", NULL);
	RNA_def_struct_sdna(srna, "wmManipulator");
	RNA_def_struct_ui_text(srna, "Manipulator", "Collection of manipulators");
	RNA_def_struct_refine_func(srna, "rna_Manipulator_refine");

#ifdef WITH_PYTHON
	RNA_def_struct_register_funcs(
	        srna,
	        "rna_Manipulator_register",
	        "rna_Manipulator_unregister",
	        "rna_Manipulator_instance");
#endif
	RNA_def_struct_translation_context(srna, BLT_I18NCONTEXT_OPERATOR_DEFAULT);

	/* -------------------------------------------------------------------- */
	/* Registerable Variables */

	RNA_define_verify_sdna(0); /* not in sdna */

	prop = RNA_def_property(srna, "bl_idname", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "type->idname");
	RNA_def_property_string_maxlength(prop, MAX_NAME);
	RNA_def_property_string_funcs(prop, NULL, NULL, "rna_Manipulator_bl_idname_set");
	/* RNA_def_property_clear_flag(prop, PROP_EDITABLE); */
	RNA_def_property_flag(prop, PROP_REGISTER);
	RNA_def_struct_name_property(srna, prop);

	RNA_define_verify_sdna(1); /* not in sdna */


	/* wmManipulator.draw */
	func = RNA_def_function(srna, "draw", NULL);
	RNA_def_function_ui_description(func, "");
	RNA_def_function_flag(func, FUNC_REGISTER);
	parm = RNA_def_pointer(func, "context", "Context", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_pointer(func, "manipulator", "Manipulator", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

	/* wmManipulator.draw_select */
	func = RNA_def_function(srna, "draw_select", NULL);
	RNA_def_function_ui_description(func, "");
	RNA_def_function_flag(func, FUNC_REGISTER_OPTIONAL);
	parm = RNA_def_pointer(func, "context", "Context", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_pointer(func, "manipulator", "Manipulator", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_int(func, "select_id", 0, 0, INT_MAX, "", "", 0, INT_MAX);

	/* wmManipulator.intersect */
	func = RNA_def_function(srna, "intersect", NULL);
	RNA_def_function_ui_description(func, "");
	RNA_def_function_flag(func, FUNC_REGISTER_OPTIONAL);
	parm = RNA_def_pointer(func, "context", "Context", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_pointer(func, "manipulator", "Manipulator", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_pointer(func, "event", "Event", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_int(func, "intersect_id", 0, 0, INT_MAX, "", "", 0, INT_MAX);
	RNA_def_function_return(func, parm);

	/* wmManipulator.handler */
	static EnumPropertyItem tweak_actions[] = {
		{WM_MANIPULATOR_TWEAK_PRECISE, "PRECISE", 0, "Precise", ""},
		{0, NULL, 0, NULL, NULL}
	};
	func = RNA_def_function(srna, "modal", NULL);
	RNA_def_function_ui_description(func, "");
	RNA_def_function_flag(func, FUNC_REGISTER_OPTIONAL);
	parm = RNA_def_pointer(func, "context", "Context", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_pointer(func, "manipulator", "Manipulator", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_pointer(func, "event", "Event", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	/* TODO, shuold be a enum-flag */
	parm = RNA_def_enum(func, "tweak", tweak_actions, 0, "Tweak", "");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	RNA_def_property_flag(parm, PROP_ENUM_FLAG);

	/* wmManipulator.prop_data_update */
	/* TODO */

	/* wmManipulator.invoke */
	func = RNA_def_function(srna, "invoke", NULL);
	RNA_def_function_ui_description(func, "");
	RNA_def_function_flag(func, FUNC_REGISTER_OPTIONAL);
	parm = RNA_def_pointer(func, "context", "Context", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_pointer(func, "manipulator", "Manipulator", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_pointer(func, "event", "Event", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

	/* wmManipulator.exit */
	func = RNA_def_function(srna, "exit", NULL);
	RNA_def_function_ui_description(func, "");
	RNA_def_function_flag(func, FUNC_REGISTER_OPTIONAL);
	parm = RNA_def_pointer(func, "context", "Context", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_pointer(func, "manipulator", "Manipulator", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_boolean(func, "cancel", 0, "Cancel, otherwise confirm", "");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

	/* wmManipulator.cursor_get */
	/* TODO */

	/* wmManipulator.select */
	/* TODO, de-duplicate! */
	static EnumPropertyItem select_actions[] = {
		{SEL_TOGGLE, "TOGGLE", 0, "Toggle", "Toggle selection for all elements"},
		{SEL_SELECT, "SELECT", 0, "Select", "Select all elements"},
		{SEL_DESELECT, "DESELECT", 0, "Deselect", "Deselect all elements"},
		{SEL_INVERT, "INVERT", 0, "Invert", "Invert selection of all elements"},
		{0, NULL, 0, NULL, NULL}
	};
	func = RNA_def_function(srna, "select", NULL);
	RNA_def_function_ui_description(func, "");
	RNA_def_function_flag(func, FUNC_REGISTER_OPTIONAL);
	parm = RNA_def_pointer(func, "context", "Context", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_pointer(func, "manipulator", "Manipulator", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_enum(func, "action", select_actions, 0, "Action", "Selection action to execute");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);


	/* -------------------------------------------------------------------- */
	/* Instance Variables */

	prop = RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_array(prop, 4);
	RNA_def_property_float_funcs(prop, "rna_Manipulator_color_get", "rna_Manipulator_color_set", NULL);

	prop = RNA_def_property(srna, "color_highlight", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_array(prop, 4);
	RNA_def_property_float_funcs(prop, "rna_Manipulator_color_hi_get", "rna_Manipulator_color_hi_set", NULL);

	RNA_def_property_ui_text(prop, "Color", "");

	RNA_api_manipulator(srna);
}

static void rna_def_manipulatorgroup(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	FunctionRNA *func;
	PropertyRNA *parm;

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

	/* -------------------------------------------------------------------- */
	/* Registration */

	prop = RNA_def_property(srna, "bl_idname", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "type->idname");
	RNA_def_property_string_maxlength(prop, MAX_NAME);
	RNA_def_property_string_funcs(prop, NULL, NULL, "rna_ManipulatorGroup_bl_idname_set");
	RNA_def_property_flag(prop, PROP_REGISTER);
	RNA_def_struct_name_property(srna, prop);

	prop = RNA_def_property(srna, "bl_label", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "type->name");
	RNA_def_property_string_maxlength(prop, MAX_NAME); /* else it uses the pointer size! */
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

	/* Registration */

	/* poll */
	func = RNA_def_function(srna, "poll", NULL);
	RNA_def_function_ui_description(func, "Test if the manipulator group can be called or not");
	RNA_def_function_flag(func, FUNC_NO_SELF | FUNC_REGISTER_OPTIONAL);
	RNA_def_function_return(func, RNA_def_boolean(func, "visible", 1, "", ""));
	parm = RNA_def_pointer(func, "context", "Context", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

	/* keymap_init */
	func = RNA_def_function(srna, "setup_keymap", NULL);
	RNA_def_function_ui_description(
	        func,
	        "Initialize keymaps for this manipulator group, use fallback keymap when not present");
	RNA_def_function_flag(func, FUNC_NO_SELF | FUNC_REGISTER_OPTIONAL);
	parm = RNA_def_pointer(func, "keyconf", "KeyConfig", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
	parm = RNA_def_property(func, "manipulator_group", PROP_STRING, PROP_NONE);
	RNA_def_property_ui_text(parm, "Manipulator Group", "Manipulator Group ID");
	// RNA_def_property_string_default(parm, "");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
	/* return */
	parm = RNA_def_pointer(func, "keymap", "KeyMap", "", "");
	RNA_def_property_flag(parm, PROP_NEVER_NULL);
	RNA_def_function_return(func, parm);

	/* draw */
	func = RNA_def_function(srna, "setup", NULL);
	RNA_def_function_ui_description(func, "Create manipulators function for the manipulator group");
	RNA_def_function_flag(func, FUNC_REGISTER);
	parm = RNA_def_pointer(func, "context", "Context", "", "");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

	/* -------------------------------------------------------------------- */
	/* Instance Variables */

	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_string_funcs(prop, "rna_ManipulatorGroup_name_get", "rna_ManipulatorGroup_name_length", NULL);
	RNA_def_property_ui_text(prop, "Name", "");

	prop = RNA_def_property(srna, "has_reports", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE); /* this is 'virtual' property */
	RNA_def_property_boolean_funcs(prop, "rna_ManipulatorGroup_has_reports_get", NULL);
	RNA_def_property_ui_text(prop, "Has Reports",
	                         "ManipulatorGroup has a set of reports (warnings and errors) from last execution");

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
