// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include "surface_closure.h"
#include <vector>
namespace ccl {class DeviceScene;
// Canonical comparison records, not a native ShaderData or ShaderClosure ABI.
// Unused closure fields are zero, including undefined holdout N/sample_weight.
struct SurfaceSvmRecord {
  uint count,remaining,runtime_flag,lcg_state;
  packed_float3 N,emission,transparency;
  SurfaceClosureRecord closures[MAX_CLOSURE];
};
}
std::vector<ccl::SurfaceSvmRecord> original_surface_svm(ccl::DeviceScene&,
    const std::vector<int>&,const std::vector<ccl::IntegratorStateCPU>&);
