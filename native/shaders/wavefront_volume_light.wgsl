// SPDX-FileCopyrightText: 2011-2025 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Volume direct light and shadow paths through volumes, adapted from the pinned
// kernel/integrator/shade_volume.h (volume_direct_sample_method, integrate_volume_sample_direct_light,
// integrate_volume_direct_light, integrate_volume_phase_scatter), volume_stack.h
// (volume_stack_sample_method), state_util.h (integrator_state_copy_volume_stack_to_shadow) and
// closure/volume_util.h (phase_henyey_greenstein). Part of the surface module (light sampling and shadow
// paths live there); shade_volume (wavefront_volume.wgsl) integrates the segment between the two entries.
// Record per input (push word 24, VL_RECORD_BYTES): 0 the direct sample method (0 none, 1 distance,
// 2 equiangular, 3 MIS), 4 the equiangular point, 16 and 20 the valid ray segment, 32 the direct light
// pending, 36 its scatter position, 48 its throughput, 60 the closure count, 64 the indirect scatter pending,
// 68 its distance, 72 the shading object.
//#if VOLUME_SHADOW
fn vl_shadow_stack_address(shadow:u32,slot:u32,member:u32)->u32{return load_word(p(7u)+VL_S_shadow_volume_stack+slot*VL_SVS_stride+member)+shadow*4u;}
// integrator_state_copy_volume_stack_to_shadow: every entry up to and including the terminator.
fn vl_copy_volume_stack(index:u32,shadow:u32){
    let limit=data_word(D_volume_stack_size);
    for(var slot=0u;slot<limit;slot++){
        let shader=load_word(volume_stack_address(index,slot,VS_shader));
        store_word(vl_shadow_stack_address(shadow,slot,VL_SVS_object),load_word(volume_stack_address(index,slot,VS_object)));
        store_word(vl_shadow_stack_address(shadow,slot,VL_SVS_shader),shader);
        if(shader==NONE){return;}
    }
}
//#endif
//#if VOLUME_LIGHT
// volume_stack_sample_method over the entries shade_volume integrates.
fn vl_sample_method(index:u32,entries:u32)->u32{
    var method=0u;
    for(var slot=0u;slot<entries;slot++){
        let shader=load_word(volume_stack_address(index,slot,VS_shader));if(shader==NONE){break;}
        let flags=load_word(table_at(G_shaders,shader&SHADER_MASK,SIZE_shaders)+SH_flags);
        if((flags&VL_SD_VOLUME_MIS)!=0u){return 3u;}
        else if((flags&VL_SD_VOLUME_EQUIANGULAR)!=0u){if(method==1u){return 3u;}method=2u;}
        else{if(method==2u){return 3u;}method=1u;}
    }
    return method;
}
// DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME, part 1: light_sample_from_volume_segment for the direct light and
// the method of volume_direct_sample_method, from the segment's start sd->P = ray->P + ray->D * ray->tmin.
@compute @workgroup_size(1) fn shade_volume_segment(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    let vl=p(24u)+id.x*VL_RECORD_BYTES;store_word(vl,0u);store_word(vl+32u,0u);store_word(vl+64u,0u);
//#if VOLUME_HETEROGENEOUS
    store_word(vl+76u,0u);
//#endif
//#if VOLUME_RAY_MARCHING else     if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=VL_K_SHADE_VOLUME){return;}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=vr_volume_kernel()){return;}
//#endif
    if(data_word(D_integrator_use_direct_light)==0u||(state_word(S_path_flag,index)&PATH_RAY_TERMINATE)!=0u){return;}
    let prim=state_word(S_isect_prim,index);let count=volume_stack_count(index);if(count==NONE){return;}
    let entries=select(count,select(0u,1u,data_word(D_background_volume_shader)!=NONE),prim==NONE);if(entries==0u){return;}
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));
    let tmin=heap_float(state_address(S_ray_tmin,index,4u));var tmax=bitcast<f32>(0x7f7fffffu);if(prim!=NONE){tmax=heap_float(state_address(S_isect_t,index,4u));}
    let selection=light_select(index);if(selection.prim>=0){atomicOr(&heap[p(17u)/4u],2u);return;}
    let light=table_at(G_lights,~bitcast<u32>(selection.prim),SIZE_lights);
    if(f32(state_half(S_path_bounce,index))>heap_float(light+L_max_bounces)){return;}
    let kind=load_word(light+L_type);var point=vec3<f32>(0);var distant=true;
    if(kind==LIGHT_POINT){
        // light_sample<true>: the point light sampled from the segment start with N = ray->D and transmission.
        var sd:SurfaceData;sd.P=rounded_add3(origin,direction*tmin);sd.N=direction;var r:SvmResult;r.runtime_flag=VL_SR_BSDF_HAS_TRANSMISSION;
        let ls=light_sample_lamp(index,sd,r,selection);point=ls.P;distant=false;
    }else if(kind!=LIGHT_SUN&&kind!=LIGHT_BACKGROUND){atomicOr(&heap[p(17u)/4u],2u);return;}
    if((load_word(light+L_shader_id)&VL_SHADER_EXCLUDE_SCATTER)!=0u){return;}
    // volume_valid_direct_ray_segment for point and distant lights: the whole ray range.
    if(!(tmin<tmax)){return;}
    store_word(vl+4u,bitcast<u32>(point.x));store_word(vl+8u,bitcast<u32>(point.y));store_word(vl+12u,bitcast<u32>(point.z));
    store_word(vl+16u,bitcast<u32>(tmin));store_word(vl+20u,bitcast<u32>(tmax));
    store_word(vl,select(vl_sample_method(index,entries),1u,distant));
}
//#endif
//#if PHASE_HG
// phase_henyey_greenstein (volume_henyey_greenstein_eval's pdf).
fn vl_hg_pdf(cosine:f32,g:f32)->f32{
    if(abs(g)<0.001){return 0.07957747154594767;}
    let fac=rounded_add(1.0,g*rounded_add(g,-2.0*cosine));
    return rounded_divide(rounded_add(1.0,-g*g),(12.566370614359172*fac)*rounded_sqrt(max(fac,0.0)));
}
//#endif
//#if VOLUME_LIGHT
// volume_shader_phase_eval (_volume_shader_phase_eval_mis): eval accumulates eval * sample_weight, then
// times 1 / sum_sample_weight; the pdf is the sample-weighted average.
struct VlPhase {eval:vec3<f32>,pdf:f32}
fn vl_phase_eval(r:SvmResult,D:vec3<f32>,wo:vec3<f32>)->VlPhase{
    var out:VlPhase;var sum_pdf=0.0;var sum_weight=0.0;
    for(var i=0u;i<r.count;i++){
//#if VOLUME_PHASES else         let c=svm_closure_read(r,i);let pdf=vl_hg_pdf(rounded_dot(D,wo),c.roughness);
        let c=svm_closure_read(r,i);let pdf=vp_eval(c,rounded_dot(D,wo));
//#endif
        if(pdf!=0.0){out.eval=rounded_add3(out.eval,vec3<f32>(pdf)*c.sample_weight);sum_pdf=rounded_add(sum_pdf,pdf*c.sample_weight);}
        sum_weight=rounded_add(sum_weight,c.sample_weight);
    }
    out.eval*=rounded_divide(1.0,sum_weight);
    out.pdf=select(0.0,rounded_divide(sum_pdf,sum_weight),sum_weight>0.0);
    return out;
}
// integrate_volume_direct_light: the light sampled again from the scatter position (N = 0, transmission), the
// phase function with MIS, the light termination and the shadow path with the path's volume stack.
fn vl_direct_light(index:u32,slot:u32,r:SvmResult,P:vec3<f32>,throughput:vec3<f32>,D:vec3<f32>){
    var sd:SurfaceData;sd.P=P;sd.N=vec3<f32>(0);sd.time=heap_float(state_address(S_ray_time,index,4u));
    var rr:SvmResult;rr.runtime_flag=VL_SR_BSDF_HAS_TRANSMISSION;
    let selection=light_select(index);if(selection.prim>=0){atomicOr(&heap[p(17u)/4u],2u);return;}
    let ls=light_sample_lamp(index,sd,rr,selection);
    if(!(ls.pdf>0.0)||load_word(p(17u))!=0u){return;}
    if((ls.shader&VL_SHADER_EXCLUDE_SCATTER)!=0u){return;}
    let shader=table_at(G_shaders,ls.shader&SHADER_MASK,SIZE_shaders);
//#if LIGHT_NEE else     let constant=true;var emission=heap_vec3(shader+SH_constant_emission);
    let constant=(load_word(shader+SH_flags)&SD_HAS_CONSTANT_EMISSION)!=0u;
    var emission=select(vec3<f32>(1),heap_vec3(shader+SH_constant_emission),constant);
//#endif
    if(ls.is_light){emission*=heap_vec3(table_at(G_lights,ls.prim,SIZE_lights)+L_strength);}
    var phase=vl_phase_eval(r,D,ls.D);if((ls.shader&SHADER_USE_MIS)==0u){phase.pdf=0.0;}
    let mis=(ls.pdf*ls.pdf)/(ls.pdf*ls.pdf+phase.pdf*phase.pdf);
    var eval=phase.eval*(emission*ls.eval_fac/ls.pdf*mis);
    if(all(eval==vec3<f32>(0))){return;}
    // Path termination for constant light shaders (non-constant: shade_light_nee); the light tree is refused with volumes.
    if(constant){
        let rr_threshold=data_float(D_integrator_light_inv_rr_threshold);
        let probability=select(1.0,max(abs(eval.x),max(abs(eval.y),abs(eval.z)))*rr_threshold,rr_threshold>0.0);
        if(probability<1.0){let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_LIGHT_TERMINATE).x;
            if(random>=probability){return;}eval*=1.0/probability;}
    }
    // light_sample_to_volume_shadow_ray (shadow_ray_setup without self skip).
    var direction=ls.D;var tmax=ls.t;if(ls.t!=bitcast<f32>(0x7f7fffffu)){let ray=light_normalize_len(ls.P-P);direction=ray.xyz;tmax=ray.w;}
    var shadow:u32;
    if(p(28u)!=0u){shadow=load_word(p(28u))+slot;if(shadow>=p(25u)){fail();return;}}else{fail();return;}
    state_vec3(S_shadow_ray_P,shadow,P);state_vec3(S_shadow_ray_D,shadow,direction);
    state_f32(S_shadow_ray_tmin,shadow,select(bitcast<f32>(0x7f7fffffu),0.0,(ls.shader&SHADER_CAST_SHADOW)!=0u));state_f32(S_shadow_ray_tmax,shadow,tmax);
    state_f32(S_shadow_ray_time,shadow,sd.time);state_f32(S_shadow_ray_dP,shadow,0.0);state_f32(S_shadow_ray_dD,shadow,0.0);
    state_u32(S_shadow_self_object,shadow,NONE);state_u32(S_shadow_self_prim,shadow,NONE);
    state_u32(S_shadow_ray_self_light_object,shadow,ls.object_id);state_u32(S_shadow_ray_self_light_prim,shadow,ls.prim);
    let bounce=state_half(S_path_bounce,index);var visibility=state_byte(S_path_visibility,index);var flag=state_word(S_path_flag,index);
