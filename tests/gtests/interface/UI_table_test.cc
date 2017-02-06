/* Apache License, Version 2.0 */

#include "testing/testing.h"

extern "C" {
#include "BLI_mempool.h"
#include "BLI_rect.h"
#include "BLI_utildefines.h"
#include "UI_table.h"
}


/**
 * Add a bunch of rows, check if their count matches expectation.
 */
TEST(ui_table, RowAdd)
{
	uiTable *table = UI_table_vertical_flow_create();

	UI_table_column_add(table, "testcol", NULL, NULL);
	for (int i = 0; i < 100; i++) {
		UI_table_row_add(table, NULL);
	}

	EXPECT_EQ(100, UI_table_get_rowcount(table));

	UI_table_free(table);
}

static struct {
	int tot_cells;
	int tot_rows_col1;
	int tot_rows_col2;
	int tot_rows_col3;
} draw_stats = {};

static void table_draw_test_ex()
{
	draw_stats.tot_cells++;
}
static void table_draw_test_col1(void *UNUSED(rowdata), rcti UNUSED(drawrect))
{
	table_draw_test_ex();
	draw_stats.tot_rows_col1++;
}
static void table_draw_test_col2(void *UNUSED(rowdata), rcti UNUSED(drawrect))
{
	table_draw_test_ex();
	draw_stats.tot_rows_col2++;
}
static void table_draw_test_col3(void *UNUSED(rowdata), rcti UNUSED(drawrect))
{
	table_draw_test_ex();
	draw_stats.tot_rows_col3++;
}

/**
 * Draw a number of columns and rows, gather some statistics and check if they meet expectations.
 */
TEST(ui_table, CellsDraw)
{
	uiTable *table = UI_table_vertical_flow_create();

	UI_table_column_add(table, "testcol1", NULL, table_draw_test_col1);
	UI_table_column_add(table, "testcol2", NULL, table_draw_test_col2);
	UI_table_column_add(table, "testcol3", NULL, table_draw_test_col3);
	for (int i = 0; i < 10; i++) {
		UI_table_row_add(table, NULL);
	}

	/* fills draw_stats */
	UI_table_draw(table);

	EXPECT_EQ(30, draw_stats.tot_cells);
	EXPECT_EQ(10, draw_stats.tot_rows_col1);
	EXPECT_EQ(10, draw_stats.tot_rows_col2);
	EXPECT_EQ(10, draw_stats.tot_rows_col3);

	UI_table_free(table);
}

void table_draw_test_alignment_left(void *UNUSED(rowdata), rcti drawrect)
{
	EXPECT_EQ(0, drawrect.xmin);
	EXPECT_EQ(50, drawrect.xmax);
}
void table_draw_test_alignment_right(void *UNUSED(rowdata), rcti drawrect)
{
	EXPECT_EQ(50, drawrect.xmin);
	EXPECT_EQ(100, drawrect.xmax);
}

/**
 * Check if alignment works as expected with a column-width of 50%, one aligned to left and one to right.
 */
TEST(ui_table, ColumnAlignPercentage)
{
	uiTable *table;
	uiTableColumn *col;

	table = UI_table_vertical_flow_create();
	UI_table_max_width_set(table, 100);

	col = UI_table_column_add(table, "left_align", NULL, table_draw_test_alignment_left);
	UI_table_column_width_set(col, 50, TABLE_UNIT_PERCENT, 0);
	col = UI_table_column_add(table, "right_align", NULL, table_draw_test_alignment_right);
	UI_table_column_width_set(col, 50, TABLE_UNIT_PERCENT, 0);
	UI_table_column_alignment_set(col, TABLE_COLUMN_ALIGN_RIGHT);
	for (int i = 0; i < 10; i++) {
		UI_table_row_add(table, NULL);
	}

	UI_table_draw(table);

	UI_table_free(table);
}

