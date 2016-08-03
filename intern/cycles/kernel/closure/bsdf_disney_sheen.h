
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

#ifndef __BSDF_DISNEY_SHEEN_H__
#define __BSDF_DISNEY_SHEEN_H__

CCL_NAMESPACE_BEGIN

typedef ccl_addr_space struct DisneySheenBsdf {
	SHADER_CLOSURE_BASE;

	float sheen, sheenTint;
	float3 N;
	float3 baseColor, csheen0;
} DisneySheenBsdf;

ccl_device float3 calculate_disney_sheen_brdf(const DisneySheenBsdf *bsdf,
	float3 N, float3 V, float3 L, float3 H, float *pdf)
{
	float NdotL = dot(N, L);
	float NdotV = dot(N, V);

    if (NdotL < 0 || NdotV < 0 || bsdf->sheen == 0.0f) {
        *pdf = 0.0f;
        return make_float3(0.0f, 0.0f, 0.0f);
    }

	float LdotH = dot(L, H);

	*pdf = M_1_PI_F * 0.5f;

	float FH = schlick_fresnel(LdotH);

	float3 value = FH * bsdf->sheen * bsdf->csheen0;

	value *= NdotL;

	return value;
}

ccl_device int bsdf_disney_sheen_setup(DisneySheenBsdf *bsdf)
{
	float m_cdlum = 0.3f * bsdf->baseColor.x + 0.6f * bsdf->baseColor.y + 0.1f * bsdf->baseColor.z; // luminance approx.

	float3 m_ctint = m_cdlum > 0.0f ? bsdf->baseColor / m_cdlum : make_float3(1.0f, 1.0f, 1.0f); // normalize lum. to isolate hue+sat

	/* csheen0 */
	bsdf->csheen0 = make_float3(1.0f, 1.0f, 1.0f) * (1.0f - bsdf->sheenTint) + m_ctint * bsdf->sheenTint; // lerp(make_float3(1.0f, 1.0f, 1.0f), m_ctint, sc->data1/*sheenTint*/);

	bsdf->type = CLOSURE_BSDF_DISNEY_SHEEN_ID;
	return SD_BSDF|SD_BSDF_HAS_EVAL;
}

ccl_device float3 bsdf_disney_sheen_eval_reflect(const ShaderClosure *sc, const float3 I,
	const float3 omega_in, float *pdf)
{
	const DisneySheenBsdf *bsdf = (const DisneySheenBsdf *)sc;

	float3 N = normalize(bsdf->N);
	float3 V = I; // outgoing
	float3 L = omega_in; // incoming
	float3 H = normalize(L + V);

    if (dot(bsdf->N, omega_in) > 0.0f) {
        float3 value = calculate_disney_sheen_brdf(bsdf, N, V, L, H, pdf);

		return value;
    }
    else {
        *pdf = 0.0f;
        return make_float3(0.0f, 0.0f, 0.0f);
    }
}

ccl_device float3 bsdf_disney_sheen_eval_transmit(const ShaderClosure *sc, const float3 I,
	const float3 omega_in, float *pdf)
{
	return make_float3(0.0f, 0.0f, 0.0f);
}

ccl_device int bsdf_disney_sheen_sample(const ShaderClosure *sc,
	float3 Ng, float3 I, float3 dIdx, float3 dIdy, float randu, float randv,
	float3 *eval, float3 *omega_in, float3 *domega_in_dx,
	float3 *domega_in_dy, float *pdf)
{
	const DisneySheenBsdf *bsdf = (const DisneySheenBsdf *)sc;

	float3 N = normalize(bsdf->N);

	sample_uniform_hemisphere(N, randu, randv, omega_in, pdf);

	if (dot(Ng, *omega_in) > 0) {
		float3 H = normalize(I + *omega_in);

		*eval = calculate_disney_sheen_brdf(bsdf, N, I, *omega_in, H, pdf);

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

#endif /* __BSDF_DISNEY_SHEEN_H__ */


