const DIFFUSE: u32=2u;
const OREN_NAYAR: u32=3u;
const TRANSLUCENT: u32=9u;
const REFLECT_DIFFUSE: u32=6u;
const TRANSMIT_DIFFUSE: u32=5u;
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

@group(0) @binding(0) var<storage,read> queries: array<vec4<f32>>;
@group(0) @binding(1) var<storage,read_write> outputs: array<vec4<f32>>;
@group(0) @binding(2) var<storage,read> light_data: array<vec4<u32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if(i*3u>=arrayLength(&queries)) { return; }
    let p=queries[i*3u]; let n=queries[i*3u+1u]; let random=queries[i*3u+2u].xyz;
    let sample=sample_light(p.xyz,n.xyz,p.w!=0,random); let light=light_float(light_row(sample.id));
    let t=sphere_intersect(p.xyz,sample.d,0,3.4028234663852886e38,light);
    let delta=p.xyz-light.xyz;
    let pdf=sphere_pdf(dot(delta,delta),light.w*light.w,n.xyz,sample.d,p.w!=0)*light_float(1).x;
    outputs[i*5u]=vec4<f32>(sample.p,sample.t);
    outputs[i*5u+1u]=vec4<f32>(sample.d,sample.pdf);
    outputs[i*5u+2u]=vec4<f32>(sample.n,sample.eval_fac);
    outputs[i*5u+3u]=vec4<f32>(f32(sample.id),select(0.0,1.0,t>=0),max(t,0),pdf);
    outputs[i*5u+4u]=vec4<f32>(power_weight(sample.pdf,n.w),power_weight(n.w,pdf),light_float(1).x,0);
}
