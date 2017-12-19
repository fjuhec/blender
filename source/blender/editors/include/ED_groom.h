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

/** \file ED_groom.h
 *  \ingroup editors
 */

#ifndef __ED_GROOM_H__
#define __ED_GROOM_H__

struct bContext;
struct Groom;
struct Object;
struct wmOperator;
struct wmKeyConfig;
struct EditGroom;

/* groom_ops.c */
void    ED_operatortypes_groom(void);
void    ED_operatormacros_groom(void);
void    ED_keymap_groom(struct wmKeyConfig *keyconf);

/* editgroom.c */
void    undo_push_groom(struct bContext *C, const char *name);
ListBase *object_editgroom_get(struct Object *ob);

void ED_groom_editgroom_load(struct Object *obedit);
void ED_groom_editgroom_make(struct Object *obedit);
void ED_groom_editgroom_free(struct Object *obedit);

#endif /* __ED_GROOM_H__ */
