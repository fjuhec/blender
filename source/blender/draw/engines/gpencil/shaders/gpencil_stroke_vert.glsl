uniform mat4 ModelViewProjectionMatrix;

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
	finalThickness = thickness;
} 