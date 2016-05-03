
/*
 * Adapted from Open Shading Language with this license:
 *
 * Copyright (c) 2009-2010 Sony Pictures Imageworks Inc., et al.
 * All Rights Reserved.
 *
 * Modifications Copyright 2011, Blender Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the name of Sony Pictures Imageworks nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __BSDF_DISNEY_DIFFUSE_H__
#define __BSDF_DISNEY_DIFFUSE_H__

#include <cmath>

CCL_NAMESPACE_BEGIN

/* DISNEY DIFFUSE */

ccl_device float diff_sqr(float a) {
	return a * a;
}

ccl_device float3 diff_mon2lin(float3 x, float gamma) {
	return make_float3(pow(x[0], gamma), pow(x[1], gamma), pow(x[2], gamma));
}

ccl_device float diff_GTR1(float NdotH, float a) {
	if (a >= 1.0f) return 1.0f / M_PI_F;
	float a2 = a*a;
	float t = 1.0f + (a2 - 1.0f) * NdotH * NdotH;
	return (a2 - 1.0f) / (M_PI_F * log(a2) * t);
}

ccl_device float diff_GTR2(float NdotH, float a) {
	float a2 = a * a;
	float t = 1.0f + (a2 - 1.0f) * NdotH * NdotH;
	return a2 / (M_PI_F * t * t);
}

ccl_device float diff_GTR2_aniso(
	float NdotH,
	float HdotX,
	float HdotY,
	float ax,
	float ay)
{
	return 1.0f / (M_PI_F * ax * ay * diff_sqr(diff_sqr(HdotX / ax) + diff_sqr(HdotY / ay)
		+ NdotH * NdotH));
}

ccl_device float diff_smithG_GGX(float Ndotv, float alphaG) {
	float a = alphaG * alphaG;
	float b = Ndotv * Ndotv;
	return 1.0f / (Ndotv + sqrtf(a + b - a * b));
}

ccl_device float diff_SchlickFresnel(float u) {
	float m = clamp(1.0f - u, 0.0f, 1.0f);
	float m2 = m * m;
	return m2 * m2 * m; // pow(m, 5)
}

ccl_device float3 diff_transform_to_local(const float3& v, const float3& n,
	const float3& x, const float3& y)
{
	return make_float3(dot(v, x), dot(v, n), dot(v, y));
}

ccl_device float3 diff_mix(float3 x, float3 y, float a) {
	return x * (1.0f - a) + y * a;
}

ccl_device float diff_mix(float x, float y, float a) {
	return x * (1.0f - a) + y * a;
}

/* structures */
struct DisneyDiffuseBRDFParams {
	// brdf parameters
	float3 m_base_color;
	float m_subsurface;
	float m_roughness;
	float m_sheen;
	float m_sheen_tint;

	// precomputed values
	float3 m_cdlin, m_ctint, m_csheen;
	float m_cdlum;
	float m_weights[4];

	void precompute_values() {
		m_cdlin = m_base_color;
		m_cdlum = 0.3f * m_cdlin[0] + 0.6f * m_cdlin[1] + 0.1f * m_cdlin[2]; // luminance approx.

		m_ctint = m_cdlum > 0.0f ? m_cdlin / m_cdlum : make_float3(1.0f, 1.0f, 1.0f); // normalize lum. to isolate hue+sat
		m_csheen = diff_mix(make_float3(1.0f, 1.0f, 1.0f), m_ctint, m_sheen_tint);
	}
};

typedef struct DisneyDiffuseBRDFParams DisneyDiffuseBRDFParams;

