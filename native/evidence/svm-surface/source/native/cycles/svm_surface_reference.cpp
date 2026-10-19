// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device/device.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_util.h"
#include "kernel/geom/shader_data.h"
#include "kernel/integrator/surface_shader.h"
#include "scene/scene.h"
#include "wavefront.h"
#include "svm_surface_reference.h"
std::vector<ccl::SurfaceSvmRecord> original_surface_svm(ccl::DeviceScene &scene,
    const std::vector<int> &indices,const std::vector<ccl::IntegratorStateCPU> &states){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
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
      if(c.type==CLOSURE_BSDF_OREN_NAYAR_ID){auto &p=reinterpret_cast<const OrenNayarBsdf*>(&c)->param;
        r.roughness=p.roughness;r.a=p.a;r.b=p.b;r.multiscatter=p.multiscatter_term;}
    }
    out.push_back(result);
  }
  return out;
}
