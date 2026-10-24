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
fn inverse_direction(d:vec3<f32>)->vec3<f32>{
    // Approximate reciprocal can reject a slab exactly at ray.tmin/tmax.
    return vec3<f32>(rounded_divide(1.0,d.x),rounded_divide(1.0,d.y),rounded_divide(1.0,d.z));
}
// Instance push (kernel/geom/object.h bvh_instance_push, util/transform.h
// transform_point/direction) with the originating oracle's grouping and every
// product rounded before its sum. SSE builds transpose and accumulate
// ((w+z)+y)+x (directions start from the z product); scalar builds take the
// float4 dot (x+y)+(z+w) with w=1 for points and w*0 (signed zero) otherwise.
fn exact_transform_row(t:vec4<f32>,v:vec3<f32>,point:bool)->f32{
    if(NATIVE_TRIANGLE_ARITHMETIC==0u){
        let w=select(bitcast<f32>(bitcast<u32>(t.w)&0x80000000u),t.w,point);
        return rounded_add(rounded_add(v.x*t.x,v.y*t.y),rounded_add(v.z*t.z,w));
    }
    var sum=v.z*t.z;if(point){sum=rounded_add(t.w,sum);}
    return rounded_add(rounded_add(sum,v.y*t.y),v.x*t.x);
}
fn transform_rows(x:vec4<f32>,y:vec4<f32>,z:vec4<f32>,v:vec3<f32>,point:bool)->vec3<f32>{
    let rows=array<vec4<f32>,3>(x,y,z);var result:vec3<f32>;
    for(var i=0u;i<3u;i++){result[i]=exact_transform_row(rows[i],v,point);}
    return result;
}
fn transform_vector(a:u32,v:vec3<f32>,point:bool)->vec3<f32>{
    return transform_rows(heap_vec4(a),heap_vec4(a+16u),heap_vec4(a+32u),v,point);
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
                    let inverse=motion_inverse(motion_object(object,time));pos=transform_rows(inverse.x,inverse.y,inverse.z,origin,true);dir=clamp_direction(transform_rows(inverse.x,inverse.y,inverse.z,direction,false));
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
// Point emitters use the same native geometry and strict ray interval tests as Cycles.
fn intersect_point_lights(index:u32,origin:vec3<f32>,direction:vec3<f32>,tmin:f32,tmax:f32,input:NativeHit)->NativeHit{
    var hit=input;let visibility=state_byte(S_path_visibility,index);let flag=state_word(S_path_flag,index);
    for(var lamp=0u;lamp<data_word(D_integrator_num_lights);lamp++){
        let light=table_at(G_lights,lamp,SIZE_lights);let light_kind=load_word(light+L_type);if(light_kind!=LIGHT_POINT&&light_kind!=LIGHT_SPOT&&light_kind!=LIGHT_AREA){continue;}
        let shader=load_word(light+L_shader_id);let object=load_word(light+L_object_id);
        if((visibility&VIS_CAMERA)!=0u){if((shader&LIGHT_EXCLUDE_CAMERA)!=0u){continue;}}
        else if((shader&SHADER_USE_MIS)==0u){continue;}
//#if MNEE
        // lights_intersect_impl: a path MNEE should have resolved skips caustic lamps.
        else if((state_byte(S_path_mnee,index)&MNEE_PATH_CULL_LIGHT_CONNECTION)!=0u&&load_word(light+MNEE_L_use_caustics)!=0u){continue;}
//#endif
        if((flag&PATH_RAY_SHADOW_CATCHER_PASS)!=0u&&(shader&LIGHT_EXCLUDE_SHADOW_CATCHER)!=0u){continue;}
        if(state_word(S_isect_prim,index)==lamp&&state_word(S_isect_object,index)==object&&state_word(S_isect_type,index)==PRIMITIVE_LAMP){continue;}
//#if LIGHT_LINKING
        // lights_intersect_impl: indirect rays skip lamps not linked to the receiver.
        if(!light_link_forward_visible(index,object)){continue;}
//#endif
        let position=heap_vec3(light+L_co);var t=0.0;
        if(light_kind==LIGHT_AREA){
            // area_light_intersect + ray_quad_intersect with rounded products, sums and quotients.
            let N=heap_vec3(light+L_area_dir);let invarea=abs(heap_float(light+L_area_invarea));let facing=rounded_dot(direction,N);if(invarea==0.0||facing>=0.0){continue;}
            t=rounded_divide(-rounded_add(rounded_dot(origin,N),-rounded_dot(position,N)),facing);if(!(t>tmin&&t<tmax)){continue;}
            let plane=rounded_add3(origin,t*direction)-position;
            let axis_u=heap_vec3(light+L_area_axis_u);let len_u=heap_float(light+L_area_len_u);let axis_v=heap_vec3(light+L_area_axis_v);let len_v=heap_float(light+L_area_len_v);
            let u=rounded_dot(plane,vec3<f32>(rounded_divide(axis_u.x,len_u),rounded_divide(axis_u.y,len_u),rounded_divide(axis_u.z,len_u)));
            if(u< -0.5||u>0.5){continue;}
            let v=rounded_dot(plane,vec3<f32>(rounded_divide(axis_v.x,len_v),rounded_divide(axis_v.y,len_v),rounded_divide(axis_v.z,len_v)));
            if(v< -0.5||v>0.5){continue;}if(heap_float(light+L_area_invarea)<0.0&&rounded_add(u*u,v*v)>0.25){continue;}
        }else{
        if(light_kind==LIGHT_SPOT&&rounded_dot(direction,origin-position)>=0.0){continue;}
        let radius=heap_float(light+L_radius);if(radius==0.0){continue;}
        if(load_word(light+L_is_sphere)!=0u){
            let delta=position-origin;let r2=radius*radius;let d2=rounded_dot(delta,delta);let cosine=rounded_dot(delta,direction);
            if(d2>r2&&cosine<0.0){continue;}let perpendicular=rounded_add3(delta,-cosine*direction);let sine2=rounded_dot(perpendicular,perpendicular);
            if(sine2>r2){continue;}let root=rounded_sqrt(rounded_add(r2,-sine2));
            t=rounded_add(cosine,-bitcast<f32>((bitcast<u32>(root)&0x7fffffffu)|(bitcast<u32>(rounded_add(d2,-r2))&0x80000000u)));
            if(!(t>tmin&&t<tmax)){continue;}
        }else{
            let vp=origin-position;let normal=rounded_normalize(vp);let dp=rounded_dot(vp,normal);let cosine=rounded_dot(normal,-direction);
            if(!(dp*cosine>0.0)){continue;}t=rounded_divide(dp,cosine);if(t<0.0){continue;}
            let point=rounded_add3(origin,t*direction);let delta=point-position;
            if(!(rounded_dot(delta,delta)<radius*radius&&t>tmin&&t<tmax)){continue;}
        }
        }
        if(t>=hit.t){continue;}hit=NativeHit(t,0.0,0.0,object,lamp,PRIMITIVE_LAMP);
    }return hit;
}
fn terminate_surface(index:u32,shader_flags:u32)->bool{
    var flag=state_word(S_path_flag,index);
    if(path_ao(index)){
        if((shader_flags&(SD_HAS_TRANSPARENT_SHADOW|SD_HAS_EMISSION))!=0u){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;state_u32(S_path_flag,index,flag);}
        else if(volume_active(index)){flag|=PATH_RAY_TERMINATE_AFTER_VOLUME;state_u32(S_path_flag,index,flag);}
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
            else if(volume_active(index)){state_u32(S_path_flag,index,flag|PATH_RAY_TERMINATE_IN_NEXT_VOLUME);}
            else{return true;}
        }
    }
    return false;
}
//#if SHADOW_CATCHER
// integrator_split_shadow_catcher up to the state copy: the split decision
// (kernel_shadow_catcher_is_path_split_bounce), film_write_shadow_catcher_bounce_data
// and PATH_RAY_SHADOW_CATCHER_HIT on this path. The new path's kernel and shader
// go to this path's mark pair (p(24)); shadow_catcher_assign and
// shadow_catcher_split (wavefront_shadow_catcher_split.wgsl) make the copy.
fn sc_split_mark(index:u32,object:u32,shader:u32,next:u32){
    if(!sc_enabled()){return;}
    let object_flag=load_word(table_at(G_object_flag,object,4u));
    if((object_flag&SC_OBJECT)==0u||(object_flag&SC_HOLDOUT_MASK)!=0u){return;}
    let flag=state_word(S_path_flag,index);
    if((flag&SC_TRANSPARENT_BACKGROUND)==0u||(flag&SC_PATH_HIT)!=0u){return;}
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_path_render_pixel_index,index);
    if(pixel>=p(11u)/stride||p(24u)==0u||shader>0xffffffu){fail();return;}let base=p(4u)+pixel*stride*4u;
    let throughput=heap_vec3(state_address(S_path_throughput,index,12u));
    sc_film_add(base+data_word(SC_D_pass_shadow_catcher_sample_count)*4u,1.0);
    sc_film_add(base+(data_word(SC_D_pass_shadow_catcher_matte)+3u)*4u,(throughput.x+throughput.y+throughput.z)*(1.0/3.0));
    state_u32(S_path_flag,index,flag|SC_PATH_HIT);
    var kernel=next;
    if(data_word(SC_D_pass_background)!=PASS_NONE&&data_word(SC_D_background_transparent)==0u){kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;}
    store_word(p(24u)+index*8u,0x80000000u|(kernel<<24u)|shader);
}
//#endif
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
        hit=trace_native(origin,direction,tmin,tmax,heap_float(state_address(S_ray_time,index,4u)),visibility,self_object,self_prim);
    }
