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
 * The Original Code is Copyright (C) 2013 Blender Foundation.
 * All rights reserved.
 *
 * Original Author: Joshua Leung
 * Contributor(s): Based on original depsgraph.c code - Blender Foundation (2005-2013)
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/depsgraph/intern/depsgraph_build.cc
 *  \ingroup depsgraph
 *
 * Methods for constructing depsgraph.
 */

#include "MEM_guardedalloc.h"

extern "C" {
#include "BLI_blenlib.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "DNA_action_types.h"
#include "DNA_anim_types.h"
#include "DNA_armature_types.h"
#include "DNA_camera_types.h"
#include "DNA_constraint_types.h"
#include "DNA_curve_types.h"
#include "DNA_effect_types.h"
#include "DNA_group_types.h"
#include "DNA_key_types.h"
#include "DNA_lamp_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meta_types.h"
#include "DNA_node_types.h"
#include "DNA_particle_types.h"
#include "DNA_object_types.h"
#include "DNA_rigidbody_types.h"
#include "DNA_scene_types.h"
#include "DNA_texture_types.h"
#include "DNA_world_types.h"

#include "BKE_action.h"
#include "BKE_armature.h"
#include "BKE_animsys.h"
#include "BKE_constraint.h"
#include "BKE_curve.h"
#include "BKE_effect.h"
#include "BKE_fcurve.h"
#include "BKE_group.h"
#include "BKE_key.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_mball.h"
#include "BKE_modifier.h"
#include "BKE_node.h"
#include "BKE_object.h"
#include "BKE_particle.h"
#include "BKE_rigidbody.h"
#include "BKE_sound.h"
#include "BKE_texture.h"
#include "BKE_tracking.h"
#include "BKE_world.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_debug.h"
#include "DEG_depsgraph_build.h"

#include "RNA_access.h"
#include "RNA_types.h"
} /* extern "C" */

#include "builder/deg_builder.h"
#include "builder/deg_builder_nodes.h"
#include "builder/deg_builder_relations.h"

#include "depsnode.h"
#include "depsnode_component.h"
#include "depsgraph_debug.h"
#include "depsnode_operation.h"
#include "depsgraph_types.h"
#include "depsgraph_intern.h"

#include "depsgraph_util_cycle.h"
#include "depsgraph_util_foreach.h"
#include "depsgraph_util_transitive.h"

/* ****************** */
/* External Build API */

static eDepsNode_Type deg_build_scene_component_type(
        eDepsSceneComponentType component)
{
	switch (component) {
		case DEG_SCENE_COMP_PARAMETERS:     return DEPSNODE_TYPE_PARAMETERS;
		case DEG_SCENE_COMP_ANIMATION:      return DEPSNODE_TYPE_ANIMATION;
		case DEG_SCENE_COMP_SEQUENCER:      return DEPSNODE_TYPE_SEQUENCER;
	}
	return DEPSNODE_TYPE_UNDEFINED;
}

static eDepsNode_Type deg_build_object_component_type(
        eDepsObjectComponentType component)
{
	switch (component) {
		case DEG_OB_COMP_PARAMETERS:        return DEPSNODE_TYPE_PARAMETERS;
		case DEG_OB_COMP_PROXY:             return DEPSNODE_TYPE_PROXY;
		case DEG_OB_COMP_ANIMATION:         return DEPSNODE_TYPE_ANIMATION;
		case DEG_OB_COMP_TRANSFORM:         return DEPSNODE_TYPE_TRANSFORM;
		case DEG_OB_COMP_GEOMETRY:          return DEPSNODE_TYPE_GEOMETRY;
		case DEG_OB_COMP_EVAL_POSE:         return DEPSNODE_TYPE_EVAL_POSE;
		case DEG_OB_COMP_BONE:              return DEPSNODE_TYPE_BONE;
		case DEG_OB_COMP_EVAL_PARTICLES:    return DEPSNODE_TYPE_EVAL_PARTICLES;
		case DEG_OB_COMP_SHADING:           return DEPSNODE_TYPE_SHADING;
	}
	return DEPSNODE_TYPE_UNDEFINED;
}

static DEG::DepsNodeHandle *get_handle(DepsNodeHandle *handle)
{
	return reinterpret_cast<DEG::DepsNodeHandle *>(handle);
}

void DEG_add_scene_relation(DepsNodeHandle *handle,
                            Scene *scene,
                            eDepsSceneComponentType component,
                            const char *description)
{
	eDepsNode_Type type = deg_build_scene_component_type(component);
	DEG::ComponentKey comp_key(&scene->id, type);
	DEG::DepsNodeHandle *deg_handle = get_handle(handle);
	deg_handle->builder->add_node_handle_relation(comp_key,
	                                              deg_handle,
	                                              DEPSREL_TYPE_GEOMETRY_EVAL,
	                                              description);
}

void DEG_add_object_relation(DepsNodeHandle *handle,
                             Object *ob,
                             eDepsObjectComponentType component,
                             const char *description)
{
	eDepsNode_Type type = deg_build_object_component_type(component);
	DEG::ComponentKey comp_key(&ob->id, type);
	DEG::DepsNodeHandle *deg_handle = get_handle(handle);
	deg_handle->builder->add_node_handle_relation(comp_key,
	                                              deg_handle,
	                                              DEPSREL_TYPE_GEOMETRY_EVAL,
	                                              description);
}

