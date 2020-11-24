/****************************************************************************
 * 			scene.cc: scene_t controls the rendering of a scene
 *      This is part of the yafray package
 *      Copyright (C) 2006  Mathias Wein
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

#include <core_api/scene.h>
#include <core_api/environment.h>
#include <core_api/logging.h>
#include <core_api/object3d.h>
#include <core_api/material.h>
#include <core_api/light.h>
#include <core_api/integrator.h>
#include <core_api/imagefilm.h>
#include <core_api/sysinfo.h>
#include <yafraycore/triangle.h>
#include <yafraycore/ray_kdtree.h>
#include <iostream>
#include <limits>
#include <sstream>

__BEGIN_YAFRAY

scene_t::scene_t(const renderEnvironment_t *render_environment):  volIntegrator(nullptr), camera(nullptr), imageFilm(nullptr), tree(nullptr), vtree(nullptr), background(nullptr), surfIntegrator(nullptr),	AA_samples(1), AA_passes(1), AA_threshold(0.05), nthreads(1), nthreads_photons(1), mode(1), signals(0), env(render_environment)
{
	state.changes = C_ALL;
	state.stack.push_front(READY);
	state.nextFreeID = std::numeric_limits<int>::max();
	state.curObj = nullptr;

	AA_resampled_floor = 0.f;
	AA_sample_multiplier_factor = 1.f;
	AA_light_sample_multiplier_factor = 1.f;
	AA_indirect_sample_multiplier_factor = 1.f;
	AA_detect_color_noise = false;
	AA_dark_threshold_factor = 0.f;
	AA_variance_edge_size = 10;
	AA_variance_pixels = 0;
	AA_clamp_samples = 0.f;
	AA_clamp_indirect = 0.f;
}

scene_t::scene_t(const scene_t &s)
{
	Y_ERROR << "Scene: You may NOT use the copy constructor!" << yendl;
}

scene_t::~scene_t()
{
	if(tree) delete tree;
	if(vtree) delete vtree;
	for(auto i = meshes.begin(); i != meshes.end(); ++i)
	{
		if(i->second.type == TRIM)
			delete i->second.obj;
		else
			delete i->second.mobj;
	}
}

void scene_t::abort()
{
	sig_mutex.lock();
	signals |= Y_SIG_ABORT;
	sig_mutex.unlock();
}

int scene_t::getSignals() const
{
	int sig;
	sig_mutex.lock();
	sig = signals;
	sig_mutex.unlock();
	return sig;
}

void scene_t::getAAParameters(int &samples, int &passes, int &inc_samples, float &threshold, float &resampled_floor, float &sample_multiplier_factor, float &light_sample_multiplier_factor, float &indirect_sample_multiplier_factor, bool &detect_color_noise, int &dark_detection_type, float &dark_threshold_factor, int &variance_edge_size, int &variance_pixels, float &clamp_samples, float &clamp_indirect) const
{
	samples = AA_samples;
	passes = AA_passes;
	inc_samples = AA_inc_samples;
	threshold = AA_threshold;
	resampled_floor = AA_resampled_floor;
	sample_multiplier_factor = AA_sample_multiplier_factor;
	light_sample_multiplier_factor = AA_light_sample_multiplier_factor;
	indirect_sample_multiplier_factor = AA_indirect_sample_multiplier_factor;
	detect_color_noise = AA_detect_color_noise;
	dark_detection_type = AA_dark_detection_type;
	dark_threshold_factor = AA_dark_threshold_factor;
	variance_edge_size = AA_variance_edge_size;
	variance_pixels = AA_variance_pixels;
	clamp_samples = AA_clamp_samples;
	clamp_indirect = AA_clamp_indirect;
}

bool scene_t::startGeometry()
{
	if(state.stack.front() != READY) return false;
	state.stack.push_front(GEOMETRY);
	return true;
}

bool scene_t::endGeometry()
{
	if(state.stack.front() != GEOMETRY) return false;
	// in case objects share arrays, so they all need to be updated
	// after each object change, uncomment the below block again:
	// don't forget to update the mesh object iterators!
/*	for(auto i=meshes.begin();
		 i!=meshes.end(); ++i)
	{
		objData_t &dat = (*i).second;
		dat.obj->setContext(dat.points.begin(), dat.normals.begin() );
	}*/
	state.stack.pop_front();
	return true;
}

bool scene_t::startCurveMesh(objID_t id, int vertices, int obj_pass_index)
{
	if(state.stack.front() != GEOMETRY) return false;
	int ptype = 0 & 0xFF;

	objData_t &nObj = meshes[id];

	//TODO: switch?
	// Allocate triangles to render the curve
	nObj.obj = new triangleObject_t( 2 * (vertices-1) , true, false);
	nObj.obj->setObjectIndex(obj_pass_index);
	nObj.type = ptype;
	state.stack.push_front(OBJECT);
	state.changes |= C_GEOM;
	state.orco=false;
	state.curObj = &nObj;

	nObj.obj->points.reserve(2*vertices);
	return true;
}

