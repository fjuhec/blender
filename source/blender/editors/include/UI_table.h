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

/** \file UI_table.h
 *  \ingroup editorui
 */

#ifndef __UI_TABLE_H__
#define __UI_TABLE_H__

#include "BLI_compiler_attrs.h"

struct rcti;
struct uiBlock;
struct uiLayout;
struct uiStyle;

typedef struct uiTable uiTable;
typedef struct uiTableColumn uiTableColumn;
typedef struct uiTableRow uiTableRow;
typedef struct uiTableSize uiTableSize;

typedef void (*uiTableCellDrawFunc)(struct uiLayout *layout, void *rowdata, struct rcti drawrect);

enum uiTableColumnAlignemt {
	TABLE_COLUMN_ALIGN_LEFT,
	TABLE_COLUMN_ALIGN_RIGHT,
};

enum uiTableUnit {
	TABLE_UNIT_PX,
	TABLE_UNIT_PERCENT,
};


uiTable *UI_table_horizontal_flow_create(void) ATTR_WARN_UNUSED_RESULT;
uiTable *UI_table_vertical_flow_create(void) ATTR_WARN_UNUSED_RESULT;
void UI_table_free(uiTable *table) ATTR_NONNULL();

void UI_table_max_width_set(uiTable *table, const unsigned int max_width) ATTR_NONNULL();
void UI_table_horizontal_flow_max_height_set(uiTable *table, const unsigned int max_height) ATTR_NONNULL();
void UI_table_background_colors_set(uiTable *table, const unsigned char rgb1[3], const unsigned char rgb2[3]);
void UI_table_draw(uiTable *table, struct uiBlock *block, struct uiStyle *style) ATTR_NONNULL(1);

/* *** Columns *** */
uiTableColumn *UI_table_column_add(uiTable *table, const char *idname, const char *drawname,
                                   uiTableCellDrawFunc cell_draw) ATTR_NONNULL(1, 2);
void UI_table_column_remove(uiTable *table, uiTableColumn *column) ATTR_NONNULL();
uiTableColumn *UI_table_column_lookup(uiTable *table, const char *idname) ATTR_NONNULL() ATTR_WARN_UNUSED_RESULT;
void UI_table_column_width_set(uiTableColumn *column, const unsigned int width, enum uiTableUnit unit,
                               const int min_width_px) ATTR_NONNULL();
void UI_table_column_alignment_set(uiTableColumn *column, enum uiTableColumnAlignemt alignment) ATTR_NONNULL();
/* *** Rows *** */
uiTableRow *UI_table_row_add(uiTable *table, void *rowdata) ATTR_NONNULL(1);
void UI_table_row_height_set(uiTable *table, uiTableRow *row, unsigned int height) ATTR_NONNULL();

unsigned int UI_table_get_rowcount(const uiTable *table);

#endif /* __UI_TABLE_H__ */
