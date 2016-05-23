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
 * TODO sorting, renaming, drawing
 */

#include "BKE_context.h"
#include "BKE_layer.h" /* own include */

#include "BLI_listbase.h"

#include "DNA_defs.h"

#include "MEM_guardedalloc.h"


#define MAX_LAYER_FILTER_STR 64

typedef struct LayerTree {
	eLayerTree_Type type;

	ListBase items; /* LayerTreeItem - TODO check if worth using array instead */

	/* filtering */
	short filterflag;
	char filter_str[MAX_LAYER_FILTER_STR];
} LayerTree;

/**
 * \brief An item of the layer tree.
 * Used as a base struct for the individual layer tree item types (layer, layer group, compositing layer, etc).
 */
typedef struct LayerTreeItem {
	struct LayerTreeItem *next, *prev;

	eLayerTreeItem_Type type;
	char name[MAX_NAME]; /* name displayed in GUI */

	LayerTree *tree; /* pointer back to layer tree - TODO check if needed */
	struct LayerTreeItem *parent; /* the group this item belongs to */

	/* item is grayed out if this check fails */
	LayerItemPollFunc poll;
	/* drawing of the item in the list */
	LayerItemDrawFunc draw;
	/* drawing of the expanded layer settings (gear wheel icon) */
	LayerItemDrawSettingsFunc draw_settings;
} LayerTreeItem;


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
		BKE_layeritem_remove(ltree, litem);
	}
	BLI_assert(BLI_listbase_is_empty(&ltree->items));

	MEM_freeN(ltree);
}

/** \} */ /* Layer Tree */


/* -------------------------------------------------------------------- */
/** \name Layer Tree Item
 *
 * An item of the layer tree (layer, layer group, compositing layer, etc).
 * Although the technical precise term is "layer tree item", we usually just call it "layer item".
 *
 * \{ */

/**
 * Allocate a new layer item of \a type and add it to the layer tree \a tree. Sorting happens later.
 *
 * \param parent: The parent layer group of the new item. NULL for ungrouped items
 * \return The newly created layer item.
 */
LayerTreeItem *BKE_layeritem_add(
        LayerTree *tree, LayerTreeItem *parent, const eLayerTreeItem_Type type,
        const LayerItemPollFunc poll, LayerItemDrawFunc draw, LayerItemDrawSettingsFunc draw_settings)
{
	LayerTreeItem *litem = MEM_callocN(sizeof(LayerTreeItem), __func__);

	BLI_assert(!parent || ELEM(parent->type, LAYER_ITEMTYPE_GROUP));
	BLI_assert(!parent || parent->tree == tree);

	litem->type = type;
	litem->parent = parent;
	litem->tree = tree;

	/* callbacks */
	litem->poll = poll;
	litem->draw = draw;
	litem->draw_settings = draw_settings;

	BLI_addhead(&tree->items, litem);

	return litem;
}

/**
 * Free and unlink \a litem from \a tree.
 */
void BKE_layeritem_remove(LayerTree *tree, LayerTreeItem *litem)
{
	BLI_remlink(&tree->items, litem);
	MEM_freeN(litem);
}

/** \} */ /* Layer Tree Item */
