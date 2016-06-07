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

#ifndef __BSDF_DISNEY_CLEARCOAT_H__
#define __BSDF_DISNEY_CLEARCOAT_H__

CCL_NAMESPACE_BEGIN


ccl_device int bsdf_disney_clearcoat_setup(ShaderClosure *sc)
{
	/* clearcoat roughness */
	sc->custom1 = 0.1f * (1.0f - sc->data1/*clearcoatGloss*/) + 0.001f * sc->data1/*clearcoatGloss*/; // lerp(0.1f, 0.001f, sc->data1/*clearcoatGloss*/); // 

    sc->type = CLOSURE_BSDF_DISNEY_CLEARCOAT_ID;
    return SD_BSDF|SD_BSDF_HAS_EVAL;
}

ccl_device float3 bsdf_disney_clearcoat_eval_reflect(const ShaderClosure *sc, const float3 I,
    const float3 omega_in, float *pdf)
{
	if (sc->data0 > 0.0f) {
		float alpha = sc->custom1;
		float3 N = sc->N;

		if (alpha <= 1e-4f)
			return make_float3(0.0f, 0.0f, 0.0f);

		float cosNO = dot(N, I);
		float cosNI = dot(N, omega_in);

		if (cosNI > 0 && cosNO > 0) {
			/* get half vector */
			float3 m = normalize(omega_in + I);
			float alpha2 = alpha * alpha;
			float D, G1o, G1i;

			/* isotropic
				* eq. 20: (F*G*D)/(4*in*on)
				* eq. 33: first we calculate D(m) */
			float cosThetaM = dot(N, m);
			float cosThetaM2 = cosThetaM * cosThetaM;
			D = (alpha2 - 1) / (M_PI_F * logf(alpha2) * (1 + (alpha2 - 1) * cosThetaM2));

			/* eq. 34: now calculate G1(i,m) and G1(o,m) */
			G1o = 2 / (1 + safe_sqrtf(1 + 0.0625f * (1 - cosNO * cosNO) / (cosNO * cosNO)));
			G1i = 2 / (1 + safe_sqrtf(1 + 0.0625f * (1 - cosNI * cosNI) / (cosNI * cosNI)));

			float G = G1o * G1i;

			/* eq. 20 */
			float common = D * 0.25f / cosNO;

			float FH = schlick_fresnel(dot(omega_in, m));
			float3 F = (0.04f * (1.0f - FH) + 1.0f * FH) * 0.25f * sc->data0/*clearcoat*/ * make_float3(1.0f, 1.0f, 1.0f); // lerp(make_float3(0.04f, 0.04f, 0.04f), make_float3(1.0f, 1.0f, 1.0f), FH);

			float3 out = F * G * common;

			/* eq. 2 in distribution of visible normals sampling
			 * pm = Dw = G1o * dot(m, I) * D / dot(N, I); */

			/* eq. 38 - but see also:
			 * eq. 17 in http://www.graphics.cornell.edu/~bjw/wardnotes.pdf
			 * pdf = pm * 0.25 / dot(m, I); */
			*pdf = G1o * common;

			return out;
		}
	}

	return make_float3(0.0f, 0.0f, 0.0f);
}

ccl_device float3 bsdf_disney_clearcoat_eval_transmit(const ShaderClosure *sc, const float3 I, const float3 omega_in, float *pdf)
{
    return make_float3(0.0f, 0.0f, 0.0f);
}

ccl_device int bsdf_disney_clearcoat_sample(const ShaderClosure *sc,
    float3 Ng, float3 I, float3 dIdx, float3 dIdy, float randu, float randv,
    float3 *eval, float3 *omega_in, float3 *domega_in_dx,
    float3 *domega_in_dy, float *pdf)
{
	if (sc->data0 > 0.0f) {
		float alpha = sc->custom1;
		float3 N = sc->N;

		float cosNO = dot(N, I);
		if (cosNO > 0) {
			float3 X, Y, Z = N;

			make_orthonormals(Z, &X, &Y);

			/* importance sampling with distribution of visible normals. vectors are
			 * transformed to local space before and after */
			float3 local_I = make_float3(dot(X, I), dot(Y, I), cosNO);
			float3 local_m;
			float3 m;
			float G1o;

			local_m = importance_sample_microfacet_stretched(local_I, alpha, alpha,
				randu, randv, false, &G1o);

			m = X*local_m.x + Y*local_m.y + Z*local_m.z;
			float cosThetaM = local_m.z;

			/* reflection or refraction? */
			float cosMO = dot(m, I);

			if (cosMO > 0) {
				/* eq. 39 - compute actual reflected direction */
				*omega_in = 2 * cosMO * m - I;

				if (dot(Ng, *omega_in) > 0) {
					if (alpha <= 1e-4f) {
						/* some high number for MIS */
						*pdf = 1e6f;
						*eval = make_float3(1e6f, 1e6f, 1e6f);
					}
					else {
						/* microfacet normal is visible to this ray */
						/* eq. 33 */
						float alpha2 = alpha * alpha;
						float D, G1i;

						float cosThetaM2 = cosThetaM * cosThetaM;
						D = (alpha2 - 1) / (M_PI_F * logf(alpha2) * (1 + (alpha2 - 1) * cosThetaM2));

						/* eval BRDF*cosNI */
						float cosNI = dot(N, *omega_in);

						/* eq. 34: now calculate G1(i,m) */
						G1o = 2 / (1 + safe_sqrtf(1 + 0.0625f * (1 - cosNO * cosNO) / (cosNO * cosNO)));
						G1i = 2 / (1 + safe_sqrtf(1 + 0.0625f * (1 - cosNI * cosNI) / (cosNI * cosNI)));

						/* see eval function for derivation */
						float common = (G1o * D) * 0.25f / cosNO;
						*pdf = common;

						float FH = schlick_fresnel(dot(*omega_in, m));
						float3 F = make_float3(0.04f, 0.04f, 0.04f) * (1.0f - FH) + make_float3(1.0f, 1.0f, 1.0f) * FH; // lerp(make_float3(0.04f, 0.04f, 0.04f), make_float3(1.0f, 1.0f, 1.0f), FH);

						*eval = G1i * common * F * 0.25f * sc->data0/*clearcoat*/;
					}

#ifdef __RAY_DIFFERENTIALS__
					*domega_in_dx = (2 * dot(m, dIdx)) * m - dIdx;
					*domega_in_dy = (2 * dot(m, dIdy)) * m - dIdy;
#endif
				}
			}
		}
	}

	return LABEL_REFLECT|LABEL_GLOSSY;
}

CCL_NAMESPACE_END

#endif /* __BSDF_DISNEY_CLEARCOAT_H__ */

