// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Subsurface scattering on the WebGPU device (wavefront_subsurface.wgsl): the BSSRDF closures, subsurface_bounce in
// the surface stages, DEVICE_KERNEL_INTEGRATOR_INTERSECT_SUBSURFACE (the random walk, subsurface_scatter) and the
// diffuse closure at the exit point.
#include "device.h"
#include "wavefront.h"
#include "kernel/bvh/types.h"
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace ccl {
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
// Constants of the SUBSURFACE regions of the surface module (the SVM part is in svm_layout.cpp).
std::string subsurface_layout(){
  std::ostringstream source;auto constant=[&](const char *name,size_t v){source<<"const "<<name<<":u32="<<uint32_t(v)<<"u;\n";};
  source<<"//#if SUBSURFACE\n";
  constant("SS_LABEL_SUBSURFACE_SCATTER",LABEL_SUBSURFACE_SCATTER);constant("SS_K_INTERSECT",DEVICE_KERNEL_INTEGRATOR_INTERSECT_SUBSURFACE);
  constant("SS_PATH_RAY_SUBSURFACE",PATH_RAY_SUBSURFACE);constant("SS_PATH_RAY_SUBSURFACE_RANDOM_WALK",PATH_RAY_SUBSURFACE_RANDOM_WALK);
  constant("SS_PATH_RAY_SUBSURFACE_BACKFACING",PATH_RAY_SUBSURFACE_BACKFACING);constant("SS_PATH_RAY_VISIBILITY_CAMERA",PATH_RAY_VISIBILITY_CAMERA);
  constant("SS_SD_HAS_BSSRDF_BUMP",SD_HAS_BSSRDF_BUMP);constant("SS_PRNG_BSDF",PRNG_SUBSURFACE_BSDF);
  source<<"//#endif\n//#if SUBSURFACE_ENTRY\n";
  constant("SS_PRNG_COLOR_CHANNEL",PRNG_SUBSURFACE_COLOR_CHANNEL);constant("SS_PRNG_SCATTER_DISTANCE",PRNG_SUBSURFACE_SCATTER_DISTANCE);
  constant("SS_PRNG_GUIDE_STRATEGY",PRNG_SUBSURFACE_GUIDE_STRATEGY);constant("SS_PRNG_GUIDE_DIRECTION",PRNG_SUBSURFACE_GUIDE_DIRECTION);
  constant("SS_MAX_BOUNCES",BSSRDF_MAX_BOUNCES);
  // SUBSURFACE_RANDOM_WALK_SIMILARITY_LEVEL (integrator/subsurface_random_walk.h, a local define).
  constant("SS_SIMILARITY_LEVEL",9);
  source<<"//#endif\n";
  return source.str();
}
// Scene combinations the subsurface port supports; everything else is refused by name before rendering.
void WebGPUDevice::validate_subsurface(){
  require(!(data.kernel_features&KERNEL_FEATURE_VOLUME),"WebGPU subsurface scattering with volumes not implemented");
  require(!data.integrator.use_caustics,"WebGPU subsurface scattering with MNEE caustics not implemented");
  require(!data.integrator.has_shadow_catcher,"WebGPU subsurface scattering with the shadow catcher not implemented");
  require(!(data.kernel_features&(KERNEL_FEATURE_LIGHT_LINKING|KERNEL_FEATURE_SHADOW_LINKING|KERNEL_FEATURE_PATH_GUIDING|KERNEL_FEATURE_DENOISING)),
          "WebGPU subsurface scattering with light linking, path guiding or denoising features not implemented");
  require(!(shader_features()&(SF_FILM_DATA_PASSES|SF_FILM_DENOISING|SF_NODE_AOV|SF_FILM_CRYPTOMATTE)).any(),
          "WebGPU subsurface scattering with data, denoising, AOV or cryptomatte passes not implemented");
  for(const uint flags:object_flag_records){
    require(!(flags&(SD_OBJECT_MOTION|SD_OBJECT_HAS_VERTEX_MOTION)),"WebGPU subsurface scattering with motion blur not implemented");
    require(!(flags&SD_OBJECT_HOLDOUT_MASK),"WebGPU subsurface scattering with holdout objects not implemented");
  }
}
void WebGPUDevice::enqueue_subsurface(int work,const DeviceKernelArguments &args){
  require(args.count==2,"Subsurface argument count");
  require(args.types[1]==DeviceKernelArguments::INT32&&args.sizes[1]==4,"Invalid subsurface work argument");
  int n;std::memcpy(&n,args.values[1],4);require(n>=0&&work>=n,"Incomplete subsurface dispatch");
  require(args.types[0]==DeviceKernelArguments::POINTER&&args.sizes[0]==sizeof(device_ptr),"Invalid subsurface pointer argument");
  device_ptr indices;std::memcpy(&indices,args.values[0],sizeof(indices));require(indices%4==0,"Unaligned subsurface indices");
  require(data.kernel_features&KERNEL_FEATURE_SUBSURFACE,"Invalid native subsurface feature flags");
  validate_subsurface();validate_geometry_motion();
  std::array<uint32_t,32> p{};p[1]=n;p[2]=uint32_t(indices);p[17]=error_word;if(p[2])validate(p[2],size_t(n)*4);
  require(data.max_shaders>0&&state.sort_partition_divisor>0,"Invalid subsurface sort configuration");
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");p[9]=sample_pattern_table("subsurface");p[18]=globals.at("shaders");
  require(remaining(p[18])%sizeof(KernelShader)==0,"Invalid subsurface shader table");p[19]=uint32_t(remaining(p[18])/sizeof(KernelShader));
  p[10]=uint32_t(remaining(uintptr_t(state.path.render_pixel_index))/4);
  if(!p[2])require(uint32_t(n)<=p[10],"Subsurface dispatch exceeds path capacity");
  require(uint32_t(work)<=svm_stack_slots,"Subsurface dispatch exceeds the SVM stack slots");
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(feature==KERNEL_FEATURE_PATH_TRACING||feature==KERNEL_FEATURE_SUBSURFACE||\
  ((data.kernel_features&KERNEL_FEATURE_LIGHT_PASSES)&&feature==KERNEL_FEATURE_LIGHT_PASSES))validate(uintptr_t(state.parent.name),size_t(p[10])*sizeof(type));
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
  if(state.sort_key_counter[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE])p[15]=uint32_t(remaining(uintptr_t(state.sort_key_counter[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE]))/4);
  require(p[15]>=uint32_t(data.max_shaders),"Missing surface sort counters");
  if(state.sort_key_counter[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE])p[16]=uint32_t(remaining(uintptr_t(state.sort_key_counter[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE]))/4);
  p[14]=scene_tables();
  if(!work||!n)return;
  // The walk's BVH traversal stack (raytrace_traverse, push word 30): one 192-word slot per input.
  const size_t needed=size_t(work)*BVH_STACK_SIZE*sizeof(uint32_t);
  if(needed>subsurface_stack_bytes){
    if(subsurface_stack){flush();release(subsurface_stack);subsurface_stack=0;subsurface_stack_bytes=0;}
    subsurface_stack=allocate(needed);subsurface_stack_bytes=needed;
  }
  p[30]=subsurface_stack;
  // The walk evaluates no SVM program: the module variant leaves the scene's node and closure handlers out.
  constexpr ShaderFeatures svm=(ShaderFeatures::range(0,64)&~ShaderFeatures::range(26,32))|SF_TEX_ENVIRONMENT|SF_TEX_IMAGE_BOX|SF_MATH_ARC|
    SF_BSDF_FAMILIES|SF_NODE_BEVEL|SF_NODE_RAYCAST|SF_NODE_RADIAL_TILING|SF_NODE_AO|SF_NODE_IES|SF_TEX_SKY|SF_TEX_SKY_SCATTERING|SF_VOLUME_PHASES|SF_VOLUME_PHASE_KINDS|SF_NODE_VOLUME_COEFFICIENTS|SF_NODE_PRINCIPLED_VOLUME;
  const ShaderFeatures unused=specialization_mode==SpecializationMode::Scene?shader_features()&svm:SF_NONE;
  if(!pipeline_current(subsurface_walk,"intersect_subsurface",unused))
    subsurface_walk=std::move(build_pipelines("surface",surface_source(),{"intersect_subsurface"},SF_SUBSURFACE_ENTRY,unused)[0]);
  dispatch(p,uint32_t(n),subsurface_walk,workgroup("intersect_subsurface"));
}
}
