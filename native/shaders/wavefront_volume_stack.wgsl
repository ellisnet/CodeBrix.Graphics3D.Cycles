// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native CPU volume_all BVH2 hit recording and intersect_volume_stack
// initialization, using original scene tables and the native hit limit/order.
// Hits are kept as parallel scalar arrays and filled through a pointer. The
// Adreno 640 (EV031.31.04.01) compiler collapsed a dynamically indexed
// array<NativeHit,VS_MAX_HITS> (every entry read back as the last hit), which
// corrupted the initial volume stack; order and comparisons are unchanged.
//#if VOLUME_MOTION else struct VolumeHits {t:array<f32,VS_MAX_HITS>,object:array<u32,VS_MAX_HITS>,prim:array<u32,VS_MAX_HITS>,count:u32}
// kind: PRIMITIVE_TRIANGLE or PRIMITIVE_MOTION_TRIANGLE (Intersection.type), for the boundary normal at the ray's time.
struct VolumeHits {t:array<f32,VS_MAX_HITS>,object:array<u32,VS_MAX_HITS>,prim:array<u32,VS_MAX_HITS>,kind:array<u32,VS_MAX_HITS>,count:u32}
//#endif
//#if VOLUME_MOTION else fn volume_all_hits(origin:vec3<f32>,direction:vec3<f32>,visibility:u32,limit:u32,result:ptr<function,VolumeHits>){
// bvh_intersect_volume_all_motion (BVH_MOTION): moving instances and motion triangles at the ray's time.
fn volume_all_hits(origin:vec3<f32>,direction:vec3<f32>,visibility:u32,limit:u32,time:f32,result:ptr<function,VolumeHits>){
//#endif
    let tmin=0.0;let tmax=bitcast<f32>(0x7f7fffffu);
    if(table_count(G_prim_index)==0u){return;}
    var pos=origin;var dir=clamp_direction(direction);var inv=inverse_direction(dir);
    var stack:array<i32,192>;stack[0]=0x76543210i;var sp=0i;
    var node=bitcast<i32>(data_word(D_bvh_root));var object=NONE;var visits=0u;
    loop{
        // Malformed cycles in a scene table must fail instead of hanging a GPU.
        visits++;if(visits>1048576u){fail();return;}
        while(node>=0&&node!=0x76543210i){
            visits++;if(visits>1048576u){fail();return;}
//#if CURVES else             let bounds=bounds_hit(pos,inv,tmin,tmax,u32(node),visibility);
            let bounds=curve_node_hit(pos,dir,inv,tmin,tmax,u32(node),visibility);
//#endif
            let a=table_at(G_bvh_nodes,u32(node),SIZE_bvh_nodes);
            node=bitcast<i32>(load_word(a+8u));var other=bitcast<i32>(load_word(a+12u));let mask=u32(bounds.x);
            if(mask==3u){if(bounds.z<bounds.y){let tmp=node;node=other;other=tmp;}
                sp++;if(sp>=192){fail();return;}stack[u32(sp)]=other;
            }else if(mask==2u){node=other;}
            else if(mask==0u){if(sp<0){fail();return;}node=stack[u32(sp)];sp--;}
        }
        if(node<0){
            let a=table_at(G_bvh_leaf_nodes,u32(-node-1),SIZE_bvh_leaf_nodes);
            let first=bitcast<i32>(load_word(a));let end=load_word(a+4u);let kind=load_word(a+12u);
            if(first>=0){
                if(end>table_count(G_prim_index)||u32(first)>end){fail();return;}
                if(sp<0){fail();return;}node=stack[u32(sp)];sp--;
//#if CURVES
                // bvh/volume_all.h records triangles only: curve leaves are passed over.
                if((kind&PRIMITIVE_CURVE)!=0u&&(kind&PRIMITIVE_LAMP)==0u){continue;}
//#endif
//#if POINTS
                // volume.h, volume_all.h: the volume traversals ignore points.
                if((kind&PRIMITIVE_POINT)!=0u){continue;}
//#endif
//#if VOLUME_MOTION else                 if((kind&PRIMITIVE_ALL)!=PRIMITIVE_TRIANGLE&&u32(first)!=end){fail();return;}
                if(!native_triangle_kind(kind&PRIMITIVE_ALL)&&u32(first)!=end){fail();return;}
//#endif
                for(var index=u32(first);index<end;index++){
                    var prim_object=object;if(object==NONE){prim_object=load_word(table_at(G_prim_object,index,SIZE_prim_object));}
                    let prim=load_word(table_at(G_prim_index,index,SIZE_prim_index));
                    if((load_word(table_at(G_object_flag,prim_object,SIZE_object_flag))&VS_OBJECT_HAS_VOLUME)==0u){continue;}
                    let shader=load_word(table_at(G_tri_shader,prim,SIZE_tri_shader))&SHADER_MASK;
                    if((load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags)&VS_HAS_VOLUME)==0u){continue;}
                    if((load_word(table_at(G_prim_visibility,index,SIZE_prim_visibility))&visibility)==0u){continue;}
//#if !VOLUME_MOTION
                    let obj=table_at(G_objects,prim_object,SIZE_objects);let offset=load_word(obj+O_position_offset);
                    let vi=table_at(G_tri_vindex,prim,SIZE_tri_vindex);
                    let va=heap_vec3(table_at(G_tri_verts,offset+load_word(vi),SIZE_tri_verts));
                    let vb=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+4u),SIZE_tri_verts));
                    let vc=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+8u),SIZE_tri_verts));
