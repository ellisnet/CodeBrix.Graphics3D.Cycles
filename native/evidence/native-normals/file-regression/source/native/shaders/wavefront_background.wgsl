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
fn background_visible(shader:u32,visibility:u32,flag:u32)->bool{
    return !(((shader&SHADER_EXCLUDE_DIFFUSE)!=0u&&(visibility&PATH_RAY_VISIBILITY_DIFFUSE)!=0u)||
        ((shader&SHADER_EXCLUDE_GLOSSY)!=0u&&(visibility&PATH_RAY_VISIBILITY_GLOSSY)!=0u&&(flag&PATH_RAY_REFLECT)!=0u)||
        ((shader&SHADER_EXCLUDE_TRANSMIT)!=0u&&(visibility&PATH_RAY_VISIBILITY_TRANSMIT)!=0u)||
        ((shader&SHADER_EXCLUDE_CAMERA)!=0u&&(visibility&VIS_CAMERA)!=0u));
}
@compute @workgroup_size(64) fn shade_background(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
    if(state_half(S_path_queued_kernel,index)!=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND){fail();return;}
    let flag=state_word(S_path_flag,index);let visibility=state_byte(S_path_visibility,index);
    if((flag&BACKGROUND_UNSUPPORTED_FLAGS)!=0u||(visibility&PATH_RAY_VISIBILITY_VOLUME_SCATTER)!=0u){fail();return;}
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_path_render_pixel_index,index);
    if(pixel>=p(11u)/stride){fail();return;}let base=p(4u)+pixel*stride*4u;
    let light_flags=data_word(D_film_light_pass_flag);let throughput=heap_vec3(state_address(S_path_throughput,index,12u));
    let transparent_ray=data_word(D_background_transparent)!=0u&&(flag&PATH_RAY_TRANSPARENT_BACKGROUND)!=0u;
    var transparency=0.0;if(transparent_ray){transparency=(throughput.x+throughput.y+throughput.z)*(1.0/3.0);}
    var light=vec3<f32>(0);let shader=data_word(D_background_surface_shader);
    if((!transparent_ray||(light_flags&MASK_BACKGROUND)!=0u)&&background_visible(shader,visibility,flag)){
        let shader_id=shader&SHADER_MASK;if(shader_id>=p(19u)){fail();return;}
        let address=p(18u)+shader_id*SIZE_shaders;
        if((load_word(address+SH_flags)&SD_HAS_CONSTANT_EMISSION)!=0u){light=heap_vec3(address+SH_constant_emission);}
        else{light=svm_world(index,shader_id);if(load_word(p(17u))!=0u){return;}}
        if(path_ao(index)){light*=data_float(D_integrator_ao_bounces_factor);}
    }
    var contribution=throughput*light;
    contribution=select(contribution,vec3<f32>(0),(bitcast<vec3<u32>>(contribution)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
    let bounce=state_half(S_path_bounce,index);
    let limit=select(data_float(D_integrator_sample_clamp_direct),data_float(D_integrator_sample_clamp_indirect),i32(bounce)-1>0);
    let total=abs(contribution.x)+abs(contribution.y)+abs(contribution.z);if(total>limit){contribution*=limit/total;}
    let sample_id=state_word(S_path_sample,index);
    if((light_flags&MASK_COMBINED)!=0u){
        if(!transparent_ray){film_rgb(base,data_word(D_film_pass_combined),contribution);}
        film_add(base+(data_word(D_film_pass_combined)+3u)*4u,transparency);
    }
    if(!transparent_ray&&data_word(D_film_pass_adaptive_aux_buffer)!=PASS_NONE&&((countOneBits(sample_id&0xaaaaaaaau)&1u)!=0u)){
        film_rgb(base,data_word(D_film_pass_adaptive_aux_buffer),contribution*2.0);
        film_add(base+(data_word(D_film_pass_adaptive_aux_buffer)+3u)*4u,0.0);
    }
    if(light_flags!=0u){
        let group=data_word(D_background_lightgroup);let group_offset=data_word(D_film_pass_lightgroup);
        if(group!=NONE&&group_offset!=PASS_NONE){film_rgb(base,group_offset+group*3u,contribution);}
        if((flag&PATH_RAY_ANY_PASS)==0u){film_rgb(base,data_word(D_film_pass_background),contribution);}
        else if(p(20u)!=0u&&(flag&PATH_RAY_SURFACE_PASS)!=0u){
            let diffuse=heap_vec3(state_address(S_path_pass_diffuse_weight,index,12u));let glossy=heap_vec3(state_address(S_path_pass_glossy_weight,index,12u));
            film_rgb(base,select(data_word(D_film_pass_glossy_indirect),data_word(D_film_pass_glossy_direct),bounce==1u),glossy*contribution);
            film_rgb(base,select(data_word(D_film_pass_transmission_indirect),data_word(D_film_pass_transmission_direct),bounce==1u),(vec3<f32>(1)-diffuse-glossy)*contribution);
            film_rgb(base,select(data_word(D_film_pass_diffuse_indirect),data_word(D_film_pass_diffuse_direct),bounce==1u),diffuse*contribution);
        }
    }
    if((flag&PATH_RAY_TRANSPARENT_BACKGROUND)!=0u&&(flag&PATH_RAY_SINGLE_PASS_DONE)==0u&&sample_id==0u){
        let flags=data_word(D_film_pass_flag);
        if((flags&MASK_DEPTH)!=0u){store_word(base+data_word(D_film_pass_depth)*4u,0u);}
        if((flags&MASK_OBJECT_ID)!=0u){store_word(base+data_word(D_film_pass_object_id)*4u,0u);}
        if((flags&MASK_MATERIAL_ID)!=0u){store_word(base+data_word(D_film_pass_material_id)*4u,0u);}
        if((flags&MASK_POSITION)!=0u){let address=base+data_word(D_film_pass_position)*4u;store_word(address,0u);store_word(address+4u,0u);store_word(address+8u,0u);}
    }
    let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[(counter+DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND*4u)/4u],1u);
    state_u16(S_path_queued_kernel,index,0u);
}
