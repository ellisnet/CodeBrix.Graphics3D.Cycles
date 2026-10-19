const SHARED_RAYS: bool=false;
// Generated from Blender Cycles; see provenance.json and THIRD-PARTY-NOTICES.txt.
// Original code: SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-License-Identifier: Apache-2.0
// wrapf retains upstream's attribution: Adapted from godot-engine math_funcs.h.
// C library adapters below: Copyright (c) 2026 Jeremy Ellis and contributors
fn c_fmod(a: f32, b: f32) -> f32 { return a - trunc(a / b) * b; }
fn c_pow(a: f32, b: f32) -> f32 {
    // C pow accepts negative bases with integer exponents; WGSL pow does not.
    if (a < 0.0) {
        let p = pow(-a, b);
        if (c_fmod(abs(b), 2.0) == 0.0) { return p; }
        return -p;
    }
    return pow(a, b);
}


fn c_ray_triangle_reciprocal(in_x: f32) -> f32 {
var v_x = in_x;
return (1.0f / v_x);
}

fn c_ray_triangle_dot(in_a: vec3<f32>, in_b: vec3<f32>) -> f32 {
var v_a = in_a;
var v_b = in_b;
return (((v_a.x * v_b.x) + (v_a.y * v_b.y)) + (v_a.z * v_b.z));
}

fn c_ray_triangle_cross(in_a: vec3<f32>, in_b: vec3<f32>) -> vec3<f32> {
var v_a = in_a;
var v_b = in_b;
return vec3<f32>(((v_a.y * v_b.z) - (v_a.z * v_b.y)), ((v_a.z * v_b.x) - (v_a.x * v_b.z)), ((v_a.x * v_b.y) - (v_a.y * v_b.x)));
}

fn c_ray_triangle_intersect(in_ray_P: vec3<f32>, in_ray_D: vec3<f32>, in_ray_tmin: f32, in_ray_tmax: f32, in_tri_a: vec3<f32>, in_tri_b: vec3<f32>, in_tri_c: vec3<f32>, v_isect_u: ptr<function, f32>, v_isect_v: ptr<function, f32>, v_isect_t: ptr<function, f32>) -> bool {
var v_ray_P = in_ray_P;
var v_ray_D = in_ray_D;
var v_ray_tmin = in_ray_tmin;
var v_ray_tmax = in_ray_tmax;
var v_tri_a = in_tri_a;
var v_tri_b = in_tri_b;
var v_tri_c = in_tri_c;
var v_v0: vec3<f32> = (v_tri_a - v_ray_P);
var v_v1: vec3<f32> = (v_tri_b - v_ray_P);
var v_v2: vec3<f32> = (v_tri_c - v_ray_P);
var v_e0: vec3<f32> = (v_v2 - v_v0);
var v_e1: vec3<f32> = (v_v0 - v_v1);
var v_e2: vec3<f32> = (v_v1 - v_v2);
var v_U: f32 = c_ray_triangle_dot(c_ray_triangle_cross(v_e0, (v_v2 + v_v0)), v_ray_D);
var v_V: f32 = c_ray_triangle_dot(c_ray_triangle_cross(v_e1, (v_v0 + v_v1)), v_ray_D);
var v_W: f32 = c_ray_triangle_dot(c_ray_triangle_cross(v_e2, (v_v1 + v_v2)), v_ray_D);
var v_UVW: f32 = ((v_U + v_V) + v_W);
var v_eps: f32 = (1.1920929E-7f * abs(v_UVW));
var v_minUVW: f32 = min(v_U, min(v_V, v_W));
var v_maxUVW: f32 = max(v_U, max(v_V, v_W));
if ((!(((v_minUVW >= (-v_eps)) || (v_maxUVW <= v_eps))))) {
return false;
}
var v_Ng1: vec3<f32> = c_ray_triangle_cross(v_e1, v_e0);
var v_Ng: vec3<f32> = (v_Ng1 + v_Ng1);
var v_den: f32 = dot(v_Ng, v_ray_D);
if (((v_den == 0.0f))) {
return false;
}
var v_T: f32 = dot(v_v0, v_Ng);
var v_t: f32 = (v_T / v_den);
if ((!(((v_t >= v_ray_tmin) && (v_t <= v_ray_tmax))))) {
return false;
}
var choice_1: f32;
if (((abs(v_UVW) < 1.00000005E-18f))) {
choice_1 = 0.0f;
} else {
choice_1 = c_ray_triangle_reciprocal(v_UVW);
}
var v_rcp_uvw: f32 = choice_1;
(*v_isect_u) = min((v_U * v_rcp_uvw), 1.0f);
(*v_isect_v) = min((v_V * v_rcp_uvw), 1.0f);
(*v_isect_t) = v_t;
return true;
}

