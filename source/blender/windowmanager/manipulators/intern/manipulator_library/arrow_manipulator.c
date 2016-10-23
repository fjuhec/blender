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

/** \file blender/windowmanager/manipulators/intern/manipulator_library/arrow_manipulator.c
 *  \ingroup wm
 */

#include "BIF_gl.h"

#include "BKE_context.h"

#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "DNA_defs.h"
#include "DNA_listBase.h"
#include "DNA_userdef_types.h"
#include "DNA_windowmanager_types.h"

#include "GPU_immediate.h"
#include "GPU_select.h"

#include "MEM_guardedalloc.h"

#include "RNA_types.h"

#include "WM_types.h"

#include "manipulator_geometry.h"
#include "manipulator_library_intern.h"
#include "WM_manipulator_types.h"
#include "wm_manipulator_wmapi.h"
#include "wm_manipulator_intern.h"
#include "WM_manipulator_library.h"

typedef struct ArrowManipulator {
	wmManipulator manipulator;

	int style;

	float direction[3];
} ArrowManipulator;


static void arrow_draw_line(const ArrowManipulator *arrow, const float col[4], const float len)
{
	VertexFormat *format = immVertexFormat();
	unsigned int pos = add_attrib(format, "pos", GL_FLOAT, 3, KEEP_FLOAT);

	immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
	immUniformColor4fv(col);

	glLineWidth(arrow->manipulator.line_width);

	immBegin(GL_LINES, 2);
	immVertex3f(pos, 0.0f, 0.0f, 0.0f);
	immVertex3f(pos, 0.0f, 0.0f, len);
	immEnd();

	immUnbindProgram();
}

static void arrow_draw_head_cone(const float col[4], const bool select)
{
	const ManipulatorGeometryInfo cone_geo = {
		_MANIPULATOR_nverts_cone,
		_MANIPULATOR_ntris_cone,
		_MANIPULATOR_verts_cone,
		_MANIPULATOR_normals_cone,
		_MANIPULATOR_indices_cone,
		true,
	};
	const float scale = 0.25f;

	glColor4fv(col);
	glScalef(scale, scale, scale);
	wm_manipulator_geometryinfo_draw(&cone_geo, select);
}

static void arrow_draw_head_cube(const float col[4], const bool select)
{
	const ManipulatorGeometryInfo cone_geo = {
		_MANIPULATOR_nverts_cube,
		_MANIPULATOR_ntris_cube,
		_MANIPULATOR_verts_cube,
		_MANIPULATOR_normals_cube,
		_MANIPULATOR_indices_cube,
		true,
	};
	const float scale = 0.05f;

	glColor4fv(col);
	glScalef(scale, scale, scale);
	glTranslatef(0.0f, 0.0f, 1.0f); /* Cube origin is at its center, needs this offset to not overlap with line. */
	wm_manipulator_geometryinfo_draw(&cone_geo, select);
}

static void arrow_draw_head(const ArrowManipulator *arrow, const float col[4], const bool select)
{
	switch (arrow->style) {
		case MANIPULATOR_ARROW_STYLE_CONE:
			arrow_draw_head_cone(col, select);
			break;
		case MANIPULATOR_ARROW_STYLE_CUBE:
			arrow_draw_head_cube(col, select);
			break;
	}
}

static void arrow_draw_geom(const ArrowManipulator *arrow, const float col[4], const bool select)
{
	const float len = 1.0f; /* TODO arrow->len */
	const bool use_lighting = /*select == false && ((U.manipulator_flag & V3D_SHADED_MANIPULATORS) != 0)*/ false;

	glTranslate3fv(arrow->manipulator.offset);

	arrow_draw_line(arrow, col, len);

	/* *** draw arrow head *** */

	/* translate to line end */
	glPushMatrix();
	glTranslatef(0.0f, 0.0f, len);

	if (use_lighting) {
		glShadeModel(GL_SMOOTH);
	}

	arrow_draw_head(arrow, col, select);

	if (use_lighting) {
		glShadeModel(GL_FLAT);
	}
	glPopMatrix();
}

