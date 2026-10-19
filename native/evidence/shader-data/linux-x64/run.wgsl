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
@compute @workgroup_size(64) fn shader_data_probe(@builtin(global_invocation_id) id:vec3<u32>){
 if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
 let sd=surface_from_ray(index);if(sd.error!=0u){return;}
 let base=p(4u)+id.x*140u;
 store_word(base+0u,bitcast<u32>(sd.P.x));
 store_word(base+4u,bitcast<u32>(sd.P.y));
 store_word(base+8u,bitcast<u32>(sd.P.z));
 store_word(base+12u,bitcast<u32>(sd.N.x));
 store_word(base+16u,bitcast<u32>(sd.N.y));
 store_word(base+20u,bitcast<u32>(sd.N.z));
 store_word(base+24u,bitcast<u32>(sd.Ng.x));
 store_word(base+28u,bitcast<u32>(sd.Ng.y));
 store_word(base+32u,bitcast<u32>(sd.Ng.z));
 store_word(base+36u,bitcast<u32>(sd.wi.x));
 store_word(base+40u,bitcast<u32>(sd.wi.y));
 store_word(base+44u,bitcast<u32>(sd.wi.z));
 store_word(base+48u,bitcast<u32>(sd.dPdu.x));
 store_word(base+52u,bitcast<u32>(sd.dPdu.y));
 store_word(base+56u,bitcast<u32>(sd.dPdu.z));
 store_word(base+60u,bitcast<u32>(sd.dPdv.x));
 store_word(base+64u,bitcast<u32>(sd.dPdv.y));
 store_word(base+68u,bitcast<u32>(sd.dPdv.z));
 store_word(base+72u,bitcast<u32>(sd.du.x));
 store_word(base+76u,bitcast<u32>(sd.du.y));
 store_word(base+80u,bitcast<u32>(sd.dv.x));
 store_word(base+84u,bitcast<u32>(sd.dv.y));
 store_word(base+88u,bitcast<u32>(sd.u));
 store_word(base+92u,bitcast<u32>(sd.v));
 store_word(base+96u,bitcast<u32>(sd.time));
 store_word(base+100u,bitcast<u32>(sd.ray_length));
 store_word(base+104u,bitcast<u32>(sd.dP));
 store_word(base+108u,bitcast<u32>(sd.dI));
 store_word(base+112u,sd.object_id);
 store_word(base+116u,sd.prim);
 store_word(base+120u,sd.primitive_type);
 store_word(base+124u,sd.shader);
 store_word(base+128u,sd.runtime_flag);
 store_word(base+132u,sd.shader_flag);
 store_word(base+136u,sd.object_flag);
}
