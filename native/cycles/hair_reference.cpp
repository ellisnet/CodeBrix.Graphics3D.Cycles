// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device/device.h"
#include "image_reference.h"
#include "kernel/globals.h"
#include "kernel/util/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_util.h"
#include "kernel/geom/shader_data.h"
#include "kernel/integrator/surface_shader.h"
#include "scene/scene.h"
#include "wavefront.h"
#include "hair_reference.h"
#include <cmath>
void original_hair_probe(ccl::DeviceScene &scene,const std::vector<int> &indices,const std::vector<ccl::IntegratorStateCPU> &states,std::vector<ccl::HairProbePath> &paths){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(lookup_table,float) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  auto to3=[](float *out,float3 v){out[0]=v.x;out[1]=v.y;out[2]=v.z;};
  for(size_t n=0;n<indices.size();n++){
    auto state=states[indices[n]];auto &path=paths[n];Ray ray{};Intersection isect{};ShaderData sd{};
    integrator_state_read_ray(&state,&ray);integrator_state_read_isect(&state,&isect);shader_setup_from_ray(kg,&sd,&ray,&isect);
    surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE>(kg,&state,&sd,nullptr,PathRayVisibility(state.path.visibility),state.path.flag);
    path.closure_count=std::min<unsigned>(sd.num_closure,hair_probe_closures);
    for(unsigned k=0;k<path.closure_count;k++)path.kinds[k]=sd.closure[k].type;
    for(unsigned j=0;j<hair_probe_samples;j++){
      auto &record=path.samples[j];record={};float3 rand=make_float3(path.rand[j][0],path.rand[j][1],path.rand[j][2]);
      if(!(sd.runtime_flag&SR_BSDF)||!sd.num_closure)continue;
      const ShaderClosure *sc=surface_shader_bsdf_bssrdf_pick(&sd,&rand);if(!CLOSURE_IS_BSDF(sc->type))continue;
      BsdfEval eval;float3 wo=zero_float3();float pdf=0,eta=0,roughness=0;float2 sampled=make_float2(0,0);
      const int label=surface_shader_bsdf_sample_closure(kg,&sd,sc,rand,&eval,&wo,&pdf,&sampled,&eta,roughness);
      record.label=label;to3(record.wo,wo);record.pdf=pdf;record.selected=uint(sc-sd.closure);
      if(pdf!=0){auto sum=bsdf_eval_sum(&eval);to3(record.eval,sum);to3(record.diffuse,eval.diffuse);to3(record.glossy,eval.glossy);record.roughness=roughness;}
    }
    for(unsigned j=0;j<hair_probe_directions;j++){
      // Odd directions: the sampled ones, when the sample has a density; the others scatter over the sphere.
      const auto &from=path.samples[j%hair_probe_samples];float3 wo=make_float3(from.wo[0],from.wo[1],from.wo[2]);
      if(!(j&1)||!(from.pdf>0)){const float a=float(j)*2.399963f+float(n)*.37f,z=1-(2*float(j)+1)/float(hair_probe_directions);const float r=std::sqrt(1-z*z);wo=make_float3(r*std::cos(a),r*std::sin(a),z);}
      to3(path.directions[j],wo);
      for(unsigned k=0;k<path.closure_count;k++){float pdf=0;auto value=bsdf_eval(kg,&sd,&sd.closure[k],wo,&pdf);auto &e=path.evals[j][k];to3(e.value,value);e.pdf=pdf;}
    }
  }
}
