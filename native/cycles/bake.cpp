// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Texture baking on the WebGPU device (pinned Cycles kernel/integrator/init_from_bake.h,
// kernel/device/gpu/kernel.h integrator_init_from_bake, integrator/path_trace_work_gpu.cpp
// enqueue_work_tiles): DEVICE_KERNEL_INTEGRATOR_INIT_FROM_BAKE, its WGSL constants and the
// bake module (wavefront_bake.wgsl). The kernel reads the map the host wrote into the
// render buffer's PASS_BAKE_PRIMITIVE and PASS_BAKE_DIFFERENTIAL passes (and PASS_BAKE_SEED
// when it exists) and starts each texel's path on the bake object's surface; the rest of the
// path runs through the device's ordinary kernels. Only a bake builds this module, so the
// modules of every other render are unchanged.
#include "device.h"
#include "wavefront.h"
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace ccl {
static constexpr char bake_source[]=
#include "bake_shader.inc"
;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static std::string bake_layout(){
  std::ostringstream out;
  auto constant=[&](const char *name,size_t value){out<<"const "<<name<<":u32="<<value<<"u;\n";};
#define DATA(parent,member) constant("BK_D_" #member,offsetof(KernelData,parent.member))
  DATA(film,pass_bake_primitive);DATA(film,pass_bake_differential);DATA(film,pass_bake_seed);
  DATA(film,pass_background);DATA(film,pass_position);DATA(film,pass_normal);DATA(film,pass_combined);DATA(film,light_pass_flag);
#undef DATA
  constant("BK_D_bake_object_index",offsetof(KernelData,bake.object_index));constant("BK_D_bake_tri_offset",offsetof(KernelData,bake.tri_offset));
  constant("BK_D_bake_use_camera",offsetof(KernelData,bake.use_camera));constant("BK_D_cam_type",offsetof(KernelData,cam.type));
  constant("BK_D_use_caustics",offsetof(KernelData,integrator.use_caustics));
  constant("BK_MASK_COMBINED",uint32_t(PASSMASK(COMBINED)));constant("BK_CAMERA_ORTHOGRAPHIC",CAMERA_ORTHOGRAPHIC);
  constant("BK_SD_HAS_BUMP",SD_HAS_BUMP);constant("BK_SD_OBJECT_CAUSTICS_RECEIVER",SD_OBJECT_CAUSTICS_RECEIVER);
  constant("BK_K_INTERSECT_MNEE",DEVICE_KERNEL_INTEGRATOR_INTERSECT_MNEE);
  return out.str();
}
std::string bake_wgsl(){return wavefront_layout()+shader_data_layout()+shadow_catcher_layout()+bake_layout()+bake_source;}
// Arguments as PathTraceWorkGPU::enqueue_work_tiles passes them: tiles, tile count, render
// buffer, largest tile work size (the camera's retry pointer and count are unused by a bake).
void WebGPUDevice::enqueue_bake(int work,const DeviceKernelArguments &args){
  require(args.count==6,"Bake argument count");
  auto scalar=[&](unsigned i){require(args.types[i]==DeviceKernelArguments::INT32&&args.sizes[i]==4,"Invalid bake integer argument");
    int32_t n;std::memcpy(&n,args.values[i],4);require(n>=0,"Negative bake argument");return uint32_t(n);};
  auto pointer=[&](unsigned i){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid bake pointer argument");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));validate(address,4);
    require(address%4==0,"Unaligned bake pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};
  p[1]=uint32_t(work);p[2]=pointer(0);p[3]=scalar(1);p[4]=pointer(2);p[5]=scalar(3);p[17]=error_word;
  require(p[3]&&p[5]&&uint64_t(p[3])*p[5]<=uint32_t(work),"Incomplete bake tile dispatch");
  validate(p[2],size_t(p[3])*sizeof(KernelWorkTile));
  // Scene::get_device_kernel_features sets KERNEL_FEATURE_BAKING while baking; only this module needs it, so it selects no region.
  require(data.bake.use&&(data.kernel_features&KERNEL_FEATURE_BAKING),"Bake kernel without a bake");
  require(data.film.pass_stride>0,"Invalid bake film stride");
  // The map passes the host fills (Film::update_passes adds them while baking); the seed pass only when present.
  for(auto [offset,length]:{std::pair{data.film.pass_bake_primitive,3},std::pair{data.film.pass_bake_differential,4}})
    require(offset!=PASS_UNUSED&&offset>=0&&offset<=data.film.pass_stride-length,"Invalid bake map film pass offset");
  require(data.film.pass_bake_seed==0||(data.film.pass_bake_seed>0&&data.film.pass_bake_seed<data.film.pass_stride),"Invalid bake seed film pass offset");
  for(auto [offset,length]:{std::pair{data.film.pass_sample_count,1},std::pair{data.film.pass_adaptive_aux_buffer,4},std::pair{data.film.pass_position,3},
                            std::pair{data.film.pass_normal,3},std::pair{data.film.pass_background,3}})
    require(offset==PASS_UNUSED||(offset>=0&&offset<=data.film.pass_stride-length),"Invalid bake film pass offset");
  if(data.film.light_pass_flag&PASSMASK(COMBINED))require(data.film.pass_combined!=PASS_UNUSED&&data.film.pass_combined>=0&&data.film.pass_combined<=data.film.pass_stride-4,"Missing combined film pass");
  require(data.bake.object_index>=0&&size_t(data.bake.object_index)<object_records.size()&&size_t(data.bake.object_index)<object_flag_records.size(),"Invalid bake object");
  require(data.cam.type==CAMERA_PERSPECTIVE||data.cam.type==CAMERA_ORTHOGRAPHIC||data.cam.type==CAMERA_PANORAMA,"WebGPU custom camera (OSL) not implemented");
  // The kernel places the surface by the mesh's static positions (triangle_point_normal) and the shading stages read the
  // triangle as a plain one: for a deforming mesh BAKE_DEFORM (pipelines.cpp) reads its plain triangles at the static
  // positions (native/DEVICE.md, item 115); secondary hits on it are motion triangles at the ray's time, as in a render.
  validate_volume();
  require(!(data.kernel_features&KERNEL_FEATURE_PATH_GUIDING),"WebGPU bake optional path features not implemented");
  // The Ray Portal BSDF (RAY_PORTAL): path_state_init_integrator clears the portal bounce, as the camera does.
  if(data.kernel_features&KERNEL_FEATURE_NODE_PORTAL)validate(uintptr_t(state.path.portal_bounce),remaining(uintptr_t(state.path.render_pixel_index))/4*sizeof(uint16_t));
  validate_film_passes();
  validate_shadow_catcher();
  if(data.integrator.use_caustics)validate_mnee();
  if(data.kernel_features&KERNEL_FEATURE_DENOISING)validate(uintptr_t(state.path.denoising_feature_throughput),
    remaining(uintptr_t(state.path.render_pixel_index))/4*sizeof(PackedSpectrum));
  if(data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)require(state.path.mis_ray_object,"Missing light linking path state");
  require(data.max_shaders>0&&state.sort_partition_divisor>0,"Invalid bake sort configuration");
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");p[9]=sample_pattern_table("bake");
  p[14]=scene_tables();
  p[10]=uint32_t(remaining(uintptr_t(state.path.render_pixel_index))/4);p[11]=uint32_t(remaining(p[4])/4);
  // All mandatory fields must cover the same capacity before any shader writes.
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(feature==KERNEL_FEATURE_PATH_TRACING)validate(uintptr_t(state.parent.name),size_t(p[10])*sizeof(gpu_state_storage<type>::gpu_type));
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
  if(!work)return;
  p[24]=shadow_catcher_marks(p[10]);
  if(!pipeline_current(bake,"init_bake"))bake=std::move(build_pipelines("bake",bake_wgsl(),{"init_bake"})[0]);
  dispatch(p,uint32_t(work),bake,workgroup("init_bake"));
  split_shadow_catcher(p[10]);
}
}
