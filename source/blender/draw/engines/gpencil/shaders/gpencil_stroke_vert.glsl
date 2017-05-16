uniform mat4 ModelViewProjectionMatrix;

uniform float pixsize;   /* rv3d->pixsize */
uniform float pixelsize; /* U.pixelsize */
uniform int is_persp;    /* rv3d->is_persp (1-yes) */

in vec3 pos;
in vec4 color;
in float thickness;

out vec4 finalColor;
out float finalThickness;

#define TRUE 1

/* port of c code function */
float mul_project_m4_v3_zfac(mat4 mat, vec3 co)
{
	return (mat[0][3] * co[0]) +
	       (mat[1][3] * co[1]) +
	       (mat[2][3] * co[2]) + mat[3][3];
}
	
float defaultpixsize = pixsize * pixelsize;

void main(void)
{
	gl_Position = ModelViewProjectionMatrix * vec4( pos, 1.0 );
	finalColor = color;

	float pixsize = mul_project_m4_v3_zfac(ModelViewProjectionMatrix, pos) * defaultpixsize;
	float scale_thickness = 0.0f;
	
	if (is_persp == TRUE) {
		/* need a factor to mimmic old glLine size, 10.0f works fine */
		scale_thickness = (defaultpixsize / pixsize) * 10.0f;
	}
	else {
		scale_thickness = (1.0f / pixsize) / 100.0f;
	}

	finalThickness = thickness * max(scale_thickness, 0.01f);
} 