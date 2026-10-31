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
//#if SVM_NODE_AO
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
//#endif
// DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE: prepare_surface with the AO
// node's ray tracing (KERNEL_FEATURE_NODE_MASK_SURFACE). The other surface stages
// are shared with the SHADE_SURFACE queue (push word 29 names the queue).
@compute @workgroup_size(64) fn prepare_surface_raytrace(@builtin(global_invocation_id) id:vec3<u32>){
    svm_raytrace=true;surface_prepare(id);
}
//#endif
//#if SVM_NODE_RAYTRACE
//#if SVM_RAYTRACE_NODES
// SPDX-FileCopyrightText: 2011-2025 Blender Foundation (svm/raycast.h; Apache-2.0)
// The Bevel and Raycast nodes' ray tracing (pinned svm/bevel.h, svm/raycast.h,
// bvh/local.h with geom/geom_intersect.h local_intersect_get_record_index), compiled
// only into prepare_surface_raytrace like the AO node's.
// One traversal for both nodes (so the motion transform has one call site):
// scene_intersect_local with hit records (local = the shading object): from the
// object's own BVH root with its instance transform (the shading point's motion
// transform for a moving object, identical to motion_object at the same time),
// the prims of that object only, self skip by primitive, every hit recorded (no
// tmax shrinking): with lcg the native reservoir of RAYTRACE_LOCAL_MAX_HITS
// records (duplicates by t dropped), without it the closest hit only. With local
// NONE, the Raycast node's scene_intersect (trace_native_filtered, closest hit):
// from the scene root, instances pushed and popped, the visibility test on nodes
// and prims, self skip by object and primitive; the hit object's motion transform
// is kept for the hit setup. The stack is this invocation's AO stack slot (push
// word 30).
struct LocalHits {count:u32,lcg:u32,kind:u32,object:u32,prim:array<u32,4>,u:array<f32,4>,v:array<f32,4>,t:array<f32,4>,motion_tfm:NativeTransform,motion_itfm:NativeTransform}
fn raytrace_bounds(origin:vec3<f32>,inv:vec3<f32>,tmin:f32,tmax:f32,node:u32,visibility:u32)->vec3<f32>{
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
fn raytrace_traverse(sd:SurfaceData,origin:vec3<f32>,direction:vec3<f32>,tmin:f32,tmax:f32,local:u32,self_object:u32,self_prim:u32,visibility:u32,lcg:u32,reservoir:bool)->LocalHits{
    var hits:LocalHits;hits.lcg=lcg;hits.object=local;
    let global=local==NONE;
    if(table_count(G_prim_index)==0u||(!global&&local>=table_count(G_object_node))){return hits;}
    let dir0=ao_clamp_direction(direction);let inv0=ao_inverse_direction(dir0);
    var pos=origin;var dir=dir0;var inv=inv0;var object=NONE;var applied=false;var motion=NativeTransform();var motion_inverse_tfm=NativeTransform();
    var best=tmax;var enter=NONE;
    if(!global){
        // Prims are filtered by object only in a transform-applied object (its prims sit in the scene BVH).
        applied=(load_word(table_at(G_object_flag,local,SIZE_object_flag))&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
        if(applied){object=local;}else{enter=local;}
    }
    if(p(30u)==0u||svm_slot>=p(1u)){fail();return hits;}let stack_at=p(30u)+svm_slot*AO_STACK_BYTES;store_word(stack_at,0x76543210u);var sp=0i;
    var node=bitcast<i32>(data_word(D_bvh_root));
    if(!global){node=bitcast<i32>(load_word(table_at(G_object_node,local,SIZE_object_node)));}
    var visits=0u;
    loop{
        // Instance push (the local object at the start, an instance leaf of the scene
        // traversal): one transform site. A moving object takes the shading point's
        // motion transform when it is the shading object (motion_object at the same
        // time), otherwise motion_object here.
        if(enter!=NONE){
            object=enter;enter=NONE;var rows:NativeTransform;
            if((load_word(table_at(G_object_flag,object,SIZE_object_flag))&SD_OBJECT_MOTION)!=0u){
//#if MOTION_OBJECT else atomicOr(&heap[p(17u)/4u],2u);return hits;
                if(object==sd.object_id){motion=sd.motion_tfm;motion_inverse_tfm=sd.motion_itfm;}
                else{motion=motion_object(object,sd.time);motion_inverse_tfm=motion_inverse(motion);}
                rows=motion_inverse_tfm;
//#endif
            }else{let obj=table_at(G_objects,object,SIZE_objects)+O_itfm;rows=NativeTransform(heap_vec4(obj),heap_vec4(obj+16u),heap_vec4(obj+32u));}
            pos=ao_transform_rows(rows.x,rows.y,rows.z,origin,true);dir=ao_clamp_direction(ao_transform_rows(rows.x,rows.y,rows.z,direction,false));inv=ao_inverse_direction(dir);
        }
        visits++;if(visits>1048576u){fail();return hits;}
        while(node>=0&&node!=0x76543210i){
            visits++;if(visits>1048576u){fail();return hits;}
            let bounds=raytrace_bounds(pos,inv,tmin,select(tmax,best,global),u32(node),visibility);
            let a=table_at(G_bvh_nodes,u32(node),SIZE_bvh_nodes);
            node=bitcast<i32>(load_word(a+8u));var other=bitcast<i32>(load_word(a+12u));let mask=u32(bounds.x);
            if(mask==3u){if(bounds.z<bounds.y){let tmp=node;node=other;other=tmp;}
                sp++;if(sp>=192){fail();return hits;}store_word(stack_at+u32(sp)*4u,bitcast<u32>(other));
            }else if(mask==2u){node=other;}
            else if(mask==0u){if(sp<0){fail();return hits;}node=bitcast<i32>(load_word(stack_at+u32(sp)*4u));sp--;}
        }
        if(node<0){
            let a=table_at(G_bvh_leaf_nodes,u32(-node-1),SIZE_bvh_leaf_nodes);
            let first=bitcast<i32>(load_word(a));let end=load_word(a+4u);let kind=load_word(a+12u);
            if(first>=0){
                if(end>table_count(G_prim_index)||u32(first)>end){fail();return hits;}
                if(sp<0){fail();return hits;}node=bitcast<i32>(load_word(stack_at+u32(sp)*4u));sp--;
                if(!native_triangle_kind(kind&PRIMITIVE_ALL)&&u32(first)!=end){fail();return hits;}
                for(var index=u32(first);index<end;index++){
                    var prim_object=object;
                    if(global){
                        if(object==NONE){prim_object=load_word(table_at(G_prim_object,index,SIZE_prim_object));}
                        if((load_word(table_at(G_prim_visibility,index,SIZE_prim_visibility))&visibility)==0u){continue;}
                    }else if(applied&&load_word(table_at(G_prim_object,index,SIZE_prim_object))!=local){continue;}
                    let prim=load_word(table_at(G_prim_index,index,SIZE_prim_index));
                    if(prim==self_prim&&(!global||prim_object==self_object)){continue;}
                    let vertices=native_triangle_vertices(prim_object,prim,kind&PRIMITIVE_ALL,sd.time);if(vertices.error!=0u){return hits;}
                    var u:f32;var v:f32;var t:f32;
                    if(!c_ray_triangle_intersect(pos,dir,tmin,select(tmax,best,global),vertices.a,vertices.b,vertices.c,&u,&v,&t)){continue;}
                    // local_intersect_get_record_index; the closest hit otherwise
                    var slot=0u;
                    if(reservoir){
                        var duplicate=false;for(var i=0u;i<min(RAYTRACE_LOCAL_MAX_HITS,hits.count);i++){if(hits.t[i]==t){duplicate=true;}}
                        if(duplicate){continue;}
                        hits.count++;
                        if(hits.count<=RAYTRACE_LOCAL_MAX_HITS){slot=hits.count-1u;}
                        else{hits.lcg=1103515245u*hits.lcg+12345u;slot=hits.lcg%hits.count;if(slot>=RAYTRACE_LOCAL_MAX_HITS){continue;}}
                    }else if(global){best=t;hits.count=1u;hits.object=prim_object;hits.motion_tfm=motion;hits.motion_itfm=motion_inverse_tfm;}
                    else{if(hits.count!=0u&&t>hits.t[0]){continue;}hits.count=1u;}
                    hits.kind=kind&PRIMITIVE_ALL;hits.prim[slot]=prim;hits.u[slot]=u;hits.v[slot]=v;hits.t[slot]=t;
                }
            }else{
                // An instance leaf: only the scene traversal enters it.
                if(!global){atomicOr(&heap[p(17u)/4u],16u);return hits;}
                if(object!=NONE){fail();return hits;}
                enter=load_word(table_at(G_prim_object,u32(-first-1),SIZE_prim_object));
                sp++;if(sp>=192){fail();return hits;}store_word(stack_at+u32(sp)*4u,0x76543210u);
                node=bitcast<i32>(load_word(table_at(G_object_node,enter,SIZE_object_node)));
            }
        }
        if(node==0x76543210i){
            if(!global||sp<=0){break;}
            if(object==NONE){fail();return hits;}
            pos=origin;dir=dir0;inv=inv0;object=NONE;
            node=bitcast<i32>(load_word(stack_at+u32(sp)*4u));sp--;if(node==0x76543210i){break;}
        }
    }
    return hits;
}
// The geometry of a hit (shader_setup_from_ray, the steps of surface_setup_hit with
// the hit object's motion transform from the traversal or the shading point instead
// of a second motion_object, and without the unused differentials), without the
// backfacing flip of N and Ng when `facing` is false (bevel's hit normal and smooth
// normal).
fn raytrace_hit_geometry(hits:LocalHits,hit:u32,shading:SurfaceData,ray_direction:vec3<f32>,facing:bool)->SurfaceData{
    var sd:SurfaceData;
    sd.primitive_type=hits.kind;sd.object_id=hits.object;sd.prim=hits.prim[hit];
    if(!native_triangle_kind(sd.primitive_type)||sd.object_id>=table_count(G_objects)||sd.object_id>=table_count(G_object_flag)||
       sd.prim>=table_count(G_tri_vindex)||sd.prim>=table_count(G_tri_shader)){surface_failure(&sd);return sd;}
    let object=table_at(G_objects,sd.object_id,SIZE_objects);
    sd.object_flag=load_word(table_at(G_object_flag,sd.object_id,4u));
    sd.u=hits.u[hit];sd.v=hits.v[hit];
    sd.ray_length=hits.t[hit];sd.time=shading.time;
    sd.wi=-ray_direction;
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){
//#if MOTION_OBJECT else atomicOr(&heap[p(17u)/4u],2u);surface_failure(&sd);return sd;
        if(sd.object_id==shading.object_id){sd.motion_tfm=shading.motion_tfm;sd.motion_itfm=shading.motion_itfm;}
        else{sd.motion_tfm=hits.motion_tfm;sd.motion_itfm=hits.motion_itfm;}
//#endif
    }
    let vi_address=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);
    let vi=vec3<u32>(load_word(vi_address),load_word(vi_address+4u),load_word(vi_address+8u));
    let vertices=native_triangle_vertices(sd.object_id,sd.prim,sd.primitive_type,sd.time);
    if(vertices.error!=0u){surface_failure(&sd);return sd;}let a=vertices.a;let b=vertices.b;let c=vertices.c;
    sd.dPdu=b-a;sd.dPdv=c-a;sd.P=rounded_add3(rounded_add3(a,sd.u*sd.dPdu),sd.v*sd.dPdv);
    let applied=(sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
    if(!applied){sd.P=surface_object_point(sd,sd.P,false);}
    let swap_winding=applied&&(sd.object_flag&SD_OBJECT_NEGATIVE_SCALE)!=0u;
    let cross_normal=rounded_cross(select(sd.dPdu,sd.dPdv,swap_winding),select(sd.dPdv,sd.dPdu,swap_winding));
    if(dot(cross_normal,cross_normal)==0.0){surface_failure(&sd);return sd;}
    sd.Ng=surface_native_normalize(cross_normal);sd.N=sd.Ng;
    sd.shader=load_word(table_at(G_tri_shader,sd.prim,4u));let shader_id=sd.shader&SHADER_MASK;
    if(shader_id>=table_count(G_shaders)){surface_failure(&sd);return sd;}
    if((sd.shader&SHADER_SMOOTH_NORMAL)!=0u){
        var ni=vi;
        if((sd.object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0u){ni=vec3<u32>(sd.prim*3u)+vec3<u32>(0,1,2);}
        let offset=bitcast<i32>(load_word(object+O_normal_offset));
        let nc=table_count(G_attributes_normal);var normals:array<vec3<f32>,3>;
        var frame:TriangleTime;var stride=load_word(object+O_numverts);
        if((sd.object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0u){stride=3u*load_word(object+O_numprims);}
        if(sd.primitive_type==PRIMITIVE_MOTION_TRIANGLE){frame=triangle_motion_time(sd.object_id,sd.time);}
        for(var j=0u;j<3u;j++){
            let at=triangle_frame_index(ni[j],offset,frame.a,stride,nc);
            if(at==NONE){surface_failure(&sd);return sd;}
            normals[j]=surface_decode_normal(load_word(table_at(G_attributes_normal,at,SIZE_attributes_normal)));
            if(sd.primitive_type==PRIMITIVE_MOTION_TRIANGLE){
                let next=triangle_frame_index(ni[j],offset,frame.b,stride,nc);if(next==NONE){surface_failure(&sd);return sd;}
                let n=surface_decode_normal(load_word(table_at(G_attributes_normal,next,SIZE_attributes_normal)));
                normals[j]=surface_native_normalize(rounded_add3((1.0-frame.t)*normals[j],frame.t*n));
            }
        }
        let weight=rounded_add(rounded_add(1.0,-sd.u),-sd.v);
        let interpolated=surface_normalize(rounded_add3(rounded_add3(weight*normals[0],sd.u*normals[1]),sd.v*normals[2]));
        if(any(interpolated!=vec3<f32>(0))){sd.N=interpolated;}
    }
    if(!applied){
        // The same transforms in the same order as surface_setup_hit, each through one
        // call site (drivers inline every call).
        var normals=array<vec3<f32>,2>(sd.N,sd.Ng);var directions=array<vec3<f32>,2>(sd.dPdu,sd.dPdv);
        for(var i=0u;i<2u;i++){normals[i]=surface_object_normal(sd,normals[i],false);}
        for(var i=0u;i<2u;i++){directions[i]=surface_object_direction(sd,directions[i],false);}
        sd.N=normals[0];sd.Ng=normals[1];sd.dPdu=directions[0];sd.dPdv=directions[1];
    }
    sd.shader_flag=load_word(table_at(G_shaders,shader_id,SIZE_shaders)+SH_flags);
    if(dot(sd.Ng,sd.wi)<0.0){sd.runtime_flag|=SR_BACKFACING;if(facing){sd.N=-sd.N;sd.Ng=-sd.Ng;}sd.dPdu=-sd.dPdu;sd.dPdv=-sd.dPdv;}
    // The rays have zero differentials (differential_zero_compact), and nothing reads the
    // hit's du/dv: the raycast attributes are evaluated without derivatives.
    sd.dI=0.0;sd.dP=0.0;
    return sd;
}
//#endif
//#if SVM_NODE_BEVEL
fn bevel_cubic(radius:f32,r:f32)->f32{
    if(r>=radius){return 0.0;}let rm5=((radius*radius)*(radius*radius))*radius;let f=rounded_add(radius,-r);
    return rounded_divide(10.0*((f*f)*f),rm5*PI);
}
fn bevel_root(xi:f32)->f32{
    var x=0.25;
    for(var i=0u;i<10u;i++){
        let x2=x*x;let x3=x2*x;let nx=rounded_add(1.0,-x);
        let f=rounded_add(rounded_add(rounded_add(rounded_add(10.0*x2,-(20.0*x3)),(15.0*x2)*x2),-((4.0*x2)*x3)),-xi);
        let derivative=(20.0*(x*nx))*(nx*nx);
        if(abs(f)<1e-6||derivative==0.0){break;}
        x=clamp(rounded_add(x,-rounded_divide(f,derivative)),0.0,1.0);
    }
    return x;
}
// lcg_state_init(rng_pixel, rng_offset, sample, 0x64c6a40e): hash_uint3 (Jenkins).
fn bevel_lcg(index:u32)->u32{
    var a=0xdeadbf08u+(state_word(S_path_rng_pixel,index)^0x64c6a40eu);
    var b=0xdeadbf08u+state_half(S_path_rng_offset,index);var c=0xdeadbf08u+state_word(S_path_sample,index);
    c^=b;c-=(b<<14u)|(b>>18u);a^=c;a-=(c<<11u)|(c>>21u);
    b^=a;b-=(a<<25u)|(a>>7u);c^=b;c-=(b<<16u)|(b>>16u);
    a^=c;a-=(c<<4u)|(c>>28u);b^=a;b-=(a<<14u)|(a>>18u);c^=b;c-=(b<<24u)|(b>>8u);return c;
}
//#endif
//#if SVM_RAYTRACE_NODES
// svm_bevel and svm_raycast (one function, so their local traversal, closest-hit
// traversal and hit setup each have one call site). svm_bevel: normals of nearby
// surfaces of the same object averaged by num_samples local rays from a disk around
// one of three axes (BSSRDF importance sampling), each hit weighted by MIS over the
// axes and the cubic profile. svm_raycast: the hit of a ray from `position` along
// `direction` up to `distance` (local: this object's closest hit; otherwise
// scene_intersect with PATH_RAY_VISIBILITY_RAYCAST), set up as shader data
// (object_id NONE: no hit).
struct RaytraceNodeResult {N:vec3<f32>,hit:SurfaceData,error:u32}
fn raytrace_node_trace(index:u32,sd:SurfaceData,bevel:bool,radius:f32,samples:u32,position:vec3<f32>,direction:vec3<f32>,distance:f32,only_local:bool,bump_filter_width:f32)->RaytraceNodeResult{
    var result:RaytraceNodeResult;result.N=sd.N;result.hit.object_id=NONE;
    var iterations=1u;var lcg=0u;var sum=vec3<f32>(0);var basis:MicrofacetBasis;var sample_id=0u;var pixel=0u;var offset=0u;
    var tmin=0.0;var avoid=false;
    if(bevel){
//#if SVM_NODE_BEVEL
        if(radius<=0.0||samples<1u||sd.object_id==NONE||table_count(G_prim_index)==0u){return result;}
        if(heap_float(state_address(S_path_min_ray_pdf,index,4u))<8.0){return result;}
        lcg=bevel_lcg(index);iterations=samples;
        sample_id=state_word(S_path_sample,index);pixel=state_word(S_path_rng_pixel,index);offset=state_half(S_path_rng_offset,index);
        var basis_closure:SvmClosure;basis_closure.N=sd.Ng;basis=ggx_basis(basis_closure);
//#endif
    }else{
        if(distance<=0.0||sd.object_id==NONE||table_count(G_prim_index)==0u){return result;}
        if(bump_filter_width>0.0){tmin=(bump_filter_width*sd.dP)*1.1;}else{avoid=all(position==sd.P);}
    }
    for(var i=0u;i<iterations;i++){
        var origin=position;var dir=direction;var ray_tmin=tmin;var tmax=distance;var self_prim=select(NONE,sd.prim,avoid);
        var disk_N=sd.Ng;var disk_T=basis.X;var disk_B=basis.Y;var pick=vec3<f32>(0.5,0.25,0.25);var disk_r=0.0;
        if(bevel){
//#if SVM_NODE_BEVEL
            var rand=native_rng(sample_id*samples+i,pixel,offset+RAYTRACE_PRNG_SURFACE_BEVEL).xy;
            if(rand.x<0.5){rand.x*=2.0;}
            else if(rand.x<0.75){disk_N=basis.X;disk_T=sd.Ng;pick=vec3<f32>(0.25,0.5,0.25);rand.x=rounded_add(rand.x,-0.5)*4.0;}
            else{disk_N=basis.Y;disk_B=sd.Ng;pick=vec3<f32>(0.25,0.25,0.5);rand.x=rounded_add(rand.x,-0.75)*4.0;}
            let phi=6.2831855*rand.x;disk_r=bevel_root(rand.y)*radius;let disk_height=rounded_sqrt(max(rounded_add(radius*radius,-(disk_r*disk_r)),0.0));
            let sc=trig_sincos_wide(phi);let disk_P=rounded_add3((disk_r*sc.y)*disk_T,(disk_r*sc.x)*disk_B);
            origin=rounded_add3(rounded_add3(sd.P,disk_N*disk_height),disk_P);dir=-disk_N;ray_tmin=0.0;tmax=2.0*disk_height;self_prim=NONE;
//#endif
        }
        let global=!bevel&&!only_local;
        let hits=raytrace_traverse(sd,origin,dir,ray_tmin,tmax,select(sd.object_id,NONE,global),select(NONE,sd.object_id,avoid),self_prim,
            select(AO_VISIBILITY_ALL,RAYTRACE_VISIBILITY_RAYCAST,global),lcg,bevel);lcg=hits.lcg;
        if(load_word(p(17u))!=0u){result.error=1u;return result;}
        for(var hit=0u;hit<min(hits.count,RAYTRACE_LOCAL_MAX_HITS);hit++){
            let geometry=raytrace_hit_geometry(hits,hit,sd,dir,!bevel);
            if(geometry.error!=0u){result.error=1u;return result;}
            if(!bevel){result.hit=geometry;break;}
//#if SVM_NODE_BEVEL
            let pdf_N=pick.x*abs(rounded_dot(disk_N,geometry.Ng));let pdf_T=pick.y*abs(rounded_dot(disk_T,geometry.Ng));let pdf_B=pick.z*abs(rounded_dot(disk_B,geometry.Ng));
            var w=rounded_divide(pdf_N,rounded_add(rounded_add(pdf_N*pdf_N,pdf_T*pdf_T),pdf_B*pdf_B));
            if(hits.count>RAYTRACE_LOCAL_MAX_HITS){w*=rounded_divide(f32(hits.count),f32(RAYTRACE_LOCAL_MAX_HITS));}
            let difference=rounded_add3(geometry.P,-sd.P);let r=rounded_sqrt(rounded_dot(difference,difference));
            w*=rounded_divide(bevel_cubic(radius,r),bevel_cubic(radius,disk_r));
            sum=rounded_add3(sum,w*geometry.N);
//#endif
        }
    }
    if(bevel){let N=rounded_safe_normalize(sum);if(!all(N==vec3<f32>(0))){result.N=select(N,-N,(sd.runtime_flag&SR_BACKFACING)!=0u);}}
    return result;
}
//#endif
//#endif
