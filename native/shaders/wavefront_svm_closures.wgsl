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
// bsdf_oren_nayar_G with the host's rounding: sin_from_cos, the correctly rounded
// glibc acosf (trig_acos), every product rounded before its sum, scalar quotient.
fn svm_oren_g(cosine:f32)->f32{
    if(cosine<1e-6){return (1.5707963267948966f-2.0f/3.0f)-cosine;}
    let sine=rounded_sqrt(max(rounded_add(1.0,-(cosine*cosine)),0.0));let theta=trig_acos(cosine);
    return rounded_add(sine*rounded_add(rounded_add(theta,-2.0f/3.0f),-(sine*cosine)),((2.0f/3.0f)*rounded_divide(sine,cosine))*rounded_add(1.0,-((sine*sine)*sine)));
}
// bsdf_oren_nayar_param (sigma already saturated, albedo saturated): correctly
// rounded scalar and per-lane float3 quotients, products rounded before sums.
struct SvmOrenParam {a:f32,b:f32,multiscatter:vec3<f32>}
fn svm_oren_param(sigma:f32,albedo:vec3<f32>,nv:f32)->SvmOrenParam{
    let pi=3.141592653589793;var r:SvmOrenParam;
    r.a=rounded_divide(1.0,rounded_add(pi,sigma*(1.5707963267948966f-2.0f/3.0f)));r.b=sigma*r.a;
    let average=svm_oren_add(r.a*pi,((6.283185307179586f-5.6f)/3.0f)*r.b);
    let scale=(0.3183098861837907f*(albedo*albedo))*rounded_divide(average,1.0-average);
    let d=rounded_add3(vec3<f32>(1),-(albedo*(1.0-average)));
    // The three lane quotients through one rounded_divide body (rounded_steps).
    var ems=vec3<f32>(0);let count=rounded_steps(3u);
    for(var k=0u;k<count;k++){
        let q=rounded_divide(select(select(scale.z,scale.y,k==1u),scale.x,k==0u),select(select(d.z,d.y,k==1u),d.x,k==0u));
        ems=select(ems,vec3<f32>(q),vec3<u32>(0u,1u,2u)==vec3<u32>(k));
    }
    let ev=svm_oren_add(r.a*pi,r.b*svm_oren_g(max(nv,0.0)));
    r.multiscatter=ems*(1.0-ev);return r;
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
//#if SVM_BSDF_METALLIC
        case CLOSURE_BSDF_PHYSICAL_CONDUCTOR,CLOSURE_BSDF_F82_CONDUCTOR:{bytes=SZ_MetallicBsdfData;}
//#endif
//#if SVM_BSDF_ASHIKHMIN_SHIRLEY
        case CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID:{bytes=SZ_GlossyBsdfData;}
//#endif
//#if SVM_BSDF_VELVET
        case CLOSURE_BSDF_ASHIKHMIN_VELVET_ID:{bytes=SZ_SimpleBsdfData;}
//#endif
//#if SVM_BSDF_TOON
        case CLOSURE_BSDF_DIFFUSE_TOON_ID,CLOSURE_BSDF_GLOSSY_TOON_ID:{bytes=SZ_ToonBsdfData;}
//#endif
//#if SVM_BSDF_BECKMANN
        case CLOSURE_BSDF_MICROFACET_BECKMANN_ID:{bytes=SZ_GlossyBsdfData;}
        case CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID:{bytes=SZ_RefractionBsdfData;}
        case CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID:{bytes=SZ_GlassBsdfData;}
//#endif
//#if SUBSURFACE
        case SS_CLOSURE_BURLEY,SS_CLOSURE_RANDOM_WALK,SS_CLOSURE_RANDOM_WALK_LEGACY,SS_CLOSURE_RANDOM_WALK_SKIN:{bytes=SZ_BssrdfData;}
//#endif
        default:{svm_error(s,16u);return;}}
    let data=svm_take(s,bytes);if((*s).error!=0u||mix_weight==0.0){return;}
//#if SVM_BSDF_PRINCIPLED
    if(kind==CLOSURE_BSDF_PRINCIPLED_ID){svm_principled(s,data,mix_weight,flag,visibility);return;}
//#endif
//#if SUBSURFACE
    if(kind>=SS_CLOSURE_BURLEY&&kind<=SS_CLOSURE_RANDOM_WALK_SKIN){svm_bssrdf(s,data,kind,mix_weight,flag);return;}
//#endif
//#if SVM_BSDF_METALLIC
    if(kind==CLOSURE_BSDF_PHYSICAL_CONDUCTOR||kind==CLOSURE_BSDF_F82_CONDUCTOR){svm_metallic(s,data,kind,mix_weight,visibility);return;}
//#endif
//#if SVM_BSDF_ASHIKHMIN_SHIRLEY
    if(kind==CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID){svm_ashikhmin_shirley(s,data,mix_weight,visibility);return;}
//#endif
//#if SVM_BSDF_VELVET
    if(kind==CLOSURE_BSDF_ASHIKHMIN_VELVET_ID){svm_velvet(s,data,mix_weight);return;}
//#endif
//#if SVM_BSDF_TOON
    if(kind==CLOSURE_BSDF_DIFFUSE_TOON_ID||kind==CLOSURE_BSDF_GLOSSY_TOON_ID){svm_toon(s,data,kind,mix_weight,visibility);return;}
//#endif
//#if SVM_BSDF_BECKMANN
    if(kind==CLOSURE_BSDF_MICROFACET_BECKMANN_ID||kind==CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID||kind==CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID){
        svm_beckmann(s,data,kind,mix_weight,visibility);return;}
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
    // safe_normalize_fallback(N, sd->N): len() = sqrt(dot) rounded once each, then
    // N * (1 / len) with a correctly rounded reciprocal (WGSL length and '/' are not).
    let magnitude=rounded_sqrt(rounded_dot(n,n));if(magnitude!=0.0){n*=rounded_divide(1.0,magnitude);}else{n=(*s).geometry.N;}
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
        let param=svm_oren_param(clamp(roughness,0.0,1.0),color,rounded_dot(n,(*s).geometry.wi));
        closure.kind=CLOSURE_BSDF_OREN_NAYAR_ID;closure.roughness=roughness;
        closure.a=param.a;closure.b=param.b;closure.multiscatter=param.multiscatter;
    }
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,closure);
}