bool scene_t::endCurveMesh(const material_t *mat, float strandStart, float strandEnd, float strandShape)
{
	if(state.stack.front() != OBJECT) return false;

	// TODO: Check if we have at least 2 vertex...
	// TODO: math optimizations

	// extrude vertices and create faces
	std::vector<point3d_t> &points = state.curObj->obj->points;
	float r;	//current radius
	int i;
	point3d_t o,a,b;
	vector3d_t N(0),u(0),v(0);
	int n = points.size();
	// Vertex extruding
	for (i=0;i<n;i++){
		o = points[i];
		if (strandShape < 0)
		{
			r = strandStart + pow((float)i/(n-1) ,1+strandShape) * ( strandEnd - strandStart );
		}
		else
		{
			r = strandStart + (1 - pow(((float)(n-i-1))/(n-1) ,1-strandShape)) * ( strandEnd - strandStart );
		}
		// Last point keep previous tangent plane
		if (i<n-1)
		{
			N = points[i+1]-points[i];
			N.normalize();
			createCS(N,u,v);
		}
		// TODO: thikness?
		a = o - (0.5 * r *v) - 1.5 * r / sqrt(3.f) * u;
		b = o - (0.5 * r *v) + 1.5 * r / sqrt(3.f) * u;

		state.curObj->obj->points.push_back(a);
		state.curObj->obj->points.push_back(b);
	}

	// Face fill
	triangle_t tri;
	int a1,a2,a3,b1,b2,b3;
	float su,sv;
	int iu,iv;
	for (i=0;i<n-1;i++){
		// 1D particles UV mapping
		su = (float)i / (n-1);
		sv = su + 1. / (n-1);
		iu = addUV(su,su);
		iv = addUV(sv,sv);
		a1 = i;
		a2 = 2*i+n;
		a3 = a2 +1;
		b1 = i+1;
		b2 = a2 +2;
		b3 = b2 +1;
		// Close bottom
		if (i == 0)
		{
			tri = triangle_t(a1, a3, a2, state.curObj->obj);
			tri.setMaterial(mat);
			state.curTri = state.curObj->obj->addTriangle(tri);
			state.curObj->obj->uv_offsets.push_back(iu);
			state.curObj->obj->uv_offsets.push_back(iu);
			state.curObj->obj->uv_offsets.push_back(iu);
		}

		// Fill
		tri = triangle_t(a1, b2, b1, state.curObj->obj);
		tri.setMaterial(mat);
		state.curTri = state.curObj->obj->addTriangle(tri);
		// StrandUV
		state.curObj->obj->uv_offsets.push_back(iu);
		state.curObj->obj->uv_offsets.push_back(iv);
		state.curObj->obj->uv_offsets.push_back(iv);

		tri = triangle_t(a1, a2, b2, state.curObj->obj);
		tri.setMaterial(mat);
		state.curTri = state.curObj->obj->addTriangle(tri);
		state.curObj->obj->uv_offsets.push_back(iu);
		state.curObj->obj->uv_offsets.push_back(iu);
		state.curObj->obj->uv_offsets.push_back(iv);

		tri = triangle_t(a2, b3, b2, state.curObj->obj);
		tri.setMaterial(mat);
		state.curTri = state.curObj->obj->addTriangle(tri);
		state.curObj->obj->uv_offsets.push_back(iu);
		state.curObj->obj->uv_offsets.push_back(iv);
		state.curObj->obj->uv_offsets.push_back(iv);

		tri = triangle_t(a2, a3, b3, state.curObj->obj);
		tri.setMaterial(mat);
		state.curTri = state.curObj->obj->addTriangle(tri);
		state.curObj->obj->uv_offsets.push_back(iu);
		state.curObj->obj->uv_offsets.push_back(iu);
		state.curObj->obj->uv_offsets.push_back(iv);

		tri = triangle_t(b3, a3, a1, state.curObj->obj);
		tri.setMaterial(mat);
		state.curTri = state.curObj->obj->addTriangle(tri);
		state.curObj->obj->uv_offsets.push_back(iv);
		state.curObj->obj->uv_offsets.push_back(iu);
		state.curObj->obj->uv_offsets.push_back(iu);

		tri = triangle_t(b3, a1, b1, state.curObj->obj);
		tri.setMaterial(mat);
		state.curTri = state.curObj->obj->addTriangle(tri);
		state.curObj->obj->uv_offsets.push_back(iv);
		state.curObj->obj->uv_offsets.push_back(iu);
		state.curObj->obj->uv_offsets.push_back(iv);

	}
	// Close top
	tri = triangle_t(i, 2*i+n, 2*i+n+1, state.curObj->obj);
	tri.setMaterial(mat);
	state.curTri = state.curObj->obj->addTriangle(tri);
	state.curObj->obj->uv_offsets.push_back(iv);
	state.curObj->obj->uv_offsets.push_back(iv);
	state.curObj->obj->uv_offsets.push_back(iv);

	state.curObj->obj->finish();

	state.stack.pop_front();
	return true;
}

