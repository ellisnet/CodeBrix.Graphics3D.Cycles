// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// DEVICE_KERNEL_INTEGRATOR_INTERSECT_MNEE on the WebGPU device: manifold
// next-event estimation through caustic casters (wavefront_mnee.wgsl).
#include "device.h"
#include "wavefront.h"
#include "surface_closure.h"
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace ccl {
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
// Per dispatched path: stage word, receiver ShaderData, light sample, walk
// metadata, six caster hits, the manifold vertices and their tentative copies,
// the result, then SVM closure storage (wavefront_mnee.wgsl MR_* offsets).
static constexpr size_t mnee_closure_offset=2720;
static constexpr size_t mnee_record_bytes=mnee_closure_offset+MAX_CLOSURE*sizeof(SurfaceClosureRecord);
static_assert(surface_data_cache_bytes==240&&mnee_record_bytes%16==0,"wavefront_mnee.wgsl record layout");
// Constants of every MNEE region (closest, surface and background modules). The
// block is itself one MNEE region, so modules of scenes without caustics are unchanged.
std::string mnee_layout(){
  std::ostringstream out;out<<"//#if MNEE\n";auto constant=[&](const char *name,size_t value){out<<"const "<<name<<":u32="<<value<<"u;\n";};
  constant("MNEE_K_INTERSECT",DEVICE_KERNEL_INTEGRATOR_INTERSECT_MNEE);constant("MNEE_K_PENDING",DEVICE_KERNEL_INTEGRATOR_SHADOW_PATH_MNEE_PENDING);
  constant("MNEE_PATH_VALID",PATH_MNEE_VALID);constant("MNEE_PATH_RECEIVER_ANCESTOR",PATH_MNEE_RECEIVER_ANCESTOR);
  constant("MNEE_PATH_CULL_LIGHT_CONNECTION",PATH_MNEE_CULL_LIGHT_CONNECTION);constant("MNEE_PATH_SAMPLED",PATH_MNEE_SAMPLED);
  constant("MNEE_KERNEL_FEATURE",KERNEL_FEATURE_MNEE);constant("MNEE_L_use_caustics",offsetof(KernelLight,use_caustics));
  constant("MNEE_D_use_caustics",offsetof(KernelData,integrator.use_caustics));
  constant("SD_OBJECT_CAUSTICS_CASTER",SD_OBJECT_CAUSTICS_CASTER);constant("SD_OBJECT_CAUSTICS_RECEIVER",SD_OBJECT_CAUSTICS_RECEIVER);
  constant("MNEE_CLOSURE_REFRACTION_FIRST",CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID);constant("MNEE_CLOSURE_REFRACTION_LAST",CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID);
  constant("MNEE_CLOSURE_GLASS_FIRST",CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID);constant("MNEE_CLOSURE_GLASS_LAST",CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID);
  constant("MNEE_CAUSTICS_MAX_CLOSURE",CAUSTICS_MAX_CLOSURE);constant("MR_CLOSURES",mnee_closure_offset);constant("MNEE_RECORD_BYTES",mnee_record_bytes);
#define ISECT(index,member) constant("MNEE_S_I" #index "_" #member,offsetof(IntegratorStateGPU,shadow_isect[index].member))
  ISECT(0,t);ISECT(0,u);ISECT(0,v);ISECT(0,object);ISECT(0,prim);ISECT(0,type);
  ISECT(1,t);ISECT(1,u);ISECT(1,v);ISECT(1,object);ISECT(1,prim);ISECT(1,type);ISECT(2,prim);
#undef ISECT
  static_assert(INTEGRATOR_SHADOW_ISECT_SIZE_GPU>=4,"MNEE payload and the shadow ray self record share shadow_isect");
  out<<"//#endif\n";return out.str();
}
// Scene combinations MNEE supports on this device; everything else is refused
// before any MNEE dispatch, never rendered without caustics.
void WebGPUDevice::validate_mnee() const{
  require(data.kernel_features&KERNEL_FEATURE_MNEE,"Invalid native MNEE feature flags");
  require(!data.integrator.use_light_tree,"WebGPU MNEE with the light tree not implemented");
  require(!(data.kernel_features&KERNEL_FEATURE_VOLUME),"WebGPU MNEE with volumes not implemented");
  require(!data.bvh.have_motion,"WebGPU MNEE with motion blur not implemented");
  require(!(data.kernel_features&(KERNEL_FEATURE_LIGHT_TREE|KERNEL_FEATURE_LIGHT_LINKING|KERNEL_FEATURE_SHADOW_LINKING|KERNEL_FEATURE_AO)),
          "WebGPU MNEE with light linking, shadow linking or AO not implemented");
  for(const KernelLight &light:light_records)if(light.use_caustics)
    require(light.type==LIGHT_POINT||light.type==LIGHT_SUN,"WebGPU MNEE caustics from spot, area or background lights not implemented");
}
void WebGPUDevice::enqueue_mnee(int work,const DeviceKernelArguments &args){
  require(args.count==2,"MNEE argument count");
  require(args.types[1]==DeviceKernelArguments::INT32&&args.sizes[1]==4,"Invalid MNEE work argument");
  int n;std::memcpy(&n,args.values[1],4);require(n>=0&&work>=n,"Incomplete MNEE dispatch");
  require(args.types[0]==DeviceKernelArguments::POINTER&&args.sizes[0]==sizeof(device_ptr),"Invalid MNEE pointer argument");
  device_ptr indices;std::memcpy(&indices,args.values[0],sizeof(indices));require(indices%4==0,"Unaligned MNEE indices");
  std::array<uint32_t,32> p{};p[1]=n;p[2]=uint32_t(indices);p[17]=error_word;if(p[2])validate(p[2],size_t(n)*4);
  require(!data.bvh.have_curves&&!data.bvh.have_points,"WebGPU MNEE currently requires triangle primitives");
  require(data.integrator.use_caustics,"Invalid native MNEE dispatch without caustics");
  validate_mnee();validate_geometry_motion();validate_lighting(data.integrator.use_direct_light);
  require(data.integrator.sampling_pattern==SAMPLING_PATTERN_TABULATED_SOBOL,"WebGPU MNEE requires tabulated Sobol");
  require(data.max_shaders>0&&state.sort_partition_divisor>0,"Invalid MNEE sort configuration");
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");p[9]=globals.at("sample_pattern_lut");p[18]=globals.at("shaders");
  require(remaining(p[18])%sizeof(KernelShader)==0,"Invalid MNEE shader table");p[19]=uint32_t(remaining(p[18])/sizeof(KernelShader));
  if(auto it=globals.find("svm_nodes");it!=globals.end()){p[21]=it->second;require(remaining(p[21])%4==0,"Invalid SVM word table");p[22]=uint32_t(remaining(p[21])/4);}
  p[10]=uint32_t(remaining(uintptr_t(state.path.render_pixel_index))/4);
  if(!p[2])require(uint32_t(n)<=p[10],"MNEE dispatch exceeds path capacity");
  require(uint32_t(work)<=svm_stack_slots,"MNEE dispatch exceeds the SVM stack slots");
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(feature==KERNEL_FEATURE_PATH_TRACING||feature==KERNEL_FEATURE_MNEE)validate(uintptr_t(state.parent.name),size_t(p[10])*sizeof(type));
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
  p[25]=uint32_t(remaining(uintptr_t(state.shadow_path.render_pixel_index))/4);
  for(int k=0;k<3;k++){validate(uintptr_t(state.shadow_isect[k].t),size_t(p[25])*4);validate(uintptr_t(state.shadow_isect[k].prim),size_t(p[25])*4);}
  validate(uintptr_t(state.next_shadow_path_index),4);
  const auto sequence=data.integrator.tabulated_sobol_sequence_size;
  require(sequence>1&&(sequence&(sequence-1))==0,"Invalid MNEE Sobol sequence size");
  validate(p[9],size_t(sequence)*NUM_TAB_SOBOL_PATTERNS*NUM_TAB_SOBOL_DIMENSIONS*4);
  p[14]=scene_tables();
  if(!work||!n)return;
  const size_t needed=size_t(work)*mnee_record_bytes+16;
  if(needed>mnee_record_bytes_allocated){
    if(mnee_records){flush();release(mnee_records);mnee_records=0;mnee_record_bytes_allocated=0;}
    mnee_records=allocate(needed);mnee_record_bytes_allocated=needed;
  }
  p[26]=mnee_records;
  // Stable MNEE shadow slots: every dispatched path owns slot base+id (control op 5, as the
  // surface stage reserves them for transparent shadows), so slot numbers, and with them
  // later compaction decisions, do not depend on atomic order. The native scheduler
  // budgets one shadow slot per INTERSECT_MNEE input (kernel_creates_shadow_paths);
  // unused reservations stay terminated and compaction removes them.
  const size_t reserve_bytes=sizeof(uint32_t);
  static const char *const entries[mnee_entries]={"mnee_receiver","mnee_seed","mnee_eval","mnee_setup","mnee_solve","mnee_transfer","mnee_contribute"};
  if(!pipeline_current(mnee_pipelines[mnee_entries-1],"mnee_contribute")){
    auto built=build_pipelines("surface",surface_source(),{entries[0],entries[1],entries[2],entries[3],entries[4],entries[5],entries[6]});
    for(unsigned i=0;i<mnee_entries;i++)mnee_pipelines[i]=std::move(built[i]);
  }
  p[28]=mnee_records+uint32_t(size_t(work)*mnee_record_bytes);(void)reserve_bytes;
  {std::array<uint32_t,32> reserve{};reserve[0]=5;reserve[1]=n;reserve[2]=uint32_t(uintptr_t(state.next_shadow_path_index));
   reserve[3]=uint32_t(uintptr_t(state.shadow_path.queued_kernel));reserve[4]=p[28];reserve[17]=error_word;reserve[18]=p[25];dispatch(reserve,1);}
  // Dispatch ordering makes each stage's GPU-private record visible to the next.
  for(unsigned i=0;i<mnee_entries;i++)dispatch(p,uint32_t(work),mnee_pipelines[i],workgroup(entries[i]));
}
}
