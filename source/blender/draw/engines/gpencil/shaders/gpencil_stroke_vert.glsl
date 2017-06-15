uniform mat4 ModelViewProjectionMatrix;
uniform mat4 ProjectionMatrix;

uniform float pixsize;   /* rv3d->pixsize */
uniform float pixelsize; /* U.pixelsize */
uniform int is_persp;    /* 0: Keep Stroke size */

in vec3 pos;
in vec4 color;
in float thickness;

out vec4 finalColor;
out float finalThickness;

#define TRUE 1

float defaultpixsize = pixsize * pixelsize;

void main(void)
{
	gl_Position = ModelViewProjectionMatrix * vec4( pos, 1.0 );
	finalColor = color;

	if (is_persp == TRUE) {
		float size = (ProjectionMatrix[3][3] == 0.0) ? (thickness / (-gl_Position.z * defaultpixsize)) : (thickness / defaultpixsize);
		float objscale = (ModelViewProjectionMatrix[0][0] + ModelViewProjectionMatrix[1][1] + ModelViewProjectionMatrix[2][2]) / 3.0;
		finalThickness = max((abs(size) / 40.0) * objscale , 1.0);
	}
	else {
		finalThickness = thickness;
	}
	
} 