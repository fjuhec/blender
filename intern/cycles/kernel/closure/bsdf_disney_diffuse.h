
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

CCL_NAMESPACE_BEGIN


ccl_device float3 calculate_disney_diffuse_brdf(const ShaderClosure *sc,
	float3 N, float3 V, float3 L, float3 H, float *pdf)
{
	float NdotL = dot(N, L);
	float NdotV = dot(N, V);

    if (NdotL < 0 || NdotV < 0) {
        *pdf = 0.0f;
        return make_float3(0.0f, 0.0f, 0.0f);
    }

	float LdotH = dot(L, H);

	float FL = schlick_fresnel(NdotL), FV = schlick_fresnel(NdotV);
    const float Fd90 = 0.5f + 2.0f * LdotH*LdotH * sc->data0/*roughness*/;
	float Fd = (1.0f * (1.0f - FL) + Fd90 * FL) * (1.0f * (1.0f - FV) + Fd90 * FV); //lerp(1.0f, Fd90, FL) * lerp(1.0f, Fd90, FV);

	float3 value = M_1_PI_F * Fd * sc->color0/*baseColor*/;

	*pdf = M_1_PI_F * 0.5f;

	value *= NdotL;

	return value;
}

ccl_device int bsdf_disney_diffuse_setup(ShaderClosure *sc)
{
	sc->type = CLOSURE_BSDF_DISNEY_DIFFUSE_ID;
	return SD_BSDF|SD_BSDF_HAS_EVAL;
}

ccl_device float3 bsdf_disney_diffuse_eval_reflect(const ShaderClosure *sc, const float3 I,
	const float3 omega_in, float *pdf)
{
	float3 N = normalize(sc->N);
	float3 V = I; // outgoing
	float3 L = omega_in; // incoming
	float3 H = normalize(L + V);

    if (dot(sc->N, omega_in) > 0.0f) {
        float3 value = calculate_disney_diffuse_brdf(sc, N, V, L, H, pdf);

		return value;
    }
    else {
        *pdf = 0.0f;
        return make_float3(0.0f, 0.0f, 0.0f);
    }
}

ccl_device float3 bsdf_disney_diffuse_eval_transmit(const ShaderClosure *sc, const float3 I,
	const float3 omega_in, float *pdf)
{
	return make_float3(0.0f, 0.0f, 0.0f);
}

ccl_device int bsdf_disney_diffuse_sample(const ShaderClosure *sc,
	float3 Ng, float3 I, float3 dIdx, float3 dIdy, float randu, float randv,
	float3 *eval, float3 *omega_in, float3 *domega_in_dx,
	float3 *domega_in_dy, float *pdf)
{
	float3 N = normalize(sc->N);

	sample_uniform_hemisphere(N, randu, randv, omega_in, pdf);

	if (dot(Ng, *omega_in) > 0) {
		float3 H = normalize(I + *omega_in);

		*eval = calculate_disney_diffuse_brdf(sc, N, I, *omega_in, H, pdf);

#ifdef __RAY_DIFFERENTIALS__
		// TODO: find a better approximation for the diffuse bounce
		*domega_in_dx = -((2 * dot(N, dIdx)) * N - dIdx);
		*domega_in_dy = -((2 * dot(N, dIdy)) * N - dIdy);
#endif
	}
	else {
		*pdf = 0;
	}
	return LABEL_DIFFUSE;
}

CCL_NAMESPACE_END

#endif /* __BSDF_DISNEY_DIFFUSE_H__ */


