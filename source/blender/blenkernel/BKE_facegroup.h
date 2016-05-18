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
 * The Original Code is Copyright (C) 2014 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file BKE_facegroup
 *  \ingroup bke
 *
 * \brief Functions for dealing with objects and facemaps.
 */


#ifndef __BKE_FACEGROUP_H__
#define __BKE_FACEGROUP_H__

#ifdef __cplusplus
extern "C" {
#endif

struct bFaceMap;
struct ListBase;
struct Object;

/* Face map operations */
struct bFaceMap *BKE_object_facemap_add(struct Object *ob);
struct bFaceMap *BKE_object_facemap_add_name(struct Object *ob, const char *name);
void BKE_object_facemap_remove(struct Object *ob, struct bFaceMap *fmap);
void BKE_object_fmap_remove_all(struct Object *ob);

int fmap_name_index(struct Object *ob, const char *name);
void fmap_unique_name(struct bFaceMap *fmap, struct Object *ob);
struct bFaceMap *fmap_find_name(struct Object *ob, const char *name);
void fmap_copy_list(struct ListBase *outbase, struct ListBase *inbase);

#ifdef __cplusplus
}
#endif

#endif  /* __BKE_FACEGROUP_H__ */

