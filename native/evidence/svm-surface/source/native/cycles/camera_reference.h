// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include <vector>
namespace ccl {class DeviceScene;}
void original_camera(ccl::DeviceScene&,const std::vector<ccl::KernelWorkTile>&,unsigned,
                     std::vector<ccl::IntegratorStateCPU>&,std::vector<float>&);
void original_camera_retry(ccl::DeviceScene&,const std::vector<int>&,
                           std::vector<ccl::IntegratorStateCPU>&,std::vector<float>&);
