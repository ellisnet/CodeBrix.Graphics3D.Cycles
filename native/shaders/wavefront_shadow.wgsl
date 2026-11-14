// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from the pinned Cycles intersect_shadow, shade_shadow, state flow
// and film/light_passes kernels. Opaque/transparent static triangles and light passes.
// Volume, hair, linking and cache-miss paths remain explicit errors; AO shadow
// rays (PATH_RAY_SHADOW_FOR_AO) take the pinned film_write_direct_light AO branch.
// Shadow paths flagged for ambient occlusion exist only in scenes with
// KERNEL_FEATURE_AO (AO_SHADOW); elsewhere the flag is invalid input.
fn shadow_flag_supported(flag:u32)->bool{
//#if VOLUME_SHADOW
    // Shadow paths from and through volumes carry the volume pass and primary-transmit flags.
    let volume_flags=flag&~VL_SHADOW_VOLUME_FLAGS;
//#if AO_SHADOW else     return (volume_flags&(SHADOW_UNSUPPORTED_FLAGS|AO_SHADOW_FLAG))==0u;
    return (volume_flags&SHADOW_UNSUPPORTED_FLAGS)==0u;
//#endif
//#endif
//#if AO_SHADOW else return (flag&(SHADOW_UNSUPPORTED_FLAGS|AO_SHADOW_FLAG))==0u;
    return (flag&SHADOW_UNSUPPORTED_FLAGS)==0u;
//#endif
}
fn shadow_transition(index:u32,current:u32,next:u32){
    let counter=load_word(p(7u)+S_queue_counter);
    atomicSub(&heap[counter/4u+current],1u);
    if(next!=0u){atomicAdd(&heap[counter/4u+next],1u);}
    state_u16(S_shadow_path_queued_kernel,index,next);
}
@compute @workgroup_size(64) fn intersect_shadow(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
//#if SHADOW_CATCHER else     if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_INTERSECT||!shadow_flag_supported(state_word(S_shadow_path_flag,index))){fail();return;}
    if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_INTERSECT||!shadow_flag_supported(state_word(S_shadow_path_flag,index)&~SC_FLAGS)){fail();return;}
//#endif
    let origin=heap_vec3(state_address(S_shadow_ray_P,index,12u));let direction=heap_vec3(state_address(S_shadow_ray_D,index,12u));
    let tmin=heap_float(state_address(S_shadow_ray_tmin,index,4u));let tmax=heap_float(state_address(S_shadow_ray_tmax,index,4u));
    var hit=false;let transparent=data_word(D_integrator_transparent_shadows)!=0u;var count=0u;
//#if CURVE_SHADOW
    var curve_throughput=vec3<f32>(1);
//#endif
    if((bitcast<u32>(origin.x)&0x7f800000u)!=0x7f800000u&&(bitcast<u32>(direction.x)&0x7f800000u)!=0x7f800000u&&dot(direction,direction)!=0.0&&tmin<bitcast<f32>(0x7f7fffffu)){
        let self_object=state_word(S_shadow_self_object,index);let self_prim=state_word(S_shadow_self_prim,index);
        let light_object=state_word(S_shadow_ray_self_light_object,index);let light_prim=state_word(S_shadow_ray_self_light_prim,index);
        if(transparent){let maximum=data_word(D_integrator_transparent_max_bounce);let used=state_half(S_shadow_path_transparent_bounce,index);
//#if !CURVE_SHADOW
//#if SHADOW_CATCHER else             let all=trace_shadow_all(index,origin,direction,tmin,tmax,PATH_RAY_VISIBILITY_SHADOW,self_object,self_prim,light_object,light_prim,maximum-min(maximum,used));
            let all=trace_shadow_all(index,origin,direction,tmin,tmax,sc_shadow_visibility(index,PATH_RAY_VISIBILITY_SHADOW),self_object,self_prim,light_object,light_prim,maximum-min(maximum,used));
//#endif
//#endif
//#if CURVE_SHADOW
            // With curves the transparent traversal has its own entry (intersect_shadow_all): one curve intersection per entry.
            var all=ShadowTrace(false,0u,vec3<f32>(1));fail();return;
//#endif
            hit=all.opaque;count=all.count;
//#if CURVE_SHADOW
            curve_throughput=all.throughput;
//#endif
//#if SHADOW_CATCHER else         }else{let result=trace_native_filtered(origin,direction,tmin,tmax,heap_float(state_address(S_shadow_ray_time,index,4u)),PATH_RAY_VISIBILITY_SHADOW_OPAQUE,self_object,self_prim,light_object,light_prim);hit=result.prim!=NONE;}
        }else{let result=trace_native_filtered(origin,direction,tmin,tmax,heap_float(state_address(S_shadow_ray_time,index,4u)),sc_shadow_visibility(index,PATH_RAY_VISIBILITY_SHADOW_OPAQUE),self_object,self_prim,light_object,light_prim);hit=result.prim!=NONE;}
//#endif
    }
    if(load_word(p(17u))!=0u){return;}
    if(transparent){if(hit){state_vec3(S_shadow_path_throughput,index,heap_vec3(state_address(S_shadow_path_throughput,index,12u))*vec3<f32>(0));count=0u;}
        else if(count>0u){shadow_hit_sort(index,min(count,SHADOW_HIT_CAPACITY));}state_u16(S_shadow_path_packed_num_hits,index,count);
//#if CURVE_SHADOW
        if(!hit&&any(curve_throughput!=vec3<f32>(1))){state_vec3(S_shadow_path_throughput,index,heap_vec3(state_address(S_shadow_path_throughput,index,12u))*curve_throughput);}
//#endif
    }else if(!hit){state_u16(S_shadow_path_packed_num_hits,index,0u);}
    shadow_transition(index,K_SHADOW_INTERSECT,select(K_SHADOW_SHADE,0u,hit));
}
//#if CURVE_SHADOW
// intersect_shadow for scenes with transparent shadows and curves: the shadow-all traversal alone (shadow.cpp picks the entry).
@compute @workgroup_size(64) fn intersect_shadow_all(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
//#if SHADOW_CATCHER else     if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_INTERSECT||!shadow_flag_supported(state_word(S_shadow_path_flag,index))){fail();return;}
    if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_INTERSECT||!shadow_flag_supported(state_word(S_shadow_path_flag,index)&~SC_FLAGS)){fail();return;}
//#endif
    let origin=heap_vec3(state_address(S_shadow_ray_P,index,12u));let direction=heap_vec3(state_address(S_shadow_ray_D,index,12u));
    let tmin=heap_float(state_address(S_shadow_ray_tmin,index,4u));let tmax=heap_float(state_address(S_shadow_ray_tmax,index,4u));
    var hit=false;let transparent=data_word(D_integrator_transparent_shadows)!=0u;var count=0u;
//#if CURVE_SHADOW
    var curve_throughput=vec3<f32>(1);
