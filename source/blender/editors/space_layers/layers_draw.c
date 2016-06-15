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

#include "BIF_gl.h"
#include "BIF_glutil.h"

#include "BLI_utildefines.h"
#include "BLI_ghash.h"
#include "BLI_listbase.h"
#include "BLI_rect.h"

#include "BLT_translation.h"

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

/**
 * \return the height of the drawn tile.
 */
static float layer_tile_draw(
        LayerTile *tile,
        const bContext *C, ARegion *ar, uiBlock *block, uiStyle *style,
        float row_ofs_y, int idx)
{
	LayerTreeItem *litem = tile->litem;
	const bool expanded = litem->draw_settings && (tile->flag & LAYERTILE_EXPANDED);

	const float pad_x = 4.0f * UI_DPI_FAC;
	const float header_y = LAYERTILE_HEADER_HEIGHT;

	const float ofs_x = layer_tile_indent_level_get(litem) * LAYERITEM_INDENT_SIZE + tile->ofs[0];
	const float ofs_y = row_ofs_y + tile->ofs[1];
	const rctf rect = {-ar->v2d.cur.xmin + ofs_x, ar->winx,
	                   -ar->v2d.cur.ymin - ofs_y - header_y, -ar->v2d.cur.ymin - ofs_y};
	int tile_size_y = 0;

	/* draw item itself */
	if (tile->flag & LAYERTILE_RENAME) {
		uiBut *but = uiDefBut(
		        block, UI_BTYPE_TEXT, 1, "", rect.xmin, rect.ymin,
		        UI_UNIT_X * 7.0f, BLI_rctf_size_y(&rect),
		        litem->name, 1.0f, (float)sizeof(litem->name), 0, 0, "");
		UI_but_flag_enable(but, UI_BUT_NO_UTF8); /* allow non utf8 names */
		UI_but_flag_disable(but, UI_BUT_UNDO);

		/* returns false if button got removed */
		if (UI_but_active_only(C, ar, block, but) == false) {
			tile->flag &= ~LAYERTILE_RENAME;
			/* Yuk! Sending notifier during draw. Need to
			 * do that so item uses regular drawing again. */
			WM_event_add_notifier(C, NC_SPACE | ND_SPACE_LAYERS, NULL);
		}
	}
	else {
		uiLayout *layout = UI_block_layout(
		        block, UI_LAYOUT_HORIZONTAL, UI_LAYOUT_HEADER,
		        rect.xmin, rect.ymax, BLI_rctf_size_y(&rect), 0, 0, style);
		litem->draw(C, litem, layout);
		uiItemL(layout, "", 0); /* XXX without this editing last item causes crashes */
		UI_block_layout_resolve(block, NULL, NULL);
	}
	tile_size_y = header_y;

	if (expanded) {
		uiLayout *layout = UI_block_layout(
		        block, UI_LAYOUT_VERTICAL, UI_LAYOUT_PANEL,
		        rect.xmin, rect.ymin, BLI_rctf_size_x(&rect), 0, 0, style);
		litem->draw_settings(C, litem, layout);

		int size_y = 0;
		UI_block_layout_resolve(block, NULL, &size_y);
		tile_size_y = -(ofs_y + size_y + ar->v2d.cur.ymin);
	}

	/* draw background, this is done after defining buttons so we can get real layout height */
	if ((idx % 2) || (tile->flag & LAYERTILE_FLOATING)) {
		if (tile->flag & LAYERTILE_FLOATING) {
			UI_ThemeColorShadeAlpha(TH_BACK, idx % 2 ? 10 : 0, -100);
		}
		else {
			UI_ThemeColorShade(TH_BACK, 10);
		}

		glEnable(GL_BLEND);
		fdrawbox_filled(0, rect.ymax - tile_size_y, rect.xmax, rect.ymax);
		glDisable(GL_BLEND);
	}
	/* draw selection */
	if (tile->flag & LAYERTILE_SELECTED) {
		UI_draw_roundbox_corner_set(UI_CNR_ALL);
		UI_ThemeColor(TH_HILITE);
		UI_draw_roundbox(rect.xmin + pad_x, rect.ymin, rect.xmax - pad_x, rect.ymax, 5.0f);
	}

	idx++;
	BLI_rcti_rctf_copy(&tile->rect, &rect);
	/* set tile height */
	tile->tot_height = tile_size_y;

	return tile_size_y;
}

struct FloatingTileDrawInfo {
	LayerTile *tile;
	int idx;
	float pos_y;
};

