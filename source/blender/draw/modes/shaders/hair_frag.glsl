#ifndef MAX_LIGHT
#define MAX_LIGHT 3
#endif

uniform vec4 ambient;
uniform vec4 diffuse;
uniform vec4 specular;

uniform mat4 ProjectionMatrix;

in vec3 fPosition;
in vec3 fTangent;
in vec3 fColor;

out vec4 outColor;

void main()
{
#ifdef SHADING_CLASSIC_BLENDER
	vec3 N = normalize(fTangent);
#endif
#ifdef SHADING_KAJIYA
	vec3 T = normalize(fTangent);
#endif 

	/* view vector computation, depends on orthographics or perspective */
	vec3 V = (ProjectionMatrix[3][3] == 0.0) ? normalize(fPosition) : vec3(0.0, 0.0, -1.0);
#ifdef SHADING_KAJIYA
	float cosine_eye = dot(T, V);
	float sine_eye = sqrt(1.0 - cosine_eye*cosine_eye);
#endif

	vec3 L_diffuse = vec3(0.0);
	vec3 L_specular = vec3(0.0);
#if 0
	for (int i = 0; i < MAX_LIGHT; i++) {
		LightData ld = lights_data[i];

		//vec4 l_vector; /* Non-Normalized Light Vector with length in last component. */
		//l_vector.xyz = ld.l_position - worldPosition;
		//l_vector.w = length(l_vector.xyz);
		//vec3 l_color_vis = ld.l_color * light_visibility(ld, worldPosition, l_vector);

#ifdef SHADING_CLASSIC_BLENDER
		/* diffuse light */
		vec3 light_diffuse = gl_LightSource[i].diffuse.rgb;
		float diffuse_bsdf = max(dot(N, light_direction), 0.0);
		L_diffuse += light_diffuse * diffuse_bsdf * intensity;

		/* specular light */
		vec3 light_specular = gl_LightSource[i].specular.rgb;
		vec3 H = normalize(light_direction - V);
		float specular_bsdf = pow(max(dot(N, H), 0.0), gl_FrontMaterial.shininess);
		L_specular += light_specular * specular_bsdf * intensity;
#endif
#ifdef SHADING_KAJIYA
		float cosine_light = dot(T, light_direction);
		float sine_light = sqrt(1.0 - cosine_light*cosine_light);

		/* diffuse light */
		vec3 light_diffuse = gl_LightSource[i].diffuse.rgb;
		float diffuse_bsdf = sine_light;
		L_diffuse += light_diffuse * diffuse_bsdf * intensity;

		/* specular light */
		vec3 light_specular = gl_LightSource[i].specular.rgb;
		float specular_bsdf = pow(abs(cosine_light)*abs(cosine_eye) + sine_light*sine_eye, gl_FrontMaterial.shininess);
		L_specular += light_specular * specular_bsdf * intensity;
#endif
	}
#endif

	/* sum lighting */
	
	vec3 L = vec3(0.0, 0.0, 0.0);
	L += ambient.rgb;
	L += L_diffuse * diffuse.rgb;
	L += L_specular * specular.rgb;
	float alpha = diffuse.a;

	/* write out fragment color */
	outColor = vec4(L, alpha);
}
