// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "device/device.h"
#include "device/queue.h"
#include "kernel/integrator/state.h"
#include "webgpu_device.h"
#include <array>
#include <map>
#include <mutex>
#include <vector>

namespace ccl {
std::string controls_wgsl();
// Optional test recorder: observes commands without replacing renderer work.
struct WebGPUCommandObserver {
  virtual ~WebGPUCommandObserver()=default;
  virtual void upload(uint32_t,const void*,size_t)=0;
  virtual void clear(uint32_t,size_t)=0;
  virtual void dispatch(const std::array<uint32_t,32>&,uint32_t,unsigned)=0;
  virtual void readback(uint32_t,const void*,size_t)=0;
};
// Virtual byte addresses preserve Cycles' device_ptr arithmetic without exposing
// CPU pointers to WGSL. This first arena is limited by one storage binding.
class WebGPUDevice final:public Device {
 public:
  struct Allocation {size_t bytes,extent;};
  cbc::Device gpu;
  WebGPUCommandObserver *observer=nullptr;
  cbc::Buffer arena;
  cbc::Handle<WGPUCommandEncoder,wgpuCommandEncoderRelease> encoder;
  std::recursive_mutex mutex;
  std::map<uint32_t,Allocation> allocations;
  std::map<uint32_t,size_t> free_ranges;
  std::map<std::string,uint32_t> globals,constants;
  size_t arena_bytes=0;
  uint64_t dispatches=0,submissions=0,growths=0,uploads=0,downloads=0;
  KernelData data{};
  IntegratorStateGPU state{};
  uint32_t fields=0,field_count=0,error_word=0;
  cbc::BindLayout bind_layout;
  cbc::PipelineLayout pipeline_layout;
  cbc::Pipeline controls;
  cbc::Pipeline camera;
  cbc::Pipeline closest;
  cbc::Pipeline background;
  cbc::Pipeline surface;
  uint32_t surface_closures=0;size_t surface_closure_bytes=0;
  std::map<uint32_t,device_image*> images;
  bool image_info_dirty=false;
  uint32_t image_info_address=0,image_bounds_address=0;
  void load_image_info();
  std::vector<KernelImageInfo> reference_image_info() const;
  uint32_t intersection_tables=0;
  std::vector<uint32_t> intersection_table_cache;
  WebGPUDevice(const DeviceInfo&,Stats&,Profiler&,bool);
  ~WebGPUDevice() override;
  BVHLayoutMask get_bvh_layout_mask(uint) const override{return BVH_LAYOUT_BVH2;}
  unique_ptr<DeviceQueue> gpu_queue_create() override;
  void const_copy_to(const char*,void*,size_t) override;
  void mem_alloc(device_memory&) override;
  void mem_copy_to(device_memory&) override;
  void mem_copy_from(device_memory&,size_t,size_t,size_t,size_t) override;
  void mem_zero(device_memory&) override;
  void mem_free(device_memory&) override;
  void mem_move_to_host(device_memory&) override;
  uint32_t allocate(size_t);
  void release(uint32_t);
  void validate(uint64_t,size_t) const;
  size_t remaining(uint64_t) const;
  void upload(uint32_t,const void*,size_t);
  void download(uint32_t,void*,size_t);
  void zero(uint32_t,size_t);
  WGPUCommandEncoder commands();
  void flush();
  void sync();
  void dispatch(const std::array<uint32_t,32>&,uint32_t,WGPUComputePipeline=nullptr);
  void enqueue_camera(int,const DeviceKernelArguments&);
  void enqueue_closest(int,const DeviceKernelArguments&);
  void enqueue_background(int,const DeviceKernelArguments&);
  void enqueue_surface(int,const DeviceKernelArguments&);
  void update_state(const IntegratorStateGPU&);
  uint32_t scene_tables();
  template<class F>void guard(F f){std::lock_guard lock(mutex);try{gpu.check();f();}catch(const std::exception &e){set_error(e.what());}}
};
unique_ptr<Device> device_webgpu_create(const DeviceInfo&,Stats&,Profiler&,bool);
void device_webgpu_info(vector<DeviceInfo>&);
}
