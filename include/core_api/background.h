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

#ifndef YAFARAY_BACKGROUND_H
#define YAFARAY_BACKGROUND_H

#include <yafray_constants.h>

BEGIN_YAFRAY

struct RenderState;
class Light;
class Rgb;
class Ray;

class YAFRAYCORE_EXPORT Background
{
	public:
		//! get the background color for a given ray
		virtual Rgb operator()(const Ray &ray, RenderState &state, bool from_postprocessed = false) const = 0;
		virtual Rgb eval(const Ray &ray, bool from_postprocessed = false) const = 0;
		/*! get the light source representing background lighting.
			\return the light source that reproduces background lighting, or nullptr if background
					shall only be sampled from BSDFs
		*/
		virtual bool hasIbl() const { return false; }
		virtual bool shootsCaustic() const { return false; }
		virtual ~Background() = default;
};

END_YAFRAY

#endif // YAFARAY_BACKGROUND_H
