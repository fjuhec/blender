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
 * The Original Code is Copyright (C) 2016 Blender Foundation.
 * All rights reserved.
 *
 * Original Author: Sergey Sharybin
 * Contributor(s): None Yet
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include "depsnode_opcodes.h"

#include <cstdlib>  // for BLI_assert()

#include "BLI_utildefines.h"

DepsOperationStringifier DEG_OPNAMES;

static const char *stringify_opcode(eDepsOperation_Code opcode)
{
	switch (opcode) {
#define STRINGIFY_OPCODE(name) case DEG_OPCODE_##name: return #name
		STRINGIFY_OPCODE(OPERATION);
		STRINGIFY_OPCODE(PLACEHOLDER);
		STRINGIFY_OPCODE(NOOP);
		STRINGIFY_OPCODE(ANIMATION);
		STRINGIFY_OPCODE(DRIVER);
		//STRINGIFY_OPCODE(PROXY);
		STRINGIFY_OPCODE(TRANSFORM_LOCAL);
		STRINGIFY_OPCODE(TRANSFORM_PARENT);
		STRINGIFY_OPCODE(TRANSFORM_CONSTRAINTS);
		//STRINGIFY_OPCODE(TRANSFORM_CONSTRAINTS_INIT);
		//STRINGIFY_OPCODE(TRANSFORM_CONSTRAINT);
		//STRINGIFY_OPCODE(TRANSFORM_CONSTRAINTS_DONE);
		STRINGIFY_OPCODE(RIGIDBODY_REBUILD);
		STRINGIFY_OPCODE(RIGIDBODY_SIM);
		STRINGIFY_OPCODE(TRANSFORM_RIGIDBODY);
		STRINGIFY_OPCODE(TRANSFORM_FINAL);
		STRINGIFY_OPCODE(OBJECT_UBEREVAL);
		STRINGIFY_OPCODE(GEOMETRY_UBEREVAL);
		STRINGIFY_OPCODE(GEOMETRY_MODIFIER);
		STRINGIFY_OPCODE(GEOMETRY_PATH);
		STRINGIFY_OPCODE(POSE_INIT);
		STRINGIFY_OPCODE(POSE_DONE);
		STRINGIFY_OPCODE(POSE_IK_SOLVER);
		STRINGIFY_OPCODE(POSE_SPLINE_IK_SOLVER);
		STRINGIFY_OPCODE(BONE_LOCAL);
		STRINGIFY_OPCODE(BONE_POSE_PARENT);
		STRINGIFY_OPCODE(BONE_CONSTRAINTS);
		//STRINGIFY_OPCODE(BONE_CONSTRAINTS_INIT);
		//STRINGIFY_OPCODE(BONE_CONSTRAINT);
		//STRINGIFY_OPCODE(BONE_CONSTRAINTS_DONE);
		STRINGIFY_OPCODE(BONE_READY);
		STRINGIFY_OPCODE(BONE_DONE);
		STRINGIFY_OPCODE(PSYS_EVAL);

		case DEG_NUM_OPCODES: return "SpecialCase";
#undef STRINGIFY_OPCODE
	}
	return "UNKNOWN";
}

DepsOperationStringifier::DepsOperationStringifier() {
	for (int i = 0; i < DEG_NUM_OPCODES; ++i) {
		names_[i] = stringify_opcode((eDepsOperation_Code)i);
	}
}

const char *DepsOperationStringifier::operator[](eDepsOperation_Code opcode) {
	BLI_assert((opcode > 0) && (opcode < DEG_NUM_OPCODES));
	if (opcode >= 0 && opcode < DEG_NUM_OPCODES) {
		return names_[opcode];
	}
	return "UnknownOpcode";
}
