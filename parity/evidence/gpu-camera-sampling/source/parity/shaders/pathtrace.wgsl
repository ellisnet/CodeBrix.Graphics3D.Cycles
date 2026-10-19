// Ray origin and path updates adapted from Cycles geom/triangle_intersect.h,
// util/math_intersect.h, kernel/bvh/util.h and integrator/shade_surface.h.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Limited forward path tracer: single implemented SVM closure per surface,
// constant world, fixed bounce limit, no NEE, MIS, RR, textures or volumes.
@group(0) @binding(1) var<storage,read_write> outputs: array<vec4<f32>>;
@group(0) @binding(2) var<storage,read> scene: array<vec4<u32>>;
fn metadata_row() -> u32 { let h=scene_float(0); return u32(h.w+4.0*h.x); }
fn svm_word_count() -> u32 { return scene[metadata_row()].x; }
fn svm_word(index: u32) -> u32 {
    let address=scene[metadata_row()].y+index;
    return scene[address/4u][address%4u];
}
fn self_intersection(p: vec3<f32>, d: vec3<f32>, a: vec3<f32>, b: vec3<f32>, c: vec3<f32>) -> bool {
    let v0=a-p; let v1=b-p; let v2=c-p;
    let e0=v2-v0; let e1=v0-v1; let e2=v1-v2;
    let u=c_ray_triangle_dot(c_ray_triangle_cross(v2+v0,e0),d);
    let v=c_ray_triangle_dot(c_ray_triangle_cross(v0+v1,e1),d);
    let w=c_ray_triangle_dot(c_ray_triangle_cross(v1+v2,e2),d);
    let epsilon=1.1920928955078125e-7*abs(u+v+w);
    return min(u,min(v,w))>=epsilon || max(u,max(v,w))<= -epsilon;
}
fn offset_ray(p: vec3<f32>, n: vec3<f32>) -> vec3<f32> {
    // Cycles attributes this method to "A Fast and Robust Method for Avoiding
    // Self-Intersection", Ray Tracing Gems, chapter 6.
    let offset_integer=vec3<i32>(256.0*n);
    let shifted=bitcast<vec3<f32>>(bitcast<vec3<i32>>(p)+select(offset_integer,-offset_integer,p<vec3<f32>(0)));
    return select(shifted,p+(1.0/65536.0)*n,abs(p)<vec3<f32>(1.0/32.0));
}
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; let rows=MAX_BOUNCES+3u;
    if (i>=path_count()) { return; }
    let output_base=i*rows; let initial=path_initial(i);
    var p=initial.origin.xyz; var tmin=initial.origin.w;
    var d=initial.direction.xyz; var tmax=initial.direction.w;
    var throughput=initial.throughput; var color=vec3<f32>(0);
    var skip= -1; var bounces=0u; var error=0u;
    for(var b=0u;b<=MAX_BOUNCES;b++) { outputs[output_base+2u+b]=vec4<f32>(0,0,0,-2); }
    for(var b=0u;b<=MAX_BOUNCES;b++) {
        let hit=trace(p,d,tmin,tmax,skip);
        outputs[output_base+2u+b]=hit;
        if (hit.w<0.0) { color=throughput*scene_float(i32(metadata_row()+1u)).xyz; break; }
        if (b==MAX_BOUNCES) { break; }
        let h=scene_float(0); let base=i32(h.w)+3*i32(hit.w);
        let a=scene_float(base).xyz; let bv=scene_float(base+1).xyz; let c=scene_float(base+2).xyz;
        var n=normalize(cross(bv-a,c-a)); if (dot(n,-d)<0.0) { n=-n; }
        let shader=u32(scene_float(i32(h.w+3.0*h.x)+i32(hit.w)).w);
        let closure=decode(shader,n,-d);
        if (closure.error!=0u) { error=closure.error; break; }
        if (closure.count==0u) { break; }
        let sampled=bsdf_sample(closure,n,-d,path_random(i,b).xy);
        if (sampled.pdf==0.0 || all(sampled.eval==vec3<f32>(0))) { break; }
        throughput*=sampled.eval*closure.weight/sampled.pdf;
        p=a+hit.y*(bv-a)+hit.z*(c-a); d=normalize(sampled.direction);
        if (!self_intersection(p,d,a,bv,c)) { p=offset_ray(p,n); }
        tmin=0; tmax=3.4028234663852886e38; skip=i32(hit.w); bounces++;
    }
    outputs[output_base]=vec4<f32>(color,1);
    outputs[output_base+1u]=vec4<f32>(f32(bounces),f32(error),0,0);
}
