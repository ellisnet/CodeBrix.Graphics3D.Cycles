// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include <vector>
namespace ccl {struct DeviceScene;}
void original_volume(ccl::DeviceScene &,const std::vector<int>&,std::vector<ccl::IntegratorStateCPU>&,std::vector<float>&);

void original_volume_stack(ccl::DeviceScene &,const std::vector<int>&,std::vector<ccl::IntegratorStateCPU>&);

void original_volume_density(ccl::DeviceScene &,const std::vector<ccl::KernelShaderEvalInput>&,std::vector<float>&);
