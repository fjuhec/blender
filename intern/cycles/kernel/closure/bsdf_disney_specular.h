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

#ifndef __BSDF_DISNEY_SPECULAR_H__
#define __BSDF_DISNEY_SPECULAR_H__

#include <cmath>

CCL_NAMESPACE_BEGIN

/* DISNEY SPECULAR */

ccl_device float spec_sqr(float a) {
    return a * a;
}

ccl_device float3 spec_mon2lin(float3 x, float gamma) {
    return make_float3(pow(x[0], gamma), pow(x[1], gamma), pow(x[2], gamma));
}

ccl_device float spec_GTR1(float NdotH, float a) {
    if (a >= 1.0f) return 1.0f / M_PI_F;
    float a2 = a*a;
    float t = 1.0f + (a2 - 1.0f) * NdotH * NdotH;
    return (a2 - 1.0f) / (M_PI_F * log(a2) * t);
}

ccl_device float spec_GTR2(float NdotH, float a) {
    float a2 = a * a;
    float t = 1.0f + (a2 - 1.0f) * NdotH * NdotH;
    return a2 / (M_PI_F * t * t);
}

ccl_device float spec_GTR2_aniso(
    float NdotH,
    float HdotX,
    float HdotY,
    float ax,
    float ay)
{
    return 1.0f / (M_PI_F * ax * ay * spec_sqr(spec_sqr(HdotX / ax) + spec_sqr(HdotY / ay)
        + NdotH * NdotH));
}

ccl_device float spec_smithG_GGX(float Ndotv, float alphaG) {
    float a = alphaG * alphaG;
    float b = Ndotv * Ndotv;
    return 1.0f / (Ndotv + sqrtf(a + b - a * b));
}

ccl_device float spec_SchlickFresnel(float u) {
    float m = clamp(1.0f - u, 0.0f, 1.0f);
    float m2 = m * m;
    return m2 * m2 * m; // pow(m, 5)
}

ccl_device float3 spec_transform_to_local(const float3& v, const float3& n,
    const float3& x, const float3& y)
{
    return make_float3(dot(v, x), dot(v, n), dot(v, y));
}

ccl_device float3 spec_mix(float3 x, float3 y, float a) {
    return x * (1.0f - a) + y * a;
}

ccl_device float spec_mix(float x, float y, float a) {
    return x * (1.0f - a) + y * a;
}

ccl_device float spec_max(float a, float b) {
    if (a > b) return a;
    else return b;
}

/* structures */
struct DisneySpecularBRDFParams {
    // brdf parameters
    float3 m_base_color;
    float m_metallic;
    float m_specular;
    float m_specular_tint;
    float m_roughness;
    float m_anisotropic;

    // color correction
    float m_withNdotL;
    float m_brightness;
    float m_gamma;
    float m_exposure;
    float m_mon2lingamma;

    // precomputed values
    float3 m_cdlin, m_ctint, m_cspec0;
    float m_cdlum;
    float m_ax, m_ay;
    float m_roughg;
    bool m_withNdotL_b;

    void precompute_values() {
        m_cdlin = spec_mon2lin(m_base_color, m_mon2lingamma); //make_float3(1.0f, 0.795f, 0.0f));
        m_cdlum = 0.3f * m_cdlin[0] + 0.6f * m_cdlin[1] + 0.1f * m_cdlin[2]; // luminance approx.

        m_ctint = m_cdlum > 0.0f ? m_cdlin / m_cdlum : make_float3(1.0f, 1.0f, 1.0f); // normalize lum. to isolate hue+sat

		m_cspec0 = spec_mix(m_specular * 0.08f * spec_mix(make_float3(1.0f, 1.0f, 1.0f),
			m_ctint, m_specular_tint), m_cdlin, m_metallic);

        float aspect = sqrt(1.0f - m_anisotropic * 0.9f);
        m_ax = spec_max(0.001f, spec_sqr(m_roughness) / aspect);
        m_ay = spec_max(0.001f, spec_sqr(m_roughness) * aspect);

        m_roughg = spec_sqr(m_roughness * 0.5f + 0.5f);

        //m_gamma = clamp(m_gamma, 0.0f, 5.0f);
		//m_exposure = clamp(m_exposure, -6.0f, 6.0f);
        m_withNdotL_b = (m_withNdotL != 0.0f);
    }
};

