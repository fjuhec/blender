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
 * The Original Code is Copyright (C) 2007 Blender Foundation.
 * All rights reserved.
 *
 * 
 * Contributor(s): Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/intern/wm_files_link.c
 *  \ingroup wm
 *
 * Functions for dealing with append/link operators and helpers.
 */


#include <float.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stddef.h>
#include <assert.h>
#include <errno.h>

#include "MEM_guardedalloc.h"

#include "DNA_ID.h"
#include "DNA_screen_types.h"
#include "DNA_scene_types.h"
#include "DNA_space_types.h"
#include "DNA_windowmanager_types.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "BLI_blenlib.h"
#include "BLI_bitmap.h"
#include "BLI_linklist.h"
#include "BLI_math.h"
#include "BLI_memarena.h"
#include "BLI_utildefines.h"
#include "BLI_ghash.h"

#include "PIL_time.h"

#include "BLO_readfile.h"

#include "BKE_asset.h"
#include "BKE_context.h"
#include "BKE_depsgraph.h"
#include "BKE_library.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_report.h"
#include "BKE_scene.h"
#include "BKE_screen.h" /* BKE_ST_MAXNAME */

#include "BKE_idcode.h"

#include "IMB_colormanagement.h"

#include "ED_screen.h"

#include "GPU_material.h"

#include "WM_api.h"
#include "WM_types.h"

#include "wm_files.h"

/* **************** link/append *************** */

static int wm_link_append_poll(bContext *C)
{
	if (WM_operator_winactive(C)) {
		/* linking changes active object which is pretty useful in general,
		 * but which totally confuses edit mode (i.e. it becoming not so obvious
		 * to leave from edit mode and invalid tools in toolbar might be displayed)
		 * so disable link/append when in edit mode (sergey) */
		if (CTX_data_edit_object(C))
			return 0;

		return 1;
	}

	return 0;
}

static int wm_link_append_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	if (RNA_struct_property_is_set(op->ptr, "filepath")) {
		return WM_operator_call_notest(C, op);
	}
	else {
		/* XXX TODO solve where to get last linked library from */
		if (G.lib[0] != '\0') {
			RNA_string_set(op->ptr, "filepath", G.lib);
		}
		else if (G.relbase_valid) {
			char path[FILE_MAX];
			BLI_strncpy(path, G.main->name, sizeof(G.main->name));
			BLI_parent_dir(path);
			RNA_string_set(op->ptr, "filepath", path);
		}
		WM_event_add_fileselect(C, op);
		return OPERATOR_RUNNING_MODAL;
	}
}

static short wm_link_append_flag(wmOperator *op)
{
	PropertyRNA *prop;
	short flag = 0;

	if (RNA_boolean_get(op->ptr, "autoselect"))
		flag |= FILE_AUTOSELECT;
	if (RNA_boolean_get(op->ptr, "active_layer"))
		flag |= FILE_ACTIVELAY;
	if ((prop = RNA_struct_find_property(op->ptr, "relative_path")) && RNA_property_boolean_get(op->ptr, prop))
		flag |= FILE_RELPATH;
	if (RNA_boolean_get(op->ptr, "link"))
		flag |= FILE_LINK;
	if (RNA_boolean_get(op->ptr, "instance_groups"))
		flag |= FILE_GROUP_INSTANCE;

	return flag;
}

typedef struct WMLinkAppendDataItem {
	AssetUUID *uuid;
	char *name;
	BLI_bitmap *libraries;  /* All libs (from WMLinkAppendData.libraries) to try to load this ID from. */
	short idcode;

	ID *new_id;
	void *customdata;
} WMLinkAppendDataItem;

typedef struct WMLinkAppendData {
	const char *root;
	LinkNodePair libraries;
	LinkNodePair items;
	int num_libraries;
	int num_items;
	short flag;

	/* Internal 'private' data */
	MemArena *memarena;
} WMLinkAppendData;

static WMLinkAppendData *wm_link_append_data_new(const int flag)
{
	MemArena *ma = BLI_memarena_new(BLI_MEMARENA_STD_BUFSIZE, __func__);
	WMLinkAppendData *lapp_data = BLI_memarena_calloc(ma, sizeof(*lapp_data));

	lapp_data->flag = flag;
	lapp_data->memarena = ma;

	return lapp_data;
}

static void wm_link_append_data_free(WMLinkAppendData *lapp_data)
{
	BLI_memarena_free(lapp_data->memarena);
}

/* WARNING! *Never* call wm_link_append_data_library_add() after having added some items! */

static void wm_link_append_data_library_add(WMLinkAppendData *lapp_data, const char *libname)
{
	size_t len = strlen(libname) + 1;
	char *libpath = BLI_memarena_alloc(lapp_data->memarena, len);

	BLI_strncpy(libpath, libname, len);
	BLI_linklist_append_arena(&lapp_data->libraries, libpath, lapp_data->memarena);
	lapp_data->num_libraries++;
}

static WMLinkAppendDataItem *wm_link_append_data_item_add(
        WMLinkAppendData *lapp_data, const char *idname, const short idcode, const AssetUUID *uuid, void *customdata)
{
	WMLinkAppendDataItem *item = BLI_memarena_alloc(lapp_data->memarena, sizeof(*item));
	size_t len = strlen(idname) + 1;

	if (uuid) {
		item->uuid = BLI_memarena_alloc(lapp_data->memarena, sizeof(*item->uuid));
		*item->uuid = *uuid;
	}
	else {
		item->uuid = NULL;
	}
	item->name = BLI_memarena_alloc(lapp_data->memarena, len);
	BLI_strncpy(item->name, idname, len);
	item->idcode = idcode;
	item->libraries = BLI_BITMAP_NEW_MEMARENA(lapp_data->memarena, lapp_data->num_libraries);

	item->new_id = NULL;
	item->customdata = customdata;

	BLI_linklist_append_arena(&lapp_data->items, item, lapp_data->memarena);
	lapp_data->num_items++;

	return item;
}

static void wm_link_do(
        WMLinkAppendData *lapp_data, ReportList *reports, Main *bmain, AssetEngineType *aet, Scene *scene, View3D *v3d,
        const bool use_placeholders, const bool force_indirect)
{
	Main *mainl;
	BlendHandle *bh;
	Library *lib;

	const int flag = lapp_data->flag;

	LinkNode *liblink, *itemlink;
	int lib_idx, item_idx;

	BLI_assert(lapp_data->num_items && lapp_data->num_libraries);

	for (lib_idx = 0, liblink = lapp_data->libraries.list; liblink; lib_idx++, liblink = liblink->next) {
		char *libname = liblink->link;

		bh = BLO_blendhandle_from_file(libname, reports);

		if (bh == NULL) {
			/* Unlikely since we just browsed it, but possible
			 * Error reports will have been made by BLO_blendhandle_from_file() */
			continue;
		}

		/* here appending/linking starts */
		mainl = BLO_library_link_begin(bmain, &bh, libname);
		lib = mainl->curlib;
		BLI_assert(lib);
		UNUSED_VARS_NDEBUG(lib);

		if (mainl->versionfile < 250) {
			BKE_reportf(reports, RPT_WARNING,
			            "Linking or appending from a very old .blend file format (%d.%d), no animation conversion will "
			            "be done! You may want to re-save your lib file with current Blender",
			            mainl->versionfile, mainl->subversionfile);
		}

		/* For each lib file, we try to link all items belonging to that lib,
		 * and tag those successful to not try to load them again with the other libs. */
		for (item_idx = 0, itemlink = lapp_data->items.list; itemlink; item_idx++, itemlink = itemlink->next) {
			WMLinkAppendDataItem *item = itemlink->link;
			ID *new_id;

			if (!BLI_BITMAP_TEST(item->libraries, lib_idx)) {
				continue;
			}

			new_id = BLO_library_link_named_part_asset(
			             mainl, &bh, aet, lapp_data->root, item->idcode, item->name, item->uuid, flag, scene, v3d,
			             use_placeholders, force_indirect);

			if (new_id) {
				/* If the link is sucessful, clear item's libs 'todo' flags.
				 * This avoids trying to link same item with other libraries to come. */
				BLI_BITMAP_SET_ALL(item->libraries, false, lapp_data->num_libraries);
				item->new_id = new_id;
			}
		}

		BLO_library_link_end(mainl, &bh, flag, scene, v3d);
		BLO_blendhandle_close(bh);
	}
}

