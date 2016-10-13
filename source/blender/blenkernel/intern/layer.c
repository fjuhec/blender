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
 * The Original Code is Copyright (C) 2016 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/layer.c
 *  \ingroup bke
 *
 * \brief Functions for a generic layer managment system.
 *
 * TODO sorting, renaming, drawing, filtering
 */

#include <stdlib.h>
#include <string.h>

#include "BKE_context.h"
#include "BKE_idprop.h"
#include "BKE_layer.h" /* own include */
#include "BKE_object.h"

#include "BLI_alloca.h"
#include "BLI_listbase.h"
#include "BLI_ghash.h"
#include "BLI_string.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

static void layeritem_free(LayerTreeItem *litem);


/* -------------------------------------------------------------------- */
/** \name Layer Tree
 *
 * A layer tree is the container for the tree/list of layers and layer groups that is displayed in the GUI later.
 *
 * \{ */

LayerTree *BKE_layertree_new(const eLayerTree_Type type)
{
	LayerTree *ltree = MEM_callocN(sizeof(LayerTree), __func__);
	ltree->type = type;
	return ltree;
}

LayerTree *BKE_layertree_copy(const LayerTree *original_tree)
{
	LayerTree *copied_tree = MEM_dupallocN(original_tree);

	/* copy layer items */
	LayerTreeItem *copied_item;
	copied_tree->items_all = MEM_dupallocN(original_tree->items_all);
	BKE_LAYERTREE_ITER_START(original_tree, 0, i, original_item)
	{
		copied_item = copied_tree->items_all[i] = MEM_dupallocN(original_item);
		copied_item->tree = copied_tree;
		copied_item->prop = IDP_CopyProperty(original_item->prop);
		copied_item->ptr = MEM_callocN(sizeof(PointerRNA), "LayerTreeItem PointerRNA duplicate");
		RNA_pointer_create(NULL, copied_item->type->srna, copied_item->prop, copied_item->ptr);

		if (original_item->parent) {
			/* we assume here that parent came before the child */
			copied_item->parent = copied_tree->items_all[original_item->parent->index];
			BLI_addhead(&copied_item->parent->childs, copied_item);
		}
		else {
			BLI_addhead(&copied_tree->items, copied_item);
		}

		if (copied_item->type->copy) {
			copied_item->type->copy(copied_item, original_item);
		}
	}
	BKE_LAYERTREE_ITER_END;

	copied_tree->active_layer = copied_tree->items_all[original_tree->active_layer->index];
	/* should use new address by now */
	BLI_assert(copied_tree->active_layer != original_tree->active_layer);

	return copied_tree;
}

void BKE_layertree_delete(LayerTree *ltree)
{
	BKE_LAYERTREE_ITER_START(ltree, 0, i, litem)
	{
		/* layeritem_free does all we need in this case. No un-registering needed */
		layeritem_free(litem);
	}
	BKE_LAYERTREE_ITER_END;

	if (ltree->items_all) {
		MEM_freeN(ltree->items_all);
	}
	MEM_freeN(ltree);
}

/**
 * Iterate over \a itemlist and all of its children, wrapped by #BKE_layertree_iterate.
 * \note Recursive
 */
static bool layertree_iterate_list(
        const ListBase *itemlist, LayerTreeIterFunc foreach, void *customdata,
        const bool inverse)
{
	for (LayerTreeItem *litem = (inverse ? itemlist->last : itemlist->first), *litem_next;
	     litem;
	     litem = litem_next)
	{
		litem_next = inverse ? litem->prev : litem->next; /* in case list order is changed in callback */
		if (foreach(litem, customdata) == false || /* execute callback for current item */
		    layertree_iterate_list(&litem->childs, foreach, customdata, inverse) == false) /* iterate over childs */
		{
			return false;
		}
	}
	return true;
}

