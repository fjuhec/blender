/*
 * Copyright 2011-2014 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __BSDF_DISNEY_DIFFUSE_H__
#define __BSDF_DISNEY_DIFFUSE_H__

/* DISNEY DIFFUSE BRDF
 *
 * Shading model by Brent Burley (Disney): "Physically Based Shading at Disney" (2012)
 */

CCL_NAMESPACE_BEGIN

typedef ccl_addr_space struct DisneyDiffuseBsdf {
	SHADER_CLOSURE_BASE;

	float roughness, flatness;
	float3 N;
} DisneyDiffuseBsdf;


/* DIFFUSE */

ccl_device float3 calculate_disney_diffuse_brdf(const DisneyDiffuseBsdf *bsdf,
	float NdotL, float NdotV, float LdotH)
{
	float FL = schlick_fresnel(NdotL), FV = schlick_fresnel(NdotV);
	float Fd = (1.0f - 0.5f * FL) * (1.0f - 0.5f * FV);

	float ss = 0.0f;
	if(bsdf->flatness > 0.0f) {
		// Based on Hanrahan-Krueger BRDF approximation of isotropic BSSRDF
		// 1.25 scale is used to (roughly) preserve albedo
		// Fss90 used to "flatten" retro-reflection based on roughness
		float Fss90 = LdotH*LdotH * bsdf->roughness;
		float Fss = (1.0f + (Fss90 - 1.0f) * FL) * (1.0f + (Fss90 - 1.0f) * FV);
		ss = 1.25f * (Fss * (1.0f / (NdotL + NdotV) - 0.5f) + 0.5f);
	}

	float value = Fd + (ss - Fd) * bsdf->flatness;
	value *= M_1_PI_F * NdotL;

	return make_float3(value, value, value);
}

ccl_device int bsdf_disney_diffuse_setup(DisneyDiffuseBsdf *bsdf)
{
	bsdf->type = CLOSURE_BSDF_DISNEY_DIFFUSE_ID;
	return SD_BSDF|SD_BSDF_HAS_EVAL;
}

ccl_device int bsdf_disney_diffuse_transmit_setup(DisneyDiffuseBsdf *bsdf)
{
	bsdf->type = CLOSURE_BSDF_DISNEY_DIFFUSE_TRANSMIT_ID;
	return SD_BSDF|SD_BSDF_HAS_EVAL;
}

ccl_device float3 bsdf_disney_diffuse_eval_reflect(const ShaderClosure *sc, const float3 I,
	const float3 omega_in, float *pdf)
{
	const DisneyDiffuseBsdf *bsdf = (const DisneyDiffuseBsdf *)sc;
	bool m_transmittance = bsdf->type == CLOSURE_BSDF_DISNEY_DIFFUSE_TRANSMIT_ID;

	if(m_transmittance)
		return make_float3(0.0f, 0.0f, 0.0f);

	float3 N = bsdf->N;

	if(dot(N, omega_in) > 0.0f) {
		float3 H = normalize(I + omega_in);

		*pdf = fmaxf(dot(N, omega_in), 0.0f) * M_1_PI_F;
		return calculate_disney_diffuse_brdf(bsdf, fmaxf(dot(N, omega_in), 0.0f), fmaxf(dot(N, I), 0.0f), dot(omega_in, H));
	}
	else {
		*pdf = 0.0f;
		return make_float3(0.0f, 0.0f, 0.0f);
	}
}

ccl_device float3 bsdf_disney_diffuse_eval_transmit(const ShaderClosure *sc, const float3 I,
	const float3 omega_in, float *pdf)
{
	const DisneyDiffuseBsdf *bsdf = (const DisneyDiffuseBsdf *)sc;
	bool m_transmittance = bsdf->type == CLOSURE_BSDF_DISNEY_DIFFUSE_TRANSMIT_ID;

	if(!m_transmittance)
		return make_float3(0.0f, 0.0f, 0.0f);

	float3 N = bsdf->N;

	if(dot(-N, omega_in) > 0.0f) {
		float3 H = normalize(I + omega_in);

		*pdf = fmaxf(dot(-N, omega_in), 0.0f) * M_1_PI_F;
		return calculate_disney_diffuse_brdf(bsdf, fmaxf(dot(-N, omega_in), 0.0f), fmaxf(dot(N, I), 0.0f), dot(omega_in, H));
	}
	else {
		*pdf = 0.0f;
		return make_float3(0.0f, 0.0f, 0.0f);
	}
}

