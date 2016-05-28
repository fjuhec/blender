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

#include "BKE_context.h"
#include "BKE_layer.h" /* own include */

#include "BLI_listbase.h"
#include "BLI_string.h"

#include "DNA_defs.h"
#include "DNA_userdef_types.h"

#include "MEM_guardedalloc.h"


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

void BKE_layertree_delete(LayerTree *ltree)
{
	for (LayerTreeItem *litem = ltree->items.first, *next_litem; litem; litem = next_litem) {
		next_litem = litem->next;
		BKE_layeritem_remove(litem, true);
	}
	BLI_assert(BLI_listbase_is_empty(&ltree->items));

	MEM_freeN(ltree);
}

/**
 * Iterate over \a itemlist and all of its children, wrapped by #BKE_layertree_iterate.
 * \note Recursive
 */
static bool layertree_iterate_list(const ListBase *itemlist, LayerTreeIterFunc foreach, void *customdata)
{
	for (LayerTreeItem *litem = itemlist->first, *litem_next; litem; litem = litem_next) {
		litem_next = litem->next; /* in case list order is changed in callback */
		if (foreach(litem, customdata) == false || /* execute callback for current item */
		    layertree_iterate_list(&litem->childs, foreach, customdata) == false) /* iterate over childs */
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
bool BKE_layertree_iterate(const LayerTree *ltree, LayerTreeIterFunc foreach, void *customdata)
{
	return layertree_iterate_list(&ltree->items, foreach, customdata);
}

/** \} */ /* Layer Tree */


/* -------------------------------------------------------------------- */
/** \name Layer Tree Item
 *
 * An item of the layer tree (layer, layer group, compositing layer, etc).
 * Although the technical precise term is "layer tree item", we usually just call it "layer item".
 *
 * \{ */

#define LAYERITEM_DEFAULT_HEIGHT U.widget_unit

/**
 * Allocate a new layer item of \a type and add it to the layer tree \a tree. Sorting happens later.
 *
 * \param parent: The parent layer group of the new item. NULL for ungrouped items
 * \return The newly created layer item.
 */
LayerTreeItem *BKE_layeritem_add(
        LayerTree *tree, LayerTreeItem *parent,
        const eLayerTreeItem_Type type, const char *name,
        const LayerItemPollFunc poll, LayerItemDrawFunc draw, LayerItemDrawSettingsFunc draw_settings)
{
	LayerTreeItem *litem = MEM_callocN(sizeof(LayerTreeItem), __func__);

	litem->type = type;
	litem->height = LAYERITEM_DEFAULT_HEIGHT;
	litem->tree = tree;
	BLI_strncpy(litem->name, name, sizeof(litem->name));

	/* callbacks */
	litem->poll = poll;
	litem->draw = draw;
	litem->draw_settings = draw_settings;

	if (parent) {
		BLI_assert(ELEM(parent->type, LAYER_ITEMTYPE_GROUP));
		BLI_assert(parent->tree == tree);

		litem->parent = parent;
		/* add to child list of parent, not to item list of tree */
		BLI_addtail(&parent->childs, litem);
	}
	else {
		BLI_addhead(&tree->items, litem);
	}

	return litem;
}

/**
 * Free and unlink \a litem from the list it's stored in.
 *
 * \param remove_children: Free and unlink all children (and their children, etc) of \a litem as well.
 * \note Recursive
 */
void BKE_layeritem_remove(LayerTreeItem *litem, const bool remove_children)
{
	BLI_remlink(litem->parent ? &litem->parent->childs : &litem->tree->items, litem);
	if (litem->drawdata) {
		MEM_freeN(litem->drawdata);
	}

	if (remove_children) {
		for (LayerTreeItem *child = litem->childs.first, *child_next; child; child = child_next) {
			child_next = child->next;
			BKE_layeritem_remove(child, true);
		}
	}

	MEM_freeN(litem);
}

/**
 * Assign \a item to \a group.
 */
void BKE_layeritem_group_assign(LayerTreeItem *group, LayerTreeItem *item)
{
	ListBase *oldlist = item->parent ? &item->parent->childs : &item->tree->items;

	BLI_assert(group->type == LAYER_ITEMTYPE_GROUP);
	BLI_assert(BLI_findindex(oldlist, item) != -1);

	item->parent = group;
	BLI_remlink(oldlist, item);
	BLI_addtail(&group->childs, item);
}

/** \} */ /* Layer Tree Item */