//#endif
//#if VOLUME_MOTION
                    // triangle_intersect and motion_triangle_intersect (the vertices at the ray's time): the closest
                    // traversal's vertex fetch, as the transparent shadow traversal's (native_triangle_vertices).
                    let vertices=native_triangle_vertices(prim_object,prim,kind&PRIMITIVE_ALL,time);
                    if(vertices.error!=0u){return;}let va=vertices.a;let vb=vertices.b;let vc=vertices.c;
//#endif
                    var u:f32;var v:f32;var t:f32;
                    if(c_ray_triangle_intersect(pos,dir,tmin,tmax,va,vb,vc,&u,&v,&t)){
//#if VOLUME_MOTION else                         let k=(*result).count;(*result).t[k]=t;(*result).object[k]=prim_object;(*result).prim[k]=prim;(*result).count=k+1u;
                        let k=(*result).count;(*result).t[k]=t;(*result).object[k]=prim_object;(*result).prim[k]=prim;(*result).kind[k]=kind&PRIMITIVE_ALL;(*result).count=k+1u;
//#endif
                        if((*result).count==limit){return;}
                    }
                }
            }else{
                if(object!=NONE){fail();return;}
                object=load_word(table_at(G_prim_object,u32(-first-1),SIZE_prim_object));
                let obj=table_at(G_objects,object,SIZE_objects);
//#if VOLUME_MOTION else                 pos=transform_vector(obj+O_itfm,origin,true);dir=clamp_direction(transform_vector(obj+O_itfm,direction,false));inv=inverse_direction(dir);
                // The instance push of volume_all.h: an object without a volume is popped untraversed; a moving one is
                // entered with its inverse motion transform at the ray's time (bvh_instance_motion_push).
                let object_flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));
                if((object_flags&VS_OBJECT_HAS_VOLUME)==0u){object=NONE;if(sp<0){fail();return;}node=stack[u32(sp)];sp--;continue;}
                if((object_flags&SD_OBJECT_MOTION)!=0u){
                    let inverse=motion_inverse(motion_object(object,time));
                    pos=transform_rows(inverse.x,inverse.y,inverse.z,origin,true);dir=clamp_direction(transform_rows(inverse.x,inverse.y,inverse.z,direction,false));}
                else{pos=transform_vector(obj+O_itfm,origin,true);dir=clamp_direction(transform_vector(obj+O_itfm,direction,false));}
                inv=inverse_direction(dir);
