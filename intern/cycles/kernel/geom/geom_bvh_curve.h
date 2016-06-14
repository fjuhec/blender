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

ccl_device_inline Transform bvh_curve_fetch_aligned_space(KernelGlobals *kg,
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
ccl_device_inline bool bvh_curve_intersect_unaligned_child(KernelGlobals *kg,
                                                           const float3 P,
                                                           const float3 dir,
                                                           const float t,
                                                           const float difl,
                                                           int nodeAddr,
                                                           int child,
                                                           float *dist)
{
	Transform aligned_space  = bvh_curve_fetch_aligned_space(kg, nodeAddr, child);
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
		const float round_down = 1.0f - difl;
		const float round_up = 1.0f + difl;
		return round_down*tNear <= round_up*tFar;
	}
	else {
		return tNear <= tFar;
	}
}

ccl_device_inline int bvh_curve_intersect_aligned(KernelGlobals *kg,
                                                  const float3 P,
                                                  const float3 idir,
                                                  const float t,
                                                  const float difl,
                                                  int nodeAddr,
                                                  const uint visibility,
                                                  float *dist)
{

	/* fetch node data */
	float4 node0 = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+0);
	float4 node1 = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+1);
	float4 node2 = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+2);
	float4 cnodes = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+8);

	/* intersect ray against child nodes */
	NO_EXTENDED_PRECISION float c0lox = (node0.x - P.x) * idir.x;
	NO_EXTENDED_PRECISION float c0hix = (node0.z - P.x) * idir.x;
	NO_EXTENDED_PRECISION float c0loy = (node1.x - P.y) * idir.y;
	NO_EXTENDED_PRECISION float c0hiy = (node1.z - P.y) * idir.y;
	NO_EXTENDED_PRECISION float c0loz = (node2.x - P.z) * idir.z;
	NO_EXTENDED_PRECISION float c0hiz = (node2.z - P.z) * idir.z;
	NO_EXTENDED_PRECISION float c0min = max4(min(c0lox, c0hix), min(c0loy, c0hiy), min(c0loz, c0hiz), 0.0f);
	NO_EXTENDED_PRECISION float c0max = min4(max(c0lox, c0hix), max(c0loy, c0hiy), max(c0loz, c0hiz), t);

	NO_EXTENDED_PRECISION float c1lox = (node0.y - P.x) * idir.x;
	NO_EXTENDED_PRECISION float c1hix = (node0.w - P.x) * idir.x;
	NO_EXTENDED_PRECISION float c1loy = (node1.y - P.y) * idir.y;
	NO_EXTENDED_PRECISION float c1hiy = (node1.w - P.y) * idir.y;
	NO_EXTENDED_PRECISION float c1loz = (node2.y - P.z) * idir.z;
	NO_EXTENDED_PRECISION float c1hiz = (node2.w - P.z) * idir.z;
	NO_EXTENDED_PRECISION float c1min = max4(min(c1lox, c1hix), min(c1loy, c1hiy), min(c1loz, c1hiz), 0.0f);
	NO_EXTENDED_PRECISION float c1max = min4(max(c1lox, c1hix), max(c1loy, c1hiy), max(c1loz, c1hiz), t);

	if(difl != 0.0f) {
		float hdiff = 1.0f + difl;
		float ldiff = 1.0f - difl;
		if(__float_as_int(cnodes.z) & PATH_RAY_CURVE) {
			c0min *= ldiff;
			c0max *= hdiff;
		}
		if(__float_as_int(cnodes.w) & PATH_RAY_CURVE) {
			c1min *= ldiff;
			c1max *= hdiff;
		}
	}

	dist[0] = c0min;
	dist[1] = c1min;

#ifdef __VISIBILITY_FLAG__
	/* this visibility test gives a 5% performance hit, how to solve? */
	return (((c0max >= c0min) && (__float_as_uint(cnodes.z) & visibility))? 1: 0) |
	       (((c1max >= c1min) && (__float_as_uint(cnodes.w) & visibility))? 2: 0);
#else
	return ((c0max >= c0min)? 1: 0) |
	       ((c1max >= c1min)? 2: 0);
#endif
}

int ccl_device bvh_curve_intersect_node(KernelGlobals *kg,
                                        const float3 P,
                                        const float3 dir,
                                        const float3 idir,
                                        const float t,
                                        const float difl,
                                        const uint visibility,
                                        int nodeAddr,
                                        float dist[2])
{
	int mask = 0;
	float4 node = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+7);
	if(node.w != 0.0f) {
		float4 cnodes = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+8);
		if(bvh_curve_intersect_unaligned_child(kg, P, dir, t, difl, nodeAddr, 0, &dist[0])) {
			if((__float_as_uint(cnodes.z) & visibility)) {
				mask |= 1;
			}
		}
		if(bvh_curve_intersect_unaligned_child(kg, P, dir, t, difl, nodeAddr, 1, &dist[1])) {
			if((__float_as_uint(cnodes.w) & visibility)) {
				mask |= 2;
			}
		}
	}
	else {
		return bvh_curve_intersect_aligned(kg,
		                                   P,
		                                   idir,
		                                   t,
		                                   difl,
		                                   nodeAddr,
		                                   visibility,
		                                   dist);
	}
	return mask;
}
#else  /* !defined(__KERNEL_SSE2__) */
int ccl_device bvh_curve_intersect_node_unaligned(KernelGlobals *kg,
                                                  const float3 P,
                                                  const float3 dir,
                                                  const ssef& tnear,
                                                  const ssef& tfar,
                                                  const float difl,
                                                  const uint visibility,
                                                  int nodeAddr,
                                                  float dist[2])
{
	Transform aligned_space0 = bvh_curve_fetch_aligned_space(kg, nodeAddr, 0);
	Transform aligned_space1 = bvh_curve_fetch_aligned_space(kg, nodeAddr, 1);

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
	sseb vmask;
	if(difl != 0.0f) {
		const float round_down = 1.0f - difl;
		const float round_up = 1.0f + difl;
		vmask = round_down*tNear <= round_up*tFar;
	}
	else {
		vmask = tNear <= tFar;
	}

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

int ccl_device_inline bvh_curve_intersect_node_aligned(KernelGlobals *kg,
                                                       const float3& P,
                                                       const float3& dir,
                                                       const ssef& tsplat,
                                                       const ssef Psplat[3],
                                                       const ssef idirsplat[3],
                                                       const shuffle_swap_t shufflexyz[3],
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

int ccl_device_inline bvh_curve_intersect_node(KernelGlobals *kg,
                                               const float3& P,
                                               const float3& dir,
                                               const ssef& tnear,
                                               const ssef& tfar,
                                               const ssef& tsplat,
                                               const ssef Psplat[3],
                                               const ssef idirsplat[3],
                                               const shuffle_swap_t shufflexyz[3],
                                               const float difl,
                                               const uint visibility,
                                               int nodeAddr,
                                               float dist[2])
{
	float4 node = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+7);
	if(node.w != 0.0f) {
		return bvh_curve_intersect_node_unaligned(kg,
		                                          P,
		                                          dir,
		                                          tnear,
		                                          tfar,
		                                          difl,
		                                          visibility,
		                                          nodeAddr,
		                                          dist);
	}
	else {
		return bvh_curve_intersect_node_aligned(kg,
		                                        P,
		                                        dir,
		                                        tsplat,
		                                        Psplat,
		                                        idirsplat,
		                                        shufflexyz,
		                                        visibility,
		                                        nodeAddr,
		                                        dist);
	}
}

#endif  /* !defined(__KERNEL_SSE2__) */
