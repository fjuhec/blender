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

#include <string.h>

#include "BKE_layer.h"
#include "BKE_object.h"

#include "BLI_utildefines.h"
#include "BLI_ghash.h"

#include "DNA_object_types.h"

#include "MEM_guardedalloc.h"


/**
 * Allocate a new object layer tree and add a default layer (having no layer isn't allowed).
 */
LayerTree *BKE_objectlayer_tree_new(void)
{
	LayerTree *ltree = BKE_layertree_new(LAYER_TREETYPE_OBJECT);
	LayerTreeItem *defaultlayer = BKE_objectlayer_add(ltree, NULL, "Default Layer");
	ltree->active_layer = defaultlayer;
	return ltree;
}

LayerTreeItem *BKE_objectlayer_add(LayerTree *tree, LayerTreeItem *parent, const char *name)
{
	LayerTypeObject *oblayer = MEM_callocN(sizeof(LayerTypeObject), __func__);

	BLI_assert(tree->type == LAYER_TREETYPE_OBJECT);
	oblayer->visibility_bits = 1; /* Should always be same default as set in LAYERTYPE_object */
	BKE_layeritem_register(tree, &oblayer->litem, parent, LAYER_ITEMTYPE_LAYER, name);

	return &oblayer->litem;
}

void BKE_objectlayer_free(LayerTreeItem *litem)
{
	LayerTypeObject *oblayer = (LayerTypeObject *)litem;

	/* Free bases */
	for (int i = 0; i < oblayer->tot_bases; i++) {
		MEM_freeN(oblayer->bases[i]);
	}

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

void BKE_objectlayer_base_assign_ex(Base *base, LayerTreeItem *litem, const bool has_reserved, const bool add_head)
{
	LayerTypeObject *oblayer = (LayerTypeObject *)litem;

	if (!has_reserved) {
		objectlayer_array_resize(oblayer, oblayer->tot_bases + 1);
	}
	/* offset current elements to give space for new one at start of array */
	if (add_head && oblayer->tot_bases > 0) {
		/* Could use memmove for offsetting base pointers, but indices need to be updated anyway. */
		for (int i = oblayer->tot_bases; i > 0; i--) {
			oblayer->bases[i] = oblayer->bases[i - 1];
			oblayer->bases[i]->index = i;
		}
	}

	base->layer = litem;
	base->index = add_head ? 0 : oblayer->tot_bases;
	oblayer->bases[base->index] = base;
	oblayer->tot_bases++;
}

/**
 * Assign \a base to object layer \a litem. Adds it to the end of the layer.
 */
void BKE_objectlayer_base_assign(Base *base, LayerTreeItem *litem)
{
	BKE_objectlayer_base_assign_ex(base, litem, false, false);
}

/**
 * Un-assign \a base from object layer \a litem.
 */
void BKE_objectlayer_base_unassign(Base *base)
{
	LayerTypeObject *oblayer = (LayerTypeObject *)base->layer;

	for (int i = base->index + 1; i < oblayer->tot_bases; i++) {
		oblayer->bases[i]->index--;
		oblayer->bases[i - 1] = oblayer->bases[i];
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
			base->index = -1;
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

int BKE_objectlayer_bases_count(const LayerTree *ltree)
{
	int count = 0;

	BKE_LAYERTREE_ITER_START(ltree, 0, i, litem)
	{
		if (litem->type->type == LAYER_ITEMTYPE_LAYER) {
			LayerTypeObject *oblayer = (LayerTypeObject *)litem;
			count += oblayer->tot_bases;
		}
	}
	BKE_LAYERTREE_ITER_END;

	return count;
}

Base *BKE_objectlayer_base_first_find(const LayerTree *ltree)
{
	BKE_LAYERTREE_ITER_START(ltree, 0, i, litem)
	{
		if (litem->type->type == LAYER_ITEMTYPE_LAYER) {
			LayerTypeObject *oblayer = (LayerTypeObject *)litem;
			if (oblayer->tot_bases > 0) {
				return oblayer->bases[0];
			}
		}
	}
	BKE_LAYERTREE_ITER_END;

	return NULL;
}

Base *BKE_objectlayer_base_last_find(const LayerTree *ltree)
{
	for (int i = ltree->tot_items - 1; i >= 0; i--) {
		LayerTreeItem *litem = ltree->items_all[i];
		if (litem->type->type == LAYER_ITEMTYPE_LAYER) {
			LayerTypeObject *oblayer = (LayerTypeObject *)litem;
			if (oblayer->tot_bases > 0) {
				return oblayer->bases[oblayer->tot_bases - 1];
			}
		}
	}

	return NULL;
}

/**
 * \return The next base or NULL if not found.
 */
Base *BKE_objectlayer_base_next_find(const Base *prev, const bool skip_hidden_layers)
{
	LayerTypeObject *oblayer = (LayerTypeObject *)prev->layer;

	/* can directly access if next object is on same layer as prev */
	if ((prev->index + 1) < oblayer->tot_bases) {
		if (!skip_hidden_layers || BKE_layeritem_is_visible(&oblayer->litem)) {
			return oblayer->bases[prev->index + 1];
		}
	}
	/* else, have to do lookup starting from next layer */
	BKE_LAYERTREE_ITER_START(prev->layer->tree, prev->layer->index + 1, i, litem)
	{
		if ((litem->type->type == LAYER_ITEMTYPE_LAYER) &&
		    (!skip_hidden_layers || BKE_layeritem_is_visible(litem)))
		{
			LayerTypeObject *oblayer_iter = (LayerTypeObject *)litem;
			BKE_OBJECTLAYER_BASES_ITER_START(oblayer_iter, j, base_iter)
			{
				return base_iter;
			}
			BKE_OBJECTLAYER_BASES_ITER_END;
		}
	}
	BKE_LAYERTREE_ITER_END;

	return NULL;
}
