// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include <vector>
namespace ccl {class DeviceScene;}
void original_light_forward(ccl::DeviceScene&,const std::vector<int>&,
                         std::vector<ccl::IntegratorStateCPU>&,std::vector<float>&);

std::vector<float> original_light_samples(ccl::DeviceScene&,const std::vector<ccl::IntegratorStateCPU>&);
