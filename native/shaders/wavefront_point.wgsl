//#if POINTS
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2021-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles kernel/geom/point_intersect.h (point_intersect_test), kernel/geom/motion_point.h and
// kernel/bvh/util.h (intersection_get_shader). Every product and sum through the order word (a value the compiler cannot
// see, always 0), so that no sum fuses with a product on any backend.
fn pt_order()->u32{return load_word(p(17u)+12u);}
fn ptb(x:f32,o:u32)->f32{return bitcast<f32>(bitcast<u32>(x)^o);}
fn ptb3(x:vec3<f32>,o:u32)->vec3<f32>{return bitcast<vec3<f32>>(bitcast<vec3<u32>>(x)^vec3<u32>(o));}
fn ptb4(x:vec4<f32>,o:u32)->vec4<f32>{return bitcast<vec4<f32>>(bitcast<vec4<u32>>(x)^vec4<u32>(o));}
// (x+y)+z of the rounded products. dpps with mask 0x7f adds z to the masked w (+0), which decides the sign of a zero sum: a
// ray starting exactly at a point. The zero is opaque, so that no compiler folds the sum.
fn pt_dot(a:vec3<f32>,b:vec3<f32>,o:u32)->f32{
    let z=ptb(a.z*b.z,o);
    let tail=select(ptb(z+bitcast<f32>(o),o),z,NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u);
    return ptb(ptb(ptb(a.x*b.x,o)+ptb(a.y*b.y,o),o)+tail,o);
}
// sqrtf: a negative argument gives NaN on every oracle; WGSL leaves it indeterminate.
fn pt_sqrt(x:f32)->f32{if(x<0.0){return bitcast<f32>(0xffc00000u);}return rounded_sqrt(x);}
fn pt_len(v:vec3<f32>,o:u32)->f32{return pt_sqrt(pt_dot(v,v,o));}
// float3 / float through one rounded_divide body (rounded_steps).
fn pt_div3(v:vec3<f32>,f:f32,o:u32)->vec3<f32>{
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return ptb3(v*rounded_divide(1.0,f),o);}
    var out=vec3<f32>(0);let count=rounded_steps(3u);
    for(var k=0u;k<count;k++){
        let q=rounded_divide(select(select(v.z,v.y,k==1u),v.x,k==0u),f);
        out=select(out,vec3<f32>(q),vec3<u32>(0u,1u,2u)==vec3<u32>(k));
    }
    return out;
}
fn pt_normalize(v:vec3<f32>,o:u32)->vec3<f32>{return pt_div3(v,pt_len(v,o),o);}
// A point's float4 (position, radius) at a signed native offset: pointclouds use the global primitive index, which the
// offset cancels.
fn point_at(offset:i32,prim:u32)->vec4<f32>{
    let at=offset+bitcast<i32>(prim);
    if(at<0||u32(at)>=table_count(G_points)){fail();return vec4<f32>(0);}
    return heap_vec4(table_at(G_points,u32(at),16u));
}
//#if POINTS_MOTION
// motion_point_for_step: the center step first, the others after it with the center index skipped.
fn point_step_frame(step:i32,center:i32)->i32{
    if(step==center){return 0;}
    return select(step,step+1,step<center);
}
// motion_point: the two keys around the ray's time, interpolated.
fn point_motion(object:u32,prim:u32,time:f32)->vec4<f32>{
    let o=pt_order();let at=table_at(G_objects,object,SIZE_objects);
    let numsteps=i32(load_half(at+O_num_geom_steps));let numverts=bitcast<i32>(load_word(at+O_numverts));let offset=bitcast<i32>(load_word(at+O_position_offset));
    if(numsteps<2||!(time>=0.0&&time<=1.0)){fail();return vec4<f32>(0);}
    let maxstep=numsteps-1;let scaled=ptb(time*f32(maxstep),o);
    let step=min(i32(scaled),maxstep-1);let t=ptb(scaled-f32(step),o);let center=(numsteps-1)/2;
    let a=point_at(offset+point_step_frame(step,center)*numverts,prim);
    let b=point_at(offset+point_step_frame(step+1,center)*numverts,prim);
    return ptb4(ptb4(ptb(1.0-t,o)*a,o)+ptb4(t*b,o),o);
}
//#endif
// The point at the ray's time: kernel_data_fetch(points, position_offset + prim), or motion_point.
fn point_record(object:u32,prim:u32,kind:u32,time:f32)->vec4<f32>{
    if(object>=table_count(G_objects)){fail();return vec4<f32>(0);}
//#if POINTS_MOTION else     if((kind&PT_PRIMITIVE_MOTION)!=0u){fail();return vec4<f32>(0);}
    if((kind&PT_PRIMITIVE_MOTION)!=0u){return point_motion(object,prim,time);}
//#endif
    return point_at(bitcast<i32>(load_word(table_at(G_objects,object,SIZE_objects)+O_position_offset)),prim);
}
struct PointHit {hit:bool,t:f32}
// point_intersect_test: always back-face culled, as pinned.
fn point_intersect(ray_P:vec3<f32>,ray_D:vec3<f32>,ray_tmin:f32,ray_tmax:f32,object:u32,prim:u32,kind:u32,time:f32)->PointHit{
    let point=point_record(object,prim,kind,time);let o=pt_order();
    let rd2=rounded_divide(1.0,pt_dot(ray_D,ray_D,o));
    let c0=ptb3(point.xyz-ray_P,o);
    let projC0=ptb(pt_dot(c0,ray_D,o)*rd2,o);
    let perp=ptb3(c0-ptb3(projC0*ray_D,o),o);
    let l2=pt_dot(perp,perp,o);let r2=ptb(point.w*point.w,o);
    if(!(l2<=r2)){return PointHit(false,0.0);}
    let td=pt_sqrt(ptb(ptb(r2-l2,o)*rd2,o));
    let t_front=ptb(projC0-td,o);
    if(!(ray_tmin<=t_front&&t_front<=ray_tmax)){return PointHit(false,0.0);}
    return PointHit(true,t_front);
}
fn point_shader(prim:u32)->u32{
    if(prim>=table_count(G_points_shader)){fail();return 0u;}
    return load_word(table_at(G_points_shader,prim,4u));
}
// intersection_get_shader for a hit that is a point or a triangle.
fn point_or_triangle_shader(prim:u32,kind:u32)->u32{
    if((kind&PRIMITIVE_POINT)!=0u){return point_shader(prim);}
    return load_word(table_at(G_tri_shader,prim,SIZE_tri_shader));
}
//#endif
