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
// Typed host copies are capability checks only; GPU kernels evaluate every
// ray-time transform. Native arrays stay authoritative for rendering.
// Motion support is limited to triangle deformation and triangle instances.
// Animated emitters, curves, points and subdivision are outside this adapter;
// transparent-shadow and volume combinations retain their separate guards.
void WebGPUDevice::validate_geometry_motion() const{
  require(object_records.size()==object_flag_records.size(),"Invalid native object flag table");
  bool moving=false;const auto motion=globals.find("object_motion");
  const size_t count=motion==globals.end()?0:remaining(motion->second)/sizeof(DecomposedTransform);
  if(motion!=globals.end())require(remaining(motion->second)%sizeof(DecomposedTransform)==0,"Invalid object motion table size");
  for(size_t i=0;i<object_records.size();i++){
    const auto flags=object_flag_records[i];const auto &object=object_records[i];
    if(flags&SD_OBJECT_HAS_VERTEX_MOTION){
      moving=true;require(object.primitive_type==PRIMITIVE_MOTION_TRIANGLE,"Invalid deforming triangle flags/type");
      require(object.num_geom_steps>=2&&object.numverts>0&&object.numprims>0,"Invalid deforming triangle steps/size");
    }else require(object.primitive_type!=PRIMITIVE_MOTION_TRIANGLE,"Invalid deforming triangle flags/type");
    if(flags&SD_OBJECT_MOTION){
      moving=true;require(object.primitive_type==PRIMITIVE_TRIANGLE||object.primitive_type==PRIMITIVE_MOTION_TRIANGLE,"WebGPU object motion requires triangle instances; animated emitters not implemented");
      require(!(flags&SD_OBJECT_TRANSFORM_APPLIED),"Invalid applied object motion transform");
      require(object.num_tfm_steps>=2&&object.motion_offset<=count&&object.num_tfm_steps<=count-object.motion_offset,
              "Invalid native object motion range");
    }
  }
  require(bool(data.bvh.have_motion)==moving,"Native object motion flag/table mismatch");
}
uint32_t WebGPUDevice::scene_tables(){
  load_image_info();
  std::vector<uint32_t> descriptors;
#define TABLE(name,type) {auto it=globals.find(#name);uint32_t address=it==globals.end()?0:it->second;size_t bytes=address?remaining(address):0; \
    require(bytes%sizeof(type)==0,"Invalid native scene table size: " #name);descriptors.insert(descriptors.end(),{address,uint32_t(bytes/sizeof(type))});}
  CBC_SCENE_CORE_TABLES(TABLE) CBC_DEVICE_TABLES(TABLE) CBC_LIGHT_TREE_TABLES(TABLE)
#undef TABLE
  if(!intersection_tables)intersection_tables=allocate(descriptors.size()*4);
  if(descriptors!=intersection_table_cache){upload(intersection_tables,descriptors.data(),descriptors.size()*4);intersection_table_cache=descriptors;}
  return intersection_tables;
}
std::string closest_wgsl(){return wavefront_layout()+lighting_layout()+shadow_catcher_layout()+closest_source;}
void WebGPUDevice::enqueue_closest(int work,const DeviceKernelArguments &args){
  require(args.count==3,"Closest intersection argument count");
  require(args.types[2]==DeviceKernelArguments::INT32&&args.sizes[2]==4,"Invalid closest work argument");
  int n;std::memcpy(&n,args.values[2],4);require(n>=0&&work>=n,"Incomplete closest intersection dispatch");
  auto pointer=[&](unsigned i,bool nullable){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid closest pointer argument");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));if(address||!nullable)validate(address,4);
    require(address%4==0,"Unaligned closest pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};p[1]=n;p[2]=pointer(0,true);p[4]=pointer(1,false);p[17]=error_word;
  if(p[2])validate(p[2],size_t(n)*4);
  require(!data.bvh.have_curves&&!data.bvh.have_points,"WebGPU closest currently requires triangle primitives");
  validate_geometry_motion();
  require(!(data.kernel_features&(KERNEL_FEATURE_PATH_GUIDING|KERNEL_FEATURE_SHADOW_LINKING))&&
          !data.integrator.use_caustics,"WebGPU closest optional integration features not implemented");
  validate_shadow_catcher();
  validate_volume();
  validate_lighting(false);
  if(data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)require(state.path.mis_ray_object,"Missing light linking path state");
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
  p[24]=shadow_catcher_marks(p[10]);
  if(!pipeline_current(closest,"intersect_closest"))closest=std::move(build_pipelines("closest",closest_wgsl(),{"intersect_closest"})[0]);
  dispatch(p,uint32_t(work),closest,workgroup("intersect_closest"));
  split_shadow_catcher(p[10]);
}
}
