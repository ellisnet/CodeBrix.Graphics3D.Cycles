const OP_END: u32=0u;
const OP_JUMP: u32=1u;
const OP_GEOMETRY: u32=11u;
const OP_SET_WEIGHT: u32=5u;
const OP_BSDF: u32=2u;
const DIFFUSE: u32=2u;
const OREN_NAYAR: u32=3u;
const TRANSLUCENT: u32=9u;
const REFLECT_DIFFUSE: u32=6u;
const TRANSMIT_DIFFUSE: u32=5u;
const MAX_BOUNCES: u32=4u;
const OUTPUT_ROWS: u32=16u;
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
// BVH traversal adapted from Cycles kernel/bvh/traversal.h and nodes.h.
// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
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

// Manually adapted from Cycles bsdf_diffuse.h and sample/mapping.h.
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Adapted code from Open Shading Language.
// Oren-Nayar, allocation and SVM adaptation: Blender Foundation, Apache-2.0.
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: BSD-3-Clause AND Apache-2.0
// Typed WGSL records replace C++ closure-pointer casts. See THIRD-PARTY-NOTICES.txt.
const PI=3.14159265358979323846f;
const INV_PI=0.31830988618379067154f;
struct Closure { weight: vec3<f32>, sample_weight: f32, normal: vec3<f32>, kind: u32,
                 a: f32, b: f32, multi: vec3<f32>, count: u32, error: u32, }
