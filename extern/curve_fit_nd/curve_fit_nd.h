/*
 * Copyright (c) 2016, DWANGO Co., Ltd.
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

#ifndef __SPLINE_FIT__
#define __SPLINE_FIT__

/** \file curve_fit_nd.h
 *  \ingroup curve_fit
 */


/* curve_fit_cubic.c */

/**
 * Takes a flat array of points:
 * `points[dims]`
 *
 * And calculates cubic (bezier) splines:
 * `r_cubic_array[r_cubic_array_len][3][dims]`.
 *
 * Where each point has 0 and 2 for the tangents and the middle index 1 for the knot.
 */
int spline_fit_cubic_to_points_db(
        const double *points,
        const unsigned int points_len,
        const unsigned int    dims,
        const double  error,
        const unsigned int   *corners,
        const unsigned int    corners_len,

        double **r_cubic_array, unsigned int *r_cubic_array_len);

int spline_fit_cubic_to_points_fl(
        const float  *points,
        const unsigned int    points_len,
        const unsigned int    dims,
        const float   error,
        const unsigned int   *corners,
        const unsigned int    corners_len,

        float **r_cubic_array, unsigned int *r_cubic_array_len);


/* curve_fit_corners_detect.c */

int spline_fit_corners_detect_db(
        const double      *points,
        const unsigned int points_len,
        const unsigned int dims,
        const double       radius_min,
        const double       radius_max,
        const unsigned int samples_max,
        const double       angle_limit,

        unsigned int **r_corners,
        unsigned int  *r_corners_len);

int spline_fit_corners_detect_fl(
        const float       *points,
        const unsigned int points_len,
        const unsigned int dims,
        const float        radius_min,
        const float        radius_max,
        const unsigned int samples_max,
        const float        angle_limit,

        unsigned int **r_corners,
        unsigned int  *r_corners_len);

#endif  /* __SPLINE_FIT__ */
