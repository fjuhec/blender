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


LayerTreeItem *BKE_objectlayer_add(LayerTree *tree, LayerTreeItem *parent, const char *name)
{
	LayerTypeObject *oblayer = MEM_callocN(sizeof(LayerTypeObject), __func__);

	BLI_assert(tree->type == LAYER_TREETYPE_OBJECT);
	BKE_layeritem_register(tree, &oblayer->litem, parent, LAYER_ITEMTYPE_LAYER, name);

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
}

/**
 * Assign \a base to object layer \a litem.
 * \param has_reserved: Set to true if entries have been reserved before using #BKE_objectlayer_bases_reserve.
 */
void BKE_objectlayer_base_assign(Base *base, LayerTreeItem *litem, const bool has_reserved)
{
	LayerTypeObject *oblayer = (LayerTypeObject *)litem;

	oblayer->tot_bases++;
	if (!has_reserved) {
		objectlayer_array_resize(oblayer, oblayer->tot_bases);
	}
	oblayer->bases[oblayer->tot_bases - 1] = base;
	base->layer = litem;
}

/**
 * Un-assign \a base from object layer \a litem.
 */
void BKE_objectlayer_base_unassign(Base *base, LayerTreeItem *litem)
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
	base->layer = NULL;

	objectlayer_array_resize(oblayer, --oblayer->tot_bases);
}

/**
 * Unassign all bases.
 * \param unset_base_layer: Unset Base.layer of all bases in the layer. This is done in an extra
 *                          loop which can be avoided in some cases, so making it optional.
 */
void BKE_objectlayer_bases_unassign_all(LayerTreeItem *litem, const bool unset_base_layer)
{
	LayerTypeObject *oblayer = (LayerTypeObject *)litem;

	if (!oblayer->bases)
		return;

	if (unset_base_layer) {
		BKE_OBJECTLAYER_BASES_ITER_START(oblayer, i, base)
		{
			base->layer = NULL;
		}
		BKE_OBJECTLAYER_BASES_ITER_END;
	}
	MEM_freeN(oblayer->bases);
	oblayer->bases = NULL;
	oblayer->tot_bases = 0;
}

/**
 * Reserve memory for \a nentries_reserve number of entries. Use to avoid multiple
 * allocations, but note that it's up to you to insert the entries correctly.
 */
void BKE_objectlayer_base_entries_reserve(LayerTreeItem *litem, const unsigned int nentries_reserve)
{
	LayerTypeObject *oblayer = (LayerTypeObject *)litem;
	objectlayer_array_resize(oblayer, nentries_reserve);
}