void table_draw_test_alignment_left_percent(void *UNUSED(rowdata), rcti drawrect)
{
	EXPECT_EQ(10, drawrect.xmin);
	EXPECT_EQ(50, drawrect.xmax);
}
void table_draw_test_alignment_right_percent(void *UNUSED(rowdata), rcti drawrect)
{
	EXPECT_EQ(60, drawrect.xmin);
	EXPECT_EQ(100, drawrect.xmax);
}
void table_draw_test_alignment_left_px(void *UNUSED(rowdata), rcti drawrect)
{
	EXPECT_EQ(0, drawrect.xmin);
	EXPECT_EQ(10, drawrect.xmax);
}
void table_draw_test_alignment_right_px(void *UNUSED(rowdata), rcti drawrect)
{
	EXPECT_EQ(50, drawrect.xmin);
	EXPECT_EQ(60, drawrect.xmax);
}

/**
 * Check if alignment works as expected with mixed left/right alignment and px/percentage sizes.
 */
TEST(ui_table, ColumnAlignMixed)
{
	uiTable *table;
	uiTableColumn *col;

	table = UI_table_vertical_flow_create();
	UI_table_max_width_set(table, 100);

	col = UI_table_column_add(table, "left_align_px", NULL, table_draw_test_alignment_left_px);
	UI_table_column_width_set(col, 10, TABLE_UNIT_PX, 0);
	/* intentionally adding a right aligned column first */
	col = UI_table_column_add(table, "right_align_percent", NULL, table_draw_test_alignment_right_percent);
	UI_table_column_width_set(col, 50, TABLE_UNIT_PERCENT, 0);
	UI_table_column_alignment_set(col, TABLE_COLUMN_ALIGN_RIGHT);
	col = UI_table_column_add(table, "left_align_percent", NULL, table_draw_test_alignment_left_percent);
	UI_table_column_width_set(col, 50, TABLE_UNIT_PERCENT, 0);
	col = UI_table_column_add(table, "right_align_px", NULL, table_draw_test_alignment_right_px);
	UI_table_column_width_set(col, 10, TABLE_UNIT_PX, 0);
	for (int i = 0; i < 10; i++) {
		UI_table_row_add(table, NULL);
	}

	UI_table_draw(table);

	UI_table_free(table);
}

void table_draw_test_oversize(void *UNUSED(rowdata), rcti drawrect)
{
	EXPECT_EQ(100, BLI_rcti_size_x(&drawrect));
}

/**
 * Try creating a table with columns of a larger width than table.
 */
TEST(ui_table, ColumnOversize)
{
	uiTable *table;
	uiTableColumn *col;

	table = UI_table_vertical_flow_create();
	UI_table_max_width_set(table, 100);

	col = UI_table_column_add(table, "oversize", NULL, table_draw_test_oversize);
	UI_table_column_width_set(col, 110, TABLE_UNIT_PX, 0);
	for (int i = 0; i < 10; i++) {
		UI_table_row_add(table, NULL);
	}

	UI_table_draw(table);

	UI_table_free(table);
}

void table_draw_test_horizontal_flow_oversize(void *UNUSED(rowdata), rcti drawrect)
{
	EXPECT_EQ(0, drawrect.ymax);
	EXPECT_EQ(-10, drawrect.ymin);
}

/**
 * Try creating a horizontal-flow table where rows have larger height than table max-height.
 */
TEST(ui_table, HorizontalFlowOversize)
{
	uiTable *table;

	table = UI_table_horizontal_flow_create();
	UI_table_horizontal_flow_max_height_set(table, 10);
	UI_table_max_width_set(table, 100);

	UI_table_column_add(table, "oversize", NULL, table_draw_test_horizontal_flow_oversize);
	for (int i = 0; i < 10; i++) {
		uiTableRow *row = UI_table_row_add(table, NULL);
		UI_table_row_height_set(table, row, 20);
	}

	UI_table_draw(table);

	UI_table_free(table);
}
