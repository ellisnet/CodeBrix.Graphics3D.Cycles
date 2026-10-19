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