static int wm_link_append_exec(bContext *C, wmOperator *op)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	PropertyRNA *prop;
	WMLinkAppendData *lapp_data;
	char path[FILE_MAX_LIBEXTRA], root[FILE_MAXDIR], libname[FILE_MAX], relname[FILE_MAX];
	char *group, *name;
	int totfiles = 0;
	short flag;

	char asset_engine[BKE_ST_MAXNAME];
	AssetEngineType *aet = NULL;
	AssetUUID uuid = {0};

	RNA_string_get(op->ptr, "filename", relname);
	RNA_string_get(op->ptr, "directory", root);

	BLI_join_dirfile(path, sizeof(path), root, relname);

	RNA_string_get(op->ptr, "asset_engine", asset_engine);
	if (asset_engine[0] != '\0') {
		aet = BKE_asset_engines_find(asset_engine);
	}

	/* test if we have a valid data */
	if (!BLO_library_path_explode(path, libname, &group, &name)) {
		BKE_reportf(op->reports, RPT_ERROR, "'%s': not a library", path);
		return OPERATOR_CANCELLED;
	}
	else if (!group) {
		BKE_reportf(op->reports, RPT_ERROR, "'%s': nothing indicated", path);
		return OPERATOR_CANCELLED;
	}
	else if (BLI_path_cmp(bmain->name, libname) == 0) {
		BKE_reportf(op->reports, RPT_ERROR, "'%s': cannot use current file as library", path);
		return OPERATOR_CANCELLED;
	}

	/* check if something is indicated for append/link */
	prop = RNA_struct_find_property(op->ptr, "files");
	if (prop) {
		totfiles = RNA_property_collection_length(op->ptr, prop);
		if (totfiles == 0) {
			if (!name) {
				BKE_reportf(op->reports, RPT_ERROR, "'%s': nothing indicated", path);
				return OPERATOR_CANCELLED;
			}
		}
	}
	else if (!name) {
		BKE_reportf(op->reports, RPT_ERROR, "'%s': nothing indicated", path);
		return OPERATOR_CANCELLED;
	}

	flag = wm_link_append_flag(op);

	/* sanity checks for flag */
	if (scene && scene->id.lib) {
		BKE_reportf(op->reports, RPT_WARNING,
		            "Scene '%s' is linked, instantiation of objects & groups is disabled", scene->id.name + 2);
		flag &= ~FILE_GROUP_INSTANCE;
		scene = NULL;
	}

	/* from here down, no error returns */

	if (scene && RNA_boolean_get(op->ptr, "autoselect")) {
		BKE_scene_base_deselect_all(scene);
	}

	/* tag everything, all untagged data can be made local
	 * its also generally useful to know what is new
	 *
	 * take extra care BKE_main_id_flag_all(bmain, LIB_TAG_PRE_EXISTING, false) is called after! */
	BKE_main_id_tag_all(bmain, LIB_TAG_PRE_EXISTING, true);

	/* We define our working data...
	 * Note that here, each item 'uses' one library, and only one. */
	lapp_data = wm_link_append_data_new(flag);
	lapp_data->root = root;
	if (totfiles != 0) {
		GHash *libraries = BLI_ghash_new(BLI_ghashutil_strhash_p, BLI_ghashutil_strcmp, __func__);
		int lib_idx = 0;

		RNA_BEGIN (op->ptr, itemptr, "files")
		{
			RNA_string_get(&itemptr, "name", relname);

			BLI_join_dirfile(path, sizeof(path), root, relname);

			if (BLO_library_path_explode(path, libname, &group, &name)) {
				if (!group || !name) {
					continue;
				}

				if (!BLI_ghash_haskey(libraries, libname)) {
					BLI_ghash_insert(libraries, BLI_strdup(libname), SET_INT_IN_POINTER(lib_idx));
					lib_idx++;
					wm_link_append_data_library_add(lapp_data, libname);
				}
			}
		}
		RNA_END;

		RNA_BEGIN (op->ptr, itemptr, "files")
		{
			RNA_string_get(&itemptr, "name", relname);

			BLI_join_dirfile(path, sizeof(path), root, relname);

			if (BLO_library_path_explode(path, libname, &group, &name)) {
				WMLinkAppendDataItem *item;
				if (!group || !name) {
					printf("skipping %s\n", path);
					continue;
				}

				lib_idx = GET_INT_FROM_POINTER(BLI_ghash_lookup(libraries, libname));

				if (aet) {
					RNA_int_get_array(&itemptr, "asset_uuid", uuid.uuid_asset);
					RNA_int_get_array(&itemptr, "variant_uuid", uuid.uuid_variant);
					RNA_int_get_array(&itemptr, "revision_uuid", uuid.uuid_revision);
				}

				item = wm_link_append_data_item_add(lapp_data, name, BKE_idcode_from_name(group), &uuid, NULL);
				BLI_BITMAP_ENABLE(item->libraries, lib_idx);
			}
		}
		RNA_END;

		BLI_ghash_free(libraries, MEM_freeN, NULL);
	}
	else {
		WMLinkAppendDataItem *item;

		wm_link_append_data_library_add(lapp_data, libname);
		item = wm_link_append_data_item_add(lapp_data, name, BKE_idcode_from_name(group), &uuid, NULL);
		BLI_BITMAP_ENABLE(item->libraries, 0);
	}

	/* XXX We'd need re-entrant locking on Main for this to work... */
	/* BKE_main_lock(bmain); */

	wm_link_do(lapp_data, op->reports, bmain, aet, scene, CTX_wm_view3d(C), false, false);

	/* BKE_main_unlock(bmain); */

	wm_link_append_data_free(lapp_data);

	/* mark all library linked objects to be updated */
	BKE_main_lib_objects_recalc_all(bmain);
	IMB_colormanagement_check_file_config(bmain);

	/* append, rather than linking */
	if ((flag & FILE_LINK) == 0) {
		bool set_fake = RNA_boolean_get(op->ptr, "set_fake");
		BKE_library_make_local(bmain, NULL, true, set_fake);
	}

	/* important we unset, otherwise these object wont
	 * link into other scenes from this blend file */
	BKE_main_id_tag_all(bmain, LIB_TAG_PRE_EXISTING, false);

	/* recreate dependency graph to include new objects */
	DAG_scene_relations_rebuild(bmain, scene);

	/* free gpu materials, some materials depend on existing objects, such as lamps so freeing correctly refreshes */
	GPU_materials_free();

	/* XXX TODO: align G.lib with other directory storage (like last opened image etc...) */
	BLI_strncpy(G.lib, root, FILE_MAX);

	WM_event_add_notifier(C, NC_WINDOW, NULL);

	return OPERATOR_FINISHED;
}

