
out vec4 FragColor;

uniform sampler2D strokeColor;
uniform sampler2D strokeDepth;

/* next 2 fields must be uniforms, now, valuer are only for testing */
float resolution = 100.0;
float radius = 300.0;
uniform vec2 dir;

void main()
{
	ivec2 uv = ivec2(gl_FragCoord.xy);
	float stroke_depth = texelFetch(strokeDepth, uv, 0).r;
	gl_FragDepth = stroke_depth;

	float blur = radius/resolution; 
	float hstep = dir.x;
    float vstep = dir.y;
	vec4 outcolor = vec4(0.0);
	/* apply blurring, using a 9-tap filter with predefined gaussian weights (base on code written by Matt DesLauriers)*/
    outcolor += texelFetch(strokeColor, ivec2(uv.x - 4.0 * blur * hstep, uv.y - 4.0 * blur * vstep), 0) * 0.0162162162;
    outcolor += texelFetch(strokeColor, ivec2(uv.x - 3.0 * blur * hstep, uv.y - 3.0 * blur * vstep), 0) * 0.0540540541;
    outcolor += texelFetch(strokeColor, ivec2(uv.x - 2.0 * blur * hstep, uv.y - 2.0 * blur * vstep), 0) * 0.1216216216;
    outcolor += texelFetch(strokeColor, ivec2(uv.x - 1.0 * blur * hstep, uv.y - 1.0 * blur * vstep), 0) * 0.1945945946;

    outcolor += texelFetch(strokeColor, ivec2(uv.x, uv.y), 0) * 0.2270270270;

    outcolor += texelFetch(strokeColor, ivec2(uv.x + 1.0 * blur * hstep, uv.y + 1.0 * blur * vstep), 0) * 0.1945945946;
    outcolor += texelFetch(strokeColor, ivec2(uv.x + 2.0 * blur * hstep, uv.y + 2.0 * blur * vstep), 0) * 0.1216216216;
    outcolor += texelFetch(strokeColor, ivec2(uv.x + 3.0 * blur * hstep, uv.y + 3.0 * blur * vstep), 0) * 0.0540540541;
    outcolor += texelFetch(strokeColor, ivec2(uv.x + 4.0 * blur * hstep, uv.y + 4.0 * blur * vstep), 0) * 0.0162162162;

	FragColor = outcolor;
}
