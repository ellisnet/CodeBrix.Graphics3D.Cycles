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
const G_image_textures:u32=19u;
const SIZE_image_textures:u32=112u;
const G_image_texture_udims:u32=20u;
const SIZE_image_texture_udims:u32=8u;
const G_image_info:u32=21u;
const SIZE_image_info:u32=40u;
const G_image_bounds:u32=22u;
const SIZE_image_bounds:u32=8u;
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
const O_tfm:u32=0u;
const O_normal_offset:u32=172u;
const SD_OBJECT_TRANSFORM_APPLIED:u32=4u;
const SD_OBJECT_NEGATIVE_SCALE:u32=8u;
const SD_OBJECT_HAS_CORNER_NORMALS:u32=4096u;
const SD_OBJECT_MOTION:u32=2u;
const SD_OBJECT_HAS_VERTEX_MOTION:u32=64u;
const SHADER_SMOOTH_NORMAL:u32=2147483648u;
const SR_BACKFACING:u32=1u;
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
// SPDX-FileCopyrightText: 2011-2026 Blender Foundation
// SPDX-FileCopyrightText: 2011-2013 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native triangle ShaderData setup, packed normals and compact differentials.
// See native/THIRD-PARTY-NOTICES.txt for source files and transformations.
struct SurfaceData {
    P:vec3<f32>, N:vec3<f32>, Ng:vec3<f32>, wi:vec3<f32>,
    dPdu:vec3<f32>, dPdv:vec3<f32>, du:vec2<f32>, dv:vec2<f32>,
    u:f32, v:f32, time:f32, ray_length:f32, dP:f32, dI:f32,
    object_id:u32, prim:u32, primitive_type:u32, shader:u32,
    runtime_flag:u32, shader_flag:u32, object_flag:u32, error:u32,
}
fn surface_failure(sd:ptr<function,SurfaceData>){(*sd).error=1u;fail();}
fn surface_normalize(v:vec3<f32>)->vec3<f32>{let d=sqrt(dot(v,v));if(d!=0.0){return v*(1.0/d);}return v;}
fn surface_orthogonal(n:vec3<f32>)->vec3<f32>{
    if(n.x!=n.y||n.x!=n.z){return surface_normalize(vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x));}
    return surface_normalize(vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x));
}
fn surface_transform_direction(address:u32,v:vec3<f32>)->vec3<f32>{
    let x=heap_vec4(address);let y=heap_vec4(address+16u);let z=heap_vec4(address+32u);
    return v.x*vec3<f32>(x.x,y.x,z.x)+(v.y*vec3<f32>(x.y,y.y,z.y)+v.z*vec3<f32>(x.z,y.z,z.z));
}
fn surface_transform_point(address:u32,v:vec3<f32>)->vec3<f32>{
    return surface_transform_direction(address,v)+vec3<f32>(heap_float(address+12u),heap_float(address+28u),heap_float(address+44u));
}
fn surface_transform_normal(address:u32,v:vec3<f32>)->vec3<f32>{
    let x=heap_vec3(address);let y=heap_vec3(address+16u);let z=heap_vec3(address+32u);
    return surface_normalize(v.x*x+v.y*y+v.z*z);
}
fn surface_decode_normal(value:u32)->vec3<f32>{
    let xy=vec2<f32>(f32(value&65535u),f32(value>>16u))*(2.0/65535.0)-vec2<f32>(1);
    let z=1.0-abs(xy.x)-abs(xy.y);let t=max(-z,0.0);
    // The packed grid has no exact zero coordinate; sign comparison matches
    // both upstream scalar copysign and the CPU SIMD decoder.
    return surface_normalize(vec3<f32>(xy+select(vec2<f32>(t),vec2<f32>(-t),xy>vec2<f32>(0)),z));
}
fn surface_differentials(sd:ptr<function,SurfaceData>){
    let n=(*sd).Ng;let tangent=surface_orthogonal(n);
    var dx=tangent*(*sd).dP;var dy=cross(n,tangent)*(*sd).dP;
    var a=(*sd).dPdu;var b=(*sd).dPdv;let axis=abs(n);
    if(axis.z<axis.x||axis.z<axis.y){
        if(axis.y<axis.x||axis.y<axis.z){a.x=a.y;b.x=b.y;dx.x=dx.y;dy.x=dy.y;}
        a.y=a.z;b.y=b.z;dx.y=dx.z;dy.y=dy.z;
    }
    var det=a.x*b.y-b.x*a.y;if(det!=0.0){det=1.0/det;}
    (*sd).du=vec2<f32>((dx.x*b.y-dx.y*b.x)*det,(dy.x*b.y-dy.y*b.x)*det);
    (*sd).dv=vec2<f32>((dx.y*a.x-dx.x*a.y)*det,(dy.y*a.x-dy.x*a.y)*det);
}
fn surface_from_ray(index:u32)->SurfaceData{
    var sd:SurfaceData;
    if(index>=p(10u)){surface_failure(&sd);return sd;}
    sd.primitive_type=state_word(S_isect_type,index);sd.object_id=state_word(S_isect_object,index);sd.prim=state_word(S_isect_prim,index);
    if(sd.primitive_type!=PRIMITIVE_TRIANGLE||sd.object_id>=table_count(G_objects)||sd.object_id>=table_count(G_object_flag)||
       sd.prim>=table_count(G_tri_vindex)||sd.prim>=table_count(G_tri_shader)){surface_failure(&sd);return sd;}
    let object=table_at(G_objects,sd.object_id,SIZE_objects);
    sd.object_flag=load_word(table_at(G_object_flag,sd.object_id,4u));
    if((sd.object_flag&(SD_OBJECT_MOTION|SD_OBJECT_HAS_VERTEX_MOTION))!=0u){surface_failure(&sd);return sd;}
    sd.u=heap_float(state_address(S_isect_u,index,4u));sd.v=heap_float(state_address(S_isect_v,index,4u));
    sd.ray_length=heap_float(state_address(S_isect_t,index,4u));sd.time=heap_float(state_address(S_ray_time,index,4u));
    sd.wi=-heap_vec3(state_address(S_ray_D,index,12u));
    let vi_address=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);
    let vi=vec3<u32>(load_word(vi_address),load_word(vi_address+4u),load_word(vi_address+8u));
    let start=bitcast<i32>(load_word(object+O_position_offset));let count=table_count(G_tri_verts);
    let positions=bitcast<vec3<i32>>(vi)+vec3<i32>(start);
    if(any(vi>vec3<u32>(0x7fffffffu))||any(positions<vec3<i32>(0))||any(vec3<u32>(positions)>=vec3<u32>(count))){surface_failure(&sd);return sd;}
    let a=heap_vec3(table_at(G_tri_verts,u32(positions.x),SIZE_tri_verts));
    let b=heap_vec3(table_at(G_tri_verts,u32(positions.y),SIZE_tri_verts));
    let c=heap_vec3(table_at(G_tri_verts,u32(positions.z),SIZE_tri_verts));
    sd.dPdu=b-a;sd.dPdv=c-a;sd.P=(a+sd.u*sd.dPdu)+sd.v*sd.dPdv;
    let applied=(sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
    if(!applied){sd.P=surface_transform_point(object+O_tfm,sd.P);}
    var cross_normal=cross(sd.dPdu,sd.dPdv);
    if(applied&&(sd.object_flag&SD_OBJECT_NEGATIVE_SCALE)!=0u){cross_normal=cross(sd.dPdv,sd.dPdu);}
    if(dot(cross_normal,cross_normal)==0.0){surface_failure(&sd);return sd;}
    sd.Ng=surface_normalize(cross_normal);sd.N=sd.Ng;
    sd.shader=load_word(table_at(G_tri_shader,sd.prim,4u));let shader_id=sd.shader&SHADER_MASK;
    if(shader_id>=table_count(G_shaders)){surface_failure(&sd);return sd;}
    if((sd.shader&SHADER_SMOOTH_NORMAL)!=0u){
        var ni=vi;
        if((sd.object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0u){ni=vec3<u32>(sd.prim*3u)+vec3<u32>(0,1,2);}
        // Native offsets may be negative: global triangle/vertex indices cancel
        // that offset. Check signed addition before turning it into an address.
        let offset=bitcast<i32>(load_word(object+O_normal_offset));
        let nc=table_count(G_attributes_normal);var normals:array<vec3<f32>,3>;
        for(var j=0u;j<3u;j++){
            let at=bitcast<i32>(ni[j])+offset;
            if(at<0||u32(at)>=nc){surface_failure(&sd);return sd;}
            normals[j]=surface_decode_normal(load_word(table_at(G_attributes_normal,u32(at),SIZE_attributes_normal)));
        }
        let interpolated=surface_normalize(((1.0-sd.u)-sd.v)*normals[0]+sd.u*normals[1]+sd.v*normals[2]);
        if(any(interpolated!=vec3<f32>(0))){sd.N=interpolated;}
    }
    if(!applied){
        sd.N=surface_transform_normal(object+O_itfm,sd.N);sd.Ng=surface_transform_normal(object+O_itfm,sd.Ng);
        sd.dPdu=surface_transform_direction(object+O_tfm,sd.dPdu);sd.dPdv=surface_transform_direction(object+O_tfm,sd.dPdv);
    }
    sd.shader_flag=load_word(table_at(G_shaders,shader_id,SIZE_shaders)+SH_flags);
    if(dot(sd.Ng,sd.wi)<0.0){sd.runtime_flag|=SR_BACKFACING;sd.N=-sd.N;sd.Ng=-sd.Ng;sd.dPdu=-sd.dPdu;sd.dPdv=-sd.dPdv;}
    sd.dI=heap_float(state_address(S_ray_dD,index,4u));
    sd.dP=heap_float(state_address(S_ray_dP,index,4u))+sd.ray_length*sd.dI;surface_differentials(&sd);
    return sd;
}
const SZ_ShaderJump:u32=12u;
const N_ShaderJump_offset_surface:u32=0u;
const N_ShaderJump_offset_volume:u32=4u;
const N_ShaderJump_offset_displacement:u32=8u;
const SZ_JumpIfZero:u32=8u;
const N_JumpIfZero_jump_offset:u32=0u;
const N_JumpIfZero_stack_offset:u32=4u;
const SZ_JumpIfOne:u32=8u;
const N_JumpIfOne_jump_offset:u32=0u;
const N_JumpIfOne_stack_offset:u32=4u;
const SZ_Math:u32=20u;
const N_Math_math_type:u32=0u;
const N_Math_value1:u32=4u;
const N_Math_value2:u32=8u;
const N_Math_value3:u32=12u;
const N_Math_result_offset:u32=16u;
const SZ_Clamp:u32=20u;
const N_Clamp_clamp_type:u32=0u;
const N_Clamp_min:u32=4u;
const N_Clamp_max:u32=8u;
const N_Clamp_value:u32=12u;
const N_Clamp_result_offset:u32=16u;
const SZ_ValueF:u32=8u;
const N_ValueF_value:u32=0u;
const N_ValueF_out_offset:u32=4u;
const SZ_ValueV:u32=16u;
const N_ValueV_out_offset:u32=0u;
const N_ValueV_value:u32=4u;
const SZ_Convert:u32=8u;
const N_Convert_convert_type:u32=0u;
const N_Convert_from_offset:u32=4u;
const N_Convert_to_offset:u32=5u;
const SZ_Geometry:u32=8u;
const N_Geometry_geom_type:u32=0u;
const N_Geometry_bump_offset:u32=1u;
const N_Geometry_store_derivatives:u32=2u;
const N_Geometry_out_offset:u32=3u;
const N_Geometry_bump_filter_width:u32=4u;
const SZ_TexCoord:u32=8u;
const N_TexCoord_texco_type:u32=0u;
const N_TexCoord_bump_offset:u32=1u;
const N_TexCoord_store_derivatives:u32=2u;
const N_TexCoord_out_offset:u32=3u;
const N_TexCoord_bump_filter_width:u32=4u;
const SZ_Attr:u32=12u;
const N_Attr_attr:u32=0u;
const N_Attr_out_offset:u32=4u;
const N_Attr_output_type:u32=5u;
const N_Attr_bump_offset:u32=6u;
const N_Attr_store_derivatives:u32=7u;
const N_Attr_bump_filter_width:u32=8u;
const SZ_TexImage:u32=12u;
const N_TexImage_id:u32=0u;
const N_TexImage_projection:u32=4u;
const N_TexImage_flags:u32=8u;
const N_TexImage_co:u32=9u;
const N_TexImage_out_offset:u32=10u;
const N_TexImage_alpha_offset:u32=11u;
const SZ_TexImageBox:u32=12u;
const N_TexImageBox_blend:u32=4u;
const SZ_TexEnvironment:u32=12u;
const SZ_VertexColor:u32=8u;
const N_VertexColor_layer_id:u32=0u;
const N_VertexColor_color_offset:u32=1u;
const N_VertexColor_alpha_offset:u32=2u;
const N_VertexColor_bump_offset:u32=3u;
const N_VertexColor_bump_filter_width:u32=4u;
const SZ_VectorMath:u32=48u;
const N_VectorMath_math_type:u32=0u;
const N_VectorMath_a:u32=4u;
const N_VectorMath_b:u32=16u;
const N_VectorMath_c:u32=28u;
const N_VectorMath_param1:u32=40u;
const N_VectorMath_value_offset:u32=44u;
const N_VectorMath_vector_offset:u32=45u;
const SZ_SeparateVector:u32=16u;
const N_SeparateVector_vector:u32=0u;
const N_SeparateVector_vector_index:u32=12u;
const N_SeparateVector_out_offset:u32=13u;
const SZ_CombineVector:u32=8u;
const N_CombineVector_in:u32=0u;
const N_CombineVector_vector_index:u32=4u;
const N_CombineVector_out_offset:u32=5u;
const SZ_ClosureSetWeight:u32=12u;
const N_ClosureSetWeight_rgb:u32=0u;
const SZ_ClosureWeight:u32=4u;
const N_ClosureWeight_weight_offset:u32=0u;
const SZ_ClosureBackground:u32=4u;
const N_ClosureBackground_mix_weight_offset:u32=0u;
const SZ_ClosureEmission:u32=4u;
const N_ClosureEmission_mix_weight_offset:u32=0u;
const SZ_ClosureHoldout:u32=4u;
const N_ClosureHoldout_mix_weight_offset:u32=0u;
const SZ_ClosureBsdf:u32=8u;
const N_ClosureBsdf_closure_type:u32=0u;
const N_ClosureBsdf_mix_weight_offset:u32=4u;
const SZ_DiffuseBsdfData:u32=20u;
const N_DiffuseBsdfData_color:u32=0u;
const N_DiffuseBsdfData_roughness:u32=12u;
const N_DiffuseBsdfData_normal_offset:u32=16u;
const SZ_SimpleBsdfData:u32=8u;
const N_SimpleBsdfData_param1:u32=0u;
const N_SimpleBsdfData_normal_offset:u32=4u;
const SZ_ClosureSetNormal:u32=4u;
const N_ClosureSetNormal_direction_offset:u32=0u;
const N_ClosureSetNormal_normal_offset:u32=1u;
const SZ_EmissionWeight:u32=16u;
const N_EmissionWeight_color:u32=0u;
const N_EmissionWeight_strength:u32=12u;
const SZ_MixClosure:u32=8u;
const N_MixClosure_fac:u32=0u;
const N_MixClosure_in_weight_offset:u32=4u;
const N_MixClosure_weight1_offset:u32=5u;
const N_MixClosure_weight2_offset:u32=6u;
const SZ_TexChecker:u32=32u;
const N_TexChecker_color1:u32=0u;
const N_TexChecker_color2:u32=12u;
const N_TexChecker_scale:u32=24u;
const N_TexChecker_co:u32=28u;
const N_TexChecker_color_offset:u32=29u;
const N_TexChecker_fac_offset:u32=30u;
const SZ_TexGradient:u32=8u;
const N_TexGradient_gradient_type:u32=0u;
const N_TexGradient_co:u32=4u;
const N_TexGradient_fac_offset:u32=5u;
const N_TexGradient_color_offset:u32=6u;
const SZ_Gamma:u32=20u;
const N_Gamma_color:u32=0u;
const N_Gamma_gamma:u32=12u;
const N_Gamma_out_offset:u32=16u;
const SZ_Invert:u32=20u;
const N_Invert_color:u32=0u;
const N_Invert_fac:u32=12u;
const N_Invert_out_offset:u32=16u;
const SZ_BrightContrast:u32=24u;
const N_BrightContrast_color:u32=0u;
const N_BrightContrast_bright:u32=12u;
const N_BrightContrast_contrast:u32=16u;
const N_BrightContrast_out_offset:u32=20u;
const ATTR_ELEMENT_CORNER:u32=16u;
const ATTR_ELEMENT_FACE:u32=4u;
const ATTR_ELEMENT_IS_BYTE:u32=1024u;
const ATTR_ELEMENT_IS_NORMAL:u32=512u;
const ATTR_ELEMENT_MESH:u32=2u;
const ATTR_ELEMENT_NONE:u32=0u;
const ATTR_ELEMENT_OBJECT:u32=1u;
const ATTR_ELEMENT_VERTEX:u32=8u;
const ATTR_ELEMENT_VOXEL:u32=128u;
const ATTR_PRIM_TYPES:u32=2u;
const ATTR_STD_GENERATED:u32=11u;
const ATTR_STD_NONE:u32=0u;
const ATTR_STD_NOT_FOUND:u32=2147483649u;
const CAMERA_ORTHOGRAPHIC:u32=1u;
const CAMERA_PERSPECTIVE:u32=0u;
const EXTENSION_EXTEND:u32=1u;
const EXTENSION_MIRROR:u32=3u;
const EXTENSION_NUM_TYPES:u32=4u;
const EXTENSION_REPEAT:u32=0u;
const IMAGE_DATA_TYPE_BYTE:u32=4u;
const IMAGE_DATA_TYPE_BYTE4:u32=1u;
const IMAGE_DATA_TYPE_FLOAT:u32=3u;
const IMAGE_DATA_TYPE_FLOAT4:u32=0u;
const IMAGE_DATA_TYPE_HALF:u32=5u;
const IMAGE_DATA_TYPE_HALF4:u32=2u;
const IMAGE_DATA_TYPE_USHORT:u32=7u;
const IMAGE_DATA_TYPE_USHORT4:u32=6u;
const INTERPOLATION_CLOSEST:u32=1u;
const INTERPOLATION_LINEAR:u32=0u;
const INTERPOLATION_NUM_TYPES:u32=4u;
const KERNEL_IMAGE_NONE:u32=2147483647u;
const KERNEL_TILE_LOAD_NONE:u32=4294967295u;
const NODE_ATTR:u32=21u;
const NODE_ATTR_DERIVATIVE:u32=22u;
const NODE_ATTR_FLOAT:u32=0u;
const NODE_ATTR_FLOAT2:u32=1u;
const NODE_ATTR_FLOAT3:u32=2u;
const NODE_ATTR_FLOAT4:u32=3u;
const NODE_ATTR_OUTPUT_FLOAT:u32=1u;
const NODE_ATTR_OUTPUT_FLOAT3:u32=0u;
const NODE_ATTR_OUTPUT_FLOAT_ALPHA:u32=2u;
const NODE_ATTR_RGBA:u32=4u;
const NODE_BLEND_DIAGONAL:u32=3u;
const NODE_BLEND_EASING:u32=2u;
const NODE_BLEND_LINEAR:u32=0u;
const NODE_BLEND_QUADRATIC:u32=1u;
const NODE_BLEND_QUADRATIC_SPHERE:u32=5u;
const NODE_BLEND_RADIAL:u32=4u;
const NODE_BLEND_SPHERICAL:u32=6u;
const NODE_BRIGHTCONTRAST:u32=49u;
const NODE_BUMP_OFFSET_DX:u32=1u;
const NODE_BUMP_OFFSET_DY:u32=2u;
const NODE_CLAMP:u32=95u;
const NODE_CLAMP_MINMAX:u32=0u;
const NODE_CLAMP_RANGE:u32=1u;
const NODE_CLOSURE_BACKGROUND:u32=4u;
const NODE_CLOSURE_BSDF:u32=2u;
const NODE_CLOSURE_EMISSION:u32=3u;
const NODE_CLOSURE_HOLDOUT:u32=38u;
const NODE_CLOSURE_SET_NORMAL:u32=34u;
const NODE_CLOSURE_SET_WEIGHT:u32=5u;
const NODE_CLOSURE_WEIGHT:u32=6u;
const NODE_COMBINE_VECTOR:u32=86u;
const NODE_CONVERT:u32=13u;
const NODE_CONVERT_CF:u32=2u;
const NODE_CONVERT_CI:u32=3u;
const NODE_CONVERT_FI:u32=1u;
const NODE_CONVERT_FV:u32=0u;
const NODE_CONVERT_IF:u32=6u;
const NODE_CONVERT_IV:u32=7u;
const NODE_CONVERT_VF:u32=4u;
const NODE_CONVERT_VI:u32=5u;
const NODE_EMISSION_WEIGHT:u32=7u;
const NODE_END:u32=0u;
const NODE_GAMMA:u32=48u;
const NODE_GEOMETRY:u32=11u;
const NODE_GEOM_I:u32=3u;
const NODE_GEOM_N:u32=1u;
const NODE_GEOM_Ng:u32=4u;
const NODE_GEOM_P:u32=0u;
const NODE_GEOM_T:u32=2u;
const NODE_GEOM_uv:u32=5u;
const NODE_IMAGE_ALPHA_UNASSOCIATE:u32=2u;
const NODE_IMAGE_COMPRESS_AS_SRGB:u32=1u;
const NODE_IMAGE_PROJ_FLAT:u32=0u;
const NODE_IMAGE_PROJ_SPHERE:u32=2u;
const NODE_IMAGE_PROJ_TUBE:u32=3u;
const NODE_INVERT:u32=80u;
const NODE_JUMP_IF_ONE:u32=10u;
const NODE_JUMP_IF_ZERO:u32=9u;
const NODE_MATH:u32=44u;
const NODE_MATH_FLOORED_MODULO:u32=40u;
const NODE_MIX_CLOSURE:u32=8u;
const NODE_SEPARATE_VECTOR:u32=84u;
const NODE_SHADER_JUMP:u32=1u;
const NODE_TEXCO_CAMERA:u32=3u;
const NODE_TEXCO_DUPLI_GENERATED:u32=6u;
const NODE_TEXCO_DUPLI_UV:u32=7u;
const NODE_TEXCO_NORMAL:u32=0u;
const NODE_TEXCO_OBJECT:u32=1u;
const NODE_TEXCO_OBJECT_WITH_TRANSFORM:u32=2u;
const NODE_TEXCO_REFLECTION:u32=5u;
const NODE_TEXCO_VOLUME_GENERATED:u32=8u;
const NODE_TEXCO_WINDOW:u32=4u;
const NODE_TEX_CHECKER:u32=69u;
const NODE_TEX_COORD:u32=15u;
const NODE_TEX_ENVIRONMENT:u32=61u;
const NODE_TEX_ENVIRONMENT_DERIVATIVE:u32=62u;
const NODE_TEX_GRADIENT:u32=64u;
const NODE_TEX_IMAGE:u32=28u;
const NODE_TEX_IMAGE_BOX:u32=30u;
const NODE_TEX_IMAGE_BOX_DERIVATIVE:u32=31u;
const NODE_TEX_IMAGE_DERIVATIVE:u32=29u;
const NODE_VALUE_F:u32=17u;
const NODE_VALUE_V:u32=19u;
const NODE_VECTOR_MATH:u32=45u;
const NODE_VECTOR_MATH_ABSOLUTE:u32=17u;
const NODE_VECTOR_MATH_ADD:u32=0u;
const NODE_VECTOR_MATH_CEIL:u32=14u;
const NODE_VECTOR_MATH_COSINE:u32=22u;
const NODE_VECTOR_MATH_CROSS_PRODUCT:u32=4u;
const NODE_VECTOR_MATH_DISTANCE:u32=8u;
const NODE_VECTOR_MATH_DIVIDE:u32=3u;
const NODE_VECTOR_MATH_DOT_PRODUCT:u32=7u;
const NODE_VECTOR_MATH_FACEFORWARD:u32=25u;
const NODE_VECTOR_MATH_FLOOR:u32=13u;
const NODE_VECTOR_MATH_FRACTION:u32=16u;
const NODE_VECTOR_MATH_LENGTH:u32=9u;
const NODE_VECTOR_MATH_MAXIMUM:u32=19u;
const NODE_VECTOR_MATH_MINIMUM:u32=18u;
const NODE_VECTOR_MATH_MODULO:u32=15u;
const NODE_VECTOR_MATH_MULTIPLY:u32=2u;
const NODE_VECTOR_MATH_MULTIPLY_ADD:u32=26u;
const NODE_VECTOR_MATH_NORMALIZE:u32=11u;
const NODE_VECTOR_MATH_POWER:u32=27u;
const NODE_VECTOR_MATH_PROJECT:u32=5u;
const NODE_VECTOR_MATH_REFLECT:u32=6u;
const NODE_VECTOR_MATH_REFRACT:u32=24u;
const NODE_VECTOR_MATH_ROUND:u32=29u;
const NODE_VECTOR_MATH_SCALE:u32=10u;
const NODE_VECTOR_MATH_SIGN:u32=28u;
const NODE_VECTOR_MATH_SINE:u32=21u;
const NODE_VECTOR_MATH_SNAP:u32=12u;
const NODE_VECTOR_MATH_SUBTRACT:u32=1u;
const NODE_VECTOR_MATH_TANGENT:u32=23u;
const NODE_VECTOR_MATH_WRAP:u32=20u;
const NODE_VERTEX_COLOR:u32=23u;
const NODE_VERTEX_COLOR_DERIVATIVE:u32=24u;
const SVM_INPUT_STACK_OFFSET_MASK:u32=2143289344u;
const SVM_STACK_INVALID:u32=255u;
const SVM_STACK_SIZE:u32=255u;
const CLOSURE_BSDF_DIFFUSE_ID:u32=2u;
const CLOSURE_BSDF_OREN_NAYAR_ID:u32=3u;
const CLOSURE_BSDF_TRANSLUCENT_ID:u32=9u;
const CLOSURE_BSDF_TRANSPARENT_ID:u32=30u;
const CLOSURE_HOLDOUT_ID:u32=35u;
const SR_EMISSION:u32=2u;
const SR_BSDF:u32=4u;
const SR_BSDF_HAS_EVAL:u32=8u;
const SR_BSDF_HAS_TRANSMISSION:u32=1024u;
const SR_TRANSPARENT:u32=512u;
const SR_HOLDOUT:u32=32u;
const SD_USE_BUMP_MAP_CORRECTION:u32=32768u;
const PATH_RAY_TERMINATE:u32=15360u;
const PATH_RAY_EMISSION:u32=32u;
const PATH_RAY_VISIBILITY_SHADOW:u32=96u;
const SVM_SURFACE:bool=true;
const SVM_CLOSURE_CAPACITY:u32=64u;
const D_max_closures:u32=4u;
const O_dupli_generated:u32=128u;
const O_dupli_uv:u32=140u;
const D_film_rgb_to_y:u32=1504u;
const II_data:u32=0u;
const II_data_type:u32=8u;
const II_interpolation:u32=12u;
const II_extension:u32=16u;
const II_width:u32=20u;
const II_height:u32=24u;
const IT_image_info_id:u32=0u;
const IT_tile_descriptor_offset:u32=4u;
const IT_use_transform_3d:u32=36u;
const IU_tile:u32=0u;
const IU_image_texture_id:u32=4u;
const O_attribute_map_offset:u32=160u;
const AM_id:u32=0u;
const AM_offset:u32=8u;
const AM_element:u32=12u;
const AM_type:u32=14u;
const D_film_is_rec709:u32=1584u;
const D_film_rec709_to_r:u32=1536u;
const D_film_rec709_to_g:u32=1552u;
const D_film_rec709_to_b:u32=1568u;
const D_cam_type:u32=16u;
const D_cam_worldtocamera:u32=720u;
const D_cam_worldtondc:u32=656u;
const SZ_PackedTransform:u32=48u;
// Generated from Blender Cycles; see provenance.json and THIRD-PARTY-NOTICES.txt.
// Original code: SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-License-Identifier: Apache-2.0
// wrapf retains upstream's attribution: Adapted from godot-engine math_funcs.h.
// C library adapters below: Copyright (c) 2026 Jeremy Ellis and contributors
fn c_asin_series(x: f32) -> f32 {
    // Taylor coefficients binomial(2n,n)/(4^n*(2n+1)), n=0..11.
    // Half-angle reduction below keeps |x| <= 0.5; omitted tail < 4e-10.
    let z = x*x;
    var term = x;
    var sum = x;
    for (var n=1u; n<12u; n++) {
        let k = f32(n);
        term *= z*((2.0*k-1.0)*(2.0*k-1.0))/((2.0*k)*(2.0*k+1.0));
        sum += term;
    }
    return sum;
}
fn c_asin(x: f32) -> f32 {
    let a = abs(x);
    if (a <= 0.5) { return c_asin_series(x); }
    let result = 1.5707963267948966 - 2.0*c_asin_series(sqrt((1.0-a)*0.5));
    return select(result, -result, x < 0.0);
}
fn c_acos(x: f32) -> f32 {
    if (x >= 0.5) { return 2.0*c_asin_series(sqrt((1.0-x)*0.5)); }
    if (x <= -0.5) { return 3.141592653589793 - 2.0*c_asin_series(sqrt((1.0+x)*0.5)); }
    return 1.5707963267948966-c_asin_series(x);
}
fn c_fmod(a: f32, b: f32) -> f32 { return a - trunc(a / b) * b; }
fn c_pow(a: f32, b: f32) -> f32 {
    // C pow accepts negative bases with integer exponents; WGSL pow does not.
    if (a < 0.0) {
        let p = pow(-a, b);
        if (c_fmod(abs(b), 2.0) == 0.0) { return p; }
        return -p;
    }
    return pow(a, b);
}


fn c_ray_triangle_reciprocal(in_x: f32) -> f32 {
var v_x = in_x;
return (1.0f / v_x);
}

fn c_ray_triangle_dot_scalar(in_a: vec3<f32>, in_b: vec3<f32>) -> f32 {
var v_a = in_a;
var v_b = in_b;
return (((v_a.x * v_b.x) + (v_a.y * v_b.y)) + (v_a.z * v_b.z));
}

fn c_ray_triangle_cross(in_a: vec3<f32>, in_b: vec3<f32>) -> vec3<f32> {
var v_a = in_a;
var v_b = in_b;
return vec3<f32>(((v_a.y * v_b.z) - (v_a.z * v_b.y)), ((v_a.z * v_b.x) - (v_a.x * v_b.z)), ((v_a.x * v_b.y) - (v_a.y * v_b.x)));
}

fn c_ray_triangle_intersect(in_ray_P: vec3<f32>, in_ray_D: vec3<f32>, in_ray_tmin: f32, in_ray_tmax: f32, in_tri_a: vec3<f32>, in_tri_b: vec3<f32>, in_tri_c: vec3<f32>, v_isect_u: ptr<function, f32>, v_isect_v: ptr<function, f32>, v_isect_t: ptr<function, f32>) -> bool {
var v_ray_P = in_ray_P;
var v_ray_D = in_ray_D;
var v_ray_tmin = in_ray_tmin;
var v_ray_tmax = in_ray_tmax;
var v_tri_a = in_tri_a;
var v_tri_b = in_tri_b;
var v_tri_c = in_tri_c;
var v_v0: vec3<f32> = (v_tri_a - v_ray_P);
var v_v1: vec3<f32> = (v_tri_b - v_ray_P);
var v_v2: vec3<f32> = (v_tri_c - v_ray_P);
var v_e0: vec3<f32> = (v_v2 - v_v0);
var v_e1: vec3<f32> = (v_v0 - v_v1);
var v_e2: vec3<f32> = (v_v1 - v_v2);
var v_U: f32 = c_ray_triangle_dot(c_ray_triangle_cross(v_e0, (v_v2 + v_v0)), v_ray_D);
var v_V: f32 = c_ray_triangle_dot(c_ray_triangle_cross(v_e1, (v_v0 + v_v1)), v_ray_D);
var v_W: f32 = c_ray_triangle_dot(c_ray_triangle_cross(v_e2, (v_v1 + v_v2)), v_ray_D);
var v_UVW: f32 = ((v_U + v_V) + v_W);
var v_eps: f32 = (1.1920929E-7f * abs(v_UVW));
var v_minUVW: f32 = min(v_U, min(v_V, v_W));
var v_maxUVW: f32 = max(v_U, max(v_V, v_W));
if ((!(((v_minUVW >= (-v_eps)) || (v_maxUVW <= v_eps))))) {
return false;
}
var v_Ng1: vec3<f32> = c_ray_triangle_cross(v_e1, v_e0);
var v_Ng: vec3<f32> = (v_Ng1 + v_Ng1);
var v_den: f32 = dot(v_Ng, v_ray_D);
if (((v_den == 0.0f))) {
return false;
}
var v_T: f32 = dot(v_v0, v_Ng);
var v_t: f32 = (v_T / v_den);
if ((!(((v_t >= v_ray_tmin) && (v_t <= v_ray_tmax))))) {
return false;
}
var choice_1: f32;
if (((abs(v_UVW) < 1.00000005E-18f))) {
choice_1 = 0.0f;
} else {
choice_1 = c_ray_triangle_reciprocal(v_UVW);
}
var v_rcp_uvw: f32 = choice_1;
(*v_isect_u) = min((v_U * v_rcp_uvw), 1.0f);
(*v_isect_v) = min((v_V * v_rcp_uvw), 1.0f);
(*v_isect_t) = v_t;
return true;
}

fn c_fractf(in_x: f32) -> f32 {
var v_x = in_x;
return (v_x - floor(v_x));
}

fn c_wrapf(in_value: f32, in_max: f32, in_min: f32) -> f32 {
var v_value = in_value;
var v_max = in_max;
var v_min = in_min;
var v_range: f32 = (v_max - v_min);
var choice_2: f32;
if (((v_range != 0.0f))) {
choice_2 = (v_value - ((v_range * floor((((v_value - v_min)) / v_range)))));
} else {
choice_2 = v_min;
}
return choice_2;
}

fn c_pingpongf(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var choice_3: f32;
if (((v_b != 0.0f))) {
choice_3 = abs((((c_fractf((((v_a - v_b)) / ((v_b * 2.0f)))) * v_b) * 2.0f) - v_b));
} else {
choice_3 = 0.0f;
}
return choice_3;
}

fn c_smoothminf(in_a: f32, in_b: f32, in_k: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var v_k = in_k;
if ((v_k != 0.0f)) {
var v_h: f32 = (max((v_k - abs((v_a - v_b))), 0.0f) / v_k);
return (min(v_a, v_b) - ((((v_h * v_h) * v_h) * v_k) * ((1.0f / 6.0f))));
}
return min(v_a, v_b);
}

fn c_signf(in_f: f32) -> f32 {
var v_f = in_f;
var choice_4: f32;
if (((v_f < 0.0f))) {
choice_4 = (-1.0f);
} else {
choice_4 = 1.0f;
}
return choice_4;
}

fn c_compatible_atan2(in_y: f32, in_x: f32) -> f32 {
var v_y = in_y;
var v_x = in_x;
var choice_5: f32;
if ((((v_x == 0.0f) && (v_y == 0.0f)))) {
choice_5 = 0.0f;
} else {
choice_5 = atan2(v_y, v_x);
}
return choice_5;
}

fn c_compatible_signf(in_f: f32) -> f32 {
var v_f = in_f;
if ((v_f == 0.0f)) {
return 0.0f;
}
return c_signf(v_f);
}

fn c_safe_sqrtf(in_f: f32) -> f32 {
var v_f = in_f;
return sqrt(max(v_f, 0.0f));
}

fn c_inversesqrtf(in_f: f32) -> f32 {
var v_f = in_f;
var choice_6: f32;
if (((v_f > 0.0f))) {
choice_6 = (1.0f / sqrt(v_f));
} else {
choice_6 = 0.0f;
}
return choice_6;
}

fn c_safe_asinf(in_a: f32) -> f32 {
var v_a = in_a;
return c_asin(clamp(v_a, (-1.0f), 1.0f));
}

fn c_safe_acosf(in_a: f32) -> f32 {
var v_a = in_a;
return c_acos(clamp(v_a, (-1.0f), 1.0f));
}

fn c_compatible_powf(in_x: f32, in_y: f32) -> f32 {
var v_x = in_x;
var v_y = in_y;
if ((v_y == 0.0f)) {
return 1.0f;
}
if ((v_x == 0.0f)) {
return 0.0f;
}
return c_pow(v_x, v_y);
}

fn c_safe_powf(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
if ((((v_a < 0.0f) && (v_b != f32(i32(v_b)))))) {
return 0.0f;
}
return c_compatible_powf(v_a, v_b);
}

fn c_safe_divide(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var choice_7: f32;
if (((v_b != 0.0f))) {
choice_7 = (v_a / v_b);
} else {
choice_7 = 0.0f;
}
return choice_7;
}

fn c_safe_logf(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
if ((((v_a <= 0.0f) || (v_b <= 0.0f)))) {
return 0.0f;
}
return c_safe_divide(log(v_a), log(v_b));
}

fn c_safe_modulo(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var choice_8: f32;
if (((v_b != 0.0f))) {
choice_8 = c_fmod(v_a, v_b);
} else {
choice_8 = 0.0f;
}
return choice_8;
}

fn c_safe_floored_modulo(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var choice_9: f32;
if (((v_b != 0.0f))) {
choice_9 = (v_a - (floor((v_a / v_b)) * v_b));
} else {
choice_9 = 0.0f;
}
return choice_9;
}

fn c_svm_math(in_type: u32, in_a: f32, in_b: f32, in_c: f32) -> f32 {
var v_type = in_type;
var v_a = in_a;
var v_b = in_b;
var v_c = in_c;
switch (v_type) {
case 0u: {
return (v_a + v_b);
}
case 1u: {
return (v_a - v_b);
}
case 2u: {
return (v_a * v_b);
}
case 3u: {
return c_safe_divide(v_a, v_b);
}
case 10u: {
return c_safe_powf(v_a, v_b);
}
case 11u: {
return c_safe_logf(v_a, v_b);
}
case 23u: {
return c_safe_sqrtf(v_a);
}
case 24u: {
return c_inversesqrtf(v_a);
}
case 18u: {
return abs(v_a);
}
case 27u: {
return (v_a * (((3.14159274f) / 180.0f)));
}
case 28u: {
return (v_a * ((180.0f / (3.14159274f))));
}
case 12u: {
return min(v_a, v_b);
}
case 13u: {
return max(v_a, v_b);
}
case 15u: {
return select(f32(0), f32(1), (v_a < v_b));
}
case 16u: {
return select(f32(0), f32(1), (v_a > v_b));
}
case 14u: {
return floor((v_a + 0.5f));
}
case 20u: {
return floor(v_a);
}
case 21u: {
return ceil(v_a);
}
case 22u: {
return (v_a - floor(v_a));
}
case 17u: {
return c_safe_modulo(v_a, v_b);
}
case 40u: {
return c_safe_floored_modulo(v_a, v_b);
}
case 32u: {
var choice_10: f32;
if ((v_a >= 0.0f)) {
choice_10 = floor(v_a);
} else {
choice_10 = ceil(v_a);
}
return choice_10;
}
case 33u: {
return (floor(c_safe_divide(v_a, v_b)) * v_b);
}
case 34u: {
return c_wrapf(v_a, v_b, v_c);
}
case 37u: {
return c_pingpongf(v_a, v_b);
}
case 4u: {
return sin(v_a);
}
case 5u: {
return cos(v_a);
}
case 6u: {
return tan(v_a);
}
case 29u: {
return sinh(v_a);
}
case 30u: {
return cosh(v_a);
}
case 31u: {
return tanh(v_a);
}
case 7u: {
return c_safe_asinf(v_a);
}
case 8u: {
return c_safe_acosf(v_a);
}
case 9u: {
return atan(v_a);
}
case 19u: {
return c_compatible_atan2(v_a, v_b);
}
case 25u: {
return c_compatible_signf(v_a);
}
case 26u: {
return exp(v_a);
}
case 35u: {
var choice_11: f32;
if (((((v_a == v_b)) || ((abs((v_a - v_b)) <= max(v_c, 1.1920929E-7f)))))) {
choice_11 = 1.0f;
} else {
choice_11 = 0.0f;
}
return choice_11;
}
case 36u: {
return ((v_a * v_b) + v_c);
}
case 38u: {
return c_smoothminf(v_a, v_b, v_c);
}
case 39u: {
return (-c_smoothminf((-v_a), (-v_b), v_c));
}
default: {
return 0.0f;
}
}
}

fn c_svm_math_gamma_color(in_color: vec3<f32>, in_gamma: f32) -> vec3<f32> {
var v_color = in_color;
var v_gamma = in_gamma;
if ((v_gamma == 0.0f)) {
return vec3<f32>(1.0f, 1.0f, 1.0f);
}
if ((v_color.x > 0.0f)) {
v_color.x = c_pow(v_color.x, v_gamma);
}
if ((v_color.y > 0.0f)) {
v_color.y = c_pow(v_color.y, v_gamma);
}
if ((v_color.z > 0.0f)) {
v_color.z = c_pow(v_color.z, v_gamma);
}
return v_color;
}
// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native comparison uses Cycles' SSE4.2 CPU oracle. Its ray_triangle_dot
// groups y/z before x, unlike the generic scalar AST branch. This adapter
// preserves that grouping; it does not emulate SSE reciprocal approximation.
fn c_ray_triangle_dot(a:vec3<f32>,b:vec3<f32>)->f32{
    return a.x*b.x+(a.y*b.y+a.z*b.z);
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from Cycles: native SVM closure allocation/setup ported to WGSL.
// Source lineage and BSD notice are in
// native/THIRD-PARTY-NOTICES.txt and parity/licenses/BSD-3-Clause.txt.
struct SvmClosure {
    weight:vec3<f32>, sample_weight:f32, N:vec3<f32>, kind:u32,
    roughness:f32, a:f32, b:f32, multiscatter:vec3<f32>,
}
struct SvmResult {
    closure_base:u32, count:u32, remaining:u32,
    runtime_flag:u32, error:u32, lcg_state:u32, N:vec3<f32>, emission:vec3<f32>, transparency:vec3<f32>,
}
// Per-path closure storage stays in the arena, avoiding large private arrays
// that exceed register/spill limits on mobile GPU compilers. Packed stride 56.
const SVM_CLOSURE_BYTES:u32=56u;
fn svm_closure_read(r:SvmResult,index:u32)->SvmClosure{
    var c:SvmClosure;if(index>=r.count){return c;}let base=r.closure_base+index*SVM_CLOSURE_BYTES;
    c.weight=heap_vec3(base);c.sample_weight=heap_float(base+12u);c.N=heap_vec3(base+16u);c.kind=load_word(base+28u);
    c.roughness=heap_float(base+32u);c.a=heap_float(base+36u);c.b=heap_float(base+40u);c.multiscatter=heap_vec3(base+44u);return c;
}
fn svm_closure_write(base:u32,c:SvmClosure){
    store_word(base,bitcast<u32>(c.weight.x));store_word(base+4u,bitcast<u32>(c.weight.y));store_word(base+8u,bitcast<u32>(c.weight.z));
    store_word(base+12u,bitcast<u32>(c.sample_weight));store_word(base+16u,bitcast<u32>(c.N.x));store_word(base+20u,bitcast<u32>(c.N.y));store_word(base+24u,bitcast<u32>(c.N.z));store_word(base+28u,c.kind);
    store_word(base+32u,bitcast<u32>(c.roughness));store_word(base+36u,bitcast<u32>(c.a));store_word(base+40u,bitcast<u32>(c.b));
    store_word(base+44u,bitcast<u32>(c.multiscatter.x));store_word(base+48u,bitcast<u32>(c.multiscatter.y));store_word(base+52u,bitcast<u32>(c.multiscatter.z));
}
fn svm_lcg(index:u32)->u32{
    // Cycles hash_uint3 / Jenkins finalizer, used by lcg_state_init.
    var a=0xdeadbf08u+(state_word(S_path_rng_pixel,index)^0xb4bc3953u);
    var b=0xdeadbf08u+state_half(S_path_rng_offset,index);var c=0xdeadbf08u+state_word(S_path_sample,index);
    c^=b;c-=(b<<14u)|(b>>18u);a^=c;a-=(c<<11u)|(c>>21u);
    b^=a;b-=(a<<25u)|(a>>7u);c^=b;c-=(b<<16u)|(b>>16u);
    a^=c;a-=(c<<4u)|(c>>28u);b^=a;b-=(a<<14u)|(a>>18u);c^=b;c-=(b<<24u)|(b>>8u);return c;
}
fn svm_allocate(s:ptr<function,SvmState>,kind:u32,weight:vec3<f32>)->u32{
    if((*s).error!=0u||(*s).output.remaining==0u){return NONE;}
    let index=(*s).output.count;
    if(index>=SVM_CLOSURE_CAPACITY){svm_error(s,16u);return NONE;}
    if((*s).output.closure_base==0u){svm_error(s,1u);return NONE;}
    var c:SvmClosure;c.kind=kind;c.weight=weight;svm_closure_write((*s).output.closure_base+index*SVM_CLOSURE_BYTES,c);
    (*s).output.count++;(*s).output.remaining--;return index;
}
fn svm_valid_normal(ng:vec3<f32>,wi:vec3<f32>,n:vec3<f32>)->vec3<f32>{
    let reflection=2.0*dot(n,wi)*n-wi;let iz=dot(wi,ng);let threshold=min(0.9*iz,0.01);
    if(dot(ng,reflection)>=threshold){return n;}
    var x=n-dot(n,ng)*ng;let magnitude=length(x);if(magnitude!=0.0){x*=1.0/magnitude;}else{x=n;}
    let ix=dot(wi,x);let a=ix*ix+iz*iz;let b=2.0*(a+iz*threshold);let c=(threshold+iz)*(threshold+iz);
    let root=sqrt(max(b*b-4.0*a*c,0.0));let nz2=select(0.25*(b-root)/a,0.25*(b+root)/a,ix<0.0);
    return sqrt(max(1.0-nz2,0.0))*x+sqrt(max(nz2,0.0))*ng;
}
// Original CodeBrix arithmetic adapter. Add already-rounded binary32 products
// without a driver contracting the multiply/add before 1-E cancellation.
// Oren's two calls have positive normal a, |b| < a, and a normal positive sum.
// Align significands with three guard bits and round ties to an even mantissa.
fn svm_oren_add(a:f32,b:f32)->f32{
    let ax=bitcast<u32>(a);let bx=bitcast<u32>(b);
    if((bx&0x7fffffffu)==0u){return a;}
    var exponent=(ax>>23u)&255u;let delta=exponent-((bx>>23u)&255u);
    let ma=((ax&0x7fffffu)|0x800000u)<<3u;
    var mb=((bx&0x7fffffu)|0x800000u)<<3u;
    if(delta>=31u){mb=1u;}
    else if(delta>0u){mb=(mb>>delta)|select(0u,1u,(mb&((1u<<delta)-1u))!=0u);}
    var sum=ma+mb;if((bx&0x80000000u)!=0u){sum=ma-mb;}
    if(sum>=0x8000000u){sum=(sum>>1u)|(sum&1u);exponent++;}
    else{while(sum<0x4000000u){sum<<=1u;exponent--;}}
    let tail=sum&7u;var mantissa=sum>>3u;
    if(tail>4u||(tail==4u&&(mantissa&1u)!=0u)){mantissa++;}
    if(mantissa>=0x1000000u){mantissa>>=1u;exponent++;}
    return bitcast<f32>((exponent<<23u)|(mantissa&0x7fffffu));
}
fn svm_oren_g(cosine:f32)->f32{
    if(cosine<1e-6){return (1.5707963267948966f-2.0f/3.0f)-cosine;}
    let sine=sqrt(max(1.0-cosine*cosine,0.0));let theta=c_safe_acosf(cosine);
    return sine*(theta-2.0f/3.0f-sine*cosine)+(2.0f/3.0f)*(sine/cosine)*(1.0-sine*sine*sine);
}
fn svm_transparent(s:ptr<function,SvmState>,weight:vec3<f32>,flag:u32){
    let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));if(!(sample_weight>=1e-5)){return;}
    (*s).output.transparency+=weight;
    if(((*s).output.runtime_flag&SR_TRANSPARENT)!=0u){
        for(var i=0u;i<(*s).output.count;i++){var c=svm_closure_read((*s).output,i);if(c.kind==CLOSURE_BSDF_TRANSPARENT_ID){
            c.weight+=weight;c.sample_weight+=sample_weight;svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);break;}}
    }else{
        (*s).output.runtime_flag|=SR_BSDF|SR_TRANSPARENT;
        if((flag&PATH_RAY_TERMINATE)!=0u){(*s).output.remaining=1u;}
        let i=svm_allocate(s,CLOSURE_BSDF_TRANSPARENT_ID,weight);
        if(i!=NONE){var c=svm_closure_read((*s).output,i);c.sample_weight=sample_weight;c.N=(*s).geometry.N;svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);}
        else if((flag&PATH_RAY_TERMINATE)!=0u){(*s).output.remaining=0u;}
    }
}
fn svm_bsdf(s:ptr<function,SvmState>,node:u32,flag:u32){
    let kind=load_word(node+N_ClosureBsdf_closure_type);let mix_weight=svm_optional(s,svm_byte(node+N_ClosureBsdf_mix_weight_offset),1.0);
    var bytes=0u;switch(kind){case CLOSURE_BSDF_DIFFUSE_ID:{bytes=SZ_DiffuseBsdfData;}
        case CLOSURE_BSDF_TRANSLUCENT_ID,CLOSURE_BSDF_TRANSPARENT_ID:{bytes=SZ_SimpleBsdfData;}
        default:{svm_error(s,16u);return;}}
    let data=svm_take(s,bytes);if((*s).error!=0u||mix_weight==0.0){return;}
    let raw_weight=(*s).weight*mix_weight;
    if(kind==CLOSURE_BSDF_TRANSPARENT_ID){svm_transparent(s,raw_weight,flag);return;}
    let normal_index=svm_byte(data+select(N_SimpleBsdfData_normal_offset,N_DiffuseBsdfData_normal_offset,kind==CLOSURE_BSDF_DIFFUSE_ID));
    var n=(*s).geometry.N;if(normal_index!=SVM_STACK_INVALID){n=svm_load3(s,normal_index);}
    let magnitude=length(n);if(magnitude!=0.0){n*=1.0/magnitude;}else{n=(*s).geometry.N;}
    var roughness=0.0;var color=vec3<f32>(0);
    if(kind==CLOSURE_BSDF_DIFFUSE_ID){roughness=svm_input(s,data+N_DiffuseBsdfData_roughness);
        if(!(roughness<1e-5)){color=clamp(svm_input3(s,data+N_DiffuseBsdfData_color),vec3<f32>(0),vec3<f32>(1));}}
    else if(((*s).geometry.shader_flag&SD_USE_BUMP_MAP_CORRECTION)!=0u&&any((*s).geometry.Ng!=n)){
        n=svm_valid_normal((*s).geometry.Ng,(*s).geometry.wi,n);
    }
    let weight=max(raw_weight,vec3<f32>(0));let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));
    if(!(sample_weight>=1e-5)){return;}
    let i=svm_allocate(s,kind,weight);if(i==NONE){return;}
    var closure=svm_closure_read((*s).output,i);closure.N=n;closure.sample_weight=sample_weight;
    (*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_EVAL;
    if(kind==CLOSURE_BSDF_TRANSLUCENT_ID){(*s).output.runtime_flag|=SR_BSDF_HAS_TRANSMISSION;}
    else if(!(roughness<1e-5)){
        let pi=3.141592653589793;let sigma=clamp(roughness,0.0,1.0);
        let a=1.0/(pi+sigma*(1.5707963267948966f-2.0f/3.0f));let b=sigma*a;
        let average=svm_oren_add(a*pi,((6.283185307179586f-5.6f)/3.0f)*b);
        let ems=0.3183098861837907f*(color*color)*(average/(1.0-average))/(vec3<f32>(1)-color*(1.0-average));
        let ev=svm_oren_add(a*pi,b*svm_oren_g(max(dot(n,(*s).geometry.wi),0.0)));
        closure.kind=CLOSURE_BSDF_OREN_NAYAR_ID;closure.roughness=roughness;
        closure.a=a;closure.b=b;closure.multiscatter=ems*(1.0-ev);
    }
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,closure);
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from Cycles: SVM bytecode interpretation ported to WGSL.
// Node layouts/enums come from the compiled
// Cycles ABI; scalar math/gamma functions are generated from upstream AST.
// Background emission accumulation also derives from closure/emissive.h (BSD).
struct SvmState {
    stack:array<u32,255>, initialized:array<u32,8>, pc:u32, error:u32,
    weight:vec3<f32>, geometry:SurfaceData, output:SvmResult,
}
// Error bits: 2 unsupported opcode; 4 bytecode bounds; 8 invalid/uninitialized
// stack; 16 unsupported parameter; 32 instruction limit. Bit 1 is shared with
// other wavefront checks; 64 invalid attribute map/data range. An error never
// supplies substitute shader radiance. Image errors: 128 descriptor/data, 256 tiled cache.
fn svm_error(s:ptr<function,SvmState>,code:u32){(*s).error|=code;(*s).output.error|=code;atomicOr(&heap[p(17u)/4u],code);}
fn svm_take(s:ptr<function,SvmState>,bytes:u32)->u32{
    let count=bytes/4u;
    if((*s).pc>p(22u)||count>p(22u)-(*s).pc){svm_error(s,4u);return 0u;}
    let address=p(21u)+(*s).pc*4u;(*s).pc+=count;return address;
}
fn svm_byte(address:u32)->u32{return (load_word(address)>>(8u*(address%4u)))&255u;}
fn svm_load_bits(s:ptr<function,SvmState>,index:u32)->u32{
    if(index>=SVM_STACK_SIZE){svm_error(s,8u);return 0u;}
    if(((*s).initialized[index/32u]&(1u<<(index%32u)))==0u){svm_error(s,8u);return 0u;}
    return (*s).stack[index];
}
fn svm_load(s:ptr<function,SvmState>,index:u32)->f32{return bitcast<f32>(svm_load_bits(s,index));}
fn svm_store_bits(s:ptr<function,SvmState>,index:u32,value:u32){
    if(index>=SVM_STACK_SIZE){svm_error(s,8u);return;}
    (*s).stack[index]=value;(*s).initialized[index/32u]|=1u<<(index%32u);
}
fn svm_store(s:ptr<function,SvmState>,index:u32,value:f32){svm_store_bits(s,index,bitcast<u32>(value));}
fn svm_load3(s:ptr<function,SvmState>,index:u32)->vec3<f32>{return vec3<f32>(svm_load(s,index),svm_load(s,index+1u),svm_load(s,index+2u));}
fn svm_store3(s:ptr<function,SvmState>,index:u32,value:vec3<f32>){svm_store(s,index,value.x);svm_store(s,index+1u,value.y);svm_store(s,index+2u,value.z);}
fn svm_input(s:ptr<function,SvmState>,address:u32)->f32{
    let bits=load_word(address);if((bits>>8u)==(SVM_INPUT_STACK_OFFSET_MASK>>8u)){return svm_load(s,bits&255u);}return bitcast<f32>(bits);
}
fn svm_input3(s:ptr<function,SvmState>,address:u32)->vec3<f32>{
    let bits=load_word(address);if((bits>>8u)==(SVM_INPUT_STACK_OFFSET_MASK>>8u)){return svm_load3(s,bits&255u);}return heap_vec3(address);
}
fn svm_optional(s:ptr<function,SvmState>,index:u32,default_value:f32)->f32{if(index==SVM_STACK_INVALID){return default_value;}return svm_load(s,index);}
fn svm_optional_store(s:ptr<function,SvmState>,index:u32,value:f32){if(index!=SVM_STACK_INVALID){svm_store(s,index,value);}}
fn svm_optional_store3(s:ptr<function,SvmState>,index:u32,value:vec3<f32>){if(index!=SVM_STACK_INVALID){svm_store3(s,index,value);}}
fn svm_int(s:ptr<function,SvmState>,value:f32)->u32{
    if((bitcast<u32>(value)&0x7f800000u)==0x7f800000u||value>=2147483648.0||value< -2147483648.0){svm_error(s,16u);return 0u;}
    return bitcast<u32>(i32(value));
}
fn svm_normalize(v:vec3<f32>)->vec3<f32>{let len=length(v);if(len!=0.0){return v*(1.0/len);}return v;}
fn svm_divide(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{return vec3<f32>(c_safe_divide(a.x,b.x),c_safe_divide(a.y,b.y),c_safe_divide(a.z,b.z));}
fn svm_vector(s:ptr<function,SvmState>,node:u32){
    let kind=load_word(node+N_VectorMath_math_type);
    let a=svm_input3(s,node+N_VectorMath_a);let b=svm_input3(s,node+N_VectorMath_b);let c=svm_input3(s,node+N_VectorMath_c);let scale=svm_input(s,node+N_VectorMath_param1);
    var v=vec3<f32>(0);var value=0.0;
    switch(kind){
        case NODE_VECTOR_MATH_ADD:{v=a+b;}
        case NODE_VECTOR_MATH_SUBTRACT:{v=a-b;}
        case NODE_VECTOR_MATH_MULTIPLY:{v=a*b;}
        case NODE_VECTOR_MATH_DIVIDE:{v=svm_divide(a,b);}
        case NODE_VECTOR_MATH_CROSS_PRODUCT:{v=cross(a,b);}
        case NODE_VECTOR_MATH_PROJECT:{let len2=dot(b,b);if(len2!=0.0){v=(dot(a,b)/len2)*b;}}
        case NODE_VECTOR_MATH_REFLECT:{let n=svm_normalize(b);v=a-2.0*dot(a,n)*n;}
        case NODE_VECTOR_MATH_REFRACT:{let n=svm_normalize(b);let ndot=dot(n,a);let k=1.0-scale*scale*(1.0-ndot*ndot);if(k>=0.0){v=scale*a-(scale*ndot+sqrt(k))*n;}}
        case NODE_VECTOR_MATH_FACEFORWARD:{v=select(-a,a,dot(c,b)<0.0);}
        case NODE_VECTOR_MATH_MULTIPLY_ADD:{v=a*b+c;}
        case NODE_VECTOR_MATH_DOT_PRODUCT:{value=dot(a,b);}
        case NODE_VECTOR_MATH_DISTANCE:{value=length(a-b);}
        case NODE_VECTOR_MATH_LENGTH:{value=length(a);}
        case NODE_VECTOR_MATH_SCALE:{v=a*scale;}
        case NODE_VECTOR_MATH_NORMALIZE:{v=svm_normalize(a);}
        case NODE_VECTOR_MATH_SNAP:{v=floor(svm_divide(a,b))*b;}
        case NODE_VECTOR_MATH_ROUND:{v=floor(a+vec3<f32>(0.5));}
        case NODE_VECTOR_MATH_FLOOR:{v=floor(a);}
        case NODE_VECTOR_MATH_CEIL:{v=ceil(a);}
        case NODE_VECTOR_MATH_MODULO:{v=vec3<f32>(c_safe_modulo(a.x,b.x),c_safe_modulo(a.y,b.y),c_safe_modulo(a.z,b.z));}
        case NODE_VECTOR_MATH_WRAP:{v=vec3<f32>(c_wrapf(a.x,b.x,c.x),c_wrapf(a.y,b.y,c.y),c_wrapf(a.z,b.z,c.z));}
        case NODE_VECTOR_MATH_FRACTION:{v=a-floor(a);}
        case NODE_VECTOR_MATH_ABSOLUTE:{v=abs(a);}
        case NODE_VECTOR_MATH_POWER:{v=vec3<f32>(c_safe_powf(a.x,b.x),c_safe_powf(a.y,b.y),c_safe_powf(a.z,b.z));}
        case NODE_VECTOR_MATH_SIGN:{v=vec3<f32>(c_compatible_signf(a.x),c_compatible_signf(a.y),c_compatible_signf(a.z));}
        case NODE_VECTOR_MATH_MINIMUM:{v=min(a,b);}
        case NODE_VECTOR_MATH_MAXIMUM:{v=max(a,b);}
        case NODE_VECTOR_MATH_SINE:{v=sin(a);}
        case NODE_VECTOR_MATH_COSINE:{v=cos(a);}
        case NODE_VECTOR_MATH_TANGENT:{v=tan(a);}
        default:{svm_error(s,16u);}
    }
    svm_optional_store(s,svm_byte(node+N_VectorMath_value_offset),value);svm_optional_store3(s,svm_byte(node+N_VectorMath_vector_offset),v);
}
fn svm_transform(address:u32,v:vec3<f32>)->vec3<f32>{
    let x=heap_vec4(address);let y=heap_vec4(address+16u);let z=heap_vec4(address+32u);
    return vec3<f32>(x.x,y.x,z.x)*v.x+(vec3<f32>(x.y,y.y,z.y)*v.y+vec3<f32>(x.z,y.z,z.z)*v.z)+vec3<f32>(x.w,y.w,z.w);
}
fn svm_texco(s:ptr<function,SvmState>,node:u32,index:u32)->vec3<f32>{
    let kind=svm_byte(node+N_TexCoord_texco_type);let sd=(*s).geometry;let world=sd.object_id==NONE;
    var object=0u;if(!world){object=table_at(G_objects,sd.object_id,SIZE_objects);}
    switch(kind){
        case NODE_TEXCO_OBJECT:{if(world){return sd.P;}return surface_transform_point(object+O_itfm,sd.P);}
        case NODE_TEXCO_VOLUME_GENERATED:{if(!world){svm_error(s,16u);}return sd.P;}
        case NODE_TEXCO_NORMAL:{if(world){return sd.N;}return surface_transform_normal(object+O_tfm,sd.N);}
        case NODE_TEXCO_REFLECTION:{if(world){return sd.wi;}return 2.0*dot(sd.wi,sd.N)*sd.N-sd.wi;}
        case NODE_TEXCO_DUPLI_GENERATED:{if(world){return vec3<f32>(0);}return heap_vec3(object+O_dupli_generated);}
        case NODE_TEXCO_DUPLI_UV:{if(world){return vec3<f32>(0);}return vec3<f32>(heap_float(object+O_dupli_uv),heap_float(object+O_dupli_uv+4u),0);}
        case NODE_TEXCO_OBJECT_WITH_TRANSFORM:{let address=svm_take(s,SZ_PackedTransform);if((*s).error!=0u){return vec3<f32>(0);}return svm_transform(address,sd.P);}
        case NODE_TEXCO_CAMERA,NODE_TEXCO_WINDOW:{
            let matrix_world=p(6u)+D_cam_cameratoworld;let camera_pos=vec3<f32>(heap_float(matrix_world+12u),heap_float(matrix_world+28u),heap_float(matrix_world+44u));
            if(kind==NODE_TEXCO_CAMERA){return svm_transform(p(6u)+D_cam_worldtocamera,sd.P+select(vec3<f32>(0),camera_pos,world));}
            let camera_type=data_word(D_cam_type);var point=sd.P;
            if(camera_type==CAMERA_PERSPECTIVE){if(world){point+=camera_pos;}}
            else if(camera_type==CAMERA_ORTHOGRAPHIC){if(world&&(state_byte(S_path_visibility,index)&VIS_CAMERA)!=0u){point=heap_vec3(state_address(S_ray_P,index,12u));}}
            else{svm_error(s,16u);return vec3<f32>(0);}
            let matrix=p(6u)+D_cam_worldtondc;let v=vec4<f32>(point,1);let w=dot(heap_vec4(matrix+48u),v);
            return vec3<f32>(dot(heap_vec4(matrix),v)/w,dot(heap_vec4(matrix+16u),v)/w,0);
        }
        default:{svm_error(s,16u);return vec3<f32>(0);}
    }
}
fn svm_gradient(s:ptr<function,SvmState>,kind:u32,v:vec3<f32>)->f32{
    switch(kind){
        case NODE_BLEND_LINEAR:{return v.x;}
        case NODE_BLEND_QUADRATIC:{let r=max(v.x,0.0);return r*r;}
        case NODE_BLEND_EASING:{let r=clamp(v.x,0.0,1.0);let t=r*r;return 3.0*t-2.0*t*r;}
        case NODE_BLEND_DIAGONAL:{return (v.x+v.y)*0.5;}
        case NODE_BLEND_RADIAL:{return atan2(v.y,v.x)/6.2831855+0.5;}
        case NODE_BLEND_QUADRATIC_SPHERE,NODE_BLEND_SPHERICAL:{let r=max(0.999999-sqrt(v.x*v.x+v.y*v.y+v.z*v.z),0.0);return select(r,r*r,kind==NODE_BLEND_QUADRATIC_SPHERE);}
        default:{svm_error(s,16u);return 0.0;}
    }
}
fn svm_evaluate(index:u32,geometry:SurfaceData,closure_limit:u32,closure_base:u32)->SvmResult{
    var s:SvmState;s.geometry=geometry;s.pc=(geometry.shader&SHADER_MASK)*(1u+SZ_ShaderJump/4u);
    s.output.runtime_flag=geometry.runtime_flag;s.output.N=geometry.N;s.output.lcg_state=svm_lcg(index);
    let flag=state_word(S_path_flag,index);let visibility=state_byte(S_path_visibility,index);
    if(closure_limit>SVM_CLOSURE_CAPACITY){svm_error(&s,16u);return s.output;}
    s.output.closure_base=closure_base;
    if(closure_base!=0u){let bytes=arrayLength(&heap)*4u;let required=SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES;
        if(closure_base%4u!=0u||closure_base>bytes||required>bytes-closure_base){svm_error(&s,1u);return s.output;}}
    else if(closure_limit!=0u){svm_error(&s,1u);return s.output;}

    if((visibility&PATH_RAY_VISIBILITY_SHADOW)==0u&&(flag&(PATH_RAY_TERMINATE|PATH_RAY_EMISSION))==0u){s.output.remaining=closure_limit;}

    for(var instructions=0u;instructions<65536u;instructions++){
        let opcode_address=svm_take(&s,4u);if(s.error!=0u){return s.output;}let opcode=load_word(opcode_address);
        if(opcode==NODE_END){s.output.N=s.geometry.N;return s.output;}
        var size=0u;
        switch(opcode){
            case NODE_SHADER_JUMP:{size=SZ_ShaderJump;}
            case NODE_JUMP_IF_ZERO:{size=SZ_JumpIfZero;}
            case NODE_JUMP_IF_ONE:{size=SZ_JumpIfOne;}
            case NODE_MATH:{size=SZ_Math;}
            case NODE_CLAMP:{size=SZ_Clamp;}
            case NODE_VALUE_F:{size=SZ_ValueF;}
            case NODE_VALUE_V:{size=SZ_ValueV;}
            case NODE_CONVERT:{size=SZ_Convert;}
            case NODE_GEOMETRY:{size=SZ_Geometry;}
            case NODE_TEX_COORD:{size=SZ_TexCoord;}
            case NODE_TEX_IMAGE,NODE_TEX_IMAGE_DERIVATIVE:{size=SZ_TexImage;}
            case NODE_TEX_IMAGE_BOX,NODE_TEX_IMAGE_BOX_DERIVATIVE:{size=SZ_TexImageBox;}
            case NODE_TEX_ENVIRONMENT,NODE_TEX_ENVIRONMENT_DERIVATIVE:{size=SZ_TexEnvironment;}
            case NODE_ATTR,NODE_ATTR_DERIVATIVE:{size=SZ_Attr;}
            case NODE_VERTEX_COLOR,NODE_VERTEX_COLOR_DERIVATIVE:{size=SZ_VertexColor;}
            case NODE_VECTOR_MATH:{size=SZ_VectorMath;}
            case NODE_SEPARATE_VECTOR:{size=SZ_SeparateVector;}
            case NODE_COMBINE_VECTOR:{size=SZ_CombineVector;}
            case NODE_CLOSURE_BSDF:{size=SZ_ClosureBsdf;}
            case NODE_CLOSURE_HOLDOUT:{size=SZ_ClosureHoldout;}
            case NODE_CLOSURE_SET_NORMAL:{size=SZ_ClosureSetNormal;}
            case NODE_CLOSURE_SET_WEIGHT:{size=SZ_ClosureSetWeight;}
            case NODE_CLOSURE_WEIGHT:{size=SZ_ClosureWeight;}
            case NODE_CLOSURE_BACKGROUND:{size=SZ_ClosureBackground;}
            case NODE_CLOSURE_EMISSION:{size=SZ_ClosureEmission;}
            case NODE_EMISSION_WEIGHT:{size=SZ_EmissionWeight;}
            case NODE_MIX_CLOSURE:{size=SZ_MixClosure;}
            case NODE_TEX_CHECKER:{size=SZ_TexChecker;}
            case NODE_TEX_GRADIENT:{size=SZ_TexGradient;}
            case NODE_GAMMA:{size=SZ_Gamma;}
            case NODE_INVERT:{size=SZ_Invert;}
            case NODE_BRIGHTCONTRAST:{size=SZ_BrightContrast;}
            default:{svm_error(&s,2u);return s.output;}
        }
        let node=svm_take(&s,size);if(s.error!=0u){return s.output;}
        switch(opcode){
            case NODE_SHADER_JUMP:{s.pc=load_word(node+N_ShaderJump_offset_surface);}
            case NODE_JUMP_IF_ZERO,NODE_JUMP_IF_ONE:{
                var offset=N_JumpIfZero_stack_offset;var jump_offset=N_JumpIfZero_jump_offset;
                if(opcode==NODE_JUMP_IF_ONE){offset=N_JumpIfOne_stack_offset;jump_offset=N_JumpIfOne_jump_offset;}
                let f=svm_load(&s,svm_byte(node+offset));if((opcode==NODE_JUMP_IF_ZERO&&f<=0.0)||(opcode==NODE_JUMP_IF_ONE&&f>=1.0)){s.pc+=load_word(node+jump_offset);}
            }
            case NODE_VALUE_F:{svm_store_bits(&s,svm_byte(node+N_ValueF_out_offset),load_word(node+N_ValueF_value));}
            case NODE_VALUE_V:{svm_store3(&s,svm_byte(node+N_ValueV_out_offset),heap_vec3(node+N_ValueV_value));}
            case NODE_GEOMETRY:{
                let kind=svm_byte(node+N_Geometry_geom_type);var v=vec3<f32>(0);
                switch(kind){
                    case NODE_GEOM_P:{v=s.geometry.P;}
                    case NODE_GEOM_N:{v=s.geometry.N;}
                    case NODE_GEOM_Ng:{v=s.geometry.Ng;}
                    case NODE_GEOM_I:{v=s.geometry.wi;}
                    case NODE_GEOM_uv:{v=vec3<f32>(1.0-s.geometry.u-s.geometry.v,s.geometry.u,0);}
                    case NODE_GEOM_T:{if(s.geometry.object_id!=NONE){svm_error(&s,16u);}else{v=normalize(s.geometry.dPdu);}}
                    default:{svm_error(&s,16u);}
                }
                svm_store3(&s,svm_byte(node+N_Geometry_out_offset),v);
            }
            case NODE_TEX_COORD:{svm_store3(&s,svm_byte(node+N_TexCoord_out_offset),svm_texco(&s,node,index));}
            case NODE_TEX_IMAGE,NODE_TEX_IMAGE_DERIVATIVE:{svm_image(&s,node,0u,opcode==NODE_TEX_IMAGE_DERIVATIVE);}
            case NODE_TEX_IMAGE_BOX,NODE_TEX_IMAGE_BOX_DERIVATIVE:{svm_image(&s,node,1u,opcode==NODE_TEX_IMAGE_BOX_DERIVATIVE);}
            case NODE_TEX_ENVIRONMENT,NODE_TEX_ENVIRONMENT_DERIVATIVE:{svm_image(&s,node,2u,opcode==NODE_TEX_ENVIRONMENT_DERIVATIVE);}
            case NODE_ATTR,NODE_ATTR_DERIVATIVE:{svm_attribute(&s,node,opcode==NODE_ATTR_DERIVATIVE);}
            case NODE_VERTEX_COLOR,NODE_VERTEX_COLOR_DERIVATIVE:{svm_vertex_color(&s,node,opcode==NODE_VERTEX_COLOR_DERIVATIVE);}
            case NODE_MATH:{
                let kind=load_word(node+N_Math_math_type);if(kind>NODE_MATH_FLOORED_MODULO){svm_error(&s,16u);}
                else{let value=c_svm_math(kind,svm_input(&s,node+N_Math_value1),svm_input(&s,node+N_Math_value2),svm_input(&s,node+N_Math_value3));svm_store(&s,svm_byte(node+N_Math_result_offset),value);}
            }
            case NODE_VECTOR_MATH:{svm_vector(&s,node);}
            case NODE_CLAMP:{
                let kind=load_word(node+N_Clamp_clamp_type);var lo=svm_input(&s,node+N_Clamp_min);var hi=svm_input(&s,node+N_Clamp_max);let v=svm_input(&s,node+N_Clamp_value);
                if(kind==NODE_CLAMP_RANGE&&lo>hi){let tmp=lo;lo=hi;hi=tmp;}
                else if(kind!=NODE_CLAMP_RANGE&&kind!=NODE_CLAMP_MINMAX){svm_error(&s,16u);}
                // Cycles permits reversed MINMAX inputs; WGSL clamp requires ordered bounds.
                svm_store(&s,svm_byte(node+N_Clamp_result_offset),min(max(v,lo),hi));
            }
            case NODE_CONVERT:{
                let kind=load_word(node+N_Convert_convert_type);let source_index=svm_byte(node+N_Convert_from_offset);let dest=svm_byte(node+N_Convert_to_offset);
                switch(kind){
                    case NODE_CONVERT_FI:{svm_store_bits(&s,dest,svm_int(&s,svm_load(&s,source_index)));}
                    case NODE_CONVERT_FV:{svm_store3(&s,dest,vec3<f32>(svm_load(&s,source_index)));}
                    case NODE_CONVERT_IF:{svm_store(&s,dest,f32(bitcast<i32>(svm_load_bits(&s,source_index))));}
                    case NODE_CONVERT_IV:{svm_store3(&s,dest,vec3<f32>(f32(bitcast<i32>(svm_load_bits(&s,source_index)))));}
                    case NODE_CONVERT_CF,NODE_CONVERT_CI,NODE_CONVERT_VF,NODE_CONVERT_VI:{
                        let v=svm_load3(&s,source_index);var f=(v.x+v.y+v.z)*(1.0/3.0);
                        if(kind==NODE_CONVERT_CF||kind==NODE_CONVERT_CI){f=dot(v,heap_vec3(p(6u)+D_film_rgb_to_y));}
                        if(kind==NODE_CONVERT_CI||kind==NODE_CONVERT_VI){svm_store_bits(&s,dest,svm_int(&s,f));}else{svm_store(&s,dest,f);}
                    }
                    default:{svm_error(&s,16u);}
                }
            }
            case NODE_SEPARATE_VECTOR:{let component=svm_byte(node+N_SeparateVector_vector_index);if(component>2u){svm_error(&s,16u);}else{let v=svm_input3(&s,node+N_SeparateVector_vector);svm_optional_store(&s,svm_byte(node+N_SeparateVector_out_offset),v[component]);}}
            case NODE_COMBINE_VECTOR:{let component=svm_byte(node+N_CombineVector_vector_index);let dest=svm_byte(node+N_CombineVector_out_offset);if(component>2u){svm_error(&s,16u);}else if(dest!=SVM_STACK_INVALID){svm_store(&s,dest+component,svm_input(&s,node+N_CombineVector_in));}}
            case NODE_CLOSURE_BSDF:{if(SVM_SURFACE){svm_bsdf(&s,node,flag);}else{svm_error(&s,2u);}}
            case NODE_CLOSURE_HOLDOUT:{
                if(!SVM_SURFACE){svm_error(&s,2u);return s.output;}
                let mix_index=svm_byte(node+N_ClosureHoldout_mix_weight_offset);let mix_weight=svm_optional(&s,mix_index,1.0);
                if(mix_index==SVM_STACK_INVALID||mix_weight!=0.0){_ = svm_allocate(&s,CLOSURE_HOLDOUT_ID,s.weight*mix_weight);s.output.runtime_flag|=SR_HOLDOUT;}
            }
            case NODE_CLOSURE_SET_NORMAL:{if(!SVM_SURFACE){svm_error(&s,2u);return s.output;}let n=svm_load3(&s,svm_byte(node+N_ClosureSetNormal_direction_offset));s.geometry.N=n;svm_store3(&s,svm_byte(node+N_ClosureSetNormal_normal_offset),n);}
            case NODE_CLOSURE_SET_WEIGHT:{s.weight=heap_vec3(node+N_ClosureSetWeight_rgb);}
            case NODE_CLOSURE_WEIGHT:{s.weight=svm_load3(&s,svm_byte(node+N_ClosureWeight_weight_offset));}
            case NODE_EMISSION_WEIGHT:{s.weight=svm_input3(&s,node+N_EmissionWeight_color)*svm_input(&s,node+N_EmissionWeight_strength);}
            case NODE_CLOSURE_BACKGROUND,NODE_CLOSURE_EMISSION:{
                let offset=select(N_ClosureBackground_mix_weight_offset,N_ClosureEmission_mix_weight_offset,opcode==NODE_CLOSURE_EMISSION);
                let mix_index=svm_byte(node+offset);let mix_weight=svm_optional(&s,mix_index,1.0);
                if(mix_index==SVM_STACK_INVALID||mix_weight!=0.0){let weight=s.weight*mix_weight;if((s.output.runtime_flag&SR_EMISSION)!=0u){s.output.emission+=weight;}else{s.output.emission=weight;s.output.runtime_flag|=SR_EMISSION;}}
            }
            case NODE_MIX_CLOSURE:{
                let f=clamp(svm_input(&s,node+N_MixClosure_fac),0.0,1.0);let weight=svm_optional(&s,svm_byte(node+N_MixClosure_in_weight_offset),1.0);
                svm_optional_store(&s,svm_byte(node+N_MixClosure_weight1_offset),weight*(1.0-f));svm_optional_store(&s,svm_byte(node+N_MixClosure_weight2_offset),weight*f);
            }
            case NODE_TEX_CHECKER:{
                let co=svm_load3(&s,svm_byte(node+N_TexChecker_co))*svm_input(&s,node+N_TexChecker_scale);
                let adjusted=(co+vec3<f32>(0.000001))*0.999999;let parity=abs(vec3<i32>(floor(adjusted)))%vec3<i32>(2);
                let f=select(0.0,1.0,i32(parity.x==parity.y)==parity.z);
                let color1=svm_input3(&s,node+N_TexChecker_color1);let color2=svm_input3(&s,node+N_TexChecker_color2);
                svm_optional_store3(&s,svm_byte(node+N_TexChecker_color_offset),select(color2,color1,f==1.0));svm_optional_store(&s,svm_byte(node+N_TexChecker_fac_offset),f);
            }
            case NODE_TEX_GRADIENT:{let f=clamp(svm_gradient(&s,load_word(node+N_TexGradient_gradient_type),svm_load3(&s,svm_byte(node+N_TexGradient_co))),0.0,1.0);
                svm_optional_store(&s,svm_byte(node+N_TexGradient_fac_offset),f);svm_optional_store3(&s,svm_byte(node+N_TexGradient_color_offset),vec3<f32>(f));}
            case NODE_GAMMA:{svm_optional_store3(&s,svm_byte(node+N_Gamma_out_offset),c_svm_math_gamma_color(svm_input3(&s,node+N_Gamma_color),svm_input(&s,node+N_Gamma_gamma)));}
            case NODE_INVERT:{let color=svm_input3(&s,node+N_Invert_color);let f=svm_input(&s,node+N_Invert_fac);svm_optional_store3(&s,svm_byte(node+N_Invert_out_offset),f*(vec3<f32>(1)-color)+(1.0-f)*color);}
            case NODE_BRIGHTCONTRAST:{let color=svm_input3(&s,node+N_BrightContrast_color);let bright=svm_input(&s,node+N_BrightContrast_bright);let contrast=svm_input(&s,node+N_BrightContrast_contrast);
                svm_optional_store3(&s,svm_byte(node+N_BrightContrast_out_offset),max((1.0+contrast)*color+vec3<f32>(bright-contrast*0.5),vec3<f32>(0)));}
            default:{svm_error(&s,2u);}
        }
        if(s.error!=0u){return s.output;}
    }
    svm_error(&s,32u);return s.output;
}

fn svm_world(index:u32,shader_id:u32)->vec3<f32>{
    var sd:SurfaceData;let direction=heap_vec3(state_address(S_ray_D,index,12u));
    sd.P=direction;sd.N=-direction;sd.Ng=-direction;sd.wi=-direction;sd.object_id=NONE;sd.prim=NONE;
    sd.shader=shader_id;sd.shader_flag=load_word(p(18u)+shader_id*SIZE_shaders+SH_flags);
    sd.dPdu=surface_orthogonal(sd.Ng);sd.dPdv=cross(sd.Ng,sd.dPdu);
    sd.time=heap_float(state_address(S_ray_time,index,4u));sd.ray_length=bitcast<f32>(0x7f7fffffu);
    sd.dP=heap_float(state_address(S_ray_dD,index,4u));sd.dI=sd.dP;sd.du=vec2<f32>(sd.dP,0);sd.dv=vec2<f32>(0,sd.dP);
    return svm_evaluate(index,sd,0u,0u).emission;
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles geom/attribute.h, geom/triangle.h, geom/primitive.h,
// svm/attribute.h, svm/vertex_color.h, svm/util.h, util/color.h and
// kernel/util/colorspace.h. Native layouts, bounded virtual-address loads and
// explicit unsupported-domain errors replace native pointers/templates.
struct AttributeDesc {element:u32, kind:u32, offset:i32, found:bool,}
struct AttributeValue {value:vec4<f32>, dx:vec4<f32>, dy:vec4<f32>,}

fn attribute_find(s:ptr<function,SvmState>,id_low:u32,id_high:u32)->AttributeDesc{
    var desc:AttributeDesc;
    let sd=(*s).geometry;if(sd.object_id==NONE){return desc;}
    if(sd.object_id>=table_count(G_objects)){svm_error(s,64u);return desc;}
    var at=load_word(table_at(G_objects,sd.object_id,SIZE_objects)+O_attribute_map_offset);
    let count=table_count(G_attributes_map);
    // A valid chain cannot visit more entries than the complete map contains.
    for(var steps=0u;steps<count;steps++){
        if(at>=count){svm_error(s,64u);return desc;}
        let address=table_at(G_attributes_map,at,SIZE_attributes_map);
        let lo=load_word(address+AM_id);let hi=load_word(address+AM_id+4u);
        let element=svm_byte(address+AM_element)|(svm_byte(address+AM_element+1u)<<8u);
        let offset=load_word(address+AM_offset);
        if(lo==id_low&&hi==id_high){
            desc.element=element;desc.kind=svm_byte(address+AM_type);desc.offset=bitcast<i32>(offset);
            desc.found=element!=ATTR_ELEMENT_NONE&&offset!=ATTR_STD_NOT_FOUND;
            if(sd.prim==NONE&&(element&(ATTR_ELEMENT_OBJECT|ATTR_ELEMENT_MESH|ATTR_ELEMENT_VOXEL))==0u){desc.found=false;}
            return desc;
        }
        if(lo==ATTR_STD_NONE&&hi==0u){
            if(element==0u){return desc;}at=offset;
        }else{
            if(at>0xffffffffu-ATTR_PRIM_TYPES){svm_error(s,64u);return desc;}at+=ATTR_PRIM_TYPES;
        }
    }
    svm_error(s,64u);return desc;
}
fn attribute_srgb(c:f32)->f32{
    if(c<0.04045){return max(c,0.0)*(1.0/12.92);}
    return pow((c+0.055)*(1.0/1.055),2.4);
}
fn attribute_fetch(s:ptr<function,SvmState>,desc:AttributeDesc,index:u32)->vec4<f32>{
    // Native offsets may be negative. Avoid signed overflow before conversion.
    var at=0u;
    if(desc.offset<0){let magnitude=0u-bitcast<u32>(desc.offset);if(index<magnitude){svm_error(s,64u);return vec4<f32>(0);}at=index-magnitude;}
    else{let offset=u32(desc.offset);if(index>0xffffffffu-offset){svm_error(s,64u);return vec4<f32>(0);}at=index+offset;}
    var table=G_attributes_float;var stride=SIZE_attributes_float;
    switch(desc.kind){
        case NODE_ATTR_FLOAT:{}
        case NODE_ATTR_FLOAT2:{table=G_attributes_float2;stride=SIZE_attributes_float2;}
        case NODE_ATTR_FLOAT3:{table=G_attributes_float3;stride=SIZE_attributes_float3;
            if((desc.element&ATTR_ELEMENT_IS_NORMAL)!=0u){table=G_attributes_normal;stride=SIZE_attributes_normal;}}
        case NODE_ATTR_FLOAT4,NODE_ATTR_RGBA:{table=G_attributes_float4;stride=SIZE_attributes_float4;
            if((desc.element&ATTR_ELEMENT_IS_BYTE)!=0u){table=G_attributes_uchar4;stride=SIZE_attributes_uchar4;}}
        default:{svm_error(s,16u);return vec4<f32>(0);}
    }
    if(at>=table_count(table)){svm_error(s,64u);return vec4<f32>(0);}
    let address=table_at(table,at,stride);
    if(table==G_attributes_float){return vec4<f32>(heap_float(address),0,0,0);}
    if(table==G_attributes_float2){return vec4<f32>(heap_float(address),heap_float(address+4u),0,0);}
    if(table==G_attributes_float3){return vec4<f32>(heap_vec3(address),0);}
    if(table==G_attributes_normal){return vec4<f32>(surface_decode_normal(load_word(address)),0);}
    if(table==G_attributes_uchar4){
        let bits=load_word(address);let c=vec4<f32>(f32(bits&255u),f32((bits>>8u)&255u),f32((bits>>16u)&255u),f32(bits>>24u))*(1.0/255.0);
        var rgb=vec3<f32>(attribute_srgb(c.x),attribute_srgb(c.y),attribute_srgb(c.z));
        if(data_word(D_film_is_rec709)==0u){rgb=vec3<f32>(dot(heap_vec3(p(6u)+D_film_rec709_to_r),rgb),dot(heap_vec3(p(6u)+D_film_rec709_to_g),rgb),dot(heap_vec3(p(6u)+D_film_rec709_to_b),rgb));}
        return vec4<f32>(rgb,c.w);
    }
    return heap_vec4(address);
}
fn attribute_read(s:ptr<function,SvmState>,desc:AttributeDesc)->AttributeValue{
    var result:AttributeValue;if(!desc.found){return result;}
    let sd=(*s).geometry;
    let domain=desc.element&~(ATTR_ELEMENT_IS_BYTE|ATTR_ELEMENT_IS_NORMAL);
    if((desc.element&ATTR_ELEMENT_IS_BYTE)!=0u&&desc.kind!=NODE_ATTR_RGBA&&desc.kind!=NODE_ATTR_FLOAT4){svm_error(s,16u);return result;}
    if((desc.element&ATTR_ELEMENT_IS_NORMAL)!=0u&&desc.kind!=NODE_ATTR_FLOAT3){svm_error(s,16u);return result;}
    if(domain==ATTR_ELEMENT_OBJECT||domain==ATTR_ELEMENT_MESH){result.value=attribute_fetch(s,desc,0u);return result;}
    if(sd.primitive_type!=PRIMITIVE_TRIANGLE){svm_error(s,16u);return result;}
    if(domain==ATTR_ELEMENT_FACE){result.value=attribute_fetch(s,desc,sd.prim);return result;}
    var indices:vec3<u32>;
    if(domain==ATTR_ELEMENT_VERTEX){
        if(sd.prim>=table_count(G_tri_vindex)){svm_error(s,64u);return result;}
        let address=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);indices=vec3<u32>(load_word(address),load_word(address+4u),load_word(address+8u));
    }else if(domain==ATTR_ELEMENT_CORNER){
        if(sd.prim>(0xffffffffu-2u)/3u){svm_error(s,64u);return result;}indices=vec3<u32>(sd.prim*3u)+vec3<u32>(0,1,2);
    }else{svm_error(s,16u);return result;}
    let a=attribute_fetch(s,desc,indices.x);let b=attribute_fetch(s,desc,indices.y);let c=attribute_fetch(s,desc,indices.z);
    result.value=((1.0-sd.u)-sd.v)*a+sd.u*b+sd.v*c;
    result.dx=sd.du.x*b+sd.dv.x*c-(sd.du.x+sd.dv.x)*a;
    result.dy=sd.du.y*b+sd.dv.y*c-(sd.du.y+sd.dv.y)*a;
    return result;
}
fn attribute_convert(value:vec4<f32>,kind:u32,output:u32,derivative:bool)->vec3<f32>{
    if(kind==NODE_ATTR_FLOAT){
        if(output==NODE_ATTR_OUTPUT_FLOAT_ALPHA){return vec3<f32>(select(1.0,0.0,derivative));}return vec3<f32>(value.x);
    }
    if(kind==NODE_ATTR_FLOAT2){
        if(output==NODE_ATTR_OUTPUT_FLOAT){return vec3<f32>(value.x);}
        if(output==NODE_ATTR_OUTPUT_FLOAT_ALPHA){return vec3<f32>(select(1.0,0.0,derivative));}return vec3<f32>(value.xy,0);
    }
    if(output==NODE_ATTR_OUTPUT_FLOAT_ALPHA){
        if(kind==NODE_ATTR_FLOAT4||kind==NODE_ATTR_RGBA){return vec3<f32>(value.w);}return vec3<f32>(select(1.0,0.0,derivative));
    }
    if(output==NODE_ATTR_OUTPUT_FLOAT){return vec3<f32>((value.x+value.y+value.z)*(1.0/3.0));}
    return value.xyz;
}
fn attribute_store(s:ptr<function,SvmState>,offset:u32,output:u32,value:vec3<f32>){
    if(output==NODE_ATTR_OUTPUT_FLOAT3){svm_store3(s,offset,value);}else{svm_store(s,offset,(value.x+value.y+value.z)*(1.0/3.0));}
}
fn svm_attribute(s:ptr<function,SvmState>,node:u32,derivative:bool){
    let id=load_word(node+N_Attr_attr);let output=svm_byte(node+N_Attr_output_type);
    let bump=svm_byte(node+N_Attr_bump_offset);let store=svm_byte(node+N_Attr_store_derivatives);
    if(output>NODE_ATTR_OUTPUT_FLOAT_ALPHA||(derivative&&(bump>NODE_BUMP_OFFSET_DY||store>1u))){svm_error(s,16u);return;}
    var desc=attribute_find(s,id,select(0u,0xffffffffu,(id&0x80000000u)!=0u));
    if(!desc.found){desc.kind=output;}
    var value:vec3<f32>;var dx:vec3<f32>;var dy:vec3<f32>;
    if(id==ATTR_STD_GENERATED&&!desc.found){
        let sd=(*s).geometry;value=sd.P;dx=sd.dPdu*sd.du.x+sd.dPdv*sd.dv.x;dy=sd.dPdu*sd.du.y+sd.dPdv*sd.dv.y;
        if(sd.object_id!=NONE){let object=table_at(G_objects,sd.object_id,SIZE_objects);
            value=surface_transform_point(object+O_itfm,value);dx=surface_transform_direction(object+O_itfm,dx);dy=surface_transform_direction(object+O_itfm,dy);}
    }else{
        let data=attribute_read(s,desc);value=attribute_convert(data.value,desc.kind,output,false);
        dx=attribute_convert(data.dx,desc.kind,output,true);dy=attribute_convert(data.dy,desc.kind,output,true);
    }
    if((*s).error!=0u){return;}
    if(derivative){let width=heap_float(node+N_Attr_bump_filter_width);
        if(bump==NODE_BUMP_OFFSET_DX){value+=dx*width;}else if(bump==NODE_BUMP_OFFSET_DY){value+=dy*width;}}
    let offset=svm_byte(node+N_Attr_out_offset);attribute_store(s,offset,output,value);
    if(derivative&&store!=0u){let stride=select(1u,3u,output==NODE_ATTR_OUTPUT_FLOAT3);attribute_store(s,offset+stride,output,dx);attribute_store(s,offset+2u*stride,output,dy);}
}
fn svm_vertex_color(s:ptr<function,SvmState>,node:u32,derivative:bool){
    let desc=attribute_find(s,svm_byte(node+N_VertexColor_layer_id),0u);
    let bump=svm_byte(node+N_VertexColor_bump_offset);
    if(derivative&&bump>NODE_BUMP_OFFSET_DY){svm_error(s,16u);return;}
    var color=vec3<f32>(0);var alpha=0.0;
    if(desc.found){
        if(desc.kind!=NODE_ATTR_FLOAT3&&desc.kind!=NODE_ATTR_FLOAT4&&desc.kind!=NODE_ATTR_RGBA){svm_error(s,16u);return;}
        var data=attribute_read(s,desc);
        if(derivative){let width=heap_float(node+N_VertexColor_bump_filter_width);
            if(bump==NODE_BUMP_OFFSET_DX){data.value+=data.dx*width;}else if(bump==NODE_BUMP_OFFSET_DY){data.value+=data.dy*width;}}
        color=data.value.xyz;alpha=select(1.0,data.value.w,desc.kind==NODE_ATTR_FLOAT4||desc.kind==NODE_ATTR_RGBA);
    }
    if((*s).error!=0u){return;}
    svm_optional_store3(s,svm_byte(node+N_VertexColor_color_offset),color);svm_optional_store(s,svm_byte(node+N_VertexColor_alpha_offset),alpha);
}
// SPDX-FileCopyrightText: 2011-2026 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Adapted from pinned Cycles kernel/device/cpu/image.h, kernel/util/image_2d.h,
// kernel/svm/image.h, kernel/camera/projection.h and util/math_float3.h.
// Resident native images use checked virtual addresses and software sampling.
struct ImageInfo {data:u32, kind:u32, width:i32, height:i32, interpolation:u32, extension:u32,}
struct ImageFraction {index:i32, weight:f32,}
fn image_fraction(x:f32)->ImageFraction{
    // Preserve the reference's negative-integer convention, including closest.
    let i=i32(x)-select(0,1,x<0.0);return ImageFraction(i,x-f32(i));
}
fn image_wrap(x:i32,width:i32,mode:u32)->i32{
    if(mode==EXTENSION_REPEAT){let m=x%width;return select(m,m+width,m<0);}
    if(mode==EXTENSION_EXTEND){return clamp(x,0,width-1);}
    if(mode==EXTENSION_MIRROR){let m=abs(x+select(0,1,x<0))%(2*width);return select(m,2*width-m-1,m>=width);}
    return x;
}
fn image_read(info:ImageInfo,x:i32,y:i32)->vec4<f32>{
    let ix=image_wrap(x,info.width,info.extension);let iy=image_wrap(y,info.height,info.extension);
    if(ix<0||ix>=info.width||iy<0||iy>=info.height){return vec4<f32>(0);}
    let pixel=u32(iy)*u32(info.width)+u32(ix);let kind=info.kind;
    if(kind==IMAGE_DATA_TYPE_FLOAT4){return heap_vec4(info.data+pixel*16u);}
    if(kind==IMAGE_DATA_TYPE_FLOAT){return vec4<f32>(heap_float(info.data+pixel*4u));}
    if(kind==IMAGE_DATA_TYPE_BYTE4){let bits=load_word(info.data+pixel*4u);return vec4<f32>(f32(bits&255u),f32((bits>>8u)&255u),f32((bits>>16u)&255u),f32(bits>>24u))*(1.0/255.0);}
    if(kind==IMAGE_DATA_TYPE_BYTE){return vec4<f32>(f32(svm_byte(info.data+pixel))*(1.0/255.0));}
    if(kind==IMAGE_DATA_TYPE_HALF){let a=info.data+pixel*2u;return vec4<f32>(unpack2x16float(load_word(a))[(a%4u)/2u]);}
    if(kind==IMAGE_DATA_TYPE_HALF4){let a=info.data+pixel*8u;return vec4<f32>(unpack2x16float(load_word(a)),unpack2x16float(load_word(a+4u)));}
    if(kind==IMAGE_DATA_TYPE_USHORT){return vec4<f32>(f32(load_half(info.data+pixel*2u))*(1.0/65535.0));}
    let a=info.data+pixel*8u;return vec4<f32>(f32(load_half(a)),f32(load_half(a+2u)),f32(load_half(a+4u)),f32(load_half(a+6u)))*(1.0/65535.0);
}
fn image_cubic_weights(t:f32)->vec4<f32>{
    return vec4<f32>((((-1.0/6.0)*t+0.5)*t-0.5)*t+(1.0/6.0),((0.5*t-1.0)*t)*t+(2.0/3.0),((-0.5*t+0.5)*t+0.5)*t+(1.0/6.0),(1.0/6.0)*t*t*t);
}
fn image_interpolate(info:ImageInfo,xy:vec2<f32>)->vec4<f32>{
    if(info.interpolation==INTERPOLATION_CLOSEST){return image_read(info,image_fraction(xy.x).index,image_fraction(xy.y).index);}
    let fx=image_fraction(xy.x-0.5);let fy=image_fraction(xy.y-0.5);let ix=fx.index;let iy=fy.index;let tx=fx.weight;let ty=fy.weight;
    if(info.interpolation==INTERPOLATION_LINEAR){
        return (1.0-ty)*(1.0-tx)*image_read(info,ix,iy)+(1.0-ty)*tx*image_read(info,ix+1,iy)+ty*(1.0-tx)*image_read(info,ix,iy+1)+ty*tx*image_read(info,ix+1,iy+1);
    }
    let u=image_cubic_weights(tx);let v=image_cubic_weights(ty);var result=vec4<f32>(0);
    for(var j=0;j<4;j++){
        let y=iy+j-1;
        result+=v[u32(j)]*(u.x*image_read(info,ix-1,y)+u.y*image_read(info,ix,y)+u.z*image_read(info,ix+1,y)+u.w*image_read(info,ix+2,y));
    }
    return result;
}
fn image_sample(s:ptr<function,SvmState>,input_id:i32,input_uv:vec2<f32>)->vec4<f32>{
    var id=input_id;var uv=input_uv;
    if(!all(abs(uv)<vec2<f32>(1.0e9))){svm_error(s,128u);return vec4<f32>(0);}
    if(id<0){
        let tx=i32(uv.x);let ty=i32(uv.y);if(tx<0||ty<0||tx>=10){return vec4<f32>(1,0,1,1);}
        // IDs are encoded as -tableOffset-1; unsigned arithmetic avoids INT_MIN overflow.
        let at=0u-bitcast<u32>(id)-1u;let count=table_count(G_image_texture_udims);
        if(at>=count||ty>(2147483647-1010)/10){svm_error(s,128u);return vec4<f32>(0);}
        let address=table_at(G_image_texture_udims,at,SIZE_image_texture_udims);let n=load_word(address+IU_tile);
        if(n>count-at-1u){svm_error(s,128u);return vec4<f32>(0);}
        id=i32(KERNEL_IMAGE_NONE);let tile=1001+10*ty+tx;
        for(var i=0u;i<n;i++){
            let entry=table_at(G_image_texture_udims,at+1u+i,SIZE_image_texture_udims);
            if(bitcast<i32>(load_word(entry+IU_tile))==tile){id=bitcast<i32>(load_word(entry+IU_image_texture_id));uv-=vec2<f32>(f32(tx),f32(ty));break;}
        }
    }
    if(id==i32(KERNEL_IMAGE_NONE)){return vec4<f32>(1,0,1,1);}
    if(id<0||u32(id)>=table_count(G_image_textures)){svm_error(s,128u);return vec4<f32>(0);}
    let tex=table_at(G_image_textures,u32(id),SIZE_image_textures);
    if(load_word(tex+IT_tile_descriptor_offset)!=KERNEL_TILE_LOAD_NONE){svm_error(s,256u);return vec4<f32>(0);}
    if(load_word(tex+IT_use_transform_3d)!=0u){svm_error(s,16u);return vec4<f32>(0);}
    let info_id=load_word(tex+IT_image_info_id);if(info_id==KERNEL_IMAGE_NONE){return vec4<f32>(1,0,1,1);}
    if(info_id>=table_count(G_image_info)||info_id>=table_count(G_image_bounds)){svm_error(s,128u);return vec4<f32>(0);}
    let a=table_at(G_image_info,info_id,SIZE_image_info);let bounds=table_at(G_image_bounds,info_id,SIZE_image_bounds);
    var info:ImageInfo;info.data=load_word(a+II_data);info.kind=load_word(a+II_data_type);
    if(load_word(a+II_data+4u)!=0u){svm_error(s,128u);return vec4<f32>(0);}
    if(info.data==0u){return vec4<f32>(0);}
    info.width=bitcast<i32>(load_word(a+II_width));info.height=bitcast<i32>(load_word(a+II_height));
    info.interpolation=load_word(a+II_interpolation);info.extension=load_word(a+II_extension);
    if(info.kind>IMAGE_DATA_TYPE_USHORT||info.width<=0||info.height<=0||info.width>1073741823||info.height>1073741823||info.interpolation>=INTERPOLATION_NUM_TYPES||info.extension>=EXTENSION_NUM_TYPES){svm_error(s,128u);return vec4<f32>(0);}
    var pixel_bytes=4u;
    switch(info.kind){case IMAGE_DATA_TYPE_FLOAT4:{pixel_bytes=16u;}case IMAGE_DATA_TYPE_HALF4,IMAGE_DATA_TYPE_USHORT4:{pixel_bytes=8u;}case IMAGE_DATA_TYPE_BYTE:{pixel_bytes=1u;}case IMAGE_DATA_TYPE_HALF,IMAGE_DATA_TYPE_USHORT:{pixel_bytes=2u;}default:{}}
    let bytes=load_word(bounds+4u);
    if(info.data!=load_word(bounds)||bytes/pixel_bytes/u32(info.width)<u32(info.height)){svm_error(s,128u);return vec4<f32>(0);}
    let xy=uv*vec2<f32>(f32(info.width),f32(info.height));
    if(!all(abs(xy)<vec2<f32>(2147483000.0))){svm_error(s,128u);return vec4<f32>(0);}
    var result=image_interpolate(info,xy);
    if(info.kind==IMAGE_DATA_TYPE_FLOAT||info.kind==IMAGE_DATA_TYPE_HALF||info.kind==IMAGE_DATA_TYPE_BYTE||info.kind==IMAGE_DATA_TYPE_USHORT){result.w=1.0;}
    return result;
}
fn svm_image_sample(s:ptr<function,SvmState>,id:i32,uv:vec2<f32>,flags:u32)->vec4<f32>{
    if((flags&~(NODE_IMAGE_ALPHA_UNASSOCIATE|NODE_IMAGE_COMPRESS_AS_SRGB))!=0u){svm_error(s,16u);return vec4<f32>(0);}
    var result=image_sample(s,id,uv);let alpha=result.w;
    if((flags&NODE_IMAGE_ALPHA_UNASSOCIATE)!=0u&&alpha!=1.0&&alpha!=0.0){result/=alpha;result.w=alpha;}
    if((flags&NODE_IMAGE_COMPRESS_AS_SRGB)!=0u){result=vec4<f32>(attribute_srgb(result.x),attribute_srgb(result.y),attribute_srgb(result.z),alpha);}
    return result;
}
fn image_projection(s:ptr<function,SvmState>,co:vec3<f32>,projection:u32)->vec2<f32>{
    if(projection==NODE_IMAGE_PROJ_FLAT){return co.xy;}
    let v=(co-vec3<f32>(0.5))*2.0;
    if(projection==NODE_IMAGE_PROJ_SPHERE){
        let l=dot(v,v);if(l<=0.0){return vec2<f32>(0);}
        var u=0.0;if(v.x!=0.0||v.y!=0.0){u=0.5-atan2(v.x,v.y)*0.15915494;}
        return vec2<f32>(u,1.0-c_safe_acosf(v.z/sqrt(l))*0.31830987);
    }
    if(projection==NODE_IMAGE_PROJ_TUBE){let l=sqrt(v.x*v.x+v.y*v.y);if(l>0.0){return vec2<f32>((1.0-atan2(v.x/l,v.y/l)/3.1415927)*0.5,(v.z+1.0)*0.5);}return vec2<f32>(0);}
    svm_error(s,16u);return vec2<f32>(0);
}
fn environment_projection(s:ptr<function,SvmState>,input:vec3<f32>,projection:u32)->vec2<f32>{
    var v=surface_normalize(input);
    if(projection==0u){if(all(v==vec3<f32>(0))){return vec2<f32>(0);}return vec2<f32>((atan2(v.y,v.x)-3.1415927)/-6.2831855,(c_safe_acosf(v.z/length(v))-3.1415927)/-3.1415927);}
    if(projection!=1u){svm_error(s,16u);return vec2<f32>(0);}
    v.y-=1.0;let div=2.0*sqrt(max(-0.5*v.y,0.0));if(div>0.0){v/=div;}return (v.xz+vec2<f32>(1))*0.5;
}
fn image_box(s:ptr<function,SvmState>,co:vec3<f32>,id:i32,blend:f32,flags:u32)->vec4<f32>{
    var n=(*s).geometry.N;let object=(*s).geometry.object_id;
    if(object!=NONE){n=surface_transform_normal(table_at(G_objects,object,SIZE_objects)+O_tfm,n);}
    let signed_n=n;n=abs(n);n/=(n.x+n.y+n.z);let limit=0.5*(1.0+blend);var weight=vec3<f32>(0);
    if(n.x>limit*(n.x+n.y)&&n.x>limit*(n.x+n.z)){weight.x=1.0;}
    else if(n.y>limit*(n.x+n.y)&&n.y>limit*(n.y+n.z)){weight.y=1.0;}
    else if(n.z>limit*(n.x+n.z)&&n.z>limit*(n.y+n.z)){weight.z=1.0;}
    else if(blend>0.0){
        if(n.z<(1.0-limit)*(n.y+n.x)){weight.x=clamp((n.x/(n.x+n.y)-0.5*(1.0-blend))/blend,0.0,1.0);weight.y=1.0-weight.x;}
        else if(n.x<(1.0-limit)*(n.y+n.z)){weight.y=clamp((n.y/(n.y+n.z)-0.5*(1.0-blend))/blend,0.0,1.0);weight.z=1.0-weight.y;}
        else if(n.y<(1.0-limit)*(n.x+n.z)){weight.x=clamp((n.x/(n.x+n.z)-0.5*(1.0-blend))/blend,0.0,1.0);weight.z=1.0-weight.x;}
        else{weight=((2.0-limit)*n+vec3<f32>(limit-1.0))/(2.0*limit-1.0);}
    }else{weight.x=1.0;}
    var result=vec4<f32>(0);
    if(weight.x>0.0){result+=weight.x*svm_image_sample(s,id,vec2<f32>(select(co.y,1.0-co.y,signed_n.x<0.0),co.z),flags);}
    if(weight.y>0.0){result+=weight.y*svm_image_sample(s,id,vec2<f32>(select(co.x,1.0-co.x,signed_n.y>0.0),co.z),flags);}
    if(weight.z>0.0){result+=weight.z*svm_image_sample(s,id,vec2<f32>(select(co.y,1.0-co.y,signed_n.z>0.0),co.x),flags);}
    return result;
}
fn svm_image(s:ptr<function,SvmState>,node:u32,mode:u32,derivative:bool){
    // These native records have matching offsets, checked by the layout generator.
    let offset=svm_byte(node+N_TexImage_co);let co=svm_load3(s,offset);
    if(derivative){let dx=svm_load3(s,offset+3u);let dy=svm_load3(s,offset+6u);
        if(!all(abs(dx)<=vec3<f32>(3.402823e38))||!all(abs(dy)<=vec3<f32>(3.402823e38))){svm_error(s,128u);}}
    if((*s).error!=0u){return;}
    let id=bitcast<i32>(load_word(node+N_TexImage_id));let flags=svm_byte(node+N_TexImage_flags);var result:vec4<f32>;
    if(mode==1u){result=image_box(s,co,id,heap_float(node+N_TexImageBox_blend),flags);}
    else{var uv:vec2<f32>;let projection=load_word(node+N_TexImage_projection);
        if(mode==2u){uv=environment_projection(s,co,projection);}else{uv=image_projection(s,co,projection);}
        if((*s).error==0u){result=svm_image_sample(s,id,uv,flags);}}
    if((*s).error!=0u){return;}
    svm_optional_store3(s,svm_byte(node+N_TexImage_out_offset),result.xyz);svm_optional_store(s,svm_byte(node+N_TexImage_alpha_offset),result.w);
}
const D_background_surface_shader:u32=1364u;
const D_background_transparent:u32=1372u;
const D_background_lightgroup:u32=1408u;
const D_integrator_ao_bounces_factor:u32=1952u;
const D_integrator_sample_clamp_direct:u32=1988u;
const D_integrator_sample_clamp_indirect:u32=1992u;
const D_film_pass_flag:u32=1592u;
const D_film_light_pass_flag:u32=1600u;
const D_film_pass_combined:u32=1608u;
const D_film_pass_background:u32=1720u;
const D_film_pass_diffuse_direct:u32=1676u;
const D_film_pass_glossy_direct:u32=1680u;
const D_film_pass_transmission_direct:u32=1684u;
const D_film_pass_diffuse_indirect:u32=1660u;
const D_film_pass_glossy_indirect:u32=1664u;
const D_film_pass_transmission_indirect:u32=1668u;
const D_film_pass_lightgroup:u32=1824u;
const D_film_pass_depth:u32=1612u;
const D_film_pass_position:u32=1616u;
const D_film_pass_object_id:u32=1640u;
const D_film_pass_material_id:u32=1644u;
const SH_constant_emission:u32=0u;
const SD_HAS_CONSTANT_EMISSION:u32=134217728u;
const SHADER_EXCLUDE_DIFFUSE:u32=134217728u;
const SHADER_EXCLUDE_GLOSSY:u32=67108864u;
const SHADER_EXCLUDE_TRANSMIT:u32=33554432u;
const SHADER_EXCLUDE_CAMERA:u32=16777216u;
const PATH_RAY_REFLECT:u32=1u;
const PATH_RAY_TRANSPARENT_BACKGROUND:u32=512u;
const PATH_RAY_ANY_PASS:u32=786432u;
const PATH_RAY_SURFACE_PASS:u32=262144u;
const PATH_RAY_SINGLE_PASS_DONE:u32=256u;
const BACKGROUND_UNSUPPORTED_FLAGS:u32=32112640u;
const PATH_RAY_VISIBILITY_VOLUME_SCATTER:u32=16u;
const MASK_COMBINED:u32=2u;
const MASK_BACKGROUND:u32=8u;
const MASK_DEPTH:u32=1u;
const MASK_POSITION:u32=2u;
const MASK_OBJECT_ID:u32=32u;
const MASK_MATERIAL_ID:u32=64u;
const D_integrator_transparent_max_bounce:u32=1964u;
const D_integrator_max_bounce:u32=1924u;
const D_integrator_max_diffuse_bounce:u32=1928u;
const D_integrator_max_transmission_bounce:u32=1936u;
const D_background_transparent_roughness_squared_threshold:u32=1376u;
const D_film_pass_emission:u32=1716u;
const O_lightgroup:u32=196u;
const O_shadow_terminator_shading_offset:u32=184u;
const LABEL_NONE:u32=0u;
const LABEL_REFLECT:u32=2u;
const LABEL_TRANSMIT:u32=1u;
const LABEL_DIFFUSE:u32=4u;
const LABEL_TRANSPARENT:u32=32u;
const LABEL_TRANSMIT_TRANSPARENT:u32=128u;
const PATH_RAY_MIS_SKIP:u32=128u;
const PATH_RAY_SINGULAR:u32=2u;
const PATH_RAY_IMPORTANCE_BAKE:u32=8u;
const PATH_RAY_MIS_HAD_TRANSMISSION:u32=64u;
const PATH_RAY_DIFFUSE_ANCESTOR:u32=16u;
const FILTER_CLOSURE_EMISSION:u32=1u;
const FILTER_CLOSURE_DIFFUSE:u32=2u;
const CLOSURE_NONE_ID:u32=0u;
const NBUILTIN_CLOSURES:u32=44u;
const FILTER_CLOSURE_TRANSPARENT:u32=16u;
const SD_OBJECT_HOLDOUT_MASK:u32=1u;
const SD_HAS_ONLY_VOLUME:u32=524288u;
const SD_MIS_FRONT:u32=65536u;
const SD_MIS_BACK:u32=2147483648u;
const PRNG_SURFACE_BSDF:u32=3u;
const SURFACE_UNSUPPORTED_FLAGS:u32=32227328u;
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
// Manually adapted from Cycles bsdf_diffuse.h and sample/mapping.h.
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Adapted code from Open Shading Language.
// Oren-Nayar, allocation and SVM adaptation: Blender Foundation, Apache-2.0.
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: BSD-3-Clause AND Apache-2.0
// Typed WGSL records replace C++ closure-pointer casts. See THIRD-PARTY-NOTICES.txt.
const PI=3.14159265358979323846f;
const INV_PI=0.31830988618379067154f;
// Original numerical adapter: bounded-angle Taylor polynomials. Disk sampling
// only supplies angles in [-pi/4, 3*pi/4]. This avoids the loose native sin/cos
// accuracy observed on some shipped WebGPU drivers without changing WebGPU.
fn disk_sincos(angle: f32) -> vec2<f32> {
    var x=angle; var cos_sign=1.0;
    if (x>PI*.5) { x=(3.141592502593994140625-x)+1.509957990978376432e-7; cos_sign=-1.0; }
    let z=x*x;
    var s=1.6059043836821613e-10;
    s=fma(s,z,-2.505210838544172e-8); s=fma(s,z,2.7557319223985893e-6);
    s=fma(s,z,-.0001984126984126984); s=fma(s,z,.008333333333333333);
    s=fma(s,z,-.16666666666666666);
    let sine=fma(x*z,s,x);
    var c=-1.1470745597729725e-11;
    c=fma(c,z,2.08767569878681e-9); c=fma(c,z,-2.755731922398589e-7);
    c=fma(c,z,.0000248015873015873); c=fma(c,z,-.001388888888888889);
    c=fma(c,z,.041666666666666664); c=fma(c,z,-.5);
    return vec2<f32>(sine,fma(c,z,1.0)*cos_sign);
}
fn disk_sample(r: vec2<f32>) -> vec2<f32> {
    let a=2.0*r.x-1.0; let b=2.0*r.y-1.0;
    if (a==0.0 && b==0.0) { return vec2<f32>(0.0); }
    var radius: f32; var phi: f32;
    if (a*a>b*b) { radius=a; phi=(PI*.25)*(b/a); }
    else { radius=b; phi=PI*.5-(PI*.25)*(a/b); }
    let sc=disk_sincos(phi);
    return vec2<f32>(radius*sc.y,radius*sc.x);
}
fn hemisphere(n: vec3<f32>, r: vec2<f32>) -> vec4<f32> {
    let d=disk_sample(r); let cosine=sqrt(max(1.0-dot(d,d),0.0));
    var t: vec3<f32>;
    if (n.x!=n.y || n.x!=n.z) { t=vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x); }
    else { t=vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x); }
    t=normalize(t); let b=cross(n,t);
    return vec4<f32>(d.x*t+d.y*b+cosine*n,cosine*INV_PI);
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from Cycles: selected surface integration, BSDF mixtures, native
// path-state updates and film writes in WGSL. See native/THIRD-PARTY-NOTICES.txt.
fn surf_diffuse(kind:u32)->bool{return kind==CLOSURE_BSDF_DIFFUSE_ID||kind==CLOSURE_BSDF_OREN_NAYAR_ID||kind==CLOSURE_BSDF_TRANSLUCENT_ID;}
fn surf_bsdf(kind:u32)->bool{return surf_diffuse(kind)||kind==CLOSURE_BSDF_TRANSLUCENT_ID||kind==CLOSURE_BSDF_TRANSPARENT_ID;}
fn surf_roughness(kind:u32)->f32{return select(1.0,0.0,kind==CLOSURE_BSDF_TRANSPARENT_ID);}
fn surf_bump(sd:SurfaceData,c:SvmClosure,wo:vec3<f32>,is_eval:bool)->f32{
    if(all(c.N==sd.N)){return 1.0;}
    let ns_i=dot(sd.N,wo);let ns_n=dot(sd.N,c.N);let n_i=dot(c.N,wo);let diffuse=surf_diffuse(c.kind);
    if(ns_i*ns_n*n_i<0.0&&(is_eval||diffuse)){return 0.0;}
    if(!diffuse||(sd.shader_flag&SD_USE_BUMP_MAP_CORRECTION)==0u){return 1.0;}
    let ci=abs(ns_i);let cd=abs(ns_n);if(cd>=1.0||ci>=1.0){return 1.0;}if(ci<1e-6){return 0.0;}
    let alpha=clamp(0.125*(1.0/(cd*cd)-1.0),0.0,1.0);
    return 2.0/(1.0+sqrt(1.0+alpha*(1.0/(ci*ci)-1.0)));
}
fn surf_oren(c:SvmClosure,wi:vec3<f32>,wo:vec3<f32>)->vec3<f32>{
    let nl=max(dot(c.N,wo),0.0);if(c.b<=0.0){return vec3<f32>(nl*INV_PI);}
    let nv=max(dot(c.N,wi),0.0);var t=dot(wo,wi)-nl*nv;if(t>0.0){t/=max(nl,nv)+1.1754943508222875e-38;}
    let el=svm_oren_add(c.a*PI,c.b*svm_oren_g(nl));return nl*(vec3<f32>(c.a+c.b*t)+c.multiscatter*(1.0-el));
}
fn surf_evaluate(sd:SurfaceData,c:SvmClosure,wo:vec3<f32>)->vec4<f32>{
    let bump=surf_bump(sd,c,wo,true);if(bump==0.0||c.kind==CLOSURE_BSDF_TRANSPARENT_ID){return vec4<f32>(0);}
    let cosine=select(dot(c.N,wo),-dot(c.N,wo),c.kind==CLOSURE_BSDF_TRANSLUCENT_ID);let pdf=max(cosine,0.0)*INV_PI;
    if(c.kind==CLOSURE_BSDF_OREN_NAYAR_ID){if(cosine>0.0){return vec4<f32>(surf_oren(c,sd.wi,wo)*bump,pdf);}return vec4<f32>(0);}
    return vec4<f32>(vec3<f32>(pdf*bump),pdf);
}
struct SurfaceSample {wo:vec3<f32>,pdf:f32,eval:vec3<f32>,label:u32,diffuse:vec3<f32>,roughness:f32}
fn surf_sample(sd:SurfaceData,r:SvmResult,random:vec3<f32>)->SurfaceSample{
    var out:SurfaceSample;if((r.runtime_flag&SR_BSDF)==0u||r.count==0u){return out;}
    var selected=0u;
    if(r.count>1u){var sum=0.0;for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(surf_bsdf(c.kind)){sum+=c.sample_weight;}}
        let pick=random.z*sum;var partial=0.0;for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(surf_bsdf(c.kind)){
            let next=partial+c.sample_weight;if(pick<next){selected=i;break;}partial=next;}}}
    let c=svm_closure_read(r,selected);if(!surf_bsdf(c.kind)){return out;}
    var value=vec3<f32>(0);
    if(c.kind==CLOSURE_BSDF_TRANSPARENT_ID){out.wo=-sd.wi;out.pdf=1e6;value=vec3<f32>(1e6);out.label=LABEL_TRANSMIT|LABEL_TRANSPARENT;
        if(data_float(D_background_transparent_roughness_squared_threshold)>=0.0){out.label|=LABEL_TRANSMIT_TRANSPARENT;}}
    else{
        let transmission=c.kind==CLOSURE_BSDF_TRANSLUCENT_ID;let sampled=hemisphere(select(c.N,-c.N,transmission),random.xy);
        out.wo=sampled.xyz;out.pdf=sampled.w;out.label=LABEL_DIFFUSE|select(LABEL_REFLECT,LABEL_TRANSMIT,transmission);
        let valid=select(dot(sd.Ng,out.wo)>0.0,dot(sd.Ng,out.wo)<0.0,transmission);
        if(valid){value=vec3<f32>(out.pdf);if(c.kind==CLOSURE_BSDF_OREN_NAYAR_ID){value=surf_oren(c,sd.wi,out.wo);}
            if(!transmission){value*=surf_bump(sd,c,out.wo,false);}}
        else{out.pdf=0.0;}
    }
    if(out.pdf==0.0){return out;}
    out.eval=value*c.weight;if(surf_diffuse(c.kind)){out.diffuse=out.eval;}
    out.roughness=surf_roughness(c.kind);
    if(r.count>1u){var pdf_sum=out.pdf*c.sample_weight;var weight_sum=c.sample_weight;var rough_sum=pdf_sum*out.roughness;
        for(var i=0u;i<r.count;i++){if(i==selected){continue;}let other=svm_closure_read(r,i);if(!surf_bsdf(other.kind)){continue;}
            let e=surf_evaluate(sd,other,out.wo);if(e.w!=0.0){out.eval+=e.xyz*other.weight;if(surf_diffuse(other.kind)){out.diffuse+=e.xyz*other.weight;}
                let w=e.w*other.sample_weight;pdf_sum+=w;rough_sum+=w*surf_roughness(other.kind);}weight_sum+=other.sample_weight;}
        out.roughness=select(0.0,rough_sum/pdf_sum,pdf_sum>0.0);out.pdf=select(0.0,pdf_sum/weight_sum,weight_sum>0.0);
    }
    return out;
}
fn surf_prepare(index:u32,r:ptr<function,SvmResult>){
    let closure_filter=data_word(D_integrator_filter_closures);if((closure_filter&FILTER_CLOSURE_EMISSION)!=0u){(*r).emission=vec3<f32>(0);}
    if((state_byte(S_path_visibility,index)&VIS_CAMERA)==0u){return;}
    if((closure_filter&FILTER_CLOSURE_DIRECT_LIGHT)!=0u){(*r).runtime_flag&=~SR_BSDF_HAS_EVAL;}
    for(var i=0u;i<(*r).count;i++){var c=svm_closure_read(*r,i);
        if(surf_diffuse(c.kind)&&(closure_filter&FILTER_CLOSURE_DIFFUSE)!=0u){
            c.kind=CLOSURE_NONE_ID;c.sample_weight=0.0;}
        else if(c.kind==CLOSURE_BSDF_TRANSPARENT_ID&&(closure_filter&FILTER_CLOSURE_TRANSPARENT)!=0u){c.kind=CLOSURE_HOLDOUT_ID;c.sample_weight=0.0;(*r).runtime_flag|=SR_HOLDOUT;}
        svm_closure_write((*r).closure_base+i*SVM_CLOSURE_BYTES,c);
    }
}
fn surf_holdout(sd:SurfaceData,r:ptr<function,SvmResult>)->vec3<f32>{
    var weight=vec3<f32>(0);
    if((sd.object_flag&SD_OBJECT_HOLDOUT_MASK)!=0u){
        if(((*r).runtime_flag&SR_TRANSPARENT)!=0u){weight=vec3<f32>(1)-(*r).transparency;
            for(var i=0u;i<(*r).count;i++){var c=svm_closure_read(*r,i);if(c.kind!=CLOSURE_BSDF_TRANSPARENT_ID){c.kind=NBUILTIN_CLOSURES;svm_closure_write((*r).closure_base+i*SVM_CLOSURE_BYTES,c);}}
            (*r).runtime_flag&=SR_TRANSPARENT|SR_BSDF;
        }else{weight=vec3<f32>(1);}
    }else{for(var i=0u;i<(*r).count;i++){let c=svm_closure_read(*r,i);if(c.kind==CLOSURE_HOLDOUT_ID){weight+=c.weight;}}}
    return weight;
}
fn surf_ray_offset(sd:SurfaceData,direction:vec3<f32>)->vec3<f32>{
    let object=table_at(G_objects,sd.object_id,SIZE_objects);let offset=load_word(object+O_position_offset);let vi=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);
    var pos=sd.P;var dir=direction;if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)==0u){pos=surface_transform_point(object+O_itfm,pos);dir=surface_transform_direction(object+O_itfm,dir);}
    let a=heap_vec3(table_at(G_tri_verts,offset+load_word(vi),SIZE_tri_verts))-pos;
    let b=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+4u),SIZE_tri_verts))-pos;
    let c=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+8u),SIZE_tri_verts))-pos;
    let u=c_ray_triangle_dot(c_ray_triangle_cross(c+a,c-a),dir);let v=c_ray_triangle_dot(c_ray_triangle_cross(a+b,a-b),dir);let w=c_ray_triangle_dot(c_ray_triangle_cross(b+c,b-c),dir);
    let eps=1.1920928955078125e-7*abs(u+v+w);if(min(u,min(v,w))>=eps||max(u,max(v,w))<=-eps){return sd.P;}
    let amount=vec3<i32>(256.0*sd.Ng);let bits=bitcast<vec3<i32>>(sd.P)+select(amount,-amount,sd.P<vec3<f32>(0));
    return select(bitcast<vec3<f32>>(bits),sd.P+(1.0/65536.0)*sd.Ng,abs(sd.P)<vec3<f32>(1.0/32.0));
}
fn surf_next(index:u32,label:u32,runtime:u32){
    var flag=state_word(S_path_flag,index);let offset=state_half(S_path_rng_offset,index)+RNG_BOUNCE;state_u16(S_path_rng_offset,index,offset);
    if((label&LABEL_TRANSPARENT)!=0u){let n=state_half(S_path_transparent_bounce,index)+1u;flag|=PATH_RAY_TRANSPARENT;
        if(n>=data_word(D_integrator_transparent_max_bounce)){flag|=PATH_RAY_TERMINATE_ON_NEXT_SURFACE;}
        state_u16(S_path_transparent_bounce,index,n);state_u32(S_path_flag,index,flag);return;}
    let bounce=state_half(S_path_bounce,index)+1u;if(bounce>=data_word(D_integrator_max_bounce)){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;}
    flag&=~(PATH_RAY_REFLECT|PATH_RAY_SINGULAR|PATH_RAY_TRANSPARENT|PATH_RAY_IMPORTANCE_BAKE|PATH_RAY_MIS_SKIP|PATH_RAY_MIS_HAD_TRANSMISSION);
    var visibility=PATH_RAY_VISIBILITY_DIFFUSE;flag|=PATH_RAY_DIFFUSE_ANCESTOR;
    if((label&LABEL_REFLECT)!=0u){flag|=PATH_RAY_REFLECT;flag&=~PATH_RAY_TRANSPARENT_BACKGROUND;
        let n=state_half(S_path_diffuse_bounce,index)+1u;state_u16(S_path_diffuse_bounce,index,n);if(n>=data_word(D_integrator_max_diffuse_bounce)){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;}}
    else{visibility|=PATH_RAY_VISIBILITY_TRANSMIT;if((label&LABEL_TRANSMIT_TRANSPARENT)==0u){flag&=~PATH_RAY_TRANSPARENT_BACKGROUND;}
        let n=state_half(S_path_transmission_bounce,index)+1u;state_u16(S_path_transmission_bounce,index,n);if(n>=data_word(D_integrator_max_transmission_bounce)){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;}}
    if((runtime&SR_BSDF_HAS_TRANSMISSION)!=0u){flag|=PATH_RAY_MIS_HAD_TRANSMISSION;}
    if((flag&PATH_RAY_ANY_PASS)==0u&&(flag&PATH_RAY_TRANSPARENT_BACKGROUND)==0u){flag|=PATH_RAY_SURFACE_PASS;}
    state_u8(S_path_visibility,index,visibility);state_u32(S_path_flag,index,flag);state_u16(S_path_bounce,index,bounce);
}
fn surf_finish(index:u32,continues:bool){
    let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[counter/4u+DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE],1u);
    if(continues){atomicAdd(&heap[counter/4u+K_CLOSEST],1u);state_u16(S_path_queued_kernel,index,K_CLOSEST);}else{state_u16(S_path_queued_kernel,index,0u);}
}
fn surf_emission(index:u32,base:u32,sd:SurfaceData,emission:vec3<f32>){
    let throughput=heap_vec3(state_address(S_path_throughput,index,12u));var value=throughput*emission;
    value=select(value,vec3<f32>(0),(bitcast<vec3<u32>>(value)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
    let bounce=state_half(S_path_bounce,index);let limit=select(data_float(D_integrator_sample_clamp_direct),data_float(D_integrator_sample_clamp_indirect),i32(bounce)-1>0);
    let total=abs(value.x)+abs(value.y)+abs(value.z);if(total>limit){value*=limit/total;}
    let flags=data_word(D_film_light_pass_flag);if((flags&MASK_COMBINED)!=0u){film_rgb(base,data_word(D_film_pass_combined),value);}
    if(data_word(D_film_pass_adaptive_aux_buffer)!=PASS_NONE&&(countOneBits(state_word(S_path_sample,index)&0xaaaaaaaau)&1u)!=0u){film_rgb(base,data_word(D_film_pass_adaptive_aux_buffer),value*2.0);}
    if(flags==0u){return;}let flag=state_word(S_path_flag,index);let group=load_word(table_at(G_objects,sd.object_id,SIZE_objects)+O_lightgroup);
    if(group!=NONE&&data_word(D_film_pass_lightgroup)!=PASS_NONE){film_rgb(base,data_word(D_film_pass_lightgroup)+3u*group,value);}
    if((flag&PATH_RAY_ANY_PASS)==0u){film_rgb(base,data_word(D_film_pass_emission),value);}
    else if(p(20u)!=0u&&(flag&PATH_RAY_SURFACE_PASS)!=0u){let diffuse=heap_vec3(state_address(S_path_pass_diffuse_weight,index,12u));let glossy=heap_vec3(state_address(S_path_pass_glossy_weight,index,12u));
        film_rgb(base,select(data_word(D_film_pass_glossy_indirect),data_word(D_film_pass_glossy_direct),bounce==1u),glossy*value);
        film_rgb(base,select(data_word(D_film_pass_transmission_indirect),data_word(D_film_pass_transmission_direct),bounce==1u),(vec3<f32>(1)-diffuse-glossy)*value);
        film_rgb(base,select(data_word(D_film_pass_diffuse_indirect),data_word(D_film_pass_diffuse_direct),bounce==1u),diffuse*value);}
}
@compute @workgroup_size(64) fn shade_surface(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}if(state_half(S_path_queued_kernel,index)!=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){fail();return;}
    let flag=state_word(S_path_flag,index);if((flag&SURFACE_UNSUPPORTED_FLAGS)!=0u||(state_byte(S_path_visibility,index)&PATH_RAY_VISIBILITY_VOLUME_SCATTER)!=0u){fail();return;}
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_path_render_pixel_index,index);if(pixel>=p(11u)/stride){fail();return;}let base=p(4u)+pixel*stride*4u;
    var sd=surface_from_ray(index);if(sd.error!=0u){return;}let object=table_at(G_objects,sd.object_id,SIZE_objects);
    if(heap_float(object+O_shadow_terminator_shading_offset)>1.0||(sd.shader_flag&SD_HAS_ONLY_VOLUME)!=0u){fail();return;}
    if((flag&PATH_RAY_MIS_SKIP)==0u&&(sd.shader_flag&select(SD_MIS_FRONT,SD_MIS_BACK,(sd.runtime_flag&SR_BACKFACING)!=0u))!=0u){fail();return;}
    let group=load_word(object+O_lightgroup);let go=data_word(D_film_pass_lightgroup);if(group!=NONE&&go!=PASS_NONE&&(group>=stride/3u||go>stride-3u*(group+1u))){fail();return;}
    var r=svm_evaluate(index,sd,data_word(D_max_closures),p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES);if(r.error!=0u){return;}sd.N=r.N;surf_prepare(index,&r);
    if(((r.runtime_flag&SR_HOLDOUT)!=0u||(sd.object_flag&SD_OBJECT_HOLDOUT_MASK)!=0u)&&(flag&PATH_RAY_TRANSPARENT_BACKGROUND)!=0u){
        let weight=surf_holdout(sd,&r);let contribution=weight*heap_vec3(state_address(S_path_throughput,index,12u));
        if((data_word(D_film_light_pass_flag)&MASK_COMBINED)!=0u){film_add(base+(data_word(D_film_pass_combined)+3u)*4u,(contribution.x+contribution.y+contribution.z)*(1.0/3.0));}
        if(all(weight==vec3<f32>(1))){surf_finish(index,false);return;}
    }
    if((r.runtime_flag&SR_EMISSION)!=0u){surf_emission(index,base,sd,select(vec3<f32>(0),r.emission,abs(dot(sd.Ng,sd.wi))>0.0));}
    let probability=select(heap_float(state_address(S_path_continuation_probability,index,4u)),0.0,(flag&PATH_RAY_TERMINATE_ON_NEXT_SURFACE)!=0u);
    if(probability==0.0){surf_finish(index,false);return;}
    var throughput=heap_vec3(state_address(S_path_throughput,index,12u));if(probability!=1.0){throughput/=probability;state_vec3(S_path_throughput,index,throughput);}
    let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_SURFACE_BSDF).xyz;
    let sampled=surf_sample(sd,r,random);if(sampled.pdf==0.0||all(sampled.eval==vec3<f32>(0))){surf_finish(index,false);return;}
    if((sampled.label&LABEL_TRANSPARENT)!=0u){let t=sd.ray_length;state_f32(S_ray_tmin,index,select(bitcast<f32>(bitcast<u32>(t)+1u),1.1754943508222875e-38,t==0.0));}
    else{let direction=surface_normalize(sampled.wo);state_vec3(S_ray_P,index,surf_ray_offset(sd,direction));state_vec3(S_ray_D,index,direction);
        state_f32(S_ray_tmin,index,0.0);state_f32(S_ray_tmax,index,3.4028234663852886e38);state_f32(S_ray_dP,index,sd.dP);
        let dd=heap_float(state_address(S_ray_dD,index,4u));state_f32(S_ray_dD,index,select(dd,max(dd,sqrt(sampled.roughness)),sampled.roughness>0.0));}
    state_vec3(S_path_throughput,index,throughput*(sampled.eval/sampled.pdf));
    if(p(20u)!=0u&&state_half(S_path_bounce,index)==0u){state_vec3(S_path_pass_diffuse_weight,index,svm_divide(sampled.diffuse,sampled.eval));state_vec3(S_path_pass_glossy_weight,index,vec3<f32>(0));}
    if((sampled.label&LABEL_TRANSPARENT)==0u){state_f32(S_path_mis_ray_pdf,index,sampled.pdf);state_vec3(S_path_mis_origin_n,index,sd.N);
        state_f32(S_path_min_ray_pdf,index,min(sampled.pdf,heap_float(state_address(S_path_min_ray_pdf,index,4u))));}
    surf_next(index,sampled.label,r.runtime_flag);surf_finish(index,true);
}