bool scene_t::startTriMesh(objID_t id, int vertices, int triangles, bool hasOrco, bool hasUV, int type, int obj_pass_index)
{
	if(state.stack.front() != GEOMETRY) return false;
	int ptype = type & 0xFF;
	if(ptype != TRIM && type != VTRIM && type != MTRIM) return false;

	objData_t &nObj = meshes[id];
	switch(ptype)
	{
		case TRIM:	nObj.obj = new triangleObject_t(triangles, hasUV, hasOrco);
					nObj.obj->setVisibility( !(type & INVISIBLEM) );
					nObj.obj->useAsBaseObject( (type & BASEMESH) );
					nObj.obj->setObjectIndex(obj_pass_index);
					break;
		case VTRIM:
		case MTRIM:	nObj.mobj = new meshObject_t(triangles, hasUV, hasOrco);
					nObj.mobj->setVisibility( !(type & INVISIBLEM) );
					nObj.obj->setObjectIndex(obj_pass_index);
					break;
		default: return false;
	}
	nObj.type = ptype;
	state.stack.push_front(OBJECT);
	state.changes |= C_GEOM;
	state.orco=hasOrco;
	state.curObj = &nObj;

	return true;
}

bool scene_t::endTriMesh()
{
	if(state.stack.front() != OBJECT) return false;

	if(state.curObj->type == TRIM)
	{
		if(state.curObj->obj->has_uv)
		{
			if( state.curObj->obj->uv_offsets.size() != 3*state.curObj->obj->triangles.size() )
			{
				Y_ERROR << "Scene: UV-offsets mismatch!" << yendl;
				return false;
			}
		}

		//calculate geometric normals of tris
		state.curObj->obj->finish();
	}
	else
	{
		state.curObj->mobj->finish();
	}

	state.stack.pop_front();
	return true;
}

void scene_t::setNumThreads(int threads)
{
	nthreads = threads;

	if(nthreads == -1) //Automatic detection of number of threads supported by this system, taken from Blender. (DT)
	{
		Y_VERBOSE << "Automatic Detection of Threads: Active." << yendl;
		const sysInfo_t sys_info;
		nthreads = sys_info.getNumSystemThreads();
		Y_VERBOSE << "Number of Threads supported: [" << nthreads << "]." << yendl;
	}
	else
	{
		Y_VERBOSE << "Automatic Detection of Threads: Inactive." << yendl;
	}

	Y_PARAMS << "Using [" << nthreads << "] Threads." << yendl;
	
	std::stringstream set;
	set << "CPU threads=" << nthreads << std::endl;
	
	yafLog.appendRenderSettings(set.str());
}

void scene_t::setNumThreadsPhotons(int threads_photons)
{
	nthreads_photons = threads_photons;

	if(nthreads_photons == -1) //Automatic detection of number of threads supported by this system, taken from Blender. (DT)
	{
		Y_VERBOSE << "Automatic Detection of Threads for Photon Mapping: Active." << yendl;
		const sysInfo_t sys_info;
		nthreads_photons = sys_info.getNumSystemThreads();
		Y_VERBOSE << "Number of Threads supported for Photon Mapping: [" << nthreads_photons << "]." << yendl;
	}
	else
	{
		Y_VERBOSE << "Automatic Detection of Threads for Photon Mapping: Inactive." << yendl;
	}

	Y_PARAMS << "Using for Photon Mapping [" << nthreads_photons << "] Threads." << yendl;
}

#define prepareEdges(q, v1, v2) e1 = vertices[v1] - vertices[q]; \
			e2 = vertices[v2] - vertices[q];

