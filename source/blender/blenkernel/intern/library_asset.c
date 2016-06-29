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
 * The Original Code is Copyright (C) 2015,2016 by Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Bastien Montagne.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/library_asset.c
 *  \ingroup bke
 *
 * Contains asset-related management of ID's and libraries.
 */

#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"

#include "RNA_types.h"

#include "BKE_asset_engine.h"
#include "BKE_library.h"
#include "BKE_library_query.h"
#include "BKE_main.h"


/* Asset managing - TODO: we most likely want to turn this into a hashing at some point, could become a bit slow
 *                        when having huge assets (or many of them)... */
void BKE_library_asset_repository_init(Library *lib, const AssetEngineType *aet, const char *repo_root)
{
	BKE_library_asset_repository_free(lib);
	lib->asset_repository = MEM_mallocN(sizeof(*lib->asset_repository), __func__);

	BLI_strncpy(lib->asset_repository->asset_engine, aet->idname, sizeof(lib->asset_repository->asset_engine));
	lib->asset_repository->asset_engine_version = aet->version;
	BLI_strncpy(lib->asset_repository->root, repo_root, sizeof(lib->asset_repository->root));

	BLI_listbase_clear(&lib->asset_repository->assets);
}

void BKE_library_asset_repository_clear(Library *lib)
{
	if (lib->asset_repository) {
		for (AssetRef *aref; (aref = BLI_pophead(&lib->asset_repository->assets)); ) {
			BLI_freelistN(&aref->id_list);
			MEM_freeN(aref);
		}
	}
}

void BKE_library_asset_repository_free(Library *lib)
{
	if (lib->asset_repository) {
		BKE_library_asset_repository_clear(lib);
		MEM_freeN(lib->asset_repository);
		lib->asset_repository = NULL;
	}
}

AssetRef *BKE_library_asset_repository_asset_add(Library *lib, const void *idv)
{
	const ID *id = idv;
	BLI_assert(id->uuid != NULL);

	AssetRef *aref = BKE_library_asset_repository_asset_find(lib, idv);
	if (!aref) {
		aref = MEM_callocN(sizeof(*aref), __func__);
		aref->uuid = *id->uuid;
		BKE_library_asset_repository_subdata_add(aref, idv);
		BLI_addtail(&lib->asset_repository->assets, aref);
	}

	return aref;
}

AssetRef *BKE_library_asset_repository_asset_find(Library *lib, const void *idv)
{
	const ID *id = idv;
	BLI_assert(id->uuid != NULL);

	for (AssetRef *aref = lib->asset_repository->assets.first; aref; aref = aref->next) {
		if (ASSETUUID_COMPARE(&aref->uuid, id->uuid)) {
#ifndef NDEBUG
			LinkData *link = aref->id_list.first;
			BLI_assert(link && (link->data == idv));
#endif
			return aref;
		}
	}
	return NULL;
}

void BKE_library_asset_repository_asset_remove(Library *lib, const void *idv)
{
	AssetRef *aref = BKE_library_asset_repository_asset_find(lib, idv);
	BLI_remlink(&lib->asset_repository->assets, aref);
	BLI_freelistN(&aref->id_list);
	MEM_freeN(aref);
}

void BKE_library_asset_repository_subdata_add(AssetRef *aref, const void *idv)
{
	if (BLI_findptr(&aref->id_list, idv, offsetof(LinkData, data)) == NULL) {
		BLI_addtail(&aref->id_list, BLI_genericNodeN((void *)idv));
	}
}

void BKE_library_asset_repository_subdata_remove(AssetRef *aref, const void *idv)
{
	LinkData *link = BLI_findptr(&aref->id_list, idv, offsetof(LinkData, data));
	if (link) {
		BLI_freelinkN(&aref->id_list, link);
	}
}

void BKE_libraries_asset_subdata_remove(Main *bmain, const void *idv)
{
	const ID *id = idv;

	if (id->lib == NULL) {
		return;
	}

	ListBase *lb = which_libbase(bmain, ID_LI);
	for (Library *lib = lb->first; lib; lib = lib->id.next) {
		if (lib->asset_repository) {
			for (AssetRef *aref = lib->asset_repository->assets.first; aref; aref = aref->next) {
				BLI_freelinkN(&aref->id_list, BLI_findptr(&aref->id_list, idv, offsetof(LinkData, data)));
			}
		}
	}
}

