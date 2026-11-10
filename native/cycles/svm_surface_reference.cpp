// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device/device.h"
#include "image_reference.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_util.h"
#include "kernel/geom/shader_data.h"
#include "kernel/integrator/surface_shader.h"
#include "scene/scene.h"
#include "wavefront.h"
#include "svm_surface_reference.h"
#include <stdexcept>
std::vector<ccl::SurfaceSvmRecord> original_surface_svm(ccl::DeviceScene &scene,
    const std::vector<int> &indices,const std::vector<ccl::IntegratorStateCPU> &states){
  using namespace ccl;
  static_assert((sizeof(FresnelGeneralizedSchlick)+sizeof(ShaderClosure)-1)/sizeof(ShaderClosure)==1);
  static_assert((sizeof(FresnelF82Tint)+sizeof(ShaderClosure)-1)/sizeof(ShaderClosure)==1);
  static_assert((sizeof(FresnelConductor)+sizeof(ShaderClosure)-1)/sizeof(ShaderClosure)==1);
  KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(lookup_table,float) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  std::vector<SurfaceSvmRecord> out;
  for(int i:indices){
    Ray ray{};Intersection isect{};ShaderData sd{};SurfaceSvmRecord result{};
    integrator_state_read_ray(&states[i],&ray);integrator_state_read_isect(&states[i],&isect);
    shader_setup_from_ray(kg,&sd,&ray,&isect);
    surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE>(kg,&states[i],&sd,nullptr,
        PathRayVisibility(states[i].path.visibility),states[i].path.flag);
    result.count=sd.num_closure;result.remaining=sd.num_closure_left;
    result.runtime_flag=sd.runtime_flag;result.lcg_state=sd.lcg_state;result.N=sd.N;
    if(sd.runtime_flag&SR_EMISSION)result.emission=sd.closure_emission_background;
    result.transparency=sd.closure_transparent_extinction;
    for(int j=0;j<sd.num_closure;j++){
      auto &c=sd.closure[j];auto &r=result.closures[j];r.weight=c.weight;r.kind=c.type;
      if(c.type!=CLOSURE_HOLDOUT_ID){r.N=c.N;r.sample_weight=c.sample_weight;}
      // The rough translucent closure of a thin wall's subsurface lobe has the Oren-Nayar record too.
      if(c.type==CLOSURE_BSDF_OREN_NAYAR_ID||c.type==CLOSURE_BSDF_ROUGH_TRANSLUCENT_ID){auto &p=reinterpret_cast<const OrenNayarBsdf*>(&c)->param;
        r.roughness=p.roughness;r.a=p.a;r.b=p.b;r.multiscatter=p.multiscatter_term;}
      if(CLOSURE_IS_BSDF_MICROFACET(c.type)){auto &p=*reinterpret_cast<const MicrofacetBsdf*>(&c);
        r.alpha_x=p.alpha_x;r.alpha_y=p.alpha_y;r.ior=p.ior;r.energy_scale=p.energy_scale;r.T=p.T;
        // Beckmann and Ashikhmin-Shirley setups leave energy_scale undefined (never read): canonical 0.
        if(c.type==CLOSURE_BSDF_MICROFACET_BECKMANN_ID||c.type==CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID||
           c.type==CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID||c.type==CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID)r.energy_scale=0;
        // Canonical Fresnel encoding (surface_closure.h); only the models the
        // WebGPU device implements are representable.
        if(!(p.fresnel_type==MicrofacetFresnel::NONE||p.fresnel_type==MicrofacetFresnel::DIELECTRIC||p.fresnel_type==MicrofacetFresnel::CONDUCTOR||
             p.fresnel_type==MicrofacetFresnel::GENERALIZED_SCHLICK||p.fresnel_type==MicrofacetFresnel::F82_TINT))throw std::runtime_error("Unrepresentable reference Fresnel model");
        r.roughness=float(p.fresnel_type);
        if(p.fresnel_type==MicrofacetFresnel::GENERALIZED_SCHLICK){auto &f=*reinterpret_cast<const FresnelGeneralizedSchlick*>(p.fresnel);
          if(!(f.exponent<0&&isequal(f.f90,one_spectrum())))throw std::runtime_error("Unrepresentable generalized Schlick reference");
          if(f.thin_film.thickness>THINFILM_THICKNESS_CUTOFF){r.a=f.thin_film.thickness;r.b=f.thin_film.ior;}
          r.fresnel_f0=f.f0.x;r.multiscatter=f.f0;r.reflection_tint=f.reflection_tint;r.transmission_tint=f.transmission_tint;}
        if(p.fresnel_type==MicrofacetFresnel::F82_TINT){auto &f=*reinterpret_cast<const FresnelF82Tint*>(p.fresnel);
          if(f.thin_film.thickness>THINFILM_THICKNESS_CUTOFF){r.a=f.thin_film.thickness;r.b=f.thin_film.ior;}
          r.multiscatter=f.f0;r.reflection_tint=f.b;}
        // Metallic BSDF physical conductor: n in multiscatter, k in reflection_tint.
        if(p.fresnel_type==MicrofacetFresnel::CONDUCTOR){auto &f=*reinterpret_cast<const FresnelConductor*>(p.fresnel);
          if(f.thin_film.thickness>THINFILM_THICKNESS_CUTOFF){r.a=f.thin_film.thickness;r.b=f.thin_film.ior;}
          r.multiscatter=f.ior.re;r.reflection_tint=f.ior.im;}}
      // Velvet: sigma in roughness, 1/max(sigma, 0.01)^2 in a. Toon: size and smooth (scaled) in a and b.
      if(c.type==CLOSURE_BSDF_ASHIKHMIN_VELVET_ID){auto &p=*reinterpret_cast<const VelvetBsdf*>(&c);r.roughness=p.sigma;r.a=p.invsigma2;}
      if(c.type==CLOSURE_BSDF_DIFFUSE_TOON_ID||c.type==CLOSURE_BSDF_GLOSSY_TOON_ID){auto &p=*reinterpret_cast<const ToonBsdf*>(&c);r.a=p.size;r.b=p.smooth;}
      // Bssrdf (wavefront_svm_closures.wgsl): radius in multiscatter, albedo in T, anisotropy in a, ior, alpha in roughness.
      if(CLOSURE_IS_BSSRDF(c.type)){auto &p=*reinterpret_cast<const Bssrdf*>(&c);r.multiscatter=p.radius;r.T=p.albedo;r.a=p.anisotropy;r.ior=p.ior;r.roughness=p.alpha;}
      // Hair: T, roughness1 and 2 in alpha_x and alpha_y, offset in a. Chiang: sigma in multiscatter, v in roughness, s in a, alpha in b, eta in ior, m0_roughness in alpha_x, h in alpha_y.
      if(c.type==CLOSURE_BSDF_HAIR_REFLECTION_ID||c.type==CLOSURE_BSDF_HAIR_TRANSMISSION_ID){auto &p=*reinterpret_cast<const HairBsdf*>(&c);r.T=p.T;r.alpha_x=p.roughness1;r.alpha_y=p.roughness2;r.a=p.offset;}
      if(c.type==CLOSURE_BSDF_HAIR_CHIANG_ID){auto &p=*reinterpret_cast<const ChiangHairBSDF*>(&c);r.multiscatter=p.sigma;r.roughness=p.v;r.a=p.s;r.b=p.alpha;r.ior=p.eta;r.alpha_x=p.m0_roughness;r.alpha_y=p.h;}
      // Ray portal (wavefront_svm_closures.wgsl svm_ray_portal): the position in multiscatter, the direction in T.
      if(c.type==CLOSURE_BSDF_RAY_PORTAL_ID){auto &p=*reinterpret_cast<const RayPortalClosure*>(&c);r.multiscatter=p.P;r.T=p.D;}
      if(c.type==CLOSURE_BSDF_SHEEN_ID){auto &p=*reinterpret_cast<const SheenBsdf*>(&c);
        r.roughness=p.roughness;r.a=p.transformA;r.b=p.transformB;r.T=p.T;r.multiscatter=p.B;}
    }
    out.push_back(result);
  }
  return out;
}
