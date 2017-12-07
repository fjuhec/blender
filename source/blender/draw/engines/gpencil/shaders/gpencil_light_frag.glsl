uniform mat4 ProjectionMatrix;
uniform mat4 ViewMatrix;

uniform sampler2D strokeColor;
uniform sampler2D strokeDepth;
uniform vec2 Viewport;
uniform vec3 loc;
uniform float energy;
uniform float ambient;

out vec4 FragColor;

/* project 3d point to 2d on screen space */
vec2 toScreenSpace(vec4 vertex)
{
	vec3 ndc = vec3(vertex.x / vertex.w, 
					vertex.y / vertex.w,
					vertex.z / vertex.w);
					
	vec2 sc;
	sc.x = ((ndc.x + 1.0) / 2.0) * Viewport.x;
	sc.y = ((ndc.y + 1.0) / 2.0) * Viewport.y;
	
	return sc;
}

void main()
{
	float stroke_depth;
	vec4 objcolor;

	vec4 light_loc = ProjectionMatrix * ViewMatrix * vec4(loc, 1.0); 
	vec2 light2d = toScreenSpace(light_loc);
	vec3 light3d = vec3(light2d.x, light2d.y, 10.0); 
	
	vec2 uv = vec2(gl_FragCoord.xy);
	vec3 frag_loc = vec3(uv.x, uv.y, 0);
	vec3 norm = vec3(0, 0, 1.0); /* always z-up */

	ivec2 iuv = ivec2(uv.x, uv.y);
	stroke_depth = texelFetch(strokeDepth, iuv, 0).r;
	objcolor = texelFetch(strokeColor, iuv, 0);
	
	/* diffuse light */
	vec3 lightdir = normalize(light3d - frag_loc);
	float diff = max(dot(norm, lightdir), 0.0);
	float dist  = length(light3d - frag_loc);
	float factor = diff * (energy / (dist * dist));	
	
    vec3 result = factor * ambient * vec3(objcolor);
	
	gl_FragDepth = stroke_depth;
	FragColor = vec4(result.r, result.g, result.b, objcolor.a);
}
