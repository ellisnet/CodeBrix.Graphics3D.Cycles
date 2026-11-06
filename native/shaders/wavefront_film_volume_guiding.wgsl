// SPDX-FileCopyrightText: 2025 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Volume scattering probability guiding buffers (pinned kernel/device/gpu/kernel.h volume_guiding_filter_x and
// volume_guiding_filter_y; film/volume_guiding_denoise.h; util/types_rgbe.h rgb_to_rgbe and rgbe_to_rgb).
// Prefixed with the controls module header and wavefront_float.wgsl.
// p(1) work items, p(4) render buffer, p(5) pass stride (floats), p(6) sx, p(7) sy, p(8) sw, p(9) sh,
// p(10) buffer offset (pixels, signed), p(12) buffer stride (pixels), p(16) pass_volume_scatter,
// p(18) pass_volume_transmit, p(19) pass_sample_count, p(20) pass_volume_scatter_denoised,
// p(21) pass_volume_transmit_denoised.
// Every product and sum is a binary32 step in the pinned order: rounded_add is integer arithmetic and the
// products feed it, so no driver contraction can fuse them.
fn vg_pixel(x:i32,y:i32)->u32{
    // render_buffer + (offset + x + y * stride) * pass_stride, the index in the pinned int arithmetic.
    return p(4u)+u32(i32(p(10u))+x+y*i32(p(12u)))*p(5u)*4u;
}
fn vg_float3(address:u32)->vec3<f32>{return vec3<f32>(bitcast<f32>(load_word(address)),bitcast<f32>(load_word(address+4u)),bitcast<f32>(load_word(address+8u)));}
fn vg_add3(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{return vec3<f32>(rounded_add(a.x,b.x),rounded_add(a.y,b.y),rounded_add(a.z,b.z));}
// roundf: halfway cases away from zero (the argument is not negative here).
fn vg_roundf(x:f32)->f32{let f=floor(x);return select(f,f+1.0,x-f>=0.5);}
// rgbe_to_rgb (kernel_read_pass_rgbe).
fn vg_rgbe_read(address:u32)->vec3<f32>{
    // rgbe.f == 0: either zero (integer test, so a subnormal word is never flushed to zero).
    let bits=load_word(address);if((bits&0x7fffffffu)==0u){return vec3<f32>(0);}
    let e=(bits>>24u)&31u;let f=bitcast<f32>((e+104u)<<23u);
    var v=vec3<f32>(f32(bits&255u),f32((bits>>8u)&255u),f32((bits>>16u)&255u))*f;
    v=bitcast<vec3<f32>>(bitcast<vec3<u32>>(v)|vec3<u32>(((bits>>24u)&0x80u)<<24u,((bits>>24u)&0x40u)<<25u,((bits>>24u)&0x20u)<<26u));
    return v;
}
// rgb_to_rgbe (film_overwrite_pass_rgbe): the channels round half to even as the SSE and NEON round(float3)
// (_MM_FROUND_NINT, vrndnq_f32), the scalar overflow test half away from zero (roundf).
fn vg_rgbe_write(address:u32,rgb:vec3<f32>){
    let max_v=min(max(abs(rgb.x),max(abs(rgb.y),abs(rgb.z))),65280.0);
    if(max_v<bitcast<f32>(0x33800000u)){store_word(address,0u);return;}
    var e=max(-16,i32(bitcast<u32>(max_v)>>23u)-127)+1;
    var v=bitcast<f32>(u32(127+8-e)<<23u);
    if(i32(vg_roundf(max_v*v))==256){e+=1;v*=0.5;}
    let sign=((bitcast<u32>(rgb.x)>>31u)<<7u)|((bitcast<u32>(rgb.y)>>31u)<<6u)|((bitcast<u32>(rgb.z)>>31u)<<5u);
    let c=min(round(abs(rgb)*v),vec3<f32>(255.0));
    store_word(address,u32(c.x)|(u32(c.y)<<8u)|(u32(c.z)<<16u)|((((u32(e+15))&0x1fu)|sign)<<24u));
}
// Gaussian, sigma 1.5, radius 5 (the pinned binary32 constants).
const VG_GAUSSIAN=array<f32,11>(0.0012273699895602,0.0084674212370284,0.0379843612914121,0.1108921888487800,0.2108379677336155,
    0.2611813817992076,0.2108379677336155,0.1108921888487800,0.0379843612914121,0.0084674212370284,0.0012273699895602);
@compute @workgroup_size(64) fn volume_guiding_filter_x(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)||load_word(p(17u))!=0u){return;}
    let sw=i32(p(8u));let sh=i32(p(9u));let work=i32(id.x);let yy=work/sw;let xx=work%sw;
    if(yy>=sh){return;}
    let min_x=i32(p(6u));let max_x=min_x+sw;let y=i32(p(7u))+yy;let center_x=min_x+xx;
    var scatter=vec3<f32>(0);var transmit=vec3<f32>(0);
    for(var dx=0;dx<11;dx++){
        let x=center_x+dx-5;if(x<min_x||x>=max_x){continue;}
        let buffer=vg_pixel(x,y);
        let weight=rounded_divide(VG_GAUSSIAN[dx],f32(load_word(buffer+p(19u)*4u)));
        scatter=vg_add3(scatter,vg_float3(buffer+p(16u)*4u)*weight);
        transmit=vg_add3(transmit,vg_float3(buffer+p(18u)*4u)*weight);
    }
    let center=vg_pixel(center_x,y);
    vg_rgbe_write(center+p(20u)*4u,scatter);vg_rgbe_write(center+p(21u)*4u,transmit);
}
@compute @workgroup_size(64) fn volume_guiding_filter_y(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)||load_word(p(17u))!=0u){return;}
    if(id.x>=p(8u)){return;}
    let x=i32(p(6u))+i32(id.x);let min_y=i32(p(7u));let max_y=min_y+i32(p(9u));
    var scatter_n:array<vec3<f32>,11>;var transmit_n:array<vec3<f32>,11>;
    for(var i=0;i<11;i++){
        let y=min_y+i;
        if(i>=5||y<min_y||y>=max_y){scatter_n[i]=vec3<f32>(0);transmit_n[i]=vec3<f32>(0);}
        else{let buffer=vg_pixel(x,y);scatter_n[i]=vg_rgbe_read(buffer+p(20u)*4u);transmit_n[i]=vg_rgbe_read(buffer+p(21u)*4u);}
    }
    var index=5;
    for(var y=min_y;y<max_y;y++){
        let next_y=y+5;
        if(next_y<min_y||next_y>=max_y){scatter_n[index]=vec3<f32>(0);transmit_n[index]=vec3<f32>(0);}
        else{let buffer=vg_pixel(x,next_y);scatter_n[index]=vg_rgbe_read(buffer+p(20u)*4u);transmit_n[index]=vg_rgbe_read(buffer+p(21u)*4u);}
        index=(index+1)%11;
        var scatter=vec3<f32>(0);var transmit=vec3<f32>(0);
        for(var i=0;i<11;i++){
            scatter=vg_add3(scatter,VG_GAUSSIAN[i]*scatter_n[(index+i)%11]);
            transmit=vg_add3(transmit,VG_GAUSSIAN[i]*transmit_n[(index+i)%11]);
        }
        let buffer=vg_pixel(x,y);
        vg_rgbe_write(buffer+p(20u)*4u,abs(scatter));vg_rgbe_write(buffer+p(21u)*4u,abs(transmit));
    }
}
