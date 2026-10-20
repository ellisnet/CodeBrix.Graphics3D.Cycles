// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2011-2026 Blender Foundation
// SPDX-FileCopyrightText: 2011-2013 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native triangle ShaderData setup, packed normals and compact differentials.
// See native/THIRD-PARTY-NOTICES.txt for source files and transformations.
struct SurfaceData {
    P:vec3<f32>, N:vec3<f32>, Ng:vec3<f32>, wi:vec3<f32>,
    dPdu:vec3<f32>, dPdv:vec3<f32>, du:vec2<f32>, dv:vec2<f32>,
    u:f32, v:f32, time:f32, ray_length:f32, dP:f32, dI:f32,
    object_id:u32, prim:u32, primitive_type:u32, shader:u32,
    runtime_flag:u32, shader_flag:u32, object_flag:u32, error:u32,
}
fn surface_failure(sd:ptr<function,SurfaceData>){(*sd).error=1u;fail();}
fn surface_normalize(v:vec3<f32>)->vec3<f32>{let d=sqrt(dot(v,v));if(d!=0.0){return v*(1.0/d);}return v;}
fn surface_orthogonal(n:vec3<f32>)->vec3<f32>{
    if(n.x!=n.y||n.x!=n.z){return surface_normalize(vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x));}
    return surface_normalize(vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x));
}
fn surface_transform_direction(address:u32,v:vec3<f32>)->vec3<f32>{
    let x=heap_vec4(address);let y=heap_vec4(address+16u);let z=heap_vec4(address+32u);
    return v.x*vec3<f32>(x.x,y.x,z.x)+(v.y*vec3<f32>(x.y,y.y,z.y)+v.z*vec3<f32>(x.z,y.z,z.z));
}
fn surface_transform_point(address:u32,v:vec3<f32>)->vec3<f32>{
    return surface_transform_direction(address,v)+vec3<f32>(heap_float(address+12u),heap_float(address+28u),heap_float(address+44u));
}
fn surface_transform_normal(address:u32,v:vec3<f32>)->vec3<f32>{
    let x=heap_vec3(address);let y=heap_vec3(address+16u);let z=heap_vec3(address+32u);
    return surface_normalize(v.x*x+v.y*y+v.z*z);
}
fn surface_decode_normal(value:u32)->vec3<f32>{
    let xy=vec2<f32>(f32(value&65535u),f32(value>>16u))*(2.0/65535.0)-vec2<f32>(1);
    let z=1.0-abs(xy.x)-abs(xy.y);let t=max(-z,0.0);
    // The packed grid has no exact zero coordinate; sign comparison matches
    // both upstream scalar copysign and the CPU SIMD decoder.
    return surface_normalize(vec3<f32>(xy+select(vec2<f32>(t),vec2<f32>(-t),xy>vec2<f32>(0)),z));
}
fn surface_differentials(sd:ptr<function,SurfaceData>){
    let n=(*sd).Ng;let tangent=surface_orthogonal(n);
    var dx=tangent*(*sd).dP;var dy=cross(n,tangent)*(*sd).dP;
    var a=(*sd).dPdu;var b=(*sd).dPdv;let axis=abs(n);
    if(axis.z<axis.x||axis.z<axis.y){
        if(axis.y<axis.x||axis.y<axis.z){a.x=a.y;b.x=b.y;dx.x=dx.y;dy.x=dy.y;}
        a.y=a.z;b.y=b.z;dx.y=dx.z;dy.y=dy.z;
    }
    var det=a.x*b.y-b.x*a.y;if(det!=0.0){det=1.0/det;}
    (*sd).du=vec2<f32>((dx.x*b.y-dx.y*b.x)*det,(dy.x*b.y-dy.y*b.x)*det);
    (*sd).dv=vec2<f32>((dx.y*a.x-dx.x*a.y)*det,(dy.y*a.x-dy.x*a.y)*det);
}
fn surface_from_ray(index:u32)->SurfaceData{
    var sd:SurfaceData;
    if(index>=p(10u)){surface_failure(&sd);return sd;}
    sd.primitive_type=state_word(S_isect_type,index);sd.object_id=state_word(S_isect_object,index);sd.prim=state_word(S_isect_prim,index);
    if(sd.primitive_type!=PRIMITIVE_TRIANGLE||sd.object_id>=table_count(G_objects)||sd.object_id>=table_count(G_object_flag)||
       sd.prim>=table_count(G_tri_vindex)||sd.prim>=table_count(G_tri_shader)){surface_failure(&sd);return sd;}
    let object=table_at(G_objects,sd.object_id,SIZE_objects);
    sd.object_flag=load_word(table_at(G_object_flag,sd.object_id,4u));
    if((sd.object_flag&(SD_OBJECT_MOTION|SD_OBJECT_HAS_VERTEX_MOTION))!=0u){surface_failure(&sd);return sd;}
    sd.u=heap_float(state_address(S_isect_u,index,4u));sd.v=heap_float(state_address(S_isect_v,index,4u));
    sd.ray_length=heap_float(state_address(S_isect_t,index,4u));sd.time=heap_float(state_address(S_ray_time,index,4u));
    sd.wi=-heap_vec3(state_address(S_ray_D,index,12u));
    let vi_address=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);
    let vi=vec3<u32>(load_word(vi_address),load_word(vi_address+4u),load_word(vi_address+8u));
    let start=bitcast<i32>(load_word(object+O_position_offset));let count=table_count(G_tri_verts);
    let positions=bitcast<vec3<i32>>(vi)+vec3<i32>(start);
    if(any(vi>vec3<u32>(0x7fffffffu))||any(positions<vec3<i32>(0))||any(vec3<u32>(positions)>=vec3<u32>(count))){surface_failure(&sd);return sd;}
    let a=heap_vec3(table_at(G_tri_verts,u32(positions.x),SIZE_tri_verts));
    let b=heap_vec3(table_at(G_tri_verts,u32(positions.y),SIZE_tri_verts));
    let c=heap_vec3(table_at(G_tri_verts,u32(positions.z),SIZE_tri_verts));
    sd.dPdu=b-a;sd.dPdv=c-a;sd.P=(a+sd.u*sd.dPdu)+sd.v*sd.dPdv;
    let applied=(sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
    if(!applied){sd.P=surface_transform_point(object+O_tfm,sd.P);}
    var cross_normal=cross(sd.dPdu,sd.dPdv);
    if(applied&&(sd.object_flag&SD_OBJECT_NEGATIVE_SCALE)!=0u){cross_normal=cross(sd.dPdv,sd.dPdu);}
    if(dot(cross_normal,cross_normal)==0.0){surface_failure(&sd);return sd;}
    sd.Ng=surface_normalize(cross_normal);sd.N=sd.Ng;
    sd.shader=load_word(table_at(G_tri_shader,sd.prim,4u));let shader_id=sd.shader&SHADER_MASK;
    if(shader_id>=table_count(G_shaders)){surface_failure(&sd);return sd;}
    if((sd.shader&SHADER_SMOOTH_NORMAL)!=0u){
        var ni=vi;
        if((sd.object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0u){ni=vec3<u32>(sd.prim*3u)+vec3<u32>(0,1,2);}
        // Native offsets may be negative: global triangle/vertex indices cancel
        // that offset. Check signed addition before turning it into an address.
        let offset=bitcast<i32>(load_word(object+O_normal_offset));
        let nc=table_count(G_attributes_normal);var normals:array<vec3<f32>,3>;
        for(var j=0u;j<3u;j++){
            let at=bitcast<i32>(ni[j])+offset;
            if(at<0||u32(at)>=nc){surface_failure(&sd);return sd;}
            normals[j]=surface_decode_normal(load_word(table_at(G_attributes_normal,u32(at),SIZE_attributes_normal)));
        }
        let interpolated=surface_normalize(((1.0-sd.u)-sd.v)*normals[0]+sd.u*normals[1]+sd.v*normals[2]);
        if(any(interpolated!=vec3<f32>(0))){sd.N=interpolated;}
    }
    if(!applied){
        sd.N=surface_transform_normal(object+O_itfm,sd.N);sd.Ng=surface_transform_normal(object+O_itfm,sd.Ng);
        sd.dPdu=surface_transform_direction(object+O_tfm,sd.dPdu);sd.dPdv=surface_transform_direction(object+O_tfm,sd.dPdv);
    }
    sd.shader_flag=load_word(table_at(G_shaders,shader_id,SIZE_shaders)+SH_flags);
    if(dot(sd.Ng,sd.wi)<0.0){sd.runtime_flag|=SR_BACKFACING;sd.N=-sd.N;sd.Ng=-sd.Ng;sd.dPdu=-sd.dPdu;sd.dPdv=-sd.dPdv;}
    sd.dI=heap_float(state_address(S_ray_dD,index,4u));
    sd.dP=heap_float(state_address(S_ray_dP,index,4u))+sd.ray_length*sd.dI;surface_differentials(&sd);
    return sd;
}