bool scene_t::smoothMesh(objID_t id, float angle)
{
	if( state.stack.front() != GEOMETRY ) return false;
	objData_t *odat;
	if(id)
	{
		auto it = meshes.find(id);
		if(it == meshes.end() ) return false;
		odat = &(it->second);
	}
	else
	{
		odat = state.curObj;
		if(!odat) return false;
	}

	if(odat->obj->normals_exported && odat->obj->points.size() == odat->obj->normals.size())
	{
		odat->obj->is_smooth = true;
		return true;
	}

	// cannot smooth other mesh types yet...
	if(odat->type > 0) return false;
	unsigned int idx = 0;
	std::vector<normal_t> &normals = odat->obj->normals;
	std::vector<triangle_t> &triangles = odat->obj->triangles;
	size_t points = odat->obj->points.size();
	std::vector<triangle_t>::iterator tri;
	std::vector<point3d_t> const &vertices = odat->obj->points;

    normals.reserve(points);
    normals.resize(points, normal_t(0,0,0));

	if (angle>=180)
	{
		for (tri=triangles.begin(); tri!=triangles.end(); ++tri)
		{

			vector3d_t n = tri->getNormal();
			vector3d_t e1, e2;
			float alpha = 0;

			prepareEdges(tri->pa, tri->pb, tri->pc)
            alpha = e1.sinFromVectors(e2);

            normals[tri->pa] += n * alpha;

			prepareEdges(tri->pb, tri->pa, tri->pc)
            alpha = e1.sinFromVectors(e2);

            normals[tri->pb] += n * alpha;

			prepareEdges(tri->pc, tri->pa, tri->pb)
            alpha = e1.sinFromVectors(e2);

            normals[tri->pc] += n * alpha;

            tri->setNormals(tri->pa, tri->pb, tri->pc);
  		}

		for (idx=0;idx<normals.size();++idx) normals[idx].normalize();

	}
	else if(angle>0.1)// angle dependant smoothing
	{
		float thresh = fCos(degToRad(angle));
		std::vector<vector3d_t> vnormals;
		std::vector<int> vn_index;
		// create list of faces that include given vertex
		std::vector<std::vector<triangle_t*> > vface(points);
		std::vector<std::vector<float> > alphas(points);
		for (tri=triangles.begin(); tri!=triangles.end(); ++tri)
		{
			vector3d_t e1, e2;

			prepareEdges(tri->pa, tri->pb, tri->pc)
            alphas[tri->pa].push_back(e1.sinFromVectors(e2));
            vface[tri->pa].push_back(&(*tri));

			prepareEdges(tri->pb, tri->pa, tri->pc)
            alphas[tri->pb].push_back(e1.sinFromVectors(e2));
			vface[tri->pb].push_back(&(*tri));

			prepareEdges(tri->pc, tri->pa, tri->pb)
            alphas[tri->pc].push_back(e1.sinFromVectors(e2));
            vface[tri->pc].push_back(&(*tri));
		}
		for(int i=0; i<(int)vface.size(); ++i)
		{
			std::vector<triangle_t*> &tris = vface[i];
			int j = 0;
			for(auto fi=tris.begin(); fi!=tris.end(); ++fi)
			{
				triangle_t* f = *fi;
				bool smooth = false;
				// calculate vertex normal for face
				vector3d_t vnorm, fnorm;

				fnorm = f->getNormal();
				vnorm = fnorm * alphas[i][j];
                int k = 0;
				for(auto f2=tris.begin(); f2!=tris.end(); ++f2)
				{
					if(**fi == **f2)
					{
                        k++;
                        continue;
					}
					vector3d_t f2norm = (*f2)->getNormal();
					if((fnorm * f2norm) > thresh)
					{
						smooth = true;
						vnorm += f2norm * alphas[i][k];
					}
					k++;
				}
				int n_idx = -1;
				if(smooth)
				{
					vnorm.normalize();
					//search for existing normal
					for(unsigned int l=0; l<vnormals.size(); ++l)
					{
						if(vnorm*vnormals[l] > 0.999)
                        {
                            n_idx = vn_index[l];
                            break;
                        }
					}
					// create new if none found
					if(n_idx == -1)
					{
						n_idx = normals.size();
						vnormals.push_back(vnorm);
						vn_index.push_back(n_idx);
						normals.push_back( normal_t(vnorm) );
					}
				}
				// set vertex normal to idx
				if	   (f->pa == i) f->na = n_idx;
				else if(f->pb == i) f->nb = n_idx;
				else if(f->pc == i) f->nc = n_idx;
				else
				{
					Y_ERROR << "Scene: Mesh smoothing error!" << yendl;
					return false;
				}
				j++;
			}
			vnormals.clear();
			vn_index.clear();
		}
	}

    odat->obj->is_smooth = true;

	return true;
}

int scene_t::addVertex(const point3d_t &p)
{
	if(state.stack.front() != OBJECT) return -1;
	state.curObj->obj->points.push_back(p);
	if(state.curObj->type == MTRIM)
	{
		std::vector<point3d_t> &points = state.curObj->mobj->points;
		int n = points.size();
		if(n%3==0)
		{
			//convert point 2 to quadratic bezier control point
			points[n-2] = 2.f*points[n-2] - 0.5f*(points[n-3] + points[n-1]);
		}
		return (n-1)/3;
	}

	state.curObj->lastVertId = state.curObj->obj->points.size()-1;

	return state.curObj->lastVertId;
}

