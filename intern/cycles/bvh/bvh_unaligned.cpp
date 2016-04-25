/*
 * Copyright 2011-2016 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "bvh_unaligned.h"

#include "mesh.h"
#include "object.h"

#include "bvh_binning.h"

#include "util_boundbox.h"
#include "util_debug.h"
#include "util_transform.h"

CCL_NAMESPACE_BEGIN


BVHUnaligned::BVHUnaligned(const vector<Object*>& objects)
        : objects_(objects)
{
}

Transform BVHUnaligned::compute_aligned_space(
        const BVHObjectBinning& range,
        const vector<BVHReference>& references) const
{
	for(int i = range.start(); i < range.end(); ++i) {
		const BVHReference& ref = references[i];
		Transform aligned_space;
		/* Use first primitive which defines correct direction to define
		 * the orientation space.
		 */
		if(compute_aligned_space(ref, &aligned_space)) {
			return aligned_space;
		}
	}
	return make_trasnform_frame(make_float3(0.0f, 0.0f, 1.0f));
}

bool BVHUnaligned::compute_aligned_space(const BVHReference& ref,
                                         Transform *aligned_space) const
{
	const Object *object = objects_[ref.prim_object()];
	const int packed_type = ref.prim_type();
	const int type = (packed_type & PRIMITIVE_ALL);
	if(type & PRIMITIVE_CURVE) {
		const int curve_index = ref.prim_index();
		const int segment = PRIMITIVE_UNPACK_SEGMENT(packed_type);
		const Mesh *mesh = object->mesh;
		const Mesh::Curve& curve = mesh->curves[curve_index];
		const int key = curve.first_key + segment;
		const float3 v1 = float4_to_float3(mesh->curve_keys[key]),
		             v2 = float4_to_float3(mesh->curve_keys[key + 1]);
		float length;
		const float3 axis = normalize_len(v2 - v1, &length);
		if(length > 1e-6f) {
			*aligned_space = make_trasnform_frame(axis);
			return true;
		}
	}
	*aligned_space = make_trasnform_frame(make_float3(0.0f, 0.0f, 1.0f));
	return false;
}

BoundBox BVHUnaligned::compute_aligned_prim_boundbox(
        const BVHReference& prim,
        const Transform& aligned_space) const
{
	BoundBox bounds = BoundBox::empty;
	const Object *object = objects_[prim.prim_object()];
	const int packed_type = prim.prim_type();
	const int type = (packed_type & PRIMITIVE_ALL);
	if(type & PRIMITIVE_CURVE) {
		const int curve_index = prim.prim_index();
		const int segment = PRIMITIVE_UNPACK_SEGMENT(packed_type);
		const Mesh *mesh = object->mesh;
		const Mesh::Curve& curve = mesh->curves[curve_index];
		curve.bounds_grow(segment, &mesh->curve_keys[0], aligned_space, bounds);
	}
	else {
		bounds = prim.bounds().transformed(&aligned_space);
	}
	return bounds;
}

BoundBox BVHUnaligned::compute_aligned_boundbox(
        const BVHObjectBinning& range,
        const vector<BVHReference>& references,
        const Transform& aligned_space) const
{
	BoundBox bounds = BoundBox::empty;
	for(int i = range.start(); i < range.end(); ++i) {
		const BVHReference& ref = references[i];
		BoundBox ref_bounds = compute_aligned_prim_boundbox(ref, aligned_space);
		bounds.grow(ref_bounds);
	}
	return bounds;
}

CCL_NAMESPACE_END
