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

/** \file blender/editors/space_layers/layers_draw.c
 *  \ingroup splayers
 */

#include "BLI_listbase.h"
#include "BLI_rect.h"

#include "BKE_context.h"
#include "BKE_layer.h"

#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"

#include "UI_interface.h"
#include "UI_view2d.h"
#include "UI_resources.h"

#include "layers_intern.h" /* own include */

void layers_draw_tiles(const bContext *C, ARegion *ar)
{
	uiStyle *style = UI_style_get_dpi();
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	View2D *v2d = &ar->v2d;
	float size_y = 0.0f;

	uiBlock *block = UI_block_begin(C, ar, __func__, UI_EMBOSS);

	/* draw items */
	for (LayerTile *tile = slayer->layer_tiles.first; tile; tile = tile->next) {
		LayerTreeItem *litem = tile->litem;

		/* draw selection */
		if (tile->flag & LAYERTILE_SELECTED) {
			const float padx = 4.0f * UI_DPI_FAC;
			rctf rect = {padx, ar->winx - padx, -v2d->cur.ymin - size_y - litem->height};
			rect.ymax = rect.ymin + litem->height;

			UI_draw_roundbox_corner_set(UI_CNR_ALL);
			UI_ThemeColor(TH_HILITE);
			UI_draw_roundbox(rect.xmin, rect.ymin, rect.xmax, rect.ymax, 5.0f);
		}
		/* draw item itself */
		if (litem->draw) {
			uiLayout *layout = UI_block_layout(
			                       block, UI_LAYOUT_HORIZONTAL, UI_LAYOUT_HEADER,
			                       -v2d->cur.xmin, -v2d->cur.ymin - size_y, litem->height, 0, 0, style);
			litem->draw(litem, layout);
			UI_block_layout_resolve(block, NULL, NULL);
		}
		size_y += litem->height;
	}

	UI_block_end(C, block);
	UI_block_draw(C, block);

	/* update size of tot-rect (extents of data/viewable area) */
	UI_view2d_totRect_set(v2d, ar->winx - BLI_rcti_size_x(&v2d->vert), size_y);
}
