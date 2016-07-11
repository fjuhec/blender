/*
 * Adapted from code Copyright 2009-2010 NVIDIA Corporation
 * Modifications Copyright 2011, Blender Foundation.
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

/* Triangle Primitive
 *
 * Basic triangle with 3 vertices is used to represent mesh surfaces. For BVH
 * ray intersection we use a precomputed triangle storage to accelerate
 * intersection at the cost of more memory usage */

CCL_NAMESPACE_BEGIN

/* normal on triangle  */
ccl_device_inline float3 triangle_normal(KernelGlobals *kg, ShaderData *sd)
{
	/* load triangle vertices */
	const uint4 tri_vindex = kernel_tex_fetch(__tri_vindex, ccl_fetch(sd, prim));
	const float3 v0 = float4_to_float3(kernel_tex_fetch(__prim_tri_verts, tri_vindex.w+0));
	const float3 v1 = float4_to_float3(kernel_tex_fetch(__prim_tri_verts, tri_vindex.w+1));
	const float3 v2 = float4_to_float3(kernel_tex_fetch(__prim_tri_verts, tri_vindex.w+2));

	/* return normal */
	if(ccl_fetch(sd, flag) & SD_NEGATIVE_SCALE_APPLIED)
		return normalize(cross(v2 - v0, v1 - v0));
	else
		return normalize(cross(v1 - v0, v2 - v0));
}

/* point and normal on triangle  */
ccl_device_inline void triangle_point_normal(KernelGlobals *kg, int object, int prim, float u, float v, float3 *P, float3 *Ng, int *shader)
{
	/* load triangle vertices */
	const uint4 tri_vindex = kernel_tex_fetch(__tri_vindex, prim);
	float3 v0 = float4_to_float3(kernel_tex_fetch(__prim_tri_verts, tri_vindex.w+0));
	float3 v1 = float4_to_float3(kernel_tex_fetch(__prim_tri_verts, tri_vindex.w+1));
	float3 v2 = float4_to_float3(kernel_tex_fetch(__prim_tri_verts, tri_vindex.w+2));

	/* compute point */
	float t = 1.0f - u - v;
	*P = (u*v0 + v*v1 + t*v2);

	/* get object flags, instance-aware */
	int object_flag = kernel_tex_fetch(__object_flag, object >= 0 ? object : ~object);

	/* compute normal */
	if(object_flag & SD_NEGATIVE_SCALE_APPLIED)
		*Ng = normalize(cross(v2 - v0, v1 - v0));
	else
		*Ng = normalize(cross(v1 - v0, v2 - v0));

	/* shader`*/
	*shader = kernel_tex_fetch(__tri_shader, prim);
}

/* Triangle vertex locations */

ccl_device_inline void triangle_vertices(KernelGlobals *kg, int prim, float3 P[3])
{
	const uint4 tri_vindex = kernel_tex_fetch(__tri_vindex, prim);
	P[0] = float4_to_float3(kernel_tex_fetch(__prim_tri_verts, tri_vindex.w+0));
	P[1] = float4_to_float3(kernel_tex_fetch(__prim_tri_verts, tri_vindex.w+1));
	P[2] = float4_to_float3(kernel_tex_fetch(__prim_tri_verts, tri_vindex.w+2));
}

/* Interpolate smooth vertex normal from vertices */

ccl_device_inline float3 triangle_smooth_normal(KernelGlobals *kg, int prim, float u, float v)
{
	/* load triangle vertices */
	const uint4 tri_vindex = kernel_tex_fetch(__tri_vindex, prim);
	float3 n0 = float4_to_float3(kernel_tex_fetch(__tri_vnormal, tri_vindex.x));
	float3 n1 = float4_to_float3(kernel_tex_fetch(__tri_vnormal, tri_vindex.y));
	float3 n2 = float4_to_float3(kernel_tex_fetch(__tri_vnormal, tri_vindex.z));

	return normalize((1.0f - u - v)*n2 + u*n0 + v*n1);
}

/* Ray differentials on triangle */

ccl_device_inline void triangle_dPdudv(KernelGlobals *kg, int prim, ccl_addr_space float3 *dPdu, ccl_addr_space float3 *dPdv)
{
	/* fetch triangle vertex coordinates */
	const uint4 tri_vindex = kernel_tex_fetch(__tri_vindex, prim);
	const float3 p0 = float4_to_float3(kernel_tex_fetch(__prim_tri_verts, tri_vindex.w+0));
	const float3 p1 = float4_to_float3(kernel_tex_fetch(__prim_tri_verts, tri_vindex.w+1));
	const float3 p2 = float4_to_float3(kernel_tex_fetch(__prim_tri_verts, tri_vindex.w+2));

	/* compute derivatives of P w.r.t. uv */
	*dPdu = (p0 - p2);
	*dPdv = (p1 - p2);
}

