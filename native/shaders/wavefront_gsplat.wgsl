//#if GSPLATS
// SPDX-FileCopyrightText: 2011-2026 Blender Foundation
// SPDX-FileCopyrightText: 2021-2026 Blender Foundation
// SPDX-FileCopyrightText: 2026 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles kernel/geom/gsplat_intersect.h (gsplat_intersect_get_params, gsplat_intersect_test),
// kernel/geom/motion_gsplat.h, kernel/geom/gsplat.h (gsplat_normal), kernel/geom/point_intersect.h
// (point_intersect_test<false>), util/math_float3x3.h, util/math_quaternion.h, util/math_fast.h (fast_expf) and
// util/hash.h (hash_float3_to_float). A Gaussian splat is a point of a point cloud rendered as splats: the primitive
// kinds PRIMITIVE_GSPLAT and PRIMITIVE_MOTION_GSPLAT. In a module with this region the layout's PRIMITIVE_POINT is
// PRIMITIVE_ANY_POINT (points and splats; camera.cpp), GS_PRIMITIVE_POINT the plain point's bit and GS_PRIMITIVE_GSPLAT
// the splat's. The stochastic test hashes the bits of the hit position, so every sum, product and quotient here is the
// oracle's operation in the oracle's order: each passes the order word (wavefront_point.wgsl), and the quotients and
// the square root are the correctly rounded adapters.
// kernel_data_fetch of a splat attribute at a signed native index (the offsets hold the global primitive index).
fn gs_scale_at(at:i32)->vec3<f32>{
    if(at<0||u32(at)>=table_count(G_attributes_float3)){fail();return vec3<f32>(0);}
    return heap_vec3(table_at(G_attributes_float3,u32(at),SIZE_attributes_float3));
}
fn gs_base_at(at:i32)->vec4<f32>{
    if(at<0||u32(at)>=table_count(G_attributes_float4)){fail();return vec4<f32>(0);}
    return heap_vec4(table_at(G_attributes_float4,u32(at),SIZE_attributes_float4));
}
// A Quaternion is stored w, x, y, z (util/types_quaternion.h): the vec4's x is w, y is x, z is y and w is z.
fn gs_rotation_at(at:i32)->vec4<f32>{
    if(at<0||u32(at)>=table_count(G_attributes_quaternion)){fail();return vec4<f32>(0);}
    return heap_vec4(table_at(G_attributes_quaternion,u32(at),SIZE_attributes_quaternion));
}
// dot(Quaternion, Quaternion) (util/math_quaternion.h): a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z, left to right.
fn gs_quat_dot(a:vec4<f32>,b:vec4<f32>,o:u32)->f32{
    return ptb(ptb(ptb(ptb(a.x*b.x,o)+ptb(a.y*b.y,o),o)+ptb(a.z*b.z,o),o)+ptb(a.w*b.w,o),o);
}
//#if GSPLATS_MOTION
struct GsMotion {step:i32,next:i32,t:f32}
// motion_gsplat_compute_motion_info: the offsets of the two steps around the ray's time (motion_point_offset_for_step)
// and the interpolation factor.
fn gs_motion(object:u32,time:f32)->GsMotion{
    let o=pt_order();let at=table_at(G_objects,object,SIZE_objects);
    let numsteps=i32(load_half(at+O_num_geom_steps));let numverts=bitcast<i32>(load_word(at+O_numverts));
    if(numsteps<2||!(time>=0.0&&time<=1.0)){fail();return GsMotion(0,0,0.0);}
    let maxstep=numsteps-1;let scaled=ptb(time*f32(maxstep),o);
    let step=min(i32(scaled),maxstep-1);let t=ptb(scaled-f32(step),o);let center=(numsteps-1)/2;
    return GsMotion(point_step_frame(step,center)*numverts,point_step_frame(step+1,center)*numverts,t);
}
// motion_gsplat_rotation_interpolate: lerp(normalized(r1), normalized(r2), t) (util/math_quaternion.h). normalized:
// `len != 0.0f` ? q * (1.0f / len) : identity, where a NaN length takes the first branch (tested as !(len == 0)). lerp:
// `dot(q1, q2) >= 0.0f ? q2 : -q2`, then normalized((1.0f - t) * q1 + t * q2). The three normalizations run through
// one body (rounded_steps): the same operations in the same order.
fn gs_quat_lerp(r1:vec4<f32>,r2:vec4<f32>,t:f32,o:u32)->vec4<f32>{
    var q1=r1;var q2=r2;var out=vec4<f32>(0);let count=rounded_steps(3u);
    for(var k=0u;k<count;k++){
        var q=q1;
        if(k==1u){q=q2;}
        else if(k==2u){
            if(!(gs_quat_dot(q1,q2,o)>=0.0)){q2=-q2;}
            q=ptb4(ptb4(ptb(1.0-t,o)*q1,o)+ptb4(t*q2,o),o);
        }
        let len=pt_sqrt(gs_quat_dot(q,q,o));var n=vec4<f32>(1.0,0.0,0.0,0.0);
        if(!(len==0.0)){n=ptb4(q*rounded_divide(1.0,len),o);}
        if(k==0u){q1=n;}else if(k==1u){q2=n;}else if(k==2u){out=n;}
    }
    return out;
}
//#endif
struct GsParams {scale:vec3<f32>,rotation:vec4<f32>,opacity:f32}
// gsplat_intersect_get_params, and the scale and rotation of gsplat_normal: the object's attribute offsets carry a
// motion flag in their lowest bit (gsplat_attr_*_offset, gsplat_attr_*_has_motion). Without PRIMITIVE_MOTION the values
// at offset + prim; with it motion_gsplat_scale, motion_gsplat_rotation and motion_gsplat_opacity: mix(a, b, t), which is
// a + t * (b - a), of the two steps of an attribute that has motion, the plain value of one that has none.
fn gs_params(object:u32,prim:u32,kind:u32,time:f32)->GsParams{
    if(object>=table_count(G_objects)){fail();return GsParams(vec3<f32>(0),vec4<f32>(0),0.0);}
    let at=table_at(G_objects,object,SIZE_objects);
    let scale_word=load_word(at+GS_O_scale);let rotation_word=load_word(at+GS_O_rotation);let base_word=load_word(at+GS_O_radiance_base);
    let scale_at=(bitcast<i32>(scale_word)>>1u)+bitcast<i32>(prim);let rotation_at=(bitcast<i32>(rotation_word)>>1u)+bitcast<i32>(prim);
    let base_at=(bitcast<i32>(base_word)>>1u)+bitcast<i32>(prim);
    if((kind&PT_PRIMITIVE_MOTION)==0u){return GsParams(gs_scale_at(scale_at),gs_rotation_at(rotation_at),gs_base_at(base_at).w);}
//#if GSPLATS_MOTION else     fail();return GsParams(vec3<f32>(0),vec4<f32>(0),0.0);
    let m=gs_motion(object,time);let o=pt_order();
    var scale=vec3<f32>(0);var rotation=vec4<f32>(0);var opacity=0.0;
    if((scale_word&1u)!=0u){let a=gs_scale_at(m.step+scale_at);let b=gs_scale_at(m.next+scale_at);scale=ptb3(a+ptb3(m.t*ptb3(b-a,o),o),o);}
    else{scale=gs_scale_at(scale_at);}
    if((rotation_word&1u)!=0u){rotation=gs_quat_lerp(gs_rotation_at(m.step+rotation_at),gs_rotation_at(m.next+rotation_at),m.t,o);}
    else{rotation=gs_rotation_at(rotation_at);}
    if((base_word&1u)!=0u){let a=gs_base_at(m.step+base_at).w;let b=gs_base_at(m.next+base_at).w;opacity=ptb(a+ptb(m.t*ptb(b-a,o),o),o);}
    else{opacity=gs_base_at(base_at).w;}
    return GsParams(scale,rotation,opacity);
//#endif
}
// The ten products of the quaternion's components that the rotation matrix is made of (the "common terms" of
// quaternion_to_scaled_rotation; gsplat_normal writes the same products out).
struct GsTerms {aa:f32,ab:f32,ac:f32,ad:f32,bb:f32,bc:f32,bd:f32,cc:f32,cd:f32,dd:f32}
fn gs_terms(q:vec4<f32>,o:u32)->GsTerms{
    let a=q.x;let b=q.y;let c=q.z;let d=q.w;
    return GsTerms(ptb(a*a,o),ptb(a*b,o),ptb(a*c,o),ptb(a*d,o),ptb(b*b,o),ptb(b*c,o),ptb(b*d,o),ptb(c*c,o),ptb(c*d,o),ptb(d*d,o));
}
// The rows of quaternion_to_scaled_rotation(q) * normalizer, which is quaternion_to_rotation (util/math_float3x3.h):
// "Convert quaternion to a 3x3 scaled rotation matrix. [...] No normalization of the quaternion is performed, i.e.
// R = ||q||^2 * Q, where Q is an orthonormal matrix such that det(Q) = 1 and Q*Q' = I." and "Same as above except that
// the rotation matrix is normalized by the Frobenius norm, so that R * R' = I (and det(R) = 1)."
// Adopted from Ceres-solver. (Ceres Solver, Copyright Google Inc., BSD-3-Clause: the notice is
// third_party/licenses/CeresSolver-BSD-3-Clause.txt.)
struct GsMatrix {x:vec3<f32>,y:vec3<f32>,z:vec3<f32>}
fn gs_quaternion_to_rotation(q:vec4<f32>,normalizer:f32,o:u32)->GsMatrix{
    let s=gs_terms(q,o);
    let rx=vec3<f32>(ptb(ptb(ptb(s.aa+s.bb,o)-s.cc,o)-s.dd,o),ptb(2.0*ptb(s.bc-s.ad,o),o),ptb(2.0*ptb(s.ac+s.bd,o),o));
    let ry=vec3<f32>(ptb(2.0*ptb(s.ad+s.bc,o),o),ptb(ptb(ptb(s.aa-s.bb,o)+s.cc,o)-s.dd,o),ptb(2.0*ptb(s.cd-s.ab,o),o));
    let rz=vec3<f32>(ptb(2.0*ptb(s.bd-s.ac,o),o),ptb(2.0*ptb(s.ab+s.cd,o),o),ptb(ptb(ptb(s.aa-s.bb,o)-s.cc,o)+s.dd,o));
    return GsMatrix(ptb3(rx*normalizer,o),ptb3(ry*normalizer,o),ptb3(rz*normalizer,o));
}
// float3x3 * float3: a dot product per row.
fn gs_transform(m:GsMatrix,v:vec3<f32>,o:u32)->vec3<f32>{return vec3<f32>(pt_dot(m.x,v,o),pt_dot(m.y,v,o),pt_dot(m.z,v,o));}
// fast_expf (util/math_fast.h): fast_exp2f(x / M_LN2_F). clamp is min(max(x, -126), 126) with `(a > b) ? a : b` and
// `(a < b) ? a : b`: a NaN becomes -126. madd(a, b, c) is a * b + c with the product rounded (no fused multiply-add in
// the oracle).
fn gs_fast_exp(x_in:f32,o:u32)->f32{
    var x=rounded_divide(x_in,bitcast<f32>(0x3f317218u));
    if(!(x>-126.0)){x=-126.0;}
    if(!(x<126.0)){x=126.0;}
    let m=i32(x);x=ptb(x-f32(m),o);x=ptb(1.0-ptb(1.0-x,o),o);
    var r=1.33336498402e-3;
    r=ptb(ptb(x*r,o)+9.810352697968e-3,o);r=ptb(ptb(x*r,o)+5.551834031939e-2,o);r=ptb(ptb(x*r,o)+0.2401793301105,o);
    r=ptb(ptb(x*r,o)+0.693144857883,o);r=ptb(ptb(x*r,o)+1.0,o);
    return bitcast<f32>(bitcast<u32>(r)+(u32(m)<<23u));
}
fn gs_rot(x:u32,k:u32)->u32{return (x<<k)|(x>>(32u-k));}
// hash_float3_to_float (util/hash.h): uint_to_float_incl(hash_uint3(the three floats' bits)), the lookup3 final mix. The
// conversion (float)n rounds to nearest even; the scale 1.0f / (float)0xFFFFFFFFu is exactly 2^-32.
fn gs_hash_float(k:vec3<f32>)->f32{
    let bits=bitcast<vec3<u32>>(k);var a=0xdeadbeefu+(3u<<2u)+13u;var b=a;var c=a;
    c+=bits.z;b+=bits.y;a+=bits.x;
    c^=b;c-=gs_rot(b,14u);a^=c;a-=gs_rot(c,11u);b^=a;b-=gs_rot(a,25u);c^=b;c-=gs_rot(b,16u);
    a^=c;a-=gs_rot(c,4u);b^=a;b-=gs_rot(a,14u);c^=b;c-=gs_rot(b,24u);
    if(c==0u){return 0.0;}
    let shift=countLeadingZeros(c);let value=c<<shift;let low=value&0xffu;var mantissa=value>>8u;
    if(low>0x80u||(low==0x80u&&(mantissa&1u)!=0u)){mantissa+=1u;}
    return bitcast<f32>(((158u-shift)<<23u)+mantissa-0x800000u)*2.3283064365386963e-10;
}
// gsplat_intersect_test, the stochastic alpha test of a ray against a splat. First the bounding sphere,
// point_intersect_test<false> (no back-face culling: the front or the back distance inside the ray's interval). Then
// S_inv = float3x3_scale(1.0f / scale), R = quaternion_to_rotation(rotation), S_inv * transposed(R) (operator* of two
// float3x3: each row is ((a.x * b.x) + a.y * b.y) + a.z * b.z, the zeros of the scale matrix multiplied in as the oracle
// does, so that an infinite or NaN term stays one), o_g and d_g, t = -dot(o_g, d_g) / dot(d_g, d_g), rejected by
// `!(t >= ray_tmin && t <= ray_tmax)` (a NaN distance, from a zero scale, is rejected), the response
// opacity * fast_expf(-1/2 * e) at x = ray_P + t * ray_D, and the test `hash_float3_to_float(x) > particle_opacity`.
fn gsplat_intersect(ray_P:vec3<f32>,ray_D:vec3<f32>,ray_tmin:f32,ray_tmax:f32,object:u32,prim:u32,kind:u32,time:f32)->PointHit{
    let point=point_record(object,prim,kind,time);let o=pt_order();
    let rd2=rounded_divide(1.0,pt_dot(ray_D,ray_D,o));
    let c0=ptb3(point.xyz-ray_P,o);
    let projC0=ptb(pt_dot(c0,ray_D,o)*rd2,o);
    let perp=ptb3(c0-ptb3(projC0*ray_D,o),o);
    let l2=pt_dot(perp,perp,o);let r2=ptb(point.w*point.w,o);
    if(!(l2<=r2)){return PointHit(false,0.0);}
    let td=pt_sqrt(ptb(ptb(r2-l2,o)*rd2,o));
    let t_front=ptb(projC0-td,o);let t_back=ptb(projC0+td,o);
    if(!((ray_tmin<=t_front&&t_front<=ray_tmax)||(ray_tmin<=t_back&&t_back<=ray_tmax))){return PointHit(false,0.0);}
    let params=gs_params(object,prim,kind,time);let mu=point.xyz;
    // The quotients 1.0f / scale (three) and the normalizer 1.0f / (q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z) of
    // quaternion_to_rotation through one rounded_divide body (rounded_steps).
    let den=vec4<f32>(params.scale,gs_quat_dot(params.rotation,params.rotation,o));var inv=vec4<f32>(0);let count=rounded_steps(4u);
    for(var k=0u;k<count;k++){
        let q=rounded_divide(1.0,select(select(select(den.w,den.z,k==2u),den.y,k==1u),den.x,k==0u));
        inv=select(inv,vec4<f32>(q),vec4<u32>(0u,1u,2u,3u)==vec4<u32>(k));
    }
    let R=gs_quaternion_to_rotation(params.rotation,inv.w,o);
    let tx=vec3<f32>(R.x.x,R.y.x,R.z.x);let ty=vec3<f32>(R.x.y,R.y.y,R.z.y);let tz=vec3<f32>(R.x.z,R.y.z,R.z.z);
    // The zero of float3x3_scale, opaque to the compiler: its products are computed, not folded.
    let zero=bitcast<f32>(o);
    let m=GsMatrix(ptb3(ptb3(ptb3(inv.x*tx,o)+ptb3(zero*ty,o),o)+ptb3(zero*tz,o),o),
                   ptb3(ptb3(ptb3(zero*tx,o)+ptb3(inv.y*ty,o),o)+ptb3(zero*tz,o),o),
                   ptb3(ptb3(ptb3(zero*tx,o)+ptb3(zero*ty,o),o)+ptb3(inv.z*tz,o),o));
    let o_g=gs_transform(m,ptb3(ray_P-mu,o),o);let d_g=gs_transform(m,ray_D,o);
    let t=rounded_divide(-pt_dot(o_g,d_g,o),pt_dot(d_g,d_g,o));
    if(!(t>=ray_tmin&&t<=ray_tmax)){return PointHit(false,0.0);}
    let x=ptb3(ray_P+ptb3(t*ray_D,o),o);
    let e_o_g=gs_transform(m,ptb3(x-mu,o),o);let e=pt_dot(e_o_g,e_o_g,o);
    let particle_opacity=ptb(params.opacity*gs_fast_exp(ptb(-0.5*e,o),o),o);
    if(gs_hash_float(x)>particle_opacity){return PointHit(false,0.0);}
    return PointHit(true,t);
}
// gsplat_normal (kernel/geom/gsplat.h), the splat's normal in object space: the column of the rotation matrix of the
// smallest scale's axis, with normalizer = 1.0f / (rotation.x * rotation.x + rotation.y * rotation.y +
// rotation.z * rotation.z + rotation.w * rotation.w) (this function's own order of the sum). `(scale.x <= scale.y) &&
// (scale.x <= scale.z)`, then `(scale.y < scale.x) && (scale.y <= scale.z)`, otherwise z (a NaN scale ends there).
fn gsplat_normal(object:u32,prim:u32,kind:u32,time:f32)->vec3<f32>{
    let params=gs_params(object,prim,kind,time);let o=pt_order();let scale=params.scale;let s=gs_terms(params.rotation,o);
    let normalizer=rounded_divide(1.0,ptb(ptb(ptb(s.bb+s.cc,o)+s.dd,o)+s.aa,o));
    var n=vec3<f32>(ptb(2.0*ptb(s.ac+s.bd,o),o),ptb(2.0*ptb(s.cd-s.ab,o),o),ptb(ptb(ptb(s.aa-s.bb,o)-s.cc,o)+s.dd,o));
    if(scale.x<=scale.y&&scale.x<=scale.z){n=vec3<f32>(ptb(ptb(ptb(s.aa+s.bb,o)-s.cc,o)-s.dd,o),ptb(2.0*ptb(s.ad+s.bc,o),o),ptb(2.0*ptb(s.bd-s.ac,o),o));}
    else if(scale.y<scale.x&&scale.y<=scale.z){n=vec3<f32>(ptb(2.0*ptb(s.bc-s.ad,o),o),ptb(ptb(ptb(s.aa-s.bb,o)+s.cc,o)-s.dd,o),ptb(2.0*ptb(s.ab+s.cd,o),o));}
    return ptb3(n*normalizer,o);
}
//#endif