int scene_t::addVertex(const point3d_t &p, const point3d_t &orco)
{
	if(state.stack.front() != OBJECT) return -1;

	switch(state.curObj->type)
	{
		case TRIM:
			state.curObj->obj->points.push_back(p);
			state.curObj->obj->points.push_back(orco);
			state.curObj->lastVertId = (state.curObj->obj->points.size()-1) / 2;
			break;

		case VTRIM:
			state.curObj->mobj->points.push_back(p);
			state.curObj->mobj->points.push_back(orco);
			state.curObj->lastVertId = (state.curObj->mobj->points.size()-1) / 2;
			break;

		case MTRIM:
			return addVertex(p);
	}

	return state.curObj->lastVertId;
}

void scene_t::addNormal(const normal_t& n)
{
	if(mode != 0)
	{
		Y_WARNING << "Normal exporting is only supported for triangle mode" << yendl;
		return;
	}
	if(state.curObj->obj->points.size() > state.curObj->lastVertId && state.curObj->obj->points.size() > state.curObj->obj->normals.size())
	{
		if(state.curObj->obj->normals.size() < state.curObj->obj->points.size())
			state.curObj->obj->normals.resize(state.curObj->obj->points.size());

		state.curObj->obj->normals[state.curObj->lastVertId] = n;
		state.curObj->obj->normals_exported = true;
	}
}

bool scene_t::addTriangle(int a, int b, int c, const material_t *mat)
{
	if(state.stack.front() != OBJECT) return false;
	if(state.curObj->type == MTRIM)
	{
		bsTriangle_t tri(3*a, 3*b, 3*c, state.curObj->mobj);
		tri.setMaterial(mat);
		state.curObj->mobj->addBsTriangle(tri);
	}
	else if(state.curObj->type == VTRIM)
	{
		if(state.orco) a*=2, b*=2, c*=2;
		vTriangle_t tri(a, b, c, state.curObj->mobj);
		tri.setMaterial(mat);
		state.curObj->mobj->addTriangle(tri);
	}
	else
	{
		if(state.orco) a*=2, b*=2, c*=2;
		triangle_t tri(a, b, c, state.curObj->obj);
		tri.setMaterial(mat);
		if(state.curObj->obj->normals_exported)
		{
			if(state.orco)
			{
				// Since the vertex indexes are duplicated with orco
				// we divide by 2: a / 2 == a >> 1 since is an integer division
				tri.na = a >> 1;
				tri.nb = b >> 1;
				tri.nc = c >> 1;
			}
			else
			{
				tri.na = a;
				tri.nb = b;
				tri.nc = c;
			}
		}
		state.curTri = state.curObj->obj->addTriangle(tri);
	}
	return true;
}

bool scene_t::addTriangle(int a, int b, int c, int uv_a, int uv_b, int uv_c, const material_t *mat)
{
	if(!addTriangle(a, b, c, mat)) return false;

	if(state.curObj->type == TRIM)
	{
		state.curObj->obj->uv_offsets.push_back(uv_a);
		state.curObj->obj->uv_offsets.push_back(uv_b);
		state.curObj->obj->uv_offsets.push_back(uv_c);
	}
	else
	{
		state.curObj->mobj->uv_offsets.push_back(uv_a);
		state.curObj->mobj->uv_offsets.push_back(uv_b);
		state.curObj->mobj->uv_offsets.push_back(uv_c);
	}

	return true;
}

int scene_t::addUV(float u, float v)
{
	if(state.stack.front() != OBJECT) return false;
	if(state.curObj->type == TRIM)
	{
		state.curObj->obj->uv_values.push_back(uv_t(u, v));
		return (int)state.curObj->obj->uv_values.size()-1;
	}
	else
	{
		state.curObj->mobj->uv_values.push_back(uv_t(u, v));
		return (int)state.curObj->mobj->uv_values.size()-1;
	}
	return -1;
}

bool scene_t::addLight(light_t *l)
{
	if(l != 0)
	{
		if(!l->lightEnabled()) return false; //if a light is disabled, don't add it to the list of lights
		lights.push_back(l);
		state.changes |= C_LIGHT;
		return true;
	}
	return false;
}

bool scene_t::removeLight(light_t *l)
{
	if(l != 0)
	{
		auto i = std::find(lights.begin(), lights.end(), l);
		if (i == lights.end()) {
			return false;
		}
		else 
		{
			lights.erase(i);
			state.changes |= C_LIGHT;
			return true;
		}
	}
	return false;
}

void scene_t::setCamera(camera_t *cam)
{
	camera = cam;
}

void scene_t::setImageFilm(imageFilm_t *film)
{
	imageFilm = film;
}

void scene_t::setBackground(background_t *bg)
{
	background = bg;
}

void scene_t::setSurfIntegrator(surfaceIntegrator_t *s)
{
	surfIntegrator = s;
	surfIntegrator->setScene(this);
	state.changes |= C_OTHER;
}

void scene_t::setVolIntegrator(volumeIntegrator_t *v)
{
	volIntegrator = v;
	volIntegrator->setScene(this);
	state.changes |= C_OTHER;
}

background_t* scene_t::getBackground() const
{
	return background;
}

