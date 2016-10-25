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

	OpenCLDeviceSplitKernel(DeviceInfo& info, Stats &stats, bool background_)
	: OpenCLDeviceBase(info, stats, background_)
	{
		split_kernel = new DeviceSplitKernel(this);

		background = background_;
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

			/* Keep rendering tiles until done. */
			while(task->acquire_tile(this, tile)) {
				split_kernel->path_trace(task,
		                                 tile,
		                                 *const_mem_map["__data"]);

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
