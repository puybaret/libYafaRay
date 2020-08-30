/****************************************************************************
 *      spotlight.cc: a spot light with soft edge
 *      This is part of the libYafaRay package
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

#include <core_api/light.h>
#include <core_api/surface.h>
#include <core_api/environment.h>
#include <utilities/sample_utils.h>
#include <core_api/params.h>

BEGIN_YAFRAY

class SpotLight : public Light
{
	public:
		SpotLight(const Point3 &from, const Point3 &to, const Rgb &col, float power, float angle, float falloff, bool s_sha, int smpl, float ssfuzzy, bool b_light_enabled = true, bool b_cast_shadows = true);
		virtual ~SpotLight();
		virtual Rgb totalEnergy() const;
		virtual Rgb emitPhoton(float s_1, float s_2, float s_3, float s_4, Ray &ray, float &ipdf) const;
		virtual Rgb emitSample(Vec3 &wo, LSample &s) const;
		virtual bool diracLight() const { return !soft_shadows_; }
		virtual bool illumSample(const SurfacePoint &sp, LSample &s, Ray &wi) const;
		virtual bool illuminate(const SurfacePoint &sp, Rgb &col, Ray &wi) const;
		virtual void emitPdf(const SurfacePoint &sp, const Vec3 &wo, float &area_pdf, float &dir_pdf, float &cos_wo) const;
		virtual bool canIntersect() const { return soft_shadows_; }
		virtual bool intersect(const Ray &ray, float &t, Rgb &col, float &ipdf) const;
		static Light *factory(ParamMap &params, RenderEnvironment &render);
		virtual int nSamples() const { return samples_; };
	protected:
		Point3 position_;
		Vec3 dir_; //!< orientation of the spot cone
		Vec3 ndir_; //!< negative orientation (-dir)
		Vec3 du_, dv_; //!< form a coordinate system with dir, to sample directions
		float cos_start_, cos_end_; //<! cosStart is actually larger than cosEnd, because cos goes from +1 to -1
		float icos_diff_; //<! 1.0/(cosStart-cosEnd);
		Rgb color_; //<! color, premulitplied by light intensity
		float intensity_;
		Pdf1D *pdf_;
		float interv_1_, interv_2_;

		bool soft_shadows_;
		float shadow_fuzzy_;
		int samples_;
};

SpotLight::SpotLight(const Point3 &from, const Point3 &to, const Rgb &col, float power, float angle, float falloff, bool s_sha, int smpl, float ssfuzzy, bool b_light_enabled, bool b_cast_shadows):
		Light(LightSingular), position_(from), intensity_(power), soft_shadows_(s_sha), shadow_fuzzy_(ssfuzzy), samples_(smpl)
{
	light_enabled_ = b_light_enabled;
	cast_shadows_ = b_cast_shadows;
	ndir_ = (from - to).normalize();
	dir_ = -ndir_;
	color_ = col * power;
	createCs__(dir_, du_, dv_);
	double rad_angle = DEG_TO_RAD(angle);
	double rad_inner_angle = rad_angle * (1.f - falloff);
	cos_start_ = fCos__(rad_inner_angle);
	cos_end_ = fCos__(rad_angle);
	icos_diff_ = 1.0 / (cos_start_ - cos_end_);

	float *f = new float[65];

	for(int i = 0; i < 65; i++)
	{
		float v = (float)i / 64.f;
		f[i] = v * v * (3.f - 2.f * v);
	}

	pdf_ = new Pdf1D(f, 65);

	delete [] f;

	/* the integral of the smoothstep is 0.5, and since it gets applied to the cos, and each delta cos
		corresponds to a constant surface are of the (partial) emitting sphere, we can actually simply
		compute the energie emitted from both areas, the constant and blending one...
		1  cosStart  cosEnd              -1
		|------|--------|-----------------|
	*/

	interv_1_ = (1.0 - cos_start_);
	interv_2_ = 0.5 * (cos_start_ - cos_end_); // as said, energy linear to delta cos, integral is 0.5;
	float sum = std::fabs(interv_1_) + std::fabs(interv_2_);
	if(sum > 0.f) sum = 1.0 / sum;
	interv_1_ *= sum;
	interv_2_ *= sum;
}

SpotLight::~SpotLight()
{
	if(pdf_) delete pdf_;
}

