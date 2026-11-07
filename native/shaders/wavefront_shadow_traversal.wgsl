// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2026 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native static BVH2 shadow-all traversal and closest-four intersection recording.
// Adapted from the reviewed kernel/bvh/{shadow_all,intersect_filter}.h snapshot.
//#if CURVE_SHADOW else struct ShadowTrace {opaque:bool,count:u32}
// throughput: baked curve transparency accumulated without recording (bvh_shadow_all_anyhit_filter).
struct ShadowTrace {opaque:bool,count:u32,throughput:vec3<f32>}
// intersection_find_attribute(ATTR_STD_SHADOW_TRANSPARENCY), intersection_curve_shadow_transparency (bvh/util.h) and
// rgbe_to_rgb (util/types_rgbe.h): the two segment keys' RGBE words, read as bits (a word can read as a NaN float), and
// (1-u)*f0+u*f1. No attribute: opaque.
fn curve_rgbe_to_rgb(word:u32)->vec3<f32>{
    if((word&0x7fffffffu)==0u){return vec3<f32>(0);}
    let e=word>>24u;let f=bitcast<f32>((127u+(e&0x1fu)-23u)<<23u);
    let value=vec3<f32>(f32(word&255u),f32((word>>8u)&255u),f32((word>>16u)&255u))*f;
    return bitcast<vec3<f32>>(bitcast<vec3<u32>>(value)|vec3<u32>((e&0x80u)<<24u,(e&0x40u)<<25u,(e&0x20u)<<26u));
}
fn curve_shadow_transparency(object:u32,prim:u32,kind:u32,u:f32)->vec3<f32>{
    if(object>=table_count(G_objects)||prim>=table_count(G_curves)){fail();return vec3<f32>(0);}
    var at=load_word(table_at(G_objects,object,SIZE_objects)+O_attribute_map_offset);let count=table_count(G_attributes_map);
    var offset=NONE;
    for(var steps=0u;steps<=count;steps++){
        if(at>=count||steps==count){fail();return vec3<f32>(0);}
        let address=table_at(G_attributes_map,at,SIZE_attributes_map);
        let lo=load_word(address+AM_id);let hi=load_word(address+AM_id+4u);
        let element=svm_byte(address+AM_element)|(svm_byte(address+AM_element+1u)<<8u);let value=load_word(address+AM_offset);
        if(lo==CV_ATTR_STD_SHADOW_TRANSPARENCY&&hi==0u){if(element==ATTR_ELEMENT_NONE){return vec3<f32>(0);}offset=value;break;}
        if(lo==ATTR_STD_NONE&&hi==0u){if(element==0u){return vec3<f32>(0);}at=value;}else{at+=ATTR_PRIM_TYPES;}
    }
    let k0=load_word(table_at(G_curves,prim,16u)+4u)+(kind>>PRIMITIVE_NUM_BITS);let o=cv_order();
    let words=table_count(G_attributes_float);if(offset==NONE||offset+k0+1u>=words){fail();return vec3<f32>(0);}
    let f0=curve_rgbe_to_rgb(load_word(table_at(G_attributes_float,offset+k0,4u)));let f1=curve_rgbe_to_rgb(load_word(table_at(G_attributes_float,offset+k0+1u,4u)));
    return cvb3(cvb3(cvb(1.0-u,o)*f0,o)+cvb3(u*f1,o),o);
}
//#endif
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
//#if CURVE_SHADOW else     var result=ShadowTrace(false,0u);var record_index=0u;var farthest=tmax;
    var result=ShadowTrace(false,0u,vec3<f32>(1));var record_index=0u;var farthest=tmax;var transparent_hits=0u;