//#endif
                sp++;if(sp>=192){fail();return;}stack[u32(sp)]=0x76543210i;
                node=bitcast<i32>(load_word(table_at(G_object_node,object,SIZE_object_node)));
            }
        }
        if(node==0x76543210i){
            if(sp<0){break;}
            if(object==NONE){fail();return;}
            pos=origin;dir=clamp_direction(direction);inv=inverse_direction(dir);object=NONE;
            node=stack[u32(sp)];sp--;if(node==0x76543210i){break;}
        }
    }
    return;
}
fn volume_boundary_backfacing(hit:NativeHit,direction:vec3<f32>)->bool{
    let object=table_at(G_objects,hit.object,SIZE_objects);let flags=load_word(table_at(G_object_flag,hit.object,SIZE_object_flag));
    let offset=load_word(object+O_position_offset);let vi=table_at(G_tri_vindex,hit.prim,SIZE_tri_vindex);
    let a=heap_vec3(table_at(G_tri_verts,offset+load_word(vi),SIZE_tri_verts));
    let b=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+4u),SIZE_tri_verts));
    let c=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+8u),SIZE_tri_verts));
    let applied=(flags&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
    var cross_normal=rounded_cross(b-a,c-a);if(applied&&(flags&SD_OBJECT_NEGATIVE_SCALE)!=0u){cross_normal=rounded_cross(c-a,b-a);}
    var normal=surface_native_normalize(cross_normal);if(!applied){normal=surface_transform_normal(object+O_itfm,normal);}
    return dot(normal,-direction)<0.0;
}
//#if VOLUME_MOTION
// shader_setup_from_ray's backfacing test at the ray's time: the triangle's vertices at the time (triangle_shader_setup,
// motion_triangle_shader_setup) and the instance normal transform with the object's motion transforms
// (shader_setup_object_transforms, object_normal_transform), as surface_setup_hit computes Ng.
fn volume_boundary_backfacing_motion(hit:NativeHit,direction:vec3<f32>,time:f32)->bool{
    var sd:SurfaceData;sd.object_id=hit.object;sd.prim=hit.prim;sd.primitive_type=hit.kind;sd.time=time;
    sd.object_flag=load_word(table_at(G_object_flag,hit.object,SIZE_object_flag));
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){sd.motion_tfm=motion_object(sd.object_id,sd.time);sd.motion_itfm=motion_inverse(sd.motion_tfm);}
    let vertices=native_triangle_vertices(hit.object,hit.prim,hit.kind,time);
    let a=vertices.a;let b=vertices.b;let c=vertices.c;
    let applied=(sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
    var cross_normal=rounded_cross(b-a,c-a);if(applied&&(sd.object_flag&SD_OBJECT_NEGATIVE_SCALE)!=0u){cross_normal=rounded_cross(c-a,b-a);}
    var normal=surface_native_normalize(cross_normal);if(!applied){normal=surface_object_normal(sd,normal,false);}
    return dot(normal,-direction)<0.0;
}
//#endif
@compute @workgroup_size(64) fn intersect_volume_stack(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK){fail();return;}
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=vec3<f32>(0,0,1);let size=data_word(D_volume_stack_size);
//#if VOLUME_MOTION
    // integrator_state_read_ray: the ray's time, for the motion traversal and the boundary normals.
    let time=heap_float(state_address(S_ray_time,index,4u));
//#endif
//#if !VOLUME_SHADOW_CATCHER
//#if VOLUME_MOTION else     var result:VolumeHits;volume_all_hits(origin,direction,VIS_CAMERA,2u*size,&result);if(load_word(p(17u))!=0u){return;}
    var result:VolumeHits;volume_all_hits(origin,direction,VIS_CAMERA,2u*size,time,&result);if(load_word(p(17u))!=0u){return;}
