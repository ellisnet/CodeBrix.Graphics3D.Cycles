// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Cycles integrator_init_from_bake (pinned kernel/integrator/init_from_bake.h: bake_jitter_barycentric,
// bake_offset_towards_center and the kernel) with geom/triangle.h triangle_point_normal, triangle_smooth_normal
// and triangle_dPdudv, camera/camera.h camera_direction_from_point, util/hash.h hash_float2_to_float2 and the
// path initialization of integrator/path_state.h. The bake module (bake.cpp) holds only this entry: modules of
// scenes that do not bake are unchanged. Sums and products round as the originating CPU build's float3
// arithmetic does (per-ISA: NATIVE_FLOAT3_RECIPROCAL_NORMALIZE selects the scalar build's reciprocal forms);
// every sum is an integer rounded_add, so no driver can contract it.
// Jenkins lookup3 hash_uint, hash_uint2 and hash_uint3 (util/hash.h) on wrapping u32 arithmetic.
fn bk_rot(x:u32,k:u32)->u32{return (x<<k)|(x>>(32u-k));}
fn bk_hash(k:vec3<u32>,n:u32)->u32{
    var a=0xdeadbeefu+(n<<2u)+13u;var b=a;var c=a;
    a+=k.x;if(n>=2u){b+=k.y;}if(n==3u){c+=k.z;}
    c^=b;c-=bk_rot(b,14u);a^=c;a-=bk_rot(c,11u);b^=a;b-=bk_rot(a,25u);c^=b;c-=bk_rot(b,16u);
    a^=c;a-=bk_rot(c,4u);b^=a;b-=bk_rot(a,14u);c^=b;c-=bk_rot(b,24u);return c;
}
// Nearest-even u32 -> f32 like the host conversion (WGSL leaves inexact integer conversions to the implementation).
fn bk_u32_float(n:u32)->f32{
    if(n<0x1000000u){return f32(n);}
    let drop=8u-countLeadingZeros(n);var m=n>>drop;let rest=n&((1u<<drop)-1u);let half=1u<<(drop-1u);
    if(rest>half||(rest==half&&(m&1u)!=0u)){m+=1u;}
    return ldexp(f32(m),i32(drop));
}
// hash_float2_to_float2: uint_to_float_incl of the hashes (the scale 1/(float)0xFFFFFFFF is exactly 2^-32).
fn bk_hash_float2(k:vec2<f32>)->vec2<f32>{
    let x=bitcast<u32>(k.x);let y=bitcast<u32>(k.y);
    return vec2<f32>(bk_u32_float(bk_hash(vec3<u32>(x,y,0u),2u))*2.3283064365386963e-10,
                     bk_u32_float(bk_hash(vec3<u32>(x,y,0x3f800000u),3u))*2.3283064365386963e-10);
}
// float3 / float: per-component quotients (SSE), or the product with the reciprocal (scalar float3).
fn bk_div3(v:vec3<f32>,f:f32)->vec3<f32>{
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return v*rounded_divide(1.0,f);}
    return vec3<f32>(rounded_divide(v.x,f),rounded_divide(v.y,f),rounded_divide(v.z,f));
}
fn bk_length(v:vec3<f32>)->f32{return rounded_sqrt(rounded_dot(v,v));}
// bake_offset_towards_center: the vertex nudge towards the triangle's centre, as barycentrics of the moved point.
fn bk_offset_towards_center(a:vec3<f32>,b:vec3<f32>,c:vec3<f32>,u:f32,v:f32)->vec2<f32>{
    let w=rounded_add(rounded_add(1.0,-u),-v);
    let P=rounded_add3(rounded_add3(u*a,v*b),w*c);
    let center=bk_div3(rounded_add3(rounded_add3(a,b),c),3.0);
    let to_center=rounded_add3(center,-P);
    let extent=max(max(max(abs(P.x),abs(P.y)),abs(P.z)),1.0)*1e-4;
    let offset_P=rounded_add3(P,surface_native_normalize(to_center)*min(bk_length(to_center),extent));
    let v1=rounded_add3(b,-a);let v2=rounded_add3(c,-a);let vP=rounded_add3(offset_P,-a);
    let d11=rounded_dot(v1,v1);let d12=rounded_dot(v1,v2);let d22=rounded_dot(v2,v2);let dP1=rounded_dot(vP,v1);let dP2=rounded_dot(vP,v2);
    let denom=rounded_add(d11*d22,-(d12*d12));
    if(denom==0.0){return vec2<f32>(0.0);}
    let high=rounded_add(1.0,-1e-5);
    let offset_v=clamp(rounded_divide(rounded_add(d22*dP1,-(d12*dP2)),denom),1e-5,high);
    let offset_w=clamp(rounded_divide(rounded_add(d11*dP2,-(d12*dP1)),denom),1e-5,high);
    let offset_u=clamp(rounded_add(rounded_add(1.0,-offset_v),-offset_w),1e-5,high);
    return vec2<f32>(offset_u,offset_v);
}
// film_write_pass_float3 and the combined alpha of film_write_transparent: atomic float adds, as on the GPU devices.
fn bk_film_add(address:u32,value:f32){
    var old=load_word(address);
    loop{let next=bitcast<u32>(bitcast<f32>(old)+value);let result=atomicCompareExchangeWeak(&heap[address/4u],old,next);
        if(result.exchanged){break;}old=result.old_value;}
}
fn bk_film_add3(address:u32,value:vec3<f32>){bk_film_add(address,value.x);bk_film_add(address+4u,value.y);bk_film_add(address+8u,value.z);}
//#if SHADOW_CATCHER
// integrator_split_shadow_catcher after a bake's surface setup: the decision and the bounce data, as the closest stage
// marks a split (wavefront_closest.wgsl sc_split_mark); shadow_catcher_assign and shadow_catcher_split make the copy.
fn bk_split_mark(index:u32,base:u32,object:u32,shader:u32,next:u32){
    if(!sc_enabled()){return;}
    let object_flag=load_word(table_at(G_object_flag,object,4u));
    if((object_flag&SC_OBJECT)==0u||(object_flag&SC_HOLDOUT_MASK)!=0u){return;}
    let flag=state_word(S_path_flag,index);
    if((flag&SC_TRANSPARENT_BACKGROUND)==0u||(flag&SC_PATH_HIT)!=0u){return;}
    if(p(24u)==0u||shader>0xffffffu){fail();return;}
    let throughput=heap_vec3(state_address(S_path_throughput,index,12u));
    sc_film_add(base+data_word(SC_D_pass_shadow_catcher_sample_count)*4u,1.0);
    sc_film_add(base+(data_word(SC_D_pass_shadow_catcher_matte)+3u)*4u,(throughput.x+throughput.y+throughput.z)*(1.0/3.0));
    state_u32(S_path_flag,index,flag|SC_PATH_HIT);
    var kernel=next;
    if(data_word(SC_D_pass_background)!=PASS_NONE&&data_word(SC_D_background_transparent)==0u){kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;}
    store_word(p(24u)+index*8u,0x80000000u|(kernel<<24u)|shader);
}
//#endif
@compute @workgroup_size(64) fn init_bake(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}
    let tile_index=id.x/p(5u);if(tile_index>=p(3u)){return;}
    let t=p(2u)+tile_index*T_size;let tile_work=id.x%p(5u);
    let work_size=load_word(t+T_work_size);if(tile_work>=work_size){return;}
    let w=load_word(t+T_w);let h=load_word(t+T_h);let samples=load_word(t+T_num_samples);
    let begin=load_word(t+T_path_index_offset);
    if(w==0u||h==0u||samples==0u||w>p(10u)||h>p(10u)/w||samples>p(10u)/(w*h)||work_size!=w*h*samples||work_size>p(5u)||begin>=p(10u)||work_size>p(10u)-begin){fail();return;}
    let path=begin+tile_work;
    // get_work_pixel.
    var pixel_offset=tile_work/samples;var sample_in_tile=tile_work%samples;
    if(data_float(D_integrator_scrambling_distance)<0.9){sample_in_tile=tile_work/(w*h);pixel_offset=tile_work%(w*h);}
    let x=load_word(t+T_x)+pixel_offset%w;let y=load_word(t+T_y)+pixel_offset/w;
    // path_state_init: the signed tile offset wraps in the original uint index, validated before any film access.
    let pixel=load_word(t+T_offset)+x+y*load_word(t+T_stride);
    var sample_id=load_word(t+T_start_sample)+sample_in_tile;
    let stride=data_word(D_film_pass_stride);
    if(pixel>=p(11u)/stride){fail();return;}
    let film=p(4u)+pixel*stride*4u;
    state_u32(S_path_render_pixel_index,path,pixel);state_u16(S_path_queued_kernel,path,0u);
    // film_need_sample_pixel and film_write_sample.
    let adaptive=data_word(D_film_pass_adaptive_aux_buffer);
    if(adaptive!=PASS_NONE&&heap_float(film+(adaptive+3u)*4u)!=0.0){return;}
    let count=data_word(D_film_pass_sample_count);
    if(count!=PASS_NONE){sample_id=atomicAdd(&heap[(film+count*4u)/4u],1u)+load_word(t+T_sample_offset);}
    let primitive=film+data_word(BK_D_pass_bake_primitive)*4u;let differential=film+data_word(BK_D_pass_bake_differential)*4u;
    let triangle=load_word(primitive+8u);
    if(triangle==0xffffffffu){
        // film_write_transparent(kg, 0, 1.0f, buffer): accumulate transparency for empty pixels.
        if((data_word(BK_D_light_pass_flag)&BK_MASK_COMBINED)!=0u){bk_film_add(film+(data_word(BK_D_pass_combined)+3u)*4u,1.0);}
//#if SHADOW_CATCHER
        sc_transparent_only(0u,film,1.0);
//#endif
        return;
    }
    let prim=triangle+data_word(BK_D_bake_tri_offset);let object=data_word(BK_D_bake_object_index);
    if(prim>=table_count(G_tri_vindex)||prim>=table_count(G_tri_shader)||object>=table_count(G_objects)||object>=table_count(G_object_flag)){fail();return;}
    var rng=0u;
    let seed_pass=data_word(BK_D_pass_bake_seed);
    if(seed_pass!=0u){rng=bk_hash(vec3<u32>(load_word(film+seed_pass*4u),0u,0u),1u)^data_word(D_integrator_seed);}
    else{
//#if SAMPLING_SOBOL_BURLEY else         rng=pixel_hash(x,y,data_word(D_integrator_seed));
        // path_rng_pixel_init: the blue-noise patterns place each pixel along one sequence (scrambled Morton curve).
        let blue_noise=data_word(SB_D_sampling_pattern)==SB_BLUE_NOISE_PURE||data_word(SB_D_sampling_pattern)==SB_BLUE_NOISE_FIRST;
        rng=select(pixel_hash(x,y,data_word(D_integrator_seed)),sb_pixel(x,y,data_word(D_integrator_seed)),blue_noise);
//#endif
    }
