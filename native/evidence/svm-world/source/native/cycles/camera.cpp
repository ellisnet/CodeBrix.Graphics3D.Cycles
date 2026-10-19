// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "wavefront.h"
#include <cfloat>
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace ccl {
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static constexpr char camera_source[]=
#include "camera_shader.inc"
;
// Offsets come from the compiled native ABI, not a second copy of Cycles structs.
std::string wavefront_layout(){
  std::ostringstream source;
  auto constant=[&](const char *name,size_t value){source<<"const "<<name<<":u32="<<value<<"u;\n";};
#define DATA(parent,member) constant("D_" #parent "_" #member,offsetof(KernelData,parent.member))
  DATA(cam,rastertocamera);DATA(cam,cameratoworld);DATA(cam,dx);DATA(cam,dy);DATA(cam,differential_scale);
  DATA(cam,nearclip);DATA(cam,cliplength);DATA(cam,width);
  DATA(integrator,seed);DATA(integrator,tabulated_sobol_sequence_size);DATA(integrator,scrambling_distance);
  DATA(integrator,pixel_jitter);DATA(tables,filter_table_offset);
  DATA(film,pass_stride);DATA(film,pass_sample_count);DATA(film,pass_adaptive_aux_buffer);
  DATA(bvh,root);DATA(integrator,min_bounce);DATA(integrator,transparent_min_bounce);
  DATA(integrator,ao_bounces);DATA(integrator,ao_bounces_distance);DATA(integrator,filter_closures);
  constant("D_max_shaders",offsetof(KernelData,max_shaders));
#undef DATA
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) constant("S_" #parent "_" #name,offsetof(IntegratorStateGPU,parent.name));
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
  constant("S_queue_counter",offsetof(IntegratorStateGPU,queue_counter));
  constant("S_sort_partition_divisor",offsetof(IntegratorStateGPU,sort_partition_divisor));
  constant("S_sort_key_counter",offsetof(IntegratorStateGPU,sort_key_counter));
  constant("POINTER_SIZE",sizeof(void*));
  unsigned table_index=0;
#define TABLE(name,type) constant("G_" #name,table_index++);constant("SIZE_" #name,sizeof(type));
  CBC_INTERSECTION_TABLES(TABLE)
#undef TABLE
  constant("O_itfm",offsetof(KernelObject,itfm));constant("O_position_offset",offsetof(KernelObject,position_offset));
  constant("O_ao_distance",offsetof(KernelObject,ao_distance));constant("SH_flags",offsetof(KernelShader,flags));
#define ENUM(name) constant(#name,uint32_t(name))
  ENUM(PRIMITIVE_TRIANGLE);ENUM(PRIMITIVE_ALL);ENUM(SHADER_MASK);
  ENUM(SD_HAS_EMISSION);ENUM(SD_HAS_TRANSPARENT_SHADOW);ENUM(SD_HAS_RAYTRACE);
  ENUM(PATH_RAY_TRANSPARENT);ENUM(PATH_RAY_SHADOW_CATCHER_PASS);
  ENUM(PATH_RAY_TERMINATE_AFTER_TRANSPARENT);ENUM(PATH_RAY_TERMINATE_ON_NEXT_SURFACE);
  ENUM(PATH_RAY_VISIBILITY_TRANSMIT);ENUM(PATH_RAY_VISIBILITY_DIFFUSE);ENUM(PATH_RAY_VISIBILITY_GLOSSY);
  ENUM(PATH_RAY_VISIBILITY_SHADOW_OPAQUE);
  ENUM(FILTER_CLOSURE_DIRECT_LIGHT);ENUM(PRNG_TERMINATE);
  ENUM(DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND);ENUM(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE);
  ENUM(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE);
#undef ENUM
#define TILE(member) constant("T_" #member,offsetof(KernelWorkTile,member))
  TILE(x);TILE(y);TILE(w);TILE(h);TILE(start_sample);TILE(num_samples);TILE(sample_offset);
  TILE(offset);TILE(stride);TILE(path_index_offset);TILE(work_size);
#undef TILE
  constant("T_size",sizeof(KernelWorkTile));constant("FILTER_SIZE",FILTER_TABLE_SIZE);
  constant("SOBOL_PATTERNS",NUM_TAB_SOBOL_PATTERNS);constant("RNG_FILTER",PRNG_FILTER);
  constant("RNG_BOUNCE",PRNG_BOUNCE_NUM);constant("VIS_CAMERA",PATH_RAY_VISIBILITY_CAMERA);
  constant("INITIAL_FLAG",PATH_RAY_MIS_SKIP|PATH_RAY_TRANSPARENT_BACKGROUND);
  constant("K_CAMERA",DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA);
  constant("K_CLOSEST",DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST);
  constant("NONE",uint32_t(OBJECT_NONE));constant("PRIM_TYPE_NONE",PRIMITIVE_NONE);
  constant("PASS_NONE",uint32_t(PASS_UNUSED));
  return source.str();
}
void WebGPUDevice::enqueue_camera(int work,const DeviceKernelArguments &args){
  require(args.count==6,"Camera argument count");
  auto scalar=[&](unsigned i){require(args.types[i]==DeviceKernelArguments::INT32&&args.sizes[i]==4,"Invalid camera integer argument");
    int32_t n;std::memcpy(&n,args.values[i],4);require(n>=0,"Negative camera argument");return uint32_t(n);};
  auto pointer=[&](unsigned i,bool nullable){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid camera pointer argument");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));if(address||!nullable)validate(address,4);
    require(address%4==0,"Unaligned camera pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};
  p[1]=uint32_t(work);p[2]=pointer(0,true);p[3]=scalar(1);p[4]=pointer(2,false);p[5]=scalar(3);
  p[12]=pointer(4,true);p[13]=scalar(5);p[17]=error_word;
  if(p[2]){require(p[3]&&p[5]&&uint64_t(p[3])*p[5]<=uint32_t(work),"Incomplete camera tile dispatch");validate(p[2],size_t(p[3])*sizeof(KernelWorkTile));}
  else{require(p[13]<=uint32_t(work),"Incomplete camera retry dispatch");if(p[12])validate(p[12],size_t(p[13])*4);}
  require(data.cam.type==CAMERA_PERSPECTIVE&&data.cam.aperturesize==0&&data.cam.num_motion_steps==0&&
          !data.cam.have_perspective_motion&&data.cam.interocular_offset==0&&data.cam.shuttertime==-1,
          "WebGPU camera currently requires static perspective pinhole");
  require(!data.cam.is_inside_volume&&!(data.kernel_features&(KERNEL_FEATURE_VOLUME|KERNEL_FEATURE_PATH_GUIDING|
          KERNEL_FEATURE_DENOISING|KERNEL_FEATURE_LIGHT_LINKING|KERNEL_FEATURE_NODE_PORTAL)),"WebGPU camera optional path features not implemented");
  require(data.integrator.sampling_pattern==SAMPLING_PATTERN_TABULATED_SOBOL,"WebGPU camera requires tabulated Sobol");
  const auto sequence=data.integrator.tabulated_sobol_sequence_size;
  require(sequence>1&&(sequence&(sequence-1))==0,"Invalid Sobol sequence size");
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");
  p[8]=globals.at("lookup_table");p[9]=globals.at("sample_pattern_lut");
  require(data.tables.filter_table_offset>=0,"Invalid camera filter offset");
  validate(p[8],(size_t(data.tables.filter_table_offset)+FILTER_TABLE_SIZE)*4);
  validate(p[9],size_t(sequence)*NUM_TAB_SOBOL_PATTERNS*NUM_TAB_SOBOL_DIMENSIONS*4);
  require(data.film.pass_stride>0,"Invalid camera film stride");
  for(auto [offset,length]:{std::pair{data.film.pass_sample_count,1},std::pair{data.film.pass_adaptive_aux_buffer,4}})
    require(offset==PASS_UNUSED||(offset>=0&&offset<=data.film.pass_stride-length),"Invalid camera film pass offset");
  p[10]=uint32_t(remaining(uintptr_t(state.path.render_pixel_index))/4);p[11]=uint32_t(remaining(p[4])/4);
  // All mandatory fields must cover the same capacity before any shader writes.
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
  if(!work)return;
  if(!camera.value){
    const std::string code=wavefront_layout()+camera_source;
    WGPUShaderSourceWGSL source{};source.chain.sType=WGPUSType_ShaderSourceWGSL;source.code={code.data(),code.size()};
    WGPUShaderModuleDescriptor sd{};sd.nextInChain=&source.chain;gpu.push_scope();
    cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> shader(wgpuDeviceCreateShaderModule(gpu.get(),&sd));gpu.pop_scope();
    camera=gpu.pipeline(shader,pipeline_layout,"init_camera");
  }
  dispatch(p,uint32_t(work),camera);
}
}
