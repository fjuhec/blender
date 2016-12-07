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

#ifndef __DEVICE_SPLIT_KERNEL_H__
#define __DEVICE_SPLIT_KERNEL_H__

#include "device.h"
#include "buffers.h"

CCL_NAMESPACE_BEGIN

/* This value may be tuned according to the scene we are rendering.
 *
 * Modifying PATH_ITER_INC_FACTOR value proportional to number of expected
 * ray-bounces will improve performance.
 */
#define PATH_ITER_INC_FACTOR 8

/* When allocate global memory in chunks. We may not be able to
 * allocate exactly "CL_DEVICE_MAX_MEM_ALLOC_SIZE" bytes in chunks;
 * Since some bytes may be needed for aligning chunks of memory;
 * This is the amount of memory that we dedicate for that purpose.
 */
#define DATA_ALLOCATION_MEM_FACTOR 5000000 //5MB

class DeviceSplitKernel {
private:
	Device *device;

	SplitKernelFunction *kernel_scene_intersect;
	SplitKernelFunction *kernel_lamp_emission;
	SplitKernelFunction *kernel_queue_enqueue;
	SplitKernelFunction *kernel_background_buffer_update;
	SplitKernelFunction *kernel_shader_eval;
	SplitKernelFunction *kernel_holdout_emission_blurring_pathtermination_ao;
	SplitKernelFunction *kernel_direct_lighting;
	SplitKernelFunction *kernel_shadow_blocked;
	SplitKernelFunction *kernel_next_iteration_setup;
	SplitKernelFunction *kernel_sum_all_radiance;

	/* Global memory variables [porting]; These memory is used for
	 * co-operation between different kernels; Data written by one
	 * kernel will be available to another kernel via this global
	 * memory.
	 */
	device_memory kgbuffer;
	device_memory split_data;
	device_vector<uchar> ray_state;
	device_memory queue_index; /* Array of size num_queues * sizeof(int) that tracks the size of each queue. */

	/* Flag to make sceneintersect and lampemission kernel use queues. */
	device_memory use_queues_flag;

	/* Number of path-iterations to be done in one shot. */
	unsigned int path_iteration_times;

	/* Work pool with respect to each work group. */
	device_memory work_pool_wgs;

	/* clos_max value for which the kernels have been loaded currently. */
	int current_max_closure;

	/* Marked True in constructor and marked false at the end of path_trace(). */
	bool first_tile;

public:
	explicit DeviceSplitKernel(Device* device);
	~DeviceSplitKernel();

	bool load_kernels(const DeviceRequestedFeatures& requested_features);
	bool path_trace(DeviceTask *task,
	                RenderTile& rtile,
	                device_memory& kernel_data);
};

CCL_NAMESPACE_END

#endif /* __DEVICE_SPLIT_KERNEL_H__ */



