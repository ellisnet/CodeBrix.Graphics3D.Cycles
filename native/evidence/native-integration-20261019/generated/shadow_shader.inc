R"CBWGSL(// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
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
fn inverse_direction(d:vec3<f32>)->vec3<f32>{
    // Approximate reciprocal can reject a slab exactly at ray.tmin/tmax.
    return vec3<f32>(rounded_divide(1.0,d.x),rounded_divide(1.0,d.y),rounded_divide(1.0,d.z));
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
fn trace_native_filtered(origin:vec3<f32>,direction:vec3<f32>,tmin:f32,tmax:f32,time:f32,visibility:u32,self_object:u32,self_prim:u32,light_object:u32,light_prim:u32)->NativeHit{
    var result=NativeHit(tmax,0.0,0.0,NONE,NONE,PRIM_TYPE_NONE);
    if(table_count(G_prim_index)==0u){return result;}
    var pos=origin;var dir=clamp_direction(direction);var inv=inverse_direction(dir);
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
                if(!native_triangle_kind(kind&PRIMITIVE_ALL)&&u32(first)!=end){fail();return result;}
                for(var index=u32(first);index<end;index++){
                    var prim_object=object;if(object==NONE){prim_object=load_word(table_at(G_prim_object,index,SIZE_prim_object));}
                    let prim=load_word(table_at(G_prim_index,index,SIZE_prim_index));
                    if(prim_object==self_object&&prim==self_prim){continue;}
                    if(prim_object==light_object&&prim==light_prim){continue;}
                    if((load_word(table_at(G_prim_visibility,index,SIZE_prim_visibility))&visibility)==0u){continue;}
                    let vertices=native_triangle_vertices(prim_object,prim,kind&PRIMITIVE_ALL,time);if(vertices.error!=0u){return result;}
                    let va=vertices.a;let vb=vertices.b;let vc=vertices.c;
                    var u:f32;var v:f32;var t:f32;
                    if(c_ray_triangle_intersect(pos,dir,tmin,result.t,va,vb,vc,&u,&v,&t)){
                        result=NativeHit(t,u,v,prim_object,prim,kind&PRIMITIVE_ALL);
                        if((visibility&PATH_RAY_VISIBILITY_SHADOW_OPAQUE)!=0u){return result;}
                    }
                }
            }else{
                if(object!=NONE){fail();return result;}
                object=load_word(table_at(G_prim_object,u32(-first-1),SIZE_prim_object));
                let obj=table_at(G_objects,object,SIZE_objects);
                let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));
                if((flags&SD_OBJECT_MOTION)!=0u){
//#if MOTION_OBJECT else atomicOr(&heap[p(17u)/4u],2u);return result;
                    let inverse=motion_inverse(motion_object(object,time));pos=motion_point(inverse,origin);dir=clamp_direction(motion_direction(inverse,direction));
//#endif
                }
                else{pos=transform_vector(obj+O_itfm,origin,true);dir=clamp_direction(transform_vector(obj+O_itfm,direction,false));}
                inv=inverse_direction(dir);
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
fn trace_native(origin:vec3<f32>,direction:vec3<f32>,tmin:f32,tmax:f32,time:f32,visibility:u32,self_object:u32,self_prim:u32)->NativeHit{
    return trace_native_filtered(origin,direction,tmin,tmax,time,visibility,self_object,self_prim,NONE,NONE);
}
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
// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2026 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native static BVH2 shadow-all traversal and closest-four intersection recording.
// Adapted from the reviewed kernel/bvh/{shadow_all,intersect_filter}.h snapshot.
struct ShadowTrace {opaque:bool,count:u32}
fn shadow_hit_load(index:u32,slot:u32)->NativeHit{
    let offset=slot*SHADOW_HIT_STRIDE;
    return NativeHit(heap_float(state_address(S_shadow_hit_t+offset,index,4u)),heap_float(state_address(S_shadow_hit_u+offset,index,4u)),heap_float(state_address(S_shadow_hit_v+offset,index,4u)),
      state_word(S_shadow_hit_object+offset,index),state_word(S_shadow_hit_prim+offset,index),state_word(S_shadow_hit_type+offset,index));
}
fn shadow_hit_store(index:u32,slot:u32,hit:NativeHit){
    let offset=slot*SHADOW_HIT_STRIDE;
    state_f32(S_shadow_hit_t+offset,index,hit.t);state_f32(S_shadow_hit_u+offset,index,hit.u);state_f32(S_shadow_hit_v+offset,index,hit.v);
    state_u32(S_shadow_hit_object+offset,index,hit.object);state_u32(S_shadow_hit_prim+offset,index,hit.prim);state_u32(S_shadow_hit_type+offset,index,hit.kind);
}
fn shadow_hit_sort(index:u32,count:u32){
    var n=count;loop{var changed=false;for(var i=1u;i<n;i++){let a=shadow_hit_load(index,i-1u);let b=shadow_hit_load(index,i);if(a.t>b.t){shadow_hit_store(index,i-1u,b);shadow_hit_store(index,i,a);changed=true;}}
      if(!changed||n==0u){break;}n--;}
}
fn trace_shadow_all(path:u32,origin:vec3<f32>,direction:vec3<f32>,tmin:f32,tmax:f32,visibility:u32,self_object:u32,self_prim:u32,light_object:u32,light_prim:u32,max_hits:u32)->ShadowTrace{
    var result=ShadowTrace(false,0u);var record_index=0u;var farthest=tmax;
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
                    if(prim_object==self_object&&prim==self_prim){continue;}
                    if(prim_object==light_object&&prim==light_prim){continue;}
                    if((load_word(table_at(G_prim_visibility,index,SIZE_prim_visibility))&visibility)==0u){continue;}
                    let obj=table_at(G_objects,prim_object,SIZE_objects);let offset=load_word(obj+O_position_offset);
                    let vi=table_at(G_tri_vindex,prim,SIZE_tri_vindex);
                    let va=heap_vec3(table_at(G_tri_verts,offset+load_word(vi),SIZE_tri_verts));
                    let vb=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+4u),SIZE_tri_verts));
                    let vc=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+8u),SIZE_tri_verts));
                    var u:f32;var v:f32;var t:f32;
                    if(c_ray_triangle_intersect(pos,dir,tmin,tmax,va,vb,vc,&u,&v,&t)){
                        let shader=load_word(table_at(G_tri_shader,prim,4u))&SHADER_MASK;
                        let flags=load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags);
                        if((flags&SD_HAS_TRANSPARENT_SHADOW)==0u){result.opaque=true;return result;}
                        if((flags&SHADOW_ONLY_VOLUME)!=0u){fail();return result;}
                        var duplicate=false;for(var k=0u;k<min(result.count,SHADOW_HIT_CAPACITY);k++){let old=shadow_hit_load(path,k);if(old.object==prim_object&&old.prim==prim){duplicate=true;break;}}
                        if(duplicate){continue;}result.count++;
                        if(result.count>max_hits){result.opaque=true;return result;}
                        if(result.count<=SHADOW_HIT_CAPACITY||t<farthest){
                            shadow_hit_store(path,record_index,NativeHit(t,u,v,prim_object,prim,PRIMITIVE_TRIANGLE));
                            if(result.count>=SHADOW_HIT_CAPACITY){record_index=0u;farthest=shadow_hit_load(path,0u).t;
                                for(var k=1u;k<SHADOW_HIT_CAPACITY;k++){let candidate=shadow_hit_load(path,k).t;if(candidate>farthest){record_index=k;farthest=candidate;}}
                            }else{record_index=result.count;}
                        }
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
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from the pinned Cycles intersect_shadow, shade_shadow, state flow
// and film/light_passes kernels. Opaque/transparent static triangles and light passes.
// Volume, hair, AO, linking and cache-miss paths remain explicit errors.
fn shadow_transition(index:u32,current:u32,next:u32){
    let counter=load_word(p(7u)+S_queue_counter);
    atomicSub(&heap[counter/4u+current],1u);
    if(next!=0u){atomicAdd(&heap[counter/4u+next],1u);}
    state_u16(S_shadow_path_queued_kernel,index,next);
}
@compute @workgroup_size(64) fn intersect_shadow(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
    if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_INTERSECT||(state_word(S_shadow_path_flag,index)&SHADOW_UNSUPPORTED_FLAGS)!=0u){fail();return;}
    let origin=heap_vec3(state_address(S_shadow_ray_P,index,12u));let direction=heap_vec3(state_address(S_shadow_ray_D,index,12u));
    let tmin=heap_float(state_address(S_shadow_ray_tmin,index,4u));let tmax=heap_float(state_address(S_shadow_ray_tmax,index,4u));
    var hit=false;let transparent=data_word(D_integrator_transparent_shadows)!=0u;var count=0u;
    if((bitcast<u32>(origin.x)&0x7f800000u)!=0x7f800000u&&(bitcast<u32>(direction.x)&0x7f800000u)!=0x7f800000u&&dot(direction,direction)!=0.0&&tmin<bitcast<f32>(0x7f7fffffu)){
        let self_object=state_word(S_shadow_self_object,index);let self_prim=state_word(S_shadow_self_prim,index);
        let light_object=state_word(S_shadow_ray_self_light_object,index);let light_prim=state_word(S_shadow_ray_self_light_prim,index);
        if(transparent){let maximum=data_word(D_integrator_transparent_max_bounce);let used=state_half(S_shadow_path_transparent_bounce,index);
            let all=trace_shadow_all(index,origin,direction,tmin,tmax,PATH_RAY_VISIBILITY_SHADOW,self_object,self_prim,light_object,light_prim,maximum-min(maximum,used));
            hit=all.opaque;count=all.count;
        }else{let result=trace_native_filtered(origin,direction,tmin,tmax,heap_float(state_address(S_shadow_ray_time,index,4u)),PATH_RAY_VISIBILITY_SHADOW_OPAQUE,self_object,self_prim,light_object,light_prim);hit=result.prim!=NONE;}
    }
    if(load_word(p(17u))!=0u){return;}
    if(transparent){if(hit){state_vec3(S_shadow_path_throughput,index,heap_vec3(state_address(S_shadow_path_throughput,index,12u))*vec3<f32>(0));count=0u;}
        else if(count>0u){shadow_hit_sort(index,min(count,SHADOW_HIT_CAPACITY));}state_u16(S_shadow_path_packed_num_hits,index,count);
    }else if(!hit){state_u16(S_shadow_path_packed_num_hits,index,0u);}
    shadow_transition(index,K_SHADOW_INTERSECT,select(K_SHADOW_SHADE,0u,hit));
}
fn shadow_write_film(index:u32){
    let flag=state_word(S_shadow_path_flag,index);
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_shadow_path_render_pixel_index,index);
    if(pixel>=p(11u)/stride){fail();return;}let base=p(4u)+pixel*stride*4u;
    let flags=data_word(D_film_light_pass_flag);let group=state_byte(S_shadow_path_lightgroup,index);let go=data_word(D_film_pass_lightgroup);
    if(flags!=0u&&group!=0u&&go!=PASS_NONE&&(group>stride/3u||go>stride-3u*group)){fail();return;}
    var value=heap_vec3(state_address(S_shadow_path_throughput,index,12u));
    value=select(value,vec3<f32>(0),(bitcast<vec3<u32>>(value)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
    let bounce=state_half(S_shadow_path_bounce,index);
    let limit=select(data_float(D_integrator_sample_clamp_direct),data_float(D_integrator_sample_clamp_indirect),bounce>0u);
    let total=abs(value.x)+abs(value.y)+abs(value.z);if(total>limit){value*=limit/total;}
    if((flags&MASK_COMBINED)!=0u){film_rgb(base,data_word(D_film_pass_combined),value);}
    if(data_word(D_film_pass_adaptive_aux_buffer)!=PASS_NONE&&(countOneBits(state_word(S_shadow_path_sample,index)&0xaaaaaaaau)&1u)!=0u){
        film_rgb(base,data_word(D_film_pass_adaptive_aux_buffer),2.0*value);
    }
    if(flags!=0u){
        if(group!=0u&&go!=PASS_NONE){film_rgb(base,go+3u*(group-1u),value);}
        if(p(20u)!=0u&&(flag&PATH_RAY_SURFACE_PASS)!=0u){
            let diffuse=heap_vec3(state_address(S_shadow_path_pass_diffuse_weight,index,12u));let glossy=heap_vec3(state_address(S_shadow_path_pass_glossy_weight,index,12u));
            film_rgb(base,select(data_word(D_film_pass_glossy_indirect),data_word(D_film_pass_glossy_direct),bounce==0u),glossy*value);
            film_rgb(base,select(data_word(D_film_pass_transmission_indirect),data_word(D_film_pass_transmission_direct),bounce==0u),(vec3<f32>(1)-diffuse-glossy)*value);
            film_rgb(base,select(data_word(D_film_pass_diffuse_indirect),data_word(D_film_pass_diffuse_direct),bounce==0u),diffuse*value);
        }
    }
    shadow_transition(index,K_SHADOW_SHADE,0u);
}
@compute @workgroup_size(64) fn shade_shadow(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
    let flag=state_word(S_shadow_path_flag,index);
    if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_SHADE||(flag&SHADOW_UNSUPPORTED_FLAGS)!=0u||state_half(S_shadow_path_packed_num_hits,index)!=0u){fail();return;}
    shadow_write_film(index);
}
// Recorded transparent hits get their native ShaderData in a separate GPU
// dispatch (GPU-private cache, no readback). Keeping the triangle/motion setup
// out of the SVM stage bounds driver compiler pressure: the combined entry
// intermittently crashed the Intel Mesa 25.0.7 backend (brw_opt_combine_constants)
// in cold compiles; the split entries compiled every probe (native/work/b1).
@compute @workgroup_size(64) fn prepare_shadow_shader_data(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}let count=state_half(S_shadow_path_packed_num_hits,index);
    if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_SHADE||count>4095u){return;}
    let recorded=min(count,SHADOW_HIT_CAPACITY);
    for(var slot=0u;slot<recorded;slot++){
        let hit=shadow_hit_load(index,slot);
        let sd=surface_setup_hit(hit.kind,hit.object,hit.prim,hit.u,hit.v,hit.t,heap_float(state_address(S_shadow_ray_time,index,4u)),
          heap_vec3(state_address(S_shadow_ray_D,index,12u)),heap_float(state_address(S_shadow_ray_dP,index,4u)),heap_float(state_address(S_shadow_ray_dD,index,4u)));
        let at=surface_cache_address(id.x*SHADOW_HIT_CAPACITY+slot);if(at==0u){return;}surface_data_write(at,sd);
        if(sd.error!=0u){return;}
    }
}
// Bound compiler pressure from the SVM path with one invocation per workgroup.
// Static transparent triangles remain the supported scope; tuning is pending.
@compute @workgroup_size(1) fn shade_shadow_transparent(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}let flag=state_word(S_shadow_path_flag,index);let count=state_half(S_shadow_path_packed_num_hits,index);
    if(state_half(S_shadow_path_queued_kernel,index)!=K_SHADOW_SHADE||(flag&SHADOW_UNSUPPORTED_FLAGS)!=0u||count>4095u){fail();return;}
    let recorded=min(count,SHADOW_HIT_CAPACITY);
    for(var slot=0u;slot<recorded;slot++){
        let hit=shadow_hit_load(index,slot);
        let sd=surface_cached(id.x*SHADOW_HIT_CAPACITY+slot);
        if(sd.error!=0u){return;}if((sd.shader_flag&SHADOW_ONLY_VOLUME)!=0u){fail();return;}
        let r=svm_evaluate_context(index,sd,0u,0u,0u,PATH_RAY_VISIBILITY_SHADOW,0u,false);if(r.error!=0u){return;}
        let throughput=heap_vec3(state_address(S_shadow_path_throughput,index,12u))*r.transparency;
        if(all(throughput==vec3<f32>(0))){shadow_transition(index,K_SHADOW_SHADE,0u);return;}
        state_vec3(S_shadow_path_throughput,index,throughput);
        state_u16(S_shadow_path_transparent_bounce,index,state_half(S_shadow_path_transparent_bounce,index)+1u);
        state_u16(S_shadow_path_rng_offset,index,state_half(S_shadow_path_rng_offset,index)+RNG_BOUNCE);
        if(state_half(S_shadow_path_volume_bounds_bounce,index)>SHADOW_VOLUME_BOUNDS_MAX){shadow_transition(index,K_SHADOW_SHADE,0u);return;}
    }
    if(state_half(S_shadow_path_volume_bounds_bounce,index)>SHADOW_VOLUME_BOUNDS_MAX){shadow_transition(index,K_SHADOW_SHADE,0u);return;}
    if(count>=SHADOW_HIT_CAPACITY){let last=shadow_hit_load(index,recorded-1u).t;
        state_f32(S_shadow_ray_tmin,index,select(bitcast<f32>(bitcast<u32>(last)+1u),1.1754943508222875e-38,last==0.0));
        shadow_transition(index,K_SHADOW_SHADE,K_SHADOW_INTERSECT);return;}
    shadow_write_film(index);
}
)CBWGSL"
