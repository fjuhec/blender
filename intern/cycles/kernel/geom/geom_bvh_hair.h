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
                                                         int nodeAddr,
                                                         int child)
{
	Transform aligned_space;
	if(child == 0) {
		aligned_space.x = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+0);
		aligned_space.y = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+1);
		aligned_space.z = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+2);
		aligned_space.w = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+3);
	}
	else {
		aligned_space.x = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+4);
		aligned_space.y = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+5);
		aligned_space.z = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+6);
		aligned_space.w = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+7);
	}
	return aligned_space;
}

#if !defined(__KERNEL_SSE2__)
ccl_device_inline bool bvh_hair_intersect_child(KernelGlobals *kg,
                                                const float3 P,
                                                const float3 dir,
                                                const float t,
                                                const float difl,
                                                const float extmax,
                                                int nodeAddr,
                                                int child,
                                                float *dist)
{
	Transform aligned_space  = bvh_hair_fetch_aligned_space(kg, nodeAddr, child);
	float3 aligned_dir = transform_direction(&aligned_space, dir);
	float3 aligned_P = transform_point(&aligned_space, P);
	float3 nrdir = -1.0f * bvh_inverse_direction(aligned_dir);
	/* TODO(sergey): Do we need NO_EXTENDED_PRECISION here as well? */
	float3 tLowerXYZ = make_float3(aligned_P.x * nrdir.x,
	                               aligned_P.y * nrdir.y,
	                               aligned_P.z * nrdir.z);
	float3 tUpperXYZ = tLowerXYZ - nrdir;
	NO_EXTENDED_PRECISION const float tNearX = min(tLowerXYZ.x, tUpperXYZ.x);
	NO_EXTENDED_PRECISION const float tNearY = min(tLowerXYZ.y, tUpperXYZ.y);
	NO_EXTENDED_PRECISION const float tNearZ = min(tLowerXYZ.z, tUpperXYZ.z);
	NO_EXTENDED_PRECISION const float tFarX  = max(tLowerXYZ.x, tUpperXYZ.x);
	NO_EXTENDED_PRECISION const float tFarY  = max(tLowerXYZ.y, tUpperXYZ.y);
	NO_EXTENDED_PRECISION const float tFarZ  = max(tLowerXYZ.z, tUpperXYZ.z);
	NO_EXTENDED_PRECISION const float tNear  = max4(0.0f, tNearX, tNearY, tNearZ);
	NO_EXTENDED_PRECISION const float tFar   = min4(t, tFarX, tFarY, tFarZ);
	*dist = tNear;
	if(difl != 0.0f) {
		/* TODO(sergey): Same as for QBVH, needs a proper use. */
		(void)extmax;
		const float round_down = 1.0f - difl;
		const float round_up = 1.0f + difl;
		return round_down*tNear <= round_up*tFar;
	}
	else {
		return tNear <= tFar;
	}
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
	/* TODO(sergey): Add visibility check. */
	if(bvh_hair_intersect_child(kg, P, dir, t, difl, extmax, nodeAddr, 0, &dist[0])) {
		mask |= 1;
	}
	if(bvh_hair_intersect_child(kg, P, dir, t, difl, extmax, nodeAddr, 1, &dist[1])) {
		mask |= 2;
	}
	return mask;
}
#else  /* !defined(__KERNEL_SSE2__) */
int ccl_device bvh_hair_intersect_node_unaligned(KernelGlobals *kg,
                                                 const float3 P,
                                                 const float3 dir,
                                                 const ssef& tnear,
                                                 const ssef& tfar,
                                                 const float difl,
                                                 const float extmax,
                                                 const uint visibility,
                                                 int nodeAddr,
                                                 float dist[2])
{
	Transform aligned_space0 = bvh_hair_fetch_aligned_space(kg, nodeAddr, 0);
	Transform aligned_space1 = bvh_hair_fetch_aligned_space(kg, nodeAddr, 1);

	float3 aligned_dir0 = transform_direction(&aligned_space0, dir),
	       aligned_dir1 = transform_direction(&aligned_space1, dir);;
	float3 aligned_P0 = transform_point(&aligned_space0, P),
	       aligned_P1 = transform_point(&aligned_space1, P);
	float3 nrdir0 = -1.0f * bvh_inverse_direction(aligned_dir0),
	       nrdir1 = -1.0f * bvh_inverse_direction(aligned_dir1);

	ssef tLowerX = ssef(aligned_P0.x * nrdir0.x,
	                    aligned_P1.x * nrdir1.x,
	                    0.0f, 0.0f),
	     tLowerY = ssef(aligned_P0.y * nrdir0.y,
	                    aligned_P1.y * nrdir1.y,
	                    0.0f,
	                    0.0f),
	     tLowerZ = ssef(aligned_P0.z * nrdir0.z,
	                    aligned_P1.z * nrdir1.z,
	                    0.0f,
	                    0.0f);

	ssef tUpperX = tLowerX - ssef(nrdir0.x, nrdir1.x, 0.0f, 0.0f),
	     tUpperY = tLowerY - ssef(nrdir0.y, nrdir1.y, 0.0f, 0.0f),
	     tUpperZ = tLowerZ - ssef(nrdir0.z, nrdir1.z, 0.0f, 0.0f);

	ssef tnear_x = min(tLowerX, tUpperX);
	ssef tnear_y = min(tLowerY, tUpperY);
	ssef tnear_z = min(tLowerZ, tUpperZ);
	ssef tfar_x = max(tLowerX, tUpperX);
	ssef tfar_y = max(tLowerY, tUpperY);
	ssef tfar_z = max(tLowerZ, tUpperZ);

	const ssef tNear = max4(tnear_x, tnear_y, tnear_z, tnear);
	const ssef tFar = min4(tfar_x, tfar_y, tfar_z, tfar);
	const sseb vmask = tNear <= tFar;

	dist[0] = tNear.f[0];
	dist[1] = tNear.f[1];

	int mask = (int)movemask(vmask);

#  ifdef __VISIBILITY_FLAG__
	/* this visibility test gives a 5% performance hit, how to solve? */
	float4 cnodes = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+8);
	int cmask = (((mask & 1) && (__float_as_uint(cnodes.z) & visibility))? 1: 0) |
	            (((mask & 2) && (__float_as_uint(cnodes.w) & visibility))? 2: 0);
	return cmask;
