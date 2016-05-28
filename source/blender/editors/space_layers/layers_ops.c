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

/** \file blender/editors/space_layers/layers_ops.c
 *  \ingroup splayers
 */

#include "BKE_context.h"
#include "BKE_layer.h"

#include "BLI_compiler_attrs.h"
#include "BLI_utildefines.h"
#include "BLI_ghash.h"
#include "BLI_listbase.h"

#include "DNA_windowmanager_types.h"

#include "ED_object.h"
#include "ED_screen.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "WM_api.h"
#include "WM_types.h"

#include "layers_intern.h" /* own include */

#define LAYERGROUP_DEFAULT_NAME "Untitled Group"


static int layer_add_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *UNUSED(event))
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	LayerTreeItem *new_item;

	if (slayer->act_tree->type == LAYER_TREETYPE_OBJECT) {
		new_item = ED_object_layer_add(slayer->act_tree, NULL);
	}
	else {
		BLI_assert(0);
	}
	layers_tile_add(slayer, new_item);

	WM_event_add_notifier(C, NC_SCENE | ND_LAYER, NULL);

	return OPERATOR_FINISHED;
}

static void LAYERS_OT_layer_add(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Layer";
	ot->idname = "LAYERS_OT_layer_add";
	ot->description = "Add a new layer to the layer list";

	/* api callbacks */
	ot->invoke = layer_add_invoke;
	ot->poll = ED_operator_layers_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int layer_remove_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *UNUSED(event))
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	ListBase remlist = {NULL};

	/* First iterate over tiles. Ghash iterator doesn't allow removing items
	 * while iterating, so temporarily store selected items in a list */
	GHashIterator gh_iter;
	GHASH_ITER(gh_iter, slayer->tiles) {
		LayerTile *tile = BLI_ghashIterator_getValue(&gh_iter);
		if (tile->flag & LAYERTILE_SELECTED) {
			LinkData *tile_link = BLI_genericNodeN(tile);
			BLI_addhead(&remlist, tile_link);
		}
	}
	/* Now, delete all items in the list. */
	for (LinkData *tile_link = remlist.first, *next_link; tile_link; tile_link = next_link) {
		LayerTile *tile = tile_link->data;
		LayerTreeItem *litem = tile->litem;

		layers_tile_remove(slayer, tile, true);
		BKE_layeritem_remove(litem, true);

		next_link = tile_link->next;
		BLI_freelinkN(&remlist, tile_link);
	}
	BLI_assert(BLI_listbase_is_empty(&remlist));

	WM_event_add_notifier(C, NC_SCENE | ND_LAYER, NULL);

	return OPERATOR_FINISHED;
}

static void LAYERS_OT_remove(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Remove Layers";
	ot->idname = "LAYERS_OT_remove";
	ot->description = "Remove selected layers";

	/* api callbacks */
	ot->invoke = layer_remove_invoke;
	ot->poll = ED_operator_layers_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

typedef struct {
	SpaceLayers *slayer;
	LayerTreeItem *group;
} GroupAddSelectedData;

static bool layer_group_add_selected_cb(LayerTreeItem *litem, void *customdata)
{
	GroupAddSelectedData *gadata = customdata;
	LayerTile *tile = BLI_ghash_lookup(gadata->slayer->tiles, litem);

	if (tile->flag & LAYERTILE_SELECTED) {
		BKE_layeritem_group_assign(gadata->group, litem);
	}

	return true;
}

static int layer_group_add_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *UNUSED(event))
{
	Scene *scene = CTX_data_scene(C);
	SpaceLayers *slayer = CTX_wm_space_layers(C);

	LayerTreeItem *new_group = BKE_layeritem_add(
		            scene->object_layers, NULL, LAYER_ITEMTYPE_GROUP, LAYERGROUP_DEFAULT_NAME,
		            NULL, layer_group_draw, NULL);
	layers_tile_add(slayer, new_group);

	/* Add selected items to group */
	GroupAddSelectedData gadata = {slayer, new_group};
	BKE_layertree_iterate(slayer->act_tree, layer_group_add_selected_cb, &gadata);

	WM_event_add_notifier(C, NC_SCENE | ND_LAYER, NULL);
	return OPERATOR_FINISHED;
}

