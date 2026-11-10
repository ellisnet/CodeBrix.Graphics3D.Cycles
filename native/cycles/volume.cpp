// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "wavefront.h"
#include "surface_closure.h"
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace ccl {
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static constexpr char volume_stack_fragment[]=
#include "volume_stack_shader.inc"
;
std::string volume_stack_source(){return wavefront_layout()+shader_data_layout()+shadow_catcher_layout()+volume_stack_fragment;}
static constexpr char volume_fragment[]=
#include "volume_shader.inc"
;
// Volume direct light (VOLUME_LIGHT regions): the per-input record shade_volume_segment, shade_volume and
// shade_volume_light share (wavefront_volume.wgsl, wavefront_volume_light.wgsl), and the shader flags they read.
static constexpr uint32_t volume_light_record_bytes=192;
std::string volume_light_layout(){
  std::ostringstream source;auto constant=[&](const char *name,size_t v){source<<"const "<<name<<":u32="<<uint32_t(v)<<"u;\n";};
  // The record size is shared with the phase stage (VL_RECORD: VOLUME_LIGHT or VOLUME_PHASES).
  source<<"//#if VL_RECORD\n";constant("VL_RECORD_BYTES",volume_light_record_bytes);source<<"//#endif\n//#if VOLUME_LIGHT\n";
  constant("VL_SD_VOLUME_MIS",SD_VOLUME_MIS);constant("VL_SD_VOLUME_EQUIANGULAR",SD_VOLUME_EQUIANGULAR);
  constant("VL_SHADER_EXCLUDE_SCATTER",SHADER_EXCLUDE_SCATTER);constant("VL_K_SHADE_VOLUME",DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME);
  constant("VL_K_INTERSECT_CLOSEST",DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST);constant("VL_PRNG_VOLUME_PHASE",PRNG_VOLUME_PHASE);
  constant("VL_D_max_bounce",offsetof(KernelData,integrator.max_bounce));constant("VL_D_max_volume_bounce",offsetof(KernelData,integrator.max_volume_bounce));
  constant("VL_TREE_SELECTION",96);constant("VL_D_use_light_tree",offsetof(KernelData,integrator.use_light_tree));
#define ENUM(name) constant("VL_" #name,uint32_t(name))
  ENUM(PATH_RAY_REFLECT);ENUM(PATH_RAY_SINGULAR);ENUM(PATH_RAY_IMPORTANCE_BAKE);ENUM(PATH_RAY_MIS_SKIP);ENUM(PATH_RAY_MIS_HAD_TRANSMISSION);
  ENUM(PATH_RAY_TRANSPARENT_BACKGROUND);ENUM(PATH_RAY_ANY_PASS);ENUM(PATH_RAY_VISIBILITY_VOLUME_SCATTER);ENUM(PATH_RAY_VOLUME_PASS);
  ENUM(PATH_RAY_VOLUME_PRIMARY_TRANSMIT);ENUM(SR_BSDF_HAS_TRANSMISSION);
#undef ENUM
  source<<"//#endif\n//#if VOLUME_EMISSION\n";
  constant("VE_D_sample_clamp_direct",offsetof(KernelData,integrator.sample_clamp_direct));constant("VE_D_sample_clamp_indirect",offsetof(KernelData,integrator.sample_clamp_indirect));
  constant("VE_D_light_pass_flag",offsetof(KernelData,film.light_pass_flag));constant("VE_MASK_COMBINED",PASSMASK(COMBINED));
  constant("VE_D_pass_combined",offsetof(KernelData,film.pass_combined));constant("VE_D_pass_adaptive_aux_buffer",offsetof(KernelData,film.pass_adaptive_aux_buffer));
  source<<"//#endif\n//#if VOLUME_LIGHT_PASSES\n";
#define PASS(name) constant("VE_D_" #name,offsetof(KernelData,film.name))
  PASS(pass_emission);PASS(pass_lightgroup);PASS(pass_diffuse_direct);PASS(pass_diffuse_indirect);PASS(pass_glossy_direct);PASS(pass_glossy_indirect);
  PASS(pass_transmission_direct);PASS(pass_transmission_indirect);PASS(pass_volume_direct);PASS(pass_volume_indirect);
#undef PASS
  constant("VE_O_lightgroup",offsetof(KernelObject,lightgroup));constant("VE_PATH_RAY_ANY_PASS",PATH_RAY_ANY_PASS);constant("VE_PATH_RAY_SURFACE_PASS",PATH_RAY_SURFACE_PASS);constant("VE_PATH_RAY_VOLUME_PASS",PATH_RAY_VOLUME_PASS);
  source<<"//#endif\n";
  return source.str();
}
std::string volume_source(){
  std::ostringstream source;auto constant=[&](const char *name,size_t v){source<<"const "<<name<<":u32="<<uint32_t(v)<<"u;\n";};
#define DATA(parent,member) constant("V_" #parent "_" #member,offsetof(KernelData,parent.member))
  DATA(integrator,max_bounce);DATA(integrator,max_volume_bounce);
  DATA(film,pass_volume_scatter_denoised);DATA(film,pass_volume_transmit_denoised);
#undef DATA
#define ENUM(name) constant("V_" #name,uint32_t(name))
  constant("VD_INPUT_SIZE",sizeof(KernelShaderEvalInput));constant("VD_INPUT_OBJECT",offsetof(KernelShaderEvalInput,object));constant("VD_HETEROGENEOUS",SD_HETEROGENEOUS_VOLUME);
  ENUM(PRNG_VOLUME_PHASE);ENUM(PRNG_VOLUME_SCATTER_DISTANCE);ENUM(PRNG_VOLUME_RESERVOIR);
  ENUM(PATH_RAY_REFLECT);ENUM(PATH_RAY_SINGULAR);ENUM(PATH_RAY_IMPORTANCE_BAKE);ENUM(PATH_RAY_MIS_SKIP);ENUM(PATH_RAY_MIS_HAD_TRANSMISSION);
  ENUM(PATH_RAY_TRANSPARENT_BACKGROUND);ENUM(PATH_RAY_ANY_PASS);ENUM(PATH_RAY_VISIBILITY_VOLUME_SCATTER);ENUM(PRIMITIVE_VOLUME);
#undef ENUM
  // Heterogeneous density prepass (VOLUME_HETEROGENEOUS): the voxel corner and size words, the sample dimension.
  source<<"//#if VOLUME_HETEROGENEOUS\n";
  constant("VD_INPUT_PRIM",offsetof(KernelShaderEvalInput,prim));constant("VD_INPUT_U",offsetof(KernelShaderEvalInput,u));
  constant("VD_INPUT_V",offsetof(KernelShaderEvalInput,v));constant("VD_PRNG_DENSITY",PRNG_BAKE_VOLUME_DENSITY_EVAL);
  constant("VD_TRANSFORM_APPLIED",SD_OBJECT_TRANSFORM_APPLIED);
  source<<"//#endif\n";
  // svm_world's background_light_clamp_dD (LIGHT_BACKGROUND, wavefront_svm.wgsl) sits in the shared SVM text.
  source<<"//#if LIGHT_BACKGROUND\n";constant("D_background_map_dD",offsetof(KernelData,background.map_dD));source<<"//#endif\n";
  // Light linking (LIGHT_LINKING): a medium's emission is seen by the rays whose receiver it lights (light_link_object_match).
  source<<"//#if LIGHT_LINKING\n";constant("VLL_O_receiver_light_set",offsetof(KernelObject,receiver_light_set));
  constant("VLL_O_light_set_membership",offsetof(KernelObject,light_set_membership));constant("VLL_LINK_SET_MAX",LIGHT_LINK_SET_MAX);source<<"//#endif\n";
  return svm_volume_source()+source.str()+volume_light_layout()+shadow_catcher_layout()+volume_fragment;
}
// Bounded static homogeneous media: density preparation, stack boundaries, HG transport, the volume
// scattering probability guidance (with its film passes and filters), and direct light from point, sun and
// background lights with distance, equiangular and MIS scatter sampling, shadows through the media
// (VOLUME_LIGHT, VOLUME_SHADOW), spot, area and triangle lights in volume direct light (VOLUME_LIGHT_SHAPES),
// the light tree's volume segments (VOLUME_LIGHT_TREE), light passes (VOLUME_LIGHT_PASSES), heterogeneous media and
// biased ray marching, in scenes with camera, object and deformation motion (VOLUME_MOTION).
// Mixed-anisotropy duplicate closure compaction is rejected in WGSL before state/film mutation.
void WebGPUDevice::validate_volume() const{
  if(!(data.kernel_features&KERNEL_FEATURE_VOLUME))return;
  // Motion blur (VOLUME_MOTION regions, pipelines.h SF_VOLUME_MOTION): the stack initialization traverses moving instances and
  // motion triangles at the ray's time (bvh/volume_all.h, BVH_MOTION) with the boundary normals at that time, and every volume
  // shader evaluation takes its stack entry's object motion transforms (volume_shader_eval_entry). Not ported: velocity-advected
  // volumes (volume_shader_motion_blur, SD_OBJECT_HAS_VOLUME_MOTION).
  for(const uint flags:object_flag_records)require(!(flags&SD_OBJECT_HAS_VOLUME_MOTION),"WebGPU volume velocity motion blur not implemented");
  // Malformed stack and closure sizes first (data validity, before the capability refusals below).
  require(data.volume_stack_size>=2&&data.volume_stack_size<=MAX_VOLUME_STACK_SIZE,"Invalid homogeneous volume stack size");
  require(data.max_closures>=1&&data.max_closures<=MAX_CLOSURE,"Invalid homogeneous volume closure capacity");
  // Denoising features (FILM_DENOISING): film_write_denoising_features_volume in the three integrators. Light linking
  // (LIGHT_LINKING): the receivers of the volume direct light, the emission's forward receiver, mis_ray_object at a scatter.
  // Path guiding is a permanent refusal of the device.
  require(!(data.kernel_features&KERNEL_FEATURE_PATH_GUIDING),"WebGPU optional volume integration features not implemented");
  if(data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)require(state.path.mis_ray_object,"Missing light linking path state");
  // Heterogeneous volumes (VOLUME_HETEROGENEOUS): the octree's node extrema are the camera-ray densities; a Light Path node
  // makes Cycles estimate them per segment for other rays (volume_object_get_extrema, volume_estimate_extrema), for every
  // stack entry with the node, a homogeneous one too (one sample): VOLUME_LIGHT_PATH.
  auto pass=[&](int offset,int length,bool required){require((!required&&offset==PASS_UNUSED)||(offset>=0&&offset<=data.film.pass_stride-length),"Invalid homogeneous volume film pass");};
  pass(data.film.pass_volume_majorant,1,true);pass(data.film.pass_volume_majorant_sample_count,1,true);
  pass(data.film.pass_volume_scatter_denoised,1,true);pass(data.film.pass_volume_transmit_denoised,1,true);
  pass(data.film.pass_volume_scatter,3,false);pass(data.film.pass_volume_transmit,3,true);
  pass(data.film.pass_volume_direct,3,false);pass(data.film.pass_volume_indirect,3,false);
  if(data.kernel_features&KERNEL_FEATURE_DENOISING){pass(data.film.pass_denoising_normal,3,false);pass(data.film.pass_denoising_albedo,3,false);}
  if(state.path.render_pixel_index){const size_t count=remaining(uintptr_t(state.path.render_pixel_index))/4;
    for(unsigned slot=0;slot<data.volume_stack_size;slot++){validate(uintptr_t(state.volume_stack[slot].object),count*4);validate(uintptr_t(state.volume_stack[slot].shader),count*4);}}
}
void WebGPUDevice::enqueue_volume(int work,const DeviceKernelArguments &args){
  require(args.count==3,"Volume argument count");require(args.types[2]==DeviceKernelArguments::INT32&&args.sizes[2]==4,"Invalid volume work argument");
  int n;std::memcpy(&n,args.values[2],4);require(n>=0&&work>=n,"Incomplete volume dispatch");
  auto pointer=[&](unsigned i,bool nullable){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid volume pointer argument");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));if(address||!nullable)validate(address,4);require(address%4==0,"Unaligned volume pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};p[1]=n;p[2]=pointer(0,true);p[4]=pointer(1,false);p[17]=error_word;if(p[2])validate(p[2],size_t(n)*4);
  require(data.kernel_features&KERNEL_FEATURE_VOLUME,"Volume kernel without native volume feature");validate_volume();
  // The sampling pattern (camera.cpp sample_pattern_table): tabulated Sobol's table, or 0 for Sobol-Burley and the blue-noise
  // patterns, whose samples the volume entries compute (VOLUME_SAMPLING_PATTERN regions, native_rng_pattern).
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");p[9]=sample_pattern_table("volume");p[14]=scene_tables();
  require(data.integrator.sampling_pattern==SAMPLING_PATTERN_TABULATED_SOBOL||(shader_features()&SF_VOLUME_SAMPLING_PATTERN).any(),"Volume sampling pattern without its specialization");
  p[18]=globals.at("shaders");p[19]=uint32_t(remaining(p[18])/sizeof(KernelShader));p[20]=(data.kernel_features&KERNEL_FEATURE_LIGHT_PASSES)!=0;
  p[21]=globals.at("svm_nodes");p[22]=uint32_t(remaining(p[21])/4);
  p[10]=uint32_t(remaining(uintptr_t(state.path.render_pixel_index))/4);p[11]=uint32_t(remaining(p[4])/4);
  require(data.film.pass_stride>0&&p[11]>=uint32_t(data.film.pass_stride),"Volume film smaller than one pixel");
  if(!p[2])require(uint32_t(n)<=p[10],"Volume dispatch exceeds path capacity");
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(feature==KERNEL_FEATURE_PATH_TRACING||((data.kernel_features&KERNEL_FEATURE_LIGHT_PASSES)&&feature==KERNEL_FEATURE_LIGHT_PASSES)||\
  ((data.kernel_features&KERNEL_FEATURE_DENOISING)&&feature==KERNEL_FEATURE_DENOISING)||((data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)&&feature==KERNEL_FEATURE_LIGHT_LINKING))validate(uintptr_t(state.parent.name),size_t(p[10])*sizeof(type));
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
  for(auto [kernel,slot]:{std::pair{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,15},std::pair{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,16}}){if(state.sort_key_counter[kernel])p[slot]=uint32_t(remaining(uintptr_t(state.sort_key_counter[kernel]))/4);}
  require(p[15]>=uint32_t(data.max_shaders)&&state.sort_partition_divisor>0,"Invalid volume surface sort counters");
  // Volume emission's light passes have no gate case with shadow linking.
  require(!((data.kernel_features&KERNEL_FEATURE_SHADOW_LINKING)&&(shader_features()&SF_VOLUME_EMISSION).any()&&(data.film.pass_emission!=PASS_UNUSED||data.film.pass_lightgroup!=PASS_UNUSED)),
          "WebGPU shadow linking with volume emission and light passes not implemented");
  // Volume direct light (VOLUME_LIGHT): shade_volume_segment and shade_volume_light come from the surface module
  // (light sampling, shadow paths); the three entries share the per-input record at push word 24.
  const bool light_stage=(shader_features()&SF_VOLUME_LIGHT).any();
  // Emissive triangles in the volume direct light: their own entry before shade_volume_light (compile envelope).
  const bool triangle_light_stage=light_stage&&(shader_features()&SF_VOLUME_LIGHT_SHAPES).any()&&(shader_features()&SF_LIGHT_TRIANGLE).any();
  // The phase functions beyond Henyey-Greenstein (VOLUME_PHASES): the indirect phase scatter runs in the volume
  // module's shade_volume_phase entry after the other volume entries (compile envelope), with the per-input record.
  // Heterogeneous volumes (VOLUME_HETEROGENEOUS): shade_volume_heterogeneous integrates the paths with one in their
  // stack, after shade_volume; their indirect phase scatter is shade_volume_phase's too.
  const bool heterogeneous=(shader_features()&SF_VOLUME_HETEROGENEOUS).any();
  // Biased ray marching (VOLUME_RAY_MARCHING, DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME_RAY_MARCHING): shade_volume_ray_marching
  // integrates every path in place of shade_volume and shade_volume_heterogeneous (the per-object step sizes in the
  // scene tables), with the heterogeneous entry's closure arenas and records.
  const bool ray_marching=data.integrator.volume_ray_marching!=0;
  if(ray_marching)require(data.integrator.volume_max_steps>0,"Invalid volume ray marching step count");
  const bool phase_stage=(shader_features()&(SF_VOLUME_PHASES|SF_VOLUME_HETEROGENEOUS)).any();
  if(light_stage){
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
    for(unsigned slot=0;slot<data.volume_stack_size;slot++){validate(uintptr_t(state.shadow_volume_stack[slot].object),size_t(p[25])*4);validate(uintptr_t(state.shadow_volume_stack[slot].shader),size_t(p[25])*4);}
    validate(uintptr_t(state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].object),size_t(p[25])*4);
    validate(uintptr_t(state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].prim),size_t(p[25])*4);
    validate(uintptr_t(state.next_shadow_path_index),4);
    require(state.shadow_path.bsdf_eval_average||(data.kernel_features&KERNEL_FEATURE_LIGHT_TREE),"Missing shadow BSDF average");
  }
  if(!work)return;
  // Heterogeneous volumes: the evaluation scratch plus the direct and the indirect phases of every input.
  const size_t bytes=size_t(n)*MAX_CLOSURE*sizeof(SurfaceClosureRecord)*(heterogeneous?3:1);
  if(volume_closure_bytes<bytes){if(volume_closures){flush();release(volume_closures);}volume_closures=allocate(bytes);volume_closure_bytes=bytes;}p[23]=volume_closures;
  if(ray_marching){
    if(!pipeline_current(volume_ray_marching,"shade_volume_ray_marching")||!pipeline_current(volume_phase,"shade_volume_phase")){
      auto built=build_pipelines("volume",volume_source(),{"shade_volume_ray_marching","shade_volume_phase"});
      volume_ray_marching=std::move(built[0]);volume_phase=std::move(built[1]);
    }
  }
  else if(heterogeneous){
    if(!pipeline_current(volume,"shade_volume")||!pipeline_current(volume_phase,"shade_volume_phase")||!pipeline_current(volume_heterogeneous,"shade_volume_heterogeneous")){
      auto built=build_pipelines("volume",volume_source(),{"shade_volume","shade_volume_phase","shade_volume_heterogeneous"});
      volume=std::move(built[0]);volume_phase=std::move(built[1]);volume_heterogeneous=std::move(built[2]);
    }
  }
  else if(phase_stage){
    if(!pipeline_current(volume,"shade_volume")||!pipeline_current(volume_phase,"shade_volume_phase")){
      auto built=build_pipelines("volume",volume_source(),{"shade_volume","shade_volume_phase"});
      volume=std::move(built[0]);volume_phase=std::move(built[1]);
    }
  }
  else if(!pipeline_current(volume,"shade_volume"))volume=std::move(build_pipelines("volume",volume_source(),{"shade_volume"})[0]);
  if(light_stage||phase_stage){
    // One record per input plus the reserved shadow slot base (control op 5, as the surface stage: every
    // input gets a fixed shadow slot n + ..., in a run-independent order).
    const size_t record_bytes=size_t(n)*volume_light_record_bytes+sizeof(uint32_t);
    if(volume_light_record_size<record_bytes){if(volume_light_records){flush();release(volume_light_records);}volume_light_records=allocate(record_bytes);volume_light_record_size=record_bytes;}
    p[24]=volume_light_records;p[28]=volume_light_records+uint32_t(size_t(n)*volume_light_record_bytes);
  }
  if(light_stage){
    // Background light portals (BACKGROUND_PORTAL): the background light's sample with the portal strategy runs in
    // shade_volume_light_background before shade_volume_light, whose module variant leaves it pending
    // (SF_BACKGROUND_SAMPLE_ENTRY), as the surface light entries do (compile envelope).
    const bool background_stage=(shader_features()&SF_BACKGROUND_PORTAL).any();
    if(!pipeline_current(volume_segment,"shade_volume_segment")||!pipeline_current(volume_light,"shade_volume_light")){
      if(background_stage){
        auto built=build_pipelines("surface",surface_source(),{"shade_volume_segment","shade_volume_light","shade_volume_light_background"},SF_BACKGROUND_SAMPLE_ENTRY);
        volume_segment=std::move(built[0]);volume_light=std::move(built[1]);volume_light_background=std::move(built[2]);
      }else{
      auto built=build_pipelines("surface",surface_source(),{"shade_volume_segment","shade_volume_light"});
      volume_segment=std::move(built[0]);volume_light=std::move(built[1]);volume_light_background={};
      }
    }
    if(data.integrator.use_light_tree&&!pipeline_current(volume_tree_select,"shade_volume_tree_select"))
      volume_tree_select=std::move(build_pipelines("surface",surface_source(),{"shade_volume_tree_select"})[0]);
    if(triangle_light_stage&&!pipeline_current(volume_light_triangle,"shade_volume_light_triangle"))
      volume_light_triangle=std::move(build_pipelines("surface",surface_source(),{"shade_volume_light_triangle"})[0]);
    std::array<uint32_t,32> reserve{};reserve[0]=5;reserve[1]=uint32_t(n);
    reserve[2]=uint32_t(uintptr_t(state.next_shadow_path_index));reserve[3]=uint32_t(uintptr_t(state.shadow_path.queued_kernel));
    reserve[4]=p[28];reserve[17]=error_word;reserve[18]=p[25];dispatch(reserve,1);
    if(data.integrator.use_light_tree)dispatch(p,uint32_t(work),volume_tree_select,workgroup("shade_volume_tree_select"));
    dispatch(p,uint32_t(work),volume_segment,workgroup("shade_volume_segment"));
  }
  // The shadow catcher split of paths whose segment ends on a catcher (integrator_intersect_next_kernel_after_volume).
  if(data.integrator.has_shadow_catcher)p[29]=shadow_catcher_marks(p[10]);
  if(ray_marching)dispatch(p,uint32_t(work),volume_ray_marching,workgroup("shade_volume_ray_marching"));
  else{
    dispatch(p,uint32_t(work),volume,workgroup("shade_volume"));
    if(heterogeneous)dispatch(p,uint32_t(work),volume_heterogeneous,workgroup("shade_volume_heterogeneous"));
  }
  if(triangle_light_stage)dispatch(p,uint32_t(work),volume_light_triangle,workgroup("shade_volume_light_triangle"));
  if(light_stage&&volume_light_background.value)dispatch(p,uint32_t(work),volume_light_background,workgroup("shade_volume_light_background"));
  if(light_stage)dispatch(p,uint32_t(work),volume_light,workgroup("shade_volume_light"));
  if(phase_stage)dispatch(p,uint32_t(work),volume_phase,workgroup("shade_volume_phase"));
  split_shadow_catcher(p[10]);
}
}

