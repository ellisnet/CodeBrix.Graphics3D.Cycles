// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2023 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles svm/closure.h (the Principled Hair BSDF node), closure/bsdf_util.h and
// closure/bsdf_principled_hair_huang.h (bsdf_hair_huang_setup): the Huang model's closure on the canonical
// record. wavefront_hair_huang.wgsl states what each record field holds.
//#if SVM_BSDF_HAIR_HUANG
// logf(x) for a positive normal x, as family_log_parts, its coefficients in a select chain.
fn huang_log(x:f32)->f32{
    let bits=bitcast<u32>(x);var e=f32(i32(bits>>23u)-127);var m=bitcast<f32>((bits&0x007fffffu)|0x3f800000u);
    if(m>1.4142135){m*=0.5;e+=1.0;}
    let num=m-1.0;let den=trig_two_sum(m,1.0);let q=rounded_divide(num,den.x);let qd=trig_product(q,den.x);
    let sl=rounded_divide(rounded_add(rounded_add(rounded_add(num,-qd.x),-qd.y),-(q*den.y)),den.x);let sv=vec2<f32>(q,sl);
    let z=trig_mul(sv,sv);var polynomial=vec2<f32>(0.0476190485060215,-8.869738832295582e-10);
    let terms=rounded_steps(10u);
    for(var i=0u;i<terms;i++){
        var c=vec2<f32>(1.0,0.0);
        if(i==0u){c=vec2<f32>(0.05263157933950424,-3.9213582381236733e-10);}else if(i==1u){c=vec2<f32>(0.05882352963089943,-2.1913472425527658e-10);}
        else if(i==2u){c=vec2<f32>(0.06666667014360428,-3.47693762670076e-09);}else if(i==3u){c=vec2<f32>(0.07692307978868484,-2.8656079731348427e-09);}
        else if(i==4u){c=vec2<f32>(0.09090909361839294,-2.709302115988521e-09);}else if(i==5u){c=vec2<f32>(0.1111111119389534,-8.278422947149977e-10);}
        else if(i==6u){c=vec2<f32>(0.1428571492433548,-6.38621200366174e-09);}else if(i==7u){c=vec2<f32>(0.20000000298023224,-2.9802322831784522e-09);}
        else if(i==8u){c=vec2<f32>(0.3333333432674408,-9.934107758624577e-09);}
        polynomial=trig_coeff(c,trig_mul(polynomial,z));
    }
    let lm=trig_mul(vec2<f32>(2.0*sv.x,2.0*sv.y),polynomial);
    let le=trig_product(e,0.6931471824645996);let tail=rounded_add(le.y,e*-1.9046542121259336e-09);
    let sum=trig_two_sum(le.x,lm.x);let result=vec2<f32>(sum.x,rounded_add(rounded_add(sum.y,lm.y),tail));return rounded_add(result.x,result.y);
}
// bsdf_principled_hair_sigma_from_concentration.
fn huang_sigma_from_concentration(eumelanin:f32,pheomelanin:f32)->vec3<f32>{
    return rounded_add3(eumelanin*vec3<f32>(0.506,0.841,1.653),pheomelanin*vec3<f32>(0.343,0.733,1.924));
}
// svm/closure.h: the absorption of the node's colour parametrization. Direct colouring is
// bsdf_principled_hair_sigma_from_reflectance of the colour; melanin is the concentration term of the remapped melanin plus
// sigma_from_reflectance of the tint. The four logarithms (three colour lanes, the melanin remap) share one body.
fn huang_sigma(s:ptr<function,SvmState>,data:u32,random:f32,radial_roughness:f32)->vec3<f32>{
    let parametrization=load_word(data+N_PrincipledHairBsdfData_parametrization);
    if(parametrization==HH_PARAM_DIRECT_ABSORPTION){return svm_input3(s,data+N_PrincipledHairBsdfData_absorption_coefficient);}
    if(parametrization!=HH_PARAM_REFLECTANCE&&parametrization!=HH_PARAM_PIGMENT_CONCENTRATION){return huang_sigma_from_concentration(0.0,0.8054375);}
    let pigment=parametrization==HH_PARAM_PIGMENT_CONCENTRATION;
    var reflectance=svm_input3(s,data+N_PrincipledHairBsdfData_color);var melanin=0.0;var redness=0.0;var melanin_arg=1.0;
    if(pigment){
        melanin=svm_input(s,data+N_PrincipledHairBsdfData_melanin);redness=svm_input(s,data+N_PrincipledHairBsdfData_melanin_redness);
        let random_color=clamp(svm_input(s,data+N_PrincipledHairBsdfData_random_color),0.0,1.0);
        let factor=rounded_add(1.0,(2.0*rounded_add(random,-0.5))*random_color);melanin=melanin*factor;
        melanin_arg=max(rounded_add(1.0,-melanin),0.0001);reflectance=svm_input3(s,data+N_PrincipledHairBsdfData_tint);
    }
    var logs=vec4<f32>(0);let lanes=rounded_steps(4u);
    for(var k=0u;k<lanes;k++){
        let v=select(select(select(melanin_arg,reflectance.z,k==2u),reflectance.y,k==1u),reflectance.x,k==0u);
        // logf(max(color, 0)): -inf at 0.
        var l=bitcast<f32>(0xff800000u);if(v>0.0){l=huang_log(v);}
        logs=select(logs,vec4<f32>(l),vec4<u32>(0u,1u,2u,3u)==vec4<u32>(k));
    }
    var melanin_sigma=vec3<f32>(0);
    if(pigment){let m=-logs.w;melanin_sigma=huang_sigma_from_concentration(m*rounded_add(1.0,-redness),m*redness);}
    // bsdf_principled_hair_albedo_roughness_scale, then (log / scale)^2 per lane.
    let x=radial_roughness;var scale=0.245*x;let terms=rounded_steps(5u);
    for(var k=0u;k<terms;k++){
        let c=select(select(select(select(5.969,-0.215,k==3u),2.532,k==2u),-10.73,k==1u),5.574,k==0u);
        scale=rounded_add(scale*select(x,1.0,k==0u),c);
    }
    var sigma=vec3<f32>(0);let quotients=rounded_steps(3u);
    for(var k=0u;k<quotients;k++){
        let l=select(select(logs.z,logs.y,k==1u),logs.x,k==0u);var q=bitcast<f32>(0x7f800000u);if(l!=bitcast<f32>(0xff800000u)){q=rounded_divide(l,scale);}
        sigma=select(sigma,vec3<f32>(q*q),vec3<u32>(0u,1u,2u)==vec3<u32>(k));
    }
    return rounded_add3(melanin_sigma,sigma);
}
//#endif
//#if SVM_BSDF_HAIR_HUANG
// tanf (glibc's is correctly rounded): the quotient of the double-float series, rounded once.
fn huang_tan(angle:f32)->f32{
    let n=i32(floor(angle*0.6366197723675814+0.5));let product=trig_product(f32(n),1.5707963705062866);
    let head=trig_two_sum(angle,-product.x);let r=trig_two_sum(head.x,rounded_add(head.y,rounded_add(-product.y,f32(n)*4.3711390001862426e-8)));
    let sc=select(trig_sincos_series(r),vec4<f32>(r,1.0,0.0),abs(r.x)<1e-6);var num=sc.xy;var den=sc.zw;if((n&1)!=0){num=sc.zw;den=-sc.xy;}
    let q=rounded_divide(num.x,den.x);let qd=trig_product(q,den.x);
    let correction=rounded_divide(rounded_add(rounded_add(rounded_add(num.x,-qd.x),-qd.y),rounded_add(num.y,-(q*den.y))),den.x);
    return rounded_add(q,correction);
}
// CLOSURE_BSDF_HAIR_HUANG_ID: svm/closure.h, then bsdf_hair_huang_setup.
fn svm_hair_huang(s:ptr<function,SvmState>,data:u32,mix_weight:f32,flag:u32,visibility:u32){
    let alpha=svm_input(s,data+N_PrincipledHairBsdfData_offset);let ior=svm_input(s,data+N_PrincipledHairBsdfData_ior);
    let sd=(*s).geometry;let curve=(sd.primitive_type&HH_PRIMITIVE_CURVE)!=0u;var aspect=svm_input(s,data+N_PrincipledHairBsdfData_aspect_ratio);
    // The Random attribute, then (an ellipse on a curve) the normal of the major axis: one attribute read.
    var random=svm_input(s,data+N_PrincipledHairBsdfData_random);var N=vec3<f32>(0);let reads=rounded_steps(2u);
    for(var k=0u;k<reads;k++){
        if(k==1u&&!(aspect!=1.0&&curve)){break;}
        let id=load_word(data+select(N_PrincipledHairBsdfData_attr_normal,N_PrincipledHairBsdfData_attr_random,k==0u));
        let desc=attribute_find(s,id,select(0u,0xffffffffu,(id&0x80000000u)!=0u));
        if(desc.found){let value=attribute_read(s,desc).value;if(k==0u){random=value.x;}else{N=value.xyz;}}
    }
    let random_roughness=svm_input(s,data+N_PrincipledHairBsdfData_random_roughness);
    let factor=rounded_add(1.0,(2.0*rounded_add(random,-0.5))*random_roughness);
    let roughness=svm_input(s,data+N_PrincipledHairBsdfData_roughness)*factor;
    let sigma=huang_sigma(s,data,random,roughness);
    let R=svm_input(s,data+N_PrincipledHairBsdfData_R);let TT=svm_input(s,data+N_PrincipledHairBsdfData_TT);let TRT=svm_input(s,data+N_PrincipledHairBsdfData_TRT);
    if(R<=0.0&&TT<=0.0&&TRT<=0.0){return;}
    let i=svm_family_allocate(s,HH_CLOSURE_BSDF_HAIR_HUANG_ID,mix_weight);if(i==NONE){return;}
    // closure_alloc_extra: one slot from the end, or the closure goes.
    if((*s).output.remaining<1u){(*s).output.count--;(*s).output.remaining++;return;}
    (*s).output.remaining--;
    let extra_slot=(*s).output.count+(*s).output.remaining;
    var c=svm_closure_read((*s).output,i);
    c.transmission_tint=vec3<f32>(max(R,0.0),max(TT,0.0),max(TRT,0.0));
    // pixel_coverage: a camera ray's hair may cover more than one pixel (the nearfield model).
    c.alpha_y=1.0;
//#if CURVES
    if((visibility&HH_PATH_RAY_VISIBILITY_CAMERA)!=0u&&curve){
        if(sd.prim>=table_count(G_curves)){svm_error(s,64u);return;}
        let k0=load_word(table_at(G_curves,sd.prim,16u)+4u)+(sd.primitive_type>>PRIMITIVE_NUM_BITS);
        let position_offset=bitcast<i32>(load_word(table_at(G_objects,sd.object_id,SIZE_objects)+O_position_offset));
        let w0=curve_key(position_offset,k0).w;let w1=curve_key(position_offset,k0+1u).w;
        c.alpha_y=rounded_divide(0.5*sd.dP,rounded_add(w0,sd.u*rounded_add(w1,-w0)));
    }
//#endif
    // bsdf_hair_huang_setup.
    c.roughness=clamp(roughness,0.001,1.0);c.a=-alpha;c.ior=ior;c.multiscatter=sigma;
    // The frame's safe_normalize(cross(a, b)) calls through one body: Y, X, then for an ellipse on a curve N orthogonal to
    // dPdu and, above an aspect ratio of 1, the swapped axes; then Z (or make_orthonormals of Y for a degenerate N).
    let ellipse=aspect!=1.0&&curve;var Y=vec3<f32>(0);var X=vec3<f32>(0);var T=vec3<f32>(0);var Z=vec3<f32>(0);
    let swap=ellipse&&aspect>1.0;if(swap){aspect=rounded_divide(1.0,aspect);}var degenerate=false;
    let steps=rounded_steps(7u);
    for(var k=0u;k<steps;k++){
        if((k==2u||k==3u)&&!ellipse){continue;}
        if((k==4u||k==5u)&&!swap){continue;}
        if(k==6u){if(!ellipse){N=X;}if(all(N==vec3<f32>(0))||any((bitcast<vec3<u32>>(N)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u))){degenerate=true;break;}}
        var a=sd.dPdu;var b=sd.wi;
        if(k==2u||k==6u){a=N;b=sd.dPdu;}else if(k==3u){b=T;}else if(k==4u){b=N;}else if(k==5u){a=T;b=sd.dPdu;}
        let v=rounded_safe_normalize(select(rounded_cross(a,b),a,k==0u));
        if(k==0u){Y=v;}else if(k==1u){X=v;}else if(k==2u||k==4u){T=v;}else if(k==3u||k==5u){N=v;}else if(k==6u){Z=v;}
    }
    let h=select(-rounded_dot(X,sd.N),-sd.v,(sd.primitive_type&HH_PRIMITIVE_CURVE)==HH_PRIMITIVE_CURVE_RIBBON);
    if(degenerate){
        // make_orthonormals(Y, &Z, &N).
        var o=vec3<f32>(Y.z-Y.y,Y.x-Y.z,Y.y-Y.x);if(Y.x==Y.y&&Y.x==Y.z){o=vec3<f32>(Y.z-Y.y,Y.x+Y.z,-Y.y-Y.x);}
        Z=surface_native_normalize(o);N=rounded_cross(Y,Z);
    }
    let ix=rounded_dot(sd.wi,N);let iz=rounded_dot(sd.wi,Z);
    let e2=rounded_add(1.0,-(aspect*aspect));var radius=1.0;
    if(e2!=0.0){let xx=ix*ix;radius=rounded_sqrt(rounded_add(1.0,-rounded_divide(e2*xx,rounded_add(xx,iz*iz))));}
    // Outside the projected radius the hair is transparent.
    if(abs(h)>=radius){
        (*s).output.count--;(*s).output.remaining+=2u;svm_transparent(s,c.weight,flag);return;
    }
    c.b=aspect;c.N=N;c.T=Y;c.reflection_tint=Z;c.alpha_x=h;c.energy_scale=radius;c.fresnel_f0=f32(extra_slot);
    (*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_EVAL|SR_BSDF_HAS_TRANSMISSION;
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);
}
//#endif
