uniform int xraymode;

in vec4 finalColor;
out vec4 fragColor;

#define GP_XRAY_FRONT 0
#define GP_XRAY_3DSPACE 1
#define GP_XRAY_BACK  2

void main()
{
	vec2 centered = gl_PointCoord - vec2(0.5);
	float dist_squared = dot(centered, centered);
	const float rad_squared = 0.25;

	// round point with jaggy edges
	if (dist_squared > rad_squared)
		discard;

	fragColor = finalColor;

	/* set zdepth */
	if (xraymode == GP_XRAY_FRONT) {
		gl_FragDepth = 0.0;
	}
	if (xraymode == GP_XRAY_3DSPACE) {
		gl_FragDepth = gl_FragCoord.z;
	}
	if  (xraymode == GP_XRAY_BACK) {
		gl_FragDepth = 1.0;
	}
}