//#endif
    if((bitcast<u32>(origin.x)&0x7f800000u)!=0x7f800000u&&(bitcast<u32>(direction.x)&0x7f800000u)!=0x7f800000u&&dot(direction,direction)!=0.0&&tmin<bitcast<f32>(0x7f7fffffu)){
        let self_object=state_word(S_shadow_self_object,index);let self_prim=state_word(S_shadow_self_prim,index);
        let light_object=state_word(S_shadow_ray_self_light_object,index);let light_prim=state_word(S_shadow_ray_self_light_prim,index);
        if(transparent){let maximum=data_word(D_integrator_transparent_max_bounce);let used=state_half(S_shadow_path_transparent_bounce,index);
//#if SHADOW_CATCHER else             let all=trace_shadow_all(index,origin,direction,tmin,tmax,PATH_RAY_VISIBILITY_SHADOW,self_object,self_prim,light_object,light_prim,maximum-min(maximum,used));
            let all=trace_shadow_all(index,origin,direction,tmin,tmax,sc_shadow_visibility(index,PATH_RAY_VISIBILITY_SHADOW),self_object,self_prim,light_object,light_prim,maximum-min(maximum,used));
//#endif
            hit=all.opaque;count=all.count;
//#if CURVE_SHADOW
            curve_throughput=all.throughput;
//#endif
        }else{fail();return;}
    }
    if(load_word(p(17u))!=0u){return;}
    if(transparent){if(hit){state_vec3(S_shadow_path_throughput,index,heap_vec3(state_address(S_shadow_path_throughput,index,12u))*vec3<f32>(0));count=0u;}
        else if(count>0u){shadow_hit_sort(index,min(count,SHADOW_HIT_CAPACITY));}state_u16(S_shadow_path_packed_num_hits,index,count);
//#if CURVE_SHADOW
        if(!hit&&any(curve_throughput!=vec3<f32>(1))){state_vec3(S_shadow_path_throughput,index,heap_vec3(state_address(S_shadow_path_throughput,index,12u))*curve_throughput);}
//#endif
    }else if(!hit){state_u16(S_shadow_path_packed_num_hits,index,0u);}
    shadow_transition(index,K_SHADOW_INTERSECT,select(K_SHADOW_SHADE,0u,hit));
}
//#endif
//#if SHADOW_CATCHER
// SHADOW_CATCHER_PATH_VISIBILITY: shadow catcher paths see only shadow catcher
// objects (their visibility bits shifted by 16; the opaque mask likewise).
fn sc_shadow_visibility(index:u32,visibility:u32)->u32{
    return select(visibility,visibility<<16u,(state_word(S_shadow_path_flag,index)&SC_PATH_PASS)!=0u);
}
//#endif
fn shadow_write_film(index:u32){
    let flag=state_word(S_shadow_path_flag,index);
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_shadow_path_render_pixel_index,index);
    if(pixel>=p(11u)/stride){fail();return;}let base=p(4u)+pixel*stride*4u;
    let flags=data_word(D_film_light_pass_flag);let group=state_byte(S_shadow_path_lightgroup,index);let go=data_word(D_film_pass_lightgroup);
    if(flags!=0u&&group!=0u&&go!=PASS_NONE&&(group>stride/3u||go>stride-3u*group)){fail();return;}
    var value=heap_vec3(state_address(S_shadow_path_throughput,index,12u));
    value=select(value,vec3<f32>(0),(bitcast<vec3<u32>>(value)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
    let bounce=state_half(S_shadow_path_bounce,index);
    let limit=select(data_float(D_integrator_sample_clamp_direct),data_float(D_integrator_sample_clamp_indirect),bounce>0u);
    let total=abs(value.x)+abs(value.y)+abs(value.z);if(total>limit){value*=limit/total;}
//#if AO_SHADOW
    // film_write_direct_light, ambient occlusion: the AO pass for camera paths,
    // and fast GI's additive term weighted by the unshadowed AO throughput.
    if((flag&AO_SHADOW_FLAG)!=0u){
        let features=data_word(D_kernel_features);
        if((features&AO_FEATURE_PASS)!=0u&&(state_byte(S_shadow_path_visibility,index)&VIS_CAMERA)!=0u){film_rgb(base,data_word(D_film_pass_ao),value);}
        if((features&AO_FEATURE_ADDITIVE)!=0u){let ao=value*heap_vec3(state_address(S_shadow_path_unshadowed_throughput,index,12u));
//#if VOLUME_SHADOW
//#if !VOLUME_SHADOW_CATCHER
            vl_shadow_guidance(base,flag,state_byte(S_shadow_path_visibility,index),ao);
//#endif
//#endif
//#if SHADOW_CATCHER else             if((flags&MASK_COMBINED)!=0u){film_rgb(base,data_word(D_film_pass_combined),ao);}
            // film_write_combined_pass: the shadow catcher split first (matte, catcher pass).
            let ao_handled=sc_combined(flag,base,ao,select(PASS_NONE,data_word(D_film_pass_combined),(flags&MASK_COMBINED)!=0u));
//#endif
//#if VOLUME_SHADOW_CATCHER
            if(!ao_handled){vl_shadow_guidance(base,flag,state_byte(S_shadow_path_visibility,index),ao);}
//#endif
//#if SHADOW_CATCHER else             if(data_word(D_film_pass_adaptive_aux_buffer)!=PASS_NONE&&(countOneBits(state_word(S_shadow_path_sample,index)&0xaaaaaaaau)&1u)!=0u){
            if(!ao_handled&&data_word(D_film_pass_adaptive_aux_buffer)!=PASS_NONE&&(countOneBits(state_word(S_shadow_path_sample,index)&0xaaaaaaaau)&1u)!=0u){
//#endif
                film_rgb(base,data_word(D_film_pass_adaptive_aux_buffer),2.0*ao);}}
        shadow_transition(index,K_SHADOW_SHADE,0u);return;
    }
//#endif
//#if SHADOW_CATCHER else     if((flags&MASK_COMBINED)!=0u){film_rgb(base,data_word(D_film_pass_combined),value);}
    let sc_handled=sc_combined(flag,base,value,select(PASS_NONE,data_word(D_film_pass_combined),(flags&MASK_COMBINED)!=0u));
//#endif
//#if VOLUME_SHADOW
    // film_write_combined_pass: the volume scattering probability guiding pass.
//#if VOLUME_SHADOW_CATCHER else     vl_shadow_guidance(base,flag,state_byte(S_shadow_path_visibility,index),value);
    if(!sc_handled){vl_shadow_guidance(base,flag,state_byte(S_shadow_path_visibility,index),value);}
//#endif
//#endif
//#if SHADOW_CATCHER else     if(data_word(D_film_pass_adaptive_aux_buffer)!=PASS_NONE&&(countOneBits(state_word(S_shadow_path_sample,index)&0xaaaaaaaau)&1u)!=0u){
    if(!sc_handled&&data_word(D_film_pass_adaptive_aux_buffer)!=PASS_NONE&&(countOneBits(state_word(S_shadow_path_sample,index)&0xaaaaaaaau)&1u)!=0u){
//#endif
        film_rgb(base,data_word(D_film_pass_adaptive_aux_buffer),2.0*value);
    }
//#if SHADOW_CATCHER else     if(flags!=0u){
    if(flags!=0u&&sc_light_passes(flag)){
//#endif
        if(group!=0u&&go!=PASS_NONE){film_rgb(base,go+3u*(group-1u),value);}
        if(p(20u)!=0u&&(flag&PATH_RAY_SURFACE_PASS)!=0u){
            let diffuse=heap_vec3(state_address(S_shadow_path_pass_diffuse_weight,index,12u));let glossy=heap_vec3(state_address(S_shadow_path_pass_glossy_weight,index,12u));
            film_rgb(base,select(data_word(D_film_pass_glossy_indirect),data_word(D_film_pass_glossy_direct),bounce==0u),glossy*value);
            film_rgb(base,select(data_word(D_film_pass_transmission_indirect),data_word(D_film_pass_transmission_direct),bounce==0u),(vec3<f32>(1)-diffuse-glossy)*value);
            film_rgb(base,select(data_word(D_film_pass_diffuse_indirect),data_word(D_film_pass_diffuse_direct),bounce==0u),diffuse*value);
        }
//#if VOLUME_LIGHT_PASSES
        // film_write_direct_light: indirectly visible through a volume.
        else if(p(20u)!=0u&&(flag&PATH_RAY_VOLUME_PASS)!=0u){film_rgb(base,select(data_word(D_film_pass_volume_indirect),data_word(D_film_pass_volume_direct),bounce==0u),value);}
//#endif
    }
    shadow_transition(index,K_SHADOW_SHADE,0u);
}
@compute @workgroup_size(64) fn shade_shadow(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
    let flag=state_word(S_shadow_path_flag,index);
//#if SHADOW_CATCHER else     if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_SHADE||!shadow_flag_supported(flag)||state_half(S_shadow_path_packed_num_hits,index)!=0u){fail();return;}
    if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_SHADE||!shadow_flag_supported(flag&~SC_FLAGS)||state_half(S_shadow_path_packed_num_hits,index)!=0u){fail();return;}
//#endif
    shadow_write_film(index);
}
// Recorded transparent hits get their native ShaderData in a separate GPU
// dispatch (GPU-private cache, no readback). Keeping the triangle/motion setup
// out of the SVM stage bounds driver compiler pressure: the combined entry
// intermittently crashed the Intel Mesa 25.0.7 backend (brw_opt_combine_constants)
// in cold compiles; the split entries compiled every probe (native/work/b1).
@compute @workgroup_size(64) fn prepare_shadow_shader_data(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}let count=state_half(S_shadow_path_packed_num_hits,index);
    if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_SHADE||count>4095u){return;}
    let recorded=min(count,SHADOW_HIT_CAPACITY);
    for(var slot=0u;slot<recorded;slot++){
        let hit=shadow_hit_load(index,slot);
//#if POINTS
        if((hit.kind&PRIMITIVE_POINT)!=0u){
            let point=point_setup_hit(hit.kind,hit.object,hit.prim,hit.u,hit.v,hit.t,heap_float(state_address(S_shadow_ray_time,index,4u)),heap_vec3(state_address(S_shadow_ray_P,index,12u)),
              heap_vec3(state_address(S_shadow_ray_D,index,12u)),heap_float(state_address(S_shadow_ray_dP,index,4u)),heap_float(state_address(S_shadow_ray_dD,index,4u)));
            let at=surface_cache_address(id.x*SHADOW_HIT_CAPACITY+slot);if(at==0u){return;}surface_data_write(at,point);
            if(point.error!=0u){return;}
            continue;
        }
//#endif
        let sd=surface_setup_hit(hit.kind,hit.object,hit.prim,hit.u,hit.v,hit.t,heap_float(state_address(S_shadow_ray_time,index,4u)),
          heap_vec3(state_address(S_shadow_ray_D,index,12u)),heap_float(state_address(S_shadow_ray_dP,index,4u)),heap_float(state_address(S_shadow_ray_dD,index,4u)));
        let at=surface_cache_address(id.x*SHADOW_HIT_CAPACITY+slot);if(at==0u){return;}surface_data_write(at,sd);
        if(sd.error!=0u){return;}
    }
}
// Bound compiler pressure from the SVM path with one invocation per workgroup.
// Static transparent triangles remain the supported scope; tuning is pending.
@compute @workgroup_size(1) fn shade_shadow_transparent(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}let flag=state_word(S_shadow_path_flag,index);let count=state_half(S_shadow_path_packed_num_hits,index);
//#if SHADOW_CATCHER else     if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_SHADE||!shadow_flag_supported(flag)||count>4095u){fail();return;}
    if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_SHADE||!shadow_flag_supported(flag&~SC_FLAGS)||count>4095u){fail();return;}
