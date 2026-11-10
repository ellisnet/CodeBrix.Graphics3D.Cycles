// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include <vector>
namespace ccl {class DeviceScene;}
void original_subsurface(ccl::DeviceScene &,const std::vector<int>&,std::vector<ccl::IntegratorStateCPU>&);
struct DiskHits {int count=0;std::vector<int> prim;std::vector<float> t;};
std::vector<DiskHits> original_disk_hits(ccl::DeviceScene &,const std::vector<int>&,const std::vector<ccl::IntegratorStateCPU>&);
