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

/** \file blender/editors/space_layers/space_layers.c
 *  \ingroup splayers
 */

#include <string.h>

#include "MEM_guardedalloc.h"

#include "BIF_gl.h"

#include "BKE_context.h"
#include "BKE_layer.h"
#include "BKE_screen.h"

#include "BLI_listbase.h"

#include "ED_screen.h"
#include "ED_space_api.h"

#include "UI_resources.h"
#include "UI_view2d.h"

#include "WM_api.h"
#include "WM_types.h"

#include "layers_intern.h" /* own include */


/* ******************** default callbacks for layer manager space ***************** */

static SpaceLink *layers_new(const bContext *UNUSED(C))
{
	ARegion *ar;
	SpaceLayers *slayer; /* hmm, that's actually a good band name... */

	slayer = MEM_callocN(sizeof(SpaceLayers), __func__);
	slayer->spacetype = SPACE_LAYERS;

	/* header */
	ar = MEM_callocN(sizeof(ARegion), "header for layer manager");
	BLI_addtail(&slayer->regionbase, ar);
	ar->regiontype = RGN_TYPE_HEADER;
	ar->alignment = RGN_ALIGN_BOTTOM;

	/* main region */
	ar = MEM_callocN(sizeof(ARegion), "main region for layer manager");
	BLI_addtail(&slayer->regionbase, ar);
	ar->regiontype = RGN_TYPE_WINDOW;
	ar->v2d.scroll = (V2D_SCROLL_RIGHT | V2D_SCROLL_BOTTOM | V2D_SCROLL_HORIZONTAL_HIDE | V2D_SCROLL_VERTICAL_HIDE);
	ar->v2d.align = (V2D_ALIGN_NO_NEG_X | V2D_ALIGN_NO_POS_Y);

	return (SpaceLink *)slayer;
}

/* not spacelink itself */
static void layers_free(SpaceLink *sl)
{
	SpaceLayers *slayer = (SpaceLayers *)sl;
	BLI_freelistN(&slayer->layer_tiles);
}

static SpaceLink *layers_duplicate(SpaceLink *sl)
{
	SpaceLayers *slayer = MEM_dupallocN(sl);

	/* clear or remove stuff from old */

	return (SpaceLink *)slayer;
}

/* add handlers, stuff you only do once or on area changes */
static void layer_init(wmWindowManager *wm, ScrArea *sa)
{
	/* own keymap */
	wmKeyMap *keymap = WM_keymap_find(wm->defaultconf, "Layer Manager", SPACE_LAYERS, 0);
	WM_event_add_keymap_handler(&sa->handlers, keymap);
}

/* add handlers, stuff you only do once or on area/region changes */
static void layer_main_region_init(wmWindowManager *UNUSED(wm), ARegion *ar)
{
	UI_view2d_region_reinit(&ar->v2d, V2D_COMMONVIEW_LIST, ar->winx, ar->winy);
	ar->v2d.scroll |= (V2D_SCROLL_VERTICAL_FULLR | V2D_SCROLL_HORIZONTAL_FULLR);
}

static void layers_main_region_draw(const bContext *C, ARegion *ar)
{
	View2D *v2d = &ar->v2d;

	/* v2d has initialized flag, so this call will only set the mask correct */
	UI_view2d_region_reinit(v2d, V2D_COMMONVIEW_LIST, ar->winx, ar->winy);

	UI_ThemeClearColor(TH_BACK);
	glClear(GL_COLOR_BUFFER_BIT);

	layers_draw_tiles(C, ar);

	/* reset view matrix */
	UI_view2d_view_restore(C);

	/* scrollers */
	View2DScrollers *scrollers;
	scrollers = UI_view2d_scrollers_calc(C, v2d, V2D_ARG_DUMMY, V2D_ARG_DUMMY, V2D_ARG_DUMMY, V2D_ARG_DUMMY);
	UI_view2d_scrollers_draw(C, v2d, scrollers);
	UI_view2d_scrollers_free(scrollers);
}

/* add handlers, stuff you only do once or on area/region changes */
static void layers_header_region_init(wmWindowManager *UNUSED(wm), ARegion *ar)
{
	ED_region_header_init(ar);
}

static void layers_header_region_draw(const bContext *C, ARegion *ar)
{
	ED_region_header(C, ar);
}

static void layers_main_region_listener(bScreen *UNUSED(sc), ScrArea *UNUSED(sa), ARegion *ar, wmNotifier *wmn)
{
	switch (wmn->category) {
		case NC_SCENE:
			if (wmn->data == ND_LAYER) {
				ED_region_tag_redraw(ar);
			}
			break;
	}
}

/* only called once, from space/spacetypes.c */
void ED_spacetype_layers(void)
{
	SpaceType *st = MEM_callocN(sizeof(SpaceType), "spacetype layers");
	ARegionType *art;

	st->spaceid = SPACE_LAYERS;
	strncpy(st->name, "LayerManager", BKE_ST_MAXNAME);

	st->new = layers_new;
	st->free = layers_free;
	st->duplicate = layers_duplicate;
	st->init = layer_init;
	st->operatortypes = layers_operatortypes;
	st->keymap = layers_keymap;

	/* regions: main window */
	art = MEM_callocN(sizeof(ARegionType), "spacetype layers region");
	art->regionid = RGN_TYPE_WINDOW;
	art->init = layer_main_region_init;
	art->draw = layers_main_region_draw;
	art->listener = layers_main_region_listener;
	art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D;
	BLI_addhead(&st->regiontypes, art);

	/* regions: header */
	art = MEM_callocN(sizeof(ARegionType), "spacetype layers header");
	art->regionid = RGN_TYPE_HEADER;
	art->prefsizey = HEADERY;
	art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_HEADER;
	art->init = layers_header_region_init;
	art->draw = layers_header_region_draw;
	BLI_addhead(&st->regiontypes, art);

	BKE_spacetype_register(st);
}
