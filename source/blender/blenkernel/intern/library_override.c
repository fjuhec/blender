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
 * The Original Code is Copyright (C) 2016 by Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Bastien Montagne.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/library_override.c
 *  \ingroup bke
 */

#include <stdlib.h>

#include "MEM_guardedalloc.h"

#include "DNA_ID.h"
#include "DNA_object_types.h"

#include "BKE_depsgraph.h"
#include "BKE_library.h"
#include "BKE_library_override.h"
#include "BKE_library_remap.h"
#include "BKE_main.h"

#include "BLI_utildefines.h"
#include "BLI_listbase.h"
#include "BLI_string.h"

#include "RNA_access.h"
#include "RNA_types.h"

#include "PIL_time.h"
#include "PIL_time_utildefines.h"

#define OVERRIDE_AUTO_CHECK_DELAY 0.2  /* 200ms between auto-override checks. */

static void bke_override_property_clear(IDOverrideProperty *op);
static void bke_override_property_operation_clear(IDOverridePropertyOperation *opop);

/** Initialize empty overriding of \a reference_id by \a local_id. */
IDOverride *BKE_override_init(struct ID *local_id, struct ID *reference_id)
{
	BLI_assert(reference_id->lib != NULL);

	local_id->override = MEM_callocN(sizeof(*local_id->override), __func__);
	local_id->override->reference = reference_id;
	id_us_plus(reference_id);
	local_id->tag &= ~LIB_TAG_OVERRIDE_OK;
	/* TODO do we want to add tag or flag to referee to mark it as such? */
	return local_id->override;
}

/** Clear any overriding data from given \a override. */
void BKE_override_clear(struct IDOverride *override)
{
	BLI_assert(override != NULL);

	for (IDOverrideProperty *op = override->properties.first; op; op = op->next) {
		bke_override_property_clear(op);
	}
	BLI_freelistN(&override->properties);
}

/** Free given \a override. */
void BKE_override_free(struct IDOverride **override)
{
	BLI_assert(*override != NULL);

	BKE_override_clear(*override);
	MEM_freeN(*override);
	*override = NULL;
}

/**
 * Find override property from given RNA path, if it exists.
 */
IDOverrideProperty *BKE_override_property_find(IDOverride *override, const char *rna_path)
{
	/* XXX TODO we'll most likely want a runtime ghash to store taht mapping at some point. */
	return BLI_findstring_ptr(&override->properties, rna_path, offsetof(IDOverrideProperty, rna_path));
}

/**
 * Find override property from given RNA path, or create it if it does not exist.
 */
IDOverrideProperty *BKE_override_property_get(IDOverride *override, const char *rna_path, bool *r_created)
{
	/* XXX TODO we'll most likely want a runtime ghash to store taht mapping at some point. */
	IDOverrideProperty *op = BKE_override_property_find(override, rna_path);

	if (op == NULL) {
		op = MEM_callocN(sizeof(IDOverrideProperty), __func__);
		op->rna_path = BLI_strdup(rna_path);
		BLI_addtail(&override->properties, op);

		if (r_created) {
			*r_created = true;
		}
	}
	else if (r_created) {
		*r_created = false;
	}

	return op;
}

void bke_override_property_clear(IDOverrideProperty *op)
{
	BLI_assert(op->rna_path != NULL);

	MEM_freeN(op->rna_path);

	for (IDOverridePropertyOperation *opop = op->operations.first; opop; opop = opop->next) {
		bke_override_property_operation_clear(opop);
	}
	BLI_freelistN(&op->operations);
}

/**
 * Remove and free given \a override_property from given ID \a override.
 */
void BKE_override_property_delete(IDOverride *override, IDOverrideProperty *override_property)
{
	bke_override_property_clear(override_property);
	BLI_freelinkN(&override->properties, override_property);
}

/**
 * Find override property operation from given sub-item(s), if it exists.
 */
IDOverridePropertyOperation *BKE_override_property_operation_find(
        IDOverrideProperty *override_property,
        const char *subitem_refname, const char *subitem_locname,
        const int subitem_refindex, const int subitem_locindex, const bool strict, bool *r_strict)
{
	IDOverridePropertyOperation *opop;
	const int subitem_defindex = -1;

	if (r_strict) {
		*r_strict = true;
	}

	if (subitem_locname &&
	    (opop = BLI_findstring_ptr(&override_property->operations, subitem_locname,
	                               offsetof(IDOverridePropertyOperation, subitem_local_name))))
	{
		return opop;
	}

	if (subitem_refname &&
	    (opop = BLI_findstring_ptr(&override_property->operations, subitem_refname,
	                               offsetof(IDOverridePropertyOperation, subitem_reference_name))))
	{
		return opop;
	}

	if ((opop = BLI_listbase_bytes_find(&override_property->operations, &subitem_locindex, sizeof(subitem_locindex),
	                                    offsetof(IDOverridePropertyOperation, subitem_local_index))))
	{
		return opop;
	}

	if ((opop = BLI_listbase_bytes_find(&override_property->operations, &subitem_refindex, sizeof(subitem_refindex),
	                                    offsetof(IDOverridePropertyOperation, subitem_reference_index))))
	{
		return opop;
	}

	/* index == -1 means all indices, that is valid fallback in case we requested specific index. */
	if (!strict && (subitem_locindex != subitem_defindex) &&
	    (opop = BLI_listbase_bytes_find(&override_property->operations, &subitem_defindex, sizeof(subitem_defindex),
	                                    offsetof(IDOverridePropertyOperation, subitem_local_index))))
	{
		if (r_strict) {
			*r_strict = false;
		}
		return opop;
	}

	return NULL;
}