/**
 * Iterate over all items (including children) in the layer tree, executing \a foreach callback for each element.
 * (Pre-order traversal)
 *
 * \param foreach: Callback that can return false to stop the iteration.
 * \return if the iteration has been stopped because of a callback returning false.
 */
bool BKE_layertree_iterate(const LayerTree *ltree, LayerTreeIterFunc foreach, void *customdata, const bool inverse)
{
	return layertree_iterate_list(&ltree->items, foreach, customdata, inverse);
}

int BKE_layertree_get_totitems(const LayerTree *ltree)
{
	return ltree->tot_items;
}

/** \} */ /* Layer Tree */


/* -------------------------------------------------------------------- */
/** \name Layer Type
 *
 * Layer types store information that is shared between all layers of
 * the given type. They work just like operator and operator types.
 *
 * \{ */

/**
 * String hash table for quick LayerType.idname lookups.
 */
static GHash *layertypes_hash = NULL;
/**
 * Array of all registered layer types. The index of a layer type matches items
 * in eLayerTreeItem_Type. Length should always match #LAYER_ITEMTYPE_TOT.
*/
static LayerType *layertypes_vec[LAYER_ITEMTYPE_TOT] = {NULL};


void BKE_layertype_append(void (*ltfunc)(LayerType *))
{
	LayerType *lt = MEM_callocN(sizeof(LayerType), __func__);
	lt->srna = RNA_def_struct_ptr(&BLENDER_RNA, "", &RNA_LayerProperties);
	ltfunc(lt);

	/* insert into array */
	BLI_assert(lt->type >= 0 && lt->type < LAYER_ITEMTYPE_TOT);
	layertypes_vec[lt->type] = lt;
	/* insert into hash */
	if (UNLIKELY(!layertypes_hash)) {
		/* reserve size is set based on blender default setup */
		layertypes_hash = BLI_ghash_str_new_ex("wm_operatortype_init gh", 2);
	}
	BLI_ghash_insert(layertypes_hash, (void *)lt->idname, lt);
}

void BKE_layertypes_free(void)
{
	BLI_ghash_free(layertypes_hash, NULL, MEM_freeN);
}

LayerType *BKE_layertype_find(const char *idname)
{
	return BLI_ghash_lookup(layertypes_hash, idname);
}

/** \} */ /* Layer Type */

/* -------------------------------------------------------------------- */
/** \name Layer Tree Item
 *
 * An item of the layer tree (layer, layer group, compositing layer, etc).
 * Although the technical precise term is "layer tree item", we usually just call it "layer item".
 *
 * \{ */

/**
 * Register an already allocated \a litem.
 *
 * \note Reallocates memory for item storage array, if you want to add many items at once,
 * better do differently (e.g. _ex version that allows reserving memory)
 */
void BKE_layeritem_register(
        LayerTree *tree, LayerTreeItem *litem, LayerTreeItem *parent,
        const eLayerTreeItem_Type type, const char *name)
{
	litem->type = layertypes_vec[type];
	BLI_strncpy(litem->idname, litem->type->idname, MAX_NAME);

	/* initialize properties */
	IDPropertyTemplate val = {0};
	litem->ptr = MEM_callocN(sizeof(PointerRNA), "LayerTreeItem PointerRNA");
	litem->prop = IDP_New(IDP_GROUP, &val, "LayerTreeItem Properties");
	RNA_pointer_create(NULL, litem->type->srna, litem->prop, litem->ptr);

	litem->index = tree->tot_items;
	litem->tree = tree;
	BLI_strncpy(litem->name, name, sizeof(litem->name));

	/* add to item array */
	tree->items_all = MEM_reallocN(tree->items_all, sizeof(*tree->items_all) * ++tree->tot_items);
	tree->items_all[tree->tot_items - 1] = litem;

	if (parent) {
		BLI_assert(ELEM(parent->type->type, LAYER_ITEMTYPE_GROUP));
		BLI_assert(parent->tree == tree);

		litem->parent = parent;
		/* add to child list of parent, not to item list of tree */
		BLI_addtail(&parent->childs, litem);
	}
	else {
		BLI_addhead(&tree->items, litem);
	}
}