//#if LIGHT_TREE else     state_f32(S_shadow_path_bsdf_eval_average,shadow,(eval.x+eval.y+eval.z)*(1.0/3.0));
    if((data_word(D_kernel_features)&LT_FEATURE_LIGHT_TREE)==0u){state_f32(S_shadow_path_bsdf_eval_average,shadow,(eval.x+eval.y+eval.z)*(1.0/3.0));}
//#endif
    if(p(20u)!=0u){var diffuse=vec3<f32>(1);var glossy=vec3<f32>(0);
        if((flag&VL_PATH_RAY_ANY_PASS)!=0u){diffuse=heap_vec3(state_address(S_path_pass_diffuse_weight,index,12u));glossy=heap_vec3(state_address(S_path_pass_glossy_weight,index,12u));}
        else{flag|=VL_PATH_RAY_VOLUME_PASS;}
        state_vec3(S_shadow_path_pass_diffuse_weight,shadow,diffuse);state_vec3(S_shadow_path_pass_glossy_weight,shadow,glossy);
    }
    if(bounce==0u){visibility|=VL_PATH_RAY_VISIBILITY_VOLUME_SCATTER;flag&=~VL_PATH_RAY_VOLUME_PRIMARY_TRANSMIT;}
    state_u32(S_shadow_path_render_pixel_index,shadow,state_word(S_path_render_pixel_index,index));
    state_u16(S_shadow_path_rng_offset,shadow,state_half(S_path_rng_offset,index));state_u32(S_shadow_path_rng_pixel,shadow,state_word(S_path_rng_pixel,index));
    state_u32(S_shadow_path_sample,shadow,state_word(S_path_sample,index));
    state_u8(S_shadow_path_visibility,shadow,visibility);state_u32(S_shadow_path_flag,shadow,flag);
    state_u16(S_shadow_path_bounce,shadow,bounce);state_u16(S_shadow_path_transparent_bounce,shadow,state_half(S_path_transparent_bounce,index));
    state_u16(S_shadow_path_diffuse_bounce,shadow,state_half(S_path_diffuse_bounce,index));state_u16(S_shadow_path_glossy_bounce,shadow,state_half(S_path_glossy_bounce,index));
    state_u16(S_shadow_path_transmission_bounce,shadow,state_half(S_path_transmission_bounce,index));
    state_u16(S_shadow_path_volume_bounds_bounce,shadow,state_half(S_path_volume_bounds_bounce,index));
    state_vec3(S_shadow_path_throughput,shadow,throughput*eval);
    let group=load_word(table_at(G_objects,ls.object_id,SIZE_objects)+O_lightgroup);state_u8(S_shadow_path_lightgroup,shadow,group+1u);
    vl_copy_volume_stack(index,shadow);