/**
 * Find override property operation from given sub-item(s), or create it if it does not exist.
 */
IDOverridePropertyOperation *BKE_override_property_operation_get(
        IDOverrideProperty *override_property, const short operation,
        const char *subitem_refname, const char *subitem_locname,
        const int subitem_refindex, const int subitem_locindex,
        const bool strict, bool *r_strict, bool *r_created)
{
	IDOverridePropertyOperation *opop = BKE_override_property_operation_find(override_property,
	                                                                         subitem_refname, subitem_locname,
	                                                                         subitem_refindex, subitem_locindex,
	                                                                         strict, r_strict);

	if (opop == NULL) {
		opop = MEM_callocN(sizeof(IDOverridePropertyOperation), __func__);
		opop->operation = operation;
		if (subitem_locname) {
			opop->subitem_local_name = BLI_strdup(subitem_locname);
		}
		if (subitem_refname) {
			opop->subitem_reference_name = BLI_strdup(subitem_refname);
		}
		opop->subitem_local_index = subitem_locindex;
		opop->subitem_reference_index = subitem_refindex;

		BLI_addtail(&override_property->operations, opop);

		if (r_created) {
			*r_created = true;
		}
	}
	else if (r_created) {
		*r_created = false;
	}

	return opop;
}

void bke_override_property_operation_clear(IDOverridePropertyOperation *opop)
{
	if (opop->subitem_reference_name) {
		MEM_freeN(opop->subitem_reference_name);
	}
	if (opop->subitem_local_name) {
		MEM_freeN(opop->subitem_local_name);
	}
}

/**
 * Remove and free given \a override_property_operation from given ID \a override_property.
 */
void BKE_override_property_operation_delete(
        IDOverrideProperty *override_property, IDOverridePropertyOperation *override_property_operation)
{
	bke_override_property_operation_clear(override_property_operation);
	BLI_freelinkN(&override_property->operations, override_property_operation);
}

/**
 * Check that status of local data-block is still valid against current reference one.
 *
 * It means that all overridable, but not overridden, properties' local values must be equal to reference ones.
 * Clears LIB_TAG_OVERRIDE_OK if they do not.
 *
 * This is typically used to detect whether some property has been changed in local and a new IDOverrideProperty
 * (of IDOverridePropertyOperation) has to be added.
 *
 * \return true if status is OK, false otherwise. */
bool BKE_override_status_check_local(ID *local)
{
	BLI_assert(local->override != NULL);

	ID *reference = local->override->reference;

	BLI_assert(reference != NULL && GS(local->name) == GS(reference->name));

	/* Note that reference is assumed always valid, caller has to ensure that itself. */

	PointerRNA rnaptr_local, rnaptr_reference;
	RNA_id_pointer_create(local, &rnaptr_local);
	RNA_id_pointer_create(reference, &rnaptr_reference);

	if (!RNA_struct_override_matches(&rnaptr_local, &rnaptr_reference, local->override, true, true)) {
		local->tag &= ~LIB_TAG_OVERRIDE_OK;
		return false;
	}

	return true;
}

/**
 * Check that status of reference data-block is still valid against current local one.
 *
 * It means that all non-overridden properties' local values must be equal to reference ones.
 * Clears LIB_TAG_OVERRIDE_OK if they do not.
 *
 * This is typically used to detect whether some reference has changed and local needs to be updated against it.
 *
 * \return true if status is OK, false otherwise. */
bool BKE_override_status_check_reference(ID *local)
{
	BLI_assert(local->override != NULL);

	ID *reference = local->override->reference;

	BLI_assert(reference != NULL && GS(local->name) == GS(reference->name));

	if (reference->override && (reference->tag & LIB_TAG_OVERRIDE_OK) == 0) {
		if (!BKE_override_status_check_reference(reference)) {
			/* If reference is also override of another data-block, and its status is not OK,
			 * then this override is not OK either.
			 * Note that this should only happen when reloading libraries... */
			local->tag &= ~LIB_TAG_OVERRIDE_OK;
			return false;
		}
	}

	PointerRNA rnaptr_local, rnaptr_reference;
	RNA_id_pointer_create(local, &rnaptr_local);
	RNA_id_pointer_create(reference, &rnaptr_reference);

	if (!RNA_struct_override_matches(&rnaptr_local, &rnaptr_reference, local->override, false, true)) {
		local->tag &= ~LIB_TAG_OVERRIDE_OK;
		return false;
	}

	return true;
}

