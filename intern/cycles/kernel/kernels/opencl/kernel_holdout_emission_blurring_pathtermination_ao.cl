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

#include "split/kernel_holdout_emission_blurring_pathtermination_ao.h"

__kernel void kernel_ocl_path_trace_holdout_emission_blurring_pathtermination_ao(
        KernelGlobals *kg,
        ccl_constant KernelData *data,
        int sw, int sh, int sx, int sy, int stride,
        ccl_global int *Queue_index,           /* Tracks the number of elements in each queue */
        int queuesize,                         /* Size (capacity) of each queue */
#ifdef __WORK_STEALING__
        unsigned int start_sample,
#endif
        int parallel_samples)                  /* Number of samples to be processed in parallel */
{
	ccl_local unsigned int local_queue_atomics_bg;
	ccl_local unsigned int local_queue_atomics_ao;
	if(get_local_id(0) == 0 && get_local_id(1) == 0) {
		local_queue_atomics_bg = 0;
		local_queue_atomics_ao = 0;
	}
	barrier(CLK_LOCAL_MEM_FENCE);

	char enqueue_flag = 0;
	char enqueue_flag_AO_SHADOW_RAY_CAST = 0;
	int ray_index = get_global_id(1) * get_global_size(0) + get_global_id(0);
	ray_index = get_ray_index(ray_index,
	                          QUEUE_ACTIVE_AND_REGENERATED_RAYS,
	                          split_state->queue_data,
	                          queuesize,
	                          0);

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
#endif  /* __COMPUTE_DEVICE_GPU__ */

#ifndef __COMPUTE_DEVICE_GPU__
	if(ray_index != QUEUE_EMPTY_SLOT) {
#endif
		kernel_holdout_emission_blurring_pathtermination_ao(
		        kg,
		        sw, sh, sx, sy, stride,
#ifdef __WORK_STEALING__
		        start_sample,
#endif
		        parallel_samples,
		        ray_index,
		        &enqueue_flag,
		        &enqueue_flag_AO_SHADOW_RAY_CAST);
#ifndef __COMPUTE_DEVICE_GPU__
	}
#endif

	/* Enqueue RAY_UPDATE_BUFFER rays. */
	enqueue_ray_index_local(ray_index,
	                        QUEUE_HITBG_BUFF_UPDATE_TOREGEN_RAYS,
	                        enqueue_flag,
	                        queuesize,
	                        &local_queue_atomics_bg,
	                        split_state->queue_data,
	                        Queue_index);

#ifdef __AO__
	/* Enqueue to-shadow-ray-cast rays. */
	enqueue_ray_index_local(ray_index,
	                        QUEUE_SHADOW_RAY_CAST_AO_RAYS,
	                        enqueue_flag_AO_SHADOW_RAY_CAST,
	                        queuesize,
	                        &local_queue_atomics_ao,
	                        split_state->queue_data,
	                        Queue_index);
#endif
}
