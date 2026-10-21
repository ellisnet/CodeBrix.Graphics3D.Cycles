// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from Cycles: native SVM closure allocation/setup ported to WGSL.
// Source lineage and BSD notice are in
// native/THIRD-PARTY-NOTICES.txt and parity/licenses/BSD-3-Clause.txt.
struct SvmClosure {
    weight:vec3<f32>, sample_weight:f32, N:vec3<f32>, kind:u32,
    roughness:f32, a:f32, b:f32, multiscatter:vec3<f32>,
    alpha_x:f32, alpha_y:f32, ior:f32, energy_scale:f32, T:vec3<f32>,
    fresnel_f0:f32,reflection_tint:vec3<f32>,transmission_tint:vec3<f32>,
}
struct SvmResult {
    closure_base:u32, count:u32, remaining:u32,
    runtime_flag:u32, error:u32, lcg_state:u32, N:vec3<f32>, emission:vec3<f32>, transparency:vec3<f32>,
}
// Per-path closure storage stays in the arena, avoiding large private arrays
// that exceed register/spill limits on mobile GPU compilers. Native packed stride.
fn svm_closure_read(r:SvmResult,index:u32)->SvmClosure{
    var c:SvmClosure;if(index>=r.count){return c;}let base=r.closure_base+index*SVM_CLOSURE_BYTES;
    c.weight=heap_vec3(base);c.sample_weight=heap_float(base+12u);c.N=heap_vec3(base+16u);c.kind=load_word(base+28u);
    c.roughness=heap_float(base+32u);c.a=heap_float(base+36u);c.b=heap_float(base+40u);c.multiscatter=heap_vec3(base+44u);
    c.alpha_x=heap_float(base+C_alpha_x);c.alpha_y=heap_float(base+C_alpha_y);c.ior=heap_float(base+C_ior);c.energy_scale=heap_float(base+C_energy_scale);c.T=heap_vec3(base+C_T);c.fresnel_f0=heap_float(base+C_fresnel_f0);c.reflection_tint=heap_vec3(base+C_reflection_tint);c.transmission_tint=heap_vec3(base+C_transmission_tint);return c;
}
fn svm_closure_write(base:u32,c:SvmClosure){
    store_word(base,bitcast<u32>(c.weight.x));store_word(base+4u,bitcast<u32>(c.weight.y));store_word(base+8u,bitcast<u32>(c.weight.z));
    store_word(base+12u,bitcast<u32>(c.sample_weight));store_word(base+16u,bitcast<u32>(c.N.x));store_word(base+20u,bitcast<u32>(c.N.y));store_word(base+24u,bitcast<u32>(c.N.z));store_word(base+28u,c.kind);
    store_word(base+32u,bitcast<u32>(c.roughness));store_word(base+36u,bitcast<u32>(c.a));store_word(base+40u,bitcast<u32>(c.b));
    store_word(base+44u,bitcast<u32>(c.multiscatter.x));store_word(base+48u,bitcast<u32>(c.multiscatter.y));store_word(base+52u,bitcast<u32>(c.multiscatter.z));
    store_word(base+C_alpha_x,bitcast<u32>(c.alpha_x));store_word(base+C_alpha_y,bitcast<u32>(c.alpha_y));store_word(base+C_ior,bitcast<u32>(c.ior));store_word(base+C_energy_scale,bitcast<u32>(c.energy_scale));
    store_word(base+C_fresnel_f0,bitcast<u32>(c.fresnel_f0));
    for(var k=0u;k<3u;k++){store_word(base+C_reflection_tint+k*4u,bitcast<u32>(c.reflection_tint[k]));store_word(base+C_transmission_tint+k*4u,bitcast<u32>(c.transmission_tint[k]));}
    store_word(base+C_T,bitcast<u32>(c.T.x));store_word(base+C_T+4u,bitcast<u32>(c.T.y));store_word(base+C_T+8u,bitcast<u32>(c.T.z));
}
fn svm_lcg(index:u32)->u32{
    // Cycles hash_uint3 / Jenkins finalizer, used by lcg_state_init.
    var a=0xdeadbf08u+(state_word(S_path_rng_pixel,index)^0xb4bc3953u);
    var b=0xdeadbf08u+state_half(S_path_rng_offset,index);var c=0xdeadbf08u+state_word(S_path_sample,index);
    c^=b;c-=(b<<14u)|(b>>18u);a^=c;a-=(c<<11u)|(c>>21u);
    b^=a;b-=(a<<25u)|(a>>7u);c^=b;c-=(b<<16u)|(b>>16u);
    a^=c;a-=(c<<4u)|(c>>28u);b^=a;b-=(a<<14u)|(a>>18u);c^=b;c-=(b<<24u)|(b>>8u);return c;
}
fn svm_allocate(s:ptr<function,SvmState>,kind:u32,weight:vec3<f32>)->u32{
    if((*s).error!=0u||(*s).output.remaining==0u){return NONE;}
    let index=(*s).output.count;
    if(index>=SVM_CLOSURE_CAPACITY){svm_error(s,16u);return NONE;}
    if((*s).output.closure_base==0u){svm_error(s,1u);return NONE;}
    var c:SvmClosure;c.kind=kind;c.weight=weight;svm_closure_write((*s).output.closure_base+index*SVM_CLOSURE_BYTES,c);
    (*s).output.count++;(*s).output.remaining--;return index;
}
// Original CodeBrix arithmetic adapter: the pinned ensure_valid_specular_reflection
// formula with separately rounded binary32 products, sums, square roots and
// divisions (no driver contraction or approximate builtins). The adjusted
// normal feeds sharp GGX lobes, where one ulp changes the sampled density.
fn svm_valid_normal(ng:vec3<f32>,wi:vec3<f32>,n:vec3<f32>)->vec3<f32>{
    let reflection=rounded_add3((2.0*rounded_dot(n,wi))*n,-wi);let iz=rounded_dot(wi,ng);let threshold=min(0.9*iz,0.01);
    if(rounded_dot(ng,reflection)>=threshold){return n;}
    var x=rounded_add3(n,-(rounded_dot(n,ng)*ng));let magnitude=rounded_sqrt(rounded_dot(x,x));
    if(magnitude!=0.0){x=x*rounded_divide(1.0,magnitude);}else{x=n;}
    let ix=rounded_dot(wi,x);let a=rounded_add(ix*ix,iz*iz);let b=2.0*rounded_add(a,iz*threshold);let tz=rounded_add(threshold,iz);let c=tz*tz;
    let root=rounded_sqrt(max(rounded_add(b*b,-((4.0*a)*c)),0.0));
    let nz2=rounded_divide(0.25*rounded_add(b,select(-root,root,ix<0.0)),a);
    return rounded_add3(rounded_sqrt(max(rounded_add(1.0,-nz2),0.0))*x,rounded_sqrt(max(nz2,0.0))*ng);
}
// Original CodeBrix arithmetic adapter. Add already-rounded binary32 products
// without a driver contracting the multiply/add before 1-E cancellation.
// Oren's two calls have positive normal a, |b| < a, and a normal positive sum.
// Align significands with three guard bits and round ties to an even mantissa.
fn svm_oren_add(a:f32,b:f32)->f32{
    let ax=bitcast<u32>(a);let bx=bitcast<u32>(b);
    if((bx&0x7fffffffu)==0u){return a;}
    var exponent=(ax>>23u)&255u;let delta=exponent-((bx>>23u)&255u);
    let ma=((ax&0x7fffffu)|0x800000u)<<3u;
    var mb=((bx&0x7fffffu)|0x800000u)<<3u;
    if(delta>=31u){mb=1u;}
    else if(delta>0u){mb=(mb>>delta)|select(0u,1u,(mb&((1u<<delta)-1u))!=0u);}
    var sum=ma+mb;if((bx&0x80000000u)!=0u){sum=ma-mb;}
    if(sum>=0x8000000u){sum=(sum>>1u)|(sum&1u);exponent++;}
    else{while(sum<0x4000000u){sum<<=1u;exponent--;}}
    let tail=sum&7u;var mantissa=sum>>3u;
    if(tail>4u||(tail==4u&&(mantissa&1u)!=0u)){mantissa++;}
    if(mantissa>=0x1000000u){mantissa>>=1u;exponent++;}
    return bitcast<f32>((exponent<<23u)|(mantissa&0x7fffffu));
}
fn svm_oren_g(cosine:f32)->f32{
    if(cosine<1e-6){return (1.5707963267948966f-2.0f/3.0f)-cosine;}
    let sine=sqrt(max(1.0-cosine*cosine,0.0));let theta=c_safe_acosf(cosine);
    return sine*(theta-2.0f/3.0f-sine*cosine)+(2.0f/3.0f)*(sine/cosine)*(1.0-sine*sine*sine);
}
fn svm_transparent(s:ptr<function,SvmState>,weight:vec3<f32>,flag:u32){
    let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));if(!(sample_weight>=1e-5)){return;}
    (*s).output.transparency+=weight;
    if(((*s).output.runtime_flag&SR_TRANSPARENT)!=0u){
        for(var i=0u;i<(*s).output.count;i++){var c=svm_closure_read((*s).output,i);if(c.kind==CLOSURE_BSDF_TRANSPARENT_ID){
            c.weight+=weight;c.sample_weight+=sample_weight;svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);break;}}
    }else{
        (*s).output.runtime_flag|=SR_BSDF|SR_TRANSPARENT;
        if((flag&PATH_RAY_TERMINATE)!=0u){(*s).output.remaining=1u;}
        let i=svm_allocate(s,CLOSURE_BSDF_TRANSPARENT_ID,weight);
        if(i!=NONE){var c=svm_closure_read((*s).output,i);c.sample_weight=sample_weight;c.N=(*s).geometry.N;svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);}
        else if((flag&PATH_RAY_TERMINATE)!=0u){(*s).output.remaining=0u;}
    }
}
fn svm_bsdf(s:ptr<function,SvmState>,node:u32,flag:u32,visibility:u32){
    let kind=load_word(node+N_ClosureBsdf_closure_type);let mix_weight=svm_optional(s,svm_byte(node+N_ClosureBsdf_mix_weight_offset),1.0);
    var bytes=0u;switch(kind){case CLOSURE_BSDF_DIFFUSE_ID:{bytes=SZ_DiffuseBsdfData;}
        case CLOSURE_BSDF_TRANSLUCENT_ID,CLOSURE_BSDF_TRANSPARENT_ID:{bytes=SZ_SimpleBsdfData;}
        case CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID:{bytes=SZ_RefractionBsdfData;}
        case CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID:{bytes=SZ_GlassBsdfData;}
        case CLOSURE_BSDF_MICROFACET_GGX_ID,CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID:{bytes=SZ_GlossyBsdfData;}
//#if SVM_BSDF_PRINCIPLED
        case CLOSURE_BSDF_PRINCIPLED_ID:{bytes=SZ_PrincipledBsdfData;}
//#endif
        default:{svm_error(s,16u);return;}}
    let data=svm_take(s,bytes);if((*s).error!=0u||mix_weight==0.0){return;}
//#if SVM_BSDF_PRINCIPLED
    if(kind==CLOSURE_BSDF_PRINCIPLED_ID){svm_principled(s,data,mix_weight,flag,visibility);return;}
//#endif
    // Scene specialization (pipelines.h) removes closure setup the uploaded SVM
    // programs cannot reach; reaching a removed handler is reported, never shaded.
    if(kind==CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID||kind==CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID){
//#if SVM_BSDF_GLASS else svm_error(s,2u);return;
        svm_glass(s,data,kind,mix_weight,visibility);return;
//#endif
    }
    if(kind==CLOSURE_BSDF_MICROFACET_GGX_ID||kind==CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID){
//#if SVM_BSDF_GLOSSY else svm_error(s,2u);return;
        svm_glossy(s,data,kind,mix_weight,visibility);return;
//#endif
    }
    let raw_weight=(*s).weight*mix_weight;
    if(kind==CLOSURE_BSDF_TRANSPARENT_ID){svm_transparent(s,raw_weight,flag);return;}
    let normal_index=svm_byte(data+select(N_SimpleBsdfData_normal_offset,N_DiffuseBsdfData_normal_offset,kind==CLOSURE_BSDF_DIFFUSE_ID));
    var n=(*s).geometry.N;if(normal_index!=SVM_STACK_INVALID){n=svm_load3(s,normal_index);}
    let magnitude=length(n);if(magnitude!=0.0){n*=1.0/magnitude;}else{n=(*s).geometry.N;}
    var roughness=0.0;var color=vec3<f32>(0);
    if(kind==CLOSURE_BSDF_DIFFUSE_ID){roughness=svm_input(s,data+N_DiffuseBsdfData_roughness);
        if(!(roughness<1e-5)){color=clamp(svm_input3(s,data+N_DiffuseBsdfData_color),vec3<f32>(0),vec3<f32>(1));}}
    else if(((*s).geometry.shader_flag&SD_USE_BUMP_MAP_CORRECTION)!=0u&&any((*s).geometry.Ng!=n)){
        n=svm_valid_normal((*s).geometry.Ng,(*s).geometry.wi,n);
    }
    let weight=max(raw_weight,vec3<f32>(0));let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));
    if(!(sample_weight>=1e-5)){return;}
    let i=svm_allocate(s,kind,weight);if(i==NONE){return;}
    var closure=svm_closure_read((*s).output,i);closure.N=n;closure.sample_weight=sample_weight;
    (*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_EVAL;
    if(kind==CLOSURE_BSDF_TRANSLUCENT_ID){(*s).output.runtime_flag|=SR_BSDF_HAS_TRANSMISSION;}
    else if(!(roughness<1e-5)){
        let pi=3.141592653589793;let sigma=clamp(roughness,0.0,1.0);
        let a=1.0/(pi+sigma*(1.5707963267948966f-2.0f/3.0f));let b=sigma*a;
        let average=svm_oren_add(a*pi,((6.283185307179586f-5.6f)/3.0f)*b);
        let ems=0.3183098861837907f*(color*color)*(average/(1.0-average))/(vec3<f32>(1)-color*(1.0-average));
        let ev=svm_oren_add(a*pi,b*svm_oren_g(max(dot(n,(*s).geometry.wi),0.0)));
        closure.kind=CLOSURE_BSDF_OREN_NAYAR_ID;closure.roughness=roughness;
        closure.a=a;closure.b=b;closure.multiscatter=ems*(1.0-ev);
    }
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,closure);
}

