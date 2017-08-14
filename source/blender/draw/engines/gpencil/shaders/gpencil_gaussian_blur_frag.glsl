
out vec4 FragColor;

uniform sampler2D strokeColor;
uniform sampler2D strokeDepth;

uniform float blurx;
uniform float blury;

void main()
{
	ivec2 uv = ivec2(gl_FragCoord.xy);
	float stroke_depth = texelFetch(strokeDepth, uv, 0).r;
	gl_FragDepth = stroke_depth;

	vec4 outcolor = vec4(0.0);
	/* apply blurring, using a 9-tap filter with predefined gaussian weights */
    outcolor += texelFetch(strokeColor, ivec2(uv.x - 1.0 * blurx, uv.y + 1.0 * blury), 0) * 0.0947416;
    outcolor += texelFetch(strokeColor, ivec2(uv.x - 0.0 * blurx, uv.y + 1.0 * blury), 0) * 0.118318;
    outcolor += texelFetch(strokeColor, ivec2(uv.x + 1.0 * blurx, uv.y + 1.0 * blury), 0) * 0.0947416;
    outcolor += texelFetch(strokeColor, ivec2(uv.x - 1.0 * blurx, uv.y + 0.0 * blury), 0) * 0.118318;

    outcolor += texelFetch(strokeColor, ivec2(uv.x, uv.y), 0) * 0.147761;

    outcolor += texelFetch(strokeColor, ivec2(uv.x + 1.0 * blurx, uv.y + 0.0 * blury), 0) * 0.118318;
    outcolor += texelFetch(strokeColor, ivec2(uv.x - 1.0 * blurx, uv.y - 1.0 * blury), 0) * 0.0947416;
    outcolor += texelFetch(strokeColor, ivec2(uv.x + 0.0 * blurx, uv.y - 1.0 * blury), 0) * 0.118318;
    outcolor += texelFetch(strokeColor, ivec2(uv.x + 1.0 * blurx, uv.y - 1.0 * blury), 0) * 0.0947416;

	FragColor = outcolor;
}
