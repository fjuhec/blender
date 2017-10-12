in vec4 uvcoordsvar;

out vec4 FragColor;

uniform sampler2D strokeColor;

void main()
{
	ivec2 uv = ivec2(gl_FragCoord.xy);
	vec4 stroke_color =  texelFetch(strokeColor, uv, 0).rgba;

	FragColor = stroke_color;
	gl_FragDepth = 0.0; /* always on front */
}