static void wm_link_append_properties_common(wmOperatorType *ot, bool is_link)
{
	PropertyRNA *prop;

	/* better not save _any_ settings for this operator */
	/* properties */
	prop = RNA_def_string(ot->srna, "asset_engine", NULL, sizeof(((AssetEngineType *)NULL)->idname),
	                      "Asset Engine", "Asset engine identifier used to append/link the data");
	RNA_def_property_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);

	prop = RNA_def_boolean(ot->srna, "link", is_link,
	                       "Link", "Link the objects or datablocks rather than appending");
	RNA_def_property_flag(prop, PROP_SKIP_SAVE | PROP_HIDDEN);
	prop = RNA_def_boolean(ot->srna, "autoselect", true,
	                       "Select", "Select new objects");
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);
	prop = RNA_def_boolean(ot->srna, "active_layer", true,
	                       "Active Layer", "Put new objects on the active layer");
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);
	prop = RNA_def_boolean(ot->srna, "instance_groups", is_link,
	                       "Instance Groups", "Create Dupli-Group instances for each group");
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

void WM_OT_link(wmOperatorType *ot)
{
	ot->name = "Link from Library";
	ot->idname = "WM_OT_link";
	ot->description = "Link from a Library .blend file";
	
	ot->invoke = wm_link_append_invoke;
	ot->exec = wm_link_append_exec;
	ot->poll = wm_link_append_poll;
	
	ot->flag |= OPTYPE_UNDO;

	WM_operator_properties_filesel(
	        ot, FILE_TYPE_FOLDER | FILE_TYPE_BLENDER | FILE_TYPE_BLENDERLIB, FILE_LOADLIB, FILE_OPENFILE,
	        WM_FILESEL_FILEPATH | WM_FILESEL_DIRECTORY | WM_FILESEL_FILENAME | WM_FILESEL_RELPATH | WM_FILESEL_FILES,
	        FILE_DEFAULTDISPLAY, FILE_SORT_ALPHA);
	
	wm_link_append_properties_common(ot, true);
}

void WM_OT_append(wmOperatorType *ot)
{
	ot->name = "Append from Library";
	ot->idname = "WM_OT_append";
	ot->description = "Append from a Library .blend file";

	ot->invoke = wm_link_append_invoke;
	ot->exec = wm_link_append_exec;
	ot->poll = wm_link_append_poll;

	ot->flag |= OPTYPE_UNDO;

	WM_operator_properties_filesel(
	        ot, FILE_TYPE_FOLDER | FILE_TYPE_BLENDER | FILE_TYPE_BLENDERLIB, FILE_LOADLIB, FILE_OPENFILE,
	        WM_FILESEL_FILEPATH | WM_FILESEL_DIRECTORY | WM_FILESEL_FILENAME | WM_FILESEL_FILES,
	        FILE_DEFAULTDISPLAY, FILE_SORT_ALPHA);

	wm_link_append_properties_common(ot, false);
	RNA_def_boolean(ot->srna, "set_fake", false, "Fake User", "Set Fake User for appended items (except Objects and Groups)");
}

/** \name Reload/relocate libraries.
 *
 * \{ */

static int wm_lib_relocate_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
	Library *lib;
	char lib_name[MAX_NAME];

	RNA_string_get(op->ptr, "library", lib_name);
	lib = (Library *)BKE_libblock_find_name_ex(CTX_data_main(C), ID_LI, lib_name);

	if (lib) {
		if (lib->parent) {
			BKE_reportf(op->reports, RPT_ERROR_INVALID_INPUT,
			            "Cannot relocate indirectly linked library '%s'", lib->filepath);
			return OPERATOR_CANCELLED;
		}
		RNA_string_set(op->ptr, "filepath", lib->filepath);

		WM_event_add_fileselect(C, op);

		return OPERATOR_RUNNING_MODAL;
	}

	return OPERATOR_CANCELLED;
}

