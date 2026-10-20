// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "wavefront.h"
#include <cstring>
#include <stdexcept>
namespace ccl {
static constexpr char closest_source[]=
#include "closest_shader.inc"
;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
uint32_t WebGPUDevice::scene_tables(){
  std::vector<uint32_t> descriptors;
#define TABLE(name,type) {auto it=globals.find(#name);uint32_t address=it==globals.end()?0:it->second;size_t bytes=address?remaining(address):0; \
    require(bytes%sizeof(type)==0,"Invalid native scene table size: " #name);descriptors.insert(descriptors.end(),{address,uint32_t(bytes/sizeof(type))});}
  CBC_SCENE_TABLES(TABLE)
#undef TABLE
  if(!intersection_tables)intersection_tables=allocate(descriptors.size()*4);
  if(descriptors!=intersection_table_cache){upload(intersection_tables,descriptors.data(),descriptors.size()*4);intersection_table_cache=descriptors;}
  return intersection_tables;
}
std::string closest_wgsl(){return wavefront_layout()+closest_source;}
void WebGPUDevice::enqueue_closest(int work,const DeviceKernelArguments &args){
  require(args.count==3,"Closest intersection argument count");
  require(args.types[2]==DeviceKernelArguments::INT32&&args.sizes[2]==4,"Invalid closest work argument");
  int n;std::memcpy(&n,args.values[2],4);require(n>=0&&work>=n,"Incomplete closest intersection dispatch");
  auto pointer=[&](unsigned i,bool nullable){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid closest pointer argument");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));if(address||!nullable)validate(address,4);
    require(address%4==0,"Unaligned closest pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};p[1]=n;p[2]=pointer(0,true);p[4]=pointer(1,false);p[17]=error_word;
  if(p[2])validate(p[2],size_t(n)*4);
  require(!data.bvh.have_motion&&!data.bvh.have_curves&&!data.bvh.have_points,"WebGPU closest currently requires static triangles");
  require(!(data.kernel_features&(KERNEL_FEATURE_VOLUME|KERNEL_FEATURE_PATH_GUIDING|KERNEL_FEATURE_SHADOW_LINKING))&&
          !data.integrator.has_shadow_catcher&&!data.integrator.use_caustics,"WebGPU closest optional integration features not implemented");
  require(!data.integrator.use_light_mis,"WebGPU closest light intersection not implemented");
  require(data.integrator.sampling_pattern==SAMPLING_PATTERN_TABULATED_SOBOL,"WebGPU closest requires tabulated Sobol");
  require(data.max_shaders>0&&state.sort_partition_divisor>0,"Invalid closest sort configuration");
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");p[9]=globals.at("sample_pattern_lut");
  const auto sequence=data.integrator.tabulated_sobol_sequence_size;
  require(sequence>1&&(sequence&(sequence-1))==0,"Invalid closest Sobol sequence size");
  validate(p[9],size_t(sequence)*NUM_TAB_SOBOL_PATTERNS*NUM_TAB_SOBOL_DIMENSIONS*4);
  p[10]=uint32_t(remaining(uintptr_t(state.path.render_pixel_index))/4);p[11]=uint32_t(remaining(p[4])/4);
  if(!p[2])require(uint32_t(n)<=p[10],"Closest dispatch exceeds path capacity");
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(feature==KERNEL_FEATURE_PATH_TRACING)validate(uintptr_t(state.parent.name),size_t(p[10])*sizeof(type));
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
  for(auto [kernel,slot]:{std::pair{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,15},std::pair{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,16}}){
    if(state.sort_key_counter[kernel])p[slot]=uint32_t(remaining(uintptr_t(state.sort_key_counter[kernel]))/4);
  }
  require(p[15]>=uint32_t(data.max_shaders),"Missing surface sort counters");
  p[14]=scene_tables();
  if(!work)return;
  if(!closest.value){
    const std::string code=closest_wgsl();WGPUShaderSourceWGSL source{};
    source.chain.sType=WGPUSType_ShaderSourceWGSL;source.code={code.data(),code.size()};WGPUShaderModuleDescriptor sd{};sd.nextInChain=&source.chain;
    gpu.push_scope();cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> shader(wgpuDeviceCreateShaderModule(gpu.get(),&sd));gpu.pop_scope();
    closest=gpu.pipeline(shader,pipeline_layout,"intersect_closest");
  }
  dispatch(p,uint32_t(work),closest);
}
}