//#endif
//#endif
//#if VOLUME_SHADOW_CATCHER
    // SHADOW_CATCHER_PATH_VISIBILITY: the catcher path's stack excludes non-catcher objects.
    let catcher_path=(state_word(S_path_flag,index)&SC_PATH_PASS)!=0u;
//#if VOLUME_MOTION else     var result:VolumeHits;volume_all_hits(origin,direction,select(VIS_CAMERA,VIS_CAMERA<<16u,catcher_path),2u*size,&result);if(load_word(p(17u))!=0u){return;}
    var result:VolumeHits;volume_all_hits(origin,direction,select(VIS_CAMERA,VIS_CAMERA<<16u,catcher_path),2u*size,time,&result);if(load_word(p(17u))!=0u){return;}
//#endif
//#endif
    // Native qsort orders hits by distance; retain traversal order for equal t.
//#if VOLUME_MOTION else     for(var i=1u;i<result.count;i++){let ht=result.t[i];let ho=result.object[i];let hp=result.prim[i];var j=i;while(j>0u&&result.t[j-1u]>ht){result.t[j]=result.t[j-1u];result.object[j]=result.object[j-1u];result.prim[j]=result.prim[j-1u];j--;}result.t[j]=ht;result.object[j]=ho;result.prim[j]=hp;}
    for(var i=1u;i<result.count;i++){let ht=result.t[i];let ho=result.object[i];let hp=result.prim[i];let hk=result.kind[i];var j=i;while(j>0u&&result.t[j-1u]>ht){result.t[j]=result.t[j-1u];result.object[j]=result.object[j-1u];result.prim[j]=result.prim[j-1u];result.kind[j]=result.kind[j-1u];j--;}result.t[j]=ht;result.object[j]=ho;result.prim[j]=hp;result.kind[j]=hk;}
//#endif
    var objects:array<u32,VS_MAX>;var shaders:array<u32,VS_MAX>;var enclosed:array<u32,VS_MAX_HITS>;var enclosed_count=0u;var count=0u;
//#if VOLUME_SHADOW_CATCHER else     if(data_word(D_background_volume_shader)!=NONE){objects[0]=data_word(D_background_object_index);shaders[0]=data_word(D_background_volume_shader);count=1u;}
    if(data_word(D_background_volume_shader)!=NONE&&!catcher_path){objects[0]=data_word(D_background_object_index);shaders[0]=data_word(D_background_volume_shader);count=1u;}
//#endif
//#if VOLUME_MOTION else     for(var i=0u;i<result.count;i++){let hit=NativeHit(result.t[i],0.0,0.0,result.object[i],result.prim[i],PRIMITIVE_TRIANGLE);
    for(var i=0u;i<result.count;i++){let hit=NativeHit(result.t[i],0.0,0.0,result.object[i],result.prim[i],result.kind[i]);
//#endif
//#if VOLUME_MOTION else         if(volume_boundary_backfacing(hit,direction)){
        if(volume_boundary_backfacing_motion(hit,direction,time)){
//#endif
            var add=true;for(var j=0u;j<enclosed_count;j++){if(enclosed[j]==hit.object){add=false;break;}}
            for(var j=0u;j<count;j++){if(objects[j]==hit.object){add=false;break;}}
            if(add&&count<size-1u){objects[count]=hit.object;shaders[count]=load_word(table_at(G_tri_shader,hit.prim,SIZE_tri_shader));count++;}
        }else{if(enclosed_count>=VS_MAX_HITS){fail();return;}enclosed[enclosed_count]=hit.object;enclosed_count++;}
    }
    if(load_word(p(17u))!=0u){return;}
    for(var i=0u;i<count;i++){store_word(volume_stack_address(index,i,VS_object),objects[i]);store_word(volume_stack_address(index,i,VS_shader),shaders[i]);}
    store_word(volume_stack_address(index,count,VS_object),NONE);store_word(volume_stack_address(index,count,VS_shader),NONE);
