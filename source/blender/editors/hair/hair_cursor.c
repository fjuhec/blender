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

/** \file blender/editors/hair/hair_cursor.c
 *  \ingroup edhair
 */

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"
#include "BLI_math.h"

#include "DNA_brush_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"

#include "BKE_brush.h"
#include "BKE_context.h"

#include "GPU_immediate.h"
#include "GPU_immediate_util.h"

#include "WM_api.h"
#include "WM_types.h"

#include "BIF_gl.h"
#include "BIF_glutil.h"

#include "ED_view3d.h"

#include "hair_intern.h"

static void hair_draw_cursor(bContext *C, int x, int y, void *UNUSED(customdata))
{
	Scene *scene = CTX_data_scene(C);
	UnifiedPaintSettings *ups = &scene->toolsettings->unified_paint_settings;
	HairEditSettings *settings = &scene->toolsettings->hair_edit;
	Brush *brush = settings->brush;
	if (!brush)
		return;

	float final_radius = BKE_brush_size_get(scene, brush);
	float col[4];

	/* set various defaults */
	copy_v3_v3(col, brush->add_col);
	col[3] = 0.5f;
	
	Gwn_VertFormat *format = immVertexFormat();
	uint pos = GWN_vertformat_attr_add(format, "pos", GWN_COMP_F32, 2, GWN_FETCH_FLOAT);

	immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

	immUniformColor4fv(col);

	/* draw an inner brush */
	if (ups->stroke_active && BKE_brush_use_size_pressure(scene, brush)) {
		/* inner at full alpha */
		imm_draw_circle_wire(pos, x, y, final_radius * ups->size_pressure_value, 40);
		/* outer at half alpha */
		immUniformColor3fvAlpha(col, col[3]*0.5f);
	}
	imm_draw_circle_wire(pos, x, y, final_radius, 40);

	immUnbindProgram();
}

void hair_edit_cursor_start(bContext *C, int (*poll)(bContext *C))
{
	Scene *scene = CTX_data_scene(C);
	HairEditSettings *settings = &scene->toolsettings->hair_edit;
	
	if (!settings->paint_cursor)
		settings->paint_cursor = WM_paint_cursor_activate(CTX_wm_manager(C), poll, hair_draw_cursor, NULL);
}
