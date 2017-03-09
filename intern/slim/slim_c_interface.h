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

#ifndef slim_c_interface_h
#define slim_c_interface_h

#include "matrix_transfer.h"

#ifdef __cplusplus
extern "C" {
#endif


void param_slim_C(matrix_transfer *mt, int nIterations, bool borderVerticesArePinned, bool skipInitialization);
void transfer_uvs_blended_C(matrix_transfer *mt, void* slim, int chartNr, float blend);
void param_slim_single_iteration_C(void *slim);
void* setup_slim_C(matrix_transfer *mt, int chartNr, bool fixBorder, bool skipInitialization);
void free_slim_data_C(void* slimDataPtr);

#ifdef __cplusplus
}
#endif

#endif /* slim_C_interface_hpp */
