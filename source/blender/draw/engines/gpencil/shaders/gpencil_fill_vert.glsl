uniform mat4 ModelViewProjectionMatrix;

#if __VERSION__ == 120
	attribute vec3 pos;
	attribute vec4 color;
	attribute vec2 texCoord;
	varying vec4 finalColor;
	varying vec2 texCoord_interp;
#else
	in vec3 pos;
	in vec4 color;
	in vec2 texCoord;
	out vec4 finalColor;
	out vec2 texCoord_interp;
#endif
	
void main(void)
{
	gl_Position = ModelViewProjectionMatrix * vec4( pos, 1.0 );
	finalColor = color;
	texCoord_interp = texCoord;
} 