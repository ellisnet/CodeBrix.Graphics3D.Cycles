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
    motion_tfm:NativeTransform,motion_itfm:NativeTransform,
}
fn surface_failure(sd:ptr<function,SurfaceData>){(*sd).error=1u;fail();}
fn surface_normalize(v:vec3<f32>)->vec3<f32>{return rounded_safe_normalize(v);}
// Original adapter for Cycles normalize(float3): per-component division (SSE
// hosts) or reciprocal-length multiplication (scalar float3 hosts), selected
// by the generated NATIVE_FLOAT3_RECIPROCAL_NORMALIZE of the originating host.
fn surface_native_normalize(v:vec3<f32>)->vec3<f32>{
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return v*rounded_divide(1.0,rounded_sqrt(rounded_dot(v,v)));}
    return rounded_normalize(v);
}
fn surface_orthogonal(n:vec3<f32>)->vec3<f32>{
    if(n.x!=n.y||n.x!=n.z){return surface_normalize(vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x));}
    return surface_normalize(vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x));
}
// Static object transforms (util/transform.h transform_point/direction) with the
// originating oracle's grouping and every product rounded before its sum, as in
// the closest-stage instance push: SSE builds transpose and accumulate
// ((w+z)+y)+x, scalar builds take the float4 dot (x+y)+(z+w). Plain WGSL
// arithmetic let drivers fuse products into sums differently (Intel Mesa and the
// Adreno compiler disagreed by an ULP in ShaderData P of static instances).
fn surface_transform_row(t:vec4<f32>,v:vec3<f32>,point:bool)->f32{
    if(NATIVE_TRIANGLE_ARITHMETIC==0u){
        let w=select(bitcast<f32>(bitcast<u32>(t.w)&0x80000000u),t.w,point);
        return rounded_add(rounded_add(v.x*t.x,v.y*t.y),rounded_add(v.z*t.z,w));
    }
    var sum=v.z*t.z;if(point){sum=rounded_add(t.w,sum);}
    return rounded_add(rounded_add(sum,v.y*t.y),v.x*t.x);
}
fn surface_transform_vector(address:u32,v:vec3<f32>,point:bool)->vec3<f32>{
    return vec3<f32>(surface_transform_row(heap_vec4(address),v,point),surface_transform_row(heap_vec4(address+16u),v,point),surface_transform_row(heap_vec4(address+32u),v,point));
}
fn surface_transform_direction(address:u32,v:vec3<f32>)->vec3<f32>{return surface_transform_vector(address,v,false);}
fn surface_transform_point(address:u32,v:vec3<f32>)->vec3<f32>{return surface_transform_vector(address,v,true);}
fn surface_transform_normal(address:u32,v:vec3<f32>)->vec3<f32>{
    let x=heap_vec3(address);let y=heap_vec3(address+16u);let z=heap_vec3(address+32u);
    return surface_native_normalize(rounded_add3(rounded_add3(v.x*x,v.y*y),v.z*z));
}
fn surface_object_direction(sd:SurfaceData,v:vec3<f32>,inverse:bool)->vec3<f32>{
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){if(inverse){return motion_direction(sd.motion_itfm,v);}return motion_direction(sd.motion_tfm,v);}
    let object=table_at(G_objects,sd.object_id,SIZE_objects);return surface_transform_direction(object+select(O_tfm,O_itfm,inverse),v);
}
fn surface_object_point(sd:SurfaceData,v:vec3<f32>,inverse:bool)->vec3<f32>{
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){if(inverse){return motion_point(sd.motion_itfm,v);}return motion_point(sd.motion_tfm,v);}
    let object=table_at(G_objects,sd.object_id,SIZE_objects);return surface_transform_point(object+select(O_tfm,O_itfm,inverse),v);
}
fn surface_object_normal(sd:SurfaceData,v:vec3<f32>,inverse:bool)->vec3<f32>{
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){if(inverse){return surface_native_normalize(motion_transpose(sd.motion_tfm,v));}return surface_native_normalize(motion_transpose(sd.motion_itfm,v));}
    let object=table_at(G_objects,sd.object_id,SIZE_objects);return surface_transform_normal(object+select(O_itfm,O_tfm,inverse),v);
}
fn surface_decode_normal(value:u32)->vec3<f32>{
    let raw=vec2<f32>(f32(value&65535u),f32(value>>16u))*(2.0/65535.0);
    let xy=vec2<f32>(rounded_add(raw.x,-1.0),rounded_add(raw.y,-1.0));
    let z=rounded_add(rounded_add(1.0,-abs(xy.x)),-abs(xy.y));let t=max(-z,0.0);
    // The packed grid has no exact zero coordinate; sign comparison matches
    // both upstream scalar copysign and the CPU SIMD decoder.
    let shift=select(vec2<f32>(t),vec2<f32>(-t),xy>vec2<f32>(0));
    return surface_normalize(vec3<f32>(rounded_add(xy.x,shift.x),rounded_add(xy.y,shift.y),z));
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
fn surface_setup_hit(primitive_type:u32,object_id:u32,prim:u32,u:f32,v:f32,t:f32,time:f32,ray_direction:vec3<f32>,ray_dP:f32,ray_dD:f32)->SurfaceData{
    var sd:SurfaceData;
    sd.primitive_type=primitive_type;sd.object_id=object_id;sd.prim=prim;
    if(!native_triangle_kind(sd.primitive_type)||sd.object_id>=table_count(G_objects)||sd.object_id>=table_count(G_object_flag)||
       sd.prim>=table_count(G_tri_vindex)||sd.prim>=table_count(G_tri_shader)){surface_failure(&sd);return sd;}
    let object=table_at(G_objects,sd.object_id,SIZE_objects);
    sd.object_flag=load_word(table_at(G_object_flag,sd.object_id,4u));
    sd.u=u;sd.v=v;
    sd.ray_length=t;sd.time=time;
    sd.wi=-ray_direction;
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){
//#if MOTION_OBJECT else atomicOr(&heap[p(17u)/4u],2u);surface_failure(&sd);return sd;
        sd.motion_tfm=motion_object(sd.object_id,sd.time);sd.motion_itfm=motion_inverse(sd.motion_tfm);
        if(load_word(p(17u))!=0u){surface_failure(&sd);return sd;}
//#endif
    }
    let vi_address=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);
    let vi=vec3<u32>(load_word(vi_address),load_word(vi_address+4u),load_word(vi_address+8u));
    let vertices=native_triangle_vertices(sd.object_id,sd.prim,sd.primitive_type,sd.time);
    if(vertices.error!=0u){surface_failure(&sd);return sd;}let a=vertices.a;let b=vertices.b;let c=vertices.c;
    // triangle_point_from_uv: a+u*(b-a)+v*(c-a), each product rounded before its sum.
    sd.dPdu=b-a;sd.dPdv=c-a;sd.P=rounded_add3(rounded_add3(a,sd.u*sd.dPdu),sd.v*sd.dPdv);
    let applied=(sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
    if(!applied){sd.P=surface_object_point(sd,sd.P,false);}
    var cross_normal=rounded_cross(sd.dPdu,sd.dPdv);
    if(applied&&(sd.object_flag&SD_OBJECT_NEGATIVE_SCALE)!=0u){cross_normal=rounded_cross(sd.dPdv,sd.dPdu);}
    if(dot(cross_normal,cross_normal)==0.0){surface_failure(&sd);return sd;}
    sd.Ng=surface_native_normalize(cross_normal);sd.N=sd.Ng;
    sd.shader=load_word(table_at(G_tri_shader,sd.prim,4u));let shader_id=sd.shader&SHADER_MASK;
    if(shader_id>=table_count(G_shaders)){surface_failure(&sd);return sd;}
    if((sd.shader&SHADER_SMOOTH_NORMAL)!=0u){
        var ni=vi;
        if((sd.object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0u){ni=vec3<u32>(sd.prim*3u)+vec3<u32>(0,1,2);}
        // Native offsets may be negative: global triangle/vertex indices cancel
        // that offset. Check signed addition before turning it into an address.
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
        sd.N=surface_object_normal(sd,sd.N,false);sd.Ng=surface_object_normal(sd,sd.Ng,false);
        sd.dPdu=surface_object_direction(sd,sd.dPdu,false);sd.dPdv=surface_object_direction(sd,sd.dPdv,false);
    }
    sd.shader_flag=load_word(table_at(G_shaders,shader_id,SIZE_shaders)+SH_flags);
    if(dot(sd.Ng,sd.wi)<0.0){sd.runtime_flag|=SR_BACKFACING;sd.N=-sd.N;sd.Ng=-sd.Ng;sd.dPdu=-sd.dPdu;sd.dPdv=-sd.dPdv;}
    sd.dI=ray_dD;
    sd.dP=ray_dP+sd.ray_length*sd.dI;surface_differentials(&sd);
    return sd;
}

fn surface_from_ray(index:u32)->SurfaceData{
    if(index>=p(10u)){var sd:SurfaceData;surface_failure(&sd);return sd;}
    return surface_setup_hit(state_word(S_isect_type,index),state_word(S_isect_object,index),state_word(S_isect_prim,index),
      heap_float(state_address(S_isect_u,index,4u)),heap_float(state_address(S_isect_v,index,4u)),heap_float(state_address(S_isect_t,index,4u)),
      heap_float(state_address(S_ray_time,index,4u)),heap_vec3(state_address(S_ray_D,index,12u)),
      heap_float(state_address(S_ray_dP,index,4u)),heap_float(state_address(S_ray_dD,index,4u)));
}
