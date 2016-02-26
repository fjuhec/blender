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

/** \file blender/windowmanager/widgets/intern/widget_library/widget_library_utils.c
 *  \ingroup wm
 *
 * \name Widget Library Utilities
 *
 * \brief This file contains functions for common behaviors of widgets.
 */

#include "BKE_context.h"

#include "RNA_access.h"

#include "WM_api.h"

/* own includes */
#include "WM_widget_types.h"
#include "wm_widget_wmapi.h"
#include "widget_library_intern.h"

/* factor for precision tweaking */
#define WIDGET_PRECISION_FAC 0.05f


BLI_INLINE float widget_offset_from_value_constr(
        const float range_fac, const float min, const float range, const float value,
        const bool inverted)
{
	return inverted ? (range_fac * (min + range - value) / range) : (range_fac * (value / range));
}

BLI_INLINE float widget_value_from_offset_constr(
        const float range_fac, const float min, const float range, const float value,
        const bool inverted)
{
	return inverted ? (min + range - (value * range / range_fac)) : (value * range / range_fac);
}

float widget_offset_from_value(WidgetCommonData *data, const float value, const bool constrained, const bool inverted)
{
	if (constrained)
		return widget_offset_from_value_constr(data->range_fac, data->min, data->range, value, inverted);

	return value;
}

float widget_value_from_offset(
        WidgetCommonData *data, WidgetInteraction *inter, const float offset,
        const bool constrained, const bool inverted, const bool use_precision)
{
	const float max = data->min + data->range;

	if (use_precision) {
		/* add delta offset of this step to total precision_offset */
		inter->precision_offset += offset - inter->prev_offset;
	}
	inter->prev_offset = offset;

	float ofs_new = inter->init_offset + offset - inter->precision_offset * (1.0f - WIDGET_PRECISION_FAC);
	float value;

	if (constrained) {
		value = widget_value_from_offset_constr(data->range_fac, data->min, data->range, ofs_new, inverted);
	}
	else {
		value = ofs_new;
	}

	/* clamp to custom range */
	if (data->flag & WIDGET_CUSTOM_RANGE_SET) {
		CLAMP(value, data->min, max);
	}

	return value;
}

void widget_property_bind(
        wmWidget *widget, WidgetCommonData *data, const int slot,
        const bool constrained, const bool inverted)
{
	if (!widget->props[slot]) {
		data->offset = 0.0f;
		return;
	}

	PointerRNA ptr = widget->ptr[slot];
	PropertyRNA *prop = widget->props[slot];
	float value = widget_property_value_get(widget, slot);

	if (constrained) {
		if ((data->flag & WIDGET_CUSTOM_RANGE_SET) == 0) {
			float step, precision;
			float min, max;
			RNA_property_float_ui_range(&ptr, prop, &min, &max, &step, &precision);
			data->range = max - min;
			data->min = min;
		}
		data->offset = widget_offset_from_value_constr(data->range_fac, data->min, data->range, value, inverted);
	}
	else {
		data->offset = value;
	}
}

void widget_property_value_set(bContext *C, const wmWidget *widget, const int slot, const float value)
{
	PointerRNA ptr = widget->ptr[slot];
	PropertyRNA *prop = widget->props[slot];

	/* reset property */
	RNA_property_float_set(&ptr, prop, value);
	RNA_property_update(C, &ptr, prop);
}

float widget_property_value_get(const wmWidget *widget, const int slot)
{
	BLI_assert(RNA_property_type(widget->props[slot]) == PROP_FLOAT);
	return RNA_property_float_get(&widget->ptr[slot], widget->props[slot]);
}

void widget_property_value_reset(bContext *C, const wmWidget *widget, WidgetInteraction *inter, const int slot)
{
	widget_property_value_set(C, widget, slot, inter->init_value);
}


/* -------------------------------------------------------------------- */

/* TODO use everywhere */
float *widget_color_get(wmWidget *widget, const bool highlight)
{
	return (highlight && !(widget->flag & WM_WIDGET_DRAW_HOVER)) ? widget->col_hi : widget->col;
}
