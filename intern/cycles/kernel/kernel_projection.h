/*
 * Parts adapted from Open Shading Language with this license:
 *
 * Copyright (c) 2009-2010 Sony Pictures Imageworks Inc., et al.
 * All Rights Reserved.
 *
 * Modifications Copyright 2011, Blender Foundation.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the name of Sony Pictures Imageworks nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __KERNEL_PROJECTION_CL__
#define __KERNEL_PROJECTION_CL__

CCL_NAMESPACE_BEGIN

/* Spherical coordinates <-> Cartesian direction  */

ccl_device float2 direction_to_spherical(float3 dir)
{
	float theta = safe_acosf(dir.z);
	float phi = atan2f(dir.x, dir.y);

	return make_float2(theta, phi);
}

ccl_device float3 spherical_to_direction(float theta, float phi)
{
	float sin_theta = sinf(theta);
	return make_float3(sin_theta*cosf(phi),
	                   sin_theta*sinf(phi),
	                   cosf(theta));
}

/* Equirectangular coordinates <-> Cartesian direction */

ccl_device float2 direction_to_equirectangular_range(float3 dir, float4 range)
{
	float u = (atan2f(dir.y, dir.x) - range.y) / range.x;
	float v = (acosf(dir.z / len(dir)) - range.w) / range.z;

	return make_float2(u, v);
}

ccl_device float3 equirectangular_range_to_direction(float u, float v, float4 range)
{
	float phi = range.x*u + range.y;
	float theta = range.z*v + range.w;
	float sin_theta = sinf(theta);
	return make_float3(sin_theta*cosf(phi),
	                   sin_theta*sinf(phi),
	                   cosf(theta));
}

ccl_device float2 direction_to_equirectangular(float3 dir)
{
	return direction_to_equirectangular_range(dir, make_float4(-M_2PI_F, M_PI_F, -M_PI_F, M_PI_F));
}

ccl_device float3 equirectangular_to_direction(float u, float v)
{
	return equirectangular_range_to_direction(u, v, make_float4(-M_2PI_F, M_PI_F, -M_PI_F, M_PI_F));
}

/* Lambert coordinates <-> Cartesian direction */

#define LAMBERT_CLAMP (1.0f - 0.01f)
#define LAMBERT_SCALE (1.0990301169315693f)

ccl_device_inline float lambert_asinf_clamped(float x)
{
	return asinf(x * LAMBERT_CLAMP) * LAMBERT_SCALE;
}

ccl_device_inline float lambert_sinf_clamped(float x)
{
	return sinf(x / LAMBERT_SCALE) / LAMBERT_CLAMP;
}

ccl_device float2 direction_to_lambert(float3 dir)
{
	float u = -atan2f(dir.y, dir.x) / (M_2PI_F) + 0.5f;
	float v = lambert_sinf_clamped(atan2f(dir.z, hypotf(dir.x, dir.y))) * 0.5f + 0.5f;

	return make_float2(u, v);
}

ccl_device float3 lambert_to_direction(float u, float v)
{
	const float phi = M_PI_F*(1.0f - 2.0f*u);
	const float theta = lambert_asinf_clamped(1.0f - 2.0f*v) + M_PI_2_F;
	return make_float3(sinf(theta)*cosf(phi),
	                   sinf(theta)*sinf(phi),
	                   cosf(theta));
}

/* Fisheye <-> Cartesian direction */

ccl_device float2 direction_to_fisheye(float3 dir, float fov)
{
	float r = atan2f(sqrtf(dir.y*dir.y +  dir.z*dir.z), dir.x) / fov;
	float phi = atan2f(dir.z, dir.y);

	float u = r * cosf(phi) + 0.5f;
	float v = r * sinf(phi) + 0.5f;

	return make_float2(u, v);
}

ccl_device float3 fisheye_to_direction(float u, float v, float fov)
{
	u = (u - 0.5f) * 2.0f;
	v = (v - 0.5f) * 2.0f;

	float r = sqrtf(u*u + v*v);

	if(r > 1.0f)
		return make_float3(0.0f, 0.0f, 0.0f);

	float phi = safe_acosf((r != 0.0f)? u/r: 0.0f);
	float theta = r * fov * 0.5f;

	if(v < 0.0f) phi = -phi;

	return make_float3(
		 cosf(theta),
		 -cosf(phi)*sinf(theta),
		 sinf(phi)*sinf(theta)
	);
}

