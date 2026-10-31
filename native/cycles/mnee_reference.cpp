// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device/device.h"
#include "image_reference.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#include "kernel/integrator/intersect_mnee.h"
#include "scene/scene.h"
#include "wavefront.h"
#include "mnee_reference.h"
void original_mnee(ccl::DeviceScene &scene,const std::vector<int> &indices,std::vector<ccl::IntegratorStateCPU> &states){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(lookup_table,float) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  for(int i:indices)integrator_intersect_mnee(kg,&states[i]);
}
MneeTrace original_mnee_trace(ccl::DeviceScene &scene,const ccl::IntegratorStateCPU &input){
  using namespace ccl;MneeTrace out;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(lookup_table,float) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  IntegratorStateCPU copy=input;IntegratorState state=&copy;ShaderData sd;integrate_surface_shader_setup(kg,state,&sd);
  RNGState rng;path_state_rng_load(state,&rng);LightSample ls;
  const float3 rand_light=path_state_rng_3D(kg,&rng,PRNG_LIGHT);
  if(!light_sample_from_position(kg,rand_light,sd.time,sd.P,sd.N,light_link_receiver_nee(kg,&sd),sd.runtime_flag,copy.path.bounce,copy.path.flag,&ls))return out;
  ShaderDataCausticsStorage storage;ShaderData *sd_mnee=AS_SHADER_DATA(&storage);
  Ray probe;probe.self.object=sd.object;probe.self.prim=sd.prim;probe.self.light_object=ls.object;probe.self.light_prim=ls.prim;probe.P=sd.P;probe.tmin=0;
  if(ls.t==FLT_MAX){probe.D=ls.D;probe.tmax=ls.t;}else{probe.D=normalize_len(ls.P-probe.P,&probe.tmax);}
  probe.dP=differential_make_compact(sd.dP);probe.dD=differential_zero_compact();probe.time=sd.time;Intersection isect;
  ManifoldVertex v[MNEE_MAX_CAUSTIC_CASTERS];int count=0;
  for(int k=0;k<MNEE_MAX_INTERSECTION_COUNT;k++){
    if(!scene_intersect(kg,&probe,PATH_RAY_VISIBILITY_TRANSMIT,&isect))break;
    if(intersection_get_object_flags(kg,&isect)&SD_OBJECT_CAUSTICS_CASTER){
      if(count>=MNEE_MAX_CAUSTIC_CASTERS)return out;
      shader_setup_from_ray(kg,sd_mnee,&probe,&isect);
      surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE_SHADOW>(kg,state,sd_mnee,nullptr,PATH_RAY_VISIBILITY_DIFFUSE,PATH_RAY_FLAG_NONE,true);
      bool found=false;
      for(int ci=0;ci<sd_mnee->num_closure;ci++){ShaderClosure *bsdf=&sd_mnee->closure[ci];
        if(CLOSURE_IS_REFRACTION(bsdf->type)||CLOSURE_IS_GLASS(bsdf->type)){found=true;MicrofacetBsdf *m=(MicrofacetBsdf*)bsdf;
          const float eta=(sd_mnee->runtime_flag&SR_BACKFACING)?1.0f/m->ior:m->ior;float2 h=zero_float2();
          if(m->alpha_x>0&&m->alpha_y>0){const float2 uv=path_state_rng_2D(kg,&rng,PRNG_SURFACE_BSDF);h=mnee_sample_bsdf_dh(bsdf->type,m->alpha_x,m->alpha_y,uv.x,uv.y);}
          mnee_setup_manifold_vertex(kg,&v[count],bsdf,eta,h,&probe,&isect,sd_mnee);out.n_offset[count][0]=h.x;out.n_offset[count][1]=h.y;
          for(int c=0;c<3;c++)out.seed_p[count][c]=v[count].p[c];
          break;}}
      if(!found)return out;
      count++;
    }
    probe.self.object=isect.object;probe.self.prim=isect.prim;probe.tmin=intersection_t_offset(isect.t);
  }
  out.count=count;if(!count)return out;
  bool fixed=ls.t==FLT_MAX;
  // mnee_newton_solver with the iteration and constraint-norm record.
  float2 dx[MNEE_MAX_CAUSTIC_CASTERS];ManifoldVertex tentative[MNEE_MAX_CAUSTIC_CASTERS];Ray pr;
  pr.self.light_object=OBJECT_NONE;pr.self.light_prim=PRIM_NONE;pr.dP=differential_make_compact(sd.dP);pr.dD=differential_zero_compact();pr.tmin=0;pr.time=sd.time;Intersection pi;
  const float3 light=fixed?ls.D:ls.P;float beta=.1f;bool reduce=false,resolve=true;
  for(int it=0;it<MNEE_MAX_ITERATIONS;it++){
    out.iterations=it+1;
    if(resolve){if(!mnee_compute_constraint_derivatives(count,v,sd.P,fixed,light)){out.solved=2;break;}
      float norm=0;for(int i=0;i<count;i++)norm=::fmaxf(norm,len(v[i].constraint));out.norms[it]=norm;
      if(norm<MNEE_SOLVER_THRESHOLD){out.solved=1;break;}if(!mnee_solve_matrix_h_to_x(count,v,dx)){out.solved=3;break;}}
    bool stuck=false;
    for(int i=0;i<count;i++){const ManifoldVertex &mv=v[i];float3 tp=mv.p-beta*(dx[i].x*mv.dp_du+dx[i].y*mv.dp_dv);
      if(i==0&&dot(sd.Ng,tp-sd.P)<=0.f)tp=mv.p+beta*(dx[i].x*mv.dp_du+dx[i].y*mv.dp_dv);
      if(i==0){pr.self.object=sd.object;pr.self.prim=sd.prim;pr.P=sd.P;}else{pr.self.object=v[i-1].object;pr.self.prim=v[i-1].prim;pr.P=v[i-1].p;}
      pr.D=normalize_len(tp-pr.P,&pr.tmax);pr.tmax*=MNEE_PROJECTION_DISTANCE_MULTIPLIER;bool ok=false;
      for(int k=0;k<MNEE_MAX_INTERSECTION_COUNT;k++){if(!scene_intersect(kg,&pr,PATH_RAY_VISIBILITY_TRANSMIT,&pi))break;if(pi.object==mv.object){ok=true;break;}
        pr.self.object=pi.object;pr.self.prim=pi.prim;pr.tmin=intersection_t_offset(pi.t);}
      if(!ok){reduce=true;break;}
      ManifoldVertex &tv=tentative[i];tv.p=mv.p;tv.dp_du=mv.dp_du;tv.dp_dv=mv.dp_dv;mnee_setup_manifold_vertex(kg,&tv,mv.bsdf,mv.eta,mv.n_offset,&pr,&pi,sd_mnee);
      if(len(tv.p-mv.p)<MNEE_MIN_PROGRESS_DISTANCE){stuck=true;break;}}
    if(stuck){out.solved=4;break;}
    if(!reduce)for(int i=0;i<count;i++){const ManifoldVertex &tv=tentative[i];const float3 wi=(i==0?sd.P:tentative[i-1].p)-tv.p;
      const float3 wo=(i==count-1)?(fixed?ls.D:ls.P-tv.p):tentative[i+1].p-tv.p;if(dot(tv.n,wi)*dot(tv.n,wo)>=0.f){reduce=true;break;}}
    if(reduce){reduce=false;resolve=false;beta*=.5f;if(beta<MNEE_MINIMUM_STEP_SIZE){out.solved=5;break;}continue;}
    for(int i=0;i<count;i++)v[i]=tentative[i];
    resolve=true;beta=min(1.f,2.f*beta);
  }
  if(!out.solved)out.solved=6;
  for(int i=0;i<count;i++)for(int c=0;c<3;c++)out.final_p[i][c]=v[i].p[c];
  return out;
}
std::array<float,3> original_ray_hit(ccl::DeviceScene &scene,const float P[3],const float D[3],float tmin,float tmax,float time,int self_object,int self_prim,int light_object,int light_prim,unsigned visibility){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  Ray ray;ray.P=make_float3(P[0],P[1],P[2]);ray.D=make_float3(D[0],D[1],D[2]);ray.tmin=tmin;ray.tmax=tmax;ray.time=time;
  ray.self.object=self_object;ray.self.prim=self_prim;ray.self.light_object=light_object;ray.self.light_prim=light_prim;ray.dP=0;ray.dD=0;
  Intersection isect;if(!scene_intersect(kg,&ray,visibility,&isect))return {-1.f,-1.f,0.f};
  return {float(isect.object),float(isect.prim),isect.t};
}
#include "kernel/integrator/intersect_shadow.h"
int original_intersect_shadow_one(ccl::DeviceScene &scene,const ccl::IntegratorShadowStateCPU &input){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  auto copy=std::make_unique<IntegratorShadowStateCPU>(input);integrator_intersect_shadow(kg,copy.get());return copy->shadow_path.queued_kernel;
}
