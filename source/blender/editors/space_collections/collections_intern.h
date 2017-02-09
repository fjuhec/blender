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

/** \file blender/editors/space_collections/collections_intern.h
 *  \ingroup spcollections
 */

#ifndef __COLLECTIONS_INTERN_H__
#define __COLLECTIONS_INTERN_H__

struct bContext;
struct rcti;
struct SceneLayer;
struct uiLayout;
struct wmKeyConfig;

/* collections_edit.c */
void collections_table_create(struct SceneLayer *layer, struct uiTable **r_table);
void collections_table_free(struct uiTable *table);
void collections_table_item_add(struct uiTable *table, struct LayerCollection *collection);

/* collections_ops.c */
void collections_operatortypes(void);
void collections_keymap(struct wmKeyConfig *keyconf);

/* collections_draw.c */
void collections_draw_table(const struct bContext *C, struct SpaceCollections *spc, ARegion *ar);
void collections_draw_cell(struct uiLayout *layout, void *rowdata, struct rcti drawrect);
void collections_draw_cell_visibility(struct uiLayout *layout, void *rowdata, struct rcti drawrect);
void collections_draw_cell_selectability(struct uiLayout *layout, void *rowdata, struct rcti drawrect);

#endif  /* __COLLECTIONS_INTERN_H__ */