triangleObject_t* scene_t::getMesh(objID_t id) const
{
	auto i = meshes.find(id);
	return (i==meshes.end()) ? 0 : i->second.obj;
}

object3d_t* scene_t::getObject(objID_t id) const
{
	auto i = meshes.find(id);
	if(i != meshes.end())
	{
		if(i->second.type == TRIM) return i->second.obj;
		else return i->second.mobj;
	}
	else
	{
		auto oi = objects.find(id);
		if(oi != objects.end() ) return oi->second;
	}
	return nullptr;
}

bound_t scene_t::getSceneBound() const
{
	return sceneBound;
}

void scene_t::setAntialiasing(int numSamples, int numPasses, int incSamples, double threshold, float resampled_floor, float sample_multiplier_factor, float light_sample_multiplier_factor, float indirect_sample_multiplier_factor, bool detect_color_noise, int dark_detection_type, float dark_threshold_factor, int variance_edge_size, int variance_pixels, float clamp_samples, float clamp_indirect)
{
	AA_samples = std::max(1, numSamples);
	AA_passes = numPasses;
	AA_inc_samples = (incSamples > 0) ? incSamples : AA_samples;
	AA_threshold = (float)threshold;
	AA_resampled_floor = resampled_floor;
	AA_sample_multiplier_factor = sample_multiplier_factor;
	AA_light_sample_multiplier_factor = light_sample_multiplier_factor;
	AA_indirect_sample_multiplier_factor = indirect_sample_multiplier_factor;
	AA_detect_color_noise = detect_color_noise;
	AA_dark_detection_type = dark_detection_type;
	AA_dark_threshold_factor = dark_threshold_factor;
	AA_variance_edge_size = variance_edge_size;
	AA_variance_pixels = variance_pixels;
	AA_clamp_samples = clamp_samples;
	AA_clamp_indirect = clamp_indirect;
}

/*! update scene state to prepare for rendering.
	\return false if something vital to render the scene is missing
			true otherwise
*/
bool scene_t::update()
{
	Y_VERBOSE << "Scene: Mode \"" << ((mode == 0) ? "Triangle" : "Universal" ) << "\"" << yendl;
	if(!camera || !imageFilm) return false;
	if(state.changes & C_GEOM)
	{
		if(tree) delete tree;
		if(vtree) delete vtree;
		tree = nullptr, vtree = nullptr;
		int nprims=0;
		if(mode==0)
		{
			for(auto i=meshes.begin(); i!=meshes.end(); ++i)
			{
                objData_t &dat = (*i).second;

                if (!dat.obj->isVisible()) continue;
                if (dat.obj->isBaseObject()) continue;

				if(dat.type == TRIM) nprims += dat.obj->numPrimitives();
			}
			if(nprims > 0)
			{
				const triangle_t **tris = new const triangle_t*[nprims];
				const triangle_t **insert = tris;
				for(auto i=meshes.begin(); i!=meshes.end(); ++i)
				{
					objData_t &dat = (*i).second;

					if (!dat.obj->isVisible()) continue;
					if (dat.obj->isBaseObject()) continue;

					if(dat.type == TRIM) insert += dat.obj->getPrimitives(insert);
				}
				tree = new triKdTree_t(tris, nprims, -1, 1, 0.8, 0.33 /* -1, 1.2, 0.40 */ );
				delete [] tris;
				sceneBound = tree->getBound();
				Y_VERBOSE << "Scene: New scene bound is:" <<
				"(" << sceneBound.a.x << ", " << sceneBound.a.y << ", " << sceneBound.a.z << "), (" <<
				sceneBound.g.x << ", " << sceneBound.g.y << ", " << sceneBound.g.z << ")" << yendl;

				if(shadowBiasAuto) shadowBias = YAF_SHADOW_BIAS;
				if(rayMinDistAuto) rayMinDist = MIN_RAYDIST;

				Y_INFO << "Scene: total scene dimensions: X=" << sceneBound.longX() << ", Y=" << sceneBound.longY() << ", Z=" << sceneBound.longZ() << ", volume=" << sceneBound.vol() << ", Shadow Bias=" << shadowBias << (shadowBiasAuto ? " (auto)":"") << ", Ray Min Dist=" << rayMinDist << (rayMinDistAuto ? " (auto)":"") << yendl;
			}
			else Y_WARNING << "Scene: Scene is empty..." << yendl;
		}
		else
		{
			for(auto i=meshes.begin(); i!=meshes.end(); ++i)
			{
				objData_t &dat = (*i).second;
				if(dat.type != TRIM) nprims += dat.mobj->numPrimitives();
			}
			// include all non-mesh objects; eventually make a common map...
			for(auto i=objects.begin(); i!=objects.end(); ++i)
			{
				nprims += i->second->numPrimitives();
			}
			if(nprims > 0)
			{
				const primitive_t **tris = new const primitive_t*[nprims];
				const primitive_t **insert = tris;
				for(auto i=meshes.begin(); i!=meshes.end(); ++i)
				{
					objData_t &dat = (*i).second;
					if(dat.type != TRIM) insert += dat.mobj->getPrimitives(insert);
				}
				for(auto i=objects.begin(); i!=objects.end(); ++i)
				{
					insert += i->second->getPrimitives(insert);
				}
				vtree = new kdTree_t<primitive_t>(tris, nprims, -1, 1, 0.8, 0.33 /* -1, 1.2, 0.40 */ );
				delete [] tris;
				sceneBound = vtree->getBound();
				Y_VERBOSE << "Scene: New scene bound is:" << yendl <<
				"(" << sceneBound.a.x << ", " << sceneBound.a.y << ", " << sceneBound.a.z << "), (" <<
				sceneBound.g.x << ", " << sceneBound.g.y << ", " << sceneBound.g.z << ")" << yendl;

				if(shadowBiasAuto) shadowBias = YAF_SHADOW_BIAS;
				if(rayMinDistAuto) rayMinDist = MIN_RAYDIST;

				Y_INFO << "Scene: total scene dimensions: X=" << sceneBound.longX() << ", Y=" << sceneBound.longY() << ", Z=" << sceneBound.longZ() << ", volume=" << sceneBound.vol() << ", Shadow Bias=" << shadowBias << (shadowBiasAuto ? " (auto)":"") << ", Ray Min Dist=" << rayMinDist << (rayMinDistAuto ? " (auto)":"") << yendl;
				
			}
			else Y_ERROR << "Scene: Scene is empty..." << yendl;
		}
	}

	for(unsigned int i=0; i<lights.size(); ++i) lights[i]->init(*this);

	if(!surfIntegrator)
	{
		Y_ERROR << "Scene: No surface integrator, bailing out..." << yendl;
		return false;
	}

	if(state.changes != C_NONE)
	{
		std::stringstream inteSettings;

		bool success = (surfIntegrator->preprocess() && volIntegrator->preprocess());

		if(!success) return false;
	}

	state.changes = C_NONE;

	return true;
}

