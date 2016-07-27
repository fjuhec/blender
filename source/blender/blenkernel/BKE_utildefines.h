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

/** \file BKE_utildefines.h
 *  \ingroup bke
 *  \brief blender format specific macros
 *  \note generic defines should go in BLI_utildefines.h
 */


#ifndef __BKE_UTILDEFINES_H__
#define __BKE_UTILDEFINES_H__

#ifdef __cplusplus
extern "C" {
#endif

#define BKE_BIT_TEST_SET(value, test, flag) \
{                                           \
	if (test) (value) |=  flag;             \
	else      (value) &= ~flag;             \
} (void)0

/**
 * Local view utility macros.
 *
 * Even though it's possible to access LocalView DNA structs directly,
 * please only access using these macros (or extend it if needed).
 */

/* visibility check */
#define BKE_LOCALVIEW_IS_OBJECT_VISIBLE(v3d, ob) \
	(((v3d)->localviewd == NULL) || (((v3d)->localviewd->viewbits & (ob)->localview.viewbits) != 0))

/* Adjust local view info of ob to be visible if v3d is in local view */
#define BKE_LOCALVIEW_OBJECT_ASSIGN(v3d, ob) \
	if ((v3d)->localviewd) { \
		(ob)->localview.viewbits = (v3d)->localviewd->viewbits; \
	} (void)0

#ifdef __cplusplus
}
#endif

#endif  /* __BKE_UTILDEFINES_H__ */
