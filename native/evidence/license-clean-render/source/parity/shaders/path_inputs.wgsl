// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Baseline adapter for the frozen gate with shared native camera/sampler inputs.
@group(0) @binding(0) var<storage,read> inputs: array<vec4<f32>>;
struct InitialPath { origin: vec4<f32>, direction: vec4<f32>, throughput: vec3<f32> }
fn path_count() -> u32 { return arrayLength(&inputs)/(MAX_BOUNCES+3u); }
fn path_initial(i: u32) -> InitialPath {
    let base=i*(MAX_BOUNCES+3u);
    return InitialPath(inputs[base],inputs[base+1u],inputs[base+2u].xyz);
}
fn path_random(i: u32, bounce: u32) -> vec3<f32> {
    return inputs[i*(MAX_BOUNCES+3u)+3u+bounce].xyz;
}
