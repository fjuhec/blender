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
 * Contributor(s): None Yet
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/depsgraph/intern/depsnode.cc
 *  \ingroup depsgraph
 */

#include "intern/nodes/deg_node.h"

#include <stdio.h>
#include <string.h>

#include "BLI_utildefines.h"

extern "C" {
#include "DNA_ID.h"
#include "DNA_anim_types.h"

#include "BKE_animsys.h"

#include "DEG_depsgraph.h"
}

#include "intern/nodes/deg_node_component.h"
#include "intern/nodes/deg_node_operation.h"
#include "depsgraph_intern.h"
#include "depsgraph_util_foreach.h"

/* *************** */
/* Node Management */

/* Add ------------------------------------------------ */

DepsNode::TypeInfo::TypeInfo(eDepsNode_Type type, const char *tname)
{
	this->type = type;
	if (type == DEPSNODE_TYPE_OPERATION)
		this->tclass = DEPSNODE_CLASS_OPERATION;
	else if (type < DEPSNODE_TYPE_PARAMETERS)
		this->tclass = DEPSNODE_CLASS_GENERIC;
	else
		this->tclass = DEPSNODE_CLASS_COMPONENT;
	this->tname = tname;
}

DepsNode::DepsNode()
{
	this->name[0] = '\0';
}

DepsNode::~DepsNode()
{
	/* Free links. */
	/* NOTE: We only free incoming links. This is to avoid double-free of links
	 * when we're trying to free same link from both it's sides. We don't have
	 * dangling links so this is not a problem from memory leaks point of view.
	 */
	foreach (DepsRelation *rel, inlinks) {
		OBJECT_GUARDED_DELETE(rel, DepsRelation);
	}
}


/* Generic identifier for Depsgraph Nodes. */
string DepsNode::identifier() const
{
	char typebuf[7];
	sprintf(typebuf, "(%d)", type);

	return string(typebuf) + " : " + name;
}

/* ************* */
/* Generic Nodes */

/* Time Source Node ============================================== */

void TimeSourceDepsNode::tag_update(Depsgraph *graph)
{
	foreach (DepsRelation *rel, outlinks) {
		DepsNode *node = rel->to;
		node->tag_update(graph);
	}
}


/* Root Node ============================================== */

RootDepsNode::RootDepsNode() : scene(NULL), time_source(NULL)
{
}

RootDepsNode::~RootDepsNode()
{
	OBJECT_GUARDED_DELETE(time_source, TimeSourceDepsNode);
}

TimeSourceDepsNode *RootDepsNode::add_time_source(const string &name)
{
	if (!time_source) {
		DepsNodeFactory *factory = DEG_get_node_factory(DEPSNODE_TYPE_TIMESOURCE);
		time_source = (TimeSourceDepsNode *)factory->create_node(NULL, "", name);
		/*time_source->owner = this;*/ // XXX
	}
	return time_source;
}

DEG_DEPSNODE_DEFINE(RootDepsNode, DEPSNODE_TYPE_ROOT, "Root DepsNode");
static DepsNodeFactoryImpl<RootDepsNode> DNTI_ROOT;

/* Time Source Node ======================================= */

DEG_DEPSNODE_DEFINE(TimeSourceDepsNode, DEPSNODE_TYPE_TIMESOURCE, "Time Source");
static DepsNodeFactoryImpl<TimeSourceDepsNode> DNTI_TIMESOURCE;

/* ID Node ================================================ */

/* Initialize 'id' node - from pointer data given. */
void IDDepsNode::init(const ID *id, const string &UNUSED(subdata))
{
	/* Store ID-pointer. */
	BLI_assert(id != NULL);
	this->id = (ID *)id;
	this->layers = (1 << 20) - 1;
	this->eval_flags = 0;

	/* NOTE: components themselves are created if/when needed.
	 * This prevents problems with components getting added
	 * twice if an ID-Ref needs to be created to house it...
	 */
}

/* Free 'id' node. */
IDDepsNode::~IDDepsNode()
{
	clear_components();
}

