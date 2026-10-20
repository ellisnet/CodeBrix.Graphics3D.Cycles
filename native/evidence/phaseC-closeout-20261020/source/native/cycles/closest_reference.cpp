// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device/device.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
// Upstream leaves some miss-record fields undefined on CPU. Enable its
// optional initialization hook so the oracle never reads indeterminate data;
// no defined intersection or scheduling behavior is changed.
#undef ccl_optional_struct_init
#define ccl_optional_struct_init {}
#include "kernel/integrator/intersect_closest.h"
#include "scene/scene.h"
#include "closest_reference.h"
#include "wavefront.h"
void original_closest(ccl::DeviceScene &scene,const std::vector<int> &indices,
                      std::vector<ccl::IntegratorStateCPU> &states,std::vector<float> &film){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(lookup_table,float) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  for(int i:indices)integrator_intersect_closest(kg,&states[i],film.data());
}
