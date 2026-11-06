// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device/device.h"
#include "image_reference.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#include "kernel/integrator/intersect_dedicated_light.h"
#include "kernel/integrator/shade_dedicated_light.h"
#include "kernel/integrator/intersect_shadow.h"
#include "scene/scene.h"
#include "wavefront.h"
#include "shadow_linking_reference.h"
#include <algorithm>
#include <memory>
namespace {
struct Globals {
  ccl::KernelGlobalsCPU base;std::vector<ccl::KernelImageInfo> images;ccl::Profiler profiler;std::unique_ptr<ccl::ThreadKernelGlobalsCPU> thread;
  explicit Globals(ccl::DeviceScene &scene){
    using namespace ccl;base.data=scene.data;images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
    CBC_SCENE_TABLES(TABLE)
    TABLE(svm_nodes,uint) TABLE(lookup_table,float) TABLE(sample_pattern_lut,float)
#undef TABLE
    thread=std::make_unique<ThreadKernelGlobalsCPU>(base,nullptr,profiler,0);
  }
};
template<class T> void three(float (&out)[3],const T &v){out[0]=v.x;out[1]=v.y;out[2]=v.z;}
}
void original_dedicated(ccl::DeviceScene &scene,ccl::DeviceKernel kernel,const std::vector<int> &ids,std::vector<ccl::IntegratorStateCPU> &states){
  using namespace ccl;Globals globals(scene);KernelGlobals kg=globals.thread.get();
  for(int i:ids){if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_DEDICATED_LIGHT)integrator_intersect_dedicated_light(kg,&states[i]);else integrator_shade_dedicated_light(kg,&states[i],nullptr);}
}
DedicatedRow original_dedicated_row(ccl::DeviceScene &scene,const ccl::IntegratorStateCPU &input){
  using namespace ccl;Globals globals(scene);KernelGlobals kg=globals.thread.get();DedicatedRow r{};
  auto copy=std::make_unique<IntegratorStateCPU>(input);IntegratorState state=copy.get();
  three(r.P,input.ray.P);three(r.D,input.ray.D);r.tmin=input.ray.tmin;r.tmax=input.ray.tmax;r.time=input.ray.time;r.dP=input.ray.dP;r.dD=input.ray.dD;r.previous_dt=input.ray.previous_dt;
  r.isect_t=input.isect.t;r.isect_u=input.isect.u;r.isect_v=input.isect.v;r.isect_object=input.isect.object;r.isect_prim=input.isect.prim;r.isect_type=input.isect.type;
  r.flag=input.path.flag;r.visibility=input.path.visibility;r.rng_pixel=input.path.rng_pixel;r.rng_offset=input.path.rng_offset;r.sample=input.path.sample;r.bounce=input.path.bounce;
  r.diffuse_bounce=input.path.diffuse_bounce;r.glossy_bounce=input.path.glossy_bounce;r.transmission_bounce=input.path.transmission_bounce;r.transparent_bounce=input.path.transparent_bounce;
  r.volume_bounds_bounce=input.path.volume_bounds_bounce;r.render_pixel_index=input.path.render_pixel_index;
  three(r.throughput,input.path.throughput);r.mis_ray_pdf=input.path.mis_ray_pdf;three(r.mis_origin_n,input.path.mis_origin_n);r.mis_ray_object=input.path.mis_ray_object;
  three(r.pass_diffuse,input.path.pass_diffuse_weight);three(r.pass_glossy,input.path.pass_glossy_weight);
  integrator_intersect_dedicated_light(kg,state);
  r.after_intersect=copy->path.queued_kernel;r.out_t=copy->isect.t;r.out_u=copy->isect.u;r.out_v=copy->isect.v;r.out_object=copy->isect.object;r.out_prim=copy->isect.prim;r.out_type=copy->isect.type;
  if(r.after_intersect!=DEVICE_KERNEL_INTEGRATOR_SHADE_DEDICATED_LIGHT)return r;
  r.weight=copy->shadow_link.dedicated_light_weight;r.last_prim=copy->shadow_link.last_isect_prim;r.last_object=copy->shadow_link.last_isect_object;
  // The MIS weight, by the pinned functions shadow_linking_shade_light / _surface_emission call.
  {const PathRayVisibility path_visibility=INTEGRATOR_STATE(state,path,visibility);const uint32_t path_flag=INTEGRATOR_STATE(state,path,flag);
   Intersection isect;integrator_state_read_isect(state,&isect);Ray ray;integrator_state_read_ray(state,&ray);
   if(isect.type==PRIMITIVE_LAMP){const float3 N=INTEGRATOR_STATE(state,path,mis_origin_n);
     const LightEval eval=shadow_linking_light_eval_from_intersection(kg,isect,ray,N,path_flag);const ccl_global KernelLight *klight=&kernel_data_fetch(lights,isect.prim);
     if(eval.eval_fac!=0.0f&&is_light_shader_visible_to_path(klight->shader_id,path_visibility,path_flag)){r.mis_valid=1;
       r.mis=shadow_linking_light_sample_mis_weight(kg,state,path_visibility,path_flag,isect.prim,isect.object,eval.pdf,ray.P);}
   }else{ShaderDataTinyStorage storage;ShaderData *sd=AS_SHADER_DATA(&storage);integrate_surface_shader_setup(kg,state,sd);
     if(!(sd->shader_flag&SD_HAS_ONLY_VOLUME)){r.mis_valid=1;r.mis=light_sample_mis_weight_forward_surface(kg,state,path_visibility,path_flag,sd);}}}
  copy->shadow.shadow_path.queued_kernel=0;
  integrator_shade_dedicated_light(kg,state,nullptr);
  r.after_shade=copy->path.queued_kernel;r.restored_prim=copy->isect.prim;r.restored_object=copy->isect.object;
  const auto &s=copy->shadow;r.shadow_kernel=s.shadow_path.queued_kernel;if(!r.shadow_kernel)return r;
  three(r.s_throughput,s.shadow_path.throughput);three(r.s_P,s.shadow_ray.P);three(r.s_D,s.shadow_ray.D);r.s_tmin=s.shadow_ray.tmin;r.s_tmax=s.shadow_ray.tmax;r.s_time=s.shadow_ray.time;r.s_dP=s.shadow_ray.dP;r.s_dD=s.shadow_ray.dD;
  r.s_light_object=s.shadow_ray.self_light_object;r.s_light_prim=s.shadow_ray.self_light_prim;
  r.s_self_object=s.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1].object;r.s_self_prim=s.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1].prim;
  r.s_bounce=s.shadow_path.bounce;r.s_diffuse=s.shadow_path.diffuse_bounce;r.s_glossy=s.shadow_path.glossy_bounce;r.s_transmission=s.shadow_path.transmission_bounce;r.s_transparent=s.shadow_path.transparent_bounce;
  r.s_volume_bounds=s.shadow_path.volume_bounds_bounce;r.s_group=s.shadow_path.lightgroup;r.s_flag=s.shadow_path.flag;r.s_visibility=s.shadow_path.visibility;r.s_pixel=s.shadow_path.render_pixel_index;
  r.s_sample=s.shadow_path.sample;r.s_rng_pixel=s.shadow_path.rng_pixel;r.s_rng_offset=s.shadow_path.rng_offset;r.s_average=s.shadow_path.bsdf_eval_average;
  if(r.shadow_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW){auto shadow=std::make_unique<IntegratorShadowStateCPU>(s);
    integrator_intersect_shadow(kg,shadow.get());r.shadow_after_intersect=shadow->shadow_path.queued_kernel;r.shadow_hits=shadow->shadow_path.packed_num_hits;}
  return r;
}
ShadowRow original_shadow_row(ccl::DeviceScene &scene,const ccl::IntegratorShadowStateCPU &input){
  using namespace ccl;Globals globals(scene);KernelGlobals kg=globals.thread.get();ShadowRow r{};
  three(r.P,input.shadow_ray.P);three(r.D,input.shadow_ray.D);r.tmin=input.shadow_ray.tmin;r.tmax=input.shadow_ray.tmax;r.time=input.shadow_ray.time;
  r.self_object=input.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1].object;r.self_prim=input.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1].prim;
  r.light_object=input.shadow_ray.self_light_object;r.light_prim=input.shadow_ray.self_light_prim;r.transparent_bounce=input.shadow_path.transparent_bounce;r.flag=input.shadow_path.flag;r.visibility=input.shadow_path.visibility;
  auto copy=std::make_unique<IntegratorShadowStateCPU>(input);integrator_intersect_shadow(kg,copy.get());r.after=copy->shadow_path.queued_kernel;r.hits=copy->shadow_path.packed_num_hits;
  // The hit the blocker decision rests on (diagnostic).
  {Ray ray;integrator_state_read_shadow_ray(copy.get(),&ray);ray.P=make_float3(r.P[0],r.P[1],r.P[2]);ray.D=make_float3(r.D[0],r.D[1],r.D[2]);ray.tmin=r.tmin;ray.tmax=r.tmax;ray.time=r.time;ray.self.object=r.self_object;ray.self.prim=r.self_prim;ray.self.light_object=r.light_object;ray.self.light_prim=r.light_prim;
   Intersection isect;if(scene_intersect(kg,&ray,PATH_RAY_VISIBILITY_SHADOW_OPAQUE,&isect)){r.blocker_object=isect.object;r.blocker_prim=isect.prim;r.blocker_t=isect.t;}else{r.blocker_object=OBJECT_NONE;r.blocker_prim=PRIM_NONE;}}
  *copy=input;copy->shadow_ray.self_light_object=OBJECT_NONE;integrator_intersect_shadow(kg,copy.get());r.unlinked_blocked=copy->shadow_path.queued_kernel==0;
  return r;
}
std::array<float,24> original_motion_transform(ccl::DeviceScene &scene,int object,float time){
  using namespace ccl;Globals globals(scene);KernelGlobals kg=globals.thread.get();Transform itfm;const Transform tfm=object_fetch_transform_motion_test(kg,object,time,&itfm);std::array<float,24> out;
  const float4 rows[6]={tfm.x,tfm.y,tfm.z,itfm.x,itfm.y,itfm.z};for(int i=0;i<6;i++){out[i*4]=rows[i].x;out[i*4+1]=rows[i].y;out[i*4+2]=rows[i].z;out[i*4+3]=rows[i].w;}
  return out;
}
