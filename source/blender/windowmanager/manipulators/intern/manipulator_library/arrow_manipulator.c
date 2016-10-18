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

#include "GPU_select.h"

#include "MEM_guardedalloc.h"

#include "RNA_types.h"

#include "manipulator_library_intern.h"
#include "WM_manipulator_types.h"
#include "wm_manipulator_wmapi.h"
#include "wm_manipulator_intern.h"
#include "WM_manipulator_library.h"

typedef struct ArrowManipulator {
	wmManipulator manipulator;

	float direction[3];
} ArrowManipulator;


static void arrow_draw_geom(const ArrowManipulator *arrow, const bool UNUSED(select))
{
	const float len = 1.0f; /* TODO arrow->len */
	const float vec[2][3] = {
		{0.0f, 0.0f, 0.0f},
		{0.0f, 0.0f, len},
	};

	glLineWidth(arrow->manipulator.line_width);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, vec);
	glDrawArrays(GL_LINE_STRIP, 0, ARRAY_SIZE(vec));
	glDisableClientState(GL_VERTEX_ARRAY);
	glLineWidth(1.0);


	/* *** draw arrow head *** */

	glPushMatrix();

	const float head_len = 0.25f;
	const float width = 0.06f;
	const bool use_lighting = /*select == false && ((U.manipulator_flag & V3D_SHADED_MANIPULATORS) != 0)*/ false;

	/* translate to line end */
	glTranslatef(0.0f, 0.0f, len);

	if (use_lighting) {
		glShadeModel(GL_SMOOTH);
	}

	GLUquadricObj *qobj = gluNewQuadric();
	gluQuadricDrawStyle(qobj, GLU_FILL);
	gluQuadricOrientation(qobj, GLU_INSIDE);
	gluDisk(qobj, 0.0, width, 8, 1);
	gluQuadricOrientation(qobj, GLU_OUTSIDE);
	gluCylinder(qobj, width, 0.0, head_len, 8, 1);
	gluDeleteQuadric(qobj);

	if (use_lighting) {
		glShadeModel(GL_FLAT);
	}

	glPopMatrix();
}

static void arrow_draw_intern(ArrowManipulator *arrow, const bool select, const bool highlight)
{
	const float up[3] = {0.0f, 0.0f, 1.0f};
	float col[4];
	float rot[3][3];
	float mat[4][4];

	manipulator_color_get(&arrow->manipulator, highlight, col);

	rotation_between_vecs_to_mat3(rot, up, arrow->direction);

	copy_m4_m3(mat, rot);
	copy_v3_v3(mat[3], arrow->manipulator.origin);

	glPushMatrix();
	glMultMatrixf(mat);

	glColor4fv(col);
	glEnable(GL_BLEND);
	glTranslate3fv(arrow->manipulator.offset);
	arrow_draw_geom(arrow, select);
	glDisable(GL_BLEND);

	glPopMatrix();
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

/* -------------------------------------------------------------------- */
/** \name Arrow Manipulator API
 *
 * \{ */

wmManipulator *WM_arrow_manipulator_new(wmManipulatorGroup *mgroup, const char *idname)
{
	ArrowManipulator *arrow = MEM_callocN(sizeof(*arrow), __func__);

	arrow->manipulator.draw = arrow_manipulator_draw;
	arrow->manipulator.render_3d_intersection = arrow_manipulator_render_3d_intersect;

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
