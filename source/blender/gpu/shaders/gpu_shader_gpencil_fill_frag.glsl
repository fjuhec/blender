uniform vec4 color;  
uniform vec4 color2;
uniform int fill_type;
uniform float mix_factor;

uniform float g_angle;
uniform float g_radius;
uniform float g_boxsize;
uniform vec2 g_shift;

uniform float t_angle;
uniform vec2 t_scale;
uniform vec2 t_shift;
uniform int t_mix;
uniform float t_opacity;

uniform sampler2D myTexture;

/* keep this list synchronized with list in DNA_brush_types.h */
#define SOLID 0
#define GRADIENT 1
#define RADIAL 2
#define CHESS 3
#define TEXTURE 4

#if __VERSION__ == 120
	noperspective varying vec2 texCoord_interp;
	#define fragColor gl_FragColor
#else
	noperspective in vec2 texCoord_interp;
	out vec4 fragColor;
	#define texture2D texture
#endif

void main()
{
	vec2 t_center = vec2(0.5, 0.5);
	mat2 matrot_tex = mat2(cos(t_angle), -sin(t_angle), sin(t_angle), cos(t_angle));
	vec2 rot_tex = (matrot_tex * (texCoord_interp - t_center)) + t_center + t_shift;
	vec4 tmp_color = texture2D(myTexture, rot_tex * t_scale);
	vec4 text_color = vec4(tmp_color[0], tmp_color[1], tmp_color[2], tmp_color[3] * t_opacity);

	/* solid fill */
	if (fill_type == SOLID) {
		if (t_mix == 1) {
			fragColor = mix(color, text_color, mix_factor);
		}
		else {
			fragColor = color;
		}
	}
	else {
		vec2 center = vec2(0.5, 0.5) + g_shift;
		mat2 matrot = mat2(cos(g_angle), -sin(g_angle), sin(g_angle), cos(g_angle));
		vec2 rot = (matrot * (texCoord_interp - center)) + center + g_shift;
		/* gradient */
		if (fill_type == GRADIENT) {
			if (mix_factor == 1.0) {
				fragColor = color;
			}
			else if (mix_factor == 0.0) {
				if (t_mix == 1) {
					fragColor = text_color;
				}
				else {
					fragColor = color2;
				}
			}
			else {
				if (t_mix == 1) {
					fragColor = mix(color, text_color, rot.x - mix_factor + 0.5);
				}
				else {
					fragColor = mix(color, color2, rot.x - mix_factor + 0.5);
				}
			}
		}
		/* radial gradient */
		if (fill_type == RADIAL) {
			float distance = length(center - texCoord_interp);
			if (distance > g_radius) {
				discard;
			}
			float intensity = distance / g_radius;
			if (mix_factor >= 1.0) {
				fragColor = color;
			}
			else if (mix_factor <= 0.0) {
				if (t_mix == 1) {
					fragColor = text_color;
				}
				else {
					fragColor = color2;
				}
			}
			else {
				if (t_mix == 1) {
					fragColor = mix(color, text_color, intensity - mix_factor + 0.5);
				}
				else {
					fragColor = mix(color, color2, intensity - mix_factor + 0.5);
				}
			}
		}
		/* chessboard */
		if (fill_type == CHESS) {
			vec2 pos = rot / g_boxsize;
			if ((fract(pos.x) < 0.5 && fract(pos.y) < 0.5) || (fract(pos.x) > 0.5 && fract(pos.y) > 0.5)) {
				fragColor = color;
			}
			else {
				fragColor = color2;
			}
		}
		/* texture */
		if (fill_type == TEXTURE) {
			fragColor = text_color;
		}
	}
}