void BKE_libraries_asset_repositories_clear(Main *bmain)
{
	ListBase *lb = which_libbase(bmain, ID_LI);
	for (Library *lib = lb->first; lib; lib = lib->id.next) {
		BKE_library_asset_repository_clear(lib);
	}
	BKE_main_id_tag_all(bmain, LIB_TAG_ASSET, false);
}

static int library_asset_dependencies_rebuild_cb(void *userdata, ID *id_self, ID **idp, int UNUSED(cd_flag))
{
	if (!idp || !*idp) {
		return IDWALK_RET_NOP;
	}

	AssetRef *aref = userdata;
	ID *id = *idp;

	if (id->uuid) {
		return IDWALK_RET_STOP_RECURSION;
	}

	printf("%s (from %s)\n", id->name, id_self->name);

	BKE_library_asset_repository_subdata_add(aref, (const void *)id);
	id->tag |= LIB_TAG_ASSET;
	return IDWALK_RET_NOP;
}

static void library_asset_dependencies_rebuild(ID *asset)
{
	Library *lib = asset->lib;
	BLI_assert(lib && lib->asset_repository);

	if (!(lib && lib->asset_repository)) {
		printf("asset: %s\n", asset->name);
		printf("lib: %p\n", lib);
		printf("lib: %s\n", lib->id.name);
		printf("lib: %s\n", lib->name);
		printf("lib: %p\n\n\n", lib->asset_repository);
	}

	asset->tag |= LIB_TAG_ASSET;

	AssetRef *aref = BKE_library_asset_repository_asset_add(lib, asset);

	BKE_library_foreach_ID_link(asset, library_asset_dependencies_rebuild_cb, aref, IDWALK_RECURSE);
}

void BKE_libraries_asset_repositories_rebuild(Main *bmain)
{
	ListBase *lbarray[MAX_LIBARRAY];
	ID *id;
	int a;

	BKE_libraries_asset_repositories_clear(bmain);

	a = set_listbasepointers(bmain, lbarray);
	while (a--) {
		for (id = lbarray[a]->first; id; id = id->next) {
			if (id->uuid) {
				library_asset_dependencies_rebuild(id);
			}
		}
	}
}

AssetRef *BKE_libraries_asset_repository_uuid_find(Main *bmain, const AssetUUID *uuid)
{
	ListBase *lb = which_libbase(bmain, ID_LI);
	for (Library *lib = lb->first; lib; lib = lib->id.next) {
		for (AssetRef *aref = lib->asset_repository->assets.first; aref; aref = aref->next) {
			if (ASSETUUID_COMPARE(&aref->uuid, uuid)) {
#ifndef NDEBUG
				LinkData *link = aref->id_list.first;
				BLI_assert(link && ((ID *)link->data)->uuid && ASSETUUID_COMPARE(((ID *)link->data)->uuid, uuid));
#endif
				return aref;
			}
		}
	}
	return NULL;
}

/** Find or add the 'virtual' library datablock matching this asset engine, used for non-blend-data assets. */
Library *BKE_library_asset_virtual_ensure(Main *bmain, const AssetEngineType *aet)
{
	Library *lib;
	ListBase *lb = which_libbase(bmain, ID_LI);

	for (lib = lb->first; lib; lib = lib->id.next) {
		if (!(lib->flag & LIBRARY_FLAG_VIRTUAL) || !lib->asset_repository) {
			continue;
		}

		if (STREQ(lib->asset_repository->asset_engine, aet->idname) &&
		    lib->asset_repository->asset_engine_version == aet->version)
		{
			return lib;
		}
	}

	lib = BKE_libblock_alloc(bmain, ID_LI, "VirtualLib");
	BKE_library_asset_repository_init(lib, aet, "");
	lib->flag |= LIBRARY_FLAG_VIRTUAL;
	return lib;
}
