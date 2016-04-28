/*
 * Adapted from code Copyright 2009-2010 NVIDIA Corporation,
 * and code copyright 2009-2012 Intel Corporation
 *
 * Modifications Copyright 2011-2013, Blender Foundation.
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

#ifdef __QBVH__
#  include "geom_qbvh_traversal_hair.h"
#endif

/* This is a template BVH traversal function, where various features can be
 * enabled/disabled. This way we can compile optimized versions for each case
 * without new features slowing things down.
 *
 * BVH_INSTANCING: object instancing
 * BVH_HAIR: hair curve rendering
 * BVH_HAIR_MINIMUM_WIDTH: hair curve rendering with minimum width
 * BVH_MOTION: motion blur rendering
 *
 */

ccl_device bool BVH_FUNCTION_FULL_NAME(BVH)(KernelGlobals *kg,
                                            const Ray *ray,
                                            Intersection *isect,
                                            const uint visibility
#if BVH_FEATURE(BVH_HAIR_MINIMUM_WIDTH)
                                            , uint *lcg_state,
                                            float difl,
                                            float extmax
#endif
                                            )
{
	/* todo:
	 * - test if pushing distance on the stack helps (for non shadow rays)
	 * - separate version for shadow rays
	 * - likely and unlikely for if() statements
	 * - test restrict attribute for pointers
	 */
	
	/* traversal stack in CUDA thread-local memory */
	int traversalStack[BVH_STACK_SIZE];
	traversalStack[0] = ENTRYPOINT_SENTINEL;

	/* traversal variables in registers */
	int stackPtr = 0;
	int nodeAddr = kernel_data.bvh.curve_root;

	/* ray parameters in registers */
	float3 P = ray->P;
	float3 dir = bvh_clamp_direction(ray->D);
	float3 idir = bvh_inverse_direction(dir);
	int object = OBJECT_NONE;

#if BVH_FEATURE(BVH_MOTION)
	Transform ob_itfm;
#endif

	/* traversal loop */
	do {
		do {
			/* traverse internal nodes */
			while(nodeAddr >= 0 && nodeAddr != ENTRYPOINT_SENTINEL) {
				float4 cnodes = kernel_tex_fetch(__bvh_curve_nodes, nodeAddr*BVH_UNALIGNED_NODE_SIZE+8);
				float dist[2];
				int mask = bvh_hair_intersect_node(kg,
				                                   P,
				                                   dir,
				                                   isect->t,
#if BVH_FEATURE(BVH_HAIR_MINIMUM_WIDTH)
				                                   difl,
				                                   extmax,
#endif
				                                   visibility,
				                                   nodeAddr,
				                                   dist);

				if(mask == 0) {
					/* neither child was intersected */
					nodeAddr = traversalStack[stackPtr];
					--stackPtr;
				}
				else {
					int nodeAddrChild0 = __float_as_int(cnodes.x);
					int nodeAddrChild1 = __float_as_int(cnodes.y);
					if(mask == 3) {
						++stackPtr;
						kernel_assert(stackPtr < BVH_STACK_SIZE);
						if(dist[0] < dist[1]) {
							nodeAddr = nodeAddrChild0;
							traversalStack[stackPtr] = nodeAddrChild1;
						}
						else {
							nodeAddr = nodeAddrChild1;
							traversalStack[stackPtr] = nodeAddrChild0;
						}
					}
					else if(mask == 1) {
						nodeAddr = nodeAddrChild0;
					}
					else {
						nodeAddr = nodeAddrChild1;
					}
				}
#if defined(__KERNEL_DEBUG__)
				isect->num_traversal_steps++;
#endif
			}

			/* if node is leaf, fetch triangle list */
			if(nodeAddr < 0) {
				float4 leaf = kernel_tex_fetch(__bvh_curve_leaf_nodes, (-nodeAddr-1)*BVH_UNALIGNED_NODE_LEAF_SIZE);
				int primAddr = __float_as_int(leaf.x);

#if BVH_FEATURE(BVH_INSTANCING)
				if(primAddr >= 0) {
#endif
					const int primAddr2 = __float_as_int(leaf.y);
					const uint type = __float_as_int(leaf.w);

					/* pop */
					nodeAddr = traversalStack[stackPtr];
					--stackPtr;

					/* primitive intersection */
					switch(type & PRIMITIVE_ALL) {
						case PRIMITIVE_CURVE:
						case PRIMITIVE_MOTION_CURVE: {
							for(; primAddr < primAddr2; primAddr++) {
#  if defined(__KERNEL_DEBUG__)
								isect->num_traversal_steps++;
#  endif
								kernel_assert(kernel_tex_fetch(__prim_curve_type, primAddr) == type);
								bool hit;
								if(kernel_data.curve.curveflags & CURVE_KN_INTERPOLATE)
									hit = bvh_cardinal_curve_intersect(kg, isect, P, dir, visibility, object, primAddr, ray->time, type, lcg_state, difl, extmax);
								else
									hit = bvh_curve_intersect(kg, isect, P, dir, visibility, object, primAddr, ray->time, type, lcg_state, difl, extmax);
								if(hit) {
									/* shadow ray early termination */
									if(visibility == PATH_RAY_SHADOW_OPAQUE)
										return true;
								}
							}
							break;
						}
					}
				}
#if BVH_FEATURE(BVH_INSTANCING)
				else {
					/* instance push */
					object = kernel_tex_fetch(__prim_curve_object, -primAddr-1);

#  if BVH_FEATURE(BVH_MOTION)
					bvh_instance_motion_push(kg, object, ray, &P, &dir, &idir, &isect->t, &ob_itfm);
#  else
					bvh_instance_push(kg, object, ray, &P, &dir, &idir, &isect->t);
#  endif

					++stackPtr;
					kernel_assert(stackPtr < BVH_STACK_SIZE);
					traversalStack[stackPtr] = ENTRYPOINT_SENTINEL;

					nodeAddr = kernel_tex_fetch(__object_curve_node, object);

#  if defined(__KERNEL_DEBUG__)
					isect->num_traversed_instances++;
#  endif
				}
			}
#endif  /* FEATURE(BVH_INSTANCING) */
		} while(nodeAddr != ENTRYPOINT_SENTINEL);

#if BVH_FEATURE(BVH_INSTANCING)
		if(stackPtr >= 0) {
			kernel_assert(object != OBJECT_NONE);

			/* instance pop */
#  if BVH_FEATURE(BVH_MOTION)
			bvh_instance_motion_pop(kg, object, ray, &P, &dir, &idir, &isect->t, &ob_itfm);
#  else
			bvh_instance_pop(kg, object, ray, &P, &dir, &idir, &isect->t);
#  endif

			object = OBJECT_NONE;
			nodeAddr = traversalStack[stackPtr];
			--stackPtr;
		}
#endif  /* FEATURE(BVH_INSTANCING) */
	} while(nodeAddr != ENTRYPOINT_SENTINEL);

	return (isect->prim != PRIM_NONE);
}

