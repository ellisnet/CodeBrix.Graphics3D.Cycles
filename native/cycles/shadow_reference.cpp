// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device/device.h"
#include "image_reference.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
// Keep the original CPU state ABI; compile untouched shadow algorithms with
// the native GPU intersection capacity for stage-by-stage state comparisons.
#ifdef CODEBRIX_TRANSPARENT_SHADOW_STATE_GATE
#undef INTEGRATOR_SHADOW_ISECT_SIZE
#define INTEGRATOR_SHADOW_ISECT_SIZE INTEGRATOR_SHADOW_ISECT_SIZE_GPU
#endif
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#include "kernel/geom/primitive.h"
#include "kernel/integrator/intersect_shadow.h"
#include "kernel/integrator/shade_shadow.h"
#include "scene/scene.h"
#include "shadow_reference.h"
#include "wavefront.h"
void original_shadow(ccl::DeviceScene &scene,ccl::DeviceKernel kernel,const std::vector<int> &indices,
                     std::vector<ccl::IntegratorShadowStateCPU> &states,std::vector<float> &film){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(lookup_table,float) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  for(int i:indices){
    if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW)integrator_intersect_shadow(kg,&states[i]);
    else integrator_shade_shadow(kg,&states[i],film.data());
  }
}
