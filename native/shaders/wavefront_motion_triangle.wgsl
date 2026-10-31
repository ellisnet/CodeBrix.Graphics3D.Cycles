// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Adapted from reviewed Cycles geom/motion_triangle.h and
// geom/motion_triangle_intersect.h. Native arrays retain center-first packing.
struct TriangleVertices {a:vec3<f32>,b:vec3<f32>,c:vec3<f32>,error:u32}
struct TriangleTime {a:u32,b:u32,t:f32,error:u32}
fn native_triangle_kind(kind:u32)->bool{return kind==PRIMITIVE_TRIANGLE||kind==PRIMITIVE_MOTION_TRIANGLE;}
fn triangle_motion_time(object:u32,time:f32)->TriangleTime{
    let at=table_at(G_objects,object,SIZE_objects);let steps=load_half(at+O_num_geom_steps);
    if(steps<2u||!(time>=0.0&&time<=1.0)){fail();return TriangleTime(0u,0u,0.0,1u);}
//#if MNEE_MOTION else     let step=min(u32(time*f32(steps-1u)),steps-2u);let t=time*f32(steps-1u)-f32(step);let center=(steps-1u)/2u;
    let step=min(u32(time*f32(steps-1u)),steps-2u);let t=rounded_add(time*f32(steps-1u),-f32(step));let center=(steps-1u)/2u;
//#endif
    let a=select(select(step,step+1u,step<center),0u,step==center);
    let b=select(select(step+1u,step+2u,step+1u<center),0u,step+1u==center);
    return TriangleTime(a,b,t,0u);
}
// Signed native offsets may cancel global vertex/corner indices. Bound both
// the signed center index and the unsigned frame stride before adding them.
fn triangle_frame_index(index:u32,offset:i32,frame:u32,stride:u32,count:u32)->u32{
    if(index>0x7fffffffu||(offset>0&&index>0x7fffffffu-u32(offset))){fail();return NONE;}
    let center=bitcast<i32>(index)+offset;
    if(center<0||u32(center)>=count){fail();return NONE;}
    if(stride==0u||frame>(count-1u-u32(center))/stride){fail();return NONE;}
    return u32(center)+frame*stride;
}
fn triangle_position(object:u32,vertex:u32,frame:u32)->vec3<f32>{
    let at=table_at(G_objects,object,SIZE_objects);let stride=load_word(at+O_numverts);
    let index=triangle_frame_index(vertex,bitcast<i32>(load_word(at+O_position_offset)),frame,stride,table_count(G_tri_verts));
    if(index==NONE){return vec3<f32>(0);}
    return heap_vec3(table_at(G_tri_verts,index,SIZE_tri_verts));
}
fn native_triangle_vertices(object:u32,prim:u32,kind:u32,time:f32)->TriangleVertices{
    var result:TriangleVertices;
    if(!native_triangle_kind(kind)||object>=table_count(G_objects)||object>=table_count(G_object_flag)||prim>=table_count(G_tri_vindex)){fail();result.error=1u;return result;}
    let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));let moving=kind==PRIMITIVE_MOTION_TRIANGLE;
    if(moving!=((flags&SD_OBJECT_HAS_VERTEX_MOTION)!=0u)){fail();result.error=1u;return result;}
    let at=table_at(G_tri_vindex,prim,SIZE_tri_vindex);let vi=vec3<u32>(load_word(at),load_word(at+4u),load_word(at+8u));
    var a=0u;var b=0u;var t=0.0;
    if(moving){let mt=triangle_motion_time(object,time);if(mt.error!=0u){result.error=1u;return result;}a=mt.a;b=mt.b;t=mt.t;}
    var vertices:array<vec3<f32>,3>;
    for(var i=0u;i<3u;i++){
        vertices[i]=triangle_position(object,vi[i],a);
        if(moving){vertices[i]=rounded_add3((1.0-t)*vertices[i],t*triangle_position(object,vi[i],b));}
    }
    result.a=vertices[0];result.b=vertices[1];result.c=vertices[2];
    if(load_word(p(17u))!=0u){result.error=1u;}return result;
}
