// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native decomposed motion transform interpolation from reviewed util/transform.h.
struct NativeTransform {x:vec4<f32>,y:vec4<f32>,z:vec4<f32>}
fn motion_transform_load(address:u32)->NativeTransform{
    return NativeTransform(heap_vec4(address),heap_vec4(address+16u),heap_vec4(address+32u));
}
//#if MNEE_MOTION
// MNEE with motion blur: its walk (iteration counts and outcomes) follows the last bit of every
// motion transform, so the motion transforms there are the host's: transform_point and
// transform_direction as the host CPU kernel evaluates them (the native triangle arithmetic of the
// closest stage), and transform_compose with every product rounded and DPPS-ordered dot products.
// Each helper runs its sums through one rounded_add body in a loop (rounded_steps), so the walk
// entries stay inside the pipeline size guard.
// surface_transform_row (wavefront_shader_data.wgsl, which not every module includes).
fn motion_exact_row(t:vec4<f32>,v:vec3<f32>,point:bool)->f32{
    // Scalar hosts: (x*tx + y*ty) + (z*tz + w). SSE hosts: ((w + z*tz) + y*ty) + x*tx (w only for a point).
    // Both orders through one rounded_add body: three sums, operands chosen per step.
    let scalar=NATIVE_TRIANGLE_ARITHMETIC==0u;let count=rounded_steps(3u);
    let w=select(bitcast<f32>(bitcast<u32>(t.w)&0x80000000u),t.w,point);
    var s1=0.0;var s2=0.0;var sum=v.z*t.z;
    for(var k=0u;k<count;k++){
        var a=select(select(sum,sum,k==1u),t.w,k==0u);var b=select(select(v.x*t.x,v.y*t.y,k==1u),sum,k==0u);
        if(scalar){a=select(select(s1,v.z*t.z,k==1u),v.x*t.x,k==0u);b=select(select(s2,w,k==1u),v.y*t.y,k==0u);}
        let r=rounded_add(a,b);
        if(scalar){s1=select(s1,r,k==0u||k==2u);s2=select(s2,r,k==1u);}
        else{sum=select(sum,r,(k!=0u||point)&&k<3u);}
    }
    return select(sum,s1,scalar);
}
fn motion_exact_vector(t:NativeTransform,v:vec3<f32>,point:bool)->vec3<f32>{
    var out=vec3<f32>(0);let rows=rounded_steps(3u);
    for(var i=0u;i<rows;i++){
        let row=select(select(t.z,t.y,i==1u),t.x,i==0u);
        out=select(out,vec3<f32>(motion_exact_row(row,v,point)),vec3<u32>(0u,1u,2u)==vec3<u32>(i));
    }
    return out;
}
//#endif
fn motion_direction(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
//#if MNEE_MOTION else     return vec3<f32>(t.x.x,t.y.x,t.z.x)*v.x+(vec3<f32>(t.x.y,t.y.y,t.z.y)*v.y+vec3<f32>(t.x.z,t.y.z,t.z.z)*v.z);
    return motion_exact_vector(t,v,false);
//#endif
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
//#if MNEE_MOTION else     let theta=motion_acos(costheta);let perpendicular=motion_normalize4(motion_add4(b,-a*costheta));
    let theta=trig_acos(costheta);let perpendicular=motion_normalize4(motion_add4(b,-a*costheta));
//#endif
//#if MNEE_MOTION else     let angle=theta*t;let sc=motion_sincos(angle);return motion_add4(a*sc.y,perpendicular*sc.x);
    let angle=theta*t;let sc=motion_exact_sincos(angle);return motion_add4(a*sc.y,perpendicular*sc.x);
//#endif
}
//#if MNEE_MOTION
// quat_interpolate's acosf (trig_acos, as glibc's), sinf and cosf correctly rounded (glibc's sinf and cosf are not for a small share of arguments): the
// double-float series of the two-part reduced angle, each rounded once (family_sincos,
// wavefront_microfacet.wgsl; motion_sincos can be one ULP off).
fn motion_exact_sincos(angle:f32)->vec2<f32>{
    let n=i32(floor(angle*0.6366197723675814+0.5));let product=trig_product(f32(n),1.5707963705062866);
    let head=trig_two_sum(angle,-product.x);let r=trig_two_sum(head.x,rounded_add(head.y,rounded_add(-product.y,f32(n)*4.3711390001862426e-8)));
    let sc=select(trig_sincos_series(r),vec4<f32>(r,1.0,0.0),abs(r.x)<1e-6);let q=((n%4)+4)%4;var sn=sc.xy;var cs=sc.zw;
    if(q==1){sn=sc.zw;cs=-sc.xy;}else if(q==2){sn=-sc.xy;cs=-sc.zw;}else if(q==3){sn=-sc.zw;cs=sc.xy;}
    return vec2<f32>(rounded_add(sn.x,sn.y),rounded_add(cs.x,cs.y));
}
//#endif
fn motion_compose(q:vec4<f32>,y:vec4<f32>,z:vec4<f32>,w:vec4<f32>)->NativeTransform{
    let q0=1.4142135623730951*q.w;let q1=1.4142135623730951*q.x;
    let q2=1.4142135623730951*q.y;let q3=1.4142135623730951*q.z;
    let qda=q0*q1;let qdb=q0*q2;let qdc=q0*q3;let qaa=q1*q1;let qab=q1*q2;
    let qac=q1*q3;let qbb=q2*q2;let qbc=q2*q3;let qcc=q3*q3;
//#if !MNEE_MOTION
    let rx=vec3<f32>(1.0-qbb-qcc,-qdc+qab,qdb+qac);
    let ry=vec3<f32>(qdc+qab,1.0-qaa-qcc,-qda+qbc);
    let rz=vec3<f32>(-qdb+qac,qda+qbc,1.0-qaa-qbb);
//#endif
//#if MNEE_MOTION
    // Rotation entries: (1-a)-b on the diagonal, c+d elsewhere (the second sum adds +0), in that order.
    var first=array<f32,9>(1.0,-qdc,qdb,qdc,1.0,-qda,-qdb,qda,1.0);
    var second=array<f32,9>(-qbb,qab,qac,qab,-qaa,qbc,qac,qbc,-qaa);
    var third=array<f32,9>(-qcc,0.0,0.0,0.0,-qcc,0.0,0.0,0.0,-qbb);
    var r:array<f32,9>;let sums=rounded_steps(18u);
    for(var k=0u;k<sums;k++){let i=k/2u;let odd=(k&1u)!=0u;r[i]=rounded_add(select(first[i],r[i],odd),select(second[i],third[i],odd));}
    let rx=vec3<f32>(r[0],r[1],r[2]);let ry=vec3<f32>(r[3],r[4],r[5]);let rz=vec3<f32>(r[6],r[7],r[8]);
//#endif
    let sx=vec3<f32>(y.w,z.z,w.y);let sy=vec3<f32>(z.x,z.w,w.z);let sz=vec3<f32>(z.y,w.x,w.w);
//#if !MNEE_MOTION
    return NativeTransform(vec4<f32>(dot(rx,sx),dot(rx,sy),dot(rx,sz),y.x),
                           vec4<f32>(dot(ry,sx),dot(ry,sy),dot(ry,sz),y.y),
                           vec4<f32>(dot(rz,sx),dot(rz,sy),dot(rz,sz),y.z));
//#endif
//#if MNEE_MOTION
    var m:array<f32,9>;let entries=rounded_steps(9u);
    for(var k=0u;k<entries;k++){
        let row=select(select(rz,ry,k/3u==1u),rx,k<3u);let column=select(select(sz,sy,k%3u==1u),sx,k%3u==0u);
        m[k]=rounded_dot(row,column);
    }
    return NativeTransform(vec4<f32>(m[0],m[1],m[2],y.x),vec4<f32>(m[3],m[4],m[5],y.y),vec4<f32>(m[6],m[7],m[8],y.z));
//#endif
}
fn motion_interpolate(table:u32,first:u32,steps:u32,time:f32)->NativeTransform{
    if(steps<2u||!(time>=0.0&&time<=1.0)){fail();return NativeTransform();}
//#if MNEE_MOTION else     let maximum=steps-1u;let step=min(u32(time*f32(maximum)),maximum-1u);let t=time*f32(maximum)-f32(step);
    // t = time*maxstep - step with the product rounded first (a driver may contract it).
    let maximum=steps-1u;let step=min(u32(time*f32(maximum)),maximum-1u);let t=rounded_add(time*f32(maximum),-f32(step));
//#endif
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
//#if MNEE_MOTION else     return motion_direction(t,v)+vec3<f32>(t.x.w,t.y.w,t.z.w);
    return motion_exact_vector(t,v,true);
//#endif
}
fn motion_transpose(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    return rounded_add3(rounded_add3(v.x*t.x.xyz,v.y*t.y.xyz),v.z*t.z.xyz);
}
// Cycles transform_inverse_cross: the fused _mm_fmsub_ps form exists only in AVX2
// builds; the fixed-baseline oracle (SSE4.2, native/DEVICE.md) takes cross(), whose
// msub is unfused: both products rounded, then the difference.
fn motion_inverse_cross(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{
    return rounded_cross(a,b);
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