static void LAYERS_OT_group_add(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Layer Group";
	ot->idname = "LAYERS_OT_group_add";
	ot->description = "Add a new layer group to the layer list and place selected elements inside of it";

	/* api callbacks */
	ot->invoke = layer_group_add_invoke;
	ot->poll = ED_operator_layers_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int layer_rename_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *event)
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	ARegion *ar = CTX_wm_region(C);
	LayerTile *tile = layers_tile_find_at_coordinate(slayer, ar, event->mval, NULL);
	if (tile) {
		tile->flag |= LAYERTILE_RENAME;

		ED_region_tag_redraw(ar);
		return OPERATOR_FINISHED;
	}
	return OPERATOR_CANCELLED;
}

static void LAYERS_OT_layer_rename(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Rename Layer";
	ot->idname = "LAYERS_OT_layer_rename";
	ot->description = "Rename the layer under the cursor";

	/* api callbacks */
	ot->invoke = layer_rename_invoke;
	ot->poll = ED_operator_layers_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

BLI_INLINE void layer_selection_set(SpaceLayers *slayer, LayerTile *tile, const int tile_idx, const bool enable)
{
	if (enable) {
		(tile->flag |= LAYERTILE_SELECTED);
		slayer->last_selected = tile_idx;
	}
	else {
		tile->flag &= ~LAYERTILE_SELECTED;
	}
}

typedef struct LayerSelectData {
	/* input variables */
	SpaceLayers *slayer;
	int from, to; /* from must be smaller than two, or both -1 */
	bool enable;

	/* helper variable */
	int idx;
} LayerSelectData;

static bool layer_select_cb(LayerTreeItem *litem, void *customdata)
{
	LayerSelectData *sdata = customdata;
	LayerTile *tile = BLI_ghash_lookup(sdata->slayer->tiles, litem);

	BLI_assert((sdata->from == -1 && sdata->to == -1) || sdata->from < sdata->to);

	if ((sdata->from == -1) || (sdata->idx >= sdata->from && sdata->idx <= sdata->to)) {
		layer_selection_set(sdata->slayer, tile, sdata->idx, sdata->enable);
	}
	sdata->idx++;

	return true;
}

/**
 * Change the selection state of all layer tiles.
 * \param enable: If true, tiles are selected, else they are deselected.
 */
static void layers_selection_set_all(SpaceLayers *slayer, const bool enable)
{
	LayerSelectData sdata = {slayer, -1, -1, enable};
	BKE_layertree_iterate(slayer->act_tree, layer_select_cb, &sdata);
}

/**
 * Select everything within the range of \a from to \a to.
 * \return if anything got selected. Nothing is selected if from == to or one of them is < 0.
 */
static bool layers_select_fill(SpaceLayers *slayer, const int from, const int to)
{
	const int min = MIN2(from, to);
	const int max = MAX2(from, to);
	LayerSelectData sdata = {slayer, min, max, true};

	if (min < 0 || min == max)
		return false;

	BKE_layertree_iterate(slayer->act_tree, layer_select_cb, &sdata);

	return true;
}

static int layer_select_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	ARegion *ar = CTX_wm_region(C);
	const bool extend = RNA_boolean_get(op->ptr, "extend");
	const bool deselect = RNA_boolean_get(op->ptr, "deselect");
	const bool toggle = RNA_boolean_get(op->ptr, "toggle");
	const bool fill = RNA_boolean_get(op->ptr, "fill");

	int tile_idx;
	LayerTile *tile = layers_tile_find_at_coordinate(slayer, ar, event->mval, &tile_idx);

	/* little helper for setting/unsetting selection flag */
#define TILE_SET_SELECTION(enable) layer_selection_set(slayer, tile, tile_idx, enable);

	if (tile) {
		/* deselect all, but only if extend, deselect and toggle are false */
		if (((extend == deselect) == toggle) == false) {
			layers_selection_set_all(slayer, false);
		}
		if (extend) {
			if (fill && layers_select_fill(slayer, slayer->last_selected, tile_idx)) {
				/* skip */
			}
			else {
				TILE_SET_SELECTION(true);
			}
		}
		else if (deselect) {
			TILE_SET_SELECTION(false);
		}
		else {
			if (!tile->flag & LAYERTILE_SELECTED) {
				TILE_SET_SELECTION(true);
			}
			else if (toggle) {
				TILE_SET_SELECTION(false);
			}
		}

#undef TILE_SET_SELECTION

		ED_region_tag_redraw(ar);
		return OPERATOR_FINISHED;
	}
	return OPERATOR_CANCELLED;
}

