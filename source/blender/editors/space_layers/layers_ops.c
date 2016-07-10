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

#include <stdlib.h>

#include "BKE_context.h"
#include "BKE_depsgraph.h"
#include "BKE_layer.h"
#include "BKE_object.h"

#include "BLI_alloca.h"
#include "BLI_compiler_attrs.h"
#include "BLI_easing.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"
#include "BLI_ghash.h"
#include "BLI_listbase.h"
#include "BLI_rect.h"

#include "DNA_windowmanager_types.h"

#include "ED_object.h"
#include "ED_screen.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "WM_api.h"
#include "WM_types.h"

#include "layers_intern.h" /* own include */


#define LAYERDRAG_DROP_ANIM_DURATION_FAC 0.05f

#define OBJECTLAYER_DEFAULT_NAME "Untitled Layer"
#define LAYERGROUP_DEFAULT_NAME  "Untitled Group"

/**
 * LayerTile wrapper for additional information needed for
 * offsetting and animating tiles during drag & drop reordering.
 */
typedef struct {
	LayerTile *tile;
	/* With this we can substract the added offset when done. If we simply
	 * set it to 0 LayerTile.ofs can't be reliably used elsewhere. */
	int ofs_added;

	/* anim data (note: only for LayerDragData.dragged currently) */
	int anim_start_ofsy;
	float anim_duration; /* tot duration the animation is supposed to take */
} LayerDragTile;

/**
 * Data for layer tile drag and drop reordering.
 */
typedef struct {
	LayerDragTile dragged; /* info for the tile that's being dragged */
	/* LayerDragTile hash table for all items that need special info while dragging */
	GHash *tiledrags;

	int insert_idx;
	int init_mval_y;
	bool is_dragging;
	bool is_cancel;
	bool needs_reopen;

	/* anim data */
	wmTimer *anim_timer;
} LayerDragData;

enum {
	LAYERDRAG_CANCEL = 1,
	LAYERDRAG_CONFIRM,
};

/* -------------------------------------------------------------------- */


static int layer_add_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *UNUSED(event))
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	LayerTreeItem *new_item = NULL;

	if (slayer->act_tree->type == LAYER_TREETYPE_OBJECT) {
		new_item = BKE_objectlayer_add(slayer->act_tree, NULL, OBJECTLAYER_DEFAULT_NAME);
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

enum {
	LAYER_DELETE_LAYER_ONLY,
	LAYER_DELETE_WITH_CONTENT,
};

static void layers_remove_layer_objects(bContext *C, LayerTreeItem *litem)
{
	LayerTypeObject *oblayer = (LayerTypeObject *)litem;
	struct Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);

	BKE_OBJECTLAYER_BASES_ITER_START(oblayer, i, base)
	{
		ED_base_object_free_and_unlink(bmain, scene, base);
	}
	BKE_OBJECTLAYER_BASES_ITER_END;
	BKE_objectlayer_bases_unassign_all(litem, false);

	DAG_relations_tag_update(bmain);
}

static int layer_remove_exec(bContext *C, wmOperator *op)
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	ListBase remlist = {NULL};
	const int rem_type = RNA_enum_get(op->ptr, "type");

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
	/* Now, delete all items in the list (and content if needed). */
	for (LinkData *tile_link = remlist.first, *next_link; tile_link; tile_link = next_link) {
		LayerTile *tile = tile_link->data;
		LayerTreeItem *litem = tile->litem;

		/* delete layer content */
		if (rem_type == LAYER_DELETE_WITH_CONTENT) {
			switch (slayer->act_tree->type) {
				case LAYER_TREETYPE_OBJECT:
					layers_remove_layer_objects(C, litem);
					break;
			}
		}

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
	static EnumPropertyItem prop_layers_delete_types[] = {
		{LAYER_DELETE_LAYER_ONLY,   "LAYER_ONLY",   0, "Only Layer",   "Delete layer(s), keept its content"},
		{LAYER_DELETE_WITH_CONTENT, "WITH_CONTENT", 0, "With Content", "Delete layer(s) and its content"},
		{0, NULL, 0, NULL, NULL}
	};

	/* identifiers */
	ot->name = "Remove Layers";
	ot->idname = "LAYERS_OT_remove";
	ot->description = "Remove selected layers";

	/* api callbacks */
	ot->invoke = WM_menu_invoke;
	ot->exec = layer_remove_exec;
	ot->poll = ED_operator_layers_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	ot->prop = RNA_def_enum(ot->srna, "type", prop_layers_delete_types, LAYER_DELETE_LAYER_ONLY,
	                        "Type", "Method used for deleting layers");
}