//#endif
    if(table_count(G_prim_index)==0u){return result;}
    var pos=origin;var dir=clamp_direction(direction);var inv=inverse_direction(dir);
    // The traversal stack lives in this invocation's SVM arena slot (words
    // 0..191; intersect_shadow evaluates no SVM program, wavefront_svm.wgsl).
    // As a second 192-entry private array next to the opaque traversal's in
    // intersect_shadow, the Adreno 650 compiler (EV031.32.02.10) lost pushed
    // entries (the instance sentinel: invalid state, mask 1; and lost devices in
    // transparent lighting renders). Pushes, pops and bounds are unchanged.
    let stack_slots=load_word(p(17u)+8u);let stack_region=load_word(p(17u)+4u);
    if(svm_slot>=stack_slots||stack_region==0u){fail();return result;}
    let st=stack_region+svm_slot*(SVM_STACK_WORDS*4u);
    store_word(st,0x76543210u);var sp=0i;
    var node=bitcast<i32>(data_word(D_bvh_root));var object=NONE;var visits=0u;
    loop{
        // Malformed cycles in a scene table must fail instead of hanging a GPU.
        visits++;if(visits>1048576u){fail();return result;}
        while(node>=0&&node!=0x76543210i){
            visits++;if(visits>1048576u){fail();return result;}
//#if CURVE_SHADOW else             let bounds=bounds_hit(pos,inv,tmin,tmax,u32(node),visibility);
            let bounds=curve_node_hit(pos,dir,inv,tmin,tmax,u32(node),visibility);
//#endif
            let a=table_at(G_bvh_nodes,u32(node),SIZE_bvh_nodes);
            node=bitcast<i32>(load_word(a+8u));var other=bitcast<i32>(load_word(a+12u));let mask=u32(bounds.x);
            if(mask==3u){if(bounds.z<bounds.y){let tmp=node;node=other;other=tmp;}
                sp++;if(sp>=192){fail();return result;}store_word(st+u32(sp)*4u,bitcast<u32>(other));
            }else if(mask==2u){node=other;}
            else if(mask==0u){if(sp<0){fail();return result;}node=bitcast<i32>(load_word(st+u32(sp)*4u));sp--;}
        }
        if(node<0){
            let a=table_at(G_bvh_leaf_nodes,u32(-node-1),SIZE_bvh_leaf_nodes);
            let first=bitcast<i32>(load_word(a));let end=load_word(a+4u);let kind=load_word(a+12u);
            if(first>=0){
                if(end>table_count(G_prim_index)||u32(first)>end){fail();return result;}
                if(sp<0){fail();return result;}node=bitcast<i32>(load_word(st+u32(sp)*4u));sp--;
//#if POINTS
                // shadow_all.h, bvh_shadow_all_anyhit_filter: a point leaf.
                if((kind&PRIMITIVE_POINT)!=0u){
                    for(var index=u32(first);index<end;index++){
                        var prim_object=object;if(object==NONE){prim_object=load_word(table_at(G_prim_object,index,SIZE_prim_object));}
                        let prim=load_word(table_at(G_prim_index,index,SIZE_prim_index));
                        if(prim_object==self_object&&prim==self_prim){continue;}
                        if(prim_object==light_object&&prim==light_prim){continue;}
//#if SHADOW_LINKING
                        if(shadow_link_skip(light_object,prim_object)){continue;}
//#endif
                        let point_hit=point_intersect(pos,dir,tmin,tmax,prim_object,prim,kind&PRIMITIVE_ALL,heap_float(state_address(S_shadow_ray_time,path,4u)));
                        if(!point_hit.hit){continue;}
                        let shader=point_shader(prim)&SHADER_MASK;
                        let flags=load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags);
                        if((flags&SD_HAS_TRANSPARENT_SHADOW)==0u){result.opaque=true;return result;}
//#if VOLUME_SHADOW else                         if((flags&SHADOW_ONLY_VOLUME)!=0u){fail();return result;}
//#endif
                        var duplicate=false;for(var k=0u;k<min(result.count,SHADOW_HIT_CAPACITY);k++){let old=shadow_hit_load(path,k);if(old.object==prim_object&&old.prim==prim){duplicate=true;break;}}
                        if(duplicate){continue;}result.count++;
                        if(result.count>max_hits){result.opaque=true;return result;}
                        if(result.count<=SHADOW_HIT_CAPACITY||point_hit.t<farthest){
                            shadow_hit_store(path,record_index,NativeHit(point_hit.t,0.0,0.0,prim_object,prim,kind&PRIMITIVE_ALL));
                            if(result.count>=SHADOW_HIT_CAPACITY){record_index=0u;farthest=shadow_hit_load(path,0u).t;
                                for(var k=1u;k<SHADOW_HIT_CAPACITY;k++){let candidate=shadow_hit_load(path,k).t;if(candidate>farthest){record_index=k;farthest=candidate;}}
                            }else{record_index=result.count;}
                        }
                    }
                    continue;
                }
//#endif
//#if CURVE_SHADOW
                let curve_leaf=(kind&PRIMITIVE_CURVE)!=0u&&(kind&PRIMITIVE_LAMP)==0u;
                if(!native_triangle_kind(kind&PRIMITIVE_ALL)&&!curve_leaf&&u32(first)!=end){fail();return result;}
//#endif
//#if !CURVE_SHADOW
//#if SHADOW_TRANSPARENT_MOTION else                 if((kind&PRIMITIVE_ALL)!=PRIMITIVE_TRIANGLE&&u32(first)!=end){fail();return result;}
                if(!native_triangle_kind(kind&PRIMITIVE_ALL)&&u32(first)!=end){fail();return result;}
//#endif
//#endif
                for(var index=u32(first);index<end;index++){
                    var prim_object=object;if(object==NONE){prim_object=load_word(table_at(G_prim_object,index,SIZE_prim_object));}
                    let prim=load_word(table_at(G_prim_index,index,SIZE_prim_index));
                    if(prim_object==self_object&&prim==self_prim){continue;}
                    if(prim_object==light_object&&prim==light_prim){continue;}
//#if SHADOW_LINKING
                    if(shadow_link_skip(light_object,prim_object)){continue;}
//#endif
//#if CURVE_SHADOW
                    // bvh_shadow_all_anyhit_filter for a curve hit: no per-primitive visibility, nothing recorded, the baked
                    // transparency multiplied in; below the cutoff or past the hit limit the ray is opaque.
                    if(curve_leaf){
                        let curve_type=load_word(table_at(G_prim_type,index,4u));
//#if CURVE_MOTION else                         let curve_hit=curve_intersect(pos,dir,tmin,tmax,prim_object,prim,curve_type);
                        let curve_hit=curve_intersect(pos,dir,tmin,tmax,prim_object,prim,curve_type,heap_float(state_address(S_shadow_ray_time,path,4u)));
//#endif
                        if(curve_hit.hit){
                            let flags=load_word(table_at(G_shaders,load_word(table_at(G_curves,prim,16u))&SHADER_MASK,SIZE_shaders)+SH_flags);
                            if((flags&SD_HAS_TRANSPARENT_SHADOW)==0u){result.opaque=true;return result;}
                            if((flags&SHADOW_ONLY_VOLUME)==0u){transparent_hits++;}
                            if(transparent_hits>max_hits){result.opaque=true;return result;}
                            result.throughput=result.throughput*curve_shadow_transparency(prim_object,prim,curve_type,curve_hit.u);
                            if(cv_max(cv_max(result.throughput.x,result.throughput.y),result.throughput.z)<0.0010000000474974513){result.opaque=true;return result;}
                        }
                        continue;
                    }
//#endif
                    if((load_word(table_at(G_prim_visibility,index,SIZE_prim_visibility))&visibility)==0u){continue;}
//#if !SHADOW_TRANSPARENT_MOTION
                    let obj=table_at(G_objects,prim_object,SIZE_objects);let offset=load_word(obj+O_position_offset);
                    let vi=table_at(G_tri_vindex,prim,SIZE_tri_vindex);
                    let va=heap_vec3(table_at(G_tri_verts,offset+load_word(vi),SIZE_tri_verts));
                    let vb=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+4u),SIZE_tri_verts));
                    let vc=heap_vec3(table_at(G_tri_verts,offset+load_word(vi+8u),SIZE_tri_verts));
