// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "wavefront.h"
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#if defined(__KERNEL_SSE__) && !defined(__KERNEL_NEON__)
#include <xmmintrin.h>
#endif
namespace ccl {
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
#if defined(__KERNEL_SSE__) && !defined(__KERNEL_NEON__)
// Cycles' ray_triangle_reciprocal uses this host's RCPSS estimate. The WGSL
// models it as the 12-bit mantissa nearest to 2^25/(4097+2i), where i is the
// top 11 input mantissa bits, with the exponent 253-e. Check every mantissa
// class plus exponent and special cases on this host before claiming it.
static uint32_t host_rcpss(uint32_t bits){float x;std::memcpy(&x,&bits,4);float r=_mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(x)));uint32_t out;std::memcpy(&out,&r,4);return out;}
static uint32_t modelled_rcpss(uint32_t bits){
  const uint32_t sign=bits&0x80000000u,e=(bits>>23)&255u,m=bits&0x7fffffu;
  if(e==255u)return m?(bits|0x400000u):sign;
  if(e==0u)return sign|0x7f800000u;
  if(e>=253u)return sign;
  const uint32_t d=4097u+2u*(m>>12),q=((1u<<26)+d)/(2u*d);
  return sign|((253u-e)<<23)|((q-4096u)<<11);
}
static bool host_rcpss_modelled(){
  for(uint32_t i=0;i<2048;i++)for(uint32_t low:{0u,0xfffu,0x5a5u})for(uint32_t e:{1u,2u,100u,126u,127u,128u,200u,252u,253u,254u})
    for(uint32_t sign:{0u,0x80000000u}){const uint32_t bits=sign|(e<<23)|(i<<12)|low;if(host_rcpss(bits)!=modelled_rcpss(bits))return false;}
  for(uint32_t bits:{0u,0x80000000u,1u,0x7fffffu,0x7f800000u,0xff800000u,0x7fc00000u})if(host_rcpss(bits)!=modelled_rcpss(bits))return false;
  return true;
}
#endif
bool reference_oracle_sse(){
#ifdef __KERNEL_SSE__
  return true;
#elif defined(__ANDROID__)
  const char *oracle=std::getenv("CODEBRIX_CYCLES_REFERENCE_ORACLE");return oracle&&std::strcmp(oracle,"linux-x64-sse4.2")==0;
#else
  return false;
#endif
}
static constexpr char camera_source[]=
#include "camera_shader.inc"
;
// Offsets come from the compiled native ABI, not a second copy of Cycles structs.
std::string wavefront_layout(){
  std::ostringstream source;
  auto constant=[&](const char *name,size_t value){source<<"const "<<name<<":u32="<<value<<"u;\n";};
  const bool oracle_sse=reference_oracle_sse();
  constant("CAMERA_NATIVE_VECTOR_DIVIDE",oracle_sse?1:0);
  // Cycles normalize(float3) divides each component by the length with SSE,
  // while the scalar float3 operator/ (ARM64 without SSE2NEON) multiplies by
  // the reciprocal length. Encode the originating host's rule so ShaderData
  // normals replay that oracle's rounding on any GPU.
  constant("NATIVE_FLOAT3_RECIPROCAL_NORMALIZE",oracle_sse?0:1);
  // Cycles ray_triangle_intersect arithmetic of the originating oracle host:
  // 0 scalar build (dot (x+y)+z, 1/x), 1 SSE4.2 build with the modelled RCPSS
  // (dot x+(y+z), RCPSS + Newton step), 2 SSE build not modelled (SSE2NEON or
  // an x86 RCPSS that differs): SSE grouping with 1/x, not oracle-exact.
  // An Android scalar build judged against the Linux x64 reference emits 1.
#if defined(__KERNEL_SSE__) && !defined(__KERNEL_NEON__)
  static const bool rcpss_modelled=host_rcpss_modelled();
  constant("NATIVE_TRIANGLE_ARITHMETIC",rcpss_modelled?1:2);
#elif defined(__KERNEL_SSE__)
  constant("NATIVE_TRIANGLE_ARITHMETIC",2);
#else
  // Android scalar build judged against the Linux x64 reference: that oracle's
  // RCPSS is the verified model (integer WGSL, any GPU), so mode 1.
  constant("NATIVE_TRIANGLE_ARITHMETIC",oracle_sse?1:0);
#endif
#define DATA(parent,member) constant("D_" #parent "_" #member,offsetof(KernelData,parent.member))
  DATA(cam,rastertocamera);DATA(cam,cameratoworld);DATA(cam,dx);DATA(cam,dy);DATA(cam,differential_scale);
  DATA(cam,nearclip);DATA(cam,cliplength);DATA(cam,width);DATA(cam,height);
  DATA(cam,shuttertime);DATA(cam,shutter_table_offset);DATA(cam,rolling_shutter_type);DATA(cam,rolling_shutter_duration);
  DATA(cam,num_motion_steps);DATA(cam,have_perspective_motion);DATA(cam,perspective_pre);DATA(cam,perspective_post);
  DATA(integrator,seed);DATA(integrator,tabulated_sobol_sequence_size);DATA(integrator,scrambling_distance);
  DATA(integrator,pixel_jitter);DATA(tables,filter_table_offset);
  DATA(film,pass_stride);DATA(film,pass_sample_count);DATA(film,pass_adaptive_aux_buffer);
  DATA(cam,is_inside_volume);
  DATA(background,volume_shader);DATA(background,object_index);
  DATA(film,pass_volume_scatter);DATA(film,pass_volume_transmit);
  DATA(film,pass_volume_majorant);DATA(film,pass_volume_majorant_sample_count);
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
  constant("VS_HAS_VOLUME",SD_HAS_VOLUME);constant("VS_OBJECT_HAS_VOLUME",SD_OBJECT_HAS_VOLUME);
  constant("VS_MAX",MAX_VOLUME_STACK_SIZE);constant("VS_MAX_HITS",2*MAX_VOLUME_STACK_SIZE);constant("VS_BOUNDS_MAX",VOLUME_BOUNDS_MAX);
  constant("S_volume_stack",offsetof(IntegratorStateGPU,volume_stack));
  constant("VS_stride",sizeof(IntegratorStateGPU{}.volume_stack[0]));
  constant("VS_object",offsetof(std::remove_reference_t<decltype(IntegratorStateGPU{}.volume_stack[0])>,object));
  constant("VS_shader",offsetof(std::remove_reference_t<decltype(IntegratorStateGPU{}.volume_stack[0])>,shader));
  constant("D_volume_stack_size",offsetof(KernelData,volume_stack_size));
  constant("D_kernel_features",offsetof(KernelData,kernel_features));
  constant("S_queue_counter",offsetof(IntegratorStateGPU,queue_counter));
  constant("S_sort_partition_divisor",offsetof(IntegratorStateGPU,sort_partition_divisor));
  constant("S_sort_key_counter",offsetof(IntegratorStateGPU,sort_key_counter));
  constant("POINTER_SIZE",sizeof(void*));
  unsigned table_index=0;
#define TABLE(name,type) constant("G_" #name,table_index++);constant("SIZE_" #name,sizeof(type));
  CBC_SCENE_TABLES(TABLE) CBC_DEVICE_TABLES(TABLE)
#undef TABLE
  constant("O_tfm",offsetof(KernelObject,tfm));constant("O_motion_offset",offsetof(KernelObject,motion_offset));
  constant("O_num_tfm_steps",offsetof(KernelObject,num_tfm_steps));
  constant("O_num_geom_steps",offsetof(KernelObject,num_geom_steps));constant("O_numverts",offsetof(KernelObject,numverts));constant("O_numprims",offsetof(KernelObject,numprims));constant("MOTION_SIZE",sizeof(DecomposedTransform));
  constant("O_itfm",offsetof(KernelObject,itfm));constant("O_position_offset",offsetof(KernelObject,position_offset));
  constant("O_ao_distance",offsetof(KernelObject,ao_distance));constant("SH_flags",offsetof(KernelShader,flags));
#define ENUM(name) constant(#name,uint32_t(name))
  ENUM(KERNEL_FEATURE_VOLUME);ENUM(PATH_RAY_VOLUME_PASS);ENUM(PATH_RAY_VOLUME_PRIMARY_TRANSMIT);
  ENUM(PATH_RAY_TERMINATE_IN_NEXT_VOLUME);ENUM(PATH_RAY_TERMINATE_AFTER_VOLUME);
  ENUM(DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME);ENUM(DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK);
  ENUM(SD_OBJECT_MOTION);ENUM(SD_OBJECT_HAS_VERTEX_MOTION);
  ENUM(PRIMITIVE_TRIANGLE);ENUM(PRIMITIVE_MOTION_TRIANGLE);ENUM(PRIMITIVE_ALL);ENUM(PRIMITIVE_LAMP);ENUM(SHADER_MASK);
  ENUM(DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD);
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
  constant("SHUTTER_SIZE",SHUTTER_TABLE_SIZE);constant("RNG_LENS_TIME",PRNG_LENS_TIME);
  constant("MOTION_X",offsetof(DecomposedTransform,x));constant("MOTION_Y",offsetof(DecomposedTransform,y));
  constant("MOTION_Z",offsetof(DecomposedTransform,z));constant("MOTION_W",offsetof(DecomposedTransform,w));
  constant("SOBOL_PATTERNS",NUM_TAB_SOBOL_PATTERNS);constant("RNG_FILTER",PRNG_FILTER);
  constant("RNG_BOUNCE",PRNG_BOUNCE_NUM);constant("VIS_CAMERA",PATH_RAY_VISIBILITY_CAMERA);constant("VIS_VOLUME",PATH_RAY_VISIBILITY_VOLUME_SCATTER);
  constant("INITIAL_FLAG",PATH_RAY_MIS_SKIP|PATH_RAY_TRANSPARENT_BACKGROUND);
  constant("K_CAMERA",DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA);
  constant("K_CLOSEST",DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST);
  constant("NONE",uint32_t(OBJECT_NONE));constant("PRIM_TYPE_NONE",PRIMITIVE_NONE);
  constant("PASS_NONE",uint32_t(PASS_UNUSED));
  return source.str();
}
std::string camera_wgsl(){return wavefront_layout()+camera_source;}
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
  require(data.cam.type==CAMERA_PERSPECTIVE&&data.cam.aperturesize==0&&data.cam.interocular_offset==0,
          "WebGPU camera currently requires perspective pinhole without stereo");
  require(data.cam.num_motion_steps==0||data.cam.num_motion_steps>=2,"Invalid camera motion step count");
  if(data.cam.num_motion_steps){
    const auto motion=globals.find("camera_motion");
    require(motion!=globals.end()&&remaining(motion->second)==size_t(data.cam.num_motion_steps)*sizeof(DecomposedTransform),
            "Invalid native camera motion table");
  }
  require(std::isfinite(data.cam.shuttertime)&&(data.cam.shuttertime==-1||data.cam.shuttertime>=0),"Invalid camera shutter time");
  require(std::isfinite(data.cam.height)&&data.cam.height>0,"Invalid camera height");
  require((data.cam.rolling_shutter_type==0||data.cam.rolling_shutter_type==1)&&
          std::isfinite(data.cam.rolling_shutter_duration)&&data.cam.rolling_shutter_duration>=0&&data.cam.rolling_shutter_duration<=1,
          "Invalid camera rolling shutter");
  validate_volume();
  require(!(data.kernel_features&(KERNEL_FEATURE_PATH_GUIDING|
          KERNEL_FEATURE_DENOISING|KERNEL_FEATURE_LIGHT_LINKING|KERNEL_FEATURE_NODE_PORTAL)),"WebGPU camera optional path features not implemented");
  require(data.integrator.sampling_pattern==SAMPLING_PATTERN_TABULATED_SOBOL,"WebGPU camera requires tabulated Sobol");
  const auto sequence=data.integrator.tabulated_sobol_sequence_size;
  require(sequence>1&&(sequence&(sequence-1))==0,"Invalid Sobol sequence size");
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");
  p[8]=globals.at("lookup_table");p[9]=globals.at("sample_pattern_lut");
  require(data.tables.filter_table_offset>=0,"Invalid camera filter offset");
  validate(p[8],(size_t(data.tables.filter_table_offset)+FILTER_TABLE_SIZE)*4);
  if(data.cam.shuttertime!=-1){
    require(data.cam.shutter_table_offset>=0,"Invalid camera shutter table offset");
    validate(p[8],(size_t(data.cam.shutter_table_offset)+SHUTTER_TABLE_SIZE)*4);
  }
  p[14]=scene_tables();
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
  if(!pipeline_current(camera,"init_camera"))camera=std::move(build_pipelines("camera",camera_wgsl(),{"init_camera"})[0]);
  dispatch(p,uint32_t(work),camera,workgroup("init_camera"));
}
}