//#if LIGHT_NEE else     state_u16(S_shadow_path_queued_kernel,shadow,K_SHADOW_INTERSECT);let counter=load_word(p(7u)+S_queue_counter)/4u;atomicAdd(&heap[counter+K_SHADOW_INTERSECT],1u);
    let next=select(K_LIGHT_NEE,K_SHADOW_INTERSECT,constant);
    state_u16(S_shadow_path_queued_kernel,shadow,next);let counter=load_word(p(7u)+S_queue_counter)/4u;atomicAdd(&heap[counter+next],1u);
//#endif
}
//#endif
//#if PHASE_HG
// Henyey-Greenstein sampling as shade_volume's volume_phase (phase_henyey_greenstein_sample).
fn vl_phase_sample(direction:vec3<f32>,g:f32,random:vec2<f32>)->vec4<f32>{
    var cosine=rounded_add(1.0,-2.0*random.x);let g2=g*g;
    if(abs(g)>=0.001){let k=rounded_divide(rounded_add(1.0,-g2),rounded_add(1.0,-g*cosine));cosine=rounded_divide(rounded_add(rounded_add(1.0,g2),-k*k),2.0*g);}
    var pdf=0.07957747154594767;
    if(abs(g)>=0.001){let fac=rounded_add(1.0,g*rounded_add(g,-2.0*cosine));pdf=rounded_divide(rounded_add(1.0,-g2),(12.566370614359172*fac)*rounded_sqrt(max(fac,0.0)));}
    let sine=rounded_sqrt(max(rounded_add(1.0,-cosine*cosine),0.0));let phi=6.283185307179586*random.y;
    let sc=native_phase_sincos(phi);let local=vec3<f32>(sine*sc.y,sine*sc.x,cosine);
    var tangent=vec3<f32>(direction.z-direction.y,direction.x-direction.z,direction.y-direction.x);
    if(direction.x==direction.y&&direction.x==direction.z){tangent=vec3<f32>(direction.z-direction.y,direction.x+direction.z,-direction.y-direction.x);}
    tangent=rounded_normalize(tangent);let bitangent=rounded_cross(direction,tangent);
    let wo=rounded_add3(rounded_add3(local.x*tangent,local.y*bitangent),local.z*direction);
    return vec4<f32>(wo,pdf);
}
//#endif
//#if VOLUME_LIGHT
//#if VOLUME_PHASES
// The non-Henyey-Greenstein lobes (shade_volume's vp_phase_sample).
fn vl_phase_sample_lobe(direction:vec3<f32>,c:SvmClosure,random:vec2<f32>)->vec4<f32>{
    let cosine=vp_sample_cos(c,random.x);let pdf=vp_eval(c,cosine);
    let sine=rounded_sqrt(max(rounded_add(1.0,-cosine*cosine),0.0));let phi=6.283185307179586*random.y;
    let sc=native_phase_sincos(phi);let local=vec3<f32>(sine*sc.y,sine*sc.x,cosine);
    var tangent=vec3<f32>(direction.z-direction.y,direction.x-direction.z,direction.y-direction.x);
    if(direction.x==direction.y&&direction.x==direction.z){tangent=vec3<f32>(direction.z-direction.y,direction.x+direction.z,-direction.y-direction.x);}
    tangent=rounded_normalize(tangent);let bitangent=rounded_cross(direction,tangent);
    return vec4<f32>(rounded_add3(rounded_add3(local.x*tangent,local.y*bitangent),local.z*direction),pdf);
}
//#endif
fn vl_finish(index:u32,next:u32){
    if(next==0u){volume_write_depth(index);}
//#if VOLUME_RAY_MARCHING else     let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[counter/4u+VL_K_SHADE_VOLUME],1u);
    let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[counter/4u+vr_volume_kernel()],1u);