ccl_device float2 direction_to_fisheye_equisolid(float3 dir, float lens, float width, float height)
{
	float theta = safe_acosf(dir.x);
	float r = 2.0f * lens * sinf(theta * 0.5f);
	float phi = atan2f(dir.z, dir.y);

	float u = r * cosf(phi) / width + 0.5f;
	float v = r * sinf(phi) / height + 0.5f;

	return make_float2(u, v);
}

ccl_device float3 fisheye_equisolid_to_direction(float u, float v, float lens, float fov, float width, float height)
{
	u = (u - 0.5f) * width;
	v = (v - 0.5f) * height;

	float rmax = 2.0f * lens * sinf(fov * 0.25f);
	float r = sqrtf(u*u + v*v);

	if(r > rmax)
		return make_float3(0.0f, 0.0f, 0.0f);

	float phi = safe_acosf((r != 0.0f)? u/r: 0.0f);
	float theta = 2.0f * asinf(r/(2.0f * lens));

	if(v < 0.0f) phi = -phi;

	return make_float3(
		 cosf(theta),
		 -cosf(phi)*sinf(theta),
		 sinf(phi)*sinf(theta)
	);
}

/* Mirror Ball <-> Cartesion direction */

ccl_device float3 mirrorball_to_direction(float u, float v)
{
	/* point on sphere */
	float3 dir;

	dir.x = 2.0f*u - 1.0f;
	dir.z = 2.0f*v - 1.0f;

	if(dir.x*dir.x + dir.z*dir.z > 1.0f)
		return make_float3(0.0f, 0.0f, 0.0f);

	dir.y = -sqrtf(max(1.0f - dir.x*dir.x - dir.z*dir.z, 0.0f));

	/* reflection */
	float3 I = make_float3(0.0f, -1.0f, 0.0f);

	return 2.0f*dot(dir, I)*dir - I;
}

ccl_device float2 direction_to_mirrorball(float3 dir)
{
	/* inverse of mirrorball_to_direction */
	dir.y -= 1.0f;

	float div = 2.0f*sqrtf(max(-0.5f*dir.y, 0.0f));
	if(div > 0.0f)
		dir /= div;

	float u = 0.5f*(dir.x + 1.0f);
	float v = 0.5f*(dir.z + 1.0f);

	return make_float2(u, v);
}

/* Cubemap coordinates <-> Cartesian direction */

/* Get horizontal scaling factor to make it so cubemap side has an overscan
 * of two pixels.
 */
ccl_device_inline float cubemap_scaling_x(float raster_width)
{
	const float side_wideh = raster_width / 3.0f;
	return side_wideh / (side_wideh - 4.0f);
}

/* Get vertical scaling factor to make it so cubemap side has an overscan
 * of two pixels.
 */
ccl_device_inline float cubemap_scaling_y(float raster_height)
{
	const float side_height = raster_height / 2.0f;
	return side_height / (side_height - 4.0f);
}

/* Adjust normalized coordinates to include an overscan. */
ccl_device_inline float cubemap_forward_x(float x, float raster_width)
{
	return x * cubemap_scaling_x(raster_width);
}

ccl_device_inline float cubemap_forward_y(float y, float raster_height)
{
	return y * cubemap_scaling_y(raster_height);
}

/* Adjust normalized coordinates to remove an overscan.
 *
 * NOTE: Only does scaling, still need to include translation to the result.
 */
ccl_device_inline float cubemap_backward_x(float x, float raster_width)
{
	return x / cubemap_scaling_x(raster_width);
}

ccl_device_inline float cubemap_backward_y(float y, float raster_height)
{
	return y / cubemap_scaling_y(raster_height);
}

/* Adjust normalized cube map projection coordinate (which is in [-1..1] range)
 * to remove an overscan and get normalized texture coordinate.
 *
 * NOTE: Similar to cubemap_backward functions only does scaling, trnalsation
 * should be applied separately.
 */
ccl_device_inline float cubemap_projection_backward_x(float x,
                                                      float raster_width)
{
	return cubemap_backward_x((x * 0.5f) + 0.5f, raster_width);
}

ccl_device_inline float cubemap_projection_backward_y(float x,
                                                      float raster_height)
{
	return cubemap_backward_y((x * 0.5f) + 0.5f, raster_height);
}