/* brdf */
ccl_device float3 calculate_disney_diffuse_brdf(const ShaderClosure *sc,
	const DisneyDiffuseBRDFParams *params, float3 N, float3 V, float3 L,
	float3 H, float *pdf, bool withNdotL = true)
{
	float NdotL = dot(N, L);
	float NdotV = dot(N, V);

    if (NdotL < 0 || NdotV < 0) {
        *pdf = 0.0f;
        return make_float3(0.0f, 0.0f, 0.0f);
    }

	float LdotH = dot(L, H);

    float Fd = 0.0f;
	float FL = diff_SchlickFresnel(NdotL), FV = diff_SchlickFresnel(NdotV);

    if (params->m_subsurface != 1.0f) {
	    const float Fd90 = 0.5f + 2.0f * LdotH*LdotH * params->m_roughness;
	    Fd = diff_mix(1.0f, Fd90, FL) * diff_mix(1.0f, Fd90, FV);
    }

    if (params->m_subsurface > 0.0f) {
	    float Fss90 = LdotH*LdotH * params->m_roughness;
	    float Fss = diff_mix(1.0f, Fss90, FL) * diff_mix(1.0f, Fss90, FV);
	    float ss = 1.25f * (Fss * (1.0f / (NdotL + NdotV) - 0.5f) + 0.5f);
        Fd = diff_mix(Fd, ss, params->m_subsurface);
    }

	float3 value = M_1_PI_F * Fd * params->m_cdlin;

	//if (params->m_gamma > 0.5f)
		*pdf = M_1_PI_F;

	/*if (params->m_exposure > 0.95f)
		*pdf *= params->m_cdlum;
	else if (params->m_exposure > 0.85f)
		*pdf *= params->m_cdlum * NdotL;
	else if (params->m_exposure > 0.75f)
		*pdf *= params->m_cdlum * 0.5f;
	else if (params->m_exposure > 0.65f)
		*pdf *= params->m_cdlum * NdotL * 0.5f;
	else if (params->m_exposure > 0.55f)
		*pdf *= NdotL;
	else if (params->m_exposure > 0.45f)
		*pdf *= NdotL * 0.5f;
	else if (params->m_exposure > 0.35f)*/
		*pdf *= 0.5f;

	// sheen component
	if (params->m_sheen != 0.0f) {
	    float FH = diff_SchlickFresnel(LdotH);

		value += FH * params->m_sheen * params->m_csheen;
		//*pdf += 0.5f * M_1_PI_F * params->m_sheen;
	}

	if (withNdotL)
		value *= NdotL;

	// brightness
	//value *= params->m_brightness;

	// exposure
	//value *= pow(2.0f, params->m_exposure);

	// gamma
	/*value[0] = pow(value[0], 1.0f / params->m_gamma);
	value[1] = pow(value[1], 1.0f / params->m_gamma);
	value[2] = pow(value[2], 1.0f / params->m_gamma);*/

	return value;
}

ccl_device int bsdf_disney_diffuse_setup(ShaderClosure *sc)
{
	sc->type = CLOSURE_BSDF_DISNEY_DIFFUSE_ID;
	return SD_BSDF|SD_BSDF_HAS_EVAL;
}

ccl_device float3 bsdf_disney_diffuse_eval_reflect(const ShaderClosure *sc,
	const DisneyDiffuseBRDFParams *params, const float3 I,
	const float3 omega_in, float *pdf)
{
	float3 N = normalize(sc->N);
	float3 V = I; // outgoing
	float3 L = omega_in; // incoming
	float3 H = normalize(L + V);

    if (dot(sc->N, omega_in) > 0.0f) {
        float3 value = calculate_disney_diffuse_brdf(sc, params, N, V, L, H, pdf);

		return value;
    }
    else {
        *pdf = 0.0f;
        return make_float3(0.0f, 0.0f, 0.0f);
    }
}

ccl_device float3 bsdf_disney_diffuse_eval_transmit(const ShaderClosure *sc, const float3 I, const float3 omega_in, float *pdf)
{
	return make_float3(0.0f, 0.0f, 0.0f);
}

ccl_device int bsdf_disney_diffuse_sample(const ShaderClosure *sc, const DisneyDiffuseBRDFParams *params,
	float3 Ng, float3 I, float3 dIdx, float3 dIdy, float randu, float randv,
	float3 *eval, float3 *omega_in, float3 *domega_in_dx,
	float3 *domega_in_dy, float *pdf)
{
	float3 N = normalize(sc->N);

	/*if (params->m_brightness > 0.5f)
		sample_cos_hemisphere(N, randu, randv, omega_in, pdf);
	else*/
		sample_uniform_hemisphere(N, randu, randv, omega_in, pdf);

	if (dot(Ng, *omega_in) > 0) {
		float3 V = I; // outgoing
		float3 L = *omega_in; // incoming
		float3 H = normalize(L + V);

		*eval = calculate_disney_diffuse_brdf(sc, params, N, V, L, H, pdf, true);

#ifdef __RAY_DIFFERENTIALS__
		// TODO: find a better approximation for the diffuse bounce
		*domega_in_dx = -((2 * dot(N, dIdx)) * N - dIdx);
		*domega_in_dy = -((2 * dot(N, dIdy)) * N - dIdy);
#endif
	}
	else {
		*pdf = 0;
	}
	return LABEL_REFLECT|LABEL_DIFFUSE;
}

CCL_NAMESPACE_END

#endif /* __BSDF_DISNEY_DIFFUSE_H__ */


