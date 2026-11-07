//#if CURVES
// SPDX-FileCopyrightText: 2009-2021 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles kernel/geom/curve_intersect.h and util/math_*.h: the arithmetic shared by the curve
// intersection (wavefront_curve_intersect.wgsl) and the curve shader data (wavefront_curve_shader_data.wgsl).
fn cv_order()->u32{return load_word(p(17u)+12u);}
fn cvb(x:f32,o:u32)->f32{return bitcast<f32>(bitcast<u32>(x)^o);}
fn cvb3(x:vec3<f32>,o:u32)->vec3<f32>{return bitcast<vec3<f32>>(bitcast<vec3<u32>>(x)^vec3<u32>(o));}
fn cvb4(x:vec4<f32>,o:u32)->vec4<f32>{return bitcast<vec4<f32>>(bitcast<vec4<u32>>(x)^vec4<u32>(o));}
fn cv_min(a:f32,b:f32)->f32{return select(b,a,a<b);}
fn cv_max(a:f32,b:f32)->f32{return select(b,a,a>b);}
fn cv_min4(a:vec4<f32>,b:vec4<f32>)->vec4<f32>{return select(b,a,a<b);}
fn cv_max4(a:vec4<f32>,b:vec4<f32>)->vec4<f32>{return select(b,a,a>b);}
fn cv_clamp01(x:f32)->f32{return cv_min(cv_max(x,0.0),1.0);}
// (x+y)+z of the rounded products (dpps and the scalar sum group the same way).
fn cv_dot(a:vec3<f32>,b:vec3<f32>,o:u32)->f32{return cvb(cvb(cvb(a.x*b.x,o)+cvb(a.y*b.y,o),o)+cvb(a.z*b.z,o),o);}
fn cv_cross(a:vec3<f32>,b:vec3<f32>,o:u32)->vec3<f32>{return cvb3(cvb3(a.yzx*b.zxy,o)-cvb3(a.zxy*b.yzx,o),o);}
// sqrtf: a negative argument gives NaN on every oracle; WGSL leaves it indeterminate.
fn cv_sqrt(x:f32)->f32{if(x<0.0){return bitcast<f32>(0xffc00000u);}return rounded_sqrt(x);}
fn cv_len(v:vec3<f32>,o:u32)->f32{return cv_sqrt(cv_dot(v,v,o));}
// float3 / float through one rounded_divide body (rounded_steps).
fn cv_div3(v:vec3<f32>,f:f32,o:u32)->vec3<f32>{
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return cvb3(v*rounded_divide(1.0,f),o);}
    var out=vec3<f32>(0);let count=rounded_steps(3u);
    for(var k=0u;k<count;k++){
        let q=rounded_divide(select(select(v.z,v.y,k==1u),v.x,k==0u),f);
        out=select(out,vec3<f32>(q),vec3<u32>(0u,1u,2u)==vec3<u32>(k));
    }
    return out;
}
fn cv_normalize(v:vec3<f32>,o:u32)->vec3<f32>{return cv_div3(v,cv_len(v,o),o);}
fn cv_mix(a:f32,b:f32,t:f32,o:u32)->f32{return cvb(a+cvb(t*cvb(b-a,o),o),o);}
fn cv_mix4(a:vec4<f32>,b:vec4<f32>,t:f32,o:u32)->vec4<f32>{return cvb4(a+cvb4(t*cvb4(b-a,o),o),o);}
fn cv_blend(c0:vec4<f32>,c1:vec4<f32>,c2:vec4<f32>,c3:vec4<f32>,n:vec4<f32>,o:u32)->vec4<f32>{
    return cvb4(cvb4(cvb4(cvb4(c0*n.x,o)+cvb4(c1*n.y,o),o)+cvb4(c2*n.z,o),o)+cvb4(c3*n.w,o),o);
}
fn cv_eval(c0:vec4<f32>,c1:vec4<f32>,c2:vec4<f32>,c3:vec4<f32>,u:f32,o:u32)->vec4<f32>{
    let t=u;let s=cvb(1.0-u,o);let tt=cvb(t*t,o);let ss=cvb(s*s,o);
    let n0=cvb(cvb(-t*s,o)*s,o);
    let n1=cvb(2.0+cvb(tt*cvb(cvb(3.0*t,o)-5.0,o),o),o);
    let n2=cvb(2.0+cvb(ss*cvb(cvb(3.0*s,o)-5.0,o),o),o);
    let n3=cvb(cvb(-s*t,o)*t,o);
    return cvb4(0.5*cv_blend(c0,c1,c2,c3,vec4<f32>(n0,n1,n2,n3),o),o);
}
fn cv_derivative(c0:vec4<f32>,c1:vec4<f32>,c2:vec4<f32>,c3:vec4<f32>,u:f32,o:u32)->vec4<f32>{
    let t=u;let s=cvb(1.0-u,o);let t3=cvb(3.0*t,o);
    let n0=cvb(cvb(-s*s,o)+cvb(cvb(2.0*s,o)*t,o),o);
    let n1=cvb(cvb(cvb(2.0*t,o)*cvb(t3-5.0,o),o)+cvb(t3*t,o),o);
    let n2=cvb(cvb(cvb(2.0*s,o)*cvb(t3+2.0,o),o)-cvb(cvb(3.0*s,o)*s,o),o);
    let n3=cvb(cvb(cvb(-2.0*s,o)*t,o)+cvb(t*t,o),o);
    return cvb4(0.5*cv_blend(c0,c1,c2,c3,vec4<f32>(n0,n1,n2,n3),o),o);
}
fn curve_key(position_offset:i32,key:u32)->vec4<f32>{
    let at=position_offset+i32(key);
    if(at<0||u32(at)>=table_count(G_curve_keys)){fail();return vec4<f32>(0);}
    return heap_vec4(table_at(G_curve_keys,u32(at),16u));
}
//#if CURVE_MOTION
// motion_curve_keys and motion_curve_keys_linear (geom/motion_curve.h): (1-t)*a+t*b of the two steps around the ray time,
// each product and sum through the order word; the centre step comes first in the table (triangle_motion_time's frames).
fn curve_motion_key(object:u32,frames:TriangleTime,key:u32,o:u32)->vec4<f32>{
    let at=table_at(G_objects,object,SIZE_objects);
    let position_offset=bitcast<i32>(load_word(at+O_position_offset));let numverts=load_word(at+O_numverts);
    let a=curve_key(position_offset+i32(frames.a*numverts),key);let b=curve_key(position_offset+i32(frames.b*numverts),key);
    return cvb4(cvb4(cvb(1.0-frames.t,o)*a,o)+cvb4(frames.t*b,o),o);
}
//#endif
//#endif
