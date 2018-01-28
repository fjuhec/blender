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

/** \file blender/editors/groom/groom_intern.h
 *  \ingroup edgroom
 */


#ifndef __GROOM_INTERN_H__
#define __GROOM_INTERN_H__

struct wmOperatorType;

/* editgroom_region.c */
void GROOM_OT_region_add(struct wmOperatorType *ot);
void GROOM_OT_region_bind(struct wmOperatorType *ot);

/* editgroom_select.c */
void GROOM_OT_select_all(struct wmOperatorType *ot);

/* groom_hair.c */
void GROOM_OT_hair_distribute(struct wmOperatorType *ot);

#endif /* __GROOM_INTERN_H__ */
