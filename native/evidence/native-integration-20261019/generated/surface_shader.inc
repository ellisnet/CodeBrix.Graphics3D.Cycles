R"CBWGSL(// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Cycles background shader evaluation and native film/pass semantics.
// SVM support is shared with subsequent native shader entries.
fn film_add(address:u32,value:f32){
    var old=load_word(address);
    loop{let next=bitcast<u32>(bitcast<f32>(old)+value);let result=atomicCompareExchangeWeak(&heap[address/4u],old,next);
        if(result.exchanged){break;}old=result.old_value;}
}
fn film_rgb(base:u32,offset:u32,value:vec3<f32>){
    if(offset==PASS_NONE){return;}
    film_add(base+offset*4u,value.x);film_add(base+(offset+1u)*4u,value.y);film_add(base+(offset+2u)*4u,value.z);
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
// Copyright (c) 2026 Jeremy Ellis and contributors
// Copyright (c) 2018-2024, Arm Limited.
// SPDX-License-Identifier: Apache-2.0 AND MIT
// Bounded adaptation of Arm Optimized Routines v25.01 sincosf polynomials.
// Source pin: third_party/arm-math-import.json; selected MIT grant:
// third_party/arm-optimized-routines/LICENSE.
// Changes: bounded disk-angle reduction and two-component Horner evaluation
// using original CodeBrix float helpers; no binary64 requirement. Matches the
// observed scalar platform sinf/cosf polynomial, not correctly rounded libm.
// Inputs are binary32 disk/area sampling angles in [-pi,pi].
fn native_disk_sincos(angle:f32)->vec2<f32>{
    if(angle==0.0){return vec2<f32>(angle,1);}
    var n=0;
    if(angle>=0.7853981633974483){n=1;}
    if(angle>=2.356194490192345){n=2;}
    if(angle<=-0.7853981633974483){n=-1;}
    if(angle<=-2.356194490192345){n=-2;}
    var x=vec2<f32>(angle,0);
    if(n!=0){x=vec2<f32>(rounded_add(angle,-f32(n)*1.5707963705062866),f32(n)*4.3711390001862426e-8);}
    var sine=rounded_add(x.x,x.y);var cosine=1.0;
    if(abs(x.x)>=0.000244140625){
      let z=trig_mul(x,x);
      var sn=trig_coeff(vec2<f32>(0.008332177996635437,1.4950340965214082e-10),trig_mul(z,vec2<f32>(-0.0001951729936990887,3.885231546207901e-12)));
      sn=trig_coeff(vec2<f32>(-0.16666655242443085,2.9874200890844804e-09),trig_mul(z,sn));
      sn=trig_coeff(vec2<f32>(1,0),trig_mul(z,sn));
      sn=trig_mul(x,sn);
      var cs=trig_coeff(vec2<f32>(-0.0013886763481423259,-3.129527886835959e-11),trig_mul(z,vec2<f32>(2.4390450562350452e-05,1.4121409391224665e-13)));
      cs=trig_coeff(vec2<f32>(0.04166662320494652,1.1939799149374153e-10),trig_mul(z,cs));
      cs=trig_coeff(vec2<f32>(-0.5,2.7489177600870107e-09),trig_mul(z,cs));
      cs=trig_coeff(vec2<f32>(1.0,0.0),trig_mul(z,cs));
      sine=rounded_add(sn.x,sn.y);cosine=rounded_add(cs.x,cs.y);
    }
    if(n==1){return vec2<f32>(cosine,-sine);}
    if(n==2||n== -2){return vec2<f32>(-sine,-cosine);}
    if(n== -1){return vec2<f32>(-cosine,sine);}
    return vec2<f32>(sine,cosine);
}

// Bounded HG azimuth adapter, 0 <= angle <= 2*pi. The same reviewed Arm
// polynomial is used, with two-component reduction covering all four quadrants.
// trig_product retains the rounding residual of 3*(pi/2), unlike a single float.
fn native_phase_sincos(angle:f32)->vec2<f32>{
    if(angle==0.0){return vec2<f32>(angle,1);}
    let n=i32(floor(angle*0.6366197723675814+0.5));
    var x=vec2<f32>(angle,0);
    if(n!=0){let product=trig_product(f32(n),1.5707963705062866);
        x=vec2<f32>(rounded_add(angle,-product.x),rounded_add(-product.y,f32(n)*4.3711390001862426e-8));}
    var sine=rounded_add(x.x,x.y);var cosine=1.0;
    if(abs(x.x)>=0.000244140625){
      let z=trig_mul(x,x);
      var sn=trig_coeff(vec2<f32>(0.008332177996635437,1.4950340965214082e-10),trig_mul(z,vec2<f32>(-0.0001951729936990887,3.885231546207901e-12)));
      sn=trig_coeff(vec2<f32>(-0.16666655242443085,2.9874200890844804e-09),trig_mul(z,sn));
      sn=trig_coeff(vec2<f32>(1,0),trig_mul(z,sn));
      sn=trig_mul(x,sn);
      var cs=trig_coeff(vec2<f32>(-0.0013886763481423259,-3.129527886835959e-11),trig_mul(z,vec2<f32>(2.4390450562350452e-05,1.4121409391224665e-13)));
      cs=trig_coeff(vec2<f32>(0.04166662320494652,1.1939799149374153e-10),trig_mul(z,cs));
      cs=trig_coeff(vec2<f32>(-0.5,2.7489177600870107e-09),trig_mul(z,cs));
      cs=trig_coeff(vec2<f32>(1.0,0.0),trig_mul(z,cs));
      sine=rounded_add(sn.x,sn.y);cosine=rounded_add(cs.x,cs.y);
    }
    if((n&3)==1){return vec2<f32>(cosine,-sine);}
    if((n&3)==2){return vec2<f32>(-sine,-cosine);}
    if((n&3)==3){return vec2<f32>(-cosine,sine);}
    return vec2<f32>(sine,cosine);
}
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// GPU-private stage records retain all evaluated shader and closure metadata.
// The first word is cleared before validation and set only after a full write.
struct SurfaceStage {sd:SurfaceData,r:SvmResult}
fn surface_stage_write(at:u32,sd:SurfaceData,r:SvmResult){
surface_data_write(at+4u,sd);
store_word(at+244u,r.closure_base);
store_word(at+248u,r.count);
store_word(at+252u,r.remaining);
store_word(at+256u,r.runtime_flag);
store_word(at+260u,r.error);
store_word(at+264u,r.lcg_state);
store_word(at+268u,bitcast<u32>(r.N.x));
store_word(at+272u,bitcast<u32>(r.N.y));
store_word(at+276u,bitcast<u32>(r.N.z));
store_word(at+280u,bitcast<u32>(r.emission.x));
store_word(at+284u,bitcast<u32>(r.emission.y));
store_word(at+288u,bitcast<u32>(r.emission.z));
store_word(at+292u,bitcast<u32>(r.transparency.x));
store_word(at+296u,bitcast<u32>(r.transparency.y));
store_word(at+300u,bitcast<u32>(r.transparency.z));
store_word(at,1u);
}
fn surface_stage_read(at:u32)->SurfaceStage{let sd=surface_data_read(at+4u);var r:SvmResult;
r.closure_base=load_word(at+244u);
r.count=load_word(at+248u);
r.remaining=load_word(at+252u);
r.runtime_flag=load_word(at+256u);
r.error=load_word(at+260u);
r.lcg_state=load_word(at+264u);
r.N.x=bitcast<f32>(load_word(at+268u));
r.N.y=bitcast<f32>(load_word(at+272u));
r.N.z=bitcast<f32>(load_word(at+276u));
r.emission.x=bitcast<f32>(load_word(at+280u));
r.emission.y=bitcast<f32>(load_word(at+284u));
r.emission.z=bitcast<f32>(load_word(at+288u));
r.transparency.x=bitcast<f32>(load_word(at+292u));
r.transparency.y=bitcast<f32>(load_word(at+296u));
r.transparency.z=bitcast<f32>(load_word(at+300u));
return SurfaceStage(sd,r);
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from Cycles: selected surface integration, BSDF mixtures, native
// path-state updates and film writes in WGSL. See native/THIRD-PARTY-NOTICES.txt.
fn surf_diffuse(kind:u32)->bool{return kind==CLOSURE_BSDF_DIFFUSE_ID||kind==CLOSURE_BSDF_OREN_NAYAR_ID||kind==CLOSURE_BSDF_TRANSLUCENT_ID;}
fn surf_glossy(kind:u32)->bool{return kind==CLOSURE_BSDF_MICROFACET_GGX_ID;}
fn surf_glass(kind:u32)->bool{return kind==CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID;}
fn surf_refraction(kind:u32)->bool{return kind==CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID;}
fn surf_microfacet(kind:u32)->bool{return surf_glossy(kind)||surf_glass(kind)||surf_refraction(kind);}
fn surf_glossy_direction(c:SvmClosure,wo:vec3<f32>)->bool{return surf_glossy(c.kind)||(surf_glass(c.kind)&&dot(c.N,wo)>0.0);}
fn surf_bsdf(kind:u32)->bool{return surf_diffuse(kind)||surf_microfacet(kind)||kind==CLOSURE_BSDF_TRANSPARENT_ID;}
fn surf_roughness(c:SvmClosure)->f32{if(surf_microfacet(c.kind)){return c.alpha_x*c.alpha_y;}return select(1.0,0.0,c.kind==CLOSURE_BSDF_TRANSPARENT_ID);}
fn surf_bump(sd:SurfaceData,c:SvmClosure,wo:vec3<f32>,is_eval:bool)->f32{
    if(all(c.N==sd.N)){return 1.0;}
    let ns_i=dot(sd.N,wo);let ns_n=dot(sd.N,c.N);let n_i=dot(c.N,wo);let diffuse=surf_diffuse(c.kind);
    if(ns_i*ns_n*n_i<0.0&&(is_eval||diffuse)){return 0.0;}
    if(!diffuse||(sd.shader_flag&SD_USE_BUMP_MAP_CORRECTION)==0u){return 1.0;}
    let ci=abs(ns_i);let cd=abs(ns_n);if(cd>=1.0||ci>=1.0){return 1.0;}if(ci<1e-6){return 0.0;}
    let alpha=clamp(0.125*(1.0/(cd*cd)-1.0),0.0,1.0);
    return 2.0/(1.0+sqrt(1.0+alpha*(1.0/(ci*ci)-1.0)));
}
fn surf_oren(c:SvmClosure,wi:vec3<f32>,wo:vec3<f32>)->vec3<f32>{
    let nl=max(dot(c.N,wo),0.0);if(c.b<=0.0){return vec3<f32>(nl*INV_PI);}
    let nv=max(dot(c.N,wi),0.0);var t=dot(wo,wi)-nl*nv;if(t>0.0){t/=max(nl,nv)+1.1754943508222875e-38;}
    let el=svm_oren_add(c.a*PI,c.b*svm_oren_g(nl));return nl*(vec3<f32>(c.a+c.b*t)+c.multiscatter*(1.0-el));
}
fn surf_evaluate(sd:SurfaceData,c:SvmClosure,wo:vec3<f32>)->vec4<f32>{
    let bump=surf_bump(sd,c,wo,true);if(bump==0.0||c.kind==CLOSURE_BSDF_TRANSPARENT_ID){return vec4<f32>(0);}
    if(surf_microfacet(c.kind)){
//#if SVM_BSDF_MICROFACET else atomicOr(&heap[p(17u)/4u],2u);return vec4<f32>(0);
        let value=ggx_evaluate(c,sd.wi,wo);return vec4<f32>(value.xyz*bump,value.w);
//#endif
    }
    let cosine=select(dot(c.N,wo),-dot(c.N,wo),c.kind==CLOSURE_BSDF_TRANSLUCENT_ID);let pdf=max(cosine,0.0)*INV_PI;
    if(c.kind==CLOSURE_BSDF_OREN_NAYAR_ID){if(cosine>0.0){return vec4<f32>(surf_oren(c,sd.wi,wo)*bump,pdf);}return vec4<f32>(0);}
    return vec4<f32>(vec3<f32>(pdf*bump),pdf);
}
struct SurfaceSample {wo:vec3<f32>,pdf:f32,eval:vec3<f32>,label:u32,diffuse:vec3<f32>,glossy:vec3<f32>,roughness:f32}
fn surf_sample(sd:SurfaceData,r:SvmResult,random:vec3<f32>)->SurfaceSample{
    var out:SurfaceSample;if((r.runtime_flag&SR_BSDF)==0u||r.count==0u){return out;}
    var selected=0u;var rand=random;
    if(r.count>1u){var sum=0.0;for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(surf_bsdf(c.kind)){sum+=c.sample_weight;}}
        let pick=random.z*sum;var partial=0.0;for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(surf_bsdf(c.kind)){
            let next=partial+c.sample_weight;if(pick<next){selected=i;rand.z=(pick-partial)/c.sample_weight;break;}partial=next;}}}
    let c=svm_closure_read(r,selected);if(!surf_bsdf(c.kind)){return out;}
    var value=vec3<f32>(0);
    if(c.kind==CLOSURE_BSDF_TRANSPARENT_ID){out.wo=-sd.wi;out.pdf=1e6;value=vec3<f32>(1e6);out.label=LABEL_TRANSMIT|LABEL_TRANSPARENT;
        if(data_float(D_background_transparent_roughness_squared_threshold)>=0.0){out.label|=LABEL_TRANSMIT_TRANSPARENT;}}
    else if(surf_microfacet(c.kind)){
//#if SVM_BSDF_MICROFACET else atomicOr(&heap[p(17u)/4u],2u);return out;
        out=ggx_sample(sd,c,rand);value=out.eval;
        if((out.label&LABEL_TRANSMIT)!=0u){let threshold=data_float(D_background_transparent_roughness_squared_threshold);
            if(threshold>=0.0&&c.alpha_x*c.alpha_y<=threshold){out.label|=LABEL_TRANSMIT_TRANSPARENT;}}
        else{value*=surf_bump(sd,c,out.wo,false);}
//#endif
    }
    else{
        let transmission=c.kind==CLOSURE_BSDF_TRANSLUCENT_ID;let sampled=surface_cos_hemisphere(select(c.N,-c.N,transmission),random.xy);
        out.wo=sampled.xyz;out.pdf=sampled.w;out.label=LABEL_DIFFUSE|select(LABEL_REFLECT,LABEL_TRANSMIT,transmission);
        let valid=select(dot(sd.Ng,out.wo)>0.0,dot(sd.Ng,out.wo)<0.0,transmission);
        if(valid){value=vec3<f32>(out.pdf);if(c.kind==CLOSURE_BSDF_OREN_NAYAR_ID){value=surf_oren(c,sd.wi,out.wo);}
            if(!transmission){value*=surf_bump(sd,c,out.wo,false);}}
        else{out.pdf=0.0;}
    }
    if(out.pdf==0.0){return out;}
    out.eval=value*c.weight;if(surf_diffuse(c.kind)){out.diffuse=out.eval;}
    if(surf_glossy_direction(c,out.wo)){out.glossy=out.eval;}
    out.roughness=surf_roughness(c);
    if(r.count>1u){var pdf_sum=out.pdf*c.sample_weight;var weight_sum=c.sample_weight;var rough_sum=pdf_sum*out.roughness;
        for(var i=0u;i<r.count;i++){if(i==selected){continue;}let other=svm_closure_read(r,i);if(!surf_bsdf(other.kind)){continue;}
            let e=surf_evaluate(sd,other,out.wo);if(e.w!=0.0){out.eval+=e.xyz*other.weight;if(surf_diffuse(other.kind)){out.diffuse+=e.xyz*other.weight;}if(surf_glossy_direction(other,out.wo)){out.glossy+=e.xyz*other.weight;}
                let w=e.w*other.sample_weight;pdf_sum+=w;rough_sum+=w*surf_roughness(other);}weight_sum+=other.sample_weight;}
        out.roughness=select(0.0,rough_sum/pdf_sum,pdf_sum>0.0);out.pdf=select(0.0,pdf_sum/weight_sum,weight_sum>0.0);
    }
    return out;
}
fn surf_prepare(index:u32,r:ptr<function,SvmResult>){
    let closure_filter=data_word(D_integrator_filter_closures);if((closure_filter&FILTER_CLOSURE_EMISSION)!=0u){(*r).emission=vec3<f32>(0);}
    if((state_byte(S_path_visibility,index)&VIS_CAMERA)==0u){return;}
    if((closure_filter&FILTER_CLOSURE_DIRECT_LIGHT)!=0u){(*r).runtime_flag&=~SR_BSDF_HAS_EVAL;}
    for(var i=0u;i<(*r).count;i++){var c=svm_closure_read(*r,i);
        if((surf_diffuse(c.kind)&&(closure_filter&FILTER_CLOSURE_DIFFUSE)!=0u)||(surf_glossy(c.kind)&&(closure_filter&FILTER_CLOSURE_GLOSSY)!=0u)||
           (surf_refraction(c.kind)&&(closure_filter&FILTER_CLOSURE_TRANSMISSION)!=0u)||
           (surf_glass(c.kind)&&(closure_filter&(FILTER_CLOSURE_GLOSSY|FILTER_CLOSURE_TRANSMISSION))==(FILTER_CLOSURE_GLOSSY|FILTER_CLOSURE_TRANSMISSION))){
            c.kind=CLOSURE_NONE_ID;c.sample_weight=0.0;}
        else if(c.kind==CLOSURE_BSDF_TRANSPARENT_ID&&(closure_filter&FILTER_CLOSURE_TRANSPARENT)!=0u){c.kind=CLOSURE_HOLDOUT_ID;c.sample_weight=0.0;(*r).runtime_flag|=SR_HOLDOUT;}
        svm_closure_write((*r).closure_base+i*SVM_CLOSURE_BYTES,c);
    }
}
fn surf_holdout(sd:SurfaceData,r:ptr<function,SvmResult>)->vec3<f32>{
    var weight=vec3<f32>(0);
    if((sd.object_flag&SD_OBJECT_HOLDOUT_MASK)!=0u){
        if(((*r).runtime_flag&SR_TRANSPARENT)!=0u){weight=vec3<f32>(1)-(*r).transparency;
            for(var i=0u;i<(*r).count;i++){var c=svm_closure_read(*r,i);if(c.kind!=CLOSURE_BSDF_TRANSPARENT_ID){c.kind=NBUILTIN_CLOSURES;svm_closure_write((*r).closure_base+i*SVM_CLOSURE_BYTES,c);}}
            (*r).runtime_flag&=SR_TRANSPARENT|SR_BSDF;
        }else{weight=vec3<f32>(1);}
    }else{for(var i=0u;i<(*r).count;i++){let c=svm_closure_read(*r,i);if(c.kind==CLOSURE_HOLDOUT_ID){weight+=c.weight;}}}
    return weight;
}
fn surf_ray_offset(sd:SurfaceData,direction:vec3<f32>)->vec3<f32>{
    var pos=sd.P;var dir=direction;if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)==0u){pos=surface_object_point(sd,pos,true);dir=surface_object_direction(sd,dir,true);}
    let vertices=native_triangle_vertices(sd.object_id,sd.prim,sd.primitive_type,sd.time);
    let a=vertices.a-pos;let b=vertices.b-pos;let c=vertices.c-pos;
    let u=c_ray_triangle_dot(c_ray_triangle_cross(c+a,c-a),dir);let v=c_ray_triangle_dot(c_ray_triangle_cross(a+b,a-b),dir);let w=c_ray_triangle_dot(c_ray_triangle_cross(b+c,b-c),dir);
    let eps=1.1920928955078125e-7*abs(u+v+w);if(min(u,min(v,w))>=eps||max(u,max(v,w))<=-eps){return sd.P;}
    let amount=vec3<i32>(256.0*sd.Ng);let bits=bitcast<vec3<i32>>(sd.P)+select(amount,-amount,sd.P<vec3<f32>(0));
    return select(bitcast<vec3<f32>>(bits),sd.P+(1.0/65536.0)*sd.Ng,abs(sd.P)<vec3<f32>(1.0/32.0));
}
fn surf_next(index:u32,label:u32,runtime:u32){
    var flag=state_word(S_path_flag,index);let offset=state_half(S_path_rng_offset,index)+RNG_BOUNCE;state_u16(S_path_rng_offset,index,offset);
    if((label&LABEL_TRANSPARENT)!=0u){let n=state_half(S_path_transparent_bounce,index)+1u;flag|=PATH_RAY_TRANSPARENT;
        if(n>=data_word(D_integrator_transparent_max_bounce)){flag|=PATH_RAY_TERMINATE_ON_NEXT_SURFACE;}
        state_u16(S_path_transparent_bounce,index,n);state_u32(S_path_flag,index,flag);return;}
    let bounce=state_half(S_path_bounce,index)+1u;if(bounce>=data_word(D_integrator_max_bounce)){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;}
    flag&=~(PATH_RAY_REFLECT|PATH_RAY_SINGULAR|PATH_RAY_TRANSPARENT|PATH_RAY_IMPORTANCE_BAKE|PATH_RAY_MIS_SKIP|PATH_RAY_MIS_HAD_TRANSMISSION);
    var visibility=0u;
    if((label&LABEL_DIFFUSE)!=0u){visibility=PATH_RAY_VISIBILITY_DIFFUSE;flag|=PATH_RAY_DIFFUSE_ANCESTOR;}
    else{visibility=PATH_RAY_VISIBILITY_GLOSSY;if((label&LABEL_SINGULAR)!=0u){flag|=PATH_RAY_SINGULAR|PATH_RAY_MIS_SKIP;}}
    if((label&LABEL_REFLECT)!=0u){flag|=PATH_RAY_REFLECT;flag&=~PATH_RAY_TRANSPARENT_BACKGROUND;
        if((label&LABEL_DIFFUSE)!=0u){let n=state_half(S_path_diffuse_bounce,index)+1u;state_u16(S_path_diffuse_bounce,index,n);if(n>=data_word(D_integrator_max_diffuse_bounce)){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;}}
        else{let n=state_half(S_path_glossy_bounce,index)+1u;state_u16(S_path_glossy_bounce,index,n);if(n>=data_word(D_integrator_max_glossy_bounce)){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;}}}
    else{visibility|=PATH_RAY_VISIBILITY_TRANSMIT;if((label&LABEL_TRANSMIT_TRANSPARENT)==0u){flag&=~PATH_RAY_TRANSPARENT_BACKGROUND;}
        let n=state_half(S_path_transmission_bounce,index)+1u;state_u16(S_path_transmission_bounce,index,n);if(n>=data_word(D_integrator_max_transmission_bounce)){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;}}
    if((runtime&SR_BSDF_HAS_TRANSMISSION)!=0u){flag|=PATH_RAY_MIS_HAD_TRANSMISSION;}
    if((flag&PATH_RAY_ANY_PASS)==0u&&(flag&PATH_RAY_TRANSPARENT_BACKGROUND)==0u){flag|=PATH_RAY_SURFACE_PASS;}
    state_u8(S_path_visibility,index,visibility);state_u32(S_path_flag,index,flag);state_u16(S_path_bounce,index,bounce);
}
fn surf_finish(index:u32,continues:bool){
    let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[counter/4u+DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE],1u);
    if(continues){atomicAdd(&heap[counter/4u+K_CLOSEST],1u);state_u16(S_path_queued_kernel,index,K_CLOSEST);}else{volume_write_depth(index);state_u16(S_path_queued_kernel,index,0u);}
}
fn surf_emission(index:u32,base:u32,sd:SurfaceData,emission:vec3<f32>,mis_weight:f32){
    let throughput=heap_vec3(state_address(S_path_throughput,index,12u));var value=throughput*emission*mis_weight;
    value=select(value,vec3<f32>(0),(bitcast<vec3<u32>>(value)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
    let bounce=state_half(S_path_bounce,index);let limit=select(data_float(D_integrator_sample_clamp_direct),data_float(D_integrator_sample_clamp_indirect),i32(bounce)-1>0);
    let total=abs(value.x)+abs(value.y)+abs(value.z);if(total>limit){value*=limit/total;}
    volume_guidance_pass(index,base,value);
    let flags=data_word(D_film_light_pass_flag);if((flags&MASK_COMBINED)!=0u){film_rgb(base,data_word(D_film_pass_combined),value);}
    if(data_word(D_film_pass_adaptive_aux_buffer)!=PASS_NONE&&(countOneBits(state_word(S_path_sample,index)&0xaaaaaaaau)&1u)!=0u){film_rgb(base,data_word(D_film_pass_adaptive_aux_buffer),value*2.0);}
    if(flags==0u){return;}let flag=state_word(S_path_flag,index);let group=load_word(table_at(G_objects,sd.object_id,SIZE_objects)+O_lightgroup);
    if(group!=NONE&&data_word(D_film_pass_lightgroup)!=PASS_NONE){film_rgb(base,data_word(D_film_pass_lightgroup)+3u*group,value);}
    if((flag&PATH_RAY_ANY_PASS)==0u){film_rgb(base,data_word(D_film_pass_emission),value);}
    else if(p(20u)!=0u&&(flag&PATH_RAY_SURFACE_PASS)!=0u){let diffuse=heap_vec3(state_address(S_path_pass_diffuse_weight,index,12u));let glossy=heap_vec3(state_address(S_path_pass_glossy_weight,index,12u));
        film_rgb(base,select(data_word(D_film_pass_glossy_indirect),data_word(D_film_pass_glossy_direct),bounce==1u),glossy*value);
        film_rgb(base,select(data_word(D_film_pass_transmission_indirect),data_word(D_film_pass_transmission_direct),bounce==1u),(vec3<f32>(1)-diffuse-glossy)*value);
        film_rgb(base,select(data_word(D_film_pass_diffuse_indirect),data_word(D_film_pass_diffuse_direct),bounce==1u),diffuse*value);}
}
// Preparation validates inputs and evaluates closures without changing path or film state.
@compute @workgroup_size(64) fn prepare_surface(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}store_word(p(24u)+id.x*SURFACE_STAGE_BYTES,0u);var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}if(state_half(S_path_queued_kernel,index)!=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){fail();return;}
    let flag=state_word(S_path_flag,index);if((flag&SURFACE_UNSUPPORTED_FLAGS)!=0u){fail();return;}
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_path_render_pixel_index,index);if(pixel>=p(11u)/stride){fail();return;}let base=p(4u)+pixel*stride*4u;
    var sd=surface_cached(id.x);if(sd.error!=0u){return;}let object=table_at(G_objects,sd.object_id,SIZE_objects);
    if(heap_float(object+O_shadow_terminator_shading_offset)>1.0){fail();return;}
    if(data_word(D_integrator_use_direct_light)!=0u&&(sd.shader&SHADER_SMOOTH_NORMAL)!=0u&&heap_float(object+O_shadow_terminator_geometry_offset)>0.0){fail();return;}
    let group=load_word(object+O_lightgroup);let go=data_word(D_film_pass_lightgroup);if(group!=NONE&&go!=PASS_NONE&&(group>=stride/3u||go>stride-3u*(group+1u))){fail();return;}
    if(volume_enabled()&&(sd.shader_flag&VS_HAS_VOLUME)!=0u){if(volume_stack_count(index)==NONE){return;}}
    if((sd.shader_flag&SD_HAS_ONLY_VOLUME)!=0u){var empty:SvmResult;surface_stage_write(p(24u)+id.x*SURFACE_STAGE_BYTES,sd,empty);return;}
    var r=svm_evaluate(index,sd,data_word(D_max_closures),p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES);if(r.error!=0u){return;}sd.N=r.N;surf_prepare(index,&r);
    surface_stage_write(p(24u)+id.x*SURFACE_STAGE_BYTES,sd,r);
}
// Direct lighting runs in two dispatches. shade_surface_light samples the lamp or
// emissive triangle and the forward-hit triangle MIS weight (state 1 -> 3);
// connect_surface_light applies holdout, emission, roulette and the sampled
// light's BSDF/MIS/shadow setup (state 3 -> 2). Combining emitter sampling with
// BSDF evaluation caused multi-minute, multi-GB driver compilation (and lost the
// Pixel's device); records remain GPU-private and native scheduling is unchanged.
@compute @workgroup_size(1) fn shade_surface_light(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)||(p(28u)!=0u&&load_word(p(17u))!=0u)){return;}
    let at=p(24u)+id.x*SURFACE_STAGE_BYTES;if(load_word(at)!=1u){return;}store_word(at,0u);
    var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    let stage=surface_stage_read(at);let sd=stage.sd;let r=stage.r;
    var weight=1.0;if((r.runtime_flag&SR_EMISSION)!=0u){weight=triangle_light_forward_weight(index,sd);}
    var ls:NativeLightSample;
    let flag=state_word(S_path_flag,index);
    if(data_word(D_integrator_use_direct_light)!=0u&&(r.runtime_flag&SR_BSDF_HAS_EVAL)!=0u&&
       (sd.shader_flag&SD_HAS_ONLY_VOLUME)==0u&&(flag&PATH_RAY_TERMINATE_ON_NEXT_SURFACE)==0u&&
       heap_float(state_address(S_path_continuation_probability,index,4u))!=0.0){ls=light_sample_native(index,sd,r);}
    if(load_word(p(17u))!=0u){return;}
    store_word(at+304u,bitcast<u32>(weight));light_sample_write(at+308u,ls);store_word(at,3u);
}
// Size 1 retains the verified Linux sampling layout; broader tuning remains open.
@compute @workgroup_size(1) fn connect_surface_light(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)||(p(28u)!=0u&&load_word(p(17u))!=0u)){return;}let at=p(24u)+id.x*SURFACE_STAGE_BYTES;if(load_word(at)!=3u){return;}store_word(at,0u);
    var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    let stage=surface_stage_read(at);var sd=stage.sd;var r=stage.r;
    let flag=state_word(S_path_flag,index);let stride=data_word(D_film_pass_stride);
    let pixel=state_word(S_path_render_pixel_index,index);let base=p(4u)+pixel*stride*4u;
    if(((r.runtime_flag&SR_HOLDOUT)!=0u||(sd.object_flag&SD_OBJECT_HOLDOUT_MASK)!=0u)&&(flag&PATH_RAY_TRANSPARENT_BACKGROUND)!=0u){
        let weight=surf_holdout(sd,&r);let contribution=weight*heap_vec3(state_address(S_path_throughput,index,12u));
        if((data_word(D_film_light_pass_flag)&MASK_COMBINED)!=0u){film_add(base+(data_word(D_film_pass_combined)+3u)*4u,(contribution.x+contribution.y+contribution.z)*(1.0/3.0));}
        volume_transparent_pass(index,base,(contribution.x+contribution.y+contribution.z)*(1.0/3.0));
        if(all(weight==vec3<f32>(1))){surf_finish(index,false);return;}
    }
    if((r.runtime_flag&SR_EMISSION)!=0u){surf_emission(index,base,sd,select(vec3<f32>(0),r.emission,abs(dot(sd.Ng,sd.wi))>0.0),heap_float(at+304u));}
    let probability=select(heap_float(state_address(S_path_continuation_probability,index,4u)),0.0,(flag&PATH_RAY_TERMINATE_ON_NEXT_SURFACE)!=0u);
    if(probability==0.0){surf_finish(index,false);return;}
    var throughput=heap_vec3(state_address(S_path_throughput,index,12u));if(probability!=1.0){throughput/=probability;state_vec3(S_path_throughput,index,throughput);}
    if((sd.shader_flag&SD_HAS_ONLY_VOLUME)!=0u){
        let bounds=state_half(S_path_volume_bounds_bounce,index)+1u;state_u16(S_path_volume_bounds_bounce,index,bounds);
        if(bounds>VS_BOUNDS_MAX){surf_finish(index,false);return;}
        state_u16(S_path_rng_offset,index,state_half(S_path_rng_offset,index)+RNG_BOUNCE);
        let t=sd.ray_length;state_f32(S_ray_tmin,index,select(bitcast<f32>(bitcast<u32>(t)+1u),1.1754943508222875e-38,t==0.0));
        volume_stack_enter_exit(index,sd.object_id,sd.shader,(sd.runtime_flag&SR_BACKFACING)!=0u);surf_finish(index,true);return;
    }
    surface_direct_light(index,id.x,sd,r,light_sample_read(at+308u));if(load_word(p(17u))!=0u){return;}
    // Holdout may change closure metadata; retain it with the same GPU record.
    surface_stage_write(at,sd,r);store_word(at,2u);
}
// Lighting and continuation share GPU records, with no native queue transition.
// The continuation runs only after successful lighting/roulette preparation.
@compute @workgroup_size(1) fn shade_surface(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)||(p(28u)!=0u&&load_word(p(17u))!=0u)){return;}let at=p(24u)+id.x*SURFACE_STAGE_BYTES;if(load_word(at)!=2u){return;}
    var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    let stage=surface_stage_read(at);let sd=stage.sd;let r=stage.r;
    let throughput=heap_vec3(state_address(S_path_throughput,index,12u));
    let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_SURFACE_BSDF).xyz;
    let sampled=surf_sample(sd,r,random);if(sampled.pdf==0.0||all(sampled.eval==vec3<f32>(0))){surf_finish(index,false);return;}
    if((sampled.label&LABEL_TRANSPARENT)!=0u){let t=sd.ray_length;state_f32(S_ray_tmin,index,select(bitcast<f32>(bitcast<u32>(t)+1u),1.1754943508222875e-38,t==0.0));}
    else{let direction=surface_normalize(sampled.wo);state_vec3(S_ray_P,index,surf_ray_offset(sd,direction));state_vec3(S_ray_D,index,direction);
        state_f32(S_ray_tmin,index,0.0);state_f32(S_ray_tmax,index,3.4028234663852886e38);state_f32(S_ray_dP,index,sd.dP);
        let dd=heap_float(state_address(S_ray_dD,index,4u));state_f32(S_ray_dD,index,select(dd,max(dd,sqrt(sampled.roughness)),sampled.roughness>0.0));}
    state_vec3(S_path_throughput,index,throughput*(sampled.eval/sampled.pdf));
    if(p(20u)!=0u&&state_half(S_path_bounce,index)==0u){state_vec3(S_path_pass_diffuse_weight,index,svm_divide(sampled.diffuse,sampled.eval));state_vec3(S_path_pass_glossy_weight,index,svm_divide(sampled.glossy,sampled.eval));}
    if((sampled.label&LABEL_TRANSPARENT)==0u){state_f32(S_path_mis_ray_pdf,index,sampled.pdf);state_vec3(S_path_mis_origin_n,index,sd.N);
        state_f32(S_path_min_ray_pdf,index,min(sampled.pdf,heap_float(state_address(S_path_min_ray_pdf,index,4u))));}
    surf_next(index,sampled.label,r.runtime_flag);
    if((sampled.label&LABEL_TRANSMIT)!=0u){volume_stack_enter_exit(index,sd.object_id,sd.shader,(sd.runtime_flag&SR_BACKFACING)!=0u);}
    surf_finish(index,true);
}
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
// Cycles sample_cos_hemisphere/make_orthonormals through the same rounding
// adapters as the GGX disk: near the disk edge 1-len_squared cancels, so
// driver-approximate trig/sqrt or contracted products change the direction.
fn surface_cos_hemisphere(n:vec3<f32>,r:vec2<f32>)->vec4<f32>{
    let d=ggx_disk_sample(r);let c=ggx_unit_sqrt(rounded_add(d.x*d.x,d.y*d.y));
    var t=vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x);if(n.x==n.y&&n.x==n.z){t=vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x);}
    t=surface_native_normalize(t);let b=rounded_cross(n,t);
    return vec4<f32>(rounded_add3(rounded_add3(d.x*t,d.y*b),c*n),c*INV_PI);
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
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native spot attenuation and area rectangle/ellipse sampling and evaluation.
// Adapted from the reviewed Cycles kernel/light/{spot,area,common}.h snapshot.
const EMITTER_PI:f32=3.141592653589793;
const EMITTER_INV_PI:f32=0.3183098861837907;
fn emitter_normalize_len(v:vec3<f32>)->vec4<f32>{
    let t=rounded_sqrt(rounded_dot(v,v));if(t==0.0){return vec4<f32>(0);}
    return vec4<f32>(rounded_divide(v.x,t),rounded_divide(v.y,t),rounded_divide(v.z,t),t);
}
fn emitter_sphere_hit(origin:vec3<f32>,direction:vec3<f32>,position:vec3<f32>,radius:f32)->f32{
    let delta=position-origin;let d2=dot(delta,delta);let r2=radius*radius;let cosine=dot(delta,direction);
    if(d2>r2&&cosine<0.0){return -1.0;}let perpendicular=delta-cosine*direction;let s2=dot(perpendicular,perpendicular);
    if(s2>r2){return -1.0;}let root=sqrt(max(r2-s2,0.0));let t=cosine-select(-root,root,d2-r2>=0.0);
    return select(-1.0,t,t>0.0&&t<bitcast<f32>(0x7f7fffffu));
}
fn spot_attenuation(light:u32,direction:vec3<f32>)->f32{
    let object=table_at(G_objects,load_word(light+L_object_id),SIZE_objects);let a=object+O_itfm;
    let x=heap_vec4(a);let y=heap_vec4(a+16u);let z=heap_vec4(a+32u);
    let transformed=rounded_normalize(vec3<f32>(dot(x.xyz,direction),dot(y.xyz,direction),dot(z.xyz,direction)));
    let f=(-transformed.z-heap_float(light+L_spot_cos))*heap_float(light+L_spot_smooth);
    if(f<=0.0){return 0.0;}if(f>=1.0){return 1.0;}let ff=f*f;return 3.0*ff-2.0*ff*f;
}
fn emitter_disk(random:vec2<f32>)->vec2<f32>{
    let a=rounded_add(2.0*random.x,-1.0);let b=rounded_add(2.0*random.y,-1.0);if(a==0.0&&b==0.0){return vec2<f32>(0);}
    var radius:f32;var phi:f32;if(a*a>b*b){radius=a;phi=(EMITTER_PI*0.25)*rounded_divide(b,a);}
    else{radius=b;phi=rounded_add(EMITTER_PI*0.5,-(EMITTER_PI*0.25)*rounded_divide(a,b));}
    let sc=native_disk_sincos(phi);return radius*vec2<f32>(sc.y,sc.x);
}
// Original quarter-turn rotation retains the float32 cosine of pi/2.
fn emitter_quarter_turn(p:vec3<f32>,axis:vec3<f32>)->vec3<f32>{
    let c= -4.371138828673793e-8;let s=1.0;let m=1.0-c;
    return vec3<f32>((c+m*axis.x*axis.x)*p.x+(m*axis.x*axis.y-axis.z*s)*p.y+(m*axis.x*axis.z+axis.y*s)*p.z,
      (m*axis.x*axis.y+axis.z*s)*p.x+(c+m*axis.y*axis.y)*p.y+(m*axis.y*axis.z-axis.x*s)*p.z,
      (m*axis.x*axis.z-axis.y*s)*p.x+(m*axis.y*axis.z+axis.x*s)*p.y+(c+m*axis.z*axis.z)*p.z);
}
// Original bounded arcsine adapter. The binomial Taylor series is evaluated
// with paired binary32 values after reducing its argument to [0, 1/2].
// Seventeen terms bound the truncation tail below 2e-13.
fn emitter_asin(value:f32)->f32{
    let a=abs(clamp(value,-1.0,1.0));let reduce=a>0.5;var z=vec2<f32>(a,0);var x=trig_product(a,a);
    if(reduce){x=vec2<f32>((1.0-a)*0.5,0);let root=rounded_sqrt(x.x);z=vec2<f32>(root,0);
      if(root>0.0){let product=trig_product(root,root);z.y=rounded_divide(rounded_add(rounded_add(x.x,-product.x),-product.y),2.0*root);}}
    var polynomial=vec2<f32>(0.0042409072630107403,-1.6933137720281428e-10);
    polynomial=trig_coeff(vec2<f32>(0.0046601435169577599,-3.00426636972735e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.005153309553861618,1.284582861538591e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0057400376535952091,1.7246714344731083e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0064472104422748089,-1.30385160446167e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0073125259950757027,-1.2147685755854067e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0083903353661298752,4.4348694029308498e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0097616091370582581,3.9213582089072777e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.011551801115274429,-2.1913472343893612e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.013964843936264515,-1.8626451492309571e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.017352763563394547,8.5968237656813406e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.022372158244252205,8.4665688601407135e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0303819440305233,4.1392114427354598e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0446428582072258,-1.064368656703404e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.075000002980232239,-2.9802322387695314e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.1666666716337204,-4.9670537312825518e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(1,0),trig_mul(polynomial,x));
    var result=trig_mul(z,polynomial);if(reduce){result=trig_coeff(vec2<f32>(1.5707963705062866,-4.3711390001862426e-8),-2.0*result);}
    let answer=rounded_add(result.x,result.y);return select(answer,-answer,value<0.0);
}
struct AreaRegion {P:vec3<f32>,U:vec3<f32>,V:vec3<f32>,len_u:f32,len_v:f32,rectangle:bool,valid:bool}
fn area_region(light:u32,P:vec3<f32>)->AreaRegion{
    var r=AreaRegion(heap_vec3(light+L_co),heap_vec3(light+L_area_axis_u),heap_vec3(light+L_area_axis_v),heap_float(light+L_area_len_u),heap_float(light+L_area_len_v),heap_float(light+L_area_invarea)>0.0,true);
    if(heap_float(light+L_area_normalize_spread)<=0.0){return r;}
    let N=heap_vec3(light+L_area_dir);let t=dot(N,P-r.P);let closest=P-t*N;
    let spread=t*heap_float(light+L_area_tan_half_spread);let su=dot(r.U,closest-r.P);let sv=dot(r.V,closest-r.P);
    let round= !r.rectangle&&r.len_u==r.len_v;var sample_spread=spread==0.0;
    if(round&&!sample_spread){
        let distance=length(vec2<f32>(su,sv));let radius=r.len_u*0.5;
        if(distance>=radius+spread){r.valid=false;return r;}
        sample_spread=distance<=abs(radius-spread)&&spread<radius;
        if(distance>abs(radius-spread)){
            let len_u=radius+spread-distance;var len_v=2.0*min(radius,spread);
            if(abs(radius*radius-spread*spread)<distance*distance){let x=distance+(spread*spread-radius*radius)/distance;len_v=sqrt(max(4.0*spread*spread-x*x,0.0));}
            let rectangle_area=len_u*len_v;let circle_area=EMITTER_PI*radius*radius;let spread_area=EMITTER_PI*spread*spread;
            if(rectangle_area<min(circle_area,spread_area)){
                r.rectangle=true;r.U=rounded_normalize(r.P-closest);r.V=emitter_quarter_turn(r.U,N);r.len_u=len_u;r.len_v=len_v;
                r.P=0.5*(r.P+closest+r.U*(spread-radius));return r;
            }sample_spread=spread_area<circle_area;
        }
    }else if(!round&&!sample_spread){
        let min_u=max(su-spread,-r.len_u*0.5);let max_u=min(su+spread,r.len_u*0.5);
        let min_v=max(sv-spread,-r.len_v*0.5);let max_v=min(sv+spread,r.len_v*0.5);
        if(min_u>=max_u||min_v>=max_v){r.valid=false;return r;}
        let lu=max_u-min_u;let lv=max_v-min_v;let rectangle_area=lu*lv;
        let ellipse_area=select((EMITTER_PI*0.25)*r.len_u*r.len_v,bitcast<f32>(0x7f7fffffu),r.rectangle);let spread_area=EMITTER_PI*spread*spread;
        if(r.rectangle||rectangle_area<min(ellipse_area,spread_area)){
            r.rectangle=true;r.len_u=lu;r.len_v=lv;r.P+=r.U*(0.5*(min_u+max_u))+r.V*(0.5*(min_v+max_v));return r;
        }r.rectangle=false;sample_spread=spread_area<ellipse_area;
    }
    if(sample_spread){r.rectangle=false;r.P+=r.U*su+r.V*sv;r.len_u=2.0*spread;r.len_v=2.0*spread;}
    return r;
}
struct AreaRect {P:vec3<f32>,pdf:f32}
fn area_rectangle(P:vec3<f32>,r:AreaRegion,random:vec2<f32>,sample_coord:bool)->AreaRect{
    var out=AreaRect(r.P,0.0);var Z=rounded_cross(r.U,r.V);let direction=r.P-P;var z0=rounded_dot(direction,Z);if(z0>0.0){Z= -Z;z0= -z0;}
    let xc=rounded_dot(direction,r.U);let yc=rounded_dot(direction,r.V);let x0=rounded_add(xc,-0.5*r.len_u);let x1=rounded_add(xc,0.5*r.len_u);let y0=rounded_add(yc,-0.5*r.len_v);let y1=rounded_add(yc,0.5*r.len_v);
    var nz=vec4<f32>(-y0,x1,y1,-x0);for(var i=0u;i<4u;i++){nz[i]=rounded_divide(nz[i],rounded_sqrt(rounded_add(nz[i]*nz[i],z0*z0)));}
    let g0=emitter_asin(-nz.x*nz.y);let g1=emitter_asin(-nz.y*nz.z);let g2=emitter_asin(-nz.z*nz.w);let g3=emitter_asin(-nz.w*nz.x);
    let S= -rounded_add(rounded_add(rounded_add(g0,g1),g2),g3);
    if(sample_coord){
        let b0=nz.x;let b1=nz.z;let au=rounded_add(rounded_add(random.x*S,g2),g3);let sc=native_disk_sincos(au);var fu=0.0;if(sc.x!=0.0){fu=rounded_divide(rounded_add(sc.y*b0,b1),sc.x);}
        let inverse=rounded_divide(1.0,rounded_sqrt(rounded_add(fu*fu,b0*b0)));let cu=clamp(bitcast<f32>((bitcast<u32>(inverse)&0x7fffffffu)|(bitcast<u32>(fu)&0x80000000u)),-1.0,1.0);
        let xu=clamp(rounded_divide(-(cu*z0),max(rounded_sqrt(max(rounded_add(1.0,-cu*cu),0.0)),1e-7)),x0,x1);let d2=rounded_add(xu*xu,z0*z0);
        let h0=rounded_divide(y0,rounded_sqrt(rounded_add(d2,y0*y0)));let h1=rounded_divide(y1,rounded_sqrt(rounded_add(d2,y1*y1)));let hv=rounded_add(h0,random.y*rounded_add(h1,-h0));let hv2=hv*hv;
        var yv=y1;if(hv2<1.0-1e-6){yv=hv*rounded_sqrt(rounded_divide(d2,rounded_add(1.0,-hv2)));}out.P=rounded_add3(rounded_add3(rounded_add3(P,xu*r.U),yv*r.V),z0*Z);
    }
    if(S<1e-5||min(min(nz.x*nz.x,nz.y*nz.y),min(nz.z*nz.z,nz.w*nz.w))>0.99999){
        let t=rounded_sqrt(rounded_dot(direction,direction));let denom=z0*r.len_u*r.len_v;out.pdf=select(0.0,rounded_divide(-t*t*t,denom),denom!=0.0);
    }else{out.pdf=rounded_divide(1.0,S);}return out;
}
struct AreaEval {P:vec3<f32>,D:vec3<f32>,t:f32,pdf:f32,eval_fac:f32,valid:bool}
fn area_evaluate(light:u32,origin:vec3<f32>,direction:vec3<f32>,distance:f32,random:vec2<f32>,sample_coord:bool)->AreaEval{
    var out:AreaEval;let N=heap_vec3(light+L_area_dir);let center=heap_vec3(light+L_co);
    if(sample_coord&&dot(center-origin,N)>0.0){return out;}
    let region=area_region(light,origin);if(!region.valid){return out;}
    out.P=region.P;let spread=heap_float(light+L_area_tan_half_spread);let normalization=heap_float(light+L_area_normalize_spread);
    if(region.rectangle){let rect=area_rectangle(origin,region,random,sample_coord);out.P=rect.P;out.pdf=rect.pdf;}
    else if(spread==0.0){out.pdf=1.0;}
    else{if(sample_coord){let disk=emitter_disk(random);out.P+=region.U*(region.len_u*0.5*disk.x)+region.V*(region.len_v*0.5*disk.y);}
        out.pdf=(4.0*EMITTER_INV_PI)/(region.len_u*region.len_v);}
    out.D=direction;out.t=distance;if(sample_coord){let ray=emitter_normalize_len(out.P-origin);out.D=ray.xyz;out.t=ray.w;}
    out.eval_fac=EMITTER_INV_PI*abs(heap_float(light+L_area_invarea));
    if(normalization>0.0){let tangent=length(cross(-out.D,N))/dot(-out.D,N);var attenuation=max((spread-tangent)*normalization,0.0);
        if(spread==0.0){attenuation=select(EMITTER_PI,0.0,tangent>1e-5);}out.eval_fac*=attenuation;}
    if(!region.rectangle&&spread>0.0){let cosine=dot(N,-out.D);out.pdf*=select(0.0,out.t*out.t/cosine,cosine>0.0);}
    if(out.eval_fac<=0.0){return out;}
    if(sample_coord&&normalization>0.0){let plane=out.P-center;let u=dot(plane,heap_vec3(light+L_area_axis_u));let v=dot(plane,heap_vec3(light+L_area_axis_v));
        let eu=(0.5+1e-7)*heap_float(light+L_area_len_u)+1e-6;let ev=(0.5+1e-7)*heap_float(light+L_area_len_v)+1e-6;
        if(heap_float(light+L_area_invarea)<0.0){if((u/eu)*(u/eu)+(v/ev)*(v/ev)>1.0){return out;}}
        else if(abs(u)>eu||abs(v)>ev){return out;}
    }out.valid=true;return out;
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2004 NVIDIA Corporation
// SPDX-FileCopyrightText: 2008-2014 Larry Gritz
// SPDX-FileCopyrightText: 2009-2014 Sony Pictures Imageworks Inc., et al.
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Native triangle solid-angle/area sampling and forward PDF, adapted from the
// pinned kernel/light/triangle.h and util/math_fast.h. Flat CDF selection only;
// light trees, volume sampling and nonconstant emitter evaluation remain open.
// Triangle-light-private motion arithmetic. The shared motion helpers stay
// byte-identical to the validated baseline: any change to them alters every
// renderer module, and the Intel Mesa 25.0.7 backend compiler intermittently
// crashes (brw_opt_combine_constants) compiling the transparent-shadow entry
// for each tested perturbation (native/work/b1 compile probes). These copies keep
// the native rounding needed by emitter sampling: reciprocal quaternion
// normalization, acos subtraction residual and rounded transform composition.
fn triangle_light_acos(value:f32)->f32{
    let absolute=abs(clamp(value,-1.0,1.0));let high=rounded_add(1.0,-absolute);
    let low=rounded_add(rounded_add(1.0,-high),-absolute);
    let x=vec2<f32>(high*.5,low*.5);let root=rounded_sqrt(x.x);
    if(root==0.0){return select(0.0,3.141592653589793,value<0.0);}
    let product=trig_product(root,root);
    let residual=rounded_add(rounded_add(x.x,-product.x),rounded_add(x.y,-product.y));
    let z=vec2<f32>(root,rounded_divide(residual,2.0*root));
    var polynomial=vec2<f32>(0.0017680810997262597,2.0789158589051837e-11);
    polynomial=trig_coeff(vec2<f32>(0.0018622264033183455,3.0847819958157441e-12),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0019650335889309645,2.7346319219495596e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0020776609890162945,4.3501873110696199e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0022014740388840437,-6.5173905335058624e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0023380918428301811,4.9281793923494632e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0024894487578421831,-7.9595299535412112e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0026578705292195082,1.0898778190701619e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0028461783658713102,3.5237631822865323e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0030578216537833214,-4.5252907324155878e-12),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0032970595639199018,-6.0446416969006656e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0035692052915692329,1.0225670180399882e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0038809645920991898,-3.3261520707844605e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0042409072630107403,-1.6933137704511214e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0046601435169577599,-3.004266366929409e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.005153309553861618,1.2845828654267644e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0057400376535952091,1.7246714473229119e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0064472104422748089,-1.3038516013391677e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0073125259950757027,-1.2147685721913826e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0083903353661298752,4.4348693988005561e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0097616091370582581,3.9213582034292038e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.011551801115274429,-2.1913472425527658e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.013964843936264515,-1.8626451422920631e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.017352763563394547,8.5968237736877562e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.022372158244252205,8.4665688696028418e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0303819440305233,4.139211438880519e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0446428582072258,-1.0643686557121335e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.075000002980232239,-2.9802322415450888e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.1666666716337204,-4.9670537405344106e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(1,0),trig_mul(polynomial,x));
    let result=2.0*trig_mul(z,polynomial);
    // Retain the subtraction residual before the final rounding. Rounding the
    // high subtraction first can move acos by one ULP and amplify triangle samples.
    if(value<0.0){let reduced=trig_coeff(vec2<f32>(3.1415927410125732,-8.742278000372485e-8),-result);return rounded_add(reduced.x,reduced.y);}
    return rounded_add(result.x,result.y);
}
fn triangle_light_normalize4(a:vec4<f32>)->vec4<f32>{
    let norm=rounded_sqrt(motion_dot4(a,a));
    // Native float4/scalar division multiplies by the reciprocal, including SSE.
    return a*rounded_divide(1.0,norm);
}
fn triangle_light_rotation(a:vec4<f32>,b:vec4<f32>,t:f32)->vec4<f32>{
    let costheta=motion_dot4(a,b);
    if(costheta>0.9995){return triangle_light_normalize4(motion_add4((1.0-t)*a,t*b));}
    let theta=triangle_light_acos(costheta);let perpendicular=triangle_light_normalize4(motion_add4(b,-a*costheta));
    let angle=theta*t;let sc=motion_sincos(angle);return motion_add4(a*sc.y,perpendicular*sc.x);
}
fn triangle_light_compose(q:vec4<f32>,y:vec4<f32>,z:vec4<f32>,w:vec4<f32>)->NativeTransform{
    let q0=1.4142135623730951*q.w;let q1=1.4142135623730951*q.x;
    let q2=1.4142135623730951*q.y;let q3=1.4142135623730951*q.z;
    let qda=q0*q1;let qdb=q0*q2;let qdc=q0*q3;let qaa=q1*q1;let qab=q1*q2;
    let qac=q1*q3;let qbb=q2*q2;let qbc=q2*q3;let qcc=q3*q3;
    // Preserve native multiply/add rounding when composing a sampled transform.
    // An ULP of vertex drift is amplified by grazing emitter solid angles.
    let rx=vec3<f32>(rounded_add(rounded_add(1.0,-qbb),-qcc),rounded_add(-qdc,qab),rounded_add(qdb,qac));
    let ry=vec3<f32>(rounded_add(qdc,qab),rounded_add(rounded_add(1.0,-qaa),-qcc),rounded_add(-qda,qbc));
    let rz=vec3<f32>(rounded_add(-qdb,qac),rounded_add(qda,qbc),rounded_add(rounded_add(1.0,-qaa),-qbb));
    let sx=vec3<f32>(y.w,z.z,w.y);let sy=vec3<f32>(z.x,z.w,w.z);let sz=vec3<f32>(z.y,w.x,w.w);
    return NativeTransform(vec4<f32>(rounded_dot(rx,sx),rounded_dot(rx,sy),rounded_dot(rx,sz),y.x),
                           vec4<f32>(rounded_dot(ry,sx),rounded_dot(ry,sy),rounded_dot(ry,sz),y.y),
                           vec4<f32>(rounded_dot(rz,sx),rounded_dot(rz,sy),rounded_dot(rz,sz),y.z));
}
fn triangle_light_object(object:u32,time:f32)->NativeTransform{
    let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));let at=table_at(G_objects,object,SIZE_objects);
    if((flags&SD_OBJECT_MOTION)==0u){return motion_transform_load(at+O_tfm);}
    // Removed by scene specialization when no object has transform motion.