struct BsdfResult { direction: vec3<f32>, pdf: f32, eval: vec3<f32>, label: u32, }
// Original numerical adapter: bounded-angle Taylor polynomials. Disk sampling
// only supplies angles in [-pi/4, 3*pi/4]. This avoids the loose native sin/cos
// accuracy observed on some shipped WebGPU drivers without changing WebGPU.
fn disk_sincos(angle: f32) -> vec2<f32> {
    var x=angle; var cos_sign=1.0;
    if (x>PI*.5) { x=(3.141592502593994140625-x)+1.509957990978376432e-7; cos_sign=-1.0; }
    let z=x*x;
    var s=1.6059043836821613e-10;
    s=fma(s,z,-2.505210838544172e-8); s=fma(s,z,2.7557319223985893e-6);
    s=fma(s,z,-.0001984126984126984); s=fma(s,z,.008333333333333333);
    s=fma(s,z,-.16666666666666666);
    let sine=fma(x*z,s,x);
    var c=-1.1470745597729725e-11;
    c=fma(c,z,2.08767569878681e-9); c=fma(c,z,-2.755731922398589e-7);
    c=fma(c,z,.0000248015873015873); c=fma(c,z,-.001388888888888889);
    c=fma(c,z,.041666666666666664); c=fma(c,z,-.5);
    return vec2<f32>(sine,fma(c,z,1.0)*cos_sign);
}
fn disk_sample(r: vec2<f32>) -> vec2<f32> {
    let a=2.0*r.x-1.0; let b=2.0*r.y-1.0;
    if (a==0.0 && b==0.0) { return vec2<f32>(0.0); }
    var radius: f32; var phi: f32;
    if (a*a>b*b) { radius=a; phi=(PI*.25)*(b/a); }
    else { radius=b; phi=PI*.5-(PI*.25)*(a/b); }
    let sc=disk_sincos(phi);
    return vec2<f32>(radius*sc.y,radius*sc.x);
}
fn hemisphere(n: vec3<f32>, r: vec2<f32>) -> vec4<f32> {
    let d=disk_sample(r); let cosine=sqrt(max(1.0-dot(d,d),0.0));
    var t: vec3<f32>;
    if (n.x!=n.y || n.x!=n.z) { t=vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x); }
    else { t=vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x); }
    t=normalize(t); let b=cross(n,t);
    return vec4<f32>(d.x*t+d.y*b+cosine*n,cosine*INV_PI);
}
fn oren_g(cosine: f32) -> f32 {
    if (cosine<1e-6) { return (PI*.5-2.0/3.0)-cosine; }
    let sine=sqrt(max(1.0-cosine*cosine,0.0));
    let theta=acos(clamp(cosine,-1.0,1.0));
    return sine*(theta-2.0/3.0-sine*cosine)+(2.0/3.0)*(sine/cosine)*(1.0-sine*sine*sine);
}
fn oren_setup(c: ptr<function,Closure>, color: vec3<f32>, nv: f32, roughness: f32) {
    (*c).a=1.0/(PI+clamp(roughness,0.0,1.0)*(PI*.5-2.0/3.0));
    (*c).b=clamp(roughness,0.0,1.0)*(*c).a;
    let albedo=clamp(color,vec3<f32>(0),vec3<f32>(1));
    let average=(*c).a*PI+((2.0*PI-5.6)/3.0)*(*c).b;
    let ems=INV_PI*albedo*albedo*(average/(1.0-average))/(vec3<f32>(1)-albedo*(1.0-average));
    let ev=(*c).a*PI+(*c).b*oren_g(max(nv,0.0));
    (*c).multi=ems*(1.0-ev);
}
fn oren_intensity(c: Closure, wi: vec3<f32>, wo: vec3<f32>) -> vec3<f32> {
    let nl=max(dot(c.normal,wo),0.0);
    if (c.b<=0.0) { return vec3<f32>(nl*INV_PI); }
    let nv=max(dot(c.normal,wi),0.0);
    var t=dot(wo,wi)-nl*nv;
    if (t>0.0) { t/=max(nl,nv)+1.1754943508222875e-38; }
    let single=c.a+c.b*t;
    let el=c.a*PI+c.b*oren_g(nl);
    return nl*(vec3<f32>(single)+c.multi*(1.0-el));
}
fn bsdf_eval(c: Closure, wi: vec3<f32>, wo: vec3<f32>) -> vec4<f32> {
    var cosine=dot(c.normal,wo);
    if (c.kind==TRANSLUCENT) { cosine=-cosine; }
    let pdf=max(cosine,0.0)*INV_PI;
    if (c.kind==OREN_NAYAR) {
        if (cosine>0.0) { return vec4<f32>(oren_intensity(c,wi,wo),pdf); }
        return vec4<f32>(0);
    }
    return vec4<f32>(vec3<f32>(pdf),pdf);
}
fn bsdf_sample(c: Closure, ng: vec3<f32>, wi: vec3<f32>, random: vec2<f32>) -> BsdfResult {
    let transmit=c.kind==TRANSLUCENT;
    let sample=hemisphere(select(c.normal,-c.normal,transmit),random);
    var result: BsdfResult; result.direction=sample.xyz; result.pdf=sample.w;
    result.label=select(REFLECT_DIFFUSE,TRANSMIT_DIFFUSE,transmit);
    let valid=select(dot(ng,sample.xyz)>0.0,dot(ng,sample.xyz)<0.0,transmit);
    if (valid) {
        if (c.kind==OREN_NAYAR) { result.eval=oren_intensity(c,wi,sample.xyz); }
        else { result.eval=vec3<f32>(result.pdf); }
    } else { result.pdf=0.0; result.eval=vec3<f32>(0); }
    return result;
}

