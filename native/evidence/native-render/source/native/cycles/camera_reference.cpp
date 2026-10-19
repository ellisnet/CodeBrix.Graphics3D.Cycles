// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device/device.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#include "kernel/integrator/init_from_camera.h"
#include "scene/scene.h"
#include "camera_reference.h"
namespace {
using namespace ccl;
KernelGlobalsCPU globals(DeviceScene &scene){
  KernelGlobalsCPU kg;kg.data=scene.data;
#define KERNEL_DATA_ARRAY(type,name) kg.name.data=scene.name.data();kg.name.width=int(scene.name.size());
#define KERNEL_DATA_ARRAY_WRITABLE KERNEL_DATA_ARRAY
  KERNEL_DATA_ARRAY(float,lookup_table)
  KERNEL_DATA_ARRAY(float,sample_pattern_lut)
#undef KERNEL_DATA_ARRAY
#undef KERNEL_DATA_ARRAY_WRITABLE
  return kg;
}
}
void original_camera(ccl::DeviceScene &scene,const std::vector<ccl::KernelWorkTile> &tiles,unsigned first,
                     std::vector<ccl::IntegratorStateCPU> &states,std::vector<float> &film){
  using namespace ccl;auto base=globals(scene);Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  for(auto tile:tiles){
    // Canonical CPU pixel/sample iteration, mapped to the original GPU's two
    // work orderings. Oracle calls the unchanged CPU camera/path/film code.
    for(unsigned y=0;y<tile.h;y++)for(unsigned x=0;x<tile.w;x++)for(unsigned s=0;s<tile.num_samples;s++){
      unsigned pixel=y*tile.w+x,index=scene.data.integrator.scrambling_distance<.9f?s*tile.w*tile.h+pixel:pixel*tile.num_samples+s;
      integrator_init_from_camera(kg,&states[first+index],&tile,film.data(),tile.x+x,tile.y+y,tile.start_sample+s);
    }
    first+=tile.w*tile.h*tile.num_samples;
  }
}
void original_camera_retry(ccl::DeviceScene &scene,const std::vector<int> &indices,
                           std::vector<ccl::IntegratorStateCPU> &states,std::vector<float> &film){
  using namespace ccl;auto base=globals(scene);Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  for(auto index:indices)integrator_init_from_camera(kg,&states[index],nullptr,film.data(),0,0,0);
}
