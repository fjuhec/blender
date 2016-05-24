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
 * Allocate and register a LayerTile entry for \a litem in layer_item list of \a slayer.
 */
LayerTile *layers_tile_add(SpaceLayers *slayer, LayerTreeItem *litem)
{
	LayerTile *tile = MEM_callocN(sizeof(LayerTile), __func__);

	tile->litem = litem;
	BLI_addhead(&slayer->layer_tiles, tile);

	return tile;
}

/**
 * Find the tile at coordinate \a co (regionspace).
 */
LayerTile *layers_tile_find_at_coordinate(const SpaceLayers *slayer, const ARegion *ar, const int co[2])
{
	int ofsx = 0;

	for (LayerTile *tile = slayer->layer_tiles.first; tile; tile = tile->next) {
		ofsx += tile->litem->height;
		if (co[1] >= -ar->v2d.cur.ymin - ofsx) {
			return tile;
		}
	}
	return NULL;
}