//#if MNEE
    // MNEE path culling (intersect_closest.h): a ray leaving a caustic caster
    // after a caustic receiver culls caustic light connections.
    if(data_word(MNEE_D_use_caustics)!=0u){
        var from_caster=false;var from_receiver=false;
        if((state_byte(S_path_visibility,index)&VIS_CAMERA)==0u&&self_object!=NONE){let flags=load_word(table_at(G_object_flag,self_object,4u));
            from_receiver=(flags&SD_OBJECT_CAUSTICS_RECEIVER)!=0u;from_caster=(flags&SD_OBJECT_CAUSTICS_CASTER)!=0u;}
        var mnee=state_byte(S_path_mnee,index);let ancestor=(mnee&MNEE_PATH_RECEIVER_ANCESTOR)!=0u;
        mnee&=~MNEE_PATH_CULL_LIGHT_CONNECTION;if(from_caster&&ancestor){mnee|=MNEE_PATH_CULL_LIGHT_CONNECTION;}
        if(from_receiver){mnee|=MNEE_PATH_RECEIVER_ANCESTOR;}state_u8(S_path_mnee,index,mnee);
    }
//#endif
    let skip_lights=(data_word(D_integrator_filter_closures)&FILTER_CLOSURE_DIRECT_LIGHT)!=0u&&state_half(S_path_bounce,index)==1u;
    if(data_word(D_integrator_use_light_mis)!=0u&&!skip_lights){hit=intersect_point_lights(index,origin,direction,tmin,tmax,hit);}
    if(load_word(p(17u))!=0u){return;}
    state_f32(S_isect_t,index,hit.t);state_f32(S_isect_u,index,hit.u);state_f32(S_isect_v,index,hit.v);
    state_u32(S_isect_object,index,hit.object);state_u32(S_isect_prim,index,hit.prim);state_u32(S_isect_type,index,hit.kind);
    var next=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;var shader=0u;var sorted=false;
    if(volume_active(index)){
        var flags=0u;if(hit.prim!=NONE&&(hit.kind&PRIMITIVE_LAMP)==0u){let id=load_word(table_at(G_tri_shader,hit.prim,SIZE_tri_shader))&SHADER_MASK;flags=load_word(table_at(G_shaders,id,SIZE_shaders)+SH_flags);}
        next=select(DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME,0u,terminate_surface(index,flags));
    }
    else if(hit.prim!=NONE&&(hit.kind&PRIMITIVE_LAMP)!=0u){next=DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD;}
    else if(hit.prim!=NONE){
        shader=load_word(table_at(G_tri_shader,hit.prim,SIZE_tri_shader))&SHADER_MASK;
        let flags=load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags);
        if(terminate_surface(index,flags)){next=0u;}
        else{next=select(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,(flags&SD_HAS_RAYTRACE)!=0u);sorted=true;}
    }else if((data_word(D_integrator_filter_closures)&FILTER_CLOSURE_DIRECT_LIGHT)!=0u&&state_half(S_path_bounce,index)==1u){next=0u;}