static int layer_group_add_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *UNUSED(event))
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);

	LayerTreeItem *new_group = BKE_layeritem_add(slayer->act_tree, NULL, LAYER_ITEMTYPE_GROUP, LAYERGROUP_DEFAULT_NAME);
	layers_tile_add(slayer, new_group);

	/* Add selected items to group */
	bool is_first = true;
	BKE_LAYERTREE_ITER_START(slayer->act_tree, 0, i, litem)
	{
		LayerTile *tile = BLI_ghash_lookup(slayer->tiles, litem);
		if (tile->flag & LAYERTILE_SELECTED) {
			if (is_first) {
				BLI_assert(BLI_listbase_is_empty(&new_group->childs));
				BKE_layeritem_move(new_group, litem->index, false);
				is_first = false;
			}
			BKE_layeritem_group_assign(new_group, litem);
		}
	}
	BKE_LAYERTREE_ITER_END;

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

static LayerDragTile *layer_drag_tile_data_init(LayerDragData *ldrag, LayerTile *tile)
{
	LayerDragTile *tiledata = MEM_callocN(sizeof(LayerDragTile), "LayerDragTile");
	tiledata->tile = tile;
	BLI_ghash_insert(ldrag->tiledrags, tile, tiledata);

	return tiledata;
}

static void layer_drag_tile_add_offset(LayerDragTile *tiledata, const int offset, const bool delta)
{
	tiledata->tile->ofs[1] += delta ? offset - tiledata->ofs_added : offset;
	tiledata->ofs_added += delta ? offset - tiledata->ofs_added : offset;
}

static void layer_drag_tile_remove_cb(void *val)
{
	LayerDragTile *tiledata = val;
	tiledata->tile->ofs[1] -= tiledata->ofs_added;
	MEM_freeN(tiledata);
}

/**
 * Update offsets and information on where to insert the tile if key is released now. Note that
 * items are *not* reordered here, this should only be done on key release to avoid updates in-between.
 */
static void layer_drag_update_positions(SpaceLayers *slayer, LayerDragData *ldrag, const wmEvent *event)
{
	LayerTile *drag_tile = ldrag->dragged.tile;
	/* will the tile be moved up or down in the list? */
	const bool move_up = ldrag->init_mval_y < event->mval[1];
	/* did mouse move up since last event? */
	const bool upwards_motion = event->prevy < event->y;

	BKE_LAYERTREE_ITER_START(slayer->act_tree, 0, i, iter_litem)
	{
		LayerTile *iter_tile = BLI_ghash_lookup(slayer->tiles, iter_litem);
		bool needs_offset = false;

		if (iter_tile == drag_tile)
			continue;

		/* check if the tile is supposed to be offset */
		if ((move_up && iter_tile->litem->index < drag_tile->litem->index) ||
		    (!move_up && iter_tile->litem->index > drag_tile->litem->index))
		{
			const int iter_cent = BLI_rcti_cent_y(&iter_tile->rect);
			const int cmp_yval = upwards_motion ? drag_tile->rect.ymax : drag_tile->rect.ymin;

			if ((move_up && cmp_yval > iter_cent) || (!move_up && cmp_yval < iter_cent)) {
				needs_offset = true;
			}
		}

		/* remember, ldrag->tiledrags only contains offset tiles */
		LayerDragTile *tiledata = BLI_ghash_lookup(ldrag->tiledrags, iter_tile);
		if (needs_offset) {
			/* ensure the tile is offset (is not the case if it's not in LayerDragData.tiledrags) */
			if (!tiledata) {
				tiledata = layer_drag_tile_data_init(ldrag, iter_tile);
				layer_drag_tile_add_offset(tiledata, drag_tile->tot_height * (move_up ? 1 : -1), false);
			}
			if (ldrag->insert_idx == -1 || !move_up || ldrag->insert_idx > iter_litem->index) {
				/* store where the tile should be inserted if key is released now */
				ldrag->insert_idx = iter_litem->index;
			}
		}
		else if (tiledata) {
			if (ldrag->insert_idx == -1 || move_up || ldrag->insert_idx <= iter_litem->index) {
				/* Store where the tile should be inserted if key is released now. It's
				 * possible that this is the tile's initial position, so check for that. */
				if (iter_litem->index + (upwards_motion ? -1 : 1) == drag_tile->litem->index) {
					/* back to initial position */
					ldrag->insert_idx = drag_tile->litem->index;
				}
				else {
					ldrag->insert_idx = iter_litem->index - (upwards_motion ? 1 : 0);
				}
			}
			/* remove offset from tile and remove it from LayerDragData.tiledrags
			 * hash table since it should only contain offset tiles */
			BLI_ghash_remove(ldrag->tiledrags, iter_tile, NULL, layer_drag_tile_remove_cb);
		}
	}
	BKE_LAYERTREE_ITER_END;

	/* fallback to initial position */
	if (ldrag->insert_idx == -1) {
		ldrag->insert_idx = drag_tile->litem->index;
	}

	layer_drag_tile_add_offset(&ldrag->dragged, ldrag->init_mval_y - event->mval[1], true);
}