#  else
	return mask & 3;
#  endif
}

int ccl_device_inline bvh_hair_intersect_node_aligned(KernelGlobals *kg,
                                               const float3& P,
                                               const float3& dir,
                                               const ssef& tsplat,
                                               const ssef Psplat[3],
                                               const ssef idirsplat[3],
                                               const shuffle_swap_t shufflexyz[3],
                                               const float difl,
                                               const float extmax,
                                               const uint visibility,
                                               int nodeAddr,
                                               float dist[2])
{
	/* Intersect two child bounding boxes, SSE3 version adapted from Embree */
	const ssef pn = cast(ssei(0, 0, 0x80000000, 0x80000000));

	/* fetch node data */
	const ssef *bvh_nodes = (ssef*)kg->__bvh_curve_nodes.data + nodeAddr*BVH_UNALIGNED_NODE_SIZE;

	/* intersect ray against child nodes */
	const ssef tminmaxx = (shuffle_swap(bvh_nodes[0], shufflexyz[0]) - Psplat[0]) * idirsplat[0];
	const ssef tminmaxy = (shuffle_swap(bvh_nodes[1], shufflexyz[1]) - Psplat[1]) * idirsplat[1];
	const ssef tminmaxz = (shuffle_swap(bvh_nodes[2], shufflexyz[2]) - Psplat[2]) * idirsplat[2];

	/* calculate { c0min, c1min, -c0max, -c1max} */
	ssef minmax = max(max(tminmaxx, tminmaxy), max(tminmaxz, tsplat));
	const ssef tminmax = minmax ^ pn;
	const sseb lrhit = tminmax <= shuffle<2, 3, 0, 1>(tminmax);

	dist[0] = tminmax[0];
	dist[1] = tminmax[1];

	int mask = movemask(lrhit);

#  ifdef __VISIBILITY_FLAG__
	/* this visibility test gives a 5% performance hit, how to solve? */
	float4 cnodes = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+8);
	int cmask = (((mask & 1) && (__float_as_uint(cnodes.z) & visibility))? 1: 0) |
	            (((mask & 2) && (__float_as_uint(cnodes.w) & visibility))? 2: 0);
	return cmask;
#  else
	return mask & 3;
#  endif
}

int ccl_device_inline bvh_hair_intersect_node(KernelGlobals *kg,
                                              const float3& P,
                                              const float3& dir,
                                              const ssef& tnear,
                                              const ssef& tfar,
                                              const ssef& tsplat,
                                              const ssef Psplat[3],
                                              const ssef idirsplat[3],
                                              const shuffle_swap_t shufflexyz[3],
                                              const float difl,
                                              const float extmax,
                                              const uint visibility,
                                              int nodeAddr,
                                              float dist[2])
{
	float4 node = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+7);
	if(node.w != 0.0f) {
		return bvh_hair_intersect_node_unaligned(kg,
		                                         P,
		                                         dir,
		                                         tnear,
		                                         tfar,
		                                         difl,
		                                         extmax,
		                                         visibility,
		                                         nodeAddr,
		                                         dist);
	}
	else {
		return bvh_hair_intersect_node_aligned(kg,
		                                       P,
		                                       dir,
		                                       tsplat,
		                                       Psplat,
		                                       idirsplat,
		                                       shufflexyz,
		                                       difl,
		                                       extmax,
		                                       visibility,
		                                       nodeAddr,
		                                       dist);
	}
}

#endif  /* !defined(__KERNEL_SSE2__) */
