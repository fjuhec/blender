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

/** \file blender/windowmanager/manipulators/intern/manipulator_library/manipulator_library_presets.c
 *  \ingroup wm
 *
 * \name Manipulator Lib Presets
 *
 * \brief Preset shapes that can be drawn from any manipulator type.
 */

#include "BIF_gl.h"

#include "BKE_context.h"

#include "BLI_math.h"

#include "DNA_manipulator_types.h"
#include "DNA_view3d_types.h"

#include "ED_view3d.h"
#include "ED_screen.h"

#include "GPU_draw.h"
#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_matrix.h"
#include "GPU_select.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"

#include "WM_types.h"
#include "WM_api.h"

/* own includes */
#include "wm_manipulator_wmapi.h"
#include "wm_manipulator_intern.h"
#include "manipulator_geometry.h"
#include "manipulator_library_intern.h"

void WM_manipulator_draw_preset_box(
        const struct wmManipulator *mpr, float mat[4][4], int select_id)
{
	const bool is_select = (select_id != -1);
	const bool is_highlight = is_select && (mpr->state & WM_MANIPULATOR_HIGHLIGHT) != 0;

	float color[4];
	manipulator_color_get(mpr, is_highlight, color);

	if (is_select) {
		GPU_select_load_id(select_id);
	}

	gpuPushMatrix();
	gpuMultMatrix(mat);
	wm_manipulator_geometryinfo_draw(&wm_manipulator_geom_data_cube, is_select, color);
	gpuPopMatrix();

	if (is_select) {
		GPU_select_load_id(-1);
	}
}
