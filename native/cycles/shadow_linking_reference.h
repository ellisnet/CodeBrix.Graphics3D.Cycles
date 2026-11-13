// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include <cstdint>
#include <array>
#include <vector>
namespace ccl {class DeviceScene;}
// One path at INTERSECT_DEDICATED_LIGHT: its input and what the unmodified pinned kernels make of it. Plain
// 32-bit words, so a table of rows travels through the reference exchange (reference_policy.h) as floats.
struct DedicatedRow {
  // Input: the path state the two kernels read.
  float P[3],D[3],tmin,tmax,time,dP,dD,previous_dt,isect_t,isect_u,isect_v;int isect_object,isect_prim,isect_type;
  uint32_t flag,visibility,rng_pixel,rng_offset,sample,bounce,diffuse_bounce,glossy_bounce,transmission_bounce,transparent_bounce,volume_bounds_bounce,render_pixel_index;
  float throughput[3],mis_ray_pdf,mis_origin_n[3];int mis_ray_object;float pass_diffuse[3],pass_glossy[3];
  // integrator_intersect_dedicated_light.
  uint32_t after_intersect;float out_t,out_u,out_v;int out_object,out_prim,out_type;float weight;int last_prim,last_object;
  // integrator_shade_dedicated_light: the MIS weight (mis_valid: the pinned kernel computed one), the restored self
  // primitives, the successor, and the dedicated shadow path.
  uint32_t mis_valid;float mis;uint32_t after_shade;int restored_prim,restored_object;
  uint32_t shadow_kernel;float s_throughput[3],s_P[3],s_D[3],s_tmin,s_tmax,s_time,s_dP,s_dD;int s_light_object,s_light_prim,s_self_object,s_self_prim;
  uint32_t s_bounce,s_diffuse,s_glossy,s_transmission,s_transparent,s_volume_bounds,s_group,s_flag,s_visibility,s_pixel,s_sample,s_rng_pixel,s_rng_offset;float s_average;
  // integrator_state_write_shadow_light_isect: shadow_isect[0] of a dedicated shadow path queued for SHADE_LIGHT_NEE (an emitter
  // whose shader is not constant): the picked intersection.
  float s_isect_t,s_isect_u,s_isect_v;int s_isect_object,s_isect_prim,s_isect_type;
  // integrator_intersect_shadow on that path: the blocker-set decision.
  uint32_t shadow_after_intersect,shadow_hits;
};
static_assert(sizeof(DedicatedRow)%4==0,"DedicatedRow travels as 32-bit words");
// One next-event shadow ray towards a shadow-linked emitter (blocker sets on integrator_intersect_shadow):
// unlinked_blocked: the same ray with no light object (every object blocks) does not reach the light.
struct ShadowRow {float P[3],D[3],tmin,tmax,time;int self_object,self_prim,light_object,light_prim;uint32_t transparent_bounce,flag,visibility,after,hits,unlinked_blocked;int blocker_object,blocker_prim;float blocker_t;};
static_assert(sizeof(ShadowRow)%4==0,"ShadowRow travels as 32-bit words");
// The unmodified pinned integrator_intersect_dedicated_light / integrator_shade_dedicated_light on host states.
void original_dedicated(ccl::DeviceScene&,ccl::DeviceKernel,const std::vector<int>&,std::vector<ccl::IntegratorStateCPU>&);
DedicatedRow original_dedicated_row(ccl::DeviceScene&,const ccl::IntegratorStateCPU&);
ShadowRow original_shadow_row(ccl::DeviceScene&,const ccl::IntegratorShadowStateCPU&);
// object_fetch_transform_motion_test at one time: the transform and its inverse, 24 words.
std::array<float,24> original_motion_transform(ccl::DeviceScene&,int object,float time);