//#if MOTION_OBJECT else atomicOr(&heap[p(17u)/4u],2u);return NativeTransform();
    let first=load_word(at+O_motion_offset);let steps=load_half(at+O_num_tfm_steps);let count=table_count(G_object_motion);
    if(first>count||steps>count-first){fail();return NativeTransform();}
    if(steps<2u||!(time>=0.0&&time<=1.0)){fail();return NativeTransform();}
    let maximum=steps-1u;let step=min(u32(time*f32(maximum)),maximum-1u);let t=time*f32(maximum)-f32(step);
    let a=table_at(G_object_motion,first+step,MOTION_SIZE);let b=table_at(G_object_motion,first+step+1u,MOTION_SIZE);
    let rotation=triangle_light_rotation(heap_vec4(a+MOTION_X),heap_vec4(b+MOTION_X),t);
    let translation=motion_add4((1.0-t)*heap_vec4(a+MOTION_Y),t*heap_vec4(b+MOTION_Y));
    let zz=motion_add4((1.0-t)*heap_vec4(a+MOTION_Z),t*heap_vec4(b+MOTION_Z));
    let ww=motion_add4((1.0-t)*heap_vec4(a+MOTION_W),t*heap_vec4(b+MOTION_W));
    return triangle_light_compose(rotation,translation,zz,ww);
