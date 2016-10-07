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
#include "BLI_compiler_compat.h"

struct LocalViewInfo;
struct Object;
struct View3D;

/* Forcing inline as some of these are called a lot, mostly in loops even. */

BLI_INLINE bool BKE_localview_info_cmp(struct LocalViewInfo a, struct LocalViewInfo b) ATTR_WARN_UNUSED_RESULT;
BLI_INLINE bool BKE_localview_is_valid(struct LocalViewInfo localview) ATTR_WARN_UNUSED_RESULT;

BLI_INLINE void BKE_localview_object_assign(struct View3D *v3d, struct Object *ob) ATTR_NONNULL();
BLI_INLINE void BKE_localview_object_unassign(struct View3D *v3d, struct Object *ob) ATTR_NONNULL();

#include "intern/localview.c"

#endif /* __BKE_LOCALVIEW_H__ */