//#endif
    if(next!=0u){atomicAdd(&heap[counter/4u+next],1u);}state_u16(S_path_queued_kernel,index,next);
}
// integrate_volume_phase_scatter after the direct light (shade_volume's own phase scatter, deferred here so the
// direct light reads the path state before the bounce).
fn vl_phase_scatter(index:u32,r:SvmResult,sampled_t:f32,object:u32){
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));let tmin=heap_float(state_address(S_ray_tmin,index,4u));
    var flag=state_word(S_path_flag,index);let bounce=state_half(S_path_bounce,index);
    var random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+VL_PRNG_VOLUME_PHASE).xy;
    var selected=0u;var sum=svm_closure_read(r,0u).sample_weight;
    for(var i=1u;i<r.count;i++){let weight=svm_closure_read(r,i).sample_weight;sum+=weight;let threshold=weight/sum;
        if(random.x<threshold){selected=i;random.x/=threshold;}else{random.x=(random.x-threshold)/(1.0-threshold);}}
//#if VOLUME_PHASES else     let g=svm_closure_read(r,selected).roughness;let phase=vl_phase_sample(direction,g,random);if(phase.w==0.0){vl_finish(index,0u);return;}
    let closure=svm_closure_read(r,selected);var g=closure.roughness;var phase:vec4<f32>;
    if(closure.kind==CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID){phase=vl_phase_sample(direction,closure.roughness,random);}
    else{phase=vl_phase_sample_lobe(direction,closure,random);g=select(select(closure.roughness,0.0,closure.kind==VP_CLOSURE_VOLUME_RAYLEIGH_ID),1.0,closure.kind==VP_CLOSURE_VOLUME_FOURNIER_FORAND_ID);}
    if(phase.w==0.0){vl_finish(index,0u);return;}