//#endif
//#if SHADOW_TRANSPARENT_MOTION
                    // Moving objects and deforming (motion) triangles at the shadow ray's time,
                    // the closest traversal's vertex fetch (native_triangle_vertices).
                    let vertices=native_triangle_vertices(prim_object,prim,kind&PRIMITIVE_ALL,heap_float(state_address(S_shadow_ray_time,path,4u)));
                    if(vertices.error!=0u){return result;}let va=vertices.a;let vb=vertices.b;let vc=vertices.c;
//#endif
                    var u:f32;var v:f32;var t:f32;
                    if(c_ray_triangle_intersect(pos,dir,tmin,tmax,va,vb,vc,&u,&v,&t)){
                        let shader=load_word(table_at(G_tri_shader,prim,4u))&SHADER_MASK;
                        let flags=load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags);
                        if((flags&SD_HAS_TRANSPARENT_SHADOW)==0u){result.opaque=true;return result;}
//#if VOLUME_SHADOW else                         if((flags&SHADOW_ONLY_VOLUME)!=0u){fail();return result;}
                        // A volume-only boundary is a transparent hit: shade_shadow_transparent attenuates the
                        // medium between hits and enters or exits the volume (VOLUME_SHADOW).
//#endif
                        var duplicate=false;for(var k=0u;k<min(result.count,SHADOW_HIT_CAPACITY);k++){let old=shadow_hit_load(path,k);if(old.object==prim_object&&old.prim==prim){duplicate=true;break;}}
                        if(duplicate){continue;}result.count++;
