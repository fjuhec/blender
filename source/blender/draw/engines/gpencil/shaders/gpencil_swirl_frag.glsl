out vec4 FragColor;

uniform sampler2D strokeColor;
uniform sampler2D strokeDepth;

uniform vec2 center;
uniform float radius;
uniform float angle;
uniform int transparent;

/* This swirl shader is a modified version of original Geeks3d.com code */
void main()
{
	vec2 uv = vec2(gl_FragCoord.xy);
	float stroke_depth;
	vec4 outcolor;
	
	vec2 tc = uv - center;
	float dist = length(tc);
    if (dist <= radius) {
		float percent = (radius - dist) / radius;
		float theta = percent * percent * angle * 8.0;
		float s = sin(theta);
		float c = cos(theta);
		tc = vec2(dot(tc, vec2(c, -s)), dot(tc, vec2(s, c)));
		tc += center;

		stroke_depth = texelFetch(strokeDepth, ivec2(tc), 0).r;
		outcolor = texelFetch(strokeColor, ivec2(tc), 0);
	}
	else {
		if (transparent == 1) {
			discard;
		}
		stroke_depth = texelFetch(strokeDepth, ivec2(uv), 0).r;
		outcolor = texelFetch(strokeColor, ivec2(uv), 0);
	}

	gl_FragDepth = stroke_depth;
	FragColor = outcolor;
}