static int wm_lib_relocate_exec_do(bContext *C, wmOperator *op, const bool reload)
{
	Library *lib;
	char lib_name[MAX_NAME];

	RNA_string_get(op->ptr, "library", lib_name);
	lib = (Library *)BKE_libblock_find_name_ex(CTX_data_main(C), ID_LI, lib_name);

	if (lib) {
		Main *bmain = CTX_data_main(C);
		Scene *scene = CTX_data_scene(C);
		PropertyRNA *prop;
		WMLinkAppendData *lapp_data;

		ListBase *lbarray[MAX_LIBARRAY];
		int lba_idx;

		LinkNode *itemlink;
		int item_idx;

		int num_ids;
		char path[FILE_MAX], root[FILE_MAXDIR], libname[FILE_MAX], relname[FILE_MAX];
		short flag = 0;

		if (RNA_boolean_get(op->ptr, "relative_path")) {
			flag |= FILE_RELPATH;
		}

		if (lib->parent && !reload) {
			BKE_reportf(op->reports, RPT_ERROR_INVALID_INPUT,
			            "Cannot relocate indirectly linked library '%s'", lib->filepath);
			return OPERATOR_CANCELLED;
		}

		RNA_string_get(op->ptr, "directory", root);
		RNA_string_get(op->ptr, "filename", libname);

		if (!BLO_has_bfile_extension(libname)) {
			BKE_report(op->reports, RPT_ERROR, "Not a library");
			return OPERATOR_CANCELLED;
		}

		BLI_join_dirfile(path, sizeof(path), root, libname);

		if (BLI_path_cmp(lib->filepath, path) == 0) {
			printf("We are supposed to reload '%s' lib (%d)...\n", lib->filepath, lib->id.us);

			lapp_data = wm_link_append_data_new(flag);
			wm_link_append_data_library_add(lapp_data, path);

			BKE_main_lock(bmain);

			lba_idx = set_listbasepointers(bmain, lbarray);
			while (lba_idx--) {
				ID *id = lbarray[lba_idx]->first;
				const short idcode = id ? GS(id->name) : 0;

				if (!id || !BKE_idcode_is_linkable(idcode)) {
					/* No need to reload non-linkable datatypes, those will get relinked with their 'users ID'. */
					continue;
				}

				for (; id; id = id->next) {
					if (id->lib == lib) {
						WMLinkAppendDataItem *item;

						/* We remove it from current Main, and add it to items to link... */
						/* Note that non-linkable IDs (like e.g. shapekeys) are also explicitely linked here... */
						BLI_remlink(lbarray[lba_idx], id);
						item = wm_link_append_data_item_add(lapp_data, id->name + 2, idcode, NULL, id);
						BLI_BITMAP_SET_ALL(item->libraries, true, lapp_data->num_libraries);

						printf("\tdatablock to seek for: %s\n", id->name);
					}
				}
			}

			BKE_main_id_tag_all(bmain, LIB_TAG_PRE_EXISTING, true);

			/* XXX For now, locking is not reentrant so it's not safe to call core linking code with locked Main. */
			BKE_main_unlock(bmain);

			/* We do not want any instanciation here! */
			wm_link_do(lapp_data, op->reports, bmain, NULL, NULL, NULL, true, true);

			BKE_main_lock(bmain);

			/* We add back old id to bmain.
			 * We need to do this in a first, separated loop, otherwise some of those may not be handled by
			 * ID remapping, which means they would still reference old data to be deleted... */
			for (item_idx = 0, itemlink = lapp_data->items.list; itemlink; item_idx++, itemlink = itemlink->next) {
				WMLinkAppendDataItem *item = itemlink->link;
				ID *old_id = item->customdata;

				BLI_assert(old_id);
				BLI_addtail(which_libbase(bmain, GS(old_id->name)), old_id);
			}

			for (item_idx = 0, itemlink = lapp_data->items.list; itemlink; item_idx++, itemlink = itemlink->next) {
				WMLinkAppendDataItem *item = itemlink->link;
				ID *old_id = item->customdata;
				ID *new_id = item->new_id;

				/* Since we asked for placeholders in case of missing IDs, we expect to always get a valid one. */
				BLI_assert(new_id);
				if (new_id) {
//					printf("before remap, old_id users: %d (%p), new_id users: %d (%p)\n", old_id->us, old_id, new_id->us, new_id);
					/* Note that here, we also want to replace indirect usages. */
					BKE_libblock_remap_locked(bmain, old_id, new_id, ID_REMAP_SKIP_NEVER_NULL_USAGE);

//					printf("after remap, old_id users: %d, new_id users: %d\n", old_id->us, new_id->us);

					/* In some cases, new_id might become direct link, remove parent of library in this case. */
					if (new_id->lib->parent && (new_id->tag & LIB_TAG_INDIRECT) == 0) {
						BLI_assert(0);  /* Should not happen in reload case... */
						new_id->lib->parent = NULL;
					}
				}

				if (old_id->us > 0 && new_id) {
					size_t len = strlen(old_id->name);

					/* XXX TODO This is utterly weak!!! */
					if (len > MAX_ID_NAME - 3 && old_id->name[len - 4] == '.') {
						old_id->name[len - 6] = '.';
						old_id->name[len - 5] = 'P';
					}
					else {
						len = MIN2(len, MAX_ID_NAME - 3);
						old_id->name[len] = '.';
						old_id->name[len + 1] = 'P';
						old_id->name[len + 2] = '\0';
					}

					id_sort_by_name(which_libbase(bmain, GS(old_id->name)), old_id);

					BKE_reportf(op->reports, RPT_WARNING,
					            "Lib Reload: Replacing all references to old datablock '%s' by reloaded one failed, "
					            "old one (%d remaining users) had to be kept and was renamed to '%s'",
					            new_id->name, old_id->us, old_id->name);
				}
			}

			BKE_main_unlock(bmain);

			for (item_idx = 0, itemlink = lapp_data->items.list; itemlink; item_idx++, itemlink = itemlink->next) {
				WMLinkAppendDataItem *item = itemlink->link;
				ID *old_id = item->customdata;

//				printf("%p\n", old_id);

				if (old_id->us == 0) {
					BKE_libblock_free(bmain, old_id);
					num_ids--;
				}
			}

			wm_link_append_data_free(lapp_data);
		}
		else {
			int totfiles = 0;

			printf("We are supposed to relocate '%s' lib to new '%s' one...\n", lib->filepath, libname);

			/* Check if something is indicated for relocate. */
			prop = RNA_struct_find_property(op->ptr, "files");
			if (prop) {
				totfiles = RNA_property_collection_length(op->ptr, prop);
				if (totfiles == 0) {
					if (!libname[0]) {
						BKE_report(op->reports, RPT_ERROR, "Nothing indicated");
						return OPERATOR_CANCELLED;
					}
				}
			}

			lapp_data = wm_link_append_data_new(flag);

			if (totfiles) {
				RNA_BEGIN (op->ptr, itemptr, "files")
				{
					RNA_string_get(&itemptr, "name", relname);

					BLI_join_dirfile(path, sizeof(path), root, relname);

					if (BLI_path_cmp(path, lib->filepath) == 0 || !BLO_has_bfile_extension(relname)) {
						continue;
					}

					printf("\t candidate new lib to reload datablocks from: %s\n", path);
					wm_link_append_data_library_add(lapp_data, path);
				}
				RNA_END;
			}
			else {
				printf("\t candidate new lib to reload datablocks from: %s\n", path);
				wm_link_append_data_library_add(lapp_data, path);
			}

			BKE_main_lock(bmain);

			lba_idx = set_listbasepointers(bmain, lbarray);
			while (lba_idx--) {
				ID *id = lbarray[lba_idx]->first;
				const int idcode = id ? GS(id->name) : 0;

				if (!id || !BKE_idcode_is_linkable(idcode)) {
					continue;
				}
				for (; id; id = id->next) {
					if (id->lib == lib) {
						WMLinkAppendDataItem *item = wm_link_append_data_item_add(
						                                 lapp_data, id->name + 2, idcode, NULL, id);
						BLI_BITMAP_SET_ALL(item->libraries, true, lapp_data->num_libraries);

						printf("\tdatablock to seek for: %s\n", id->name);
					}
				}
			}

			BKE_main_id_tag_all(bmain, LIB_TAG_PRE_EXISTING, true);

			/* XXX For now, locking is not reentrant so it's not safe to call core linking code with locked Main. */
			BKE_main_unlock(bmain);

			/* We do not want any instanciation here! */
			wm_link_do(lapp_data, op->reports, bmain, NULL, NULL, NULL, false, false);

			BKE_main_lock(bmain);

			for (item_idx = 0, itemlink = lapp_data->items.list; itemlink; item_idx++, itemlink = itemlink->next) {
				WMLinkAppendDataItem *item = itemlink->link;
				ID *old_id = item->customdata;
				ID *new_id = item->new_id;

				BLI_assert(old_id);
				if (new_id) {
//					printf("before remap, old_id users: %d, new_id users: %d\n", old_id->us, new_id->us);
					BKE_libblock_remap_locked(bmain, old_id, new_id,
					                          ID_REMAP_SKIP_INDIRECT_USAGE | ID_REMAP_SKIP_NEVER_NULL_USAGE);

					if (old_id->flag & LIB_FAKEUSER) {
						id_fake_user_clear(old_id);
						id_fake_user_set(new_id);
					}

//					printf("after remap, old_id users: %d, new_id users: %d\n", old_id->us, new_id->us);

					/* In some cases, new_id might become direct link, remove parent of library in this case. */
					if (new_id->lib->parent && (new_id->tag & LIB_TAG_INDIRECT) == 0) {
						new_id->lib->parent = NULL;
					}
				}
			}

			BKE_main_unlock(bmain);

			num_ids = lapp_data->num_items;
			for (item_idx = 0, itemlink = lapp_data->items.list; itemlink; item_idx++, itemlink = itemlink->next) {
				WMLinkAppendDataItem *item = itemlink->link;
				ID *old_id = item->customdata;

				if (old_id->us == 0) {
					BKE_libblock_free(bmain, old_id);
					num_ids--;
				}
			}

			if (num_ids == 0) {
				/* Nothing uses old lib anymore, we can get rid of it. */
				id_us_min(&lib->id);
				if (lib->id.us == 0) {
					BKE_libblock_free(bmain, (ID *)lib);
				}
			}

			wm_link_append_data_free(lapp_data);
		}

		/* Some datablocks can get reloaded/replaced 'silently' because they are not linkable (shape keys e.g.),
		 * so we need another loop here to clear old ones if possible. */
		lba_idx = set_listbasepointers(bmain, lbarray);
		while (lba_idx--) {
			ID *id = lbarray[lba_idx]->first;

			for (; id; id = id->next) {
				if (id->lib == lib && (id->flag & LIB_TAG_PRE_EXISTING) && id->us == 0) {
					BKE_libblock_free(bmain, id);
				}
			}
		}

		BKE_main_lib_objects_recalc_all(bmain);
		IMB_colormanagement_check_file_config(bmain);

		/* important we unset, otherwise these object wont
		 * link into other scenes from this blend file */
		BKE_main_id_tag_all(bmain, LIB_TAG_PRE_EXISTING, false);

		/* recreate dependency graph to include new objects */
		DAG_scene_relations_rebuild(bmain, scene);

		/* free gpu materials, some materials depend on existing objects, such as lamps so freeing correctly refreshes */
		GPU_materials_free();

		/* XXX TODO: align G.lib with other directory storage (like last opened image etc...) */
		BLI_strncpy(G.lib, root, FILE_MAX);

		WM_event_add_notifier(C, NC_WINDOW, NULL);

		return OPERATOR_FINISHED;
	}

	return OPERATOR_CANCELLED;
}

