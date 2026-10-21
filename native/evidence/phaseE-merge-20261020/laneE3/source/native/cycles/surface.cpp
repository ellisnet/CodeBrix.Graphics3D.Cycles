// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "wavefront.h"
#include "surface_closure.h"
#include <cfloat>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace ccl {
static constexpr char surface_fragment[]=
#include "surface_shader.inc"
;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
// GPU-private record: ready word, 60 ShaderData words, 15 SVM metadata words,
// then the forward-hit triangle MIS weight and the 13-word sampled light (lamp
// or emissive triangle) passed from shade_surface_light to connect_surface_light.
// Its scalar encoding is in wavefront_surface_stage.wgsl and wavefront_lighting.wgsl.
static constexpr size_t surface_stage_bytes=surface_data_cache_bytes+30*sizeof(uint32_t);
static std::string surface_layout(){
  std::ostringstream source;
  auto constant=[&](const char *name,size_t value){source<<"const "<<name<<":u32="<<value<<"u;\n";};
  constant("SURFACE_STAGE_BYTES",surface_stage_bytes);
  // Preserve the originating native kernel's float3 division order. This is
  // encoded into captures too, independently of the GPU that replays them.
#ifdef __KERNEL_SSE__
  constant("GGX_NATIVE_RECIPROCAL_NORMALIZE",0);
#else
  constant("GGX_NATIVE_RECIPROCAL_NORMALIZE",1);
#endif
#define DATA(parent,member) constant("D_" #parent "_" #member,offsetof(KernelData,parent.member))
  DATA(integrator,max_bounce);
  DATA(integrator,max_diffuse_bounce);DATA(integrator,max_transmission_bounce);
  DATA(integrator,max_glossy_bounce);
  DATA(background,transparent_roughness_squared_threshold);
#undef DATA
  constant("O_shadow_terminator_shading_offset",offsetof(KernelObject,shadow_terminator_shading_offset));
#define ENUM(name) constant(#name,uint32_t(name))
  ENUM(LABEL_NONE);ENUM(LABEL_REFLECT);ENUM(LABEL_TRANSMIT);ENUM(LABEL_DIFFUSE);ENUM(LABEL_TRANSPARENT);ENUM(LABEL_TRANSMIT_TRANSPARENT);
  ENUM(LABEL_GLOSSY);ENUM(LABEL_SINGULAR);ENUM(FILTER_CLOSURE_GLOSSY);ENUM(FILTER_CLOSURE_TRANSMISSION);
  ENUM(PATH_RAY_SINGULAR);ENUM(PATH_RAY_IMPORTANCE_BAKE);ENUM(PATH_RAY_DIFFUSE_ANCESTOR);
  ENUM(FILTER_CLOSURE_EMISSION);ENUM(FILTER_CLOSURE_DIFFUSE);ENUM(CLOSURE_NONE_ID);ENUM(NBUILTIN_CLOSURES);
  ENUM(FILTER_CLOSURE_TRANSPARENT);ENUM(SD_OBJECT_HOLDOUT_MASK);ENUM(SD_HAS_ONLY_VOLUME);ENUM(SD_MIS_FRONT);ENUM(SD_MIS_BACK);ENUM(PRNG_SURFACE_BSDF);
#undef ENUM
  constant("SURFACE_UNSUPPORTED_FLAGS",PATH_RAY_SHADOW_CATCHER_PASS|PATH_RAY_SHADOW_CATCHER_HIT|PATH_RAY_SHADOW_CATCHER_BACKGROUND|
    PATH_RAY_DENOISING_FEATURES|PATH_RAY_SUBSURFACE);
  return source.str();
}
std::string surface_source(){return svm_surface_source()+background_layout()+shadow_layout()+lighting_layout()+surface_layout()+surface_fragment;}
void WebGPUDevice::enqueue_surface(int work,const DeviceKernelArguments &args){
  require(args.count==3,"Surface argument count");
  require(args.types[2]==DeviceKernelArguments::INT32&&args.sizes[2]==4,"Invalid surface work argument");
  int n;std::memcpy(&n,args.values[2],4);require(n>=0&&work>=n,"Incomplete surface dispatch");
  auto pointer=[&](unsigned i,bool nullable){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid surface pointer argument");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));if(address||!nullable)validate(address,4);
    require(address%4==0,"Unaligned surface pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};p[1]=n;p[2]=pointer(0,true);p[4]=pointer(1,false);p[17]=error_word;
  if(p[2])validate(p[2],size_t(n)*4);
  require(!data.bvh.have_curves&&!data.bvh.have_points,"WebGPU surface currently requires triangle primitives");
  validate_geometry_motion();
  validate_volume();
  validate_lighting(data.integrator.use_direct_light);
  // Shadow terminator: the shading multiplier 1/(1-offset/2) is implemented
  // for offsets in [0,1] (multiplier at most 2, keeping shift_cos_in's cosine
  // argument inside [0,pi]); larger offsets are rejected, never approximated.
  for(const KernelObject &object:object_records){
    require(std::isfinite(object.shadow_terminator_geometry_offset),"Invalid native shadow terminator geometry offset");
    require(!(object.shadow_terminator_shading_offset>2.0f)&&!std::isnan(object.shadow_terminator_shading_offset),
            "WebGPU shadow terminator shading offset outside [0, 1] not implemented");
  }
  require(!(data.kernel_features&(KERNEL_FEATURE_PATH_GUIDING|KERNEL_FEATURE_DENOISING|KERNEL_FEATURE_MNEE|KERNEL_FEATURE_LIGHT_LINKING|KERNEL_FEATURE_SHADOW_LINKING|KERNEL_FEATURE_AO))&&
          !data.integrator.has_shadow_catcher&&!data.integrator.use_caustics,"WebGPU surface optional integration features not implemented");
  require(data.film.pass_flag==0,"WebGPU surface data passes not implemented");
  require(data.integrator.filter_glossy==FLT_MAX,"WebGPU surface glossy filtering not implemented");
  // Native Scene::get_max_closure_count() preserves a global minimum of one.
  // Local zero-storage SVM evaluation for termination remains valid.
  require(data.max_closures>=1&&data.max_closures<=MAX_CLOSURE,"Invalid surface closure capacity");
  require(data.integrator.sampling_pattern==SAMPLING_PATTERN_TABULATED_SOBOL,"WebGPU surface requires tabulated Sobol");
  require(data.film.pass_stride>0,"Invalid surface film stride");
  auto pass=[&](int offset,int length){require(offset==PASS_UNUSED||(offset>=0&&offset<=data.film.pass_stride-length),"Invalid surface film pass offset");};
  pass(data.film.pass_combined,4);pass(data.film.pass_emission,3);pass(data.film.pass_adaptive_aux_buffer,4);pass(data.film.pass_lightgroup,3);
  pass(data.film.pass_diffuse_direct,3);pass(data.film.pass_glossy_direct,3);pass(data.film.pass_transmission_direct,3);
  pass(data.film.pass_diffuse_indirect,3);pass(data.film.pass_glossy_indirect,3);pass(data.film.pass_transmission_indirect,3);
  if(data.film.light_pass_flag&PASSMASK(COMBINED))require(data.film.pass_combined!=PASS_UNUSED,"Missing combined film pass");
  require(std::isfinite(data.integrator.sample_clamp_direct)&&data.integrator.sample_clamp_direct>=0&&
          std::isfinite(data.integrator.sample_clamp_indirect)&&data.integrator.sample_clamp_indirect>=0,"Invalid surface clamp");
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");p[18]=globals.at("shaders");
  require(remaining(p[18])%sizeof(KernelShader)==0,"Invalid surface shader table");p[19]=uint32_t(remaining(p[18])/sizeof(KernelShader));
  p[20]=(data.kernel_features&KERNEL_FEATURE_LIGHT_PASSES)!=0;
  if(auto it=globals.find("svm_nodes");it!=globals.end()){
    p[21]=it->second;require(remaining(p[21])%4==0,"Invalid SVM word table");p[22]=uint32_t(remaining(p[21])/4);
  }
  p[10]=uint32_t(remaining(uintptr_t(state.path.render_pixel_index))/4);p[11]=uint32_t(remaining(p[4])/4);
  require(p[11]>=uint32_t(data.film.pass_stride),"Surface film smaller than one pixel");
  if(!p[2])require(uint32_t(n)<=p[10],"Surface dispatch exceeds path capacity");
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(feature==KERNEL_FEATURE_PATH_TRACING||((data.kernel_features&KERNEL_FEATURE_LIGHT_PASSES)&&feature==KERNEL_FEATURE_LIGHT_PASSES))validate(uintptr_t(state.parent.name),size_t(p[10])*sizeof(type));
#define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#define KERNEL_STRUCT_ARRAY_MEMBER(parent,type,name,feature)
#define KERNEL_STRUCT_END(name)
#define KERNEL_STRUCT_END_ARRAY(name,cpu_size,gpu_size)
#include "kernel/integrator/state_template.h"
#undef KERNEL_STRUCT_BEGIN
#undef KERNEL_STRUCT_BEGIN_PACKED
#undef KERNEL_STRUCT_MEMBER
#undef KERNEL_STRUCT_MEMBER_PACKED
#undef KERNEL_STRUCT_ARRAY_MEMBER
#undef KERNEL_STRUCT_END
#undef KERNEL_STRUCT_END_ARRAY
  validate(uintptr_t(state.queue_counter),sizeof(IntegratorQueueCounter));
  p[9]=globals.at("sample_pattern_lut");p[14]=scene_tables();
  if(data.integrator.use_direct_light){
    p[25]=uint32_t(remaining(uintptr_t(state.shadow_path.render_pixel_index))/4);
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(KernelFeatureRequest(feature).test(data.kernel_features))validate(uintptr_t(state.parent.name),size_t(p[25])*sizeof(type));
#define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#define KERNEL_STRUCT_ARRAY_MEMBER(parent,type,name,feature)
#define KERNEL_STRUCT_END(name)
#define KERNEL_STRUCT_END_ARRAY(name,cpu_size,gpu_size)
#include "kernel/integrator/shadow_state_template.h"
#undef KERNEL_STRUCT_BEGIN
#undef KERNEL_STRUCT_BEGIN_PACKED
#undef KERNEL_STRUCT_MEMBER
#undef KERNEL_STRUCT_MEMBER_PACKED
#undef KERNEL_STRUCT_ARRAY_MEMBER
#undef KERNEL_STRUCT_END
#undef KERNEL_STRUCT_END_ARRAY
    require(state.shadow_path.bsdf_eval_average,"Missing shadow BSDF average");
    validate(uintptr_t(state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].object),size_t(p[25])*4);
    validate(uintptr_t(state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].prim),size_t(p[25])*4);
    validate(uintptr_t(state.next_shadow_path_index),4);
  }
  const auto sequence=data.integrator.tabulated_sobol_sequence_size;
  require(sequence>1&&(sequence&(sequence-1))==0,"Invalid surface Sobol sequence size");
  validate(p[9],size_t(sequence)*NUM_TAB_SOBOL_PATTERNS*NUM_TAB_SOBOL_DIMENSIONS*4);
  if(!work||!n)return;
  const size_t closure_bytes=size_t(n)*MAX_CLOSURE*sizeof(SurfaceClosureRecord);
  const size_t records_bytes=closure_bytes+size_t(n)*surface_stage_bytes;
  const bool reserve_shadow=data.integrator.use_direct_light&&data.integrator.transparent_shadows;
  const size_t needed=records_bytes+(reserve_shadow?sizeof(uint32_t):0);
  if(needed>surface_closure_bytes){
    if(surface_closures){flush();release(surface_closures);surface_closures=0;surface_closure_bytes=0;}
    surface_closures=allocate(needed);surface_closure_bytes=needed;
  }
  p[23]=surface_closures;
  p[24]=surface_closures+uint32_t(closure_bytes);
  p[26]=p[24]+4;p[27]=surface_stage_bytes;
  if(reserve_shadow)p[28]=surface_closures+uint32_t(records_bytes);
  if(!pipeline_current(surface,"shade_surface")){
    auto built=build_pipelines("surface",surface_source(),{"prepare_shader_data","prepare_surface","shade_surface_light","connect_surface_light","shade_surface"});
    surface_geometry=std::move(built[0]);surface_prepare=std::move(built[1]);surface_light=std::move(built[2]);
    surface_connect=std::move(built[3]);surface=std::move(built[4]);
  }
  // Dispatch ordering makes the GPU-private preparation records visible to the
  // sampling stage without a CPU readback or a native scheduler queue transition.
  dispatch(p,uint32_t(work),surface_geometry,workgroup("prepare_shader_data"));
  dispatch(p,uint32_t(work),surface_prepare,workgroup("prepare_surface"));
  if(reserve_shadow){
    // PathTraceWorkGPU::enqueue_path_iteration budgets at least one shadow slot
    // for each surface input before invoking this kernel. Unused reservations
    // stay terminated; native counters/compaction still decide subsequent work.
    std::array<uint32_t,32> reserve{};reserve[0]=5;reserve[1]=n;
    reserve[2]=uint32_t(uintptr_t(state.next_shadow_path_index));reserve[3]=uint32_t(uintptr_t(state.shadow_path.queued_kernel));
    reserve[4]=p[28];reserve[17]=error_word;reserve[18]=p[25];dispatch(reserve,1);
  }
  // Sizes follow the compiled declarations. Light sampling and BSDF/shadow
  // connection are separate dispatches to bound compiler pressure.
  dispatch(p,uint32_t(work),surface_light,workgroup("shade_surface_light"));
  dispatch(p,uint32_t(work),surface_connect,workgroup("connect_surface_light"));
  dispatch(p,uint32_t(work),surface,workgroup("shade_surface"));
}
}
