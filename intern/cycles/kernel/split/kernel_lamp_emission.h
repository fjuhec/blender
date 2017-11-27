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

CCL_NAMESPACE_BEGIN

/* This kernel operates on QUEUE_ACTIVE_AND_REGENERATED_RAYS.
 * It processes rays of state RAY_ACTIVE and RAY_HIT_BACKGROUND.
 * We will empty QUEUE_ACTIVE_AND_REGENERATED_RAYS queue in this kernel.
 */
ccl_device void kernel_lamp_emission(KernelGlobals *kg, ccl_local_param uint *local_queue_atomics)
{
	if(ccl_local_id(0) == 0 && ccl_local_id(1) == 0) {
		*local_queue_atomics = 0;
	}
	ccl_barrier(CCL_LOCAL_MEM_FENCE);

	if(ccl_global_id(0) == 0 && ccl_global_id(1) == 0) {
		kernel_split_params.shader_eval_queue = QUEUE_SHADER_EVAL;
		kernel_split_params.shader_eval_state = RAY_STATE_ANY;
#ifndef __VOLUME__
	/* We will empty this queue in this kernel. */
		kernel_split_params.queue_index[QUEUE_ACTIVE_AND_REGENERATED_RAYS] = 0;
#endif
	}

	/* Fetch use_queues_flag. */
	char local_use_queues_flag = *kernel_split_params.use_queues_flag;
	ccl_barrier(CCL_LOCAL_MEM_FENCE);

	int ray_index = ccl_global_id(1) * ccl_global_size(0) + ccl_global_id(0);
	if(local_use_queues_flag) {
		ray_index = get_ray_index(kg, ray_index,
		                          QUEUE_ACTIVE_AND_REGENERATED_RAYS,
		                          kernel_split_state.queue_data,
		                          kernel_split_params.queue_size,
#ifndef __VOLUME__
		                          1
#else
		                          0
#endif
		                          );
		if(ray_index == QUEUE_EMPTY_SLOT) {
			return;
		}
	}

	ShaderEvalTask *eval_task = &kernel_split_state.shader_eval_task[ray_index];
	ShaderEvalIntent intent = SHADER_EVAL_INTENT_SKIP;

	if(IS_STATE(kernel_split_state.ray_state, ray_index, RAY_ACTIVE) ||
	   IS_STATE(kernel_split_state.ray_state, ray_index, RAY_HIT_BACKGROUND))
	{
		ccl_global PathState *state = &kernel_split_state.path_state[ray_index];

		Ray ray = kernel_split_state.ray[ray_index];
		ccl_global Intersection *isect = &kernel_split_state.isect[ray_index];
		ShaderData *sd = kernel_split_sd(sd, ray_index);
		LightSample ls = kernel_split_state.light_sample[ray_index];

		intent = kernel_path_lamp_emission_setup(kg, state, &ray, isect, sd, &ls);
		if(intent) {
			shader_eval_task_setup(kg, eval_task, sd, intent);
			kernel_split_state.light_sample[ray_index] = ls;
		}
	}

	enqueue_ray_index_local(ray_index,
	                        QUEUE_SHADER_EVAL,
	                        intent != SHADER_EVAL_INTENT_SKIP,
	                        kernel_split_params.queue_size,
	                        local_queue_atomics,
	                        kernel_split_state.queue_data,
	                        kernel_split_params.queue_index);
}

CCL_NAMESPACE_END

