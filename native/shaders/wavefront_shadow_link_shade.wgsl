// SPDX-FileCopyrightText: 2011-2023 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// DEVICE_KERNEL_INTEGRATOR_SHADE_DEDICATED_LIGHT, part two (surface module), from the pinned
// kernel/integrator/shade_dedicated_light.h: an emissive triangle's forward weight (the tree pdf in its own entry for
// compiler pressure, as shade_surface_light_select), the dedicated shadow path through
// integrate_direct_light_shadow_init_common, and the return of the path to intersect_closest.
//#if SHADOW_LINKING
//#if LIGHT_TREE
// light_sample_mis_weight_forward_surface's light_tree_pdf, for a triangle that can be a MIS emitter at all
// (its facing is known only after setup; the pdf does not depend on it).
@compute @workgroup_size(1) fn dedicated_light_tree_pdf(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)||load_word(p(17u))!=0u){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=DL_K_SHADE){fail();return;}
    if(state_word(S_isect_type,index)==PRIMITIVE_LAMP||(state_word(S_path_flag,index)&PATH_RAY_MIS_SKIP)!=0u){return;}
//#if CURVES
    // light_sample_mis_weight_forward_surface: has_mis &= (type & PRIMITIVE_TRIANGLE).
    if((state_word(S_isect_type,index)&PRIMITIVE_CURVE)!=0u){return;}
//#endif
//#if POINTS
    // light_sample_mis_weight_forward_surface: has_mis &= triangle (no tree pdf for a point).
    if((state_word(S_isect_type,index)&PRIMITIVE_POINT)!=0u){return;}
