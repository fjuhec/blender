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

struct LayerTree;
struct LayerTreeItem;


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

struct LayerTree *BKE_layertree_new(const eLayerTree_Type type);
void BKE_layertree_delete(struct LayerTree *ltree);

/* -------------------------------------------------------------------- */
/* Layer Tree Item */

typedef short (*LayerItemPollFunc)(const struct bContext *, struct LayerTreeItem *);
typedef void  (*LayerItemDrawFunc)(struct LayerTreeItem *);
typedef void  (*LayerItemDrawSettingsFunc)(struct LayerTreeItem *);

typedef enum eLayerTreeItem_Type {
	LAYER_ITEMTYPE_LAYER,
	LAYER_ITEMTYPE_GROUP, /* layer group */
	LAYER_ITEMTYPE_COMP,  /* compositing layer (wireframes, SSAO, blending type, etc) */
} eLayerTreeItem_Type;

struct LayerTreeItem *BKE_layeritem_add(
        struct LayerTree *tree, struct LayerTreeItem *parent, const eLayerTreeItem_Type type,
        const LayerItemPollFunc poll, LayerItemDrawFunc draw, LayerItemDrawSettingsFunc draw_settings);
void BKE_layeritem_remove(struct LayerTree *tree, struct LayerTreeItem *litem);

#endif  /* __BKE_LAYER_H__ */

