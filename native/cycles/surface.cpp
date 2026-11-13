// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "wavefront.h"
#include "surface_closure.h"
#include "kernel/bvh/types.h"
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
// or emissive triangle) passed from shade_surface_light to connect_surface_light,
// then the 8-word light selection of shade_surface_light_select (light tree or
// light linking; LIGHT_SELECT_RECORD = byte 360).
// Its scalar encoding is in wavefront_surface_stage.wgsl and wavefront_lighting.wgsl.
static constexpr size_t surface_stage_bytes=surface_data_cache_bytes+38*sizeof(uint32_t);
static_assert(surface_data_cache_bytes+30*sizeof(uint32_t)==360,"LIGHT_SELECT_RECORD in wavefront_lighting.wgsl");
static_assert(BVH_STACK_SIZE*sizeof(uint32_t)==768,"AO_STACK_BYTES in wavefront_ao.wgsl");
static std::string surface_layout(){
  std::ostringstream source;
  auto constant=[&](const char *name,size_t value){source<<"const "<<name<<":u32="<<value<<"u;\n";};
  constant("SURFACE_STAGE_BYTES",surface_stage_bytes);
  // Preserve the originating native kernel's float3 division order. This is
  // encoded into captures too, independently of the GPU that replays them.
  constant("GGX_NATIVE_RECIPROCAL_NORMALIZE",reference_oracle_sse()?0:1);
  // Diagnostic debug output (pipelines.h SF_DEBUG_OUTPUT): emitted only while the
  // mode is set, so production modules are unchanged.
  if(const unsigned mode=debug_output_mode())constant("DEBUG_OUTPUT_MODE",mode);
#define DATA(parent,member) constant("D_" #parent "_" #member,offsetof(KernelData,parent.member))
  DATA(integrator,max_bounce);
  DATA(integrator,max_diffuse_bounce);DATA(integrator,max_transmission_bounce);
  DATA(integrator,max_glossy_bounce);
  DATA(background,transparent_roughness_squared_threshold);
  source<<"//#if SURFACE_FILTER_GLOSSY\n";DATA(integrator,filter_glossy);source<<"//#endif\n";
#undef DATA
  constant("O_shadow_terminator_shading_offset",offsetof(KernelObject,shadow_terminator_shading_offset));
#define ENUM(name) constant(#name,uint32_t(name))
  ENUM(LABEL_NONE);ENUM(LABEL_REFLECT);ENUM(LABEL_TRANSMIT);ENUM(LABEL_DIFFUSE);ENUM(LABEL_TRANSPARENT);ENUM(LABEL_TRANSMIT_TRANSPARENT);
  ENUM(LABEL_GLOSSY);ENUM(LABEL_SINGULAR);ENUM(FILTER_CLOSURE_GLOSSY);ENUM(FILTER_CLOSURE_TRANSMISSION);
  ENUM(PATH_RAY_SINGULAR);ENUM(PATH_RAY_IMPORTANCE_BAKE);ENUM(PATH_RAY_DIFFUSE_ANCESTOR);
  ENUM(FILTER_CLOSURE_EMISSION);ENUM(FILTER_CLOSURE_DIFFUSE);ENUM(CLOSURE_NONE_ID);ENUM(NBUILTIN_CLOSURES);
  ENUM(FILTER_CLOSURE_TRANSPARENT);ENUM(SD_OBJECT_HOLDOUT_MASK);ENUM(SD_HAS_ONLY_VOLUME);ENUM(SD_MIS_FRONT);ENUM(SD_MIS_BACK);ENUM(PRNG_SURFACE_BSDF);
#undef ENUM
  // With denoising features (FILM_DENOISING) their path flag is supported; the
  // markers keep every other module's constant line unchanged.
  source<<"//#if FILM_DENOISING else ";
  constant("SURFACE_UNSUPPORTED_FLAGS",PATH_RAY_SHADOW_CATCHER_PASS|PATH_RAY_SHADOW_CATCHER_HIT|PATH_RAY_SHADOW_CATCHER_BACKGROUND|
    PATH_RAY_DENOISING_FEATURES|PATH_RAY_SUBSURFACE);
  constant("SURFACE_UNSUPPORTED_FLAGS",PATH_RAY_SHADOW_CATCHER_PASS|PATH_RAY_SHADOW_CATCHER_HIT|PATH_RAY_SHADOW_CATCHER_BACKGROUND|PATH_RAY_SUBSURFACE);
  source<<"//#endif\n";
  return source.str();
}
std::string surface_source(){return svm_surface_source()+background_layout()+shadow_layout()+lighting_layout()+surface_layout()+shadow_catcher_layout()+film_passes_layout()+film_motion_layout()+mnee_layout()+volume_light_layout()+subsurface_layout()+surface_fragment;}
void WebGPUDevice::enqueue_surface(int work,const DeviceKernelArguments &args,DeviceKernel kernel){
  require(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE||kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,"Invalid surface kernel");
  require(args.count==3,"Surface argument count");
  require(args.types[2]==DeviceKernelArguments::INT32&&args.sizes[2]==4,"Invalid surface work argument");
  int n;std::memcpy(&n,args.values[2],4);require(n>=0&&work>=n,"Incomplete surface dispatch");
  auto pointer=[&](unsigned i,bool nullable){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid surface pointer argument");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));if(address||!nullable)validate(address,4);
    require(address%4==0,"Unaligned surface pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};p[1]=n;p[2]=pointer(0,true);p[4]=pointer(1,false);p[17]=error_word;
  if(p[2])validate(p[2],size_t(n)*4);
  validate_points();
  validate_curves();
  validate_geometry_motion();
  validate_volume();
  validate_lighting(data.integrator.use_direct_light);
  // Shadow terminator: the shading multiplier 1/(1-offset/2) for every offset (multipliers above 2,
  // infinite or negative through the wide cosine, SHADOW_TERMINATOR_WIDE); NaN is rejected.
  for(const KernelObject &object:object_records){
    require(std::isfinite(object.shadow_terminator_geometry_offset),"Invalid native shadow terminator geometry offset");
    require(!std::isnan(object.shadow_terminator_shading_offset),"Invalid native shadow terminator shading offset");
  }
  require(!(data.kernel_features&KERNEL_FEATURE_PATH_GUIDING),"WebGPU surface optional integration features not implemented");
  validate_shadow_linking();
  if(data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)require(state.path.mis_ray_object,"Missing light linking path state");
  if(data.integrator.use_caustics)validate_mnee();
  validate_film_passes();film_motion_words(p);
  validate_shadow_catcher();
  // Native Scene::get_max_closure_count() preserves a global minimum of one.
  // Local zero-storage SVM evaluation for termination remains valid.
  require(data.max_closures>=1&&data.max_closures<=MAX_CLOSURE,"Invalid surface closure capacity");
  require(data.film.pass_stride>0,"Invalid surface film stride");
  auto pass=[&](int offset,int length){require(offset==PASS_UNUSED||(offset>=0&&offset<=data.film.pass_stride-length),"Invalid surface film pass offset");};
  pass(data.film.pass_combined,4);pass(data.film.pass_emission,3);pass(data.film.pass_adaptive_aux_buffer,4);pass(data.film.pass_lightgroup,3);
  pass(data.film.pass_diffuse_direct,3);pass(data.film.pass_glossy_direct,3);pass(data.film.pass_transmission_direct,3);
  pass(data.film.pass_diffuse_indirect,3);pass(data.film.pass_glossy_indirect,3);pass(data.film.pass_transmission_indirect,3);
  pass(data.film.pass_volume_direct,3);pass(data.film.pass_volume_indirect,3);
  if(data.film.light_pass_flag&PASSMASK(COMBINED))require(data.film.pass_combined!=PASS_UNUSED,"Missing combined film pass");
  // Ambient occlusion shadow rays (wavefront_ao.wgsl): the AO pass the shadow stage writes.
  const bool ao=(data.kernel_features&KERNEL_FEATURE_AO)!=0;
  if(data.kernel_features&KERNEL_FEATURE_AO_PASS)require(data.film.pass_ao!=PASS_UNUSED&&data.film.pass_ao>=0&&data.film.pass_ao<=data.film.pass_stride-3,"Invalid AO film pass offset");
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
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(feature==KERNEL_FEATURE_PATH_TRACING||((data.kernel_features&KERNEL_FEATURE_LIGHT_PASSES)&&feature==KERNEL_FEATURE_LIGHT_PASSES)||\
  ((data.kernel_features&KERNEL_FEATURE_DENOISING)&&feature==KERNEL_FEATURE_DENOISING)||((data.kernel_features&KERNEL_FEATURE_NODE_PORTAL)&&feature==KERNEL_FEATURE_NODE_PORTAL))\
  validate(uintptr_t(state.parent.name),size_t(p[10])*sizeof(gpu_state_storage<type>::gpu_type));
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
  p[9]=sample_pattern_table("surface");p[14]=scene_tables();
  // Push word 29 names the queue for the shared stages (0: SHADE_SURFACE).
  p[29]=kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE?uint32_t(kernel):0;
  if(data.integrator.use_direct_light||ao){
    p[25]=uint32_t(remaining(uintptr_t(state.shadow_path.render_pixel_index))/4);
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(KernelFeatureRequest(feature).test(data.kernel_features))validate(uintptr_t(state.parent.name),size_t(p[25])*sizeof(gpu_state_storage<type>::gpu_type));
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
    require(state.shadow_path.bsdf_eval_average||(data.kernel_features&KERNEL_FEATURE_LIGHT_TREE),"Missing shadow BSDF average");
    validate(uintptr_t(state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].object),size_t(p[25])*4);
    validate(uintptr_t(state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].prim),size_t(p[25])*4);
    validate(uintptr_t(state.next_shadow_path_index),4);
  }
  if(!work||!n)return;
  const size_t closure_bytes=size_t(n)*MAX_CLOSURE*sizeof(SurfaceClosureRecord);
  const size_t records_bytes=closure_bytes+size_t(n)*surface_stage_bytes;
  // Reserved shadow slots (control op 5) give every input fixed slots whenever
  // the stage can create shadow paths: needed by resumable transparent shadows
  // and in caustic scenes (MNEE reserves its slots the same way; mnee.cpp), and
  // for every other direct-light or ambient-occlusion shadow path, which would
  // otherwise take atomic slots in a run-dependent order (the native scheduler's
  // shadow compaction counts then differ between runs). Every slot is one the
  // pinned atomic allocator could also hand out; unused slots stay terminated.
  const bool reserve_shadow=data.integrator.use_direct_light||ao;
  // The Ambient Occlusion node's traversal stacks (wavefront_ao.wgsl ao_trace, push
  // word 30): one 192-word slot per input of the raytrace entry, after the records.
  const bool raytrace=kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE;
  const size_t ao_stack_at=records_bytes+(reserve_shadow?sizeof(uint32_t):0);
  // With the remaining SVM BSDF families (pipelines.h SF_BSDF_FAMILIES) the BSDF sample is its own entry,
  // shade_surface_sample, whose 64-byte record per input (push word 0) shade_surface reads.
  const bool sample_stage=(shader_features()&SF_BSDF_FAMILIES).any();
  const size_t sample_at=ao_stack_at+(raytrace?size_t(n)*BVH_STACK_SIZE*sizeof(uint32_t):0);
  size_t needed=sample_at+(sample_stage?size_t(n)*64:0);
  // A replay capture: the records of a dispatch of the whole path capacity (device.h capture_sizing).
  if(capture_sizing){const size_t c=p[10];
    const size_t full=c*MAX_CLOSURE*sizeof(SurfaceClosureRecord)+c*surface_stage_bytes+(reserve_shadow?sizeof(uint32_t):0)+(raytrace?c*BVH_STACK_SIZE*sizeof(uint32_t):0)+(sample_stage?c*64:0);
    if(full>needed)needed=full;}
  if(needed>surface_closure_bytes){
    if(surface_closures){flush();release(surface_closures);surface_closures=0;surface_closure_bytes=0;}
    surface_closures=allocate(needed);surface_closure_bytes=needed;
  }
  p[23]=surface_closures;
  p[24]=surface_closures+uint32_t(closure_bytes);
  p[26]=p[24]+4;p[27]=surface_stage_bytes;
  if(reserve_shadow)p[28]=surface_closures+uint32_t(records_bytes);
  if(raytrace)p[30]=surface_closures+uint32_t(ao_stack_at);
  if(sample_stage)p[0]=surface_closures+uint32_t(sample_at);
  // With emissive triangles (LIGHT_TRIANGLE) the light stage is two entries
  // (wavefront_surface.wgsl): shade_surface_light samples a lamp and
  // shade_surface_light_triangle does the triangle work, which keeps each entry
  // inside the Intel Mesa 25.0.7 backend's 16-bit instruction/register range.
  const bool triangle_stage=(shader_features()&SF_LIGHT_TRIANGLE).any();
  // shade_surface_light_select (light tree or light linking) runs first whenever
  // the module carries it: the samplers then read its selection record.
  const bool select_stage=(shader_features()&SF_LIGHT_SELECT).any();
  // With sun guiding (BACKGROUND_SUN) the background light's sample is one more entry,
  // shade_surface_light_background, after shade_surface_light (SF_BACKGROUND_SAMPLE_ENTRY,
  // pipelines.h): the light entries come from the module variant with that bit. So does a scene with background light
  // portals (BACKGROUND_PORTAL), whose portal strategy kept shade_surface_light at 70,635 Intel instructions, and a scene
  // with a background light and area lights (device.h surface_background_stage).
  const bool background_stage=surface_background_stage();
  const ShaderFeatures light_variant=background_stage?SF_BACKGROUND_SAMPLE_ENTRY:SF_NONE;
  if(sample_stage&&(!pipeline_current(surface_sample,"shade_surface_sample")||!surface_sample.value))
    surface_sample=std::move(build_pipelines("surface",surface_source(),{"shade_surface_sample"})[0]);
  const bool huang_stage=(shader_features()&SF_BSDF_HAIR_HUANG).any();
  if(huang_stage&&(!pipeline_current(surface_huang_eval,"shade_surface_huang_eval")||!surface_huang_eval.value))
    surface_huang_eval=std::move(build_pipelines("surface",surface_source(),{"shade_surface_huang_eval"})[0]);
  if(huang_stage&&(!pipeline_current(surface_huang_sample,"shade_surface_huang_sample")||!surface_huang_sample.value))
    surface_huang_sample=std::move(build_pipelines("surface",surface_source(),{"shade_surface_huang_sample"})[0]);
  if(select_stage&&(!pipeline_current(surface_light_select,"shade_surface_light_select")||!surface_light_select.value))
    surface_light_select=std::move(build_pipelines("surface",surface_source(),{"shade_surface_light_select"})[0]);
  // prepare_surface evaluates only the hit triangles' shaders: the SVM node regions no triangle shader reaches (the
  // world's, the lamps') are left out of its pipeline (surface_unused_svm_features), built on its own below; scenes
  // without such regions keep it in the shared build.
  const ShaderFeatures prepare_unused=surface_unused_svm_features();
  if(!pipeline_current(surface,"shade_surface")||(triangle_stage&&!surface_light_triangle.value)||(background_stage&&!surface_light_background.value)){
    if(prepare_unused.any()){
      if(triangle_stage){
        auto built=build_pipelines("surface",surface_source(),{"prepare_shader_data","shade_surface_light","shade_surface_light_triangle","connect_surface_light","shade_surface"},light_variant);
        surface_geometry=std::move(built[0]);surface_light=std::move(built[1]);
        surface_light_triangle=std::move(built[2]);surface_connect=std::move(built[3]);surface=std::move(built[4]);
      }else{
        auto built=build_pipelines("surface",surface_source(),{"prepare_shader_data","shade_surface_light","connect_surface_light","shade_surface"},light_variant);
        surface_geometry=std::move(built[0]);surface_light=std::move(built[1]);
        surface_connect=std::move(built[2]);surface=std::move(built[3]);surface_light_triangle={};
      }
      surface_prepare={};
    }else if(triangle_stage){
      auto built=build_pipelines("surface",surface_source(),{"prepare_shader_data","prepare_surface","shade_surface_light","shade_surface_light_triangle","connect_surface_light","shade_surface"},light_variant);
      surface_geometry=std::move(built[0]);surface_prepare=std::move(built[1]);surface_light=std::move(built[2]);
      surface_light_triangle=std::move(built[3]);surface_connect=std::move(built[4]);surface=std::move(built[5]);
    }else{
      auto built=build_pipelines("surface",surface_source(),{"prepare_shader_data","prepare_surface","shade_surface_light","connect_surface_light","shade_surface"},light_variant);
      surface_geometry=std::move(built[0]);surface_prepare=std::move(built[1]);surface_light=std::move(built[2]);
      surface_connect=std::move(built[3]);surface=std::move(built[4]);surface_light_triangle={};
    }
    if(background_stage)surface_light_background=std::move(build_pipelines("surface",surface_source(),{"shade_surface_light_background"},light_variant)[0]);
    else surface_light_background={};
    surface_prepare_removed=SF_NONE;
  }
  if(!surface_prepare.value||prepare_unused!=surface_prepare_removed){
    surface_prepare=std::move(build_pipelines("surface",surface_source(),{"prepare_surface"},light_variant,prepare_unused)[0]);
    surface_prepare_removed=prepare_unused;
  }
  // Dispatch ordering makes the GPU-private preparation records visible to the
  // sampling stage without a CPU readback or a native scheduler queue transition.
  dispatch(p,uint32_t(work),surface_geometry,workgroup("prepare_shader_data"));
  if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE){
    // Shaders flagged SD_HAS_RAYTRACE (the Ambient Occlusion node): the SVM stage
    // with the node's ray tracing (SVM_NODE_RAYTRACE, wavefront_ao.wgsl), built from
    // the same surface source; the other stages are the shared pipelines above.
    const ShaderFeatures unused=raytrace_unused_svm_features();
    if(!pipeline_current(surface_prepare_raytrace,"prepare_surface_raytrace",unused))
      surface_prepare_raytrace=std::move(build_pipelines("surface",surface_source(),{"prepare_surface_raytrace"},SF_NODE_RAYTRACE,unused)[0]);
    dispatch(p,uint32_t(work),surface_prepare_raytrace,workgroup("prepare_surface_raytrace"));
  }else dispatch(p,uint32_t(work),surface_prepare,workgroup("prepare_surface"));
  if(reserve_shadow){
    // PathTraceWorkGPU::enqueue_path_iteration budgets at least one shadow slot
    // for each surface input before invoking this kernel. Unused reservations
    // stay terminated; native counters/compaction still decide subsequent work.
    std::array<uint32_t,32> reserve{};reserve[0]=5;
    // With ambient occlusion each input may add two shadow paths (the native
    // scheduler halves its work for AO kernels); the AO path takes slot n + id.x.
    reserve[1]=ao?2*n:n;
    reserve[2]=uint32_t(uintptr_t(state.next_shadow_path_index));reserve[3]=uint32_t(uintptr_t(state.shadow_path.queued_kernel));
    reserve[4]=p[28];reserve[17]=error_word;reserve[18]=p[25];dispatch(reserve,1);
  }
  // Sizes follow the compiled declarations. Light sampling and BSDF/shadow
  // connection are separate dispatches to bound compiler pressure.
  if(select_stage)dispatch(p,uint32_t(work),surface_light_select,workgroup("shade_surface_light_select"));
  dispatch(p,uint32_t(work),surface_light,workgroup("shade_surface_light"));
  // The light entries built with the background-sample variant leave such records to this entry.
  if(surface_light_background.value)dispatch(p,uint32_t(work),surface_light_background,workgroup("shade_surface_light_background"));
  if(triangle_stage)dispatch(p,uint32_t(work),surface_light_triangle,workgroup("shade_surface_light_triangle"));
  if(huang_stage)dispatch(p,uint32_t(work),surface_huang_eval,workgroup("shade_surface_huang_eval"));
  dispatch(p,uint32_t(work),surface_connect,workgroup("connect_surface_light"));
  // integrate_surface_ao (AO_SHADOW: fast GI's additive mode or the AO pass): its own entry after the
  // direct light, for compiler pressure (wavefront_surface.wgsl connect_surface_ao).
  if((shader_features()&SF_AO_SHADOW).any()){
    if(!pipeline_current(surface_ao,"connect_surface_ao")||!surface_ao.value)
      surface_ao=std::move(build_pipelines("surface",surface_source(),{"connect_surface_ao"})[0]);
    dispatch(p,uint32_t(work),surface_ao,workgroup("connect_surface_ao"));
  }
  // film_write_data_passes (FILM_DATA_PASSES, wavefront_film_passes.wgsl): after
  // holdout, emission and termination (connect_surface_light), before the bounce
  // changes the path's flag and throughput (shade_surface); the same records.
  if((shader_features()&(SF_FILM_DATA_PASSES|SF_FILM_DENOISING)).any()){
    if(!pipeline_current(surface_passes,"shade_surface_passes")||!surface_passes.value)
      surface_passes=std::move(build_pipelines("surface",surface_source(),{"shade_surface_passes"})[0]);
    dispatch(p,uint32_t(work),surface_passes,workgroup("shade_surface_passes"));
  }
  if(huang_stage)dispatch(p,uint32_t(work),surface_huang_sample,workgroup("shade_surface_huang_sample"));
  if(sample_stage)dispatch(p,uint32_t(work),surface_sample,workgroup("shade_surface_sample"));
  if(huang_stage)dispatch(p,uint32_t(work),surface_huang_eval,workgroup("shade_surface_huang_eval"));
  dispatch(p,uint32_t(work),surface,workgroup("shade_surface"));
  // integrator_shade_surface's MNEE cleanup (wavefront_mnee.wgsl), caustic scenes only.
  if(data.integrator.use_caustics){
    if(!pipeline_current(mnee_cleanup,"mnee_cleanup"))mnee_cleanup=std::move(build_pipelines("surface",surface_source(),{"mnee_cleanup"})[0]);
    dispatch(p,uint32_t(work),mnee_cleanup,workgroup("mnee_cleanup"));
  }
}
// DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_NEE: shadow paths whose sampled light has
// no constant emission (wavefront_light_nee.wgsl). Arguments as every shading
// kernel: path indices, render buffer (unused), work size.
void WebGPUDevice::enqueue_light_nee(int work,const DeviceKernelArguments &args){
  require(args.count==3,"Light NEE argument count");
  require(args.types[2]==DeviceKernelArguments::INT32&&args.sizes[2]==4,"Invalid light NEE work argument");
  int n;std::memcpy(&n,args.values[2],4);require(n>=0&&work>=n,"Incomplete light NEE dispatch");
  require(args.types[0]==DeviceKernelArguments::POINTER&&args.sizes[0]==sizeof(device_ptr),"Invalid light NEE pointer argument");
  device_ptr indices;std::memcpy(&indices,args.values[0],sizeof(indices));require(indices%4==0,"Unaligned light NEE indices");
  std::array<uint32_t,32> p{};p[1]=n;p[2]=uint32_t(indices);p[17]=error_word;if(p[2])validate(p[2],size_t(n)*4);
  validate_points();
  validate_curves();
  require(!(data.kernel_features&KERNEL_FEATURE_PATH_GUIDING),"WebGPU light NEE optional integration features not implemented");
  validate_shadow_linking();
  validate_geometry_motion();
  validate_lighting(true);
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");p[9]=sample_pattern_table("light NEE");p[18]=globals.at("shaders");
  require(remaining(p[18])%sizeof(KernelShader)==0,"Invalid light NEE shader table");p[19]=uint32_t(remaining(p[18])/sizeof(KernelShader));
  if(auto it=globals.find("svm_nodes");it!=globals.end()){p[21]=it->second;require(remaining(p[21])%4==0,"Invalid SVM word table");p[22]=uint32_t(remaining(p[21])/4);}
  p[10]=uint32_t(remaining(uintptr_t(state.shadow_path.render_pixel_index))/4);
  if(!p[2])require(uint32_t(n)<=p[10],"Light NEE dispatch exceeds shadow path capacity");
  require(uint32_t(work)<=svm_stack_slots,"Light NEE dispatch exceeds the SVM stack slots");
  auto field=[&](const void *address,size_t size){validate(uintptr_t(address),size_t(p[10])*size);};
  field(state.shadow_path.queued_kernel,2);field(state.shadow_path.throughput,12);
  if(!(data.kernel_features&KERNEL_FEATURE_LIGHT_TREE))field(state.shadow_path.bsdf_eval_average,4);
  field(state.shadow_path.sample,4);field(state.shadow_path.rng_pixel,4);field(state.shadow_path.rng_offset,2);
  field(state.shadow_ray.P,12);field(state.shadow_ray.D,12);field(state.shadow_ray.tmax,4);field(state.shadow_ray.time,4);
  field(state.shadow_ray.dP,4);field(state.shadow_ray.dD,4);field(state.shadow_ray.self_light_object,4);field(state.shadow_ray.self_light_prim,4);
  validate(uintptr_t(state.queue_counter),sizeof(IntegratorQueueCounter));
  p[14]=scene_tables();
  if(!n||!work)return;
  // GPU-private ShaderData of each dispatched path's light point (prepare_light_nee -> shade_light_nee).
  const size_t needed=size_t(work)*surface_data_cache_bytes;
  if(needed>light_nee_cache_bytes){
    if(light_nee_cache){flush();release(light_nee_cache);light_nee_cache=0;light_nee_cache_bytes=0;}
    light_nee_cache=allocate(needed);light_nee_cache_bytes=needed;
  }
  p[26]=light_nee_cache;p[27]=surface_data_cache_bytes;
  if(!pipeline_current(light_nee,"shade_light_nee")||!pipeline_current(light_nee_prepare,"prepare_light_nee")){
    auto built=build_pipelines("surface",surface_source(),{"prepare_light_nee","shade_light_nee"});
    light_nee_prepare=std::move(built[0]);light_nee=std::move(built[1]);
  }
  // Dispatch ordering makes the cached ShaderData visible to the evaluation.
  dispatch(p,uint32_t(work),light_nee_prepare,workgroup("prepare_light_nee"));
  dispatch(p,uint32_t(work),light_nee,workgroup("shade_light_nee"));
}

