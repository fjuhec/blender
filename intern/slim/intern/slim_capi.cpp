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

#include <Eigen/Dense>

#include "slim_capi.h"
#include "slim_parametrizer.h"
#include "slim.h"

#include "area_compensation.h"

#include <igl/Timer.h>


using namespace igl;

/*  Called from the native part during each iteration of interactive parametrisation.
	The blend parameter decides the linear blending between the original UV map and the one
	optained from the accumulated SLIM iterations so far.
 */
void SLIM_transfer_uvs_blended(SLIMMatrixTransfer *mt, void* slim_data_ptr, int uv_chart_index, float blend){
	SLIMData* slim_data = (SLIMData*) slim_data_ptr;
	Eigen::MatrixXd blended_uvs = getInteractiveResultBlendedWithOriginal(blend, slim_data);
	areacomp::correctMapSurfaceAreaIfNecessary(slim_data);
	transferUvsBackToNativePart(mt, blended_uvs, uv_chart_index);
}

/*	Setup call from the native C part. Necessary for interactive parametrisation.
 */
void* SLIM_setup(SLIMMatrixTransfer *mt, int uv_chart_index, bool are_border_vertices_pinned, bool skip_initialization){
	igl::Timer timer;
	timer.start();
	SLIMData* slim_data = setup_slim(mt, 0, uv_chart_index, timer, are_border_vertices_pinned, skip_initialization);
	return slim_data;
}

/*	Executes a single iteration of SLIM, to be called from the native part. It recasts the pointer to a SLIM object.
 */
void SLIM_parametrize_single_iteration(void* slim_data_ptr){
	SLIMData* slim_data = (SLIMData*) slim_data_ptr;
	param_slim_single_iteration(slim_data);
}

void SLIM_parametrize(SLIMMatrixTransfer *mt, int n_iterations, bool are_border_vertices_pinned, bool skip_initialization){
	param_slim(mt, n_iterations, are_border_vertices_pinned, skip_initialization);
}

void SLIM_free_data(void* slim_data_ptr){
	free_slim_data((SLIMData*) slim_data_ptr);
}
