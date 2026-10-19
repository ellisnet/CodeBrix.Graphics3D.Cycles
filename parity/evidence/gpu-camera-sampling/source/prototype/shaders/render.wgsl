// BVH traversal adapted from Cycles kernel/bvh/traversal.h and nodes.h.
// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Original prototype camera/shading stages: fixed Lambert diffuse, point light,
// opaque shadows, flat geometric normals, constant environment. No full SVM.
struct Config {
    origin: vec4<f32>, forward: vec4<f32>, right: vec4<f32>, up: vec4<f32>,
    light: vec4<f32>, background: vec4<f32>, image: vec4<u32>,
}
@group(0) @binding(0) var<uniform> config: Config;
@group(0) @binding(1) var<storage, read_write> film: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> paths: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> intersections: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read> scene: array<vec4<u32>>;

fn scene_float(i: i32) -> vec4<f32> { return bitcast<vec4<f32>>(scene[u32(i)]); }
fn clamp_direction(d: vec3<f32>) -> vec3<f32> {
    let tiny = 8.271806E-25f;
    let signed_tiny = bitcast<vec3<f32>>((bitcast<vec3<u32>>(d) & vec3<u32>(0x80000000u)) | vec3<u32>(bitcast<u32>(tiny)));
    return select(signed_tiny, d, abs(d) > vec3<f32>(tiny));
}
fn node_intersect(p: vec3<f32>, inv: vec3<f32>, tmin: f32, tmax: f32, node: i32) -> vec3<f32> {
    let x = scene_float(node + 1); let y = scene_float(node + 2); let z = scene_float(node + 3);
    let l0 = (vec3<f32>(x.x,y.x,z.x) - p) * inv;
    let h0 = (vec3<f32>(x.z,y.z,z.z) - p) * inv;
    let lo0 = min(l0,h0); let hi0 = max(l0,h0);
    let near0 = max(max(tmin,lo0.x),max(lo0.y,lo0.z));
    let far0 = min(min(tmax,hi0.x),min(hi0.y,hi0.z));
    let l1 = (vec3<f32>(x.y,y.y,z.y) - p) * inv;
    let h1 = (vec3<f32>(x.w,y.w,z.w) - p) * inv;
    let lo1 = min(l1,h1); let hi1 = max(l1,h1);
    let near1 = max(max(tmin,lo1.x),max(lo1.y,lo1.z));
    let far1 = min(min(tmax,hi1.x),min(hi1.y,hi1.z));
    return vec3<f32>(f32(select(0u,1u,far0>=near0) | select(0u,2u,far1>=near1)),near0,near1);
}
fn trace(p: vec3<f32>, direction: vec3<f32>, tmin: f32, tmax: f32, skip_primitive: i32) -> vec4<f32> {
    let d=clamp_direction(direction); let inv=vec3<f32>(1.0)/d;
    let scene_info=scene_float(0);
    var result=vec4<f32>(tmax,0.0,0.0,-1.0);
    var stack: array<i32,192>; stack[0]=0x76543210i;
    var sp=0i; var node=1i;
    loop {
        while (node>=0 && node!=0x76543210i) {
            let bounds=node_intersect(p,inv,tmin,result.x,node);
            let children=bitcast<vec2<i32>>(scene[u32(node)].zw);
            node=children.x; var other=children.y;
            let mask=u32(bounds.x);
            if (mask==3u) {
                if (bounds.z<bounds.y) { let tmp=node; node=other; other=tmp; }
                sp+=1; stack[u32(sp)]=other;
            } else if (mask==2u) { node=other; }
            else if (mask==0u) { node=stack[u32(sp)]; sp-=1; }
        }
        if (node<0) {
            let leaf=bitcast<vec4<i32>>(scene[u32(i32(scene_info.y)-node-1)]);
            node=stack[u32(sp)]; sp-=1;
            for (var address=leaf.x; address<leaf.y; address+=1) {
                let prim=i32(scene_float(i32(scene_info.z)+address).x);
                if (prim==skip_primitive) { continue; }
                let offset=i32(scene_info.w)+3*prim;
                var u: f32; var v: f32; var t: f32;
                if (c_ray_triangle_intersect(p,d,tmin,result.x,scene_float(offset).xyz,
                    scene_float(offset+1).xyz,scene_float(offset+2).xyz,&u,&v,&t)) {
                    result=vec4<f32>(t,u,v,f32(prim));
                }
            }
        }
        if (node==0x76543210i) { break; }
    }
    return result;
}
fn pixel_valid(i: u32) -> bool { return i<config.image.x*config.image.y; }
fn sample_jitter(sample: u32) -> vec2<f32> {
    // Deterministic 8x8 pattern, reordered so the first four cover four quadrants.
    let x=((sample&1u)*4u + ((sample>>2u)&3u));
    let y=(((sample>>1u)&1u)*4u + ((sample>>4u)&3u));
    return (vec2<f32>(f32(x),f32(y))+vec2<f32>(0.5))/8.0;
}
@compute @workgroup_size(64) fn camera(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if (!pixel_valid(i)) { return; }
    let jitter=sample_jitter(config.image.z);
    let xy=vec2<f32>(f32(i%config.image.x),f32(i/config.image.x))+jitter;
    let uv=xy/vec2<f32>(f32(config.image.x),f32(config.image.y));
    let d=normalize(config.forward.xyz + config.right.xyz*((2.0*uv.x-1.0)*config.right.w*config.forward.w)
        + config.up.xyz*((1.0-2.0*uv.y)*config.forward.w));
    paths[2u*i]=config.origin;
    paths[2u*i+1u]=vec4<f32>(d,config.up.w);
}
@compute @workgroup_size(64) fn intersect(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if (!pixel_valid(i)) { return; }
    intersections[i]=trace(paths[2u*i].xyz,paths[2u*i+1u].xyz,paths[2u*i].w,paths[2u*i+1u].w,-1);
}
@compute @workgroup_size(64) fn shade(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if (!pixel_valid(i)) { return; }
    let hit=intersections[i];
    var color=config.background.xyz;
    if (hit.w>=0.0) {
        let prim=i32(hit.w); let scene_info=scene_float(0);
        let vbase=i32(scene_info.w)+3*prim;
        let a=scene_float(vbase).xyz; let b=scene_float(vbase+1).xyz; let c=scene_float(vbase+2).xyz;
        let direction=paths[2u*i+1u].xyz;
        var normal=normalize(cross(b-a,c-a));
        if (dot(normal,direction)>0.0) { normal=-normal; }
        let p=paths[2u*i].xyz+direction*hit.x;
        let delta=config.light.xyz-p;
        let distance_squared=max(dot(delta,delta),0.0001);
        let distance=sqrt(distance_squared); let light_direction=delta/distance;
        let albedo=scene_float(i32(scene_info.w+3.0*scene_info.x)+prim).xyz;
        color=vec3<f32>(0.0);
        let cosine=max(dot(normal,light_direction),0.0);
        if (cosine>0.0 && distance>0.0002) {
            let blocker=trace(p+normal*0.0001,light_direction,0.0001,distance-0.0001,prim);
            if (blocker.w<0.0) {
                color=albedo*(cosine*0.3183098861837907)*(config.light.w/distance_squared);
            }
        }
    }
    film[i]+=vec4<f32>(color,1.0)/f32(config.image.w);
}