static void layer_drag_drop_anim_start(bContext *C, LayerDragData *ldrag, const wmEvent *event)
{
	ldrag->anim_timer = WM_event_add_timer(CTX_wm_manager(C), CTX_wm_window(C), TIMER, 0.02);

	BLI_assert(ldrag->insert_idx >= 0);
	if (ldrag->is_cancel || ldrag->insert_idx == ldrag->dragged.tile->litem->index) {
		ldrag->dragged.anim_start_ofsy = ldrag->dragged.ofs_added;
	}
	else {
		SpaceLayers *slayer = CTX_wm_space_layers(C);
		LayerTile *isert_tile = BLI_ghash_lookup(slayer->tiles, slayer->act_tree->items_all[ldrag->insert_idx]);
		const bool is_upwards = ldrag->init_mval_y < event->mval[1];
		ldrag->dragged.anim_start_ofsy = is_upwards ? (isert_tile->rect.ymax - ldrag->dragged.tile->rect.ymin) :
		                                              (isert_tile->rect.ymin - ldrag->dragged.tile->rect.ymax);
	}
	/* duration is based on distance to end position */
	ldrag->dragged.anim_duration = sqrt3f(ABS(ldrag->dragged.anim_start_ofsy)) * LAYERDRAG_DROP_ANIM_DURATION_FAC;

	/* remove old offsets, tiles are reordered now */
	GHashIterator gh_iter;
	GHASH_ITER(gh_iter, ldrag->tiledrags) {
		LayerDragTile *tiledata = BLI_ghashIterator_getValue(&gh_iter);
		tiledata->tile->ofs[1] -= tiledata->ofs_added;
		tiledata->ofs_added = 0;
	}
	ldrag->dragged.tile->ofs[1] -= ldrag->dragged.ofs_added - ldrag->dragged.anim_start_ofsy;
	ldrag->dragged.ofs_added = ldrag->dragged.anim_start_ofsy;
}

static void layer_drag_drop_anim_step(LayerDragData *ldrag)
{
	/* animation for dragged item */
	const int cur_ofs = BLI_easing_cubic_ease_in_out(ldrag->anim_timer->duration, 0.0f, ldrag->dragged.anim_start_ofsy,
	                                                 ldrag->dragged.anim_duration);
	layer_drag_tile_add_offset(&ldrag->dragged, ldrag->dragged.anim_start_ofsy - cur_ofs, true);
}

static void layer_drag_end(LayerDragData *ldrag)
{
	/* unset data for dragged tile */
	ldrag->dragged.tile->ofs[1] -= ldrag->dragged.ofs_added;
	ldrag->dragged.tile->flag &= ~LAYERTILE_FLOATING;

	BLI_ghash_free(ldrag->tiledrags, NULL, layer_drag_tile_remove_cb);
	MEM_freeN(ldrag);
}

static int layer_drag_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	ARegion *ar = CTX_wm_region(C);
	LayerDragData *ldrag = op->customdata;

	if (event->type == EVT_MODAL_MAP && ldrag->is_dragging) {
		switch (event->val) {
			case LAYERDRAG_CANCEL:
				ldrag->is_cancel = true;
				ldrag->is_dragging = false;
				layer_drag_drop_anim_start(C, ldrag, event);
				break;
			case LAYERDRAG_CONFIRM:
				ldrag->is_dragging = false;
				layer_drag_drop_anim_start(C, ldrag, event);
				if (ldrag->needs_reopen) {
					ldrag->dragged.tile->flag &= ~LAYERTILE_CLOSED;
				}
				/* apply new position before animation is done */
				BKE_layeritem_move(ldrag->dragged.tile->litem, ldrag->insert_idx, true);
				if (ldrag->insert_idx != ldrag->dragged.tile->litem->index) {
					WM_event_add_notifier(C, NC_SCENE | ND_LAYER, NULL);
				}
				break;
		}
	}
	else if (event->type == MOUSEMOVE && ldrag->is_dragging) {
		layer_drag_update_positions(slayer, ldrag, event);
		ED_region_tag_redraw(ar);
	}
	else if (event->type == TIMER && ldrag->anim_timer == event->customdata) {
		ED_region_tag_redraw(ar);
		layer_drag_drop_anim_step(ldrag);
		if (ldrag->anim_timer->duration >= ldrag->dragged.anim_duration) {
			layer_drag_end(ldrag);
			WM_event_remove_timer(CTX_wm_manager(C), NULL, ldrag->anim_timer);

			return ldrag->is_cancel ? OPERATOR_CANCELLED : OPERATOR_FINISHED;
		}
	}

	return OPERATOR_RUNNING_MODAL;
}

