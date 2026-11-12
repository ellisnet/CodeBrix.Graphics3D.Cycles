// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
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
//#if FILM_MOTION
// transform_perspective_direction: a.x * t.x.x + a.y * t.x.y + a.z * t.x.z per row, each step rounded.
fn bgm_row(t:vec4<f32>,a:vec3<f32>)->f32{return rounded_add(rounded_add(a.x*t.x,a.y*t.y),a.z*t.z);}
fn bgm_direction(address:u32,a:vec3<f32>)->vec3<f32>{return vec3<f32>(bgm_row(heap_vec4(address),a),bgm_row(heap_vec4(address+16u),a),bgm_row(heap_vec4(address+32u),a));}
//#endif
//#if FILM_DENOISING
// film_write_emission_or_background_pass, denoising albedo: the emission or
// background contribution times the path's denoising feature throughput.
fn film_denoising_albedo(index:u32,base:u32,contribution:vec3<f32>){
    if((state_word(S_path_flag,index)&DN_PATH_RAY_DENOISING_FEATURES)==0u){return;}
    film_rgb(base,data_word(DN_D_pass_denoising_albedo),heap_vec3(state_address(S_path_denoising_feature_throughput,index,12u))*contribution);
}
//#endif
fn background_visible(shader:u32,visibility:u32,flag:u32)->bool{
    return !(((shader&SHADER_EXCLUDE_DIFFUSE)!=0u&&(visibility&PATH_RAY_VISIBILITY_DIFFUSE)!=0u)||
        ((shader&SHADER_EXCLUDE_GLOSSY)!=0u&&(visibility&PATH_RAY_VISIBILITY_GLOSSY)!=0u&&(flag&PATH_RAY_REFLECT)!=0u)||
        ((shader&SHADER_EXCLUDE_TRANSMIT)!=0u&&(visibility&PATH_RAY_VISIBILITY_TRANSMIT)!=0u)||
        ((shader&SHADER_EXCLUDE_SCATTER)!=0u&&(visibility&PATH_RAY_VISIBILITY_VOLUME_SCATTER)!=0u)||
        ((shader&SHADER_EXCLUDE_CAMERA)!=0u&&(visibility&VIS_CAMERA)!=0u));
}
@compute @workgroup_size(64) fn shade_background(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
    if(state_half(S_path_queued_kernel,index)!=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND){fail();return;}
    let flag=state_word(S_path_flag,index);let visibility=state_byte(S_path_visibility,index);
//#if SHADOW_CATCHER else     if((flag&BACKGROUND_UNSUPPORTED_FLAGS)!=0u){fail();return;}
    if((flag&(BACKGROUND_UNSUPPORTED_FLAGS&~SC_FLAGS))!=0u){fail();return;}
//#endif
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_path_render_pixel_index,index);
    if(pixel>=p(11u)/stride){fail();return;}let base=p(4u)+pixel*stride*4u;
    integrate_suns(index,base);if(load_word(p(17u))!=0u){return;}
    let light_flags=data_word(D_film_light_pass_flag);let throughput=heap_vec3(state_address(S_path_throughput,index,12u));
    let transparent_ray=data_word(D_background_transparent)!=0u&&(flag&PATH_RAY_TRANSPARENT_BACKGROUND)!=0u;
    var transparency=0.0;if(transparent_ray){transparency=(throughput.x+throughput.y+throughput.z)*(1.0/3.0);}
    var light=vec3<f32>(0);let shader=data_word(D_background_surface_shader);
//#if MNEE
    // A path that MNEE should have resolved skips a caustic background light (MIS).
    var mnee_cull=false;
    if((state_byte(S_path_mnee,index)&MNEE_PATH_CULL_LIGHT_CONNECTION)!=0u&&data_word(D_background_use_mis)!=0u){
        for(var lamp=0u;lamp<data_word(D_integrator_num_lights);lamp++){let l=table_at(G_lights,lamp,SIZE_lights);
            if(load_word(l+L_type)==LIGHT_BACKGROUND&&load_word(l+MNEE_L_use_caustics)!=0u){mnee_cull=true;break;}}}