static void LAYERS_OT_select(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Select Layer";
	ot->idname = "LAYERS_OT_select";
	ot->description = "Select/activate the layer under the cursor";

	/* api callbacks */
	ot->invoke = layer_select_invoke;
	ot->poll = ED_operator_layers_active;

	WM_operator_properties_mouse_select(ot);
	PropertyRNA *prop = RNA_def_boolean(ot->srna, "fill", false, "Fill", "Select everything beginning "
	                                                                     "with the last selection");
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

static int layer_select_all_toggle_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *UNUSED(event))
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);

	/* if a tile was found we deselect all, else we select all */
	layers_selection_set_all(slayer, !layers_any_selected(slayer, slayer->act_tree));
	ED_region_tag_redraw(CTX_wm_region(C));

	return OPERATOR_FINISHED;
}

static void LAYERS_OT_select_all_toggle(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "(De)select All Layers";
	ot->idname = "LAYERS_OT_select_all_toggle";
	ot->description = "Select or deselect all layers";

	/* api callbacks */
	ot->invoke = layer_select_all_toggle_invoke;
	ot->poll = ED_operator_layers_active;
}


/* ************************** registration - operator types **********************************/

void layers_operatortypes(void)
{
	/* organization */
	WM_operatortype_append(LAYERS_OT_layer_add);
	WM_operatortype_append(LAYERS_OT_group_add);
	WM_operatortype_append(LAYERS_OT_remove);
	WM_operatortype_append(LAYERS_OT_layer_rename);

	/* states (activating selecting, highlighting) */
	WM_operatortype_append(LAYERS_OT_select);
	WM_operatortype_append(LAYERS_OT_select_all_toggle);
}

void layers_keymap(wmKeyConfig *keyconf)
{
	wmKeyMap *keymap = WM_keymap_find(keyconf, "Layer Manager", SPACE_LAYERS, 0);
	wmKeyMapItem *kmi;

	/* selection */
	WM_keymap_add_item(keymap, "LAYERS_OT_select", LEFTMOUSE, KM_CLICK, 0, 0);
	kmi = WM_keymap_add_item(keymap, "LAYERS_OT_select", LEFTMOUSE, KM_CLICK, KM_SHIFT, 0);
	RNA_boolean_set(kmi->ptr, "toggle", true);
	kmi = WM_keymap_add_item(keymap, "LAYERS_OT_select", LEFTMOUSE, KM_CLICK, KM_CTRL | KM_SHIFT, 0);
	RNA_boolean_set(kmi->ptr, "extend", true);
	RNA_boolean_set(kmi->ptr, "fill", true);
	WM_keymap_add_item(keymap, "LAYERS_OT_select_all_toggle", AKEY, KM_PRESS, 0, 0);

	WM_keymap_add_item(keymap, "LAYERS_OT_layer_rename", LEFTMOUSE, KM_DBL_CLICK, 0, 0);
	WM_keymap_add_item(keymap, "LAYERS_OT_layer_rename", LEFTMOUSE, KM_PRESS, KM_CTRL, 0);
}