static int layer_drag_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	LayerTile *tile = layers_tile_find_at_coordinate(slayer, event->mval);

	if (!tile)
		return OPERATOR_CANCELLED;

	LayerDragData *ldrag = MEM_callocN(sizeof(LayerDragData), __func__);
	ldrag->dragged.tile = tile;
	ldrag->tiledrags = BLI_ghash_ptr_new("LayerDragData");
	ldrag->insert_idx = -1;
	ldrag->init_mval_y = event->mval[1];
	ldrag->is_dragging = true;

	op->customdata = ldrag;
	tile->flag |= LAYERTILE_FLOATING;
	if (!BLI_listbase_is_empty(&tile->litem->childs) && !(tile->flag & LAYERTILE_CLOSED)) {
		tile->flag |= LAYERTILE_CLOSED;
		ldrag->needs_reopen = true;
	}

	WM_event_add_modal_handler(C, op);

	return OPERATOR_RUNNING_MODAL;
}

static void LAYERS_OT_move_drag(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Move Layer";
	ot->idname = "LAYERS_OT_move_drag";
	ot->description = "Change the position of a layer in the layer list using drag and drop";

	/* api callbacks */
	ot->invoke = layer_drag_invoke;
	ot->modal = layer_drag_modal;
	ot->poll = ED_operator_layers_active;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int layer_rename_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *event)
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	ARegion *ar = CTX_wm_region(C);
	LayerTile *tile = layers_tile_find_at_coordinate(slayer, event->mval);
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

BLI_INLINE void layer_selection_set(SpaceLayers *slayer, LayerTile *tile, const bool enable)
{
	if (enable) {
		(tile->flag |= LAYERTILE_SELECTED);
		slayer->act_tree->active_layer = tile->litem;
	}
	else {
		tile->flag &= ~LAYERTILE_SELECTED;
	}
}

/**
 * Change the selection state of all layer tiles.
 * \param enable: If true, tiles are selected, else they are deselected.
 */
static void layers_selection_set_all(SpaceLayers *slayer, const bool enable)
{
	GHashIterator gh_iter;
	GHASH_ITER(gh_iter, slayer->tiles) {
		LayerTile *tile = BLI_ghashIterator_getValue(&gh_iter);
		layer_selection_set(slayer, tile, enable);
	}
}

/**
 * Select everything within the range of \a from to \a to.
 * \return if anything got selected. Nothing is selected if from == to or one of them is < 0.
 */
