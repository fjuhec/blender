/*
 * Copyright (c) 2016, Blender Foundation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \name Simple Vector Math Lib
 * \{ */

static inline double sq(const double d)
{
	return d * d;
}

static inline double min(const double a, const double b)
{
	return b < a ? b : a;
}

static inline double max(const double a, const double b)
{
	return a < b ? b : a;
}

static inline void zero_vn(
		double *v0, const uint dims)
{
	for (uint j = 0; j < dims; j++) {
		v0[j] = 0.0;
	}
}

static inline void copy_vnvn(
        double v0[], const double v1[], const uint dims)
{
	for (uint j = 0; j < dims; j++) {
		v0[j] = v1[j];
	}
}

static inline double dot_vnvn(
        const double v0[], const double v1[], const uint dims)
{
	double d = 0.0;
	for (uint j = 0; j < dims; j++) {
		d += v0[j] * v1[j];
	}
	return d;
}

static inline void add_vn_vnvn(
        double v_out[], const double v0[], const double v1[], const uint dims)
{
	for (uint j = 0; j < dims; j++) {
		v_out[j] = v0[j] + v1[j];
	}
}

static inline void sub_vn_vnvn(
        double v_out[], const double v0[], const double v1[], const uint dims)
{
	for (uint j = 0; j < dims; j++) {
		v_out[j] = v0[j] - v1[j];
	}
}

static inline void iadd_vnvn(
        double v0[], const double v1[], const uint dims)
{
	for (uint j = 0; j < dims; j++) {
		v0[j] += v1[j];
	}
}

static inline void isub_vnvn(
        double v0[], const double v1[], const uint dims)
{
	for (uint j = 0; j < dims; j++) {
		v0[j] -= v1[j];
	}
}

static inline void madd_vn_vnvn_fl(
        double v_out[],
        const double v0[], const double v1[],
        const double f, const uint dims)
{
	for (uint j = 0; j < dims; j++) {
		v_out[j] = v0[j] + v1[j] * f;
	}
}

static inline void msub_vn_vnvn_fl(
        double v_out[],
        const double v0[], const double v1[],
        const double f, const uint dims)
{
	for (uint j = 0; j < dims; j++) {
		v_out[j] = v0[j] - v1[j] * f;
	}
}

static inline void miadd_vn_vn_fl(
        double v_out[], const double v0[], double f, const uint dims)
{
	for (uint j = 0; j < dims; j++) {
		v_out[j] += v0[j] * f;
	}
}

#if 0
static inline void misub_vn_vn_fl(
        double v_out[], const double v0[], double f, const uint dims)
{
	for (uint j = 0; j < dims; j++) {
		v_out[j] -= v0[j] * f;
	}
}
#endif

static inline void mul_vnvn_fl(
        double v_out[],
        const double v0[], const double f, const uint dims)
{
	for (uint j = 0; j < dims; j++) {
		v_out[j] = v0[j] * f;
	}
}

static inline void imul_vn_fl(double v0[], const double f, const uint dims)
{
	for (uint j = 0; j < dims; j++) {
		v0[j] *= f;
	}
}


static inline double len_squared_vnvn(
		const double v0[], const double v1[], const uint dims)
{
	double d = 0.0;
	for (uint j = 0; j < dims; j++) {
		d += sq(v0[j] - v1[j]);
	}
	return d;
}

static inline double len_squared_vn(
        const double v0[], const uint dims)
{
	double d = 0.0;
	for (uint j = 0; j < dims; j++) {
		d += sq(v0[j]);
	}
	return d;
}

static inline double len_vnvn(
        const double v0[], const double v1[], const uint dims)
{
	return sqrt(len_squared_vnvn(v0, v1, dims));
}

#if 0
static double len_vn(
		const double v0[], const uint dims)
{
	return sqrt(len_squared_vn(v0, dims));
}

static inline double normalize_vn(
        double v0[], const uint dims)
{
	double d = len_squared_vn(v0, dims);
	if (d != 0.0 && ((d = sqrt(d)) != 0.0)) {
		imul_vn_fl(v0, 1.0 / d, dims);
	}
	return d;
}
#endif

/* v_out = (v0 - v1).normalized() */
static inline double normalize_vn_vnvn(
        double v_out[],
        const double v0[], const double v1[], const uint dims)
{
	double d = 0.0;
	for (uint j = 0; j < dims; j++) {
		double a = v0[j] - v1[j];
		d += sq(a);
		v_out[j] = a;
	}
	if (d != 0.0 && ((d = sqrt(d)) != 0.0)) {
		imul_vn_fl(v_out, 1.0 / d, dims);
	}
	return d;
}

static inline bool is_almost_zero_ex(double val, double eps)
{
	return (-eps < val) && (val < eps);
}

static inline bool is_almost_zero(double val)
{
	return is_almost_zero_ex(val, 1e-8);
}

static inline bool equals_vnvn(
		const double v0[], const double v1[], const uint dims)
{
	for (uint j = 0; j < dims; j++) {
		if (v0[j] != v1[j]) {
			return false;
		}
	}
	return true;
}

/** \} */