//#endif
}
fn triangle_light_atan2(y:f32,x:f32)->f32{
    let a=abs(x);let b=abs(y);var k=0.0;
    if(b!=0.0){k=select(select(rounded_divide(b,a),rounded_divide(a,b),b>a),1.0,a==b);}
    let s=rounded_add(1.0,-rounded_add(1.0,-k));let t=s*s;
    var r=rounded_divide(s*rounded_add(0.43157974*t,1.0),rounded_add(rounded_add(0.05831938*t,0.76443945)*t,1.0));
    if(b>a){r=rounded_add(1.5707963267948966,-r);}if((bitcast<u32>(x)&0x80000000u)!=0u){r=rounded_add(3.141592653589793,-r);}
    return bitcast<f32>((bitcast<u32>(r)&0x7fffffffu)|(bitcast<u32>(y)&0x80000000u));
}
fn triangle_light_sincos(angle:f32)->vec2<f32>{
    let q=i32(round(angle*0.3183098861837907));let qf=f32(q);
    var x=rounded_add(qf*(-0.78515625*4.0),angle);
    x=rounded_add(qf*(-0.00024187564849853515625*4.0),x);
    x=rounded_add(qf*(-3.7747668102383613586e-8*4.0),x);
    x=rounded_add(qf*(-1.2816720341285448015e-12*4.0),x);
    x=rounded_add(1.5707963267948966,-rounded_add(1.5707963267948966,-x));let s=x*x;
    if((q&1)!=0){x= -x;}
    var su=2.6083159809786593541503e-6;
    su=rounded_add(su*s,-0.0001981069071916863322258);su=rounded_add(su*s,0.00833307858556509017944336);
    su=rounded_add(su*s,-0.166666597127914428710938);su=rounded_add(s*(su*x),x);
    var cu= -2.71811842367242206819355e-7;
    cu=rounded_add(cu*s,2.47990446951007470488548e-5);cu=rounded_add(cu*s,-0.00138888787478208541870117);
    cu=rounded_add(cu*s,0.0416666641831398010253906);cu=rounded_add(cu*s,-0.5);cu=rounded_add(cu*s,1.0);
    if((q&1)!=0){cu= -cu;}return clamp(vec2<f32>(su,cu),vec2<f32>(-1),vec2<f32>(1));
}
fn triangle_light_sin_from_cos(c:f32)->f32{return rounded_sqrt(max(rounded_add(1.0,-c*c),0.0));}
// Native SSE transform_point nests translation in the z product. Keeping it
// outside the dot changes a vertex by an ULP, amplified by grazing solid angles.
fn triangle_light_point(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    if(TRIANGLE_LIGHT_NATIVE_SSE!=0u){
        let z=rounded_add3(v.z*vec3<f32>(t.x.z,t.y.z,t.z.z),vec3<f32>(t.x.w,t.y.w,t.z.w));
        return rounded_add3(v.x*vec3<f32>(t.x.x,t.y.x,t.z.x),rounded_add3(v.y*vec3<f32>(t.x.y,t.y.y,t.z.y),z));
    }
    return vec3<f32>(motion_dot4(vec4<f32>(v,1),t.x),motion_dot4(vec4<f32>(v,1),t.y),motion_dot4(vec4<f32>(v,1),t.z));
}
// Negative time requests the center positions and center object transform used
// by the native host CDF, even when the sampled geometry itself is moving.
fn triangle_light_vertices(object:u32,prim:u32,time:f32)->TriangleVertices{
    var v:TriangleVertices;
    if(object>=table_count(G_objects)||object>=table_count(G_object_flag)||prim>=table_count(G_tri_vindex)){fail();v.error=1u;return v;}
    let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));
    if(time<0.0){let at=table_at(G_tri_vindex,prim,SIZE_tri_vindex);
        v.a=triangle_position(object,load_word(at),0u);v.b=triangle_position(object,load_word(at+4u),0u);v.c=triangle_position(object,load_word(at+8u),0u);
    }else{v=native_triangle_vertices(object,prim,select(PRIMITIVE_TRIANGLE,PRIMITIVE_MOTION_TRIANGLE,(flags&SD_OBJECT_HAS_VERTEX_MOTION)!=0u),time);}
    if((flags&SD_OBJECT_TRANSFORM_APPLIED)==0u){let t=triangle_light_object(object,select(time,0.5,time<0.0));v.a=triangle_light_point(t,v.a);v.b=triangle_light_point(t,v.b);v.c=triangle_light_point(t,v.c);}
    if(load_word(p(17u))!=0u){v.error=1u;}return v;
}
fn triangle_light_selection_area(object:u32,prim:u32,area:f32)->f32{
    let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));
    if(area!=0.0&&((flags&SD_OBJECT_HAS_VERTEX_MOTION)!=0u||(flags&SD_OBJECT_TRANSFORM_APPLIED)==0u)){
        let v=triangle_light_vertices(object,prim,-1.0);let n=rounded_cross(v.b-v.a,v.c-v.a);return 0.5*rounded_sqrt(rounded_dot(n,n));}
    return area;
}
fn triangle_light_solid_angle(a:vec3<f32>,b:vec3<f32>,c:vec3<f32>)->f32{
    let denominator=rounded_add(rounded_add(rounded_add(1.0,rounded_dot(b,c)),rounded_dot(a,c)),rounded_dot(a,b));
    return 2.0*triangle_light_atan2(abs(rounded_dot(a,rounded_cross(b,c))),denominator);
}
fn triangle_light_sample_native(object:u32,prim:u32,time:f32,P:vec3<f32>,random:vec2<f32>)->NativeLightSample{
    var ls:NativeLightSample;let v=triangle_light_vertices(object,prim,time);if(v.error!=0u){return ls;}
    let e0=v.b-v.a;let e1=v.c-v.a;let e2=v.c-v.b;
    let longest=max(rounded_dot(e0,e0),max(rounded_dot(e1,e1),rounded_dot(e2,e2)));var n=rounded_cross(e0,e1);
    if((load_word(table_at(G_object_flag,object,SIZE_object_flag))&SD_OBJECT_NEGATIVE_SCALE)!=0u){n= -n;}
    let n2=rounded_dot(n,n);if(n2==0.0){return ls;}
    let distance=rounded_divide(rounded_dot(n,v.a-P),n2);
    ls.shader=load_word(table_at(G_tri_shader,prim,SIZE_tri_shader));let flags=load_word(table_at(G_shaders,ls.shader&SHADER_MASK,SIZE_shaders)+SH_flags);
    if((flags&select(SD_MIS_FRONT,SD_MIS_BACK,distance>0.0))==0u){return ls;}
    let normal=light_normalize_len(n);let area=0.5*normal.w;
    ls.object_id=object;ls.prim=prim;ls.shader|=SHADER_USE_MIS;ls.eval_fac=1.0;
    if(longest>distance*distance){
        let A=rounded_safe_normalize(v.a-P);let B=rounded_safe_normalize(v.b-P);let C=rounded_safe_normalize(v.c-P);
        let cb=rounded_dot(A,C);let cc=rounded_dot(A,B);let solid=triangle_light_solid_angle(A,B,C);
        let ca=rounded_dot(rounded_safe_normalize(rounded_cross(A,B)),rounded_safe_normalize(rounded_cross(A,C)));
        let sa=triangle_light_sin_from_cos(ca);let alpha=triangle_light_acos(ca);
        let sc=triangle_light_sincos(rounded_add(random.x*solid,-alpha));let u=rounded_add(sc.y,-ca);let w=rounded_add(sc.x,sa*cc);
        let numerator=rounded_add(rounded_add(w*sc.y,-u*sc.x)*ca,-w);let denominator=rounded_add(w*sc.x,u*sc.y)*sa;
        var q=1.0;if(denominator!=0.0){q=rounded_divide(numerator,denominator);}
        let U=rounded_safe_normalize(rounded_add3(C,-cb*A));let Cp=rounded_safe_normalize(rounded_add3(q*A,triangle_light_sin_from_cos(q)*U));
        let dp=rounded_dot(Cp,B);let z=rounded_add(1.0,-random.y*rounded_add(1.0,-dp));
        ls.D=rounded_add3(z*B,triangle_light_sin_from_cos(z)*rounded_safe_normalize(rounded_add3(Cp,-dp*B)));
        var hit_u=0.0;var hit_v=0.0;var hit_t=0.0;
        if(!c_ray_triangle_intersect(P,ls.D,0.0,bitcast<f32>(0x7f7fffffu),v.a,v.b,v.c,&hit_u,&hit_v,&hit_t)||solid==0.0){return ls;}
        ls.t=hit_t;ls.P=rounded_add3(P,ls.D*ls.t);ls.pdf=rounded_divide(1.0,solid);
    }else{
        if(area==0.0){return ls;}var u=random.x;var w=random.y;if(w>u){u*=0.5;w-=u;}else{w*=0.5;u-=w;}
        let t=rounded_add(rounded_add(1.0,-u),-w);ls.P=rounded_add3(rounded_add3(t*v.a,u*v.b),w*v.c);
        let delta=ls.P-P;ls.t=rounded_sqrt(rounded_dot(delta,delta));ls.D=delta*rounded_divide(1.0,ls.t);let cosine=abs(rounded_dot(normal.xyz,-ls.D));
        if(cosine!=0.0){ls.pdf=rounded_divide(rounded_divide(ls.t*ls.t,cosine),area);}
    }
    ls.pdf*=triangle_light_selection_area(object,prim,area)*data_float(D_integrator_distribution_pdf_triangles);return ls;
}
fn triangle_light_forward_pdf(sd:SurfaceData)->f32{
    let v=triangle_light_vertices(sd.object_id,sd.prim,sd.time);if(v.error!=0u){return 0.0;}
    let e0=v.b-v.a;let e1=v.c-v.a;let e2=v.c-v.b;let n=rounded_cross(e0,e1);let n2=rounded_dot(n,n);if(n2==0.0){return 0.0;}
    let longest=max(rounded_dot(e0,e0),max(rounded_dot(e1,e1),rounded_dot(e2,e2)));let t=sd.ray_length;
    let distance=rounded_divide(abs(rounded_dot(n,sd.wi*t)),n2);let area=0.5*rounded_sqrt(n2);var pdf=0.0;
    if(longest>distance*distance){let P=rounded_add3(sd.P,sd.wi*t);let angle=triangle_light_solid_angle(rounded_safe_normalize(v.a-P),rounded_safe_normalize(v.b-P),rounded_safe_normalize(v.c-P));if(angle!=0.0){pdf=rounded_divide(1.0,angle);}}
    else{let cosine=abs(rounded_dot(sd.Ng,sd.wi));if(area!=0.0&&cosine!=0.0){pdf=rounded_divide(rounded_divide(t*t,cosine),area);}}
    return pdf*triangle_light_selection_area(sd.object_id,sd.prim,area)*data_float(D_integrator_distribution_pdf_triangles);
}
fn triangle_light_forward_weight(index:u32,sd:SurfaceData)->f32{
    if((state_word(S_path_flag,index)&PATH_RAY_MIS_SKIP)!=0u||(sd.shader_flag&select(SD_MIS_FRONT,SD_MIS_BACK,(sd.runtime_flag&SR_BACKFACING)!=0u))==0u){return 1.0;}
    // Removed by scene specialization when no shader is a MIS emitter and no
    // triangle is in the light distribution (pipelines.h).
//#if LIGHT_TRIANGLE else atomicOr(&heap[p(17u)/4u],2u);return 1.0;
    let nee=triangle_light_forward_pdf(sd);let bsdf=heap_float(state_address(S_path_mis_ray_pdf,index,4u));return (bsdf*bsdf)/(bsdf*bsdf+nee*nee);
//#endif
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Native Cycles light distribution, point/sun sampling, BSDF evaluation and
// shadow-path initialization. See native/THIRD-PARTY-NOTICES.txt.
struct NativeLightSample {P:vec3<f32>,D:vec3<f32>,t:f32,pdf:f32,eval_fac:f32,shader:u32,object_id:u32,prim:u32,is_light:bool}
// GPU-private sample cache; 13 scalar words avoid host shading or readback.
fn light_sample_write(at:u32,ls:NativeLightSample){
    let words=array<u32,13>(bitcast<u32>(ls.P.x),bitcast<u32>(ls.P.y),bitcast<u32>(ls.P.z),bitcast<u32>(ls.D.x),bitcast<u32>(ls.D.y),bitcast<u32>(ls.D.z),bitcast<u32>(ls.t),bitcast<u32>(ls.pdf),bitcast<u32>(ls.eval_fac),ls.shader,ls.object_id,ls.prim,select(0u,1u,ls.is_light));
    for(var j=0u;j<13u;j++){store_word(at+j*4u,words[j]);}
}
fn light_sample_read(at:u32)->NativeLightSample{
    var ls:NativeLightSample;ls.P=heap_vec3(at);ls.D=heap_vec3(at+12u);ls.t=heap_float(at+24u);ls.pdf=heap_float(at+28u);ls.eval_fac=heap_float(at+32u);
    ls.shader=load_word(at+36u);ls.object_id=load_word(at+40u);ls.prim=load_word(at+44u);ls.is_light=load_word(at+48u)!=0u;return ls;
}
fn light_normalize_len(v:vec3<f32>)->vec4<f32>{
    let length=rounded_sqrt(rounded_dot(v,v));if(length==0.0){return vec4<f32>(0);}
    return vec4<f32>(rounded_divide(v.x,length),rounded_divide(v.y,length),rounded_divide(v.z,length),length);
}
fn light_basis(n:vec3<f32>)->MicrofacetBasis{var c:SvmClosure;c.N=n;return ggx_basis(c);}
fn light_cone(n:vec3<f32>,angle:f32,random:vec2<f32>)->vec4<f32>{
    if(angle==0.0){return vec4<f32>(n,1.0);}
    var xy=ggx_disk_sample(random);let r2=dot(xy,xy);let cosine=1.0-r2*angle;
    xy*=sqrt(max(angle*(2.0-angle*r2),0.0));let b=light_basis(n);
    return vec4<f32>(xy.x*b.X+xy.y*b.Y+cosine*n,(0.5*INV_PI)/angle);
}
fn light_sample_native(index:u32,sd:SurfaceData,r:SvmResult)->NativeLightSample{
    var ls:NativeLightSample;
    let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_LIGHT).xyz;
    let n=data_word(D_integrator_num_distribution);var first=0u;var length=n+1u;
    loop{let half_count=length>>1u;let middle=first+half_count;
        if(random.z<heap_float(table_at(G_light_distribution,middle,SIZE_light_distribution)+LD_totarea)){length=half_count;}
        else{first=middle+1u;length-=half_count+1u;}if(length==0u){break;}}
    let emitter=u32(clamp(i32(first)-1,0,i32(n)-1));
    let distribution=table_at(G_light_distribution,emitter,SIZE_light_distribution);let prim=bitcast<i32>(load_word(distribution+LD_prim));
    if(prim>=0){
        // Removed by scene specialization when no emissive triangle is in the light
        // distribution (pipelines.h); a reached removed handler reports error 2.
//#if LIGHT_TRIANGLE else atomicOr(&heap[p(17u)/4u],2u);return ls;
        ls=triangle_light_sample_native(load_word(distribution+LD_object),u32(prim),sd.time,sd.P,random.xy);
        ls.shader|=load_word(distribution+LD_visibility);return ls;
//#endif
    }
    ls.is_light=true;ls.prim=~bitcast<u32>(prim);
    let light=table_at(G_lights,ls.prim,SIZE_lights);
    if(f32(state_half(S_path_bounce,index))>heap_float(light+L_max_bounces)){return ls;}
    ls.shader=load_word(light+L_shader_id);ls.object_id=load_word(light+L_object_id);
    let position=heap_vec3(light+L_co);
    if(load_word(light+L_type)==LIGHT_SUN){
        let cone=light_cone(position,heap_float(light+L_sun_angle),random.xy);
        ls.P=cone.xyz;ls.D=-cone.xyz;ls.pdf=cone.w;ls.t=bitcast<f32>(0x7f7fffffu);ls.eval_fac=heap_float(light+L_sun_eval);
    }else if(load_word(light+L_type)==LIGHT_AREA){
        // Removed by scene specialization when no area light is uploaded (pipelines.h).
//#if LIGHT_AREA else atomicOr(&heap[p(17u)/4u],2u);return ls;
        let area=area_evaluate(light,sd.P,vec3<f32>(0),0.0,random.xy,true);if(!area.valid){return ls;}
        ls.P=area.P;ls.D=area.D;ls.t=area.t;ls.pdf=area.pdf;ls.eval_fac=area.eval_fac;
//#endif
    }else{
        let spot=load_word(light+L_type)==LIGHT_SPOT;
        let radius=heap_float(light+L_radius);let r2=radius*radius;let delta=sd.P-position;
        let d2=rounded_dot(delta,delta);let distance=rounded_sqrt(d2);let normal=delta/distance;
        ls.eval_fac=heap_float(light+L_point_eval);
        if(load_word(light+L_is_sphere)!=0u){
            var cosine:f32;var sampled_t= -1.0;
            if(d2>r2){let ratio=r2/d2;let angle=select(0.5*ratio,1.0-sqrt(max(1.0-ratio,0.0)),ratio>0.0004);
                let spot_angle=1.0-heap_float(light+L_spot_spread_cos);
                if(spot&&angle>=spot_angle){let cone=light_cone(-heap_vec3(light+L_spot_dir),spot_angle,random.xy);ls.D=cone.xyz;ls.pdf=cone.w;
                    sampled_t=emitter_sphere_hit(sd.P,ls.D,position,radius);if(sampled_t<0.0){ls.pdf=0.0;return ls;}
                }else{let cone=light_cone(-normal,angle,random.xy);ls.D=cone.xyz;ls.pdf=cone.w;
                    // The sampler's cosine is derived before transforming its direction.
                    let disk=ggx_disk_sample(random.xy);cosine=1.0-dot(disk,disk)*angle;}

            }else{
                if((r.runtime_flag&SR_BSDF_HAS_TRANSMISSION)!=0u){let z=1.0-2.0*random.x;let radial=sqrt(max(1.0-z*z,0.0));let phi=2.0*PI*random.y;
                    ls.D=vec3<f32>(radial*cos(phi),radial*sin(phi),z);ls.pdf=0.25*INV_PI;
                }else{let sampled=surface_cos_hemisphere(sd.N,random.xy);ls.D=sampled.xyz;ls.pdf=sampled.w;}
                cosine=-dot(ls.D,normal);
            }
            if(spot&&d2>r2){ls.eval_fac*=spot_attenuation(light,-ls.D);if(ls.eval_fac==0.0){ls.pdf=0.0;return ls;}}
            let root=sqrt(max(r2-d2+d2*cosine*cosine,0.0));ls.t=select(distance*cosine-select(-root,root,d2-r2>=0.0),sampled_t,sampled_t>=0.0);
            ls.P=sd.P+ls.D*ls.t;let ng=surface_normalize(ls.P-position);ls.P=ng*radius+position;
        }else{
            ls.P=position;if(radius>0.0){let b=light_basis(normal);let disk=ggx_disk_sample(random.xy);ls.P+=(b.X*disk.x+b.Y*disk.y)*radius;}
            let direction=light_normalize_len(ls.P-sd.P);ls.D=direction.xyz;ls.t=direction.w;
            if(spot){ls.eval_fac*=spot_attenuation(light,-ls.D);if(ls.eval_fac==0.0){ls.pdf=0.0;return ls;}}
            let cosine=dot(normal,-ls.D);let inverse_area=select(1.0,1.0/(r2*PI),r2>0.0);
            if(cosine>0.0){ls.pdf=inverse_area*(ls.t*ls.t/cosine);}
        }
    }
    ls.pdf*=data_float(D_integrator_distribution_pdf_lights);return ls;
}
fn light_bsdf(sd:SurfaceData,r:SvmResult,direction:vec3<f32>,shader:u32)->SurfaceSample{
    var result:SurfaceSample;var weight_sum=0.0;var pdf_sum=0.0;var rough_sum=0.0;
    for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(!surf_bsdf(c.kind)){continue;}
        let eval=surf_evaluate(sd,c,direction);
        if(eval.w!=0.0){
            let excluded=(surf_diffuse(c.kind)&&(shader&SHADER_EXCLUDE_DIFFUSE)!=0u)||
                (surf_glossy(c.kind)&&(shader&SHADER_EXCLUDE_GLOSSY)!=0u)||
                (surf_refraction(c.kind)&&(shader&SHADER_EXCLUDE_TRANSMIT)!=0u)||
                (surf_glass(c.kind)&&(shader&(SHADER_EXCLUDE_GLOSSY|SHADER_EXCLUDE_TRANSMIT))==(SHADER_EXCLUDE_GLOSSY|SHADER_EXCLUDE_TRANSMIT));
            if(!excluded){let value=eval.xyz*c.weight;result.eval+=value;if(surf_diffuse(c.kind)){result.diffuse+=value;}if(surf_glossy_direction(c,direction)){result.glossy+=value;}}
            let weight=eval.w*c.sample_weight;pdf_sum+=weight;rough_sum+=weight*surf_roughness(c);
        }weight_sum+=c.sample_weight;
    }
    result.roughness=select(0.0,rough_sum/pdf_sum,pdf_sum>0.0);
    if((shader&SHADER_USE_MIS)!=0u&&weight_sum>0.0){result.pdf=pdf_sum/weight_sum;}
    return result;
}
// Called by connect_surface_light with the sample stored by shade_surface_light;
// the split keeps emitter sampling and BSDF evaluation in separate pipelines.
fn surface_direct_light(index:u32,slot:u32,sd:SurfaceData,r:SvmResult,ls:NativeLightSample){
    if(data_word(D_integrator_use_direct_light)==0u||(r.runtime_flag&SR_BSDF_HAS_EVAL)==0u){return;}
    if(!(ls.pdf>0.0)||load_word(p(17u))!=0u){return;}
    let shader=table_at(G_shaders,ls.shader&SHADER_MASK,SIZE_shaders);
    var emission=heap_vec3(shader+SH_constant_emission);
    if(ls.is_light){emission*=heap_vec3(table_at(G_lights,ls.prim,SIZE_lights)+L_strength);}
    var bsdf=light_bsdf(sd,r,ls.D,ls.shader);let mis=(ls.pdf*ls.pdf)/(ls.pdf*ls.pdf+bsdf.pdf*bsdf.pdf);
    let factor=emission*ls.eval_fac/ls.pdf*mis;bsdf.eval*=factor;bsdf.diffuse*=factor;bsdf.glossy*=factor;
    if(all(bsdf.eval==vec3<f32>(0))){return;}
    let rr=data_float(D_integrator_light_inv_rr_threshold);
    let probability=select(1.0,max(abs(bsdf.eval.x),max(abs(bsdf.eval.y),abs(bsdf.eval.z)))*rr,rr>0.0);
    if(probability<1.0){let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_LIGHT_TERMINATE).x;
        if(random>=probability){return;}let inverse=1.0/probability;bsdf.eval*=inverse;bsdf.diffuse*=inverse;bsdf.glossy*=inverse;}
    var direction=ls.D;var tmax=ls.t;if(ls.t!=bitcast<f32>(0x7f7fffffu)){let ray=light_normalize_len(ls.P-sd.P);direction=ray.xyz;tmax=ray.w;}
    let origin=surf_ray_offset(sd,direction);let allocator=load_word(p(7u)+S_next_shadow_path_index)/4u;var shadow:u32;
    if(p(28u)!=0u){shadow=load_word(p(28u))+slot;if(shadow>=p(25u)){fail();return;}}
    else{loop{shadow=atomicLoad(&heap[allocator]);if(shadow>=p(25u)){fail();return;}
        if(atomicCompareExchangeWeak(&heap[allocator],shadow,shadow+1u).exchanged){break;}}}
    state_vec3(S_shadow_ray_P,shadow,origin);state_vec3(S_shadow_ray_D,shadow,direction);
    state_f32(S_shadow_ray_tmin,shadow,select(bitcast<f32>(0x7f7fffffu),0.0,(ls.shader&SHADER_CAST_SHADOW)!=0u));state_f32(S_shadow_ray_tmax,shadow,tmax);
    state_f32(S_shadow_ray_time,shadow,sd.time);state_f32(S_shadow_ray_dP,shadow,sd.dP);
    state_f32(S_shadow_ray_dD,shadow,select(sd.dI,max(sd.dI,sqrt(bsdf.roughness)),bsdf.roughness>0.0));
    state_u32(S_shadow_self_object,shadow,sd.object_id);state_u32(S_shadow_self_prim,shadow,sd.prim);
    state_u32(S_shadow_ray_self_light_object,shadow,ls.object_id);state_u32(S_shadow_ray_self_light_prim,shadow,ls.prim);
    state_f32(S_shadow_path_bsdf_eval_average,shadow,(bsdf.eval.x+bsdf.eval.y+bsdf.eval.z)*(1.0/3.0));
    state_u32(S_shadow_path_render_pixel_index,shadow,state_word(S_path_render_pixel_index,index));
    state_u32(S_shadow_path_sample,shadow,state_word(S_path_sample,index));state_u32(S_shadow_path_rng_pixel,shadow,state_word(S_path_rng_pixel,index));
    state_u16(S_shadow_path_rng_offset,shadow,state_half(S_path_rng_offset,index));state_u16(S_shadow_path_bounce,shadow,state_half(S_path_bounce,index));
    state_u16(S_shadow_path_diffuse_bounce,shadow,state_half(S_path_diffuse_bounce,index));state_u16(S_shadow_path_glossy_bounce,shadow,state_half(S_path_glossy_bounce,index));
    state_u16(S_shadow_path_transmission_bounce,shadow,state_half(S_path_transmission_bounce,index));state_u16(S_shadow_path_transparent_bounce,shadow,state_half(S_path_transparent_bounce,index));
    state_u16(S_shadow_path_volume_bounds_bounce,shadow,state_half(S_path_volume_bounds_bounce,index));
    state_vec3(S_shadow_path_throughput,shadow,heap_vec3(state_address(S_path_throughput,index,12u))*bsdf.eval);
    let group=load_word(table_at(G_objects,ls.object_id,SIZE_objects)+O_lightgroup);state_u8(S_shadow_path_lightgroup,shadow,group+1u);
    var flag=state_word(S_path_flag,index);
    if(p(20u)!=0u){var diffuse=svm_divide(bsdf.diffuse,bsdf.eval);var glossy=svm_divide(bsdf.glossy,bsdf.eval);
        if((flag&PATH_RAY_ANY_PASS)!=0u){diffuse=heap_vec3(state_address(S_path_pass_diffuse_weight,index,12u));glossy=heap_vec3(state_address(S_path_pass_glossy_weight,index,12u));}
        else{flag|=PATH_RAY_SURFACE_PASS;}
        state_vec3(S_shadow_path_pass_diffuse_weight,shadow,diffuse);state_vec3(S_shadow_path_pass_glossy_weight,shadow,glossy);
    }
    state_u8(S_shadow_path_visibility,shadow,state_byte(S_path_visibility,index));state_u32(S_shadow_path_flag,shadow,flag);
    state_u16(S_shadow_path_queued_kernel,shadow,K_SHADOW_INTERSECT);let counter=load_word(p(7u)+S_queue_counter)/4u;atomicAdd(&heap[counter+K_SHADOW_INTERSECT],1u);
}
)CBWGSL"
