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
#include "hair_huang_reference.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace ccl {
std::vector<HuangGateRecord> original_hair_huang(DeviceScene &scene,const std::vector<int> &indices,const std::vector<IntegratorStateCPU> &states,const std::vector<HuangGateInput> &inputs){
  KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(lookup_table,float) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  std::vector<HuangGateRecord> out;out.reserve(indices.size());
  for(size_t n=0;n<indices.size();n++){
    const int i=indices[n];const auto &input=inputs[n];
    Ray ray{};Intersection isect{};ShaderData sd{};HuangGateRecord r{};
    integrator_state_read_ray(&states[i],&ray);integrator_state_read_isect(&states[i],&isect);
    if(std::getenv("CODEBRIX_HAIR_GATE_TRACE"))fprintf(stderr,"TRACE n=%zu i=%d prim=%d object=%d type=%d t=%g\n",n,i,isect.prim,isect.object,isect.type,isect.t);
    shader_setup_from_ray(kg,&sd,&ray,&isect);
    surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE>(kg,&states[i],&sd,nullptr,PathRayVisibility(states[i].path.visibility),states[i].path.flag);
    r.count=sd.num_closure;r.remaining=sd.num_closure_left;r.runtime_flag=sd.runtime_flag;r.lcg=sd.lcg_state;r.N=sd.N;r.transparency=sd.closure_transparent_extinction.x;
    const int closures=std::min<int>(sd.num_closure,huang_gate_closures);
    for(int j=0;j<closures;j++){
      const auto &c=sd.closure[j];auto &g=r.closures[j];g.weight=c.weight;g.sample_weight=c.sample_weight;g.N=c.N;g.kind=c.type;
      if(c.type==CLOSURE_BSDF_HAIR_HUANG_ID){const auto &h=*reinterpret_cast<const HuangHairBSDF*>(&c);const auto &e=*h.extra;
        g.roughness=h.roughness;g.tilt=h.tilt;g.eta=h.eta;g.aspect_ratio=h.aspect_ratio;g.h=h.h;g.sigma=h.sigma;g.Y=e.Y;g.Z=e.Z;g.wi=e.wi;
        g.radius=e.radius;g.e2=e.e2;g.pixel_coverage=e.pixel_coverage;g.R=e.R;g.TT=e.TT;g.TRT=e.TRT;}
    }
    for(unsigned k=0;k<huang_gate_directions;k++)for(int j=0;j<closures;j++){
      float pdf=0;const Spectrum value=bsdf_eval(kg,&sd,&sd.closure[j],float3(input.wo[k]),&pdf);
      r.eval[k][j]={value,pdf,sd.lcg_state};
    }
    for(int j=0;j<closures;j++){
      Spectrum eval=zero_spectrum();float3 wo=zero_float3();float pdf=0,eta=1;float2 roughness=zero_float2();
      const int label=bsdf_sample(kg,&sd,&sd.closure[j],float3(input.random[j]),&eval,&wo,&pdf,&roughness,&eta);
      r.sample[j]={wo,pdf,eval,uint(label),sd.lcg_state};
    }
    out.push_back(r);
  }
  return out;
}
}
