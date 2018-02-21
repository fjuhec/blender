uniform vec4 color2;
uniform int fill_type;
uniform float mix_factor;

uniform float g_angle;
uniform float g_radius;
uniform float g_boxsize;
uniform vec2 g_scale;
uniform vec2 g_shift;

uniform float t_angle;
uniform vec2 t_scale;
uniform vec2 t_offset;
uniform int t_mix;
uniform int t_flip;
uniform float t_opacity;
uniform int xraymode;

uniform sampler2D myTexture;
uniform int t_clamp;

/* keep this list synchronized with list in DNA_brush_types.h */
#define SOLID 0
#define GRADIENT 1
#define RADIAL 2
#define CHESS 3
#define TEXTURE 4
#define PATTERN 5

#define GP_XRAY_FRONT 0
#define GP_XRAY_3DSPACE 1
#define GP_XRAY_BACK  2

in vec4 finalColor;
in vec2 texCoord_interp;
out vec4 fragColor;
#define texture2D texture

void set_color(in vec4 color, in vec4 color2, in vec4 tcolor, in float mixv, in float factor, 
			   in int tmix, in int flip, out vec4 ocolor)
{
	/* full color A */
	if (mixv == 1.0) {
		if (tmix == 1) {
			ocolor = (flip == 0) ? color : tcolor;
		}
		else {
			ocolor = (flip == 0) ? color : color2;
		}
	}
	/* full color B */
	else if (mixv == 0.0) {
		if (tmix == 1) {
			ocolor = (flip == 0) ? tcolor : color;
		}
		else {
			ocolor = (flip == 0) ? color2 : color;
		}
	}
	/* mix of colors */
	else {
		if (tmix == 1) {
			ocolor = (flip == 0) ? mix(color, tcolor, factor) : mix(tcolor, color, factor);
		}
		else {
			ocolor = (flip == 0) ? mix(color, color2, factor) : mix(color2, color, factor);
		}
	}
}

void main()
{
	vec2 t_center = vec2(0.5, 0.5);
	mat2 matrot_tex = mat2(cos(t_angle), -sin(t_angle), sin(t_angle), cos(t_angle));
	vec2 rot_tex = (matrot_tex * (texCoord_interp - t_center)) + t_center + t_offset;
	vec4 tmp_color;
	tmp_color = (t_clamp == 0) ? texture2D(myTexture, rot_tex * t_scale) : texture2D(myTexture, clamp(rot_tex * t_scale, 0.0, 1.0));
	vec4 text_color = vec4(tmp_color[0], tmp_color[1], tmp_color[2], tmp_color[3] * t_opacity);
	vec4 chesscolor;

	/* solid fill */
	if (fill_type == SOLID) {
		fragColor = (t_mix == 1) ? mix(finalColor, text_color, mix_factor) : finalColor;
	}
	else {
		vec2 center = vec2(0.5, 0.5) + g_shift;
		mat2 matrot = mat2(cos(g_angle), -sin(g_angle), sin(g_angle), cos(g_angle));
		vec2 rot = (((matrot * (texCoord_interp - center)) + center) * g_scale) + g_shift;
		/* gradient */
		if (fill_type == GRADIENT) {
			set_color(finalColor, color2, text_color, mix_factor, rot.x - mix_factor + 0.5, t_mix, t_flip, fragColor);
		}
		/* radial gradient */
		if (fill_type == RADIAL) {
			float in_rad = g_radius * mix_factor;
			float ex_rad = g_radius - in_rad;
			float intensity = 0;
			float distance = length((center - texCoord_interp) * g_scale);
			if (distance > g_radius) {
				discard;
			}
			if (distance > in_rad) {
				intensity = clamp(((distance - in_rad) / ex_rad), 0.0, 1.0);
			}
			set_color(finalColor, color2, text_color, mix_factor, intensity, t_mix, t_flip, fragColor);
		}
		/* chessboard */
		if (fill_type == CHESS) {
			vec2 pos = rot / g_boxsize;
			if ((fract(pos.x) < 0.5 && fract(pos.y) < 0.5) || (fract(pos.x) > 0.5 && fract(pos.y) > 0.5)) {
			    chesscolor = (t_flip == 0) ? finalColor : color2; 
			}
			else {
			    chesscolor = (t_flip == 0) ? color2 : finalColor; 
			}
			/* mix with texture */
			fragColor = (t_mix == 1) ? mix(chesscolor, text_color, mix_factor) : chesscolor;
		}
		/* texture */
		if (fill_type == TEXTURE) {
			fragColor = text_color;
		}
		/* pattern */
		if (fill_type == PATTERN) {
			fragColor = finalColor;
			fragColor.a = min(text_color.a, finalColor.a);
		}
	}

	/* set zdepth */
	if (xraymode == GP_XRAY_FRONT) {
		gl_FragDepth = 0.0;
	}
	if (xraymode == GP_XRAY_3DSPACE) {
		gl_FragDepth = gl_FragCoord.z;
	}
	if  (xraymode == GP_XRAY_BACK) {
		gl_FragDepth = 1.0;
	}
}
