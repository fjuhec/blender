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
 * The Original Code is Copyright (C) 2016 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/object/object_layer.c
 *  \ingroup edobj
 *
 * Functions to define object layer data and UI.
 */

#include "BKE_layer.h"

#include "BLI_compiler_attrs.h"
#include "BLI_utildefines.h"

#include "ED_object.h"

static void object_layer_draw(LayerTreeItem *litem)
{
	printf("%s\n", BKE_layeritem_name_get(litem));
}

static void object_layer_draw_settings(LayerTreeItem *UNUSED(litem))
{
	/* TODO */
}

void ED_object_layer_add(LayerTree *ltree)
{
	BKE_layeritem_add(ltree, NULL, LAYER_TREETYPE_OBJECT, NULL, object_layer_draw, object_layer_draw_settings);
}
