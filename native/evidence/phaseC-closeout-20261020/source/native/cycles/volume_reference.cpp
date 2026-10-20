// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Calls the unchanged pinned native volume integrator.
// See native/THIRD-PARTY-NOTICES.txt and
// parity/licenses/BSD-3-Clause.txt for the complete retained notices/license.
#include "device/device.h"
#include "image_reference.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#include "kernel/integrator/shade_surface.h"
#include "kernel/integrator/shade_volume.h"
#include "kernel/integrator/intersect_volume_stack.h"
#include "kernel/bake/bake.h"
#include "scene/scene.h"
#include "wavefront.h"
#include "volume_reference.h"
#include <cstdio>
#include <cstdlib>
void original_volume(ccl::DeviceScene &scene,const std::vector<int> &indices,std::vector<ccl::IntegratorStateCPU> &states,std::vector<float> &film){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  for(int i:indices){
    if(i==243&&std::getenv("CODEBRIX_VOLUME_STACK_TRACE")){
      Ray ray;integrator_state_read_ray(&states[i],&ray);ShaderData sd;shader_setup_from_volume(&sd,&ray,states[i].volume_stack[0].object);
      volume_shader_eval<false>(kg,&states[i],&sd,states[i].path.visibility,states[i].path.flag);
      for(int j=0;j<sd.num_closure;j++){const auto &c=sd.closure[j];const auto *hg=reinterpret_cast<const HenyeyGreensteinVolume*>(&c);std::fprintf(stderr,"CPU phase g=%.9g sample=%.9g weight=%.9g,%.9g,%.9g\n",hg->g,c.sample_weight,c.weight.x,c.weight.y,c.weight.z);}
      RNGState rng;path_state_rng_load(&states[i],&rng);auto random=path_state_rng_2D(kg,&rng,PRNG_VOLUME_PHASE);std::fprintf(stderr,"CPU random %.9g %.9g\n",random.x,random.y);
    }
    integrator_shade_volume(kg,&states[i],film.data());
  }
}

void original_volume_stack(ccl::DeviceScene &scene,const std::vector<int> &indices,std::vector<ccl::IntegratorStateCPU> &states ){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  for(int i:indices)integrator_intersect_volume_stack(kg,&states[i]);
}

void original_volume_density(ccl::DeviceScene &scene,const std::vector<ccl::KernelShaderEvalInput> &input,std::vector<float> &output){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  uint cache_miss=0;for(size_t i=0;i<output.size()/2;i++)kernel_volume_density_evaluate(kg,input.data(),output.data(),&cache_miss,int(i));
}
