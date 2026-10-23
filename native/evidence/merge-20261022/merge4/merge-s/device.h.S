// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "device/device.h"
#include "device/queue.h"
#include "kernel/integrator/state.h"
#include "webgpu_device.h"
#include "pipelines.h"
#include "device_tier.h"
#include <array>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace ccl {
std::string controls_wgsl();
// Optional test recorder: observes commands without replacing renderer work.
struct WebGPUCommandObserver {
  virtual ~WebGPUCommandObserver()=default;
  virtual void upload(uint32_t,const void*,size_t)=0;
  virtual void clear(uint32_t,size_t)=0;
  virtual void dispatch(const std::array<uint32_t,32>&,uint32_t,unsigned,uint32_t)=0;
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
  // Exact native table copies for capability validation; sampling remains on GPU.
  std::vector<KernelLight> light_records;
  std::vector<KernelLightDistribution> light_distribution_records;
  std::vector<KernelShader> shader_records;
  std::vector<KernelObject> object_records;
  std::vector<uint> object_flag_records;
  // Any uploaded triangle with SHADER_SMOOTH_NORMAL (shadow terminator geometry offset).
  bool smooth_triangles=false;
  // Scene-driven WGSL specialization (pipelines.h). svm_node_words is the exact
  // uploaded SVM program; svm_generation counts SVM, shader, light, light
  // distribution, object-flag and KernelData uploads (features also cover
  // lights, emissive triangles and motion).
  // enabled_features only grows during a device lifetime, so
  // a scene update that reaches a new handler recompiles affected pipelines.
  std::vector<uint32_t> svm_node_words;
  uint64_t svm_generation=0,census_generation=~uint64_t(0),enabled_features=0;
  SvmCensus census;
  // Principled features refused at upload (pipelines.cpp shader_features).
  uint64_t unsupported_generation=~uint64_t(0),principled_features=0;std::string unsupported_feature;
  std::vector<uint32_t> tri_shader_words; // per-triangle shader words (Principled census roots)
  bool specialization_enabled=true;
  struct PipelineRecord {std::string module,entry;uint64_t features;size_t wgsl_bytes;double module_ms,pipeline_ms;};
  std::vector<PipelineRecord> pipeline_records;
  std::map<std::string,uint64_t> pipeline_features;
  // Pipeline tier (pipelines.h). "full" by default; "conservative" (workgroup
  // size 1, same algorithm; last-bit differences observed on Linux Intel) only
  // after a non-fatal creation failure, when forced, or after a budget overrun
  // when budget_conservative is opted in. Budget overruns are always reported.
  // Dispatch sizes always follow the workgroup size the compiled module declares.
  bool conservative=false,budget_conservative=false;
  double pipeline_budget_ms=pipeline_default_budget_ms;
  // Tier selection before any compile (device_tier.h): explicit request,
  // persisted device record, known-adapter table, then full; optional canary.
  DeviceTierPolicy tier_policy;
  void select_tier();
  void run_canary();
  std::vector<std::string> pipeline_warnings;
  std::map<std::string,uint32_t> pipeline_workgroups;
  uint32_t workgroup(const char *entry) const;
  uint64_t shader_features();
  std::string module_wgsl(const std::string &source);
  bool pipeline_current(const cbc::Pipeline &pipeline,const char *entry);
  std::vector<cbc::Pipeline> build_pipelines(const char *module,const std::string &source,std::initializer_list<const char*> entries);
  std::string pipeline_report_json() const;
  void validate_geometry_motion() const;
  void validate_lighting(bool sampling) const;
  void validate_volume() const;
  IntegratorStateGPU state{};
  uint32_t fields=0,field_count=0,error_word=0;
  // SVM value stacks (wavefront_svm.wgsl): one slot of svm_stack_words words per
  // invocation of a dispatch; the header after the error word holds the region's
  // address and slot count; header word 3 stays 0 (the noise fade's ordering barrier,
  // wavefront_svm_nodes.wgsl svmn_order). Path kernels dispatch at most num_concurrent_states
  // (4,096) invocations; a larger SVM dispatch reports error 1.
  static constexpr uint32_t svm_stack_words=SVM_STACK_SIZE+(SVM_STACK_SIZE+31)/32,svm_stack_slots=4096;
  static_assert(svm_stack_words==263,"wavefront_svm.wgsl SVM_STACK_WORDS");
  uint32_t svm_stack_region=0;
  cbc::BindLayout bind_layout;
  cbc::PipelineLayout pipeline_layout;
  cbc::Pipeline controls;
  cbc::Pipeline camera;
  cbc::Pipeline closest;
  cbc::Pipeline shadow_intersect;
  cbc::Pipeline shadow_transparent;
  cbc::Pipeline shadow_shade;
  cbc::Pipeline shadow_shader_data;
  uint32_t shadow_shader_cache=0;size_t shadow_shader_cache_bytes=0;
  cbc::Pipeline background;
  cbc::Pipeline light_forward;
  cbc::Pipeline volume;
  cbc::Pipeline volume_stack;
  cbc::Pipeline volume_density;
  uint32_t volume_closures=0;size_t volume_closure_bytes=0;
  cbc::Pipeline surface;
  cbc::Pipeline surface_geometry;
  cbc::Pipeline surface_prepare;
  cbc::Pipeline surface_light;
  cbc::Pipeline surface_connect;
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
  void dispatch(const std::array<uint32_t,32>&,uint32_t,WGPUComputePipeline=nullptr,uint32_t workgroup_size=64);
  void enqueue_camera(int,const DeviceKernelArguments&);
  void enqueue_closest(int,const DeviceKernelArguments&);
  void enqueue_shadow(DeviceKernel,int,const DeviceKernelArguments&);
  void enqueue_background(int,const DeviceKernelArguments&,DeviceKernel=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND);
  void enqueue_volume_density(int,const DeviceKernelArguments&);
  void enqueue_volume_stack(int,const DeviceKernelArguments&);
  void enqueue_volume(int,const DeviceKernelArguments&);
  void enqueue_surface(int,const DeviceKernelArguments&);
  void update_state(const IntegratorStateGPU&);
  uint32_t scene_tables();
  // Host recovery (Phase E3, native/host ABI.md "Device errors"): clears a
  // reported device error when the WebGPU device itself is intact. Returns
  // false, with the reason, when the device has a fatal error (lost device,
  // uncaptured validation error); such a device stays failed.
  bool recover(std::string &reason);
  template<class F>void guard(F f){std::lock_guard lock(mutex);try{gpu.check();f();}catch(const std::exception &e){set_error(e.what());}}
};
unique_ptr<Device> device_webgpu_create(const DeviceInfo&,Stats&,Profiler&,bool);
// Gate binaries: when the WebGPU device cannot be created, the factory returns
// Cycles' dummy device carrying the reason (device_webgpu_create). Stop with a
// message that names the stage instead of dereferencing a failed cast; every
// gate's main reports the exception and exits non-zero.
inline WebGPUDevice &require_webgpu_device(Device *device,const char *stage){
  auto *webgpu=dynamic_cast<WebGPUDevice*>(device);
  if(!webgpu)throw std::runtime_error(std::string(stage)+": WebGPU device creation failed: "+
    (device&&device->have_error()?device->error_message():std::string("no device was returned")));
  return *webgpu;
}
// Gate probe modules (surface SVM, shader-data and image probes) compile the
// scene-specialized module, like every renderer pipeline (module_wgsl), never the
// whole unspecialized source: Intel Mesa 25.0.7 miscompiles the module-scope
// svm_stack inside the large unspecialized SVM probe module (normal_gate SVM
// error 8 after the materials merge; the withheld Gabor node hit the same class).
// CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0 still selects the unspecialized module.
inline std::string gate_probe_wgsl(WebGPUDevice &device,const std::string &source){return device.module_wgsl(source);}
void device_webgpu_info(vector<DeviceInfo>&);
}