// Cycles SVM adaptation: SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Requires svm_word(index), svm_word_count() and the Closure helpers.
fn stack_float(stack: ptr<function,array<f32,255>>, bits: u32) -> f32 {
    if ((bits>>8u)==0x7fc000u) { return (*stack)[bits&255u]; }
    return bitcast<f32>(bits);
}
fn stack_color(stack: ptr<function,array<f32,255>>, offset: u32) -> vec3<f32> {
    if ((svm_word(offset)>>8u)==0x7fc000u) {
        let k=svm_word(offset)&255u; return vec3<f32>((*stack)[k],(*stack)[k+1u],(*stack)[k+2u]);
    }
    return bitcast<vec3<f32>>(vec3<u32>(svm_word(offset),svm_word(offset+1u),svm_word(offset+2u)));
}
fn decode(shader: u32, n: vec3<f32>, wi: vec3<f32>) -> Closure {
    var c: Closure; var weight=vec3<f32>(0); var stack: array<f32,255>;
    let size=svm_word_count();
    if (shader>=size/4u) { c.error=1u; return c; }
    var offset=shader*4u;
    for(var instruction=0u;instruction<256u;instruction++) {
        if (offset>=size) { c.error=1u; return c; }
        let op=svm_word(offset); offset++;
        if (op==OP_END) { return c; }
        if (op==OP_JUMP) {
            if (offset+3u>size) { c.error=2u; return c; }
            offset=svm_word(offset); continue;
        }
        if (op==OP_GEOMETRY) {
            if (offset+2u>size) { c.error=3u; return c; }
            let bits=svm_word(offset); let out=bits>>24u;
            if ((bits&255u)!=1u || out+2u>=255u || (bits&0xffff00u)!=0u) { c.error=4u; return c; }
            stack[out]=n.x; stack[out+1u]=n.y; stack[out+2u]=n.z; offset+=2u; continue;
        }
        if (op==OP_SET_WEIGHT) {
            if (offset+3u>size) { c.error=5u; return c; }
            weight=bitcast<vec3<f32>>(vec3<u32>(svm_word(offset),svm_word(offset+1u),svm_word(offset+2u))); offset+=3u; continue;
        }
        if (op==OP_BSDF) {
            if (offset+2u>size) { c.error=6u; return c; }
            let kind=svm_word(offset); let mix_offset=svm_word(offset+1u)&255u; offset+=2u;
            var mix=1.0; if (mix_offset!=255u) { mix=stack[mix_offset]; }
            var normal_offset: u32; var roughness=0.0; var color=vec3<f32>(0);
            if (kind==DIFFUSE) {
                if (offset+5u>size) { c.error=7u; return c; }
                if ((svm_word(offset)>>8u)==0x7fc000u && (svm_word(offset)&255u)>252u) { c.error=14u; return c; }
                if ((svm_word(offset+3u)>>8u)==0x7fc000u && (svm_word(offset+3u)&255u)==255u) { c.error=15u; return c; }
                color=stack_color(&stack,offset); roughness=stack_float(&stack,svm_word(offset+3u));
                normal_offset=svm_word(offset+4u)&255u; offset+=5u;
            } else if (kind==TRANSLUCENT) {
                if (offset+2u>size) { c.error=8u; return c; }
                normal_offset=svm_word(offset+1u)&255u; offset+=2u;
            } else { c.error=9u; return c; }
            var normal=n;
            if (normal_offset!=255u) {
                if (normal_offset+2u>=255u) { c.error=10u; return c; }
                normal=vec3<f32>(stack[normal_offset],stack[normal_offset+1u],stack[normal_offset+2u]);
            }
            let magnitude=length(normal); if (magnitude!=0.0) { normal*=1.0/magnitude; } else { normal=n; }
            let nonnegative=max(weight*mix,vec3<f32>(0));
            let sample_weight=abs((nonnegative.x+nonnegative.y+nonnegative.z)*(1.0/3.0));
            if (sample_weight<1e-5 || mix==0.0) { continue; }
            if (c.count!=0u) { c.error=11u; return c; }
            c.count=1u; c.weight=nonnegative; c.sample_weight=sample_weight; c.normal=normal; c.kind=kind;
            if (kind==DIFFUSE && roughness>=1e-5) {
                c.kind=OREN_NAYAR; oren_setup(&c,clamp(color,vec3<f32>(0),vec3<f32>(1)),dot(normal,wi),roughness);
            }
            continue;
        }
        c.error=12u; return c;
    }
    c.error=13u; return c;
}

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

// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Ported from Cycles light/point.h, light/distribution.h, sample/mapping.h,
// sample/mis.h and util/math_intersect.h. Mapping/MIS retain the OSL BSD notice:
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
struct LightSample { p: vec3<f32>, t: f32, d: vec3<f32>, pdf: f32,
                     n: vec3<f32>, eval_fac: f32, id: u32 }