/* Reading attributes on various triangle elements */

ccl_device float triangle_attribute_float(KernelGlobals *kg, const ShaderData *sd, const AttributeDescriptor *desc, float *dx, float *dy)
{
	if(desc->element == ATTR_ELEMENT_FACE) {
		if(dx) *dx = 0.0f;
		if(dy) *dy = 0.0f;

		return kernel_tex_fetch(__attributes_float, desc->offset + ccl_fetch(sd, prim));
	}
	else if(desc->element == ATTR_ELEMENT_VERTEX || desc->element == ATTR_ELEMENT_VERTEX_MOTION) {
		uint4 tri_vindex = kernel_tex_fetch(__tri_vindex, ccl_fetch(sd, prim));

		float f0 = kernel_tex_fetch(__attributes_float, desc->offset + tri_vindex.x);
		float f1 = kernel_tex_fetch(__attributes_float, desc->offset + tri_vindex.y);
		float f2 = kernel_tex_fetch(__attributes_float, desc->offset + tri_vindex.z);

#ifdef __RAY_DIFFERENTIALS__
		if(dx) *dx = ccl_fetch(sd, du).dx*f0 + ccl_fetch(sd, dv).dx*f1 - (ccl_fetch(sd, du).dx + ccl_fetch(sd, dv).dx)*f2;
		if(dy) *dy = ccl_fetch(sd, du).dy*f0 + ccl_fetch(sd, dv).dy*f1 - (ccl_fetch(sd, du).dy + ccl_fetch(sd, dv).dy)*f2;
#endif

		return ccl_fetch(sd, u)*f0 + ccl_fetch(sd, v)*f1 + (1.0f - ccl_fetch(sd, u) - ccl_fetch(sd, v))*f2;
	}
	else if(desc->element == ATTR_ELEMENT_CORNER) {
		int tri = desc->offset + ccl_fetch(sd, prim)*3;
		float f0 = kernel_tex_fetch(__attributes_float, tri + 0);
		float f1 = kernel_tex_fetch(__attributes_float, tri + 1);
		float f2 = kernel_tex_fetch(__attributes_float, tri + 2);

#ifdef __RAY_DIFFERENTIALS__
		if(dx) *dx = ccl_fetch(sd, du).dx*f0 + ccl_fetch(sd, dv).dx*f1 - (ccl_fetch(sd, du).dx + ccl_fetch(sd, dv).dx)*f2;
		if(dy) *dy = ccl_fetch(sd, du).dy*f0 + ccl_fetch(sd, dv).dy*f1 - (ccl_fetch(sd, du).dy + ccl_fetch(sd, dv).dy)*f2;
#endif

		return ccl_fetch(sd, u)*f0 + ccl_fetch(sd, v)*f1 + (1.0f - ccl_fetch(sd, u) - ccl_fetch(sd, v))*f2;
	}
	else {
		if(dx) *dx = 0.0f;
		if(dy) *dy = 0.0f;

		return 0.0f;
	}
}

