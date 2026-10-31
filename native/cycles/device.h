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
  // Light tree host copies (validate_light_tree; lighting.cpp).
  std::vector<KernelLightTreeNode> light_tree_nodes;
  std::vector<KernelLightTreeEmitter> light_tree_emitters;
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
  uint64_t svm_generation=0,census_generation=~uint64_t(0);ShaderFeatures enabled_features;
  SvmCensus census;
  // Principled features refused at upload (pipelines.cpp shader_features).
  uint64_t unsupported_generation=~uint64_t(0);ShaderFeatures principled_features;ShaderFeatures displacement_features;ShaderFeatures displacement_program_features;std::string unsupported_feature;
  std::vector<uint32_t> tri_shader_words; // per-triangle shader words (Principled census roots)
  bool specialization_enabled=true; // false only for SpecializationMode::Off
  SpecializationMode specialization_mode=SpecializationMode::Scene;
  // Superset mode (pipelines.h): requested group, the scene features the groups
  // were computed for, and the groups.
  unsigned superset_group=0;ShaderFeatures superset_scene=~SF_NONE;SupersetGroups superset;
  std::string superset_json() const;
  struct PipelineRecord {std::string module,entry;ShaderFeatures features;size_t wgsl_bytes,inlined_bytes;double module_ms,pipeline_ms;};
  std::vector<PipelineRecord> pipeline_records;
  std::map<std::string,ShaderFeatures> pipeline_features;
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
  // Pipeline-build size guard (pipelines.h): threshold read at construction,
  // warnings (reported with pipeline_warnings, never "degraded") and the largest entry.
  size_t size_warning_bytes=pipeline_size_warning_bytes(),max_inlined_bytes=0;std::string max_inlined_entry;
  std::vector<std::string> size_warnings;
  std::map<std::string,uint32_t> pipeline_workgroups;
  uint32_t workgroup(const char *entry) const;
  ShaderFeatures shader_features();
  std::string module_wgsl(const std::string &source);
  // removed: scene regions an entry never reaches (raytrace_unused_svm_features).
  bool pipeline_current(const cbc::Pipeline &pipeline,const char *entry,ShaderFeatures removed=SF_NONE);
  // extra: regions compiled in beyond the scene features (the AO node's
  // traversal for the raytrace surface queue, SVM_NODE_RAYTRACE); removed:
  // scene regions left out of these entries.
  std::vector<cbc::Pipeline> build_pipelines(const char *module,const std::string &source,std::initializer_list<const char*> entries,ShaderFeatures extra=SF_NONE,ShaderFeatures removed=SF_NONE);
  // SVM node and closure regions of the scene that no SD_HAS_RAYTRACE shader
  // reaches (prepare_surface_raytrace evaluates only those shaders).
  ShaderFeatures raytrace_unused_svm_features();
  // The scene's SVM handler regions the displacement programs never reach (shader_eval_displace).
  ShaderFeatures displacement_unused_svm_features();
  std::string pipeline_report_json() const;
  std::string adapter_report_json() const;
  void validate_geometry_motion() const;
  void validate_lighting(bool sampling) const;
  void validate_light_tree() const;
  void validate_volume() const;
  // Ambient occlusion shadow paths (shadow.cpp; surface stage ray setup in surface.cpp).
  void validate_ao_shadow(bool shading,uint32_t capacity) const;
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
  // DEVICE_KERNEL_SHADER_EVAL_BACKGROUND (background module) and
  // DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_NEE (surface module, wavefront_light_nee.wgsl).
  cbc::Pipeline background_eval;
  // DEVICE_KERNEL_SHADER_EVAL_DISPLACE (surface module, SHADER_EVAL_DISPLACE: true displacement).
  cbc::Pipeline surface_displace;
  cbc::Pipeline light_nee,light_nee_prepare;
  uint32_t light_nee_cache=0;size_t light_nee_cache_bytes=0;
  cbc::Pipeline volume;
  cbc::Pipeline volume_stack;
  cbc::Pipeline volume_density;
  uint32_t volume_closures=0;size_t volume_closure_bytes=0;
  cbc::Pipeline surface;
  cbc::Pipeline surface_geometry;
  cbc::Pipeline surface_prepare;
  cbc::Pipeline surface_light;
  // Emissive-triangle half of the light stage (LIGHT_TRIANGLE scenes only; surface.cpp).
  cbc::Pipeline surface_light_triangle;
  cbc::Pipeline surface_light_select;
  // BSDF sample stage of scenes with the remaining SVM BSDF families (surface.cpp).
  cbc::Pipeline surface_sample;
  // Light tree forward pdfs (background module) and their per-path scratch.
  cbc::Pipeline light_tree_forward;uint32_t light_tree_forward_scratch=0;size_t light_tree_forward_bytes=0;
  cbc::Pipeline surface_connect;
  // DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE: the surface module specialized
  // with SVM_NODE_RAYTRACE (AO node traversal); its other stages are shared.
  cbc::Pipeline surface_prepare_raytrace;
  uint32_t surface_closures=0;size_t surface_closure_bytes=0;
  // Shadow catcher split (shadow_catcher.cpp): per-path marks and the split module.
  cbc::Pipeline sc_assign,sc_split,sc_count;
  uint32_t sc_marks=0;size_t sc_mark_bytes=0;
  void validate_shadow_catcher() const;
  uint32_t shadow_catcher_marks(uint32_t capacity);
  void shadow_catcher_pipelines();
  void split_shadow_catcher(uint32_t capacity);
  void enqueue_shadow_catcher_count(int,const DeviceKernelArguments&);
  // Manifold next-event estimation: the seven INTERSECT_MNEE entries, the surface
  // stage's cleanup entry and their GPU-private per-path records.
  static constexpr unsigned mnee_entries=7;
  std::array<cbc::Pipeline,mnee_entries> mnee_pipelines;cbc::Pipeline mnee_cleanup;
  uint32_t mnee_records=0;size_t mnee_record_bytes_allocated=0;
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
  void enqueue_background_eval(int,const DeviceKernelArguments&);
  void enqueue_displace(int,const DeviceKernelArguments&);
  void enqueue_light_nee(int,const DeviceKernelArguments&);
  void enqueue_volume_density(int,const DeviceKernelArguments&);
  void enqueue_volume_stack(int,const DeviceKernelArguments&);
  void enqueue_volume(int,const DeviceKernelArguments&);
  void enqueue_surface(int,const DeviceKernelArguments&,DeviceKernel=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE);
  // DEVICE_KERNEL_INTEGRATOR_INTERSECT_MNEE (mnee.cpp, wavefront_mnee.wgsl).
  void enqueue_mnee(int,const DeviceKernelArguments&);
  void validate_mnee() const;
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