// DEVICE_KERNEL_SHADER_EVAL_DISPLACE (integrator/shader_eval.cpp eval_gpu, for
// GeometryManager::displace before the BVH build): the KernelShaderEvalInput array,
// the float3 output (added to), the cache-miss word (always false: no tiled images),
// the chunk offset and the chunk size. The displacement programs run in the surface
// module's shader_eval_displace (SHADER_EVAL_DISPLACE), at most svm_stack_slots items
// per dispatch (one SVM stack slot per invocation).
void WebGPUDevice::enqueue_displace(int work,const DeviceKernelArguments &args){
  require(args.count==5,"Displacement evaluation argument count");
  auto scalar=[&](unsigned i){require(args.types[i]==DeviceKernelArguments::INT32&&args.sizes[i]==4,"Invalid displacement evaluation integer");
    int value;std::memcpy(&value,args.values[i],4);require(value>=0,"Negative displacement evaluation range");return uint32_t(value);};
  const uint32_t offset=scalar(3),count=scalar(4);require(uint32_t(work)>=count,"Incomplete displacement evaluation dispatch");
  auto pointer=[&](unsigned i,size_t bytes){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid displacement evaluation pointer");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));validate(address,bytes);require(address%4==0,"Unaligned displacement evaluation pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};p[1]=count;p[3]=offset;p[17]=error_word;
  p[2]=pointer(0,(size_t(offset)+count)*sizeof(KernelShaderEvalInput));p[4]=pointer(1,(size_t(offset)+count)*3*sizeof(float));
  {uint32_t miss=pointer(2,4);uint32_t zero=0;upload(miss,&zero,4);}
  // Runs during scene preparation, before any integrator state exists (p[7] stays 0).
  p[6]=constants.at("data");p[18]=globals.at("shaders");
  require(remaining(p[18])%sizeof(KernelShader)==0,"Invalid displacement shader table");p[19]=uint32_t(remaining(p[18])/sizeof(KernelShader));
  if(auto it=globals.find("svm_nodes");it!=globals.end()){p[21]=it->second;require(remaining(p[21])%4==0,"Invalid SVM word table");p[22]=uint32_t(remaining(p[21])/4);}
  p[14]=scene_tables();
  if(!count||!work)return;
  require((shader_features()&SF_EVAL_DISPLACE).any(),"Invalid native displacement evaluation without a displacement shader");
  const ShaderFeatures unused=displacement_unused_svm_features();
  if(!pipeline_current(surface_displace,"shader_eval_displace",unused))
    surface_displace=std::move(build_pipelines("surface",surface_source(),{"shader_eval_displace"},SF_NONE,unused)[0]);
  for(uint32_t first=0;first<count;first+=svm_stack_slots){
    p[30]=first;dispatch(p,std::min<uint32_t>(svm_stack_slots,count-first),surface_displace,workgroup("shader_eval_displace"));
  }
}
}
