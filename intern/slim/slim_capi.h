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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Aurel Gruber
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef slim_capi_h
#define slim_capi_h

#include "slim_matrix_transfer.h"

#ifdef __cplusplus
extern "C" {
#endif

void SLIM_parametrize(SLIMMatrixTransfer *mt, int n_iterations, bool are_border_vertices_pinned, bool skip_initialization);
void SLIM_transfer_uvs_blended(SLIMMatrixTransfer *mt, void *slim, int uv_chart_index, float blend);
void SLIM_parametrize_single_iteration(void *slim);
void* SLIM_setup(SLIMMatrixTransfer *mt, int uv_chart_index, bool are_border_vertices_pinned, bool skip_initialization);
void SLIM_free_data(void* slim_data_ptr);

#ifdef __cplusplus
}
#endif

#endif /* slim_capi_h */