fn light_float(row: u32) -> vec4<f32> { return bitcast<vec4<f32>>(light_data[row]); }
fn light_row(id: u32) -> u32 { return light_data[0].z+4u*id; }
fn power_weight(a: f32, b: f32) -> f32 { return (a*a)/(a*a+b*b); }
fn cone_angle(sine_squared: f32) -> f32 {
    if(sine_squared>0.0004) { return 1.0-sqrt(max(1.0-sine_squared,0.0)); }
    return 0.5*sine_squared;
}
fn light_tangent(n: vec3<f32>) -> vec3<f32> {
    if(n.x!=n.y || n.x!=n.z) { return normalize(vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x)); }
    return normalize(vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x));
}
fn sphere_pdf(distance_squared: f32, radius_squared: f32, n: vec3<f32>, d: vec3<f32>, transmission: bool) -> f32 {
    if(distance_squared>radius_squared) { return (INV_PI*.5)/cone_angle(radius_squared/distance_squared); }
    if(transmission) { return INV_PI*.25; }
    return max(dot(n,d),0.0)*INV_PI;
}
fn sphere_intersect(p: vec3<f32>, d: vec3<f32>, tmin: f32, tmax: f32, light: vec4<f32>) -> f32 {
    let delta=light.xyz-p; let r2=light.w*light.w; let d2=dot(delta,delta); let dc=dot(delta,d);
    if(d2>r2 && dc<0.0) { return -1; }
    let perpendicular=delta-dc*d; let ds2=dot(perpendicular,perpendicular);
    if(ds2>r2) { return -1; }
    let t=dc-select(-1.0,1.0,d2-r2>=0.0)*sqrt(r2-ds2);
    if(t>tmin && t<tmax) { return t; } return -1;
}
fn select_light(random: f32) -> u32 {
    var first=0u; var len=light_data[0].y+1u; let base=light_data[0].w;
    loop {
        let half=len>>1u; let middle=first+half;
        if(random<light_float(base+middle).x) { len=half; }
        else { first=middle+1u; len-=half+1u; }
        if(len==0u) { break; }
    }
    let index=clamp(i32(first)-1,0,i32(light_data[0].y)-1);
    return light_data[base+u32(index)].y;
}
fn sample_light(p: vec3<f32>, n: vec3<f32>, transmission: bool, random: vec3<f32>) -> LightSample {
    let id=select_light(random.z); let base=light_row(id); let light=light_float(base);
    let radius_squared=light.w*light.w; let delta=p-light.xyz;
    let distance_squared=dot(delta,delta); let distance=sqrt(distance_squared); let light_n=delta/distance;
    var d: vec3<f32>; var pdf: f32; var cosine: f32;
    if(distance_squared>radius_squared) {
        let angle=cone_angle(radius_squared/distance_squared);
        var xy=disk_sample(random.xy); let r2=dot(xy,xy);
        cosine=1.0-r2*angle; xy*=sqrt(max(angle*(2.0-angle*r2),0.0));
        let axis= -light_n; let t=light_tangent(axis); let b=cross(axis,t);
        d=xy.x*t+xy.y*b+cosine*axis; pdf=(INV_PI*.5)/angle;
    } else {
        if(transmission) {
            let z=1.0-2.0*random.x; let radius=sqrt(max(1.0-z*z,0.0)); let phi=2.0*PI*random.y;
            d=vec3<f32>(radius*cos(phi),radius*sin(phi),z); pdf=INV_PI*.25;
        } else { let h=hemisphere(n,random.xy); d=h.xyz; pdf=h.w; }
        cosine= -dot(d,light_n);
    }
    let t=distance*cosine-select(-1.0,1.0,distance_squared-radius_squared>=0.0)*
          sqrt(max(radius_squared-distance_squared+distance_squared*(cosine*cosine),0.0));
    let sampled=p+d*t; let normal=normalize(sampled-light.xyz);
    return LightSample(normal*light.w+light.xyz,t,d,pdf*light_float(1).x,normal,light_float(base+1u).w,id);
}

// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Adapted from Cycles light/sample.h and integrator/shade_surface.h/shade_light.h.
// Constant finite spherical lights, flat opaque geometry; other modes rejected by exporter.
@group(0) @binding(3) var<storage,read> light_data: array<vec4<u32>>;
@group(0) @binding(4) var<storage,read_write> shadow_rays: array<vec4<f32>>;
fn lighting_begin(base: u32) {
    for(var b=0u;b<MAX_BOUNCES;b++) {
        outputs[base+MAX_BOUNCES+3u+2u*b]=vec4<f32>(0);
        outputs[base+MAX_BOUNCES+4u+2u*b]=vec4<f32>(-1,-1,0,0);
    }
    outputs[base+3u*MAX_BOUNCES+3u]=vec4<f32>(0);
}
fn lighting_forward(p: vec3<f32>, d: vec3<f32>, tmin: f32, tmax: f32,
    previous_pdf: f32, previous_n: vec3<f32>, transmission: bool, bounce: u32,
    count: u32, throughput: vec3<f32>, base: u32) -> ForwardLighting {
    var result=ForwardLighting(vec3<f32>(0),0u,false); var start=tmin;
    var last= -1; let limit=u32(light_float(1).z);
    for(var step=0u;step<light_data[0].x;step++) {
        var closest=tmax; var selected= -1;
        for(var id=0u;id<light_data[0].x;id++) {
            if(i32(id)==last) { continue; }
            let t=sphere_intersect(p,d,start,closest,light_float(light_row(id)));
            if(t>=0.0) { closest=t; selected=i32(id); }
        }
        if(selected<0) { break; }
        let row=light_row(u32(selected)); let light=light_float(row); let emission=light_float(row+1u);
        var weight=1.0;
        if(bounce>0u) {
            let delta=p-light.xyz;
            let pdf=sphere_pdf(dot(delta,delta),light.w*light.w,previous_n,d,transmission)*light_float(1).x;
            weight=power_weight(previous_pdf,pdf);
        }
        let contribution=throughput*(emission.xyz*emission.w)*weight;
        result.color+=contribution; result.hits++;
        if(count+result.hits>=limit) { result.terminated=true; break; }
        start=bitcast<f32>(bitcast<u32>(closest)+1u); last=selected;
    }
    outputs[base+3u*MAX_BOUNCES+3u]+=vec4<f32>(result.color,f32(result.hits));
    return result;
}
fn lighting_direct(i: u32, bounce: u32, base: u32, p: vec3<f32>, n: vec3<f32>, wi: vec3<f32>,
    closure: Closure, throughput: vec3<f32>, a: vec3<f32>, b: vec3<f32>, c: vec3<f32>, prim: i32) -> vec3<f32> {
    let pixel=pixel_sample(i); let dimension=(bounce+1u)*camera_data[3].y+light_data[2].x;
    let random=sobol_sample(pixel.z,pixel.w,dimension,camera_data[1].x,camera_float(2).x).xyz;
    let sample=sample_light(p,n,closure.kind==TRANSLUCENT,random);
    let row=light_row(sample.id);
    if(bounce>light_data[row+2u].y || sample.pdf<=0.0) { return vec3<f32>(0); }
    let evaluated=bsdf_eval(closure,wi,sample.d); let emission=light_float(row+1u);
    let weight=power_weight(sample.pdf,evaluated.w);
    let light_factor=emission.xyz*sample.eval_fac/sample.pdf*weight;
    let value=(evaluated.xyz*closure.weight)*light_factor;
    if(all(value==vec3<f32>(0))) { return vec3<f32>(0); }
    let delta=sample.p-p; let distance=length(delta); let direction=delta/distance;
    var origin=p;
    if(!self_intersection(p,direction,a,b,c)) { origin=offset_ray(p,n); }
    let contribution=throughput*value;
    let record=base+MAX_BOUNCES+3u+2u*bounce;
    outputs[record]=vec4<f32>(contribution,distance);
    outputs[record+1u]=vec4<f32>(f32(sample.id),-1,0,0);
    let shadow=(i*MAX_BOUNCES+bounce)*2u;
    shadow_rays[shadow]=vec4<f32>(origin,f32(prim));
    shadow_rays[shadow+1u]=vec4<f32>(direction,distance);
    return vec3<f32>(0);
}
// A separate dispatch keeps shadow BVH traversal out of the surface integrator.
// Dispatch boundaries provide visibility for the GPU-written ray/state buffers.
@compute @workgroup_size(64) fn shadow_resolve(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if(i>=path_count()) { return; }
    let base=i*OUTPUT_ROWS; var contribution=vec3<f32>(0);
    for(var b=0u;b<MAX_BOUNCES;b++) {
        let record=base+MAX_BOUNCES+3u+2u*b;
        if(outputs[record+1u].x<0.0) { continue; }
        let shadow=(i*MAX_BOUNCES+b)*2u; let p=shadow_rays[shadow]; let d=shadow_rays[shadow+1u];
        let blocked=trace(p.xyz,d.xyz,0.0,d.w,i32(p.w)).w>=0.0;
        outputs[record+1u].y=select(0.0,1.0,blocked);
        if(!blocked) { contribution+=outputs[record].xyz; }
    }
    outputs[base]=vec4<f32>(outputs[base].xyz+contribution,outputs[base].w);
}

