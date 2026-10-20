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
// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2014-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native BVH2 traversal, static triangle instances, and Cycles closest-stage
// termination/scheduling. Triangle arithmetic is generated from upstream AST.
fn clamp_direction(d:vec3<f32>)->vec3<f32>{
    let tiny=8.271806E-25;let signed_tiny=bitcast<vec3<f32>>((bitcast<vec3<u32>>(d)&vec3<u32>(0x80000000u))|vec3<u32>(bitcast<u32>(tiny)));
    return select(signed_tiny,d,abs(d)>vec3<f32>(tiny));
}
fn transform_vector(a:u32,v:vec3<f32>,point:bool)->vec3<f32>{
    let x=heap_vec4(a);let y=heap_vec4(a+16u);let z=heap_vec4(a+32u);
    var result=vec3<f32>(x.x,y.x,z.x)*v.x+(vec3<f32>(x.y,y.y,z.y)*v.y+vec3<f32>(x.z,y.z,z.z)*v.z);
    if(point){result+=vec3<f32>(x.w,y.w,z.w);}return result;
}
fn bounds_hit(origin:vec3<f32>,inv:vec3<f32>,tmin:f32,tmax:f32,node:u32,visibility:u32)->vec3<f32>{
    let a=table_at(G_bvh_nodes,node,SIZE_bvh_nodes);
    if(node>=table_count(G_bvh_nodes)||table_count(G_bvh_nodes)-node<4u){fail();return vec3<f32>(0);}
    let x=heap_vec4(a+16u);let y=heap_vec4(a+32u);let z=heap_vec4(a+48u);
    let lo0=(vec3<f32>(x.x,y.x,z.x)-origin)*inv;let hi0=(vec3<f32>(x.z,y.z,z.z)-origin)*inv;
    let lo1=(vec3<f32>(x.y,y.y,z.y)-origin)*inv;let hi1=(vec3<f32>(x.w,y.w,z.w)-origin)*inv;
    let near0=max(max(tmin,min(lo0.x,hi0.x)),max(min(lo0.y,hi0.y),min(lo0.z,hi0.z)));
    let far0=min(min(tmax,max(lo0.x,hi0.x)),min(max(lo0.y,hi0.y),max(lo0.z,hi0.z)));
    let near1=max(max(tmin,min(lo1.x,hi1.x)),max(min(lo1.y,hi1.y),min(lo1.z,hi1.z)));
    let far1=min(min(tmax,max(lo1.x,hi1.x)),min(max(lo1.y,hi1.y),max(lo1.z,hi1.z)));
    let mask=select(0u,1u,far0>=near0&&(load_word(a)&visibility)!=0u)|select(0u,2u,far1>=near1&&(load_word(a+4u)&visibility)!=0u);
    return vec3<f32>(f32(mask),near0,near1);
}
struct NativeHit {t:f32,u:f32,v:f32,object:u32,prim:u32,kind:u32}
fn trace_native(origin:vec3<f32>,direction:vec3<f32>,tmin:f32,tmax:f32,visibility:u32,self_object:u32,self_prim:u32)->NativeHit{
    var result=NativeHit(tmax,0.0,0.0,NONE,NONE,PRIM_TYPE_NONE);
    if(table_count(G_prim_index)==0u){return result;}
    var pos=origin;var dir=clamp_direction(direction);var inv=vec3<f32>(1.0)/dir;
    var stack:array<i32,192>;stack[0]=0x76543210i;var sp=0i;
    var node=bitcast<i32>(data_word(D_bvh_root));var object=NONE;var visits=0u;
    loop{
        // Malformed cycles in a scene table must fail instead of hanging a GPU.
        visits++;if(visits>1048576u){fail();return result;}
        while(node>=0&&node!=0x76543210i){
            visits++;if(visits>1048576u){fail();return result;}
            let bounds=bounds_hit(pos,inv,tmin,result.t,u32(node),visibility);
            let a=table_at(G_bvh_nodes,u32(node),SIZE_bvh_nodes);
            node=bitcast<i32>(load_word(a+8u));var other=bitcast<i32>(load_word(a+12u));let mask=u32(bounds.x);
            if(mask==3u){if(bounds.z<bounds.y){let tmp=node;node=other;other=tmp;}
                sp++;if(sp>=192){fail();return result;}stack[u32(sp)]=other;
            }else if(mask==2u){node=other;}
            else if(mask==0u){if(sp<0){fail();return result;}node=stack[u32(sp)];sp--;}
        }
        if(node<0){
            let a=table_at(G_bvh_leaf_nodes,u32(-node-1),SIZE_bvh_leaf_nodes);
            let first=bitcast<i32>(load_word(a));let end=load_word(a+4u);let kind=load_word(a+12u);
            if(first>=0){
                if(end>table_count(G_prim_index)||u32(first)>end){fail();return result;}
                if(sp<0){fail();return result;}node=stack[u32(sp)];sp--;
                if((kind&PRIMITIVE_ALL)!=PRIMITIVE_TRIANGLE&&u32(first)!=end){fail();return result;}
                for(var index=u32(first);index<end;index++){
                    var prim_object=object;if(object==NONE){prim_object=load_word(table_at(G_prim_object,index,SIZE_prim_object));}
                    let prim=load_word(table_at(G_prim_index,index,SIZE_prim_index));
                    if(prim_object==self_object&&prim==self_prim){continue;}
                    if((load_word(table_at(G_prim_visibility,index,SIZE_prim_visibility))&visibility)==0u){continue;}
                    let obj=table_at(G_objects,prim_object,SIZE_objects);let offset=load_word(obj+O_position_offset);
                    let vi=table_at(G_tri_vindex,prim,SIZE_tri_vindex);
                    let va=heap_vec3(table_at(G_tri_verts,offset+load_word(vi),SIZE_tri_verts));
                    let vb=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+4u),SIZE_tri_verts));
                    let vc=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+8u),SIZE_tri_verts));
                    var u:f32;var v:f32;var t:f32;
                    if(c_ray_triangle_intersect(pos,dir,tmin,result.t,va,vb,vc,&u,&v,&t)){
                        result=NativeHit(t,u,v,prim_object,prim,PRIMITIVE_TRIANGLE);
                        if((visibility&PATH_RAY_VISIBILITY_SHADOW_OPAQUE)!=0u){return result;}
                    }
                }
            }else{
                if(object!=NONE){fail();return result;}
                object=load_word(table_at(G_prim_object,u32(-first-1),SIZE_prim_object));
                let obj=table_at(G_objects,object,SIZE_objects);
                pos=transform_vector(obj+O_itfm,origin,true);dir=clamp_direction(transform_vector(obj+O_itfm,direction,false));inv=vec3<f32>(1.0)/dir;
                sp++;if(sp>=192){fail();return result;}stack[u32(sp)]=0x76543210i;
                node=bitcast<i32>(load_word(table_at(G_object_node,object,SIZE_object_node)));
            }
        }
        if(node==0x76543210i){
            if(sp<0){break;}
            if(object==NONE){fail();return result;}
            pos=origin;dir=clamp_direction(direction);inv=vec3<f32>(1.0)/dir;object=NONE;
            node=stack[u32(sp)];sp--;if(node==0x76543210i){break;}
        }
    }
    return result;
}
fn terminate_surface(index:u32,shader_flags:u32)->bool{
    var flag=state_word(S_path_flag,index);
    if(path_ao(index)){
        if((shader_flags&(SD_HAS_TRANSPARENT_SHADOW|SD_HAS_EMISSION))!=0u){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;state_u32(S_path_flag,index,flag);}
        else{return true;}
    }
    var probability=1.0;var use_rr=false;
    if((flag&PATH_RAY_TRANSPARENT)!=0u){use_rr=state_half(S_path_transparent_bounce,index)>data_word(D_integrator_transparent_min_bounce);}
    else{use_rr=state_half(S_path_bounce,index)>data_word(D_integrator_min_bounce);}
    if(use_rr){let beta=abs(heap_vec3(state_address(S_path_throughput,index,12u)));probability=min(sqrt(max(beta.x,max(beta.y,beta.z))),1.0);}
    state_f32(S_path_continuation_probability,index,probability);
    if(probability!=1.0){
        let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_TERMINATE).x;
        if(probability==0.0||random>=probability){
            if((shader_flags&SD_HAS_EMISSION)!=0u){state_u32(S_path_flag,index,flag|PATH_RAY_TERMINATE_ON_NEXT_SURFACE);}
            else{return true;}
        }
    }
    return false;
}
@compute @workgroup_size(64) fn intersect_closest(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
    if(state_half(S_path_queued_kernel,index)!=K_CLOSEST){fail();return;}
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));
    let tmin=heap_float(state_address(S_ray_tmin,index,4u));var tmax=heap_float(state_address(S_ray_tmax,index,4u));
    let self_prim=state_word(S_isect_prim,index);let self_object=state_word(S_isect_object,index);
    if(path_ao(index)){
        tmax=data_float(D_integrator_ao_bounces_distance);
        if(self_object!=NONE){let distance=heap_float(table_at(G_objects,self_object,SIZE_objects)+O_ao_distance);if(distance!=0.0){tmax=distance;}}
    }
    var visibility=state_byte(S_path_visibility,index);let flag=state_word(S_path_flag,index);
    if((visibility&PATH_RAY_VISIBILITY_TRANSMIT)!=0u){visibility&=~(PATH_RAY_VISIBILITY_DIFFUSE|PATH_RAY_VISIBILITY_GLOSSY);}
    if((flag&PATH_RAY_SHADOW_CATCHER_PASS)!=0u){visibility<<=16u;}
    var hit=NativeHit(0.0,0.0,0.0,NONE,NONE,PRIM_TYPE_NONE);
    // Match Cycles' early validity test, including zero-length conditional rays.
    if((bitcast<u32>(origin.x)&0x7f800000u)!=0x7f800000u&&(bitcast<u32>(direction.x)&0x7f800000u)!=0x7f800000u&&dot(direction,direction)!=0.0&&tmin<bitcast<f32>(0x7f7fffffu)){
        hit=trace_native(origin,direction,tmin,tmax,visibility,self_object,self_prim);
    }
    if(load_word(p(17u))!=0u){return;}
    state_f32(S_isect_t,index,hit.t);state_f32(S_isect_u,index,hit.u);state_f32(S_isect_v,index,hit.v);
    state_u32(S_isect_object,index,hit.object);state_u32(S_isect_prim,index,hit.prim);state_u32(S_isect_type,index,hit.kind);
    var next=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;var shader=0u;var sorted=false;
    if(hit.prim!=NONE){
        shader=load_word(table_at(G_tri_shader,hit.prim,SIZE_tri_shader))&SHADER_MASK;
        let flags=load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags);
        if(terminate_surface(index,flags)){next=0u;}
        else{next=select(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,(flags&SD_HAS_RAYTRACE)!=0u);sorted=true;}
    }else if((data_word(D_integrator_filter_closures)&FILTER_CLOSURE_DIRECT_LIGHT)!=0u&&state_half(S_path_bounce,index)==1u){next=0u;}
    if(sorted){
        let divisor=load_word(p(7u)+S_sort_partition_divisor);let key=shader+data_word(D_max_shaders)*(index/divisor);
        let limit=select(p(15u),p(16u),next==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE);
        if(key>=limit){fail();return;}
        let counter=load_word(p(7u)+S_sort_key_counter+next*POINTER_SIZE);
        atomicAdd(&heap[(counter+key*4u)/4u],1u);state_u32(S_path_shader_sort_key,index,key);
    }
    let counter=load_word(p(7u)+S_queue_counter);
    atomicSub(&heap[(counter+K_CLOSEST*4u)/4u],1u);
    if(next!=0u){atomicAdd(&heap[(counter+next*4u)/4u],1u);}
    state_u16(S_path_queued_kernel,index,next);
}
