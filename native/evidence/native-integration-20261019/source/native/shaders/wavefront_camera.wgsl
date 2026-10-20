// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2019-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Cycles init_from_camera, perspective camera, path initialization and Sobol.
// Native byte offsets are generated from the compiled upstream types.
fn filter_value(value:f32)->f32{
    let x=clamp(value,0.0,1.0)*f32(FILTER_SIZE-1u);let i=min(u32(x),FILTER_SIZE-1u);
    let a=p(8u)+(data_word(D_tables_filter_table_offset)+i)*4u;let t=x-f32(i);let first=heap_float(a);
    if(t==0.0){return first;}
    return (1.0-t)*first+t*heap_float(p(8u)+(data_word(D_tables_filter_table_offset)+min(i+1u,FILTER_SIZE-1u))*4u);
}
fn camera_shutter(value:f32)->f32{
    let x=clamp(value,0.0,1.0)*f32(SHUTTER_SIZE-1u);let i=min(u32(x),SHUTTER_SIZE-1u);
    let address=p(8u)+(data_word(D_cam_shutter_table_offset)+i)*4u;let t=x-f32(i);let first=heap_float(address);
    if(t==0.0){return first;}
    return (1.0-t)*first+t*heap_float(p(8u)+(data_word(D_cam_shutter_table_offset)+min(i+1u,SHUTTER_SIZE-1u))*4u);
}
// Preserve native float3 normalization at transparent/volume boundary corners.
// Captures retain the originating host's SSE division or scalar reciprocal rule.
fn camera_normalize(v:vec3<f32>)->vec3<f32>{
    if(CAMERA_NATIVE_VECTOR_DIVIDE!=0u){return rounded_normalize(v);}
    return v*rounded_divide(1.0,rounded_sqrt(rounded_dot(v,v)));
}
fn camera_projection(address:u32,v:vec4<f32>)->vec3<f32>{
    // Native float4 dot rounds paired sums before perspective division.
    // Fused projection multiply-add can make a ray cross a box corner twice.
    let w=motion_dot4(heap_vec4(address+48u),v);
    let c=vec3<f32>(motion_dot4(heap_vec4(address),v),motion_dot4(heap_vec4(address+16u),v),motion_dot4(heap_vec4(address+32u),v));
    if(w==0.0){return vec3<f32>(0);}
    if(CAMERA_NATIVE_VECTOR_DIVIDE!=0u){return vec3<f32>(rounded_divide(c.x,w),rounded_divide(c.y,w),rounded_divide(c.z,w));}
    return c*rounded_divide(1.0,w);
}
@compute @workgroup_size(64) fn init_camera(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}
    var path=0u;var x=0u;var y=0u;var sample_id=0u;var pixel=0u;var sample_offset=0u;
    let retry=p(2u)==0u;
    if(!retry){
        let tile_index=id.x/p(5u);if(tile_index>=p(3u)){return;}
        let t=p(2u)+tile_index*T_size;let tile_work=id.x%p(5u);
        let work_size=load_word(t+T_work_size);if(tile_work>=work_size){return;}
        let w=load_word(t+T_w);let h=load_word(t+T_h);let samples=load_word(t+T_num_samples);
        let begin=load_word(t+T_path_index_offset);
        if(w==0u||h==0u||samples==0u||w>p(10u)||h>p(10u)/w||samples>p(10u)/(w*h)||work_size!=w*h*samples||work_size>p(5u)||begin>=p(10u)||work_size>p(10u)-begin){fail();return;}
        path=begin+tile_work;
        var pixel_offset=tile_work/samples;var sample_in_tile=tile_work%samples;
        if(data_float(D_integrator_scrambling_distance)<0.9){sample_in_tile=tile_work/(w*h);pixel_offset=tile_work%(w*h);}
        x=load_word(t+T_x)+pixel_offset%w;y=load_word(t+T_y)+pixel_offset/w;
        let stride=load_word(t+T_stride);let offset=load_word(t+T_offset);
        // Signed tile offset intentionally wraps in the original uint index.
        // Validate the final index before accessing the render allocation.
        pixel=offset+x+y*stride;
        sample_id=load_word(t+T_start_sample)+sample_in_tile;sample_offset=load_word(t+T_sample_offset);
    }else{
        if(id.x>=p(13u)){return;}path=id.x;if(p(12u)!=0u){path=load_word(p(12u)+id.x*4u);}
        if(path>=p(10u)){fail();return;}
        pixel=load_word(state_address(S_path_render_pixel_index,path,4u));
        let width=u32(data_float(D_cam_width));if(width==0u){fail();return;}
        x=pixel%width;y=pixel/width;sample_id=load_word(state_address(S_path_sample,path,4u));
        if(load_half(state_address(S_path_queued_kernel,path,2u))!=K_CAMERA){fail();return;}
    }
    let stride=data_word(D_film_pass_stride);
    if(pixel>=p(11u)/stride){fail();return;}
    let film=p(4u)+pixel*stride*4u;
    if(!retry){
        state_u32(S_path_render_pixel_index,path,pixel);state_u16(S_path_queued_kernel,path,0u);
        let adaptive=data_word(D_film_pass_adaptive_aux_buffer);
        if(adaptive!=PASS_NONE&&heap_float(film+(adaptive+3u)*4u)!=0.0){return;}
        let count=data_word(D_film_pass_sample_count);
        if(count!=PASS_NONE){sample_id=atomicAdd(&heap[(film+count*4u)/4u],1u)+sample_offset;}
    }
    let rng=pixel_hash(x,y,data_word(D_integrator_seed));
    var uv=vec2<f32>(0.5);if(sample_id!=0u){uv=native_rng(sample_id,rng,RNG_FILTER).xy;}
    var raster=vec2<f32>(f32(x),f32(y));
    if(data_word(D_integrator_pixel_jitter)==0x7f7fffffu){raster+=vec2<f32>(filter_value(uv.x),filter_value(uv.y));}
    else{raster-=vec2<f32>(data_float(D_integrator_pixel_jitter),data_float(D_integrator_pixel_jitter+4u));}
    var time=0.5;
    if(data_float(D_cam_shuttertime)!=-1.0){
        time=camera_shutter(native_rng(sample_id,rng,RNG_LENS_TIME).x);
        if(data_word(D_cam_rolling_shutter_type)!=0u){
            let row_time=1.0-f32(y)/data_float(D_cam_height);let duration=data_float(D_cam_rolling_shutter_duration);
            if(duration!=0.0){time=(time-0.5)*duration;time+=(row_time-0.5)*(1.0-duration)+0.5;}
            else{time=row_time;}
        }
    }
    let rt=p(6u)+D_cam_rastertocamera;let v=vec4<f32>(raster,0.0,1.0);
    var pc=camera_projection(rt,v);
    if(data_word(D_cam_have_perspective_motion)!=0u){
        if(time<0.5){let before=camera_projection(p(6u)+D_cam_perspective_pre,v);pc=before+(pc-before)*(time*2.0);}
        else{let after=camera_projection(p(6u)+D_cam_perspective_post,v);pc=pc+(after-pc)*((time-0.5)*2.0);}
    }
    var world=motion_transform_load(p(6u)+D_cam_cameratoworld);
    let steps=data_word(D_cam_num_motion_steps);
    if(steps!=0u){
        // Removed by scene specialization when the camera has no motion steps (pipelines.h).
//#if MOTION_CAMERA else atomicOr(&heap[p(17u)/4u],2u);return;
        world=motion_interpolate(G_camera_motion,0u,steps,time);
//#endif
    }
    if(load_word(p(17u))!=0u){return;}
    let center=motion_direction(world,pc);let direction=camera_normalize(center);
    let dx=camera_normalize(center+heap_vec3(p(6u)+D_cam_dx))-direction;
    let dy=camera_normalize(center+heap_vec3(p(6u)+D_cam_dy))-direction;
    let dd=(0.5*(length(dx)+length(dy)))*data_float(D_cam_differential_scale);
    let zi=rounded_divide(1.0,camera_normalize(pc).z);let near=data_float(D_cam_nearclip)*zi;
    let origin=vec3<f32>(world.x.w,world.y.w,world.z.w)+near*direction;
    state_vec3(S_ray_P,path,origin);state_vec3(S_ray_D,path,direction);
    state_f32(S_ray_dP,path,near*dd);state_f32(S_ray_dD,path,dd);
    state_f32(S_ray_tmin,path,0.0);state_f32(S_ray_tmax,path,data_float(D_cam_cliplength)*zi);state_f32(S_ray_time,path,time);
    state_u32(S_path_sample,path,sample_id);state_u32(S_path_rng_pixel,path,rng);state_u16(S_path_rng_offset,path,RNG_BOUNCE);
    state_u16(S_path_bounce,path,0u);state_u16(S_path_diffuse_bounce,path,0u);state_u16(S_path_glossy_bounce,path,0u);
    state_u16(S_path_transmission_bounce,path,0u);state_u16(S_path_transparent_bounce,path,0u);
    state_u16(S_path_volume_bounce,path,0u);state_u16(S_path_volume_bounds_bounce,path,0u);
    state_u8(S_path_visibility,path,VIS_CAMERA);state_u8(S_path_mnee,path,0u);state_u32(S_path_flag,path,INITIAL_FLAG);
    state_f32(S_path_mis_ray_pdf,path,0.0);state_u32(S_path_min_ray_pdf,path,0x7f7fffffu);
    state_f32(S_path_continuation_probability,path,1.0);state_vec3(S_path_throughput,path,vec3<f32>(1.0));
    state_f32(S_path_optical_depth,path,0.0);state_u32(S_isect_object,path,NONE);state_u32(S_isect_prim,path,NONE);state_u32(S_isect_type,path,PRIM_TYPE_NONE);
    if(volume_enabled()){
        store_word(volume_stack_address(path,0u,VS_object),data_word(D_background_object_index));
        store_word(volume_stack_address(path,0u,VS_shader),data_word(D_background_volume_shader));
        store_word(volume_stack_address(path,1u,VS_object),NONE);store_word(volume_stack_address(path,1u,VS_shader),NONE);
    }
    let counter=load_word(p(7u)+S_queue_counter);
    if(retry){atomicSub(&heap[(counter+K_CAMERA*4u)/4u],1u);}
    let next=select(K_CLOSEST,DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK,data_word(D_cam_is_inside_volume)!=0u);
    atomicAdd(&heap[(counter+next*4u)/4u],1u);state_u16(S_path_queued_kernel,path,next);
}
