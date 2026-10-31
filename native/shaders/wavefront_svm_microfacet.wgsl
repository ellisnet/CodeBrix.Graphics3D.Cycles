// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2011-2013 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from pinned Cycles svm/closure.h, closure/bsdf_microfacet.h,
// util/lookup_table.h and util/math_float3.h. Typed records and bounded native
// table descriptors replace pointers; original notices are retained in
// native/THIRD-PARTY-NOTICES.txt and parity/licenses/BSD-3-Clause.txt.
fn microfacet_rotate(pnt:vec3<f32>,axis:vec3<f32>,angle:f32)->vec3<f32>{
    let c=cos(angle);let s=sin(angle);let m=1.0-c;
    return vec3<f32>((c+m*axis.x*axis.x)*pnt.x+(m*axis.x*axis.y-axis.z*s)*pnt.y+(m*axis.x*axis.z+axis.y*s)*pnt.z,
        (m*axis.x*axis.y+axis.z*s)*pnt.x+(c+m*axis.y*axis.y)*pnt.y+(m*axis.y*axis.z-axis.x*s)*pnt.z,
        (m*axis.x*axis.z-axis.y*s)*pnt.x+(m*axis.y*axis.z+axis.x*s)*pnt.y+(c+m*axis.z*axis.z)*pnt.z);
}
fn microfacet_lut1(s:ptr<function,SvmState>,x:f32,offset:u32,size:u32)->f32{
    let count=table_count(G_lookup_table);
    if(size==0u||offset>count||size>count-offset){svm_error(s,512u);return 0.0;}
    let value=clamp(x,0.0,1.0)*f32(size-1u);let index=min(u32(value),size-1u);let t=value-f32(index);
    let a=heap_float(table_at(G_lookup_table,offset+index,SIZE_lookup_table));if(t==0.0){return a;}
    let b=heap_float(table_at(G_lookup_table,offset+min(index+1u,size-1u),SIZE_lookup_table));return (1.0-t)*a+t*b;
}
fn microfacet_lut2(s:ptr<function,SvmState>,x:f32,y:f32,offset:u32)->f32{
    let count=table_count(G_lookup_table);
    if(offset>count||1024u>count-offset){svm_error(s,512u);return 0.0;}
    let value=clamp(y,0.0,1.0)*31.0;let index=min(u32(value),31u);let t=value-f32(index);
    let a=microfacet_lut1(s,x,offset+32u*index,32u);if(t==0.0){return a;}
    let b=microfacet_lut1(s,x,offset+32u*min(index+1u,31u),32u);return (1.0-t)*a+t*b;
}
fn svm_glossy(s:ptr<function,SvmState>,data:u32,kind:u32,mix_weight:f32,visibility:u32){
    if(data_word(D_caustics_reflective)==0u&&(visibility&PATH_RAY_VISIBILITY_DIFFUSE)!=0u){return;}
    let ni=svm_byte(data+N_GlossyBsdfData_normal_offset);var n=(*s).geometry.N;
    if(ni!=SVM_STACK_INVALID){n=svm_load3(s,ni);}
    if(any(n!=vec3<f32>(0))){n=surface_normalize(n);}else{n=(*s).geometry.N;}
    let weight=max((*s).weight*mix_weight,vec3<f32>(0));let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));
    if(!(sample_weight>=1e-5)){return;}
    let i=svm_allocate(s,CLOSURE_BSDF_MICROFACET_GGX_ID,weight);if(i==NONE){return;}
    var c=svm_closure_read((*s).output,i);c.sample_weight=sample_weight;c.N=n;c.ior=1.0;c.energy_scale=1.0;
    if(((*s).geometry.shader_flag&SD_USE_BUMP_MAP_CORRECTION)!=0u&&any((*s).geometry.Ng!=n)){c.N=svm_valid_normal((*s).geometry.Ng,(*s).geometry.wi,n);}
    let r=clamp(svm_input(s,data+N_GlossyBsdfData_roughness),0.0,1.0);let rough=r*r;
    let anisotropy=clamp(svm_input(s,data+N_GlossyBsdfData_anisotropy),-0.99,0.99);
    let ti=svm_byte(data+N_GlossyBsdfData_tangent_offset);
    c.alpha_x=rough;c.alpha_y=rough;
    if(ti!=SVM_STACK_INVALID&&abs(anisotropy)>1e-4){
        c.T=svm_load3(s,ti);let rotation=svm_input(s,data+N_GlossyBsdfData_rotation);
        if(rotation!=0.0){c.T=microfacet_rotate(c.T,c.N,rotation*6.283185307179586f);}
        if(anisotropy<0.0){c.alpha_x=rough/(1.0+anisotropy);c.alpha_y=rough*(1.0+anisotropy);}
        else{c.alpha_x=rough*(1.0-anisotropy);c.alpha_y=rough/(1.0-anisotropy);}
    }
    c.alpha_x=clamp(c.alpha_x,0.0,1.0);c.alpha_y=clamp(c.alpha_y,0.0,1.0);
    (*s).output.runtime_flag|=SR_BSDF;
    if(c.alpha_x*c.alpha_y>2e-10){(*s).output.runtime_flag|=SR_BSDF_HAS_EVAL;}
    if(kind==CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID){
        let color=max(svm_input3(s,data+N_GlossyBsdfData_color),vec3<f32>(0));
        let roughness=sqrt(sqrt(c.alpha_x*c.alpha_y));let mu=dot((*s).geometry.wi,c.N);
        let e=microfacet_lut2(s,roughness,mu,data_word(D_ggx_E));
        let average=microfacet_lut1(s,roughness,data_word(D_ggx_Eavg),32u);
        if((*s).error!=0u){return;}
        let missing=(1.0-e)/e;c.energy_scale=1.0+missing;
        if(any(color!=vec3<f32>(1))){
            let multi=color*average/(vec3<f32>(1)-color*(1.0-average));
            let darkening=(vec3<f32>(1)+multi*missing)/c.energy_scale;
            c.weight*=darkening;c.sample_weight*=(darkening.x+darkening.y+darkening.z)*(1.0/3.0);
        }
    }
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);
}
// Native Glass/Refraction SVM setup; Fresnel data stays in canonical arena
// records while allocation capacity follows native closure_alloc_extra.
fn ggx_f0(ior:f32)->f32{let r=rounded_divide(rounded_add(ior,-1.0),rounded_add(ior,1.0));return r*r;}
fn ggx_dielectric(cosine:f32,eta:f32)->vec2<f32>{
    let square=rounded_add(eta*eta,-rounded_add(1.0,-cosine*cosine));
    if(square<=0.0){return vec2<f32>(1,0);}
    let ci=abs(cosine);let ct=-rounded_divide(rounded_sqrt(max(square,0.0)),eta);
    let rs=rounded_divide(rounded_add(ci,eta*ct),rounded_add(ci,-eta*ct));
    let rp=rounded_divide(rounded_add(ct,eta*ci),rounded_add(eta*ci,-ct));
    return vec2<f32>(rounded_add(rs*rs,rp*rp)*0.5,ct);
}
fn microfacet_lut2_size(s:ptr<function,SvmState>,x:f32,y:f32,offset:u32,size:u32)->f32{
    let count=table_count(G_lookup_table);
    if(offset>count||size*size>count-offset){svm_error(s,512u);return 0.0;}
    let value=clamp(y,0.0,1.0)*f32(size-1u);let index=min(u32(value),size-1u);let t=value-f32(index);
    let a=microfacet_lut1(s,x,offset+size*index,size);if(t==0.0){return a;}
    let b=microfacet_lut1(s,x,offset+size*min(index+1u,size-1u),size);return (1.0-t)*a+t*b;
}
fn microfacet_lut3(s:ptr<function,SvmState>,x:f32,y:f32,z:f32,offset:u32)->f32{
    let count=table_count(G_lookup_table);
    if(offset>count||4096u>count-offset){svm_error(s,512u);return 0.0;}
    let value=clamp(z,0.0,1.0)*15.0;let index=min(u32(value),15u);let t=value-f32(index);
    let a=microfacet_lut2_size(s,x,y,offset+256u*index,16u);if(t==0.0){return a;}
    let b=microfacet_lut2_size(s,x,y,offset+256u*min(index+1u,15u),16u);return (1.0-t)*a+t*b;
}
fn svm_glass(s:ptr<function,SvmState>,data:u32,kind:u32,mix_weight:f32,visibility:u32){
    let glass=kind==CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID;
    let reflect=data_word(D_caustics_reflective)!=0u||(visibility&PATH_RAY_VISIBILITY_DIFFUSE)==0u;
    let transmit=data_word(D_caustics_refractive)!=0u||(visibility&PATH_RAY_VISIBILITY_DIFFUSE)==0u;
    if((!glass&&!transmit)||(glass&&!reflect&&!transmit)){return;}
    let normal_offset=select(N_RefractionBsdfData_normal_offset,N_GlassBsdfData_normal_offset,glass);
    let ni=svm_byte(data+normal_offset);var n=(*s).geometry.N;
    if(ni!=SVM_STACK_INVALID){n=svm_load3(s,ni);}
    if(any(n!=vec3<f32>(0))){n=surface_normalize(n);}else{n=(*s).geometry.N;}
    if(glass){let thickness=svm_input(s,data+N_GlassBsdfData_thin_film_thickness);svm_input(s,data+N_GlassBsdfData_thin_film_ior);
        if(!(thickness<=0.1)){svm_error(s,16u);return;}}
    let weight=max(select((*s).weight*mix_weight,vec3<f32>(mix_weight),glass),vec3<f32>(0));
    let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));if(!(sample_weight>=1e-5)){return;}
    let i=svm_allocate(s,kind,weight);if(i==NONE){return;}
    if(glass){if((*s).output.remaining<GLASS_EXTRA_CLOSURES){(*s).output.count--;(*s).output.remaining++;
            var empty:SvmClosure;svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,empty);return;}
        (*s).output.remaining-=GLASS_EXTRA_CLOSURES;}
    var c=svm_closure_read((*s).output,i);c.sample_weight=sample_weight;c.N=n;c.energy_scale=1.0;
    if(((*s).geometry.shader_flag&SD_USE_BUMP_MAP_CORRECTION)!=0u&&any((*s).geometry.Ng!=n)){c.N=svm_valid_normal((*s).geometry.Ng,(*s).geometry.wi,n);}
    let ior=max(svm_input(s,data+select(N_RefractionBsdfData_ior,N_GlassBsdfData_ior,glass)),1e-5);
    c.ior=select(ior,rounded_divide(1.0,ior),((*s).geometry.runtime_flag&SR_BACKFACING)!=0u);
    var rough=svm_input(s,data+select(N_RefractionBsdfData_roughness,N_GlassBsdfData_roughness,glass));
    if(glass){rough=clamp(rough,0.0,1.0);}c.alpha_x=clamp(rough*rough,0.0,1.0);c.alpha_y=c.alpha_x;
    (*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_TRANSMISSION;
    if(c.alpha_x*c.alpha_y>2e-10){(*s).output.runtime_flag|=SR_BSDF_HAS_EVAL;}
    if(glass){c.fresnel_f0=ggx_f0(ior);let color=max(svm_input3(s,data+N_GlassBsdfData_color),vec3<f32>(0));
        c.reflection_tint=select(vec3<f32>(0),color,reflect);c.transmission_tint=select(vec3<f32>(0),color,transmit);
        let roughness=sqrt(sqrt(c.alpha_x*c.alpha_y));let mu=dot((*s).geometry.wi,c.N);
        let z=sqrt(abs((c.ior-1.0)/(c.ior+1.0)));let v=microfacet_lut3(s,roughness,mu,z,data_word(D_ggx_gen_schlick_ior_s));
        let f=(1.0-v)*c.fresnel_f0+v;let albedo=f*c.reflection_tint+(1.0-f)*c.transmission_tint;
        c.sample_weight*=(albedo.x+albedo.y+albedo.z)*(1.0/3.0);
    }
//#if SVM_BSDF_PRINCIPLED
    // Canonical generalized-Schlick fields shared with Principled transmission.
    if(glass){c.roughness=4.0;c.multiscatter=vec3<f32>(c.fresnel_f0);}
//#endif
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);
}
//#if SVM_BSDF_PRINCIPLED
// Native Principled BSDF setup (pinned svm/closure.h, CLOSURE_BSDF_PRINCIPLED_ID
// branch and principled_bsdf_emission; closure/bsdf_microfacet.h Fresnel setup and
// energy preservation; closure/bsdf_sheen.h; closure/alloc.h). Canonical records
// reuse fields per kind (surface_closure.h): a microfacet `roughness` holds the
// native MicrofacetFresnel model (1 dielectric, 4 generalized Schlick, 5 F82 tint),
// `multiscatter` its f0 and an F82 closure its b term in `reflection_tint`; sheen
// keeps transformA/B in a/b, T in T and B in multiscatter. Subsurface is reported
// (error 16) and refused natively before rendering; it is never approximated.
const PRINCIPLED_LOCAL:u32=0xfffffffeu;
fn principled_average(v:vec3<f32>)->f32{return (v.x+v.y+v.z)*(1.0/3.0);}
// rotate_around_axis with binary32 sinf/cosf: quadrant reduction by a split
// pi/2 and the bounded precise_disk_sincos adapter (driver sin/cos are only
// accurate to about 2^-11, which visibly turns sharp anisotropic GGX lobes).
fn principled_rotate(pt:vec3<f32>,axis:vec3<f32>,angle:f32)->vec3<f32>{
    // Exact quadrant reduction (trig_product keeps k*pi/2's rounding residual); the reduced angle stays two-part.
    let k=round(angle*0.6366197723675814);let product=trig_product(k,1.5707963705062866);
    let q=precise_disk_sincos_pair(vec2<f32>(rounded_add(angle,-product.x),rounded_add(-product.y,k*4.3711390001862426e-8)));let quadrant=u32(i32(k)&3);var s=q.x;var c=q.y;
    if(quadrant==1u){s=q.y;c=-q.x;}if(quadrant==2u){s=-q.x;c=-q.y;}if(quadrant==3u){s=-q.y;c=q.x;}
    // util/math_float3.h rotate_around_axis in its operation order, every product rounded before its sum
    // (rounded_add), so neither the Intel nor the Adreno compiler contracts it (lane S2, principled_render stage 40).
    // Row r of the rotation is rounded_add(rounded_add(e0*pt.x,e1*pt.y),e2*pt.z) with e_i=rounded_add(u_r[i],v_r[i]).
    // The three rows run through one body, in a loop whose bound is opaque to the compiler (header word 3 after the
    // error word, always 0; a larger value only adds iterations that write nothing). With all fifteen rounded_add
    // bodies inlined, the Adreno 650 compiler (EV031.32.02.10) dropped an SVM store in the Principled SVM probe
    // (error 8, native/DEVICE.md "Adreno 650 lane 2"). Same sums in the same order.
    let m=1.0-c;let xy=(m*axis.x)*axis.y;let xz=(m*axis.x)*axis.z;let yz=(m*axis.y)*axis.z;
    let u0=vec3<f32>(c,xy,xz);let u1=vec3<f32>(xy,c,yz);let u2=vec3<f32>(xz,yz,c);
    let v0=vec3<f32>((m*axis.x)*axis.x,-(axis.z*s),axis.y*s);let v1=vec3<f32>(axis.z*s,(m*axis.y)*axis.y,-(axis.x*s));
    let v2=vec3<f32>(-(axis.y*s),axis.x*s,(m*axis.z)*axis.z);
    var out=vec3<f32>(0);let count=3u|load_word(p(17u)+12u);
    for(var row=0u;row<count;row++){
        let u=select(select(u2,u1,row==1u),u0,row==0u);let v=select(select(v2,v1,row==1u),v0,row==0u);
        let r=rounded_add(rounded_add(rounded_add(u.x,v.x)*pt.x,rounded_add(u.y,v.y)*pt.y),rounded_add(u.z,v.z)*pt.z);
        out=select(out,vec3<f32>(r),vec3<u32>(0u,1u,2u)==vec3<u32>(row));
    }
    return out;
}
fn principled_new(kind:u32,weight:vec3<f32>)->SvmClosure{
    var c:SvmClosure;c.kind=kind;c.weight=max(weight,vec3<f32>(0));c.sample_weight=abs(principled_average(c.weight));return c;
}
// bsdf_alloc (+closure_alloc_extra) or, for emission-only evaluation, the
// stack closure of bsdf_alloc_maybe_emission. NONE when nothing is allocated.
fn principled_slot(s:ptr<function,SvmState>,c:SvmClosure,local:bool,extra:bool)->u32{
    if(!(c.sample_weight>=1e-5)){return NONE;}
    if(local){return PRINCIPLED_LOCAL;}
    let i=svm_allocate(s,c.kind,c.weight);if(i==NONE){return NONE;}
    if(extra){if((*s).output.remaining<1u){(*s).output.count--;(*s).output.remaining++;var empty:SvmClosure;
            svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,empty);return NONE;}
        (*s).output.remaining-=1u;}
    return i;
}
fn principled_store(s:ptr<function,SvmState>,i:u32,c:SvmClosure){if(i!=PRINCIPLED_LOCAL){svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);}}
fn principled_normal(s:ptr<function,SvmState>,index:u32,fallback:vec3<f32>)->vec3<f32>{
    var n=fallback;if(index!=SVM_STACK_INVALID){n=svm_load3(s,index);}
    if(any(n!=vec3<f32>(0))){return surface_normalize(n);}return (*s).geometry.N;
}
fn principled_valid(s:ptr<function,SvmState>,n:vec3<f32>)->vec3<f32>{
    if(((*s).geometry.shader_flag&SD_USE_BUMP_MAP_CORRECTION)!=0u&&any((*s).geometry.Ng!=n)){return svm_valid_normal((*s).geometry.Ng,(*s).geometry.wi,n);}
    return n;
}
fn principled_ggx(s:ptr<function,SvmState>,c:ptr<function,SvmClosure>,glass:bool){
    (*c).alpha_x=clamp((*c).alpha_x,0.0,1.0);(*c).alpha_y=select(clamp((*c).alpha_y,0.0,1.0),(*c).alpha_x,glass);(*c).energy_scale=1.0;
    (*c).kind=select(CLOSURE_BSDF_MICROFACET_GGX_ID,CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID,glass);
    (*s).output.runtime_flag|=SR_BSDF|select(0u,SR_BSDF_HAS_TRANSMISSION,glass)|select(0u,SR_BSDF_HAS_EVAL,(*c).alpha_x*(*c).alpha_y>2e-10);
}
fn principled_fss(eta:f32)->f32{
    if(eta<1.0){return 0.997118+eta*(0.1014-eta*(0.965241+eta*0.130607));}return (eta-1.0)/(4.08567+1.00071*eta);
}
// Native microfacet_fresnel for the models carried in a record's `roughness`
// (surface_closure.h): 1 dielectric, 4 generalized Schlick (exponent < 0,
// f90 = 1; the Glass-node arithmetic, per channel), 5 F82 tint; `a`/`b` carry
// an enabled_lobe thin film (thickness > 0.1, film IOR) for iridescence.
struct PrincipledFresnel {reflection:vec3<f32>,transmission:vec3<f32>,cos_transmit:f32}
fn principled_fresnel(c:SvmClosure,cosine:f32)->PrincipledFresnel{
    var f:PrincipledFresnel;let model=u32(c.roughness);let glass=c.kind==CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID;
//#if SVM_BSDF_THIN_FILM
    if(c.a>0.1){
        // One channel at a time; F82 estimates the conductor n, k (Gulbrandsen).
        var value=vec3<f32>(0);
        for(var i=0u;i<3u;i++){var n=c.ior;var k=0.0;var g=-1.0;
            if(model==5u){let f0=c.multiscatter[i];let r=min(f0,0.999);g=thin_film_f82(1.0/7.0,f0,c.reflection_tint[i]);let root=sqrt(r);
                let a=rounded_divide(1.0+root,1.0-root);n=a+g*((1.0-r)/(1.0+r)-a);k=sqrt(max((r*((n+1.0)*(n+1.0))-(n-1.0)*(n-1.0))/(1.0-r),0.0));}
            let r=thin_film_channel(i,c.a,c.b,n,k,g,cosine,model==5u);value[i]=r.x;if(model!=5u){f.cos_transmit=r.y;}}
        if(model==5u){f.reflection=value;return f;}
        let real_f0=ggx_f0(c.ior);
        if(real_f0>1e-5&&any(value!=vec3<f32>(1))){for(var i=0u;i<3u;i++){let s=clamp((value[i]-1.0)/(real_f0-1.0),0.0,1.0);value[i]*=1.0+s*(c.multiscatter[i]/real_f0-1.0);}}
        f.reflection=value*c.reflection_tint;if(glass){f.transmission=(vec3<f32>(1)-value)*c.transmission_tint;}return f;
    }
//#endif
    if(model==5u){let s=clamp(1.0-cosine,0.0,1.0);let s5=(s*s)*(s*s)*s;
        f.reflection=clamp(c.multiscatter+s5*(vec3<f32>(1)-c.multiscatter)-c.reflection_tint*cosine*s5*s,vec3<f32>(0),vec3<f32>(1));return f;}
    let dielectric=ggx_dielectric(cosine,c.ior);f.cos_transmit=dielectric.y;
    if(model==1u){f.reflection=vec3<f32>(dielectric.x);if(glass){f.transmission=vec3<f32>(1.0-dielectric.x);}return f;}
    let real_f0=ggx_f0(c.ior);let t=clamp(rounded_divide(rounded_add(dielectric.x,-real_f0),rounded_add(1.0,-real_f0)),0.0,1.0);
    let u=rounded_add(1.0,-t);let fresnel=vec3<f32>(rounded_add(u*c.multiscatter.x,t),rounded_add(u*c.multiscatter.y,t),rounded_add(u*c.multiscatter.z,t));
    f.reflection=fresnel*c.reflection_tint;
    if(glass){f.transmission=vec3<f32>(rounded_add(1.0,-fresnel.x),rounded_add(1.0,-fresnel.y),rounded_add(1.0,-fresnel.z))*c.transmission_tint;}
    return f;
}
// bsdf_microfacet_estimate_albedo with reflection evaluated (one LUT call site).
fn principled_albedo(s:ptr<function,SvmState>,c:SvmClosure,transmission:bool)->vec3<f32>{
    let cos_ni=dot((*s).geometry.wi,c.N);let model=u32(c.roughness);
//#if SVM_BSDF_THIN_FILM
    if(c.a>0.1){let f=principled_fresnel(c,cos_ni);return f.reflection+select(vec3<f32>(0),f.transmission,transmission&&c.kind==CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID);}
//#endif
    if(model==1u&&!(c.ior>1.0)){return vec3<f32>(ggx_dielectric(cos_ni,c.ior).x);}
    var z=0.5;var table=data_word(D_ggx_gen_schlick_s);
    if(model!=5u){z=sqrt(abs((c.ior-1.0)/(c.ior+1.0)));table=data_word(D_ggx_gen_schlick_ior_s);}
    let v=microfacet_lut3(s,sqrt(sqrt(c.alpha_x*c.alpha_y)),cos_ni,z,table);
    var f0=c.multiscatter;if(model==1u){f0=vec3<f32>(ggx_f0(c.ior));}
    let f=f0+v*(vec3<f32>(1)-f0);if(model!=4u){return f;}
    return f*c.reflection_tint+select(vec3<f32>(0),(vec3<f32>(1)-f)*c.transmission_tint,transmission);
}
// microfacet_ggx_preserve_energy.
fn principled_preserve(s:ptr<function,SvmState>,c:ptr<function,SvmClosure>,wi:vec3<f32>,fss:vec3<f32>){
    let mu=dot(wi,(*c).N);let rough=sqrt(sqrt((*c).alpha_x*(*c).alpha_y));var e:f32;var average:f32;
    if((*c).kind!=CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID){e=microfacet_lut2(s,rough,mu,data_word(D_ggx_E));average=microfacet_lut1(s,rough,data_word(D_ggx_Eavg),32u);}
    else{var ior=(*c).ior;var table=data_word(D_ggx_glass_E);var average_table=data_word(D_ggx_glass_Eavg);
        if(ior<1.0){ior=rounded_divide(1.0,ior);table=data_word(D_ggx_glass_inv_E);average_table=data_word(D_ggx_glass_inv_Eavg);}
        let z=sqrt(abs((ior-1.0)/(ior+1.0)));e=microfacet_lut3(s,rough,mu,z,table);average=microfacet_lut2_size(s,rough,z,average_table,16u);}
    if((*s).error!=0u){return;}
    let missing=(1.0-e)/e;(*c).energy_scale=1.0+missing;
    if(any(fss!=vec3<f32>(1))){let fms=fss*average/(vec3<f32>(1)-fss*(1.0-average));let darkening=(vec3<f32>(1)+fms*missing)/(*c).energy_scale;
        (*c).weight*=darkening;(*c).sample_weight*=principled_average(darkening);}
}
fn principled_orthonormals(n:vec3<f32>,wi:vec3<f32>)->array<vec3<f32>,2>{
    let b=surface_normalize(rounded_cross(n,wi));
    if(rounded_dot(b,b)>=0.99){return array<vec3<f32>,2>(rounded_cross(b,n),b);}
    var a=vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x);if(n.x==n.y&&n.x==n.z){a=vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x);}
    a=surface_native_normalize(a);return array<vec3<f32>,2>(a,rounded_cross(n,a));
}
// svm_node_closure_bsdf, CLOSURE_BSDF_PRINCIPLED_ID, including
// principled_bsdf_emission. Drivers inline every WGSL call, so the lobes run in
// native order through one loop with a single call site per shared helper
// (allocation, albedo estimate, energy preservation, layering, store):
// 0 sheen, 1 coat, 2 metallic, 3 transmission or thin-wall reflection,
// 4 thin-wall transmission, 5 specular, 6 diffuse/Oren-Nayar.
fn svm_principled(s:ptr<function,SvmState>,data:u32,mix_weight:f32,flag:u32,visibility:u32){
    let wi=(*s).geometry.wi;let local=(flag&PATH_RAY_EMISSION)!=0u;
    let n=principled_normal(s,svm_byte(data+N_PrincipledBsdfData_normal_offset),(*s).geometry.N);
    let not_diffuse=(visibility&PATH_RAY_VISIBILITY_DIFFUSE)==0u;
    let reflective=data_word(D_caustics_reflective)!=0u||not_diffuse;let refractive=data_word(D_caustics_refractive)!=0u||not_diffuse;
    var weight=vec3<f32>(mix_weight);
    let alpha=clamp(svm_input(s,data+N_PrincipledBsdfData_alpha),0.0,1.0);
    if(alpha<1.0){svm_transparent(s,weight*(1.0-alpha),flag);weight*=alpha;}
    let coat_weight=max(svm_input(s,data+N_PrincipledBsdfData_coat_weight),0.0);let sheen_weight=max(svm_input(s,data+N_PrincipledBsdfData_sheen_weight),0.0);
    var coat_normal=n;if(sheen_weight>1e-5||coat_weight>1e-5){coat_normal=principled_normal(s,svm_byte(data+N_PrincipledBsdfData_coat_normal_offset),n);}
    // maybe_ensure_valid_specular_reflection for the base and coat normals.
    var valid=n;var coat_valid=coat_normal;
    for(var k=0u;k<2u;k++){let v=principled_valid(s,select(n,coat_normal,k==1u));if(k==0u){valid=v;}else{coat_valid=v;}}
    let base_color=max(svm_input3(s,data+N_PrincipledBsdfData_base_color),vec3<f32>(0));let clamped_base=min(base_color,vec3<f32>(1));
    let ior=max(svm_input(s,data+N_PrincipledBsdfData_ior),1e-5);let roughness=clamp(svm_input(s,data+N_PrincipledBsdfData_roughness),0.0,1.0);
    let anisotropic=clamp(svm_input(s,data+N_PrincipledBsdfData_anisotropic),0.0,1.0);
    let multi=load_word(data+N_PrincipledBsdfData_distribution)==CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID;
    let specular_tint=max(svm_input3(s,data+N_PrincipledBsdfData_specular_tint),vec3<f32>(0));
    let thickness=svm_input(s,data+N_PrincipledBsdfData_thin_film_thickness);var film=0.0;
    if(thickness>0.1){
//#if SVM_BSDF_THIN_FILM else svm_error(s,2u);return;
        film=max(svm_input(s,data+N_PrincipledBsdfData_thin_film_ior),1e-5);
        let table=data_word(D_thin_film_table);let count=table_count(G_lookup_table);if(table>count||6u*THIN_FILM_TABLE_SIZE>count-table){svm_error(s,512u);return;}
//#endif
    }
    let film_thickness=select(0.0,thickness,thickness>0.1);
    var alpha_x=roughness*roughness;var alpha_y=alpha_x;var tangent=vec3<f32>(0);let ti=svm_byte(data+N_PrincipledBsdfData_tangent_offset);
    if(anisotropic>0.0&&ti!=SVM_STACK_INVALID){
        tangent=svm_load3(s,ti);let aspect=sqrt(1.0-anisotropic*0.9);alpha_x=rounded_divide(alpha_x,aspect);alpha_y*=aspect;
        let rotation=svm_input(s,data+N_PrincipledBsdfData_anisotropic_rotation);
        if(rotation!=0.0){tangent=principled_rotate(tangent,n,rotation*6.283185307179586f);}
    }
    let metallic=clamp(svm_input(s,data+N_PrincipledBsdfData_metallic),0.0,1.0);
    let wall=load_word(data+N_PrincipledBsdfData_thin_wall);let wall_offset=svm_byte(data+N_PrincipledBsdfData_thin_wall+4u);
    var thin_wall=wall!=0u;if(wall_offset!=SVM_STACK_INVALID){thin_wall=svm_load_bits(s,wall_offset)!=0u;}
    let transmission=clamp(svm_input(s,data+N_PrincipledBsdfData_transmission_weight),0.0,1.0);
    let level=max(svm_input(s,data+N_PrincipledBsdfData_specular_ior_level),0.0);var eta=ior;var f0=ggx_f0(eta);
    if(level!=0.5){f0*=2.0*level;let root=sqrt(clamp(f0,0.0,0.99));eta=rounded_divide(1.0+root,1.0-root);if(ior<1.0){eta=rounded_divide(1.0,eta);}}
    let backfacing=((*s).geometry.runtime_flag&SR_BACKFACING)!=0u;
    var thin_reflectance=vec3<f32>(0);var thin_transmittance=vec3<f32>(0);var thin_rough=0.0;
    if((*s).error!=0u){return;}
    for(var lobe=0u;lobe<7u;lobe++){
        var enabled_lobe=false;var kind=CLOSURE_BSDF_MICROFACET_GGX_ID;var w=weight;var extra=false;var may_be_local=false;
        switch(lobe){
            case 0u:{enabled_lobe=sheen_weight>1e-5;kind=CLOSURE_BSDF_SHEEN_ID;may_be_local=true;
                if(enabled_lobe){w=sheen_weight*max(svm_input3(s,data+N_PrincipledBsdfData_sheen_tint),vec3<f32>(0))*weight;}}
            case 1u:{enabled_lobe=coat_weight>1e-5&&reflective;w=coat_weight*weight;may_be_local=true;}
            case 2u:{enabled_lobe=metallic>1e-5&&reflective;w=metallic*weight;extra=true;}
            case 3u:{enabled_lobe=transmission>1e-5&&(reflective||refractive);w=transmission*weight;extra=!thin_wall;
                if(enabled_lobe&&thin_wall){
//#if SVM_BSDF_THIN_WALL else svm_error(s,2u);return;
                    // bsdf_thin_glass_fresnel: front and (with a film) back interface.
                    var fresnel:SvmClosure;fresnel.kind=CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID;fresnel.roughness=4.0;fresnel.ior=ior;fresnel.multiscatter=ggx_f0(ior)*specular_tint;
                    fresnel.reflection_tint=select(vec3<f32>(0),vec3<f32>(1),reflective);fresnel.transmission_tint=select(vec3<f32>(0),vec3<f32>(1),refractive);fresnel.a=film_thickness;fresnel.b=film;
                    var front:PrincipledFresnel;var back:PrincipledFresnel;var cosine=dot(valid,wi);
                    for(var side=0u;side<select(1u,2u,film_thickness>0.1);side++){
                        if(side==1u){fresnel.ior=rounded_divide(1.0,ior);fresnel.b=film*fresnel.ior;cosine=-front.cos_transmit;}
                        let f=principled_fresnel(fresnel,cosine);if(side==0u){front=f;}back=f;}
                    if(any(front.reflection!=vec3<f32>(0))||any(front.transmission!=vec3<f32>(0))){
                        var c=vec3<f32>(0);if(front.cos_transmit!=0.0){c=pow(clamped_base,vec3<f32>(-1.0/front.cos_transmit));}
                        let denominator=vec3<f32>(1)-(back.reflection*c)*(back.reflection*c);
                        thin_transmittance=select(vec3<f32>(0),c*front.transmission*back.transmission/denominator,denominator!=vec3<f32>(0));
                        thin_reflectance=front.reflection+thin_transmittance*back.reflection*c;}
                    enabled_lobe=any(thin_reflectance!=vec3<f32>(0));w=thin_reflectance*(transmission*weight);
//#endif
                }
                else{kind=CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID;}}
            case 4u:{if(transmission>1e-5&&thin_wall&&any(thin_transmittance!=vec3<f32>(0))){
                    let r=roughness*roughness*sqrt(3.4*(ior-1.0)*((ior-0.5)*(ior-0.5))/((ior*ior)*ior));thin_rough=min(select(0.0,r,r>0.0),1.0);
                    w=thin_transmittance*(transmission*weight);
                    if((visibility&VIS_CAMERA)==0u&&thin_rough*thin_rough<=2e-10){svm_transparent(s,w,flag);}
                    else{enabled_lobe=true;kind=PRINCIPLED_THIN_GLASS;}}}
            case 5u:{enabled_lobe=reflective&&(eta!=1.0||thickness>0.1);extra=true;}
            default:{let subsurface=clamp(svm_input(s,data+N_PrincipledBsdfData_subsurface_weight),0.0,1.0);
                if(subsurface>1e-5){svm_error(s,16u);return;}
                let diffuse_roughness=clamp(svm_input(s,data+N_PrincipledBsdfData_diffuse_roughness),0.0,1.0);
                enabled_lobe=true;kind=select(CLOSURE_BSDF_DIFFUSE_ID,CLOSURE_BSDF_OREN_NAYAR_ID,!(diffuse_roughness<1e-5));w=base_color*(1.0-subsurface)*weight;
                thin_rough=diffuse_roughness;}
        }
        if((*s).error!=0u){return;}
        if(enabled_lobe){
            var c=principled_new(kind,w);let i=principled_slot(s,c,local&&may_be_local,extra);
            if(i!=NONE){
                var albedo=false;var albedo_transmission=false;var preserve=false;var fss=vec3<f32>(1);var preserve_wi=wi;var layer=false;
                switch(lobe){
                    case 0u:{c.N=surface_normalize(n+clamp(coat_weight,0.0,1.0)*(coat_normal-n));
                        c.roughness=clamp(clamp(svm_input(s,data+N_PrincipledBsdfData_sheen_roughness),0.0,1.0),1e-3,1.0);
                        let frame=principled_orthonormals(c.N,wi);c.T=frame[0];c.multiscatter=frame[1];
                        let cos_ni=dot(c.N,wi);let table=data_word(D_sheen_ltc);var ltc=vec3<f32>(0);
                        for(var k=0u;k<3u;k++){ltc[k]=microfacet_lut2(s,cos_ni,c.roughness,table+k*1024u);}
                        c.a=ltc.x;c.b=ltc.y;
                        if(abs(c.a)<1e-5||ltc.z<1e-5){var none:SvmClosure;none.kind=PRINCIPLED_CLOSURE_NONE;none.weight=c.weight;none.N=c.N;c=none;}
                        else{c.weight*=ltc.z;c.sample_weight*=ltc.z;(*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_EVAL;layer=true;}}
                    case 1u:{let coat_ior=max(svm_input(s,data+N_PrincipledBsdfData_coat_ior),1.0);let r=clamp(svm_input(s,data+N_PrincipledBsdfData_coat_roughness),0.0,1.0);
                        c.N=coat_valid;c.ior=coat_ior;c.alpha_x=r*r;c.alpha_y=c.alpha_x;principled_ggx(s,&c,false);c.roughness=1.0;
                        albedo=true;preserve=true;fss=vec3<f32>(principled_fss(coat_ior));layer=true;}
                    case 2u:{c.N=valid;c.ior=1.0;c.T=tangent;c.alpha_x=alpha_x;c.alpha_y=alpha_y;principled_ggx(s,&c,false);
                        c.roughness=5.0;c.multiscatter=clamped_base;c.a=film_thickness;c.b=film;let tint=min(specular_tint,vec3<f32>(1));
                        if(any(tint!=vec3<f32>(1))){let f=6.0/7.0;let f5=(f*f)*(f*f)*f;c.reflection_tint=(clamped_base+f5*(vec3<f32>(1)-clamped_base))*(7.0/(f5*f))*(vec3<f32>(1)-tint);}
                        albedo=true;preserve=multi;fss=clamped_base+(1.0/21.0)*(vec3<f32>(1)-clamped_base)-c.reflection_tint*(1.0/126.0);}
                    case 3u:{if(thin_wall){c.N=valid;c.alpha_x=roughness*roughness;c.alpha_y=c.alpha_x;c.ior=1.0;principled_ggx(s,&c,false);preserve=true;fss=specular_tint;}
                        else{c.N=valid;c.alpha_x=roughness*roughness;c.alpha_y=c.alpha_x;c.ior=select(ior,rounded_divide(1.0,ior),backfacing);
                            c.a=film_thickness;c.b=select(film,film*c.ior,backfacing);
                            c.reflection_tint=select(vec3<f32>(0),vec3<f32>(1),reflective);c.transmission_tint=select(vec3<f32>(0),sqrt(clamped_base),refractive);
                            c.multiscatter=ggx_f0(ior)*specular_tint;principled_ggx(s,&c,true);albedo=true;albedo_transmission=true;preserve=multi;}}
                    case 4u:{c.N=-valid;c.alpha_x=thin_rough;c.alpha_y=thin_rough;c.ior=1.0;c.energy_scale=1.0;
                        (*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_TRANSMISSION|select(0u,SR_BSDF_HAS_EVAL,thin_rough*thin_rough>2e-10);
                        preserve=true;preserve_wi=rounded_add3(wi,-((2.0*valid)*rounded_dot(wi,valid)));}
                    case 5u:{c.N=valid;c.ior=eta;c.T=tangent;c.alpha_x=alpha_x;c.alpha_y=alpha_y;c.multiscatter=f0*specular_tint;c.a=film_thickness;c.b=film;
                        c.reflection_tint=vec3<f32>(1);principled_ggx(s,&c,false);albedo=true;preserve=multi;layer=true;}
                    default:{c.N=n;(*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_EVAL;
                        if(c.kind==CLOSURE_BSDF_OREN_NAYAR_ID){
                            // bsdf_oren_nayar_param, as in svm_bsdf (shared rounding adapters).
                            let param=svm_oren_param(thin_rough,clamped_base,rounded_dot(n,wi));
                            c.roughness=thin_rough;c.a=param.a;c.b=param.b;c.multiscatter=param.multiscatter;}}
                }
                // bsdf_microfacet_setup_fresnel_generalized_schlick (exponent < 0, f90 = 1).
                if((lobe==3u&&!thin_wall)||lobe==5u){c.roughness=4.0;c.multiscatter=clamp(c.multiscatter,vec3<f32>(0),vec3<f32>(1));c.fresnel_f0=c.multiscatter.x;
                    fss=c.transmission_tint;if(all(fss==vec3<f32>(0))){let real=ggx_f0(c.ior);let t=clamp((principled_fss(c.ior)-real)/(1.0-real),0.0,1.0);
                        fss=c.reflection_tint*(c.multiscatter+t*(vec3<f32>(1)-c.multiscatter));}}
                var estimate=vec3<f32>(1);
                if(albedo){estimate=principled_albedo(s,c,albedo_transmission);c.sample_weight*=principled_average(estimate);}
                if(preserve){principled_preserve(s,&c,preserve_wi,fss);}
                // closure_layering_weight(bsdf_albedo(reflection), weight); reflection-only lobes.
                if(layer){let layer_albedo=c.weight*estimate;let ratio=select(vec3<f32>(0),layer_albedo/weight,weight!=vec3<f32>(0));
                    weight*=clamp(1.0-max(max(ratio.x,ratio.y),ratio.z),0.0,1.0);}
                principled_store(s,i,c);
            }
        }
        if(lobe==1u&&coat_weight>1e-5){
            let coat_ior=max(svm_input(s,data+N_PrincipledBsdfData_coat_ior),1.0);let tint=max(svm_input3(s,data+N_PrincipledBsdfData_coat_tint),vec3<f32>(0));
            if(any(tint!=vec3<f32>(1))){let cos_ni=dot(wi,coat_valid);let inverse=1.0/coat_ior;let depth=1.0/sqrt(1.0-inverse*inverse*(1.0-cos_ni*cos_ni));
                weight*=vec3<f32>(1)+coat_weight*(pow(tint,vec3<f32>(depth))-vec3<f32>(1));}
        }
        if(lobe==1u){let emission=svm_input3(s,data+N_PrincipledBsdfData_emission_color)*svm_input(s,data+N_PrincipledBsdfData_emission_strength);
            if(any(emission!=vec3<f32>(0))){let e=emission*weight;if(((*s).output.runtime_flag&SR_EMISSION)!=0u){(*s).output.emission+=e;}else{(*s).output.emission=e;(*s).output.runtime_flag|=SR_EMISSION;}}}
        if(lobe==2u&&metallic>1e-5){weight*=1.0-metallic;}
        if(lobe==4u&&transmission>1e-5){weight*=1.0-transmission;}
    }
}
//#endif
//#if SVM_BSDF_THIN_FILM
// Thin-film iridescence (pinned closure/bsdf_util.h: Belcour and Barla's Airy
// summation with the native Fourier sensitivity table). Table reads are bounds
// checked against the uploaded lookup table (error 512, no substitute value).
struct ThinFilmFresnel {r:vec2<f32>,cos_t:f32,phase:vec2<f32>}
fn thin_film_dielectric(cosine:f32,eta:f32)->ThinFilmFresnel{
    var f:ThinFilmFresnel;f.r=vec2<f32>(1);f.phase=vec2<f32>(1);
    let square=rounded_add(eta*eta,-rounded_add(1.0,-cosine*cosine));if(square<=0.0){return f;}
    let ci=abs(cosine);let ct=-rounded_divide(rounded_sqrt(square),eta);f.cos_t=ct;
    let rs=rounded_divide(rounded_add(ci,eta*ct),rounded_add(ci,-eta*ct));let rp=rounded_divide(rounded_add(ct,eta*ci),rounded_add(eta*ci,-ct));
    f.phase=vec2<f32>(select(-1.0,1.0,rs>=0.0),select(-1.0,1.0,rp>=0.0));f.r=vec2<f32>(rs*rs,rp*rp);return f;
}
fn thin_film_f82(cosine:f32,f0:f32,b:f32)->f32{let s=clamp(1.0-cosine,0.0,1.0);let s5=(s*s)*(s*s)*s;return clamp(f0+s5*(1.0-f0)-b*cosine*s5*s,0.0,1.0);}
fn thin_film_table(x:f32,offset:u32)->f32{
    let value=clamp(x,0.0,1.0)*f32(THIN_FILM_TABLE_SIZE-1u);let index=min(u32(value),THIN_FILM_TABLE_SIZE-1u);let t=value-f32(index);
    let a=heap_float(table_at(G_lookup_table,offset+index,SIZE_lookup_table));if(t==0.0){return a;}
    return (1.0-t)*a+t*heap_float(table_at(G_lookup_table,offset+min(index+1u,THIN_FILM_TABLE_SIZE-1u),SIZE_lookup_table));
}
fn thin_film_airy(channel:u32,r12:f32,r23:f32,opd:f32,phasor:vec2<f32>)->f32{
    let base=data_word(D_thin_film_table);let count=table_count(G_lookup_table);
    if(base>count||6u*THIN_FILM_TABLE_SIZE>count-base){atomicOr(&heap[p(17u)/4u],512u);return 0.0;}
    let t121=1.0-r12;let r123=r12*r23;let root=sqrt(r123);let rs=(t121*t121)*r23/(1.0-r123);
    var accumulator=phasor;var r=rs+r12;var cm=rs-t121;
    for(var m=1u;m<4u;m++){cm*=root;let x=6.283185307179586*(f32(m)*opd)/60000.0;
        let sensitivity=vec2<f32>(thin_film_table(x,base+channel*THIN_FILM_TABLE_SIZE),thin_film_table(x,base+(channel+3u)*THIN_FILM_TABLE_SIZE));
        r+=cm*2.0*(accumulator.x*sensitivity.x+accumulator.y*sensitivity.y);
        accumulator=vec2<f32>(accumulator.x*phasor.x-accumulator.y*phasor.y,accumulator.x*phasor.y+accumulator.y*phasor.x);}
    return r;
}
// fresnel_iridescence_channel: (reflectance, cos_theta_3; 0 when not refracted).
fn thin_film_channel(channel:u32,thickness:f32,film:f32,n:f32,k:f32,f82:f32,cosine:f32,conductive:bool)->vec2<f32>{
    var film_ior=film;if(thickness<1.0){let s=select(select((3.0-2.0*thickness)*(thickness*thickness),1.0,thickness>=1.0),0.0,thickness<0.0);film_ior=1.0+s*(film-1.0);}
    let top=thin_film_dielectric(cosine,film_ior);if(all(top.r==vec2<f32>(1))){return vec2<f32>(1,0);}
    var r23:vec2<f32>;var phase_s:vec2<f32>;var phase_p:vec2<f32>;var cos3=0.0;
    if(conductive){
        let cosi=-top.cos_t;let e1=film_ior*film_ior;let e2=n*n;let k2=k*k;let cross=2.0*n*k;
        let t1=e2-k2-e1*(1.0-cosi*cosi);let t2=sqrt(t1*t1+cross*cross);let u2=max(0.5*(t2+t1),0.0);let v2=max(0.5*(t2-t1),0.0);let u=sqrt(u2);let v=sqrt(v2);
        let rn=n/film_ior;let rk=(k/film_ior)*(k/film_ior);let f0=((rn-1.0)*(rn-1.0)+rk)/((rn+1.0)*(rn+1.0)+rk);
        let f=6.0/7.0;let f5=(f*f)*(f*f)*f;let b=(7.0/(f5*f))*(f0+f5*(1.0-f0)-f82);let rs=thin_film_f82(cosi,f0,b);r23=vec2<f32>(rs);
        let re_s=-u2-v2+(film_ior*cosi)*(film_ior*cosi);let im_s=-2.0*film_ior*cosi*v;let mag_s=sqrt(re_s*re_s+im_s*im_s);
        phase_s=select(vec2<f32>(re_s/mag_s,im_s/mag_s),vec2<f32>(1,0),mag_s==0.0);
        let re_p=((e2+k2)*cosi)*((e2+k2)*cosi)-e1*(u2+v2);let im_p=2.0*film_ior*cosi*(cross*u-(e2-k2)*v);let mag_p=sqrt(re_p*re_p+im_p*im_p);
        phase_p=select(vec2<f32>(re_p/mag_p,im_p/mag_p),vec2<f32>(1,0),mag_p==0.0);
    }else{
        let bottom=thin_film_dielectric(-top.cos_t,n/film_ior);cos3=bottom.cos_t;if(all(bottom.r==vec2<f32>(1))){return vec2<f32>(1,cos3);}
        r23=bottom.r;phase_s=vec2<f32>(bottom.phase.x,0);phase_p=vec2<f32>(bottom.phase.y,0);
    }
    let opd=-2.0*film_ior*thickness*top.cos_t;
    // Airy summation per polarization, then their average.
    var sum=0.0;for(var pol=0u;pol<2u;pol++){sum+=thin_film_airy(channel,top.r[pol],r23[pol],opd,select(phase_s*-top.phase.x,phase_p*-top.phase.y,pol==1u));}
    return vec2<f32>(clamp(0.5*sum,0.0,1.0),cos3);
}
//#endif
