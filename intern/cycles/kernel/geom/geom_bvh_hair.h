/*
 * Copyright 2011-2016, Blender Foundation.
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

ccl_device_inline Transform bvh_hair_fetch_aligned_space(KernelGlobals *kg,
                                                         int nodeAddr)
{
	Transform aligned_space;
	if(nodeAddr >= 0) {
		aligned_space.x = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+0);
		aligned_space.y = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+1);
		aligned_space.z = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+2);
		aligned_space.w = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+3);
	}
	else {
		int leafAddr = -nodeAddr-1;
		aligned_space.x = kernel_tex_fetch(__bvh_curve_leaf_nodes, leafAddr*BVH_UNALIGNED_NODE_LEAF_SIZE+1);
		aligned_space.y = kernel_tex_fetch(__bvh_curve_leaf_nodes, leafAddr*BVH_UNALIGNED_NODE_LEAF_SIZE+2);
		aligned_space.z = kernel_tex_fetch(__bvh_curve_leaf_nodes, leafAddr*BVH_UNALIGNED_NODE_LEAF_SIZE+3);
		aligned_space.w = kernel_tex_fetch(__bvh_curve_leaf_nodes, leafAddr*BVH_UNALIGNED_NODE_LEAF_SIZE+4);
	}
	return aligned_space;
}

ccl_device_inline bool bvh_hair_intersect_single_node(KernelGlobals *kg,
                                                      const float3 P,
                                                      const float3 dir,
                                                      const float t,
                                                      const float difl,
                                                      const float extmax,
                                                      int nodeAddr,
                                                      float *dist)
{
	Transform aligned_space  = bvh_hair_fetch_aligned_space(kg, nodeAddr);
	float3 aligned_dir = transform_direction(&aligned_space, dir);
	float3 aligned_P = transform_point(&aligned_space, P);
	float3 idir = bvh_inverse_direction(aligned_dir);

	float4 node4, node5;
	if(nodeAddr >= 0) {
		node4 = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+4);
		node5 = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+5);
	}
	else {
		int leafAddr = -nodeAddr-1;
		node4 = kernel_tex_fetch(__bvh_curve_leaf_nodes, leafAddr*BVH_UNALIGNED_NODE_LEAF_SIZE+5);
		node5 = kernel_tex_fetch(__bvh_curve_leaf_nodes, leafAddr*BVH_UNALIGNED_NODE_LEAF_SIZE+6);
	}

	NO_EXTENDED_PRECISION float c0lox = (node4.x - aligned_P.x) * idir.x;
	NO_EXTENDED_PRECISION float c0hix = (node5.x - aligned_P.x) * idir.x;
	NO_EXTENDED_PRECISION float c0loy = (node4.y - aligned_P.y) * idir.y;
	NO_EXTENDED_PRECISION float c0hiy = (node5.y - aligned_P.y) * idir.y;
	NO_EXTENDED_PRECISION float c0loz = (node4.z - aligned_P.z) * idir.z;
	NO_EXTENDED_PRECISION float c0hiz = (node5.z - aligned_P.z) * idir.z;
	NO_EXTENDED_PRECISION float c0min = max4(min(c0lox, c0hix), min(c0loy, c0hiy), min(c0loz, c0hiz), 0.0f);
	NO_EXTENDED_PRECISION float c0max = min4(max(c0lox, c0hix), max(c0loy, c0hiy), max(c0loz, c0hiz), t);

	if(difl != 0.0f) {
		float hdiff = 1.0f + difl;
		float ldiff = 1.0f - difl;
		c0min = max(ldiff * c0min, c0min - extmax);
		c0max = min(hdiff * c0max, c0max + extmax);
	}

	if(dist != NULL) {
		*dist = c0min;
	}

	return (c0max >= c0min);
}

int ccl_device bvh_hair_intersect_node(KernelGlobals *kg,
                                       const float3 P,
                                       const float3 dir,
                                       const float t,
                                       const float difl,
                                       const float extmax,
                                       const uint visibility,
                                       int nodeAddr,
                                       float dist[2])
{
	int mask = 0;
	if(bvh_hair_intersect_single_node(kg, P, dir, t, difl, extmax, nodeAddr, NULL)) {
		float4 cnodes = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+6);
		int nodeAddrChild0 = __float_as_int(cnodes.x);
		int nodeAddrChild1 = __float_as_int(cnodes.y);
		if(bvh_hair_intersect_single_node(kg, P, dir, t, difl, extmax, nodeAddrChild0, &dist[0])) {
			if(__float_as_uint(cnodes.z) & visibility) {
				mask |= 1;
			}
		}
		if(bvh_hair_intersect_single_node(kg, P, dir, t, difl, extmax, nodeAddrChild1, &dist[1])) {
			if(__float_as_uint(cnodes.w) & visibility) {
				mask |= 2;
			}
		}
	}
	return mask;
}