// Ray origin and path updates adapted from Cycles geom/triangle_intersect.h,
// util/math_intersect.h, kernel/bvh/util.h and integrator/shade_surface.h.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Limited forward path tracer: single implemented SVM closure per surface,
// fixed bounce limit; lighting hooks supply NEE/MIS in the light gate.
// RR, textures and volumes remain unsupported here.
struct ForwardLighting { color: vec3<f32>, hits: u32, terminated: bool }
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
    let i=id.x; let rows=OUTPUT_ROWS;
    if (i>=path_count()) { return; }
    let output_base=i*rows; let initial=path_initial(i);
    var p=initial.origin.xyz; var tmin=initial.origin.w;
    var d=initial.direction.xyz; var tmax=initial.direction.w;
    var throughput=initial.throughput; var color=vec3<f32>(0);
    var skip= -1; var bounces=0u; var error=0u;
    var previous_pdf=0.0; var previous_n=vec3<f32>(0); var transmission=false; var light_hits=0u;
    lighting_begin(output_base);
    for(var b=0u;b<=MAX_BOUNCES;b++) { outputs[output_base+2u+b]=vec4<f32>(0,0,0,-2); }
    for(var b=0u;b<=MAX_BOUNCES;b++) {
        let hit=trace(p,d,tmin,tmax,skip);
        let forward=lighting_forward(p,d,tmin,hit.x,previous_pdf,previous_n,transmission,b,
                                     light_hits,throughput,output_base);
        color+=forward.color; light_hits+=forward.hits;
        if(forward.terminated) { break; }
        outputs[output_base+2u+b]=hit;
        if (hit.w<0.0) { color+=throughput*scene_float(i32(metadata_row()+1u)).xyz; break; }
        if (b==MAX_BOUNCES) { break; }
        let h=scene_float(0); let base=i32(h.w)+3*i32(hit.w);
        let a=scene_float(base).xyz; let bv=scene_float(base+1).xyz; let c=scene_float(base+2).xyz;
        var n=normalize(cross(bv-a,c-a)); if (dot(n,-d)<0.0) { n=-n; }
        let shader=u32(scene_float(i32(h.w+3.0*h.x)+i32(hit.w)).w);
        let closure=decode(shader,n,-d);
        if (closure.error!=0u) { error=closure.error; break; }
        if (closure.count==0u) { break; }
        let position=a+hit.y*(bv-a)+hit.z*(c-a);
        color+=lighting_direct(i,b,output_base,position,n,-d,closure,throughput,a,bv,c,i32(hit.w));
        let sampled=bsdf_sample(closure,n,-d,path_random(i,b).xy);
        if (sampled.pdf==0.0 || all(sampled.eval==vec3<f32>(0))) { break; }
        throughput*=sampled.eval*closure.weight/sampled.pdf;
        previous_pdf=sampled.pdf; previous_n=n; transmission=closure.kind==TRANSLUCENT;
        p=position; d=normalize(sampled.direction);
        if (!self_intersection(p,d,a,bv,c)) { p=offset_ray(p,n); }
        tmin=0; tmax=3.4028234663852886e38; skip=i32(hit.w); bounces++;
    }
    outputs[output_base]=vec4<f32>(color,1);
    outputs[output_base+1u]=vec4<f32>(f32(bounces),f32(error),0,0);
}

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
@compute @workgroup_size(64) fn inactive_accumulate(@builtin(global_invocation_id) id: vec3<u32>) {
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

@compute @workgroup_size(64) fn accumulate() {}
