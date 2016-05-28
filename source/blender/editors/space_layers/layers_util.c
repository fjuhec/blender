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

#include "BLI_listbase.h"

#include "BKE_layer.h"

#include "DNA_screen_types.h"
#include "DNA_space_types.h"

#include "MEM_guardedalloc.h"

#include "layers_intern.h"


/**
 * Allocate and register a LayerTile for \a litem.
 */
LayerTile *layers_tile_add(LayerTreeItem *litem)
{
	LayerTile *tile = MEM_callocN(sizeof(LayerTile), __func__);
	litem->drawdata = tile;
	return tile;
}

typedef struct LayerIsectData {
	/* input values */
	View2D *v2d;
	int co_x, co_y;

	/* helper values for callback */
	int ofs_y;

	/* return values */
	LayerTile *r_result;
	int r_idx; /* index of r_result */
} LayerIsectData;

static bool layers_tile_find_at_coordinate_cb(LayerTreeItem *litem, void *customdata)
{
	LayerIsectData *idata = customdata;

	idata->ofs_y += litem->height;
	if (idata->co_y >= -idata->v2d->cur.ymin - idata->ofs_y) {
		idata->r_result = litem->drawdata;
		/* found tile, stop iterating */
		return false;
	}
	idata->r_idx++;

	return true;
}

/**
 * Find the tile at coordinate \a co (regionspace).
 * \param r_tile_idx: Returns the index of the result, -1 if no tile was found.
 */
LayerTile *layers_tile_find_at_coordinate(
        const SpaceLayers *slayer, ARegion *ar, const int co[2],
        int *r_tile_idx)
{
	LayerIsectData idata = {&ar->v2d, co[0], co[1]};
	BKE_layertree_iterate(slayer->act_tree, layers_tile_find_at_coordinate_cb, &idata);

	/* return values */
	if (r_tile_idx) {
		(*r_tile_idx) = (idata.r_result != NULL) ? idata.r_idx : -1;
	}
	return idata.r_result;
}

static bool layers_has_selected_cb(LayerTreeItem *litem, void *UNUSED(customdata))
{
	LayerTile *tile = litem->drawdata;
	if (tile->flag & LAYERTILE_SELECTED) {
		/* false tells iterator to stop */
		return false;
	}
	return true;
}

bool layers_any_selected(const LayerTree *ltree)
{
	/* returns false if iterator was stopped because layers_has_selected_cb found a selected tile */
	return BKE_layertree_iterate(ltree, layers_has_selected_cb, NULL) == false;
}
