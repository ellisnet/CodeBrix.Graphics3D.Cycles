// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include <vector>
namespace ccl {class DeviceScene;}
// The unmodified CPU integrator_shade_surface_raytrace (KERNEL_FEATURE_NODE_MASK_SURFACE:
// the Ambient Occlusion node's ray tracing) on the given main path states.
void original_surface_raytrace(ccl::DeviceScene&,const std::vector<int>&,
    std::vector<ccl::IntegratorStateCPU>&,std::vector<float>&);