namespace ccl {
void WebGPUDevice::enqueue_volume_stack(int work,const DeviceKernelArguments &args){
  require(args.count==2,"Volume stack argument count");require(args.types[1]==DeviceKernelArguments::INT32&&args.sizes[1]==4,"Invalid volume stack work argument");
  int n;std::memcpy(&n,args.values[1],4);require(n>=0&&work>=n,"Incomplete volume stack dispatch");
  require(args.types[0]==DeviceKernelArguments::POINTER&&args.sizes[0]==sizeof(device_ptr),"Invalid volume stack pointer argument");
  device_ptr indices;std::memcpy(&indices,args.values[0],sizeof(indices));require(indices%4==0,"Unaligned volume stack indices");if(indices)validate(indices,size_t(n)*4);
  require(data.kernel_features&KERNEL_FEATURE_VOLUME,"Volume stack without native volume feature");validate_volume();
  std::array<uint32_t,32> p{};p[1]=n;p[2]=uint32_t(indices);p[6]=constants.at("data");p[7]=constants.at("integrator_state");p[17]=error_word;p[14]=scene_tables();
  p[10]=uint32_t(remaining(uintptr_t(state.path.render_pixel_index))/4);if(!indices)require(uint32_t(n)<=p[10],"Volume stack dispatch exceeds path capacity");
  validate(uintptr_t(state.ray.P),size_t(p[10])*sizeof(packed_float3));validate(uintptr_t(state.path.queued_kernel),size_t(p[10])*sizeof(uint16_t));validate(uintptr_t(state.queue_counter),sizeof(IntegratorQueueCounter));
  // A shadow catcher path's stack re-init continues with the surface (the sort key counters, as enqueue_volume's).
  for(auto [kernel,slot]:{std::pair{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,15},std::pair{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,16}}){if(state.sort_key_counter[kernel])p[slot]=uint32_t(remaining(uintptr_t(state.sort_key_counter[kernel]))/4);}
  if(!work||!n)return;
  if(!pipeline_current(volume_stack,"intersect_volume_stack"))volume_stack=std::move(build_pipelines("volume_stack",volume_stack_source(),{"intersect_volume_stack"})[0]);
  dispatch(p,uint32_t(work),volume_stack,workgroup("intersect_volume_stack"));
}
}