static void layers_tiles_draw_floating(const bContext *C, struct FloatingTileDrawInfo *floating)
{
	ARegion *ar = CTX_wm_region(C);
	uiStyle *style = UI_style_get_dpi();

	BLI_assert(floating->tile->flag & LAYERTILE_FLOATING);

	/* Own block for both draw steps because otherwise buttons from
	 * fixed tiles are drawn over background of floating ones. */
	uiBlock *block = UI_block_begin(C, ar, __func__, UI_EMBOSS);

	if (floating->tile->litem->draw) {
		layer_tile_draw(floating->tile, C, ar, block, style, floating->pos_y, floating->idx);
	}

	UI_block_end(C, block);
	UI_block_draw(C, block);
}

static void layers_tiles_draw_fixed(
        const bContext *C,
        float *r_ofs_y, int *r_idx,
        struct FloatingTileDrawInfo *r_floating)
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	ARegion *ar = CTX_wm_region(C);
	uiStyle *style = UI_style_get_dpi();

	/* Own block for both draw steps because otherwise buttons from
	 * fixed tiles are drawn over background of floating ones. */
	uiBlock *block = UI_block_begin(C, ar, __func__, UI_EMBOSS);

	BKE_LAYERTREE_ITER_START(slayer->act_tree, 0, i, litem)
	{
		LayerTile *tile = BLI_ghash_lookup(slayer->tiles, litem);
		BLI_assert(tile->litem == litem);

		/* skip floating buts but return some info for drawing them later */
		if (tile->flag & LAYERTILE_FLOATING) {
			BLI_assert(r_floating->tile == NULL); /* only one floating tile allowed */
			r_floating->tile = tile;
			r_floating->idx = *r_idx;
			r_floating->pos_y = *r_ofs_y;
			/* use tot_height from last draw, we can assume it hasn't changed */
			*r_ofs_y += tile->tot_height;
			(*r_idx)++;
			continue;
		}

		if (litem->draw) {
			*r_ofs_y += layer_tile_draw(tile, C, ar, block, style, *r_ofs_y, *r_idx);
			(*r_idx)++;
		}
	}
	BKE_LAYERTREE_ITER_END;

	UI_block_end(C, block);
	UI_block_draw(C, block);
}

void layers_tiles_draw(const bContext *C, ARegion *ar)
{
	struct FloatingTileDrawInfo floating = {NULL};
	float ofs_y = 0.0f;
	int idx = 0;

	/* draw fixed (not floating) tiles first */
	layers_tiles_draw_fixed(C, &ofs_y, &idx, &floating);

	/* fill remaining space with empty boxes */
	const float tot_fill_tiles = (-ar->v2d.cur.ymin - ofs_y) / LAYERTILE_HEADER_HEIGHT + 1;
	for (int i = 0; i < tot_fill_tiles; i++) {
		if ((i + idx) % 2) {
			const float pos[2] = {0, -ar->v2d.cur.ymin - ofs_y - (LAYERTILE_HEADER_HEIGHT * (i + 1))};
			UI_ThemeColorShade(TH_BACK, 10);
			fdrawbox_filled(pos[0], pos[1], pos[0] + ar->winx, pos[1] + LAYERTILE_HEADER_HEIGHT);
		}
	}

	/* draw floating tile last */
	if (floating.tile) {
		layers_tiles_draw_floating(C, &floating);
	}

	/* update size of tot-rect (extents of data/viewable area) */
	UI_view2d_totRect_set(&ar->v2d, ar->winx - BLI_rcti_size_x(&ar->v2d.vert), ofs_y);
}


/* -------------------------------------------------------------------- */
/* Layer draw callbacks */

void layer_group_draw(const bContext *UNUSED(C), LayerTreeItem *litem, uiLayout *layout)
{
	uiItemL(layout, litem->name, ICON_FILE_FOLDER);
}

void object_layer_draw(const bContext *C, LayerTreeItem *litem, uiLayout *layout)
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	LayerTile *tile = BLI_ghash_lookup(slayer->tiles, litem);
	uiBlock *block = uiLayoutGetBlock(layout);
	const bool draw_settingbut = litem->draw_settings && tile->flag & (LAYERTILE_SELECTED | LAYERTILE_EXPANDED);

	uiItemL(layout, litem->name, 0);
	if (draw_settingbut) {
		UI_block_emboss_set(block, UI_EMBOSS_NONE);
		uiDefIconButBitI(block, UI_BTYPE_TOGGLE, LAYERTILE_EXPANDED, 0,
		                 ICON_SCRIPTWIN, 0, 0, UI_UNIT_X, UI_UNIT_Y, (int *)&tile->flag,
		                 0.0f, 0.0f, 0.0f, 0.0f, TIP_("Toggle layer settings"));
		UI_block_emboss_set(block, UI_EMBOSS);
	}
}

void object_layer_draw_settings(const bContext *UNUSED(C), LayerTreeItem *UNUSED(litem), uiLayout *layout)
{
	/* TODO */
	uiItemL(layout, "Add stuff here!", 0);
	uiItemL(layout, "Test", 0);
}
