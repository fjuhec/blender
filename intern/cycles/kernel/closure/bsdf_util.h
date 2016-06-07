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

#ifndef __BSDF_UTIL_H__
#define __BSDF_UTIL_H__

CCL_NAMESPACE_BEGIN

ccl_device float fresnel_dielectric(
        float eta, const float3 N,
        const float3 I, float3 *R, float3 *T,
#ifdef __RAY_DIFFERENTIALS__
        const float3 dIdx, const float3 dIdy,
        float3 *dRdx, float3 *dRdy,
        float3 *dTdx, float3 *dTdy,
#endif
        bool *is_inside)
{
	float cos = dot(N, I), neta;
	float3 Nn;

	// check which side of the surface we are on
	if(cos > 0) {
		// we are on the outside of the surface, going in
		neta = 1 / eta;
		Nn   = N;
		*is_inside = false;
	}
	else {
		// we are inside the surface
		cos  = -cos;
		neta = eta;
		Nn   = -N;
		*is_inside = true;
	}
	
	// compute reflection
	*R = (2 * cos)* Nn - I;
#ifdef __RAY_DIFFERENTIALS__
	*dRdx = (2 * dot(Nn, dIdx)) * Nn - dIdx;
	*dRdy = (2 * dot(Nn, dIdy)) * Nn - dIdy;
#endif
	
	float arg = 1 -(neta * neta *(1 -(cos * cos)));
	if(arg < 0) {
		*T = make_float3(0.0f, 0.0f, 0.0f);
#ifdef __RAY_DIFFERENTIALS__
		*dTdx = make_float3(0.0f, 0.0f, 0.0f);
		*dTdy = make_float3(0.0f, 0.0f, 0.0f);
#endif
		return 1; // total internal reflection
	}
	else {
		float dnp = sqrtf(arg);
		float nK = (neta * cos)- dnp;
		*T = -(neta * I)+(nK * Nn);
#ifdef __RAY_DIFFERENTIALS__
		*dTdx = -(neta * dIdx) + ((neta - neta * neta * cos / dnp) * dot(dIdx, Nn)) * Nn;
		*dTdy = -(neta * dIdy) + ((neta - neta * neta * cos / dnp) * dot(dIdy, Nn)) * Nn;
#endif
		// compute Fresnel terms
		float cosTheta1 = cos; // N.R
		float cosTheta2 = -dot(Nn, *T);
		float pPara = (cosTheta1 - eta * cosTheta2)/(cosTheta1 + eta * cosTheta2);
		float pPerp = (eta * cosTheta1 - cosTheta2)/(eta * cosTheta1 + cosTheta2);
		return 0.5f * (pPara * pPara + pPerp * pPerp);
	}
}

ccl_device float fresnel_dielectric_cos(float cosi, float eta)
{
	// compute fresnel reflectance without explicitly computing
	// the refracted direction
	float c = fabsf(cosi);
	float g = eta * eta - 1 + c * c;
	if(g > 0) {
		g = sqrtf(g);
		float A = (g - c)/(g + c);
		float B = (c *(g + c)- 1)/(c *(g - c)+ 1);
		return 0.5f * A * A *(1 + B * B);
	}
	return 1.0f; // TIR(no refracted component)
}

#if 0
ccl_device float3 fresnel_conductor(float cosi, const float3 eta, const float3 k)
{
	float3 cosi2 = make_float3(cosi*cosi);
	float3 one = make_float3(1.0f, 1.0f, 1.0f);
	float3 tmp_f = eta * eta + k * k;
	float3 tmp = tmp_f * cosi2;
	float3 Rparl2 = (tmp - (2.0f * eta * cosi) + one) /
					(tmp + (2.0f * eta * cosi) + one);
	float3 Rperp2 = (tmp_f - (2.0f * eta * cosi) + cosi2) /
					(tmp_f + (2.0f * eta * cosi) + cosi2);
	return(Rparl2 + Rperp2) * 0.5f;
}
#endif

ccl_device float schlick_fresnel(float u)
{
	float m = clamp(1.0f - u, 0.0f, 1.0f);
	float m2 = m * m;
	return m2 * m2 * m; // pow(m, 5)
}

ccl_device float sqr(float a)
{
	return a * a;
}

ccl_device float smooth_step(float edge0, float edge1, float x)
{
	float result;
	if(x < edge0) result = 0.0f;
	else if(x >= edge1) result = 1.0f;
	else {
		float t = (x - edge0)/(edge1 - edge0);
		result = (3.0f-2.0f*t)*(t*t);
	}
	return result;
}

ccl_device void importance_sample_ggx_slopes(
	const float cos_theta_i, const float sin_theta_i,
	float randu, float randv, float *slope_x, float *slope_y,
	float *G1i)
{
	/* special case (normal incidence) */
	if (cos_theta_i >= 0.99999f) {
		const float r = sqrtf(randu / (1.0f - randu));
		const float phi = M_2PI_F * randv;
		*slope_x = r * cosf(phi);
		*slope_y = r * sinf(phi);
		*G1i = 1.0f;

		return;
	}

	/* precomputations */
	const float tan_theta_i = sin_theta_i / cos_theta_i;
	const float G1_inv = 0.5f * (1.0f + safe_sqrtf(1.0f + tan_theta_i*tan_theta_i));

	*G1i = 1.0f / G1_inv;

	/* sample slope_x */
	const float A = 2.0f*randu*G1_inv - 1.0f;
	const float AA = A*A;
	const float tmp = 1.0f / (AA - 1.0f);
	const float B = tan_theta_i;
	const float BB = B*B;
	const float D = safe_sqrtf(BB*(tmp*tmp) - (AA - BB)*tmp);
	const float slope_x_1 = B*tmp - D;
	const float slope_x_2 = B*tmp + D;
	*slope_x = (A < 0.0f || slope_x_2*tan_theta_i > 1.0f) ? slope_x_1 : slope_x_2;

	/* sample slope_y */
	float S;

	if (randv > 0.5f) {
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

ccl_device float3 importance_sample_microfacet_stretched(
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

	if (omega_i_.z < 0.99999f) {
		costheta_ = omega_i_.z;
		sintheta_ = safe_sqrtf(1.0f - costheta_*costheta_);

		float invlen = 1.0f / sintheta_;
		cosphi_ = omega_i_.x * invlen;
		sinphi_ = omega_i_.y * invlen;
	}

	/* 2. sample P22_{omega_i}(x_slope, y_slope, 1, 1) */
	float slope_x, slope_y;

	importance_sample_ggx_slopes(costheta_, sintheta_,
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

CCL_NAMESPACE_END

#endif /* __BSDF_UTIL_H__ */