//#endif
//#if MNEE else     if((!transparent_ray||(light_flags&MASK_BACKGROUND)!=0u)&&background_visible(shader,visibility,flag)){
    if((!transparent_ray||(light_flags&MASK_BACKGROUND)!=0u)&&background_visible(shader,visibility,flag)&&!mnee_cull){
//#endif
        let shader_id=shader&SHADER_MASK;if(shader_id>=p(19u)){fail();return;}
        let address=p(18u)+shader_id*SIZE_shaders;
        if((load_word(address+SH_flags)&SD_HAS_CONSTANT_EMISSION)!=0u){light=heap_vec3(address+SH_constant_emission);}
//#if SVM_NODE_AOV else         else{light=svm_world(index,shader_id);if(load_word(p(17u))!=0u){return;}}
        else{svm_aov_base=base;light=svm_world(index,shader_id);svm_aov_base=0u;if(load_word(p(17u))!=0u){return;}}
//#endif
        if(path_ao(index)){light*=data_float(D_integrator_ao_bounces_factor);}
        // light_sample_mis_weight_forward_background (background light uploaded).
//#if LIGHT_BACKGROUND
        light*=background_mis_weight(index,flag);
//#endif
    }
    var contribution=throughput*light;
    contribution=select(contribution,vec3<f32>(0),(bitcast<vec3<u32>>(contribution)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
    let bounce=state_half(S_path_bounce,index);
    let limit=select(data_float(D_integrator_sample_clamp_direct),data_float(D_integrator_sample_clamp_indirect),i32(bounce)-1>0);
    let total=abs(contribution.x)+abs(contribution.y)+abs(contribution.z);if(total>limit){contribution*=limit/total;}
//#if SHADOW_CATCHER else     if(transparent_ray){volume_transparent_pass(index,base,transparency);}
    var sc_handled=false;if(transparent_ray){sc_transparent_only(flag,base,transparency);volume_transparent_pass(index,base,transparency);}
    else{sc_handled=sc_combined_transparent(flag,base,contribution,transparency,select(PASS_NONE,data_word(D_film_pass_combined),(light_flags&MASK_COMBINED)!=0u));if(!sc_handled){volume_guidance_pass(index,base,contribution);}}
//#endif
//#if SHADOW_CATCHER else     else{volume_guidance_pass(index,base,contribution);}
//#endif
    let sample_id=state_word(S_path_sample,index);
//#if SHADOW_CATCHER else     if((light_flags&MASK_COMBINED)!=0u){
    if(!sc_handled&&(light_flags&MASK_COMBINED)!=0u){
//#endif
//#if !SHADOW_CATCHER
        if(!transparent_ray){film_rgb(base,data_word(D_film_pass_combined),contribution);}
//#endif
        film_add(base+(data_word(D_film_pass_combined)+3u)*4u,transparency);
    }
//#if SHADOW_CATCHER else     if(!transparent_ray&&data_word(D_film_pass_adaptive_aux_buffer)!=PASS_NONE&&((countOneBits(sample_id&0xaaaaaaaau)&1u)!=0u)){
    if(!sc_handled&&!transparent_ray&&data_word(D_film_pass_adaptive_aux_buffer)!=PASS_NONE&&((countOneBits(sample_id&0xaaaaaaaau)&1u)!=0u)){
//#endif
        film_rgb(base,data_word(D_film_pass_adaptive_aux_buffer),contribution*2.0);
        film_add(base+(data_word(D_film_pass_adaptive_aux_buffer)+3u)*4u,0.0);
    }
    if(light_flags!=0u){
//#if FILM_DENOISING
        film_denoising_albedo(index,base,contribution);
//#endif
        let group=data_word(D_background_lightgroup);let group_offset=data_word(D_film_pass_lightgroup);
//#if SHADOW_CATCHER else         if(group!=NONE&&group_offset!=PASS_NONE){film_rgb(base,group_offset+group*3u,contribution);}
        if(sc_light_passes(flag)&&group!=NONE&&group_offset!=PASS_NONE){film_rgb(base,group_offset+group*3u,contribution);}
//#endif
        if((flag&PATH_RAY_ANY_PASS)==0u){film_rgb(base,data_word(D_film_pass_background),contribution);}
//#if SHADOW_CATCHER else         else if(p(20u)!=0u&&(flag&PATH_RAY_SURFACE_PASS)!=0u){
        else if(sc_light_passes(flag)&&p(20u)!=0u&&(flag&PATH_RAY_SURFACE_PASS)!=0u){
//#endif
            let diffuse=heap_vec3(state_address(S_path_pass_diffuse_weight,index,12u));let glossy=heap_vec3(state_address(S_path_pass_glossy_weight,index,12u));
            film_rgb(base,select(data_word(D_film_pass_glossy_indirect),data_word(D_film_pass_glossy_direct),bounce==1u),glossy*contribution);
            film_rgb(base,select(data_word(D_film_pass_transmission_indirect),data_word(D_film_pass_transmission_direct),bounce==1u),(vec3<f32>(1)-diffuse-glossy)*contribution);
            film_rgb(base,select(data_word(D_film_pass_diffuse_indirect),data_word(D_film_pass_diffuse_direct),bounce==1u),diffuse*contribution);
        }
//#if VOLUME_LIGHT_PASSES
        // film_write_emission_or_background_pass: indirectly visible through a volume.
//#if SHADOW_CATCHER else         else if(p(20u)!=0u&&(flag&PATH_RAY_VOLUME_PASS)!=0u){film_rgb(base,select(data_word(D_film_pass_volume_indirect),data_word(D_film_pass_volume_direct),bounce==1u),contribution);}
        else if(sc_light_passes(flag)&&p(20u)!=0u&&(flag&PATH_RAY_VOLUME_PASS)!=0u){film_rgb(base,select(data_word(D_film_pass_volume_indirect),data_word(D_film_pass_volume_direct),bounce==1u),contribution);}
//#endif
//#endif
    }
//#if SHADOW_CATCHER else     if((flag&PATH_RAY_TRANSPARENT_BACKGROUND)!=0u&&(flag&PATH_RAY_SINGLE_PASS_DONE)==0u&&sample_id==0u){
    if((flag&PATH_RAY_TRANSPARENT_BACKGROUND)!=0u&&(flag&SC_PATH_PASS)==0u&&(flag&PATH_RAY_SINGLE_PASS_DONE)==0u&&sample_id==0u){
//#endif
        let flags=data_word(D_film_pass_flag);
        if((flags&MASK_DEPTH)!=0u){store_word(base+data_word(D_film_pass_depth)*4u,0u);}
        if((flags&MASK_OBJECT_ID)!=0u){store_word(base+data_word(D_film_pass_object_id)*4u,0u);}
        if((flags&MASK_MATERIAL_ID)!=0u){store_word(base+data_word(D_film_pass_material_id)*4u,0u);}
        if((flags&MASK_POSITION)!=0u){let address=base+data_word(D_film_pass_position)*4u;store_word(address,0u);store_word(address+4u,0u);store_word(address+8u,0u);}
    }
//#if FILM_MOTION
    // film_write_data_passes_background, motion: camera_motion_vector_direction (perspective camera).
    if((flag&PATH_RAY_TRANSPARENT_BACKGROUND)!=0u&&(flag&FPM_PATH_RAY_SHADOW_CATCHER_PASS)==0u&&(flag&PATH_RAY_SINGLE_PASS_DONE)==0u&&(data_word(D_film_pass_flag)&FPM_MASK_MOTION)!=0u){
        let D=heap_vec3(state_address(S_ray_D,index,12u));
//#if CAMERA_PANORAMA_INVERSE else         let c=bgm_direction(p(6u)+FPM_D_worldtoraster,D);let a=rounded_add3(bgm_direction(p(6u)+FPM_D_perspective_pre,D),-c);let b=rounded_add3(c,-bgm_direction(p(6u)+FPM_D_perspective_post,D));
        var c=bgm_direction(p(6u)+FPM_D_worldtoraster,D);var a=rounded_add3(bgm_direction(p(6u)+FPM_D_perspective_pre,D),-c);var b=rounded_add3(c,-bgm_direction(p(6u)+FPM_D_perspective_post,D));
        // camera_motion_vector_direction, panoramic cameras: the camera-space directions through direction_to_panorama.
        if(data_word(D_cam_type)==CPI_CAMERA_PANORAMA){let m=cpi_motion_vector(D,D,D,false);a=vec3<f32>(m.xy,0.0);b=vec3<f32>(m.zw,0.0);}
//#endif
        let at=base+data_word(FPM_D_pass_motion)*4u;film_add(at,a.x);film_add(at+4u,a.y);film_add(at+8u,b.x);film_add(at+12u,b.y);
        film_add(base+data_word(FPM_D_pass_motion_weight)*4u,1.0);
    }
//#endif
//#if FILM_DENOISING
    // film_write_denoising_features_background: the depth feature of camera rays that miss.
    if((flag&DN_PATH_RAY_DENOISING_FEATURES)!=0u&&bounce==0u&&data_word(DN_D_pass_denoising_depth)!=PASS_NONE){
        store_word(base+data_word(DN_D_pass_denoising_depth)*4u,0x7f7fffffu);}
//#endif
//#if SHADOW_CATCHER
    if((flag&SC_PATH_BACKGROUND)!=0u){sc_background_continue(index,flag);return;}
//#endif
    volume_write_depth(index);
    let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[(counter+DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND*4u)/4u],1u);
    state_u16(S_path_queued_kernel,index,0u);
}
//#if SHADOW_CATCHER
// shade_background for PATH_RAY_SHADOW_CATCHER_BACKGROUND: the background behind
// the shadow catcher is written, then the path continues with the shadow catcher
// surface (integrator_intersect_next_kernel_after_shadow_catcher_volume, sorted).
fn sc_background_continue(index:u32,flag:u32){
    state_u32(S_path_flag,index,flag&~SC_PATH_BACKGROUND);
//#if VOLUME_SHADOW_CATCHER
    // integrator_intersect_next_kernel_after_shadow_catcher_background: a stack that is not empty is re-initialized first.
    if(volume_active(index)){
        let queue=load_word(p(7u)+S_queue_counter);atomicSub(&heap[(queue+DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND*4u)/4u],1u);
        atomicAdd(&heap[(queue+DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK*4u)/4u],1u);state_u16(S_path_queued_kernel,index,DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK);return;
    }
//#endif
    let prim=state_word(S_isect_prim,index);if(prim==NONE){fail();return;}
//#if !CURVE_SHADOW_CATCHER
//#if POINTS else     let shader=load_word(table_at(G_tri_shader,prim,SIZE_tri_shader))&SHADER_MASK;
    let shader=point_or_triangle_shader(prim,state_word(S_isect_type,index))&SHADER_MASK;
//#endif
//#endif
//#if CURVE_SHADOW_CATCHER
    var shader=0u;
    if((state_word(S_isect_type,index)&PRIMITIVE_CURVE)!=0u){shader=load_word(table_at(G_curves,prim,16u))&SHADER_MASK;}
//#if POINTS else     else{shader=load_word(table_at(G_tri_shader,prim,SIZE_tri_shader))&SHADER_MASK;}
    else{shader=point_or_triangle_shader(prim,state_word(S_isect_type,index))&SHADER_MASK;}
//#endif
//#endif
//#if MNEE
    // integrator_intersect_next_kernel_after_shadow_catcher_volume: a caustic receiver goes to INTERSECT_MNEE, unsorted.
    if(data_word(MNEE_D_use_caustics)!=0u&&(load_word(table_at(G_object_flag,state_word(S_isect_object,index),4u))&SD_OBJECT_CAUSTICS_RECEIVER)!=0u){
        let queue=load_word(p(7u)+S_queue_counter);atomicSub(&heap[(queue+DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND*4u)/4u],1u);
        atomicAdd(&heap[(queue+MNEE_K_INTERSECT*4u)/4u],1u);state_u16(S_path_queued_kernel,index,MNEE_K_INTERSECT);return;}
//#endif
    if((load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags)&SD_HAS_RAYTRACE)!=0u){fail();return;}
    let divisor=load_word(p(7u)+S_sort_partition_divisor);let key=shader+data_word(D_max_shaders)*(index/divisor);
    if(key>=p(15u)){fail();return;}
    let sort=load_word(p(7u)+S_sort_key_counter+DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE*POINTER_SIZE);
    atomicAdd(&heap[(sort+key*4u)/4u],1u);state_u32(S_path_shader_sort_key,index,key);
    let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[(counter+DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND*4u)/4u],1u);
    atomicAdd(&heap[(counter+DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE*4u)/4u],1u);
    state_u16(S_path_queued_kernel,index,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE);
}
//#endif
//#if LIGHT_BACKGROUND
fn background_mis_weight(index:u32,flag:u32)->f32{
    if(data_word(D_background_use_mis)==0u||(flag&PATH_RAY_MIS_SKIP)!=0u){return 1.0;}
    let direction=heap_vec3(state_address(S_ray_D,index,12u));
    let forward=heap_float(state_address(S_path_mis_ray_pdf,index,4u));
//#if BACKGROUND_PORTAL
    // background_light_pdf reads the ray origin for the portals.
    bgl_portal_origin=heap_vec3(state_address(S_ray_P,index,12u));
//#endif
//#if LIGHT_TREE else let pdf=bgl_pdf(direction)*data_float(D_integrator_distribution_pdf_lights);
    // light_sample_mis_weight_forward_background: the tree's selection pdf of the background light.
    var selection=data_float(D_integrator_distribution_pdf_lights);if(data_word(LT_D_use_light_tree)!=0u){selection=light_tree_forward_read(index,0u);}
    let pdf=bgl_pdf(direction)*selection;
//#endif
    return rounded_divide(forward*forward,rounded_add(forward*forward,pdf*pdf));
}
// DEVICE_KERNEL_SHADER_EVAL_BACKGROUND (kernel_background_evaluate) for the
// importance map: p(1) items, p(2) KernelShaderEvalInput array, p(3) the
// native chunk offset, p(4) float3 output, p(30) this dispatch's first item
// (dispatches are sliced to the SVM stack slots).
@compute @workgroup_size(64) fn shader_eval_background(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    let item=id.x+p(30u);if(item>=p(1u)){return;}let offset=p(3u)+item;
    let input=p(2u)+offset*16u;let width=bitcast<i32>(load_word(input));let height=bitcast<i32>(load_word(input+4u));
    let u=heap_float(input+8u);let v=heap_float(input+12u);
    let direction=bgl_direction(u,v);
    let du=rounded_divide(1.0,f32(width));let dv=rounded_divide(1.0,f32(height));
    let direction_du=bgl_direction(rounded_add(u,du),v);let direction_dv=bgl_direction(u,rounded_add(v,dv));
    let a=direction_du-direction;let b=direction_dv-direction;
    let dD=0.5*rounded_add(rounded_sqrt(rounded_dot(a,a)),rounded_sqrt(rounded_dot(b,b)));
    let sd=bgl_world_data(direction,dD,0.5);if(sd.error!=0u){return;}
    let r=svm_evaluate_context(NONE,sd,0u,0u,PATH_RAY_EMISSION|BGL_IMPORTANCE_BAKE,0u,0u,false);if(r.error!=0u){return;}
    var color=r.emission;
    color=select(color,vec3<f32>(0),(bitcast<vec3<u32>>(color)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
    let out=p(4u)+offset*12u;store_word(out,bitcast<u32>(color.x));store_word(out+4u,bitcast<u32>(color.y));store_word(out+8u,bitcast<u32>(color.z));
}
//#endif
