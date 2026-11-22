// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
namespace ccl {
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
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
// Tiled images (Cycles' texture cache; pipelines.h SF_IMAGE_TILED). The pinned GPU kernels leave a path that requested a
// tile queued and run it again after the host loaded the tile (integrator_path_cache_miss, kernel/integrator/state_flow.h),
// but they detect some misses only after the kernel wrote to the film and the path (integrate_surface: the direct light's
// shader evaluation follows the emission, the termination's throughput division and the data passes), and the CPU kernels,
// which this device is judged against, load the tile inside the lookup and never run anything twice. So this device runs a
// shader-evaluating kernel of such a scene as a transaction: everything a kernel may write is copied aside, the kernel runs,
// and when a lookup requested a tile (the miss word, wavefront_images.wgsl image_tile_map) the host loads the requested
// tiles through the pinned ImageManager::device_gpu_load_requested (Device::image_load_requested_gpu: it reads the access
// state, loads, and copies pool images, descriptors and image info), the copy is put back and the kernel runs again. The
// run that counts has every tile it reads loaded, as on the CPU. The tile access state is put back too, so it ends with
// the CPU's values (KERNEL_TILE_ACCESS_USED for the tiles read).
bool WebGPUDevice::tile_written(uint32_t address) const{
  if(tile_readonly.count(address)||address==image_info_address||address==image_bounds_address||address==intersection_tables||address==fields||address==tile_miss_word)return false;
  for(const auto &[name,constant]:constants)if(constant==address)return false;
  return true;
}
void WebGPUDevice::tile_copy(WGPUBuffer from,WGPUBuffer to){
  for(size_t i=0;i<tile_snapshot_ranges.size();){
    const uint32_t begin=tile_snapshot_ranges[i].first;size_t bytes=tile_snapshot_ranges[i].second;
    for(i++;i<tile_snapshot_ranges.size()&&tile_snapshot_ranges[i].first==begin+bytes;i++)bytes+=tile_snapshot_ranges[i].second;
    wgpuCommandEncoderCopyBufferToBuffer(commands(),from,begin,to,begin,bytes);
  }
}
void WebGPUDevice::tile_transaction(DeviceQueue &queue,const std::function<void()> &kernel,const char *name){
  for(unsigned round=0;;round++){
    guard([&]{
      // The snapshot: every allocation except those no kernel writes. Adjacent allocations are copied as one range.
      tile_snapshot_ranges.clear();
      for(const auto &[address,allocation]:allocations)if(tile_written(address))tile_snapshot_ranges.emplace_back(address,allocation.extent);
      if(observer){
        // Under a command recorder (replay captures) the snapshot is taken to host memory and put back through uploads,
        // which the recorder sees: the stream then replays the run that missed, the uploads that undo it, the tile
        // uploads and the run that counts.
        auto staging=gpu.buffer(arena_bytes,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
        wgpuCommandEncoderCopyBufferToBuffer(commands(),arena,0,staging,0,arena_bytes);flush();
        tile_snapshot_host.resize(arena_bytes);gpu.read(staging,tile_snapshot_host.data(),arena_bytes);
      }else{
        if(tile_snapshot_bytes!=arena_bytes){flush();tile_snapshot=gpu.buffer(arena_bytes,WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);tile_snapshot_bytes=arena_bytes;}
        tile_copy(arena,tile_snapshot);
      }
    });
    if(have_error())return;
    kernel();
    if(have_error())return;
    bool miss=false;
    guard([&]{if(!tile_miss_word)return;uint32_t word=0;download(tile_miss_word,&word,4);miss=word!=0;});
    if(have_error()||!miss)return;
    tile_misses++;
    // Not under the device lock: the loader's threads allocate and upload pool images through this device.
    image_load_requested_gpu(queue);
    if(have_error())return;
    guard([&]{
      require(round<256,"WebGPU tiled images: a kernel kept requesting tiles after 256 loads");
      // Only what is still the allocation it was: the kernel and the loader may have freed, grown or allocated memory
      // (a freed range can hold a new pool image by now).
      std::erase_if(tile_snapshot_ranges,[&](const auto &range){auto it=allocations.find(range.first);
        return it==allocations.end()||it->second.extent!=range.second||!tile_written(range.first);});
      if(observer){
        require(tile_snapshot_host.size()<=arena_bytes,"Invalid tile snapshot");
        for(const auto &[address,extent]:tile_snapshot_ranges){const size_t bytes=allocations.at(address).bytes;
          require(address+bytes<=tile_snapshot_host.size(),"Invalid tile snapshot range");upload(address,tile_snapshot_host.data()+address,bytes);}
      }else{require(tile_snapshot_bytes<=arena_bytes,"Invalid tile snapshot");tile_copy(tile_snapshot,arena);}
      zero(tile_miss_word,4);tile_restarts++;
      // Diagnostic (maintainers): one line per restart.
      if(std::getenv("CODEBRIX_CYCLES_TILE_LOG"))std::fprintf(stderr,"codebrix tile cache: restart %llu of %s (round %u, %zu ranges put back)\n",(unsigned long long)tile_restarts,name,round,tile_snapshot_ranges.size());
    });
    if(have_error())return;
  }
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
