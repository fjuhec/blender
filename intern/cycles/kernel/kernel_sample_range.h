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

CCL_NAMESPACE_BEGIN

ccl_device_inline void kernel_set_sample_range(
		ccl_global SampleRange *sample_ranges,
		int range,
		ccl_global float *buffer,
		ccl_global uint *rng_state,
		int sample,
		int sx,
		int sy,
		int sw,
		int sh,
		int offset,
		int stride)
{
	ccl_global SampleRange* sample_range = &sample_ranges[range];

	sample_range->buffer = buffer;
	sample_range->rng_state = rng_state;
	sample_range->sample = sample;
	sample_range->x = sx;
	sample_range->y = sy;
	sample_range->w = sw;
	sample_range->h = sh;
	sample_range->offset = offset;
	sample_range->stride = stride;

	if(range == 0) {
		sample_range->work_offset = 0;
	}
	else {
		ccl_global SampleRange* prev_range = &sample_ranges[range-1];
		sample_range->work_offset = prev_range->work_offset + prev_range->w * prev_range->h;
	}
}

ccl_device_inline bool kernel_pixel_sample_for_thread(
		KernelGlobals *kg,
		ccl_global SampleRange *sample_ranges,
		int num_sample_ranges,
		int *thread_x,
		int *thread_y,
		int *thread_sample,
		ccl_global SampleRange **thread_sample_range)
{
	/* order threads to maintain inner block coherency */
	const int group_id = ccl_group_id(0) + ccl_num_groups(0) * ccl_group_id(1);
	const int local_thread_id = ccl_local_id(0) + ccl_local_id(1) * ccl_local_size(0);

	const int thread_id = group_id * (ccl_local_size(0) * ccl_local_size(1)) + local_thread_id;

	/* find which sample range belongs to this thread */
	ccl_global SampleRange* sample_range = NULL;

	for(int i = 0; i < num_sample_ranges; i++) {
		if(thread_id >= sample_ranges[i].work_offset &&
		   thread_id < sample_ranges[i].work_offset + sample_ranges[i].w * sample_ranges[i].h)
		{
			sample_range = &sample_ranges[i];
		}
	}

	/* check if theres work for this thread */
	if(!sample_range) {
		return false;
	}

	int work_offset = thread_id - sample_range->work_offset;

	if(work_offset < 0 || work_offset >= sample_range->w * sample_range->h) {
		return false;
	}

	if(thread_sample_range) *thread_sample_range = sample_range;
	if(thread_x) *thread_x = (work_offset % sample_range->w) + sample_range->x;
	if(thread_y) *thread_y = (work_offset / sample_range->w) + sample_range->y;
	if(thread_sample) *thread_sample = sample_range->sample;

	return true;
}

CCL_NAMESPACE_END

