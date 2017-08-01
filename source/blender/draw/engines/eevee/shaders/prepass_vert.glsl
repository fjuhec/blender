
uniform mat4 ModelViewProjectionMatrix;
uniform mat4 ModelMatrix;
uniform mat4 ModelViewMatrix;
#ifdef CLIP_PLANES
uniform vec4 ClipPlanes[1];
#endif

#ifndef HAIR_SHADER_FIBERS
in vec3 pos;
#else
in int fiber_index;
in float curve_param;
#endif

void main()
{
#ifndef HAIR_SHADER_FIBERS
	gl_Position = ModelViewProjectionMatrix * vec4(pos, 1.0);
#else
	vec3 pos;
	vec3 nor;
	vec2 view_offset;
	hair_fiber_get_vertex(fiber_index, curve_param, ModelViewMatrix, pos, nor, view_offset);
	gl_Position = ModelViewProjectionMatrix * vec4(pos, 1.0);
	gl_Position.xy += view_offset * gl_Position.w;
#endif

#ifdef CLIP_PLANES
	vec4 worldPosition = (ModelMatrix * vec4(pos, 1.0));
	gl_ClipDistance[0] = dot(worldPosition, ClipPlanes[0]);
#endif
	/* TODO motion vectors */
}
