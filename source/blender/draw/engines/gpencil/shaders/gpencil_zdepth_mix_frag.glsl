in vec4 uvcoordsvar;

out vec4 FragColor;

uniform sampler2D strokeColor;
uniform sampler2D strokeDepth;
uniform vec2 rcpDimensions;

void main()
{
    float aa_alpha = FxaaPixelShader(
        uvcoordsvar.st,
        strokeColor,
        rcpDimensions,
        1.0,
        0.166,
        0.0833
    ).r;

	ivec2 uv = ivec2(gl_FragCoord.xy);
	float stroke_depth = texelFetch(strokeDepth, uv, 0).r;
	vec4 stroke_color =  texelFetch(strokeColor, uv, 0).rgba;

	FragColor = stroke_color;
    //FragColor.a = aa_alpha;
	// Test
    //FragColor.r = aa_alpha;
	gl_FragDepth = stroke_depth;
}
