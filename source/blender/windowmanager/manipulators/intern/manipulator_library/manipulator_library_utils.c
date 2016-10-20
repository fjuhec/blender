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
 * The Original Code is Copyright (C) 2015 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/manipulators/intern/manipulator_library/manipulator_library_utils.c
 *  \ingroup wm
 *
 * \name Manipulator Library Utilities
 *
 * \brief This file contains functions for common behaviors of manipulators.
 */

#include "BIF_gl.h"

#include "BKE_context.h"

#include "BLI_math.h"

#include "RNA_access.h"

#include "WM_api.h"

/* own includes */
#include "WM_manipulator_types.h"
#include "wm_manipulator_wmapi.h"
#include "wm_manipulator_intern.h"
#include "manipulator_library_intern.h"

/* factor for precision tweaking */
#define MANIPULATOR_PRECISION_FAC 0.05f

/* -------------------------------------------------------------------- */
/* Manipulator drawing */

/**
 * Main draw call for ManipulatorGeometryInfo data.
 */
void wm_manipulator_geometryinfo_draw(const ManipulatorGeometryInfo *info, const bool UNUSED(select))
{
	GLuint buf[2];

	glGenBuffers(2, buf);

	/* vertex buffer */
	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, buf[0]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * info->nverts, info->verts, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

	/* normal buffer */
	/* TODO normal buffer for lighting (if we need this?) */

	/* index buffer */
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[1]);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned short) * (3 * info->ntris), info->indices, GL_STATIC_DRAW);

	glEnable(GL_CULL_FACE);
//	glEnable(GL_DEPTH_TEST);

	glDrawElements(GL_TRIANGLES, info->ntris * 3, GL_UNSIGNED_SHORT, NULL);

//	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	glDisableVertexAttribArray(0);
	glDeleteBuffers(2, buf);
}


/* -------------------------------------------------------------------- */
/* Manipulator handling */

BLI_INLINE float manipulator_offset_from_value_constr(
        const float range_fac, const float min, const float range, const float value,
        const bool inverted)
{
	return inverted ? (range_fac * (min + range - value) / range) : (range_fac * (value / range));
}

BLI_INLINE float manipulator_value_from_offset_constr(
        const float range_fac, const float min, const float range, const float value,
        const bool inverted)
{
	return inverted ? (min + range - (value * range / range_fac)) : (value * range / range_fac);
}

float manipulator_offset_from_value(
        ManipulatorCommonData *data, const float value, const bool constrained, const bool inverted)
{
	if (constrained)
		return manipulator_offset_from_value_constr(data->range_fac, data->min, data->range, value, inverted);

	return value;
}

float manipulator_value_from_offset(
        ManipulatorCommonData *data, ManipulatorInteraction *inter, const float offset,
        const bool constrained, const bool inverted, const bool use_precision)
{
	const float max = data->min + data->range;

	if (use_precision) {
		/* add delta offset of this step to total precision_offset */
		inter->precision_offset += offset - inter->prev_offset;
	}
	inter->prev_offset = offset;

	float ofs_new = inter->init_offset + offset - inter->precision_offset * (1.0f - MANIPULATOR_PRECISION_FAC);
	float value;

	if (constrained) {
		value = manipulator_value_from_offset_constr(data->range_fac, data->min, data->range, ofs_new, inverted);
	}
	else {
		value = ofs_new;
	}

	/* clamp to custom range */
	if (data->flag & MANIPULATOR_CUSTOM_RANGE_SET) {
		CLAMP(value, data->min, max);
	}

	return value;
}

void manipulator_property_data_update(
        wmManipulator *manipulator, ManipulatorCommonData *data, const int slot,
        const bool constrained, const bool inverted)
{
	if (!manipulator->props[slot]) {
		data->offset = 0.0f;
		return;
	}

	PointerRNA ptr = manipulator->ptr[slot];
	PropertyRNA *prop = manipulator->props[slot];
	float value = manipulator_property_value_get(manipulator, slot);

	if (constrained) {
		if ((data->flag & MANIPULATOR_CUSTOM_RANGE_SET) == 0) {
			float step, precision;
			float min, max;
			RNA_property_float_ui_range(&ptr, prop, &min, &max, &step, &precision);
			data->range = max - min;
			data->min = min;
		}
		data->offset = manipulator_offset_from_value_constr(data->range_fac, data->min, data->range, value, inverted);
	}
	else {
		data->offset = value;
	}
}

void manipulator_property_value_set(
        bContext *C, const wmManipulator *manipulator,
        const int slot, const float value)
{
	PointerRNA ptr = manipulator->ptr[slot];
	PropertyRNA *prop = manipulator->props[slot];

	/* reset property */
	RNA_property_float_set(&ptr, prop, value);
	RNA_property_update(C, &ptr, prop);
}

float manipulator_property_value_get(const wmManipulator *manipulator, const int slot)
{
	BLI_assert(RNA_property_type(manipulator->props[slot]) == PROP_FLOAT);
	return RNA_property_float_get(&manipulator->ptr[slot], manipulator->props[slot]);
}

void manipulator_property_value_reset(
        bContext *C, const wmManipulator *manipulator, ManipulatorInteraction *inter,
        const int slot)
{
	manipulator_property_value_set(C, manipulator, slot, inter->init_value);
}


/* -------------------------------------------------------------------- */

void manipulator_color_get(
        const wmManipulator *manipulator, const bool highlight,
        float r_col[4])
{
	if (highlight && !(manipulator->flag & WM_MANIPULATOR_DRAW_HOVER)) {
		copy_v4_v4(r_col, manipulator->col_hi);
	}
	else {
		copy_v4_v4(r_col, manipulator->col);
	}
}
