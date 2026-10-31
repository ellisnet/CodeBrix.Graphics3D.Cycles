// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Ambient occlusion in the surface stage, modified from the pinned Cycles
// kernels: integrate_surface_ao (integrator/shade_surface.h), surface_shader_ao
// (integrator/surface_shader.h), svm_ao (svm/ao.h) and the local BVH2 traversal
// (bvh/local.h). See native/THIRD-PARTY-NOTICES.txt.
//#if AO_SHADOW
// integrate_surface_ao: the AO shadow ray of fast GI's additive mode
// (KERNEL_FEATURE_AO_ADDITIVE) and of the AO pass (KERNEL_FEATURE_AO_PASS),
// called by connect_surface_light after the direct light, as the pinned kernel
// does before the BSDF bounce. slot is the dispatch invocation; with reserved
// shadow slots (transparent shadows) the AO path takes slot n + id.x.
fn surface_ao(index:u32,slot:u32,sd:SurfaceData,r:SvmResult){
    let features=data_word(D_kernel_features);
    if((features&AO_FEATURE_ADDITIVE)==0u&&(state_byte(S_path_visibility,index)&VIS_CAMERA)==0u){return;}
//#if SHADOW_CATCHER else     // Shadow-catcher passes (PATH_RAY_SHADOW_CATCHER_PASS) never reach this
    // No AO for paths split off for the shadow catcher (PATH_RAY_SHADOW_CATCHER_PASS),
    // as the pinned kernel skips them to avoid double counting.
    if((state_word(S_path_flag,index)&SC_PATH_PASS)!=0u){return;}
//#endif
//#if SHADOW_CATCHER else     // stage: SURFACE_UNSUPPORTED_FLAGS rejects them in prepare_surface.
//#endif
    let rand=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_SURFACE_BSDF).xy;
    // surface_shader_ao: diffuse closure weights times the additive factor and
    // their weighted normal; every sum is rounded before the next term.
    let factor=data_float(D_integrator_ao_additive_factor);var weight=vec3<f32>(0);var normal=vec3<f32>(0);
    for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(!surf_diffuse(c.kind)){continue;}
        weight=rounded_add3(weight,c.weight*factor);
        let average=rounded_add(rounded_add(c.weight.x,c.weight.y),c.weight.z)*(1.0/3.0);
        normal=rounded_add3(normal,c.N*abs(average));}
    var ao_N=sd.N;if(any(normal!=vec3<f32>(0))){ao_N=surface_native_normalize(normal);}
    let direction=surface_cos_hemisphere(ao_N,rand).xyz;
    // shadow_ray_offset (shadow terminator geometry offset); w=0: no self skip.
    var start=vec4<f32>(sd.P,1.0);
    if(terminator_geometry_active(sd)){
//#if SHADOW_TERMINATOR_GEOMETRY else atomicOr(&heap[p(17u)/4u],2u);return;
        start=terminator_shadow_origin(sd,direction);
//#endif
    }
    var origin=start.xyz;if(start.w!=0.0){origin=surf_ray_offset_at(sd,start.xyz,direction);}
    let allocator=load_word(p(7u)+S_next_shadow_path_index)/4u;var shadow:u32;
    if(p(28u)!=0u){shadow=load_word(p(28u))+p(1u)+slot;if(shadow>=p(25u)){fail();return;}}
    else{loop{shadow=atomicLoad(&heap[allocator]);if(shadow>=p(25u)){fail();return;}
        if(atomicCompareExchangeWeak(&heap[allocator],shadow,shadow+1u).exchanged){break;}}}
    state_vec3(S_shadow_ray_P,shadow,origin);state_vec3(S_shadow_ray_D,shadow,direction);
    state_f32(S_shadow_ray_tmin,shadow,0.0);state_f32(S_shadow_ray_tmax,shadow,data_float(D_integrator_ao_bounces_distance));
    state_f32(S_shadow_ray_time,shadow,sd.time);state_f32(S_shadow_ray_dP,shadow,0.0);state_f32(S_shadow_ray_dD,shadow,0.0);
    state_u32(S_shadow_self_object,shadow,select(NONE,sd.object_id,start.w!=0.0));state_u32(S_shadow_self_prim,shadow,select(NONE,sd.prim,start.w!=0.0));
    state_u32(S_shadow_ray_self_light_object,shadow,NONE);state_u32(S_shadow_ray_self_light_prim,shadow,NONE);
    state_u32(S_shadow_path_render_pixel_index,shadow,state_word(S_path_render_pixel_index,index));
    state_u16(S_shadow_path_rng_offset,shadow,state_half(S_path_rng_offset,index));state_u32(S_shadow_path_rng_pixel,shadow,state_word(S_path_rng_pixel,index));
    state_u32(S_shadow_path_sample,shadow,state_word(S_path_sample,index));state_u8(S_shadow_path_visibility,shadow,state_byte(S_path_visibility,index));
    state_u32(S_shadow_path_flag,shadow,state_word(S_path_flag,index)|AO_SHADOW_FLAG);
    state_u16(S_shadow_path_bounce,shadow,state_half(S_path_bounce,index));state_u16(S_shadow_path_transparent_bounce,shadow,state_half(S_path_transparent_bounce,index));
    state_u16(S_shadow_path_volume_bounds_bounce,shadow,state_half(S_path_volume_bounds_bounce,index));
    // surface_shader_alpha: saturate(1 - transparency); transparency is zero without SD_TRANSPARENT.
    let alpha=select(vec3<f32>(1),clamp(vec3<f32>(1)-r.transparency,vec3<f32>(0),vec3<f32>(1)),(r.runtime_flag&SR_TRANSPARENT)!=0u);
    state_vec3(S_shadow_path_throughput,shadow,heap_vec3(state_address(S_path_throughput,index,12u))*alpha);
    if((features&AO_FEATURE_ADDITIVE)!=0u){state_vec3(S_shadow_path_unshadowed_throughput,shadow,weight);}
    state_u16(S_shadow_path_queued_kernel,shadow,K_SHADOW_INTERSECT);let counter=load_word(p(7u)+S_queue_counter)/4u;atomicAdd(&heap[counter+K_SHADOW_INTERSECT],1u);
}
//#endif
//#if SVM_NODE_RAYTRACE
// The Ambient Occlusion shader node's ray tracing (svm_ao), compiled only into
// prepare_surface_raytrace (DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE). Every
// other SVM evaluation runs the node with Cycles' non-raytrace node masks (ao = 1).
var<private> svm_raytrace:bool=false;
// scene_intersect_shadow (opaque, any hit) when local is NONE; otherwise
// scene_intersect_local without hit records: any hit on that object, traversal
// from its own BVH root, self skip by primitive only (intersection_skip_self_local).
// The traversal stack (BVH_STACK_SIZE entries) is this invocation's slot of the
// surface stage's AO stack region (push word 30, surface.cpp), not a private array:
// with object motion the Adreno 730 driver could not link the entry with the
// 192-word private array (per-invocation private memory, as the SVM value stack).
const AO_STACK_BYTES:u32=768u;
fn ao_trace(origin:vec3<f32>,direction:vec3<f32>,tmax:f32,time:f32,self_object:u32,self_prim:u32,local:u32)->bool{
    // intersection_ray_valid
    if((bitcast<u32>(origin.x)&0x7f800000u)==0x7f800000u||(bitcast<u32>(direction.x)&0x7f800000u)==0x7f800000u||dot(direction,direction)==0.0){return false;}
    if(table_count(G_prim_index)==0u){return false;}
    let visibility=select(AO_VISIBILITY_ALL,AO_VISIBILITY_SHADOW_OPAQUE,local==NONE);
    var pos=origin;var dir=ao_clamp_direction(direction);var inv=ao_inverse_direction(dir);
    if(p(30u)==0u||svm_slot>=p(1u)){fail();return false;}let stack_at=p(30u)+svm_slot*AO_STACK_BYTES;store_word(stack_at,0x76543210u);var sp=0i;
    var node=bitcast<i32>(data_word(D_bvh_root));var object=NONE;var visits=0u;
    // The object being entered (the local object, or an instance below a leaf): its
    // transform is applied in one place, at the top of the traversal loop, which keeps
    // a single copy of the motion transform in the entry (Intel compile envelope).
    var enter=NONE;
    if(local!=NONE){
        node=bitcast<i32>(load_word(table_at(G_object_node,local,SIZE_object_node)));
        if((load_word(table_at(G_object_flag,local,SIZE_object_flag))&SD_OBJECT_TRANSFORM_APPLIED)==0u){enter=local;}
    }
    loop{
        visits++;if(visits>1048576u){fail();return false;}
        if(enter!=NONE){
            let obj=table_at(G_objects,enter,SIZE_objects);
            if((load_word(table_at(G_object_flag,enter,SIZE_object_flag))&SD_OBJECT_MOTION)!=0u){
//#if MOTION_OBJECT else atomicOr(&heap[p(17u)/4u],2u);return false;
                let inverse=motion_inverse(motion_object(enter,time));pos=ao_transform_rows(inverse.x,inverse.y,inverse.z,origin,true);dir=ao_clamp_direction(ao_transform_rows(inverse.x,inverse.y,inverse.z,direction,false));
//#endif
            }else{pos=ao_transform_vector(obj+O_itfm,origin,true);dir=ao_clamp_direction(ao_transform_vector(obj+O_itfm,direction,false));}
            inv=ao_inverse_direction(dir);object=enter;enter=NONE;
        }
        while(node>=0&&node!=0x76543210i){
            visits++;if(visits>1048576u){fail();return false;}
            let bounds=ao_bounds_hit(pos,inv,tmax,u32(node),visibility);
            let a=table_at(G_bvh_nodes,u32(node),SIZE_bvh_nodes);
            node=bitcast<i32>(load_word(a+8u));var other=bitcast<i32>(load_word(a+12u));let mask=u32(bounds.x);
            if(mask==3u){if(bounds.z<bounds.y){let tmp=node;node=other;other=tmp;}
                sp++;if(sp>=192){fail();return false;}store_word(stack_at+u32(sp)*4u,bitcast<u32>(other));
            }else if(mask==2u){node=other;}
            else if(mask==0u){if(sp<0){fail();return false;}node=bitcast<i32>(load_word(stack_at+u32(sp)*4u));sp--;}
        }
        if(node<0){
            let a=table_at(G_bvh_leaf_nodes,u32(-node-1),SIZE_bvh_leaf_nodes);
            let first=bitcast<i32>(load_word(a));let end=load_word(a+4u);let kind=load_word(a+12u);
            if(first>=0){
                if(end>table_count(G_prim_index)||u32(first)>end){fail();return false;}
                if(sp<0){fail();return false;}node=bitcast<i32>(load_word(stack_at+u32(sp)*4u));sp--;
                if(!native_triangle_kind(kind&PRIMITIVE_ALL)&&u32(first)!=end){fail();return false;}
                for(var index=u32(first);index<end;index++){
                    var prim_object=object;if(object==NONE){prim_object=load_word(table_at(G_prim_object,index,SIZE_prim_object));}
                    if(local!=NONE&&prim_object!=local){continue;}
                    let prim=load_word(table_at(G_prim_index,index,SIZE_prim_index));
                    if(local!=NONE){if(prim==self_prim){continue;}}
                    else{if(prim_object==self_object&&prim==self_prim){continue;}
                        if((load_word(table_at(G_prim_visibility,index,SIZE_prim_visibility))&visibility)==0u){continue;}}
                    let vertices=native_triangle_vertices(prim_object,prim,kind&PRIMITIVE_ALL,time);if(vertices.error!=0u){return false;}
                    var u:f32;var v:f32;var t:f32;
                    if(c_ray_triangle_intersect(pos,dir,0.0,tmax,vertices.a,vertices.b,vertices.c,&u,&v,&t)){return true;}
                }
            }else{
                // Instances below an object's own root cannot occur; a local
                // traversal from the top-level root (transform-applied object
                // in a scene with instances) is refused, never approximated.
                if(object!=NONE||local!=NONE){atomicOr(&heap[p(17u)/4u],16u);return false;}
                object=load_word(table_at(G_prim_object,u32(-first-1),SIZE_prim_object));enter=object;
                sp++;if(sp>=192){fail();return false;}store_word(stack_at+u32(sp)*4u,bitcast<u32>(0x76543210i));
                node=bitcast<i32>(load_word(table_at(G_object_node,object,SIZE_object_node)));
            }
        }
        if(node==0x76543210i){
            if(sp<0){break;}
            if(object==NONE||local!=NONE){if(local!=NONE&&sp<0){break;}if(local!=NONE){break;}fail();return false;}
            pos=origin;dir=ao_clamp_direction(direction);inv=ao_inverse_direction(dir);object=NONE;
            node=bitcast<i32>(load_word(stack_at+u32(sp)*4u));sp--;if(node==0x76543210i){break;}
        }
    }
    return false;
}
// The closest stage's traversal helpers (wavefront_closest.wgsl), under their own
// names because the closest module is not part of the surface module.
fn ao_clamp_direction(d:vec3<f32>)->vec3<f32>{
    let tiny=8.271806E-25;let signed_tiny=bitcast<vec3<f32>>((bitcast<vec3<u32>>(d)&vec3<u32>(0x80000000u))|vec3<u32>(bitcast<u32>(tiny)));
    return select(signed_tiny,d,abs(d)>vec3<f32>(tiny));
}
fn ao_inverse_direction(d:vec3<f32>)->vec3<f32>{return vec3<f32>(rounded_divide(1.0,d.x),rounded_divide(1.0,d.y),rounded_divide(1.0,d.z));}
fn ao_transform_row(t:vec4<f32>,v:vec3<f32>,point:bool)->f32{
    if(NATIVE_TRIANGLE_ARITHMETIC==0u){
        let w=select(bitcast<f32>(bitcast<u32>(t.w)&0x80000000u),t.w,point);
        return rounded_add(rounded_add(v.x*t.x,v.y*t.y),rounded_add(v.z*t.z,w));
    }
    var sum=v.z*t.z;if(point){sum=rounded_add(t.w,sum);}
    return rounded_add(rounded_add(sum,v.y*t.y),v.x*t.x);
}
fn ao_transform_rows(x:vec4<f32>,y:vec4<f32>,z:vec4<f32>,v:vec3<f32>,point:bool)->vec3<f32>{
    let rows=array<vec4<f32>,3>(x,y,z);var result:vec3<f32>;
    for(var i=0u;i<3u;i++){result[i]=ao_transform_row(rows[i],v,point);}
    return result;
}
fn ao_transform_vector(a:u32,v:vec3<f32>,point:bool)->vec3<f32>{return ao_transform_rows(heap_vec4(a),heap_vec4(a+16u),heap_vec4(a+32u),v,point);}
fn ao_bounds_hit(origin:vec3<f32>,inv:vec3<f32>,tmax:f32,node:u32,visibility:u32)->vec3<f32>{
    let a=table_at(G_bvh_nodes,node,SIZE_bvh_nodes);
    if(node>=table_count(G_bvh_nodes)||table_count(G_bvh_nodes)-node<4u){fail();return vec3<f32>(0);}
    let x=heap_vec4(a+16u);let y=heap_vec4(a+32u);let z=heap_vec4(a+48u);
    let lo0=(vec3<f32>(x.x,y.x,z.x)-origin)*inv;let hi0=(vec3<f32>(x.z,y.z,z.z)-origin)*inv;
    let lo1=(vec3<f32>(x.y,y.y,z.y)-origin)*inv;let hi1=(vec3<f32>(x.w,y.w,z.w)-origin)*inv;
    let near0=max(max(0.0,min(lo0.x,hi0.x)),max(min(lo0.y,hi0.y),min(lo0.z,hi0.z)));
    let far0=min(min(tmax,max(lo0.x,hi0.x)),min(max(lo0.y,hi0.y),max(lo0.z,hi0.z)));
    let near1=max(max(0.0,min(lo1.x,hi1.x)),max(min(lo1.y,hi1.y),min(lo1.z,hi1.z)));
    let far1=min(min(tmax,max(lo1.x,hi1.x)),min(max(lo1.y,hi1.y),max(lo1.z,hi1.z)));
    let mask=select(0u,1u,far0>=near0&&(load_word(a)&visibility)!=0u)|select(0u,2u,far1>=near1&&(load_word(a+4u)&visibility)!=0u);
    return vec3<f32>(f32(mask),near0,near1);
}
// svm_ao: the fraction of num_samples cosine-distributed rays around N (or -N
// with inside) that reach max_dist unoccluded; only_local tests the shading
// object alone. The direction is sample_uniform_disk lifted to the hemisphere in
// make_orthonormals(N), the arithmetic of surface_cos_hemisphere.
fn svm_ao_occlusion(index:u32,sd:SurfaceData,normal:vec3<f32>,distance:f32,samples:u32,flags:u32)->f32{
    var max_dist=distance;if((flags&AO_NODE_GLOBAL_RADIUS)!=0u){max_dist=data_float(D_integrator_ao_bounces_distance);}
    if(max_dist<=0.0||samples<1u||sd.object_id==NONE){return 1.0;}
    var N=normal;if((flags&AO_NODE_INSIDE)!=0u){N=-N;}
    let sample_id=state_word(S_path_sample,index);let pixel=state_word(S_path_rng_pixel,index);let offset=state_half(S_path_rng_offset,index);
    var unoccluded=0u;
    for(var i=0u;i<samples;i++){
        let rand=native_rng(sample_id*samples+i,pixel,offset+AO_PRNG_SURFACE_AO).xy;
        let D=surface_cos_hemisphere(N,rand).xyz;
        let local=select(NONE,sd.object_id,(flags&AO_NODE_ONLY_LOCAL)!=0u);
        if(!ao_trace(sd.P,D,max_dist,sd.time,sd.object_id,sd.prim,local)){unoccluded++;}
        if(load_word(p(17u))!=0u){return 1.0;}
    }
    return f32(unoccluded)/f32(samples);
}
// DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE: prepare_surface with the AO
// node's ray tracing (KERNEL_FEATURE_NODE_MASK_SURFACE). The other surface stages
// are shared with the SHADE_SURFACE queue (push word 29 names the queue).
@compute @workgroup_size(64) fn prepare_surface_raytrace(@builtin(global_invocation_id) id:vec3<u32>){
    svm_raytrace=true;surface_prepare(id);
}
//#endif