//#if MNEE
    if(sorted&&data_word(MNEE_D_use_caustics)!=0u&&(load_word(table_at(G_object_flag,hit.object,4u))&SD_OBJECT_CAUSTICS_RECEIVER)!=0u){next=MNEE_K_INTERSECT;sorted=false;}
//#endif
    if(sorted){
        let divisor=load_word(p(7u)+S_sort_partition_divisor);let key=shader+data_word(D_max_shaders)*(index/divisor);
        let limit=select(p(15u),p(16u),next==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE);
        if(key>=limit){fail();return;}
        let counter=load_word(p(7u)+S_sort_key_counter+next*POINTER_SIZE);
        atomicAdd(&heap[(counter+key*4u)/4u],1u);state_u32(S_path_shader_sort_key,index,key);
    }
//#if SHADOW_CATCHER
    if(sorted){sc_split_mark(index,hit.object,shader,next);if(load_word(p(17u))!=0u){return;}}
//#endif
    let counter=load_word(p(7u)+S_queue_counter);
    atomicSub(&heap[(counter+K_CLOSEST*4u)/4u],1u);
    if(next==0u){volume_write_depth(index);}
    if(next!=0u){atomicAdd(&heap[(counter+next*4u)/4u],1u);}
    state_u16(S_path_queued_kernel,index,next);
}