fn c_fractf(in_x: f32) -> f32 {
var v_x = in_x;
return (v_x - floor(v_x));
}

fn c_wrapf(in_value: f32, in_max: f32, in_min: f32) -> f32 {
var v_value = in_value;
var v_max = in_max;
var v_min = in_min;
var v_range: f32 = (v_max - v_min);
var choice_2: f32;
if (((v_range != 0.0f))) {
choice_2 = (v_value - ((v_range * floor((((v_value - v_min)) / v_range)))));
} else {
choice_2 = v_min;
}
return choice_2;
}

fn c_pingpongf(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var choice_3: f32;
if (((v_b != 0.0f))) {
choice_3 = abs((((c_fractf((((v_a - v_b)) / ((v_b * 2.0f)))) * v_b) * 2.0f) - v_b));
} else {
choice_3 = 0.0f;
}
return choice_3;
}

fn c_smoothminf(in_a: f32, in_b: f32, in_k: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var v_k = in_k;
if ((v_k != 0.0f)) {
var v_h: f32 = (max((v_k - abs((v_a - v_b))), 0.0f) / v_k);
return (min(v_a, v_b) - ((((v_h * v_h) * v_h) * v_k) * ((1.0f / 6.0f))));
}
return min(v_a, v_b);
}

fn c_signf(in_f: f32) -> f32 {
var v_f = in_f;
var choice_4: f32;
if (((v_f < 0.0f))) {
choice_4 = (-1.0f);
} else {
choice_4 = 1.0f;
}
return choice_4;
}

fn c_compatible_atan2(in_y: f32, in_x: f32) -> f32 {
var v_y = in_y;
var v_x = in_x;
var choice_5: f32;
if ((((v_x == 0.0f) && (v_y == 0.0f)))) {
choice_5 = 0.0f;
} else {
choice_5 = atan2(v_y, v_x);
}
return choice_5;
}

fn c_compatible_signf(in_f: f32) -> f32 {
var v_f = in_f;
if ((v_f == 0.0f)) {
return 0.0f;
}
return c_signf(v_f);
}

fn c_safe_sqrtf(in_f: f32) -> f32 {
var v_f = in_f;
return sqrt(max(v_f, 0.0f));
}

fn c_inversesqrtf(in_f: f32) -> f32 {
var v_f = in_f;
var choice_6: f32;
if (((v_f > 0.0f))) {
choice_6 = (1.0f / sqrt(v_f));
} else {
choice_6 = 0.0f;
}
return choice_6;
}

fn c_safe_asinf(in_a: f32) -> f32 {
var v_a = in_a;
return asin(clamp(v_a, (-1.0f), 1.0f));
}

fn c_safe_acosf(in_a: f32) -> f32 {
var v_a = in_a;
return acos(clamp(v_a, (-1.0f), 1.0f));
}

fn c_compatible_powf(in_x: f32, in_y: f32) -> f32 {
var v_x = in_x;
var v_y = in_y;
if ((v_y == 0.0f)) {
return 1.0f;
}
if ((v_x == 0.0f)) {
return 0.0f;
}
return c_pow(v_x, v_y);
}

fn c_safe_powf(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
if ((((v_a < 0.0f) && (v_b != f32(i32(v_b)))))) {
return 0.0f;
}
return c_compatible_powf(v_a, v_b);
}

