// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device/device.h"
#include "kernel/globals.h"
#include "kernel/util/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_util.h"
#include "kernel/geom/shader_data.h"
#include "scene/scene.h"
#include "wavefront.h"
#include "shader_data_reference.h"
#include <bit>
#include <cstring>
std::vector<ccl::ShaderDataRecord> original_shader_data(ccl::DeviceScene &scene,
    const std::vector<int> &indices,const std::vector<ccl::IntegratorStateCPU> &states){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  std::vector<ShaderDataRecord> out;
  for(int i:indices){
    Ray ray{};Intersection isect{};ShaderData sd{};ShaderDataRecord result{};
    integrator_state_read_ray(&states[i],&ray);integrator_state_read_isect(&states[i],&isect);
    shader_setup_from_ray(kg,&sd,&ray,&isect);
#define FIELD(type,name,wgsl) static_assert(sizeof(result.name)<=sizeof(sd.name));std::memcpy(&result.name,&sd.name,sizeof(result.name));
    CBC_SHADER_FLOAT_FIELDS(FIELD)
    CBC_SHADER_INT_FIELDS(FIELD)
#undef FIELD
    out.push_back(result);
  }
  return out;
}
std::vector<ccl::ObjectNormalRecord> original_object_normals(ccl::DeviceScene &scene,
    const std::vector<int> &indices,const std::vector<ccl::IntegratorStateCPU> &states,unsigned &division_differences){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  std::vector<ObjectNormalRecord> out;
  for(int i:indices){
    Ray ray{};Intersection isect{};ShaderData sd{};ObjectNormalRecord result{};
    integrator_state_read_ray(&states[i],&ray);integrator_state_read_isect(&states[i],&isect);
    shader_setup_from_ray(kg,&sd,&ray,&isect);
    const float3 N=sd.N,P=sd.P;float3 v;result.N=N;result.P=P;
    v=N;object_inverse_normal_transform(kg,&sd,&v);result.inverse_N=v;
    v=N;object_normal_transform(kg,&sd,&v);result.forward_N=v;
    v=P;object_inverse_normal_transform(kg,&sd,&v);result.inverse_P=v;
    v=P;object_normal_transform(kg,&sd,&v);result.forward_P=v;
    v=zero_float3();object_inverse_normal_transform(kg,&sd,&v);result.inverse_zero=v;
    const Transform tfm=(sd.object_flag&SD_OBJECT_MOTION)?sd.ob_tfm_motion:object_fetch_transform(kg,sd.object,OBJECT_TRANSFORM);
    for(const float3 &x:{N,P}){const float3 a=transform_direction_transposed(&tfm,x),reciprocal=safe_normalize(a),quotient=normalize(a);
      for(int k=0;k<3;k++)division_differences+=std::bit_cast<uint>(reciprocal[k])!=std::bit_cast<uint>(quotient[k]);}
    out.push_back(result);
  }
  return out;
}
