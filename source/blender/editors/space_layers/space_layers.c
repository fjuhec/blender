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

#include "BKE_context.h"
#include "BKE_screen.h"

#include "BLI_listbase.h"
#include "BLI_utildefines.h"

#include "DNA_space_types.h"
#include "DNA_windowmanager_types.h"

#include "ED_screen.h"
#include "ED_space_api.h"

#include "WM_types.h"


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

	return (SpaceLink *)slayer;
}

static SpaceLink *layers_duplicate(SpaceLink *sl)
{
	SpaceLayers *slayer = MEM_dupallocN(sl);

	/* clear or remove stuff from old */

	return (SpaceLink *)slayer;
}

/* add handlers, stuff you only do once or on area/region changes */
static void layer_main_region_init(wmWindowManager *UNUSED(wm), ARegion *ar)
{
	/* do not use here, the properties changed in userprefs do a system-wide refresh, then scroller jumps back */
	/*	ar->v2d.flag &= ~V2D_IS_INITIALISED; */
	
	ar->v2d.scroll = V2D_SCROLL_RIGHT | V2D_SCROLL_VERTICAL_HIDE;
}

static void layers_main_region_draw(const bContext *UNUSED(C), ARegion *UNUSED(ar))
{
	printf("DRAW!\n");
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

static void layers_main_region_listener(bScreen *UNUSED(sc), ScrArea *UNUSED(sa), ARegion *UNUSED(ar), wmNotifier *UNUSED(wmn))
{
	/* context changes */
}

/* only called once, from space/spacetypes.c */
void ED_spacetype_layers(void)
{
	SpaceType *st = MEM_callocN(sizeof(SpaceType), "spacetype layers");
	ARegionType *art;

	st->spaceid = SPACE_LAYERS;
	strncpy(st->name, "LayerManager", BKE_ST_MAXNAME);

	st->new = layers_new;
	st->duplicate = layers_duplicate;

	/* regions: main window */
	art = MEM_callocN(sizeof(ARegionType), "spacetype layers region");
	art->regionid = RGN_TYPE_WINDOW;
	art->init = layer_main_region_init;
	art->draw = layers_main_region_draw;
	art->listener = layers_main_region_listener;
	art->keymapflag = ED_KEYMAP_UI;
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