void DEG_add_bone_relation(DepsNodeHandle *handle,
                           Object *ob,
                           const char *bone_name,
                           eDepsObjectComponentType component,
                           const char *description)
{
	eDepsNode_Type type = deg_build_object_component_type(component);
	DEG::ComponentKey comp_key(&ob->id, type, bone_name);
	DEG::DepsNodeHandle *deg_handle = get_handle(handle);
	/* XXX: "Geometry Eval" might not always be true, but this only gets called
	 * from modifier building now.
	 */
	deg_handle->builder->add_node_handle_relation(comp_key,
	                                              deg_handle,
	                                              DEPSREL_TYPE_GEOMETRY_EVAL,
	                                              description);
}

void DEG_add_special_eval_flag(Depsgraph *graph, ID *id, short flag)
{
	if (graph == NULL) {
		BLI_assert(!"Graph should always be valid");
		return;
	}
	IDDepsNode *id_node = graph->find_id_node(id);
	if (id_node == NULL) {
		BLI_assert(!"ID should always be valid");
		return;
	}
	id_node->eval_flags |= flag;
}

/* ******************** */
/* Graph Building API's */

/* Build depsgraph for the given scene, and dump results in given
 * graph container.
 */
/* XXX: assume that this is called from outside, given the current scene as
 * the "main" scene.
 */
void DEG_graph_build_from_scene(Depsgraph *graph, Main *bmain, Scene *scene)
{
	/* 1) Generate all the nodes in the graph first */
	DEG::DepsgraphNodeBuilder node_builder(bmain, graph);
	/* create root node for scene first
	 * - this way it should be the first in the graph,
	 *   reflecting its role as the entrypoint
	 */
	node_builder.add_root_node();
	node_builder.build_scene(bmain, scene);

	/* 2) Hook up relationships between operations - to determine evaluation
	 *    order.
	 */
	DEG::DepsgraphRelationBuilder relation_builder(graph);
	/* Hook scene up to the root node as entrypoint to graph. */
	/* XXX what does this relation actually mean?
	 * it doesnt add any operations anyway and is not clear what part of the
	 * scene is to be connected.
	 */
#if 0
	relation_builder.add_relation(RootKey(),
	                              IDKey(scene),
	                              DEPSREL_TYPE_ROOT_TO_ACTIVE,
	                              "Root to Active Scene");
#endif
	relation_builder.build_scene(bmain, scene);

	/* Detect and solve cycles. */
	DEG::deg_graph_detect_cycles(graph);

	/* 3) Simplify the graph by removing redundant relations (to optimize
	 *    traversal later). */
	/* TODO: it would be useful to have an option to disable this in cases where
	 *       it is causing trouble.
	 */
	if (G.debug_value == 799) {
		DEG::deg_graph_transitive_reduction(graph);
	}

	/* 4) Flush visibility layer and re-schedule nodes for update. */
	DEG::deg_graph_build_finalize(graph);

#if 0
	if (!DEG_debug_consistency_check(graph)) {
		printf("Consistency validation failed, ABORTING!\n");
		abort();
	}
#endif
}

/* Tag graph relations for update. */
void DEG_graph_tag_relations_update(Depsgraph *graph)
{
	graph->need_update = true;
}

/* Tag all relations for update. */
void DEG_relations_tag_update(Main *bmain)
{
	for (Scene *scene = (Scene *)bmain->scene.first;
	     scene != NULL;
	     scene = (Scene *)scene->id.next)
	{
		if (scene->depsgraph != NULL) {
			DEG_graph_tag_relations_update(scene->depsgraph);
		}
	}
}

/* Create new graph if didn't exist yet,
 * or update relations if graph was tagged for update.
 */
void DEG_scene_relations_update(Main *bmain, Scene *scene)
{
	if (scene->depsgraph == NULL) {
		/* Rebuild graph from scratch and exit. */
		scene->depsgraph = DEG_graph_new();
		DEG_graph_build_from_scene(scene->depsgraph, bmain, scene);
		return;
	}

	Depsgraph *graph = scene->depsgraph;
	if (!graph->need_update) {
		/* Graph is up to date, nothing to do. */
		return;
	}

	/* Clear all previous nodes and operations. */
	graph->clear_all_nodes();
	graph->operations.clear();
	graph->entry_tags.clear();

	/* Build new nodes and relations. */
	DEG_graph_build_from_scene(graph, bmain, scene);

	graph->need_update = false;
}

/* Rebuild dependency graph only for a given scene. */
void DEG_scene_relations_rebuild(Main *bmain, Scene *scene)
{
	if (scene->depsgraph != NULL) {
		DEG_graph_tag_relations_update(scene->depsgraph);
	}
	DEG_scene_relations_update(bmain, scene);
}

void DEG_scene_graph_free(Scene *scene)
{
	if (scene->depsgraph) {
		DEG_graph_free(scene->depsgraph);
		scene->depsgraph = NULL;
	}
}
