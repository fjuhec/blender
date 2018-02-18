in vec4 uvcoordsvar;

out vec4 FragColor;

uniform sampler2D strokeColor;
uniform sampler2D strokeDepth;
uniform int tonemapping;

float srgb_to_linearrgb(float c)
{
	if (c < 0.04045)
		return (c < 0.0) ? 0.0 : c * (1.0 / 12.92);
	else
		return pow((c + 0.055) * (1.0 / 1.055), 2.4);
}

float linearrgb_to_srgb(float c)
{
	if (c < 0.0031308)
		return (c < 0.0) ? 0.0 : c * 12.92;
	else
		return 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

void main()
{
	ivec2 uv = ivec2(gl_FragCoord.xy);
	float stroke_depth = texelFetch(strokeDepth, uv, 0).r;
	vec4 stroke_color =  texelFetch(strokeColor, uv, 0).rgba;

	if (tonemapping == 1) {
		FragColor.r = srgb_to_linearrgb(stroke_color.r);
		FragColor.g = srgb_to_linearrgb(stroke_color.g);
		FragColor.b = srgb_to_linearrgb(stroke_color.b);
		FragColor.a = stroke_color.a;
	}
	else {
		FragColor = stroke_color;
	}
	
	gl_FragDepth = stroke_depth;
}
