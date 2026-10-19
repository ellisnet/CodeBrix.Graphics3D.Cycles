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