ccl_device float2 direction_to_cubemap(float3 dir,
                                       float raster_width, float raster_height)
{
	/* We require direction to be normalized. */
	float3 normalized_dir = normalize(dir);
	/* Calculate barycentric coordinates in an equilateral triangle
	 * to see which side of a cube direction point to.
	 *
	 * See comments svm_node_tex_image_box() about how it works.
	 */
	float3 abs_N = make_float3(fabsf(normalized_dir.x),
	                           fabsf(normalized_dir.y),
	                           fabsf(normalized_dir.z));
	float den = max(max(abs_N.x, abs_N.y), abs_N.z);
	/* Project point to a closest side of a cube.
	 * After such projeciton one coordinate is either -1.0 or 1.0 and others
	 * are in a -1.0 .. 1.0 range.
	 */
	float3 P = normalized_dir / den;
	float u, v;
	if(abs_N.x == den) {
		if(P.x < 0.0f) {
			/* Left view. */
			u = cubemap_projection_backward_x(P.y, raster_width) / 3.0f + 1.0f / 3.0f;
			v = cubemap_projection_backward_y(P.z, raster_height) * 0.5f;
		}
		else {
			/* Right view. */
			u = cubemap_projection_backward_x(-P.y, raster_width) / 3.0f + 1.0f / 3.0f;
			v = cubemap_projection_backward_y(P.z, raster_height) * 0.5f + 0.5f;
		}
	}
	else if(abs_N.y == den) {
		if(P.y > 0.0f) {
			/* Front view. */
			u = cubemap_projection_backward_x(P.x, raster_width) / 3.0f;
			v = cubemap_projection_backward_y(P.z, raster_height) * 0.5f;
		}
		else {
			/* Back view. */
			u = cubemap_projection_backward_x(P.x, raster_width) / 3.0f;
			v = cubemap_projection_backward_y(P.z, raster_height) * 0.5f + 0.5f;
		}
	}
	else {
		if(P.z < 0.0f) {
			/* Bottom view. */
			u = cubemap_projection_backward_x(P.x, raster_width) / 3.0f + 2.0f / 3.0f;
			v = cubemap_projection_backward_y(P.y, raster_height) * 0.5f;
		}
		else {
			/* Top view. */
			u = cubemap_projection_backward_x(P.x, raster_width) / 3.0f + 2.0f / 3.0f;
			v = cubemap_projection_backward_y(-P.y, raster_height) * 0.5f + 0.5f;
		}
	}
	u += 2.0f / raster_width;
	v += 2.0f / raster_height;
	return make_float2(u, v);
}

ccl_device float3 cubemap_to_direction(float x, float y,
                                       float raster_width, float raster_height,
                                       float u, float v)
{
	float3 D;
	if(y < 0.5f) {
		if(x < 1.0f / 3.0f) {
			/* Front view. */
			D.x = 0.5f;
			D.y = -cubemap_forward_x(u * 3.0f - 0.5f, raster_width);
			D.z =  cubemap_forward_y(v * 2.0f - 0.5f, raster_height);
			return normalize(D);
		}
		else if(x < 2.0f / 3.0f) {
			/* Left view. */
			D.x = cubemap_forward_x((u - 1.0f / 3.0f) * 3.0f - 0.5f, raster_width);
			D.y = 0.5f;
			D.z = cubemap_forward_y(v * 2.0f - 0.5f, raster_height);
		}
		else {
			/* Bottom view. */
			D.x = cubemap_forward_y(v * 2.0f - 0.5f, raster_width);
			D.y = -cubemap_forward_x((u - 2.0f / 3.0f) * 3.0f - 0.5f, raster_height);
			D.z = -0.5f;
		}
	}
	else {
		if(x < 1.0f / 3.0f) {
			/* Back view. */
			D.x = -0.5f;
			D.y = -cubemap_forward_x(u * 3.0f - 0.5f, raster_width);
			D.z =  cubemap_forward_y((v - 0.5f) * 2.0f - 0.5f, raster_height);
		}
		else if(x < 2.0f / 3.0f) {
			/* Right view. */
			D.x = -cubemap_forward_x((u - 1.0f / 3.0f) * 3.0f - 0.5f, raster_width);
			D.y = -0.5f;
			D.z = cubemap_forward_y((v - 0.5f) * 2.0f - 0.5f, raster_height);
		}
		else {
			/* Top view. */
			D.x = -cubemap_forward_y((v - 0.5f) * 2.0f - 0.5f, raster_height);
			D.y = -cubemap_forward_x((u - 2.0f / 3.0f) * 3.0f - 0.5f, raster_width);
			D.z = 0.5f;
		}
	}
	return normalize(D);
}

