// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Calls the unchanged pinned subsurface kernel (integrator_intersect_subsurface).
#include "device/device.h"
#include "image_reference.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#include "kernel/integrator/shade_surface.h"
#include "kernel/integrator/intersect_subsurface.h"
#include "kernel/bake/bake.h"
#include "scene/scene.h"
#include "wavefront.h"
#include "subsurface_reference.h"
void original_subsurface(ccl::DeviceScene &scene,const std::vector<int> &indices,std::vector<ccl::IntegratorStateCPU> &states){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  for(int i:indices)integrator_intersect_subsurface(kg,&states[i]);
}
// The hits scene_intersect_local finds for a Christensen-Burley path's probe ray, built as the pinned subsurface_disk
// builds it: their number and the kept ones sorted by t (sort_intersections_and_normals). Gate statistics: how often the
// BSSRDF_MAX_HITS reservoir chooses among more hits, and which sorted hit the kernel's resampling selected.
std::vector<DiskHits> original_disk_hits(ccl::DeviceScene &scene,const std::vector<int> &indices,const std::vector<ccl::IntegratorStateCPU> &states){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;std::vector<DiskHits> out;
  for(int i:indices){
    ConstIntegratorState state=&states[i];RNGState rng_state;path_state_rng_load(state,&rng_state);
    float2 rand_disk=path_state_rng_2D(kg,&rng_state,PRNG_SUBSURFACE_DISK);
    float3 disk_N=INTEGRATOR_STATE(state,subsurface,N),disk_T,disk_B;make_orthonormals(disk_N,&disk_T,&disk_B);
    if(rand_disk.y<0.5f)rand_disk.y*=2.0f;
    else if(rand_disk.y<0.75f){std::swap(disk_N,disk_T);rand_disk.y=(rand_disk.y-0.5f)*4.0f;}
    else{std::swap(disk_N,disk_B);rand_disk.y=(rand_disk.y-0.75f)*4.0f;}
    float disk_r,disk_height;bssrdf_sample(INTEGRATOR_STATE(state,subsurface,radius),rand_disk.x,&disk_r,&disk_height);
    Ray ray{};ray.P=INTEGRATOR_STATE(state,ray,P)+disk_N*disk_height+to_global(polar_to_cartesian(disk_r,M_2PI_F*rand_disk.y),disk_T,disk_B);
    ray.D=-disk_N;ray.tmin=0;ray.tmax=2.0f*disk_height;ray.time=INTEGRATOR_STATE(state,ray,time);
    ray.self.object=OBJECT_NONE;ray.self.prim=PRIM_NONE;ray.self.light_object=OBJECT_NONE;ray.self.light_prim=PRIM_NONE;
    LocalIntersection isect;uint lcg=lcg_state_init(rng_state.rng_pixel,rng_state.rng_offset,rng_state.sample,0x68bc21eb);
    scene_intersect_local(kg,&ray,&isect,INTEGRATOR_STATE(state,isect,object),&lcg,BSSRDF_MAX_HITS);
    DiskHits hits;hits.count=int(isect.num_hits);const int kept=min(hits.count,BSSRDF_MAX_HITS);
    if(kept>0)sort_intersections_and_normals(isect.hits,isect.Ng,kept);
    for(int k=0;k<kept;k++){hits.prim.push_back(isect.hits[k].prim);hits.t.push_back(isect.hits[k].t);}
    out.push_back(hits);
  }
  return out;
}