namespace ccl {
void WebGPUDevice::enqueue_volume_density(int work,const DeviceKernelArguments &args){
  require(args.count==5,"Volume density argument count");
  auto scalar=[&](unsigned i){require(args.types[i]==DeviceKernelArguments::INT32&&args.sizes[i]==4,"Invalid volume density integer");int value;std::memcpy(&value,args.values[i],4);require(value>=0,"Negative volume density range");return uint32_t(value);};
  const uint32_t offset=scalar(3),count=scalar(4);require(uint32_t(work)>=count,"Incomplete volume density dispatch");
  auto pointer=[&](unsigned i,size_t bytes){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid volume density pointer");device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));validate(address,bytes);require(address%4==0,"Unaligned volume density pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};p[1]=count;p[2]=pointer(0,(size_t(offset)+count)*2*sizeof(KernelShaderEvalInput));p[3]=offset;p[4]=pointer(1,(size_t(offset)+count)*8);pointer(2,4);
  p[6]=constants.at("data");p[14]=scene_tables();p[17]=error_word;p[18]=globals.at("shaders");p[19]=uint32_t(remaining(p[18])/sizeof(KernelShader));p[21]=globals.at("svm_nodes");p[22]=uint32_t(remaining(p[21])/4);
  if(!count)return;
  if(!pipeline_current(volume_density,"shader_eval_volume_density"))volume_density=std::move(build_pipelines("volume",volume_source(),{"shader_eval_volume_density"})[0]);
  // One SVM stack slot per invocation: at most svm_stack_slots inputs per dispatch (the heterogeneous octree's voxels
  // outnumber them), each chunk at its input offset (push word 3).
  for(uint32_t first=0;first<count;first+=svm_stack_slots){
    p[1]=std::min<uint32_t>(svm_stack_slots,count-first);p[3]=offset+first;
    dispatch(p,p[1],volume_density,workgroup("shader_eval_volume_density"));
  }
}
}
