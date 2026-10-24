// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "wavefront.h"
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace ccl {
static constexpr char background_source[]=
#include "background_shader.inc"
;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
std::string background_layout(){
  std::ostringstream source;
  auto constant=[&](const char *name,size_t value){source<<"const "<<name<<":u32="<<value<<"u;\n";};
#define DATA(parent,member) constant("D_" #parent "_" #member,offsetof(KernelData,parent.member))
  DATA(film,pass_emission);DATA(integrator,transparent_max_bounce);
  DATA(background,surface_shader);DATA(background,transparent);DATA(background,lightgroup);
  DATA(integrator,ao_bounces_factor);DATA(integrator,sample_clamp_direct);DATA(integrator,sample_clamp_indirect);
  DATA(film,pass_flag);DATA(film,light_pass_flag);DATA(film,pass_combined);DATA(film,pass_background);
  DATA(film,pass_diffuse_direct);DATA(film,pass_glossy_direct);DATA(film,pass_transmission_direct);
  DATA(film,pass_diffuse_indirect);DATA(film,pass_glossy_indirect);DATA(film,pass_transmission_indirect);
  DATA(film,pass_lightgroup);DATA(film,pass_depth);DATA(film,pass_position);DATA(film,pass_object_id);DATA(film,pass_material_id);
  // Background light importance map and MIS (wavefront_background_light.wgsl).
  DATA(background,use_mis);DATA(background,map_res_x);DATA(background,map_res_y);DATA(background,map_weight);
  DATA(background,portal_weight);DATA(background,sun_weight);DATA(background,map_dD);
#undef DATA
  constant("O_lightgroup",offsetof(KernelObject,lightgroup));
  constant("SH_constant_emission",offsetof(KernelShader,constant_emission));
#define ENUM(name) constant(#name,uint32_t(name))
  ENUM(SD_HAS_CONSTANT_EMISSION);ENUM(SHADER_EXCLUDE_DIFFUSE);ENUM(SHADER_EXCLUDE_GLOSSY);ENUM(SHADER_EXCLUDE_TRANSMIT);ENUM(SHADER_EXCLUDE_CAMERA);ENUM(SHADER_EXCLUDE_SCATTER);
  ENUM(PATH_RAY_MIS_SKIP);ENUM(PATH_RAY_MIS_HAD_TRANSMISSION);
  ENUM(PATH_RAY_REFLECT);ENUM(PATH_RAY_TRANSPARENT_BACKGROUND);ENUM(PATH_RAY_ANY_PASS);ENUM(PATH_RAY_SURFACE_PASS);ENUM(PATH_RAY_SINGLE_PASS_DONE);
  constant("BACKGROUND_UNSUPPORTED_FLAGS",PATH_RAY_SHADOW_CATCHER_PASS|PATH_RAY_SHADOW_CATCHER_HIT|PATH_RAY_SHADOW_CATCHER_BACKGROUND|
    PATH_RAY_DENOISING_FEATURES);
  ENUM(PATH_RAY_VISIBILITY_VOLUME_SCATTER);ENUM(LIGHT_BACKGROUND);constant("BGL_IMPORTANCE_BAKE",PATH_RAY_IMPORTANCE_BAKE);
#undef ENUM
#define PASS(name) constant("MASK_" #name,uint32_t(PASSMASK(name)))
  PASS(COMBINED);PASS(BACKGROUND);PASS(DEPTH);PASS(POSITION);PASS(OBJECT_ID);PASS(MATERIAL_ID);
#undef PASS
  return source.str();
}
<<<<<<< ours(main)
std::string background_wgsl(){return wavefront_layout()+shader_data_layout()+background_layout()+lighting_layout()+svm_layout()+shadow_catcher_layout()+background_source;}
=======
std::string background_wgsl(){return wavefront_layout()+shader_data_layout()+background_layout()+lighting_layout()+svm_layout()+mnee_layout()+background_source;}
>>>>>>> theirs(m2-mnee)
void WebGPUDevice::enqueue_background(int work,const DeviceKernelArguments &args,DeviceKernel kernel){
  require(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND||kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD,"Invalid emission kernel");
  require(args.count==3,"Background argument count");
  require(args.types[2]==DeviceKernelArguments::INT32&&args.sizes[2]==4,"Invalid background work argument");
  int n;std::memcpy(&n,args.values[2],4);require(n>=0&&work>=n,"Incomplete background dispatch");
  auto pointer=[&](unsigned i,bool nullable){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid background pointer argument");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));if(address||!nullable)validate(address,4);
    require(address%4==0,"Unaligned background pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};p[1]=n;p[2]=pointer(0,true);p[4]=pointer(1,false);p[17]=error_word;
  if(p[2])validate(p[2],size_t(n)*4);
  validate_volume();
  validate_lighting(false);
<<<<<<< ours(main)
  require(!(data.kernel_features&(KERNEL_FEATURE_PATH_GUIDING|KERNEL_FEATURE_DENOISING|KERNEL_FEATURE_MNEE|KERNEL_FEATURE_SHADOW_LINKING))&&
          !data.integrator.use_caustics,"WebGPU background optional integration features not implemented");
  if(data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)require(state.path.mis_ray_object,"Missing light linking path state");
  validate_shadow_catcher();
=======
  require(!(data.kernel_features&(KERNEL_FEATURE_PATH_GUIDING|KERNEL_FEATURE_DENOISING|KERNEL_FEATURE_LIGHT_LINKING|KERNEL_FEATURE_SHADOW_LINKING))&&
          !data.integrator.has_shadow_catcher,"WebGPU background optional integration features not implemented");
  if(data.integrator.use_caustics)validate_mnee();
>>>>>>> theirs(m2-mnee)
  require(!(data.film.pass_flag&PASSMASK(MOTION)),"WebGPU background motion pass not implemented");
  require(data.film.pass_stride>0,"Invalid background film stride");
  auto pass=[&](int offset,int length){require(offset==PASS_UNUSED||(offset>=0&&offset<=data.film.pass_stride-length),"Invalid background film pass offset");};
  pass(data.film.pass_emission,3);
  pass(data.film.pass_combined,4);pass(data.film.pass_background,3);pass(data.film.pass_adaptive_aux_buffer,4);
  pass(data.film.pass_diffuse_direct,3);pass(data.film.pass_glossy_direct,3);pass(data.film.pass_transmission_direct,3);
  pass(data.film.pass_diffuse_indirect,3);pass(data.film.pass_glossy_indirect,3);pass(data.film.pass_transmission_indirect,3);
  pass(data.film.pass_depth,1);pass(data.film.pass_position,3);pass(data.film.pass_object_id,1);pass(data.film.pass_material_id,1);
  if(data.background.lightgroup!=LIGHTGROUP_NONE){require(data.background.lightgroup>=0&&data.background.lightgroup<data.film.pass_stride/3,"Invalid background lightgroup");
    pass(data.film.pass_lightgroup,3*(data.background.lightgroup+1));}
  if(data.film.light_pass_flag&PASSMASK(COMBINED))require(data.film.pass_combined!=PASS_UNUSED,"Missing combined film pass");
  for(auto [mask,offset]:{std::pair{PASSMASK(DEPTH),data.film.pass_depth},std::pair{PASSMASK(POSITION),data.film.pass_position},
    std::pair{PASSMASK(OBJECT_ID),data.film.pass_object_id},std::pair{PASSMASK(MATERIAL_ID),data.film.pass_material_id}})
    require(!(data.film.pass_flag&mask)||offset!=PASS_UNUSED,"Missing background data pass");
  require(std::isfinite(data.integrator.sample_clamp_direct)&&data.integrator.sample_clamp_direct>=0&&
          std::isfinite(data.integrator.sample_clamp_indirect)&&data.integrator.sample_clamp_indirect>=0,"Invalid background clamp");
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");p[18]=globals.at("shaders");
  require(remaining(p[18])%sizeof(KernelShader)==0,"Invalid background shader table");p[19]=uint32_t(remaining(p[18])/sizeof(KernelShader));
  p[20]=(data.kernel_features&KERNEL_FEATURE_LIGHT_PASSES)!=0;
  if(auto it=globals.find("svm_nodes");it!=globals.end()){
    p[21]=it->second;require(remaining(p[21])%4==0,"Invalid SVM word table");p[22]=uint32_t(remaining(p[21])/4);
  }
  p[10]=uint32_t(remaining(uintptr_t(state.path.render_pixel_index))/4);p[11]=uint32_t(remaining(p[4])/4);
  require(p[11]>=uint32_t(data.film.pass_stride),"Background film smaller than one pixel");
  if(!p[2])require(uint32_t(n)<=p[10],"Background dispatch exceeds path capacity");
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
  p[14]=scene_tables();
  // Shadow catcher background paths continue to the sorted surface kernel.
  if(data.integrator.has_shadow_catcher&&state.sort_key_counter[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE])
    p[15]=uint32_t(remaining(uintptr_t(state.sort_key_counter[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE]))/4);
  if(!work)return;
  auto &pipeline=kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND?background:light_forward;
  const char *entry=kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND?"shade_background":"shade_light_forward";
  // Light tree: light_tree_forward writes each path's forward tree pdfs (the
  // background light, then every MIS sun; or the lamp hit) before the entry reads them.
  if((shader_features()&SF_LIGHT_TREE).any()){
    uint32_t slots=1;for(const KernelLight &light:light_records)slots+=light.type==LIGHT_SUN&&(light.shader_id&SHADER_USE_MIS);
    const size_t needed=size_t(p[10])*slots*sizeof(float);
    if(needed>light_tree_forward_bytes){
      if(light_tree_forward_scratch){flush();release(light_tree_forward_scratch);light_tree_forward_scratch=0;light_tree_forward_bytes=0;}
      light_tree_forward_scratch=allocate(needed);light_tree_forward_bytes=needed;
    }
    p[28]=light_tree_forward_scratch;p[29]=slots;
    if(!pipeline_current(light_tree_forward,"light_tree_forward"))light_tree_forward=std::move(build_pipelines("background",background_wgsl(),{"light_tree_forward"})[0]);
    dispatch(p,uint32_t(work),light_tree_forward,workgroup("light_tree_forward"));
  }
  if(!pipeline_current(pipeline,entry))pipeline=std::move(build_pipelines("background",background_wgsl(),{entry})[0]);
  dispatch(p,uint32_t(work),pipeline,workgroup(entry));
}
// DEVICE_KERNEL_SHADER_EVAL_BACKGROUND (integrator/shader_eval.cpp eval_gpu, for
// LightManager::device_update_background's importance map): arguments are the
// KernelShaderEvalInput array, the float output, the cache-miss word (always
// false: no tiled images), the chunk offset and the chunk size. Each dispatch
// covers at most svm_stack_slots items (one SVM stack slot per invocation).
void WebGPUDevice::enqueue_background_eval(int work,const DeviceKernelArguments &args){
  require(args.count==5,"Background evaluation argument count");
  auto scalar=[&](unsigned i){require(args.types[i]==DeviceKernelArguments::INT32&&args.sizes[i]==4,"Invalid background evaluation integer");
    int value;std::memcpy(&value,args.values[i],4);require(value>=0,"Negative background evaluation range");return uint32_t(value);};
  const uint32_t offset=scalar(3),count=scalar(4);require(uint32_t(work)>=count,"Incomplete background evaluation dispatch");
  auto pointer=[&](unsigned i,size_t bytes){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid background evaluation pointer");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));validate(address,bytes);require(address%4==0,"Unaligned background evaluation pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};p[1]=count;p[3]=offset;p[17]=error_word;
  p[2]=pointer(0,(size_t(offset)+count)*sizeof(KernelShaderEvalInput));p[4]=pointer(1,(size_t(offset)+count)*3*sizeof(float));
  {uint32_t miss=pointer(2,4);uint32_t zero=0;upload(miss,&zero,4);}
  require(data.background.map_res_x>0&&data.background.map_res_y>0,"Background evaluation without an importance map");
  require(!data.background.use_sun_guiding||data.background.sun_weight==0,"WebGPU background sun guiding (sky texture sun disc) not implemented");
  // Runs during scene preparation, before any integrator state exists (p[7] stays 0).
  p[6]=constants.at("data");p[18]=globals.at("shaders");
  require(remaining(p[18])%sizeof(KernelShader)==0,"Invalid background shader table");p[19]=uint32_t(remaining(p[18])/sizeof(KernelShader));
  require(uint32_t(data.background.surface_shader&SHADER_MASK)<p[19],"Invalid background shader");
  if(auto it=globals.find("svm_nodes");it!=globals.end()){p[21]=it->second;require(remaining(p[21])%4==0,"Invalid SVM word table");p[22]=uint32_t(remaining(p[21])/4);}
  p[14]=scene_tables();
  if(!count||!work)return;
  if(!pipeline_current(background_eval,"shader_eval_background"))background_eval=std::move(build_pipelines("background",background_wgsl(),{"shader_eval_background"})[0]);
  for(uint32_t first=0;first<count;first+=svm_stack_slots){
    p[30]=first;dispatch(p,std::min<uint32_t>(svm_stack_slots,count-first),background_eval,workgroup("shader_eval_background"));
  }
}
}
