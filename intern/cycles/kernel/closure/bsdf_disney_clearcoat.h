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

#define MIX(x, y, a) (x * (1.0f - a) + y * a)

/* DISNEY CLEARCOAT */

ccl_device float clear_SchlickFresnel(float u) {
    float m = clamp(1.0f - u, 0.0f, 1.0f);
    float m2 = m * m;
    return m2 * m2 * m; // pow(m, 5)
}

/* structures */
struct DisneyClearcoatBRDFParams {
    // brdf parameters
    float m_clearcoat;
    float m_clearcoatGloss;

    // precomputed values
	float m_clearcoatRoughness;

    void precompute_values() {
		m_clearcoatRoughness = MIX(0.1f, 0.001f, m_clearcoatGloss);
    }
};

typedef struct DisneyClearcoatBRDFParams DisneyClearcoatBRDFParams;


ccl_device_inline void clear_microfacet_ggx_sample_slopes(
	const float cos_theta_i, const float sin_theta_i,
	float randu, float randv, float *slope_x, float *slope_y,
	float *G1i)
{
	/* special case (normal incidence) */
	if(cos_theta_i >= 0.99999f) {
		const float r = safe_sqrtf(randu/(1.0f - randu));
		const float phi = M_2PI_F * randv;
		*slope_x = r * cosf(phi);
		*slope_y = r * sinf(phi);
		*G1i = 1.0f;

		return;
	}

	/* precomputations */
	const float tan_theta_i = sin_theta_i/cos_theta_i;
	const float G1_inv = 0.5f * (1.0f + safe_sqrtf(1.0f + tan_theta_i*tan_theta_i));

	*G1i = 1.0f/G1_inv;

	/* sample slope_x */
	const float A = 2.0f*randu*G1_inv - 1.0f;
	const float AA = A*A;
	const float tmp = 1.0f/(AA - 1.0f);
	const float B = tan_theta_i;
	const float BB = B*B;
	const float D = safe_sqrtf(BB*(tmp*tmp) - (AA - BB)*tmp);
	const float slope_x_1 = B*tmp - D;
	const float slope_x_2 = B*tmp + D;
	*slope_x = (A < 0.0f || slope_x_2*tan_theta_i > 1.0f)? slope_x_1: slope_x_2;

	/* sample slope_y */
	float S;

	if(randv > 0.5f) {
		S = 1.0f;
		randv = 2.0f*(randv - 0.5f);
	}
	else {
		S = -1.0f;
		randv = 2.0f*(0.5f - randv);
	}

	const float z = (randv*(randv*(randv*0.27385f - 0.73369f) + 0.46341f)) / (randv*(randv*(randv*0.093073f + 0.309420f) - 1.000000f) + 0.597999f);
	*slope_y = S * z * safe_sqrtf(1.0f + (*slope_x)*(*slope_x));
}

ccl_device_inline float3 clear_microfacet_sample_stretched(
	const float3 omega_i, const float alpha_x, const float alpha_y,
	const float randu, const float randv,
	bool beckmann, float *G1i)
{
	/* 1. stretch omega_i */
	float3 omega_i_ = make_float3(alpha_x * omega_i.x, alpha_y * omega_i.y, omega_i.z);
	omega_i_ = normalize(omega_i_);

	/* get polar coordinates of omega_i_ */
	float costheta_ = 1.0f;
	float sintheta_ = 0.0f;
	float cosphi_ = 1.0f;
	float sinphi_ = 0.0f;

	if(omega_i_.z < 0.99999f) {
		costheta_ = omega_i_.z;
		sintheta_ = safe_sqrtf(1.0f - costheta_*costheta_);

		float invlen = 1.0f/sintheta_;
		cosphi_ = omega_i_.x * invlen;
		sinphi_ = omega_i_.y * invlen;
	}

	/* 2. sample P22_{omega_i}(x_slope, y_slope, 1, 1) */
	float slope_x, slope_y;

    clear_microfacet_ggx_sample_slopes(costheta_, sintheta_,
			randu, randv, &slope_x, &slope_y, G1i);

	/* 3. rotate */
	float tmp = cosphi_*slope_x - sinphi_*slope_y;
	slope_y = sinphi_*slope_x + cosphi_*slope_y;
	slope_x = tmp;

	/* 4. unstretch */
	slope_x = alpha_x * slope_x;
	slope_y = alpha_y * slope_y;

	/* 5. compute normal */
	return normalize(make_float3(-slope_x, -slope_y, 1.0f));
}