static int wm_lib_relocate_exec(bContext *C, wmOperator *op)
{
	return wm_lib_relocate_exec_do(C, op, false);
}

void WM_OT_lib_relocate(wmOperatorType *ot)
{
	PropertyRNA *prop;

	ot->name = "Relocate Library";
	ot->idname = "WM_OT_lib_relocate";
	ot->description = "Relocate the given library to one or several others";

	ot->invoke = wm_lib_relocate_invoke;
	ot->exec = wm_lib_relocate_exec;

	ot->flag |= OPTYPE_UNDO;

	prop = RNA_def_string(ot->srna, "library", NULL, MAX_NAME, "Library", "Library to relocate");
	RNA_def_property_flag(prop, PROP_HIDDEN);

	WM_operator_properties_filesel(
	            ot, FILE_TYPE_FOLDER | FILE_TYPE_BLENDER, FILE_BLENDER, FILE_OPENFILE,
	            WM_FILESEL_FILEPATH | WM_FILESEL_DIRECTORY | WM_FILESEL_FILENAME | WM_FILESEL_FILES | WM_FILESEL_RELPATH,
	            FILE_DEFAULTDISPLAY, FILE_SORT_ALPHA);
}

static int wm_lib_reload_exec(bContext *C, wmOperator *op)
{
	return wm_lib_relocate_exec_do(C, op, true);
}

void WM_OT_lib_reload(wmOperatorType *ot)
{
	PropertyRNA *prop;

	ot->name = "Reload Library";
	ot->idname = "WM_OT_lib_reload";
	ot->description = "Reload the given library";

	ot->exec = wm_lib_reload_exec;

	ot->flag |= OPTYPE_UNDO;

	prop = RNA_def_string(ot->srna, "library", NULL, MAX_NAME, "Library", "Library to relocate");
	RNA_def_property_flag(prop, PROP_HIDDEN);

	WM_operator_properties_filesel(
	            ot, FILE_TYPE_FOLDER | FILE_TYPE_BLENDER, FILE_BLENDER, FILE_OPENFILE,
	            WM_FILESEL_FILEPATH | WM_FILESEL_DIRECTORY | WM_FILESEL_FILENAME | WM_FILESEL_RELPATH,
	            FILE_DEFAULTDISPLAY, FILE_SORT_ALPHA);
}

/** \name Asset-related operators.
 *
 * \{ */

typedef struct AssetUpdateCheckEngine {
	struct AssetUpdateCheckEngine *next, *prev;
	AssetEngine *ae;

	/* Note: We cannot store IDs themselves in non-locking async task... so we'll have to check again for
	 *       UUID/IDs mapping on each update call... Not ideal, but don't think it will be that big of an override
	 *       in practice. */
	AssetUUIDList uuids;
	int ae_job_id;
	short status;
} AssetUpdateCheckEngine;

typedef struct AssetUpdateCheckJob {
	ListBase engines;
	short flag;

	float *progress;
	short *stop;
} AssetUpdateCheckJob;

/* AssetUpdateCheckEngine.status */
enum {
	AUCE_UPDATE_CHECK_DONE  = 1 << 0,  /* Update check is finished for this engine. */
	AUCE_ENSURE_ASSETS_DONE = 1 << 1,  /* Asset ensure is finished for this engine (if applicable). */
};

/* AssetUpdateCheckJob.flag */
enum {
	AUCJ_ENSURE_ASSETS = 1 << 0,  /* Try to perform the 'ensure' task too. */
};

/* Helper to fetch a set of assets to handle, regrouped by asset engine. */
static void asset_update_engines_uuids_fetch(
        ListBase *engines,
        Main *bmain, AssetUUIDList *uuids, const short uuid_tags,
        const bool do_reset_tags)
{
	for (Library *lib = bmain->library.first; lib; lib = lib->id.next) {
		if (lib->asset_repository) {
			printf("Checking lib file %s (engine %s, ver. %d)\n", lib->filepath,
			       lib->asset_repository->asset_engine, lib->asset_repository->asset_engine_version);

			AssetUpdateCheckEngine *auce = NULL;
			AssetEngineType *ae_type = BKE_asset_engines_find(lib->asset_repository->asset_engine);
			bool copy_engine = false;

			if (ae_type == NULL) {
				printf("ERROR! Unknown asset engine!\n");
			}

			for (AssetRef *aref = lib->asset_repository->assets.first; aref; aref = aref->next) {
				ID *id = ((LinkData *)aref->id_list.first)->data;
				BLI_assert(id->uuid);

				if (uuid_tags && !(id->uuid->tag & uuid_tags)) {
					continue;
				}

				if (uuids) {
					int i = uuids->nbr_uuids;
					bool skip = true;
					for (AssetUUID *uuid = uuids->uuids; i--; uuid++) {
						if (ASSETUUID_COMPARE(id->uuid, uuid)) {
							skip = false;
							break;
						}
					}
					if (skip) {
						continue;
					}
				}

				if (ae_type == NULL) {
					if (do_reset_tags) {
						id->uuid->tag = UUID_TAG_ENGINE_MISSING;
					}
					else {
						id->uuid->tag |= UUID_TAG_ENGINE_MISSING;
					}
					G.f |= G_ASSETS_FAIL;
					continue;
				}

				if (auce == NULL) {
					for (auce = engines->first; auce; auce = auce->next) {
						if (auce->ae->type == ae_type) {
							/* In case we have several engine versions for the same engine, we create several
							 * AssetUpdateCheckEngine structs (since an uuid list can only handle one ae version), using
							 * the same (shallow) copy of the actual asset engine. */
							copy_engine = (auce->uuids.asset_engine_version != lib->asset_repository->asset_engine_version);
							break;
						}
					}
					if (copy_engine || auce == NULL) {
						AssetUpdateCheckEngine *auce_prev = auce;
						auce = MEM_callocN(sizeof(*auce), __func__);
						auce->ae = copy_engine ? BKE_asset_engine_copy(auce_prev->ae) :
						                         BKE_asset_engine_create(ae_type, NULL);
						auce->ae_job_id = AE_JOB_ID_UNSET;
						auce->uuids.asset_engine_version = lib->asset_repository->asset_engine_version;
						BLI_addtail(engines, auce);
					}
				}

				printf("\tWe need to check for updated asset %s...\n", id->name);
				if (do_reset_tags) {
					id->uuid->tag = (id->tag & LIB_TAG_MISSING) ? UUID_TAG_ASSET_MISSING : 0;
				}

				/* XXX horrible, need to use some mempool, stack or something :) */
				auce->uuids.nbr_uuids++;
				if (auce->uuids.uuids) {
					auce->uuids.uuids = MEM_reallocN_id(auce->uuids.uuids, sizeof(*auce->uuids.uuids) * (size_t)auce->uuids.nbr_uuids, __func__);
				}
				else {
					auce->uuids.uuids = MEM_mallocN(sizeof(*auce->uuids.uuids) * (size_t)auce->uuids.nbr_uuids, __func__);
				}
				auce->uuids.uuids[auce->uuids.nbr_uuids - 1] = *id->uuid;
			}
		}
	}
}

