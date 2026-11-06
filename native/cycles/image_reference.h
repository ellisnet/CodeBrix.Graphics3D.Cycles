// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "device.h"
#include "kernel/device/cpu/globals.h"
#include "scene/scene.h"
inline std::vector<ccl::KernelImageInfo> original_image_info(ccl::DeviceScene &scene){
  ccl::Device *owner=scene.image_textures.device;
  if(auto device=dynamic_cast<ccl::WebGPUDevice*>(owner))return device->reference_image_info();
  // An exchange pass on the Cycles CPU device (reference_policy.h, CODEBRIX_CYCLES_EXCHANGE_CPU_DEVICE):
  // the CPU device's own image bindings, also host storage and native metadata.
  if(!owner||owner->info.type!=ccl::DEVICE_CPU)throw std::runtime_error("Image reference requires WebGPU scene storage");
  std::vector<ccl::KernelImageInfo> info;auto *threads=owner->acquire_cpu_kernel_thread_globals();
  if(threads&&!threads->empty()){const auto &globals=threads->front();info.assign(globals.image_info.data,globals.image_info.data+globals.image_info.width);}
  owner->release_cpu_kernel_thread_globals();return info;
}
