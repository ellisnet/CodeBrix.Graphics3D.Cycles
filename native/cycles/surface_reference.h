// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include <vector>
namespace ccl {class DeviceScene;}
void original_surface(ccl::DeviceScene&,const std::vector<int>&,
    std::vector<ccl::IntegratorStateCPU>&,std::vector<float>&);
// Velvet subnormal rule (native/DEVICE.md, open item 29; the BSDF family gates only). The same oracle with the
// processor's flush-to-zero and denormals-are-zero modes set on the calling thread for the call (x86-64 MXCSR bits 15
// and 6), restored afterwards; the default oracle above is never changed. Returns false, and leaves the states, where
// the host has no such mode (then no state difference is judged by the rule).
bool original_surface_flush_to_zero(ccl::DeviceScene&,const std::vector<int>&,
    std::vector<ccl::IntegratorStateCPU>&,std::vector<float>&);
// The paths among indices whose evaluated surface closures (the pinned surface_shader_eval on the input state,
// closure filtering included) contain an Ashikhmin velvet closure.
std::vector<int> original_surface_velvet_paths(ccl::DeviceScene&,const std::vector<int>&,
    const std::vector<ccl::IntegratorStateCPU>&);
