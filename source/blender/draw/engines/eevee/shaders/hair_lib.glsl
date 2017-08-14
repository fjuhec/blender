#ifdef HAIR_SHADER_FIBERS

#define M_PI 3.1415926535897932384626433832795

mat4 translate(vec3 co)
{
	return mat4(1.0, 0.0, 0.0, 0.0,
	            0.0, 1.0, 0.0, 0.0,
	            0.0, 0.0, 1.0, 0.0,
	            co.x, co.y, co.z, 1.0);
}

mat4 rotateX(float angle)
{
	float ca = cos(angle);
	float sa = sin(angle);
	return mat4(1.0, 0.0, 0.0, 0.0,
	            0.0, ca,   sa, 0.0,
	            0.0, -sa,  ca, 0.0,
	            0.0, 0.0, 0.0, 1.0);
}

mat4 rotateY(float angle)
{
	float ca = cos(angle);
	float sa = sin(angle);
	return mat4(ca,  0.0,  sa, 0.0,
	            0.0, 1.0, 0.0, 0.0,
	            -sa, 0.0,  ca, 0.0,
	            0.0, 0.0, 0.0, 1.0);
}

mat4 rotateZ(float angle)
{
	float ca = cos(angle);
	float sa = sin(angle);
	return mat4(ca,  sa,  0.0, 0.0,
	            -sa, ca,  0.0, 0.0,
	            0.0, 0.0, 1.0, 0.0,
	            0.0, 0.0, 0.0, 1.0);
}

/* Hair Displacement */

/* Note: The deformer functions below calculate a new location vector
 * as well as a new direction (aka "normal"), using the partial derivatives of the transformation.
 * 
 * Each transformation function can depend on the location L as well as the curve parameter t:
 *
 *         Lnew = f(L, t)
 *  => dLnew/dt = del f/del L * dL/dt + del f/del t
 *
 * The first term is the Jacobian of the function f, dL/dt is the original direction vector.
 * Some more information can be found here:
 * https://developer.nvidia.com/gpugems/GPUGems/gpugems_ch42.html
 */

/* Hairs tend to stick together and run in parallel.
 * The effect increases with distance from the root,
 * as the stresses pulling fibers apart decrease.
 */
struct ClumpParams
{
	/* Relative strand thickness at the tip.
	 * (0.0, 1.0]
	 * 0.0 : Strand clumps into a single line
	 * 1.0 : Strand does not clump at all
	 * (> 1.0 is possible but not recommended)
	 */
	float thickness;
};

/* Hairs often don't have a circular cross section, but are somewhat flattened.
 * This creates the local bending which results in the typical curly hair geometry.
 */ 
struct CurlParams
{
	/* Radius of the curls.
	 * >= 0.0
	 */
	float radius;
	/* Steepness of curls
	 * < 0.0 : Clockwise curls
	 * > 0.0 : Anti-clockwise curls
	 */
	float angle;
};

struct DeformParams
{
	/* Strand tapering with distance from the root.
	 * < 1.0 : Taper is concave (recommended)
	 * = 1.0 : Taper is linear
	 * > 1.0 : Taper is convex (not recommended)
	 */
	float taper;

	ClumpParams clump;
	CurlParams curl;
};

void deform_taper(DeformParams params, float t, out float taper, out float dtaper)
{
	taper = pow(t, params.taper);
	dtaper = (t > 0.0) ? taper * params.taper / t : 0.0;
}

void deform_clump(DeformParams params,
                  float t, float tscale, mat4 target_matrix,
                  inout vec3 co, inout vec3 tang)
{
	float taper, dtaper;
	deform_taper(params, t, taper, dtaper);
	float factor = (1.0 - params.clump.thickness) * taper;
	float dfactor = (1.0 - params.clump.thickness) * dtaper;
	
	vec3 target_co = target_matrix[3].xyz;
	vec3 target_tang = target_matrix[0].xyz;
	vec3 nco = co + (target_co - co) * factor;
	vec3 ntang = normalize(tang + (target_tang - tang) * factor + (target_co - co) * dfactor);

	co = nco;
	tang = ntang;
}

