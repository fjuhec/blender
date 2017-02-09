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

/** \file blender/editors/interface/table.c
 *  \ingroup edinterface
 */

#include <limits.h>
#include <string.h>

#include "BLI_alloca.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_mempool.h"
#include "BLI_rect.h"

#include "DNA_userdef_types.h"

#include "GPU_immediate.h"

#include "MEM_guardedalloc.h"

#include "UI_interface.h"
#include "UI_table.h"


typedef struct uiTable {
	BLI_mempool *row_pool;
	ListBase columns;
	unsigned int tot_columns;

	enum {
		TABLE_FLOW_VERTICAL,
		TABLE_FLOW_HORIZONTAL,
	} flow_direction;
	unsigned int max_width;

	unsigned char rgb1[3];
	unsigned char rgb2[3];
	char flag;
} uiTable;

enum eTableFlags {
	/* All rows have the same height. In this case we can avoid iterating
	 * over rows for calculations like intersection checks. */
	TABLE_ROWS_CONSTANT_HEIGHT = (1 << 0),
	TABLE_DRAW_BACKGROUND      = (1 << 1),
};

typedef struct TableHorizontalFlow {
	uiTable table;

	/* if this height is reached, we split the table */
	unsigned int max_height;
} TableHorizontalFlow;

/**
 * This struct allows using either relative or absolute scales
 * for size properties (only column widths right now).
 */
typedef struct uiTableSize {
	enum uiTableUnit unit;
	int value;
} uiTableSize;

typedef struct uiTableRow {
	void *rowdata;

	unsigned int height;
} uiTableRow;

typedef struct uiTableColumn {
	struct uiTableColumn *next, *prev;

	const char *idname;
	const char *drawname;

	uiTableSize width;
	unsigned int min_width;
	enum uiTableColumnAlignemt alignment;

	uiTableCellDrawFunc cell_draw;
} uiTableColumn;


/* -------------------------------------------------------------------- */

struct TableColumnDrawInfo {
	/* While drawing: Total width of the already drawn columns depending on alignment (left of right). */
	unsigned int totwidth_left;
	unsigned int totwidth_right;

	/* Total width of all non-fixed width columns (having size in px instead of percent). */
	unsigned int totwidth_nonfixed;
};

/**
 * Iterate over all columns in \a table. Supports removing columns while iterating.
 */
