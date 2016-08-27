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

/** \file blender/editors/space_stats/space_stats.c
 *  \ingroup splayers
 */

#include <string.h>

#include "MEM_guardedalloc.h"

#include "BIF_gl.h"

#include "BKE_context.h"
#include "BKE_screen.h"

#include "BLI_listbase.h"
#include "BLI_utildefines.h"

#include "DNA_space_types.h"
#include "DNA_windowmanager_types.h"

#include "ED_screen.h"
#include "ED_space_api.h"

#include "UI_interface.h"
#include "UI_resources.h"
#include "UI_view2d.h"

#include "WM_types.h"


/* ******************** default callbacks for stats editor space ***************** */

static SpaceLink *stats_new(const bContext *UNUSED(C))
{
	ARegion *ar;
	SpaceStats *sstats;

	sstats = MEM_callocN(sizeof(SpaceStats), __func__);
	sstats->spacetype = SPACE_STATS;

	/* header */
	ar = MEM_callocN(sizeof(ARegion), "header for stats editor");

	BLI_addtail(&sstats->regionbase, ar);
	ar->regiontype = RGN_TYPE_HEADER;
	ar->alignment = RGN_ALIGN_BOTTOM;

	/* main region */
	ar = MEM_callocN(sizeof(ARegion), "main region for stats editor");

	BLI_addtail(&sstats->regionbase, ar);
	ar->regiontype = RGN_TYPE_WINDOW;

	return (SpaceLink *)sstats;
}

static SpaceLink *stats_duplicate(SpaceLink *sl)
{
	SpaceStats *sstats = MEM_dupallocN(sl);

	/* clear or remove stuff from old */

	return (SpaceLink *)sstats;
}

/* add handlers, stuff you only do once or on area/region changes */
static void stats_main_region_init(wmWindowManager *UNUSED(wm), ARegion *ar)
{
	/* do not use here, the properties changed in userprefs do a system-wide refresh, then scroller jumps back */
	/*    ar->v2d.flag &= ~V2D_IS_INITIALISED; */

	ar->v2d.scroll = V2D_SCROLL_RIGHT | V2D_SCROLL_VERTICAL_HIDE;
}

static void stats_main_region_draw(const bContext *C, ARegion *ar)
{
	View2D *v2d = &ar->v2d;
	float size_x = ar->winx;
	float size_y = 0.0f;

	UI_ThemeClearColor(TH_BACK);
	glClear(GL_COLOR_BUFFER_BIT);

	/* update size of tot-rect (extents of data/viewable area) */
	UI_view2d_totRect_set(v2d, size_x, size_y);

	/* reset view matrix */
	UI_view2d_view_restore(C);

	/* scrollers */
	View2DScrollers *scrollers;
	scrollers = UI_view2d_scrollers_calc(C, v2d, V2D_ARG_DUMMY, V2D_ARG_DUMMY, V2D_ARG_DUMMY, V2D_ARG_DUMMY);
	UI_view2d_scrollers_draw(C, v2d, scrollers);
	UI_view2d_scrollers_free(scrollers);
}

/* add handlers, stuff you only do once or on area/region changes */
static void stats_header_region_init(wmWindowManager *UNUSED(wm), ARegion *ar)
{
	ED_region_header_init(ar);
}

static void stats_header_region_draw(const bContext *C, ARegion *ar)
{
	ED_region_header(C, ar);
}

static void stats_main_region_listener(bScreen *UNUSED(sc), ScrArea *sa, ARegion *UNUSED(ar), wmNotifier *wmn)
{
	switch(wmn->category) {
		case NC_SPACE: {
			if (wmn->data == ND_SPACE_STATS) {
				ED_area_tag_redraw(sa);
			}
			break;
		}
	}
}

/* only called once, from space/spacetypes.c */
void ED_spacetype_stats(void)
{
	SpaceType *st = MEM_callocN(sizeof(SpaceType), "spacetype stats");
	ARegionType *art;

	st->spaceid = SPACE_STATS;
	strncpy(st->name, "StatsEditor", BKE_ST_MAXNAME);

	st->new = stats_new;
	st->duplicate = stats_duplicate;

	/* regions: main window */
	art = MEM_callocN(sizeof(ARegionType), "stats editor region");
	art->regionid = RGN_TYPE_WINDOW;
	art->init = stats_main_region_init;
	art->draw = stats_main_region_draw;
	art->listener = stats_main_region_listener;
	art->keymapflag = ED_KEYMAP_UI;
	BLI_addhead(&st->regiontypes, art);

	/* regions: header */
	art = MEM_callocN(sizeof(ARegionType), "spacetype stats header");
	art->regionid = RGN_TYPE_HEADER;
	art->prefsizey = HEADERY;
	art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_HEADER;
	art->init = stats_header_region_init;
	art->draw = stats_header_region_draw;
	BLI_addhead(&st->regiontypes, art);

	BKE_spacetype_register(st);
}
