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

/* This kernel takes care of direct lighting logic.
 * However, the "shadow ray cast" part of direct lighting is handled
 * in the next kernel.
 *
 * This kernels determines the rays for which a shadow_blocked() function
 * associated with direct lighting should be executed. Those rays for which
 * a shadow_blocked() function for direct-lighting must be executed, are
 * marked with flag RAY_SHADOW_RAY_CAST_DL and enqueued into the queue
 * QUEUE_SHADOW_RAY_CAST_DL_RAYS
 *
 * Note on Queues:
 * This kernel only reads from the QUEUE_ACTIVE_AND_REGENERATED_RAYS queue
 * and processes only the rays of state RAY_ACTIVE; If a ray needs to execute
 * the corresponding shadow_blocked part, after direct lighting, the ray is
 * marked with RAY_SHADOW_RAY_CAST_DL flag.
 *
 * State of queues when this kernel is called:
 * - State of queues QUEUE_ACTIVE_AND_REGENERATED_RAYS and
 *   QUEUE_HITBG_BUFF_UPDATE_TOREGEN_RAYS will be same before and after this
 *   kernel call.
 * - QUEUE_SHADOW_RAY_CAST_DL_RAYS queue will be filled with rays for which a
 *   shadow_blocked function must be executed, after this kernel call
 *    Before this kernel call the QUEUE_SHADOW_RAY_CAST_DL_RAYS will be empty.
 */
ccl_device void kernel_direct_lighting(KernelGlobals *kg,
                                       ccl_local_param unsigned int *local_queue_atomics)
{
	if(ccl_local_id(0) == 0 && ccl_local_id(1) == 0) {
		*local_queue_atomics = 0;
	}
	ccl_barrier(CCL_LOCAL_MEM_FENCE);

	char enqueue_flag = 0;
	int ray_index = ccl_global_id(1) * ccl_global_size(0) + ccl_global_id(0);
	ray_index = get_ray_index(kg, ray_index,
	                          QUEUE_ACTIVE_AND_REGENERATED_RAYS,
	                          kernel_split_state.queue_data,
	                          kernel_split_params.queue_size,
	                          0);

	if(IS_STATE(kernel_split_state.ray_state, ray_index, RAY_ACTIVE)) {
		ccl_global PathState *state = &kernel_split_state.path_state[ray_index];
		ShaderData *sd = kernel_split_sd(sd, ray_index);

		/* direct lighting */
#ifdef __EMISSION__
		bool flag = (kernel_data.integrator.use_direct_light &&
		             (sd->flag & SD_BSDF_HAS_EVAL));

#  ifdef __BRANCHED_PATH__
		if(flag && kernel_data.integrator.branched) {
			flag = false;
			enqueue_flag = 1; // XXX
		}
#  endif  /* __BRANCHED_PATH__ */

#  ifdef __SHADOW_TRICKS__
		if(flag && state->flag & PATH_RAY_SHADOW_CATCHER) {
			flag = false;
			enqueue_flag = 1; // XXX
		}
#  endif  /* __SHADOW_TRICKS__ */

		if(flag) {
			/* Sample illumination from lights to find path contribution. */
			float light_u, light_v;
			path_state_rng_2D(kg, state, PRNG_LIGHT_U, &light_u, &light_v);

			LightSample ls;
			if(light_sample(kg,
			                light_u, light_v,
			                sd->time,
			                sd->P,
			                state->bounce,
			                &ls))
			{
				ShaderData *emission_sd = AS_SHADER_DATA(&kernel_split_state.sd_DL_shadow[ray_index]);
				ShaderEvalTask *eval_task = &kernel_split_state.shader_eval_task[ray_index];

				if(direct_emission_setup(kg, sd, emission_sd, &ls, state, eval_task)) {
					/* Write intermediate data to global memory to access from
					 * the next kernel.
					 */
					kernel_split_state.light_sample[ray_index] = ls;

					/* Mark ray state for next shadow kernel. */
					enqueue_flag = 1;
				}
			}
		}
#endif  /* __EMISSION__ */
	}

#ifdef __EMISSION__
	/* Enqueue RAY_SHADOW_RAY_CAST_DL rays. */
	enqueue_ray_index_local(ray_index,
	                        QUEUE_SHADOW_RAY_CAST_DL_RAYS,
	                        enqueue_flag,
	                        kernel_split_params.queue_size,
	                        local_queue_atomics,
	                        kernel_split_state.queue_data,
	                        kernel_split_params.queue_index);

	if(ccl_global_id(1) * ccl_global_size(0) + ccl_global_id(0) == 0) {
		kernel_split_params.shader_eval_queue = QUEUE_SHADOW_RAY_CAST_DL_RAYS;
	}
#endif

#ifdef __BRANCHED_PATH__
	/* Enqueue RAY_LIGHT_INDIRECT_NEXT_ITER rays
	 * this is the last kernel before next_iteration_setup that uses local atomics so we do this here
	 */
	ccl_barrier(CCL_LOCAL_MEM_FENCE);
	if(ccl_local_id(0) == 0 && ccl_local_id(1) == 0) {
		*local_queue_atomics = 0;
	}
	ccl_barrier(CCL_LOCAL_MEM_FENCE);

	ray_index = ccl_global_id(1) * ccl_global_size(0) + ccl_global_id(0);
	enqueue_ray_index_local(ray_index,
	                        QUEUE_LIGHT_INDIRECT_ITER,
	                        IS_STATE(kernel_split_state.ray_state, ray_index, RAY_LIGHT_INDIRECT_NEXT_ITER),
	                        kernel_split_params.queue_size,
	                        local_queue_atomics,
	                        kernel_split_state.queue_data,
	                        kernel_split_params.queue_index);

#endif  /* __BRANCHED_PATH__ */
}

CCL_NAMESPACE_END