typedef struct DisneySpecularBRDFParams DisneySpecularBRDFParams;

/*brdf*/
ccl_device float3 calculate_disney_specular_brdf(const ShaderClosure *sc,
    const DisneySpecularBRDFParams *params, float3 N, float3 X, float3 Y,
	float3 V, float3 L, float3 H, float *pdf, bool withNdotL = false)
{
    float NdotL = dot(N, L);
    float NdotV = dot(N, V);

	if (NdotL < 0.0f || NdotV < 0.0f) {
        *pdf = 0.0f;
        return make_float3(0.0f, 0.0f, 0.0f);
    }

    float NdotH = dot(N, H);
    float LdotH = dot(L, H);
    float FH = spec_SchlickFresnel(LdotH);

    float Ds;
    if (params->m_anisotropic > 0.0f)
        Ds = spec_GTR2_aniso(NdotH, dot(H, X), dot(H, Y), params->m_ax, params->m_ay);
    else
		Ds = spec_GTR2(NdotH, params->m_ax);
    float3 Fs = spec_mix(params->m_cspec0, make_float3(1.0f, 1.0f, 1.0f), FH);
    float Go = spec_smithG_GGX(NdotV, params->m_roughg);
    float Gi = spec_smithG_GGX(NdotL, params->m_roughg);
    float Gs = Go * Gi;

    float common = Go * Ds;
    if (params->m_exposure > 0.5f)
        common *= std::fabs(dot(V, H)) / spec_max(1e-6, NdotV);
    else
        common *= 0.25f / spec_max(1e-6, NdotV);

    *pdf = common;
	/*if (params->m_withNdotL > 0.75f)
		*pdf = spec_mix(*pdf, params->m_exposure, spec_SchlickFresnel(NdotV));
	else if (params->m_withNdotL > 0.5f)
		*pdf = spec_mix(*pdf, params->m_exposure, spec_sqr(params->m_roughness));
	else if (params->m_withNdotL > 0.25f)
		*pdf = spec_mix(spec_mix(*pdf, params->m_exposure, spec_sqr(params->m_roughness)), params->m_exposure, spec_SchlickFresnel(NdotV));*/

    float3 value = Gs * Ds * Fs;

	if (withNdotL)
		value *= NdotL;

    // brightness
    //value *= params->m_brightness;

    // exposure
    //value *= pow(2.f, params->m_exposure);

    // gamma
    /*value[0] = pow(value[0], 1.f / params->m_gamma);
    value[1] = pow(value[1], 1.f / params->m_gamma);
    value[2] = pow(value[2], 1.f / params->m_gamma);*/

    return value;
}