ccl_device_inline bool BVH_FUNCTION_NAME(KernelGlobals *kg,
                                         const Ray *ray,
                                         Intersection *isect,
                                         const uint visibility
#if BVH_FEATURE(BVH_HAIR_MINIMUM_WIDTH)
                                         , uint *lcg_state,
                                         float difl,
                                         float extmax
#endif
                                         )
{
#ifdef __QBVH__
	if(kernel_data.bvh.use_qbvh && false) {
		return BVH_FUNCTION_FULL_NAME(QBVH)(kg,
		                                    ray,
		                                    isect,
		                                    visibility
#if BVH_FEATURE(BVH_HAIR_MINIMUM_WIDTH)
		                                    , lcg_state,
		                                    difl,
		                                    extmax
#endif
		                                    );
	}
	else
#endif
	{
		//kernel_assert(kernel_data.bvh.use_qbvh == false);
		return BVH_FUNCTION_FULL_NAME(BVH)(kg,
		                                   ray,
		                                   isect,
		                                   visibility
#if BVH_FEATURE(BVH_HAIR_MINIMUM_WIDTH)
		                                   , lcg_state,
		                                   difl,
		                                   extmax
#endif
		                                   );
	}
}

#undef BVH_FUNCTION_NAME
#undef BVH_FUNCTION_FEATURES