//#endif
    let prim=state_word(S_isect_prim,index);let shader=load_word(table_at(G_tri_shader,prim,SIZE_tri_shader))&SHADER_MASK;
    if((load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags)&(SD_MIS_FRONT|SD_MIS_BACK))==0u){return;}
    var sd:SurfaceData;sd.object_id=state_word(S_isect_object,index);sd.prim=prim;var r:SvmResult;
    let result=light_selection_compute(index,sd,r,false,true);if(load_word(p(17u))!=0u){return;}
    store_word(p(24u)+id.x*DL_RECORD_BYTES+28u,bitcast<u32>(result.forward_pdf));
}
//#endif
// shadow_linking_shade_surface_emission, shadow_linking_shade and integrator_shade_dedicated_light.
@compute @workgroup_size(1) fn shade_dedicated_light(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)||load_word(p(17u))!=0u){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=DL_K_SHADE){fail();return;}
    let at=p(24u)+id.x*DL_RECORD_BYTES;var wanted=false;var value=vec3<f32>(0);var group=NONE;var constant=true;
    if(state_word(S_isect_type,index)==PRIMITIVE_LAMP){
        wanted=load_word(at)!=0u;
        if(wanted){value=heap_vec3(at+8u);group=load_word(at+20u);constant=load_word(at+24u)!=0u;}
    }else{
//#if LIGHT_TREE
        // triangle_light_forward_pdf reads the tree pdf at light_select_record+LIGHT_SELECT_RECORD+28.
        light_select_record=at-LIGHT_SELECT_RECORD;
//#endif
        let sd=surface_from_ray(index);if(sd.error!=0u||load_word(p(17u))!=0u){return;}
        if((sd.shader_flag&SD_HAS_ONLY_VOLUME)==0u){
//#if !CURVES
            triangle_light_prefill(index,sd,true,false);
            let mis=triangle_light_forward_weight(index,sd);if(load_word(p(17u))!=0u){return;}
//#endif
//#if CURVES
            // light_sample_mis_weight_forward_surface: a curve hit takes no forward MIS.
            var mis=1.0;
            if((sd.primitive_type&PRIMITIVE_CURVE)==0u){
                triangle_light_prefill(index,sd,true,false);
                mis=triangle_light_forward_weight(index,sd);if(load_word(p(17u))!=0u){return;}
            }
//#endif
            store_word(at+4u,bitcast<u32>(mis));
            let shader=table_at(G_shaders,sd.shader&SHADER_MASK,SIZE_shaders);constant=(load_word(shader+SH_flags)&SD_HAS_CONSTANT_EMISSION)!=0u;
            value=select(vec3<f32>(1),heap_vec3(shader+SH_constant_emission),constant)*(mis*heap_float(state_address(S_shadow_link_dedicated_light_weight,index,4u)));
            group=load_word(table_at(G_objects,sd.object_id,SIZE_objects)+O_lightgroup);wanted=true;
        }
    }
    if(wanted&&any(value!=vec3<f32>(0))){
        // Slot reserved per input (control op 5), so slot numbers do not depend on atomic order.
        let shadow=load_word(p(28u))+id.x;if(shadow>=p(25u)){fail();return;}
        // shadow_linking_setup_ray_from_intersection: the bounce's ray up to the emitter, the main path's self primitives.
        state_vec3(S_shadow_ray_P,shadow,heap_vec3(state_address(S_ray_P,index,12u)));state_vec3(S_shadow_ray_D,shadow,heap_vec3(state_address(S_ray_D,index,12u)));
        state_f32(S_shadow_ray_tmin,shadow,heap_float(state_address(S_ray_tmin,index,4u)));state_f32(S_shadow_ray_tmax,shadow,heap_float(state_address(S_isect_t,index,4u)));
        state_f32(S_shadow_ray_time,shadow,heap_float(state_address(S_ray_time,index,4u)));state_f32(S_shadow_ray_dP,shadow,heap_float(state_address(S_ray_dP,index,4u)));
        state_f32(S_shadow_ray_dD,shadow,heap_float(state_address(S_ray_dD,index,4u)));
        state_u32(S_shadow_self_object,shadow,state_word(S_shadow_link_last_isect_object,index));state_u32(S_shadow_self_prim,shadow,state_word(S_shadow_link_last_isect_prim,index));
        state_u32(S_shadow_ray_self_light_object,shadow,state_word(S_isect_object,index));state_u32(S_shadow_ray_self_light_prim,shadow,state_word(S_isect_prim,index));
        // integrate_direct_light_shadow_init_common.
        if((data_word(D_kernel_features)&LT_FEATURE_LIGHT_TREE)==0u){state_f32(S_shadow_path_bsdf_eval_average,shadow,(value.x+value.y+value.z)*(1.0/3.0));}
        state_u32(S_shadow_path_render_pixel_index,shadow,state_word(S_path_render_pixel_index,index));
        state_u32(S_shadow_path_sample,shadow,state_word(S_path_sample,index));state_u32(S_shadow_path_rng_pixel,shadow,state_word(S_path_rng_pixel,index));
        // The light is accumulated as the main path's: the shadow path's bounce is one less (shadow_linking_shade).
        state_u16(S_shadow_path_rng_offset,shadow,state_half(S_path_rng_offset,index));state_u16(S_shadow_path_bounce,shadow,state_half(S_path_bounce,index)-1u);
        state_u16(S_shadow_path_diffuse_bounce,shadow,state_half(S_path_diffuse_bounce,index));state_u16(S_shadow_path_glossy_bounce,shadow,state_half(S_path_glossy_bounce,index));
        state_u16(S_shadow_path_transmission_bounce,shadow,state_half(S_path_transmission_bounce,index));state_u16(S_shadow_path_transparent_bounce,shadow,state_half(S_path_transparent_bounce,index));
        state_u16(S_shadow_path_volume_bounds_bounce,shadow,state_half(S_path_volume_bounds_bounce,index));
//#if VOLUME_SHADOW
        vl_copy_volume_stack(index,shadow);
//#endif
        state_vec3(S_shadow_path_throughput,shadow,heap_vec3(state_address(S_path_throughput,index,12u))*value);
        state_u8(S_shadow_path_lightgroup,shadow,group+1u);
        if(p(20u)!=0u){state_vec3(S_shadow_path_pass_diffuse_weight,shadow,heap_vec3(state_address(S_path_pass_diffuse_weight,index,12u)));
            state_vec3(S_shadow_path_pass_glossy_weight,shadow,heap_vec3(state_address(S_path_pass_glossy_weight,index,12u)));}
        state_u8(S_shadow_path_visibility,shadow,state_byte(S_path_visibility,index));
        state_u32(S_shadow_path_flag,shadow,state_word(S_path_flag,index)|DL_PATH_RAY_SHADOW_FOR_LIGHT_LINKING);
//#if LIGHT_NEE else state_u16(S_shadow_path_queued_kernel,shadow,K_SHADOW_INTERSECT);let shadow_counter=load_word(p(7u)+S_queue_counter)/4u;atomicAdd(&heap[shadow_counter+K_SHADOW_INTERSECT],1u);
        let next_shadow=select(K_LIGHT_NEE,K_SHADOW_INTERSECT,constant);
        state_u16(S_shadow_path_queued_kernel,shadow,next_shadow);let shadow_counter=load_word(p(7u)+S_queue_counter)/4u;atomicAdd(&heap[shadow_counter+next_shadow],1u);
//#endif
    }
    // shadow_linking_restore_last_primitives, integrator_shade_surface_next_kernel.
    state_u32(S_isect_prim,index,state_word(S_shadow_link_last_isect_prim,index));state_u32(S_isect_object,index,state_word(S_shadow_link_last_isect_object,index));
    let counter=load_word(p(7u)+S_queue_counter)/4u;
    atomicSub(&heap[counter+DL_K_SHADE],1u);atomicAdd(&heap[counter+K_CLOSEST],1u);state_u16(S_path_queued_kernel,index,K_CLOSEST);
}
//#endif
