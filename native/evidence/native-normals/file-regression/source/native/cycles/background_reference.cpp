// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device/device.h"
#include "image_reference.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#include "kernel/integrator/shade_background.h"
#include "scene/scene.h"
#include "background_reference.h"
void original_background(ccl::DeviceScene &scene,const std::vector<int> &indices,
                         std::vector<ccl::IntegratorStateCPU> &states,std::vector<float> &film){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  TABLE(image_textures) TABLE(image_texture_udims) TABLE(shaders) TABLE(svm_nodes) TABLE(lookup_table) TABLE(sample_pattern_lut)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  for(int i:indices)integrator_shade_background(kg,&states[i],film.data());
}
