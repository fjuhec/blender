uniform mat4 ModelViewProjectionMatrix;

in vec3 pos;
in vec4 color;
in float thickness;

out vec4 finalColor;
out float finalThickness;
	
void main(void)
{
	gl_Position = ModelViewProjectionMatrix * vec4( pos, 1.0 );
	finalColor = color;
	finalThickness = thickness;
} 