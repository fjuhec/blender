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
 * The Original Code is Copyright (C) 2015 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file BKE_asset.h
 *  \ingroup bke
 */

#ifndef __BKE_ASSET_H__
#define __BKE_ASSET_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "DNA_space_types.h"

struct AssetEngine;
struct AssetEngineType;
struct AssetUUIDList;
struct FileDirEntryArr;
struct FileDirEntry;
struct FileDirEntryVariant;
struct FileDirEntryRevision;
struct ExtensionRNA;
struct ID;
struct IDProperty;
struct ListBase;
struct Main;
struct ReportList;
struct uiLayout;

enum {
	AE_STATUS_VALID   = 1 << 0,  /* Asset engine is "OK" (if unset engine won't be used). */
	AE_STATUS_RUNNING = 1 << 1,  /* Asset engine is performing some background tasks... */
};

#define AE_FAKE_ENGINE_ID "NONE"

extern ListBase asset_engines;

/* AE instance/job is valid, is running, is idle, etc. */
typedef int (*ae_status)(struct AssetEngine *engine, const int job_id);

/* Report progress ([0.0, 1.0] range) of given job. */
typedef float (*ae_progress)(struct AssetEngine *engine, const int job_id);

/* To force end of given job (e.g. because it was cancelled by user...). */
typedef void (*ae_kill)(struct AssetEngine *engine, const int job_id);

/* ***** All callbacks below shall be non-blocking (i.e. return immediately). ***** */

/* Those callbacks will be called from a 'fake-job' start *and* update functions (i.e. main thread, working one will
 * just sleep).
 *
 * If given id is not AE_JOB_ID_UNSET, engine should update from a running job if available, otherwise it should
 * start a new one.
 * It is the responsability of the engine to start/stop background processes to actually perform tasks as/if needed.
 *
 * If the engine returns AE_JOB_ID_INVALID as job id, then code assumes whole execution was done in that single first
 * call (i.e. allows engine that do not need it to not bother with whole async crap - they should then process
 * the whole request in a very short amount of time (typically below 100ms).
 */
#define AE_JOB_ID_UNSET 0
#define AE_JOB_ID_INVALID -1

/* FILEBROWSER - List everything available at given root path - only returns numbers of entries! */
typedef int (*ae_list_dir)(struct AssetEngine *engine, const int job_id, struct FileDirEntryArr *entries_r);

/* 'update' hook, called to prepare updating of given entries (typically after a file (re)load).
 * Engine should check whether given assets are still valid, if they should be updated, etc.
 * uuids tagged as needing reload will then be reloaded as new ones
 * (ae_load_pre, then actual lib loading, then ae_load_post).
 * \warning This callback is expected to handle **real** UUIDS (not 'users' filebrowser ones),
 *          i.e. calling ae_load_pre with those shall **not** alters them in returned direntries
 *          (else 'link' between old IDs and reloaded ones would be broken). */
typedef int (*ae_update_check)(struct AssetEngine *engine, const int job_id, struct AssetUUIDList *uuids);

/* Ensure given assets (uuids) are really available for append/link (some kind of 'anticipated loading'...).
 * Note: Engine should expect any kind of UUIDs it produced here
 *       (i.e. real ones as well as 'virtual' filebrowsing ones). */
typedef int (*ae_ensure_uuids)(struct AssetEngine *engine, const int job_id, struct AssetUUIDList *uuids);

/* ***** All callbacks below are blocking. They shall be completed upon return. ***** */

/* FILEBROWSER - Perform sorting and/or filtering on engines' side.
 * Note that engine is assumed to feature its own sorting/filtering settings!
 * Number of available filtered entries is to be set in entries_r.
 */
typedef bool (*ae_sort_filter)(struct AssetEngine *engine, const bool sort, const bool filter,
                               struct FileSelectParams *params, struct FileDirEntryArr *entries_r);

/* FILEBROWSER - Return specified block of entries in entries_r. */
typedef bool (*ae_entries_block_get)(struct AssetEngine *engine, const int start_index, const int end_index,
                                     struct FileDirEntryArr *entries_r);

/* FILEBROWSER - Return specified entries from their uuids, in entries_r. */
typedef bool (*ae_entries_uuid_get)(struct AssetEngine *engine, struct AssetUUIDList *uuids,
                                    struct FileDirEntryArr *entries_r);

