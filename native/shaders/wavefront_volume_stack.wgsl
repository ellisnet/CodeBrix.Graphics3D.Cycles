// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native CPU volume_all BVH2 hit recording and intersect_volume_stack
// initialization, using original scene tables and the native hit limit/order.
struct VolumeHits {hits:array<NativeHit,VS_MAX_HITS>,count:u32}
fn volume_all_hits(origin:vec3<f32>,direction:vec3<f32>,visibility:u32,limit:u32)->VolumeHits{
    let tmin=0.0;let tmax=bitcast<f32>(0x7f7fffffu);
    var result:VolumeHits;
    if(table_count(G_prim_index)==0u){return result;}
    var pos=origin;var dir=clamp_direction(direction);var inv=inverse_direction(dir);
    var stack:array<i32,192>;stack[0]=0x76543210i;var sp=0i;
    var node=bitcast<i32>(data_word(D_bvh_root));var object=NONE;var visits=0u;
    loop{
        // Malformed cycles in a scene table must fail instead of hanging a GPU.
        visits++;if(visits>1048576u){fail();return result;}
        while(node>=0&&node!=0x76543210i){
            visits++;if(visits>1048576u){fail();return result;}
            let bounds=bounds_hit(pos,inv,tmin,tmax,u32(node),visibility);
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
                    if((load_word(table_at(G_object_flag,prim_object,SIZE_object_flag))&VS_OBJECT_HAS_VOLUME)==0u){continue;}
                    let shader=load_word(table_at(G_tri_shader,prim,SIZE_tri_shader))&SHADER_MASK;
                    if((load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags)&VS_HAS_VOLUME)==0u){continue;}
                    if((load_word(table_at(G_prim_visibility,index,SIZE_prim_visibility))&visibility)==0u){continue;}
                    let obj=table_at(G_objects,prim_object,SIZE_objects);let offset=load_word(obj+O_position_offset);
                    let vi=table_at(G_tri_vindex,prim,SIZE_tri_vindex);
                    let va=heap_vec3(table_at(G_tri_verts,offset+load_word(vi),SIZE_tri_verts));
                    let vb=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+4u),SIZE_tri_verts));
                    let vc=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+8u),SIZE_tri_verts));
                    var u:f32;var v:f32;var t:f32;
                    if(c_ray_triangle_intersect(pos,dir,tmin,tmax,va,vb,vc,&u,&v,&t)){
                        result.hits[result.count]=NativeHit(t,u,v,prim_object,prim,PRIMITIVE_TRIANGLE);result.count++;
                        if(result.count==limit){return result;}
                    }
                }
            }else{
                if(object!=NONE){fail();return result;}
                object=load_word(table_at(G_prim_object,u32(-first-1),SIZE_prim_object));
                let obj=table_at(G_objects,object,SIZE_objects);
                pos=transform_vector(obj+O_itfm,origin,true);dir=clamp_direction(transform_vector(obj+O_itfm,direction,false));inv=inverse_direction(dir);
                sp++;if(sp>=192){fail();return result;}stack[u32(sp)]=0x76543210i;
                node=bitcast<i32>(load_word(table_at(G_object_node,object,SIZE_object_node)));
            }
        }
        if(node==0x76543210i){
            if(sp<0){break;}
            if(object==NONE){fail();return result;}
            pos=origin;dir=clamp_direction(direction);inv=inverse_direction(dir);object=NONE;
            node=stack[u32(sp)];sp--;if(node==0x76543210i){break;}
        }
    }
    return result;
}
fn volume_boundary_backfacing(hit:NativeHit,direction:vec3<f32>)->bool{
    let object=table_at(G_objects,hit.object,SIZE_objects);let flags=load_word(table_at(G_object_flag,hit.object,SIZE_object_flag));
    let offset=load_word(object+O_position_offset);let vi=table_at(G_tri_vindex,hit.prim,SIZE_tri_vindex);
    let a=heap_vec3(table_at(G_tri_verts,offset+load_word(vi),SIZE_tri_verts));
    let b=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+4u),SIZE_tri_verts));
    let c=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+8u),SIZE_tri_verts));
    let applied=(flags&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
    var cross_normal=rounded_cross(b-a,c-a);if(applied&&(flags&SD_OBJECT_NEGATIVE_SCALE)!=0u){cross_normal=rounded_cross(c-a,b-a);}
    var normal=rounded_normalize(cross_normal);if(!applied){normal=surface_transform_normal(object+O_itfm,normal);}
    return dot(normal,-direction)<0.0;
}
@compute @workgroup_size(64) fn intersect_volume_stack(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK){fail();return;}
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=vec3<f32>(0,0,1);let size=data_word(D_volume_stack_size);
    var result=volume_all_hits(origin,direction,VIS_CAMERA,2u*size);if(load_word(p(17u))!=0u){return;}
    // Native qsort orders hits by distance; retain traversal order for equal t.
    for(var i=1u;i<result.count;i++){let hit=result.hits[i];var j=i;while(j>0u&&result.hits[j-1u].t>hit.t){result.hits[j]=result.hits[j-1u];j--;}result.hits[j]=hit;}
    var objects:array<u32,VS_MAX>;var shaders:array<u32,VS_MAX>;var enclosed:array<u32,VS_MAX_HITS>;var enclosed_count=0u;var count=0u;
    if(data_word(D_background_volume_shader)!=NONE){objects[0]=data_word(D_background_object_index);shaders[0]=data_word(D_background_volume_shader);count=1u;}
    for(var i=0u;i<result.count;i++){let hit=result.hits[i];
        if(volume_boundary_backfacing(hit,direction)){
            var add=true;for(var j=0u;j<enclosed_count;j++){if(enclosed[j]==hit.object){add=false;break;}}
            for(var j=0u;j<count;j++){if(objects[j]==hit.object){add=false;break;}}
            if(add&&count<size-1u){objects[count]=hit.object;shaders[count]=load_word(table_at(G_tri_shader,hit.prim,SIZE_tri_shader));count++;}
        }else{if(enclosed_count>=VS_MAX_HITS){fail();return;}enclosed[enclosed_count]=hit.object;enclosed_count++;}
    }
    if(load_word(p(17u))!=0u){return;}
    for(var i=0u;i<count;i++){store_word(volume_stack_address(index,i,VS_object),objects[i]);store_word(volume_stack_address(index,i,VS_shader),shaders[i]);}
    store_word(volume_stack_address(index,count,VS_object),NONE);store_word(volume_stack_address(index,count,VS_shader),NONE);
    let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[counter/4u+DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK],1u);atomicAdd(&heap[counter/4u+K_CLOSEST],1u);state_u16(S_path_queued_kernel,index,K_CLOSEST);
}
