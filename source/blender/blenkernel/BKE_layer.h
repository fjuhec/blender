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

/** \file BKE_layer.h
 *  \ingroup bke
 */

#ifndef __BKE_LAYER_H__
#define __BKE_LAYER_H__

struct bContext;
struct uiLayout;

typedef struct LayerTree LayerTree;
typedef struct LayerTreeItem LayerTreeItem;


/* -------------------------------------------------------------------- */
/* Layer Tree */

/**
 * LayerTree.type
 * Defines the type used for the layer tree.
 */
typedef enum eLayerTree_Type {
	LAYER_TREETYPE_OBJECT,
//	LAYER_TREETYPE_GPENCIL,
//	LAYER_TREETYPE_ARMATURE,
//	...
} eLayerTree_Type;

#define MAX_LAYER_FILTER_STR 64

typedef struct LayerTree {
	eLayerTree_Type type;

	ListBase items; /* LayerTreeItem - TODO check if worth using array instead */

	/* filtering */
	short filterflag;
	char filter_str[MAX_LAYER_FILTER_STR];
} LayerTree;


struct LayerTree *BKE_layertree_new(const eLayerTree_Type type);
void BKE_layertree_delete(struct LayerTree *ltree);

/* -------------------------------------------------------------------- */
/* Layer Tree Item */

typedef short (*LayerItemPollFunc)(const struct bContext *, struct LayerTreeItem *);
typedef void  (*LayerItemDrawFunc)(struct LayerTreeItem *, struct uiLayout *layout);
typedef void  (*LayerItemDrawSettingsFunc)(struct LayerTreeItem *, struct uiLayout *layout);

typedef enum eLayerTreeItem_Type {
	LAYER_ITEMTYPE_LAYER,
	LAYER_ITEMTYPE_GROUP, /* layer group */
	LAYER_ITEMTYPE_COMP,  /* compositing layer (wireframes, SSAO, blending type, etc) */
} eLayerTreeItem_Type;

/**
 * \brief An item of the layer tree.
 * Used as a base struct for the individual layer tree item types (layer, layer group, compositing layer, etc).
 */
typedef struct LayerTreeItem {
	struct LayerTreeItem *next, *prev;

	eLayerTreeItem_Type type;
	char name[64]; /* MAX_NAME */
	int height; /* the height of this item */

	LayerTree *tree; /* pointer back to layer tree - TODO check if needed */
	struct LayerTreeItem *parent; /* the group this item belongs to */

	/* item is grayed out if this check fails */
	LayerItemPollFunc poll;
	/* drawing of the item in the list */
	LayerItemDrawFunc draw;
	/* drawing of the expanded layer settings (gear wheel icon) */
	LayerItemDrawSettingsFunc draw_settings;
} LayerTreeItem;

struct LayerTreeItem *BKE_layeritem_add(
        struct LayerTree *tree, struct LayerTreeItem *parent,
        const eLayerTreeItem_Type type, const char *name,
        const LayerItemPollFunc poll, LayerItemDrawFunc draw, LayerItemDrawSettingsFunc draw_settings);
void BKE_layeritem_remove(struct LayerTree *tree, struct LayerTreeItem *litem);

#endif  /* __BKE_LAYER_H__ */

