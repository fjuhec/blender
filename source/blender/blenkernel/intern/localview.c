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

/** \file blender/blenkernel/intern/localview.c
 *  \ingroup bke
 */

#include "BLI_compiler_attrs.h"
#include "BLI_utildefines.h"

#include "DNA_object_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"

/**
 * Local view main visibility checks.
 * \return if \a a is visible in \a b, or the other way around (order doesn't matter).
 */
BLI_INLINE bool BKE_localview_info_cmp(LocalViewInfo a, LocalViewInfo b)
{
	return (a.viewbits & b.viewbits) != 0;
}

/**
 * Check if \a localview defines a visible local view.
 */
BLI_INLINE bool BKE_localview_is_valid(LocalViewInfo localview)
{
	return localview.viewbits != 0;
}

/**
 * Adjust local view info of \a ob to be visible if \a v3d is in local view.
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
