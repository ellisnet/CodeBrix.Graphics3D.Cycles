// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from the pinned Cycles intersect_shadow, shade_shadow, state flow
// and film/light_passes kernels. Opaque/transparent static triangles and light passes.
// Volume, hair, AO, linking and cache-miss paths remain explicit errors.
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
    if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_INTERSECT||(state_word(S_shadow_path_flag,index)&SHADOW_UNSUPPORTED_FLAGS)!=0u){fail();return;}
    let origin=heap_vec3(state_address(S_shadow_ray_P,index,12u));let direction=heap_vec3(state_address(S_shadow_ray_D,index,12u));
    let tmin=heap_float(state_address(S_shadow_ray_tmin,index,4u));let tmax=heap_float(state_address(S_shadow_ray_tmax,index,4u));
    var hit=false;let transparent=data_word(D_integrator_transparent_shadows)!=0u;var count=0u;
    if((bitcast<u32>(origin.x)&0x7f800000u)!=0x7f800000u&&(bitcast<u32>(direction.x)&0x7f800000u)!=0x7f800000u&&dot(direction,direction)!=0.0&&tmin<bitcast<f32>(0x7f7fffffu)){
        let self_object=state_word(S_shadow_self_object,index);let self_prim=state_word(S_shadow_self_prim,index);
        let light_object=state_word(S_shadow_ray_self_light_object,index);let light_prim=state_word(S_shadow_ray_self_light_prim,index);
        if(transparent){let maximum=data_word(D_integrator_transparent_max_bounce);let used=state_half(S_shadow_path_transparent_bounce,index);
            let all=trace_shadow_all(index,origin,direction,tmin,tmax,PATH_RAY_VISIBILITY_SHADOW,self_object,self_prim,light_object,light_prim,maximum-min(maximum,used));
            hit=all.opaque;count=all.count;
        }else{let result=trace_native_filtered(origin,direction,tmin,tmax,heap_float(state_address(S_shadow_ray_time,index,4u)),PATH_RAY_VISIBILITY_SHADOW_OPAQUE,self_object,self_prim,light_object,light_prim);hit=result.prim!=NONE;}
    }
    if(load_word(p(17u))!=0u){return;}
    if(transparent){if(hit){state_vec3(S_shadow_path_throughput,index,heap_vec3(state_address(S_shadow_path_throughput,index,12u))*vec3<f32>(0));count=0u;}
        else if(count>0u){shadow_hit_sort(index,min(count,SHADOW_HIT_CAPACITY));}state_u16(S_shadow_path_packed_num_hits,index,count);
    }else if(!hit){state_u16(S_shadow_path_packed_num_hits,index,0u);}
    shadow_transition(index,K_SHADOW_INTERSECT,select(K_SHADOW_SHADE,0u,hit));
}
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
    if((flags&MASK_COMBINED)!=0u){film_rgb(base,data_word(D_film_pass_combined),value);}
    if(data_word(D_film_pass_adaptive_aux_buffer)!=PASS_NONE&&(countOneBits(state_word(S_shadow_path_sample,index)&0xaaaaaaaau)&1u)!=0u){
        film_rgb(base,data_word(D_film_pass_adaptive_aux_buffer),2.0*value);
    }
    if(flags!=0u){
        if(group!=0u&&go!=PASS_NONE){film_rgb(base,go+3u*(group-1u),value);}
        if(p(20u)!=0u&&(flag&PATH_RAY_SURFACE_PASS)!=0u){
            let diffuse=heap_vec3(state_address(S_shadow_path_pass_diffuse_weight,index,12u));let glossy=heap_vec3(state_address(S_shadow_path_pass_glossy_weight,index,12u));
            film_rgb(base,select(data_word(D_film_pass_glossy_indirect),data_word(D_film_pass_glossy_direct),bounce==0u),glossy*value);
            film_rgb(base,select(data_word(D_film_pass_transmission_indirect),data_word(D_film_pass_transmission_direct),bounce==0u),(vec3<f32>(1)-diffuse-glossy)*value);
            film_rgb(base,select(data_word(D_film_pass_diffuse_indirect),data_word(D_film_pass_diffuse_direct),bounce==0u),diffuse*value);
        }
    }
    shadow_transition(index,K_SHADOW_SHADE,0u);
}
@compute @workgroup_size(64) fn shade_shadow(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
    let flag=state_word(S_shadow_path_flag,index);
    if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_SHADE||(flag&SHADOW_UNSUPPORTED_FLAGS)!=0u||state_half(S_shadow_path_packed_num_hits,index)!=0u){fail();return;}
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
    if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_SHADE||(flag&SHADOW_UNSUPPORTED_FLAGS)!=0u||count>4095u){fail();return;}
    let recorded=min(count,SHADOW_HIT_CAPACITY);
    for(var slot=0u;slot<recorded;slot++){
        let hit=shadow_hit_load(index,slot);
        let sd=surface_cached(id.x*SHADOW_HIT_CAPACITY+slot);
        if(sd.error!=0u){return;}if((sd.shader_flag&SHADOW_ONLY_VOLUME)!=0u){fail();return;}
        let r=svm_evaluate_context(index,sd,0u,0u,0u,PATH_RAY_VISIBILITY_SHADOW,0u,false);if(r.error!=0u){return;}
        let throughput=heap_vec3(state_address(S_shadow_path_throughput,index,12u))*r.transparency;
        if(all(throughput==vec3<f32>(0))){shadow_transition(index,K_SHADOW_SHADE,0u);return;}
        state_vec3(S_shadow_path_throughput,index,throughput);
        state_u16(S_shadow_path_transparent_bounce,index,state_half(S_shadow_path_transparent_bounce,index)+1u);
        state_u16(S_shadow_path_rng_offset,index,state_half(S_shadow_path_rng_offset,index)+RNG_BOUNCE);
        if(state_half(S_shadow_path_volume_bounds_bounce,index)>SHADOW_VOLUME_BOUNDS_MAX){shadow_transition(index,K_SHADOW_SHADE,0u);return;}
    }
    if(state_half(S_shadow_path_volume_bounds_bounce,index)>SHADOW_VOLUME_BOUNDS_MAX){shadow_transition(index,K_SHADOW_SHADE,0u);return;}
    if(count>=SHADOW_HIT_CAPACITY){let last=shadow_hit_load(index,recorded-1u).t;
        state_f32(S_shadow_ray_tmin,index,select(bitcast<f32>(bitcast<u32>(last)+1u),1.1754943508222875e-38,last==0.0));
        shadow_transition(index,K_SHADOW_SHADE,K_SHADOW_INTERSECT);return;}
    shadow_write_film(index);
}
