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
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Lukas Toenne
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __BKE_GROOM_H__
#define __BKE_GROOM_H__

/** \file BKE_groom.h
 *  \ingroup bke
 */

struct Groom;
struct Main;

void BKE_groom_init(struct Groom *groom);
void *BKE_groom_add(struct Main *bmain, const char *name);

void BKE_groom_free(struct Groom *groom);

void BKE_groom_copy_data(struct Main *bmain, struct Groom *groom_dst, const struct Groom *groom_src, const int flag);
struct Groom *BKE_groom_copy(struct Main *bmain, const struct Groom *groom);

void BKE_groom_make_local(struct Main *bmain, struct Groom *groom, const bool lib_local);


bool BKE_groom_minmax(struct Groom *groom, float min[3], float max[3]);
void BKE_groom_boundbox_calc(struct Groom *groom, float r_loc[3], float r_size[3]);


/* === Draw Cache === */

enum {
	BKE_GROOM_BATCH_DIRTY_ALL = 0,
	BKE_GROOM_BATCH_DIRTY_SELECT,
};
void BKE_groom_batch_cache_dirty(struct Groom *groom, int mode);
void BKE_groom_batch_cache_free(struct Groom *groom);

#endif /*  __BKE_GROOM_H__ */
