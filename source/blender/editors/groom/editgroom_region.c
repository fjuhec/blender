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

/** \file blender/editors/groom/editgroom_region.c
 *  \ingroup edgroom
 */

#include "DNA_groom_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"

#include "BLT_translation.h"

#include "BKE_context.h"
#include "BKE_groom.h"
#include "BKE_library.h"

#include "DEG_depsgraph.h"

#include "RNA_access.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_object.h"
#include "ED_screen.h"
#include "ED_util.h"
#include "ED_view3d.h"
#include "ED_groom.h"

#include "groom_intern.h"

static void groom_bundle_section_init(GroomSection *section, GroomSectionVertex *verts, int numverts, float mat[4][4], float cparam)
{
	madd_v3_v3v3fl(section->center, mat[3], mat[2], cparam);
	copy_v3_v3(section->normal, mat[2]);
	
	{
		const float radius = 0.5f;
		GroomSectionVertex *vertex = verts;
		for (int i = 0; i < numverts; ++i, ++vertex)
		{
			float angle = 2*M_PI * (float)i / (float)numverts;
			vertex->co[0] = cos(angle) * radius;
			vertex->co[1] = sin(angle) * radius;
		}
	}
}

static GroomBundle* groom_add_bundle(float mat[4][4])
{
	GroomBundle *bundle = MEM_callocN(sizeof(GroomBundle), "groom bundle");
	
	bundle->numloopverts = 6;
	bundle->totsections = 2;
	bundle->totverts = bundle->numloopverts * bundle->totsections;
	bundle->sections = MEM_mallocN(sizeof(GroomSection) * bundle->totsections, "groom bundle sections");
	bundle->verts = MEM_mallocN(sizeof(GroomSectionVertex) * bundle->totverts, "groom bundle vertices");
	
	int numverts = bundle->numloopverts;
	groom_bundle_section_init(&bundle->sections[0], &bundle->verts[numverts * 0], numverts, mat, 0.0f);
	groom_bundle_section_init(&bundle->sections[1], &bundle->verts[numverts * 1], numverts, mat, 1.0f);
	
	return bundle;
}

static int region_add_exec(bContext *C, wmOperator *op)
{
	Object *obedit = CTX_data_edit_object(C);
	Groom *groom = obedit->data;
	EditGroom *editgroom = groom->editgroom;

	WM_operator_view3d_unit_defaults(C, op);

	float loc[3], rot[3];
	unsigned int layer;
	if (!ED_object_add_generic_get_opts(C, op, 'Z', loc, rot, NULL, &layer, NULL))
		return OPERATOR_CANCELLED;

	float mat[4][4];
	ED_object_new_primitive_matrix(C, obedit, loc, rot, mat);

	GroomBundle *bundle = groom_add_bundle(mat);
	BLI_addtail(&editgroom->bundles, bundle);

	WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, obedit);
	DEG_id_tag_update(&obedit->id, OB_RECALC_DATA);

	return OPERATOR_FINISHED;
}

void GROOM_OT_region_add(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Region";
	ot->description = "Add a new region to the groom object";
	ot->idname = "GROOM_OT_region_add";

	/* api callbacks */
	ot->exec = region_add_exec;
	ot->poll = ED_operator_editgroom;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	ED_object_add_generic_props(ot, false);
}
