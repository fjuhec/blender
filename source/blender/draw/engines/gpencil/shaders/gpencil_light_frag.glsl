out vec4 FragColor;

uniform sampler2D strokeColor;
uniform sampler2D strokeDepth;
uniform vec3 loc;
uniform float energy;
uniform float ambient;

float constant = 1.0f;
float linear = 0.0014f;
float quadratic = 0.000007f;

void main()
{
	vec2 uv = vec2(gl_FragCoord.xy);
	float stroke_depth;
	vec4 objcolor;
	
	ivec2 iuv = ivec2(uv.x, uv.y);
	stroke_depth = texelFetch(strokeDepth, iuv, 0).r;
	objcolor = texelFetch(strokeColor, iuv, 0);
	
	/* diffuse light */
	vec3 frag_loc = vec3(uv.x, uv.y, 0);
	vec3 norm = vec3(0, 0, 1.0); /* always z-up */
	vec3 lightdir = normalize(loc - frag_loc);
	float diff = max(dot(norm, lightdir), 0.0);
	float dist  = length(loc - frag_loc);
	float factor = diff * (energy / (dist * dist));	
	
    vec3 result = factor * ambient * vec3(objcolor);
	
	gl_FragDepth = stroke_depth;
	FragColor = vec4(result.r, result.g, result.b, objcolor.a);
}
