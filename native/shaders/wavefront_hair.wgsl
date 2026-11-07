//#if SVM_BSDF_HAIR
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from Cycles: closure/bsdf_hair.h (adapted from Open Shading Language; BSD-3-Clause) and
// util/math_fast.h. See native/THIRD-PARTY-NOTICES.txt and parity/licenses/BSD-3-Clause.txt.
// Every product rounded before its sum (no contraction, as in the oracle build), madd likewise; tanf as
// a correctly rounded quotient of the double-float sine and cosine.
// a + b with both operands through the order word (push word 31, always 0, which no compiler sees through), so no product
// fuses into the sum: the pattern of wavefront_curve_math.wgsl, one instruction each instead of rounded_add's software sum.
fn hair_add(a:f32,b:f32)->f32{let o=p(31u);return bitcast<f32>(bitcast<u32>(bitcast<f32>(bitcast<u32>(a)^o)+bitcast<f32>(bitcast<u32>(b)^o))^o);}
fn hair_madd(a:f32,b:f32,c:f32)->f32{return hair_add(a*b,c);}
// fast_acosf.
fn hair_fast_acos(x:f32)->f32{
    let f=abs(x);let m=select(1.0,hair_add(1.0,-hair_add(1.0,-f)),f<1.0);
    let a=rounded_sqrt(hair_add(1.0,-m))*hair_add(1.5707963267,m*hair_add(-0.213300989,m*hair_add(0.077980478,m*-0.02164095)));
    return select(a,hair_add(3.141592653589793,-a),x<0.0);
}
// fast_atan2f(y, 1).
fn hair_fast_atan(y:f32)->f32{
    let b=abs(y);let k=select(select(select(b,rounded_divide(1.0,b),b>1.0),1.0,b==1.0),0.0,b==0.0);
    let s=hair_add(1.0,-hair_add(1.0,-k));let t=s*s;
    var r=rounded_divide(s*hair_madd(0.43157974,t,1.0),hair_madd(hair_madd(0.05831938,t,0.76443945),t,1.0));
    if(b>1.0){r=hair_add(1.5707963267948966,-r);}
    return select(r,-r,y<0.0);
}
// fast_sincosf as (sine, cosine); fast_cosf is the same cosine.
fn hair_fast_sincos(x_in:f32)->vec2<f32>{
    let q=round(x_in*0.3183098861837067);var x=x_in;
    x=hair_madd(q,-3.140625,x);x=hair_madd(q,-0.000967502593994140625,x);
    x=hair_madd(q,bitcast<f32>(0xb4222000u),x);x=hair_madd(q,bitcast<f32>(0xacb4611au),x);
    x=hair_add(1.5707963267948966,-hair_add(1.5707963267948966,-x));
    let s=x*x;let odd=(i32(q)&1)!=0;if(odd){x=-x;}
    var su=2.6083159809786593541503e-06;su=hair_madd(su,s,-0.0001981069071916863322258);su=hair_madd(su,s,0.00833307858556509017944336);
    su=hair_madd(su,s,-0.166666597127914428710938);su=hair_madd(s,su*x,x);
    var cu=-2.71811842367242206819355e-07;cu=hair_madd(cu,s,2.47990446951007470488548e-05);cu=hair_madd(cu,s,-0.00138888787478208541870117);
    cu=hair_madd(cu,s,0.0416666641831398010253906);cu=hair_madd(cu,s,-0.5);cu=hair_madd(cu,s,1.0);if(odd){cu=-cu;}
    return clamp(vec2<f32>(su,cu),vec2<f32>(-1.0),vec2<f32>(1.0));
}
// tanf (glibc's is correctly rounded): quadrant reduction with a two-component pi/2 (trig_atan2's), the double-float
// sine and cosine, their double-float quotient, rounded once. At a grazing theta_i the density divides by a cosine
// near zero, so one ulp of the angle shows in the pdf.
fn hair_tan(x:f32)->f32{
    if(abs(x)<0.000244140625){return x;}
    let n=i32(floor(x*0.6366197723675814+0.5));let product=trig_product(f32(n),1.5707963705062866);
    let head=trig_two_sum(x,-product.x);let r=trig_two_sum(head.x,hair_add(head.y,hair_add(-product.y,f32(n)*4.3711390001862426e-8)));
    let sc=trig_sincos_series(r);let odd=(n&1)!=0;
    // tan(r + n pi/2) = tan r for even n, -cot r for odd n.
    var numerator=sc.xy;var denominator=sc.zw;if(odd){numerator=-sc.zw;denominator=sc.xy;}
    let q=rounded_divide(numerator.x,denominator.x);let qd=trig_product(q,denominator.x);
    let remainder=hair_add(hair_add(hair_add(numerator.x,-qd.x),-qd.y),hair_add(numerator.y,-(q*denominator.y)));
    return hair_add(q,rounded_divide(remainder,denominator.x));
}
// roughness1 / (2 * (t * t + roughness1 * roughness1) * width * costheta_i).
fn hair_theta_pdf(t:f32,roughness1:f32,width:f32,costheta_i:f32)->f32{
    return rounded_divide(roughness1,((2.0*hair_add(t*t,roughness1*roughness1))*width)*costheta_i);
}
// The (a, b) of the longitudinal Cauchy lobe.
fn hair_lobe(theta_r:f32,offset:f32,roughness1:f32)->vec2<f32>{
    let inv=rounded_divide(1.0,roughness1);
    return vec2<f32>(hair_fast_atan(hair_add(0.5*hair_add(1.5707963267948966,theta_r),-offset)*inv),
                     hair_fast_atan(hair_add(0.5*hair_add(-1.5707963267948966,theta_r),-offset)*inv));
}
// bsdf_hair_reflection_eval / bsdf_hair_transmission_eval.
fn hair_evaluate(c:SvmClosure,wi:vec3<f32>,wo:vec3<f32>)->vec4<f32>{
    let reflection=c.kind==CLOSURE_BSDF_HAIR_REFLECTION_ID;let side=rounded_dot(c.N,wo);
    if((reflection&&side<0.0)||(!reflection&&side>=0.0)){return vec4<f32>(0);}
    let Iz=rounded_dot(c.T,wi);let locy=surface_native_normalize(rounded_add3(wi,-(c.T*Iz)));
    let theta_r=hair_add(1.5707963267948966,-hair_fast_acos(Iz));
    let wo_z=rounded_dot(c.T,wo);let wo_y=surface_native_normalize(rounded_add3(wo,-(c.T*wo_z)));
    let theta_i=hair_add(1.5707963267948966,-hair_fast_acos(wo_z));let cosphi_i=rounded_dot(wo_y,locy);
    if(hair_add(1.5707963267948966,-abs(theta_i))<0.001||(reflection&&cosphi_i<0.0)){return vec4<f32>(0);}
    let roughness2_inv=rounded_divide(1.0,c.alpha_y);var phi_pdf=0.0;
    let costheta_i=hair_fast_sincos(theta_i).y;let lobe=hair_lobe(theta_r,c.a,c.alpha_x);
    let t=hair_add(0.5*hair_add(theta_i,theta_r),-c.a);
    if(reflection){
        var phi_i=hair_fast_acos(cosphi_i)*roughness2_inv;if(!(abs(phi_i)<3.141592653589793)){phi_i=3.141592653589793;}
        phi_pdf=(hair_fast_sincos(phi_i*0.5).y*0.25)*roughness2_inv;
    }else{
        let c_TT=2.0*hair_fast_atan(rounded_divide(1.5707963267948966,c.alpha_y));
        let p=hair_add(3.141592653589793,-abs(hair_fast_acos(cosphi_i)));
        phi_pdf=rounded_divide(c.alpha_y,c_TT*hair_add(p*p,c.alpha_y*c.alpha_y));
    }
    let pdf=phi_pdf*hair_theta_pdf(t,c.alpha_x,hair_add(lobe.x,-lobe.y),costheta_i);
    return vec4<f32>(vec3<f32>(pdf),pdf);
}
// bsdf_hair_reflection_sample / bsdf_hair_transmission_sample.
fn hair_sample(c:SvmClosure,wi:vec3<f32>,rand:vec2<f32>)->SurfaceSample{
    var out:SurfaceSample;let reflection=c.kind==CLOSURE_BSDF_HAIR_REFLECTION_ID;
    let Iz=rounded_dot(c.T,wi);let locy=surface_native_normalize(rounded_add3(wi,-(c.T*Iz)));let locx=rounded_cross(locy,c.T);
    let theta_r=hair_add(1.5707963267948966,-hair_fast_acos(Iz));
    let lobe=hair_lobe(theta_r,c.a,c.alpha_x);let width=hair_add(lobe.x,-lobe.y);
    let c_TT=2.0*hair_fast_atan(rounded_divide(1.5707963267948966,c.alpha_y));
    // The two tanf calls through one body.
    var tan_t=0.0;var tan_p=0.0;let tans=rounded_steps(select(2u,1u,reflection));
    for(var k=0u;k<tans;k++){let r=hair_tan(select(c_TT*hair_add(rand.y,-0.5),hair_add(rand.x*width,lobe.y),k==0u));if(k==0u){tan_t=r;}else if(k==1u){tan_p=r;}}
    let t=c.alpha_x*tan_t;
    let theta_i=hair_add(2.0*hair_add(t,c.a),-theta_r);let theta=hair_fast_sincos(theta_i);
    var phi=0.0;var phi_pdf=0.0;
    if(reflection){
        phi=(2.0*trig_asin(clamp(hair_add(1.0,-(2.0*rand.y)),-1.0,1.0)))*c.alpha_y;
        phi_pdf=rounded_divide(hair_fast_sincos(phi*0.5).y*0.25,c.alpha_y);
    }else{
        let p=c.alpha_y*tan_p;phi=hair_add(p,3.141592653589793);
        phi_pdf=rounded_divide(c.alpha_y,c_TT*hair_add(p*p,c.alpha_y*c.alpha_y));
    }
    let sc=hair_fast_sincos(phi);
    out.wo=rounded_add3(rounded_add3((sc.y*theta.y)*locy,-((sc.x*theta.y)*locx)),theta.x*c.T);
    out.pdf=abs(phi_pdf*hair_theta_pdf(t,c.alpha_x,width,theta.y));
    if(hair_add(1.5707963267948966,-abs(theta_i))<0.001){out.pdf=0.0;}
    out.eval=vec3<f32>(out.pdf);out.label=select(LABEL_TRANSMIT,LABEL_REFLECT,reflection)|LABEL_GLOSSY;
    return out;
}
//#endif
//#if SVM_BSDF_HAIR_CHIANG
// SPDX-FileCopyrightText: 2018-2022 Blender Foundation
// Modified from Cycles: closure/bsdf_principled_hair_chiang.h (Apache-2.0). Implements the paper "A practical and
// controllable hair and fur model for production path tracing" by Chiang, Matt Jen-Yuan, et al. The arrays of the
// pinned code (Ap, Ap_energy, angles) are named fields here and its loops run through one body (rounded_steps), so
// nothing is dynamically indexed. expf and logf go through family_exp and family_log_parts, atan2f and asinf through
// the double-float adapters. sinhf: glibc's is within two ULP of the rounded value this computes.
const HAIR_PI:f32=3.141592653589793;const HAIR_2PI:f32=6.283185307179586;
// hair_add of the Hair BSDF region, for modules without it.
fn chiang_add(a:f32,b:f32)->f32{let o=p(31u);return bitcast<f32>(bitcast<u32>(bitcast<f32>(bitcast<u32>(a)^o)+bitcast<f32>(bitcast<u32>(b)^o))^o);}
fn hair_c_madd(a:f32,b:f32,c:f32)->f32{return chiang_add(a*b,c);}
fn hair_logf(x:f32)->f32{
    if(x==0.0){return bitcast<f32>(0xff800000u);}if((bitcast<u32>(x)&0x7f800000u)==0x7f800000u){return x;}
    let l=family_log_parts(x);return chiang_add(l.x,l.y);
}
// sinf and cosf as (sine, cosine): trig_sincos_wide's quadrant reduction kept as a double-float pair, so the reduced angle
// carries its residual into precise_disk_sincos_pair (trig_sincos_wide rounds it first and misses the rounded value by one
// ulp for about a tenth of the arguments in [-7.5, 7.5]).
fn hair_sincos(angle:f32)->vec2<f32>{
    let n=i32(floor(angle*0.6366197723675814+0.5));let product=trig_product(f32(n),1.5707963705062866);
    let head=trig_two_sum(angle,-product.x);let r=trig_two_sum(head.x,chiang_add(head.y,chiang_add(-product.y,f32(n)*4.3711390001862426e-8)));
    let sc=precise_disk_sincos_pair(r);let q=((n%4)+4)%4;
    if(q==1){return vec2<f32>(sc.y,-sc.x);}
    if(q==2){return vec2<f32>(-sc.x,-sc.y);}
    if(q==3){return vec2<f32>(-sc.y,sc.x);}
    return sc;
}
fn hair_cos_from_sin(s:f32)->f32{return rounded_sqrt(max(chiang_add(1.0,-(s*s)),0.0));}
fn hair_atan2(y:f32,x:f32)->f32{
    if(y==0.0&&x==0.0){return y;}
    return trig_atan2_guarded(y,x);
}
// Numerical approximation to the Bessel function of the first kind (the pinned denominators, 4^i (i!)^2, are all exact in binary32).
fn hair_bessel_I0(x_in:f32)->f32{
    let x=x_in*x_in;var value=chiang_add(1.0,0.25*x);var power=x*x;var denominator=4.0;let count=rounded_steps(8u);
    for(var k=0u;k<count;k++){
        let i=f32(k+2u);denominator*=4.0*(i*i);
        let next=chiang_add(value,rounded_divide(power,denominator));if(value==next){return value;}
        value=next;power*=x;
    }
    return value;
}
// Longitudinal scattering function Mp, with log_bessel_I0 and sinhf inline: one Bessel series, one logf body (the
// Bessel term, then log(0.5 / v)) and one expf body (the value, then sinhf's) per call.
fn hair_longitudinal(sin_i:f32,cos_i:f32,sin_o:f32,cos_o:f32,v:f32)->f32{
    let inv_v=rounded_divide(1.0,v);let cos_arg=(cos_i*cos_o)*inv_v;let sin_arg=(sin_i*sin_o)*inv_v;
    let small=v<=0.1;let large=small&&cos_arg>12.0;
    var i0=1.0;if(!large){i0=hair_bessel_I0(cos_arg);}
    var argument=-sin_arg;
    if(small){
        var log_i0=0.0;var log_half=0.0;let logs=rounded_steps(2u);
        for(var k=0u;k<logs;k++){let l=hair_logf(select(0.5*inv_v,select(i0,cos_arg,large),k==0u));if(k==0u){log_i0=l;}else if(k==1u){log_half=l;}}
        // x + 0.5 (1 / (8 x) - ln(2 pi) - log(x)) past x = 12, else log(I0(x)).
        let bessel=select(log_i0,chiang_add(cos_arg,0.5*chiang_add(chiang_add(rounded_divide(1.0,8.0*cos_arg),-1.8378770664093454),-log_i0)),large);
        argument=chiang_add(chiang_add(chiang_add(chiang_add(bessel,-sin_arg),-inv_v),0.6931),log_half);
    }
    var value=0.0;var e=0.0;let exps=rounded_steps(select(1u,2u,!small&&inv_v>=1.0));
    for(var k=0u;k<exps;k++){let r=family_exp(select(inv_v,argument,k==0u));if(k==0u){value=r;}else if(k==1u){e=r;}}
    if(small){return value;}
    // sinhf of a positive argument.
    var sinh=0.5*chiang_add(e,-rounded_divide(1.0,e));
    if(inv_v<1.0){let t=inv_v*inv_v;
        var u=1.0/39916800.0;u=hair_c_madd(u,t,1.0/362880.0);u=hair_c_madd(u,t,1.0/5040.0);u=hair_c_madd(u,t,1.0/120.0);u=hair_c_madd(u,t,1.0/6.0);
        sinh=chiang_add(inv_v,(inv_v*t)*u);}
    return rounded_divide(value*i0,(sinh*2.0)*v);
}
fn hair_delta_phi(p:f32,gamma_o:f32,gamma_t:f32)->f32{return chiang_add(chiang_add((2.0*p)*gamma_t,-(2.0*gamma_o)),p*HAIR_PI);}
// Azimuthal scattering function Np: the trimmed logistic of the wrapped angle.
fn hair_azimuthal(phi:f32,p:f32,s:f32,gamma_o:f32,gamma_t:f32,scaling:f32)->f32{
    let shifted=chiang_add(chiang_add(phi,-hair_delta_phi(p,gamma_o,gamma_t)),HAIR_PI);
    let wrapped=chiang_add(chiang_add(shifted,-(HAIR_2PI*floor(rounded_divide(shifted,HAIR_2PI)))),-HAIR_PI);
    let v=family_exp(rounded_divide(-abs(wrapped),s));let w=chiang_add(1.0,v);
    if(scaling==0.0){return 0.0;}
    return rounded_divide(rounded_divide(v,s*(w*w)),scaling);
}
// fresnel_dielectric_cos.
fn hair_fresnel(cosi:f32,eta:f32)->f32{
    let c=abs(cosi);let g2=chiang_add(chiang_add(eta*eta,-1.0),c*c);if(!(g2>0.0)){return 1.0;}
    let g=rounded_sqrt(g2);let A=rounded_divide(chiang_add(g,-c),chiang_add(g,c));
    let B=rounded_divide(chiang_add(c*chiang_add(g,c),-1.0),chiang_add(c*chiang_add(g,-c),1.0));
    return ((0.5*A)*A)*chiang_add(1.0,B*B);
}
fn hair_gray(c:vec3<f32>)->f32{return rounded_dot(c,heap_vec3(p(6u)+D_film_rgb_to_y));}
// Everything eval and sample share: the frame, the angles, hair_attenuation, hair_alpha_angles.
struct HairChiang {X:vec3<f32>,Z:vec3<f32>,sin_o:f32,cos_o:f32,phi_o:f32,gamma_o:f32,gamma_t:f32,cdf:f32,scaling:f32,f:f32,a1:vec3<f32>,a2:vec3<f32>,a3:vec3<f32>,energy:vec4<f32>,sin_tilt:vec3<f32>,cos_tilt:vec3<f32>}
fn hair_chiang_common(sd:SurfaceData,c:SvmClosure)->HairChiang{
    var h:HairChiang;h.X=surface_normalize(sd.dPdu);h.Z=surface_normalize(rounded_cross(h.X,c.N));
    h.sin_o=rounded_dot(sd.wi,h.X);h.cos_o=hair_cos_from_sin(h.sin_o);h.phi_o=hair_atan2(rounded_dot(sd.wi,h.Z),rounded_dot(sd.wi,c.N));
    let cos_t=hair_cos_from_sin(rounded_divide(h.sin_o,c.ior));
    let sin_gamma_o=c.alpha_y;let cos_gamma_o=hair_cos_from_sin(sin_gamma_o);
    let sin_gamma_t=rounded_divide(sin_gamma_o*h.cos_o,rounded_sqrt(chiang_add(c.ior*c.ior,-(h.sin_o*h.sin_o))));
    let cos_gamma_t=hair_cos_from_sin(sin_gamma_t);
    let arcs=rounded_steps(2u);
    for(var k=0u;k<arcs;k++){let a=trig_asin(clamp(select(sin_gamma_t,sin_gamma_o,k==0u),-1.0,1.0));if(k==0u){h.gamma_o=a;}else if(k==1u){h.gamma_t=a;}}
    let path=rounded_divide(2.0*cos_gamma_t,cos_t);
    // exp(-sigma * path), one body for the three lanes and logistic_cdf(-pi, s)'s expf (1/(1+exp(pi/s)), 0 past expf's overflow).
    var T=vec3<f32>(0);let lanes=rounded_steps(3u);let cdf_argument=rounded_divide(HAIR_PI,c.a);var cdf_exp=0.0;let exps=rounded_steps(4u);
    for(var k=0u;k<exps;k++){
        var x=cdf_argument;if(k<3u){x=-select(select(c.multiscatter.z,c.multiscatter.y,k==1u),c.multiscatter.x,k==0u)*path;}
        let e=family_exp(x);if(k==3u){cdf_exp=e;}
        T=select(T,vec3<f32>(e),vec3<u32>(0u,1u,2u)==vec3<u32>(k));
    }
    // hair_attenuation: R, TT, TRT, then the residual TRRT+ with its per-lane safe_divide.
    h.f=hair_fresnel(h.cos_o*cos_gamma_o,c.ior);let one_minus=chiang_add(1.0,-h.f);
    h.a1=(one_minus*one_minus)*T;let Tf=T*h.f;h.a2=h.a1*Tf;
    let d=rounded_add3(vec3<f32>(1),-Tf);var ratio=vec3<f32>(0);
    for(var k=0u;k<lanes;k++){
        let q=select(0.0,rounded_divide(select(select(Tf.z,Tf.y,k==1u),Tf.x,k==0u),select(select(d.z,d.y,k==1u),d.x,k==0u)),select(select(d.z,d.y,k==1u),d.x,k==0u)!=0.0);
        ratio=select(ratio,vec3<f32>(q),vec3<u32>(0u,1u,2u)==vec3<u32>(k));
    }
    h.a3=h.a2*ratio;
    let e=vec4<f32>(h.f,hair_gray(h.a1),hair_gray(h.a2),hair_gray(h.a3));
    let total=chiang_add(chiang_add(chiang_add(e.x,e.y),e.z),e.w);
    h.energy=e*select(0.0,rounded_divide(1.0,total),total!=0.0);
    // hair_alpha_angles: sin_tilt.xyz, cos_tilt.xyz are angles[0, 2, 4], angles[1, 3, 5].
    let s1=hair_sincos(c.b).x;let c1=hair_cos_from_sin(s1);let s2=(2.0*s1)*c1;let c2=chiang_add(c1*c1,-(s1*s1));
    let s4=(2.0*s2)*c2;let c4=chiang_add(c2*c2,-(s2*s2));
    h.sin_tilt=vec3<f32>(chiang_add(h.sin_o*c2,-(h.cos_o*s2)),chiang_add(h.sin_o*c1,h.cos_o*s1),chiang_add(h.sin_o*c4,h.cos_o*s4));
    h.cos_tilt=vec3<f32>(abs(chiang_add(h.cos_o*c2,h.sin_o*s2)),abs(chiang_add(h.cos_o*c1,-(h.sin_o*s1))),abs(chiang_add(h.cos_o*c4,-(h.sin_o*s4))));
    // logistic_cdf(x, s) - logistic_cdf(-x, s) = 1 - 2 logistic_cdf(-x, s).
    h.cdf=select(rounded_divide(1.0,chiang_add(1.0,cdf_exp)),0.0,cdf_argument>88.0);h.scaling=chiang_add(1.0,-(2.0*h.cdf));
    return h;
}
// The four lobes (R, TT, TRT, then the residual TRRT+), one body: (F, F_energy).
fn hair_chiang_lobes(c:SvmClosure,h:HairChiang,sin_i:f32,cos_i:f32,phi:f32)->vec4<f32>{
    var F=vec3<f32>(0);var energy=0.0;let count=rounded_steps(4u);
    for(var i=0u;i<count;i++){
        var sin_o=h.sin_o;var cos_o=h.cos_o;var v=4.0*c.roughness;var color=h.a3;var weight=h.energy.w;
        if(i==0u){sin_o=h.sin_tilt.x;cos_o=h.cos_tilt.x;v=c.alpha_x;color=vec3<f32>(h.f);weight=h.energy.x;}
        else if(i==1u){sin_o=h.sin_tilt.y;cos_o=h.cos_tilt.y;v=0.25*c.roughness;color=h.a1;weight=h.energy.y;}
        else if(i==2u){sin_o=h.sin_tilt.z;cos_o=h.cos_tilt.z;color=h.a2;weight=h.energy.z;}
        let Mp=hair_longitudinal(sin_i,cos_i,sin_o,cos_o,v);
        var Np=0.1591549430918953;if(i<3u){Np=hair_azimuthal(phi,f32(i),c.a,h.gamma_o,h.gamma_t,h.scaling);}
        F=rounded_add3(F,(color*Mp)*Np);energy=chiang_add(energy,(weight*Mp)*Np);
    }
    return vec4<f32>(F,energy);
}
// bsdf_hair_chiang_eval.
fn hair_chiang_evaluate(sd:SurfaceData,c:SvmClosure,wo:vec3<f32>)->vec4<f32>{
    let h=hair_chiang_common(sd,c);
    let sin_i=rounded_dot(wo,h.X);let phi_i=hair_atan2(rounded_dot(wo,h.Z),rounded_dot(wo,c.N));
    return hair_chiang_lobes(c,h,sin_i,hair_cos_from_sin(sin_i),chiang_add(phi_i,-h.phi_o));
}
// bsdf_hair_chiang_sample.
fn hair_chiang_sample(sd:SurfaceData,c:SvmClosure,rand:vec3<f32>)->SurfaceSample{
    var out:SurfaceSample;let h=hair_chiang_common(sd,c);
    var z=rand.z;var lobe=3u;var picked=h.energy.w;
    if(z<h.energy.x){lobe=0u;picked=h.energy.x;}
    else{z=chiang_add(z,-h.energy.x);
        if(z<h.energy.y){lobe=1u;picked=h.energy.y;}
        else{z=chiang_add(z,-h.energy.y);
            if(z<h.energy.z){lobe=2u;picked=h.energy.z;}else{z=chiang_add(z,-h.energy.z);}}}
    z=rounded_divide(z,picked);z=select(0.00001,z,z>0.00001);
    var v=c.roughness;if(lobe==1u){v*=0.25;}if(lobe>=2u){v*=4.0;}
    var sin_tilted=h.sin_o;var cos_tilted=h.cos_o;
    if(lobe==0u){sin_tilted=h.sin_tilt.x;cos_tilted=h.cos_tilt.x;}else if(lobe==1u){sin_tilted=h.sin_tilt.y;cos_tilted=h.cos_tilt.y;}else if(lobe==2u){sin_tilted=h.sin_tilt.z;cos_tilted=h.cos_tilt.z;}
    // sample_trimmed_logistic's quotient; one logf body for fac and the logistic, one sine and cosine body for the two angles.
    let inner=chiang_add(rand.x*h.scaling,h.cdf);var q=0.0;if(lobe<3u&&inner!=0.0){q=chiang_add(rounded_divide(1.0,inner),-1.0);}
    let finite_q=lobe<3u&&inner!=0.0&&q>0.0&&(bitcast<u32>(q)&0x7f800000u)!=0x7f800000u;
    var log_fac=0.0;var log_q=0.0;let logs=rounded_steps(select(1u,2u,finite_q));
    for(var k=0u;k<logs;k++){let l=hair_logf(select(q,chiang_add(z,chiang_add(1.0,-z)*family_exp(rounded_divide(-2.0,v))),k==0u));if(k==0u){log_fac=l;}else if(k==1u){log_q=l;}}
    let fac=chiang_add(1.0,v*log_fac);
    var phi=HAIR_2PI*rand.x;
    if(lobe<3u){
        var x=-HAIR_PI;if(inner!=0.0&&!(q>0.0)){x=HAIR_PI;}else if(finite_q){x=clamp((-c.a)*log_q,-HAIR_PI,HAIR_PI);}
        phi=chiang_add(hair_delta_phi(f32(lobe),h.gamma_o,h.gamma_t),x);
    }
    let phi_i=chiang_add(h.phi_o,phi);
    var cos_y=0.0;var sc=vec2<f32>(0);let angles=rounded_steps(2u);
    for(var k=0u;k<angles;k++){let r=hair_sincos(select(phi_i,HAIR_2PI*rand.y,k==0u));if(k==0u){cos_y=r.y;}else if(k==1u){sc=r;}}
    let sin_i=chiang_add((-fac)*sin_tilted,(hair_cos_from_sin(fac)*cos_y)*cos_tilted);
    let cos_i=hair_cos_from_sin(sin_i);
    let e=hair_chiang_lobes(c,h,sin_i,cos_i,phi);out.eval=e.xyz;out.pdf=e.w;
    // to_global(spherical_cos_to_direction(sin_i, phi_i), Y, Z, X).
    out.wo=rounded_add3(rounded_add3((cos_i*sc.y)*c.N,(cos_i*sc.x)*h.Z),sin_i*h.X);
    out.label=LABEL_GLOSSY|select(LABEL_TRANSMIT,LABEL_REFLECT,lobe==0u);
    return out;
}
// bsdf_hair_chiang_albedo.
fn hair_chiang_albedo(sd:SurfaceData,c:SvmClosure)->vec3<f32>{
    let cos_theta_o=hair_cos_from_sin(rounded_dot(sd.wi,surface_normalize(sd.dPdu)));let cos_gamma_o=hair_cos_from_sin(c.alpha_y);
    let f=hair_fresnel(cos_theta_o*cos_gamma_o,c.ior);let x=c.roughness;
    let scale=chiang_add(chiang_add(chiang_add(chiang_add(chiang_add(0.245*x,5.574)*x,-10.73)*x,2.532)*x,-0.215)*x,5.969);
    var out=vec3<f32>(0);let count=rounded_steps(3u);
    for(var k=0u;k<count;k++){
        let e=family_exp(-rounded_sqrt(select(select(c.multiscatter.z,c.multiscatter.y,k==1u),c.multiscatter.x,k==0u))*scale);
        out=select(out,vec3<f32>(chiang_add(e,f)),vec3<u32>(0u,1u,2u)==vec3<u32>(k));
    }
    return out;
}
//#endif
