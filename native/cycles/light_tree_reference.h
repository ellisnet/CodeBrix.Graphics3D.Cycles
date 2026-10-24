// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include <vector>
namespace ccl {class DeviceScene;}
// Light-tree / light-linking probe input per path state (the state's own ray P,
// ray D as the normal, sample and rng carry the sampling input).
struct LightTreeProbeInput {int receiver,forward_receiver,target_object,target_emitter;};
// Per state, light_tree_probe_record_floats values: selection valid, emitter id
// (tree) or distribution index (flat CDF), selection pdf, sample valid, P, D, t,
// pdf, eval_fac, prim, object; then the forward tree pdf of the target emitter.
inline constexpr unsigned light_tree_probe_record_floats=16;
// Diagnostic: (maximum, minimum) importance of every node then every emitter, per state.
std::vector<float> original_light_tree_importance(ccl::DeviceScene&,const std::vector<ccl::IntegratorStateCPU>&);
std::vector<float> original_light_tree_samples(ccl::DeviceScene&,const std::vector<ccl::IntegratorStateCPU>&,const std::vector<LightTreeProbeInput>&);