//#if SUBSURFACE
// Subsurface scattering closures (closure/bssrdf.h; svm/closure.h CLOSURE_BSSRDF_*_ID and the Principled BSDF's
// subsurface lobe). Bssrdf fields in the closure record: radius in multiscatter, albedo in T, anisotropy in a, ior,
// alpha in roughness. expf and sqrtf are correctly rounded (vp_exp_parts, rounded_sqrt), every product is rounded
// before its sum (the oracle kernels' -ffp-contract=off order).
fn ss_exp(x:f32)->f32{let e=vp_exp_parts(vec2<f32>(x,0.0));return ldexp(e.x,i32(e.y));}
// bssrdf_dipole_compute_Rd and bssrdf_dipole_compute_alpha_prime (twelve bisection steps).
fn ss_dipole_rd(alpha_prime:f32,fourthird:f32)->f32{
    let s=rounded_sqrt(3.0*rounded_add(1.0,-alpha_prime));
    return ((0.5*alpha_prime)*rounded_add(1.0,ss_exp(-fourthird*s)))*ss_exp(-s);
}
fn ss_dipole_alpha_prime(rd:f32,fourthird:f32)->f32{
    if(rd<1e-4){return 0.0;}if(rd>=0.995){return 0.999999;}
    var x0=0.0;var x1=1.0;var xmid=0.0;
    for(var i=0u;i<12u;i++){xmid=0.5*rounded_add(x0,x1);if(ss_dipole_rd(xmid,fourthird)<rd){x0=xmid;}else{x1=xmid;}}
    return xmid;
}
// bssrdf_alloc: no closure below the weight cutoff (|average(weight)|, weights not clamped) or without storage.
fn ss_alloc(s:ptr<function,SvmState>,weight:vec3<f32>)->u32{
    let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));if(sample_weight<1e-5){return NONE;}
    return svm_allocate(s,SS_CLOSURE_NONE,weight);
}
// bssrdf_setup with bssrdf_setup_radius for the closure in slot i; channels with a radius below BSSRDF_MIN_RADIUS
// (and the whole Burley closure below a diffuse bounce) become a diffuse closure (bsdf_diffuse_setup).
fn ss_setup(s:ptr<function,SvmState>,i:u32,record:SvmClosure,kind:u32,flag:u32){
    var c=record;
    c.a=select(clamp(c.a,-0.99,0.9),clamp(c.a,-0.99,0.99),kind==SS_CLOSURE_RANDOM_WALK);c.ior=clamp(c.ior,1.01,3.8);
    if(kind==SS_CLOSURE_RANDOM_WALK_SKIN){c.roughness=1.0;}
    var channels=3u;var diffuse=vec3<f32>(0);
    if(kind==SS_CLOSURE_BURLEY&&(flag&SS_PATH_RAY_DIFFUSE_ANCESTOR)!=0u){channels=0u;diffuse=c.weight;}
    else{for(var k=0u;k<3u;k++){if(c.multiscatter[k]<1e-8){diffuse[k]=c.weight[k];c.weight[k]=0.0;c.multiscatter[k]=0.0;channels--;}}}
    if(channels<3u){
        let w=max(diffuse,vec3<f32>(0));let sample_weight=abs((w.x+w.y+w.z)*(1.0/3.0));
        if(sample_weight>=1e-5){let d=svm_allocate(s,CLOSURE_BSDF_DIFFUSE_ID,w);
            if(d!=NONE){var b=svm_closure_read((*s).output,d);b.N=c.N;b.sample_weight=sample_weight;
                svm_closure_write((*s).output.closure_base+d*SVM_CLOSURE_BYTES,b);(*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_EVAL;}}
    }
    if(channels>0u){
        c.kind=kind;c.sample_weight=abs((c.weight.x+c.weight.y+c.weight.z)*(1.0/3.0))*f32(channels);
        if(kind==SS_CLOSURE_BURLEY||kind==SS_CLOSURE_RANDOM_WALK_LEGACY){c.multiscatter*=0.07957746833562851;}
        else if(kind==SS_CLOSURE_RANDOM_WALK_SKIN){
            let inv_eta=rounded_divide(1.0,c.ior);
            let fdr=rounded_add(rounded_add(inv_eta*rounded_add(-1.44*inv_eta,0.71),0.668),0.0636*c.ior);
            let fourthird=rounded_divide((4.0/3.0)*rounded_add(1.0,fdr),rounded_add(1.0,-fdr));
            for(var k=0u;k<3u;k++){let ap=ss_dipole_alpha_prime(c.T[k],fourthird);c.multiscatter[k]*=rounded_sqrt(3.0*rounded_add(1.0,-ap));}
        }
        (*s).output.runtime_flag|=SS_SR_BSSRDF;
    }else{c.kind=SS_CLOSURE_NONE;c.sample_weight=0.0;c.multiscatter=vec3<f32>(0);c.T=vec3<f32>(0);c.a=0.0;c.ior=0.0;c.roughness=0.0;}
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);
}
// The Subsurface Scattering node (svm_node_closure_bsdf, CLOSURE_BSSRDF_*_ID): albedo is the closure weight.
fn svm_bssrdf(s:ptr<function,SvmState>,data:u32,kind:u32,mix_weight:f32,flag:u32){
    let normal_index=svm_byte(data+N_BssrdfData_normal_offset);var n=(*s).geometry.N;if(normal_index!=SVM_STACK_INVALID){n=svm_load3(s,normal_index);}
    let magnitude=rounded_sqrt(rounded_dot(n,n));if(magnitude!=0.0){n*=rounded_divide(1.0,magnitude);}else{n=(*s).geometry.N;}
    let i=ss_alloc(s,(*s).weight*mix_weight);if(i==NONE){return;}
    var c=svm_closure_read((*s).output,i);
    c.multiscatter=max(svm_input3(s,data+N_BssrdfData_radius)*svm_input(s,data+N_BssrdfData_scale),vec3<f32>(0));c.T=(*s).weight;
    // maybe_ensure_valid_specular_reflection
    if(((*s).geometry.shader_flag&SD_USE_BUMP_MAP_CORRECTION)!=0u&&any((*s).geometry.Ng!=n)){n=svm_valid_normal((*s).geometry.Ng,(*s).geometry.wi,n);}
    c.N=n;c.ior=svm_input(s,data+N_BssrdfData_ior);c.roughness=clamp(svm_input(s,data+N_BssrdfData_roughness),0.0,1.0);
    c.a=svm_input(s,data+N_BssrdfData_anisotropy);
    ss_setup(s,i,c,kind,flag);
}
//#if SVM_BSDF_PRINCIPLED
// The Principled BSDF's subsurface lobe (not thin-walled; that case is refused before rendering).
fn principled_subsurface(s:ptr<function,SvmState>,data:u32,subsurface:f32,base:vec3<f32>,weight:vec3<f32>,n:vec3<f32>,roughness:f32,eta:f32,flag:u32){
    let anisotropy=svm_input(s,data+N_PrincipledBsdfData_subsurface_anisotropy);let method=load_word(data+N_PrincipledBsdfData_subsurface_method);
    let i=ss_alloc(s,(base*subsurface)*weight);if(i==NONE){return;}
    var c=svm_closure_read((*s).output,i);
    c.multiscatter=max(svm_input3(s,data+N_PrincipledBsdfData_subsurface_radius)*svm_input(s,data+N_PrincipledBsdfData_subsurface_scale),vec3<f32>(0));
    c.T=base;c.N=n;c.roughness=roughness*roughness;c.ior=eta;c.a=anisotropy;
    if(method==SS_CLOSURE_RANDOM_WALK_SKIN){c.ior=svm_input(s,data+N_PrincipledBsdfData_subsurface_ior);}
    ss_setup(s,i,c,method,flag);
}
//#endif
//#endif
// Native svm_node_closure_volume: absorption shares the extinction accumulator.
// HG anisotropy uses the existing scalar roughness slot in the private record.
fn svm_volume(s:ptr<function,SvmState>,node:u32){
    let kind=load_word(node+N_ClosureVolume_closure_type);
//#if VOLUME_PHASES else     if(kind!=CLOSURE_VOLUME_ABSORPTION_ID&&kind!=CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID){svm_error(s,16u);return;}
    if(kind!=CLOSURE_VOLUME_ABSORPTION_ID&&kind!=CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID&&!vp_scatter_kind(kind)){svm_error(s,16u);return;}
//#endif
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
//#if VOLUME_PHASES
    else if(kind!=CLOSURE_VOLUME_ABSORPTION_ID){vp_scatter(s,kind,weight,svm_input(s,node+N_ClosureVolume_param1),svm_input(s,node+N_ClosureVolume_param_extra));}
//#endif
    (*s).output.transparency+=weight;(*s).output.runtime_flag|=SR_EXTINCTION;
}
//#if SVM_BSDF_FAMILIES
// safe_normalize_fallback(N, sd->N) of an optional SVM normal input, as in svm_bsdf.
fn svm_family_normal(s:ptr<function,SvmState>,index:u32)->vec3<f32>{
    var n=(*s).geometry.N;if(index!=SVM_STACK_INVALID){n=svm_load3(s,index);}
    let magnitude=rounded_sqrt(rounded_dot(n,n));if(magnitude!=0.0){return n*rounded_divide(1.0,magnitude);}return (*s).geometry.N;
}
// bsdf_alloc with closure_weight * mix_weight (closure_sample_weight cutoff).
fn svm_family_allocate(s:ptr<function,SvmState>,kind:u32,mix_weight:f32)->u32{
    let weight=max((*s).weight*mix_weight,vec3<f32>(0));let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));
    if(!(sample_weight>=1e-5)){return NONE;}
    let i=svm_allocate(s,kind,weight);if(i==NONE){return NONE;}
    var c=svm_closure_read((*s).output,i);c.sample_weight=sample_weight;svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);return i;
}
//#endif
//#if SVM_BSDF_VELVET
// Sheen BSDF node, Ashikhmin velvet distribution (pinned svm/closure.h and
// bsdf_ashikhmin_velvet_setup): sigma = saturate(param1) in `roughness`,
// 1/max(sigma, 0.01)^2 in `a`.
fn svm_velvet(s:ptr<function,SvmState>,data:u32,mix_weight:f32){
    let n=svm_family_normal(s,svm_byte(data+N_SimpleBsdfData_normal_offset));
    let i=svm_family_allocate(s,CLOSURE_BSDF_ASHIKHMIN_VELVET_ID,mix_weight);if(i==NONE){return;}
    var c=svm_closure_read((*s).output,i);c.N=n;c.roughness=clamp(svm_input(s,data+N_SimpleBsdfData_param1),0.0,1.0);
    let sigma=max(c.roughness,0.01);c.a=rounded_divide(1.0,sigma*sigma);
    (*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_EVAL;svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);
}
//#endif
//#if SVM_BSDF_TOON
// Toon BSDF node (pinned svm/closure.h and bsdf_toon.h setup): size = clamp(size,
// 1e-5, 1) * pi/2 in `a`, smooth = saturate(smooth) * pi/2 in `b`.
fn svm_toon(s:ptr<function,SvmState>,data:u32,kind:u32,mix_weight:f32,visibility:u32){
    if(kind==CLOSURE_BSDF_GLOSSY_TOON_ID&&data_word(D_caustics_reflective)==0u&&(visibility&PATH_RAY_VISIBILITY_DIFFUSE)!=0u){return;}
    let n=svm_family_normal(s,svm_byte(data+N_ToonBsdfData_normal_offset));
    let i=svm_family_allocate(s,kind,mix_weight);if(i==NONE){return;}
    var c=svm_closure_read((*s).output,i);c.N=n;
    c.a=clamp(svm_input(s,data+N_ToonBsdfData_size),1e-5,1.0)*1.5707963267948966;c.b=clamp(svm_input(s,data+N_ToonBsdfData_smooth),0.0,1.0)*1.5707963267948966;
    (*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_EVAL;svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);
}
//#endif
//#if SVM_BSDF_ASHIKHMIN_SHIRLEY
// Glossy BSDF node, Ashikhmin-Shirley distribution (pinned svm/closure.h and
// bsdf_ashikhmin_shirley_setup): the GGX node setup with alpha clamped to
// [1e-4, 1] and an always-evaluable lobe (undefined energy scale stored 0).
fn svm_ashikhmin_shirley(s:ptr<function,SvmState>,data:u32,mix_weight:f32,visibility:u32){
    let count=(*s).output.count;svm_glossy(s,data,CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID,mix_weight,visibility);
    if((*s).error!=0u||(*s).output.count==count){return;}
    let i=(*s).output.count-1u;var c=svm_closure_read((*s).output,i);c.kind=CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID;c.energy_scale=0.0;
    c.alpha_x=clamp(c.alpha_x,1e-4,1.0);c.alpha_y=clamp(c.alpha_y,1e-4,1.0);(*s).output.runtime_flag|=SR_BSDF_HAS_EVAL;
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);
}
//#endif
//#if VOLUME_PHASES
// closure/volume_util.h and closure/volume_*.h: the Rayleigh, Draine and Fournier-Forand phase functions, their
// samplers and phase_mie_fitted_parameters (util/math_fast.h fast_* functions), each sum a correctly rounded
// binary32 step after its products (the CPU kernels' -ffp-contract=off order). Closure record: kind, roughness
// = g (Henyey-Greenstein, Draine), a = alpha (Draine) or c1, b = c2, ior = c3 (Fournier-Forand).
fn vp_madd(a:f32,b:f32,c:f32)->f32{return rounded_add(a*b,c);}
fn vp_inv_cbrt(x:f32)->f32{
    var y=bitcast<f32>(0x54a24242i-bitcast<i32>(x)/3i);
    y=rounded_add(bitcast<f32>(0x3f2aaaabu)*y,rounded_divide(1.0,((3.0*y)*y)*x));
    y=rounded_add(bitcast<f32>(0x3f2aaaabu)*y,rounded_divide(1.0,((3.0*y)*y)*x));
    return y;
}
fn vp_rayleigh(c:f32)->f32{return bitcast<f32>(0x3d747644u)*rounded_add(1.0,c*c);}
fn vp_hg(c:f32,g:f32)->f32{
    if(abs(g)<0.001){return 0.07957747154594767;}
    let fac=rounded_add(1.0,g*rounded_add(g,-2.0*c));
    return rounded_divide(rounded_add(1.0,-g*g),(bitcast<f32>(0x41490fdbu)*fac)*rounded_sqrt(max(fac,0.0)));
}
fn vp_draine(c:f32,g:f32,alpha:f32)->f32{
    if(abs(g)<0.001&&alpha>0.999){return vp_rayleigh(c);}
    if(abs(alpha)<0.001){return vp_hg(c,g);}
    let g2=g*g;let fac=rounded_add(rounded_add(1.0,g2),-((2.0*g)*c));
    let numerator=rounded_add(1.0,-g2)*rounded_add(1.0,alpha*(c*c));
    let norm=rounded_add(1.0,(alpha*rounded_add(1.0,2.0*g2))*bitcast<f32>(0x3eaaaaabu));
    return rounded_divide(numerator,((norm*bitcast<f32>(0x41490fdbu))*fac)*rounded_sqrt(fac));
}
fn vp_draine_sample_cos(g:f32,alpha:f32,rand:f32)->f32{
    if(abs(g)<0.01){
        let inv_alpha=rounded_divide(1.0,alpha);
        let b_2=(rounded_add(3.0,alpha)*inv_alpha)*rounded_add(0.5,-rand);
        let inv_u=-vp_inv_cbrt(rounded_add(b_2,rounded_sqrt(rounded_add(b_2*b_2,(inv_alpha*inv_alpha)*inv_alpha))));
        return rounded_add(rounded_divide(1.0,inv_u),-rounded_divide(inv_u,alpha));
    }
    let g2=g*g;let g3=g*g2;let g4=g2*g2;let g6=g2*g4;
    let pgp1=rounded_add(1.0,g2);let pgp1_2=pgp1*pgp1;
    let T1a=alpha*rounded_add(g4,-1.0);let T1a3=(T1a*T1a)*T1a;
    let T2=(((-1296.0*rounded_add(g2,-1.0))*rounded_add(alpha,-(alpha*g2)))*T1a)*rounded_add(4.0*g2,alpha*pgp1_2);
    let rand2=rounded_add(2.0*rand,-1.0);
    let T9=rounded_add(rounded_add(2.0,g2),(g3*rounded_add(1.0,2.0*g2))*rand2);
    let T3=rounded_add((3.0*g2)*rounded_add(1.0,g*rand2),alpha*T9);
    let T4a=rounded_add(rounded_add(432.0*T1a3,T2),(432.0*(alpha*rounded_add(1.0,-g2)))*(T3*T3));
    let T10=alpha*rounded_add(rounded_add(2.0*g4,-g2),-g6);
    let T4b=144.0*T10;let T4b3=(T4b*T4b)*T4b;
    let T4=rounded_add(T4a,rounded_sqrt(rounded_add(-4.0*T4b3,T4a*T4a)));
    let inv_T4p3=vp_inv_cbrt(T4);
    let T8=(48.0*bitcast<f32>(0x3fa14518u))*T10;
    let T6=rounded_divide(rounded_add(rounded_add(2.0*T1a,T8*inv_T4p3),rounded_divide(1.0,(3.0*bitcast<f32>(0x3fa14518u))*inv_T4p3)),alpha*rounded_add(1.0,-g2));
    let T5=rounded_add(6.0*pgp1,T6);
    let T7=rounded_add(rounded_add(6.0*pgp1,-rounded_divide(8.0*T3,(alpha*rounded_add(g2,-1.0))*rounded_sqrt(T5))),-T6);
    let d=rounded_add(rounded_sqrt(T7),-rounded_sqrt(T5));
    return rounded_divide(rounded_add(pgp1,-(0.25*(d*d))),2.0*g);
}
fn vp_ff_delta(n:f32,sin_htheta_sqr:f32)->f32{let u=4.0*sin_htheta_sqr;let m=rounded_add(n,-1.0);return rounded_divide(u,3.0*(m*m));}
//#endif
//#if VP_MATH
// expf, logf and powf of the BSDF families (family_exp_parts, family_log_parts, family_pow in
// wavefront_microfacet.wgsl), copied here for the modules without those regions.
fn vp_exp_parts(x2:vec2<f32>)->vec2<f32>{
    let x=x2.x;
    if(!(x==x)){return vec2<f32>(x,0.0);}if(x>88.72284){return vec2<f32>(bitcast<f32>(0x7f800000u),0.0);}if(x< -103.97208){return vec2<f32>(0.0,0.0);}
    let k=round(x*1.4426950408889634);let p=trig_product(k,0.6931471824645996);let head=trig_two_sum(x,-p.x);
    let r=vec2<f32>(head.x,rounded_add(rounded_add(rounded_add(head.y,x2.y),-p.y),k*1.904654323148236e-09));
    var coefficients=array<vec2<f32>,10>(vec2<f32>(2.7557318844628753e-06,3.793571224297229e-14),vec2<f32>(2.4801587642286904e-05,-3.40699609366682e-13),
        vec2<f32>(0.00019841270113829523,-2.725596874933456e-12),vec2<f32>(0.0013888889225199819,-3.3631094437103215e-11),vec2<f32>(0.008333333767950535,-4.34617203337595e-10),
        vec2<f32>(0.0416666679084301,-1.2417634698280722e-09),vec2<f32>(0.1666666716337204,-4.967053879312289e-09),vec2<f32>(0.5,0.0),vec2<f32>(1.0,0.0),vec2<f32>(1.0,0.0));
    var polynomial=vec2<f32>(2.755731998149713e-07,-7.575112209051195e-15);
    for(var i=0u;i<10u;i++){polynomial=trig_coeff(coefficients[i],trig_mul(polynomial,r));}
    return vec2<f32>(rounded_add(polynomial.x,polynomial.y),k);
}
// logf of a positive normal x as a double-float pair: e ln2 + 2 atanh((m-1)/(m+1)),
// m in [sqrt(1/2), sqrt(2)), the series to s^21.
fn vp_log_parts(x:f32)->vec2<f32>{
    var bits=bitcast<u32>(x);var e=f32(i32(bits>>23u)-127);var m=bitcast<f32>((bits&0x007fffffu)|0x3f800000u);
    if(m>1.4142135){m*=0.5;e+=1.0;}
    let num=m-1.0;let den=trig_two_sum(m,1.0);let q=rounded_divide(num,den.x);let qd=trig_product(q,den.x);
    let sl=rounded_divide(rounded_add(rounded_add(rounded_add(num,-qd.x),-qd.y),-(q*den.y)),den.x);let sv=vec2<f32>(q,sl);
    let z=trig_mul(sv,sv);
    var coefficients=array<vec2<f32>,10>(vec2<f32>(0.05263157933950424,-3.9213582381236733e-10),vec2<f32>(0.05882352963089943,-2.1913472425527658e-10),
        vec2<f32>(0.06666667014360428,-3.47693762670076e-09),vec2<f32>(0.07692307978868484,-2.8656079731348427e-09),vec2<f32>(0.09090909361839294,-2.709302115988521e-09),
        vec2<f32>(0.1111111119389534,-8.278422947149977e-10),vec2<f32>(0.1428571492433548,-6.38621200366174e-09),vec2<f32>(0.20000000298023224,-2.9802322831784522e-09),
        vec2<f32>(0.3333333432674408,-9.934107758624577e-09),vec2<f32>(1.0,0.0));
    var polynomial=vec2<f32>(0.0476190485060215,-8.869738832295582e-10);
    for(var i=0u;i<10u;i++){polynomial=trig_coeff(coefficients[i],trig_mul(polynomial,z));}
    let lm=trig_mul(vec2<f32>(2.0*sv.x,2.0*sv.y),polynomial);
    let le=trig_product(e,0.6931471824645996);let tail=rounded_add(le.y,e*-1.9046542121259336e-09);
    let sum=trig_two_sum(le.x,lm.x);return vec2<f32>(sum.x,rounded_add(rounded_add(sum.y,lm.y),tail));
}
// powf(x, y) for positive normal x: expf(y logf(x)) in double-float, rounded once.
fn vp_pow(x:f32,y:f32)->f32{
    if(x==1.0||y==0.0){return 1.0;}
    let t=trig_mul(vec2<f32>(y,0.0),vp_log_parts(x));let e=vp_exp_parts(t);return ldexp(e.x,i32(e.y));
}
fn vp_logf(x:f32)->f32{let l=vp_log_parts(x);return rounded_add(l.x,l.y);}
//#endif
//#if VOLUME_PHASES
fn vp_ff_impl(c:f32,delta:f32,pow_delta_v:f32,v:f32,sin_htheta_sqr:f32,pf_coeff:f32)->f32{
    let m_delta=rounded_add(1.0,-delta);let m_pow=rounded_add(1.0,-pow_delta_v);var pf:f32;
    if(abs(m_delta)<0.001){
        pf=(v*rounded_add(rounded_add(v,-1.0),-rounded_divide(rounded_add(v,1.0),sin_htheta_sqr)))*bitcast<f32>(0x3d22f983u);
        pf=rounded_add(pf,(((v*rounded_add(v,1.0))*m_delta)*rounded_add(2.0*rounded_add(v,-1.0),-rounded_divide(rounded_add(2.0*v,1.0),sin_htheta_sqr)))*bitcast<f32>(0x3c594cafu));
    }else{
        let top=rounded_add(rounded_add(v*m_delta,-m_pow),rounded_divide(rounded_add(delta*m_pow,-(v*m_delta)),sin_htheta_sqr));
        pf=rounded_divide(top,(bitcast<f32>(0x41490fdbu)*(m_delta*m_delta))*pow_delta_v);
    }
    return rounded_add(pf,pf_coeff*rounded_add(3.0*(c*c),-1.0));
}
fn vp_ff(c:f32,n:f32,v:f32,c3:f32)->f32{
    if(abs(c)>=1.0){return 0.0;}
    let pf_coeff=c3*bitcast<f32>(0x3ca2f983u);let s=0.5*rounded_add(1.0,-c);let delta=vp_ff_delta(n,s);
    return vp_ff_impl(c,delta,vp_pow(delta,v),v,s,pf_coeff);
}
fn vp_ff_newton(rand:f32,n:f32,v:f32,c3:f32)->f32{
    let cdf_coeff=c3*0.125;let pf_coeff=c3*bitcast<f32>(0x3ca2f983u);var c=0.64278760968;
    for(var it=0;it<20;it++){
        let s=0.5*rounded_add(1.0,-c);let delta=vp_ff_delta(n,s);let pow_delta_v=vp_pow(delta,v);
        let m_delta=rounded_add(1.0,-delta);let m_pow=rounded_add(1.0,-pow_delta_v);var cdf:f32;
        if(abs(m_delta)<0.001){cdf=rounded_add(1.0,(v*rounded_add(1.0,-s))*rounded_add(1.0,-((0.5*rounded_add(v,1.0))*m_delta)));}
        else{cdf=rounded_divide(rounded_add(rounded_add(1.0,-(pow_delta_v*delta)),-(m_pow*s)),m_delta*pow_delta_v);}
        cdf=rounded_add(cdf,(cdf_coeff*c)*rounded_add(1.0,-(c*c)));
        let pf=vp_ff_impl(c,delta,pow_delta_v,v,s,pf_coeff);
        var next=rounded_add(c,rounded_divide(bitcast<f32>(0x3e22f983u)*rounded_add(cdf,-rand),pf));
        if(next>=1.0){next=max(rounded_add(c,0.5*rounded_add(1.0,-c)),0.99);}
        if(abs(rounded_add(c,-next))<1e-6||next==1.0){return next;}
        c=next;
    }
    return c;
}
// phase_fournier_forand_coeffs (B and IOR clamped as volume_fournier_forand_setup).
fn vp_ff_coeffs(B_in:f32,IOR_in:f32)->vec3<f32>{
    let B=min(abs(B_in),0.499);let IOR=max(IOR_in,1.001);
    let d90=vp_ff_delta(IOR,0.5);let d180=vp_ff_delta(IOR,1.0);
    let v=rounded_divide(-vp_logf(rounded_add((2.0*B)*rounded_add(d90,-1.0),1.0)),vp_logf(d90));
    return vec3<f32>(IOR,v,rounded_divide(rounded_add(vp_pow(d180,-v),-1.0),rounded_add(d180,-1.0)));
}
// util/math_fast.h (madd steps rounded; fast_rint rounds half to even as SSE4.2 and NEON).
fn vp_fast_trig_reduce(x_in:f32,scale:f32,k:f32)->vec2<f32>{
    let q=round(x_in*scale);var x=x_in;
    x=vp_madd(q,-0.78515625*k,x);x=vp_madd(q,-0.00024187564849853515625*k,x);
    x=vp_madd(q,bitcast<f32>(select(0xb3a22000u,0xb4222000u,k==4.0)),x);x=vp_madd(q,bitcast<f32>(select(0xac34611au,0xacb4611au,k==4.0)),x);
    return vec2<f32>(x,q);
}
fn vp_fast_sin(x_in:f32)->f32{
    let r=vp_fast_trig_reduce(x_in,0.31830987,4.0);var x=rounded_add(bitcast<f32>(0x3fc90fdbu),-rounded_add(bitcast<f32>(0x3fc90fdbu),-r.x));
    let s=x*x;if((i32(r.y)&1)!=0){x=-x;}
    var u=2.6083159809786593541503e-06;u=vp_madd(u,s,-0.0001981069071916863322258);u=vp_madd(u,s,0.00833307858556509017944336);
    u=vp_madd(u,s,-0.166666597127914428710938);u=vp_madd(s,u*x,x);return clamp(u,-1.0,1.0);
}
fn vp_fast_cos(x_in:f32)->f32{
    let r=vp_fast_trig_reduce(x_in,0.31830987,4.0);let x=rounded_add(bitcast<f32>(0x3fc90fdbu),-rounded_add(bitcast<f32>(0x3fc90fdbu),-r.x));
    let s=x*x;var u=-2.71811842367242206819355e-07;u=vp_madd(u,s,2.47990446951007470488548e-05);u=vp_madd(u,s,-0.00138888787478208541870117);
    u=vp_madd(u,s,0.0416666641831398010253906);u=vp_madd(u,s,-0.5);u=vp_madd(u,s,1.0);if((i32(r.y)&1)!=0){u=-u;}return clamp(u,-1.0,1.0);
}
fn vp_fast_tan(x_in:f32)->f32{
    let q=round((x_in*2.0)*0.31830987);var x=x_in;
    x=vp_madd(q,-0.78515625*2.0,x);x=vp_madd(q,-0.00024187564849853515625*2.0,x);x=vp_madd(q,bitcast<f32>(0xb3a22000u),x);x=vp_madd(q,bitcast<f32>(0xac34611au),x);
    if((i32(q)&1)==0){x=rounded_add(bitcast<f32>(0x3f490fdbu),-rounded_add(bitcast<f32>(0x3f490fdbu),-x));}
    let s=x*x;var u=0.00927245803177356719970703;u=vp_madd(u,s,0.00331984995864331722259521);u=vp_madd(u,s,0.0242998078465461730957031);
    u=vp_madd(u,s,0.0534495301544666290283203);u=vp_madd(u,s,0.133383005857467651367188);u=vp_madd(u,s,0.333331853151321411132812);
    u=vp_madd(s,u*x,x);if((i32(q)&1)!=0){u=-rounded_divide(1.0,u);}return u;
}
fn vp_fast_log(x_in:f32)->f32{
    let x=clamp(x_in,1.17549435e-38,3.40282347e+38);let bits=bitcast<u32>(x);let exponent=f32(i32(bits>>23u)-127);
    let f=rounded_add(bitcast<f32>((bits&0x007FFFFFu)|0x3f800000u),-1.0);let f2=f*f;let f4=f2*f2;
    var hi=vp_madd(f,-0.00931049621349,0.05206469089414);var lo=vp_madd(f,0.47868480909345,-0.72116591947498);
    hi=vp_madd(f,hi,-0.13753123777116);hi=vp_madd(f,hi,0.24187369696082);hi=vp_madd(f,hi,-0.34730547155299);lo=vp_madd(f,lo,1.442689881667200);
    return rounded_add(rounded_add(f4*hi,f*lo),exponent)*bitcast<f32>(0x3f317218u);
}
fn vp_fast_exp(x_in:f32)->f32{
    var x=clamp(rounded_divide(x_in,bitcast<f32>(0x3f317218u)),-126.0,126.0);let m=i32(x);x=rounded_add(x,-f32(m));x=rounded_add(1.0,-rounded_add(1.0,-x));
    var r=1.33336498402e-3;r=vp_madd(x,r,9.810352697968e-3);r=vp_madd(x,r,5.551834031939e-2);r=vp_madd(x,r,0.2401793301105);
    r=vp_madd(x,r,0.693144857883);r=vp_madd(x,r,1.0);return bitcast<f32>(bitcast<u32>(r)+(u32(m)<<23u));
}
// One call site per helper (compile envelope): the fitted functions of every diameter range run in one loop through
// vp_fast_any, each operation as in phase_mie_fitted_parameters.
fn vp_fast_any(k:u32,x:f32)->f32{
    if(k==0u){return vp_fast_sin(x);}if(k==1u){return vp_fast_cos(x);}if(k==2u){return vp_fast_tan(x);}
    if(k==3u){return vp_fast_log(x);}if(k==4u){return vp_fast_exp(x);}return vp_pow(x,4.3);
}
// phase_mie_fitted_parameters: (g_HG, g_D, alpha, w).
fn vp_mie(d_in:f32)->vec4<f32>{
    let d=max(d_in,0.0);let range=select(select(select(3u,2u,d<5.0),1u,d<1.5),0u,d<=0.1);
    var v=array<f32,4>(0.0,0.0,0.0,0.0);let steps=select(4u,2u,range==0u)|load_word(p(17u)+12u);
    for(var i=0u;i<steps;i++){
        var k=4u;var x=0.0;let l=v[0];
        if(range==0u){if(i==0u){k=0u;x=9.29044*d;}else{k=5u;x=d;}}
        else if(range==1u){
            if(i==0u){k=3u;x=d;}
            else if(i==1u){k=1u;x=rounded_divide(rounded_add(l,-0.238604)*rounded_add(l,1.00667),rounded_add(0.507522,-(0.15677*l)));}
            else if(i==2u){k=1u;x=rounded_add(rounded_add(1.19692*v[1],1.37932*l),0.0625835);}
            else{k=1u;x=rounded_add(3.38707*l,2.11193);}
        }
        else if(range==2u){
            if(i==0u){k=3u;x=d;}else if(i==1u){k=3u;x=l;}else if(i==2u){k=1u;x=5.68947*rounded_add(v[1],-0.0292149);}else{k=2u;x=l;}
        }
        else{
            if(i==0u){x=rounded_divide(-0.0990567,rounded_add(d,-1.67154));}
            else if(i==1u){x=rounded_add(rounded_divide(-2.20679,rounded_add(d,3.91029)),-0.428934);}
            else if(i==2u){x=rounded_add(3.62489,-rounded_divide(8.29288,rounded_add(d,5.52825)));}
            else{x=rounded_add(rounded_divide(-0.599085,rounded_add(d,-0.641583)),-0.665888);}
        }
        v[min(i,3u)]=vp_fast_any(k,x);
    }
    let l=v[0];
    if(range==0u){return vec4<f32>(13.8*(d*d),(1.1456*d)*v[0],250.0,rounded_add(0.252977,-(312.983*v[1])));}
    if(range==1u){return vec4<f32>(rounded_add(0.862,-(0.143*(l*l))),rounded_add(0.379685*v[2],0.344213),250.0,rounded_add(rounded_add(0.146209*v[3],0.316072),0.0778917*l));}
    if(range==2u){return vec4<f32>(rounded_add(0.0604931*v[1],0.940256),rounded_add(0.500411,-rounded_divide(0.081287,rounded_add(rounded_add(-2.0*l,v[3]),1.27551))),
                                   rounded_add(7.30354*l,6.31675),rounded_add(0.026914*rounded_add(l,-v[2]),0.3764));}
    return vec4<f32>(v[0],v[1],v[2],v[3]);
}
// volume_phase_eval for one closure: the pdf (the evaluation is the pdf).
fn vp_eval(c:SvmClosure,cos_theta:f32)->f32{
//#if PHASE_RAYLEIGH
    if(c.kind==VP_CLOSURE_VOLUME_RAYLEIGH_ID){return vp_rayleigh(cos_theta);}
//#endif
//#if PHASE_DRAINE
    if(c.kind==VP_CLOSURE_VOLUME_DRAINE_ID){return vp_draine(cos_theta,c.roughness,c.a);}
//#endif
//#if PHASE_FOURNIER_FORAND
    if(c.kind==VP_CLOSURE_VOLUME_FOURNIER_FORAND_ID){return vp_ff(cos_theta,c.a,c.b,c.ior);}
//#endif
    return vp_hg(cos_theta,c.roughness);
}
// The sampled cosine of the closure's lobe (phase_*_sample before phase_sample_direction); Henyey-Greenstein
// keeps the callers' own form.
fn vp_sample_cos(c:SvmClosure,rand:f32)->f32{
    var rayleigh=false;
//#if PHASE_RAYLEIGH
    rayleigh=c.kind==VP_CLOSURE_VOLUME_RAYLEIGH_ID;
//#endif
//#if PHASE_DRAINE
    rayleigh=rayleigh||(c.kind==VP_CLOSURE_VOLUME_DRAINE_ID&&abs(c.roughness)<0.001&&c.a>0.999);
//#endif
    if(rayleigh){
        let a=rounded_add(2.0,-4.0*rand);let inv_u=-vp_inv_cbrt(rounded_add(rounded_sqrt(rounded_add(1.0,a*a)),a));
        return rounded_add(rounded_divide(1.0,inv_u),-inv_u);
    }
//#if PHASE_DRAINE
    if(c.kind==VP_CLOSURE_VOLUME_DRAINE_ID){return vp_draine_sample_cos(c.roughness,c.a,rand);}
//#endif
//#if PHASE_FOURNIER_FORAND
    return vp_ff_newton(rand,c.a,c.b,c.ior);
//#endif
//#if !PHASE_FOURNIER_FORAND
    return 1.0;
//#endif
}
// volume_phase_equal for the closure merge of overlapping volumes.
fn vp_equal(a:SvmClosure,b:SvmClosure)->bool{
    if(a.kind!=b.kind){return false;}
    if(a.kind==VP_CLOSURE_VOLUME_RAYLEIGH_ID){return true;}
    if(a.kind==VP_CLOSURE_VOLUME_DRAINE_ID){return a.roughness==b.roughness&&a.a==b.a;}
    if(a.kind==VP_CLOSURE_VOLUME_FOURNIER_FORAND_ID){return a.a==b.a&&a.b==b.b&&a.ior==b.ior;}
    return a.roughness==b.roughness;
}
//#endif
//#if VOLUME_PHASES
fn vp_scatter_kind(kind:u32)->bool{return kind==VP_CLOSURE_VOLUME_FOURNIER_FORAND_ID||kind==VP_CLOSURE_VOLUME_RAYLEIGH_ID||kind==VP_CLOSURE_VOLUME_DRAINE_ID||kind==VP_CLOSURE_VOLUME_MIE_ID;}
//#endif
//#if VP_ALLOC
// One volume closure (bsdf_alloc: sample_weight |average(weight)|), its parameters as described above.
fn vp_allocate(s:ptr<function,SvmState>,kind:u32,weight:vec3<f32>,g:f32,a:f32,b:f32,ior:f32){
    let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));if(!(sample_weight>0.0)){return;}
    let i=svm_allocate(s,kind,weight);if(i==NONE){return;}
    var c=svm_closure_read((*s).output,i);c.sample_weight=sample_weight;c.roughness=g;c.a=a;c.b=b;c.ior=ior;
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);(*s).output.runtime_flag|=SR_SCATTER;
}
fn vp_clamp_g(g:f32)->f32{return sign(g)*min(abs(g),1.0-0.001);}
//#endif
//#if VOLUME_PHASES
// svm_alloc_closure_volume_scatter for the phases beyond Henyey-Greenstein (Mie: Henyey-Greenstein + Draine).
fn vp_scatter(s:ptr<function,SvmState>,kind:u32,weight:vec3<f32>,param1:f32,param_extra:f32){
//#if PHASE_FOURNIER_FORAND
    if(kind==VP_CLOSURE_VOLUME_FOURNIER_FORAND_ID){let c=vp_ff_coeffs(param_extra,param1);vp_allocate(s,kind,weight,0.0,c.x,c.y,c.z);return;}
//#endif
//#if PHASE_RAYLEIGH
    if(kind==VP_CLOSURE_VOLUME_RAYLEIGH_ID){vp_allocate(s,kind,weight,0.0,0.0,0.0,0.0);return;}
//#endif
//#if PHASE_DRAINE
    if(kind==VP_CLOSURE_VOLUME_DRAINE_ID){vp_allocate(s,kind,weight,vp_clamp_g(param1),param_extra,0.0,0.0);return;}
//#endif
//#if PHASE_MIE
    if(kind==VP_CLOSURE_VOLUME_MIE_ID){let m=vp_mie(param1);
        vp_allocate(s,CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID,weight*rounded_add(1.0,-m.w),vp_clamp_g(m.x),0.0,0.0,0.0);
        vp_allocate(s,VP_CLOSURE_VOLUME_DRAINE_ID,weight*m.w,vp_clamp_g(m.y),m.z,0.0,0.0);return;}
//#endif
    // Shadow evaluations allocate no closures (max_closures 0: bsdf_alloc returns null, the fit is unused); the shadow
    // module leaves the families out (shadow.cpp), so a scatter lobe there changes nothing, as on the CPU.
    if((*s).output.remaining!=0u){svm_error(s,16u);}
}
//#endif
//#if SVM_NODE_VOLUME_COEFFICIENTS
// svm_node_volume_coefficients (volume programs only).
fn svm_volume_coefficients(s:ptr<function,SvmState>,node:u32,flag:u32,visibility:u32){
    let mix_weight=svm_optional(s,svm_byte(node+N_VolumeCoefficients_mix_weight_offset),1.0);if(mix_weight==0.0){return;}
    var weight=mix_weight;if((*s).geometry.object_id!=NONE){weight*=heap_float(table_at(G_objects,(*s).geometry.object_id,SIZE_objects)+O_volume_density);}
    let kind=load_word(node+N_VolumeCoefficients_closure_type);let scatter=(*s).weight;
    if(any(scatter!=vec3<f32>(0))){
        if(kind==CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID){let w=weight*scatter;
            vp_allocate(s,kind,w,vp_clamp_g(svm_input(s,node+N_VolumeCoefficients_param1)),0.0,0.0,0.0);}
//#if VOLUME_PHASES
        else if(vp_scatter_kind(kind)){vp_scatter(s,kind,weight*scatter,svm_input(s,node+N_VolumeCoefficients_param1),svm_input(s,node+N_VolumeCoefficients_param_extra));}
//#endif
        else if(kind!=CLOSURE_VOLUME_ABSORPTION_ID){svm_error(s,16u);return;}
    }
    let absorption=svm_input3(s,node+N_VolumeCoefficients_absorption_coeffs);
    (*s).output.transparency+=weight*(scatter+absorption);(*s).output.runtime_flag|=SR_EXTINCTION;
    if((visibility&PATH_RAY_VISIBILITY_SHADOW)!=0u||(flag&PATH_RAY_EXTINCTION)!=0u){return;}
    let emission=svm_input3(s,node+N_VolumeCoefficients_emission_coeffs);if(all(emission==vec3<f32>(0))){return;}
    let e=weight*emission;if(((*s).output.runtime_flag&SR_EMISSION)!=0u){(*s).output.emission+=e;}else{(*s).output.emission=e;(*s).output.runtime_flag|=SR_EMISSION;}
}
//#endif
//#if SVM_NODE_PRINCIPLED_VOLUME
// svm_node_principled_volume (volume programs only). Density, colour and temperature attributes are voxel data
// (OpenVDB grids, excluded): a reachable attribute refuses the evaluation.
fn svm_principled_volume(s:ptr<function,SvmState>,node:u32,flag:u32,visibility:u32){
    let mix_weight=svm_optional(s,svm_byte(node+N_PrincipledVolume_mix_weight_offset),1.0);if(mix_weight==0.0){return;}
    var weight=mix_weight;if((*s).geometry.object_id!=NONE){weight*=heap_float(table_at(G_objects,(*s).geometry.object_id,SIZE_objects)+O_volume_density);}
    var density=weight*max(svm_input(s,node+N_PrincipledVolume_density),0.0);
    if(density>0.0&&attribute_find(s,load_word(node+N_PrincipledVolume_attr_density),0u).found){svm_error(s,16u);return;}
    if(density>0.0){
        var color=(*s).weight;
        if(attribute_find(s,load_word(node+N_PrincipledVolume_attr_color),0u).found){svm_error(s,16u);return;}
        let w=color*density;
        let sample_weight=abs((w.x+w.y+w.z)*(1.0/3.0));
        if(sample_weight>0.0){let i=svm_allocate(s,CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID,w);if(i!=NONE){
            var c=svm_closure_read((*s).output,i);c.sample_weight=sample_weight;c.roughness=vp_principled_g(svm_input(s,node+N_PrincipledVolume_anisotropy));
            svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);(*s).output.runtime_flag|=SR_SCATTER;}}
        let absorption_color=max(sqrt(svm_input3(s,node+N_PrincipledVolume_absorption_color)),vec3<f32>(0));
        let absorption=max(vec3<f32>(1)-color,vec3<f32>(0))*max(vec3<f32>(1)-absorption_color,vec3<f32>(0));
        (*s).output.transparency+=(color+absorption)*density;(*s).output.runtime_flag|=SR_EXTINCTION;
    }
    if((visibility&PATH_RAY_VISIBILITY_SHADOW)!=0u||(flag&PATH_RAY_EXTINCTION)!=0u){return;}
    let emission=svm_input(s,node+N_PrincipledVolume_emission);let blackbody=svm_input(s,node+N_PrincipledVolume_blackbody);
    if(emission>0.0){let e=((emission*svm_input3(s,node+N_PrincipledVolume_emission_color))*weight);
        if(((*s).output.runtime_flag&SR_EMISSION)!=0u){(*s).output.emission+=e;}else{(*s).output.emission=e;(*s).output.runtime_flag|=SR_EMISSION;}}
    if(blackbody>0.0){
        var T=svm_input(s,node+N_PrincipledVolume_temperature);
        if(attribute_find(s,load_word(node+N_PrincipledVolume_attr_temperature),0u).found){svm_error(s,16u);return;}
        T=max(T,0.0);let T4=(T*T)*(T*T);
        let intensity=bitcast<f32>(0x28a292fcu)*rounded_add(1.0,blackbody*rounded_add(T4,-1.0));
        if(intensity>0.0){
            let bb=(svm_input3(s,node+N_PrincipledVolume_blackbody_tint)*intensity)*vp_rec709_to_rgb(vp_blackbody_rec709(T));
            let e=bb*weight;if(((*s).output.runtime_flag&SR_EMISSION)!=0u){(*s).output.emission+=e;}else{(*s).output.emission=e;(*s).output.runtime_flag|=SR_EMISSION;}
        }
    }
}
fn vp_principled_g(g:f32)->f32{return sign(g)*min(abs(g),1.0-0.001);}
// svm_math_blackbody_color_rec709 (the blackbody node's fit) and rec709_to_rgb.
fn vp_blackbody_rec709(t:f32)->vec3<f32>{
    if(t>=12000.0){return vec3<f32>(0.8262954810464208,0.9945080501520986,1.566307710274283);}
    if(t<800.0){return vec3<f32>(5.413294490189271,-0.20319390035873933,-0.0822535242887164);}
    var i=0u;if(t>=6365.0){i=6u;}else if(t>=3315.0){i=5u;}else if(t>=1902.0){i=4u;}else if(t>=1449.0){i=3u;}else if(t>=1167.0){i=2u;}else if(t>=965.0){i=1u;}
    var r=vec3<f32>(0);var g=vec3<f32>(0);var b=vec4<f32>(0);
    switch(i){
        case 0u:{r=vec3<f32>(1.61919106e+03,-2.05010916e-03,5.02995757e+00);g=vec3<f32>(-4.88999748e+02,6.04330754e-04,-7.55807526e-02);b=vec4<f32>(5.96945309e-11,-4.85742887e-08,-9.70622247e-05,-4.07936148e-03);}
        case 1u:{r=vec3<f32>(2.48845471e+03,-1.11330907e-03,3.22621544e+00);g=vec3<f32>(-7.55994277e+02,3.16730098e-04,4.78306139e-01);b=vec4<f32>(2.40430366e-11,5.55021075e-08,-1.98503712e-04,2.89312858e-02);}
        case 2u:{r=vec3<f32>(3.34143193e+03,-4.86551192e-04,1.76486769e+00);g=vec3<f32>(-1.02363977e+03,1.20223470e-04,9.36662319e-01);b=vec4<f32>(-1.40949732e-11,1.89878968e-07,-3.56632824e-04,9.10767778e-02);}
        case 3u:{r=vec3<f32>(4.09461742e+03,-1.27446582e-04,7.25731635e-01);g=vec3<f32>(-1.26571316e+03,4.87340896e-06,1.27054498e+00);b=vec4<f32>(-3.61460868e-11,2.84822009e-07,-4.93211319e-04,1.56723440e-01);}
        case 4u:{r=vec3<f32>(4.67028036e+03,2.91258199e-05,1.26703442e-01);g=vec3<f32>(-1.42529332e+03,-4.01150431e-05,1.43972784e+00);b=vec4<f32>(-1.97075738e-11,1.75359352e-07,-2.50542825e-04,-2.22783266e-02);}
        case 5u:{r=vec3<f32>(4.59509185e+03,2.87495649e-05,1.50345020e-01);g=vec3<f32>(-1.17554822e+03,-2.16378048e-05,1.30408023e+00);b=vec4<f32>(-1.61997957e-13,-1.64216008e-08,3.86216271e-04,-7.38077418e-01);}
        default:{r=vec3<f32>(3.78717450e+03,9.35907826e-06,3.99075871e-01);g=vec3<f32>(-5.00799571e+02,-4.59832026e-06,1.09098763e+00);b=vec4<f32>(6.72650283e-13,-2.73078809e-08,4.24098264e-04,-7.52335691e-01);}
    }
    let t_inv=rounded_divide(1.0,t);
    return vec3<f32>(rounded_add(rounded_add(r.x*t_inv,r.y*t),r.z),rounded_add(rounded_add(g.x*t_inv,g.y*t),g.z),
                     rounded_add(rounded_add(rounded_add(b.x*t,b.y)*t,b.z)*t,b.w));
}
fn vp_rec709_to_rgb(c:vec3<f32>)->vec3<f32>{
    if(data_word(D_film_is_rec709)!=0u){return c;}
    return vec3<f32>(rounded_dot(heap_vec3(p(6u)+D_film_rec709_to_r),c),rounded_dot(heap_vec3(p(6u)+D_film_rec709_to_g),c),rounded_dot(heap_vec3(p(6u)+D_film_rec709_to_b),c));
}
//#endif
