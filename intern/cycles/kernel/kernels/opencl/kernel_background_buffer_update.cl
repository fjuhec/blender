/*
 * Copyright 2011-2015 Blender Foundation
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

#include "split/kernel_background_buffer_update.h"

__kernel void kernel_ocl_path_trace_background_buffer_update(
        KernelGlobals *kg,
        ccl_constant KernelData *data)
{
	ccl_local unsigned int local_queue_atomics;
	if(get_local_id(0) == 0 && get_local_id(1) == 0) {
		local_queue_atomics = 0;
	}
	barrier(CLK_LOCAL_MEM_FENCE);

	int ray_index = get_global_id(1) * get_global_size(0) + get_global_id(0);
	if(ray_index == 0) {
		/* We will empty this queue in this kernel. */
		split_params->queue_index[QUEUE_HITBG_BUFF_UPDATE_TOREGEN_RAYS] = 0;
	}
	char enqueue_flag = 0;
	ray_index = get_ray_index(ray_index,
	                          QUEUE_HITBG_BUFF_UPDATE_TOREGEN_RAYS,
	                          split_state->queue_data,
	                          split_params->queue_size,
	                          1);

#ifdef __COMPUTE_DEVICE_GPU__
	/* If we are executing on a GPU device, we exit all threads that are not
	 * required.
	 *
	 * If we are executing on a CPU device, then we need to keep all threads
	 * active since we have barrier() calls later in the kernel. CPU devices,
	 * expect all threads to execute barrier statement.
	 */
	if(ray_index == QUEUE_EMPTY_SLOT) {
		return;
	}
#endif

#ifndef __COMPUTE_DEVICE_GPU__
	if(ray_index != QUEUE_EMPTY_SLOT) {
#endif
		enqueue_flag =
			kernel_background_buffer_update(kg,
			                                split_params->rng_state,
			                                split_params->w,
			                                split_params->h,
			                                split_params->x,
			                                split_params->y,
			                                split_params->stride,
			                                split_params->rng_offset_x,
			                                split_params->rng_offset_y,
			                                split_params->rng_stride,
			                                split_params->end_sample,
			                                split_params->start_sample,
#ifdef __WORK_STEALING__
			                                split_params->work_pool_wgs,
			                                split_params->num_samples,
#endif
			                                split_params->parallel_samples,
			                                ray_index);
#ifndef __COMPUTE_DEVICE_GPU__
	}
#endif

	/* Enqueue RAY_REGENERATED rays into QUEUE_ACTIVE_AND_REGENERATED_RAYS;
	 * These rays will be made active during next SceneIntersectkernel.
	 */
	enqueue_ray_index_local(ray_index,
	                        QUEUE_ACTIVE_AND_REGENERATED_RAYS,
	                        enqueue_flag,
	                        split_params->queue_size,
	                        &local_queue_atomics,
	                        split_state->queue_data,
	                        split_params->queue_index);
}