//#if VOLUME_SHADOW_CATCHER
    // integrator_intersect_next_kernel_after_shadow_catcher_volume: on to the catcher surface, sorted.
    if(catcher_path){
        let prim=state_word(S_isect_prim,index);if(prim==NONE){fail();return;}
//#if !CURVE_SHADOW_CATCHER
//#if POINTS else         let shader=load_word(table_at(G_tri_shader,prim,SIZE_tri_shader))&SHADER_MASK;
        let shader=point_or_triangle_shader(prim,state_word(S_isect_type,index))&SHADER_MASK;
//#endif
//#endif
//#if CURVE_SHADOW_CATCHER
        var shader=0u;
        if((state_word(S_isect_type,index)&PRIMITIVE_CURVE)!=0u){shader=load_word(table_at(G_curves,prim,16u))&SHADER_MASK;}
//#if POINTS else         else{shader=load_word(table_at(G_tri_shader,prim,SIZE_tri_shader))&SHADER_MASK;}
        else{shader=point_or_triangle_shader(prim,state_word(S_isect_type,index))&SHADER_MASK;}
//#endif
//#endif
//#if MNEE_VOLUME
        // A caustic receiver goes to INTERSECT_MNEE, unsorted (integrator_intersect_next_kernel_after_shadow_catcher_volume).
        if(data_word(VS_D_use_caustics)!=0u&&(load_word(table_at(G_object_flag,state_word(S_isect_object,index),4u))&VS_SD_OBJECT_CAUSTICS_RECEIVER)!=0u){
            let queue=load_word(p(7u)+S_queue_counter);atomicSub(&heap[queue/4u+DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK],1u);atomicAdd(&heap[queue/4u+VS_K_INTERSECT_MNEE],1u);
            state_u16(S_path_queued_kernel,index,VS_K_INTERSECT_MNEE);return;}
//#endif
        if((load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags)&SC_SD_HAS_RAYTRACE)!=0u||p(15u)==0u){fail();return;}
        let key=shader+data_word(D_max_shaders)*(index/load_word(p(7u)+S_sort_partition_divisor));if(key>=p(15u)){fail();return;}
        let sort=load_word(p(7u)+S_sort_key_counter+DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE*POINTER_SIZE);atomicAdd(&heap[(sort+key*4u)/4u],1u);state_u32(S_path_shader_sort_key,index,key);
        let queue=load_word(p(7u)+S_queue_counter);atomicSub(&heap[queue/4u+DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK],1u);atomicAdd(&heap[queue/4u+DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE],1u);
        state_u16(S_path_queued_kernel,index,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE);return;
    }
