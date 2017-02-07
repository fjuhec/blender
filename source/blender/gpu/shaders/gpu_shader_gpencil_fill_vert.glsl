uniform mat4 ModelViewProjectionMatrix;

#if __VERSION__ == 120
	attribute vec3 pos;
	//attribute vec2 texCoord;

	//varying vec3 pos_interp;
	//varying vec2 texCoord_interp;
#else
	in vec3 pos;
	//in vec2 texCoord;

	//out vec3 pos_interp;
	//smooth out vec2 texCoord_interp;
#endif
	
void main(void)
{
	gl_Position = ModelViewProjectionMatrix * vec4( pos, 1.0 );
	//pos_interp = ModelViewProjectionMatrix * vec4( pos, 1.0 );

	//gl_Position = pos_interp;
	//texCoord_interp = texCoord;
} 