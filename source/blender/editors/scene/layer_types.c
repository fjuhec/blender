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

#include "DNA_object_types.h"
#include "DNA_space_types.h"

#include "ED_object.h"
#include "ED_scene.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "UI_interface.h"
#include "UI_interface_icons.h"
#include "UI_resources.h"

#include "WM_api.h"
#include "WM_types.h"

#define TOT_VISIBILITY_BITS 20


static void objectlayer_visible_editobject_ensure(bContext *C, LayerTreeItem *litem)
{
	Scene *scene = CTX_data_scene(C);

	if (scene->obedit && litem->is_hidden && scene->obedit->layer == litem) {
		ED_object_mode_compat_set(C, scene->obedit, OB_MODE_OBJECT, CTX_wm_reports(C));
		WM_event_add_notifier(C, NC_SCENE | ND_MODE, scene);
	}
}

static void layer_visibility_update_cb(bContext *C, void *arg1, void *UNUSED(arg2))
{
	objectlayer_visible_editobject_ensure(C, arg1);
	WM_event_add_notifier(C, NC_SCENE | ND_LAYER, NULL);
}

static void layer_visibility_but_draw(uiBlock *block, uiLayout *layout, LayerTreeItem *litem)
{
	BLI_assert(uiLayoutGetBlock(layout) == block);

	uiBut *but = uiDefIconButBitC(block, UI_BTYPE_ICON_TOGGLE_N, 1, 0, ICON_VISIBLE_IPO_OFF, 0, 0, UI_UNIT_X, UI_UNIT_Y,
	                 &litem->is_hidden, 0.0f, 0.0f, 0.0f, 0.0f, "Layer Visibility");
	UI_but_func_set(but, layer_visibility_update_cb, litem, NULL);
}

static void object_layer_draw(const bContext *C, LayerTreeItem *litem, uiLayout *layout)
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	LayerTile *tile = BLI_ghash_lookup(slayer->tiles, litem);
	uiBlock *block = uiLayoutGetBlock(layout);
	const bool draw_settingbut = litem->type->draw_settings && tile->flag & (LAYERTILE_SELECTED | LAYERTILE_EXPANDED);

	/* name with color set icon */
	const int col_icon = UI_colorset_icon_get(RNA_enum_get(litem->ptr, "color_set"));
	uiItemL(layout, litem->name, col_icon);

	uiLayoutSetAlignment(layout, UI_LAYOUT_ALIGN_RIGHT);

	if (draw_settingbut) {
		uiDefIconButBitI(block, UI_BTYPE_TOGGLE, LAYERTILE_EXPANDED, 0,
		                 ICON_SCRIPTWIN, 0, 0, UI_UNIT_X, UI_UNIT_Y, (int *)&tile->flag,
		                 0.0f, 0.0f, 0.0f, 0.0f, TIP_("Toggle layer settings"));
	}
	else {
		/* dummy button for alignment */
		uiDefIconBut(block, UI_BTYPE_LABEL, 0, ICON_NONE, 0, 0, UI_UNIT_X, UI_UNIT_Y,
		             NULL, 0.0f, 0.0f, 0.0f, 0.0f, "");
	}

	layer_visibility_but_draw(block, layout, litem);
}

static void layer_visibility_bit_update_cb(struct Main *UNUSED(main), Scene *scene, PointerRNA *ptr)
{
	LayerTreeItem *container = NULL;

	BKE_LAYERTREE_ITER_START(scene->object_layers, 0, i, litem)
	{
		if (litem->ptr->data == ptr->data) {
			container = litem;
			break;
		}
	}
	BKE_LAYERTREE_ITER_END;
	BLI_assert(container && container->type->type == LAYER_ITEMTYPE_LAYER);

	LayerTypeObject *oblayer = (LayerTypeObject *)container;
	PropertyRNA *prop = RNA_struct_find_property(ptr, "visibility_bits");
	int bits[TOT_VISIBILITY_BITS];

	RNA_property_boolean_get_array(ptr, prop, bits);

	oblayer->visibility_bits = 0;
	for (int i = 0; i < TOT_VISIBILITY_BITS; i++) {
		if (bits[i]) {
			oblayer->visibility_bits |= (1 << i);
		}
	}
}