static bool layers_select_fill(SpaceLayers *slayer, const int from, const int to)
{
	const int min = MIN2(from, to);
	const int max = MAX2(from, to);

	if (min < 0 || min == max)
		return false;

	BKE_LAYERTREE_ITER_START(slayer->act_tree, min, i, litem)
	{
		LayerTile *tile = BLI_ghash_lookup(slayer->tiles, litem);
		layer_selection_set(slayer, tile, true);
		if (i == max)
			break;
	}
	BKE_LAYERTREE_ITER_END;

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

	LayerTile *tile = layers_tile_find_at_coordinate(slayer, event->mval);

	/* little helper for setting/unsetting selection flag */
#define TILE_SET_SELECTION(enable) layer_selection_set(slayer, tile, enable);

	if (tile) {
		/* deselect all, but only if extend, deselect and toggle are false */
		if (((extend == deselect) == toggle) == false) {
			layers_selection_set_all(slayer, false);
		}
		if (extend) {
			if (fill && layers_select_fill(slayer, slayer->act_tree->active_layer->index, tile->litem->index)) {
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
			if (!(tile->flag & LAYERTILE_SELECTED)) {
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
	layers_selection_set_all(slayer, !layers_any_selected(slayer));
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

static int layer_objects_assign_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *UNUSED(event))
{
	Scene *scene = CTX_data_scene(C);
	SpaceLayers *slayer = CTX_wm_space_layers(C);

	if (!slayer->act_tree->active_layer)
		return OPERATOR_CANCELLED;

	/* TODO Uses old base list to allow assigning objects that don't have a layer yet */
//	BKE_BASES_ITER_START(scene)
	for (Base *base = scene->base.first; base; base = base->next)
	{
		if (base->flag & SELECT) {
			if (base->layer) {
				BKE_objectlayer_base_unassign(base);
			}
			BKE_objectlayer_base_assign(base, slayer->act_tree->active_layer, false);
		}
	}
//	BKE_BASES_ITER_END;

	WM_event_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, NULL);

	return OPERATOR_FINISHED;
}

static void LAYERS_OT_objects_assign(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Assign Objects";
	ot->idname = "LAYERS_OT_objects_assign";
	ot->description = "Assign selected objects to selected layers";

	/* api callbacks */
	ot->invoke = layer_objects_assign_invoke;
	ot->poll = ED_operator_layers_active;
}


/* ************************** registration - operator types **********************************/

void layers_operatortypes(void)
{
	/* organization */
	WM_operatortype_append(LAYERS_OT_layer_add);
	WM_operatortype_append(LAYERS_OT_group_add);
	WM_operatortype_append(LAYERS_OT_remove);
	WM_operatortype_append(LAYERS_OT_move_drag);
	WM_operatortype_append(LAYERS_OT_layer_rename);

	/* states (activating selecting, highlighting) */
	WM_operatortype_append(LAYERS_OT_select);
	WM_operatortype_append(LAYERS_OT_select_all_toggle);

	WM_operatortype_append(LAYERS_OT_objects_assign);
}

static wmKeyMap *layer_drag_modal_keymap(wmKeyConfig *keyconf)
{
	static EnumPropertyItem modal_items[] = {
		{LAYERDRAG_CANCEL, "CANCEL", 0, "Cancel", ""},
		{LAYERDRAG_CONFIRM, "CONFIRM", 0, "Confirm Moving", ""},
		{0, NULL, 0, NULL, NULL}
	};

	wmKeyMap *keymap = WM_modalkeymap_get(keyconf, "Layer Dragging Modal Map");

	/* this function is called for each spacetype, only needs to add map once */
	if (keymap && keymap->modal_items)
		return NULL;

	keymap = WM_modalkeymap_add(keyconf, "Layer Dragging Modal Map", modal_items);

	/* items for modal map */
	WM_modalkeymap_add_item(keymap, ESCKEY, KM_PRESS, KM_ANY, 0, LAYERDRAG_CANCEL);
	WM_modalkeymap_add_item(keymap, RIGHTMOUSE, KM_PRESS, KM_ANY, 0, LAYERDRAG_CANCEL);

	WM_modalkeymap_add_item(keymap, RETKEY, KM_RELEASE, KM_ANY, 0, LAYERDRAG_CONFIRM);
	WM_modalkeymap_add_item(keymap, PADENTER, KM_RELEASE, KM_ANY, 0, LAYERDRAG_CONFIRM);
	WM_modalkeymap_add_item(keymap, LEFTMOUSE, KM_RELEASE, KM_ANY, 0, LAYERDRAG_CONFIRM);

	/* assign to operators */
	WM_modalkeymap_assign(keymap, "LAYERS_OT_move_drag");

	return keymap;
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

	WM_keymap_add_item(keymap, "LAYERS_OT_move_drag", EVT_TWEAK_L, KM_ANY, 0, 0);
	WM_keymap_add_item(keymap, "LAYERS_OT_layer_rename", LEFTMOUSE, KM_DBL_CLICK, 0, 0);
	WM_keymap_add_item(keymap, "LAYERS_OT_layer_rename", LEFTMOUSE, KM_PRESS, KM_CTRL, 0);

	WM_keymap_add_item(keymap, "LAYERS_OT_remove", XKEY, KM_PRESS, 0, 0);
	WM_keymap_add_item(keymap, "LAYERS_OT_remove", DELKEY, KM_PRESS, 0, 0);

	WM_keymap_add_item(keymap, "LAYERS_OT_layer_add", NKEY, KM_PRESS, KM_CTRL, 0);
	WM_keymap_add_item(keymap, "LAYERS_OT_group_add", GKEY, KM_PRESS, KM_CTRL, 0);

	WM_keymap_add_item(keymap, "LAYERS_OT_objects_assign", MKEY, KM_PRESS, 0, 0);

	layer_drag_modal_keymap(keyconf);
}