ccl_device_inline void spec_microfacet_ggx_sample_slopes(
	const float cos_theta_i, const float sin_theta_i,
	float randu, float randv, float *slope_x, float *slope_y,
	float *G1i)
{
	/* special case (normal incidence) */
	if(cos_theta_i >= 0.99999f) {
		const float r = sqrtf(randu/(1.0f - randu));
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

ccl_device_inline float3 spec_microfacet_sample_stretched(
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

    spec_microfacet_ggx_sample_slopes(costheta_, sintheta_,
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

ccl_device int bsdf_disney_specular_setup(ShaderClosure *sc)
{
    sc->type = CLOSURE_BSDF_DISNEY_SPECULAR_ID;
    return SD_BSDF|SD_BSDF_HAS_EVAL;
}

ccl_device float3 bsdf_disney_specular_eval_reflect(const ShaderClosure *sc,
    const DisneySpecularBRDFParams *params, const float3 I,
    const float3 omega_in, float *pdf)
{
    /*float3 N = normalize(sc->N);
    float3 V = normalize(I); // outgoing
    float3 L = normalize(omega_in); // incoming
    float3 H = normalize(L + V);

    float3 T = normalize(sc->T);
    float3 X, Y;
	if (params->m_anisotropic > 0.0f) {
		make_orthonormals_tangent(N, T, &X, &Y);
	}
	else {
		make_orthonormals(N, &X, &Y);
	}

    float3 value = calculate_disney_specular_brdf(sc, params, N, X, Y, V, L, H, pdf, params->m_withNdotL_b);

    return value;*/

    float alpha_x = params->m_ax;
	float alpha_y = params->m_ay;
	float3 N = sc->N;

	if (fmaxf(alpha_x, alpha_y) <= 1e-4f)
		return make_float3(0.0f, 0.0f, 0.0f);

	float cosNO = dot(N, I);
	float cosNI = dot(N, omega_in);

	if (cosNI > 0 && cosNO > 0) {
		/* get half vector */
		float3 m = normalize(omega_in + I);
		float alpha2 = alpha_x * alpha_y;
		float D, G1o, G1i;

		if (alpha_x == alpha_y) {
			/* isotropic
			 * eq. 20: (F*G*D)/(4*in*on)
			 * eq. 33: first we calculate D(m) */
            //if (params->m_brightness > 0.5f) {
                float cosThetaM = dot(N, m);
                float cosThetaM2 = cosThetaM * cosThetaM;
                float cosThetaM4 = cosThetaM2 * cosThetaM2;
                float tanThetaM2 = (1 - cosThetaM2) / cosThetaM2;
                D = alpha2 / (M_PI_F * cosThetaM4 * (alpha2 + tanThetaM2) * (alpha2 + tanThetaM2));
            /*}
            else {
                D = spec_GTR2(dot(N, m), params->m_ax);
            }*/

			/* eq. 34: now calculate G1(i,m) and G1(o,m) */
            //if (params->m_exposure > 0.5f) {
                G1o = 2 / (1 + safe_sqrtf(1 + alpha2 * (1 - cosNO * cosNO) / (cosNO * cosNO)));
                G1i = 2 / (1 + safe_sqrtf(1 + alpha2 * (1 - cosNI * cosNI) / (cosNI * cosNI)));
            /*}
            else {
                G1o = spec_smithG_GGX(cosNO, params->m_roughg);
                G1i = spec_smithG_GGX(cosNI, params->m_roughg);
            }*/
		}
		else {
			/* anisotropic */
            float3 X, Y, Z = N;
            make_orthonormals_tangent(Z, sc->T, &X, &Y);

            // distribution
            //if (params->m_brightness > 0.5f) {
                float3 local_m = make_float3(dot(X, m), dot(Y, m), dot(Z, m));
                float slope_x = -local_m.x/(local_m.z*alpha_x);
                float slope_y = -local_m.y/(local_m.z*alpha_y);
                float slope_len = 1 + slope_x*slope_x + slope_y*slope_y;

                float cosThetaM = local_m.z;
                float cosThetaM2 = cosThetaM * cosThetaM;
                float cosThetaM4 = cosThetaM2 * cosThetaM2;

                D = 1 / ((slope_len * slope_len) * M_PI_F * alpha2 * cosThetaM4);
            /*}
            else {
                D = spec_GTR2_aniso(dot(N, m), dot(X, m), dot(Y, m), params->m_ax, params->m_ay);
            }*/

			/* G1(i,m) and G1(o,m) */
            //if (params->m_exposure > 0.5f) {
                float tanThetaO2 = (1 - cosNO * cosNO) / (cosNO * cosNO);
                float cosPhiO = dot(I, X);
                float sinPhiO = dot(I, Y);

                float alphaO2 = (cosPhiO*cosPhiO)*(alpha_x*alpha_x) + (sinPhiO*sinPhiO)*(alpha_y*alpha_y);
                alphaO2 /= cosPhiO*cosPhiO + sinPhiO*sinPhiO;

                G1o = 2 / (1 + safe_sqrtf(1 + alphaO2 * tanThetaO2));

                float tanThetaI2 = (1 - cosNI * cosNI) / (cosNI * cosNI);
                float cosPhiI = dot(omega_in, X);
                float sinPhiI = dot(omega_in, Y);

                float alphaI2 = (cosPhiI*cosPhiI)*(alpha_x*alpha_x) + (sinPhiI*sinPhiI)*(alpha_y*alpha_y);
                alphaI2 /= cosPhiI*cosPhiI + sinPhiI*sinPhiI;

                G1i = 2 / (1 + safe_sqrtf(1 + alphaI2 * tanThetaI2));
            /*}
            else {
                G1o = spec_smithG_GGX(cosNO, params->m_roughg);
                G1i = spec_smithG_GGX(cosNI, params->m_roughg);
            }*/
		}

		float G = G1o * G1i;

		/* eq. 20 */
		float common = D * 0.25f / cosNO;

        float FH = spec_SchlickFresnel(dot(omega_in, m));
		float3 F = spec_mix(params->m_cspec0, make_float3(1.0f, 1.0f, 1.0f), FH);

		float3 out = F * G * common;

		/* eq. 2 in distribution of visible normals sampling
		 * pm = Dw = G1o * dot(m, I) * D / dot(N, I); */

		/* eq. 38 - but see also:
		 * eq. 17 in http://www.graphics.cornell.edu/~bjw/wardnotes.pdf
		 * pdf = pm * 0.25 / dot(m, I); */
		*pdf = G1o * common;

		return out;
	}

	return make_float3(0.0f, 0.0f, 0.0f);
}

ccl_device float3 bsdf_disney_specular_eval_transmit(const ShaderClosure *sc, const float3 I, const float3 omega_in, float *pdf)
{
    return make_float3(0.0f, 0.0f, 0.0f);
}

ccl_device void importance_sample_ggx_aniso(float3 N, float u, float v, float3 *omega_in,
	float ax, float ay, float3 TangentX, float3 TangentY, float *pdf)
{
	float a = sqrt(v) / sqrt(1 - v);

	float Phi = 2.0f * M_PI_F * u;

	float3 H;
	H.x = ax * a * cos(Phi);
	H.y = ay * a * sin(Phi);
	H.z = 1.0f;

	*omega_in = normalize(TangentX * H.x + TangentY * H.y + N * H.z);

	float CosTheta = sqrt(1.0f - (ax * a) * (ax * a) - (ay * a) * (ay * a));
	*pdf = CosTheta * M_1_PI_F;
}

ccl_device void importance_sample_ggx(float3 N, float u, float v, float3 *omega_in,
	float Roughness, float3 TangentX, float3 TangentY, float *pdf)
{
	float a = Roughness * Roughness;

	float Phi = 2.0f * M_PI_F * u;
	float CosTheta = sqrt((1.0f - v) / (1.0f + (a*a - 1.0f) * v));
	float SinTheta = sqrt(1.0f - CosTheta * CosTheta);

	float3 H;
	H.x = SinTheta * cos(Phi);
	H.y = SinTheta * sin(Phi);
	H.z = CosTheta;

	// Tangent to world space
	*omega_in = normalize(TangentX * H.x + TangentY * H.y + N * H.z);
	*pdf = CosTheta * M_1_PI_F;
}

ccl_device int bsdf_disney_specular_sample(const ShaderClosure *sc, const DisneySpecularBRDFParams *params,
    float3 Ng, float3 I, float3 dIdx, float3 dIdy, float randu, float randv,
    float3 *eval, float3 *omega_in, float3 *domega_in_dx,
    float3 *domega_in_dy, float *pdf)
{
    /*float3 N = normalize(sc->N);
	float3 T = normalize(sc->T);
	float3 X, Y;
	if (params->m_anisotropic > 0.0f)
		make_orthonormals_tangent(N, T, &X, &Y);
	else
		make_orthonormals(N, &X, &Y);

	float3 R = -I - 2.0f * dot(-I, N) * N;

	float3 H;
	if (params->m_brightness <= 0.5f) {
		if (params->m_anisotropic > 0.0f)
			importance_sample_ggx_aniso(R, randu, randv, omega_in, params->m_ax, params->m_ay, X, Y, pdf);
		else
			importance_sample_ggx(R, randu, randv, omega_in, params->m_roughness, X, Y, pdf);
		H = normalize(I + *omega_in);
	}
	else {
		if (params->m_anisotropic > 0.0f)
			importance_sample_ggx_aniso(N, randu, randv, &H, params->m_ax, params->m_ay, X, Y, pdf);
		else
			importance_sample_ggx(N, randu, randv, &H, params->m_roughness, X, Y, pdf);
		*omega_in = -I - 2.0f * dot(-I, H) * H;
	}

	if (params->m_brightness > 0.2f && params->m_brightness < 0.8f) {
		if (dot(Ng, *omega_in) < 0.0f) {
			*omega_in = -(*omega_in) - 2.0f * dot(-(*omega_in), R) * R;
			H = normalize(*omega_in + I);
		}
	}

    if (dot(Ng, *omega_in) > 0.0f) {
        float3 V = I; // outgoing
        float3 L = *omega_in; // incoming

		*eval = calculate_disney_specular_brdf(sc, params, N, X, Y, V, L, H, pdf, params->m_withNdotL > 0.75f);

#ifdef __RAY_DIFFERENTIALS__
        *domega_in_dx = 2 * dot(H, dIdx) * H - dIdx;
        *domega_in_dy = 2 * dot(H, dIdy) * H - dIdy;
#endif
    }
    else {
        *pdf = 0.0f;
        *eval = make_float3(0.0f, 0.0f, 0.0f);
    }

    return LABEL_REFLECT|LABEL_GLOSSY;*/

    float alpha_x = params->m_ax;
	float alpha_y = params->m_ay;
	float3 N = sc->N;

	float cosNO = dot(N, I);
	if(cosNO > 0) {
		float3 X, Y, Z = N;

		if(alpha_x == alpha_y)
			make_orthonormals(Z, &X, &Y);
		else
			make_orthonormals_tangent(Z, sc->T, &X, &Y);

		/* importance sampling with distribution of visible normals. vectors are
		 * transformed to local space before and after */
		float3 local_I = make_float3(dot(X, I), dot(Y, I), cosNO);
		float3 local_m;
        float3 m;
		float G1o;

        //if (params->m_exposure > 0.5f) {
		    local_m = spec_microfacet_sample_stretched(local_I, alpha_x, alpha_y,
			        randu, randv, false, &G1o);

		    m = X*local_m.x + Y*local_m.y + Z*local_m.z;
        /*}
        else {
            importance_sample_ggx_aniso(N, randu, randv, &m, alpha_x, alpha_y, X, Y, pdf);
            local_m = make_float3(dot(X, m), dot(Y, m), dot(Z, m));
        }*/
		float cosThetaM = local_m.z;

		/* reflection or refraction? */
        float cosMO = dot(m, I);

        if(cosMO > 0) {
            /* eq. 39 - compute actual reflected direction */
            *omega_in = 2 * cosMO * m - I;

            if(dot(Ng, *omega_in) > 0) {
                if(fmaxf(alpha_x, alpha_y) <= 1e-4f) {
                    /* some high number for MIS */
                    *pdf = 1e6f;
                    *eval = make_float3(1e6f, 1e6f, 1e6f);
                }
                else {
                    /* microfacet normal is visible to this ray */
                    /* eq. 33 */
                    float alpha2 = alpha_x * alpha_y;
                    float D, G1i;

                    if(alpha_x == alpha_y) {
                        /* isotropic */
                        //if (params->m_brightness > 0.5f) {
                            float cosThetaM2 = cosThetaM * cosThetaM;
                            float cosThetaM4 = cosThetaM2 * cosThetaM2;
                            float tanThetaM2 = 1/(cosThetaM2) - 1;
                            D = alpha2 / (M_PI_F * cosThetaM4 * (alpha2 + tanThetaM2) * (alpha2 + tanThetaM2));
                        /*}
                        else {
                            D = spec_GTR2(dot(N, m), params->m_ax);
                        }*/

                        /* eval BRDF*cosNI */
                        float cosNI = dot(N, *omega_in);

                        /* eq. 34: now calculate G1(i,m) */
                        //if (params->m_exposure > 0.5f) {
                            G1i = 2 / (1 + safe_sqrtf(1 + alpha2 * (1 - cosNI * cosNI) / (cosNI * cosNI)));
                        /*}
                        else {
                            G1i = spec_smithG_GGX(cosNI, params->m_roughg);
                            G1o = spec_smithG_GGX(cosNO, params->m_roughg);
                        }*/
                    }
                    else {
                        /* anisotropic distribution */
                        //if (params->m_brightness > 0.5f) {
                            //float3 local_m = make_float3(dot(X, m), dot(Y, m), dot(Z, m));
                            float slope_x = -local_m.x/(local_m.z*alpha_x);
                            float slope_y = -local_m.y/(local_m.z*alpha_y);
                            float slope_len = 1 + slope_x*slope_x + slope_y*slope_y;

                            float cosThetaM = local_m.z;
                            float cosThetaM2 = cosThetaM * cosThetaM;
                            float cosThetaM4 = cosThetaM2 * cosThetaM2;

                            D = 1 / ((slope_len * slope_len) * M_PI_F * alpha2 * cosThetaM4);
                        /*}
                        else {
                            D = spec_GTR2_aniso(dot(N, m), dot(X, m), dot(Y, m), params->m_ax, params->m_ay);
                        }*/

                        /* calculate G1(i,m) */
                        float cosNI = dot(N, *omega_in);

                        //if (params->m_exposure > 0.5f) {
                            float tanThetaI2 = (1 - cosNI * cosNI) / (cosNI * cosNI);
                            float cosPhiI = dot(*omega_in, X);
                            float sinPhiI = dot(*omega_in, Y);

                            float alphaI2 = (cosPhiI*cosPhiI)*(alpha_x*alpha_x) + (sinPhiI*sinPhiI)*(alpha_y*alpha_y);
                            alphaI2 /= cosPhiI*cosPhiI + sinPhiI*sinPhiI;

                            G1i = 2 / (1 + safe_sqrtf(1 + alphaI2 * tanThetaI2));
                        /*}
                        else {
                            G1i = spec_smithG_GGX(cosNI, params->m_roughg);
                            G1o = spec_smithG_GGX(cosNO, params->m_roughg);
                        }*/
                    }

                    /* see eval function for derivation */
                    float common = (G1o * D) * 0.25f / cosNO;
                    *pdf = common;

                    float FH = spec_SchlickFresnel(dot(*omega_in, m));
					float3 F = spec_mix(params->m_cspec0, make_float3(1.0f, 1.0f, 1.0f), FH);

                    *eval = G1i * common * F;
                }

#ifdef __RAY_DIFFERENTIALS__
                *domega_in_dx = (2 * dot(m, dIdx)) * m - dIdx;
                *domega_in_dy = (2 * dot(m, dIdy)) * m - dIdy;
#endif
            }
        }
    }

	return LABEL_REFLECT|LABEL_GLOSSY;
}

CCL_NAMESPACE_END

#endif /* __BSDF_DISNEY_SPECULAR_H__ */

