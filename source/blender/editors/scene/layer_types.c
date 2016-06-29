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

/** \file blender/editors/scene/layer_types.c
 *  \ingroup edscene
 */

#include "BKE_context.h"
#include "BKE_layer.h"
#include "BKE_object.h"

#include "BLI_utildefines.h"
#include "BLI_ghash.h"

#include "BLT_translation.h"

#include "DNA_space_types.h"

#include "ED_scene.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "UI_interface.h"
#include "UI_interface_icons.h"
#include "UI_resources.h"


static void object_layer_draw(const bContext *C, LayerTreeItem *litem, uiLayout *layout)
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	LayerTile *tile = BLI_ghash_lookup(slayer->tiles, litem);
	uiBlock *block = uiLayoutGetBlock(layout);
	const bool draw_settingbut = litem->type->draw_settings && tile->flag & (LAYERTILE_SELECTED | LAYERTILE_EXPANDED);

	/* name with color set icon */
	const int col_icon = UI_colorset_icon_get(RNA_enum_get(litem->ptr, "color_set"));
	uiItemL(layout, litem->name, col_icon);

	if (draw_settingbut) {
		UI_block_emboss_set(block, UI_EMBOSS_NONE);
		uiDefIconButBitI(block, UI_BTYPE_TOGGLE, LAYERTILE_EXPANDED, 0,
		                 ICON_SCRIPTWIN, 0, 0, UI_UNIT_X, UI_UNIT_Y, (int *)&tile->flag,
		                 0.0f, 0.0f, 0.0f, 0.0f, TIP_("Toggle layer settings"));
		UI_block_emboss_set(block, UI_EMBOSS);
	}
}

static void object_layer_draw_settings(const bContext *UNUSED(C), LayerTreeItem *litem, uiLayout *layout)
{
	uiLayout *split = uiLayoutSplit(layout, 0.5f, false);
	uiItemR(split, litem->ptr, "color_set", 0, "Color Set", ICON_NONE);
}

static void LAYERTYPE_object(LayerType *lt)
{
	/* XXX Should re-evaluate how eLayerTreeItem_Type is used */
	lt->type = LAYER_ITEMTYPE_LAYER;

	lt->draw = object_layer_draw;
	lt->draw_settings = object_layer_draw_settings;
	lt->free = BKE_objectlayer_free;

	RNA_def_enum(lt->srna, "color_set", rna_enum_color_sets_items, 0, "Color Set", "Custom color set for this layer");
}


static void layer_group_draw(const bContext *C, LayerTreeItem *litem, uiLayout *layout)
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	LayerTile *tile = BLI_ghash_lookup(slayer->tiles, litem);
	uiBlock *block = uiLayoutGetBlock(layout);
	uiBut *but;

	UI_block_emboss_set(block, UI_EMBOSS_NONE);
	but = uiDefIconButBitI(block, UI_BTYPE_ICON_TOGGLE, LAYERTILE_CLOSED, 0,
	                 ICON_TRIA_RIGHT, 0, 0, UI_UNIT_X, UI_UNIT_Y, &tile->flag,
	                 0.0f, 0.0f, 0.0f, 0.0f, TIP_("Toggle display of layer children"));
	UI_block_emboss_set(block, UI_EMBOSS);
	UI_but_drawflag_enable(but, UI_BUT_ICON_LEFT); /* doesn't align nicely without this */

	but = uiDefBut(block, UI_BTYPE_LABEL, 0, litem->name, 0, 0, UI_UNIT_X * 10, UI_UNIT_Y,
	               NULL, 0.0f, 0.0f, 0.0f, 0.0f, "");
	UI_but_drawflag_enable(but, UI_BUT_TEXT_NO_MARGIN);
}

static void LAYERTYPE_group(LayerType *lt)
{
	lt->type = LAYER_ITEMTYPE_GROUP;

	lt->draw = layer_group_draw;
}


/* -------------------------------------------------------------------- */

/**
 * Startup initialization of layer types.
 */
void ED_scene_layertypes_init(void)
{
	BKE_layertype_append(LAYERTYPE_object);
	BKE_layertype_append(LAYERTYPE_group);
}
