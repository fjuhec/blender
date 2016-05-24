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

#include "DNA_windowmanager_types.h"

#include "ED_object.h"
#include "ED_screen.h"

#include "WM_api.h"
#include "WM_types.h"

#include "layers_intern.h" /* own include */


static int layer_add_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *UNUSED(event))
{
	SpaceLayers *slayer = CTX_wm_space_layers(C);
	LayerTreeItem *new_item;

	if (true) { /* context check (like: slayer->context == SLAYER_CONTEXT_OBJECT) */
		Scene *scene = CTX_data_scene(C);
		new_item = ED_object_layer_add(scene->object_layers, NULL);
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


/* ************************** registration - operator types **********************************/

void layers_operatortypes(void)
{
	WM_operatortype_append(LAYERS_OT_layer_add);
}

void layers_keymap(wmKeyConfig *keyconf)
{
	wmKeyMap *keymap = WM_keymap_find(keyconf, "Layer Manager", SPACE_LAYERS, 0);
	(void)keymap;
}
