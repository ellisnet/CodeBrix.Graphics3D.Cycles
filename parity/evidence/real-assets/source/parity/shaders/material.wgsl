// Cycles SVM adaptation: SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Reads the original, unmodified native compiler bytecode. Unsupported nodes
// return an explicit error, never a default material.
@group(0) @binding(0) var<storage,read> inputs: array<vec4<f32>>;
@group(0) @binding(1) var<storage,read_write> outputs: array<vec4<f32>>;
@group(0) @binding(2) var<storage,read> code: array<u32>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if (i*5u>=arrayLength(&inputs)) { return; }
    let n=inputs[i*5u]; let ng=inputs[i*5u+1u].xyz; let wi=inputs[i*5u+2u].xyz;
    let c=decode(u32(n.w),n.xyz,wi);
    outputs[i*6u+5u]=vec4<f32>(f32(c.count),f32(c.error),0,0);
    if (c.count==0u || c.error!=0u) { return; }
    let sample=bsdf_sample(c,ng,wi,inputs[i*5u+3u].xy);
    let value=bsdf_eval(c,wi,inputs[i*5u+4u].xyz);
    outputs[i*6u]=vec4<f32>(c.weight,c.sample_weight);
    outputs[i*6u+1u]=vec4<f32>(c.normal,f32(c.kind));
    outputs[i*6u+2u]=vec4<f32>(sample.direction,sample.pdf);
    outputs[i*6u+3u]=vec4<f32>(sample.eval,f32(sample.label));
    outputs[i*6u+4u]=value;
}