//#endif
    let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[counter/4u+DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK],1u);atomicAdd(&heap[counter/4u+K_CLOSEST],1u);state_u16(S_path_queued_kernel,index,K_CLOSEST);
}
//#if SUBSURFACE_VOLUME
// integrator_volume_stack_update_for_subsurface (integrator/intersect_volume_stack.h) after subsurface_scatter: the
// volume objects between the entry and the exit point of a subsurface path enter or leave its volume stack. Its own
// entry, dispatched after intersect_subsurface and intersect_subsurface_disk (subsurface.cpp), which leave the inputs in
// the input's traversal stack slot (push word 30, SSV_RECORD_BYTES apart): the entry point (the state's ray P), the
// exit point (the ray P of subsurface_scatter before the reversal) and the entry primitive (the ray's self primitive).
// ssv_hits is volume_all_hits (the oracle's __VOLUME_RECORD_ALL__ bvh_intersect_volume_all) with the segment's tmax and
// bvh_volume_anyhit_triangle_filter's intersection_skip_self.
//#if VOLUME_MOTION else fn ssv_hits(origin:vec3<f32>,direction:vec3<f32>,tmax:f32,self_object:u32,self_prim:u32,visibility:u32,limit:u32,result:ptr<function,VolumeHits>){
fn ssv_hits(origin:vec3<f32>,direction:vec3<f32>,tmax:f32,self_object:u32,self_prim:u32,visibility:u32,limit:u32,time:f32,result:ptr<function,VolumeHits>){
//#endif
    let tmin=0.0;
    if(table_count(G_prim_index)==0u){return;}
    var pos=origin;var dir=clamp_direction(direction);var inv=inverse_direction(dir);
    var stack:array<i32,192>;stack[0]=0x76543210i;var sp=0i;
    var node=bitcast<i32>(data_word(D_bvh_root));var object=NONE;var visits=0u;
    loop{
        visits++;if(visits>1048576u){fail();return;}
        while(node>=0&&node!=0x76543210i){
            visits++;if(visits>1048576u){fail();return;}
//#if CURVES else             let bounds=bounds_hit(pos,inv,tmin,tmax,u32(node),visibility);
            let bounds=curve_node_hit(pos,dir,inv,tmin,tmax,u32(node),visibility);
//#endif
            let a=table_at(G_bvh_nodes,u32(node),SIZE_bvh_nodes);
            node=bitcast<i32>(load_word(a+8u));var other=bitcast<i32>(load_word(a+12u));let mask=u32(bounds.x);
            if(mask==3u){if(bounds.z<bounds.y){let tmp=node;node=other;other=tmp;}
                sp++;if(sp>=192){fail();return;}stack[u32(sp)]=other;
            }else if(mask==2u){node=other;}
            else if(mask==0u){if(sp<0){fail();return;}node=stack[u32(sp)];sp--;}
        }
        if(node<0){
            let a=table_at(G_bvh_leaf_nodes,u32(-node-1),SIZE_bvh_leaf_nodes);
            let first=bitcast<i32>(load_word(a));let end=load_word(a+4u);let kind=load_word(a+12u);
            if(first>=0){
                if(end>table_count(G_prim_index)||u32(first)>end){fail();return;}
                if(sp<0){fail();return;}node=stack[u32(sp)];sp--;
//#if CURVES
                if((kind&PRIMITIVE_CURVE)!=0u&&(kind&PRIMITIVE_LAMP)==0u){continue;}
//#endif
//#if POINTS
                if((kind&PRIMITIVE_POINT)!=0u){continue;}
//#endif
//#if VOLUME_MOTION else                 if((kind&PRIMITIVE_ALL)!=PRIMITIVE_TRIANGLE&&u32(first)!=end){fail();return;}
                if(!native_triangle_kind(kind&PRIMITIVE_ALL)&&u32(first)!=end){fail();return;}
//#endif
                for(var index=u32(first);index<end;index++){
                    var prim_object=object;if(object==NONE){prim_object=load_word(table_at(G_prim_object,index,SIZE_prim_object));}
                    let prim=load_word(table_at(G_prim_index,index,SIZE_prim_index));
                    if((load_word(table_at(G_object_flag,prim_object,SIZE_object_flag))&VS_OBJECT_HAS_VOLUME)==0u){continue;}
                    // intersection_skip_self
                    if(prim==self_prim&&prim_object==self_object){continue;}
                    let shader=load_word(table_at(G_tri_shader,prim,SIZE_tri_shader))&SHADER_MASK;
                    if((load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags)&VS_HAS_VOLUME)==0u){continue;}
                    if((load_word(table_at(G_prim_visibility,index,SIZE_prim_visibility))&visibility)==0u){continue;}
//#if !VOLUME_MOTION
                    let obj=table_at(G_objects,prim_object,SIZE_objects);let offset=load_word(obj+O_position_offset);
                    let vi=table_at(G_tri_vindex,prim,SIZE_tri_vindex);
                    let va=heap_vec3(table_at(G_tri_verts,offset+load_word(vi),SIZE_tri_verts));
                    let vb=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+4u),SIZE_tri_verts));
                    let vc=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+8u),SIZE_tri_verts));
