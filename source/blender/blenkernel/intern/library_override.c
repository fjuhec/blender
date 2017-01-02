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

#include "BKE_global.h"  /* XXX Yuck! temp hack! */
#include "BKE_library.h"
#include "BKE_library_override.h"
#include "BKE_main.h"

#include "BLI_utildefines.h"
#include "BLI_listbase.h"

#include "RNA_access.h"
#include "RNA_types.h"


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
		BLI_assert(op->rna_path != NULL);

		MEM_freeN(op->rna_path);

		for (IDOverridePropertyOperation *opop = op->operations.first; opop; opop = opop->next) {
			if (opop->subitem_reference_name) {
				MEM_freeN(opop->subitem_reference_name);
			}
			if (opop->subitem_local_name) {
				MEM_freeN(opop->subitem_local_name);
			}
		}
		BLI_freelistN(&op->operations);
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

/** Compares local and reference data-blocks and create new override operations as needed,
 * or reset to reference values if overriding is not allowed.
 * \return true is new overriding op was created, or some local data was reset. */
bool BKE_override_operations_create(ID *local)
{
	BLI_assert(local->override != NULL);
	if (local->flag & LIB_AUTOOVERRIDE) {
		printf("Should generate static override rules for %s\n", local->name);
	}
	return false;
}

/** Update given override from its reference (re-applying overriden properties). */
void BKE_override_update(ID *local)
{
	if (local->override == NULL) {
		return;
	}

	/* Recursively do 'ancestors' overrides first, if any. */
	if (local->override->reference->override && (local->override->reference->tag & LIB_TAG_OVERRIDE_OK) == 0) {
		BKE_override_update(local->override->reference);
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
	id_copy(G.main, local->override->reference, &tmp_id, false);  /* XXX ...and worse of all, this won't work with scene! */

	if (tmp_id == NULL) {
		return;
	}

	PointerRNA rnaptr_local, rnaptr_final;
	RNA_id_pointer_create(local, &rnaptr_local);
	RNA_id_pointer_create(tmp_id, &rnaptr_final);

	RNA_struct_override_apply(&rnaptr_final, &rnaptr_local, local->override);

	/* This also transfers all pointers (memory) owned by local to tmp_id, and vice-versa. So when we'll free tmp_id,
	 * we'll actually free old, outdated data from local. */
	BKE_id_swap(local, tmp_id);

	/* Again, horribly innefficient in our case, we need something off-Main (aka moar generic nolib copy/free stuff)! */
	BKE_libblock_free_ex(G.main, tmp_id, true, false);

	local->tag |= LIB_TAG_OVERRIDE_OK;
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
				BKE_override_update(id);
			}
		}
	}
}
