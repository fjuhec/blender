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
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Lukas Toenne
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/groom/groom_hair.c
 *  \ingroup groom
 */

#include "DNA_groom_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_math.h"

#include "BLT_translation.h"

#include "BKE_context.h"
#include "BKE_groom.h"
#include "BKE_report.h"

#include "DEG_depsgraph.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_object.h"
#include "ED_screen.h"
#include "ED_util.h"
#include "ED_view3d.h"
#include "ED_groom.h"

#include "groom_intern.h"

static int groom_object_poll(bContext *C)
{
	Object *ob = ED_object_context(C);
	return ob->type == OB_GROOM;
}

/* GROOM_OT_hair_distribute */

static int hair_distribute_exec(bContext *C, wmOperator *op)
{
	Object *ob = ED_object_context(C);
	Groom *groom = ob->data;
	int count = RNA_int_get(op->ptr, "count");
	unsigned int seed = (unsigned int)RNA_int_get(op->ptr, "seed");

	if (!groom->scalp_object)
	{
		BKE_reportf(op->reports, RPT_ERROR, "Scalp object needed for creating hair follicles");
		return OPERATOR_CANCELLED;
	}

	BKE_groom_distribute_follicles(groom, seed, count);

	WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, ob);
	DEG_id_tag_update(&ob->id, OB_RECALC_DATA);

	return OPERATOR_FINISHED;
}

void GROOM_OT_hair_distribute(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Distribute Hair";
	ot->description = "Distribute hair follicles and guide curves on the scalp";
	ot->idname = "GROOM_OT_hair_distribute";

	/* api callbacks */
	ot->invoke = WM_operator_props_popup_confirm;
	ot->exec = hair_distribute_exec;
	ot->poll = groom_object_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	RNA_def_int(ot->srna, "count", 1000, 0, INT_MAX,
	            "Count", "Number of follicles to generate", 1, 1e6);
	RNA_def_int(ot->srna, "seed", 0, 0, INT_MAX,
	            "Seed", "Seed value for randomized follicle distribution", 0, INT_MAX);
}
