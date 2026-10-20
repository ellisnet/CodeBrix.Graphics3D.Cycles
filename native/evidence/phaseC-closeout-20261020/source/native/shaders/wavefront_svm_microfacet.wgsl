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
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);
}
