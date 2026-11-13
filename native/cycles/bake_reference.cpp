// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device/device.h"
#include "kernel/globals.h"
#include "kernel/util/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#undef ccl_optional_struct_init
#define ccl_optional_struct_init {}
#include "kernel/integrator/init_from_bake.h"
#include "scene/scene.h"
#include "bake_reference.h"
#include "wavefront.h"
bool original_bake(ccl::DeviceScene &scene,const ccl::KernelWorkTile &tile,unsigned x,unsigned y,unsigned sample,
                   ccl::IntegratorStateCPU pair[2],std::vector<float> &film){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  return integrator_init_from_bake(kg,&pair[0],&tile,film.data(),int(x),int(y),int(sample));
}
