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
