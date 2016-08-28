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

/** \file blender/blenkernel/intern/empty.c
 *  \ingroup bke
 */

#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BKE_empty.h"
#include "BKE_image.h"
#include "BKE_object.h"

#include "DNA_object_types.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "MEM_guardedalloc.h"


void BKE_empty_draw_type_set(Object *ob, const int value)
{
	ob->empty_drawtype = value;

	if (ob->type == OB_EMPTY && ob->empty_drawtype == OB_EMPTY_IMAGE) {
		if (!ob->iuser) {
			ob->iuser = MEM_callocN(sizeof(ImageUser), "image user");
			ob->iuser->ok = 1;
			ob->iuser->frames = 100;
			ob->iuser->sfra = 1;
			ob->iuser->fie_ima = 2;
		}
	}
	else {
		if (ob->iuser) {
			MEM_freeN(ob->iuser);
			ob->iuser = NULL;
		}
	}
}

void BKE_empty_drawboundbox_get(const Object *ob, BoundBox *r_bb)
{
	const float size = ob->empty_drawsize;
	float min[3] = {0.0f};
	float max[3] = {0.0f};

	BLI_assert(ob->type == OB_EMPTY);

	switch (ob->empty_drawtype) {
		case OB_ARROWS:
			min[0] = min[1] = -size * 0.08f;
			copy_v3_fl(max, size);
			break;
		case OB_CIRCLE:
			min[0] = min[2] = -size;
			max[0] = max[2] = size;
			break;
		case OB_SINGLE_ARROW:
			min[0] = min[1] = -size * 0.035f;
			max[0] = max[1] = size * 0.035f;
			max[2] = size;
			break;
		case OB_EMPTY_CONE:
			min[0] = min[2] = -size;
			max[0] = max[2] = size;
			max[1] = size * 2.0f;
			break;
		case OB_EMPTY_IMAGE:
		{
			/* get correct image size */
			float img_size[2], img_scale[2];
			BKE_empty_image_size_get(ob, img_size, img_scale);
			mul_v2_v2v2(max, img_size, img_scale);

			/* apply offset */
			float ofs[2];
			mul_v2_v2v2(ofs, ob->ima_ofs, img_size);
			mul_v2_v2(ofs, img_scale);
			add_v2_v2(min, ofs);
			add_v2_v2(max, ofs);
			break;
		}
		default:
			copy_v3_fl(min, -size);
			copy_v3_fl(max, size);
			break;
	}

	BKE_boundbox_init_from_minmax(r_bb, min, max);
}

/**
 * \note Need to free \a r_ibuf using #BKE_image_release_ibuf.
 */
void BKE_empty_imbuf_get(const Object *ob, Image **r_ima, ImBuf **r_ibuf)
{
	BLI_assert(ob->type == OB_EMPTY && ob->empty_drawtype == OB_EMPTY_IMAGE);

	*r_ima = ob->data;
	*r_ibuf = BKE_image_acquire_ibuf(*r_ima, ob->iuser, NULL);

	if (*r_ibuf && ((*r_ibuf)->rect == NULL) && ((*r_ibuf)->rect_float != NULL)) {
		IMB_rect_from_float(* r_ibuf);
	}
}

void BKE_empty_image_size_get_ex(
        const Object *ob, Image *ima, ImBuf *ibuf,
        float r_size_xy[2], float r_scale_xy[2])
{
	float sca_x = 1.0f;
	float sca_y = 1.0f;
	int ima_x, ima_y;

	BLI_assert(ob->type == OB_EMPTY && ob->empty_drawtype == OB_EMPTY_IMAGE);
	BLI_assert(r_size_xy || r_scale_xy);

	/* Get the buffer dimensions so we can fallback to fake ones */
	if (ibuf && ibuf->rect) {
		ima_x = ibuf->x;
		ima_y = ibuf->y;
	}
	else {
		ima_x = 1;
		ima_y = 1;
	}

	/* Get the image aspect even if the buffer is invalid */
	if (ima) {
		if (ima->aspx > ima->aspy) {
			sca_y = ima->aspy / ima->aspx;
		}
		else if (ima->aspx < ima->aspy) {
			sca_x = ima->aspx / ima->aspy;
		}
	}

	/* Calculate Image scale */
	const float scale = ob->empty_drawsize / max_ff((float)ima_x * sca_x, (float)ima_y * sca_y);

	if (r_size_xy) {
		r_size_xy[0] = ima_x;
		r_size_xy[1] = ima_y;
	}
	if (r_scale_xy) {
		r_scale_xy[0] = scale * sca_x;
		r_scale_xy[1] = scale * sca_y;
	}
}

void BKE_empty_image_size_get(const Object *ob, float r_size_xy[2], float r_scale_xy[2])
{
	Image *ima = NULL;
	ImBuf *ibuf = NULL;

	BKE_empty_imbuf_get(ob, &ima, &ibuf);
	BKE_empty_image_size_get_ex(ob, ima, ibuf, r_size_xy, r_scale_xy);

	if (ibuf) {
		BKE_image_release_ibuf(ima, ibuf, NULL);
	}
}
