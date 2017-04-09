#if __VERSION__ == 120
	varying vec4 mColor;
	varying vec2 mTexCoord;
	#define fragColor gl_FragColor
#else
	in vec4 mColor;
	in vec2 mTexCoord;
	out vec4 fragColor;
#endif


void main()
{
	fragColor = mColor;
}