bool scene_t::intersect(const ray_t &ray, surfacePoint_t &sp) const
{
	float dis, Z;
	intersectData_t data;
	if(ray.tmax<0) dis=std::numeric_limits<float>::infinity();
	else dis=ray.tmax;
	// intersect with tree:
	if(mode == 0)
	{
		if(!tree) return false;
		triangle_t *hitt = nullptr;
		if( ! tree->Intersect(ray, dis, &hitt, Z, data) ){ return false; }
		point3d_t h=ray.from + Z*ray.dir;
		hitt->getSurface(sp, h, data);
		sp.origin = hitt;
		sp.data = data;
		sp.ray = nullptr;
	}
	else
	{
		if(!vtree) return false;
		primitive_t *hitprim = nullptr;
		if( ! vtree->Intersect(ray, dis, &hitprim, Z, data) ){ return false; }
		point3d_t h=ray.from + Z*ray.dir;
		hitprim->getSurface(sp, h, data);
		sp.origin = hitprim;
		sp.data = data;
		sp.ray = nullptr;
	}
	ray.tmax = Z;
	return true;
}

bool scene_t::intersect(const diffRay_t &ray, surfacePoint_t &sp) const
{
	float dis, Z;
	intersectData_t data;
	if(ray.tmax<0) dis=std::numeric_limits<float>::infinity();
	else dis=ray.tmax;
	// intersect with tree:
	if(mode == 0)
	{
		if(!tree) return false;
		triangle_t *hitt = nullptr;
		if( ! tree->Intersect(ray, dis, &hitt, Z, data) ){ return false; }
		point3d_t h=ray.from + Z*ray.dir;
		hitt->getSurface(sp, h, data);
		sp.origin = hitt;
		sp.data = data;
		sp.ray = &ray;
	}
	else
	{
		if(!vtree) return false;
		primitive_t *hitprim = nullptr;
		if( ! vtree->Intersect(ray, dis, &hitprim, Z, data) ){ return false; }
		point3d_t h=ray.from + Z*ray.dir;
		hitprim->getSurface(sp, h, data);
		sp.origin = hitprim;
		sp.data = data;
		sp.ray = &ray;
	}
	ray.tmax = Z;
	return true;
}

