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
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/space_layers/layers_util.c
 *  \ingroup splayers
 *
 * Utility functions for layer manager editor.
 */

#include "BLI_utildefines.h"
#include "BLI_ghash.h"
#include "BLI_listbase.h"
#include "BLI_rect.h"

#include "BKE_layer.h"

#include "DNA_screen_types.h"
#include "DNA_space_types.h"

#include "MEM_guardedalloc.h"

#include "layers_intern.h"

#include "UI_interface.h"


/**
 * Refresh data after undo/file read. Should be called before drawing if SL_LAYERDATA_REFRESH flag is set.
 */
void layers_data_refresh(const Scene *scene, SpaceLayers *slayer)
{
	slayer->act_tree = scene->object_layers;

	if (slayer->tiles) {
		layers_tilehash_delete(slayer);
	}
	slayer->tiles = BLI_ghash_ptr_new_ex("Layer tiles hash", BKE_layertree_get_totitems(slayer->act_tree));
	BKE_LAYERTREE_ITER_START(slayer->act_tree, 0, i, litem)
	{
		layers_tile_add(slayer, litem);
	}
	BKE_LAYERTREE_ITER_END;

	slayer->flag &= ~SL_LAYERDATA_REFRESH;
}

void layers_tilehash_delete(SpaceLayers *slayer)
{
	BLI_ghash_free(slayer->tiles, NULL, MEM_freeN);
	slayer->tiles = NULL;
}

/**
 * Allocate and register a LayerTile for \a litem.
 */
LayerTile *layers_tile_add(const SpaceLayers *slayer, LayerTreeItem *litem)
{
	LayerTile *tile = MEM_callocN(sizeof(LayerTile), __func__);

	tile->litem = litem;
	BLI_ghash_insert(slayer->tiles, litem, tile);

	return tile;
}

static bool layer_tile_remove_children_cb(LayerTreeItem *litem, void *customdata)
{
	SpaceLayers *slayer = customdata;
	BLI_ghash_remove(slayer->tiles, litem, NULL, MEM_freeN);
	return true;
}

/**
 * \note Call before removing corresponding LayerTreeItem!
 */
void layers_tile_remove(const SpaceLayers *slayer, LayerTile *tile, const bool remove_children)
{
	/* remove tiles of children first */
	if (remove_children) {
		BKE_layeritem_iterate_childs(tile->litem, layer_tile_remove_children_cb, (void *)slayer, true);
	}
	/* remove tile */
	BLI_ghash_remove(slayer->tiles, tile->litem, NULL, MEM_freeN);
}

/**
 * Find the tile at coordinate \a co (regionspace).
 * \note Does *not* account for LayerTile.ofs (could optionally do, layer dragging assumes it doesn't).
 */
LayerTile *layers_tile_find_at_coordinate(SpaceLayers *slayer, const int co[2])
{
	int ofs_y = 0;

	BKE_LAYERTREE_ITER_START(slayer->act_tree, 0, i, litem)
	{
		LayerTile *tile = BLI_ghash_lookup(slayer->tiles, litem);
		if (!layers_tile_is_visible(slayer, tile))
			continue;

		if (BLI_rcti_isect_y(&tile->rect, co[1])) {
			return tile;
		}
		ofs_y += tile->tot_height;
	}
	BKE_LAYERTREE_ITER_END;

	return NULL;
}

bool layers_tile_is_visible(SpaceLayers *slayer, LayerTile *tile)
{
	/* avoid ghash loockup */
	if (!tile->litem->parent)
		return true;

	for (LayerTreeItem *parent = tile->litem->parent; parent; parent = parent->parent) {
		LayerTile *parent_tile = BLI_ghash_lookup(slayer->tiles, parent);
		if (parent_tile->flag & LAYERTILE_CLOSED) {
			return false;
		}
	}
	return true;
}

bool layers_any_selected(SpaceLayers *slayer)
{
	BKE_LAYERTREE_ITER_START(slayer->act_tree, 0, i, litem)
	{
		LayerTile *tile = BLI_ghash_lookup(slayer->tiles, litem);
		if (tile->flag & LAYERTILE_SELECTED) {
			return true;
		}
	}
	BKE_LAYERTREE_ITER_END;

	return false;
}