/**
 * Compares local and reference data-blocks and create new override operations as needed,
 * or reset to reference values if overriding is not allowed.
 *
 * \note Defining override operations is only mandatory before saving a .blend file on disk (not for undo!).
 * Knowing that info at runtime is only useful for UI/UX feedback.
 *
 * \note This is by far the biggest operation (the more time-consuming) of the three so far, since it has to go over
 * all properties in depth (all overridable ones at least). Generating diff values and applying overrides
 * are much cheaper.
 *
 * \return true is new overriding op was created, or some local data was reset. */
bool BKE_override_operations_create(ID *local, const bool no_skip)
{
	BLI_assert(local->override != NULL);
	bool ret = false;

	if (local->flag & LIB_AUTOOVERRIDE) {
		/* This prevents running that (heavy) callback too often when editing data. */
		const double currtime = PIL_check_seconds_timer();
		if (!no_skip && (currtime - local->override->last_auto_run) < OVERRIDE_AUTO_CHECK_DELAY) {
			return ret;
		}
		local->override->last_auto_run = currtime;

		PointerRNA rnaptr_local, rnaptr_reference;
		RNA_id_pointer_create(local, &rnaptr_local);
		RNA_id_pointer_create(local->override->reference, &rnaptr_reference);

		ret = RNA_struct_auto_override(&rnaptr_local, &rnaptr_reference, local->override, NULL);
		if (ret) {
			printf("We did generate static override rules for %s\n", local->name);
		}
		else {
			printf("No new static override rules for %s\n", local->name);
		}
	}
	return ret;
}

/** Update given override from its reference (re-applying overriden properties). */
void BKE_override_update(Main *bmain, ID *local)
{
	if (local->override == NULL) {
		return;
	}

	/* Recursively do 'ancestors' overrides first, if any. */
	if (local->override->reference->override && (local->override->reference->tag & LIB_TAG_OVERRIDE_OK) == 0) {
		BKE_override_update(bmain, local->override->reference);
	}

	/* We want to avoid having to remap here, however creating up-to-date override is much simpler if based
	 * on reference than on current override.
	 * So we work on temp copy of reference. */

	/* XXX We need a way to get off-Main copies of IDs (similar to localized mats/texts/ etc.)!
	 *     However, this is whole bunch of code work in itself, so for now plain stupid ID copy will do,
	 *     as innefficient as it is. :/
	 *     Actually, maybe not! Since we are swapping with original ID's local content, we want to keep
	 *     usercount in correct state when freeing tmp_id (and that usercounts of IDs used by 'new' local data
	 *     also remain correct). */

	ID *tmp_id;
	id_copy(bmain, local->override->reference, &tmp_id, false);  /* XXX ...and worse of all, this won't work with scene! */

	if (tmp_id == NULL) {
		return;
	}

	PointerRNA rnaptr_local, rnaptr_final, rnaptr_storage_stack, *rnaptr_storage = NULL;
	RNA_id_pointer_create(local, &rnaptr_local);
	RNA_id_pointer_create(tmp_id, &rnaptr_final);
	if (local->override->storage) {
		rnaptr_storage = &rnaptr_storage_stack;
		RNA_id_pointer_create(local->override->storage, rnaptr_storage);
	}

	RNA_struct_override_apply(&rnaptr_final, &rnaptr_local, rnaptr_storage, local->override);

	/* This also transfers all pointers (memory) owned by local to tmp_id, and vice-versa. So when we'll free tmp_id,
	 * we'll actually free old, outdated data from local. */
	BKE_id_swap(local, tmp_id);
	/* Swap above may have broken internal references to itself. */
	BKE_libblock_relink_ex(bmain, local, tmp_id, local, false);
	BKE_libblock_relink_ex(bmain, tmp_id, local, tmp_id, false);  /* Grrrr... */

	/* Again, horribly innefficient in our case, we need something off-Main (aka moar generic nolib copy/free stuff)! */
	/* XXX And crashing in complex cases (e.g. because depsgraph uses same data...). */
	BKE_libblock_free_ex(bmain, tmp_id, true, false);

	if (local->override->storage) {
		/* We know this datablock is not used anywhere besides local->override->storage. */
		/* XXX For until we get fully shadow copies, we still need to ensure storage releases
		 *     its usage of any ID pointers it may have. */
		BKE_libblock_free_ex(bmain, local->override->storage, true, false);
		local->override->storage = NULL;
	}

	local->tag |= LIB_TAG_OVERRIDE_OK;

	/* Full rebuild of DAG! */
	DAG_relations_tag_update(bmain);
}

