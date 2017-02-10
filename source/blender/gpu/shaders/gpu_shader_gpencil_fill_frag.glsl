uniform vec4 color;  
uniform vec4 color2;
uniform int fill_type;
uniform float angle;
uniform float factor;
uniform vec2 shift;

#define SOLID 0
#define GRADIENT 1
#define RADIAL 2
#define CHESS 3

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
	else {
		mat2 matrot = mat2(cos(angle), -sin(angle), sin(angle), cos(angle));
		vec2 rot = (matrot * texCoord_interp) + shift;
		/* gradient */
		if (fill_type == GRADIENT) {
			fragColor = mix(color, color2, rot.x - factor);
		}
		/* radial gradient */
		if (fill_type == RADIAL) {
			vec2 center = vec2(0.5, 0.5);
			float radius = factor;
			if (radius < 0.01) {
				radius = 0.01;
			}
			float distance = length(center - texCoord_interp);
			float intensity = 1.0 - min(distance, radius) / radius;
			fragColor = mix(color2, color, intensity);
		}
		/* chessboard */
		if (fill_type == CHESS) {
			vec2 pos = rot / abs(factor);
			if ((fract(pos.x) < 0.5 && fract(pos.y) < 0.5) || (fract(pos.x) > 0.5 && fract(pos.y) > 0.5)) {
				fragColor = color;
			}
			else {
				fragColor = color2;
			}
		}
	}
}
