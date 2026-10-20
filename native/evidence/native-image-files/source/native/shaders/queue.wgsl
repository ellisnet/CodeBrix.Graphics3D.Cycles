// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
@group(0) @binding(0) var<storage,read> camera_data: array<vec4<u32>>;
struct QueueConfig { first_path: u32, paths: u32, pixels: u32, samples: u32 }
@group(0) @binding(5) var<uniform> queue_config: QueueConfig;
@group(0) @binding(6) var<storage,read_write> film: array<vec4<f32>>;
struct QueueStatus { flags: atomic<u32>, batches: atomic<u32>, paths: atomic<u32>, reserved: u32 }
@group(0) @binding(7) var<storage,read_write> queue_status: QueueStatus;
fn path_count() -> u32 { return queue_config.paths; }
fn path_sample_index(i: u32) -> u32 { return queue_config.first_path+i; }
@compute @workgroup_size(64) fn accumulate(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if(i>=path_count()) { return; }
    if(i==0u) { atomicAdd(&queue_status.batches,1u); atomicAdd(&queue_status.paths,path_count()); }
    let base=i*OUTPUT_ROWS; let color=outputs[base];
    if(outputs[base+1u].y!=0.0) { atomicOr(&queue_status.flags,1u); }
    if(color.w!=1.0) { atomicOr(&queue_status.flags,2u); }
    if(any(color!=color)||any(abs(color)>vec4<f32>(3.4028234663852886e38))) { atomicOr(&queue_status.flags,4u); }
    for(var b=0u;b<MAX_BOUNCES;b++) {
        let shadow=outputs[base+MAX_BOUNCES+4u+2u*b];
        if(shadow.x>=0.0 && shadow.y!=0.0 && shadow.y!=1.0) { atomicOr(&queue_status.flags,8u); }
    }
    // Each batch stays within one sample, so every invocation owns one pixel.
    // Separate passes order successive samples without float atomics.
    let pixel=path_sample_index(i)%queue_config.pixels;
    film[pixel]+=color/f32(queue_config.samples);
}
