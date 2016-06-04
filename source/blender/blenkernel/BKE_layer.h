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

#include "DNA_space_types.h"


/* -------------------------------------------------------------------- */
/* Layer Tree */

typedef bool (*LayerTreeIterFunc)(LayerTreeItem *, void *);

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

LayerTree *BKE_layertree_new(const eLayerTree_Type type);
void BKE_layertree_delete(LayerTree *ltree);

bool BKE_layertree_iterate(const LayerTree *ltree, LayerTreeIterFunc foreach, void *customdata);
int  BKE_layertree_get_totitems(const LayerTree *ltree);

/* -------------------------------------------------------------------- */
/* Layer Tree Item */

typedef short (*LayerItemPollFunc)(const struct bContext *, struct LayerTreeItem *);
typedef void  (*LayerItemDrawFunc)(const struct bContext *, struct LayerTreeItem *, struct uiLayout *layout);
typedef void  (*LayerItemDrawSettingsFunc)(const struct bContext *, struct LayerTreeItem *, struct uiLayout *layout);

typedef enum eLayerTreeItem_Type {
	LAYER_ITEMTYPE_LAYER,
	LAYER_ITEMTYPE_GROUP, /* layer group */
	LAYER_ITEMTYPE_COMP,  /* compositing layer (wireframes, SSAO, blending type, etc) */
} eLayerTreeItem_Type;

LayerTreeItem *BKE_layeritem_add(
        LayerTree *tree, LayerTreeItem *parent,
        const eLayerTreeItem_Type type, const char *name,
        const LayerItemPollFunc poll, LayerItemDrawFunc draw, LayerItemDrawSettingsFunc draw_settings);
void BKE_layeritem_remove(LayerTreeItem *litem, const bool remove_children);

void BKE_layeritem_group_assign(LayerTreeItem *group, LayerTreeItem *item);

bool BKE_layeritem_iterate_childs(LayerTreeItem *litem, LayerTreeIterFunc foreach, void *customdata);

#endif  /* __BKE_LAYER_H__ */

