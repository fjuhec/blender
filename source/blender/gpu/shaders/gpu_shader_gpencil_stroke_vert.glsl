uniform mat4 ModelViewProjectionMatrix;
uniform mat4 ViewMatrix;

#if __VERSION__ == 120
	attribute vec3 pos;
	attribute vec4 color;
	attribute float thickness;

	varying vec4 finalColor;
#else
	in vec3 pos;
	in vec4 color;
	in float thickness;

  out vec4 finalColor;
  out float finalThickness;
#endif
	
void main(void)
{
	gl_Position = ModelViewProjectionMatrix * vec4( pos, 1.0 );
	finalColor = color;
	float tmp = (ViewMatrix  * vec4(0.0, 0.0, thickness, 1.0)).z;
	finalThickness = thickness;
} 