/**
 * Allocate a new layer item of \a type and add it to the layer tree \a tree. Sorting happens later.
 *
 * \param parent: The parent layer group of the new item. NULL for ungrouped items
 * \return The newly created layer item.
 */
LayerTreeItem *BKE_layeritem_add(
        LayerTree *tree, LayerTreeItem *parent,
        const eLayerTreeItem_Type type, const char *name)
{
	LayerTreeItem *litem = MEM_callocN(sizeof(LayerTreeItem), __func__);
	BKE_layeritem_register(tree, litem, parent, type, name);
	return litem;
}

static void layeritem_free(LayerTreeItem *litem)
{
	if (litem->type->free) {
		litem->type->free(litem);
	}

	if (litem->ptr)
		MEM_freeN(litem->ptr);
	if (litem->prop) {
		IDP_FreeProperty(litem->prop);
		MEM_freeN(litem->prop);
	}

	MEM_freeN(litem);
}


/**
 * Recursive function to remove \a litem. Used to avoid multiple realloc's
 * for LayerTree.items_all, instead caller can simply realloc once (afterwards!).
 *
 * \param remove_children: Free and unlink all children (and their children, etc) of \a litem as well.
 */
static void layeritem_remove_ex(LayerTreeItem *litem, const bool remove_children)
{
	BLI_remlink(litem->parent ? &litem->parent->childs : &litem->tree->items, litem);

	for (int i = litem->index + 1; i < litem->tree->tot_items; i++) {
		litem->tree->items_all[i - 1] = litem->tree->items_all[i];
		litem->tree->items_all[i - 1]->index--;
	}
	litem->tree->tot_items--;

	if (remove_children) {
		for (LayerTreeItem *child = litem->childs.first, *child_next; child; child = child_next) {
			child_next = child->next;
			layeritem_remove_ex(child, true);
		}
		BLI_assert(BLI_listbase_is_empty(&litem->childs));
	}
	layeritem_free(litem);
}

/**
 * Free and unlink \a litem from the list and the array it's stored in.
 *
 * \param remove_children: Free and unlink all children (and their children, etc) of \a litem as well.
 * \note Calls recursive #layeritem_remove_ex.
 */
void BKE_layeritem_remove(LayerTreeItem *litem, const bool remove_children)
{
	LayerTree *ltree = litem->tree; /* store before deleting litem */
	layeritem_remove_ex(litem, remove_children);
	ltree->items_all = MEM_reallocN(ltree->items_all, sizeof(*ltree->items_all) * ltree->tot_items);
	ltree->active_layer = ltree->items_all[0];
}

/* XXX newidx isn't always the index the items are inserted at. */
static void layeritem_move_array(LayerTreeItem *litem, const int newidx, const int num_items)
{
	LayerTree *ltree = litem->tree;
	const int oldidx = litem->index;
	const bool is_higher = oldidx < newidx;
	const int insertidx = is_higher ? newidx - num_items + 1 : newidx;

	BLI_assert(num_items > 0 && ltree->tot_items > (insertidx + num_items - 1));
	/* Already where we want to move it to. */
	if (oldidx == newidx)
		return;

	/* Save items to be moved */
	LayerTreeItem **movechunk = &litem;
	if (num_items > 1) {
		movechunk = BLI_array_alloca(movechunk, num_items);
//		memcpy(movechunk, ltree->items_all[oldidx], sizeof(*ltree->items_all) * num_items); XXX doesn't work
		for (int i = 0; i < num_items; i++) {
			movechunk[i] = ltree->items_all[oldidx + i];
		}
	}
	BLI_assert(movechunk[0] == litem);

	/* rearrange items to fill in gaps of items to be moved */
	for (int i = is_higher ? oldidx + num_items : oldidx - 1;
	     i < ltree->tot_items && i >= 0;
	     is_higher ? i++ : i--)
	{
		const int iter_new_idx = i + (is_higher ? -num_items : num_items);
		ltree->items_all[iter_new_idx] = ltree->items_all[i];
		ltree->items_all[iter_new_idx]->index = iter_new_idx;
		if (i == newidx) {
			break;
		}
	}
	/* move items to new position starting at newidx */
	for (int i = 0; i < num_items; i++) {
		ltree->items_all[insertidx + i] = movechunk[i];
		ltree->items_all[insertidx + i]->index = insertidx + i;
	}
	/* TODO can check using gtest */
	BLI_assert(ltree->items_all[insertidx] == litem && litem->index == insertidx);
}