//#if SAMPLING_SOBOL_BURLEY else     var rand=vec2<f32>(0.5);if(sample_id!=0u){rand=native_rng(sample_id,rng,RNG_FILTER).xy;}
    var rand=vec2<f32>(0.5);if(sample_id!=0u){rand=native_rng_pattern(sample_id,rng,RNG_FILTER,2u).xy;}
//#endif
    // path_state_init_integrator.
    state_u32(S_path_sample,path,sample_id);state_u32(S_path_rng_pixel,path,rng);state_u16(S_path_rng_offset,path,RNG_BOUNCE);
    state_u16(S_path_bounce,path,0u);state_u16(S_path_diffuse_bounce,path,0u);state_u16(S_path_glossy_bounce,path,0u);
    state_u16(S_path_transmission_bounce,path,0u);state_u16(S_path_transparent_bounce,path,0u);
    state_u16(S_path_volume_bounce,path,0u);state_u16(S_path_volume_bounds_bounce,path,0u);
//#if RAY_PORTAL
    // path_state_init_integrator: the portal bounce with KERNEL_FEATURE_NODE_PORTAL.
    state_u16(S_path_portal_bounce,path,0u);
//#endif
    state_u8(S_path_visibility,path,VIS_CAMERA);state_u8(S_path_mnee,path,0u);state_u32(S_path_flag,path,INITIAL_FLAG);
    state_f32(S_path_mis_ray_pdf,path,0.0);state_u32(S_path_min_ray_pdf,path,0x7f7fffffu);
    state_f32(S_path_continuation_probability,path,1.0);state_vec3(S_path_throughput,path,vec3<f32>(1.0));
    state_f32(S_path_optical_depth,path,0.0);state_u32(S_isect_object,path,NONE);state_u32(S_isect_prim,path,NONE);state_u32(S_isect_type,path,PRIM_TYPE_NONE);
    if(volume_enabled()){
        store_word(volume_stack_address(path,0u,VS_object),data_word(D_background_object_index));
        store_word(volume_stack_address(path,0u,VS_shader),data_word(D_background_volume_shader));
        store_word(volume_stack_address(path,1u,VS_object),NONE);store_word(volume_stack_address(path,1u,VS_shader),NONE);
    }