Rgb SpotLight::totalEnergy() const
{
	return color_ * M_2PI * (1.f - 0.5f * (cos_start_ + cos_end_));
}

bool SpotLight::illuminate(const SurfacePoint &sp, Rgb &col, Ray &wi) const
{
	if(photonOnly()) return false;

	Vec3 ldir(position_ - sp.p_);
	float dist_sqr = ldir * ldir;
	float dist = fSqrt__(dist_sqr);
	if(dist == 0.0) return false;

	float idist_sqr = 1.f / (dist_sqr);
	ldir *= 1.f / dist; //normalize

	float cosa = ndir_ * ldir;

	if(cosa < cos_end_) return false; //outside cone
	if(cosa >= cos_start_) // not affected by falloff
	{
		col = color_ * (float)idist_sqr;
	}
	else
	{
		float v = (cosa - cos_end_) * icos_diff_;
		v = v * v * (3.f - 2.f * v);
		col = color_ * (float)(v * idist_sqr);
	}

	wi.tmax_ = dist;
	wi.dir_ = ldir;
	return true;
}

bool SpotLight::illumSample(const SurfacePoint &sp, LSample &s, Ray &wi) const
{
	if(photonOnly()) return false;

	Vec3 ldir(position_ - sp.p_);
	float dist_sqr = ldir * ldir;
	if(dist_sqr == 0.0) return false;
	float dist = fSqrt__(dist_sqr);

	ldir *= 1.f / dist; //normalize

	float cosa = ndir_ * ldir;
	if(cosa < cos_end_) return false; //outside cone

	wi.tmax_ = dist;
	wi.dir_ = sampleCone__(ldir, du_, dv_, cos_end_, s.s_1_ * shadow_fuzzy_, s.s_2_ * shadow_fuzzy_);

	if(cosa >= cos_start_) // not affected by falloff
	{
		s.col_ = color_;
	}
	else
	{
		float v = (cosa - cos_end_) * icos_diff_;
		v = v * v * (3.f - 2.f * v);
		s.col_ = color_ * (float)v;
	}

	s.flags_ = flags_;
	s.pdf_ = dist_sqr;

	//FIXME: I don't quite understand how pdf is calculated in the spotlight, but something looks wrong when dist<1.f, so I'm applying a manual correction to keep s.pdf >= 1 always, and "move" the effect of the distance to the color itself. This is a horrible patch but at least solves a problem causing darker light when distance between spotlight and surface is less than 1.f
	if(s.pdf_ < 1.f)
	{
		s.pdf_ = 1.f;
		s.col_ = s.col_ / dist_sqr;
	}

	return true;
}

Rgb SpotLight::emitPhoton(float s_1, float s_2, float s_3, float s_4, Ray &ray, float &ipdf) const
{
	ray.from_ = position_;

	if(s_3 <= interv_1_) // sample from cone not affected by falloff:
	{
		ray.dir_ = sampleCone__(dir_, du_, dv_, cos_start_, s_1, s_2);
		ipdf = M_2PI * (1.f - cos_start_) / interv_1_;
	}
	else // sample in the falloff area
	{
		float spdf;
		float sm_2 = pdf_->sample(s_2, &spdf) * pdf_->inv_count_;
		ipdf = M_2PI * (cos_start_ - cos_end_) / (interv_2_ * spdf);
		double cos_ang = cos_end_ + (cos_start_ - cos_end_) * (double)sm_2;
		double sin_ang = fSqrt__(1.0 - cos_ang * cos_ang);
		float t_1 = M_2PI * s_1;
		ray.dir_ = (du_ * fCos__(t_1) + dv_ * fSin__(t_1)) * (float)sin_ang + dir_ * (float)cos_ang;
		return color_ * spdf * pdf_->integral_; // scale is just the actual falloff function, since spdf is func * invIntegral...
	}
	return color_;
}

