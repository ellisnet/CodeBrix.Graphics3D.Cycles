//#if POINTS
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles kernel/geom/point_intersect.h point_shader_setup and kernel/geom/shader_data.h
// shader_setup_from_ray (the point branch). Arithmetic as in wavefront_point.wgsl.
// differential_dudv_compact with every sum and product through the order word. dPdu and dPdv are zero, so the determinant
// is zero and du, dv are signed zeros, which this keeps as pinned.
fn point_differentials(sd:ptr<function,SurfaceData>,o:u32){
    let n=(*sd).Ng;let tangent=surface_orthogonal(n);
    let bitangent=ptb3(ptb3(n.yzx*tangent.zxy,o)-ptb3(n.zxy*tangent.yzx,o),o);
    var dx=ptb3(tangent*(*sd).dP,o);var dy=ptb3(bitangent*(*sd).dP,o);
    var a=(*sd).dPdu;var b=(*sd).dPdv;let axis=abs(n);
    if(axis.z<axis.x||axis.z<axis.y){
        if(axis.y<axis.x||axis.y<axis.z){a.x=a.y;b.x=b.y;dx.x=dx.y;dy.x=dy.y;}
        a.y=a.z;b.y=b.z;dx.y=dx.z;dy.y=dy.z;
    }
    var det=ptb(ptb(a.x*b.y,o)-ptb(b.x*a.y,o),o);if(det!=0.0){det=rounded_divide(1.0,det);}
    (*sd).du=vec2<f32>(ptb(ptb(ptb(dx.x*b.y,o)-ptb(dx.y*b.x,o),o)*det,o),ptb(ptb(ptb(dy.x*b.y,o)-ptb(dy.y*b.x,o),o)*det,o));
    (*sd).dv=vec2<f32>(ptb(ptb(ptb(dx.y*a.x,o)-ptb(dx.x*a.y,o),o)*det,o),ptb(ptb(ptb(dy.y*a.x,o)-ptb(dy.x*a.y,o),o)*det,o));
}
fn point_setup_hit(kind:u32,object_id:u32,prim:u32,u:f32,v:f32,t:f32,time:f32,ray_P:vec3<f32>,ray_D:vec3<f32>,ray_dP:f32,ray_dD:f32)->SurfaceData{
    var sd:SurfaceData;let o=pt_order();
    sd.primitive_type=kind;sd.object_id=object_id;sd.prim=prim;
    if(object_id>=table_count(G_objects)||object_id>=table_count(G_object_flag)||prim>=table_count(G_points_shader)){surface_failure(&sd);return sd;}
    sd.object_flag=load_word(table_at(G_object_flag,object_id,4u));
    sd.u=u;sd.v=v;sd.ray_length=t;sd.time=time;sd.wi=-ray_D;
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){
//#if MOTION_OBJECT else atomicOr(&heap[p(17u)/4u],2u);surface_failure(&sd);return sd;
        sd.motion_tfm=motion_object(object_id,time);sd.motion_itfm=motion_inverse(sd.motion_tfm);
        if(load_word(p(17u))!=0u){surface_failure(&sd);return sd;}
//#endif
    }
    sd.shader=point_shader(prim);let shader_id=sd.shader&SHADER_MASK;
    if(shader_id>=table_count(G_shaders)){surface_failure(&sd);return sd;}
    sd.P=ptb3(ray_P+ptb3(ray_D*t,o),o);
    // The point center, for the normal.
    var center=point_record(object_id,prim,kind,time).xyz;
    if(load_word(p(17u))!=0u){sd.error=1u;return sd;}
    if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)==0u){center=surface_object_point(sd,center,false);}
    sd.Ng=pt_normalize(ptb3(sd.P-center,o),o);sd.N=sd.Ng;
    sd.shader_flag=load_word(table_at(G_shaders,shader_id,SIZE_shaders)+SH_flags);
    if(pt_dot(sd.Ng,sd.wi,o)<0.0){sd.runtime_flag|=SR_BACKFACING;sd.N=-sd.N;sd.Ng=-sd.Ng;sd.dPdu=-sd.dPdu;sd.dPdv=-sd.dPdv;}
    sd.dI=ray_dD;
    sd.dP=rounded_add(ray_dP,sd.ray_length*sd.dI);point_differentials(&sd,o);
    return sd;
}
fn point_setup_from_ray(index:u32)->SurfaceData{
    return point_setup_hit(state_word(S_isect_type,index),state_word(S_isect_object,index),state_word(S_isect_prim,index),
      heap_float(state_address(S_isect_u,index,4u)),heap_float(state_address(S_isect_v,index,4u)),heap_float(state_address(S_isect_t,index,4u)),
      heap_float(state_address(S_ray_time,index,4u)),heap_vec3(state_address(S_ray_P,index,12u)),heap_vec3(state_address(S_ray_D,index,12u)),
      heap_float(state_address(S_ray_dP,index,4u)),heap_float(state_address(S_ray_dD,index,4u)));
}
//#endif
