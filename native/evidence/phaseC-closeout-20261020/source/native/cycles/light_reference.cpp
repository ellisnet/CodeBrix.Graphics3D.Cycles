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
#include "kernel/integrator/shade_light.h"
#include "scene/scene.h"
#include "light_reference.h"
#include "wavefront.h"
void original_light_forward(ccl::DeviceScene &scene,const std::vector<int> &indices,
                         std::vector<ccl::IntegratorStateCPU> &states,std::vector<float> &film){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  for(int i:indices)integrator_shade_light_forward(kg,&states[i],film.data());
}

std::vector<float> original_light_samples(ccl::DeviceScene &scene,const std::vector<ccl::IntegratorStateCPU> &states){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;std::vector<float> out(states.size()*12);
  for(size_t i=0;i<states.size();i++){auto state=states[i];RNGState rng;path_state_rng_load(&state,&rng);const auto random=path_state_rng_3D(kg,&rng,PRNG_LIGHT);LightSample ls{};
    const int runtime=(state.path.flag&PATH_RAY_MIS_HAD_TRANSMISSION)?SR_BSDF_HAS_TRANSMISSION:0;
    const bool valid=light_sample_from_position(kg,random,state.ray.time,state.ray.P,state.ray.D,OBJECT_NONE,runtime,state.path.bounce,state.path.flag,&ls)&&ls.pdf>0;
    auto p=out.data()+i*12;p[0]=valid;if(valid){p[1]=ls.P.x;p[2]=ls.P.y;p[3]=ls.P.z;p[4]=ls.D.x;p[5]=ls.D.y;p[6]=ls.D.z;p[7]=ls.t;p[8]=ls.pdf;p[9]=ls.eval_fac;p[10]=ls.prim;p[11]=ls.object;}
  }return out;
}