void deform_curl(DeformParams params,
                 float t, float tscale,
                 inout mat4 target_matrix)
{
	float pitch = 2.0*M_PI * params.curl.radius * tan(params.curl.angle);
	float turns = tscale / (params.curl.radius * tan(params.curl.angle));
	float angle = t * turns;
	mat4 local_mat = rotateX(angle) * translate(vec3(0.0, params.curl.radius, 0.0)) * rotateY(params.curl.angle);
	target_matrix = target_matrix * local_mat;
}

void deform_fiber(DeformParams params,
                  float t, float tscale, mat4 target_matrix,
                  inout vec3 loc, inout vec3 tang)
{
	deform_curl(params, t, tscale, target_matrix);
	deform_clump(params, t, tscale, target_matrix, loc, tang);
}

/*===================================*/
/* Hair Interpolation */

#define FIBER_RIBBON

uniform sampler2D fiber_data;

uniform int fiber_start;
uniform int strand_map_start;
uniform int strand_vertex_start;

uniform float ribbon_width;
uniform vec2 viewport_size;

#define INDEX_INVALID -1

vec2 read_texdata(int offset)
{
	ivec2 offset2 = ivec2(offset % HAIR_SHADER_TEX_WIDTH, offset / HAIR_SHADER_TEX_WIDTH);
	return texelFetch(fiber_data, offset2, 0).rg;
}

mat4 mat4_from_vectors(vec3 nor, vec3 tang, vec3 co)
{
	tang = normalize(tang);
	vec3 xnor = normalize(cross(nor, tang));
	return mat4(vec4(tang, 0.0), vec4(xnor, 0.0), vec4(cross(tang, xnor), 0.0), vec4(co, 1.0));
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

void interpolate_parent_curve_full(int index, float curve_param, out vec3 co, out vec3 nor, out vec3 tang, out vec3 rootco)
{
	int start, count;
	get_strand_data(index, start, count);
	
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

void interpolate_parent_curve(int index, float curve_param, out vec3 co, out vec3 tang)
{
	vec3 nor;
	vec3 rootco;
	interpolate_parent_curve_full(index, curve_param, co, nor, tang, rootco);
}

void interpolate_vertex(int fiber_index, float curve_param,
	                    out vec3 co, out vec3 tang,
	                    out mat4 target_matrix)
{
	co = vec3(0.0);
	tang = vec3(0.0);
	target_matrix = mat4(1.0);

	ivec4 parent_index;
	vec4 parent_weight;
	vec3 rootco;
	get_fiber_data(fiber_index, parent_index, parent_weight, rootco);

	if (parent_index.x != INDEX_INVALID) {
		vec3 pco, pnor, ptang, prootco;
		interpolate_parent_curve_full(parent_index.x, curve_param, pco, pnor, ptang, prootco);
		co += parent_weight.x * pco;
		tang += parent_weight.x * normalize(ptang);

		target_matrix = mat4_from_vectors(pnor, ptang, pco + prootco);
	}
	if (parent_index.y != INDEX_INVALID) {
		vec3 pco, ptang;
		interpolate_parent_curve(parent_index.y, curve_param, pco, ptang);
		co += parent_weight.y * pco;
		tang += parent_weight.y * normalize(ptang);
	}
	if (parent_index.z != INDEX_INVALID) {
		vec3 pco, ptang;
		interpolate_parent_curve(parent_index.z, curve_param, pco, ptang);
		co += parent_weight.z * pco;
		tang += parent_weight.z * normalize(ptang);
	}
	if (parent_index.w != INDEX_INVALID) {
		vec3 pco, ptang;
		interpolate_parent_curve(parent_index.w, curve_param, pco, ptang);
		co += parent_weight.w * pco;
		tang += parent_weight.w * normalize(ptang);
	}
	
	co += rootco;
	tang = normalize(tang);
}

void hair_fiber_get_vertex(int fiber_index, float curve_param, mat4 ModelViewMatrix, out vec3 pos, out vec3 nor, out vec2 view_offset)
{
	vec3 target_loc;
	mat4 target_matrix;
	interpolate_vertex(fiber_index, curve_param, pos, nor, target_matrix);
	
	DeformParams deform_params;
	deform_params.taper = 2.0;
	deform_params.clump.thickness = 0.15;
	deform_params.curl.radius = 0.1;
	deform_params.curl.angle = 0.2;
	// TODO define proper curve scale, independent of subdivision!
	deform_fiber(deform_params, curve_param, 1.0, target_matrix, pos, nor);

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
