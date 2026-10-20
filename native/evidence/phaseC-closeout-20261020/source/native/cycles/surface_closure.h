// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "util/types.h"
namespace ccl {
// Canonical arena record, independent of native ShaderClosure pointer layout.
// Unused fields are zero. WGSL uses the compiler-generated size and offsets.
struct SurfaceClosureRecord {
  packed_float3 weight;float sample_weight;packed_float3 N;uint kind;
  float roughness,a,b;packed_float3 multiscatter;
  float alpha_x,alpha_y,ior,energy_scale;packed_float3 T;
  float fresnel_f0;packed_float3 reflection_tint,transmission_tint;
};
static_assert(sizeof(SurfaceClosureRecord)==112);
}
