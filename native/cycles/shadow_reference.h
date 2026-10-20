// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include <vector>
namespace ccl {class DeviceScene;}
void original_shadow(ccl::DeviceScene&,ccl::DeviceKernel,const std::vector<int>&,
                     std::vector<ccl::IntegratorShadowStateCPU>&,std::vector<float>&);
