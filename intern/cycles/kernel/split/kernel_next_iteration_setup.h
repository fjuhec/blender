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

#include "kernel_split_common.h"

/* Note on kernel_setup_next_iteration kernel.
 * This is the tenth kernel in the ray tracing logic. This is the ninth
 * of the path iteration kernels. This kernel takes care of setting up
 * Ray for the next iteration of path-iteration and accumulating radiance
 * corresponding to AO and direct-lighting
 *
 * Ray state of rays that are terminated in this kernel are changed to RAY_UPDATE_BUFFER
 *
 * The input and output are as follows,
 *
 * rng_coop ---------------------------------------------|--- kernel_next_iteration_setup -|--- Queue_index (QUEUE_HITBG_BUFF_UPDATE_TOREGEN_RAYS)
 * throughput_coop --------------------------------------|                                 |--- Queue_data (QUEUE_HITBF_BUFF_UPDATE_TOREGEN_RAYS)
 * PathRadiance_coop ------------------------------------|                                 |--- throughput_coop
 * PathState_coop ---------------------------------------|                                 |--- PathRadiance_coop
 * sd ---------------------------------------------------|                                 |--- PathState_coop
 * ray_state --------------------------------------------|                                 |--- ray_state
 * Queue_data (QUEUE_ACTIVE_AND_REGENERATD_RAYS) --------|                                 |--- Ray_coop
 * Queue_index (QUEUE_HITBG_BUFF_UPDATE_TOREGEN_RAYS) ---|                                 |--- use_queues_flag
 * Ray_coop ---------------------------------------------|                                 |
 * kg (globals) -----------------------------------------|                                 |
 * LightRay_dl_coop -------------------------------------|
 * ISLamp_coop ------------------------------------------|
 * BSDFEval_coop ----------------------------------------|
 * LightRay_ao_coop -------------------------------------|
 * AOBSDF_coop ------------------------------------------|
 * AOAlpha_coop -----------------------------------------|
 *
 * Note on queues,
 * This kernel fetches rays from the queue QUEUE_ACTIVE_AND_REGENERATED_RAYS and processes only
 * the rays of state RAY_ACTIVE.
 * There are different points in this kernel where a ray may terminate and reach RAY_UPDATE_BUFF
 * state. These rays are enqueued into QUEUE_HITBG_BUFF_UPDATE_TOREGEN_RAYS queue. These rays will
 * still be present in QUEUE_ACTIVE_AND_REGENERATED_RAYS queue, but since their ray-state has been
 * changed to RAY_UPDATE_BUFF, there is no problem.
 *
 * State of queues when this kernel is called :
 * At entry,
 * QUEUE_ACTIVE_AND_REGENERATED_RAYS will be filled with RAY_ACTIVE, RAY_REGENERATED, RAY_UPDATE_BUFFER rays.
 * QUEUE_HITBG_BUFF_UPDATE_TOREGEN_RAYS will be filled with RAY_TO_REGENERATE and RAY_UPDATE_BUFFER rays
 * At exit,
 * QUEUE_ACTIVE_AND_REGENERATED_RAYS will be filled with RAY_ACTIVE, RAY_REGENERATED and more RAY_UPDATE_BUFFER rays.
 * QUEUE_HITBG_BUFF_UPDATE_TOREGEN_RAYS will be filled with RAY_TO_REGENERATE and more RAY_UPDATE_BUFFER rays
 */
ccl_device char kernel_next_iteration_setup(
        KernelGlobals *kg,
        ccl_global char *use_queues_flag,     /* flag to decide if scene_intersect kernel should
                                               * use queues to fetch ray index */
        int ray_index)
{
	char enqueue_flag = 0;

	/* Load ShaderData structure. */
	PathRadiance *L = NULL;
	ccl_global PathState *state = NULL;
	ccl_global char *ray_state = split_state->ray_state;

	/* Path radiance update for AO/Direct_lighting's shadow blocked. */
	if(IS_FLAG(ray_state, ray_index, RAY_SHADOW_RAY_CAST_DL) ||
	   IS_FLAG(ray_state, ray_index, RAY_SHADOW_RAY_CAST_AO))
	{
		state = &split_state->path_state[ray_index];
		L = &split_state->path_radiance[ray_index];
		float3 _throughput = split_state->throughput[ray_index];

		if(IS_FLAG(ray_state, ray_index, RAY_SHADOW_RAY_CAST_AO)) {
			float3 shadow = split_state->ao_light_ray[ray_index].P;
			char update_path_radiance = split_state->ao_light_ray[ray_index].t;
			if(update_path_radiance) {
				path_radiance_accum_ao(L,
				                       _throughput,
				                       split_state->ao_alpha[ray_index],
				                       split_state->ao_bsdf[ray_index],
				                       shadow,
				                       state->bounce);
			}
			REMOVE_RAY_FLAG(ray_state, ray_index, RAY_SHADOW_RAY_CAST_AO);
		}

		if(IS_FLAG(ray_state, ray_index, RAY_SHADOW_RAY_CAST_DL)) {
			float3 shadow = split_state->light_ray[ray_index].P;
			char update_path_radiance = split_state->light_ray[ray_index].t;
			if(update_path_radiance) {
				BsdfEval L_light = split_state->bsdf_eval[ray_index];
				path_radiance_accum_light(L,
				                          _throughput,
				                          &L_light,
				                          shadow,
				                          1.0f,
				                          state->bounce,
				                          split_state->is_lamp[ray_index]);
			}
			REMOVE_RAY_FLAG(ray_state, ray_index, RAY_SHADOW_RAY_CAST_DL);
		}
	}

	if(IS_STATE(ray_state, ray_index, RAY_ACTIVE)) {
		ccl_global float3 *throughput = &split_state->throughput[ray_index];
		ccl_global Ray *ray = &split_state->ray[ray_index];
		ccl_global RNG *rng = &split_state->rng[ray_index];
		state = &split_state->path_state[ray_index];
		L = &split_state->path_radiance[ray_index];

		/* Compute direct lighting and next bounce. */
		if(!kernel_path_surface_bounce(kg, rng, split_state->sd, throughput, state, L, ray)) {
			ASSIGN_RAY_STATE(ray_state, ray_index, RAY_UPDATE_BUFFER);
			enqueue_flag = 1;
		}
	}

	return enqueue_flag;
}
