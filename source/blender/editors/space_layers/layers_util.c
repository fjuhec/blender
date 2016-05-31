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

#include "BKE_layer.h"

#include "DNA_screen_types.h"
#include "DNA_space_types.h"

#include "MEM_guardedalloc.h"

#include "layers_intern.h"

#include "UI_interface.h"

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
		BKE_layeritem_iterate_childs(tile->litem, layer_tile_remove_children_cb, (void *)slayer);
	}
	/* remove tile */
	BLI_ghash_remove(slayer->tiles, tile->litem, NULL, MEM_freeN);
}

typedef struct {
	/* input values */
	SpaceLayers *slayer;
	View2D *v2d;
	int co_x, co_y;

	/* helper values for callback */
	int ofs_y;

	/* return values */
	LayerTile *r_result;
	int r_idx; /* index of r_result */
} LayerIsectCheckData;

static bool layers_tile_find_at_coordinate_cb(LayerTreeItem *litem, void *customdata)
{
	LayerIsectCheckData *idata = customdata;
	LayerTile *tile = BLI_ghash_lookup(idata->slayer->tiles, litem);

	if (idata->co_y >= -idata->v2d->cur.ymin - (idata->ofs_y + tile->tot_height)) {
		if ((idata->co_y >= -idata->v2d->cur.ymin - (idata->ofs_y + LAYERTILE_HEADER_HEIGHT))) {
			idata->r_result = tile;
		}
		/* found tile, stop iterating */
		return false;
	}
	idata->ofs_y += tile->tot_height;
	idata->r_idx++;

	return true;
}

/**
 * Find the tile at coordinate \a co (regionspace).
 * \param r_tile_idx: Returns the index of the result, -1 if no tile was found.
 */
LayerTile *layers_tile_find_at_coordinate(
        SpaceLayers *slayer, ARegion *ar, const int co[2],
        int *r_tile_idx)
{
	LayerIsectCheckData idata = {slayer, &ar->v2d, co[0], co[1]};
	BKE_layertree_iterate(slayer->act_tree, layers_tile_find_at_coordinate_cb, &idata);

	/* return values */
	if (r_tile_idx) {
		(*r_tile_idx) = (idata.r_result != NULL) ? idata.r_idx : -1;
	}
	return idata.r_result;
}

static bool layers_has_selected_cb(LayerTreeItem *litem, void *customdata)
{
	SpaceLayers *slayer = customdata;
	LayerTile *tile = BLI_ghash_lookup(slayer->tiles, litem);

	if (tile->flag & LAYERTILE_SELECTED) {
		/* false tells iterator to stop */
		return false;
	}
	return true;
}

bool layers_any_selected(SpaceLayers *slayer, const LayerTree *ltree)
{
	/* returns false if iterator was stopped because layers_has_selected_cb found a selected tile */
	return BKE_layertree_iterate(ltree, layers_has_selected_cb, slayer) == false;
}
