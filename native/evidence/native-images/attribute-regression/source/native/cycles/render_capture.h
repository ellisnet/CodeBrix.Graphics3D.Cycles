// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "device.h"
#include "wavefront.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
// This recorder saves the *actual* native scheduler's writes, dispatches and
// scheduling readbacks. Replay must validate every readback that determined
// subsequent native scheduling; it may not inject those CPU decisions as data.
struct RenderCapture final:ccl::WebGPUCommandObserver {
  ccl::WebGPUDevice &device;std::ofstream commands;size_t arena_size;uint64_t count=0;
  static void file(const std::string &name,const void *data,size_t bytes){std::ofstream out(name,std::ios::binary);out.write(static_cast<const char*>(data),bytes);if(!out)throw std::runtime_error("Capture write failed: "+name);}
  void header(uint32_t kind,uint32_t address,uint32_t bytes){uint32_t h[]={kind,address,bytes};commands.write(reinterpret_cast<const char*>(h),sizeof(h));count++;}
  RenderCapture(ccl::WebGPUDevice &d,const std::string &prefix):device(d),commands(prefix+".commands.bin",std::ios::binary),arena_size(d.arena_bytes){
    auto staging=d.gpu.buffer(arena_size,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);wgpuCommandEncoderCopyBufferToBuffer(d.commands(),d.arena,0,staging,0,arena_size);d.flush();std::vector<uint8_t> bytes(arena_size);d.gpu.read(staging,bytes.data(),bytes.size());file(prefix+".arena.bin",bytes.data(),bytes.size());
    std::filesystem::path out=std::filesystem::path(prefix).parent_path();
    std::ofstream(out/"controls.wgsl")<<ccl::controls_wgsl();std::ofstream(out/"camera.wgsl")<<ccl::camera_wgsl();std::ofstream(out/"closest.wgsl")<<ccl::closest_wgsl();std::ofstream(out/"background.wgsl")<<ccl::background_wgsl();std::ofstream(out/"surface.wgsl")<<ccl::surface_source();
    device.observer=this;
  }
  ~RenderCapture(){device.observer=nullptr;}
  void upload(uint32_t address,const void *data,size_t bytes)override{header(1,address,bytes);commands.write(static_cast<const char*>(data),bytes);}
  void clear(uint32_t address,size_t bytes)override{header(2,address,bytes);}
  void dispatch(const std::array<uint32_t,32>&params,uint32_t work,unsigned entry)override{header(3,entry,work);commands.write(reinterpret_cast<const char*>(params.data()),sizeof(params));}
  void readback(uint32_t address,const void *data,size_t bytes)override{header(4,address,bytes);commands.write(static_cast<const char*>(data),bytes);}
  void finish(const std::string &prefix,uint32_t film,size_t film_bytes,uint32_t queued,unsigned capacity){
    device.observer=nullptr;commands.close();if(!commands)throw std::runtime_error("Capture command write failed");
    if(device.arena_bytes!=arena_size)throw std::runtime_error("Capture arena grew; prewarm the complete workload before recording");
    std::ofstream meta(prefix+".capture.json");meta<<"{\"format\":\"native-command-stream-v1\",\"commands\":"<<count<<",\"arenaBytes\":"<<arena_size<<",\"filmOffset\":"<<film<<",\"filmBytes\":"<<film_bytes<<",\"errorOffset\":"<<device.error_word<<",\"queuedKernelOffset\":"<<queued<<",\"queuedKernelElementBytes\":2,\"pathCapacity\":"<<capacity<<",\"width\":64,\"height\":48,\"samples\":8,\"immutableSceneSpans\":[";
    bool first=true;for(auto &[name,address]:device.globals)if(address){if(!first)meta<<',';first=false;meta<<"{\"name\":\""<<name<<"\",\"offset\":"<<address<<",\"bytes\":"<<device.remaining(address)<<'}';}
    if(!first)meta<<',';auto data=device.constants.at("data");meta<<"{\"name\":\"KernelData\",\"offset\":"<<data<<",\"bytes\":"<<device.remaining(data)<<"}]}\n";
    if(!meta)throw std::runtime_error("Capture metadata write failed");
  }
};