ccl_device float3 triangle_attribute_float3(KernelGlobals *kg, const ShaderData *sd, const AttributeDescriptor *desc, float3 *dx, float3 *dy)
{
	if(desc->element == ATTR_ELEMENT_FACE) {
		if(dx) *dx = make_float3(0.0f, 0.0f, 0.0f);
		if(dy) *dy = make_float3(0.0f, 0.0f, 0.0f);

		return float4_to_float3(kernel_tex_fetch(__attributes_float3, desc->offset + ccl_fetch(sd, prim)));
	}
	else if(desc->element == ATTR_ELEMENT_VERTEX || desc->element == ATTR_ELEMENT_VERTEX_MOTION) {
		uint4 tri_vindex = kernel_tex_fetch(__tri_vindex, ccl_fetch(sd, prim));

		float3 f0 = float4_to_float3(kernel_tex_fetch(__attributes_float3, desc->offset + tri_vindex.x));
		float3 f1 = float4_to_float3(kernel_tex_fetch(__attributes_float3, desc->offset + tri_vindex.y));
		float3 f2 = float4_to_float3(kernel_tex_fetch(__attributes_float3, desc->offset + tri_vindex.z));

#ifdef __RAY_DIFFERENTIALS__
		if(dx) *dx = ccl_fetch(sd, du).dx*f0 + ccl_fetch(sd, dv).dx*f1 - (ccl_fetch(sd, du).dx + ccl_fetch(sd, dv).dx)*f2;
		if(dy) *dy = ccl_fetch(sd, du).dy*f0 + ccl_fetch(sd, dv).dy*f1 - (ccl_fetch(sd, du).dy + ccl_fetch(sd, dv).dy)*f2;
#endif

		return ccl_fetch(sd, u)*f0 + ccl_fetch(sd, v)*f1 + (1.0f - ccl_fetch(sd, u) - ccl_fetch(sd, v))*f2;
	}
	else if(desc->element == ATTR_ELEMENT_CORNER || desc->element == ATTR_ELEMENT_CORNER_BYTE) {
		int tri = desc->offset + ccl_fetch(sd, prim)*3;
		float3 f0, f1, f2;

		if(desc->element == ATTR_ELEMENT_CORNER) {
			f0 = float4_to_float3(kernel_tex_fetch(__attributes_float3, tri + 0));
			f1 = float4_to_float3(kernel_tex_fetch(__attributes_float3, tri + 1));
			f2 = float4_to_float3(kernel_tex_fetch(__attributes_float3, tri + 2));
		}
		else {
			f0 = color_byte_to_float(kernel_tex_fetch(__attributes_uchar4, tri + 0));
			f1 = color_byte_to_float(kernel_tex_fetch(__attributes_uchar4, tri + 1));
			f2 = color_byte_to_float(kernel_tex_fetch(__attributes_uchar4, tri + 2));
		}

#ifdef __RAY_DIFFERENTIALS__
		if(dx) *dx = ccl_fetch(sd, du).dx*f0 + ccl_fetch(sd, dv).dx*f1 - (ccl_fetch(sd, du).dx + ccl_fetch(sd, dv).dx)*f2;
		if(dy) *dy = ccl_fetch(sd, du).dy*f0 + ccl_fetch(sd, dv).dy*f1 - (ccl_fetch(sd, du).dy + ccl_fetch(sd, dv).dy)*f2;
#endif

		return ccl_fetch(sd, u)*f0 + ccl_fetch(sd, v)*f1 + (1.0f - ccl_fetch(sd, u) - ccl_fetch(sd, v))*f2;
	}
	else {
		if(dx) *dx = make_float3(0.0f, 0.0f, 0.0f);
		if(dy) *dy = make_float3(0.0f, 0.0f, 0.0f);

		return make_float3(0.0f, 0.0f, 0.0f);
	}
}

/* Patch index for triangle, -1 if not subdivision triangle */

ccl_device_inline uint subd_triangle_patch(KernelGlobals *kg, const ShaderData *sd)
{
	return kernel_tex_fetch(__tri_patch, ccl_fetch(sd, prim));
}

/* UV coords of triangle within patch */

ccl_device_inline void subd_triangle_patch_uv(KernelGlobals *kg, const ShaderData *sd, float2 uv[3])
{
	uint4 tri_vindex = kernel_tex_fetch(__tri_vindex, ccl_fetch(sd, prim));

	uv[0] = kernel_tex_fetch(__tri_patch_uv, tri_vindex.x);
	uv[1] = kernel_tex_fetch(__tri_patch_uv, tri_vindex.y);
	uv[2] = kernel_tex_fetch(__tri_patch_uv, tri_vindex.z);
}

/* Vertex indices of patch */

ccl_device_inline uint4 subd_triangle_patch_indices(KernelGlobals *kg, int patch)
{
	uint4 indices;

	indices.x = kernel_tex_fetch(__patches, patch+0);
	indices.y = kernel_tex_fetch(__patches, patch+1);
	indices.z = kernel_tex_fetch(__patches, patch+2);
	indices.w = kernel_tex_fetch(__patches, patch+3);

	return indices;
}

/* Originating face for patch */

ccl_device_inline uint subd_triangle_patch_face(KernelGlobals *kg, int patch)
{
	return kernel_tex_fetch(__patches, patch+4);
}

/* Number of corners on originating face */

ccl_device_inline uint subd_triangle_patch_num_corners(KernelGlobals *kg, int patch)
{
	return kernel_tex_fetch(__patches, patch+5) & 0xffff;
}

