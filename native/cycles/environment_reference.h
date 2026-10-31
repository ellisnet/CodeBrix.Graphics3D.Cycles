// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include "kernel/types.h"
#include <vector>
namespace ccl {class DeviceScene;}
// integrator_shade_light_nee on shadow states.
void original_light_nee(ccl::DeviceScene&,const std::vector<int>&,std::vector<ccl::IntegratorShadowStateCPU>&,std::vector<float>&);
// kernel_background_evaluate (importance-map pixels, RGB per input).
std::vector<float> original_background_evaluate(ccl::DeviceScene&,const std::vector<ccl::KernelShaderEvalInput>&);
// The unmodified kernel_displace_evaluate (bake/bake.h) on the device-prepared tables: three floats per input.
std::vector<float> original_displace_evaluate(ccl::DeviceScene&,const std::vector<ccl::KernelShaderEvalInput>&);
// Per state: valid flag and the light shader evaluation of the sampled light
// (background lights: world shader with zero ray differential times strength).
std::vector<float> original_light_sample_evals(ccl::DeviceScene&,const std::vector<ccl::IntegratorStateCPU>&);
