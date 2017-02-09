uniform vec4 color;  
uniform vec4 color2;
uniform int fill_type;
uniform float angle;
uniform float factor;

#define SOLID 0
#define GRADIENT 1

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
	if (fill_type == SOLID) {
		fragColor = color;
	}
	/* gradient fill */
	if (fill_type == GRADIENT) {
		float val = texCoord_interp.x * cos(angle) + texCoord_interp.y * sin(angle);
		fragColor = mix(color, color2, val - factor);
	}
}