ccl_device int bsdf_disney_clearcoat_setup(ShaderClosure *sc)
{
    sc->type = CLOSURE_BSDF_DISNEY_CLEARCOAT_ID;
    return SD_BSDF|SD_BSDF_HAS_EVAL;
}

ccl_device float3 bsdf_disney_clearcoat_eval_reflect(const ShaderClosure *sc,
    const DisneyClearcoatBRDFParams *params, const float3 I,
    const float3 omega_in, float *pdf)
{
	if (params->m_clearcoat > 0.0f) {
		float alpha = params->m_clearcoatRoughness;
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
			float cosThetaM4 = cosThetaM2 * cosThetaM2;
			float tanThetaM2 = (1 - cosThetaM2) / cosThetaM2;
			//D = alpha2 / (M_PI_F * cosThetaM4 * (alpha2 + tanThetaM2) * (alpha2 + tanThetaM2));
			D = (alpha2 - 1) / (M_PI_F * logf(alpha2) * (1 + (alpha2 - 1) * cosThetaM2));

			/* eq. 34: now calculate G1(i,m) and G1(o,m) */
			G1o = 2 / (1 + safe_sqrtf(1 + 0.0625f * (1 - cosNO * cosNO) / (cosNO * cosNO)));
			G1i = 2 / (1 + safe_sqrtf(1 + 0.0625f * (1 - cosNI * cosNI) / (cosNI * cosNI)));

			float G = G1o * G1i;

			/* eq. 20 */
			float common = D * 0.25f / cosNO;

			float FH = clear_SchlickFresnel(dot(omega_in, m));
			float3 F = MIX(make_float3(0.04f, 0.04f, 0.04f), make_float3(1.0f, 1.0f, 1.0f), FH);

			float3 out = F * G * common * 0.25f * params->m_clearcoat;

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

ccl_device int bsdf_disney_clearcoat_sample(const ShaderClosure *sc, const DisneyClearcoatBRDFParams *params,
    float3 Ng, float3 I, float3 dIdx, float3 dIdy, float randu, float randv,
    float3 *eval, float3 *omega_in, float3 *domega_in_dx,
    float3 *domega_in_dy, float *pdf)
{
	if (params->m_clearcoat > 0.0f) {
		float alpha = params->m_clearcoatRoughness;
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

			local_m = clear_microfacet_sample_stretched(local_I, alpha, alpha,
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
						//float cosThetaM4 = cosThetaM2 * cosThetaM2;
						//float tanThetaM2 = 1/(cosThetaM2) - 1;
						//D = alpha2 / (M_PI_F * cosThetaM4 * (alpha2 + tanThetaM2) * (alpha2 + tanThetaM2));
						D = (alpha2 - 1) / (M_PI_F * logf(alpha2) * (1 + (alpha2 - 1) * cosThetaM2));

						/* eval BRDF*cosNI */
						float cosNI = dot(N, *omega_in);

						/* eq. 34: now calculate G1(i,m) */
						G1o = 2 / (1 + safe_sqrtf(1 + 0.0625f * (1 - cosNO * cosNO) / (cosNO * cosNO)));
						G1i = 2 / (1 + safe_sqrtf(1 + 0.0625f * (1 - cosNI * cosNI) / (cosNI * cosNI)));

						/* see eval function for derivation */
						float common = (G1o * D) * 0.25f / cosNO;
						*pdf = common;

						float FH = clear_SchlickFresnel(dot(*omega_in, m));
						float3 F = MIX(make_float3(0.04f, 0.04f, 0.04f), make_float3(1.0f, 1.0f, 1.0f), FH);

						*eval = G1i * common * F * 0.25f * params->m_clearcoat;
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

