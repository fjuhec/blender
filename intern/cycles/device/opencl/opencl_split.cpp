/*
 * Copyright 2011-2013 Blender Foundation
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

#ifdef WITH_OPENCL

#include "opencl.h"

#include "buffers.h"

#include "kernel_types.h"
#include "kernel_split_data.h"

#include "util_md5.h"
#include "util_path.h"
#include "util_time.h"

CCL_NAMESPACE_BEGIN

/* TODO(sergey): This is to keep tile split on OpenCL level working
 * for now, since without this view-port render does not work as it
 * should.
 *
 * Ideally it'll be done on the higher level, but we need to get ready
 * for merge rather soon, so let's keep split logic private here in
 * the file.
 */
class SplitRenderTile : public RenderTile {
public:
	SplitRenderTile()
		: RenderTile(),
		  buffer_offset_x(0),
		  buffer_offset_y(0),
		  rng_state_offset_x(0),
		  rng_state_offset_y(0),
		  buffer_rng_state_stride(0) {}

	explicit SplitRenderTile(RenderTile& tile)
		: RenderTile(),
		  buffer_offset_x(0),
		  buffer_offset_y(0),
		  rng_state_offset_x(0),
		  rng_state_offset_y(0),
		  buffer_rng_state_stride(0)
	{
		x = tile.x;
		y = tile.y;
		w = tile.w;
		h = tile.h;
		start_sample = tile.start_sample;
		num_samples = tile.num_samples;
		sample = tile.sample;
		resolution = tile.resolution;
		offset = tile.offset;
		stride = tile.stride;
		buffer = tile.buffer;
		rng_state = tile.rng_state;
		buffers = tile.buffers;
	}

	/* Split kernel is device global memory constrained;
	 * hence split kernel cant render big tile size's in
	 * one go. If the user sets a big tile size (big tile size
	 * is a term relative to the available device global memory),
	 * we split the tile further and then call path_trace on
	 * each of those split tiles. The following variables declared,
	 * assist in achieving that purpose
	 */
	int buffer_offset_x;
	int buffer_offset_y;
	int rng_state_offset_x;
	int rng_state_offset_y;
	int buffer_rng_state_stride;
};

/* OpenCLDeviceSplitKernel's declaration/definition. */
class OpenCLDeviceSplitKernel : public OpenCLDeviceBase
{
public:
	/* Kernel declaration. */
	OpenCLProgram program_data_init;
	OpenCLProgram program_scene_intersect;
	OpenCLProgram program_lamp_emission;
	OpenCLProgram program_queue_enqueue;
	OpenCLProgram program_background_buffer_update;
	OpenCLProgram program_shader_eval;
	OpenCLProgram program_holdout_emission_blurring_pathtermination_ao;
	OpenCLProgram program_direct_lighting;
	OpenCLProgram program_shadow_blocked;
	OpenCLProgram program_next_iteration_setup;
	OpenCLProgram program_sum_all_radiance;

	/* Global memory variables [porting]; These memory is used for
	 * co-operation between different kernels; Data written by one
	 * kernel will be available to another kernel via this global
	 * memory.
	 */
	cl_mem kgbuffer;  /* KernelGlobals buffer. */

	cl_mem split_data;

	/* Global state array that tracks ray state. */
	cl_mem ray_state;

	/* Queue */
	cl_mem Queue_index; /* Array of size num_queues * sizeof(int);
	                     * Tracks the size of each queue.
	                     */

	/* Flag to make sceneintersect and lampemission kernel use queues. */
	cl_mem use_queues_flag;

	/* Amount of memory in output buffer associated with one pixel/thread. */
	size_t per_thread_output_buffer_size;

	/* Total allocatable available device memory. */
	size_t total_allocatable_memory;

	/* host version of ray_state; Used in checking host path-iteration
	 * termination.
	 */
	char *hostRayStateArray;

	/* Number of path-iterations to be done in one shot. */
	unsigned int PathIteration_times;

#ifdef __WORK_STEALING__
	/* Work pool with respect to each work group. */
	cl_mem work_pool_wgs;

	/* Denotes the maximum work groups possible w.r.t. current tile size. */
	unsigned int max_work_groups;
#endif

	/* clos_max value for which the kernels have been loaded currently. */
	int current_max_closure;

	/* Marked True in constructor and marked false at the end of path_trace(). */
	bool first_tile;