// Native svm_node_closure_volume: absorption shares the extinction accumulator.
// HG anisotropy uses the existing scalar roughness slot in the private record.
fn svm_volume(s:ptr<function,SvmState>,node:u32){
    let kind=load_word(node+N_ClosureVolume_closure_type);
    if(kind!=CLOSURE_VOLUME_ABSORPTION_ID&&kind!=CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID){svm_error(s,16u);return;}
    let mix_weight=svm_optional(s,svm_byte(node+N_ClosureVolume_mix_weight_offset),1.0);if(mix_weight==0.0){return;}
    var density=mix_weight*max(svm_input(s,node+N_ClosureVolume_density),0.0);
    if((*s).geometry.object_id!=NONE){density*=heap_float(table_at(G_objects,(*s).geometry.object_id,SIZE_objects)+O_volume_density);}
    var weight=(*s).weight;if(kind==CLOSURE_VOLUME_ABSORPTION_ID){weight=vec3<f32>(1)-weight;}weight*=density;
    if(kind==CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID){
        let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));
        if(sample_weight>0.0){let i=svm_allocate(s,kind,weight);if(i!=NONE){
            var c=svm_closure_read((*s).output,i);c.sample_weight=sample_weight;
            let g=svm_input(s,node+N_ClosureVolume_param1);c.roughness=sign(g)*min(abs(g),1.0-0.001);
            svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);(*s).output.runtime_flag|=SR_SCATTER;}}
    }
    (*s).output.transparency+=weight;(*s).output.runtime_flag|=SR_EXTINCTION;
}
