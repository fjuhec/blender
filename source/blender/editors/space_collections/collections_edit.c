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

/** \file blender/editors/space_collections/collections_edit.c
 *  \ingroup spcollections
 */

#include <stdio.h>

#include "BKE_context.h"

#include "DNA_layer_types.h"
#include "DNA_screen_types.h"

#include "UI_table.h"

#include "collections_intern.h"


void collections_table_create(SceneLayer *layer, uiTable **r_table)
{
	*r_table = UI_table_vertical_flow_create();

	UI_table_column_add(*r_table, "collection_name", "Collection", collections_draw_cell);
	for (LayerCollection *collection = layer->layer_collections.first; collection; collection = collection->next) {
		UI_table_row_add(*r_table, collection);
	}
}

void collections_table_free(uiTable *table)
{
	UI_table_free(table);
}

void collections_table_item_add(uiTable *table, LayerCollection *collection)
{
	UI_table_row_add(table, collection);
}