ccl_device int bsdf_disney_diffuse_sample(const ShaderClosure *sc,
	float3 Ng, float3 I, float3 dIdx, float3 dIdy, float randu, float randv,
	float3 *eval, float3 *omega_in, float3 *domega_in_dx,
	float3 *domega_in_dy, float *pdf)
{
	const DisneyDiffuseBsdf *bsdf = (const DisneyDiffuseBsdf *)sc;
	bool m_transmittance = bsdf->type == CLOSURE_BSDF_DISNEY_DIFFUSE_TRANSMIT_ID;

	float3 N = bsdf->N;

	if(m_transmittance) {
		sample_uniform_hemisphere(-N, randu, randv, omega_in, pdf);
	}
	else {
		sample_cos_hemisphere(N, randu, randv, omega_in, pdf);
	}

	if(m_transmittance && dot(-Ng, *omega_in) > 0) {
		float3 I_t = -((2 * dot(N, I)) * N - I);
		float3 H = normalize(I_t + *omega_in);

		*eval = calculate_disney_diffuse_brdf(bsdf, fmaxf(dot(-N, *omega_in), 0.0f), fmaxf(dot(N, I), 0.0f), dot(*omega_in, H));

#ifdef __RAY_DIFFERENTIALS__
		// TODO: find a better approximation for the diffuse bounce
		*domega_in_dx = -((2 * dot(N, dIdx)) * N - dIdx);
		*domega_in_dy = -((2 * dot(N, dIdy)) * N - dIdy);
#endif
	}
	else if(!m_transmittance && dot(Ng, *omega_in) > 0) {
		float3 H = normalize(I + *omega_in);

		*eval = calculate_disney_diffuse_brdf(bsdf, fmaxf(dot(N, *omega_in), 0.0f), fmaxf(dot(N, I), 0.0f), dot(*omega_in, H));

#ifdef __RAY_DIFFERENTIALS__
		// TODO: find a better approximation for the diffuse bounce
		*domega_in_dx = -((2 * dot(N, dIdx)) * N - dIdx);
		*domega_in_dy = -((2 * dot(N, dIdy)) * N - dIdy);
#endif
	}
	else {
		*pdf = 0.0f;
	}

	return (m_transmittance) ? LABEL_TRANSMIT|LABEL_DIFFUSE : LABEL_REFLECT|LABEL_DIFFUSE;
}


/* RETRO-REFLECTION */

ccl_device float3 calculate_retro_reflection(const DisneyDiffuseBsdf *bsdf,
	float NdotL, float NdotV, float LdotH)
{
	float FL = schlick_fresnel(NdotL), FV = schlick_fresnel(NdotV);
	float RR = 2.0f * bsdf->roughness * LdotH*LdotH;

	float FRR = RR * (FL + FV + FL * FV * (RR - 1.0f));

	float value = M_1_PI_F * FRR * NdotL;

	return make_float3(value, value, value);
}

ccl_device int bsdf_disney_retro_reflection_setup(DisneyDiffuseBsdf *bsdf)
{
	bsdf->type = CLOSURE_BSDF_DISNEY_RETRO_REFLECTION_ID;
	return SD_BSDF|SD_BSDF_HAS_EVAL;
}

ccl_device float3 bsdf_disney_retro_reflection_eval_reflect(const ShaderClosure *sc, const float3 I,
	const float3 omega_in, float *pdf)
{
	const DisneyDiffuseBsdf *bsdf = (const DisneyDiffuseBsdf *)sc;

	float3 N = bsdf->N;

	if(dot(N, omega_in) > 0.0f) {
		float3 H = normalize(I + omega_in);

		*pdf = fmaxf(dot(N, omega_in), 0.0f) * M_1_PI_F;
		return calculate_retro_reflection(bsdf, fmaxf(dot(N, omega_in), 0.0f), fmaxf(dot(N, I), 0.0f), dot(omega_in, H));
	}
	else {
		*pdf = 0.0f;
		return make_float3(0.0f, 0.0f, 0.0f);
	}
}

ccl_device float3 bsdf_disney_retro_reflection_eval_transmit(const ShaderClosure *sc, const float3 I,
	const float3 omega_in, float *pdf)
{
	return make_float3(0.0f, 0.0f, 0.0f);
}

ccl_device int bsdf_disney_retro_reflection_sample(const ShaderClosure *sc,
	float3 Ng, float3 I, float3 dIdx, float3 dIdy, float randu, float randv,
	float3 *eval, float3 *omega_in, float3 *domega_in_dx,
	float3 *domega_in_dy, float *pdf)
{
	const DisneyDiffuseBsdf *bsdf = (const DisneyDiffuseBsdf *)sc;

	float3 N = bsdf->N;

	sample_uniform_hemisphere(N, randu, randv, omega_in, pdf);

	if(dot(Ng, *omega_in) > 0) {
		float3 H = normalize(I + *omega_in);

		*eval = calculate_retro_reflection(bsdf, fmaxf(dot(N, *omega_in), 0.0f), fmaxf(dot(N, I), 0.0f), dot(*omega_in, H));

#ifdef __RAY_DIFFERENTIALS__
		// TODO: find a better approximation for the diffuse bounce
		*domega_in_dx = -((2 * dot(N, dIdx)) * N - dIdx);
		*domega_in_dy = -((2 * dot(N, dIdy)) * N - dIdy);
#endif
	}
	else {
		*pdf = 0.0f;
	}

	return LABEL_REFLECT|LABEL_DIFFUSE;
}

CCL_NAMESPACE_END

#endif /* __BSDF_DISNEY_DIFFUSE_H__ */


