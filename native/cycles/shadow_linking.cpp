// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Shadow linking on the WebGPU device: DEVICE_KERNEL_INTEGRATOR_INTERSECT_DEDICATED_LIGHT and
// DEVICE_KERNEL_INTEGRATOR_SHADE_DEDICATED_LIGHT (wavefront_shadow_link_intersect.wgsl, _lamp.wgsl, _shade.wgsl).
// Blocker sets in the traversals and the indirect-emitter rules of the main path are SHADOW_LINKING regions of
// wavefront_closest.wgsl, wavefront_shadow_traversal.wgsl, wavefront_surface.wgsl and wavefront_light_eval.wgsl.
#include "device.h"
#include "wavefront.h"
#include <cstring>
#include <stdexcept>
namespace ccl {
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static constexpr size_t dedicated_record_stride=32; // DL_RECORD_BYTES (camera.cpp, wavefront_shadow_link_lamp.wgsl).
// The combinations shadow linking renders on this device; the others are refused by name before any dispatch.
void WebGPUDevice::validate_shadow_linking() const{
  if(!(data.kernel_features&KERNEL_FEATURE_SHADOW_LINKING))return;
  // Volumes: the forward MIS of a phase scatter on the flat CDF; on the tree (previous_dt, segment importance) not gated.
  require(!((data.kernel_features&KERNEL_FEATURE_VOLUME)&&data.integrator.use_light_tree),"WebGPU shadow linking with volumes and the light tree not implemented");
  // Spot, area and triangle lights in volume direct light and the volume light passes have no gate case with shadow linking.
  if(data.kernel_features&KERNEL_FEATURE_VOLUME){
    if(data.integrator.use_direct_light){
      for(const KernelLight &light:light_records)
        require(light.type==LIGHT_POINT||light.type==LIGHT_SUN||light.type==LIGHT_BACKGROUND,"WebGPU shadow linking with volumes and spot or area lights not implemented");
      for(size_t i=0;i+1<light_distribution_records.size();i++)
        require(light_distribution_records[i].prim<0,"WebGPU shadow linking with volumes and emissive triangles not implemented");
    }
    require(data.film.pass_volume_direct==PASS_UNUSED&&data.film.pass_volume_indirect==PASS_UNUSED,"WebGPU shadow linking with volume light passes not implemented");
  }
  require(!data.integrator.has_shadow_catcher,"WebGPU shadow linking with a shadow catcher not implemented");
  require(!(data.kernel_features&(KERNEL_FEATURE_AO_ADDITIVE|KERNEL_FEATURE_AO_PASS)),"WebGPU shadow linking with ambient occlusion not implemented");
}
void WebGPUDevice::enqueue_dedicated_light(DeviceKernel kernel,int work,const DeviceKernelArguments &args){
  const bool intersect=kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_DEDICATED_LIGHT;
  require(args.count==(intersect?2:3),"Dedicated light argument count");
  const unsigned size_argument=intersect?1:2;
  require(args.types[size_argument]==DeviceKernelArguments::INT32&&args.sizes[size_argument]==4,"Invalid dedicated light work argument");
  int n;std::memcpy(&n,args.values[size_argument],4);require(n>=0&&work>=n,"Incomplete dedicated light dispatch");
  require(args.types[0]==DeviceKernelArguments::POINTER&&args.sizes[0]==sizeof(device_ptr),"Invalid dedicated light pointer argument");
  device_ptr indices;std::memcpy(&indices,args.values[0],sizeof(indices));require(indices%4==0,"Unaligned dedicated light indices");
  std::array<uint32_t,32> p{};p[1]=n;p[2]=uint32_t(indices);p[17]=error_word;if(p[2])validate(p[2],size_t(n)*4);
  require(data.kernel_features&KERNEL_FEATURE_SHADOW_LINKING,"Invalid native dedicated light dispatch without shadow linking");
  require(!data.bvh.have_curves&&!data.bvh.have_points,"WebGPU dedicated light currently requires triangle primitives");
  require(!(data.kernel_features&KERNEL_FEATURE_PATH_GUIDING),"WebGPU dedicated light optional integration features not implemented");
  validate_shadow_linking();validate_geometry_motion();validate_volume();validate_lighting(false);
  if(data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)require(state.path.mis_ray_object,"Missing light linking path state");
  require(state.shadow_link.dedicated_light_weight&&state.shadow_link.last_isect_prim&&state.shadow_link.last_isect_object,"Missing shadow linking path state");
  p[6]=constants.at("data");p[7]=constants.at("integrator_state");p[9]=sample_pattern_table("dedicated light");
  p[10]=uint32_t(remaining(uintptr_t(state.path.render_pixel_index))/4);
  if(!p[2])require(uint32_t(n)<=p[10],"Dedicated light dispatch exceeds path capacity");
  validate(uintptr_t(state.shadow_link.dedicated_light_weight),size_t(p[10])*sizeof(float));
  validate(uintptr_t(state.shadow_link.last_isect_prim),size_t(p[10])*sizeof(int));
  validate(uintptr_t(state.shadow_link.last_isect_object),size_t(p[10])*sizeof(int));
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
  p[14]=scene_tables();
  if(intersect){
    if(!work||!n)return;
    if(!pipeline_current(dedicated_intersect,"intersect_dedicated_light"))dedicated_intersect=std::move(build_pipelines("closest",closest_wgsl(),{"intersect_dedicated_light"})[0]);
    dispatch(p,uint32_t(work),dedicated_intersect,workgroup("intersect_dedicated_light"));
    return;
  }
  // SHADE_DEDICATED_LIGHT: the shade stage's push words (background.cpp, surface.cpp) that its entries read.
  require(args.types[1]==DeviceKernelArguments::POINTER&&args.sizes[1]==sizeof(device_ptr),"Invalid dedicated light render buffer argument");
  p[18]=globals.at("shaders");require(remaining(p[18])%sizeof(KernelShader)==0,"Invalid dedicated light shader table");p[19]=uint32_t(remaining(p[18])/sizeof(KernelShader));
  p[20]=(data.kernel_features&KERNEL_FEATURE_LIGHT_PASSES)!=0;
  if(auto it=globals.find("svm_nodes");it!=globals.end()){p[21]=it->second;require(remaining(p[21])%4==0,"Invalid SVM word table");p[22]=uint32_t(remaining(p[21])/4);}
  p[25]=uint32_t(remaining(uintptr_t(state.shadow_path.render_pixel_index))/4);
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(KernelFeatureRequest(feature).test(data.kernel_features))validate(uintptr_t(state.parent.name),size_t(p[25])*sizeof(type));
#define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#define KERNEL_STRUCT_ARRAY_MEMBER(parent,type,name,feature)
#define KERNEL_STRUCT_END(name)
#define KERNEL_STRUCT_END_ARRAY(name,cpu_size,gpu_size)
#include "kernel/integrator/shadow_state_template.h"
#undef KERNEL_STRUCT_BEGIN
#undef KERNEL_STRUCT_BEGIN_PACKED
#undef KERNEL_STRUCT_MEMBER
#undef KERNEL_STRUCT_MEMBER_PACKED
#undef KERNEL_STRUCT_ARRAY_MEMBER
#undef KERNEL_STRUCT_END
#undef KERNEL_STRUCT_END_ARRAY
  require(state.shadow_path.bsdf_eval_average||(data.kernel_features&KERNEL_FEATURE_LIGHT_TREE),"Missing shadow BSDF average");
  validate(uintptr_t(state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].object),size_t(p[25])*4);
  validate(uintptr_t(state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].prim),size_t(p[25])*4);
  validate(uintptr_t(state.next_shadow_path_index),4);
  if(!work||!n)return;
  // Per input, the record (32 bytes, zeroed: no lamp value yet) and, after the records, the reserved slot word.
  const size_t records=size_t(work)*dedicated_record_stride;const size_t needed=records+16;
  if(needed>dedicated_record_bytes){
    if(dedicated_records){flush();release(dedicated_records);dedicated_records=0;dedicated_record_bytes=0;}
    dedicated_records=allocate(needed);dedicated_record_bytes=needed;
  }
  p[24]=dedicated_records;
  // Stable slots: every input owns base+id (control op 5), as the surface stage and MNEE reserve them; the
  // native scheduler budgets one shadow slot per input (kernel_creates_shadow_paths).
  {std::array<uint32_t,32> reserve{};reserve[0]=5;reserve[1]=n;reserve[2]=uint32_t(uintptr_t(state.next_shadow_path_index));
   reserve[3]=uint32_t(uintptr_t(state.shadow_path.queued_kernel));reserve[4]=dedicated_records+uint32_t(records);reserve[17]=error_word;reserve[18]=p[25];dispatch(reserve,1);}
  const bool tree=data.integrator.use_light_tree;
  if(tree&&(shader_features()&SF_LIGHT_TREE).any()){
    // The forward tree pdf of the lamp hit: light_tree_forward (background.cpp), one slot per path.
    const size_t bytes=size_t(p[10])*sizeof(float);
    if(bytes>light_tree_forward_bytes){
      if(light_tree_forward_scratch){flush();release(light_tree_forward_scratch);light_tree_forward_scratch=0;light_tree_forward_bytes=0;}
      light_tree_forward_scratch=allocate(bytes);light_tree_forward_bytes=bytes;
    }
    auto q=p;q[28]=light_tree_forward_scratch;q[29]=1;
    if(!pipeline_current(light_tree_forward,"light_tree_forward"))light_tree_forward=std::move(build_pipelines("background",background_wgsl(),{"light_tree_forward"})[0]);
    dispatch(q,uint32_t(work),light_tree_forward,workgroup("light_tree_forward"));
    if(!pipeline_current(dedicated_lamp,"shade_dedicated_light_lamp"))dedicated_lamp=std::move(build_pipelines("background",background_wgsl(),{"shade_dedicated_light_lamp"})[0]);
    dispatch(q,uint32_t(work),dedicated_lamp,workgroup("shade_dedicated_light_lamp"));
    if(!pipeline_current(dedicated_tree,"dedicated_light_tree_pdf"))dedicated_tree=std::move(build_pipelines("surface",surface_source(),{"dedicated_light_tree_pdf"})[0]);
    dispatch(p,uint32_t(work),dedicated_tree,workgroup("dedicated_light_tree_pdf"));
  }else{
    if(!pipeline_current(dedicated_lamp,"shade_dedicated_light_lamp"))dedicated_lamp=std::move(build_pipelines("background",background_wgsl(),{"shade_dedicated_light_lamp"})[0]);
    dispatch(p,uint32_t(work),dedicated_lamp,workgroup("shade_dedicated_light_lamp"));
  }
  p[28]=dedicated_records+uint32_t(records);
  if(!pipeline_current(dedicated_shade,"shade_dedicated_light"))dedicated_shade=std::move(build_pipelines("surface",surface_source(),{"shade_dedicated_light"})[0]);
  dispatch(p,uint32_t(work),dedicated_shade,workgroup("shade_dedicated_light"));
}
}
