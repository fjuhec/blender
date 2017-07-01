uniform int stroke_type;
uniform sampler2D myTexture;

in vec4 mColor;
in vec2 mTexCoord;
out vec4 fragColor;

#define texture2D texture

/* keep this list synchronized with list in DNA_brush_types.h */
#define SOLID 0
#define TEXTURE 2
#define PATTERN 3

void main()
{
	/* Solid */
	if (stroke_type == SOLID) {
		fragColor = mColor;
	}
	/* texture */
	if (stroke_type == TEXTURE) {
		fragColor =  texture2D(myTexture, mTexCoord);
	}
	/* pattern */
	if (stroke_type == PATTERN) {
		vec4 text_color = texture2D(myTexture, mTexCoord);
		/* normalize texture color */
		float nvalue = 1.0 - ((text_color.x + text_color.y + text_color.z) / 3.0);
		fragColor = mix(vec4(0.0, 0.0, 0.0, 0.0), mColor, nvalue);
	}
}
