uniform ivec2 RegionSize;
uniform sampler2D warpTexture;

const vec2 LensCenter = vec2(0.5, 0.5);
const vec2 Scale = vec2(0.1469278, 0.2350845);
const vec2 ScaleIn = vec2(4, 2.5);
const vec4 HmdWarpParam = vec4(1, 0.2, 0.1, 0);
const float aberr_r = 0.985;
const float aberr_b = 1.015;
// const vec4 HmdWarpParam   = vec4(1, 0.5, 0.05, 0);


void main()
{
	vec2 oTexCoord = gl_FragCoord.xy / RegionSize;
	oTexCoord = vec2(oTexCoord.x, 1.0 - oTexCoord.y);

	vec2 theta = (oTexCoord - LensCenter) * ScaleIn; // Scales to [-1, 1]
	float rSq = theta.x * theta.x + theta.y * theta.y;
	vec2 rvector = theta * (HmdWarpParam.x + HmdWarpParam.y * rSq +
	                        HmdWarpParam.z * rSq * rSq +
	                        HmdWarpParam.w * rSq * rSq * rSq);
	vec2 tc_r = LensCenter + aberr_r * Scale * rvector;
	vec2 tc_g = LensCenter +           Scale * rvector;
	vec2 tc_b = LensCenter + aberr_b * Scale * rvector;

	float rval = 0.0;
	float gval = 0.0;
	float bval = 0.0;

	tc_r.y = (1 - tc_r.y);
	tc_g.y = (1 - tc_g.y);
	tc_b.y = (1 - tc_b.y);

	rval = texture2D(warpTexture, tc_r).x;
	gval = texture2D(warpTexture, tc_g).y;
	bval = texture2D(warpTexture, tc_b).z;

	gl_FragColor = vec4(rval, gval, bval, 1.0);
}
