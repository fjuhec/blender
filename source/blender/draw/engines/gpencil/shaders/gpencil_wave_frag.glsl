
out vec4 FragColor;

uniform sampler2D strokeColor;
uniform sampler2D strokeDepth;

uniform float amplitude;
uniform float period;
uniform float phase;
uniform int orientation;

#define HORIZONTAL 0
#define VERTICAL 1

void main()
{
	vec4 outcolor;
	ivec2 uv = ivec2(gl_FragCoord.xy);
	float stroke_depth = texelFetch(strokeDepth, uv, 0).r;
	gl_FragDepth = stroke_depth;

	float value;
	if (orientation == HORIZONTAL) {
		value = amplitude * sin((period * uv.x) + phase);
		outcolor = texelFetch(strokeColor, ivec2(uv.x, uv.y + value), 0);
	}
	else {
		value = amplitude * sin((period * uv.y) + phase);
		outcolor = texelFetch(strokeColor, ivec2(uv.x + value, uv.y), 0);
	}

	FragColor = outcolor;
}
