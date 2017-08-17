out vec4 FragColor;

uniform sampler2D strokeColor;
uniform sampler2D strokeDepth;

uniform vec2 size;
uniform vec4 color;
uniform int uselines;

/* This pixelation shader is a modified version of original Geeks3d.com code */
void main()
{
	vec2 uv = vec2(gl_FragCoord.xy);
	
	float dx = size[0];
	float dy = size[1];
	vec2 coord = vec2(dx * floor(uv.x / dx), dy * floor(uv.y / dy));
	
	float stroke_depth = texelFetch(strokeDepth, ivec2(coord), 0).r;
	vec4 outcolor = texelFetch(strokeColor, ivec2(coord), 0);

	if (uselines == 1) {
		float difx = uv.x - (floor(uv.x / dx) * dx);
		if ((difx == 0.5) && (outcolor.a > 0)) {
			outcolor = color;
		}
		float dify = uv.y - (floor(uv.y / dy) * dy);
		if ((dify == 0.5) && (outcolor.a > 0)) {
			outcolor = color;
		}
	}
	gl_FragDepth = stroke_depth;
	FragColor = outcolor;
}