/** Update all overrides from given \a bmain. */
void BKE_main_override_update(Main *bmain)
{
	ListBase *lbarray[MAX_LIBARRAY];
	int base_count, i;

	base_count = set_listbasepointers(bmain, lbarray);

	for (i = 0; i < base_count; i++) {
		ListBase *lb = lbarray[i];
		ID *id;

		for (id = lb->first; id; id = id->next) {
			if (id->override != NULL && id->lib == NULL) {
				BKE_override_update(bmain, id);
			}
		}
	}
}

/***********************************************************************************************************************
 * Storage (how to wtore overriding data into .blend files).
 *
 * Basically:
 * I) Only 'differential' storage needs special handling here. All others (replacing values or
 *    inserting/removing items from a collection) can be handled with simply storing current content of local data-block.
 * II) We store the differential value into a second 'ghost' data-block, which is an empty ID of same type as local one,
 *     where we only define values that need differential data.
 *
 * This avoids us having to modify 'real' data-block at write time (and retoring it afterwards), which is inneficient,
 * and potentially dangerous (in case of concurrent access...), while not using much extra memory in typical cases.
 * It also ensures stored data-block always contains exact same data as "desired" ones (kind of "baked" data-blocks).
 */

/** Initialize an override storage. */
OverrideStorage *BKE_override_operations_store_initialize(void)
{
	return BKE_main_new();
}

/**
 * Generate suitable 'write' data (this only affects differential override operations).
 *
 * \note ID is in 'invalid' state for all usages but being written to file, after this function has been called and
 * until \a BKE_override_operations_store_end is called to restore it. */
ID *BKE_override_operations_store_start(OverrideStorage *override_storage, ID *local)
{
	BLI_assert(local->override != NULL);
	BLI_assert(override_storage != NULL);

	/* Forcefully ensure we now about all needed override operations. */
	BKE_override_operations_create(local, true);

	ID *storage_id;
	TIMEIT_START_AVERAGED(BKE_override_operations_store_start);

	/* Here we work on original local data-block, after having made a temp copy of it.
	 * Once we are done, _store_end() will swap temp and local contents.
	 * This allows us to keep most of original data to write (which is needed to (hopefully) avoid memory/pointers
	 * collisions in .blend file), and also neats things like original ID name. ;) */
	/* Note: ideally I'd rather work on copy here as well, and not touch to original at all, but then we'd have
	 * issues with ID data itself (which is currently not swapped by BKE_id_swap()) AND pointers overlapping. */

	/* XXX TODO We *need* an id_copy_nolib(), that stays out of Main and does not inc/dec ID pointers... */
	id_copy(override_storage, local, &storage_id, false);  /* XXX ...and worse of all, this won't work with scene! */

	if (storage_id != NULL) {
		PointerRNA rnaptr_reference, rnaptr_final, rnaptr_storage;
		RNA_id_pointer_create(local->override->reference, &rnaptr_reference);
		RNA_id_pointer_create(local, &rnaptr_final);
		RNA_id_pointer_create(storage_id, &rnaptr_storage);

		if (!RNA_struct_override_store(&rnaptr_final, &rnaptr_reference, &rnaptr_storage, local->override)) {
			BKE_libblock_free_ex(override_storage, storage_id, true, false);
			storage_id = NULL;
		}
	}

	local->override->storage = storage_id;

	TIMEIT_END_AVERAGED(BKE_override_operations_store_start);
	return storage_id;
}

/** Restore given ID modified by \a BKE_override_operations_store_start, to its valid original state. */
void BKE_override_operations_store_end(OverrideStorage *UNUSED(override_storage), ID *local)
{
	BLI_assert(local->override != NULL);

	/* Nothing else to do here really, we need to keep all temp override storage data-blocks in memory until
	 * whole file is written anyway (otherwise we'd get mem pointers overlap...). */
	local->override->storage = NULL;
}

void BKE_override_operations_store_finalize(OverrideStorage *override_storage)
{
	/* We cannot just call BKE_main_free(override_storage), not until we have option to make 'ghost' copies of IDs
	 * without increasing usercount of used data-blocks... */
	ListBase *lbarray[MAX_LIBARRAY];
	int base_count, i;

	base_count = set_listbasepointers(override_storage, lbarray);

	for (i = 0; i < base_count; i++) {
		ListBase *lb = lbarray[i];
		ID *id;

		while ((id = lb->first)) {
			BKE_libblock_free_ex(override_storage, id, true, false);
		}
	}

	BKE_main_free(override_storage);
}
