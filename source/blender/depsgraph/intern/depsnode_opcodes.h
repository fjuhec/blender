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
 * The Original Code is Copyright (C) 2014 Blender Foundation.
 * All rights reserved.
 *
 * Original Author: Joshua Leung
 * Contributor(s): None Yet
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/depsgraph/intern/depsnode_opcodes.h
 *  \ingroup depsgraph
 *
 * \par OpCodes for OperationDepsNodes
 *
 * This file defines all the "operation codes" (opcodes) used to identify
 * common operation node types. The intention of these defines is to have
 * a fast and reliable way of identifying the relevant nodes within a component
 * without having to use fragile dynamic strings.
 *
 * This file is meant to be used like UI_icons.h. That is, before including
 * the file, the host file must define the DEG_OPCODE(_label) macro, which
 * is responsible for converting the define into whatever form is suitable.
 * Therefore, it intentionally doesn't have header guards.
 */


typedef enum eDepsOperation_Code {
	/* Generic Operations ------------------------------ */

	/* Placeholder for operations which don't need special mention */
	DEG_OPCODE_OPERATION = 0,

	// XXX: Placeholder while porting depsgraph code
	DEG_OPCODE_PLACEHOLDER,

	DEG_OPCODE_NOOP,

	/* Animation, Drivers, etc. ------------------------ */

	/* NLA + Action */
	DEG_OPCODE_ANIMATION,

	/* Driver */
	DEG_OPCODE_DRIVER,

	/* Proxy Inherit? */
	//DEG_OPCODE_PROXY,

	/* Transform --------------------------------------- */

	/* Transform entry point - local transforms only */
	DEG_OPCODE_TRANSFORM_LOCAL,

	/* Parenting */
	DEG_OPCODE_TRANSFORM_PARENT,

	/* Constraints */
	DEG_OPCODE_TRANSFORM_CONSTRAINTS,
	//DEG_OPCODE_TRANSFORM_CONSTRAINTS_INIT,
	//DEG_OPCODE_TRANSFORM_CONSTRAINT,
	//DEG_OPCODE_TRANSFORM_CONSTRAINTS_DONE,

	/* Rigidbody Sim - Perform Sim */
	DEG_OPCODE_RIGIDBODY_REBUILD,
	DEG_OPCODE_RIGIDBODY_SIM,

	/* Rigidbody Sim - Copy Results to Object */
	DEG_OPCODE_TRANSFORM_RIGIDBODY,

	/* Transform exitpoint */
	DEG_OPCODE_TRANSFORM_FINAL,

	/* XXX: ubereval is for temporary porting purposes only */
	DEG_OPCODE_OBJECT_UBEREVAL,

	/* Geometry ---------------------------------------- */

	/* XXX: Placeholder - UberEval */
	DEG_OPCODE_GEOMETRY_UBEREVAL,

	/* Modifier */
	DEG_OPCODE_GEOMETRY_MODIFIER,

	/* Curve Objects - Path Calculation (used for path-following tools, */
	DEG_OPCODE_GEOMETRY_PATH,

	/* Pose -------------------------------------------- */

	/* Init IK Trees, etc. */
	DEG_OPCODE_POSE_INIT,

	/* Free IK Trees + Compute Deform Matrices */
	DEG_OPCODE_POSE_DONE,

	/* IK/Spline Solvers */
	DEG_OPCODE_POSE_IK_SOLVER,
	DEG_OPCODE_POSE_SPLINE_IK_SOLVER,

	/* Bone -------------------------------------------- */

	/* Bone local transforms - Entrypoint */
	DEG_OPCODE_BONE_LOCAL,

	/* Pose-space conversion (includes parent + restpose, */
	DEG_OPCODE_BONE_POSE_PARENT,

	/* Constraints */
	DEG_OPCODE_BONE_CONSTRAINTS,
	//DEG_OPCODE_BONE_CONSTRAINTS_INIT,
	//DEG_OPCODE_BONE_CONSTRAINT,
	//DEG_OPCODE_BONE_CONSTRAINTS_DONE,

	/* Bone transforms are ready
	 *
	 * - "READY"  This (internal, noop is used to signal that all pre-IK
	 *            operations are done. Its role is to help mediate situations
	 *            where cyclic relations may otherwise form (i.e. one bone in
	 *            chain targetting another in same chain,
	 *
	 * - "DONE"   This noop is used to signal that the bone's final pose
	 *            transform can be read by others
	 */
	// TODO: deform mats could get calculated in the final_transform ops...
	DEG_OPCODE_BONE_READY,
	DEG_OPCODE_BONE_DONE,

	/* Particles --------------------------------------- */

	/* XXX: placeholder - Particle System eval */
	DEG_OPCODE_PSYS_EVAL,

	DEG_NUM_OPCODES,
} eDepsOperation_Code;

class DepsOperationStringifier {
public:
	DepsOperationStringifier();
	const char *operator[](eDepsOperation_Code opcodex);
protected:
	const char *names_[DEG_NUM_OPCODES];
};

/* String defines for these opcodes, defined in depsnode_operation.cpp */
extern DepsOperationStringifier DEG_OPNAMES;
