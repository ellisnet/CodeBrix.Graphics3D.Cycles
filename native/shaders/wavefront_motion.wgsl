// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native decomposed motion transform interpolation from reviewed util/transform.h.
struct NativeTransform {x:vec4<f32>,y:vec4<f32>,z:vec4<f32>}
fn motion_transform_load(address:u32)->NativeTransform{
    return NativeTransform(heap_vec4(address),heap_vec4(address+16u),heap_vec4(address+32u));
}
fn motion_direction(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    return vec3<f32>(t.x.x,t.y.x,t.z.x)*v.x+(vec3<f32>(t.x.y,t.y.y,t.z.y)*v.y+vec3<f32>(t.x.z,t.y.z,t.z.z)*v.z);
}
// Original numerical adapter: paired binary32 sums match native float4 dot;
// arcsine Taylor coefficients derive from C(2n,n)/(4^n*(2n+1)). Reducing acos
// to 2*asin(sqrt((1-|x|)/2)) bounds the series argument by sqrt(1/2).
// Thirty terms leave a tail below 1e-11; two-component products retain the
// square-root residual and Horner roundoff instead of using loose GPU acos.
fn motion_dot4(a:vec4<f32>,b:vec4<f32>)->f32{
    return rounded_add(rounded_add(a.x*b.x,a.y*b.y),rounded_add(a.z*b.z,a.w*b.w));
}
fn motion_add4(a:vec4<f32>,b:vec4<f32>)->vec4<f32>{
    return vec4<f32>(rounded_add3(a.xyz,b.xyz),rounded_add(a.w,b.w));
}
fn motion_normalize4(a:vec4<f32>)->vec4<f32>{
    let norm=rounded_sqrt(motion_dot4(a,a));
    return vec4<f32>(rounded_divide(a.x,norm),rounded_divide(a.y,norm),rounded_divide(a.z,norm),rounded_divide(a.w,norm));
}
// The Horner steps run as loops over the coefficient pairs (lane L, 2026-10-22):
// the same operations in the same order as the unrolled form, so results are
// bit-identical, but each call site inlines one step instead of all of them.
// This keeps the entry points that use the precise trigonometry inside the Intel
// Mesa 25.0.7 backend's 16-bit instruction/register range (native/DEVICE.md,
// "Linux reference fixes on the updated Mesa").
fn motion_acos(value:f32)->f32{
    let absolute=abs(clamp(value,-1.0,1.0));let high=rounded_add(1.0,-absolute);
    let low=rounded_add(rounded_add(1.0,-high),-absolute);
    let x=vec2<f32>(high*.5,low*.5);let root=rounded_sqrt(x.x);
    if(root==0.0){return select(0.0,3.141592653589793,value<0.0);}
    let product=trig_product(root,root);
    let residual=rounded_add(rounded_add(x.x,-product.x),rounded_add(x.y,-product.y));
    let z=vec2<f32>(root,rounded_divide(residual,2.0*root));
    var polynomial=vec2<f32>(0.0017680810997262597,2.0789158589051837e-11);
    // 29 Horner steps as one loop over the same coefficients (same operations, same order).
    var polynomial_terms=array<vec2<f32>,29>(
        vec2<f32>(0.0018622264033183455,3.0847819958157441e-12),
        vec2<f32>(0.0019650335889309645,2.7346319219495596e-11),
        vec2<f32>(0.0020776609890162945,4.3501873110696199e-11),
        vec2<f32>(0.0022014740388840437,-6.5173905335058624e-11),
        vec2<f32>(0.0023380918428301811,4.9281793923494632e-11),
        vec2<f32>(0.0024894487578421831,-7.9595299535412112e-11),
        vec2<f32>(0.0026578705292195082,1.0898778190701619e-10),
        vec2<f32>(0.0028461783658713102,3.5237631822865323e-11),
        vec2<f32>(0.0030578216537833214,-4.5252907324155878e-12),
        vec2<f32>(0.0032970595639199018,-6.0446416969006656e-11),
        vec2<f32>(0.0035692052915692329,1.0225670180399882e-10),
        vec2<f32>(0.0038809645920991898,-3.3261520707844605e-11),
        vec2<f32>(0.0042409072630107403,-1.6933137704511214e-10),
        vec2<f32>(0.0046601435169577599,-3.004266366929409e-11),
        vec2<f32>(0.005153309553861618,1.2845828654267644e-10),
        vec2<f32>(0.0057400376535952091,1.7246714473229119e-11),
        vec2<f32>(0.0064472104422748089,-1.3038516013391677e-10),
        vec2<f32>(0.0073125259950757027,-1.2147685721913826e-10),
        vec2<f32>(0.0083903353661298752,4.4348693988005561e-10),
        vec2<f32>(0.0097616091370582581,3.9213582034292038e-10),
        vec2<f32>(0.011551801115274429,-2.1913472425527658e-10),
        vec2<f32>(0.013964843936264515,-1.8626451422920631e-10),
        vec2<f32>(0.017352763563394547,8.5968237736877562e-10),
        vec2<f32>(0.022372158244252205,8.4665688696028418e-10),
        vec2<f32>(0.0303819440305233,4.139211438880519e-10),
        vec2<f32>(0.0446428582072258,-1.0643686557121335e-09),
        vec2<f32>(0.075000002980232239,-2.9802322415450888e-09),
        vec2<f32>(0.1666666716337204,-4.9670537405344106e-09),
        vec2<f32>(1,0));
    for(var step=0u;step<29u;step++){polynomial=trig_coeff(polynomial_terms[step],trig_mul(polynomial,x));}
    let result=2.0*trig_mul(z,polynomial);
    if(value<0.0){return rounded_add(rounded_add(3.1415927410125732,-result.x),rounded_add(-8.742278000372485e-8,-result.y));}
    return rounded_add(result.x,result.y);
}
fn motion_sincos(angle:f32)->vec2<f32>{
    if(angle<=2.356194490192345){return precise_disk_sincos(angle);}
    let high=rounded_add(3.1415927410125732,-angle);let low=-8.742278000372485e-8;
    let sc=precise_disk_sincos(high);
    return vec2<f32>(rounded_add(sc.x,sc.y*low),-rounded_add(sc.y,-sc.x*low));
}
fn motion_rotation(a:vec4<f32>,b:vec4<f32>,t:f32)->vec4<f32>{
    let costheta=motion_dot4(a,b);
    if(costheta>0.9995){return motion_normalize4(motion_add4((1.0-t)*a,t*b));}
    let theta=motion_acos(costheta);let perpendicular=motion_normalize4(motion_add4(b,-a*costheta));
    let angle=theta*t;let sc=motion_sincos(angle);return motion_add4(a*sc.y,perpendicular*sc.x);
}
fn motion_compose(q:vec4<f32>,y:vec4<f32>,z:vec4<f32>,w:vec4<f32>)->NativeTransform{
    let q0=1.4142135623730951*q.w;let q1=1.4142135623730951*q.x;
    let q2=1.4142135623730951*q.y;let q3=1.4142135623730951*q.z;
    let qda=q0*q1;let qdb=q0*q2;let qdc=q0*q3;let qaa=q1*q1;let qab=q1*q2;
    let qac=q1*q3;let qbb=q2*q2;let qbc=q2*q3;let qcc=q3*q3;
    let rx=vec3<f32>(1.0-qbb-qcc,-qdc+qab,qdb+qac);
    let ry=vec3<f32>(qdc+qab,1.0-qaa-qcc,-qda+qbc);
    let rz=vec3<f32>(-qdb+qac,qda+qbc,1.0-qaa-qbb);
    let sx=vec3<f32>(y.w,z.z,w.y);let sy=vec3<f32>(z.x,z.w,w.z);let sz=vec3<f32>(z.y,w.x,w.w);
    return NativeTransform(vec4<f32>(dot(rx,sx),dot(rx,sy),dot(rx,sz),y.x),
                           vec4<f32>(dot(ry,sx),dot(ry,sy),dot(ry,sz),y.y),
                           vec4<f32>(dot(rz,sx),dot(rz,sy),dot(rz,sz),y.z));
}
fn motion_interpolate(table:u32,first:u32,steps:u32,time:f32)->NativeTransform{
    if(steps<2u||!(time>=0.0&&time<=1.0)){fail();return NativeTransform();}
    let maximum=steps-1u;let step=min(u32(time*f32(maximum)),maximum-1u);let t=time*f32(maximum)-f32(step);
    let a=table_at(table,first+step,MOTION_SIZE);let b=table_at(table,first+step+1u,MOTION_SIZE);
    let rotation=motion_rotation(heap_vec4(a+MOTION_X),heap_vec4(b+MOTION_X),t);
    let translation=motion_add4((1.0-t)*heap_vec4(a+MOTION_Y),t*heap_vec4(b+MOTION_Y));
    let z=motion_add4((1.0-t)*heap_vec4(a+MOTION_Z),t*heap_vec4(b+MOTION_Z));
    let w=motion_add4((1.0-t)*heap_vec4(a+MOTION_W),t*heap_vec4(b+MOTION_W));
    return motion_compose(rotation,translation,z,w);
}
// Native object transform/inverse evaluation. The inverse mirrors the reviewed
// Cycles determinant/adjugate implementation, including its degenerate fallback.
fn motion_point(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    return motion_direction(t,v)+vec3<f32>(t.x.w,t.y.w,t.z.w);
}
fn motion_transpose(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    return rounded_add3(rounded_add3(v.x*t.x.xyz,v.y*t.y.xyz),v.z*t.z.xyz);
}
fn motion_inverse_cross(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{
    return fma(a.yzx,b.zxy,-a.zxy*b.yzx);
}
fn motion_inverse(t:NativeTransform)->NativeTransform{
    var x=vec3<f32>(t.x.x,t.y.x,t.z.x);var y=vec3<f32>(t.x.y,t.y.y,t.z.y);var z=vec3<f32>(t.x.z,t.y.z,t.z.z);
    let w=vec3<f32>(t.x.w,t.y.w,t.z.w);var determinant=rounded_dot(x,motion_inverse_cross(y,z));
    if(determinant==0.0){
        x.x=rounded_add(x.x,1e-8);y.y=rounded_add(y.y,1e-8);z.z=rounded_add(z.z,1e-8);
        determinant=rounded_dot(x,rounded_cross(y,z));if(determinant==0.0){determinant=bitcast<f32>(0x7f7fffffu);}
    }
    let a=motion_inverse_cross(y,z);let b=motion_inverse_cross(z,x);let c=motion_inverse_cross(x,y);
    let ix=vec3<f32>(rounded_divide(a.x,determinant),rounded_divide(a.y,determinant),rounded_divide(a.z,determinant));
    let iy=vec3<f32>(rounded_divide(b.x,determinant),rounded_divide(b.y,determinant),rounded_divide(b.z,determinant));
    let iz=vec3<f32>(rounded_divide(c.x,determinant),rounded_divide(c.y,determinant),rounded_divide(c.z,determinant));
    return NativeTransform(vec4<f32>(ix,-rounded_dot(ix,w)),vec4<f32>(iy,-rounded_dot(iy,w)),vec4<f32>(iz,-rounded_dot(iz,w)));
}
fn motion_object(object:u32,time:f32)->NativeTransform{
    let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));let at=table_at(G_objects,object,SIZE_objects);
    if((flags&SD_OBJECT_MOTION)==0u){return motion_transform_load(at+O_tfm);}
    let first=load_word(at+O_motion_offset);let steps=load_half(at+O_num_tfm_steps);let count=table_count(G_object_motion);
    if(first>count||steps>count-first){fail();return NativeTransform();}
    return motion_interpolate(G_object_motion,first,steps,time);
}