//#endif
    let recorded=min(count,SHADOW_HIT_CAPACITY);
//#if LIGHT_PATH_STATE
    svm_shadow_path=index;
//#endif
//#if VOLUME_SHADOW
    // integrate_transparent_shadow with volumes: the medium before each recorded hit, the hit's surface (a
    // volume-only boundary counts a volume bounds bounce and is fully transparent), the stack enter or exit;
    // then the medium after the last hit when no hit is left to record (the world volume included).
    // One SVM evaluation site for the media (vl_shadow_step) and the hit surfaces (compile size: the scene's SVM
    // handlers inline once): stage 0 starts the medium before hit 'slot', 1 evaluates it, 2 shades the hit.
    var slot=0u;var stage=0u;var seg:VlShadow;var evaluated=false;var r:SvmResult;
    for(var guard=0u;guard<0x7fffffffu;guard++){
        if(stage==0u){
            stage=2u;if(volume_enabled()&&(slot<recorded||count<SHADOW_HIT_CAPACITY)){if(vl_shadow_begin(index,slot,recorded,&seg)){stage=1u;evaluated=false;}}
        }
        // Loop-local declarations take explicit assignments (naga does not re-run declaration initializers).
        var geometry:SurfaceData;var volume:bool;volume=false;var need:bool;need=false;
        if(stage==1u){
            if(evaluated){geometry=vl_shadow_geometry(index,seg);volume=true;need=true;}
        }else{
            if(slot==recorded){break;}
            geometry=surface_cached(id.x*SHADOW_HIT_CAPACITY+slot);
            if(geometry.error!=0u){return;}
            if((geometry.shader_flag&SHADOW_ONLY_VOLUME)!=0u){state_u16(S_shadow_path_volume_bounds_bounce,index,state_half(S_shadow_path_volume_bounds_bounce,index)+1u);}
            else{need=true;}
        }
        r=SvmResult();r.transparency=vec3<f32>(1);
        if(need){
            var previous:SvmResult;previous=SvmResult();var flag:u32;flag=0u;var path:u32;path=index;if(volume){previous=seg.r;flag=PATH_RAY_EXTINCTION;path=NONE;}
            r=svm_evaluate_context_seeded(path,geometry,0u,0u,flag,PATH_RAY_VISIBILITY_SHADOW,0u,volume,previous);
        }
        if(stage==1u){
            // The medium: evaluate the next stack entry, or the segment's next position, until it ends.
            let step=vl_shadow_step(index,&seg,r,evaluated);evaluated=true;
            if(step==2u){shadow_transition(index,K_SHADOW_SHADE,0u);return;}
            if(load_word(p(17u))!=0u){return;}
            if(step==1u){stage=2u;}
            continue;
        }
        if(r.error!=0u){return;}
//#if RAY_PORTAL else         let sd=geometry;let transparency=r.transparency;
        // integrate_transparent_surface_shadow: a ray portal blocks the shadow ray (after the volume stack update), and
        // surface_shader_transparency counts the transparency only with SR_TRANSPARENT or SR_RAY_PORTAL: a Ray Portal BSDF
        // that found no closure slot (shadow evaluation) added its weight to the transparency but set neither flag. A
        // volume-only boundary (not evaluated) passes.
        let sd=geometry;let transparency=select(vec3<f32>(0),r.transparency,!need||(r.runtime_flag&(SR_TRANSPARENT|RP_SR_RAY_PORTAL))==SR_TRANSPARENT);
//#endif
        vl_shadow_stack_enter_exit(index,sd.object_id,sd.shader,(sd.runtime_flag&SR_BACKFACING)!=0u);
        let throughput=heap_vec3(state_address(S_shadow_path_throughput,index,12u))*transparency;
        if(all(throughput==vec3<f32>(0))){shadow_transition(index,K_SHADOW_SHADE,0u);return;}
        state_vec3(S_shadow_path_throughput,index,throughput);
        state_u16(S_shadow_path_transparent_bounce,index,state_half(S_shadow_path_transparent_bounce,index)+1u);
        state_u16(S_shadow_path_rng_offset,index,state_half(S_shadow_path_rng_offset,index)+RNG_BOUNCE);
        if(state_half(S_shadow_path_volume_bounds_bounce,index)>SHADOW_VOLUME_BOUNDS_MAX){shadow_transition(index,K_SHADOW_SHADE,0u);return;}
        slot++;stage=0u;
    }
//#endif
//#if !VOLUME_SHADOW
    for(var slot=0u;slot<recorded;slot++){
        let hit=shadow_hit_load(index,slot);
        let sd=surface_cached(id.x*SHADOW_HIT_CAPACITY+slot);
        if(sd.error!=0u){return;}if((sd.shader_flag&SHADOW_ONLY_VOLUME)!=0u){fail();return;}
        let r=svm_evaluate_context(index,sd,0u,0u,0u,PATH_RAY_VISIBILITY_SHADOW,0u,false);if(r.error!=0u){return;}
//#if RAY_PORTAL else         let throughput=heap_vec3(state_address(S_shadow_path_throughput,index,12u))*r.transparency;
        // integrate_transparent_surface_shadow: a ray portal blocks the shadow ray; the transparency counts only with
        // SR_TRANSPARENT (a Ray Portal BSDF without a closure slot added its weight but set neither flag).
        let throughput=heap_vec3(state_address(S_shadow_path_throughput,index,12u))*select(vec3<f32>(0),r.transparency,(r.runtime_flag&(SR_TRANSPARENT|RP_SR_RAY_PORTAL))==SR_TRANSPARENT);
//#endif
        if(all(throughput==vec3<f32>(0))){shadow_transition(index,K_SHADOW_SHADE,0u);return;}
        state_vec3(S_shadow_path_throughput,index,throughput);
        state_u16(S_shadow_path_transparent_bounce,index,state_half(S_shadow_path_transparent_bounce,index)+1u);
        state_u16(S_shadow_path_rng_offset,index,state_half(S_shadow_path_rng_offset,index)+RNG_BOUNCE);
        if(state_half(S_shadow_path_volume_bounds_bounce,index)>SHADOW_VOLUME_BOUNDS_MAX){shadow_transition(index,K_SHADOW_SHADE,0u);return;}
    }
