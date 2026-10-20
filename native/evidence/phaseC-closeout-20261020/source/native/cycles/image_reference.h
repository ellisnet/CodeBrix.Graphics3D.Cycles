// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "device.h"
#include "scene/scene.h"
inline std::vector<ccl::KernelImageInfo> original_image_info(ccl::DeviceScene &scene){
  auto device=dynamic_cast<ccl::WebGPUDevice*>(scene.image_textures.device);
  if(!device)throw std::runtime_error("Image reference requires WebGPU scene storage");
  return device->reference_image_info();
}