static void asset_updatecheck_startjob(void *aucjv, short *stop, short *do_update, float *progress)
{
	AssetUpdateCheckJob *aucj = aucjv;

	aucj->progress = progress;
	aucj->stop = stop;
	/* Using AE engine, worker thread here is just sleeping! */
	while (!*stop) {
		*do_update = true;
		PIL_sleep_ms(100);
	}
}

static void asset_updatecheck_update(void *aucjv)
{
	AssetUpdateCheckJob *aucj = aucjv;
	Main *bmain = G.main;

	const bool do_ensure = ((aucj->flag & AUCJ_ENSURE_ASSETS) != 0);
	bool is_finished = true;
	int nbr_engines = 0;

	*aucj->progress = 0.0f;

	/* TODO need to take care of 'broken' engines that error - in this case we probably want to cancel the whole
	 * update process over effected libraries' data... */
	for (AssetUpdateCheckEngine *auce = aucj->engines.first; auce; auce = auce->next, nbr_engines++) {
		AssetEngine *ae = auce->ae;
		AssetEngineType *ae_type = ae->type;

		/* Step 1: we ask asset engine about status of all asset IDs from it. */
		if (!(auce->status & AUCE_UPDATE_CHECK_DONE)) {
			auce->ae_job_id = ae_type->update_check(ae, auce->ae_job_id, &auce->uuids);
			if (auce->ae_job_id == AE_JOB_ID_INVALID) {  /* Immediate execution. */
				*aucj->progress += 1.0f;
				auce->status |= AUCE_UPDATE_CHECK_DONE;
			}
			else {
				*aucj->progress += ae_type->progress(ae, auce->ae_job_id);
				if ((ae_type->status(ae, auce->ae_job_id) & (AE_STATUS_RUNNING | AE_STATUS_VALID)) !=
					(AE_STATUS_RUNNING | AE_STATUS_VALID))
				{
					auce->status |= AUCE_UPDATE_CHECK_DONE;
				}
			}

			if (auce->status & AUCE_UPDATE_CHECK_DONE) {
				auce->ae_job_id = AE_JOB_ID_UNSET;

				for (Library *lib = bmain->library.first; lib; lib = lib->id.next) {
					if (!lib->asset_repository ||
					    (BKE_asset_engines_find(lib->asset_repository->asset_engine) != ae_type))
					{
						continue;
					}

					/* UUIDs returned by update_check are assumed to be valid (one way or the other) in current
					 * asset engine version. */
					lib->asset_repository->asset_engine_version = ae_type->version;

					int i = auce->uuids.nbr_uuids;
					for (AssetUUID *uuid = auce->uuids.uuids; i--; uuid++) {
						for (AssetRef *aref = lib->asset_repository->assets.first; aref; aref = aref->next) {
							ID *id = ((LinkData *)aref->id_list.first)->data;
							BLI_assert(id->uuid);
							if (ASSETUUID_COMPARE(id->uuid, uuid)) {
								*id->uuid = *uuid;

								if (id->uuid->tag & UUID_TAG_ENGINE_MISSING) {
									G.f |= G_ASSETS_FAIL;
									printf("\t%s uses a currently unknown asset engine!\n", id->name);
								}
								else if (id->uuid->tag & UUID_TAG_ASSET_MISSING) {
									G.f |= G_ASSETS_FAIL;
									printf("\t%s is currently unknown by asset engine!\n", id->name);
								}
								else if (id->uuid->tag & UUID_TAG_ASSET_RELOAD) {
									G.f |= G_ASSETS_NEED_RELOAD;
									printf("\t%s needs to be reloaded/updated!\n", id->name);
								}
								break;
							}
						}
					}
				}

			}
		}

		/* Step 2: If required and supported, we 'ensure' assets tagged as to be reloaded. */
		if (do_ensure && !(auce->status & AUCE_ENSURE_ASSETS_DONE) && ae_type->ensure_uuids != NULL) {
			/* TODO ensure entries! */
			*aucj->progress += 1.0f;
			auce->status |= AUCE_ENSURE_ASSETS_DONE;
			if (auce->status & AUCE_ENSURE_ASSETS_DONE) {
				auce->ae_job_id = AE_JOB_ID_UNSET;
			}
		}

		if ((auce->status & (AUCE_UPDATE_CHECK_DONE | AUCE_ENSURE_ASSETS_DONE)) !=
		    (AUCE_UPDATE_CHECK_DONE | AUCE_ENSURE_ASSETS_DONE))
		{
			is_finished = false;
		}
	}

	*aucj->progress /= (float)(do_ensure ? nbr_engines * 2 : nbr_engines);
	*aucj->stop = is_finished;
}

static void asset_updatecheck_endjob(void *aucjv)
{
	AssetUpdateCheckJob *aucj = aucjv;

	/* In case there would be some dangling update. */
	asset_updatecheck_update(aucjv);

	for (AssetUpdateCheckEngine *auce = aucj->engines.first; auce; auce = auce->next) {
		AssetEngine *ae = auce->ae;
		if (!ELEM(auce->ae_job_id, AE_JOB_ID_INVALID, AE_JOB_ID_UNSET)) {
			ae->type->kill(ae, auce->ae_job_id);
		}
	}
}

static void asset_updatecheck_free(void *aucjv)
{
	AssetUpdateCheckJob *aucj = aucjv;

	for (AssetUpdateCheckEngine *auce = aucj->engines.first; auce; auce = auce->next) {
		BKE_asset_engine_free(auce->ae);
		MEM_freeN(auce->uuids.uuids);
	}
	BLI_freelistN(&aucj->engines);

	MEM_freeN(aucj);
}

static void asset_updatecheck_start(const bContext *C)
{
	wmJob *wm_job;
	AssetUpdateCheckJob *aucj;

	Main *bmain = CTX_data_main(C);

	/* prepare job data */
	aucj = MEM_callocN(sizeof(*aucj), __func__);

	G.f &= ~(G_ASSETS_FAIL | G_ASSETS_NEED_RELOAD | G_ASSETS_QUIET);

	/* Get all assets' uuids, grouped by asset engine/versions - and with cleared status tags. */
	asset_update_engines_uuids_fetch(&aucj->engines, bmain, NULL, 0, true);

	/* Early out if there is nothing to do! */
	if (BLI_listbase_is_empty(&aucj->engines)) {
		asset_updatecheck_free(aucj);
		return;
	}

	/* setup job */
	wm_job = WM_jobs_get(CTX_wm_manager(C), CTX_wm_window(C), CTX_wm_area(C), "Checking for asset updates...",
	                     WM_JOB_PROGRESS, WM_JOB_TYPE_ASSET_UPDATECHECK);
	WM_jobs_customdata_set(wm_job, aucj, asset_updatecheck_free);
	WM_jobs_timer(wm_job, 0.1, 0, 0/*NC_SPACE | ND_SPACE_FILE_LIST, NC_SPACE | ND_SPACE_FILE_LIST*/);  /* TODO probably outliner stuff once UI is defined for this! */
	WM_jobs_callbacks(wm_job, asset_updatecheck_startjob, NULL, asset_updatecheck_update, asset_updatecheck_endjob);

	/* start the job */
	WM_jobs_start(CTX_wm_manager(C), wm_job);
}


