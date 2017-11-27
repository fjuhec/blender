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

/* This kernel evaluates ShaderData structure from the values computed
 * by the previous kernels.
 */
ccl_device void kernel_shader_eval(KernelGlobals *kg)
{
	int ray_index = ccl_global_id(1) * ccl_global_size(0) + ccl_global_id(0);

	int queue = kernel_split_params.shader_eval_queue;
	int shade_state = kernel_split_params.shader_eval_state;
	int queue_index = kernel_split_params.queue_index[queue];

	if(ray_index >= queue_index) {
		return;
	}
	ray_index = get_ray_index(kg, ray_index,
	                          queue,
	                          kernel_split_state.queue_data,
	                          kernel_split_params.queue_size,
	                          0);

	if(ray_index == QUEUE_EMPTY_SLOT) {
		return;
	}

	if(IS_STATE(kernel_split_state.ray_state, ray_index, shade_state) || shade_state == RAY_STATE_ANY) {
		ShaderEvalTask *eval_task = &kernel_split_state.shader_eval_task[ray_index];
		ShaderData *sd = (ShaderData*)(kernel_split_state.data + eval_task->sd_offset);
		ccl_global PathState *state = &kernel_split_state.path_state[ray_index];

		shader_eval(kg, sd, state, eval_task->intent);
	}
}

CCL_NAMESPACE_END
