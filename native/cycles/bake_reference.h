// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include <vector>
namespace ccl {class DeviceScene;}
// The unmodified pinned CPU integrator_init_from_bake for one work item: `pair` holds the path and the state the CPU
// kernel's shadow catcher split writes (state + 1). Returns the kernel's result.
bool original_bake(ccl::DeviceScene&,const ccl::KernelWorkTile&,unsigned x,unsigned y,unsigned sample,
                   ccl::IntegratorStateCPU pair[2],std::vector<float>&);