ccl_device float3 panorama_to_direction(KernelGlobals *kg,
                                        float x, float y,
                                        float raster_width, float raster_height,
                                        float u, float v)
{
	switch(kernel_data.cam.panorama_type) {
		case PANORAMA_EQUIRECTANGULAR:
			return equirectangular_range_to_direction(u, v, kernel_data.cam.equirectangular_range);
		case PANORAMA_MIRRORBALL:
			return mirrorball_to_direction(u, v);
		case PANORAMA_FISHEYE_EQUIDISTANT:
			return fisheye_to_direction(u, v, kernel_data.cam.fisheye_fov);
		case PANORAMA_CUBEMAP:
			return cubemap_to_direction(x, y, raster_width, raster_height, u, v);
		case PANORAMA_LAMBERT:
			return lambert_to_direction(u, v);
		case PANORAMA_FISHEYE_EQUISOLID:
		default:
			return fisheye_equisolid_to_direction(u, v, kernel_data.cam.fisheye_lens,
				kernel_data.cam.fisheye_fov, kernel_data.cam.sensorwidth, kernel_data.cam.sensorheight);
	}
}

ccl_device float2 direction_to_panorama(KernelGlobals *kg, float3 dir)
{
	switch(kernel_data.cam.panorama_type) {
		case PANORAMA_EQUIRECTANGULAR:
			return direction_to_equirectangular_range(dir, kernel_data.cam.equirectangular_range);
		case PANORAMA_MIRRORBALL:
			return direction_to_mirrorball(dir);
		case PANORAMA_FISHEYE_EQUIDISTANT:
			return direction_to_fisheye(dir, kernel_data.cam.fisheye_fov);
		case PANORAMA_CUBEMAP:
			/* NOTE: Currently it's only used by camera to raster
			 * projection (no image textures used here), so we can use raster
			 * side from kernel data.
			 *
			 * However, it's not really flexible and will break if/when we'll be
			 * using this function for other image textures.
			 */
			return direction_to_cubemap(dir,
			                            kernel_data.cam.width,
			                            kernel_data.cam.height);
		case PANORAMA_LAMBERT:
			return direction_to_lambert(dir);
		case PANORAMA_FISHEYE_EQUISOLID:
		default:
			return direction_to_fisheye_equisolid(dir, kernel_data.cam.fisheye_lens,
				kernel_data.cam.sensorwidth, kernel_data.cam.sensorheight);
	}
}

ccl_device float3 spherical_stereo_position(KernelGlobals *kg,
                                            float3 dir,
                                            float3 pos)
{
	const float interocular_offset = kernel_data.cam.interocular_offset;

	/* Interocular offset of zero means either non stereo, or stereo without
	 * spherical stereo.
	 */
	if(interocular_offset == 0.0f) {
		return pos;
	}

	float3 up = make_float3(0.0f, 0.0f, 1.0f);
	float3 side = normalize(cross(dir, up));

	return pos + (side * interocular_offset);
}

/* NOTE: Ensures direction is normalized. */
ccl_device float3 spherical_stereo_direction(KernelGlobals *kg,
                                             float3 dir,
                                             float3 pos,
                                             float3 newpos)
{
	const float convergence_distance = kernel_data.cam.convergence_distance;
	const float3 normalized_dir = normalize(dir);
	/* Interocular offset of zero means either no stereo, or stereo without
	 * spherical stereo.
	 * Convergence distance is FLT_MAX in the case of parallel convergence mode,
	 * no need to mdify direction in this case either.
	 */
	if(kernel_data.cam.interocular_offset == 0.0f ||
	   convergence_distance == FLT_MAX)
	{
		return normalized_dir;
	}

	float3 screenpos = pos + (normalized_dir * convergence_distance);
	return normalize(screenpos - newpos);
}

CCL_NAMESPACE_END

#endif  /* __KERNEL_PROJECTION_CL__ */