fn c_safe_divide(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var choice_7: f32;
if (((v_b != 0.0f))) {
choice_7 = (v_a / v_b);
} else {
choice_7 = 0.0f;
}
return choice_7;
}

fn c_safe_logf(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
if ((((v_a <= 0.0f) || (v_b <= 0.0f)))) {
return 0.0f;
}
return c_safe_divide(log(v_a), log(v_b));
}

fn c_safe_modulo(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var choice_8: f32;
if (((v_b != 0.0f))) {
choice_8 = c_fmod(v_a, v_b);
} else {
choice_8 = 0.0f;
}
return choice_8;
}

fn c_safe_floored_modulo(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var choice_9: f32;
if (((v_b != 0.0f))) {
choice_9 = (v_a - (floor((v_a / v_b)) * v_b));
} else {
choice_9 = 0.0f;
}
return choice_9;
}

fn c_svm_math(in_type: u32, in_a: f32, in_b: f32, in_c: f32) -> f32 {
var v_type = in_type;
var v_a = in_a;
var v_b = in_b;
var v_c = in_c;
switch (v_type) {
case 0u: {
return (v_a + v_b);
}
case 1u: {
return (v_a - v_b);
}
case 2u: {
return (v_a * v_b);
}
case 3u: {
return c_safe_divide(v_a, v_b);
}
case 10u: {
return c_safe_powf(v_a, v_b);
}
case 11u: {
return c_safe_logf(v_a, v_b);
}
case 23u: {
return c_safe_sqrtf(v_a);
}
case 24u: {
return c_inversesqrtf(v_a);
}
case 18u: {
return abs(v_a);
}
case 27u: {
return (v_a * (((3.14159274f) / 180.0f)));
}
case 28u: {
return (v_a * ((180.0f / (3.14159274f))));
}
case 12u: {
return min(v_a, v_b);
}
case 13u: {
return max(v_a, v_b);
}
case 15u: {
return select(f32(0), f32(1), (v_a < v_b));
}
case 16u: {
return select(f32(0), f32(1), (v_a > v_b));
}
case 14u: {
return floor((v_a + 0.5f));
}
case 20u: {
return floor(v_a);
}
case 21u: {
return ceil(v_a);
}
case 22u: {
return (v_a - floor(v_a));
}
case 17u: {
return c_safe_modulo(v_a, v_b);
}
case 40u: {
return c_safe_floored_modulo(v_a, v_b);
}
case 32u: {
var choice_10: f32;
if ((v_a >= 0.0f)) {
choice_10 = floor(v_a);
} else {
choice_10 = ceil(v_a);
}
return choice_10;
}
case 33u: {
return (floor(c_safe_divide(v_a, v_b)) * v_b);
}
case 34u: {
return c_wrapf(v_a, v_b, v_c);
}
case 37u: {
return c_pingpongf(v_a, v_b);
}
case 4u: {
return sin(v_a);
}
case 5u: {
return cos(v_a);
}
case 6u: {
return tan(v_a);
}
case 29u: {
return sinh(v_a);
}
case 30u: {
return cosh(v_a);
}
case 31u: {
return tanh(v_a);
}
case 7u: {
return c_safe_asinf(v_a);
}
case 8u: {
return c_safe_acosf(v_a);
}
case 9u: {
return atan(v_a);
}
case 19u: {
return c_compatible_atan2(v_a, v_b);
}
case 25u: {
return c_compatible_signf(v_a);
}
case 26u: {
return exp(v_a);
}
case 35u: {
var choice_11: f32;
if (((((v_a == v_b)) || ((abs((v_a - v_b)) <= max(v_c, 1.1920929E-7f)))))) {
choice_11 = 1.0f;
} else {
choice_11 = 0.0f;
}
return choice_11;
}
case 36u: {
return ((v_a * v_b) + v_c);
}
case 38u: {
return c_smoothminf(v_a, v_b, v_c);
}
case 39u: {
return (-c_smoothminf((-v_a), (-v_b), v_c));
}
default: {
return 0.0f;
}
}
}

