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
 * Contributor(s): Blender Foundation (2014).
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/makesrna/intern/rna_depsgraph.c
 *  \ingroup RNA
 */

#include <stdlib.h>

#include "BLI_utildefines.h"
#include "BLI_path_util.h"

#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "rna_internal.h"

#include "DEG_depsgraph.h"

#include "BKE_depsgraph.h"

#ifdef RNA_RUNTIME

#include "BLI_iterator.h"
#include "BKE_report.h"
#include "DNA_object_types.h"

#include "DEG_depsgraph_debug.h"
#include "DEG_depsgraph_query.h"

#include "MEM_guardedalloc.h"

static void rna_Depsgraph_debug_graphviz(Depsgraph *graph, const char *filename)
{
	FILE *f = fopen(filename, "w");
	if (f == NULL)
		return;
	
	DEG_debug_graphviz(graph, f, "Depsgraph", false);
	
	fclose(f);
}

static void rna_Depsgraph_debug_rebuild(Depsgraph *UNUSED(graph), Main *bmain)
{
	Scene *sce;
	DAG_relations_tag_update(bmain);
	for (sce = bmain->scene.first; sce; sce = sce->id.next) {
		DAG_scene_relations_rebuild(bmain, sce);
		DEG_graph_on_visible_update(bmain, sce);
	}
}

static void rna_Depsgraph_debug_stats(Depsgraph *graph, ReportList *reports)
{
	size_t outer, ops, rels;
	
	DEG_stats_simple(graph, &outer, &ops, &rels);
	
	// XXX: report doesn't seem to work
	printf("Approx %lu Operations, %lu Relations, %lu Outer Nodes\n",
	       ops, rels, outer);
		   
	BKE_reportf(reports, RPT_WARNING, "Approx. %lu Operations, %lu Relations, %lu Outer Nodes",
	            ops, rels, outer);
}

static void rna_Depsgraph_objects_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
	Depsgraph *graph = (Depsgraph *)ptr->data;
	iter->internal.custom = MEM_callocN(sizeof(BLI_Iterator), __func__);
	DAG_objects_iterator_begin(iter->internal.custom, graph);
	iter->valid = ((BLI_Iterator *)iter->internal.custom)->valid;
}

static void rna_Depsgraph_objects_next(CollectionPropertyIterator *iter)
{
	DAG_objects_iterator_next(iter->internal.custom);
	iter->valid = ((BLI_Iterator *)iter->internal.custom)->valid;
}

static void rna_Depsgraph_objects_end(CollectionPropertyIterator *iter)
{
	DAG_objects_iterator_end(iter->internal.custom);
	MEM_freeN(iter->internal.custom);
}

static PointerRNA rna_Depsgraph_objects_get(CollectionPropertyIterator *iter)
{
	Object *ob = ((BLI_Iterator *)iter->internal.custom)->current;
	return rna_pointer_inherit_refine(&iter->parent, &RNA_Object, ob);
}

#else

static void rna_def_depsgraph(BlenderRNA *brna)
{
	StructRNA *srna;
	FunctionRNA *func;
	PropertyRNA *parm;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "Depsgraph", NULL);
	RNA_def_struct_ui_text(srna, "Dependency Graph", "");
	
	func = RNA_def_function(srna, "debug_graphviz", "rna_Depsgraph_debug_graphviz");
	parm = RNA_def_string_file_path(func, "filename", NULL, FILE_MAX, "File Name",
	                                "File in which to store graphviz debug output");
	RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

	func = RNA_def_function(srna, "debug_rebuild", "rna_Depsgraph_debug_rebuild");
	RNA_def_function_flag(func, FUNC_USE_MAIN);

	func = RNA_def_function(srna, "debug_stats", "rna_Depsgraph_debug_stats");
	RNA_def_function_ui_description(func, "Report the number of elements in the Dependency Graph");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);

	prop = RNA_def_property(srna, "objects", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_struct_type(prop, "Object");
	RNA_def_property_collection_funcs(prop, "rna_Depsgraph_objects_begin", "rna_Depsgraph_objects_next",
	                                  "rna_Depsgraph_objects_end", "rna_Depsgraph_objects_get",
	                                  NULL, NULL, NULL, NULL);
}

void RNA_def_depsgraph(BlenderRNA *brna)
{
	rna_def_depsgraph(brna);
}

#endif
