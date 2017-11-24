uniform vec2 size;
uniform vec4 color;
uniform int uselines;
uniform vec3 gridcolor;

out vec4 FragColor;

void main()
{
	vec2 uv = vec2(gl_FragCoord.xy);
	float dx = size[0];
	float dy = size[1];
	vec4 gcolor = vec4(gridcolor.r, gridcolor.g, gridcolor.b, 1.0);
	/* avoid too small grid */
	if (dx < 15.0) {
		dx = 15.0;
	}
	if (dy < 15.0) {
		dy = 15.0;
	}

	vec2 coord = vec2(dx * floor(uv.x / dx), dy * floor(uv.y / dy));
	vec4 outcolor = vec4(color);
	if (uselines == 1) {
		float difx = uv.x - (floor(uv.x / dx) * dx);
		if (difx == 0.5) {
			outcolor = gcolor;
		}
		float dify = uv.y - (floor(uv.y / dy) * dy);
		if (dify == 0.5) {
			outcolor = gcolor;
		}
	}
	
	FragColor = outcolor;
}
