#pragma once
/****************************************************************************
 *      This is part of the libYafaRay package
 *
 *      This library is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU Lesser General Public
 *      License as published by the Free Software Foundation; either
 *      version 2.1 of the License, or (at your option) any later version.
 *
 *      This library is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *      Lesser General Public License for more details.
 *
 *      You should have received a copy of the GNU Lesser General Public
 *      License along with this library; if not, write to the Free Software
 *      Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef YAFARAY_SPECTRUM_H
#define YAFARAY_SPECTRUM_H

#include <yafray_constants.h>
#include <core_api/color.h>

BEGIN_YAFRAY

YAFRAYCORE_EXPORT void wl2RgbFromCie__(float wl, Rgb &col);
//YAFRAYCORE_EXPORT void approxSpectrumRGB(float wl, Rgb &col);
//YAFRAYCORE_EXPORT void fakeSpectrum(float p, Rgb &col);
YAFRAYCORE_EXPORT void cauchyCoefficients__(float ior, float disp_pw, float &cauchy_a, float &cauchy_b);
YAFRAYCORE_EXPORT float getIoRcolor__(float w, float cauchy_a, float cauchy_b, Rgb &col);
YAFRAYCORE_EXPORT Rgb wl2Xyz__(float wl);

static inline float getIor__(float w, float cauchy_a, float cauchy_b)
{
	float wl = 300.0 * w + 400.0;
	return cauchy_a + cauchy_b / (wl * wl);
}

static inline void wl2Rgb__(float w, Rgb &wl_col)
{
	float wl = 300.0 * w + 400.0;
	wl2RgbFromCie__(wl, wl_col);
	wl_col *= 2.214032659670777114f;
}

END_YAFRAY

#endif // YAFARAY_SPECTRUM_H
