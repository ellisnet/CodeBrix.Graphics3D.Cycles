// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2004 NVIDIA Corporation
// SPDX-FileCopyrightText: 2008-2014 Larry Gritz
// SPDX-FileCopyrightText: 2009-2014 Sony Pictures Imageworks Inc., et al.
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Native triangle solid-angle/area sampling and forward PDF, adapted from the
// pinned kernel/light/triangle.h and util/math_fast.h. Flat CDF selection only;
// light trees, volume sampling and nonconstant emitter evaluation remain open.
// Triangle-light-private motion arithmetic. The shared motion helpers stay
// byte-identical to the validated baseline: any change to them alters every
// renderer module, and the Intel Mesa 25.0.7 backend compiler intermittently
// crashes (brw_opt_combine_constants) compiling the transparent-shadow entry
// for each tested perturbation (native/work/b1 compile probes). These copies keep
// the native rounding needed by emitter sampling: reciprocal quaternion
// normalization, acos subtraction residual and rounded transform composition.
fn triangle_light_acos(value:f32)->f32{
    let absolute=abs(clamp(value,-1.0,1.0));let high=rounded_add(1.0,-absolute);
    let low=rounded_add(rounded_add(1.0,-high),-absolute);
    let x=vec2<f32>(high*.5,low*.5);let root=rounded_sqrt(x.x);
    if(root==0.0){return select(0.0,3.141592653589793,value<0.0);}
    let product=trig_product(root,root);
    let residual=rounded_add(rounded_add(x.x,-product.x),rounded_add(x.y,-product.y));
    let z=vec2<f32>(root,rounded_divide(residual,2.0*root));
    var polynomial=vec2<f32>(0.0017680810997262597,2.0789158589051837e-11);
    polynomial=trig_coeff(vec2<f32>(0.0018622264033183455,3.0847819958157441e-12),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0019650335889309645,2.7346319219495596e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0020776609890162945,4.3501873110696199e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0022014740388840437,-6.5173905335058624e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0023380918428301811,4.9281793923494632e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0024894487578421831,-7.9595299535412112e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0026578705292195082,1.0898778190701619e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0028461783658713102,3.5237631822865323e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0030578216537833214,-4.5252907324155878e-12),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0032970595639199018,-6.0446416969006656e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0035692052915692329,1.0225670180399882e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0038809645920991898,-3.3261520707844605e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0042409072630107403,-1.6933137704511214e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0046601435169577599,-3.004266366929409e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.005153309553861618,1.2845828654267644e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0057400376535952091,1.7246714473229119e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0064472104422748089,-1.3038516013391677e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0073125259950757027,-1.2147685721913826e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0083903353661298752,4.4348693988005561e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0097616091370582581,3.9213582034292038e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.011551801115274429,-2.1913472425527658e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.013964843936264515,-1.8626451422920631e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.017352763563394547,8.5968237736877562e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.022372158244252205,8.4665688696028418e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0303819440305233,4.139211438880519e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0446428582072258,-1.0643686557121335e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.075000002980232239,-2.9802322415450888e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.1666666716337204,-4.9670537405344106e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(1,0),trig_mul(polynomial,x));
    let result=2.0*trig_mul(z,polynomial);
    // Retain the subtraction residual before the final rounding. Rounding the
    // high subtraction first can move acos by one ULP and amplify triangle samples.
    if(value<0.0){let reduced=trig_coeff(vec2<f32>(3.1415927410125732,-8.742278000372485e-8),-result);return rounded_add(reduced.x,reduced.y);}
    return rounded_add(result.x,result.y);
}
fn triangle_light_normalize4(a:vec4<f32>)->vec4<f32>{
    let norm=rounded_sqrt(motion_dot4(a,a));
    // Native float4/scalar division multiplies by the reciprocal, including SSE.
    return a*rounded_divide(1.0,norm);
}
fn triangle_light_rotation(a:vec4<f32>,b:vec4<f32>,t:f32)->vec4<f32>{
    let costheta=motion_dot4(a,b);
    if(costheta>0.9995){return triangle_light_normalize4(motion_add4((1.0-t)*a,t*b));}
    let theta=triangle_light_acos(costheta);let perpendicular=triangle_light_normalize4(motion_add4(b,-a*costheta));
    let angle=theta*t;let sc=motion_sincos(angle);return motion_add4(a*sc.y,perpendicular*sc.x);
}
fn triangle_light_compose(q:vec4<f32>,y:vec4<f32>,z:vec4<f32>,w:vec4<f32>)->NativeTransform{
    let q0=1.4142135623730951*q.w;let q1=1.4142135623730951*q.x;
    let q2=1.4142135623730951*q.y;let q3=1.4142135623730951*q.z;
    let qda=q0*q1;let qdb=q0*q2;let qdc=q0*q3;let qaa=q1*q1;let qab=q1*q2;
    let qac=q1*q3;let qbb=q2*q2;let qbc=q2*q3;let qcc=q3*q3;
    // Preserve native multiply/add rounding when composing a sampled transform.
    // An ULP of vertex drift is amplified by grazing emitter solid angles.
    let rx=vec3<f32>(rounded_add(rounded_add(1.0,-qbb),-qcc),rounded_add(-qdc,qab),rounded_add(qdb,qac));
    let ry=vec3<f32>(rounded_add(qdc,qab),rounded_add(rounded_add(1.0,-qaa),-qcc),rounded_add(-qda,qbc));
    let rz=vec3<f32>(rounded_add(-qdb,qac),rounded_add(qda,qbc),rounded_add(rounded_add(1.0,-qaa),-qbb));
    let sx=vec3<f32>(y.w,z.z,w.y);let sy=vec3<f32>(z.x,z.w,w.z);let sz=vec3<f32>(z.y,w.x,w.w);
    return NativeTransform(vec4<f32>(rounded_dot(rx,sx),rounded_dot(rx,sy),rounded_dot(rx,sz),y.x),
                           vec4<f32>(rounded_dot(ry,sx),rounded_dot(ry,sy),rounded_dot(ry,sz),y.y),
                           vec4<f32>(rounded_dot(rz,sx),rounded_dot(rz,sy),rounded_dot(rz,sz),y.z));
}
fn triangle_light_object(object:u32,time:f32)->NativeTransform{
    let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));let at=table_at(G_objects,object,SIZE_objects);
    if((flags&SD_OBJECT_MOTION)==0u){return motion_transform_load(at+O_tfm);}
    // Removed by scene specialization when no object has transform motion.
