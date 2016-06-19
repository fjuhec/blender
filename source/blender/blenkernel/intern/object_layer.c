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


LayerTreeItem *BKE_objectlayer_add(
        LayerTree *tree, LayerTreeItem *parent, const char *name,
        LayerItemDrawFunc draw, LayerItemDrawSettingsFunc draw_settings)
{
	LayerTypeObject *oblayer = MEM_callocN(sizeof(LayerTypeObject), __func__);

	BLI_assert(tree->type == LAYER_TREETYPE_OBJECT);
	BKE_layeritem_register(tree, &oblayer->litem, parent, LAYER_ITEMTYPE_LAYER, name, draw, draw_settings);

	return &oblayer->litem;
}

void BKE_objectlayer_free(LayerTreeItem *litem)
{
	LayerTypeObject *oblayer = (LayerTypeObject *)litem;
	if (oblayer->bases) {
		MEM_freeN(oblayer->bases);
	}
}

static void objectlayer_array_resize(LayerTypeObject *oblayer, unsigned int new_tot_objects)
{
	if (new_tot_objects > 0) {
		oblayer->bases = MEM_reallocN(oblayer->bases, sizeof(*oblayer->bases) * new_tot_objects);
	}
	else {
		MEM_SAFE_FREE(oblayer->bases);
	}
	oblayer->tot_bases = new_tot_objects;
}

/**
 * Assign \a base to object layer \a litem.
 */
void BKE_objectlayer_base_assign(Base *base, LayerTreeItem *litem)
{
	LayerTypeObject *oblayer = (LayerTypeObject *)litem;
	objectlayer_array_resize(oblayer, oblayer->tot_bases + 1);
	oblayer->bases[oblayer->tot_bases - 1] = base;
}

/**
 * Un-assign \a base from object layer \a litem.
 */
void BKE_objectlayer_base_unassign(const Base *base, LayerTreeItem *litem)
{
	LayerTypeObject *oblayer = (LayerTypeObject *)litem;

	bool has_base = false;
	for (int i = 0; i < oblayer->tot_bases; i++) {
		if (has_base) {
			oblayer->bases[i - 1] = oblayer->bases[i];
		}
		else if (oblayer->bases[i] == base) {
			has_base = true;
		}
	}

	objectlayer_array_resize(oblayer, oblayer->tot_bases - 1);
}