	OpenCLDeviceSplitKernel(DeviceInfo& info, Stats &stats, bool background_)
	: OpenCLDeviceBase(info, stats, background_)
	{
		background = background_;

		/* Initialize cl_mem variables. */
		kgbuffer = NULL;
		split_data = NULL;
		ray_state = NULL;

		/* Queue. */
		Queue_index = NULL;
		use_queues_flag = NULL;

		per_thread_output_buffer_size = 0;
		hostRayStateArray = NULL;
		PathIteration_times = PATH_ITER_INC_FACTOR;
#ifdef __WORK_STEALING__
		work_pool_wgs = NULL;
		max_work_groups = 0;
#endif
		current_max_closure = -1;
		first_tile = true;

		/* Get device's maximum memory that can be allocated. */
		ciErr = clGetDeviceInfo(cdDevice,
		                        CL_DEVICE_MAX_MEM_ALLOC_SIZE,
		                        sizeof(size_t),
		                        &total_allocatable_memory,
		                        NULL);
		assert(ciErr == CL_SUCCESS);
		if(platform_name == "AMD Accelerated Parallel Processing") {
			/* This value is tweak-able; AMD platform does not seem to
			 * give maximum performance when all of CL_DEVICE_MAX_MEM_ALLOC_SIZE
			 * is considered for further computation.
			 */
			total_allocatable_memory /= 2;
		}
	}

	/* Split kernel utility functions. */
	size_t get_tex_size(const char *tex_name)
	{
		cl_mem ptr;
		size_t ret_size = 0;
		MemMap::iterator i = mem_map.find(tex_name);
		if(i != mem_map.end()) {
			ptr = CL_MEM_PTR(i->second);
			ciErr = clGetMemObjectInfo(ptr,
			                           CL_MEM_SIZE,
			                           sizeof(ret_size),
			                           &ret_size,
			                           NULL);
			assert(ciErr == CL_SUCCESS);
		}
		return ret_size;
	}

	/* Returns size of KernelGlobals structure associated with OpenCL. */
	size_t get_KernelGlobals_size()
	{
		/* Copy dummy KernelGlobals related to OpenCL from kernel_globals.h to
		 * fetch its size.
		 */
		typedef struct KernelGlobals {
			ccl_constant KernelData *data;
#define KERNEL_TEX(type, ttype, name) \
	ccl_global type *name;
#include "kernel_textures.h"
#undef KERNEL_TEX
			void *sd_input;
			void *isect_shadow;
			SplitData split_data;
			SplitParams split_param_data;
		} KernelGlobals;

		return sizeof(KernelGlobals);
	}