/* Indices of the four corners that are used by the patch */

ccl_device_inline void subd_triangle_patch_corners(KernelGlobals *kg, int patch, int corners[4])
{
	uint4 data;

	data.x = kernel_tex_fetch(__patches, patch+4);
	data.y = kernel_tex_fetch(__patches, patch+5);
	data.z = kernel_tex_fetch(__patches, patch+6);
	data.w = kernel_tex_fetch(__patches, patch+7);

	int num_corners = data.y & 0xffff;

	if(num_corners == 4) {
		/* quad */
		corners[0] = data.z;
		corners[1] = data.z+1;
		corners[2] = data.z+2;
		corners[3] = data.z+3;
	}
	else {
		/* ngon */
		int c = data.y >> 16;

		corners[0] = data.z + c;
		corners[1] = data.z + mod(c+1, num_corners);
		corners[2] = data.w;
		corners[3] = data.z + mod(c-1, num_corners);
	}
}

/* Reading attributes on various subdivision triangle elements */

ccl_device float subd_triangle_attribute_float(KernelGlobals *kg, const ShaderData *sd, const AttributeDescriptor *desc, float *dx, float *dy)
{
	int patch = subd_triangle_patch(kg, sd);

	if(desc->element == ATTR_ELEMENT_FACE) {
		if(dx) *dx = 0.0f;
		if(dy) *dy = 0.0f;

		return kernel_tex_fetch(__attributes_float, desc->offset + subd_triangle_patch_face(kg, patch));
	}
	else if(desc->element == ATTR_ELEMENT_VERTEX || desc->element == ATTR_ELEMENT_VERTEX_MOTION) {
		float2 uv[3];
		subd_triangle_patch_uv(kg, sd, uv);

		float a, b, c;

		if(desc->flags & ATTR_SUBDIVIDED) {
			a = patch_eval_float(kg, sd, desc->offset, patch, uv[0].x, uv[0].y, 0, NULL, NULL);
			b = patch_eval_float(kg, sd, desc->offset, patch, uv[1].x, uv[1].y, 0, NULL, NULL);
			c = patch_eval_float(kg, sd, desc->offset, patch, uv[2].x, uv[2].y, 0, NULL, NULL);
		}
		else {
			uint4 v = subd_triangle_patch_indices(kg, patch);

			float f0 = kernel_tex_fetch(__attributes_float, desc->offset + v.x);
			float f1 = kernel_tex_fetch(__attributes_float, desc->offset + v.y);
			float f2 = kernel_tex_fetch(__attributes_float, desc->offset + v.z);
			float f3 = kernel_tex_fetch(__attributes_float, desc->offset + v.w);

			if(subd_triangle_patch_num_corners(kg, patch) != 4) {
				f1 = (f1+f0)*0.5f;
				f3 = (f3+f0)*0.5f;
			}

			a = mix(mix(f0, f1, uv[0].x), mix(f3, f2, uv[0].x), uv[0].y);
			b = mix(mix(f0, f1, uv[1].x), mix(f3, f2, uv[1].x), uv[1].y);
			c = mix(mix(f0, f1, uv[2].x), mix(f3, f2, uv[2].x), uv[2].y);
		}

#ifdef __RAY_DIFFERENTIALS__
		if(dx) *dx = ccl_fetch(sd, du).dx*a + ccl_fetch(sd, dv).dx*b - (ccl_fetch(sd, du).dx + ccl_fetch(sd, dv).dx)*c;
		if(dy) *dy = ccl_fetch(sd, du).dy*a + ccl_fetch(sd, dv).dy*b - (ccl_fetch(sd, du).dy + ccl_fetch(sd, dv).dy)*c;
#endif

		return ccl_fetch(sd, u)*a + ccl_fetch(sd, v)*b + (1.0f - ccl_fetch(sd, u) - ccl_fetch(sd, v))*c;
	}
	else if(desc->element == ATTR_ELEMENT_CORNER) {
		float2 uv[3];
		subd_triangle_patch_uv(kg, sd, uv);

		float a, b, c;

		if(desc->flags & ATTR_SUBDIVIDED) {
			a = patch_eval_float(kg, sd, desc->offset, patch, uv[0].x, uv[0].y, 0, NULL, NULL);
			b = patch_eval_float(kg, sd, desc->offset, patch, uv[1].x, uv[1].y, 0, NULL, NULL);
			c = patch_eval_float(kg, sd, desc->offset, patch, uv[2].x, uv[2].y, 0, NULL, NULL);
		}
		else {
			int corners[4];
			subd_triangle_patch_corners(kg, patch, corners);

			float f0 = kernel_tex_fetch(__attributes_float, corners[0] + desc->offset);
			float f1 = kernel_tex_fetch(__attributes_float, corners[1] + desc->offset);
			float f2 = kernel_tex_fetch(__attributes_float, corners[2] + desc->offset);
			float f3 = kernel_tex_fetch(__attributes_float, corners[3] + desc->offset);

			if(subd_triangle_patch_num_corners(kg, patch) != 4) {
				f1 = (f1+f0)*0.5f;
				f3 = (f3+f0)*0.5f;
			}

			a = mix(mix(f0, f1, uv[0].x), mix(f3, f2, uv[0].x), uv[0].y);
			b = mix(mix(f0, f1, uv[1].x), mix(f3, f2, uv[1].x), uv[1].y);
			c = mix(mix(f0, f1, uv[2].x), mix(f3, f2, uv[2].x), uv[2].y);
		}

#ifdef __RAY_DIFFERENTIALS__
		if(dx) *dx = ccl_fetch(sd, du).dx*a + ccl_fetch(sd, dv).dx*b - (ccl_fetch(sd, du).dx + ccl_fetch(sd, dv).dx)*c;
		if(dy) *dy = ccl_fetch(sd, du).dy*a + ccl_fetch(sd, dv).dy*b - (ccl_fetch(sd, du).dy + ccl_fetch(sd, dv).dy)*c;
#endif

		return ccl_fetch(sd, u)*a + ccl_fetch(sd, v)*b + (1.0f - ccl_fetch(sd, u) - ccl_fetch(sd, v))*c;
	}
	else {
		if(dx) *dx = 0.0f;
		if(dy) *dy = 0.0f;

		return 0.0f;
	}
}