//#endif
    let position=origin+sampled_t*direction;state_vec3(S_ray_P,index,position);state_vec3(S_ray_D,index,rounded_normalize(phase.xyz));
    state_f32(S_ray_tmin,index,0.0);state_u32(S_ray_tmax,index,0x7f7fffffu);state_f32(S_ray_dP,index,0.0);state_f32(S_ray_dD,index,max(heap_float(state_address(S_ray_dD,index,4u)),1.0-abs(g)));
    state_u32(S_isect_prim,index,NONE);state_u32(S_isect_object,index,object);
    if(p(20u)!=0u&&bounce==0u){state_vec3(S_path_pass_diffuse_weight,index,vec3<f32>(1));state_vec3(S_path_pass_glossy_weight,index,vec3<f32>(0));}
    state_f32(S_path_mis_ray_pdf,index,phase.w);state_vec3(S_path_mis_origin_n,index,position-(origin+direction*tmin));
    state_f32(S_path_min_ray_pdf,index,min(phase.w,heap_float(state_address(S_path_min_ray_pdf,index,4u))));
    let next_bounce=bounce+1u;let volume_bounce=state_half(S_path_volume_bounce,index)+1u;
    if(next_bounce>=data_word(VL_D_max_bounce)||volume_bounce>=data_word(VL_D_max_volume_bounce)){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;}
    flag&=~(VL_PATH_RAY_REFLECT|VL_PATH_RAY_SINGULAR|PATH_RAY_TRANSPARENT|VL_PATH_RAY_IMPORTANCE_BAKE|VL_PATH_RAY_MIS_SKIP|VL_PATH_RAY_MIS_HAD_TRANSMISSION);
    flag|=VL_PATH_RAY_MIS_HAD_TRANSMISSION;flag&=~VL_PATH_RAY_TRANSPARENT_BACKGROUND;if((flag&VL_PATH_RAY_ANY_PASS)==0u){flag|=VL_PATH_RAY_VOLUME_PASS;}
    if(next_bounce==1u){flag&=~VL_PATH_RAY_VOLUME_PRIMARY_TRANSMIT;}
    state_u32(S_path_flag,index,flag);state_u8(S_path_visibility,index,VL_PATH_RAY_VISIBILITY_VOLUME_SCATTER);state_u16(S_path_bounce,index,next_bounce);state_u16(S_path_volume_bounce,index,volume_bounce);
    state_u16(S_path_rng_offset,index,state_half(S_path_rng_offset,index)+RNG_BOUNCE);vl_finish(index,VL_K_INTERSECT_CLOSEST);
}
// DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME, part 3: the direct light at the scatter position shade_volume chose,
// then a deferred indirect scatter.
@compute @workgroup_size(1) fn shade_volume_light(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    let vl=p(24u)+id.x*VL_RECORD_BYTES;let direct=load_word(vl+32u);let indirect=load_word(vl+64u);
//#if PHASE_STAGE else     if(direct==0u&&indirect==0u){return;}store_word(vl+32u,0u);store_word(vl+64u,0u);
    // The indirect phase scatter is shade_volume_phase's (it reads and clears word 64).
    if(direct==0u){return;}store_word(vl+32u,0u);
//#endif
    if(index>=p(10u)||load_word(p(17u))!=0u){return;}
    var r:SvmResult;r.closure_base=p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES;r.count=load_word(vl+60u);
//#if VOLUME_HETEROGENEOUS
    // A heterogeneous path's direct phases (shade_volume_heterogeneous; word 76 is zero for the others).
    if(load_word(vl+76u)!=0u){r.closure_base=load_word(vl+76u);}
//#endif
    if(direct!=0u){vl_direct_light(index,id.x,r,heap_vec3(vl+36u),heap_vec3(vl+48u),heap_vec3(state_address(S_ray_D,index,12u)));if(load_word(p(17u))!=0u){return;}}
//#if !PHASE_STAGE
    if(indirect!=0u){vl_phase_scatter(index,r,heap_float(vl+68u),load_word(vl+72u));}
//#endif
}
//#endif
