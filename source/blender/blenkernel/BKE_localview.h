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

#ifndef __BKE_LOCALVIEW_H__
#define __BKE_LOCALVIEW_H__

/** \file BKE_localview.h
 *  \ingroup bke
 *  \brief Local view utility functions
 *
 * Even though it's possible to access LocalView DNA structs directly,
 * please only access using these functions (or extend it if needed).
 */

#include "BLI_compiler_attrs.h"
#include "BLI_utildefines.h"

#include "DNA_object_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"


/* Forcing inline as some of these are called a lot, mostly in loops even. */

BLI_INLINE bool BKE_localview_info_cmp(LocalViewInfo a, LocalViewInfo b) ATTR_WARN_UNUSED_RESULT;
BLI_INLINE bool BKE_localview_is_object_visible(View3D *v3d, Object *ob) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
BLI_INLINE bool BKE_localview_is_valid(LocalViewInfo localview) ATTR_WARN_UNUSED_RESULT;

BLI_INLINE void BKE_localview_object_assign(View3D *v3d, Object *ob) ATTR_NONNULL();
BLI_INLINE void BKE_localview_object_unassign(View3D *v3d, Object *ob) ATTR_NONNULL();


/* visibility checks */
BLI_INLINE bool BKE_localview_info_cmp(LocalViewInfo a, LocalViewInfo b)
{
	return (a.viewbits & b.viewbits) != 0;
}

BLI_INLINE bool BKE_localview_is_object_visible(View3D *v3d, Object *ob)
{
	return (v3d->localviewd == NULL) || BKE_localview_info_cmp(v3d->localviewd->info, ob->localview);
}

/**
 * Check if \a localview defines a visible local view.
 */
BLI_INLINE bool BKE_localview_is_valid(LocalViewInfo localview)
{
	return localview.viewbits != 0;
}

/**
 * Adjust local view info of \a ob to be visible if \a v3d is in local view
 */
BLI_INLINE void BKE_localview_object_assign(View3D *v3d, Object *ob)
{
	if (v3d->localviewd) {
		ob->localview.viewbits |= v3d->localviewd->info.viewbits;
	}
}

/**
 * Remove \a from local view of \a v3d.
 */
BLI_INLINE void BKE_localview_object_unassign(View3D *v3d, Object *ob)
{
	if (v3d->localviewd) {
		ob->localview.viewbits &= ~v3d->localviewd->info.viewbits;
	}
}

#endif // __BKE_LOCALVIEW_H__
