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

#include "BLI_utildefines.h"
#include "BLI_ghash.h"
#include "BLI_listbase.h"
#include "BLI_rect.h"

#include "BKE_context.h"
#include "BKE_layer.h"

#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"

#include "ED_screen.h"

#include "UI_interface.h"
#include "UI_view2d.h"
#include "UI_resources.h"

#include "WM_api.h"
#include "WM_types.h"

#include "layers_intern.h" /* own include */

/* Using icon size makes items align nicely with icons */
#define LAYERITEM_INDENT_SIZE UI_DPI_ICON_SIZE


static int layer_tile_indent_level_get(const LayerTreeItem *litem)
{
	LayerTreeItem *parent = litem->parent;
	int indent_level = 0;

	while (parent) {
		parent = parent->parent;
		indent_level++;
	}

	return indent_level;
}

typedef struct TileDrawInfo {
	const bContext *C;
	ARegion *ar;
	SpaceLayers *slayer;
	uiBlock *block;
	uiStyle *style;

	float size_y;
} TileDrawInfo;

static bool layer_tile_draw_cb(LayerTreeItem *litem, void *userdata)
{
	TileDrawInfo *drawinfo = userdata;
	View2D *v2d = &drawinfo->ar->v2d;
	LayerTile *tile = BLI_ghash_lookup(drawinfo->slayer->tiles, litem);
	const float padx = 4.0f * UI_DPI_FAC;

	const float ofs_x = layer_tile_indent_level_get(litem) * LAYERITEM_INDENT_SIZE;
	const float ofs_y = drawinfo->size_y;
	rctf rect = {padx + ofs_x, drawinfo->ar->winx - padx, -v2d->cur.ymin - ofs_y - litem->height};
	rect.ymax = rect.ymin + litem->height;


	/* draw selection */
	if (tile->flag & LAYERTILE_SELECTED) {
		UI_draw_roundbox_corner_set(UI_CNR_ALL);
		UI_ThemeColor(TH_HILITE);
		UI_draw_roundbox(rect.xmin, rect.ymin, rect.xmax, rect.ymax, 5.0f);
	}
	/* draw item itself */
	if (litem->draw) {
		uiBlock *block = drawinfo->block;
		uiLayout *layout = UI_block_layout(
		                       block, UI_LAYOUT_HORIZONTAL, UI_LAYOUT_HEADER,
		                       -v2d->cur.xmin + ofs_x, -v2d->cur.ymin - ofs_y, litem->height, 0, 0, drawinfo->style);
		if (tile->flag & LAYERTILE_RENAME) {
			uiBut *but = uiDefBut(
			        block, UI_BTYPE_TEXT, 1, "", rect.xmin, rect.ymin,
			        UI_UNIT_X * 7.0f, BLI_rctf_size_y(&rect),
			        litem->name, 1.0f, (float)sizeof(litem->name), 0, 0, "");
			UI_but_flag_enable(but, UI_BUT_NO_UTF8); /* allow non utf8 names */
			UI_but_flag_disable(but, UI_BUT_UNDO);

			/* returns false if button got removed */
			if (UI_but_active_only(drawinfo->C, drawinfo->ar, block, but) == false) {
				tile->flag &= ~LAYERTILE_RENAME;
				/* Yuk! Sending notifier during draw. Need to
				 * do that so item uses regular drawing again. */
				WM_event_add_notifier(drawinfo->C, NC_SPACE | ND_SPACE_LAYERS, NULL);
			}
		}
		else {
			litem->draw(litem, layout);
		}
		uiItemL(layout, "", 0); /* XXX without this editing last item causes crashes */
		UI_block_layout_resolve(block, NULL, NULL);
	}
	drawinfo->size_y += litem->height;

	return true;
}

void layers_tiles_draw(const bContext *C, ARegion *ar)
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);

	uiBlock *block = UI_block_begin(C, ar, __func__, UI_EMBOSS);
	TileDrawInfo drawinfo = {C, ar, slayer, block, UI_style_get_dpi()};

	BKE_layertree_iterate(slayer->act_tree, layer_tile_draw_cb, &drawinfo);

	UI_block_end(C, block);
	UI_block_draw(C, block);

	/* update size of tot-rect (extents of data/viewable area) */
	UI_view2d_totRect_set(&ar->v2d, ar->winx - BLI_rcti_size_x(&ar->v2d.vert), drawinfo.size_y);
}


/* -------------------------------------------------------------------- */
/* Layer draw callbacks */

void layer_group_draw(LayerTreeItem *litem, uiLayout *layout)
{
	uiItemL(layout, litem->name, ICON_FILE_FOLDER);
}
