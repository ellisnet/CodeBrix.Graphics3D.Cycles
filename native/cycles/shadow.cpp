// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "wavefront.h"
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace ccl {
static constexpr char fragment[]=
#include "shadow_shader.inc"
;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
std::string shadow_layout(){
  std::ostringstream out;
  auto constant=[&](const char *name,size_t value){out<<"const "<<name<<":u32="<<value<<"u;\n";};
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) constant("S_" #parent "_" #name,offsetof(IntegratorStateGPU,parent.name));
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
  constant("S_shadow_self_object",offsetof(IntegratorStateGPU,shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].object));
  constant("S_shadow_self_prim",offsetof(IntegratorStateGPU,shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].prim));
  constant("D_integrator_transparent_shadows",offsetof(KernelData,integrator.transparent_shadows));
  constant("SHADOW_ONLY_VOLUME",SD_HAS_ONLY_VOLUME);constant("SHADOW_VOLUME_BOUNDS_MAX",VOLUME_BOUNDS_MAX);
  constant("SHADOW_HIT_CAPACITY",INTEGRATOR_SHADOW_ISECT_SIZE_GPU);constant("SHADOW_HIT_STRIDE",sizeof(IntegratorStateGPU{}.shadow_isect[0]));
#define HIT(member) constant("S_shadow_hit_" #member,offsetof(IntegratorStateGPU,shadow_isect[0].member))
  HIT(t);HIT(u);HIT(v);HIT(prim);HIT(object);HIT(type);