//#endif
//#if VOLUME_MOTION
                    let vertices=native_triangle_vertices(prim_object,prim,kind&PRIMITIVE_ALL,time);
                    if(vertices.error!=0u){return;}let va=vertices.a;let vb=vertices.b;let vc=vertices.c;
//#endif
                    var u:f32;var v:f32;var t:f32;
                    if(c_ray_triangle_intersect(pos,dir,tmin,tmax,va,vb,vc,&u,&v,&t)){
//#if VOLUME_MOTION else                         let k=(*result).count;(*result).t[k]=t;(*result).object[k]=prim_object;(*result).prim[k]=prim;(*result).count=k+1u;
                        let k=(*result).count;(*result).t[k]=t;(*result).object[k]=prim_object;(*result).prim[k]=prim;(*result).kind[k]=kind&PRIMITIVE_ALL;(*result).count=k+1u;
//#endif
                        if((*result).count==limit){return;}
                    }
                }
            }else{
                if(object!=NONE){fail();return;}
                object=load_word(table_at(G_prim_object,u32(-first-1),SIZE_prim_object));
                let obj=table_at(G_objects,object,SIZE_objects);
//#if VOLUME_MOTION else                 pos=transform_vector(obj+O_itfm,origin,true);dir=clamp_direction(transform_vector(obj+O_itfm,direction,false));inv=inverse_direction(dir);
                let object_flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));
                if((object_flags&VS_OBJECT_HAS_VOLUME)==0u){object=NONE;if(sp<0){fail();return;}node=stack[u32(sp)];sp--;continue;}
                if((object_flags&SD_OBJECT_MOTION)!=0u){
                    let inverse=motion_inverse(motion_object(object,time));
                    pos=transform_rows(inverse.x,inverse.y,inverse.z,origin,true);dir=clamp_direction(transform_rows(inverse.x,inverse.y,inverse.z,direction,false));}
                else{pos=transform_vector(obj+O_itfm,origin,true);dir=clamp_direction(transform_vector(obj+O_itfm,direction,false));}
                inv=inverse_direction(dir);
//#endif
                sp++;if(sp>=192){fail();return;}stack[u32(sp)]=0x76543210i;
                node=bitcast<i32>(load_word(table_at(G_object_node,object,SIZE_object_node)));
            }
        }
        if(node==0x76543210i){
            if(sp<0){break;}
            if(object==NONE){fail();return;}
            pos=origin;dir=clamp_direction(direction);inv=inverse_direction(dir);object=NONE;
            node=stack[u32(sp)];sp--;if(node==0x76543210i){break;}
        }
    }
}
@compute @workgroup_size(64) fn intersect_subsurface_volume_stack(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)||load_word(p(17u))!=0u){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
    // A path subsurface_scatter sent on to its exit point (a terminated one has no queued kernel).
    let queued=state_half(S_path_queued_kernel,index);if(queued!=SSV_K_SHADE_SURFACE&&queued!=SSV_K_SHADE_SURFACE_RAYTRACE&&queued!=SSV_K_INTERSECT_MNEE){return;}
    let object=state_word(S_isect_object,index);
    if((load_word(table_at(G_object_flag,object,SIZE_object_flag))&SSV_SD_OBJECT_INTERSECTS_VOLUME)==0u){return;}
    if(p(30u)==0u){fail();return;}let at=p(30u)+id.x*SSV_RECORD_BYTES;
    let from_P=vec3<f32>(bitcast<f32>(load_word(at)),bitcast<f32>(load_word(at+4u)),bitcast<f32>(load_word(at+8u)));
    let to_P=vec3<f32>(bitcast<f32>(load_word(at+12u)),bitcast<f32>(load_word(at+16u)),bitcast<f32>(load_word(at+20u)));
    let self_prim=load_word(at+24u);
    // safe_normalize_len(to_P - from_P): float3 / float divides each lane on SSE hosts and multiplies by the reciprocal on
    // scalar ones.
    let d=to_P-from_P;let tmax=rounded_sqrt(rounded_dot(d,d));var direction=d;
    if(tmax!=0.0){
        if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){direction=d*rounded_divide(1.0,tmax);}
        else{direction=vec3<f32>(rounded_divide(d.x,tmax),rounded_divide(d.y,tmax),rounded_divide(d.z,tmax));}
    }
    // scene_intersect_volume's intersection_ray_valid (a zero-length segment has no direction).
    if((bitcast<u32>(from_P.x)&0x7f800000u)==0x7f800000u||(bitcast<u32>(direction.x)&0x7f800000u)==0x7f800000u||dot(direction,direction)==0.0){return;}
    // SHADOW_CATCHER_PATH_VISIBILITY(path_flag, PATH_RAY_VISIBILITY_ALL).
    let catcher_path=(state_word(S_path_flag,index)&SSV_PATH_RAY_SHADOW_CATCHER_PASS)!=0u;
    let visibility=select(SSV_PATH_RAY_VISIBILITY_ALL,SSV_PATH_RAY_VISIBILITY_ALL<<16u,catcher_path);
    let size=data_word(D_volume_stack_size);
