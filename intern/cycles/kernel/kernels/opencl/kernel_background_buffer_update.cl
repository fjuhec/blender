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
        ccl_constant KernelData *data,
        ccl_global uint *rng_state,
        int sw, int sh, int sx, int sy, int stride,
        int rng_state_offset_x,
        int rng_state_offset_y,
        int rng_state_stride,
        ccl_global int *Queue_index,           /* Tracks the number of elements in each queue */
        int queuesize,                         /* Size (capacity) of each queue */
        int end_sample,
        int start_sample,
#ifdef __WORK_STEALING__
        ccl_global unsigned int *work_pool_wgs,
        unsigned int num_samples,
#endif
        int parallel_samples)                  /* Number of samples to be processed in parallel */
{
	ccl_local unsigned int local_queue_atomics;
	if(get_local_id(0) == 0 && get_local_id(1) == 0) {
		local_queue_atomics = 0;
	}
	barrier(CLK_LOCAL_MEM_FENCE);

	int ray_index = get_global_id(1) * get_global_size(0) + get_global_id(0);
	if(ray_index == 0) {
		/* We will empty this queue in this kernel. */
		Queue_index[QUEUE_HITBG_BUFF_UPDATE_TOREGEN_RAYS] = 0;
	}
	char enqueue_flag = 0;
	ray_index = get_ray_index(ray_index,
	                          QUEUE_HITBG_BUFF_UPDATE_TOREGEN_RAYS,
	                          split_state->queue_data,
	                          queuesize,
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
			                                rng_state,
			                                sw, sh, sx, sy, stride,
			                                rng_state_offset_x,
			                                rng_state_offset_y,
			                                rng_state_stride,
			                                end_sample,
			                                start_sample,
#ifdef __WORK_STEALING__
			                                work_pool_wgs,
			                                num_samples,
#endif
			                                parallel_samples,
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
	                        queuesize,
	                        &local_queue_atomics,
	                        split_state->queue_data,
	                        Queue_index);
}
