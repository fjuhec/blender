out vec4 FragColor;

uniform sampler2D strokeColor;
uniform sampler2D strokeDepth;
uniform vec3 loc;
uniform vec3 lightcolor;
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
	
	/* ambient light */
    vec3 r_ambient = ambient * lightcolor;

	/* diffuse light */
	vec3 norm = vec3(0, 0, 1.0); /* always z-up */
	vec3 lightdir = normalize(loc - vec3(uv.x, uv.y, 0));
	float diff = max(dot(norm, lightdir), 0.0);
	vec3 r_diffuse = diff * lightcolor;	
	
	/* Light attenuation */
	float dist  = length(loc - vec3(uv.x, uv.y, 0));
	r_ambient  /= dist * dist; 
	r_diffuse  /= dist * dist;

	/* apply energy */
	r_ambient  *= energy; 
	r_diffuse  *= energy;
	
	/* join all values */ 
    vec3 result = (r_ambient + r_diffuse) * vec3(objcolor);
	
	gl_FragDepth = stroke_depth;
	FragColor = vec4(result.r, result.g, result.b, objcolor.a);
}
