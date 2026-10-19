// Manual WebGPU adaptation of Cycles kernel/bvh/traversal.h and nodes.h.
// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Static triangles only. Deliberately excludes instances, motion, curves,
// visibility masks and self-hit rejection. Triangle math is AST-generated.
@group(0) @binding(0) var<storage, read> rays: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> hits: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> scene: array<vec4<u32>>;

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
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x; if (i >= arrayLength(&hits)) { return; }
    let rp = rays[2u*i]; let rd = rays[2u*i+1u];
    let p = rp.xyz; let d = clamp_direction(rd.xyz); let inv = vec3<f32>(1.0)/d;
    let scene_info = scene_float(0);
    var result = vec4<f32>(rd.w,0.0,0.0,-1.0);
    var stack: array<i32,192>; stack[0] = 0x76543210i;
    var sp = 0i; var node = 1i;
    loop {
        while (node >= 0 && node != 0x76543210i) {
            let bounds = node_intersect(p,inv,rp.w,result.x,node);
            let children = bitcast<vec2<i32>>(scene[u32(node)].zw);
            node = children.x; var other = children.y;
            let mask = u32(bounds.x);
            if (mask == 3u) {
                if (bounds.z < bounds.y) { let tmp = node; node = other; other = tmp; }
                sp += 1;
                if (sp >= 192) { hits[i] = vec4<f32>(-2.0); return; }
                stack[u32(sp)] = other;
            } else if (mask == 2u) { node = other; }
            else if (mask == 0u) { node = stack[u32(sp)]; sp -= 1; }
        }
        if (node < 0) {
            let leaf = bitcast<vec4<i32>>(scene[u32(i32(scene_info.y) - node - 1)]);
            if (leaf.x < 0) { hits[i] = vec4<f32>(-3.0); return; }
            node = stack[u32(sp)]; sp -= 1;
            for (var prim_addr = leaf.x; prim_addr < leaf.y; prim_addr += 1) {
                let prim = i32(scene_float(i32(scene_info.z)+prim_addr).x);
                let offset = i32(scene_info.w) + 3*prim;
                var u: f32; var v: f32; var t: f32;
                if (c_ray_triangle_intersect(p,d,rp.w,result.x,scene_float(offset).xyz,
                    scene_float(offset+1).xyz,scene_float(offset+2).xyz,&u,&v,&t)) {
                    result = vec4<f32>(t,u,v,f32(prim));
                }
            }
        }
        if (node == 0x76543210i) { break; }
    }
    hits[i] = result;
}