/* 'pre-loading' hook, called before opening/appending/linking/updating given entries.
 * Note first given uuid is the one of 'active' entry, and first entry in returned list will be considered as such too.
 * E.g. allows the engine to ensure entries' paths are actually valid by downloading requested data, etc.
 * If is_virtual is True, then there is no requirement that returned paths actually exist.
 * Note that the generated list shall be simpler than the one generated by ae_list_dir, since only the path from
 * active revision is used, no need to bother with variants, previews, etc.
 * This allows to present 'fake' entries to user, and then import actual data.
 */
typedef bool (*ae_load_pre)(struct AssetEngine *engine, struct AssetUUIDList *uuids,
                            struct FileDirEntryArr *entries_r);

/* 'post-loading' hook, called after opening/appending/linking/updating given entries.
 * E.g. allows an advanced engine to make fancy scripted operations over loaded items. */
/* TODO */
typedef bool (*ae_load_post)(struct AssetEngine *engine, struct ID *items, const int *num_items);

/* Check if given dirpath is valid for current asset engine, it can also modify it.
 * r_dir is assumed to be least FILE_MAX. */
typedef void (*ae_check_dir)(struct AssetEngine *engine, char *r_dir);

typedef struct AssetEngineType {
	struct AssetEngineType *next, *prev;

	/* type info */
	char idname[64]; /* best keep the same size as BKE_ST_MAXNAME */
	int version;

	char name[64];
	int flag;

	/* API */
	ae_status status;
	ae_progress progress;

	ae_kill kill;

	ae_list_dir list_dir;
	ae_sort_filter sort_filter;
	ae_entries_block_get entries_block_get;
	ae_entries_uuid_get entries_uuid_get;

	ae_ensure_uuids ensure_uuids;

	ae_load_pre load_pre;
	ae_load_post load_post;
	ae_update_check update_check;
	ae_check_dir check_dir;

	/* RNA integration */
	struct ExtensionRNA ext;
} AssetEngineType;

typedef struct AssetEngine {
	AssetEngineType *type;
	void *py_instance;

	/* Custom sub-classes properties. */
	IDProperty *properties;

	int flag;
	int refcount;

	struct ReportList *reports;
} AssetEngine;

/* AssetEngine->flag */
enum {
	AE_DIRTY_FILTER  = 1 << 0,
	AE_DIRTY_SORTING = 1 << 1,
};

/* Engine Types */
void BKE_asset_engines_init(void);
void BKE_asset_engines_exit(void);

AssetEngineType *BKE_asset_engines_find(const char *idname);
AssetEngineType *BKE_asset_engines_get_default(char *r_idname, const size_t len);

/* Engine Instances */
AssetEngine *BKE_asset_engine_create(AssetEngineType *type, struct ReportList *reports);
AssetEngine *BKE_asset_engine_copy(AssetEngine *engine);
void BKE_asset_engine_free(AssetEngine *engine);

AssetUUIDList *BKE_asset_engine_load_pre(AssetEngine *engine, struct FileDirEntryArr *r_entries);

/* File listing utils... */

typedef enum FileCheckType {
	CHECK_NONE  = 0,
	CHECK_DIRS  = 1 << 0,
	CHECK_FILES = 1 << 1,
	CHECK_ALL   = CHECK_DIRS | CHECK_FILES,
} FileCheckType;

void BKE_filedir_revision_free(struct FileDirEntryRevision *rev);

void BKE_filedir_variant_free(struct FileDirEntryVariant *var);

void BKE_filedir_entry_free(struct FileDirEntry *entry);
void BKE_filedir_entry_clear(struct FileDirEntry *entry);
struct FileDirEntry *BKE_filedir_entry_copy(struct FileDirEntry *entry);

void BKE_filedir_entryarr_clear(struct FileDirEntryArr *array);

#define ASSETUUID_SUB_COMPARE(_uuida, _uuidb, _member) \
	(memcmp((_uuida)->_member, (_uuidb)->_member, sizeof((_uuida)->_member)) == 0)

#define ASSETUUID_COMPARE(_uuida, _uuidb) \
	(ASSETUUID_SUB_COMPARE(_uuida, _uuidb, uuid_asset) && \
	 ASSETUUID_SUB_COMPARE(_uuida, _uuidb, uuid_variant) && \
	 ASSETUUID_SUB_COMPARE(_uuida, _uuidb, uuid_revision))

/* GHash helpers */
unsigned int BKE_asset_uuid_hash(const void *key);
bool BKE_asset_uuid_cmp(const void *a, const void *b);

#ifdef __cplusplus
}
#endif

#endif /* __BKE_ASSET_H__ */
