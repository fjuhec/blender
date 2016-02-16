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
 * The Original Code is Copyright (C) 2016 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/widgets/intern/widget_library/widget_library_intern.h
 *  \ingroup wm
 */


#ifndef __WIDGET_LIBRARY_INTERN_H__
#define __WIDGET_LIBRARY_INTERN_H__

/**
 * Data for common interactions. Used in widget_library_utils.c functions.
 */
typedef struct WidgetCommonData {
	int flag;

	float range_fac;      /* factor for arrow min/max distance */
	float offset;

	/* property range for constrained widgets */
	float range;
	/* min/max value for constrained widgets */
	float min, max;
} WidgetCommonData;

typedef struct WidgetInteraction {
	float init_value; /* initial property value */
	float init_origin[3];
	float init_mval[2];
	float init_offset;
	float init_scale;

	/* offset of last handling step */
	float prev_offset;
	/* Total offset added by precision tweaking.
	 * Needed to allow toggling precision on/off without causing jumps */
	float precision_offset;
} WidgetInteraction;

/* WidgetCommonData->flag  */
enum {
	WIDGET_CUSTOM_RANGE_SET = (1 << 0),
};


float widget_offset_from_value_constrained_float(
        const float range_fac, const float min, const float range, const float value,
        const bool inverted);
float widget_value_from_offset_constrained_float(
        const float range_fac, const float min, const float range, const float value,
        const bool inverted);
float widget_value_from_offset_float(
        WidgetCommonData *data, WidgetInteraction *inter, const float offset,
        const bool constrained, const bool inverted, const bool use_precision);

void widget_bind_to_prop_float(
        wmWidget *widget, WidgetCommonData *data, const int slot,
        const bool constrained, const bool inverted);

void  widget_property_set_float(bContext *C, const wmWidget *widget, const int slot, const float value);
float widget_property_get_float(const wmWidget *widget, const int slot);

void widget_reset_float(bContext *C, const wmWidget *widget, WidgetInteraction *inter, const int slot);

#endif  /* __WIDGET_LIBRARY_INTERN_H__ */

