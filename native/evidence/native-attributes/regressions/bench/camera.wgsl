const D_cam_rastertocamera:u32=192u;
const D_cam_cameratoworld:u32=144u;
const D_cam_dx:u32=256u;
const D_cam_dy:u32=272u;
const D_cam_differential_scale:u32=288u;
const D_cam_nearclip:u32=292u;
const D_cam_cliplength:u32=296u;
const D_cam_width:u32=308u;
const D_integrator_seed:u32=1984u;
const D_integrator_tabulated_sobol_sequence_size:u32=2008u;
const D_integrator_scrambling_distance:u32=2004u;
const D_integrator_pixel_jitter:u32=2088u;
const D_tables_filter_table_offset:u32=1024u;
const D_film_pass_stride:u32=1604u;
const D_film_pass_sample_count:u32=1764u;
const D_film_pass_adaptive_aux_buffer:u32=1760u;
const D_bvh_root:u32=1424u;
const D_integrator_min_bounce:u32=1920u;
const D_integrator_transparent_min_bounce:u32=1960u;
const D_integrator_ao_bounces:u32=1944u;
const D_integrator_ao_bounces_distance:u32=1948u;
const D_integrator_filter_closures:u32=2036u;
const D_max_shaders:u32=8u;
const S_path_render_pixel_index:u32=0u;
const S_path_sample:u32=8u;
const S_path_bounce:u32=16u;
const S_path_transparent_bounce:u32=24u;
const S_path_diffuse_bounce:u32=32u;
const S_path_glossy_bounce:u32=40u;
const S_path_transmission_bounce:u32=48u;
const S_path_volume_bounce:u32=56u;
const S_path_volume_bounds_bounce:u32=64u;
const S_path_portal_bounce:u32=72u;
const S_path_queued_kernel:u32=80u;
const S_path_rng_pixel:u32=88u;
const S_path_rng_offset:u32=96u;
const S_path_visibility:u32=104u;
const S_path_flag:u32=112u;
const S_path_mnee:u32=120u;
const S_path_mnee_shadow_state:u32=128u;
const S_path_optical_depth:u32=136u;
const S_path_mis_ray_pdf:u32=144u;
const S_path_mis_ray_object:u32=152u;
const S_path_mis_origin_n:u32=160u;
const S_path_min_ray_pdf:u32=168u;
const S_path_continuation_probability:u32=176u;
const S_path_throughput:u32=184u;
const S_path_unguided_throughput:u32=192u;
const S_path_pass_diffuse_weight:u32=200u;
const S_path_pass_glossy_weight:u32=208u;
const S_path_denoising_feature_throughput:u32=216u;
const S_path_shader_sort_key:u32=224u;
const S_ray_P:u32=232u;
const S_ray_dP:u32=240u;
const S_ray_D:u32=248u;
const S_ray_dD:u32=256u;
const S_ray_tmin:u32=264u;
const S_ray_tmax:u32=272u;
const S_ray_time:u32=280u;
const S_ray_previous_dt:u32=288u;
const S_isect_t:u32=296u;
const S_isect_u:u32=304u;
const S_isect_v:u32=312u;
const S_isect_prim:u32=320u;
const S_isect_object:u32=328u;
const S_isect_type:u32=336u;
const S_subsurface_albedo:u32=344u;
const S_subsurface_radius:u32=352u;
const S_subsurface_anisotropy:u32=360u;
const S_subsurface_N:u32=368u;
const S_guiding_path_segment:u32=888u;
const S_guiding_use_surface_guiding:u32=896u;
const S_guiding_sample_surface_guiding_rand:u32=904u;
const S_guiding_surface_guiding_sampling_prob:u32=912u;
const S_guiding_bssrdf_sampling_prob:u32=920u;
const S_guiding_use_volume_guiding:u32=928u;
const S_guiding_sample_volume_guiding_rand:u32=936u;
const S_guiding_volume_guiding_sampling_prob:u32=944u;
const S_shadow_link_dedicated_light_weight:u32=952u;
const S_shadow_link_last_isect_prim:u32=960u;
const S_shadow_link_last_isect_object:u32=968u;
const S_queue_counter:u32=1944u;
const S_sort_partition_divisor:u32=2120u;
const S_sort_key_counter:u32=1952u;
const POINTER_SIZE:u32=8u;
const G_bvh_nodes:u32=0u;
const SIZE_bvh_nodes:u32=16u;
const G_bvh_leaf_nodes:u32=1u;
const SIZE_bvh_leaf_nodes:u32=16u;
const G_object_node:u32=2u;
const SIZE_object_node:u32=4u;
const G_prim_index:u32=3u;
const SIZE_prim_index:u32=4u;
const G_prim_object:u32=4u;
const SIZE_prim_object:u32=4u;
const G_prim_visibility:u32=5u;
const SIZE_prim_visibility:u32=4u;
const G_tri_vindex:u32=6u;
const SIZE_tri_vindex:u32=12u;
const G_tri_verts:u32=7u;
const SIZE_tri_verts:u32=12u;
const G_tri_shader:u32=8u;
const SIZE_tri_shader:u32=4u;
const G_objects:u32=9u;
const SIZE_objects:u32=256u;
const G_object_flag:u32=10u;
const SIZE_object_flag:u32=4u;
const G_shaders:u32=11u;
const SIZE_shaders:u32=32u;
const G_attributes_normal:u32=12u;
const SIZE_attributes_normal:u32=4u;
const G_attributes_map:u32=13u;
const SIZE_attributes_map:u32=16u;
const G_attributes_float:u32=14u;
const SIZE_attributes_float:u32=4u;
const G_attributes_float2:u32=15u;
const SIZE_attributes_float2:u32=8u;
const G_attributes_float3:u32=16u;
const SIZE_attributes_float3:u32=12u;
const G_attributes_float4:u32=17u;
const SIZE_attributes_float4:u32=16u;
const G_attributes_uchar4:u32=18u;
const SIZE_attributes_uchar4:u32=4u;
const O_itfm:u32=48u;
const O_position_offset:u32=168u;
const O_ao_distance:u32=192u;
const SH_flags:u32=16u;
const PRIMITIVE_TRIANGLE:u32=1u;
const PRIMITIVE_ALL:u32=127u;
const SHADER_MASK:u32=541065215u;
const SD_HAS_EMISSION:u32=536870912u;
const SD_HAS_TRANSPARENT_SHADOW:u32=131072u;
const SD_HAS_RAYTRACE:u32=1073741824u;
const PATH_RAY_TRANSPARENT:u32=4u;
const PATH_RAY_SHADOW_CATCHER_PASS:u32=4194304u;
const PATH_RAY_TERMINATE_AFTER_TRANSPARENT:u32=4096u;
const PATH_RAY_TERMINATE_ON_NEXT_SURFACE:u32=1024u;
const PATH_RAY_VISIBILITY_TRANSMIT:u32=2u;
const PATH_RAY_VISIBILITY_DIFFUSE:u32=4u;
const PATH_RAY_VISIBILITY_GLOSSY:u32=8u;
const PATH_RAY_VISIBILITY_SHADOW_OPAQUE:u32=32u;
const FILTER_CLOSURE_DIRECT_LIGHT:u32=32u;
const PRNG_TERMINATE:u32=0u;
const DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND:u32=8u;
const DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE:u32=11u;
const DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE:u32=12u;
const T_x:u32=0u;
const T_y:u32=4u;
const T_w:u32=8u;
const T_h:u32=12u;
const T_start_sample:u32=16u;
const T_num_samples:u32=20u;
const T_sample_offset:u32=24u;
const T_offset:u32=28u;
const T_stride:u32=32u;
const T_path_index_offset:u32=36u;
const T_work_size:u32=40u;
const T_size:u32=44u;
const FILTER_SIZE:u32=1024u;
const SOBOL_PATTERNS:u32=256u;
const RNG_FILTER:u32=0u;
const RNG_BOUNCE:u32=16u;
const VIS_CAMERA:u32=1u;
const INITIAL_FLAG:u32=640u;
const K_CAMERA:u32=0u;
const K_CLOSEST:u32=2u;
const NONE:u32=4294967295u;
const PRIM_TYPE_NONE:u32=0u;
const PASS_NONE:u32=4294967295u;
// Copyright (c) 2026 Jeremy Ellis and contributors
// Derived source_index Blender Cycles kernel/device/gpu/kernel.h, parallel_prefix_sum.h,
// parallel_active_index.h, parallel_sorted_index.h, integrator/state_util.h.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2021-2022 Blender Foundation
// SPDX-License-Identifier: Apache-2.0
// Changes: virtual byte addresses, portable atomic index reservation, and
// byte/halfword CAS writes preserve the original native SoA layout in WGSL.
@group(0) @binding(0) var<storage,read_write> heap: array<atomic<u32>>;
struct Parameters { words: array<vec4<u32>,8> }
@group(0) @binding(1) var<uniform> params: Parameters;
fn p(i:u32)->u32{return params.words[i/4u][i%4u];}
fn load_word(address:u32)->u32{return atomicLoad(&heap[address/4u]);}
fn store_word(address:u32,value:u32){atomicStore(&heap[address/4u],value);}
fn load_half(address:u32)->u32{return (load_word(address)>>(8u*(address%4u)))&65535u;}
fn masked(address:u32,value:u32,bits:u32){
    let shift=8u*(address%4u);let mask=((1u<<bits)-1u)<<shift;
    var old=load_word(address);
    loop { let result=atomicCompareExchangeWeak(&heap[address/4u],old,(old&~mask)|((value<<shift)&mask));
        if(result.exchanged){break;} old=result.old_value; }
}
fn fail(){atomicOr(&heap[p(17u)/4u],1u);}
@compute @workgroup_size(64) fn control(@builtin(global_invocation_id) id:vec3<u32>){
    let i=id.x;let op=p(0u);let n=p(1u);
    if(op==1u){
        if(i!=0u){return;}var offset=0u;
        for(var k=0u;k<n;k++){let value=load_word(p(8u)+k*4u);store_word(p(9u)+k*4u,offset);store_word(p(8u)+k*4u,0u);offset+=value;}
        return;
    }
    if(i>=n){return;}
    if(op==0u){masked(p(2u)+i*2u,0u,16u);masked(p(3u)+i*2u,0u,16u);return;}
    if(op==2u){
        let queued=load_half(p(2u)+i*2u);let mode=p(19u);
        let selected=select(select(select(queued==p(6u),queued!=0u,mode==1u),queued==0u,mode==2u),i>=p(6u)&&queued!=0u,mode==3u);
        if(selected){let index=atomicAdd(&heap[p(5u)/4u],1u);if(index>=n){fail();return;}store_word(p(4u)+(index+p(7u))*4u,i);}
        return;
    }
    if(op==3u){
        if(load_half(p(2u)+i*2u)!=p(6u)){return;}
        let key=load_word(p(10u)+i*4u);if(key>=p(20u)){fail();return;}
        let index=atomicAdd(&heap[(p(9u)/4u)+key],1u);
        if(index<p(11u)){if(index>=n){fail();return;}store_word(p(4u)+index*4u,i);}
        else{atomicAdd(&heap[(p(8u)/4u)+key],1u);}
        return;
    }
    if(op==4u){
        let source_index=load_word(p(4u)+(p(12u)+i)*4u);let destination_index=load_word(p(4u)+(p(13u)+i)*4u);
        if(source_index>=p(18u)||destination_index>=p(18u)||source_index==destination_index){fail();return;}
        // Upstream compaction moves selected states above the selected-count boundary
        // into distinct terminated slots below it; source and destination sets
        // are disjoint. Scalar byte writes still share words and require CAS.
        for(var f=0u;f<p(15u);f++){
            let descriptor=p(14u)+f*16u;
            if(load_word(descriptor+8u)!=p(16u)){continue;}
            let address=load_word(descriptor);let stride=load_word(descriptor+4u);let count=load_word(descriptor+12u);
            if(source_index>=count||destination_index>=count){fail();continue;}
            let source=address+source_index*stride;let destination=address+destination_index*stride;
            if(stride==1u){masked(destination,(load_word(source)>>(8u*(source%4u)))&255u,8u);}
            else if(stride==2u){masked(destination,load_half(source),16u);}
            else{for(var b=0u;b<stride;b+=4u){store_word(destination+b,load_word(source+b));}}
        }
        masked(p(2u)+source_index*2u,0u,16u);
    }
}
fn wang_hash(value: u32, seed: u32) -> u32 {
    var i=(value^61u)^seed; i+=i<<3u; i^=i>>4u; i*=0x27d4eb2du; return i;
}
fn pixel_hash(x: u32, y: u32, seed: u32) -> u32 {
    let qx=1103515245u*((x>>1u)^y); let qy=1103515245u*((y>>1u)^x);
    return (1103515245u*(qx^(qy>>3u)))^seed;
}
fn owen_scramble(value: u32, seed: u32) -> u32 {
    var n=reverseBits(value); n^=n*0x3d20adeau; n+=seed;
    n*=(seed>>16u)|1u; n^=n*0x05526c56u; n^=n*0x53a22864u; return reverseBits(n);
}
fn shuffle_pattern(value: u32, length: u32, seed: u32) -> u32 {
    var i=value%length;
    let mask=(1u<<(32u-countLeadingZeros(length-1u)))-1u;
    loop {
        i^=seed; i*=0xe170893du; i^=seed>>16u; i^=(i&mask)>>4u;
        i^=seed>>8u; i*=0x0929eb3fu; i^=seed>>23u; i^=(i&mask)>>1u;
        i*=1u|(seed>>27u); i*=0x6935fa69u; i^=(i&mask)>>11u;
        i*=0x74dcb303u; i^=(i&mask)>>2u; i*=0x9e501cc3u;
        i^=(i&mask)>>2u; i*=0xc860a3dfu; i&=mask; i^=i>>5u;
        if(i<length) { break; }
    }
    return i;
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native Cycles state/table access and tabulated Sobol sampling.
fn data_word(offset:u32)->u32{return load_word(p(6u)+offset);}
fn data_float(offset:u32)->f32{return bitcast<f32>(data_word(offset));}
fn heap_float(address:u32)->f32{return bitcast<f32>(load_word(address));}
fn heap_vec3(address:u32)->vec3<f32>{return vec3<f32>(heap_float(address),heap_float(address+4u),heap_float(address+8u));}
fn heap_vec4(address:u32)->vec4<f32>{return vec4<f32>(heap_vec3(address),heap_float(address+12u));}
fn state_address(offset:u32,index:u32,stride:u32)->u32{return load_word(p(7u)+offset)+index*stride;}
fn state_u32(offset:u32,index:u32,value:u32){store_word(state_address(offset,index,4u),value);}
fn state_f32(offset:u32,index:u32,value:f32){state_u32(offset,index,bitcast<u32>(value));}
fn state_u16(offset:u32,index:u32,value:u32){masked(state_address(offset,index,2u),value,16u);}
fn state_u8(offset:u32,index:u32,value:u32){masked(state_address(offset,index,1u),value,8u);}
fn state_vec3(offset:u32,index:u32,value:vec3<f32>){let a=state_address(offset,index,12u);store_word(a,bitcast<u32>(value.x));store_word(a+4u,bitcast<u32>(value.y));store_word(a+8u,bitcast<u32>(value.z));}
fn native_rng(sample_id:u32,pixel:u32,dimension:u32)->vec4<f32>{
    let count=data_word(D_integrator_tabulated_sobol_sequence_size);
    let distance=data_float(D_integrator_scrambling_distance);
    let seed=select(pixel,data_word(D_integrator_seed),distance<1.0);
    let pattern=shuffle_pattern(dimension,SOBOL_PATTERNS,seed);
    let mask=count-1u;let shuffled=owen_scramble(sample_id,wang_hash(dimension,seed));
    let sample_value=(sample_id&~mask)|(shuffled&mask);
    let index=(pattern*count+sample_value)%(count*SOBOL_PATTERNS);
    var value=heap_vec4(p(9u)+index*16u);
    if(distance<1.0){
        let hashes=vec4<u32>(wang_hash(dimension,pixel),wang_hash(dimension,pixel^0xca0e1151u),wang_hash(dimension,pixel^0xbf604c5au),wang_hash(dimension,pixel^0x99634d1du));
        value+=(vec4<f32>(hashes)*(1.0/4294967808.0))*distance;value-=floor(value);
    }
    return value;
}

fn table_count(table:u32)->u32{return load_word(p(14u)+table*8u+4u);}
fn table_at(table:u32,item:u32,stride:u32)->u32{
    if(item>=table_count(table)){fail();return 0u;}
    return load_word(p(14u)+table*8u)+item*stride;
}
fn state_word(field:u32,index:u32)->u32{return load_word(state_address(field,index,4u));}
fn state_half(field:u32,index:u32)->u32{return load_half(state_address(field,index,2u));}
fn state_byte(field:u32,index:u32)->u32{let a=state_address(field,index,1u);return (load_word(a)>>(8u*(a%4u)))&255u;}
fn path_ao(index:u32)->bool{
    let limit=data_word(D_integrator_ao_bounces);if(limit==0u){return false;}
    let bounce=i32(state_half(S_path_bounce,index))-i32(state_half(S_path_transmission_bounce,index))-select(0i,1i,state_half(S_path_glossy_bounce,index)>0u)+1;
    return bounce>i32(limit);
}
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
fn camera_direction(v:vec3<f32>)->vec3<f32>{
    let a=p(6u)+D_cam_cameratoworld;let tx=heap_vec4(a);let ty=heap_vec4(a+16u);let tz=heap_vec4(a+32u);
    // Original SIMD transform accumulates z, y, then x.
    return vec3<f32>(tx.x,ty.x,tz.x)*v.x+(vec3<f32>(tx.y,ty.y,tz.y)*v.y+vec3<f32>(tx.z,ty.z,tz.z)*v.z);
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
    let rt=p(6u)+D_cam_rastertocamera;let v=vec4<f32>(raster,0.0,1.0);
    let pc=vec3<f32>(dot(heap_vec4(rt),v),dot(heap_vec4(rt+16u),v),dot(heap_vec4(rt+32u),v))/dot(heap_vec4(rt+48u),v);
    let center=camera_direction(pc);let direction=normalize(center);
    let dx=normalize(center+heap_vec3(p(6u)+D_cam_dx))-direction;
    let dy=normalize(center+heap_vec3(p(6u)+D_cam_dy))-direction;
    let dd=(0.5*(length(dx)+length(dy)))*data_float(D_cam_differential_scale);
    let zi=1.0/normalize(pc).z;let near=data_float(D_cam_nearclip)*zi;
    let world=p(6u)+D_cam_cameratoworld;
    let origin=vec3<f32>(heap_float(world+12u),heap_float(world+28u),heap_float(world+44u))+near*direction;
    state_vec3(S_ray_P,path,origin);state_vec3(S_ray_D,path,direction);
    state_f32(S_ray_dP,path,near*dd);state_f32(S_ray_dD,path,dd);
    state_f32(S_ray_tmin,path,0.0);state_f32(S_ray_tmax,path,data_float(D_cam_cliplength)*zi);state_f32(S_ray_time,path,0.5);
    state_u32(S_path_sample,path,sample_id);state_u32(S_path_rng_pixel,path,rng);state_u16(S_path_rng_offset,path,RNG_BOUNCE);
    state_u16(S_path_bounce,path,0u);state_u16(S_path_diffuse_bounce,path,0u);state_u16(S_path_glossy_bounce,path,0u);
    state_u16(S_path_transmission_bounce,path,0u);state_u16(S_path_transparent_bounce,path,0u);
    state_u16(S_path_volume_bounce,path,0u);state_u16(S_path_volume_bounds_bounce,path,0u);
    state_u8(S_path_visibility,path,VIS_CAMERA);state_u8(S_path_mnee,path,0u);state_u32(S_path_flag,path,INITIAL_FLAG);
    state_f32(S_path_mis_ray_pdf,path,0.0);state_u32(S_path_min_ray_pdf,path,0x7f7fffffu);
    state_f32(S_path_continuation_probability,path,1.0);state_vec3(S_path_throughput,path,vec3<f32>(1.0));
    state_f32(S_path_optical_depth,path,0.0);state_u32(S_isect_object,path,NONE);state_u32(S_isect_prim,path,NONE);state_u32(S_isect_type,path,PRIM_TYPE_NONE);
    let counter=load_word(p(7u)+S_queue_counter);
    if(retry){atomicSub(&heap[(counter+K_CAMERA*4u)/4u],1u);}
    atomicAdd(&heap[(counter+K_CLOSEST*4u)/4u],1u);state_u16(S_path_queued_kernel,path,K_CLOSEST);
}
