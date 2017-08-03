#ifdef HAIR_SHADER_FIBERS

#define FIBER_RIBBON

uniform sampler2D strand_data;
uniform int strand_map_start;
uniform int strand_vertex_start;
uniform int fiber_start;
uniform float ribbon_width;
uniform vec2 viewport_size;

#define M_PI 3.1415926535897932384626433832795

#define INDEX_INVALID -1

vec2 read_texdata(int offset)
{
	ivec2 offset2 = ivec2(offset % HAIR_SHADER_TEX_WIDTH, offset / HAIR_SHADER_TEX_WIDTH);
	return texelFetch(strand_data, offset2, 0).rg;
}

mat3 mat3_from_vectors(vec3 nor, vec3 tang)
{
	tang = normalize(tang);
	vec3 xnor = normalize(cross(nor, tang));
	return mat3(tang, xnor, cross(tang, xnor));
}

void get_strand_data(int index, out int start, out int count)
{
	int offset = strand_map_start + index;
	vec2 a = read_texdata(offset);

	start = floatBitsToInt(a.r);
	count = floatBitsToInt(a.g);
}

void get_strand_vertex(int index, out vec3 co, out vec3 nor, out vec3 tang)
{
	int offset = strand_vertex_start + index * 5;
	vec2 a = read_texdata(offset);
	vec2 b = read_texdata(offset + 1);
	vec2 c = read_texdata(offset + 2);
	vec2 d = read_texdata(offset + 3);
	vec2 e = read_texdata(offset + 4);

	co = vec3(a.rg, b.r);
	nor = vec3(b.g, c.rg);
	tang = vec3(d.rg, e.r);
}

void get_strand_root(int index, out vec3 co)
{
	int offset = strand_vertex_start + index * 5;
	vec2 a = read_texdata(offset);
	vec2 b = read_texdata(offset + 1);

	co = vec3(a.rg, b.r);
}

void get_fiber_data(int fiber_index, out ivec4 parent_index, out vec4 parent_weight, out vec3 pos)
{
	int offset = fiber_start + fiber_index * 6;
	vec2 a = read_texdata(offset);
	vec2 b = read_texdata(offset + 1);
	vec2 c = read_texdata(offset + 2);
	vec2 d = read_texdata(offset + 3);
	vec2 e = read_texdata(offset + 4);
	vec2 f = read_texdata(offset + 5);

	parent_index = ivec4(floatBitsToInt(a.rg), floatBitsToInt(b.rg));
	parent_weight = vec4(c.rg, d.rg);
	pos = vec3(e.rg, f.r);
}

void interpolate_parent_curve(int index, float curve_param, out vec3 co, out vec3 nor, out vec3 tang)
{
	int start, count;
	get_strand_data(index, start, count);
	
	vec3 rootco;
	get_strand_root(start, rootco);
	
#if 0 // Don't have to worry about out-of-bounds segment here, as long as lerpfac becomes 0.0 when curve_param==1.0
	float maxlen = float(count - 1);
	float arclength = curve_param * maxlen;
	int segment = min(int(arclength), count - 2);
	float lerpfac = arclength - min(floor(arclength), maxlen - 1.0);
#else
	float maxlen = float(count - 1);
	float arclength = curve_param * maxlen;
	int segment = int(arclength);
	float lerpfac = arclength - floor(arclength);
#endif
	
	vec3 co0, nor0, tang0;
	vec3 co1, nor1, tang1;
	get_strand_vertex(start + segment, co0, nor0, tang0);
	get_strand_vertex(start + segment + 1, co1, nor1, tang1);
	
	co = mix(co0, co1, lerpfac) - rootco;
	nor = mix(nor0, nor1, lerpfac);
	tang = mix(tang0, tang1, lerpfac);
}

void interpolate_vertex(int fiber_index, float curve_param,
	                    out vec3 co, out vec3 tang,
	                    out vec3 target_co, out mat3 target_matrix)
{
	co = vec3(0.0);
	tang = vec3(0.0);
	target_co = vec3(0.0);
	target_matrix = mat3(1.0);

	ivec4 parent_index;
	vec4 parent_weight;
	vec3 rootpos;
	get_fiber_data(fiber_index, parent_index, parent_weight, rootpos);

	vec3 pco, pnor, ptang;
	if (parent_index.x != INDEX_INVALID) {
		interpolate_parent_curve(parent_index.x, curve_param, pco, pnor, ptang);
		co += parent_weight.x * pco;
		tang += parent_weight.x * normalize(ptang);
		target_co = co;
		target_matrix = mat3_from_vectors(pnor, ptang);
	}
	if (parent_index.y != INDEX_INVALID) {
		interpolate_parent_curve(parent_index.y, curve_param, pco, pnor, ptang);
		co += parent_weight.y * pco;
		tang += parent_weight.y * normalize(ptang);
	}
	if (parent_index.z != INDEX_INVALID) {
		interpolate_parent_curve(parent_index.z, curve_param, pco, pnor, ptang);
		co += parent_weight.z * pco;
		tang += parent_weight.z * normalize(ptang);
	}
	if (parent_index.w != INDEX_INVALID) {
		interpolate_parent_curve(parent_index.w, curve_param, pco, pnor, ptang);
		co += parent_weight.w * pco;
		tang += parent_weight.w * normalize(ptang);
	}
	
	co += rootpos;
	tang = normalize(tang);
}

void displace_vertex(inout vec3 loc, inout vec3 tang, in float t, in float tscale, in vec3 target_loc, in mat3 target_frame)
{
	// TODO
}

void hair_fiber_get_vertex(int fiber_index, float curve_param, mat4 ModelViewMatrix, out vec3 pos, out vec3 nor, out vec2 view_offset)
{
	vec3 target_loc;
	mat3 target_matrix;
	interpolate_vertex(fiber_index, curve_param, pos, nor, target_loc, target_matrix);
	
	// TODO define proper curve scale, independent of subdivision!
	displace_vertex(pos, nor, curve_param, 1.0, target_loc, target_matrix);
	
#ifdef FIBER_RIBBON
	float ribbon_side = (float(gl_VertexID % 2) - 0.5) * ribbon_width;
	{
		vec4 view_nor = ModelViewMatrix * vec4(nor, 0.0);
		view_offset = vec2(view_nor.y, -view_nor.x);
		float L = length(view_offset);
		if (L > 0.0) {
			view_offset *= ribbon_side / (L * viewport_size);
		}
	}
#else
	view_offset = vec2(0.0);
#endif
}

#endif /*HAIR_SHADER_FIBERS*/
