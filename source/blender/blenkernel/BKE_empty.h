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

#ifndef __BKE_EMPTY_H__
#define __BKE_EMPTY_H__

/** \file BKE_empty.h
 *  \ingroup bke
 *  \brief General operations for empties (object type).
 */

struct Image;
struct ImBuf;
struct Object;

void BKE_empty_draw_type_set(struct Object *ob, const int value);

struct BoundBox *BKE_empty_drawboundbox_get(const struct Object *ob);

void BKE_empty_imbuf_get(
        const struct Object *ob,
        struct Image **r_ima, struct ImBuf **r_ibuf);

void BKE_empty_image_size_get_ex(
        const struct Object *ob, struct Image *ima, struct ImBuf *ibuf,
        float r_size_xy[2], float r_scale_xy[2]);
void BKE_empty_image_size_get(
        const struct Object *ob,
        float r_size_xy[2], float r_scale_xy[2]);

#endif /* __BKE_EMPTY_H__ */
