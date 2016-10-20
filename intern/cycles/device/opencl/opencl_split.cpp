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

#include "device_split_kernel.h"

#include "util_md5.h"
#include "util_path.h"
#include "util_time.h"

CCL_NAMESPACE_BEGIN

class OpenCLSplitKernelFunction : public SplitKernelFunction {
public:
	OpenCLDeviceBase* device;
	OpenCLDeviceBase::OpenCLProgram program;

	OpenCLSplitKernelFunction(OpenCLDeviceBase* device) : device(device) {}
	~OpenCLSplitKernelFunction() { program.release(); }

	virtual bool enqueue(const KernelDimensions& dim, device_memory& kg, device_memory& data)
	{
		device->kernel_set_args(program(), 0, kg, data);

		device->ciErr = clEnqueueNDRangeKernel(device->cqCommandQueue,
		                                       program(),
		                                       2,
		                                       NULL,
		                                       dim.global_size,
		                                       dim.local_size,
		                                       0,
		                                       NULL,
		                                       NULL);

		device->opencl_assert_err(device->ciErr, "clEnqueueNDRangeKernel");

		if(device->ciErr != CL_SUCCESS) {
			string message = string_printf("OpenCL error: %s in clEnqueueNDRangeKernel()",
			                               clewErrorString(device->ciErr));
			device->opencl_error(message);
			return false;
		}

		return true;
	}
};

/* OpenCLDeviceSplitKernel's declaration/definition. */
class OpenCLDeviceSplitKernel : public OpenCLDeviceBase
{
public:
	DeviceSplitKernel *split_kernel;

	OpenCLProgram program_data_init;

	/* Global memory variables [porting]; These memory is used for
	 * co-operation between different kernels; Data written by one
	 * kernel will be available to another kernel via this global
	 * memory.
	 */

	/* Amount of memory in output buffer associated with one pixel/thread. */
	size_t per_thread_output_buffer_size;

	/* Total allocatable available device memory. */
	size_t total_allocatable_memory;

	/* clos_max value for which the kernels have been loaded currently. */
	int current_max_closure;

	OpenCLDeviceSplitKernel(DeviceInfo& info, Stats &stats, bool background_)
	: OpenCLDeviceBase(info, stats, background_)
	{
		split_kernel = new DeviceSplitKernel(this);

		background = background_;

		per_thread_output_buffer_size = 0;
		current_max_closure = -1;

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
	size_t sizeof_KernelGlobals()
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

	string get_build_options(const DeviceRequestedFeatures& requested_features)
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

		return build_options;
	}

	virtual bool load_kernels(const DeviceRequestedFeatures& requested_features,
	                          vector<OpenCLProgram*> &programs)
	{
		program_data_init = OpenCLProgram(this,
		                                  "split_data_init",
		                                  "kernel_data_init.cl",
		                                  get_build_options(requested_features));
		program_data_init.add_kernel(ustring("path_trace_data_init"));
		programs.push_back(&program_data_init);

		current_max_closure = requested_features.max_closure;

		return split_kernel->load_kernels(requested_features);
	}

	virtual SplitKernelFunction* get_split_kernel_function(string kernel_name,
	                                                       const DeviceRequestedFeatures& requested_features)
	{
		OpenCLSplitKernelFunction* kernel = new OpenCLSplitKernelFunction(this);

		kernel->program = OpenCLProgram(this,
		                                "split_" + kernel_name,
		                                "kernel_" + kernel_name + ".cl",
		                                get_build_options(requested_features));
		kernel->program.add_kernel(ustring("path_trace_" + kernel_name));
		kernel->program.load();

		if(!kernel->program.is_loaded()) {
			delete kernel;
			return NULL;
		}

		return kernel;
	}

	~OpenCLDeviceSplitKernel()
	{
		task_pool.stop();

		/* Release kernels */
		program_data_init.release();

		delete split_kernel;
	}

	virtual bool enqueue_split_kernel_data_init(const KernelDimensions& dim,
	                                            RenderTile& rtile,
	                                            int num_global_elements,
	                                            int num_parallel_samples,
	                                            device_memory& kernel_globals,
	                                            device_memory& kernel_data,
	                                            device_memory& split_data,
	                                            device_memory& ray_state,
	                                            device_memory& queue_index,
	                                            device_memory& use_queues_flag,
	                                            device_memory& work_pool_wgs
	                                            )
	{
		cl_int dQueue_size = dim.global_size[0] * dim.global_size[1];

		/* Set the range of samples to be processed for every ray in
		 * path-regeneration logic.
		 */
		cl_int start_sample = rtile.start_sample;
		cl_int end_sample = rtile.start_sample + rtile.num_samples;

		cl_uint start_arg_index =
			kernel_set_args(program_data_init(),
			                0,
			                kernel_globals,
			                kernel_data,
							split_data,
			                num_global_elements,
							ray_state,
			                rtile.rng_state);

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
			                rtile.x,
			                rtile.y,
			                rtile.w,
			                rtile.h,
			                rtile.offset,
			                rtile.stride,
			                rtile.rng_state_offset_x,
			                rtile.rng_state_offset_y,
			                rtile.buffer_rng_state_stride,
			                queue_index,
			                dQueue_size,
			                use_queues_flag,
#ifdef __WORK_STEALING__
			                work_pool_wgs,
			                rtile.num_samples,
#endif
			                num_parallel_samples,
			                rtile.buffer_offset_x,
			                rtile.buffer_offset_y,
			                rtile.buffer_rng_state_stride,
							rtile.buffer);

		/* Enqueue ckPathTraceKernel_data_init kernel. */
		ciErr = clEnqueueNDRangeKernel(cqCommandQueue,
		                               program_data_init(),
		                               2,
		                               NULL,
		                               dim.global_size,
		                               dim.local_size,
		                               0,
		                               NULL,
		                               NULL);

		opencl_assert_err(ciErr, "clEnqueueNDRangeKernel");

		if(ciErr != CL_SUCCESS) {
			string message = string_printf("OpenCL error: %s in clEnqueueNDRangeKernel()",
			                               clewErrorString(ciErr));
			opencl_error(message);
			return false;
		}

		return true;
	}

	void path_trace(DeviceTask *task,
	                RenderTile& rtile,
	                int2 max_render_feasible_tile_size)
	{
		split_kernel->path_trace(task,
		                         rtile,
		                         max_render_feasible_tile_size,
		                         per_thread_output_buffer_size,
		                         *const_mem_map["__data"]);
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

		KernelGlobals_size = sizeof_KernelGlobals();

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
	vector<RenderTile> split_tiles(RenderTile rtile, int2 split_tile_size)
	{
		vector<RenderTile> to_path_trace_rtile;
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
					vector<RenderTile> to_path_trace_render_tiles =
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
					RenderTile split_tile(tile);
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