//#if CURVE_SHADOW else                         if(result.count>max_hits){result.opaque=true;return result;}
                        if((flags&SHADOW_ONLY_VOLUME)==0u){transparent_hits++;}
                        if(transparent_hits>max_hits){result.opaque=true;return result;}
//#endif
                        if(result.count<=SHADOW_HIT_CAPACITY||t<farthest){
//#if SHADOW_TRANSPARENT_MOTION else                             shadow_hit_store(path,record_index,NativeHit(t,u,v,prim_object,prim,PRIMITIVE_TRIANGLE));
                            shadow_hit_store(path,record_index,NativeHit(t,u,v,prim_object,prim,kind&PRIMITIVE_ALL));
//#endif
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
//#if SHADOW_TRANSPARENT_MOTION else                 pos=transform_vector(obj+O_itfm,origin,true);dir=clamp_direction(transform_vector(obj+O_itfm,direction,false));inv=inverse_direction(dir);
                if((load_word(table_at(G_object_flag,object,SIZE_object_flag))&SD_OBJECT_MOTION)!=0u){
                    let inverse=motion_inverse(motion_object(object,heap_float(state_address(S_shadow_ray_time,path,4u))));
                    pos=transform_rows(inverse.x,inverse.y,inverse.z,origin,true);dir=clamp_direction(transform_rows(inverse.x,inverse.y,inverse.z,direction,false));}
                else{pos=transform_vector(obj+O_itfm,origin,true);dir=clamp_direction(transform_vector(obj+O_itfm,direction,false));}
                inv=inverse_direction(dir);
//#endif
                sp++;if(sp>=192){fail();return result;}store_word(st+u32(sp)*4u,bitcast<u32>(0x76543210i));
                node=bitcast<i32>(load_word(table_at(G_object_node,object,SIZE_object_node)));
            }
        }
        if(node==0x76543210i){
            if(sp<0){break;}
            if(object==NONE){fail();return result;}
            pos=origin;dir=clamp_direction(direction);inv=inverse_direction(dir);object=NONE;
            node=bitcast<i32>(load_word(st+u32(sp)*4u));sp--;if(node==0x76543210i){break;}
        }
    }
    return result;
}
