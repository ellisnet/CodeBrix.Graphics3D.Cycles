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
      // The motion pass without motion blur keeps a deforming mesh as plain triangles with its motion steps in the
      // position table (FILM_MOTION_DEFORM); with motion blur it is a motion-triangle mesh. The flags and type first
      // (the base order of these rejections), then the steps.
      if(object.primitive_type==PRIMITIVE_TRIANGLE)require(film_motion_vectors(),"Invalid deforming triangle flags/type");
      else{moving=true;require(object.primitive_type==PRIMITIVE_MOTION_TRIANGLE,"Invalid deforming triangle flags/type");}
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
// Static curves through the closest and opaque shadow traversals. What this device does not do with curves yet is
// refused by name, before any dispatch. Volumes, subsurface scattering, MNEE, displacement and dedicated light kernels
// keep their own refusals (volume.cpp, subsurface.cpp, mnee.cpp, surface.cpp, shadow_linking.cpp).
void WebGPUDevice::validate_curves(){
  if(!data.bvh.have_curves)return;
  require(!data.bvh.have_motion,"WebGPU curves with motion blur not implemented");
  require(!data.integrator.transparent_shadows,"WebGPU curves with transparent shadows not implemented");
  require(!data.integrator.use_caustics,"WebGPU curves with caustics not implemented");
  require(!film_motion_vectors(),"WebGPU motion pass with curves not implemented");
  require(!curve_records.empty()&&globals.count("curve_keys")&&globals.count("prim_type"),"Missing native curve tables");
  require(remaining(globals.at("curve_keys"))%sizeof(float4)==0&&remaining(globals.at("prim_type"))%sizeof(int)==0,"Invalid native curve table size");
  require(data.bvh.curve_subdivisions>=1&&data.bvh.curve_subdivisions<=16,"Invalid native curve subdivisions");
  const ShaderFeatures features=shader_features();
  require(!(features&(SF_NODE_AO|SF_NODE_BEVEL|SF_NODE_RAYCAST)).any(),"WebGPU curves with Ambient Occlusion, Bevel or Raycast shader nodes not implemented");
  for(const KernelCurve &curve:curve_records){
    const int shape=curve.type&PRIMITIVE_CURVE;
    require(shape==PRIMITIVE_CURVE_RIBBON||shape==PRIMITIVE_CURVE_THICK||shape==PRIMITIVE_CURVE_THICK_LINEAR,"Invalid native curve type");
    require(curve.first_key>=0&&curve.num_keys>=1,"Invalid native curve key range");
    require(size_t(uint32_t(curve.shader_id)&SHADER_MASK)<shader_records.size(),"Invalid native curve shader");
  }
}
uint32_t WebGPUDevice::scene_tables(){
  load_image_info();
  std::vector<uint32_t> descriptors;
#define TABLE(name,type) {auto it=globals.find(#name);uint32_t address=it==globals.end()?0:it->second;size_t bytes=address?remaining(address):0; \
    require(bytes%sizeof(type)==0,"Invalid native scene table size: " #name);descriptors.insert(descriptors.end(),{address,uint32_t(bytes/sizeof(type))});}
  CBC_SCENE_CORE_TABLES(TABLE) CBC_DEVICE_TABLES(TABLE) CBC_LIGHT_TREE_TABLES(TABLE)
  TABLE(ies,float)
  TABLE(volume_tree_nodes,KernelOctreeNode) TABLE(volume_tree_roots,KernelOctreeRoot) TABLE(volume_tree_root_ids,int) TABLE(volume_step_size,float)
  if(data.bvh.have_curves){CBC_CURVE_TABLES(TABLE)}
#undef TABLE
  // The descriptor list grows with curves (a scene edit can add or remove them).
  if(intersection_tables&&descriptors.size()!=intersection_table_cache.size()){flush();release(intersection_tables);intersection_tables=0;intersection_table_cache.clear();}
  if(!intersection_tables)intersection_tables=allocate(descriptors.size()*4);
  if(descriptors!=intersection_table_cache){upload(intersection_tables,descriptors.data(),descriptors.size()*4);intersection_table_cache=descriptors;}
  return intersection_tables;
}
std::string closest_wgsl(){return wavefront_layout()+lighting_layout()+shadow_catcher_layout()+mnee_layout()+closest_source;}
void WebGPUDevice::enqueue_closest(int work,const DeviceKernelArguments &args){
  require(args.count==3,"Closest intersection argument count");
  require(args.types[2]==DeviceKernelArguments::INT32&&args.sizes[2]==4,"Invalid closest work argument");
  int n;std::memcpy(&n,args.values[2],4);require(n>=0&&work>=n,"Incomplete closest intersection dispatch");
  auto pointer=[&](unsigned i,bool nullable){require(args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid closest pointer argument");
    device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));if(address||!nullable)validate(address,4);
    require(address%4==0,"Unaligned closest pointer");return uint32_t(address);};
  std::array<uint32_t,32> p{};p[1]=n;p[2]=pointer(0,true);p[4]=pointer(1,false);p[17]=error_word;
  if(p[2])validate(p[2],size_t(n)*4);
  require(!data.bvh.have_points,"WebGPU closest currently requires triangle or static curve primitives");
  validate_curves();
  validate_geometry_motion();
  require(!(data.kernel_features&KERNEL_FEATURE_PATH_GUIDING),"WebGPU closest optional integration features not implemented");
  validate_shadow_linking();
  validate_shadow_catcher();
  if(data.integrator.use_caustics)validate_mnee();
  validate_volume();
  validate_lighting(false);
  if(data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)require(state.path.mis_ray_object,"Missing light linking path state");
  require(data.max_shaders>0&&state.sort_partition_divisor>0,"Invalid closest sort configuration");
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");p[9]=sample_pattern_table("closest");
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
