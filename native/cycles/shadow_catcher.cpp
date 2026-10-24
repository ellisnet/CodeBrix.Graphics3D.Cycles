// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Shadow catcher on the WebGPU device (pinned Cycles kernel/integrator/shadow_catcher.h,
// intersect_closest.h integrator_split_shadow_catcher, film/light_passes.h and
// integrator/path_trace_work_gpu.cpp has_shadow_catcher): the shared validation of
// the three shadow catcher film passes, the WGSL constants of the SHADOW_CATCHER
// specialization region (wavefront_shadow_catcher.wgsl) and the split and count
// dispatches (wavefront_shadow_catcher_split.wgsl).
#include "device.h"
#include "wavefront.h"
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace ccl {
static constexpr char shadow_catcher_split_fragment[]=
#include "shadow_catcher_shader.inc"
;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int shadow_catcher_pass_mask(){return PASSMASK(SHADOW_CATCHER)|PASSMASK(SHADOW_CATCHER_SAMPLE_COUNT)|PASSMASK(SHADOW_CATCHER_MATTE);}
static void flag_constants(std::ostringstream &out){
  auto constant=[&](const char *name,uint32_t value){out<<"const "<<name<<":u32="<<value<<"u;\n";};
  constant("SC_PATH_HIT",PATH_RAY_SHADOW_CATCHER_HIT);constant("SC_PATH_PASS",PATH_RAY_SHADOW_CATCHER_PASS);
  constant("SC_PATH_BACKGROUND",PATH_RAY_SHADOW_CATCHER_BACKGROUND);constant("SC_TRANSPARENT_BACKGROUND",PATH_RAY_TRANSPARENT_BACKGROUND);
  constant("SC_K_BACKGROUND",DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND);constant("SC_K_SURFACE",DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE);
}
// Constants of the SHADOW_CATCHER region; the markers make modules of scenes
// without a shadow catcher identical to modules without this layout.
std::string shadow_catcher_layout(){
  std::ostringstream out;out<<"//#if SHADOW_CATCHER\n";
  auto constant=[&](const char *name,size_t value){out<<"const "<<name<<":u32="<<value<<"u;\n";};
#define DATA(parent,member) constant("SC_D_" #member,offsetof(KernelData,parent.member))
  DATA(integrator,has_shadow_catcher);DATA(film,pass_shadow_catcher);DATA(film,pass_shadow_catcher_sample_count);
  DATA(film,pass_shadow_catcher_matte);DATA(film,pass_flag);DATA(film,pass_alpha_threshold);
#undef DATA
  constant("SC_D_pass_background",offsetof(KernelData,film.pass_background));
  constant("SC_D_background_transparent",offsetof(KernelData,background.transparent));
  flag_constants(out);
  constant("SC_SINGLE_PASS_DONE",PATH_RAY_SINGLE_PASS_DONE);constant("SC_OBJECT",SD_OBJECT_SHADOW_CATCHER);
  constant("SC_HOLDOUT_MASK",SD_OBJECT_HOLDOUT_MASK);constant("SC_EXCLUDE",SHADER_EXCLUDE_SHADOW_CATCHER);
  constant("SC_SR_TRANSPARENT",SR_TRANSPARENT);constant("SC_SR_RAY_PORTAL",SR_RAY_PORTAL);
  constant("SC_FLAGS",PATH_RAY_SHADOW_CATCHER_HIT|PATH_RAY_SHADOW_CATCHER_PASS|PATH_RAY_SHADOW_CATCHER_BACKGROUND);
  out<<"//#endif\n";return out.str();
}
// Every stage that can see a shadow catcher path or write its passes calls this:
// the three passes (3, 1 and 4 floats) inside the film record, present whenever
// the scene has a shadow catcher; features the shadow catcher path does not
// implement yet are refused explicitly.
void WebGPUDevice::validate_shadow_catcher() const{
  auto pass=[&](int offset,int length){require(offset==PASS_UNUSED||(offset>=0&&offset<=data.film.pass_stride-length),"Invalid shadow catcher film pass offset");};
  pass(data.film.pass_shadow_catcher,3);pass(data.film.pass_shadow_catcher_sample_count,1);pass(data.film.pass_shadow_catcher_matte,4);
  if(!data.integrator.has_shadow_catcher)return;
  require(data.kernel_features&KERNEL_FEATURE_SHADOW_CATCHER,"Missing native shadow catcher kernel feature");
  require(data.film.pass_shadow_catcher!=PASS_UNUSED&&data.film.pass_shadow_catcher_sample_count!=PASS_UNUSED&&
          data.film.pass_shadow_catcher_matte!=PASS_UNUSED,"Missing shadow catcher film passes");
  require(!(data.kernel_features&KERNEL_FEATURE_VOLUME),"WebGPU shadow catcher with volumes not implemented");
  require(!data.integrator.use_caustics&&!(data.kernel_features&KERNEL_FEATURE_MNEE),"WebGPU shadow catcher with caustics not implemented");
  require(state.next_main_path_index!=nullptr,"Missing shadow catcher path index");
  validate(uintptr_t(state.next_main_path_index),4);
}
uint32_t WebGPUDevice::shadow_catcher_marks(uint32_t capacity){
  if(!data.integrator.has_shadow_catcher)return 0;
  const size_t needed=size_t(capacity)*8;
  if(needed>sc_mark_bytes){
    if(sc_marks){flush();release(sc_marks);sc_marks=0;sc_mark_bytes=0;}
    sc_marks=allocate(needed);sc_mark_bytes=needed;zero(sc_marks,needed);
  }
  return sc_marks;
}
std::string shadow_catcher_split_wgsl(){
  std::ostringstream out;flag_constants(out);
  const std::string controls=controls_wgsl();const auto entry=controls.find("@compute");
  require(entry!=std::string::npos,"Controls module header");
  return controls.substr(0,entry)+out.str()+shadow_catcher_split_fragment;
}
void WebGPUDevice::shadow_catcher_pipelines(){
  if(sc_assign.value&&sc_split.value&&sc_count.value)return;
  auto built=build_pipelines("shadow_catcher",shadow_catcher_split_wgsl(),{"shadow_catcher_assign","shadow_catcher_split","shadow_catcher_count"});
  sc_assign=std::move(built[0]);sc_split=std::move(built[1]);sc_count=std::move(built[2]);
}
// After intersect_closest: new path indices in ascending order, then the copies.
void WebGPUDevice::split_shadow_catcher(uint32_t capacity){
  if(!data.integrator.has_shadow_catcher)return;
  require(sc_marks&&sc_mark_bytes>=size_t(capacity)*8,"Missing shadow catcher marks");
  require(remaining(uintptr_t(state.path.queued_kernel))>=size_t(capacity)*2&&remaining(uintptr_t(state.path.flag))>=size_t(capacity)*4&&
          remaining(uintptr_t(state.path.shader_sort_key))>=size_t(capacity)*4,"Shadow catcher split exceeds path state");
  require(state.sort_key_counter[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE]&&state.sort_partition_divisor>0,"Missing shadow catcher sort counters");
  shadow_catcher_pipelines();
  std::array<uint32_t,32> p{};p[1]=capacity;p[2]=uint32_t(uintptr_t(state.path.queued_kernel));p[3]=uint32_t(uintptr_t(state.path.flag));
  p[4]=sc_marks;p[5]=uint32_t(uintptr_t(state.next_main_path_index));p[8]=uint32_t(uintptr_t(state.path.shader_sort_key));
  p[9]=uint32_t(uintptr_t(state.sort_key_counter[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE]));p[10]=uint32_t(remaining(p[9])/4);
  p[11]=uint32_t(data.max_shaders);p[12]=uint32_t(state.sort_partition_divisor);p[13]=uint32_t(uintptr_t(state.queue_counter));
  p[14]=fields;p[15]=field_count;p[17]=error_word;
  dispatch(p,1,sc_assign,workgroup("shadow_catcher_assign"));
  dispatch(p,capacity,sc_split,workgroup("shadow_catcher_split"));
}
// DEVICE_KERNEL_INTEGRATOR_SHADOW_CATCHER_COUNT_POSSIBLE_SPLITS: arguments are the
// work size and the counter (PathTraceWorkGPU::shadow_catcher_count_possible_splits).
void WebGPUDevice::enqueue_shadow_catcher_count(int work,const DeviceKernelArguments &args){
  require(args.count==2&&args.types[0]==DeviceKernelArguments::INT32&&args.sizes[0]==4,"Shadow catcher count arguments");
  int n;std::memcpy(&n,args.values[0],4);require(n>=0&&work>=n,"Incomplete shadow catcher count dispatch");
  require(args.types[1]==DeviceKernelArguments::POINTER&&args.sizes[1]==sizeof(device_ptr),"Invalid shadow catcher counter");
  device_ptr counter;std::memcpy(&counter,args.values[1],sizeof(counter));validate(counter,4);require(counter%4==0,"Unaligned shadow catcher counter");
  require(remaining(uintptr_t(state.path.queued_kernel))>=size_t(n)*2&&remaining(uintptr_t(state.path.flag))>=size_t(n)*4,"Shadow catcher count exceeds path state");
  if(!n)return;
  shadow_catcher_pipelines();
  std::array<uint32_t,32> p{};p[1]=uint32_t(n);p[2]=uint32_t(uintptr_t(state.path.queued_kernel));p[3]=uint32_t(uintptr_t(state.path.flag));
  p[5]=uint32_t(counter);p[17]=error_word;
  dispatch(p,uint32_t(n),sc_count,workgroup("shadow_catcher_count"));
}
}