fn c_svm_math_gamma_color(in_color: vec3<f32>, in_gamma: f32) -> vec3<f32> {
var v_color = in_color;
var v_gamma = in_gamma;
if ((v_gamma == 0.0f)) {
return vec3<f32>(1.0f, 1.0f, 1.0f);
}
if ((v_color.x > 0.0f)) {
v_color.x = c_pow(v_color.x, v_gamma);
}
if ((v_color.y > 0.0f)) {
v_color.y = c_pow(v_color.y, v_gamma);
}
if ((v_color.z > 0.0f)) {
v_color.z = c_pow(v_color.z, v_gamma);
}
return v_color;
}

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

fn sample_jitter(sample: u32) -> vec2<f32> {
    // Deterministic 8x8 pattern, reordered so the first four cover four quadrants.
    let x=((sample&1u)*4u + ((sample>>2u)&3u));
    let y=(((sample>>1u)&1u)*4u + ((sample>>4u)&3u));
    return (vec2<f32>(f32(x),f32(y))+vec2<f32>(0.5))/8.0;
}

// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Instrument the prototype's existing camera, traversal and shading expressions.
@group(0) @binding(0) var<storage,read> inputs: array<vec4<f32>>;
@group(0) @binding(1) var<storage,read_write> outputs: array<vec4<f32>>;
@group(0) @binding(2) var<storage,read> scene: array<vec4<u32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if (i*8u>=arrayLength(&inputs)) { return; }
    let origin=inputs[i*8u]; let forward=inputs[i*8u+1u]; let right=inputs[i*8u+2u];
    let up=inputs[i*8u+3u]; let light=inputs[i*8u+4u]; let background=inputs[i*8u+5u];
    let dims=vec4<u32>(inputs[i*8u+6u]); let pixel=u32(inputs[i*8u+7u].x);
    let jitter=sample_jitter(dims.z);
    let xy=vec2<f32>(f32(pixel%dims.x),f32(pixel/dims.x))+jitter;
    let uv=xy/vec2<f32>(f32(dims.x),f32(dims.y));
    var direction=normalize(forward.xyz+right.xyz*((2.0*uv.x-1.0)*right.w*forward.w)
        +up.xyz*((1.0-2.0*uv.y)*forward.w));
    if (SHARED_RAYS) { direction=inputs[i*8u+7u].yzw; }
    let hit=trace(origin.xyz,direction,origin.w,up.w,-1);
    var color=background.xyz; var normal=vec3<f32>(0); var cosine=0.0;
    var distance_squared=0.0; var blocker_id= -2.0;
    if (hit.w>=0.0) {
        let prim=i32(hit.w); let scene_info=scene_float(0); let vbase=i32(scene_info.w)+3*prim;
        let a=scene_float(vbase).xyz; let b=scene_float(vbase+1).xyz; let c=scene_float(vbase+2).xyz;
        normal=normalize(cross(b-a,c-a)); if (dot(normal,direction)>0.0) { normal=-normal; }
        let p=origin.xyz+direction*hit.x; let delta=light.xyz-p;
        distance_squared=max(dot(delta,delta),0.0001); let distance=sqrt(distance_squared);
        let light_direction=delta/distance; let albedo=scene_float(i32(scene_info.w+3.0*scene_info.x)+prim).xyz;
        color=vec3<f32>(0); cosine=max(dot(normal,light_direction),0.0);
        if (cosine>0.0 && distance>0.0002) {
            let blocker=trace(p+normal*0.0001,light_direction,0.0001,distance-0.0001,prim);
            blocker_id=blocker.w;
            if (blocker.w<0.0) { color=albedo*(cosine*0.3183098861837907)*(light.w/distance_squared); }
        }
    }
    outputs[i*4u]=vec4<f32>(direction,hit.x); outputs[i*4u+1u]=vec4<f32>(normal,hit.w);
    outputs[i*4u+2u]=vec4<f32>(cosine,distance_squared,blocker_id,0);
    outputs[i*4u+3u]=vec4<f32>(color,1);
}
