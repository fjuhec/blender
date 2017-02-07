uniform vec4 color;  

#if __VERSION__ == 120
	//varying vec3 pos_interp;
	//varying vec2 texCoord_interp;

	#define fragColor gl_FragColor
#else
	//in vec3 pos_interp;
	//in vec2 texCoord_interp;

	out vec4 fragColor;
#endif

void main()
{
	fragColor = vec4(vec3(color), 0.2); //color_interp;
}