/**
 * Helper to count all children (and grand-children etc.) of a layer item.
 * \note Recursive.
 */
static unsigned int layeritem_childs_count(ListBase *childs)
{
	int i = 0;
	for (LayerTreeItem *child = childs->first; child; child = child->next, i++) {
		if (!BLI_listbase_is_empty(&child->childs)) {
			i += layeritem_childs_count(&child->childs);
		}
	}
	return i;
}

/**
 * Move \a litem that's already in the layer tree to slot \a newidx.
 */
void BKE_layeritem_move(LayerTreeItem *litem, const int newidx, const bool with_childs)
{
	const int tot_childs = with_childs ? layeritem_childs_count(&litem->childs) : 0;

	/* Already where we want to move it to. */
	if (litem->index == newidx)
		return;

	/* move in listbase */
	BLI_remlink(litem->parent ? &litem->parent->childs : &litem->tree->items, litem);
	if (newidx == litem->tree->tot_items - 1) {
		LayerTreeItem *last = litem->tree->items_all[litem->tree->tot_items - 1];
		BLI_addtail(last->parent ? &last->parent->childs : &litem->tree->items, litem);
	}
	else {
		LayerTreeItem *moved = litem->tree->items_all[newidx + 1];
		BLI_insertlinkbefore(moved->parent ? &moved->parent->childs : &litem->tree->items, moved, litem);
	}

	/* move in array */
	layeritem_move_array(litem, newidx, tot_childs + 1);
}

/**
 * Assign \a item to \a group.
 */
void BKE_layeritem_group_assign(LayerTreeItem *group, LayerTreeItem *item)
{
	ListBase *oldlist = item->parent ? &item->parent->childs : &item->tree->items;

	BLI_assert(group->type->type == LAYER_ITEMTYPE_GROUP);
	BLI_assert(BLI_findindex(oldlist, item) != -1);

	item->parent = group;
	/* insert into list */
	BLI_remlink(oldlist, item);
	BLI_addtail(&group->childs, item);
	/* move in array */
	/* XXX we could/should limit iterations to one in case multiple elmenents are assigned to a group */
	layeritem_move_array(item, (item->prev ? item->prev->index : item->parent->index) + 1, 1);
}

/**
 * Iterate over all children (and their children, etc) of \a litem, executing \a foreach callback for each element.
 * (Pre-order traversal)
 *
 * \param foreach: Callback that can return false to stop the iteration.
 * \return if the iteration has been stopped because of a callback returning false.
 */
bool BKE_layeritem_iterate_childs(
        LayerTreeItem *litem, LayerTreeIterFunc foreach, void *customdata,
        const bool inverse)
{
	return layertree_iterate_list(&litem->childs, foreach, customdata, inverse);
}

/**
 * Check if \a litem and all of its parents are visible.
 */
bool BKE_layeritem_is_visible(LayerTreeItem *litem)
{
	if (litem->is_hidden)
		return false;

	for (LayerTreeItem *parent = litem->parent; parent; parent = parent->parent) {
		if (parent->is_hidden)
			return false;
	}

	return true;
}

/** \} */ /* Layer Tree Item */
