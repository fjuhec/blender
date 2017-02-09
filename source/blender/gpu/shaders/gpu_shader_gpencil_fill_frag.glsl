uniform vec4 color;  
uniform vec4 color2;
uniform int fill_type;

#if __VERSION__ == 120
	noperspective varying vec2 texCoord_interp;
	#define fragColor gl_FragColor
#else
	noperspective in vec2 texCoord_interp;
	out vec4 fragColor;
#endif

void main()
{
	/* solid fill */
	if (fill_type == 0) {
		fragColor = color;
	}
	/* gradient fill */
	if (fill_type == 1) {
		fragColor = mix(color, color2, texCoord_interp.x);
	}
}