bool scene_t::isShadowed(renderState_t &state, const ray_t &ray, float &obj_index, float &mat_index) const
{

	ray_t sray(ray);
	sray.from += sray.dir * sray.tmin;
	sray.time = state.time;
	float dis;
	if(ray.tmax<0)	dis=std::numeric_limits<float>::infinity();
	else  dis = sray.tmax - 2*sray.tmin;
	if(mode==0)
	{
		triangle_t *hitt = nullptr;
		if(!tree) return false;
		bool shadowed = tree->IntersectS(sray, dis, &hitt, shadowBias);
		if(hitt)
		{
			if(hitt->getMesh()) obj_index = hitt->getMesh()->getAbsObjectIndex();	//Object index of the object casting the shadow
			if(hitt->getMaterial()) mat_index = hitt->getMaterial()->getAbsMaterialIndex();	//Material index of the object casting the shadow
		}
		return shadowed;
	}
	else
	{
		primitive_t *hitt = nullptr;
		if(!vtree) return false;
		bool shadowed = vtree->IntersectS(sray, dis, &hitt, shadowBias);
		if(hitt)
		{
			if(hitt->getMaterial()) mat_index = hitt->getMaterial()->getAbsMaterialIndex();	//Material index of the object casting the shadow
		}
		return shadowed;
	}
}

bool scene_t::isShadowed(renderState_t &state, const ray_t &ray, int maxDepth, color_t &filt, float &obj_index, float &mat_index) const
{
	ray_t sray(ray);
	sray.from += sray.dir * sray.tmin;
	float dis;
	if(ray.tmax<0)	dis=std::numeric_limits<float>::infinity();
	else  dis = sray.tmax - 2*sray.tmin;
	filt = color_t(1.0);
	void *odat = state.userdata;
	unsigned char userdata[USER_DATA_SIZE+7];
	state.userdata = (void *)( ((size_t)&userdata[7])&(~7 ) ); // pad userdata to 8 bytes
	bool isect=false;
	if(mode==0)
	{
		triangle_t *hitt = nullptr;
		if(tree) 
		{
			isect = tree->IntersectTS(state, sray, maxDepth, dis, &hitt, filt, shadowBias);
			if(hitt)
			{
				if(hitt->getMesh()) obj_index = hitt->getMesh()->getAbsObjectIndex();	//Object index of the object casting the shadow
				if(hitt->getMaterial()) mat_index = hitt->getMaterial()->getAbsMaterialIndex();	//Material index of the object casting the shadow
			}
		}
	}
	else
	{
		primitive_t *hitt = nullptr;
		if(vtree)
		{
			isect = vtree->IntersectTS(state, sray, maxDepth, dis, &hitt, filt, shadowBias);
			if(hitt)
			{
				if(hitt->getMaterial()) mat_index = hitt->getMaterial()->getAbsMaterialIndex();	//Material index of the object casting the shadow
			}
		}
	}
	state.userdata = odat;
	return isect;
}

bool scene_t::render()
{
	sig_mutex.lock();
	signals = 0;
	sig_mutex.unlock();

	bool success = false;
	
	const std::map<std::string,camera_t *> *camera_table = env->getCameraTable();

	if(camera_table->size() == 0)
	{
		Y_ERROR << "No cameras/views found, exiting." << yendl;
		return false;
	}

	for(auto cam_table_entry = camera_table->begin(); cam_table_entry != camera_table->end(); ++cam_table_entry)
    {
		int numView = distance(camera_table->begin(), cam_table_entry);
		camera_t* cam = cam_table_entry->second;
		setCamera(cam);
		if(!update()) return false;

		success = surfIntegrator->render(numView, imageFilm);

		surfIntegrator->cleanup();
		imageFilm->flush(numView);
    }
    	
	return success;
}

//! does not do anything yet...maybe never will
bool scene_t::addMaterial(material_t *m, const char* name) { return false; }

objID_t scene_t::getNextFreeID()
{
	objID_t id;
	id = state.nextFreeID;

	//create new entry for object, assert that no ID collision happens:
	if(meshes.find(id) != meshes.end())
	{
		Y_ERROR << "Scene: Object ID already in use!" << yendl;
		--state.nextFreeID;
		return getNextFreeID();
	}

	--state.nextFreeID;

	return id;
}

bool scene_t::addObject(object3d_t *obj, objID_t &id)
{
	id = getNextFreeID();
	if( id > 0 )
	{
		//create new triangle object:
		objects[id] = obj;
		return true;
	}
	else
	{
		return false;
	}
}

bool scene_t::addInstance(objID_t baseObjectId, const matrix4x4_t &objToWorld)
{
	if(mode != 0) return false;

	if (meshes.find(baseObjectId) == meshes.end())
	{
		Y_ERROR << "Base mesh for instance doesn't exist " << baseObjectId << yendl;
		return false;
	}

	int id = getNextFreeID();

	if (id > 0)
	{
		objData_t &od = meshes[id];
		objData_t &base = meshes[baseObjectId];

		od.obj = new triangleObjectInstance_t(base.obj, objToWorld);

		return true;
	}
	else
	{
		return false;
	}
}

const renderPasses_t* scene_t::getRenderPasses() const { return env->getRenderPasses(); }
bool scene_t::pass_enabled(intPassTypes_t intPassType) const { return env->getRenderPasses()->pass_enabled(intPassType); }


__END_YAFRAY