//#if VOLUME_MOTION else     var result:VolumeHits;ssv_hits(from_P,direction,tmax,object,self_prim,visibility,2u*size,&result);if(load_word(p(17u))!=0u){return;}
    let time=heap_float(state_address(S_ray_time,index,4u));
    var result:VolumeHits;ssv_hits(from_P,direction,tmax,object,self_prim,visibility,2u*size,time,&result);if(load_word(p(17u))!=0u){return;}
//#endif
    // qsort by distance (intersections_compare); traversal order kept for equal t, as intersect_volume_stack.
//#if VOLUME_MOTION else     for(var i=1u;i<result.count;i++){let ht=result.t[i];let ho=result.object[i];let hp=result.prim[i];var j=i;while(j>0u&&result.t[j-1u]>ht){result.t[j]=result.t[j-1u];result.object[j]=result.object[j-1u];result.prim[j]=result.prim[j-1u];j--;}result.t[j]=ht;result.object[j]=ho;result.prim[j]=hp;}
    for(var i=1u;i<result.count;i++){let ht=result.t[i];let ho=result.object[i];let hp=result.prim[i];let hk=result.kind[i];var j=i;while(j>0u&&result.t[j-1u]>ht){result.t[j]=result.t[j-1u];result.object[j]=result.object[j-1u];result.prim[j]=result.prim[j-1u];result.kind[j]=result.kind[j-1u];j--;}result.t[j]=ht;result.object[j]=ho;result.prim[j]=hp;result.kind[j]=hk;}
//#endif
    for(var i=0u;i<result.count;i++){
        // Self is skipped: subsurface scattering itself already enters and exits the object.
        if(result.object[i]==object){continue;}
//#if VOLUME_MOTION else         let hit=NativeHit(result.t[i],0.0,0.0,result.object[i],result.prim[i],PRIMITIVE_TRIANGLE);let backfacing=volume_boundary_backfacing(hit,direction);
        let hit=NativeHit(result.t[i],0.0,0.0,result.object[i],result.prim[i],result.kind[i]);let backfacing=volume_boundary_backfacing_motion(hit,direction,time);
//#endif
        // shader_setup_from_ray then volume_stack_enter_exit<false>.
        volume_stack_enter_exit(index,hit.object,load_word(table_at(G_tri_shader,hit.prim,SIZE_tri_shader)),backfacing);
    }
}
//#endif
