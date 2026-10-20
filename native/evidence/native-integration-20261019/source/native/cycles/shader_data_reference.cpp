// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device/device.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_util.h"
#include "kernel/geom/shader_data.h"
#include "scene/scene.h"
#include "wavefront.h"
#include "shader_data_reference.h"
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
