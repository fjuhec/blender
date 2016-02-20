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
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/widgets/WM_widget_library.h
 *  \ingroup wm
 *
 * \name Generic Widget Library
 *
 * Only included in WM_api.h.
 */


#ifndef __WM_WIDGET_LIBRARY_H__
#define __WM_WIDGET_LIBRARY_H__

struct wmWidgetGroup;


/* -------------------------------------------------------------------- */
/* Arrow Widget */

enum {
	WIDGET_ARROW_STYLE_NORMAL        =  1,
	WIDGET_ARROW_STYLE_NO_AXIS       = (1 << 1),
	WIDGET_ARROW_STYLE_CROSS         = (1 << 2),
	WIDGET_ARROW_STYLE_INVERTED      = (1 << 3), /* inverted offset during interaction - if set it also sets constrained below */
	WIDGET_ARROW_STYLE_CONSTRAINED   = (1 << 4), /* clamp arrow interaction to property width */
	WIDGET_ARROW_STYLE_BOX           = (1 << 5), /* use a box for the arrowhead */
	WIDGET_ARROW_STYLE_CONE          = (1 << 6),
};

/* slots for properties */
enum {
	ARROW_SLOT_OFFSET_WORLD_SPACE = 0
};

struct wmWidget *WIDGET_arrow_new(struct wmWidgetGroup *wgroup, const char *name, const int style);
void WIDGET_arrow_set_direction(struct wmWidget *widget, const float direction[3]);
void WIDGET_arrow_set_up_vector(struct wmWidget *widget, const float direction[3]);
void WIDGET_arrow_set_line_len(struct wmWidget *widget, const float len);
void WIDGET_arrow_set_ui_range(struct wmWidget *widget, const float min, const float max);
void WIDGET_arrow_set_range_fac(struct wmWidget *widget, const float range_fac);
void WIDGET_arrow_cone_set_aspect(struct wmWidget *widget, const float aspect[2]);


/* -------------------------------------------------------------------- */
/* Cage Widget */

enum {
	WIDGET_RECT_TRANSFORM_STYLE_TRANSLATE       =  1,       /* widget translates */
	WIDGET_RECT_TRANSFORM_STYLE_ROTATE          = (1 << 1), /* widget rotates */
	WIDGET_RECT_TRANSFORM_STYLE_SCALE           = (1 << 2), /* widget scales */
	WIDGET_RECT_TRANSFORM_STYLE_SCALE_UNIFORM   = (1 << 3), /* widget scales uniformly */
};

enum {
	RECT_TRANSFORM_SLOT_OFFSET = 0,
	RECT_TRANSFORM_SLOT_SCALE = 1
};

struct wmWidget *WIDGET_rect_transform_new(
        struct wmWidgetGroup *wgroup, const char *name, const int style,
        const float width, const float height);


/* -------------------------------------------------------------------- */
/* Dial Widget */

enum {
	WIDGET_DIAL_STYLE_RING = 0,
	WIDGET_DIAL_STYLE_RING_CLIPPED = 1,
	WIDGET_DIAL_STYLE_RING_FILLED = 2,
};

struct wmWidget *WIDGET_dial_new(struct wmWidgetGroup *wgroup, const char *name, const int style);
void WIDGET_dial_set_up_vector(struct wmWidget *widget, const float direction[3]);


/* -------------------------------------------------------------------- */
/* Facemap Widget */

struct wmWidget *WIDGET_facemap_new(
        struct wmWidgetGroup *wgroup, const char *name, const int style,
        struct Object *ob, const int facemap);
struct bFaceMap *WIDGET_facemap_get_fmap(struct wmWidget *widget);


/* -------------------------------------------------------------------- */
/* Primitive Widget */

enum {
	WIDGET_PRIMITIVE_STYLE_PLANE = 0,
};

struct wmWidget *WIDGET_primitive_new(struct wmWidgetGroup *wgroup, const char *name, const int style);
void WIDGET_primitive_set_direction(struct wmWidget *widget, const float direction[3]);
void WIDGET_primitive_set_up_vector(struct wmWidget *widget, const float direction[3]);

#endif  /* __WM_WIDGET_LIBRARY_H__ */