//#if FILM_DENOISING
    if((data_word(D_kernel_features)&DN_KERNEL_FEATURE_DENOISING)!=0u){
        state_u32(S_path_flag,path,INITIAL_FLAG|DN_PATH_RAY_DENOISING_FEATURES);state_vec3(S_path_denoising_feature_throughput,path,vec3<f32>(1.0));}
//#endif
//#if LIGHT_LINKING
    if((data_word(D_kernel_features)&KF_LIGHT_LINKING)!=0u){state_u32(S_path_mis_ray_object,path,data_word(D_background_object_index));}
//#endif
    // Barycentrics and their differentials, as the host's map holds them (Blender's convention).
    var u=heap_float(primitive);var v=heap_float(primitive+4u);
    var dudx=heap_float(differential);var dudy=heap_float(differential+4u);var dvdx=heap_float(differential+8u);var dvdy=heap_float(differential+12u);
    let vertices=native_triangle_vertices(object,prim,PRIMITIVE_TRIANGLE,0.5);
    if(vertices.error!=0u){return;}
    let v0=vertices.a;let v1=vertices.b;let v2=vertices.c;
    // Exactly at a vertex: nudged inwards.
    if((u==0.0||u==1.0)&&(v==0.0||v==1.0)){let nudged=bk_offset_towards_center(v0,v1,v2,u,v);u=nudged.x;v=nudged.y;}
    // bake_jitter_barycentric: up to ten jittered positions inside the triangle, else the centre value.
    for(var i=0u;i<10u;i++){
        let rx=rounded_add(rand.x,-0.5);let ry=rounded_add(rand.y,-0.5);
        let jitter_u=rounded_add(rounded_add(u,rx*dudx),ry*dudy);let jitter_v=rounded_add(rounded_add(v,rx*dvdx),ry*dvdy);
        if(jitter_u>0.0&&jitter_v>0.0&&rounded_add(jitter_u,jitter_v)<1.0){u=jitter_u;v=jitter_v;break;}
        rand=bk_hash_float2(rand);
    }
    // To Cycles' barycentric convention.
    let blender_u=u;u=v;v=rounded_add(rounded_add(1.0,-blender_u),-v);
    let dudx_b=dudx;let dudy_b=dudy;dudx=dvdx;dudy=dvdy;dvdx=rounded_add(-dudx_b,-dvdx);dvdy=rounded_add(-dudy_b,-dvdy);
    // triangle_point_normal: P = w*v0 + u*v1 + v*v2, Ng from the (winding-corrected) cross product.
    let wt=rounded_add(rounded_add(1.0,-u),-v);
    var P=rounded_add3(rounded_add3(wt*v0,u*v1),v*v2);
    let object_flag=load_word(table_at(G_object_flag,object,4u));let applied=(object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
    let swap_winding=applied&&(object_flag&SD_OBJECT_NEGATIVE_SCALE)!=0u;
    let e1=rounded_add3(v1,-v0);let e2=rounded_add3(v2,-v0);
    let cross_normal=rounded_cross(select(e1,e2,swap_winding),select(e2,e1,swap_winding));
    var Ng=surface_native_normalize(cross_normal);
    let shader=load_word(table_at(G_tri_shader,prim,4u));
    let object_address=table_at(G_objects,object,SIZE_objects);
    if(!applied){P=surface_transform_point(object_address+O_tfm,P);}
    let counter=load_word(p(7u)+S_queue_counter);
    if(data_word(BK_D_pass_background)!=PASS_NONE){
        // Environment baking: the world along the direction of the surface point from the origin.
        state_vec3(S_ray_P,path,vec3<f32>(0.0));state_vec3(S_ray_D,path,surface_native_normalize(P));
        state_f32(S_ray_tmin,path,0.0);state_u32(S_ray_tmax,path,0x7f7fffffu);state_f32(S_ray_time,path,0.5);
        state_f32(S_ray_dP,path,0.0);state_f32(S_ray_dD,path,0.0);
        atomicAdd(&heap[(counter+DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND*4u)/4u],1u);state_u16(S_path_queued_kernel,path,DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND);
        return;
    }
    // Surface baking. triangle_smooth_normal: safe_normalize of the interpolated vertex normals, Ng where zero.
    var N=Ng;var n_is_ng=true;
    if((shader&SHADER_SMOOTH_NORMAL)!=0u){
        let vi_address=table_at(G_tri_vindex,prim,SIZE_tri_vindex);
        var ni=vec3<u32>(load_word(vi_address),load_word(vi_address+4u),load_word(vi_address+8u));
        if((object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0u){ni=vec3<u32>(prim*3u)+vec3<u32>(0,1,2);}
        let offset=bitcast<i32>(load_word(object_address+O_normal_offset));let nc=table_count(G_attributes_normal);
        var normals:array<vec3<f32>,3>;
        for(var j=0u;j<3u;j++){
            let at=triangle_frame_index(ni[j],offset,0u,1u,nc);if(at==NONE){return;}
            normals[j]=surface_decode_normal(load_word(table_at(G_attributes_normal,at,SIZE_attributes_normal)));
        }
        let interpolated=surface_normalize(rounded_add3(rounded_add3(wt*normals[0],u*normals[1]),v*normals[2]));
        if(any(interpolated!=vec3<f32>(0))){N=interpolated;n_is_ng=false;}
    }
    if(!applied){N=surface_transform_normal(object_address+O_itfm,N);Ng=surface_transform_normal(object_address+O_itfm,Ng);}
    let shader_index=shader&SHADER_MASK;
    if(shader_index>=table_count(G_shaders)){fail();return;}
    let shader_flags=load_word(table_at(G_shaders,shader_index,SIZE_shaders)+SH_flags);
    // Fast path for position and normal passes not affected by shaders.
    if(data_word(BK_D_pass_position)!=PASS_NONE){bk_film_add3(film+data_word(BK_D_pass_position)*4u,P);return;}
    // A texel on a zero-area triangle: the CPU kernel's geometric normal is not a number (normalize of a zero cross
    // product). Its normal pass gets that value where N is Ng (flat, or a zero interpolated normal), and its path ends
    // wherever it reaches. Refused by name (error bit 65536), never given another value; a smooth normal pass is unaffected.
    let zero_area=rounded_dot(cross_normal,cross_normal)==0.0;
    if(data_word(BK_D_pass_normal)!=PASS_NONE&&(shader_flags&BK_SD_HAS_BUMP)==0u){
        if(zero_area&&n_is_ng){atomicOr(&heap[p(17u)/4u],65536u);return;}
        bk_film_add3(film+data_word(BK_D_pass_normal)*4u,N);return;
    }
    if(zero_area){atomicOr(&heap[p(17u)/4u],65536u);return;}
    var origin=rounded_add3(P,N);var direction=-N;
    if(data_word(BK_D_bake_use_camera)!=0u){
        // camera_direction_from_point.
        let world=p(6u)+D_cam_cameratoworld;let row0=heap_vec4(world);let row1=heap_vec4(world+16u);let row2=heap_vec4(world+32u);
        var D=-vec3<f32>(row0.z,row1.z,row2.z);
        if(data_word(BK_D_cam_type)!=BK_CAMERA_ORTHOGRAPHIC){D=surface_native_normalize(rounded_add3(vec3<f32>(row0.w,row1.w,row2.w),-P));}
        let DN=rounded_dot(D,N);
        // Keep the direction at least 0.2 above the smooth normal; on the backside lerp towards the normal.
        if(DN<=0.2){D=surface_native_normalize(rounded_add3(D,-(N*rounded_add(DN,-0.2))));}
        if(DN<=0.0){D=surface_native_normalize(rounded_add3(D,(-DN)*rounded_add3(N,-D)));}
        // Never behind the geometric normal.
        let DNg=rounded_dot(D,Ng);
        if(DNg<=0.00001){D=surface_native_normalize(rounded_add3(D,-(Ng*rounded_add(DNg,-0.00001))));}
        origin=rounded_add3(P,D);direction=-D;
    }
    state_vec3(S_ray_P,path,origin);state_vec3(S_ray_D,path,direction);
    state_f32(S_ray_tmin,path,0.0);state_u32(S_ray_tmax,path,0x7f7fffffu);state_f32(S_ray_time,path,0.5);
    // Differentials: triangle_dPdudv, transformed, and differential_make_compact of dP.
    var dPdu=e1;var dPdv=e2;
    if(!applied){dPdu=surface_transform_direction(object_address+O_tfm,dPdu);dPdv=surface_transform_direction(object_address+O_tfm,dPdv);}
    let dx=rounded_add3(dPdu*dudx,dPdv*dvdx);let dy=rounded_add3(dPdu*dudy,dPdv*dvdy);
    state_f32(S_ray_dP,path,0.5*rounded_add(bk_length(dx),bk_length(dy)));state_f32(S_ray_dD,path,0.0);
    state_f32(S_isect_t,path,1.0);state_f32(S_isect_u,path,u);state_f32(S_isect_v,path,v);
    state_u32(S_isect_object,path,object);state_u32(S_isect_prim,path,prim);state_u32(S_isect_type,path,PRIMITIVE_TRIANGLE);
    var next=select(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,(shader_flags&SD_HAS_RAYTRACE)!=0u);
    if(data_word(BK_D_use_caustics)!=0u&&(object_flag&BK_SD_OBJECT_CAUSTICS_RECEIVER)!=0u){
        next=BK_K_INTERSECT_MNEE;
    }else{
        // integrator_path_init_sorted: the sort key and its counter.
        let divisor=load_word(p(7u)+S_sort_partition_divisor);let key=shader_index+data_word(D_max_shaders)*(path/divisor);
        let limit=select(p(15u),p(16u),next==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE);
        if(key>=limit){fail();return;}
        let sort_counter=load_word(p(7u)+S_sort_key_counter+next*POINTER_SIZE);
        atomicAdd(&heap[(sort_counter+key*4u)/4u],1u);state_u32(S_path_shader_sort_key,path,key);
    }
    atomicAdd(&heap[(counter+next*4u)/4u],1u);state_u16(S_path_queued_kernel,path,next);
//#if SHADOW_CATCHER
    bk_split_mark(path,film,object,shader_index,next);
//#endif
}
