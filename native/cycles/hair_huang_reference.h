// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Records of the Huang hair state gate (hair_huang_gate.cpp): closure setup, bsdf_eval and bsdf_sample of the
// unmodified CPU kernels, in the order the kernel's integrator takes them, so sd->lcg_state's position is compared too.
#include "kernel/integrator/state.h"
#include "scene/scene.h"
#include <vector>
namespace ccl {
inline constexpr unsigned huang_gate_closures=2,huang_gate_directions=4;
struct HuangGateClosure {
  packed_float3 weight;float sample_weight;packed_float3 N;uint kind;
  float roughness,tilt,eta,aspect_ratio,h;packed_float3 sigma;packed_float3 Y,Z,wi;
  float radius,e2,pixel_coverage,R,TT,TRT;
};
struct HuangGateEval {packed_float3 value;float pdf;uint lcg;};
struct HuangGateSample {packed_float3 wo;float pdf;packed_float3 eval;uint label;uint lcg;};
struct HuangGateRecord {
  uint count,remaining,runtime_flag,lcg;packed_float3 N;float transparency;
  HuangGateClosure closures[huang_gate_closures];
  HuangGateEval eval[huang_gate_directions][huang_gate_closures];
  HuangGateSample sample[huang_gate_closures];
};
struct HuangGateInput {packed_float3 wo[huang_gate_directions];packed_float3 random[huang_gate_closures];};
std::vector<HuangGateRecord> original_hair_huang(DeviceScene &scene,const std::vector<int> &indices,const std::vector<IntegratorStateCPU> &states,const std::vector<HuangGateInput> &inputs);
}