static void object_layer_draw_settings(const bContext *UNUSED(C), LayerTreeItem *litem, uiLayout *layout)
{
	uiLayout *split = uiLayoutSplit(layout, 0.5f, false);
	uiItemR(split, litem->ptr, "color_set", 0, "Color Set", ICON_NONE);

	uiLayout *row = uiLayoutRow(split, false);
	uiLayoutSetAlignment(row, UI_LAYOUT_ALIGN_LEFT);
	uiItemL(row, "Visibility Bits:", ICON_NONE);
	uiTemplateLayers(row, litem->ptr, "visibility_bits", NULL, NULL, 0);
}

static void object_layer_copy(LayerTreeItem *copied_item, const LayerTreeItem *original_item)
{
	LayerTypeObject *original_oblayer = (LayerTypeObject *)original_item;
	LayerTypeObject *copied_oblayer = (LayerTypeObject *)copied_item;

	BLI_assert(&copied_oblayer->litem == copied_item);

	/* copy bases */
	copied_oblayer->bases = MEM_dupallocN(original_oblayer->bases);
	BKE_OBJECTLAYER_BASES_ITER_START(original_oblayer, i, original_base)
	{
		copied_oblayer->bases[i] = MEM_dupallocN(original_base);
		copied_oblayer->bases[i]->layer = copied_item;
	}
	BKE_OBJECTLAYER_BASES_ITER_END;
}

static void LAYERTYPE_object(LayerType *lt)
{
	/* Should always be same default as set in BKE_objectlayer_add */
	static int default_bits[TOT_VISIBILITY_BITS] = {1};
	PropertyRNA *prop;

	lt->idname = __func__;
	/* XXX Should re-evaluate how eLayerTreeItem_Type is used */
	lt->type = LAYER_ITEMTYPE_LAYER;

	lt->draw = object_layer_draw;
	lt->draw_settings = object_layer_draw_settings;
	lt->copy = object_layer_copy;
	lt->free = BKE_objectlayer_free;

	RNA_def_enum(lt->srna, "color_set", rna_enum_color_sets_items, 0, "Color Set", "Custom color set for this layer");
	prop = RNA_def_boolean_layer_member(lt->srna, "visibility_bits", TOT_VISIBILITY_BITS, default_bits,
	                                    "Visibility Bits", "");
	RNA_def_property_update_runtime(prop, layer_visibility_bit_update_cb);
}


static void layer_group_draw(const bContext *C, LayerTreeItem *litem, uiLayout *layout)
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	LayerTile *tile = BLI_ghash_lookup(slayer->tiles, litem);
	uiBlock *block = uiLayoutGetBlock(layout);
	uiBut *but;

	but = uiDefIconButBitI(block, UI_BTYPE_ICON_TOGGLE_N, LAYERTILE_CLOSED, 0,
	                 ICON_TRIA_RIGHT, 0, 0, UI_UNIT_X, UI_UNIT_Y, &tile->flag,
	                 0.0f, 0.0f, 0.0f, 0.0f, TIP_("Toggle display of layer children"));
	UI_but_drawflag_enable(but, UI_BUT_ICON_LEFT); /* doesn't align nicely without this */

	but = uiDefBut(block, UI_BTYPE_LABEL, 0, litem->name, 0, 0, UI_UNIT_X * 10, UI_UNIT_Y,
	               NULL, 0.0f, 0.0f, 0.0f, 0.0f, "");
	UI_but_drawflag_enable(but, UI_BUT_TEXT_NO_MARGIN);

	uiLayoutSetAlignment(layout, UI_LAYOUT_ALIGN_RIGHT);
	layer_visibility_but_draw(block, layout, litem);
}

static void LAYERTYPE_group(LayerType *lt)
{
	lt->idname = __func__;
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
