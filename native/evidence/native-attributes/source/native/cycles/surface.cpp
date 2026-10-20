// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "wavefront.h"
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
static std::string surface_layout(){
  std::ostringstream source;
  auto constant=[&](const char *name,size_t value){source<<"const "<<name<<":u32="<<value<<"u;\n";};
#define DATA(parent,member) constant("D_" #parent "_" #member,offsetof(KernelData,parent.member))
  DATA(integrator,transparent_max_bounce);DATA(integrator,max_bounce);
  DATA(integrator,max_diffuse_bounce);DATA(integrator,max_transmission_bounce);
  DATA(background,transparent_roughness_squared_threshold);DATA(film,pass_emission);
#undef DATA
  constant("O_lightgroup",offsetof(KernelObject,lightgroup));constant("O_shadow_terminator_shading_offset",offsetof(KernelObject,shadow_terminator_shading_offset));
#define ENUM(name) constant(#name,uint32_t(name))
  ENUM(LABEL_NONE);ENUM(LABEL_REFLECT);ENUM(LABEL_TRANSMIT);ENUM(LABEL_DIFFUSE);ENUM(LABEL_TRANSPARENT);ENUM(LABEL_TRANSMIT_TRANSPARENT);
  ENUM(PATH_RAY_MIS_SKIP);ENUM(PATH_RAY_SINGULAR);ENUM(PATH_RAY_IMPORTANCE_BAKE);ENUM(PATH_RAY_MIS_HAD_TRANSMISSION);ENUM(PATH_RAY_DIFFUSE_ANCESTOR);
  ENUM(FILTER_CLOSURE_EMISSION);ENUM(FILTER_CLOSURE_DIFFUSE);ENUM(CLOSURE_NONE_ID);ENUM(NBUILTIN_CLOSURES);
  ENUM(FILTER_CLOSURE_TRANSPARENT);ENUM(SD_OBJECT_HOLDOUT_MASK);ENUM(SD_HAS_ONLY_VOLUME);ENUM(SD_MIS_FRONT);ENUM(SD_MIS_BACK);ENUM(PRNG_SURFACE_BSDF);
#undef ENUM
  constant("SURFACE_UNSUPPORTED_FLAGS",PATH_RAY_SHADOW_CATCHER_PASS|PATH_RAY_SHADOW_CATCHER_HIT|PATH_RAY_SHADOW_CATCHER_BACKGROUND|
    PATH_RAY_VOLUME_PASS|PATH_RAY_VOLUME_PRIMARY_TRANSMIT|PATH_RAY_DENOISING_FEATURES|PATH_RAY_SUBSURFACE);
  return source.str();
}
std::string surface_source(){return svm_surface_source()+background_layout()+surface_layout()+surface_fragment;}
void WebGPUDevice::enqueue_surface(int work,const DeviceKernelArguments &args){
  require(args.count==3,"Surface argument count");
  require(args.types[2]==DeviceKernelArguments::INT32&&args.sizes[2]==4,"Invalid surface work argument");
  int n;std::memcpy(&n,args.values[2],4);require(n>=0&&work>=n,"Incomplete surface dispatch");
  auto pointer=[&](unsigned i,bool nullable){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid surface pointer argument");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));if(address||!nullable)validate(address,4);
    require(address%4==0,"Unaligned surface pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};p[1]=n;p[2]=pointer(0,true);p[4]=pointer(1,false);p[17]=error_word;
  if(p[2])validate(p[2],size_t(n)*4);
  require(!data.bvh.have_motion&&!data.bvh.have_curves&&!data.bvh.have_points,"WebGPU surface currently requires static triangles");
  require(!data.integrator.use_direct_light&&!data.integrator.use_light_mis&&data.integrator.num_lights==0,"WebGPU surface direct lighting not implemented");
  require(!(data.kernel_features&(KERNEL_FEATURE_VOLUME|KERNEL_FEATURE_PATH_GUIDING|KERNEL_FEATURE_DENOISING|KERNEL_FEATURE_MNEE|KERNEL_FEATURE_LIGHT_LINKING|KERNEL_FEATURE_SHADOW_LINKING|KERNEL_FEATURE_AO))&&
          !data.integrator.has_shadow_catcher&&!data.integrator.use_caustics,"WebGPU surface optional integration features not implemented");
  require(data.film.pass_flag==0,"WebGPU surface data passes not implemented");
  require(data.integrator.filter_glossy==FLT_MAX,"WebGPU surface glossy filtering not implemented");
  require(data.max_closures>=0&&data.max_closures<=MAX_CLOSURE,"Invalid surface closure capacity");
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
  const auto sequence=data.integrator.tabulated_sobol_sequence_size;
  require(sequence>1&&(sequence&(sequence-1))==0,"Invalid surface Sobol sequence size");
  validate(p[9],size_t(sequence)*NUM_TAB_SOBOL_PATTERNS*NUM_TAB_SOBOL_DIMENSIONS*4);
  if(!work||!n)return;
  const size_t needed=size_t(n)*MAX_CLOSURE*56;
  if(needed>surface_closure_bytes){
    if(surface_closures){flush();release(surface_closures);surface_closures=0;surface_closure_bytes=0;}
    surface_closures=allocate(needed);surface_closure_bytes=needed;
  }
  p[23]=surface_closures;
  if(!surface.value){
    const std::string code=surface_source();WGPUShaderSourceWGSL source{};
    source.chain.sType=WGPUSType_ShaderSourceWGSL;source.code={code.data(),code.size()};WGPUShaderModuleDescriptor sd{};sd.nextInChain=&source.chain;
    gpu.push_scope();cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> shader(wgpuDeviceCreateShaderModule(gpu.get(),&sd));gpu.pop_scope();
    surface=gpu.pipeline(shader,pipeline_layout,"shade_surface");
  }
  dispatch(p,uint32_t(work),surface);
}
}
