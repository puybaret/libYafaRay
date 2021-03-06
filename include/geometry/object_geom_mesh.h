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

#ifndef YAFARAY_OBJECT_GEOM_MESH_H
#define YAFARAY_OBJECT_GEOM_MESH_H

#include "geometry/object_geom.h"
#include <vector>

BEGIN_YAFARAY

struct Uv;
class VTriangle;
class BsTriangle;

/*!	MeshObject holds various polygonal primitives */
class MeshObject final : public ObjectGeometric
{
	public:
		MeshObject(int ntris, bool has_uv = false, bool has_orco = false);
		/*! the number of primitives the object holds. Primitive is an element
			that by definition can perform ray-triangle intersection */
		int numPrimitives() const { return v_triangles_.size() + bs_triangles_.size(); }
		int getPrimitives(const Primitive **prims) const;

		Primitive *addTriangle(const VTriangle &t);
		Primitive *addBsTriangle(const BsTriangle &t);

		//void setContext(std::vector<point3d_t>::iterator p, std::vector<normal_t>::iterator n);
		void setLight(const Light *l) { light_ = l; }
		void finish();
		const std::vector<VTriangle> &getVTriangles() const { return v_triangles_; }
		const std::vector<BsTriangle> &getBsTriangles() const { return bs_triangles_; }
		const std::vector<Point3> &getPoints() const { return points_; }
		const std::vector<Vec3> &getNormals() const { return normals_; }
		const std::vector<int> &getUvOffsets() const { return uv_offsets_; }
		const std::vector<Uv> &getUvValues() const { return uv_values_; }
		bool hasOrco() const { return has_orco_; }
		bool hasUv() const { return has_uv_; }
		bool isSmooth() const { return is_smooth_; }
		void addPoint(const Point3 &p) { points_.push_back(p); }
		void addUvOffset(int uv_offset) { uv_offsets_.push_back(uv_offset); }
		void addUvValue(const Uv &uv) { uv_values_.push_back(uv); }
		int convertToBezierControlPoints();

	protected:
		std::vector<VTriangle> v_triangles_;
		std::vector<BsTriangle> bs_triangles_;
		std::vector<Point3> points_;
		std::vector<Vec3> normals_;
		std::vector<int> uv_offsets_;
		std::vector<Uv> uv_values_;
		bool has_orco_ = false;
		bool has_uv_ = false;
		bool is_smooth_ = false;
};

END_YAFARAY

#endif //YAFARAY_OBJECT_GEOM_MESH_H

