// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include <stdexcept>
namespace ccl {
void WebGPUDevice::load_image_info(){
  std::lock_guard lock(mutex);if(!image_info_dirty)return;
  const size_t count=images.empty()?0:size_t(images.rbegin()->first)+1;
  std::vector<KernelImageInfo> info(count);std::vector<uint2> bounds(count,make_uint2(0,0));
  for(auto &[id,image]:images){
    validate(image->device_pointer,image->memory_size());info[id]=image->info;info[id].data=image->device_pointer;
    bounds[id]=make_uint2(uint(image->device_pointer),uint(image->memory_size()));
  }
  auto update=[&](uint32_t &address,const char *name,const void *data,size_t bytes){
    if(address&&remaining(address)!=bytes){flush();release(address);address=0;}
    if(bytes){if(!address)address=allocate(bytes);upload(address,data,bytes);globals[name]=address;}
    else globals.erase(name);
  };
  update(image_info_address,"image_info",info.data(),info.size()*sizeof(KernelImageInfo));
  update(image_bounds_address,"image_bounds",bounds.data(),bounds.size()*sizeof(uint2));
  image_info_dirty=false;
}
// Original CPU oracle bindings use host image storage and native metadata;
// no GPU sampler output or WGSL-computed descriptor is fed into the reference.
std::vector<KernelImageInfo> WebGPUDevice::reference_image_info() const{
  const size_t count=images.empty()?0:size_t(images.rbegin()->first)+1;
  std::vector<KernelImageInfo> info(count);
  for(auto &[id,image]:images){info[id]=image->info;info[id].data=reinterpret_cast<uintptr_t>(image->host_pointer);}
  return info;
}
}
