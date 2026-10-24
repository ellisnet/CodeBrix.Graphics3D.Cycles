// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Original CPU oracles for the environment-light and non-constant-emitter gates:
// the unmodified shade_light_nee, kernel_background_evaluate and light sampling
// kernels on the device-prepared scene tables.
#include "device/device.h"
#include "image_reference.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#include "kernel/integrator/shade_light.h"
#include "kernel/bake/bake.h"
#include "scene/scene.h"
#include "environment_reference.h"
#include "wavefront.h"
namespace {
struct Globals {
  ccl::KernelGlobalsCPU base;std::vector<ccl::KernelImageInfo> images;ccl::Profiler profiler;std::unique_ptr<ccl::ThreadKernelGlobalsCPU> thread;
  explicit Globals(ccl::DeviceScene &scene){
    using namespace ccl;base.data=scene.data;images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
    CBC_SCENE_TABLES(TABLE)
    TABLE(svm_nodes,uint) TABLE(sample_pattern_lut,float)
#undef TABLE
    thread=std::make_unique<ThreadKernelGlobalsCPU>(base,nullptr,profiler,0);
  }
  ccl::KernelGlobals kg(){return thread.get();}
};
}
void original_light_nee(ccl::DeviceScene &scene,const std::vector<int> &indices,std::vector<ccl::IntegratorShadowStateCPU> &states,std::vector<float> &film){
  Globals g(scene);for(int i:indices)ccl::integrator_shade_light_nee(g.kg(),&states[i],film.data());
}
std::vector<float> original_background_evaluate(ccl::DeviceScene &scene,const std::vector<ccl::KernelShaderEvalInput> &inputs){
  Globals g(scene);std::vector<float> out(inputs.size()*3);ccl::uint miss=0;
  for(size_t i=0;i<inputs.size();i++)ccl::kernel_background_evaluate(g.kg(),inputs.data(),out.data(),&miss,int(i));
  return out;
}
std::vector<float> original_light_sample_evals(ccl::DeviceScene &scene,const std::vector<ccl::IntegratorStateCPU> &states){
  using namespace ccl;Globals g(scene);KernelGlobals kg=g.kg();std::vector<float> out(states.size()*4);
  for(size_t i=0;i<states.size();i++){auto state=states[i];RNGState rng;path_state_rng_load(&state,&rng);const auto random=path_state_rng_3D(kg,&rng,PRNG_LIGHT);LightSample ls{};
    const int runtime=(state.path.flag&PATH_RAY_MIS_HAD_TRANSMISSION)?SR_BSDF_HAS_TRANSMISSION:0;
    const bool valid=light_sample_from_position(kg,random,state.ray.time,state.ray.P,state.ray.D,OBJECT_NONE,runtime,state.path.bounce,state.path.flag,&ls)&&ls.pdf>0;
    auto p=out.data()+i*4;p[0]=valid;if(!valid)continue;
    Spectrum eval;
    if(ls.type==LIGHT_BACKGROUND){
      // integrate_light_nee for the background light, with a zero ray differential.
      ShaderDataCausticsStorage storage;ShaderData *sd=AS_SHADER_DATA(&storage);
      shader_setup_from_background(kg,sd,state.ray.P,ls.D,background_light_clamp_dD(kg,0.0f),state.ray.time);
      surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE_LIGHT>(kg,&state,sd,nullptr,PATH_RAY_VISIBILITY_NONE,PATH_RAY_EMISSION);
      eval=surface_shader_background(sd);
      const KernelLight &light=kernel_data_fetch(lights,ls.prim);eval*=make_float3(light.strength[0],light.strength[1],light.strength[2]);
    }else if(!light_sample_shader_eval_nee_constant(kg,ls.shader,ls.prim,ls.type!=LIGHT_TRIANGLE,eval)&&ls.type==LIGHT_TRIANGLE){
      // integrate_light_nee for an emissive triangle, from the probe's origin with zero differentials.
      Ray ray{};ray.P=state.ray.P;ray.D=ls.D;ray.tmin=0.0f;ray.tmax=ls.t;ray.time=state.ray.time;ray.dP=0.0f;ray.dD=0.0f;
      Intersection isect{};isect.object=ls.object;isect.prim=ls.prim;isect.type=kernel_data_fetch(objects,ls.object).primitive_type;isect.t=ls.t;
      const float2 uv=triangle_light_uv(kg,isect.object,isect.prim,ray.time,ray.P,ray.D);isect.u=uv.x;isect.v=uv.y;
      ShaderDataCausticsStorage storage;ShaderData *sd=AS_SHADER_DATA(&storage);shader_setup_from_ray(kg,sd,&ray,&isect);
      surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE_LIGHT>(kg,&state,sd,nullptr,PATH_RAY_VISIBILITY_NONE,PATH_RAY_EMISSION);
      eval=surface_shader_emission(sd);
    }
    p[1]=eval.x;p[2]=eval.y;p[3]=eval.z;
  }
  return out;
}
