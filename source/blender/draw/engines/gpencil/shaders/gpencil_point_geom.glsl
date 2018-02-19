uniform mat4 ModelViewProjectionMatrix;
uniform vec2 Viewport;
uniform int xraymode;

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

in vec4 finalColor[1];
in float finalThickness[1];
in vec2 finaluvdata[1];

out vec4 mColor;
out vec2 mTexCoord;

#define GP_XRAY_FRONT 0
#define GP_XRAY_3DSPACE 1
#define GP_XRAY_BACK  2

/* project 3d point to 2d on screen space */
vec2 toScreenSpace(vec4 vertex)
{
	return vec2(vertex.xy / vertex.w) * Viewport;
}

/* get zdepth value */
float getZdepth(vec4 point)
{
	if (xraymode == GP_XRAY_FRONT) {
		return 0.0;
	}
	if (xraymode == GP_XRAY_3DSPACE) {
		return (point.z / point.w);
	}
	if  (xraymode == GP_XRAY_BACK) {
		return 1.0;
	}

	/* in front by default */
	return 0.0;
}
void main(void)
{
	/* receive 4 points */
	vec4 P0 = gl_in[0].gl_Position;
	vec2 sp0 = toScreenSpace(P0);
	
	float size = finalThickness[0] * 0.5; 
	float aspect = 1.0;

	/* generate the triangle strip */
	mTexCoord = vec2(0, 1);
	mColor = finalColor[0];
	gl_Position = vec4(vec2(sp0.x - size, sp0.y + size * aspect) / Viewport, getZdepth(P0), 1.0);
	EmitVertex();

	mTexCoord = vec2(0, 0);
	mColor = finalColor[0];
	gl_Position = vec4(vec2(sp0.x - size, sp0.y - size * aspect) / Viewport, getZdepth(P0), 1.0);
	EmitVertex();

	mTexCoord = vec2(1, 1);
	mColor = finalColor[0];
	gl_Position = vec4(vec2(sp0.x + size, sp0.y + size * aspect) / Viewport, getZdepth(P0), 1.0);
	EmitVertex();

	mTexCoord = vec2(1, 0);
	mColor = finalColor[0];
	gl_Position = vec4(vec2(sp0.x + size, sp0.y - size * aspect) / Viewport, getZdepth(P0), 1.0);
	EmitVertex();

	EndPrimitive();
}
