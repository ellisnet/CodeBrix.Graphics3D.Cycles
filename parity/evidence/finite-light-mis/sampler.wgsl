
@group(0) @binding(0) var<storage,read> queries: array<vec4<u32>>;
@group(0) @binding(1) var<storage,read_write> outputs: array<vec4<u32>>;
@group(0) @binding(2) var<storage,read> camera_data: array<vec4<u32>>;
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Adapted from Cycles util/hash.h, kernel/sample/util.h and tabulated_sobol.h.
// camera_data contains unchanged scene LUTs and explicit scalar configuration.
fn camera_float(row: u32) -> vec4<f32> { return bitcast<vec4<f32>>(camera_data[row]); }
fn camera_scalar(address: u32) -> f32 { return bitcast<f32>(camera_data[address/4u][address%4u]); }
fn wang_hash(value: u32, seed: u32) -> u32 {
    var i=(value^61u)^seed; i+=i<<3u; i^=i>>4u; i*=0x27d4eb2du; return i;
}
fn pixel_hash(x: u32, y: u32, seed: u32) -> u32 {
    let qx=1103515245u*((x>>1u)^y); let qy=1103515245u*((y>>1u)^x);
    return (1103515245u*(qx^(qy>>3u)))^seed;
}
fn owen_scramble(value: u32, seed: u32) -> u32 {
    var n=reverseBits(value); n^=n*0x3d20adeau; n+=seed;
    n*=(seed>>16u)|1u; n^=n*0x05526c56u; n^=n*0x53a22864u; return reverseBits(n);
}
fn shuffle_pattern(value: u32, length: u32, seed: u32) -> u32 {
    var i=value%length;
    let mask=(1u<<(32u-countLeadingZeros(length-1u)))-1u;
    loop {
        i^=seed; i*=0xe170893du; i^=seed>>16u; i^=(i&mask)>>4u;
        i^=seed>>8u; i*=0x0929eb3fu; i^=seed>>23u; i^=(i&mask)>>1u;
        i*=1u|(seed>>27u); i*=0x6935fa69u; i^=(i&mask)>>11u;
        i*=0x74dcb303u; i^=(i&mask)>>2u; i*=0x9e501cc3u;
        i^=(i&mask)>>2u; i*=0xc860a3dfu; i&=mask; i^=i>>5u;
        if(i<length) { break; }
    }
    return i;
}
fn sobol_index(sample_id: u32, dimension: u32, seed: u32) -> u32 {
    let count=camera_data[1].y; let patterns=camera_data[1].z;
    let pattern=shuffle_pattern(dimension,patterns,seed); let mask=count-1u;
    let shuffled=owen_scramble(sample_id,wang_hash(dimension,seed));
    let sample_value=(sample_id&~mask)|(shuffled&mask);
    return (pattern*count+sample_value)%(count*patterns);
}
fn sobol_sample(sample_id: u32, pixel: u32, dimension: u32, seed: u32, distance: f32) -> vec4<f32> {
    let index=sobol_index(sample_id,dimension,select(pixel,seed,distance<1.0));
    let address=camera_data[4].x+index*4u;
    var value=bitcast<vec4<f32>>(camera_data[address/4u]);
    if(distance<1.0) {
        let hashes=vec4<u32>(wang_hash(dimension,pixel),wang_hash(dimension,pixel^0xca0e1151u),
                            wang_hash(dimension,pixel^0xbf604c5au),wang_hash(dimension,pixel^0x99634d1du));
        let jitter=(vec4<f32>(hashes)*(1.0f/4294967808.0f))*distance;
        // Floating-point contraction can change the final low bits here;
        // the strict sampler gate retains these differences for diagnosis.
        value+=jitter; value-=floor(value);
    }
    return value;
}

@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x*2u; if(i>=arrayLength(&queries)) { return; }
    let q=queries[i]; let seed=queries[i+1u].x; let distance=bitcast<f32>(queries[i+1u].y);
    let pixel=pixel_hash(q.x,q.y,seed);
    outputs[i]=vec4<u32>(pixel,sobol_index(q.z,q.w,select(pixel,seed,distance<1.0)),0,0);
    outputs[i+1u]=bitcast<vec4<u32>>(sobol_sample(q.z,pixel,q.w,seed,distance));
}
