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
 * The Original Code is Copyright (C) 2016 by Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Bastien Montagne.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __BKE_LIBRARY_OVERRIDE_H__
#define __BKE_LIBRARY_OVERRIDE_H__

/** \file BKE_library_override.h
 *  \ingroup bke
 *  \since December 2016
 *  \author mont29
 */

struct ID;
struct IDOverride;
struct IDOverrideProperty;
struct Main;

struct IDOverride *BKE_override_init(struct ID *local_id, struct ID *reference_id);
void BKE_override_clear(struct IDOverride *override);
void BKE_override_free(struct IDOverride **override);

bool BKE_override_status_check_local(struct ID *local);
bool BKE_override_status_check_reference(struct ID *local);

bool BKE_override_operations_create(struct ID *local);

void BKE_override_update(struct ID *local);
void BKE_main_override_update(struct Main *bmain);


#endif  /* __BKE_LIBRARY_OVERRIDE_H__ */