#define TABLE_COLUMNS_ITER_BEGIN(table, column_name) \
	for (uiTableColumn *column_name = (table)->columns.first, *column_name##_next; \
	     column_name; \
	     column_name = column_name##_next) \
		{ \
			column_name##_next = column_name->next;
#define TABLE_COLUMNS_ITER_END } (void)0


static void table_init(uiTable *table)
{
	table->row_pool = BLI_mempool_create(sizeof(uiTableRow), 0, 64, BLI_MEMPOOL_ALLOW_ITER);
	table->flag |= TABLE_ROWS_CONSTANT_HEIGHT;
}

static void table_column_free(uiTableColumn *column)
{
	MEM_freeN(column);
}

static void table_columns_free(uiTable *table)
{
	TABLE_COLUMNS_ITER_BEGIN(table, column)
	{
		table_column_free(column);
	}
	TABLE_COLUMNS_ITER_END;
}

static void table_row_height_set(uiTable *table, uiTableRow *row, unsigned int height)
{
	row->height = height;

	/* Figure out if the new height breaks the 'every row has the same height' state. If so, unset flag.
	 * To avoid any additional iterations, we just check if we need to set it again on drawing */
	if (table->flag & TABLE_ROWS_CONSTANT_HEIGHT) {
		uiTableRow *row_first = BLI_mempool_findelem(table->row_pool, 0);

		if (row_first->height != row->height) {
			table->flag &= ~TABLE_ROWS_CONSTANT_HEIGHT;
		}
	}
}

static unsigned int table_column_width_clamp(const uiTableColumn *column, const unsigned int maxwidth,
                                             const unsigned int unclamped_width)
{
	unsigned int width = unclamped_width;
	CLAMP(width, column->min_width, maxwidth);
	return width;
}

static unsigned int table_calc_tot_width_unfixed_columns(const uiTable *table)
{
	unsigned int nonfixed_width = table->max_width;

	TABLE_COLUMNS_ITER_BEGIN(table, column)
	{
		if (column->width.unit == TABLE_UNIT_PX) {
			unsigned int width = table_column_width_clamp(column, table->max_width, column->width.value);
			BLI_assert(nonfixed_width >= width);
			nonfixed_width -= width;
		}
	}
	TABLE_COLUMNS_ITER_END;

	return nonfixed_width;
}

static struct TableColumnDrawInfo table_column_drawinfo_init(const uiTable *table)
{
	struct TableColumnDrawInfo drawinfo = {};

	drawinfo.totwidth_nonfixed = table_calc_tot_width_unfixed_columns(table);
	BLI_assert(drawinfo.totwidth_nonfixed <= table->max_width);

	return drawinfo;
}

static unsigned int table_column_calc_width(const uiTableColumn *column, const struct TableColumnDrawInfo *drawinfo,
                                            const unsigned int maxwidth)
{
	unsigned int width = column->width.value;

	if (column->width.unit == TABLE_UNIT_PERCENT) {
		CLAMP_MAX(width, 100); /* more than 100% doesn't make sense */
		width = iroundf(width * 0.01f * drawinfo->totwidth_nonfixed);
	}

	return table_column_width_clamp(column, maxwidth, width);
}

/**
 * Calculate the table-flow relative x-coordinates, meaning we don't account for horizontal-flow
 * yet, the first column aligned to the left will just always be at xmin = 0.
 */
static void table_column_calc_x_coords(const uiTableColumn *column, const float max_width,
                                       struct TableColumnDrawInfo *io_drawinfo,
                                       int *r_xmin, int *r_xmax)
{
	const unsigned int width = table_column_calc_width(column, io_drawinfo, max_width);

	if (column->alignment == TABLE_COLUMN_ALIGN_LEFT) {
		*r_xmin = io_drawinfo->totwidth_left;
		*r_xmax = *r_xmin + width;

		io_drawinfo->totwidth_left += width;
	}
	else {
		BLI_assert(column->alignment == TABLE_COLUMN_ALIGN_RIGHT);
		*r_xmax = max_width - io_drawinfo->totwidth_right;
		*r_xmin = *r_xmax - width;

		io_drawinfo->totwidth_right += width;
	}
}

static void table_row_calc_y_coords(uiTable *table, uiTableRow *row,
                                    unsigned int *io_ofs_x, unsigned int *io_ofs_y,
                                    int *r_ymin, int *r_ymax)
{
	unsigned int height = row->height;

	if (table->flow_direction == TABLE_FLOW_HORIZONTAL) {
		TableHorizontalFlow *horizontal_table = (TableHorizontalFlow *)table;
		CLAMP_MAX(height, horizontal_table->max_height);

		if ((*io_ofs_y + height) > horizontal_table->max_height) {
			*io_ofs_x += table->max_width;
			*io_ofs_y = 0;
		}
	}

	/* Assuming inverted direction, from top to bottom. */
	*r_ymax = -(*io_ofs_y);
	*r_ymin = *r_ymax - height;
}

static void table_row_draw_background(const uiTable *table, const int row_index, const unsigned int height,
                                      const unsigned int ofs_x, const unsigned int ofs_y)
{
	if (table->flag & TABLE_DRAW_BACKGROUND) {
		unsigned int pos = add_attrib(immVertexFormat(), "pos", GL_INT, 2, CONVERT_INT_TO_FLOAT);

		immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);

		immUniformColor3ubv((row_index % 2) ? table->rgb1 : table->rgb2);
		/* remember, drawing is done top to bottom with upper left being (0, 0), use negative y coordinates */
		immRecti(pos, ofs_x, -ofs_y, ofs_x + table->max_width, -(ofs_y + height));

		immUnbindProgram();
	}
}


/* -------------------------------------------------------------------- */
/** \name UI Table API
 *
 * Generic API to define, handle and draw tables with custom data types.
 * Note, this API is purely for UI purposes, not data management.
 *
 * \{ */

uiTable *UI_table_vertical_flow_create(void)
{
	uiTable *table = MEM_callocN(sizeof(*table), __func__);

	table->flow_direction = TABLE_FLOW_VERTICAL;
	table_init(table);

	return table;
}

uiTable *UI_table_horizontal_flow_create(void)
{
	TableHorizontalFlow *horizontal_table = MEM_callocN(sizeof(*horizontal_table), __func__);

	horizontal_table->table.flow_direction = TABLE_FLOW_HORIZONTAL;
	horizontal_table->max_height = UINT_MAX;
	table_init(&horizontal_table->table);

	return &horizontal_table->table;
}

void UI_table_free(uiTable *table)
{
	table_columns_free(table);
	BLI_mempool_destroy(table->row_pool);

	MEM_freeN(table);
}

/**
 * Set the maximum width a table can use.
 * For horizontal-flow that would be the width of each section the table may be split into.
 */
void UI_table_max_width_set(uiTable *table, const unsigned int max_width)
{
	table->max_width = max_width;
}

/**
 * Set the height at which the table would be split;
 */
void UI_table_horizontal_flow_max_height_set(uiTable *table, const unsigned int max_height)
{
	TableHorizontalFlow *horizontal_table = (TableHorizontalFlow *)table;
	BLI_assert(table->flow_direction == TABLE_FLOW_HORIZONTAL);
	horizontal_table->max_height = max_height;
}

void UI_table_background_colors_set(uiTable *table, const unsigned char rgb1[3], const unsigned char rgb2[3])
{
	copy_v3_v3_uchar(table->rgb1, rgb1);
	copy_v3_v3_uchar(table->rgb2, rgb2);
	table->flag |= TABLE_DRAW_BACKGROUND;
}

/**
 * Insert a new column into \a table with default parameters (100% available width, 0px min width, aligned to left).
 *
 * \param id_name: Identifier of the column. Has to be unique within this table!
 * \param drawname: Name of the column that may be drawin in the UI. Allowed to be NULL.
 * \param cell_draw: The callback to call when drawing a cell of this column type. Passes custom data stored in row.
 */
uiTableColumn *UI_table_column_add(uiTable *table, const char *idname, const char *drawname,
                                   uiTableCellDrawFunc cell_draw)
{
	uiTableColumn *col = MEM_callocN(sizeof(*col), __func__);

	col->idname = idname;
	col->drawname = drawname;
	col->cell_draw = cell_draw;
	col->alignment = TABLE_COLUMN_ALIGN_LEFT;
	UI_table_column_width_set(col, 100, TABLE_UNIT_PERCENT, 0);

	BLI_addtail(&table->columns, col);
	table->tot_columns++;

	return col;
}

void UI_table_column_remove(uiTable *table, uiTableColumn *column)
{
	BLI_assert(BLI_findindex(&table->columns, column) != -1);
	BLI_remlink(&table->columns, column);
	table->tot_columns--;
}

uiTableColumn *UI_table_column_lookup(uiTable *table, const char *idname)
{
	TABLE_COLUMNS_ITER_BEGIN(table, column)
	{
		if (STREQ(column->idname, idname)) {
			return column;
		}
	}
	TABLE_COLUMNS_ITER_END;

	return NULL;
}

/**
 * Set the size info for \a col.
 * \param width: The width in either pixels (#UI_table_size_px), or percentage (#UI_table_size_percentage).
 * \param min_width_px: Minimum width for the column (always in px).
 */
void UI_table_column_width_set(uiTableColumn *column, const unsigned int width, enum uiTableUnit unit,
                               const int min_width_px)
{
	column->width.unit = unit;
	column->width.value = width;
	column->min_width = min_width_px;
}

void UI_table_column_alignment_set(uiTableColumn *column, enum uiTableColumnAlignemt alignment)
{
	column->alignment = alignment;
}

/*
 * Insert a new row into \a table with default parameters (height of UI_UNIT_Y). Should
 * be fine to use this for inserting many rows at once. It's using BLI_mempool with a
 * chunck size of 64, so it only allocates memory for every 65th element.
 *
 * \param rowdata: Custom data passed when drawing the row. It should contain
 *                 enough information to draw all columns for this row.
 */
uiTableRow *UI_table_row_add(uiTable *table, void *rowdata)
{
	uiTableRow *row = BLI_mempool_calloc(table->row_pool);

	row->rowdata = rowdata;
	table_row_height_set(table, row, UI_UNIT_Y);

	return row;
}

void UI_table_row_height_set(uiTable *table, uiTableRow *row, unsigned int height)
{
	table_row_height_set(table, row, height);
}

void UI_table_draw(uiTable *table, uiBlock *block, uiStyle *style)
{
	struct TableColumnDrawInfo column_drawinfo = table_column_drawinfo_init(table);
	struct {
		int xmin;
		int xmax;
	} *column_xcoords = BLI_array_alloca(column_xcoords, table->tot_columns);
	BLI_mempool_iter iter;
	unsigned int prev_row_height = 0; /* to check if rows have consistent height */
	unsigned int xofs = 0, yofs = 0;
	bool consistent_row_height = true;
	int row_index = 0;


	BLI_mempool_iternew(table->row_pool, &iter);
	for (uiTableRow *row = BLI_mempool_iterstep(&iter); row; row = BLI_mempool_iterstep(&iter)) {
		rcti drawrect;
		unsigned int draw_height;
		int column_index = 0;

		table_row_calc_y_coords(table, row, &xofs, &yofs, &drawrect.ymin, &drawrect.ymax);
		draw_height = BLI_rcti_size_y(&drawrect);

		/* check for consistent row height */
		if ((row_index > 0) && (draw_height != prev_row_height)) {
			consistent_row_height = false;
		}

		table_row_draw_background(table, row_index, draw_height, xofs, yofs);

		TABLE_COLUMNS_ITER_BEGIN(table, column)
		{
			uiLayout *cell_layout = NULL;

			if (row_index == 0) {
				/* Store column x-coords for further iterations over this column. */
				table_column_calc_x_coords(column, table->max_width, &column_drawinfo,
				                           &column_xcoords[column_index].xmin,
				                           &column_xcoords[column_index].xmax);
			}

			drawrect.xmin = column_xcoords[column_index].xmin;
			drawrect.xmax = column_xcoords[column_index].xmax;

			if (block && style) {
				/* Room for optimization: UI_block_layout allocates memory twice, could be avoided by
				 * pre-allocating into an array (uiLayout would need some changes to support this). */
				cell_layout = UI_block_layout(block, UI_LAYOUT_VERTICAL, UI_LAYOUT_PANEL, drawrect.xmin, drawrect.ymax,
				                              BLI_rcti_size_x(&drawrect), 0, 0, style);
			}
			column->cell_draw(cell_layout, row->rowdata, drawrect);
			if (block) {
				UI_block_layout_resolve(block, NULL, NULL);
			}

			column_index++;
		}
		TABLE_COLUMNS_ITER_END;
		BLI_assert(column_index == table->tot_columns);

		yofs += draw_height;
		prev_row_height = draw_height;
		row_index++;
	}

	if (consistent_row_height) {
		table->flag |= TABLE_ROWS_CONSTANT_HEIGHT;
	}
	else {
		BLI_assert((table->flag & TABLE_ROWS_CONSTANT_HEIGHT) == 0);
	}
}

unsigned int UI_table_get_rowcount(const uiTable *table)
{
	return BLI_mempool_count(table->row_pool);
}

/** \} */ /* UI Table API */
