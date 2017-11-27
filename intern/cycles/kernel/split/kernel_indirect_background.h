/*
 * Copyright 2011-2017 Blender Foundation
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

CCL_NAMESPACE_BEGIN

ccl_device void kernel_indirect_background(KernelGlobals *kg, ccl_local_param unsigned int *local_queue_atomics)
{
	if(ccl_local_id(0) == 0 && ccl_local_id(1) == 0) {
		*local_queue_atomics = 0;
	}
	ccl_barrier(CCL_LOCAL_MEM_FENCE);
	char enqueue_flag = 0;

	ccl_global char *ray_state = kernel_split_state.ray_state;

	int thread_index = ccl_global_id(1) * ccl_global_size(0) + ccl_global_id(0);
	int ray_index;

	if(kernel_data.integrator.ao_bounces != INT_MAX) {
		ray_index = get_ray_index(kg, thread_index,
		                          QUEUE_ACTIVE_AND_REGENERATED_RAYS,
		                          kernel_split_state.queue_data,
		                          kernel_split_params.queue_size,
		                          0);

		if(ray_index != QUEUE_EMPTY_SLOT) {
			if(IS_STATE(ray_state, ray_index, RAY_ACTIVE)) {
				ccl_global PathState *state = &kernel_split_state.path_state[ray_index];
				if(path_state_ao_bounce(kg, state)) {
					kernel_split_path_end(kg, ray_index);
				}
			}
		}
	}

	if(ccl_global_id(1) * ccl_global_size(0) + ccl_global_id(0) == 0) {
		kernel_split_params.shader_eval_queue = QUEUE_SHADER_EVAL;
		kernel_split_params.shader_eval_state = RAY_HIT_BACKGROUND;
	}

	ray_index = get_ray_index(kg, thread_index,
	                          QUEUE_HITBG_BUFF_UPDATE_TOREGEN_RAYS,
	                          kernel_split_state.queue_data,
	                          kernel_split_params.queue_size,
	                          0);

	if(IS_STATE(ray_state, ray_index, RAY_HIT_BACKGROUND)) {
		ccl_global PathState *state = &kernel_split_state.path_state[ray_index];
		PathRadiance *L = &kernel_split_state.path_radiance[ray_index];
		ccl_global Ray *ray = &kernel_split_state.ray[ray_index];
		float3 throughput = kernel_split_state.throughput[ray_index];
		ShaderData *sd = kernel_split_sd(sd, ray_index);
		ShaderEvalTask *eval_task = &kernel_split_state.shader_eval_task[ray_index];

		if(kernel_path_background_setup(kg, state, ray, throughput, sd, L)) {
			shader_eval_task_setup(kg, eval_task, sd, SHADER_EVAL_INTENT_BACKGROUND);
			enqueue_flag = 1;
		}
		else {
			kernel_split_path_end(kg, ray_index);
		}
	}

	enqueue_ray_index_local(ray_index,
	                        QUEUE_SHADER_EVAL,
	                        enqueue_flag,
	                        kernel_split_params.queue_size,
	                        local_queue_atomics,
	                        kernel_split_state.queue_data,
	                        kernel_split_params.queue_index);
}

CCL_NAMESPACE_END
