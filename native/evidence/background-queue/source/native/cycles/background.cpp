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
static std::string background_layout(){
  std::ostringstream source;
  auto constant=[&](const char *name,size_t value){source<<"const "<<name<<":u32="<<value<<"u;\n";};
#define DATA(parent,member) constant("D_" #parent "_" #member,offsetof(KernelData,parent.member))
  DATA(background,surface_shader);DATA(background,transparent);DATA(background,lightgroup);
  DATA(integrator,ao_bounces_factor);DATA(integrator,sample_clamp_direct);DATA(integrator,sample_clamp_indirect);
  DATA(film,pass_flag);DATA(film,light_pass_flag);DATA(film,pass_combined);DATA(film,pass_background);
  DATA(film,pass_diffuse_direct);DATA(film,pass_glossy_direct);DATA(film,pass_transmission_direct);
  DATA(film,pass_diffuse_indirect);DATA(film,pass_glossy_indirect);DATA(film,pass_transmission_indirect);
  DATA(film,pass_lightgroup);DATA(film,pass_depth);DATA(film,pass_position);DATA(film,pass_object_id);DATA(film,pass_material_id);
#undef DATA
  constant("SH_constant_emission",offsetof(KernelShader,constant_emission));
#define ENUM(name) constant(#name,uint32_t(name))
  ENUM(SD_HAS_CONSTANT_EMISSION);ENUM(SHADER_EXCLUDE_DIFFUSE);ENUM(SHADER_EXCLUDE_GLOSSY);ENUM(SHADER_EXCLUDE_TRANSMIT);ENUM(SHADER_EXCLUDE_CAMERA);
  ENUM(PATH_RAY_REFLECT);ENUM(PATH_RAY_TRANSPARENT_BACKGROUND);ENUM(PATH_RAY_ANY_PASS);ENUM(PATH_RAY_SURFACE_PASS);ENUM(PATH_RAY_SINGLE_PASS_DONE);
  constant("BACKGROUND_UNSUPPORTED_FLAGS",PATH_RAY_SHADOW_CATCHER_PASS|PATH_RAY_SHADOW_CATCHER_HIT|PATH_RAY_SHADOW_CATCHER_BACKGROUND|
    PATH_RAY_VOLUME_PASS|PATH_RAY_VOLUME_PRIMARY_TRANSMIT|PATH_RAY_DENOISING_FEATURES);
  ENUM(PATH_RAY_VISIBILITY_VOLUME_SCATTER);
#undef ENUM
#define PASS(name) constant("MASK_" #name,uint32_t(PASSMASK(name)))
  PASS(COMBINED);PASS(BACKGROUND);PASS(DEPTH);PASS(POSITION);PASS(OBJECT_ID);PASS(MATERIAL_ID);
#undef PASS
  return source.str();
}
void WebGPUDevice::enqueue_background(int work,const DeviceKernelArguments &args){
  require(args.count==3,"Background argument count");
  require(args.types[2]==DeviceKernelArguments::INT32&&args.sizes[2]==4,"Invalid background work argument");
  int n;std::memcpy(&n,args.values[2],4);require(n>=0&&work>=n,"Incomplete background dispatch");
  auto pointer=[&](unsigned i,bool nullable){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid background pointer argument");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));if(address||!nullable)validate(address,4);
    require(address%4==0,"Unaligned background pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};p[1]=n;p[2]=pointer(0,true);p[4]=pointer(1,false);p[17]=error_word;
  if(p[2])validate(p[2],size_t(n)*4);
  require(!data.background.use_mis&&data.integrator.num_lights==0,"WebGPU background light MIS and sun lights not implemented");
  require(!(data.kernel_features&(KERNEL_FEATURE_VOLUME|KERNEL_FEATURE_PATH_GUIDING|KERNEL_FEATURE_DENOISING|KERNEL_FEATURE_MNEE|KERNEL_FEATURE_LIGHT_LINKING|KERNEL_FEATURE_SHADOW_LINKING))&&
          !data.integrator.has_shadow_catcher&&!data.integrator.use_caustics,"WebGPU background optional integration features not implemented");
  require(!(data.film.pass_flag&PASSMASK(MOTION)),"WebGPU background motion pass not implemented");
  require(data.film.pass_stride>0,"Invalid background film stride");
  auto pass=[&](int offset,int length){require(offset==PASS_UNUSED||(offset>=0&&offset<=data.film.pass_stride-length),"Invalid background film pass offset");};
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
  if(!work)return;
  if(!background.value){
    const std::string code=wavefront_layout()+background_layout()+background_source;WGPUShaderSourceWGSL source{};
    source.chain.sType=WGPUSType_ShaderSourceWGSL;source.code={code.data(),code.size()};WGPUShaderModuleDescriptor sd{};sd.nextInChain=&source.chain;
    gpu.push_scope();cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> shader(wgpuDeviceCreateShaderModule(gpu.get(),&sd));gpu.pop_scope();
    background=gpu.pipeline(shader,pipeline_layout,"shade_background");
  }
  dispatch(p,uint32_t(work),background);
}
}
