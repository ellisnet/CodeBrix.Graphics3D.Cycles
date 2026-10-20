// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "wavefront_test.h"
#include "wavefront.h"
#include <filesystem>
#include <fstream>

// Optional failing-stage diagnostic. The CPU expectation is written to a
// separate file; it is never uploaded or used to choose GPU paths.
struct SurfaceDispatchCapture final:ccl::WebGPUCommandObserver {
  ccl::WebGPUDevice &device;
  std::string prefix;
  std::array<uint32_t,32> params{};
  std::vector<uint8_t> before;
  uint32_t work=0,workgroup_size=0;

  static void file(const std::string &name,const void *data,size_t bytes){
    std::ofstream out(name,std::ios::binary);
    out.write(static_cast<const char*>(data),bytes);
    wavefront_test::require(bool(out),"Surface diagnostic write failed: "+name);
  }
  std::vector<uint8_t> snapshot(){
    auto staging=device.gpu.buffer(device.arena_bytes,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    wgpuCommandEncoderCopyBufferToBuffer(device.commands(),device.arena,0,staging,0,device.arena_bytes);
    device.flush();
    std::vector<uint8_t> data(device.arena_bytes);
    device.gpu.read(staging,data.data(),data.size());
    return data;
  }
  SurfaceDispatchCapture(ccl::WebGPUDevice &d,const std::string &name):device(d),prefix(name){
    wavefront_test::require(!device.observer,"Surface diagnostic observer already in use");
    std::filesystem::create_directories(std::filesystem::path(prefix).parent_path());
    device.observer=this;
  }
  ~SurfaceDispatchCapture(){if(device.observer==this)device.observer=nullptr;}
  void upload(uint32_t,const void*,size_t)override{}
  void clear(uint32_t,size_t)override{}
  void readback(uint32_t,const void*,size_t)override{}
  void dispatch(const std::array<uint32_t,32> &p,uint32_t n,unsigned entry,uint32_t group_size)override{
    if(entry!=4||!before.empty())return;
    params=p;work=n;workgroup_size=group_size;before=snapshot();
    file(prefix+".arena.bin",before.data(),before.size());
    file(prefix+".params.bin",params.data(),sizeof(params));
    std::ofstream(prefix+".wgsl")<<device.module_wgsl(ccl::surface_source());
  }
  void finish(const std::vector<wavefront_test::Field> &fields,
              const std::vector<ccl::IntegratorStateCPU> &expected,const std::vector<float> &film){
    using namespace ccl;
    device.observer=nullptr;
    wavefront_test::require(!before.empty(),"No surface dispatch was captured");
    auto actual=snapshot();auto reference=before;
    wavefront_test::require(actual.size()==reference.size(),"Surface diagnostic arena grew");
    std::ofstream meta(prefix+".capture.json");
    meta<<"{\"format\":\"surface-dispatch-diagnostic-v1\",\"entry\":\"shade_surface\",\"work\":"<<work
        <<",\"workgroupSize\":"<<workgroup_size<<",\"paths\":"<<params[1]<<",\"capacity\":"<<expected.size()<<",\"fields\":[";
    bool first=true;
    for(const auto &f:fields){
      for(size_t i=0;i<expected.size();i++)std::memcpy(reference.data()+f.address+i*f.stride,
          reinterpret_cast<const uint8_t*>(&expected[i])+f.offset,f.stride);
      if(!first)meta<<',';
      first=false;
      meta<<"{\"name\":\""<<f.name<<"\",\"offset\":"<<f.address<<",\"stride\":"<<f.stride
          <<",\"floating\":"<<(f.floating?"true":"false")<<'}';
    }
    std::memcpy(reference.data()+params[4],film.data(),film.size()*sizeof(float));
    IntegratorQueueCounter counters{};
    for(const auto &state:expected)if(state.path.queued_kernel)counters.num_queued[state.path.queued_kernel]++;
    auto counter=uintptr_t(device.state.queue_counter);
    std::memcpy(reference.data()+counter,&counters,sizeof(counters));
    meta<<"],\"filmOffset\":"<<params[4]<<",\"filmBytes\":"<<film.size()*sizeof(float)
        <<",\"counterOffset\":"<<counter<<",\"counterBytes\":"<<sizeof(counters)
        <<",\"scratchOffset\":"<<params[23]<<",\"scratchBytes\":"<<device.remaining(params[23])
        <<",\"errorOffset\":"<<device.error_word<<"}\n";
    file(prefix+".reference.bin",reference.data(),reference.size());
    file(prefix+".actual.bin",actual.data(),actual.size());
    wavefront_test::require(bool(meta),"Surface diagnostic metadata write failed");
  }
};