//#endif
    if(state_half(S_shadow_path_volume_bounds_bounce,index)>SHADOW_VOLUME_BOUNDS_MAX){shadow_transition(index,K_SHADOW_SHADE,0u);return;}
    if(count>=SHADOW_HIT_CAPACITY){let last=shadow_hit_load(index,recorded-1u).t;
        state_f32(S_shadow_ray_tmin,index,select(bitcast<f32>(bitcast<u32>(last)+1u),1.1754943508222875e-38,last==0.0));
        shadow_transition(index,K_SHADOW_SHADE,K_SHADOW_INTERSECT);return;}
    shadow_write_film(index);
}
//#if CURVE_SHADOW
// rgb_to_rgbe (util/types_rgbe.h) of a saturated colour: bit-exact, the word the attribute stores. roundf is half away from
// zero; the float3 round of the oracles (SSE, NEON) is half to even.
fn curve_rgbe(rgb:vec3<f32>)->u32{
    let a=abs(rgb);let max_v=cv_min(cv_max(cv_max(a.x,a.y),a.z),65280.0);
    if(max_v<5.9604644775390625e-8){return 0u;}
    var e=max(-16i,i32(bitcast<u32>(max_v)>>23u)-127i)+1i;var v=bitcast<f32>(u32(8i-e+127i)<<23u);
    let scaled=max_v*v;let low=floor(scaled);
    if(select(low,low+1.0,rounded_add(scaled,-low)>=0.5)==256.0){e+=1i;v=v*0.5;}
    let signs=((bitcast<u32>(rgb.x)>>31u)<<7u)|((bitcast<u32>(rgb.y)>>31u)<<6u)|((bitcast<u32>(rgb.z)>>31u)<<5u);
    var word=((u32(e+15i)&0x1fu)|signs)<<24u;let count=rounded_steps(3u);
    for(var k=0u;k<count;k++){
        if(k>=3u){continue;}
        let x=select(select(a.z,a.y,k==1u),a.x,k==0u)*v;let down=floor(x);let d=rounded_add(x,-down);
        let even=(u32(down)&1u)==0u;let r=select(down,down+1.0,d>0.5||(d==0.5&&!even));
        word|=u32(cv_min(r,255.0))<<(8u*k);
    }
    return word;
}
// DEVICE_KERNEL_SHADER_EVAL_CURVE_SHADOW_TRANSPARENCY (bake/bake.h kernel_curve_shadow_transparency_evaluate, for
// Hair::update_shadow_transparency before the BVH build): p(1) items, p(2) KernelShaderEvalInput array, p(3) the native
// chunk offset, p(4) the float output, p(30) this dispatch's first item. shader_setup_from_curve: thick basis whatever the
// shape, at the centre keys, time 0.5, the arbitrary view direction, no differentials. The Light Path node's depths and the
// raytrace nodes do not run (the kernel's feature mask: svm_light_path below, the shadow module has no raytrace handlers).
@compute @workgroup_size(1) fn shader_eval_curve_shadow_transparency(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    let item=id.x+p(30u);if(item>=p(1u)){return;}let offset=p(3u)+item;
    let input=p(2u)+offset*CV_EVAL_INPUT_BYTES;let o=cv_order();
    let object=load_word(input+CV_EVAL_INPUT_object);let prim=load_word(input+CV_EVAL_INPUT_prim);let u=heap_float(input+CV_EVAL_INPUT_u);let segment=load_word(input+CV_EVAL_INPUT_v);
    if(object>=table_count(G_objects)||object>=table_count(G_object_flag)||prim>=table_count(G_curves)){fail();return;}
    var sd:SurfaceData;sd.object_id=object;sd.prim=prim;sd.primitive_type=(segment<<PRIMITIVE_NUM_BITS)|PRIMITIVE_CURVE_THICK;sd.u=u;sd.v=0.0;sd.time=0.5;sd.ray_length=0.0;sd.runtime_flag=0u;
    let curve=table_at(G_curves,prim,16u);let first=load_word(curve+4u);let keys=load_word(curve+8u);
    sd.shader=load_word(curve);let shader_id=sd.shader&SHADER_MASK;if(shader_id>=table_count(G_shaders)){fail();return;}
    sd.shader_flag=load_word(table_at(G_shaders,shader_id,SIZE_shaders)+SH_flags);
    sd.object_flag=load_word(table_at(G_object_flag,object,4u));
//#if MOTION_OBJECT
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){sd.motion_tfm=motion_object(object,0.5);sd.motion_itfm=motion_inverse(sd.motion_tfm);}
//#endif
    let k0=first+segment;let k1=k0+1u;let ka=select(k0-1u,first,k0==first);let kb=min(k1+1u,first+keys-1u);
    let position_offset=bitcast<i32>(load_word(table_at(G_objects,object,SIZE_objects)+O_position_offset));
    let c0=curve_key(position_offset,ka);let c1=curve_key(position_offset,k0);let c2=curve_key(position_offset,k1);let c3=curve_key(position_offset,kb);
    if(load_word(p(17u))!=0u){return;}
    sd.P=cv_eval(c0,c1,c2,c3,u,o).xyz;sd.dPdu=cv_derivative(c0,c1,c2,c3,u,o).xyz;
    if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)==0u){sd.P=surface_object_point(sd,sd.P,false);sd.dPdu=surface_object_direction(sd,sd.dPdu,false);}
    sd.wi=cv_normalize(cv_cross(vec3<f32>(0.0,1.0,0.0),sd.dPdu,o),o);sd.N=sd.wi;sd.Ng=sd.wi;sd.dPdv=cv_cross(sd.dPdu,sd.Ng,o);
    svm_curve_shadow_eval=true;
    let r=svm_evaluate_context(NONE,sd,0u,0u,0u,PATH_RAY_VISIBILITY_SHADOW,0u,false);if(r.error!=0u){return;}
    // surface_shader_transparency, saturate.
    var transparency=r.transparency;if((sd.shader_flag&SHADOW_ONLY_VOLUME)!=0u){transparency=vec3<f32>(1);}
    let saturated=vec3<f32>(cv_min(cv_max(transparency.x,0.0),1.0),cv_min(cv_max(transparency.y,0.0),1.0),cv_min(cv_max(transparency.z,0.0),1.0));
    store_word(p(4u)+offset*4u,curve_rgbe(saturated));
}
//#endif
//#if VOLUME_SHADOW
// film_write_volume_scattering_guiding_pass for a shadow path's contribution.
fn vl_shadow_guidance(base:u32,flag:u32,visibility:u32,value:vec3<f32>){
    var offset=PASS_NONE;
    if((flag&PATH_RAY_VOLUME_PRIMARY_TRANSMIT)!=0u){offset=data_word(D_film_pass_volume_transmit);}
    else if((visibility&VIS_VOLUME)!=0u){offset=data_word(D_film_pass_volume_scatter);}
    if(offset!=PASS_NONE){for(var k=0u;k<3u;k++){volume_atomic_add(base+(offset+k)*4u,value[k]);}}
}
// The shadow path's volume stack (shadow_volume_stack).
fn vl_shadow_stack_address(shadow:u32,slot:u32,member:u32)->u32{return load_word(p(7u)+VL_S_shadow_volume_stack+slot*VL_SVS_stride+member)+shadow*4u;}
fn vl_shadow_stack_count(index:u32)->u32{
    let limit=data_word(D_volume_stack_size);
    for(var slot=0u;slot<limit;slot++){
        let shader=load_word(vl_shadow_stack_address(index,slot,VL_SVS_shader));if(shader==NONE){return slot;}
        if((shader&SHADER_MASK)>=table_count(G_shaders)){fail();return NONE;}
    }
    fail();return NONE;
}
// volume_stack_enter_exit<shadow>: the main path's rules on the shadow path's stack.
fn vl_shadow_stack_enter_exit(index:u32,object:u32,shader:u32,backfacing:bool){
    let flags=load_word(table_at(G_shaders,shader&SHADER_MASK,SIZE_shaders)+SH_flags);if((flags&VS_HAS_VOLUME)==0u){return;}
    let count=vl_shadow_stack_count(index);if(count==NONE){return;}
    var found=NONE;for(var slot=0u;slot<count;slot++){
        if(load_word(vl_shadow_stack_address(index,slot,VL_SVS_object))==object&&load_word(vl_shadow_stack_address(index,slot,VL_SVS_shader))==shader){found=slot;break;}}
    if(backfacing){
        if(found==NONE){return;}
        let last=count-1u;if(found!=last){
            store_word(vl_shadow_stack_address(index,found,VL_SVS_object),load_word(vl_shadow_stack_address(index,last,VL_SVS_object)));
            store_word(vl_shadow_stack_address(index,found,VL_SVS_shader),load_word(vl_shadow_stack_address(index,last,VL_SVS_shader)));}
        store_word(vl_shadow_stack_address(index,last,VL_SVS_object),NONE);store_word(vl_shadow_stack_address(index,last,VL_SVS_shader),NONE);
    }else if(found==NONE&&count<data_word(D_volume_stack_size)-1u){
        store_word(vl_shadow_stack_address(index,count,VL_SVS_object),object);store_word(vl_shadow_stack_address(index,count,VL_SVS_shader),shader);
        store_word(vl_shadow_stack_address(index,count+1u,VL_SVS_object),NONE);store_word(vl_shadow_stack_address(index,count+1u,VL_SVS_shader),NONE);
    }
}
// integrate_transparent_volume_shadow + volume_shadow_null_scattering, as a stepper around the caller's one SVM
// evaluation site: vl_shadow_begin sets up the medium before recorded hit 'hit' (false: no medium), then each
// vl_shadow_step consumes one evaluation (every stack entry at one position: closures merged as the native
// volume_shader_eval) and names the next; 0 needs an evaluation, 1 the medium is done, 2 the shadow is opaque.
// Homogeneous media: the stack's extinction at the segment start, the transmittance over the segment.
struct VlShadow {
    sd:SurfaceData, r:SvmResult, entry:u32, count:u32, tmin:f32, tmax:f32, origin:vec3<f32>, direction:vec3<f32>, throughput:vec3<f32>,
//#if VOLUME_HETEROGENEOUS
    hs:VhShadow,
//#endif
//#if VOLUME_RAY_MARCHING
    rm:bool, vt:vec2<f32>, size:f32, shade_offset:f32, max_steps:i32, step:i32, sum:vec3<f32>, tp:vec3<f32>,
//#endif
}
fn vl_shadow_begin(index:u32,hit:u32,recorded:u32,seg:ptr<function,VlShadow>)->bool{
    let count=vl_shadow_stack_count(index);if(count==NONE||count==0u){return false;}
    (*seg).count=count;(*seg).entry=0u;
    (*seg).origin=heap_vec3(state_address(S_shadow_ray_P,index,12u));(*seg).direction=heap_vec3(state_address(S_shadow_ray_D,index,12u));
    var tmin=heap_float(state_address(S_shadow_ray_tmin,index,4u));if(hit>0u){tmin=shadow_hit_load(index,hit-1u).t;}
    var tmax=heap_float(state_address(S_shadow_ray_tmax,index,4u));if(hit<recorded){tmax=shadow_hit_load(index,hit).t;}
    (*seg).tmin=tmin;(*seg).tmax=tmax;
    var sd:SurfaceData;sd.P=rounded_add3((*seg).origin,(*seg).direction*tmin);sd.N=-(*seg).direction;sd.Ng=-(*seg).direction;sd.wi=-(*seg).direction;sd.prim=NONE;sd.primitive_type=VL_PRIMITIVE_VOLUME;
    sd.time=heap_float(state_address(S_shadow_ray_time,index,4u));sd.runtime_flag=SR_IS_VOLUME_SHADER_EVAL;(*seg).sd=sd;
    (*seg).r=SvmResult();(*seg).r.runtime_flag=SR_IS_VOLUME_SHADER_EVAL;
    (*seg).throughput=heap_vec3(state_address(S_shadow_path_throughput,index,12u));
//#if VOLUME_HETEROGENEOUS
    var hs:VhShadow;hs.walk=vh_shadow_heterogeneous(index,count);
    if(hs.walk){
        // volume_shadow_null_scattering for a heterogeneous stack: the octree segments with volume_transmittance<true>.
        hs.sample=state_word(S_shadow_path_sample,index);hs.pixel=state_word(S_shadow_path_rng_pixel,index);
        hs.offset=vh_scramble(state_half(S_shadow_path_rng_offset,index),0x8647ace4u);
        hs.o.t=vec2<f32>(tmin,bitcast<f32>(0x7f7fffffu));hs.o.node=NONE;hs.o.object=NONE;hs.o.shader=NONE;hs.o.scale=VH_MANTISSA_BITS;
    }
//#if VOLUME_RAY_MARCHING
    // volume_shadow_ray_marching with volume_step_init<true> (shadow_path_state_rng_load, no scramble).
    (*seg).rm=data_word(VR_D_volume_ray_marching)!=0u;
    if((*seg).rm){
        hs.walk=false;let object_step=vr_step_size(index,count);(*seg).vt=vec2<f32>(tmin,tmin);(*seg).shade_offset=0.0;(*seg).max_steps=1;
        if(object_step==bitcast<f32>(0x7f7fffffu)){(*seg).size=rounded_add(tmax,-tmin);}
        else{
            (*seg).max_steps=bitcast<i32>(data_word(VR_D_volume_max_steps));let length=rounded_add(tmax,-tmin);var size=min(object_step,length);
            if(length>f32((*seg).max_steps)*size){size=rounded_divide(length,f32((*seg).max_steps));}(*seg).size=size;
//#if VOLUME_SAMPLING_PATTERN else             (*seg).shade_offset=native_rng(state_word(S_shadow_path_sample,index),state_word(S_shadow_path_rng_pixel,index),state_half(S_shadow_path_rng_offset,index)+VR_PRNG_SHADE_OFFSET).x;
            (*seg).shade_offset=native_rng_pattern(state_word(S_shadow_path_sample,index),state_word(S_shadow_path_rng_pixel,index),state_half(S_shadow_path_rng_offset,index)+VR_PRNG_SHADE_OFFSET,1u).x;
//#endif
        }
        (*seg).step=0;(*seg).sum=vec3<f32>(0);(*seg).tp=(*seg).throughput;
    }
//#endif
    (*seg).hs=hs;
//#endif
    return true;
}
// The geometry of the current stack entry's evaluation.
fn vl_shadow_geometry(index:u32,seg:VlShadow)->SurfaceData{
    var sd=seg.sd;
    sd.object_id=load_word(vl_shadow_stack_address(index,seg.entry,VL_SVS_object));sd.shader=load_word(vl_shadow_stack_address(index,seg.entry,VL_SVS_shader));
    sd.shader_flag=load_word(table_at(G_shaders,sd.shader&SHADER_MASK,SIZE_shaders)+SH_flags);sd.object_flag=0u;
    if(sd.object_id!=NONE){sd.object_flag=load_word(table_at(G_object_flag,sd.object_id,SIZE_object_flag));}
//#if VOLUME_MOTION
    // volume_shader_eval_entry: shader_setup_object_transforms at the shadow ray's time for a moving stack object.
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){sd.motion_tfm=motion_object(sd.object_id,sd.time);sd.motion_itfm=motion_inverse(sd.motion_tfm);}
//#endif
    sd.runtime_flag=seg.r.runtime_flag;return sd;
}
fn vl_shadow_step(index:u32,seg:ptr<function,VlShadow>,r:SvmResult,evaluated:bool)->u32{
    var walk=false;var rm=false;
//#if VOLUME_HETEROGENEOUS
    walk=(*seg).hs.walk;
//#endif
//#if VOLUME_RAY_MARCHING
    rm=(*seg).rm;
//#endif
    if(evaluated){
        if(r.error!=0u){return 1u;}
//#if VOLUME_LIGHT_PATH else         (*seg).r=r;(*seg).entry++;if((*seg).entry<(*seg).count){return 0u;}
        // A Light Path estimate evaluates its one entry.
        (*seg).r=r;(*seg).entry++;if((*seg).entry<(*seg).count&&!(*seg).hs.estimating){return 0u;}
//#endif
//#if VOLUME_RAY_MARCHING
        if(rm){
            // The attenuation over the step; exp() every 8th step with the early exit.
            let sigma=select(vec3<f32>(0),r.transparency,(r.runtime_flag&SR_EXTINCTION)!=0u);
            (*seg).sum+=-sigma*rounded_add((*seg).vt.y,-(*seg).vt.x);
            var stop=false;
            if(((*seg).step&7)==0){(*seg).tp=(*seg).throughput*exp((*seg).sum);stop=max((*seg).tp.x,max((*seg).tp.y,(*seg).tp.z))<VH_THROUGHPUT_EPSILON;}
            (*seg).step++;
            if(stop){return vr_shadow_finish(index,seg);}
        }
//#endif
        if(!walk&&!rm){
            if(all(r.transparency==vec3<f32>(0))){return 1u;}
            let throughput=(*seg).throughput*exp(-r.transparency*((*seg).tmax-(*seg).tmin));
            if(all(throughput==vec3<f32>(0))){return 2u;}
            state_vec3(S_shadow_path_throughput,index,throughput);return 1u;
        }
    }else if(!walk&&!rm){return 0u;}
//#if VOLUME_RAY_MARCHING
    if(rm){
        // volume_ray_marching_advance.
        if((*seg).vt.y==(*seg).tmax){return vr_shadow_finish(index,seg);}
        (*seg).vt.x=(*seg).vt.y;(*seg).vt.y=min((*seg).tmax,rounded_add((*seg).tmin,(f32((*seg).step)+1.0)*(*seg).size));
        let shade_t=rounded_add((*seg).vt.x,(*seg).shade_offset*rounded_add((*seg).vt.y,-(*seg).vt.x));
        if((*seg).step>=(*seg).max_steps){return vr_shadow_finish(index,seg);}
        (*seg).sd.P=rounded_add3((*seg).origin,(*seg).direction*shade_t);(*seg).entry=0u;(*seg).r=SvmResult();(*seg).r.runtime_flag=SR_IS_VOLUME_SHADER_EVAL;
        return 0u;
    }
//#endif
//#if VOLUME_HETEROGENEOUS
//#if VOLUME_LIGHT_PATH else     if(evaluated){(*seg).hs.extinction=select(vec3<f32>(0),r.transparency,(r.runtime_flag&SR_EXTINCTION)!=0u);}
    if(evaluated){(*seg).hs.extinction=select(vec3<f32>(0),r.transparency,(r.runtime_flag&SR_EXTINCTION)!=0u);(*seg).hs.est_emission=select(vec3<f32>(0),r.emission,(r.runtime_flag&SR_EMISSION)!=0u);}
//#endif
    var hs=(*seg).hs;var throughput=(*seg).throughput;
    let more=vh_shadow_next(&hs,index,(*seg).origin,(*seg).direction,(*seg).tmax,&throughput,evaluated);
    (*seg).hs=hs;(*seg).throughput=throughput;
    if(!more){if(vh_shadow_store(index,throughput)){return 1u;}return 2u;}
//#if VOLUME_LIGHT_PATH else     (*seg).sd.P=rounded_add3((*seg).origin,(*seg).direction*hs.shade_t);(*seg).entry=0u;(*seg).r=SvmResult();(*seg).r.runtime_flag=SR_IS_VOLUME_SHADER_EVAL;
    (*seg).sd.P=rounded_add3((*seg).origin,(*seg).direction*hs.shade_t);(*seg).entry=select(0u,hs.est_slot,hs.estimating);(*seg).r=SvmResult();(*seg).r.runtime_flag=SR_IS_VOLUME_SHADER_EVAL;
//#endif
//#endif
    return 0u;
}
//#if VOLUME_RAY_MARCHING
fn vr_shadow_finish(index:u32,seg:ptr<function,VlShadow>)->u32{
    if((*seg).vt.y==(*seg).tmax){(*seg).tp=(*seg).throughput*exp((*seg).sum);}
    if(all((*seg).tp==vec3<f32>(0))){return 2u;}
    state_vec3(S_shadow_path_throughput,index,(*seg).tp);return 1u;
}
//#endif
//#if VOLUME_HETEROGENEOUS
// The shadow volume stack for the shared octree tracer.
fn vh_stack_shader(index:u32,slot:u32)->u32{return load_word(vl_shadow_stack_address(index,slot,VL_SVS_shader));}
fn vh_stack_object(index:u32,slot:u32)->u32{return load_word(vl_shadow_stack_address(index,slot,VL_SVS_object));}
fn vh_shadow_heterogeneous(index:u32,count:u32)->bool{
    for(var i=0u;i<count;i++){if((load_word(table_at(G_shaders,vh_stack_shader(index,i)&SHADER_MASK,SIZE_shaders)+SH_flags)&VH_HETEROGENEOUS)!=0u){return true;}}
    return false;
}
// The shadow estimator's state: the octree, the scrambled sequence, the transmittance counters (as VhState).
struct VhShadow {
    walk:bool, o:VhOctree, sample:u32, pixel:u32, offset:u32, tr_N:u32, tr_k:u32, tr_n:u32, tr_i:u32, tr_step:f32, tr_shade:f32,
    tr_t:vec2<f32>, tr_pmf:f32, tau_k:vec3<f32>, tau_j0:vec3<f32>, tau_j1:vec3<f32>, tau_j_1:vec3<f32>, T_k:vec3<f32>, shade_t:f32,
    extinction:vec3<f32>, in_segment:bool, started:bool,
//#if VOLUME_LIGHT_PATH
    // The resumable walk's stage and locals, the set-up, and the Light Path estimate (its entry's slot and emission).
    lstage:u32, first:bool, empty:bool, sigma2:vec2<f32>, tmin2:f32, post_ok:bool, set_i:u32, set_skip_object:u32, set_skip_shader:u32,
    set_local:VhOctree, est:VlpEstimate, estimating:bool, est_slot:u32, est_emission:vec3<f32>,
//#endif
}
fn vh_shadow_store(index:u32,throughput:vec3<f32>)->bool{
    if(all(throughput==vec3<f32>(0))){return false;}
    state_vec3(S_shadow_path_throughput,index,throughput);return true;
}
// One step of volume_shadow_null_scattering's loop: consumes the evaluation (when evaluated), finishes segments
// (volume_transmittance<true>), advances the octree (volume_octree_advance_shadow) and names the next shading
// distance; false when the segment walk ends.
//#if !VOLUME_LIGHT_PATH
fn vh_shadow_next(hs:ptr<function,VhShadow>,index:u32,origin:vec3<f32>,direction:vec3<f32>,ray_tmax:f32,throughput:ptr<function,vec3<f32>>,evaluated:bool)->bool{
    if(evaluated){
        let step=(*hs).tr_i*(*hs).tr_N+(*hs).tr_n;
        if((*hs).tr_N==1u){(*hs).tau_k+=(*hs).extinction;}
        else{let tau=(*hs).tr_step*(*hs).extinction;(*hs).tau_k+=tau*f32((*hs).tr_N);
            if(step%2u==0u){(*hs).tau_j0+=tau*2.0;}else{(*hs).tau_j1+=tau*2.0;}(*hs).tau_j_1+=tau;}
        (*hs).tr_i++;
        if((*hs).tr_i>=(*hs).tr_k){
            var done=false;
            if((*hs).tr_N==1u){*throughput*=select(exp(-(*hs).tau_k*(*hs).tr_step),vec3<f32>(1),all((*hs).tau_k==vec3<f32>(0)));done=true;}
            else{(*hs).T_k+=exp(-(*hs).tau_k);(*hs).tau_k=vec3<f32>(0);(*hs).tr_i=0u;(*hs).tr_n++;
                if((*hs).tr_n>=(*hs).tr_N){
                    let T_j_1=exp(-(*hs).tau_j_1);let T_k=(*hs).T_k/f32((*hs).tr_N);let T_j=0.5*(exp(-(*hs).tau_j0)+exp(-(*hs).tau_j1));
                    *throughput*=T_k+(T_j_1-T_j)/(*hs).tr_pmf;done=true;
                }}
            if(done){
                (*hs).in_segment=false;
                if(max(abs((*throughput).x),max(abs((*throughput).y),abs((*throughput).z)))<VH_THROUGHPUT_EPSILON){return false;}
                (*hs).o.t.x=(*hs).o.t.y;
            }
        }
        if((*hs).in_segment){(*hs).shade_t=min((*hs).tr_t.y,(*hs).tr_t.x+((*hs).tr_shade+f32((*hs).tr_i*(*hs).tr_N+(*hs).tr_n))*(*hs).tr_step);return true;}
    }
    // The first call: volume_octree_setup<true> (false: no medium, the throughput unchanged); then
    // volume_octree_advance_shadow. One setup call site for both (compile size).
    var first=!(*hs).started;(*hs).started=true;
    var empty=false;var sigma=vec2<f32>(0);var tmin=0.0;
    if(!first){
        (*hs).offset+=VH_PRNG_BOUNCE_NUM;empty=!((*hs).o.t.x<(*hs).o.t.y);
        sigma=select((*hs).o.sigma,vec2<f32>(bitcast<f32>(0x7f7fffffu),-bitcast<f32>(0x7f7fffffu)),empty);tmin=(*hs).o.t.x;
    }
    for(var guard=0u;guard<65536u;guard++){
        if(!first&&!(empty||(sigma.y-sigma.x)*((*hs).o.t.y-(*hs).o.t.x)<1.0)){break;}
        var o=(*hs).o;var ok=true;if(!first){ok=vh_octree_advance(index,origin,direction,ray_tmax,&o);}if(ok){ok=vh_octree_setup(index,origin,direction,ray_tmax,&o);}(*hs).o=o;
        if(first){
            if(!ok){return false;}first=false;
            (*hs).offset+=VH_PRNG_BOUNCE_NUM;empty=!((*hs).o.t.x<(*hs).o.t.y);
            sigma=select((*hs).o.sigma,vec2<f32>(bitcast<f32>(0x7f7fffffu),-bitcast<f32>(0x7f7fffffu)),empty);tmin=(*hs).o.t.x;continue;
        }
        if(!ok){if(!((*hs).o.t.x<(*hs).o.t.y)){return false;}break;}
        sigma=vec2<f32>(min(sigma.x,(*hs).o.sigma.x),max(sigma.y,(*hs).o.sigma.y));(*hs).o.sigma=sigma;(*hs).o.t.x=tmin;
        empty=!((*hs).o.t.x<(*hs).o.t.y);
    }
    // volume_transmittance<true> over the segment.
    let t=(*hs).o.t;let sigma_c=(*hs).o.sigma.y-(*hs).o.sigma.x;let length=t.y-t.x;
    let k=vh_expected_steps(sigma_c*length);
//#if VOLUME_SAMPLING_PATTERN else     let rand=native_rng((*hs).sample,0u,(*hs).offset+VH_PRNG_EXPANSION_ORDER).x;
    let rand=native_rng_pattern((*hs).sample,0u,(*hs).offset+VH_PRNG_EXPANSION_ORDER,1u).x;
//#endif
    var N=1u;(*hs).tr_pmf=1.0;
    if(sigma_c!=0.0){let r=vh_geometric_r();let q=1.0-r;let n=min(i32(floor(log(rand)/log(q))),4);(*hs).tr_pmf=select(r*pow(q,f32(n)),pow(q,f32(n)),n==4);N=1u<<u32(max(n,0));}
    (*hs).tr_N=N;(*hs).tr_k=k;(*hs).tr_n=0u;(*hs).tr_i=0u;(*hs).tr_t=t;
//#if VOLUME_SAMPLING_PATTERN else     (*hs).tr_shade=native_rng((*hs).sample,(*hs).pixel,(*hs).offset+VH_PRNG_SHADE_OFFSET).x;(*hs).tr_step=length/f32(N*k);
    (*hs).tr_shade=native_rng_pattern((*hs).sample,(*hs).pixel,(*hs).offset+VH_PRNG_SHADE_OFFSET,1u).x;(*hs).tr_step=length/f32(N*k);
//#endif
    (*hs).tau_k=vec3<f32>(0);(*hs).tau_j0=vec3<f32>(0);(*hs).tau_j1=vec3<f32>(0);(*hs).tau_j_1=vec3<f32>(0);(*hs).T_k=vec3<f32>(0);
    (*hs).in_segment=true;(*hs).shade_t=min(t.y,t.x+(*hs).tr_shade*(*hs).tr_step);return true;
}
//#endif
//#if VOLUME_LIGHT_PATH
fn vh_shadow_next(hs:ptr<function,VhShadow>,index:u32,origin:vec3<f32>,direction:vec3<f32>,ray_tmax:f32,throughput:ptr<function,vec3<f32>>,evaluated:bool)->bool{
    // Resumable: the octree set-up and advance stop for the Light Path entries' estimates (stage 9 asks for one entry's
    // evaluation at est's next position; the driver evaluates entry est_slot alone).
    var ev=evaluated;
    for(var guard=0u;guard<65536u;guard++){
        let stage=(*hs).lstage;
        if(stage==0u){
            if(ev){ev=false;
                let step=(*hs).tr_i*(*hs).tr_N+(*hs).tr_n;
                if((*hs).tr_N==1u){(*hs).tau_k+=(*hs).extinction;}
                else{let tau=(*hs).tr_step*(*hs).extinction;(*hs).tau_k+=tau*f32((*hs).tr_N);
                    if(step%2u==0u){(*hs).tau_j0+=tau*2.0;}else{(*hs).tau_j1+=tau*2.0;}(*hs).tau_j_1+=tau;}
                (*hs).tr_i++;
                if((*hs).tr_i>=(*hs).tr_k){
                    var done=false;
                    if((*hs).tr_N==1u){*throughput*=select(exp(-(*hs).tau_k*(*hs).tr_step),vec3<f32>(1),all((*hs).tau_k==vec3<f32>(0)));done=true;}
                    else{(*hs).T_k+=exp(-(*hs).tau_k);(*hs).tau_k=vec3<f32>(0);(*hs).tr_i=0u;(*hs).tr_n++;
                        if((*hs).tr_n>=(*hs).tr_N){
                            let T_j_1=exp(-(*hs).tau_j_1);let T_k=(*hs).T_k/f32((*hs).tr_N);let T_j=0.5*(exp(-(*hs).tau_j0)+exp(-(*hs).tau_j1));
                            *throughput*=T_k+(T_j_1-T_j)/(*hs).tr_pmf;done=true;
                        }}
                    if(done){
                        (*hs).in_segment=false;
                        if(max(abs((*throughput).x),max(abs((*throughput).y),abs((*throughput).z)))<VH_THROUGHPUT_EPSILON){return false;}
                        (*hs).o.t.x=(*hs).o.t.y;
                    }
                }
                if((*hs).in_segment){(*hs).shade_t=min((*hs).tr_t.y,(*hs).tr_t.x+((*hs).tr_shade+f32((*hs).tr_i*(*hs).tr_N+(*hs).tr_n))*(*hs).tr_step);return true;}
            }
            (*hs).lstage=1u;
        }
        else if(stage==1u){ // The first call: volume_octree_setup<true>; then volume_octree_advance_shadow.
            (*hs).first=!(*hs).started;(*hs).started=true;
            if(!(*hs).first){
                (*hs).offset+=VH_PRNG_BOUNCE_NUM;(*hs).empty=!((*hs).o.t.x<(*hs).o.t.y);
                (*hs).sigma2=select((*hs).o.sigma,vec2<f32>(bitcast<f32>(0x7f7fffffu),-bitcast<f32>(0x7f7fffffu)),(*hs).empty);(*hs).tmin2=(*hs).o.t.x;
            }
            (*hs).lstage=2u;
        }
        else if(stage==2u){
            if(!(*hs).first&&!((*hs).empty||((*hs).sigma2.y-(*hs).sigma2.x)*((*hs).o.t.y-(*hs).o.t.x)<1.0)){(*hs).lstage=8u;continue;}
            if(!(*hs).first){
                var o=(*hs).o;let ok=vh_octree_advance(index,origin,direction,ray_tmax,&o);(*hs).o=o;
                if(!ok){(*hs).post_ok=false;(*hs).lstage=7u;continue;}
                // Shadow rays are never camera rays (path_visibility PATH_RAY_VISIBILITY_SHADOW).
                if(vlp_estimates(0u,(*hs).o.shader)){(*hs).est=vlp_start((*hs).o.t,(*hs).o.object,(*hs).o.shader,(*hs).sample,(*hs).pixel,(*hs).offset,3u);(*hs).estimating=true;(*hs).lstage=9u;continue;}
            }
            (*hs).lstage=4u;
        }
        else if(stage==3u){(*hs).o.sigma=(*hs).est.ext;(*hs).lstage=4u;}
        else if(stage==4u){ // volume_octree_setup<true>.
            if((*hs).o.no_overlap){(*hs).post_ok=(*hs).o.t.x<(*hs).o.t.y;(*hs).lstage=7u;}
            else{(*hs).set_i=0u;(*hs).set_skip_object=(*hs).o.object;(*hs).set_skip_shader=(*hs).o.shader;(*hs).lstage=5u;}
        }
        else if(stage==5u){
            let limit=data_word(D_volume_stack_size);var shader=NONE;if((*hs).set_i<limit){shader=vh_stack_shader(index,(*hs).set_i);}
            if(shader==NONE){
                if((*hs).set_i==1u){(*hs).o.no_overlap=true;}
                (*hs).post_ok=(*hs).o.node!=NONE&&(*hs).o.t.x<(*hs).o.t.y;(*hs).lstage=7u;continue;
            }
            let object=vh_stack_object(index,(*hs).set_i);
            if(object==(*hs).set_skip_object&&shader==(*hs).set_skip_shader){var o=(*hs).o;vh_octree_skip(&o);(*hs).o=o;(*hs).set_i++;continue;}
            (*hs).set_local=vlp_local(index,origin,direction,ray_tmax,(*hs).o.t.x,object,shader);
            if(vlp_estimates(0u,shader)){(*hs).est=vlp_start((*hs).set_local.t,object,shader,(*hs).sample,(*hs).pixel,(*hs).offset,6u);(*hs).estimating=true;(*hs).lstage=9u;continue;}
            (*hs).est.ext=vh_extrema((*hs).set_local);(*hs).lstage=6u;
        }
        else if(stage==6u){
            (*hs).o.sigma=(*hs).o.sigma+(*hs).est.ext;var local=(*hs).set_local;
            if(local.t.y<=(*hs).o.t.y){local.sigma=(*hs).o.sigma;(*hs).o=local;}
            (*hs).set_i++;(*hs).lstage=5u;
        }
        else if(stage==7u){ // After the set-up (or a failed advance).
            let ok=(*hs).post_ok;
            if((*hs).first){
                if(!ok){return false;}(*hs).first=false;
                (*hs).offset+=VH_PRNG_BOUNCE_NUM;(*hs).empty=!((*hs).o.t.x<(*hs).o.t.y);
                (*hs).sigma2=select((*hs).o.sigma,vec2<f32>(bitcast<f32>(0x7f7fffffu),-bitcast<f32>(0x7f7fffffu)),(*hs).empty);(*hs).tmin2=(*hs).o.t.x;(*hs).lstage=2u;continue;
            }
            if(!ok){if(!((*hs).o.t.x<(*hs).o.t.y)){return false;}(*hs).lstage=8u;continue;}
            (*hs).sigma2=vec2<f32>(min((*hs).sigma2.x,(*hs).o.sigma.x),max((*hs).sigma2.y,(*hs).o.sigma.y));(*hs).o.sigma=(*hs).sigma2;(*hs).o.t.x=(*hs).tmin2;
            (*hs).empty=!((*hs).o.t.x<(*hs).o.t.y);(*hs).lstage=2u;
        }
        else if(stage==8u){
            // volume_transmittance<true> over the segment.
            let t=(*hs).o.t;let sigma_c=(*hs).o.sigma.y-(*hs).o.sigma.x;let length=t.y-t.x;
            let k=vh_expected_steps(sigma_c*length);
//#if VOLUME_SAMPLING_PATTERN else     let rand=native_rng((*hs).sample,0u,(*hs).offset+VH_PRNG_EXPANSION_ORDER).x;
            let rand=native_rng_pattern((*hs).sample,0u,(*hs).offset+VH_PRNG_EXPANSION_ORDER,1u).x;
//#endif
            var N=1u;(*hs).tr_pmf=1.0;
            if(sigma_c!=0.0){let r=vh_geometric_r();let q=1.0-r;let n=min(i32(floor(log(rand)/log(q))),4);(*hs).tr_pmf=select(r*pow(q,f32(n)),pow(q,f32(n)),n==4);N=1u<<u32(max(n,0));}
            (*hs).tr_N=N;(*hs).tr_k=k;(*hs).tr_n=0u;(*hs).tr_i=0u;(*hs).tr_t=t;
//#if VOLUME_SAMPLING_PATTERN else     (*hs).tr_shade=native_rng((*hs).sample,(*hs).pixel,(*hs).offset+VH_PRNG_SHADE_OFFSET).x;(*hs).tr_step=length/f32(N*k);
            (*hs).tr_shade=native_rng_pattern((*hs).sample,(*hs).pixel,(*hs).offset+VH_PRNG_SHADE_OFFSET,1u).x;(*hs).tr_step=length/f32(N*k);
//#endif
            (*hs).tau_k=vec3<f32>(0);(*hs).tau_j0=vec3<f32>(0);(*hs).tau_j1=vec3<f32>(0);(*hs).tau_j_1=vec3<f32>(0);(*hs).T_k=vec3<f32>(0);
            (*hs).in_segment=true;(*hs).shade_t=min(t.y,t.x+(*hs).tr_shade*(*hs).tr_step);(*hs).lstage=0u;return true;

        }
        else if(stage==9u){ // volume_estimate_extrema: one entry's evaluations.
            if(!ev){
                (*hs).shade_t=vlp_shade_t((*hs).est);(*hs).est_slot=0u;
                for(var k=0u;k<data_word(D_volume_stack_size);k++){if(vh_stack_shader(index,k)==(*hs).est.shader&&vh_stack_object(index,k)==(*hs).est.object){(*hs).est_slot=k;break;}}
                return true;
            }
            ev=false;var e=(*hs).est;let done=vlp_add(&e,(*hs).extinction,(*hs).est_emission);(*hs).est=e;
            if(done){(*hs).estimating=false;(*hs).lstage=(*hs).est.resume;}
        }
        else{return false;}
    }
    return false;
}
//#endif
//#endif
//#endif
