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
std::string volume_stack_source(){return wavefront_layout()+shader_data_layout()+volume_stack_fragment;}
static constexpr char volume_fragment[]=
#include "volume_shader.inc"
;
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
  return svm_surface_source()+source.str()+volume_fragment;
}
// Bounded static homogeneous media only: density preparation, stack boundaries
// and HG transport. Volume NEE/MIS, shadow attenuation, emission/light passes,
// heterogeneous media and any scene motion remain unsupported. Mixed-anisotropy
// duplicate closure compaction is rejected in WGSL before state/film mutation.
// Linux parity is proven in the sealed lane; ARM validation remains outstanding.
void WebGPUDevice::validate_volume() const{
  if(!(data.kernel_features&KERNEL_FEATURE_VOLUME))return;
  require(!data.bvh.have_motion&&!data.bvh.have_curves&&!data.bvh.have_points,"WebGPU volumes currently require static triangle boundaries");
  require(!data.integrator.volume_ray_marching,"WebGPU biased volume ray marching not implemented");
  require(!data.integrator.use_direct_light&&!data.integrator.use_light_mis&&!data.background.use_mis,"WebGPU volume direct-light/MIS and shadow attenuation not implemented");
  require(!(data.kernel_features&(KERNEL_FEATURE_PATH_GUIDING|KERNEL_FEATURE_DENOISING|KERNEL_FEATURE_LIGHT_LINKING|KERNEL_FEATURE_SHADOW_LINKING))&&!data.integrator.has_shadow_catcher,"WebGPU optional volume integration features not implemented");
  require(data.volume_stack_size>=2&&data.volume_stack_size<=MAX_VOLUME_STACK_SIZE,"Invalid homogeneous volume stack size");
  require(data.max_closures>=1&&data.max_closures<=MAX_CLOSURE,"Invalid homogeneous volume closure capacity");
  for(auto &shader:shader_records)if(shader.flags&SD_HAS_VOLUME){
    require(!(shader.flags&SD_HETEROGENEOUS_VOLUME),"WebGPU heterogeneous volume not implemented");
  }
  auto pass=[&](int offset,int length,bool required){require((!required&&offset==PASS_UNUSED)||(offset>=0&&offset<=data.film.pass_stride-length),"Invalid homogeneous volume film pass");};
  pass(data.film.pass_volume_majorant,1,true);pass(data.film.pass_volume_majorant_sample_count,1,true);
  pass(data.film.pass_volume_scatter_denoised,1,true);pass(data.film.pass_volume_transmit_denoised,1,true);
  pass(data.film.pass_volume_scatter,3,false);pass(data.film.pass_volume_transmit,3,true);
  // Volume direct/indirect pass routing is the next independently tested slice.
  require(data.film.pass_volume_direct==PASS_UNUSED&&data.film.pass_volume_indirect==PASS_UNUSED,"WebGPU volume light passes not implemented");
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
  require(data.integrator.sampling_pattern==SAMPLING_PATTERN_TABULATED_SOBOL,"WebGPU volume requires tabulated Sobol");
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");p[9]=globals.at("sample_pattern_lut");p[14]=scene_tables();
  p[18]=globals.at("shaders");p[19]=uint32_t(remaining(p[18])/sizeof(KernelShader));p[20]=(data.kernel_features&KERNEL_FEATURE_LIGHT_PASSES)!=0;
  p[21]=globals.at("svm_nodes");p[22]=uint32_t(remaining(p[21])/4);
  const unsigned sequence=data.integrator.tabulated_sobol_sequence_size;require(sequence>1&&(sequence&(sequence-1))==0,"Invalid volume Sobol sequence size");
  validate(p[9],size_t(sequence)*NUM_TAB_SOBOL_PATTERNS*NUM_TAB_SOBOL_DIMENSIONS*4);
  p[10]=uint32_t(remaining(uintptr_t(state.path.render_pixel_index))/4);p[11]=uint32_t(remaining(p[4])/4);
  require(data.film.pass_stride>0&&p[11]>=uint32_t(data.film.pass_stride),"Volume film smaller than one pixel");
  if(!p[2])require(uint32_t(n)<=p[10],"Volume dispatch exceeds path capacity");
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
  for(auto [kernel,slot]:{std::pair{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,15},std::pair{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,16}}){if(state.sort_key_counter[kernel])p[slot]=uint32_t(remaining(uintptr_t(state.sort_key_counter[kernel]))/4);}
  require(p[15]>=uint32_t(data.max_shaders)&&state.sort_partition_divisor>0,"Invalid volume surface sort counters");
  if(!work)return;
  const size_t bytes=size_t(n)*MAX_CLOSURE*sizeof(SurfaceClosureRecord);
  if(volume_closure_bytes<bytes){if(volume_closures){flush();release(volume_closures);}volume_closures=allocate(bytes);volume_closure_bytes=bytes;}p[23]=volume_closures;
  if(!volume.value){const std::string code=volume_source();WGPUShaderSourceWGSL source{};source.chain.sType=WGPUSType_ShaderSourceWGSL;source.code={code.data(),code.size()};
    WGPUShaderModuleDescriptor sd{};sd.nextInChain=&source.chain;gpu.push_scope();cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> shader(wgpuDeviceCreateShaderModule(gpu.get(),&sd));gpu.pop_scope();volume=gpu.pipeline(shader,pipeline_layout,"shade_volume");}
  dispatch(p,uint32_t(work),volume);
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
  if(!work||!n)return;
  if(!volume_stack.value){const std::string code=volume_stack_source();WGPUShaderSourceWGSL source{};source.chain.sType=WGPUSType_ShaderSourceWGSL;source.code={code.data(),code.size()};
    WGPUShaderModuleDescriptor sd{};sd.nextInChain=&source.chain;gpu.push_scope();cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> shader(wgpuDeviceCreateShaderModule(gpu.get(),&sd));gpu.pop_scope();volume_stack=gpu.pipeline(shader,pipeline_layout,"intersect_volume_stack");}
  dispatch(p,uint32_t(work),volume_stack);
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
  if(!volume_density.value){const std::string code=volume_source();WGPUShaderSourceWGSL source{};source.chain.sType=WGPUSType_ShaderSourceWGSL;source.code={code.data(),code.size()};
    WGPUShaderModuleDescriptor sd{};sd.nextInChain=&source.chain;gpu.push_scope();cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> shader(wgpuDeviceCreateShaderModule(gpu.get(),&sd));gpu.pop_scope();volume_density=gpu.pipeline(shader,pipeline_layout,"shader_eval_volume_density");}
  dispatch(p,uint32_t(work),volume_density);
}
}
