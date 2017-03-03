uniform mat4 ModelViewProjectionMatrix;

#if __VERSION__ == 120
	attribute vec3 pos;
	attribute vec2 texCoord;
	varying vec2 texCoord_interp;
#else
	in vec3 pos;
	in vec2 texCoord;
	out vec2 texCoord_interp;
#endif
	
void main(void)
{
	gl_Position = ModelViewProjectionMatrix * vec4( pos, 1.0 );
	texCoord_interp = texCoord;
} 