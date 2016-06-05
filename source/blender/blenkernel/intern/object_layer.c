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

/** \file blender/blenkernel/intern/object_layer.c
 *  \ingroup bke
 */

#include "BKE_layer.h"
#include "BKE_object.h"

#include "BLI_utildefines.h"
#include "BLI_ghash.h"

#include "DNA_object_types.h"

#include "MEM_guardedalloc.h"


static void objectlayer_free(LayerTreeItem *litem)
{
	LayerTypeObject *oblayer = (LayerTypeObject *)litem;
	BLI_ghash_free(oblayer->basehash, NULL, NULL);
}

LayerTreeItem *BKE_objectlayer_add(
        LayerTree *tree, LayerTreeItem *parent, const char *name,
        const LayerItemPollFunc poll, LayerItemDrawFunc draw, LayerItemDrawSettingsFunc draw_settings)
{
	LayerTypeObject *oblayer = MEM_callocN(sizeof(LayerTypeObject), __func__);

	BLI_assert(tree->type == LAYER_TREETYPE_OBJECT);
	BKE_layeritem_register(tree, &oblayer->litem, parent, LAYER_ITEMTYPE_LAYER, name, poll, draw, draw_settings);
	oblayer->basehash = BLI_ghash_str_new(__func__);
	oblayer->litem.free = objectlayer_free;

	return &oblayer->litem;
}

/**
 * Assign \a base to object layer \a litem.
 */
void BKE_objectlayer_base_assign(Base *base, LayerTreeItem *litem)
{
	LayerTypeObject *oblayer = (LayerTypeObject *)litem;
	if (!BLI_ghash_haskey(oblayer->basehash, base->object->id.name)) {
		BLI_ghash_insert(oblayer->basehash, base->object->id.name, base);
	}
}

/**
 * Un-assign \a base from object layer \a litem.
 */
void BKE_objectlayer_base_unassign(const Base *base, LayerTreeItem *litem)
{
	const LayerTypeObject *oblayer = (LayerTypeObject *)litem;
	BLI_ghash_remove(oblayer->basehash, base->object->id.name, NULL, NULL);
}
