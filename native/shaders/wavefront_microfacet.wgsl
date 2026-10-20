// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2011-2013 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from pinned Cycles closure/bsdf_microfacet.h, sample/mapping.h and
// util/math_float3.h: GGX reflection/refraction and dielectric glass, using
// canonical arena closure records.
// Native notices and the complete BSD license are retained in
// native/THIRD-PARTY-NOTICES.txt and parity/licenses/BSD-3-Clause.txt.
// Original CodeBrix arithmetic adapters. Native normalization rounds the squared
// products and x/y sum before adding z. SSE divides each component by the length;
// scalar float3 division computes a reciprocal and then multiplies. The emitted
// constant preserves the source host's operation order when replayed on any GPU.
// A one-ulp normalization error can dominate the sharp GGX denominator. Integer
// addition and integer midpoint comparisons preserve these rounding boundaries
// without changing the distribution or roughness threshold.
fn ggx_positive_add(a:f32,b:f32)->f32{return rounded_add(a,b);}
fn ggx_unit_sqrt(value:f32)->f32{if(value>=1.0){return 0.0;}return rounded_sqrt(rounded_add(1.0,-value));}
fn ggx_normalize(v:vec3<f32>)->vec3<f32>{
    if(GGX_NATIVE_RECIPROCAL_NORMALIZE!=0u){
        return v*rounded_divide(1.0,rounded_sqrt(rounded_dot(v,v)));
    }
    return rounded_normalize(v);
}
struct MicrofacetBasis {X:vec3<f32>,Y:vec3<f32>}
fn ggx_basis(c:SvmClosure)->MicrofacetBasis{
    var b:MicrofacetBasis;
    if(c.alpha_x==c.alpha_y){
        let n=c.N;
        b.X=vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x);
        if(n.x==n.y&&n.x==n.z){b.X=vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x);}
        b.X=ggx_normalize(b.X);b.Y=rounded_cross(n,b.X);
    }else{b.Y=ggx_normalize(rounded_cross(c.N,c.T));b.X=rounded_cross(b.Y,c.N);}
    return b;
}
fn ggx_lambda(alpha2:f32,cosine:f32)->f32{return 0.5*(sqrt(1.0+alpha2*max(1.0/(cosine*cosine)-1.0,0.0))-1.0);}
fn ggx_aniso_lambda(c:SvmClosure,v:vec3<f32>)->f32{
    let x=c.alpha_x*v.x;let y=c.alpha_y*v.y;return 0.5*(sqrt(1.0+(x*x+y*y)/(v.z*v.z))-1.0);
}
fn ggx_d(alpha2:f32,cosine:f32)->f32{let z=min(cosine*cosine,1.0);let v=rounded_add(rounded_add(1.0,-z),alpha2*z);return alpha2/(PI*(v*v));}
fn ggx_aniso_d(c:SvmClosure,h:vec3<f32>)->f32{let v=h/vec3<f32>(c.alpha_x,c.alpha_y,1);let l=rounded_dot(v,v);return INV_PI/((c.alpha_x*c.alpha_y)*(l*l));}
// Pinned Cycles concentric-disk mapping, with separately rounded phase math
// and the original bounded-angle adapter from wavefront_trig.wgsl.
fn ggx_disk_sample(r:vec2<f32>)->vec2<f32>{
    let a=rounded_add(2.0*r.x,-1.0);let b=rounded_add(2.0*r.y,-1.0);
    if(a==0.0&&b==0.0){return vec2<f32>(0);}
    var radius:f32;var phi:f32;
    if(a*a>b*b){radius=a;phi=(PI*0.25)*rounded_divide(b,a);}
    else{radius=b;phi=rounded_add(PI*0.5,-(PI*0.25)*rounded_divide(a,b));}
    let sc=native_disk_sincos(phi);return vec2<f32>(radius*sc.y,radius*sc.x);
}
fn ggx_vndf(wi:vec3<f32>,ax:f32,ay:f32,random:vec2<f32>)->vec3<f32>{
    let v=ggx_normalize(vec3<f32>(ax*wi.x,ay*wi.y,wi.z));let lensq=ggx_positive_add(v.x*v.x,v.y*v.y);
    var x=vec3<f32>(1,0,0);var y=vec3<f32>(0,1,0);
    if(lensq>1e-7){x=vec3<f32>(-v.y,v.x,0)*rounded_divide(1.0,rounded_sqrt(lensq));y=rounded_cross(v,x);}
    var t=ggx_disk_sample(random);let blend=0.5*rounded_add(1.0,v.z);let edge=ggx_unit_sqrt(t.x*t.x);t.y=rounded_add(edge,blend*rounded_add(t.y,-edge));
    let h=rounded_add3(rounded_add3(t.x*x,t.y*y),ggx_unit_sqrt(ggx_positive_add(t.x*t.x,t.y*t.y))*v);
    return ggx_normalize(vec3<f32>(ax*h.x,ay*h.y,max(0.0,h.z)));
}
struct MicrofacetFresnel {reflection:vec3<f32>,transmission:vec3<f32>,cos_transmit:f32}
fn ggx_fresnel(c:SvmClosure,cosine:f32)->MicrofacetFresnel{
    var f:MicrofacetFresnel;
    if(c.kind==CLOSURE_BSDF_MICROFACET_GGX_ID){f.reflection=vec3<f32>(1);return f;}
    let dielectric=ggx_dielectric(cosine,c.ior);f.cos_transmit=dielectric.y;
    if(c.kind==CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID){f.transmission=select(vec3<f32>(1),vec3<f32>(0),dielectric.x==1.0);return f;}
    let real_f0=ggx_f0(c.ior);let t=clamp(rounded_divide(rounded_add(dielectric.x,-real_f0),rounded_add(1.0,-real_f0)),0.0,1.0);
    let fresnel=rounded_add(rounded_add(1.0,-t)*c.fresnel_f0,t);
    f.reflection=fresnel*c.reflection_tint;f.transmission=rounded_add(1.0,-fresnel)*c.transmission_tint;return f;
}
fn ggx_reflection_pdf(f:MicrofacetFresnel)->f32{
    let sum=f.reflection+f.transmission;
    return ((f.reflection.x+f.reflection.y+f.reflection.z)*(1.0/3.0))/((sum.x+sum.y+sum.z)*(1.0/3.0));
}
fn ggx_evaluate(c:SvmClosure,wi:vec3<f32>,wo:vec3<f32>)->vec4<f32>{
    let ci=rounded_dot(c.N,wi);let co=rounded_dot(c.N,wo);let alpha2=c.alpha_x*c.alpha_y;let transmit=co<0.0;
    if(ci<=0.0||alpha2<=2e-10||(transmit&&c.kind==CLOSURE_BSDF_MICROFACET_GGX_ID)||(!transmit&&c.kind==CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID)){return vec4<f32>(0);}
    let unnormalized=select(wi+wo,-rounded_add3(c.ior*wo,wi),transmit);let length=rounded_sqrt(rounded_dot(unnormalized,unnormalized));
    let inverse=select(0.0,rounded_divide(1.0,length),length!=0.0);let h=unnormalized*inverse;
    let cos_hi=rounded_dot(h,wi);let f=ggx_fresnel(c,cos_hi);if(all(f.reflection==vec3<f32>(0))&&all(f.transmission==vec3<f32>(0))){return vec4<f32>(0);}
    let ch=rounded_dot(c.N,h);var d:f32;var li:f32;var lo:f32;
    if(c.alpha_x==c.alpha_y||transmit){d=ggx_d(alpha2,ch);li=ggx_lambda(alpha2,ci);lo=ggx_lambda(alpha2,co);}
    else{let b=ggx_basis(c);let hi=vec3<f32>(rounded_dot(b.X,h),rounded_dot(b.Y,h),ch);let ii=vec3<f32>(rounded_dot(b.X,wi),rounded_dot(b.Y,wi),ci);let oo=vec3<f32>(rounded_dot(b.X,wo),rounded_dot(b.Y,wo),co);
        d=ggx_aniso_d(c,hi);li=ggx_aniso_lambda(c,ii);lo=ggx_aniso_lambda(c,oo);}
    var factor=d/ci*0.25;if(transmit){let eta_inverse=c.ior*inverse;factor=d/ci*(eta_inverse*eta_inverse)*abs(cos_hi*rounded_dot(h,wo));}
    let reflection_pdf=ggx_reflection_pdf(f);let lobe_pdf=select(reflection_pdf,1.0-reflection_pdf,transmit);
    return vec4<f32>(select(f.reflection,f.transmission,transmit)*(factor/(1.0+lo+li)*c.energy_scale),factor*lobe_pdf/(1.0+li));
}
fn ggx_sample(sd:SurfaceData,c:SvmClosure,random:vec3<f32>)->SurfaceSample{
    var out:SurfaceSample;let ci=rounded_dot(c.N,sd.wi);if(ci<=0.0){return out;}
    let alpha2=c.alpha_x*c.alpha_y;var singular=alpha2<=2e-10;
    var h=c.N;var hi:vec3<f32>;var ii:vec3<f32>;
    if(!singular){let b=ggx_basis(c);ii=vec3<f32>(rounded_dot(b.X,sd.wi),rounded_dot(b.Y,sd.wi),ci);hi=ggx_vndf(ii,c.alpha_x,c.alpha_y,random.xy);h=rounded_add3(rounded_add3(hi.x*b.X,hi.y*b.Y),hi.z*c.N);}
    let ch=rounded_dot(h,sd.wi);let f=ggx_fresnel(c,ch);if(all(f.reflection==vec3<f32>(0))&&all(f.transmission==vec3<f32>(0))){return out;}
    let reflection_pdf=ggx_reflection_pdf(f);let transmit=random.z>=reflection_pdf;let inv_eta=rounded_divide(1.0,c.ior);
    out.wo=rounded_add3(2.0*ch*h,-sd.wi);if(transmit){out.wo=rounded_add3(rounded_add(inv_eta*ch,f.cos_transmit)*h,-inv_eta*sd.wi);}
    let co=rounded_dot(c.N,out.wo);if((rounded_dot(sd.Ng,out.wo)<0.0)!=transmit||(co<0.0)!=transmit){return out;}
    out.pdf=select(reflection_pdf,1.0-reflection_pdf,transmit);out.eval=select(f.reflection,f.transmission,transmit);
    singular=singular||(transmit&&abs(c.ior-1.0)<1e-4);
    if(singular){out.pdf*=1e6;out.eval*=1e6;}
    else{var d:f32;var li:f32;var lo:f32;
        if(c.alpha_x==c.alpha_y||transmit){d=ggx_d(alpha2,hi.z);li=ggx_lambda(alpha2,ci);lo=ggx_lambda(alpha2,co);}
        else{let oo=2.0*ch*hi-ii;d=ggx_aniso_d(c,hi);li=ggx_aniso_lambda(c,ii);lo=ggx_aniso_lambda(c,oo);}
        var factor=d/ci*0.25;if(transmit){let denominator=rounded_add(f.cos_transmit,ch*inv_eta);factor=d/ci*(abs(ch*f.cos_transmit)/(denominator*denominator));}
        out.pdf*=factor/(1.0+li);out.eval*=factor/(1.0+li+lo);
    }
    out.eval*=c.energy_scale;out.roughness=alpha2;out.label=select(LABEL_REFLECT,LABEL_TRANSMIT,transmit)|select(LABEL_GLOSSY,LABEL_SINGULAR,singular);return out;
}
