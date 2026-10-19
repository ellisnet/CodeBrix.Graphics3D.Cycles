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
fn disk_sample(r: vec2<f32>) -> vec2<f32> {
    let a=2.0*r.x-1.0; let b=2.0*r.y-1.0;
    if (a==0.0 && b==0.0) { return vec2<f32>(0.0); }
    var radius: f32; var phi: f32;
    if (a*a>b*b) { radius=a; phi=(PI*.25)*(b/a); }
    else { radius=b; phi=PI*.5-(PI*.25)*(a/b); }
    return vec2<f32>(radius*cos(phi),radius*sin(phi));
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
// Reads the original, unmodified native compiler bytecode. Unsupported nodes
// return an explicit error, never a default material.
@group(0) @binding(0) var<storage,read> inputs: array<vec4<f32>>;
@group(0) @binding(1) var<storage,read_write> outputs: array<vec4<f32>>;
@group(0) @binding(2) var<storage,read> code: array<u32>;
fn stack_float(stack: ptr<function,array<f32,255>>, bits: u32) -> f32 {
    if ((bits>>8u)==0x7fc000u) { return (*stack)[bits&255u]; }
    return bitcast<f32>(bits);
}
fn stack_color(stack: ptr<function,array<f32,255>>, offset: u32) -> vec3<f32> {
    if ((code[offset]>>8u)==0x7fc000u) {
        let k=code[offset]&255u; return vec3<f32>((*stack)[k],(*stack)[k+1u],(*stack)[k+2u]);
    }
    return bitcast<vec3<f32>>(vec3<u32>(code[offset],code[offset+1u],code[offset+2u]));
}
fn decode(shader: u32, n: vec3<f32>, wi: vec3<f32>) -> Closure {
    var c: Closure; var weight=vec3<f32>(0); var stack: array<f32,255>;
    var offset=shader*4u; let size=arrayLength(&code);
    for(var instruction=0u;instruction<256u;instruction++) {
        if (offset>=size) { c.error=1u; return c; }
        let op=code[offset]; offset++;
        if (op==OP_END) { return c; }
        if (op==OP_JUMP) {
            if (offset+3u>size) { c.error=2u; return c; }
            offset=code[offset]; continue;
        }
        if (op==OP_GEOMETRY) {
            if (offset+2u>size) { c.error=3u; return c; }
            let bits=code[offset]; let out=bits>>24u;
            if ((bits&255u)!=1u || out+2u>=255u || (bits&0xffff00u)!=0u) { c.error=4u; return c; }
            stack[out]=n.x; stack[out+1u]=n.y; stack[out+2u]=n.z; offset+=2u; continue;
        }
        if (op==OP_SET_WEIGHT) {
            if (offset+3u>size) { c.error=5u; return c; }
            weight=bitcast<vec3<f32>>(vec3<u32>(code[offset],code[offset+1u],code[offset+2u])); offset+=3u; continue;
        }
        if (op==OP_BSDF) {
            if (offset+2u>size) { c.error=6u; return c; }
            let kind=code[offset]; let mix_offset=code[offset+1u]&255u; offset+=2u;
            var mix=1.0; if (mix_offset!=255u) { mix=stack[mix_offset]; }
            var normal_offset: u32; var roughness=0.0; var color=vec3<f32>(0);
            if (kind==DIFFUSE) {
                if (offset+5u>size) { c.error=7u; return c; }
                color=stack_color(&stack,offset); roughness=stack_float(&stack,code[offset+3u]);
                normal_offset=code[offset+4u]&255u; offset+=5u;
            } else if (kind==TRANSLUCENT) {
                if (offset+2u>size) { c.error=8u; return c; }
                normal_offset=code[offset+1u]&255u; offset+=2u;
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
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if (i*5u>=arrayLength(&inputs)) { return; }
    let n=inputs[i*5u]; let ng=inputs[i*5u+1u].xyz; let wi=inputs[i*5u+2u].xyz;
    let c=decode(u32(n.w),n.xyz,wi);
    outputs[i*6u+5u]=vec4<f32>(f32(c.count),f32(c.error),0,0);
    if (c.count==0u || c.error!=0u) { return; }
    let sample=bsdf_sample(c,ng,wi,inputs[i*5u+3u].xy);
    let value=bsdf_eval(c,wi,inputs[i*5u+4u].xyz);
    outputs[i*6u]=vec4<f32>(c.weight,c.sample_weight);
    outputs[i*6u+1u]=vec4<f32>(c.normal,f32(c.kind));
    outputs[i*6u+2u]=vec4<f32>(sample.direction,sample.pdf);
    outputs[i*6u+3u]=vec4<f32>(sample.eval,f32(sample.label));
    outputs[i*6u+4u]=value;
}
