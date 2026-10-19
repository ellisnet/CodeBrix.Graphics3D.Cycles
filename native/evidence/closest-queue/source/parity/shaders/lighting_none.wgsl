// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Preserve the forward-only baseline while sharing path integration code.
fn lighting_begin(base: u32) {}
fn lighting_forward(p: vec3<f32>, d: vec3<f32>, tmin: f32, tmax: f32,
    previous_pdf: f32, previous_n: vec3<f32>, transmission: bool, bounce: u32,
    count: u32, throughput: vec3<f32>, base: u32) -> ForwardLighting {
    return ForwardLighting(vec3<f32>(0),0u,false);
}
fn lighting_direct(i: u32, bounce: u32, base: u32, p: vec3<f32>, n: vec3<f32>, wi: vec3<f32>,
    closure: Closure, throughput: vec3<f32>, a: vec3<f32>, b: vec3<f32>, c: vec3<f32>, prim: i32) -> vec3<f32> {
    return vec3<f32>(0);
}