Rgb SpotLight::emitSample(Vec3 &wo, LSample &s) const
{
	s.sp_->p_ = position_;
	s.area_pdf_ = 1.f;
	s.flags_ = flags_;
	if(s.s_3_ <= interv_1_) // sample from cone not affected by falloff:
	{
		wo = sampleCone__(dir_, du_, dv_, cos_start_, s.s_1_, s.s_2_);
		s.dir_pdf_ = interv_1_ / (M_2PI * (1.f - cos_start_));
	}
	else // sample in the falloff area
	{
		float spdf;
		float sm_2 = pdf_->sample(s.s_2_, &spdf) * pdf_->inv_count_;
		s.dir_pdf_ = (interv_2_ * spdf) / (M_2PI * (cos_start_ - cos_end_));
		double cos_ang = cos_end_ + (cos_start_ - cos_end_) * (double)sm_2;
		double sin_ang = fSqrt__(1.0 - cos_ang * cos_ang);
		float t_1 = M_2PI * s.s_1_;
		wo = (du_ * fCos__(t_1) + dv_ * fSin__(t_1)) * (float)sin_ang + dir_ * (float)cos_ang;
		float v = sm_2 * sm_2 * (3.f - 2.f * sm_2);
		return color_ * v;
	}
	return color_;
}

void SpotLight::emitPdf(const SurfacePoint &sp, const Vec3 &wo, float &area_pdf, float &dir_pdf, float &cos_wo) const
{
	area_pdf = 1.f;
	cos_wo = 1.f;

	float cosa = dir_ * wo;
	if(cosa < cos_end_) dir_pdf = 0.f;
	else if(cosa >= cos_start_) // not affected by falloff
	{
		dir_pdf = interv_1_ / (M_2PI * (1.f - cos_start_));
	}
	else
	{
		float v = (cosa - cos_end_) * icos_diff_;
		v = v * v * (3.f - 2.f * v);
		dir_pdf = interv_2_ * v * 2.f / (M_2PI * (cos_start_ - cos_end_));   //divide by integral of v (0.5)?
	}
}
bool SpotLight::intersect(const Ray &ray, float &t, Rgb &col, float &ipdf) const
{
	float cos_a = dir_ * ray.dir_;

	if(cos_a == 0.f) return false;

	t = (dir_ * Vec3(position_ - ray.from_)) / cos_a;

	if(t < 0.f) return false;

	Vec3 p(ray.from_ + Point3(t * ray.dir_));

	if(dir_ * Vec3(p - position_) == 0)
	{
		if(p * p <= 1e-2)
		{
			float cosa = dir_ * ray.dir_;

			if(cosa < cos_end_) return false; //outside cone

			if(cosa >= cos_start_) // not affected by falloff
			{
				col = color_;
			}
			else
			{
				float v = (cosa - cos_end_) * icos_diff_;
				v = v * v * (3.f - 2.f * v);
				col = color_ * v;
			}

			ipdf = 1.f / (t * t);
			Y_VERBOSE << "SpotLight: ipdf, color = " << ipdf << ", " << color_ << YENDL;
			return true;
		}
	}
	return false;
}

Light *SpotLight::factory(ParamMap &params, RenderEnvironment &render)
{
	Point3 from(0.0);
	Point3 to(0.f, 0.f, -1.f);
	Rgb color(1.0);
	float power = 1.0;
	float angle = 45, falloff = 0.15;
	bool p_only = false;
	bool soft_shadows = false;
	int smpl = 8;
	float ssfuzzy = 1.f;
	bool light_enabled = true;
	bool cast_shadows = true;
	bool shoot_d = true;
	bool shoot_c = true;

	params.getParam("from", from);
	params.getParam("to", to);
	params.getParam("color", color);
	params.getParam("power", power);
	params.getParam("cone_angle", angle);
	params.getParam("blend", falloff);
	params.getParam("photon_only", p_only);
	params.getParam("soft_shadows", soft_shadows);
	params.getParam("shadowFuzzyness", ssfuzzy);
	params.getParam("samples", smpl);
	params.getParam("light_enabled", light_enabled);
	params.getParam("cast_shadows", cast_shadows);
	params.getParam("with_caustic", shoot_c);
	params.getParam("with_diffuse", shoot_d);

	SpotLight *light = new SpotLight(from, to, color, power, angle, falloff, soft_shadows, smpl, ssfuzzy, light_enabled, cast_shadows);

	light->shoot_caustic_ = shoot_c;
	light->shoot_diffuse_ = shoot_d;
	light->photon_only_ = p_only;

	return light;
}


extern "C"
{
	YAFRAYPLUGIN_EXPORT void registerPlugin__(RenderEnvironment &render)
	{
		render.registerFactory("spotlight", SpotLight::factory);
	}
}

END_YAFRAY
