// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include <vector>
namespace ccl {class DeviceScene;
// Canonical comparison records of the hair state gate. Eval: bsdf_eval of one closure in one direction (value, pdf).
// Sample: the pick, bsdf_sample and the MIS of surface_shader_bsdf_sample_closure, in surf_sample_store's word order.
struct HairEvalRecord {float value[3],pdf;};
struct HairSampleRecord {float wo[3],pdf,eval[3];uint label;float diffuse[3],glossy[3],roughness;uint selected;};
static_assert(sizeof(HairEvalRecord)==16&&sizeof(HairSampleRecord)==64);
inline constexpr unsigned hair_probe_directions=24,hair_probe_samples=24,hair_probe_closures=3;
struct HairProbePath {
  float directions[hair_probe_directions][3];float rand[hair_probe_samples][3];unsigned closure_count;unsigned kinds[hair_probe_closures];
  HairEvalRecord evals[hair_probe_directions][hair_probe_closures];HairSampleRecord samples[hair_probe_samples];
};
}
// Evaluates the original CPU kernels on the given paths. The random table comes in `rand`; half of the directions are
// the sampled directions (where the lobes are), half a fixed scatter over the sphere.
void original_hair_probe(ccl::DeviceScene&,const std::vector<int>&,const std::vector<ccl::IntegratorStateCPU>&,std::vector<ccl::HairProbePath>&);
