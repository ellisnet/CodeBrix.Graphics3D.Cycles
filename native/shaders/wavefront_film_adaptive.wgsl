// SPDX-FileCopyrightText: 2019-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Adaptive sampling (pinned kernel/device/gpu/kernel.h adaptive_sampling_convergence_check,
// adaptive_sampling_filter_x and adaptive_sampling_filter_y; film/adaptive_sampling.h
// film_adaptive_sampling_convergence_check, film_adaptive_sampling_filter_x/_y).
// Prefixed with the controls module header and wavefront_float.wgsl.
// p(1) work items, p(4) render buffer, p(5) pass stride (floats), p(6) sx, p(7) sy,
// p(8) sw, p(9) sh, p(10) buffer offset (pixels, signed), p(12) buffer stride (pixels),
// p(13) threshold (binary32 bits), p(14) reset, p(15) num_active_pixels address,
// p(16) pass_adaptive_aux_buffer, p(18) pass_combined, p(19) pass_sample_count,
// p(20) film exposure (binary32 bits).
fn adaptive_pixel(x:u32,y:u32)->u32{
    // render_buffer + (offset + x + y * stride) * pass_stride, the index in the pinned int arithmetic.
    return p(4u)+u32(i32(p(10u))+i32(x)+i32(y)*i32(p(12u)))*p(5u)*4u;
}
fn adaptive_float(address:u32)->f32{return bitcast<f32>(load_word(address));}
fn adaptive_check(x:u32,y:u32)->bool{
    let buffer=adaptive_pixel(x,y);let aux=buffer+p(16u)*4u;
    let A=vec4<f32>(adaptive_float(aux),adaptive_float(aux+4u),adaptive_float(aux+8u),adaptive_float(aux+12u));
    if(p(14u)==0u&&A.w!=0.0){return true;}
    let combined=buffer+p(18u)*4u;
    let I=vec3<f32>(adaptive_float(combined),adaptive_float(combined+4u),adaptive_float(combined+8u));
    // The sample count pass stores an integer (__float_as_uint); the quotient and every sum
    // are correctly rounded binary32 steps in the pinned order (integer adds: no contraction).
    let sample=f32(load_word(buffer+p(19u)*4u));
    let scale=rounded_divide(bitcast<f32>(p(20u)),sample);
    let difference=rounded_add(rounded_add(abs(rounded_add(I.x,-A.x)),abs(rounded_add(I.y,-A.y))),abs(rounded_add(I.z,-A.z)))*scale;
    let intensity=rounded_add(rounded_add(I.x,I.y),I.z)*scale;
    var normalize=intensity;if(intensity<1.0){normalize=rounded_sqrt(intensity);}
    let error=rounded_divide(difference,rounded_add(0.0001,normalize));
    let converged=error<bitcast<f32>(p(13u));
    store_word(aux+12u,bitcast<u32>(select(0.0,1.0,converged)));
    return converged;
}
@compute @workgroup_size(64) fn adaptive_convergence_check(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)||load_word(p(17u))!=0u){return;}
    let sw=p(8u);let y=id.x/sw;let x=id.x-y*sw;
    var converged=true;
    if(x<sw&&y<p(9u)){converged=adaptive_check(p(6u)+x,p(7u)+y);}
    // The pinned kernel adds the warp's ballot count; one add per unconverged pixel gives the same total.
    if(!converged){atomicAdd(&heap[p(15u)/4u],1u);}
}
// The box filter: a row (filter_x) or a column (filter_y) per invocation, as the pinned loops.
fn adaptive_filter(line:u32,horizontal:bool){
    var prev=false;let start=select(p(7u),p(6u),horizontal);let length=select(p(9u),p(8u),horizontal);
    for(var i=start;i<start+length;i++){
        var x=line;var y=i;if(horizontal){x=i;y=line;}
        let w=adaptive_pixel(x,y)+(p(16u)+3u)*4u;
        if(adaptive_float(w)==0.0){
            if(i>start&&!prev){
                var px=x;var py=y;if(horizontal){px=x-1u;}else{py=y-1u;}
                store_word(adaptive_pixel(px,py)+(p(16u)+3u)*4u,0u);
            }
            prev=true;
        }else{
            if(prev){store_word(w,0u);}
            prev=false;
        }
    }
}
@compute @workgroup_size(64) fn adaptive_filter_x(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)||id.x>=p(9u)||load_word(p(17u))!=0u){return;}
    adaptive_filter(p(7u)+id.x,true);
}
@compute @workgroup_size(64) fn adaptive_filter_y(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)||id.x>=p(8u)||load_word(p(17u))!=0u){return;}
    adaptive_filter(p(6u)+id.x,false);
}