	virtual void load_kernels(const DeviceRequestedFeatures& requested_features,
	                          vector<OpenCLProgram*> &programs)
	{
		string build_options = "-D__SPLIT_KERNEL__ ";
#ifdef __WORK_STEALING__
		build_options += "-D__WORK_STEALING__ ";
#endif
		build_options += requested_features.get_build_options();

		/* Set compute device build option. */
		cl_device_type device_type;
		ciErr = clGetDeviceInfo(cdDevice,
		                        CL_DEVICE_TYPE,
		                        sizeof(cl_device_type),
		                        &device_type,
		                        NULL);
		assert(ciErr == CL_SUCCESS);
		if(device_type == CL_DEVICE_TYPE_GPU) {
			build_options += " -D__COMPUTE_DEVICE_GPU__";
		}

#define GLUE(a, b) a ## b
#define LOAD_KERNEL(name) \
	do { \
		GLUE(program_, name) = OpenCLProgram(this, "split_" #name, "kernel_" #name ".cl", build_options); \
		GLUE(program_, name).add_kernel(ustring("path_trace_" #name)); \
		programs.push_back(&GLUE(program_, name)); \
	} while(false)

		LOAD_KERNEL(data_init);
		LOAD_KERNEL(scene_intersect);
		LOAD_KERNEL(lamp_emission);
		LOAD_KERNEL(queue_enqueue);
		LOAD_KERNEL(background_buffer_update);
		LOAD_KERNEL(shader_eval);
		LOAD_KERNEL(holdout_emission_blurring_pathtermination_ao);
		LOAD_KERNEL(direct_lighting);
		LOAD_KERNEL(shadow_blocked);
		LOAD_KERNEL(next_iteration_setup);
		LOAD_KERNEL(sum_all_radiance);

#undef FIND_KERNEL
#undef GLUE

		current_max_closure = requested_features.max_closure;
	}

	~OpenCLDeviceSplitKernel()
	{
		task_pool.stop();

		/* Release kernels */
		program_data_init.release();
		program_scene_intersect.release();
		program_lamp_emission.release();
		program_queue_enqueue.release();
		program_background_buffer_update.release();
		program_shader_eval.release();
		program_holdout_emission_blurring_pathtermination_ao.release();
		program_direct_lighting.release();
		program_shadow_blocked.release();
		program_next_iteration_setup.release();
		program_sum_all_radiance.release();

		/* Release global memory */
		release_mem_object_safe(kgbuffer);
		release_mem_object_safe(split_data);
		release_mem_object_safe(ray_state);
		release_mem_object_safe(use_queues_flag);
		release_mem_object_safe(Queue_index);
#ifdef __WORK_STEALING__
		release_mem_object_safe(work_pool_wgs);
#endif

		if(hostRayStateArray != NULL) {
			free(hostRayStateArray);
		}
	}

	void path_trace(DeviceTask *task,
	                SplitRenderTile& rtile,
	                int2 max_render_feasible_tile_size)
	{
		/* cast arguments to cl types */
		cl_mem d_data = CL_MEM_PTR(const_mem_map["__data"]->device_pointer);
		cl_mem d_buffer = CL_MEM_PTR(rtile.buffer);
		cl_mem d_rng_state = CL_MEM_PTR(rtile.rng_state);
		cl_int d_x = rtile.x;
		cl_int d_y = rtile.y;
		cl_int d_w = rtile.w;
		cl_int d_h = rtile.h;
		cl_int d_offset = rtile.offset;
		cl_int d_stride = rtile.stride;

		/* Make sure that set render feasible tile size is a multiple of local
		 * work size dimensions.
		 */
		assert(max_render_feasible_tile_size.x % SPLIT_KERNEL_LOCAL_SIZE_X == 0);
		assert(max_render_feasible_tile_size.y % SPLIT_KERNEL_LOCAL_SIZE_Y == 0);

		size_t global_size[2];
		size_t local_size[2] = {SPLIT_KERNEL_LOCAL_SIZE_X,
		                        SPLIT_KERNEL_LOCAL_SIZE_Y};

		/* Set the range of samples to be processed for every ray in
		 * path-regeneration logic.
		 */
		cl_int start_sample = rtile.start_sample;
		cl_int end_sample = rtile.start_sample + rtile.num_samples;
		cl_int num_samples = rtile.num_samples;

#ifdef __WORK_STEALING__
		global_size[0] = (((d_w - 1) / local_size[0]) + 1) * local_size[0];
		global_size[1] = (((d_h - 1) / local_size[1]) + 1) * local_size[1];
		unsigned int num_parallel_samples = 1;
#else
		global_size[1] = (((d_h - 1) / local_size[1]) + 1) * local_size[1];
		unsigned int num_threads = max_render_feasible_tile_size.x *
		                           max_render_feasible_tile_size.y;
		unsigned int num_tile_columns_possible = num_threads / global_size[1];
		/* Estimate number of parallel samples that can be
		 * processed in parallel.
		 */
		unsigned int num_parallel_samples = min(num_tile_columns_possible / d_w,
		                                        rtile.num_samples);
		/* Wavefront size in AMD is 64.
		 * TODO(sergey): What about other platforms?
		 */
		if(num_parallel_samples >= 64) {
			/* TODO(sergey): Could use generic round-up here. */
			num_parallel_samples = (num_parallel_samples / 64) * 64;
		}
		assert(num_parallel_samples != 0);

		global_size[0] = d_w * num_parallel_samples;
#endif  /* __WORK_STEALING__ */

		assert(global_size[0] * global_size[1] <=
		       max_render_feasible_tile_size.x * max_render_feasible_tile_size.y);

		int num_global_elements = max_render_feasible_tile_size.x *
		                          max_render_feasible_tile_size.y;

		/* Allocate all required global memory once. */
		if(first_tile) {
#ifdef __WORK_STEALING__
			/* Calculate max groups */
			size_t max_global_size[2];
			size_t tile_x = max_render_feasible_tile_size.x;
			size_t tile_y = max_render_feasible_tile_size.y;
			max_global_size[0] = (((tile_x - 1) / local_size[0]) + 1) * local_size[0];
			max_global_size[1] = (((tile_y - 1) / local_size[1]) + 1) * local_size[1];
			max_work_groups = (max_global_size[0] * max_global_size[1]) /
			                  (local_size[0] * local_size[1]);
			/* Allocate work_pool_wgs memory. */
			work_pool_wgs = mem_alloc(max_work_groups * sizeof(unsigned int));
#endif  /* __WORK_STEALING__ */

			/* Allocate queue_index memory only once. */
			Queue_index = mem_alloc(NUM_QUEUES * sizeof(int));
			use_queues_flag = mem_alloc(sizeof(char));
			kgbuffer = mem_alloc(get_KernelGlobals_size());
			ray_state = mem_alloc(num_global_elements * sizeof(char));
			split_data = mem_alloc(split_data_buffer_size(num_global_elements,
			                                              current_max_closure,
			                                              per_thread_output_buffer_size));

			hostRayStateArray = (char *)calloc(num_global_elements, sizeof(char));
			assert(hostRayStateArray != NULL && "Can't create hostRayStateArray memory");
		}

		cl_int dQueue_size = global_size[0] * global_size[1];

		//printf("kernel_set_args data_init\n");
		cl_uint start_arg_index =
			kernel_set_args(program_data_init(),
			                0,
			                kgbuffer,
			                d_data,
							split_data,
			                num_global_elements,
							ray_state,
			                d_rng_state);

/* TODO(sergey): Avoid map lookup here. */
#define KERNEL_TEX(type, ttype, name) \
	set_kernel_arg_mem(program_data_init(), &start_arg_index, #name);
#include "kernel_textures.h"
#undef KERNEL_TEX

		start_arg_index +=
			kernel_set_args(program_data_init(),
			                start_arg_index,
			                start_sample,
			                end_sample,
			                d_x,
			                d_y,
			                d_w,
			                d_h,
			                d_offset,
			                d_stride,
			                rtile.rng_state_offset_x,
			                rtile.rng_state_offset_y,
			                rtile.buffer_rng_state_stride,
			                Queue_index,
			                dQueue_size,
			                use_queues_flag,
#ifdef __WORK_STEALING__
			                work_pool_wgs,
			                num_samples,
#endif
			                num_parallel_samples,
			                rtile.buffer_offset_x,
			                rtile.buffer_offset_y,
			                rtile.buffer_rng_state_stride,
							d_buffer);

#define KERNEL_SET_ARGS(name) kernel_set_args(program_##name(), 0, kgbuffer, d_data);
		KERNEL_SET_ARGS(scene_intersect);
		KERNEL_SET_ARGS(lamp_emission);
		KERNEL_SET_ARGS(queue_enqueue);
		KERNEL_SET_ARGS(background_buffer_update);
		KERNEL_SET_ARGS(shader_eval);
		KERNEL_SET_ARGS(holdout_emission_blurring_pathtermination_ao);
		KERNEL_SET_ARGS(direct_lighting);
		KERNEL_SET_ARGS(shadow_blocked);
		KERNEL_SET_ARGS(next_iteration_setup);
		KERNEL_SET_ARGS(sum_all_radiance);
#undef KERNEL_SET_ARGS

		/* Macro for Enqueuing split kernels. */
#define GLUE(a, b) a ## b
#define ENQUEUE_SPLIT_KERNEL(kernelName, globalSize, localSize) \
		{ \
			/*printf("enqueueing " #kernelName "\n");*/ \
			ciErr = clEnqueueNDRangeKernel(cqCommandQueue, \
			                               GLUE(program_, \
			                                    kernelName)(), \
			                               2, \
			                               NULL, \
			                               globalSize, \
			                               localSize, \
			                               0, \
			                               NULL, \
			                               NULL); \
			opencl_assert_err(ciErr, "clEnqueueNDRangeKernel"); \
			if(ciErr != CL_SUCCESS) { \
				string message = string_printf("OpenCL error: %s in clEnqueueNDRangeKernel()", \
				                               clewErrorString(ciErr)); \
				opencl_error(message); \
				return; \
			} \
		} (void) 0

		/* Enqueue ckPathTraceKernel_data_init kernel. */
		ENQUEUE_SPLIT_KERNEL(data_init, global_size, local_size);
		bool activeRaysAvailable = true;

		/* Record number of time host intervention has been made */
		unsigned int numHostIntervention = 0;
		unsigned int numNextPathIterTimes = PathIteration_times;
		bool canceled = false;

		while(activeRaysAvailable) {
			/* Twice the global work size of other kernels for
			 * ckPathTraceKernel_shadow_blocked_direct_lighting. */
			size_t global_size_shadow_blocked[2];
			global_size_shadow_blocked[0] = global_size[0] * 2;
			global_size_shadow_blocked[1] = global_size[1];

			/* Do path-iteration in host [Enqueue Path-iteration kernels. */
			for(int PathIter = 0; PathIter < PathIteration_times; PathIter++) {
				ENQUEUE_SPLIT_KERNEL(scene_intersect, global_size, local_size);
				ENQUEUE_SPLIT_KERNEL(lamp_emission, global_size, local_size);
				ENQUEUE_SPLIT_KERNEL(queue_enqueue, global_size, local_size);
				ENQUEUE_SPLIT_KERNEL(background_buffer_update, global_size, local_size);
				ENQUEUE_SPLIT_KERNEL(shader_eval, global_size, local_size);
				ENQUEUE_SPLIT_KERNEL(holdout_emission_blurring_pathtermination_ao, global_size, local_size);
				ENQUEUE_SPLIT_KERNEL(direct_lighting, global_size, local_size);
				ENQUEUE_SPLIT_KERNEL(shadow_blocked, global_size_shadow_blocked, local_size);
				ENQUEUE_SPLIT_KERNEL(next_iteration_setup, global_size, local_size);

				if(task->get_cancel()) {
					canceled = true;
					break;
				}
			}

			/* Read ray-state into Host memory to decide if we should exit
			 * path-iteration in host.
			 */
			//printf("enqueue read\n");
			ciErr = clEnqueueReadBuffer(cqCommandQueue,
			                            ray_state,
			                            CL_TRUE,
			                            0,
			                            global_size[0] * global_size[1] * sizeof(char),
			                            hostRayStateArray,
			                            0,
			                            NULL,
			                            NULL);
			assert(ciErr == CL_SUCCESS);

			activeRaysAvailable = false;

			for(int rayStateIter = 0;
			    rayStateIter < global_size[0] * global_size[1];
			    ++rayStateIter)
			{
				if(int8_t(hostRayStateArray[rayStateIter]) != RAY_INACTIVE) {
					/* Not all rays are RAY_INACTIVE. */
					activeRaysAvailable = true;
					break;
				}
			}

			if(activeRaysAvailable) {
				numHostIntervention++;
				PathIteration_times = PATH_ITER_INC_FACTOR;
				/* Host intervention done before all rays become RAY_INACTIVE;
				 * Set do more initial iterations for the next tile.
				 */
				numNextPathIterTimes += PATH_ITER_INC_FACTOR;
			}

			if(task->get_cancel()) {
				canceled = true;
				break;
			}
		}

		/* Execute SumALLRadiance kernel to accumulate radiance calculated in
		 * per_sample_output_buffers into RenderTile's output buffer.
		 */
		if(!canceled) {
			size_t sum_all_radiance_local_size[2] = {16, 16};
			size_t sum_all_radiance_global_size[2];
			sum_all_radiance_global_size[0] =
				(((d_w - 1) / sum_all_radiance_local_size[0]) + 1) *
				sum_all_radiance_local_size[0];
			sum_all_radiance_global_size[1] =
				(((d_h - 1) / sum_all_radiance_local_size[1]) + 1) *
				sum_all_radiance_local_size[1];
			ENQUEUE_SPLIT_KERNEL(sum_all_radiance,
			                     sum_all_radiance_global_size,
			                     sum_all_radiance_local_size);
		}

#undef ENQUEUE_SPLIT_KERNEL
#undef GLUE

		if(numHostIntervention == 0) {
			/* This means that we are executing kernel more than required
			 * Must avoid this for the next sample/tile.
			 */
			PathIteration_times = ((numNextPathIterTimes - PATH_ITER_INC_FACTOR) <= 0) ?
			PATH_ITER_INC_FACTOR : numNextPathIterTimes - PATH_ITER_INC_FACTOR;
		}
		else {
			/* Number of path-iterations done for this tile is set as
			 * Initial path-iteration times for the next tile
			 */
			PathIteration_times = numNextPathIterTimes;
		}

		first_tile = false;
	}

	/* Calculates the amount of memory that has to be always
	 * allocated in order for the split kernel to function.
	 * This memory is tile/scene-property invariant (meaning,
	 * the value returned by this function does not depend
	 * on the user set tile size or scene properties.
	 */
	size_t get_invariable_mem_allocated()
	{
		size_t total_invariable_mem_allocated = 0;
		size_t KernelGlobals_size = 0;

		KernelGlobals_size = get_KernelGlobals_size();

		total_invariable_mem_allocated += KernelGlobals_size; /* KernelGlobals size */
		total_invariable_mem_allocated += NUM_QUEUES * sizeof(unsigned int); /* Queue index size */
		total_invariable_mem_allocated += sizeof(char); /* use_queues_flag size */

		return total_invariable_mem_allocated;
	}

	/* Calculate the memory that has-to-be/has-been allocated for
	 * the split kernel to function.
	 */
	size_t get_tile_specific_mem_allocated(const int2 tile_size)
	{
		size_t tile_specific_mem_allocated = 0;

		/* Get required tile info */
		unsigned int user_set_tile_w = tile_size.x;
		unsigned int user_set_tile_h = tile_size.y;

#ifdef __WORK_STEALING__
		/* Calculate memory to be allocated for work_pools in
		 * case of work_stealing.
		 */
		size_t max_global_size[2];
		size_t max_num_work_pools = 0;
		max_global_size[0] =
			(((user_set_tile_w - 1) / SPLIT_KERNEL_LOCAL_SIZE_X) + 1) *
			SPLIT_KERNEL_LOCAL_SIZE_X;
		max_global_size[1] =
			(((user_set_tile_h - 1) / SPLIT_KERNEL_LOCAL_SIZE_Y) + 1) *
			SPLIT_KERNEL_LOCAL_SIZE_Y;
		max_num_work_pools =
			(max_global_size[0] * max_global_size[1]) /
			(SPLIT_KERNEL_LOCAL_SIZE_X * SPLIT_KERNEL_LOCAL_SIZE_Y);
		tile_specific_mem_allocated += max_num_work_pools * sizeof(unsigned int);
#endif

		tile_specific_mem_allocated +=
			user_set_tile_w * user_set_tile_h * per_thread_output_buffer_size;
		tile_specific_mem_allocated +=
			user_set_tile_w * user_set_tile_h * sizeof(RNG);

		return tile_specific_mem_allocated;
	}

	/* Calculates the texture memories and KernelData (d_data) memory
	 * that has been allocated.
	 */
	size_t get_scene_specific_mem_allocated(cl_mem d_data)
	{
		size_t scene_specific_mem_allocated = 0;
		/* Calculate texture memories. */
#define KERNEL_TEX(type, ttype, name) \
	scene_specific_mem_allocated += get_tex_size(#name);
#include "kernel_textures.h"
#undef KERNEL_TEX
		size_t d_data_size;
		ciErr = clGetMemObjectInfo(d_data,
		                           CL_MEM_SIZE,
		                           sizeof(d_data_size),
		                           &d_data_size,
		                           NULL);
		assert(ciErr == CL_SUCCESS && "Can't get d_data mem object info");
		scene_specific_mem_allocated += d_data_size;
		return scene_specific_mem_allocated;
	}

	/* Calculate the memory required for one thread in split kernel. */
	size_t get_per_thread_memory()
	{
		size_t retval = split_data_buffer_size(1, current_max_closure, per_thread_output_buffer_size);
		retval += sizeof(char); /* ray state size (since ray state is in a different buffer from split state data) */

		return retval;
	}

	/* Considers the total memory available in the device and
	 * and returns the maximum global work size possible.
	 */
	size_t get_feasible_global_work_size(int2 tile_size, cl_mem d_data)
	{
		/* Calculate invariably allocated memory. */
		size_t invariable_mem_allocated = get_invariable_mem_allocated();
		/* Calculate tile specific allocated memory. */
		size_t tile_specific_mem_allocated =
			get_tile_specific_mem_allocated(tile_size);
		/* Calculate scene specific allocated memory. */
		size_t scene_specific_mem_allocated =
			get_scene_specific_mem_allocated(d_data);
		/* Calculate total memory available for the threads in global work size. */
		size_t available_memory = total_allocatable_memory
			- invariable_mem_allocated
			- tile_specific_mem_allocated
			- scene_specific_mem_allocated
			- DATA_ALLOCATION_MEM_FACTOR;
		size_t per_thread_memory_required = get_per_thread_memory();
		return (available_memory / per_thread_memory_required);
	}

	/* Checks if the device has enough memory to render the whole tile;
	 * If not, we should split single tile into multiple tiles of small size
	 * and process them all.
	 */
	bool need_to_split_tile(unsigned int d_w,
	                        unsigned int d_h,
	                        int2 max_render_feasible_tile_size)
	{
		size_t global_size_estimate[2];
		/* TODO(sergey): Such round-ups are in quite few places, need to replace
		 * them with an utility macro.
		 */
		global_size_estimate[0] =
			(((d_w - 1) / SPLIT_KERNEL_LOCAL_SIZE_X) + 1) *
			SPLIT_KERNEL_LOCAL_SIZE_X;
		global_size_estimate[1] =
			(((d_h - 1) / SPLIT_KERNEL_LOCAL_SIZE_Y) + 1) *
			SPLIT_KERNEL_LOCAL_SIZE_Y;
		if((global_size_estimate[0] * global_size_estimate[1]) >
		   (max_render_feasible_tile_size.x * max_render_feasible_tile_size.y))
		{
			return true;
		}
		else {
			return false;
		}
	}

	/* Considers the scene properties, global memory available in the device
	 * and returns a rectanglular tile dimension (approx the maximum)
	 * that should render on split kernel.
	 */
	int2 get_max_render_feasible_tile_size(size_t feasible_global_work_size)
	{
		int2 max_render_feasible_tile_size;
		int square_root_val = (int)sqrt(feasible_global_work_size);
		max_render_feasible_tile_size.x = square_root_val;
		max_render_feasible_tile_size.y = square_root_val;
		/* Ciel round-off max_render_feasible_tile_size. */
		int2 ceil_render_feasible_tile_size;
		ceil_render_feasible_tile_size.x =
			(((max_render_feasible_tile_size.x - 1) / SPLIT_KERNEL_LOCAL_SIZE_X) + 1) *
			SPLIT_KERNEL_LOCAL_SIZE_X;
		ceil_render_feasible_tile_size.y =
			(((max_render_feasible_tile_size.y - 1) / SPLIT_KERNEL_LOCAL_SIZE_Y) + 1) *
			SPLIT_KERNEL_LOCAL_SIZE_Y;
		if(ceil_render_feasible_tile_size.x * ceil_render_feasible_tile_size.y <=
		   feasible_global_work_size)
		{
			return ceil_render_feasible_tile_size;
		}
		/* Floor round-off max_render_feasible_tile_size. */
		int2 floor_render_feasible_tile_size;
		floor_render_feasible_tile_size.x =
			(max_render_feasible_tile_size.x / SPLIT_KERNEL_LOCAL_SIZE_X) *
			SPLIT_KERNEL_LOCAL_SIZE_X;
		floor_render_feasible_tile_size.y =
			(max_render_feasible_tile_size.y / SPLIT_KERNEL_LOCAL_SIZE_Y) *
			SPLIT_KERNEL_LOCAL_SIZE_Y;
		return floor_render_feasible_tile_size;
	}

	/* Try splitting the current tile into multiple smaller
	 * almost-square-tiles.
	 */
	int2 get_split_tile_size(RenderTile rtile,
	                         int2 max_render_feasible_tile_size)
	{
		int2 split_tile_size;
		int num_global_threads = max_render_feasible_tile_size.x *
		                         max_render_feasible_tile_size.y;
		int d_w = rtile.w;
		int d_h = rtile.h;
		/* Ceil round off d_w and d_h */
		d_w = (((d_w - 1) / SPLIT_KERNEL_LOCAL_SIZE_X) + 1) *
			SPLIT_KERNEL_LOCAL_SIZE_X;
		d_h = (((d_h - 1) / SPLIT_KERNEL_LOCAL_SIZE_Y) + 1) *
			SPLIT_KERNEL_LOCAL_SIZE_Y;
		while(d_w * d_h > num_global_threads) {
			/* Halve the longer dimension. */
			if(d_w >= d_h) {
				d_w = d_w / 2;
				d_w = (((d_w - 1) / SPLIT_KERNEL_LOCAL_SIZE_X) + 1) *
					SPLIT_KERNEL_LOCAL_SIZE_X;
			}
			else {
				d_h = d_h / 2;
				d_h = (((d_h - 1) / SPLIT_KERNEL_LOCAL_SIZE_Y) + 1) *
					SPLIT_KERNEL_LOCAL_SIZE_Y;
			}
		}
		split_tile_size.x = d_w;
		split_tile_size.y = d_h;
		return split_tile_size;
	}

	/* Splits existing tile into multiple tiles of tile size split_tile_size. */
	vector<SplitRenderTile> split_tiles(RenderTile rtile, int2 split_tile_size)
	{
		vector<SplitRenderTile> to_path_trace_rtile;
		int d_w = rtile.w;
		int d_h = rtile.h;
		int num_tiles_x = (((d_w - 1) / split_tile_size.x) + 1);
		int num_tiles_y = (((d_h - 1) / split_tile_size.y) + 1);
		/* Buffer and rng_state offset calc. */
		size_t offset_index = rtile.offset + (rtile.x + rtile.y * rtile.stride);
		size_t offset_x = offset_index % rtile.stride;
		size_t offset_y = offset_index / rtile.stride;
		/* Resize to_path_trace_rtile. */
		to_path_trace_rtile.resize(num_tiles_x * num_tiles_y);
		for(int tile_iter_y = 0; tile_iter_y < num_tiles_y; tile_iter_y++) {
			for(int tile_iter_x = 0; tile_iter_x < num_tiles_x; tile_iter_x++) {
				int rtile_index = tile_iter_y * num_tiles_x + tile_iter_x;
				to_path_trace_rtile[rtile_index].rng_state_offset_x = offset_x + tile_iter_x * split_tile_size.x;
				to_path_trace_rtile[rtile_index].rng_state_offset_y = offset_y + tile_iter_y * split_tile_size.y;
				to_path_trace_rtile[rtile_index].buffer_offset_x = offset_x + tile_iter_x * split_tile_size.x;
				to_path_trace_rtile[rtile_index].buffer_offset_y = offset_y + tile_iter_y * split_tile_size.y;
				to_path_trace_rtile[rtile_index].start_sample = rtile.start_sample;
				to_path_trace_rtile[rtile_index].num_samples = rtile.num_samples;
				to_path_trace_rtile[rtile_index].sample = rtile.sample;
				to_path_trace_rtile[rtile_index].resolution = rtile.resolution;
				to_path_trace_rtile[rtile_index].offset = rtile.offset;
				to_path_trace_rtile[rtile_index].buffers = rtile.buffers;
				to_path_trace_rtile[rtile_index].buffer = rtile.buffer;
				to_path_trace_rtile[rtile_index].rng_state = rtile.rng_state;
				to_path_trace_rtile[rtile_index].x = rtile.x + (tile_iter_x * split_tile_size.x);
				to_path_trace_rtile[rtile_index].y = rtile.y + (tile_iter_y * split_tile_size.y);
				to_path_trace_rtile[rtile_index].buffer_rng_state_stride = rtile.stride;
				/* Fill width and height of the new render tile. */
				to_path_trace_rtile[rtile_index].w = (tile_iter_x == (num_tiles_x - 1)) ?
					(d_w - (tile_iter_x * split_tile_size.x)) /* Border tile */
					: split_tile_size.x;
				to_path_trace_rtile[rtile_index].h = (tile_iter_y == (num_tiles_y - 1)) ?
					(d_h - (tile_iter_y * split_tile_size.y)) /* Border tile */
					: split_tile_size.y;
				to_path_trace_rtile[rtile_index].stride = to_path_trace_rtile[rtile_index].w;
			}
		}
		return to_path_trace_rtile;
	}

	void thread_run(DeviceTask *task)
	{
		if(task->type == DeviceTask::FILM_CONVERT) {
			film_convert(*task, task->buffer, task->rgba_byte, task->rgba_half);
		}
		else if(task->type == DeviceTask::SHADER) {
			shader(*task);
		}
		else if(task->type == DeviceTask::PATH_TRACE) {
			RenderTile tile;
			bool initialize_data_and_check_render_feasibility = false;
			bool need_to_split_tiles_further = false;
			int2 max_render_feasible_tile_size;
			size_t feasible_global_work_size;
			const int2 tile_size = task->requested_tile_size;
			/* Keep rendering tiles until done. */
			while(task->acquire_tile(this, tile)) {
				if(!initialize_data_and_check_render_feasibility) {
					/* Initialize data. */
					/* Calculate per_thread_output_buffer_size. */
					size_t output_buffer_size = 0;
					ciErr = clGetMemObjectInfo((cl_mem)tile.buffer,
					                           CL_MEM_SIZE,
					                           sizeof(output_buffer_size),
					                           &output_buffer_size,
					                           NULL);
					assert(ciErr == CL_SUCCESS && "Can't get tile.buffer mem object info");
					/* This value is different when running on AMD and NV. */
					if(background) {
						/* In offline render the number of buffer elements
						 * associated with tile.buffer is the current tile size.
						 */
						per_thread_output_buffer_size =
							output_buffer_size / (tile.w * tile.h);
					}
					else {
						/* interactive rendering, unlike offline render, the number of buffer elements
						 * associated with tile.buffer is the entire viewport size.
						 */
						per_thread_output_buffer_size =
							output_buffer_size / (tile.buffers->params.width *
							                      tile.buffers->params.height);
					}
					/* Check render feasibility. */
					feasible_global_work_size = get_feasible_global_work_size(
						tile_size,
						CL_MEM_PTR(const_mem_map["__data"]->device_pointer));
					max_render_feasible_tile_size =
						get_max_render_feasible_tile_size(
							feasible_global_work_size);
					need_to_split_tiles_further =
						need_to_split_tile(tile_size.x,
						                   tile_size.y,
						                   max_render_feasible_tile_size);
					initialize_data_and_check_render_feasibility = true;
				}
				if(need_to_split_tiles_further) {
					int2 split_tile_size =
						get_split_tile_size(tile,
						                    max_render_feasible_tile_size);
					vector<SplitRenderTile> to_path_trace_render_tiles =
						split_tiles(tile, split_tile_size);
					/* Print message to console */
					if(background && (to_path_trace_render_tiles.size() > 1)) {
						fprintf(stderr, "Message : Tiles need to be split "
						        "further inside path trace (due to insufficient "
						        "device-global-memory for split kernel to "
						        "function) \n"
						        "The current tile of dimensions %dx%d is split "
						        "into tiles of dimension %dx%d for render \n",
						        tile.w, tile.h,
						        split_tile_size.x,
						        split_tile_size.y);
					}
					/* Process all split tiles. */
					for(int tile_iter = 0;
					    tile_iter < to_path_trace_render_tiles.size();
					    ++tile_iter)
					{
						path_trace(task,
						           to_path_trace_render_tiles[tile_iter],
						           max_render_feasible_tile_size);
					}
				}
				else {
					/* No splitting required; process the entire tile at once. */
					/* Render feasible tile size is user-set-tile-size itself. */
					max_render_feasible_tile_size.x =
						(((tile_size.x - 1) / SPLIT_KERNEL_LOCAL_SIZE_X) + 1) *
						SPLIT_KERNEL_LOCAL_SIZE_X;
					max_render_feasible_tile_size.y =
						(((tile_size.y - 1) / SPLIT_KERNEL_LOCAL_SIZE_Y) + 1) *
						SPLIT_KERNEL_LOCAL_SIZE_Y;
					/* buffer_rng_state_stride is stride itself. */
					SplitRenderTile split_tile(tile);
					split_tile.buffer_rng_state_stride = tile.stride;
					path_trace(task, split_tile, max_render_feasible_tile_size);
				}
				tile.sample = tile.start_sample + tile.num_samples;

				/* Complete kernel execution before release tile. */
				/* This helps in multi-device render;
				 * The device that reaches the critical-section function
				 * release_tile waits (stalling other devices from entering
				 * release_tile) for all kernels to complete. If device1 (a
				 * slow-render device) reaches release_tile first then it would
				 * stall device2 (a fast-render device) from proceeding to render
				 * next tile.
				 */
				clFinish(cqCommandQueue);

				task->release_tile(tile);
			}
		}
	}

protected:
	cl_mem mem_alloc(size_t bufsize, cl_mem_flags mem_flag = CL_MEM_READ_WRITE)
	{
		cl_mem ptr;
		assert(bufsize != 0);
		ptr = clCreateBuffer(cxContext, mem_flag, bufsize, NULL, &ciErr);
		opencl_assert_err(ciErr, "clCreateBuffer");
		return ptr;
	}

	/* ** Those guys are for workign around some compiler-specific bugs ** */

	string build_options_for_base_program(
	        const DeviceRequestedFeatures& requested_features)
	{
		return requested_features.get_build_options();
	}
};

Device *opencl_create_split_device(DeviceInfo& info, Stats& stats, bool background)
{
	return new OpenCLDeviceSplitKernel(info, stats, background);
}

CCL_NAMESPACE_END

#endif /* WITH_OPENCL */