static void arrow_get_matrix(const ArrowManipulator *arrow, float r_rot[3][3], float r_mat[4][4])
{
	const float up[3] = {0.0f, 0.0f, 1.0f};

	rotation_between_vecs_to_mat3(r_rot, up, arrow->direction);

	copy_m4_m3(r_mat, r_rot);
	copy_v3_v3(r_mat[3], arrow->manipulator.origin);
	mul_mat3_m4_fl(r_mat, arrow->manipulator.scale);
}

static void arrow_draw_intern(const ArrowManipulator *arrow, const bool select, const bool highlight)
{
	float col[4];
	float rot[3][3];
	float mat[4][4];

	manipulator_color_get(&arrow->manipulator, highlight, col);
	arrow_get_matrix(arrow, rot, mat);

	glPushMatrix();
	glMultMatrixf(mat);

	glEnable(GL_BLEND);
	arrow_draw_geom(arrow, col, select);
	glDisable(GL_BLEND);

	glPopMatrix();

	if (arrow->manipulator.interaction_data) {
		ManipulatorInteraction *inter = arrow->manipulator.interaction_data;
		const float ghost_col[] = {0.5f, 0.5f, 0.5f, 0.5f};

		copy_m4_m3(mat, rot);
		copy_v3_v3(mat[3], inter->init_origin);
		mul_mat3_m4_fl(mat, inter->init_scale);

		glPushMatrix();
		glMultMatrixf(mat);

		glEnable(GL_BLEND);
		arrow_draw_geom(arrow, ghost_col, select);
		glDisable(GL_BLEND);

		glPopMatrix();
	}
}

static void arrow_manipulator_render_3d_intersect(
        const bContext *UNUSED(C), wmManipulator *manipulator,
        int selectionbase)
{
	GPU_select_load_id(selectionbase);
	arrow_draw_intern((ArrowManipulator *)manipulator, true, false);
}

static void arrow_manipulator_draw(const bContext *UNUSED(C), wmManipulator *manipulator)
{
	arrow_draw_intern((ArrowManipulator *)manipulator, false, (manipulator->state & WM_MANIPULATOR_HIGHLIGHT) != 0);
}

static int manipulator_arrow_invoke(bContext *UNUSED(C), const wmEvent *UNUSED(event), wmManipulator *manipulator)
{
	ManipulatorInteraction *inter = MEM_callocN(sizeof(ManipulatorInteraction), __func__);

	inter->init_scale = manipulator->scale;
//	manipulator_arrow_get_final_pos(manipulator, inter->init_origin);
	copy_v3_v3(inter->init_origin, manipulator->origin);

	manipulator->interaction_data = inter;

	return OPERATOR_RUNNING_MODAL;
}

/* -------------------------------------------------------------------- */
/** \name Arrow Manipulator API
 *
 * \{ */

wmManipulator *WM_arrow_manipulator_new(
		wmManipulatorGroup *mgroup, const char *idname, const enum ArrowManipulatorStyle style)
{
	ArrowManipulator *arrow = MEM_callocN(sizeof(*arrow), __func__);

	arrow->manipulator.draw = arrow_manipulator_draw;
	arrow->manipulator.render_3d_intersection = arrow_manipulator_render_3d_intersect;
	arrow->manipulator.invoke = manipulator_arrow_invoke;
	arrow->manipulator.flag |= WM_MANIPULATOR_DRAW_ACTIVE;

	arrow->style = style;

	wm_manipulator_register(mgroup, &arrow->manipulator, idname);

	return &arrow->manipulator;
}

/**
 * Define direction the arrow will point towards
 */
void WM_arrow_manipulator_set_direction(wmManipulator *manipulator, const float direction[3])
{
	ArrowManipulator *arrow = (ArrowManipulator *)manipulator;

	copy_v3_v3(arrow->direction, direction);
	normalize_v3(arrow->direction);
}

/** \} */ /* Arrow Manipulator API */


/* -------------------------------------------------------------------- */

void fix_linking_manipulator_arrow(void)
{
	(void)0;
}
