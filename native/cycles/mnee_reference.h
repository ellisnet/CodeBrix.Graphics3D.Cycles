// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include <array>
#include <vector>
namespace ccl {class DeviceScene;}
// The unmodified pinned integrator_intersect_mnee on host states (CPU oracle).
void original_mnee(ccl::DeviceScene&,const std::vector<int>&,std::vector<ccl::IntegratorStateCPU>&);
// Diagnostic only (the gate's pass/fail uses original_mnee): the pinned seed and
// Newton walk replayed step by step from the pinned helpers, recording the walk.
struct MneeTrace {int count=0,iterations=0,solved=0;float n_offset[6][2]{};float seed_p[6][3]{},final_p[6][3]{};float norms[64]{};};
MneeTrace original_mnee_trace(ccl::DeviceScene&,const ccl::IntegratorStateCPU&);
// Diagnostic: the pinned closest-hit scene_intersect for one ray (object, prim, t; object -1 = miss).
std::array<float,3> original_ray_hit(ccl::DeviceScene&,const float P[3],const float D[3],float tmin,float tmax,float time,int self_object,int self_prim,int light_object,int light_prim,unsigned visibility);
// Diagnostic: pinned integrator_intersect_shadow on one shadow state; returns the queued kernel after it.
int original_intersect_shadow_one(ccl::DeviceScene&,const ccl::IntegratorShadowStateCPU&);