static int wm_assets_update_check_exec(bContext *C, wmOperator *UNUSED(op))
{
	asset_updatecheck_start(C);

	return OPERATOR_FINISHED;
}

void WM_OT_assets_update_check(wmOperatorType *ot)
{
	ot->name = "Check Assets Update";
	ot->idname = "WM_OT_assets_update_check";
	ot->description = "Check/refresh status of assets (in a background job)";

//	RNA_def_boolean(ot->srna, "use_scripts", true, "Trusted Source",
//	                "Allow .blend file to execute scripts automatically, default available from system preferences");

	ot->exec = wm_assets_update_check_exec;
}

static int wm_assets_reload_exec(bContext *C, wmOperator *op)
{
	/* We need to:
	 *   - get list of all asset IDs to reload (either via given uuids, or their tag), and regroup them by asset engine.
	 *   - tag somehow all their indirect 'dependencies' IDs.
	 *   - call load_pre to get actual filepaths.
	 *   - do reload/relocate and remap as in lib_reload.
	 *   - cleanup indirect dependencies IDs with zero users.
	 */
	Main *bmain = CTX_data_main(C);

	ListBase engines = {NULL};

	/* For now, ignore the uuids list of op. */
	asset_update_engines_uuids_fetch(&engines, bmain, NULL, UUID_TAG_ASSET_RELOAD, false);

	for (AssetUpdateCheckEngine *auce = engines.first; auce; auce = auce->next) {
		FileDirEntryArr *paths = BKE_asset_engine_uuids_load_pre(auce->ae, &auce->uuids);

		printf("Engine %s (ver. %d) returned root path '%s'\n", auce->ae->type->name, auce->ae->type->version, paths->root);
		for (FileDirEntry *en = paths->entries.first; en; en = en->next) {
			printf("\t-> %s\n", en->relpath);
		}

		BKE_filedir_entryarr_clear(paths);
		MEM_freeN(paths);
	}

	/* Cleanup. */
	for (AssetUpdateCheckEngine *auce = engines.first; auce; auce = auce->next) {
		BKE_asset_engine_free(auce->ae);
		MEM_SAFE_FREE(auce->uuids.uuids);
	}
	BLI_freelistN(&engines);

	G.f &= ~G_ASSETS_NEED_RELOAD;
	return OPERATOR_CANCELLED;

#if 0
	Library *lib;
	char lib_name[MAX_NAME];

	RNA_string_get(op->ptr, "library", lib_name);
	lib = (Library *)BKE_libblock_find_name_ex(CTX_data_main(C), ID_LI, lib_name);

	if (lib) {
		Main *bmain = CTX_data_main(C);
		Scene *scene = CTX_data_scene(C);
		PropertyRNA *prop;
		WMLinkAppendData *lapp_data;

		ListBase *lbarray[MAX_LIBARRAY];
		int lba_idx;

		LinkNode *itemlink;
		int item_idx;

		int num_ids;
		char path[FILE_MAX], root[FILE_MAXDIR], libname[FILE_MAX], relname[FILE_MAX];
		short flag = 0;

		if (RNA_boolean_get(op->ptr, "relative_path")) {
			flag |= FILE_RELPATH;
		}

		if (lib->parent && !reload) {
			BKE_reportf(op->reports, RPT_ERROR_INVALID_INPUT,
			            "Cannot relocate indirectly linked library '%s'", lib->filepath);
			return OPERATOR_CANCELLED;
		}

		RNA_string_get(op->ptr, "directory", root);
		RNA_string_get(op->ptr, "filename", libname);

		if (!BLO_has_bfile_extension(libname)) {
			BKE_report(op->reports, RPT_ERROR, "Not a library");
			return OPERATOR_CANCELLED;
		}

		BLI_join_dirfile(path, sizeof(path), root, libname);

		if (BLI_path_cmp(lib->filepath, path) == 0) {
			printf("We are supposed to reload '%s' lib (%d)...\n", lib->filepath, lib->id.us);

			lapp_data = wm_link_append_data_new(flag);
			wm_link_append_data_library_add(lapp_data, path);

			BKE_main_lock(bmain);

			lba_idx = set_listbasepointers(bmain, lbarray);
			while (lba_idx--) {
				ID *id = lbarray[lba_idx]->first;
				const short idcode = id ? GS(id->name) : 0;

				if (!id || !BKE_idcode_is_linkable(idcode)) {
					/* No need to reload non-linkable datatypes, those will get relinked with their 'users ID'. */
					continue;
				}

				for (; id; id = id->next) {
					if (id->lib == lib) {
						WMLinkAppendDataItem *item;

						/* We remove it from current Main, and add it to items to link... */
						/* Note that non-linkable IDs (like e.g. shapekeys) are also explicitely linked here... */
						BLI_remlink(lbarray[lba_idx], id);
						item = wm_link_append_data_item_add(lapp_data, id->name + 2, idcode, NULL, id);
						BLI_BITMAP_SET_ALL(item->libraries, true, lapp_data->num_libraries);

						printf("\tdatablock to seek for: %s\n", id->name);
					}
				}
			}

			BKE_main_id_tag_all(bmain, LIB_TAG_PRE_EXISTING, true);

			/* XXX For now, locking is not reentrant so it's not safe to call core linking code with locked Main. */
			BKE_main_unlock(bmain);

			/* We do not want any instanciation here! */
			wm_link_do(lapp_data, op->reports, bmain, NULL, NULL, NULL, true, true);

			BKE_main_lock(bmain);

			/* We add back old id to bmain.
			 * We need to do this in a first, separated loop, otherwise some of those may not be handled by
			 * ID remapping, which means they would still reference old data to be deleted... */
			for (item_idx = 0, itemlink = lapp_data->items.list; itemlink; item_idx++, itemlink = itemlink->next) {
				WMLinkAppendDataItem *item = itemlink->link;
				ID *old_id = item->customdata;

				BLI_assert(old_id);
				BLI_addtail(which_libbase(bmain, GS(old_id->name)), old_id);
			}

			for (item_idx = 0, itemlink = lapp_data->items.list; itemlink; item_idx++, itemlink = itemlink->next) {
				WMLinkAppendDataItem *item = itemlink->link;
				ID *old_id = item->customdata;
				ID *new_id = item->new_id;

				/* Since we asked for placeholders in case of missing IDs, we expect to always get a valid one. */
				BLI_assert(new_id);
				if (new_id) {
//					printf("before remap, old_id users: %d (%p), new_id users: %d (%p)\n", old_id->us, old_id, new_id->us, new_id);
					/* Note that here, we also want to replace indirect usages. */
					BKE_libblock_remap_locked(bmain, old_id, new_id, ID_REMAP_SKIP_NEVER_NULL_USAGE);

//					printf("after remap, old_id users: %d, new_id users: %d\n", old_id->us, new_id->us);

					/* In some cases, new_id might become direct link, remove parent of library in this case. */
					if (new_id->lib->parent && (new_id->tag & LIB_TAG_INDIRECT) == 0) {
						BLI_assert(0);  /* Should not happen in reload case... */
						new_id->lib->parent = NULL;
					}
				}

				if (old_id->us > 0 && new_id) {
					size_t len = strlen(old_id->name);

					/* XXX TODO This is utterly weak!!! */
					if (len > MAX_ID_NAME - 3 && old_id->name[len - 4] == '.') {
						old_id->name[len - 6] = '.';
						old_id->name[len - 5] = 'P';
					}
					else {
						len = MIN2(len, MAX_ID_NAME - 3);
						old_id->name[len] = '.';
						old_id->name[len + 1] = 'P';
						old_id->name[len + 2] = '\0';
					}

					id_sort_by_name(which_libbase(bmain, GS(old_id->name)), old_id);

					BKE_reportf(op->reports, RPT_WARNING,
					            "Lib Reload: Replacing all references to old datablock '%s' by reloaded one failed, "
					            "old one (%d remaining users) had to be kept and was renamed to '%s'",
					            new_id->name, old_id->us, old_id->name);
				}
			}

			BKE_main_unlock(bmain);

			for (item_idx = 0, itemlink = lapp_data->items.list; itemlink; item_idx++, itemlink = itemlink->next) {
				WMLinkAppendDataItem *item = itemlink->link;
				ID *old_id = item->customdata;

//				printf("%p\n", old_id);

				if (old_id->us == 0) {
					BKE_libblock_free(bmain, old_id);
					num_ids--;
				}
			}

			wm_link_append_data_free(lapp_data);
		}
		else {
			int totfiles = 0;

			printf("We are supposed to relocate '%s' lib to new '%s' one...\n", lib->filepath, libname);

			/* Check if something is indicated for relocate. */
			prop = RNA_struct_find_property(op->ptr, "files");
			if (prop) {
				totfiles = RNA_property_collection_length(op->ptr, prop);
				if (totfiles == 0) {
					if (!libname[0]) {
						BKE_report(op->reports, RPT_ERROR, "Nothing indicated");
						return OPERATOR_CANCELLED;
					}
				}
			}

			lapp_data = wm_link_append_data_new(flag);

			if (totfiles) {
				RNA_BEGIN (op->ptr, itemptr, "files")
				{
					RNA_string_get(&itemptr, "name", relname);

					BLI_join_dirfile(path, sizeof(path), root, relname);

					if (BLI_path_cmp(path, lib->filepath) == 0 || !BLO_has_bfile_extension(relname)) {
						continue;
					}

					printf("\t candidate new lib to reload datablocks from: %s\n", path);
					wm_link_append_data_library_add(lapp_data, path);
				}
				RNA_END;
			}
			else {
				printf("\t candidate new lib to reload datablocks from: %s\n", path);
				wm_link_append_data_library_add(lapp_data, path);
			}

			BKE_main_lock(bmain);

			lba_idx = set_listbasepointers(bmain, lbarray);
			while (lba_idx--) {
				ID *id = lbarray[lba_idx]->first;
				const int idcode = id ? GS(id->name) : 0;

				if (!id || !BKE_idcode_is_linkable(idcode)) {
					continue;
				}
				for (; id; id = id->next) {
					if (id->lib == lib) {
						WMLinkAppendDataItem *item = wm_link_append_data_item_add(
						                                 lapp_data, id->name + 2, idcode, NULL, id);
						BLI_BITMAP_SET_ALL(item->libraries, true, lapp_data->num_libraries);

						printf("\tdatablock to seek for: %s\n", id->name);
					}
				}
			}

			BKE_main_id_tag_all(bmain, LIB_TAG_PRE_EXISTING, true);

			/* XXX For now, locking is not reentrant so it's not safe to call core linking code with locked Main. */
			BKE_main_unlock(bmain);

			/* We do not want any instanciation here! */
			wm_link_do(lapp_data, op->reports, bmain, NULL, NULL, NULL, false, false);

			BKE_main_lock(bmain);

			for (item_idx = 0, itemlink = lapp_data->items.list; itemlink; item_idx++, itemlink = itemlink->next) {
				WMLinkAppendDataItem *item = itemlink->link;
				ID *old_id = item->customdata;
				ID *new_id = item->new_id;

				BLI_assert(old_id);
				if (new_id) {
//					printf("before remap, old_id users: %d, new_id users: %d\n", old_id->us, new_id->us);
					BKE_libblock_remap_locked(bmain, old_id, new_id,
					                          ID_REMAP_SKIP_INDIRECT_USAGE | ID_REMAP_SKIP_NEVER_NULL_USAGE);

					if (old_id->flag & LIB_FAKEUSER) {
						id_fake_user_clear(old_id);
						id_fake_user_set(new_id);
					}

//					printf("after remap, old_id users: %d, new_id users: %d\n", old_id->us, new_id->us);

					/* In some cases, new_id might become direct link, remove parent of library in this case. */
					if (new_id->lib->parent && (new_id->tag & LIB_TAG_INDIRECT) == 0) {
						new_id->lib->parent = NULL;
					}
				}
			}

			BKE_main_unlock(bmain);

			num_ids = lapp_data->num_items;
			for (item_idx = 0, itemlink = lapp_data->items.list; itemlink; item_idx++, itemlink = itemlink->next) {
				WMLinkAppendDataItem *item = itemlink->link;
				ID *old_id = item->customdata;

				if (old_id->us == 0) {
					BKE_libblock_free(bmain, old_id);
					num_ids--;
				}
			}

			if (num_ids == 0) {
				/* Nothing uses old lib anymore, we can get rid of it. */
				id_us_min(&lib->id);
				if (lib->id.us == 0) {
					BKE_libblock_free(bmain, (ID *)lib);
				}
			}

			wm_link_append_data_free(lapp_data);
		}

		/* Some datablocks can get reloaded/replaced 'silently' because they are not linkable (shape keys e.g.),
		 * so we need another loop here to clear old ones if possible. */
		lba_idx = set_listbasepointers(bmain, lbarray);
		while (lba_idx--) {
			ID *id = lbarray[lba_idx]->first;

			for (; id; id = id->next) {
				if (id->lib == lib && (id->flag & LIB_TAG_PRE_EXISTING) && id->us == 0) {
					BKE_libblock_free(bmain, id);
				}
			}
		}

		BKE_main_lib_objects_recalc_all(bmain);
		IMB_colormanagement_check_file_config(bmain);

		/* important we unset, otherwise these object wont
		 * link into other scenes from this blend file */
		BKE_main_id_tag_all(bmain, LIB_TAG_PRE_EXISTING, false);

		/* recreate dependency graph to include new objects */
		DAG_scene_relations_rebuild(bmain, scene);

		/* free gpu materials, some materials depend on existing objects, such as lamps so freeing correctly refreshes */
		GPU_materials_free();

		/* XXX TODO: align G.lib with other directory storage (like last opened image etc...) */
		BLI_strncpy(G.lib, root, FILE_MAX);

		WM_event_add_notifier(C, NC_WINDOW, NULL);

		return OPERATOR_FINISHED;
	}

	return OPERATOR_CANCELLED;
#endif
}

void WM_OT_assets_reload(wmOperatorType *ot)
{
	PropertyRNA *prop;

	ot->name = "Reload Assets";
	ot->idname = "WM_OT_assets_reload";
	ot->description = "Reload the given assets (either explicitely by their UUIDs, or all curently tagged for reloading)";

//	ot->invoke = wm_assets_reload_invoke;
	ot->exec = wm_assets_reload_exec;

	ot->flag |= OPTYPE_UNDO;  /* XXX Do we want to keep this? Is it even working? */

	prop = RNA_def_collection_runtime(ot->srna, "uuids", &RNA_AssetUUID, "UUIDs", "UUIDs of assets to reload");
	RNA_def_property_flag(prop, PROP_HIDDEN);
}

/** \} */