ComponentDepsNode *IDDepsNode::find_component(eDepsNode_Type type,
                                              const string &name) const
{
	ComponentIDKey key(type, name);
	ComponentMap::const_iterator it = components.find(key);
	return it != components.end() ? it->second : NULL;
}

ComponentDepsNode *IDDepsNode::add_component(eDepsNode_Type type,
                                             const string &name)
{
	ComponentIDKey key(type, name);
	ComponentDepsNode *comp_node = find_component(type, name);
	if (!comp_node) {
		DepsNodeFactory *factory = DEG_get_node_factory(type);
		comp_node = (ComponentDepsNode *)factory->create_node(this->id, "", name);

		/* Register. */
		this->components[key] = comp_node;
		comp_node->owner = this;
	}
	return comp_node;
}

void IDDepsNode::remove_component(eDepsNode_Type type, const string &name)
{
	ComponentIDKey key(type, name);
	ComponentDepsNode *comp_node = find_component(type, name);
	if (comp_node) {
		/* Unregister. */
		this->components.erase(key);
		OBJECT_GUARDED_DELETE(comp_node, ComponentDepsNode);
	}
}

void IDDepsNode::clear_components()
{
	for (ComponentMap::const_iterator it = components.begin();
	     it != components.end();
	     ++it)
	{
		ComponentDepsNode *comp_node = it->second;
		OBJECT_GUARDED_DELETE(comp_node, ComponentDepsNode);
	}
	components.clear();
}

void IDDepsNode::tag_update(Depsgraph *graph)
{
	for (ComponentMap::const_iterator it = components.begin();
	     it != components.end();
	     ++it)
	{
		ComponentDepsNode *comp_node = it->second;
		/* TODO(sergey): What about drievrs? */
		bool do_component_tag = comp_node->type != DEPSNODE_TYPE_ANIMATION;
		if (comp_node->type == DEPSNODE_TYPE_ANIMATION) {
			AnimData *adt = BKE_animdata_from_id(id);
			/* Animation data might be null if relations are tagged for update. */
			if (adt != NULL && (adt->recalc & ADT_RECALC_ANIM)) {
				do_component_tag = true;
			}
		}
		if (do_component_tag) {
			comp_node->tag_update(graph);
		}
	}
}

DEG_DEPSNODE_DEFINE(IDDepsNode, DEPSNODE_TYPE_ID_REF, "ID Node");
static DepsNodeFactoryImpl<IDDepsNode> DNTI_ID_REF;

/* Subgraph Node ========================================== */

/* Initialize 'subgraph' node - from pointer data given. */
void SubgraphDepsNode::init(const ID *id, const string &UNUSED(subdata))
{
	/* Store ID-ref if provided. */
	this->root_id = (ID *)id;

	/* NOTE: graph will need to be added manually,
	 * as we don't have any way of passing this down.
	 */
}

/* Free 'subgraph' node */
SubgraphDepsNode::~SubgraphDepsNode()
{
	/* Only free if graph not shared, of if this node is the first
	 * reference to it...
	 */
	// XXX: prune these flags a bit...
	if ((this->flag & SUBGRAPH_FLAG_FIRSTREF) || !(this->flag & SUBGRAPH_FLAG_SHARED)) {
		/* Free the referenced graph. */
		DEG_graph_free(this->graph);
		this->graph = NULL;
	}
}

DEG_DEPSNODE_DEFINE(SubgraphDepsNode, DEPSNODE_TYPE_SUBGRAPH, "Subgraph Node");
static DepsNodeFactoryImpl<SubgraphDepsNode> DNTI_SUBGRAPH;

void DEG_register_base_depsnodes()
{
	DEG_register_node_typeinfo(&DNTI_ROOT);
	DEG_register_node_typeinfo(&DNTI_TIMESOURCE);

	DEG_register_node_typeinfo(&DNTI_ID_REF);
	DEG_register_node_typeinfo(&DNTI_SUBGRAPH);
}