#undef HIT
  constant("S_next_shadow_path_index",offsetof(IntegratorStateGPU,next_shadow_path_index));
  constant("K_SHADOW_INTERSECT",DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW);
  constant("K_SHADOW_SHADE",DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW);
  constant("SHADOW_UNSUPPORTED_FLAGS",PATH_RAY_SHADOW_CATCHER_PASS|PATH_RAY_SHADOW_CATCHER_HIT|PATH_RAY_SHADOW_CATCHER_BACKGROUND|
    PATH_RAY_VOLUME_PASS|PATH_RAY_VOLUME_PRIMARY_TRANSMIT);
  // Ambient occlusion shadow rays (integrate_surface_ao, film_write_direct_light):
  // shared by the surface module (ray setup) and the shadow module (film writes).
  constant("AO_SHADOW_FLAG",PATH_RAY_SHADOW_FOR_AO);constant("AO_FEATURE_ADDITIVE",KERNEL_FEATURE_AO_ADDITIVE);constant("AO_FEATURE_PASS",KERNEL_FEATURE_AO_PASS);
  constant("D_integrator_ao_additive_factor",offsetof(KernelData,integrator.ao_additive_factor));constant("D_film_pass_ao",offsetof(KernelData,film.pass_ao));
  // The shadow path's volume stack (VOLUME_SHADOW regions, shadow_state_template.h shadow_volume_stack).
  out<<"//#if VOLUME_SHADOW\n";
  constant("VL_S_shadow_volume_stack",offsetof(IntegratorStateGPU,shadow_volume_stack));
  constant("VL_SVS_stride",sizeof(IntegratorStateGPU{}.shadow_volume_stack[0]));
  constant("VL_SVS_object",offsetof(std::remove_reference_t<decltype(IntegratorStateGPU{}.shadow_volume_stack[0])>,object));
  constant("VL_SVS_shader",offsetof(std::remove_reference_t<decltype(IntegratorStateGPU{}.shadow_volume_stack[0])>,shader));
  constant("VL_SHADOW_VOLUME_FLAGS",PATH_RAY_VOLUME_PASS|PATH_RAY_VOLUME_PRIMARY_TRANSMIT);constant("VL_PRIMITIVE_VOLUME",PRIMITIVE_VOLUME);
  out<<"//#endif\n";
  return out.str();
}
std::string shadow_source(){return svm_surface_source()+background_layout()+shadow_layout()+shadow_catcher_layout()+fragment;}
void WebGPUDevice::enqueue_shadow(DeviceKernel kernel,int work,const DeviceKernelArguments &args){
  const bool shading=kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW;
  require(args.count==(shading?3u:2u),"Shadow argument count");
  const unsigned count_arg=shading?2:1;
  require(args.types[count_arg]==DeviceKernelArguments::INT32&&args.sizes[count_arg]==4,"Invalid shadow work argument");
  int n;std::memcpy(&n,args.values[count_arg],4);require(n>=0&&work>=n,"Incomplete shadow dispatch");
  auto pointer=[&](unsigned i,bool nullable){
    require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid shadow pointer argument");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));if(address||!nullable)validate(address,4);
    require(address%4==0,"Unaligned shadow pointer");return uint32_t(address);
  };
  std::array<uint32_t,32> p{};p[1]=n;p[2]=pointer(0,true);p[17]=error_word;
  if(p[2])validate(p[2],size_t(n)*4);
  validate_points();
  validate_curves();
  validate_geometry_motion();
  // Transparent shadows with motion blur trace at the shadow ray's time (SHADOW_TRANSPARENT_MOTION). Shadow
  // paths through volumes carry the volume stack (VOLUME_SHADOW: homogeneous media, heterogeneous media through
  // the majorant octree, ray-marched media; the other volume limits are refused in validate_volume); linking, AO
  // and shadow catchers remain guarded below.
  if(data.integrator.transparent_shadows)require(data.integrator.transparent_max_bounce>=0&&data.integrator.transparent_max_bounce<=4095,"WebGPU transparent shadow hit limit exceeds packed state");
  require(!(data.kernel_features&KERNEL_FEATURE_PATH_GUIDING),"WebGPU shadow optional integration features not implemented");
  validate_shadow_linking();
  if(data.kernel_features&KERNEL_FEATURE_VOLUME)validate_volume();
  validate_shadow_catcher();
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");p[14]=scene_tables();
  if(data.integrator.transparent_shadows&&shading){
    if(auto it=globals.find("svm_nodes");it!=globals.end()){p[21]=it->second;require(remaining(p[21])%4==0,"Invalid shadow SVM table");p[22]=uint32_t(remaining(p[21])/4);}
  }
  // Heterogeneous volumes (VOLUME_HETEROGENEOUS): the shadow transmittance estimator draws its expansion order and
  // shading offsets from the sampling pattern (tabulated Sobol reads push word 9; the other patterns compute them,
  // VOLUME_SAMPLING_PATTERN regions).
  if(shading&&(shader_features()&SF_VOLUME_HETEROGENEOUS).any()){p[9]=sample_pattern_table("shadow");
    require(data.integrator.sampling_pattern==SAMPLING_PATTERN_TABULATED_SOBOL||(shader_features()&SF_VOLUME_SAMPLING_PATTERN).any(),"Shadow volume sampling pattern without its specialization");}
  p[10]=uint32_t(remaining(uintptr_t(state.shadow_path.render_pixel_index))/4);
  if(!p[2])require(uint32_t(n)<=p[10],"Shadow dispatch exceeds path capacity");
  auto field=[&](const void *address,size_t size){validate(uintptr_t(address),size_t(p[10])*size);};
  field(state.shadow_path.queued_kernel,2);field(state.shadow_path.flag,4);field(state.shadow_path.packed_num_hits,2);
  if(data.integrator.transparent_shadows){
    field(state.shadow_path.transparent_bounce,2);field(state.shadow_path.throughput,12);field(state.shadow_path.rng_offset,2);field(state.shadow_path.volume_bounds_bounce,2);
    field(state.shadow_ray.time,4);field(state.shadow_ray.dP,4);field(state.shadow_ray.dD,4);field(state.shadow_ray.D,12);field(state.shadow_ray.tmin,4);
    for(unsigned i=0;i<INTEGRATOR_SHADOW_ISECT_SIZE_GPU;i++){field(state.shadow_isect[i].t,4);field(state.shadow_isect[i].u,4);field(state.shadow_isect[i].v,4);field(state.shadow_isect[i].object,4);field(state.shadow_isect[i].prim,4);field(state.shadow_isect[i].type,4);}
    if(data.kernel_features&KERNEL_FEATURE_VOLUME)
      for(unsigned slot=0;slot<data.volume_stack_size;slot++){field(state.shadow_volume_stack[slot].object,4);field(state.shadow_volume_stack[slot].shader,4);}
  }
  if(shading){
    p[4]=pointer(1,false);p[11]=uint32_t(remaining(p[4])/4);
    require(data.film.pass_stride>0&&p[11]>=uint32_t(data.film.pass_stride),"Invalid shadow film stride");
    auto pass=[&](int offset,int length){require(offset==PASS_UNUSED||(offset>=0&&offset<=data.film.pass_stride-length),"Invalid shadow film pass offset");};
    pass(data.film.pass_combined,4);pass(data.film.pass_adaptive_aux_buffer,4);pass(data.film.pass_lightgroup,3);
    pass(data.film.pass_diffuse_direct,3);pass(data.film.pass_glossy_direct,3);pass(data.film.pass_transmission_direct,3);
    pass(data.film.pass_diffuse_indirect,3);pass(data.film.pass_glossy_indirect,3);pass(data.film.pass_transmission_indirect,3);
    pass(data.film.pass_volume_direct,3);pass(data.film.pass_volume_indirect,3);
    if(data.film.light_pass_flag&PASSMASK(COMBINED))require(data.film.pass_combined!=PASS_UNUSED,"Missing shadow combined film pass");
    require(std::isfinite(data.integrator.sample_clamp_direct)&&data.integrator.sample_clamp_direct>=0&&
            std::isfinite(data.integrator.sample_clamp_indirect)&&data.integrator.sample_clamp_indirect>=0,"Invalid shadow clamp");
    field(state.shadow_path.sample,4);field(state.shadow_path.bounce,2);field(state.shadow_path.throughput,12);field(state.shadow_path.lightgroup,1);
    p[20]=(data.kernel_features&KERNEL_FEATURE_LIGHT_PASSES)!=0;
    if(p[20]){field(state.shadow_path.pass_diffuse_weight,12);field(state.shadow_path.pass_glossy_weight,12);}
    validate_ao_shadow(shading,p[10]);
  }else{
    field(state.shadow_ray.time,4);field(state.shadow_ray.P,12);field(state.shadow_ray.D,12);field(state.shadow_ray.tmin,4);field(state.shadow_ray.tmax,4);
    field(state.shadow_ray.self_light_object,4);field(state.shadow_ray.self_light_prim,4);
    field(state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].object,4);field(state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].prim,4);
    p[14]=scene_tables();
  }
  validate(uintptr_t(state.queue_counter),sizeof(IntegratorQueueCounter));
  if(!n||!work)return;
  auto &pipeline=shading?(data.integrator.transparent_shadows?shadow_transparent:shadow_shade):(data.integrator.transparent_shadows&&(shader_features()&SF_CURVE_SHADOW).any())?shadow_intersect_all:shadow_intersect;
  const bool transparent_shading=shading&&data.integrator.transparent_shadows;
  if(transparent_shading){
    // GPU-private ShaderData for every recorded hit of each dispatched path.
    const size_t needed=size_t(n)*INTEGRATOR_SHADOW_ISECT_SIZE_GPU*surface_data_cache_bytes;
    if(needed>shadow_shader_cache_bytes){
      if(shadow_shader_cache){flush();release(shadow_shader_cache);shadow_shader_cache=0;shadow_shader_cache_bytes=0;}
      shadow_shader_cache=allocate(needed);shadow_shader_cache_bytes=needed;
    }
    p[26]=shadow_shader_cache;p[27]=surface_data_cache_bytes;
  }
  // With curves, transparent shadows intersect in their own entry (wavefront_shadow.wgsl, the pipeline size guard).
  const bool shadow_all_entry=!shading&&data.integrator.transparent_shadows&&(shader_features()&SF_CURVE_SHADOW).any();
  const char *entry=shading?(data.integrator.transparent_shadows?"shade_shadow_transparent":"shade_shadow"):shadow_all_entry?"intersect_shadow_all":"intersect_shadow";
  if(transparent_shading){
    // Both transparent entries come from the same specialized shadow module.
    if(!pipeline_current(pipeline,entry,SF_VOLUME_PHASE_KINDS)||!pipeline_current(shadow_shader_data,"prepare_shadow_shader_data",SF_VOLUME_PHASE_KINDS)){
      // Shadow paths allocate no volume closures: the phase families' closure setup is left out (pipeline size guard).
      auto built=build_pipelines("shadow",shadow_source(),{"prepare_shadow_shader_data",entry},SF_NONE,SF_VOLUME_PHASE_KINDS);
      shadow_shader_data=std::move(built[0]);pipeline=std::move(built[1]);
    }
  }else if(!pipeline_current(pipeline,entry,SF_VOLUME_PHASE_KINDS))pipeline=std::move(build_pipelines("shadow",shadow_source(),{entry},SF_NONE,SF_VOLUME_PHASE_KINDS)[0]);
  // Dispatch ordering makes the cached hit ShaderData visible to the SVM stage.
  if(transparent_shading)dispatch(p,uint32_t(work),shadow_shader_data,workgroup("prepare_shadow_shader_data"));
  // Match the smaller transparent SVM workgroup; opaque traversal stays at 64.
  dispatch(p,uint32_t(work),pipeline,workgroup(entry));
}
// DEVICE_KERNEL_SHADER_EVAL_CURVE_SHADOW_TRANSPARENCY (integrator/shader_eval.cpp eval_gpu, for
// Hair::update_shadow_transparency before the BVH build): the KernelShaderEvalInput array, the float output, the
// cache-miss word (always false: no tiled images), the chunk offset and the chunk size, at most svm_stack_slots items per
// dispatch (one SVM stack slot per invocation). Runs in the shadow module, whose SVM has the kernel's feature mask.
void WebGPUDevice::enqueue_curve_shadow_eval(int work,const DeviceKernelArguments &args){
  require(args.count==5,"Curve transparency evaluation argument count");
  auto scalar=[&](unsigned i){require(args.types[i]==DeviceKernelArguments::INT32&&args.sizes[i]==4,"Invalid curve transparency evaluation integer");
    int value;std::memcpy(&value,args.values[i],4);require(value>=0,"Negative curve transparency evaluation range");return uint32_t(value);};
  const uint32_t offset=scalar(3),count=scalar(4);require(uint32_t(work)>=count,"Incomplete curve transparency evaluation dispatch");
  auto pointer=[&](unsigned i,size_t bytes){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid curve transparency evaluation pointer");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));validate(address,bytes);require(address%4==0,"Unaligned curve transparency evaluation pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};p[1]=count;p[3]=offset;p[17]=error_word;
  p[2]=pointer(0,(size_t(offset)+count)*sizeof(KernelShaderEvalInput));p[4]=pointer(1,(size_t(offset)+count)*sizeof(float));
  {uint32_t miss=pointer(2,4);uint32_t zero=0;upload(miss,&zero,4);}
  require(!curve_records.empty()&&globals.count("curve_keys"),"Missing native curve tables");
  // The CPU kernels read the object flags with SD_OBJECT_TRANSFORM_APPLIED set, in memory they share with the scene; here that
  // table's upload is still to come.
  if(object_flag_memory&&object_flag_memory->device_pointer&&object_flag_memory->memory_size())upload(uint32_t(object_flag_memory->device_pointer),object_flag_memory->host_pointer,object_flag_memory->memory_size());
  // Runs during scene preparation, before any integrator state exists (p[7] stays 0).
  p[6]=constants.at("data");p[18]=globals.at("shaders");
  require(remaining(p[18])%sizeof(KernelShader)==0,"Invalid curve transparency shader table");p[19]=uint32_t(remaining(p[18])/sizeof(KernelShader));
  if(auto it=globals.find("svm_nodes");it!=globals.end()){p[21]=it->second;require(remaining(p[21])%4==0,"Invalid SVM word table");p[22]=uint32_t(remaining(p[21])/4);}
  p[14]=scene_tables();
  if(!count||!work)return;
  require((shader_features()&SF_CURVE_SHADOW).any(),"Invalid native curve transparency evaluation without transparent shadows");
  if(!pipeline_current(shadow_curve_eval,"shader_eval_curve_shadow_transparency",SF_VOLUME_PHASE_KINDS))
    shadow_curve_eval=std::move(build_pipelines("shadow",shadow_source(),{"shader_eval_curve_shadow_transparency"},SF_NONE,SF_VOLUME_PHASE_KINDS)[0]);
  for(uint32_t first=0;first<count;first+=svm_stack_slots){
    p[30]=first;dispatch(p,std::min<uint32_t>(svm_stack_slots,count-first),shadow_curve_eval,workgroup("shader_eval_curve_shadow_transparency"));
  }
}
// Ambient occlusion shadow paths (PATH_RAY_SHADOW_FOR_AO): the fields and the
// pass the shadow film write reads (film_write_direct_light's AO branch).
void WebGPUDevice::validate_ao_shadow(bool shading,uint32_t capacity) const{
  if(!(data.kernel_features&KERNEL_FEATURE_AO)||!shading)return;
  auto field=[&](const void *address,size_t size){validate(uintptr_t(address),size_t(capacity)*size);};
  if(data.kernel_features&KERNEL_FEATURE_AO_ADDITIVE){
    require(state.shadow_path.unshadowed_throughput,"Missing AO shadow throughput");field(state.shadow_path.unshadowed_throughput,12);}
  if(data.kernel_features&KERNEL_FEATURE_AO_PASS){
    require(data.film.pass_ao!=PASS_UNUSED&&data.film.pass_ao>=0&&data.film.pass_ao<=data.film.pass_stride-3,"Invalid AO film pass offset");
    field(state.shadow_path.visibility,1);}
}
}
