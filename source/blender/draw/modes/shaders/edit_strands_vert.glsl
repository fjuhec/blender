
/* Draw Curve Vertices */

uniform mat4 ModelViewProjectionMatrix;
uniform vec2 viewportSize;
uniform vec4 color;
uniform vec4 colorSelect;
uniform float sizeVertex;

in vec3 pos;
in int flag;

out vec4 finalColor;

#define VERTEX_SELECTED (1 << 0)

/* project to screen space */
vec2 proj(vec4 pos)
{
	return (0.5 * (pos.xy / pos.w) + 0.5) * viewportSize;
}

void main()
{
	gl_Position = ModelViewProjectionMatrix * vec4(pos, 1.0);
	gl_PointSize = sizeVertex;

	if ((flag & VERTEX_SELECTED) != 0) {
		finalColor = colorSelect;
	}
	else {
		finalColor = color;
	}
}
