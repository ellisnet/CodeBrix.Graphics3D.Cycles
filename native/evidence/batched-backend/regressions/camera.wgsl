@group(0) @binding(0) var<storage,read> camera_data: array<vec4<u32>>;
fn path_count() -> u32 { let c=camera_data[0]; return c.x*c.y*c.z; }
fn path_sample_index(i: u32) -> u32 { return i; }
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
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Adapted from Cycles camera/camera.h, integrator/init_from_camera.h,
// integrator/path_state.h, util/lookup_table.h, util/transform.h and projection.h.
// Static perspective pinhole camera only; host rejects other configurations.
// Differentials are not consumed by the current texture-free integration gate.
struct InitialPath { origin: vec4<f32>, direction: vec4<f32>, throughput: vec3<f32> }
// The caller supplies path_count() and path_sample_index() for full-frame or
// bounded native queue batches; camera mathematics is shared by both.
fn pixel_sample(index: u32) -> vec4<u32> {
    let i=path_sample_index(index);
    let c=camera_data[0]; let x=i%c.x; let y=(i/c.x)%c.y; let s=i/(c.x*c.y);
    return vec4<u32>(x,y,s,pixel_hash(x,y,camera_data[1].x));
}
fn path_random(i: u32, bounce: u32) -> vec3<f32> {
    let p=pixel_sample(i); let c=camera_data[3];
    return sobol_sample(p.z,p.w,(bounce+1u)*c.y+c.z,camera_data[1].x,camera_float(2).x).xyz;
}
fn filter_lookup(value: f32) -> f32 {
    let count=camera_data[3].w; let base=camera_data[4].y;
    let x=clamp(value,0.0,1.0)*f32(count-1u); let index=min(u32(x),count-1u);
    let t=x-f32(index); let a=camera_scalar(base+index);
    if(t==0.0) { return a; }
    let b=camera_scalar(base+min(index+1u,count-1u)); return (1.0-t)*a+t*b;
}
fn path_initial(i: u32) -> InitialPath {
    let p=pixel_sample(i); var uv=vec2<f32>(0.5);
    if(p.z!=0u) { uv=sobol_sample(p.z,p.w,camera_data[3].x,camera_data[1].x,camera_float(2).x).xy; }
    let raster=vec4<f32>(vec2<f32>(p.xy)+vec2<f32>(filter_lookup(uv.x),filter_lookup(uv.y)),0,1);
    var camera_point=vec3<f32>(dot(camera_float(5),raster),dot(camera_float(6),raster),dot(camera_float(7),raster));
    let w=dot(camera_float(8),raster);
    if(w!=0.0) { camera_point/=w; } else { camera_point=vec3<f32>(0); }
    let tx=camera_float(9); let ty=camera_float(10); let tz=camera_float(11);
    // Cycles CPU transform_direction accumulates z, then y, then x.
    let direction=normalize(camera_point.x*vec3<f32>(tx.x,ty.x,tz.x)+
                          (camera_point.y*vec3<f32>(tx.y,ty.y,tz.y)+camera_point.z*vec3<f32>(tx.z,ty.z,tz.z)));
    let z_inverse=1.0/normalize(camera_point).z; let clip=camera_float(2);
    let origin=vec3<f32>(tx.w,ty.w,tz.w)+(clip.y*z_inverse)*direction;
    return InitialPath(vec4<f32>(origin,0),vec4<f32>(direction,clip.z*z_inverse),vec3<f32>(1));
}

@group(0) @binding(1) var<storage,read_write> outputs: array<vec4<f32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if(i>=path_count()) { return; }
    let bounces=camera_data[0].w; let base=i*(3u+bounces); let initial=path_initial(i);
    outputs[base]=initial.origin; outputs[base+1u]=initial.direction;
    outputs[base+2u]=vec4<f32>(initial.throughput,0);
    for(var b=0u;b<bounces;b++) { outputs[base+3u+b]=vec4<f32>(path_random(i,b),0); }
}