//#if MOTION_OBJECT else atomicOr(&heap[p(17u)/4u],2u);return NativeTransform();
    let first=load_word(at+O_motion_offset);let steps=load_half(at+O_num_tfm_steps);let count=table_count(G_object_motion);
    if(first>count||steps>count-first){fail();return NativeTransform();}
    if(steps<2u||!(time>=0.0&&time<=1.0)){fail();return NativeTransform();}
    let maximum=steps-1u;let step=min(u32(time*f32(maximum)),maximum-1u);let t=time*f32(maximum)-f32(step);
    let a=table_at(G_object_motion,first+step,MOTION_SIZE);let b=table_at(G_object_motion,first+step+1u,MOTION_SIZE);
    let rotation=triangle_light_rotation(heap_vec4(a+MOTION_X),heap_vec4(b+MOTION_X),t);
    let translation=motion_add4((1.0-t)*heap_vec4(a+MOTION_Y),t*heap_vec4(b+MOTION_Y));
    let zz=motion_add4((1.0-t)*heap_vec4(a+MOTION_Z),t*heap_vec4(b+MOTION_Z));
    let ww=motion_add4((1.0-t)*heap_vec4(a+MOTION_W),t*heap_vec4(b+MOTION_W));
    return triangle_light_compose(rotation,translation,zz,ww);
