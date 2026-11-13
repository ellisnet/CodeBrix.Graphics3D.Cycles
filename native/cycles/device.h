// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "device/device.h"
#include "device/queue.h"
#include "kernel/integrator/state.h"
#include "webgpu_device.h"
#include "pipelines.h"
#include "device_tier.h"
#include "pipeline_cache.h"
#include <array>
#include <cstring>
#include <map>
#include <span>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace ccl {
struct WebGPUFingerprint;
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
  cbc::Device gpu{pipeline_cache_wanted()};
  WebGPUCommandObserver *observer=nullptr;
  // Fingerprint mode (fingerprint.h); null unless CODEBRIX_CYCLES_FINGERPRINT is set.
  WebGPUFingerprint *fingerprint=nullptr;
  cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> fingerprint_stub;
  void fingerprint_kernel_begin(DeviceKernel);
  void fingerprint_kernel_end(DeviceKernel);
  bool fingerprint_readback(uint32_t,void*,size_t);
  // Tables the pinned scene code leaves partly unset (union members of another light type, padding) hold whatever the
  // allocator returned. The kernels cannot read those bytes; fingerprint mode zeroes the block so that they cannot change the hash.
  // Table memory starts cleared, so that padding and the union members of another light type upload the same bytes in every
  // run (native/DEVICE.md 55; the fingerprint hashed them cleared already).
  void *host_alloc(const MemoryType type,const size_t size) override{void *p=Device::host_alloc(type,size);if(p)std::memset(p,0,size);return p;}
  bool fingerprint_module(const char*,const char*,ShaderFeatures,const std::string&,std::vector<cbc::Pipeline>&);
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
  // The lamps: the first integrator.num_lights records. Background light portals follow them (portal_offset), with only
  // their area geometry set by the pinned LightManager (AreaLight::copy_to_kernel returns before the shader fields).
  std::span<const KernelLight> lamp_records() const{
    return std::span<const KernelLight>(light_records.data(),std::min(light_records.size(),size_t(std::max(data.integrator.num_lights,0))));}
  std::vector<KernelLightDistribution> light_distribution_records;
  // Light tree host copies (validate_light_tree; lighting.cpp).
  std::vector<KernelLightTreeNode> light_tree_nodes;
  std::vector<KernelLightTreeEmitter> light_tree_emitters;
  std::vector<KernelShader> shader_records;
  std::vector<KernelObject> object_records;
  std::vector<uint> object_flag_records;
  // The object flag table's Cycles memory: ObjectManager::apply_static_transforms sets SD_OBJECT_TRANSFORM_APPLIED in it
  // without an upload, ahead of the curve transparency evaluation (shadow.cpp).
  device_memory *object_flag_memory=nullptr;
  // Curve records (shader ids and shapes for the census; closest.cpp validate_curves).
  std::vector<KernelCurve> curve_records;
  // Point shaders (points_shader words, for the census).
  std::vector<uint32_t> point_shader_words;
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
  uint64_t unsupported_generation=~uint64_t(0);ShaderFeatures principled_features;ShaderFeatures attribute_features;ShaderFeatures displacement_features;ShaderFeatures displacement_program_features;std::string unsupported_feature;
  std::vector<uint32_t> tri_shader_words; // per-triangle shader words (Principled census roots)
  // BVH primitive -> triangle and object (the MNEE caster census, mnee_unused_svm_features); mnee_generation counts
  // their uploads, and the census is kept per (svm_generation, mnee_generation).
  std::vector<int> prim_index_words,prim_object_words;uint64_t mnee_generation=0,mnee_census_key[2]={~uint64_t(0),~uint64_t(0)};ShaderFeatures mnee_unused;
  // A local traversal from a transform-applied object beside instance leaves (SF_LOCAL_INSTANCE_SKIP), kept per
  // (svm_generation, mnee_generation).
  uint64_t local_skip_key[2]={~uint64_t(0),~uint64_t(0)};bool local_skip=false;
  // surface_unused_svm_features cache (svm_generation counts tri_shader uploads too).
  uint64_t surface_census_key=~uint64_t(0);ShaderFeatures surface_unused;
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
  PipelineDiskCache pipeline_disk_cache;
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
  // The scene's SVM node regions no triangle shader reaches (prepare_surface evaluates only those shaders).
  ShaderFeatures surface_unused_svm_features();
  // The scene's SVM handler regions the displacement programs never reach (shader_eval_displace).
  ShaderFeatures displacement_unused_svm_features();
  // The scene's SVM handler regions the caustic casters' shaders never reach (mnee_eval and mnee_contribute
  // evaluate only those shaders).
  ShaderFeatures mnee_unused_svm_features();
  std::string pipeline_report_json() const;
  std::string adapter_report_json() const;
  void validate_geometry_motion() const;
  void validate_curves();
  void validate_points();
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
  // DEVICE_KERNEL_INTEGRATOR_INIT_FROM_BAKE (bake.cpp, wavefront_bake.wgsl).
  cbc::Pipeline bake;
  cbc::Pipeline closest;
  cbc::Pipeline shadow_intersect;
  // intersect_shadow_all: transparent shadows with curves (wavefront_shadow.wgsl); replay index 58.
  cbc::Pipeline shadow_intersect_all;
  cbc::Pipeline shadow_transparent;
  cbc::Pipeline shadow_shade;
  cbc::Pipeline shadow_shader_data;
  cbc::Pipeline shadow_curve_eval;
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
  // Volume direct light (volume.cpp, VOLUME_LIGHT): the light segment and light entries of the surface module
  // and their per-input records.
  cbc::Pipeline volume_segment,volume_light,volume_tree_select,volume_light_triangle;
  // Background light portals: the background light's sample of the volume direct light (volume.cpp).
  cbc::Pipeline volume_light_background;
  // The indirect phase scatter with phase functions beyond Henyey-Greenstein (volume module, VOLUME_PHASES).
  cbc::Pipeline volume_phase;
  // Paths through heterogeneous volumes (volume module, VOLUME_HETEROGENEOUS): the integration's steps and its shader
  // evaluations alternate between two entries through the per-input split records (volume.cpp).
  cbc::Pipeline volume_heterogeneous;
  cbc::Pipeline volume_heterogeneous_eval;uint32_t volume_split_records=0;size_t volume_split_size=0;
  cbc::Pipeline volume_ray_marching;
  // INTERSECT_SUBSURFACE (subsurface.cpp): the walk entry and its traversal stacks.
  cbc::Pipeline subsurface_walk;uint32_t subsurface_stack=0;size_t subsurface_stack_bytes=0;
  cbc::Pipeline subsurface_disk;
  // The volume stack update after subsurface_scatter (SUBSURFACE_VOLUME, the volume stack module).
  cbc::Pipeline subsurface_volume_stack;
  // The moving objects' transforms for both subsurface entries (MOTION_OBJECT).
  cbc::Pipeline subsurface_motion;
  uint32_t volume_light_records=0;size_t volume_light_record_size=0;
  cbc::Pipeline surface;
  cbc::Pipeline surface_geometry;
  cbc::Pipeline surface_prepare;
  // The node regions left out of surface_prepare (surface_unused_svm_features; none = the shared surface build).
  ShaderFeatures surface_prepare_removed;
  cbc::Pipeline surface_light;
  // Emissive-triangle half of the light stage (LIGHT_TRIANGLE scenes only; surface.cpp).
  cbc::Pipeline surface_light_triangle;
  // shade_surface_light_background: the background light's sample after shade_surface_light (sun guiding;
  // SF_BACKGROUND_SAMPLE_ENTRY, wavefront_surface.wgsl).
  cbc::Pipeline surface_light_background;
  // Whether the surface light entries come from the module variant with shade_surface_light_background (SF_BACKGROUND_SAMPLE_ENTRY):
  // sun guiding, background light portals, and a background light beside area lights, whose two samplers inlined into
  // shade_surface_light passed the pipeline size guard (native/DEVICE.md 127). The capture records the same variant.
  bool surface_background_stage(){const ShaderFeatures f=shader_features();
    return (f&(SF_BACKGROUND_SUN|SF_BACKGROUND_PORTAL)).any()||((f&SF_LIGHT_BACKGROUND).any()&&(f&SF_LIGHT_AREA).any());}
  cbc::Pipeline surface_light_select;
  // BSDF sample stage of scenes with the remaining SVM BSDF families (surface.cpp).
  cbc::Pipeline surface_sample;
  // The Huang hair closure's evaluation and sample (wavefront_hair_huang.wgsl), before the entries that read them.
  cbc::Pipeline surface_huang_eval,surface_huang_sample;
  // Light tree forward pdfs (background module) and their per-path scratch.
  cbc::Pipeline light_tree_forward;uint32_t light_tree_forward_scratch=0;size_t light_tree_forward_bytes=0;
  cbc::Pipeline surface_connect;
  // DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE: the surface module specialized
  // with SVM_NODE_RAYTRACE (AO node traversal); its other stages are shared.
  cbc::Pipeline surface_prepare_raytrace;
  uint32_t surface_closures=0;size_t surface_closure_bytes=0;
  // A replay capture (scene_capture.h) records a fixed arena: the surface stage's records are then sized for the path
  // capacity, not for the largest dispatch so far, which can grow after the capture starts as the scheduler refills paths.
  bool capture_sizing=false;
  // Shadow catcher split (shadow_catcher.cpp): per-path marks and the split module.
  cbc::Pipeline sc_assign,sc_split,sc_count;
  uint32_t sc_marks=0;size_t sc_mark_bytes=0;
  void validate_shadow_catcher() const;
  // Film data passes (film_passes.cpp): the accepted pass mask and its refusal;
  // shade_surface_passes, the surface stage's film_write_data_passes entry.
  void validate_film_passes() const;
  void film_motion_words(std::array<uint32_t,32> &p) const;
  bool film_motion_vectors() const;
  bool film_surface_passes() const;
  bool film_cryptomatte() const;
  cbc::Pipeline surface_passes;
  // connect_surface_ao: the AO shadow ray of fast GI's additive mode and the AO pass, after
  // connect_surface_light (AO_SHADOW regions, wavefront_surface.wgsl).
  cbc::Pipeline surface_ao;
  // DEVICE_KERNEL_CRYPTOMATTE_POSTPROCESS (film_passes.cpp, wavefront_film_post.wgsl).
  cbc::Pipeline film_post;
  void enqueue_cryptomatte_postprocess(int,const DeviceKernelArguments&);
  // DEVICE_KERNEL_ADAPTIVE_SAMPLING_CONVERGENCE_CHECK, _FILTER_X and _FILTER_Y (film_passes.cpp,
  // wavefront_film_adaptive.wgsl).
  cbc::Pipeline film_adaptive_check,film_adaptive_x,film_adaptive_y;
  // DEVICE_KERNEL_VOLUME_GUIDING_FILTER_X and _Y (film_passes.cpp, wavefront_film_volume_guiding.wgsl).
  cbc::Pipeline film_volume_x,film_volume_y;
  void enqueue_volume_guiding(DeviceKernel,int,const DeviceKernelArguments&);
  void enqueue_adaptive_sampling(DeviceKernel,int,const DeviceKernelArguments&);
  // The sampling pattern's table for push word 9 (camera.cpp): tabulated Sobol's sample_pattern_lut, or 0.
  uint32_t sample_pattern_table(const char *stage);
  uint32_t shadow_catcher_marks(uint32_t capacity);
  void shadow_catcher_pipelines();
  void split_shadow_catcher(uint32_t capacity);
  void enqueue_shadow_catcher_count(int,const DeviceKernelArguments&);
  // Manifold next-event estimation: the seven INTERSECT_MNEE entries, the surface
  // stage's cleanup entry and their GPU-private per-path records.
  static constexpr unsigned mnee_entries=7;
  std::array<cbc::Pipeline,mnee_entries> mnee_pipelines;cbc::Pipeline mnee_cleanup,mnee_select;
  // The spot and area lights' sample and light update (MNEE_LIGHT_STAGES).
  cbc::Pipeline mnee_light_sample,mnee_light_update;
  // The walk's projection entry (MNEE_WALK_STAGES).
  cbc::Pipeline mnee_project;
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
  BVHLayoutMask get_bvh_layout_mask(uint64_t) const override{return BVH_LAYOUT_BVH2;}
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
  // The 32 push words of a dispatch. Push word 31 is reserved: no dispatch sets it, since the Sobol-Burley samplers read it
  // as an always-zero word (wavefront_common.wgsl sb_unit) that keeps their products from being contracted.
  void dispatch(const std::array<uint32_t,32>&,uint32_t,WGPUComputePipeline=nullptr,uint32_t workgroup_size=64);
  void enqueue_camera(int,const DeviceKernelArguments&);
  void enqueue_bake(int,const DeviceKernelArguments&);
  void enqueue_closest(int,const DeviceKernelArguments&);
  void enqueue_shadow(DeviceKernel,int,const DeviceKernelArguments&);
  void enqueue_background(int,const DeviceKernelArguments&,DeviceKernel=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND);
  void enqueue_background_eval(int,const DeviceKernelArguments&);
  void enqueue_curve_shadow_eval(int work,const DeviceKernelArguments &args);
  void enqueue_displace(int,const DeviceKernelArguments&);
  void enqueue_light_nee(int,const DeviceKernelArguments&);
  void enqueue_volume_density(int,const DeviceKernelArguments&);
  void enqueue_volume_stack(int,const DeviceKernelArguments&);
  void enqueue_volume(int,const DeviceKernelArguments&);
  void enqueue_surface(int,const DeviceKernelArguments&,DeviceKernel=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE);
  // DEVICE_KERNEL_INTEGRATOR_INTERSECT_MNEE (mnee.cpp, wavefront_mnee.wgsl).
  void enqueue_mnee(int,const DeviceKernelArguments&);
  void enqueue_subsurface(int,const DeviceKernelArguments&);void validate_subsurface();
  void validate_mnee() const;
  void validate_shadow_linking() const;
  // Shadow linking (shadow_linking.cpp): DEVICE_KERNEL_INTEGRATOR_INTERSECT_DEDICATED_LIGHT and SHADE_DEDICATED_LIGHT.
  cbc::Pipeline dedicated_intersect,dedicated_lamp,dedicated_tree,dedicated_shade;
  uint32_t dedicated_records=0;size_t dedicated_record_bytes=0;
  void enqueue_dedicated_light(DeviceKernel,int,const DeviceKernelArguments&);
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
