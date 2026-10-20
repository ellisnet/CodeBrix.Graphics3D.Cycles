// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "image_reference.h"
#include "image_sampling_reference.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/svm/image.h"
std::vector<ccl::float4> original_image_samples(ccl::DeviceScene &scene,const std::vector<ImageProbe> &samples){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;auto info=original_image_info(scene);
  base.image_info.data=info.data();base.image_info.width=info.size();
  base.image_textures.data=scene.image_textures.data();base.image_textures.width=scene.image_textures.size();
  base.image_texture_udims.data=scene.image_texture_udims.data();base.image_texture_udims.width=scene.image_texture_udims.size();
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  std::vector<float4> out;for(auto &sample:samples){ShaderData sd{};out.push_back(svm_image_texture(kg,&sd,sample.id,dual2(make_float2(sample.u,sample.v)),sample.flags));}return out;
}