//#endif
}
fn triangle_light_atan2(y:f32,x:f32)->f32{
    let a=abs(x);let b=abs(y);var k=0.0;
    if(b!=0.0){k=select(select(rounded_divide(b,a),rounded_divide(a,b),b>a),1.0,a==b);}
    let s=rounded_add(1.0,-rounded_add(1.0,-k));let t=s*s;
    var r=rounded_divide(s*rounded_add(0.43157974*t,1.0),rounded_add(rounded_add(0.05831938*t,0.76443945)*t,1.0));
    if(b>a){r=rounded_add(1.5707963267948966,-r);}if((bitcast<u32>(x)&0x80000000u)!=0u){r=rounded_add(3.141592653589793,-r);}
    return bitcast<f32>((bitcast<u32>(r)&0x7fffffffu)|(bitcast<u32>(y)&0x80000000u));
}
fn triangle_light_sincos(angle:f32)->vec2<f32>{
    let q=i32(round(angle*0.3183098861837907));let qf=f32(q);
    var x=rounded_add(qf*(-0.78515625*4.0),angle);
    x=rounded_add(qf*(-0.00024187564849853515625*4.0),x);
    x=rounded_add(qf*(-3.7747668102383613586e-8*4.0),x);
    x=rounded_add(qf*(-1.2816720341285448015e-12*4.0),x);
    x=rounded_add(1.5707963267948966,-rounded_add(1.5707963267948966,-x));let s=x*x;
    if((q&1)!=0){x= -x;}
    var su=2.6083159809786593541503e-6;
    su=rounded_add(su*s,-0.0001981069071916863322258);su=rounded_add(su*s,0.00833307858556509017944336);
    su=rounded_add(su*s,-0.166666597127914428710938);su=rounded_add(s*(su*x),x);
    var cu= -2.71811842367242206819355e-7;
    cu=rounded_add(cu*s,2.47990446951007470488548e-5);cu=rounded_add(cu*s,-0.00138888787478208541870117);
    cu=rounded_add(cu*s,0.0416666641831398010253906);cu=rounded_add(cu*s,-0.5);cu=rounded_add(cu*s,1.0);
    if((q&1)!=0){cu= -cu;}return clamp(vec2<f32>(su,cu),vec2<f32>(-1),vec2<f32>(1));
}
fn triangle_light_sin_from_cos(c:f32)->f32{return rounded_sqrt(max(rounded_add(1.0,-c*c),0.0));}
// Native SSE transform_point nests translation in the z product. Keeping it
// outside the dot changes a vertex by an ULP, amplified by grazing solid angles.
fn triangle_light_point(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    if(TRIANGLE_LIGHT_NATIVE_SSE!=0u){
        let z=rounded_add3(v.z*vec3<f32>(t.x.z,t.y.z,t.z.z),vec3<f32>(t.x.w,t.y.w,t.z.w));
        return rounded_add3(v.x*vec3<f32>(t.x.x,t.y.x,t.z.x),rounded_add3(v.y*vec3<f32>(t.x.y,t.y.y,t.z.y),z));
    }
    return vec3<f32>(motion_dot4(vec4<f32>(v,1),t.x),motion_dot4(vec4<f32>(v,1),t.y),motion_dot4(vec4<f32>(v,1),t.z));
}
// Negative time requests the center positions and center object transform used
// by the native host CDF, even when the sampled geometry itself is moving.
fn triangle_light_vertices(object:u32,prim:u32,time:f32)->TriangleVertices{
    var v:TriangleVertices;
    if(object>=table_count(G_objects)||object>=table_count(G_object_flag)||prim>=table_count(G_tri_vindex)){fail();v.error=1u;return v;}
    let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));
    if(time<0.0){let at=table_at(G_tri_vindex,prim,SIZE_tri_vindex);
        v.a=triangle_position(object,load_word(at),0u);v.b=triangle_position(object,load_word(at+4u),0u);v.c=triangle_position(object,load_word(at+8u),0u);
    }else{v=native_triangle_vertices(object,prim,select(PRIMITIVE_TRIANGLE,PRIMITIVE_MOTION_TRIANGLE,(flags&SD_OBJECT_HAS_VERTEX_MOTION)!=0u),time);}
    if((flags&SD_OBJECT_TRANSFORM_APPLIED)==0u){let t=triangle_light_object(object,select(time,0.5,time<0.0));v.a=triangle_light_point(t,v.a);v.b=triangle_light_point(t,v.b);v.c=triangle_light_point(t,v.c);}
    if(load_word(p(17u))!=0u){v.error=1u;}return v;
}
fn triangle_light_selection_area(object:u32,prim:u32,area:f32)->f32{
    let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));
    if(area!=0.0&&((flags&SD_OBJECT_HAS_VERTEX_MOTION)!=0u||(flags&SD_OBJECT_TRANSFORM_APPLIED)==0u)){
        let v=triangle_light_vertices(object,prim,-1.0);let n=rounded_cross(v.b-v.a,v.c-v.a);return 0.5*rounded_sqrt(rounded_dot(n,n));}
    return area;
}
fn triangle_light_solid_angle(a:vec3<f32>,b:vec3<f32>,c:vec3<f32>)->f32{
    let denominator=rounded_add(rounded_add(rounded_add(1.0,rounded_dot(b,c)),rounded_dot(a,c)),rounded_dot(a,b));
    return 2.0*triangle_light_atan2(abs(rounded_dot(a,rounded_cross(b,c))),denominator);
}
fn triangle_light_sample_native(object:u32,prim:u32,time:f32,P:vec3<f32>,random:vec2<f32>)->NativeLightSample{
    var ls:NativeLightSample;let v=triangle_light_vertices(object,prim,time);if(v.error!=0u){return ls;}
    let e0=v.b-v.a;let e1=v.c-v.a;let e2=v.c-v.b;
    let longest=max(rounded_dot(e0,e0),max(rounded_dot(e1,e1),rounded_dot(e2,e2)));var n=rounded_cross(e0,e1);
    if((load_word(table_at(G_object_flag,object,SIZE_object_flag))&SD_OBJECT_NEGATIVE_SCALE)!=0u){n= -n;}
    let n2=rounded_dot(n,n);if(n2==0.0){return ls;}
    let distance=rounded_divide(rounded_dot(n,v.a-P),n2);
    ls.shader=load_word(table_at(G_tri_shader,prim,SIZE_tri_shader));let flags=load_word(table_at(G_shaders,ls.shader&SHADER_MASK,SIZE_shaders)+SH_flags);
    if((flags&select(SD_MIS_FRONT,SD_MIS_BACK,distance>0.0))==0u){return ls;}
    let normal=light_normalize_len(n);let area=0.5*normal.w;
    ls.object_id=object;ls.prim=prim;ls.shader|=SHADER_USE_MIS;ls.eval_fac=1.0;
    if(longest>distance*distance){
        let A=rounded_safe_normalize(v.a-P);let B=rounded_safe_normalize(v.b-P);let C=rounded_safe_normalize(v.c-P);
        let cb=rounded_dot(A,C);let cc=rounded_dot(A,B);let solid=triangle_light_solid_angle(A,B,C);
        let ca=rounded_dot(rounded_safe_normalize(rounded_cross(A,B)),rounded_safe_normalize(rounded_cross(A,C)));
        let sa=triangle_light_sin_from_cos(ca);let alpha=triangle_light_acos(ca);
        let sc=triangle_light_sincos(rounded_add(random.x*solid,-alpha));let u=rounded_add(sc.y,-ca);let w=rounded_add(sc.x,sa*cc);
        let numerator=rounded_add(rounded_add(w*sc.y,-u*sc.x)*ca,-w);let denominator=rounded_add(w*sc.x,u*sc.y)*sa;
        var q=1.0;if(denominator!=0.0){q=rounded_divide(numerator,denominator);}
        let U=rounded_safe_normalize(rounded_add3(C,-cb*A));let Cp=rounded_safe_normalize(rounded_add3(q*A,triangle_light_sin_from_cos(q)*U));
        let dp=rounded_dot(Cp,B);let z=rounded_add(1.0,-random.y*rounded_add(1.0,-dp));
        ls.D=rounded_add3(z*B,triangle_light_sin_from_cos(z)*rounded_safe_normalize(rounded_add3(Cp,-dp*B)));
        var hit_u=0.0;var hit_v=0.0;var hit_t=0.0;
        if(!c_ray_triangle_intersect(P,ls.D,0.0,bitcast<f32>(0x7f7fffffu),v.a,v.b,v.c,&hit_u,&hit_v,&hit_t)||solid==0.0){return ls;}
        ls.t=hit_t;ls.P=rounded_add3(P,ls.D*ls.t);ls.pdf=rounded_divide(1.0,solid);
    }else{
        if(area==0.0){return ls;}var u=random.x;var w=random.y;if(w>u){u*=0.5;w-=u;}else{w*=0.5;u-=w;}
        let t=rounded_add(rounded_add(1.0,-u),-w);ls.P=rounded_add3(rounded_add3(t*v.a,u*v.b),w*v.c);
        let delta=ls.P-P;ls.t=rounded_sqrt(rounded_dot(delta,delta));ls.D=delta*rounded_divide(1.0,ls.t);let cosine=abs(rounded_dot(normal.xyz,-ls.D));
        if(cosine!=0.0){ls.pdf=rounded_divide(rounded_divide(ls.t*ls.t,cosine),area);}
    }
    ls.pdf*=triangle_light_selection_area(object,prim,area)*data_float(D_integrator_distribution_pdf_triangles);return ls;
}
fn triangle_light_forward_pdf(sd:SurfaceData)->f32{
    let v=triangle_light_vertices(sd.object_id,sd.prim,sd.time);if(v.error!=0u){return 0.0;}
    let e0=v.b-v.a;let e1=v.c-v.a;let e2=v.c-v.b;let n=rounded_cross(e0,e1);let n2=rounded_dot(n,n);if(n2==0.0){return 0.0;}
    let longest=max(rounded_dot(e0,e0),max(rounded_dot(e1,e1),rounded_dot(e2,e2)));let t=sd.ray_length;
    let distance=rounded_divide(abs(rounded_dot(n,sd.wi*t)),n2);let area=0.5*rounded_sqrt(n2);var pdf=0.0;
    if(longest>distance*distance){let P=rounded_add3(sd.P,sd.wi*t);let angle=triangle_light_solid_angle(rounded_safe_normalize(v.a-P),rounded_safe_normalize(v.b-P),rounded_safe_normalize(v.c-P));if(angle!=0.0){pdf=rounded_divide(1.0,angle);}}
    else{let cosine=abs(rounded_dot(sd.Ng,sd.wi));if(area!=0.0&&cosine!=0.0){pdf=rounded_divide(rounded_divide(t*t,cosine),area);}}
    return pdf*triangle_light_selection_area(sd.object_id,sd.prim,area)*data_float(D_integrator_distribution_pdf_triangles);
}
fn triangle_light_forward_weight(index:u32,sd:SurfaceData)->f32{
    if((state_word(S_path_flag,index)&PATH_RAY_MIS_SKIP)!=0u||(sd.shader_flag&select(SD_MIS_FRONT,SD_MIS_BACK,(sd.runtime_flag&SR_BACKFACING)!=0u))==0u){return 1.0;}
    // Removed by scene specialization when no shader is a MIS emitter and no
    // triangle is in the light distribution (pipelines.h).
//#if LIGHT_TRIANGLE else atomicOr(&heap[p(17u)/4u],2u);return 1.0;
    let nee=triangle_light_forward_pdf(sd);let bsdf=heap_float(state_address(S_path_mis_ray_pdf,index,4u));return (bsdf*bsdf)/(bsdf*bsdf+nee*nee);
//#endif
}