ccl_device float3 subd_triangle_attribute_float3(KernelGlobals *kg, const ShaderData *sd, const AttributeDescriptor *desc, float3 *dx, float3 *dy)
{
	int patch = subd_triangle_patch(kg, sd);

	if(desc->element == ATTR_ELEMENT_FACE) {
		if(dx) *dx = make_float3(0.0f, 0.0f, 0.0f);
		if(dy) *dy = make_float3(0.0f, 0.0f, 0.0f);

		return float4_to_float3(kernel_tex_fetch(__attributes_float3, desc->offset + subd_triangle_patch_face(kg, patch)));
	}
	else if(desc->element == ATTR_ELEMENT_VERTEX || desc->element == ATTR_ELEMENT_VERTEX_MOTION) {
		float2 uv[3];
		subd_triangle_patch_uv(kg, sd, uv);

		float3 a, b, c;

		if(desc->flags & ATTR_SUBDIVIDED) {
			a = patch_eval_float3(kg, sd, desc->offset, patch, uv[0].x, uv[0].y, 0, NULL, NULL);
			b = patch_eval_float3(kg, sd, desc->offset, patch, uv[1].x, uv[1].y, 0, NULL, NULL);
			c = patch_eval_float3(kg, sd, desc->offset, patch, uv[2].x, uv[2].y, 0, NULL, NULL);
		}
		else {
			uint4 v = subd_triangle_patch_indices(kg, patch);

			float3 f0 = float4_to_float3(kernel_tex_fetch(__attributes_float3, desc->offset + v.x));
			float3 f1 = float4_to_float3(kernel_tex_fetch(__attributes_float3, desc->offset + v.y));
			float3 f2 = float4_to_float3(kernel_tex_fetch(__attributes_float3, desc->offset + v.z));
			float3 f3 = float4_to_float3(kernel_tex_fetch(__attributes_float3, desc->offset + v.w));

			if(subd_triangle_patch_num_corners(kg, patch) != 4) {
				f1 = (f1+f0)*0.5f;
				f3 = (f3+f0)*0.5f;
			}

			a = mix(mix(f0, f1, uv[0].x), mix(f3, f2, uv[0].x), uv[0].y);
			b = mix(mix(f0, f1, uv[1].x), mix(f3, f2, uv[1].x), uv[1].y);
			c = mix(mix(f0, f1, uv[2].x), mix(f3, f2, uv[2].x), uv[2].y);
		}

#ifdef __RAY_DIFFERENTIALS__
		if(dx) *dx = ccl_fetch(sd, du).dx*a + ccl_fetch(sd, dv).dx*b - (ccl_fetch(sd, du).dx + ccl_fetch(sd, dv).dx)*c;
		if(dy) *dy = ccl_fetch(sd, du).dy*a + ccl_fetch(sd, dv).dy*b - (ccl_fetch(sd, du).dy + ccl_fetch(sd, dv).dy)*c;
#endif

		return ccl_fetch(sd, u)*a + ccl_fetch(sd, v)*b + (1.0f - ccl_fetch(sd, u) - ccl_fetch(sd, v))*c;
	}
	else if(desc->element == ATTR_ELEMENT_CORNER || desc->element == ATTR_ELEMENT_CORNER_BYTE) {
		float2 uv[3];
		subd_triangle_patch_uv(kg, sd, uv);

		float3 a, b, c;

		if(desc->flags & ATTR_SUBDIVIDED) {
			if(desc->element == ATTR_ELEMENT_CORNER) {
				a = patch_eval_float3(kg, sd, desc->offset, patch, uv[0].x, uv[0].y, 0, NULL, NULL);
				b = patch_eval_float3(kg, sd, desc->offset, patch, uv[1].x, uv[1].y, 0, NULL, NULL);
				c = patch_eval_float3(kg, sd, desc->offset, patch, uv[2].x, uv[2].y, 0, NULL, NULL);
			}
			else {
				a = patch_eval_uchar4(kg, sd, desc->offset, patch, uv[0].x, uv[0].y, 0, NULL, NULL);
				b = patch_eval_uchar4(kg, sd, desc->offset, patch, uv[1].x, uv[1].y, 0, NULL, NULL);
				c = patch_eval_uchar4(kg, sd, desc->offset, patch, uv[2].x, uv[2].y, 0, NULL, NULL);
			}
		}
		else {
			int corners[4];
			subd_triangle_patch_corners(kg, patch, corners);

			float3 f0, f1, f2, f3;

			if(desc->element == ATTR_ELEMENT_CORNER) {
				f0 = float4_to_float3(kernel_tex_fetch(__attributes_float3, corners[0] + desc->offset));
				f1 = float4_to_float3(kernel_tex_fetch(__attributes_float3, corners[1] + desc->offset));
				f2 = float4_to_float3(kernel_tex_fetch(__attributes_float3, corners[2] + desc->offset));
				f3 = float4_to_float3(kernel_tex_fetch(__attributes_float3, corners[3] + desc->offset));
			}
			else {
				f0 = color_byte_to_float(kernel_tex_fetch(__attributes_uchar4, corners[0] + desc->offset));
				f1 = color_byte_to_float(kernel_tex_fetch(__attributes_uchar4, corners[1] + desc->offset));
				f2 = color_byte_to_float(kernel_tex_fetch(__attributes_uchar4, corners[2] + desc->offset));
				f3 = color_byte_to_float(kernel_tex_fetch(__attributes_uchar4, corners[3] + desc->offset));
			}

			if(subd_triangle_patch_num_corners(kg, patch) != 4) {
				f1 = (f1+f0)*0.5f;
				f3 = (f3+f0)*0.5f;
			}

			a = mix(mix(f0, f1, uv[0].x), mix(f3, f2, uv[0].x), uv[0].y);
			b = mix(mix(f0, f1, uv[1].x), mix(f3, f2, uv[1].x), uv[1].y);
			c = mix(mix(f0, f1, uv[2].x), mix(f3, f2, uv[2].x), uv[2].y);
		}

#ifdef __RAY_DIFFERENTIALS__
		if(dx) *dx = ccl_fetch(sd, du).dx*a + ccl_fetch(sd, dv).dx*b - (ccl_fetch(sd, du).dx + ccl_fetch(sd, dv).dx)*c;
		if(dy) *dy = ccl_fetch(sd, du).dy*a + ccl_fetch(sd, dv).dy*b - (ccl_fetch(sd, du).dy + ccl_fetch(sd, dv).dy)*c;
#endif

		return ccl_fetch(sd, u)*a + ccl_fetch(sd, v)*b + (1.0f - ccl_fetch(sd, u) - ccl_fetch(sd, v))*c;
	}
	else {
		if(dx) *dx = make_float3(0.0f, 0.0f, 0.0f);
		if(dy) *dy = make_float3(0.0f, 0.0f, 0.0f);

		return make_float3(0.0f, 0.0f, 0.0f);
	}
}

CCL_NAMESPACE_END
