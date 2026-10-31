// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2011-2013 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from pinned Cycles closure/bsdf_microfacet.h, sample/mapping.h and
// util/math_float3.h: GGX reflection/refraction and dielectric glass, using
// canonical arena closure records.
// Native notices and the complete BSD license are retained in
// native/THIRD-PARTY-NOTICES.txt and parity/licenses/BSD-3-Clause.txt.
// Original CodeBrix arithmetic adapters. Native normalization rounds the squared
// products and x/y sum before adding z. SSE divides each component by the length;
// scalar float3 division computes a reciprocal and then multiplies. The emitted
// constant preserves the source host's operation order when replayed on any GPU.
// A one-ulp normalization error can dominate the sharp GGX denominator. Integer
// addition and integer midpoint comparisons preserve these rounding boundaries
// without changing the distribution or roughness threshold.
fn ggx_positive_add(a:f32,b:f32)->f32{return rounded_add(a,b);}
fn ggx_unit_sqrt(value:f32)->f32{if(value>=1.0){return 0.0;}return rounded_sqrt(rounded_add(1.0,-value));}
fn ggx_normalize(v:vec3<f32>)->vec3<f32>{
    if(GGX_NATIVE_RECIPROCAL_NORMALIZE!=0u){
        return v*rounded_divide(1.0,rounded_sqrt(rounded_dot(v,v)));
    }
    return rounded_normalize(v);
}
struct MicrofacetBasis {X:vec3<f32>,Y:vec3<f32>}
fn ggx_basis(c:SvmClosure)->MicrofacetBasis{
    var b:MicrofacetBasis;
    if(c.alpha_x==c.alpha_y){
        let n=c.N;
        b.X=vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x);
        if(n.x==n.y&&n.x==n.z){b.X=vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x);}
        b.X=ggx_normalize(b.X);b.Y=rounded_cross(n,b.X);
    }else{b.Y=ggx_normalize(rounded_cross(c.N,c.T));b.X=rounded_cross(b.Y,c.N);}
    return b;
}
fn ggx_lambda(alpha2:f32,cosine:f32)->f32{return 0.5*(sqrt(1.0+alpha2*max(1.0/(cosine*cosine)-1.0,0.0))-1.0);}
fn ggx_aniso_lambda(c:SvmClosure,v:vec3<f32>)->f32{
    let x=c.alpha_x*v.x;let y=c.alpha_y*v.y;return 0.5*(sqrt(1.0+(x*x+y*y)/(v.z*v.z))-1.0);
}
fn ggx_d(alpha2:f32,cosine:f32)->f32{let z=min(cosine*cosine,1.0);let v=rounded_add(rounded_add(1.0,-z),alpha2*z);return alpha2/(PI*(v*v));}
fn ggx_aniso_d(c:SvmClosure,h:vec3<f32>)->f32{let v=h/vec3<f32>(c.alpha_x,c.alpha_y,1);let l=rounded_dot(v,v);return INV_PI/((c.alpha_x*c.alpha_y)*(l*l));}
// Pinned Cycles concentric-disk mapping, with separately rounded phase math
// and the original bounded-angle adapter from wavefront_trig.wgsl.
fn ggx_disk_sample(r:vec2<f32>)->vec2<f32>{
    let a=rounded_add(2.0*r.x,-1.0);let b=rounded_add(2.0*r.y,-1.0);
    if(a==0.0&&b==0.0){return vec2<f32>(0);}
    var radius:f32;var phi:f32;
    if(a*a>b*b){radius=a;phi=(PI*0.25)*rounded_divide(b,a);}
    else{radius=b;phi=rounded_add(PI*0.5,-(PI*0.25)*rounded_divide(a,b));}
    let sc=native_disk_sincos(phi);return vec2<f32>(radius*sc.y,radius*sc.x);
}
// Cycles sample_cos_hemisphere/make_orthonormals through the same rounding
// adapters as the GGX disk: near the disk edge 1-len_squared cancels, so
// driver-approximate trig/sqrt or contracted products change the direction.
fn surface_cos_hemisphere(n:vec3<f32>,r:vec2<f32>)->vec4<f32>{
    let d=ggx_disk_sample(r);let c=ggx_unit_sqrt(rounded_add(d.x*d.x,d.y*d.y));
    var t=vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x);if(n.x==n.y&&n.x==n.z){t=vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x);}
    t=surface_native_normalize(t);let b=rounded_cross(n,t);
    return vec4<f32>(rounded_add3(rounded_add3(d.x*t,d.y*b),c*n),c*INV_PI);
}
fn ggx_vndf(wi:vec3<f32>,ax:f32,ay:f32,random:vec2<f32>)->vec3<f32>{
    let v=ggx_normalize(vec3<f32>(ax*wi.x,ay*wi.y,wi.z));let lensq=ggx_positive_add(v.x*v.x,v.y*v.y);
    var x=vec3<f32>(1,0,0);var y=vec3<f32>(0,1,0);
    if(lensq>1e-7){x=vec3<f32>(-v.y,v.x,0)*rounded_divide(1.0,rounded_sqrt(lensq));y=rounded_cross(v,x);}
    var t=ggx_disk_sample(random);let blend=0.5*rounded_add(1.0,v.z);let edge=ggx_unit_sqrt(t.x*t.x);t.y=rounded_add(edge,blend*rounded_add(t.y,-edge));
    let h=rounded_add3(rounded_add3(t.x*x,t.y*y),ggx_unit_sqrt(ggx_positive_add(t.x*t.x,t.y*t.y))*v);
    return ggx_normalize(vec3<f32>(ax*h.x,ay*h.y,max(0.0,h.z)));
}
struct MicrofacetFresnel {reflection:vec3<f32>,transmission:vec3<f32>,cos_transmit:f32}
fn ggx_fresnel(c:SvmClosure,cosine:f32)->MicrofacetFresnel{
    var f:MicrofacetFresnel;
//#if SVM_BSDF_FRESNEL_MODELS
    if(c.roughness!=0.0){let p=principled_fresnel(c,cosine);f.reflection=p.reflection;f.transmission=p.transmission;f.cos_transmit=p.cos_transmit;return f;}
//#endif
    if(c.kind==CLOSURE_BSDF_MICROFACET_GGX_ID){f.reflection=vec3<f32>(1);return f;}
    let dielectric=ggx_dielectric(cosine,c.ior);f.cos_transmit=dielectric.y;
    if(c.kind==CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID){f.transmission=select(vec3<f32>(1),vec3<f32>(0),dielectric.x==1.0);return f;}
    let real_f0=ggx_f0(c.ior);let t=clamp(rounded_divide(rounded_add(dielectric.x,-real_f0),rounded_add(1.0,-real_f0)),0.0,1.0);
    let fresnel=rounded_add(rounded_add(1.0,-t)*c.fresnel_f0,t);
    f.reflection=fresnel*c.reflection_tint;f.transmission=rounded_add(1.0,-fresnel)*c.transmission_tint;return f;
}
fn ggx_reflection_pdf(f:MicrofacetFresnel)->f32{
    let sum=f.reflection+f.transmission;
    return ((f.reflection.x+f.reflection.y+f.reflection.z)*(1.0/3.0))/((sum.x+sum.y+sum.z)*(1.0/3.0));
}
fn ggx_evaluate(c:SvmClosure,wi:vec3<f32>,wo:vec3<f32>)->vec4<f32>{
    let ci=rounded_dot(c.N,wi);let co=rounded_dot(c.N,wo);let alpha2=c.alpha_x*c.alpha_y;let transmit=co<0.0;
    if(ci<=0.0||alpha2<=2e-10||(transmit&&c.kind==CLOSURE_BSDF_MICROFACET_GGX_ID)||(!transmit&&c.kind==CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID)){return vec4<f32>(0);}
    let unnormalized=select(wi+wo,-rounded_add3(c.ior*wo,wi),transmit);let length=rounded_sqrt(rounded_dot(unnormalized,unnormalized));
    let inverse=select(0.0,rounded_divide(1.0,length),length!=0.0);let h=unnormalized*inverse;
    let cos_hi=rounded_dot(h,wi);let f=ggx_fresnel(c,cos_hi);if(all(f.reflection==vec3<f32>(0))&&all(f.transmission==vec3<f32>(0))){return vec4<f32>(0);}
    let ch=rounded_dot(c.N,h);var d:f32;var li:f32;var lo:f32;
    if(c.alpha_x==c.alpha_y||transmit){d=ggx_d(alpha2,ch);li=ggx_lambda(alpha2,ci);lo=ggx_lambda(alpha2,co);}
    else{let b=ggx_basis(c);let hi=vec3<f32>(rounded_dot(b.X,h),rounded_dot(b.Y,h),ch);let ii=vec3<f32>(rounded_dot(b.X,wi),rounded_dot(b.Y,wi),ci);let oo=vec3<f32>(rounded_dot(b.X,wo),rounded_dot(b.Y,wo),co);
        d=ggx_aniso_d(c,hi);li=ggx_aniso_lambda(c,ii);lo=ggx_aniso_lambda(c,oo);}
    var factor=d/ci*0.25;if(transmit){let eta_inverse=c.ior*inverse;factor=d/ci*(eta_inverse*eta_inverse)*abs(cos_hi*rounded_dot(h,wo));}
    let reflection_pdf=ggx_reflection_pdf(f);let lobe_pdf=select(reflection_pdf,1.0-reflection_pdf,transmit);
    return vec4<f32>(select(f.reflection,f.transmission,transmit)*(factor/(1.0+lo+li)*c.energy_scale),factor*lobe_pdf/(1.0+li));
}
fn ggx_sample(sd:SurfaceData,c:SvmClosure,random:vec3<f32>)->SurfaceSample{
    var out:SurfaceSample;let ci=rounded_dot(c.N,sd.wi);if(ci<=0.0){return out;}
    let alpha2=c.alpha_x*c.alpha_y;var singular=alpha2<=2e-10;
    var h=c.N;var hi:vec3<f32>;var ii:vec3<f32>;
    if(!singular){let b=ggx_basis(c);ii=vec3<f32>(rounded_dot(b.X,sd.wi),rounded_dot(b.Y,sd.wi),ci);hi=ggx_vndf(ii,c.alpha_x,c.alpha_y,random.xy);h=rounded_add3(rounded_add3(hi.x*b.X,hi.y*b.Y),hi.z*c.N);}
    let ch=rounded_dot(h,sd.wi);let f=ggx_fresnel(c,ch);if(all(f.reflection==vec3<f32>(0))&&all(f.transmission==vec3<f32>(0))){return out;}
    let reflection_pdf=ggx_reflection_pdf(f);let transmit=random.z>=reflection_pdf;let inv_eta=rounded_divide(1.0,c.ior);
    out.wo=rounded_add3(2.0*ch*h,-sd.wi);if(transmit){out.wo=rounded_add3(rounded_add(inv_eta*ch,f.cos_transmit)*h,-inv_eta*sd.wi);}
    let co=rounded_dot(c.N,out.wo);if((rounded_dot(sd.Ng,out.wo)<0.0)!=transmit||(co<0.0)!=transmit){return out;}
    out.pdf=select(reflection_pdf,1.0-reflection_pdf,transmit);out.eval=select(f.reflection,f.transmission,transmit);
    singular=singular||(transmit&&abs(c.ior-1.0)<1e-4);
    if(singular){out.pdf*=1e6;out.eval*=1e6;}
    else{var d:f32;var li:f32;var lo:f32;
        if(c.alpha_x==c.alpha_y||transmit){d=ggx_d(alpha2,hi.z);li=ggx_lambda(alpha2,ci);lo=ggx_lambda(alpha2,co);}
        else{let oo=2.0*ch*hi-ii;d=ggx_aniso_d(c,hi);li=ggx_aniso_lambda(c,ii);lo=ggx_aniso_lambda(c,oo);}
        var factor=d/ci*0.25;if(transmit){let denominator=rounded_add(f.cos_transmit,ch*inv_eta);factor=d/ci*(abs(ch*f.cos_transmit)/(denominator*denominator));}
        out.pdf*=factor/(1.0+li);out.eval*=factor/(1.0+li+lo);
    }
    out.eval*=c.energy_scale;out.roughness=alpha2;out.label=select(LABEL_REFLECT,LABEL_TRANSMIT,transmit)|select(LABEL_GLOSSY,LABEL_SINGULAR,singular);return out;
}
//#if SVM_BSDF_PRINCIPLED
// bsdf_sheen_eval / bsdf_sheen_sample (LTC; T, B and the transform in the record).
fn sheen_value(c:SvmClosure,o:vec3<f32>,cosine:f32)->f32{
    let x=c.a*o.x+c.b*o.z;let y=c.a*o.y;let length2=x*x+y*y+o.z*o.z;let r=c.a/length2;return INV_PI*cosine*(r*r);
}
fn sheen_evaluate(c:SvmClosure,wo:vec3<f32>)->f32{let o=vec3<f32>(rounded_dot(wo,c.T),rounded_dot(wo,c.multiscatter),rounded_dot(wo,c.N));return sheen_value(c,o,max(o.z,0.0));}
fn sheen_sample(sd:SurfaceData,c:SvmClosure,random:vec2<f32>)->SurfaceSample{
    var out:SurfaceSample;let disk=ggx_disk_sample(random);let z=ggx_unit_sqrt(rounded_add(disk.x*disk.x,disk.y*disk.y));
    let o=surface_native_normalize(vec3<f32>(rounded_add(disk.x,-z*c.b),disk.y,z*c.a));
    out.wo=rounded_add3(rounded_add3(o.x*c.T,o.y*c.multiscatter),o.z*c.N);out.label=LABEL_REFLECT|LABEL_DIFFUSE;
    if(rounded_dot(sd.Ng,out.wo)<=0.0){return out;}
    out.pdf=sheen_value(c,o,o.z);out.eval=vec3<f32>(out.pdf);return out;
}
//#endif
//#if SVM_BSDF_FAMILIES
// expf (glibc's is within 0.502 ULP: correctly rounded but for halfway cases far
// below binary32 rounding) as k ln2 + r in double-float binary32, Taylor degree 10,
// one rounding of the double-float result before the exact 2^k scaling.
// family_exp_parts: (m, k) with expf(x) = m 2^k, so a caller can scale a product
// back once at the end: the CPU keeps subnormal intermediates that the GPU flushes.
// surface_cos_hemisphere from a precomputed sample_uniform_disk point.
fn surface_cos_hemisphere_disk(n:vec3<f32>,d:vec2<f32>)->vec4<f32>{
    let c=ggx_unit_sqrt(rounded_add(d.x*d.x,d.y*d.y));
    var t=vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x);if(n.x==n.y&&n.x==n.z){t=vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x);}
    t=surface_native_normalize(t);let b=rounded_cross(n,t);
    return vec4<f32>(rounded_add3(rounded_add3(d.x*t,d.y*b),c*n),c*INV_PI);
}
fn family_exp(x:f32)->f32{let e=family_exp_parts(vec2<f32>(x,0.0));return ldexp(e.x,i32(e.y));}
// The argument is a double-float pair (high, low).
fn family_exp_parts(x2:vec2<f32>)->vec2<f32>{
    let x=x2.x;
    if(!(x==x)){return vec2<f32>(x,0.0);}if(x>88.72284){return vec2<f32>(bitcast<f32>(0x7f800000u),0.0);}if(x< -103.97208){return vec2<f32>(0.0,0.0);}
    let k=round(x*1.4426950408889634);let p=trig_product(k,0.6931471824645996);let head=trig_two_sum(x,-p.x);
    let r=vec2<f32>(head.x,rounded_add(rounded_add(rounded_add(head.y,x2.y),-p.y),k*1.904654323148236e-09));
    var coefficients=array<vec2<f32>,10>(vec2<f32>(2.7557318844628753e-06,3.793571224297229e-14),vec2<f32>(2.4801587642286904e-05,-3.40699609366682e-13),
        vec2<f32>(0.00019841270113829523,-2.725596874933456e-12),vec2<f32>(0.0013888889225199819,-3.3631094437103215e-11),vec2<f32>(0.008333333767950535,-4.34617203337595e-10),
        vec2<f32>(0.0416666679084301,-1.2417634698280722e-09),vec2<f32>(0.1666666716337204,-4.967053879312289e-09),vec2<f32>(0.5,0.0),vec2<f32>(1.0,0.0),vec2<f32>(1.0,0.0));
    var polynomial=vec2<f32>(2.755731998149713e-07,-7.575112209051195e-15);
    for(var i=0u;i<10u;i++){polynomial=trig_coeff(coefficients[i],trig_mul(polynomial,r));}
    return vec2<f32>(rounded_add(polynomial.x,polynomial.y),k);
}
// logf of a positive normal x as a double-float pair: e ln2 + 2 atanh((m-1)/(m+1)),
// m in [sqrt(1/2), sqrt(2)), the series to s^21.
fn family_log_parts(x:f32)->vec2<f32>{
    var bits=bitcast<u32>(x);var e=f32(i32(bits>>23u)-127);var m=bitcast<f32>((bits&0x007fffffu)|0x3f800000u);
    if(m>1.4142135){m*=0.5;e+=1.0;}
    let num=m-1.0;let den=trig_two_sum(m,1.0);let q=rounded_divide(num,den.x);let qd=trig_product(q,den.x);
    let sl=rounded_divide(rounded_add(rounded_add(rounded_add(num,-qd.x),-qd.y),-(q*den.y)),den.x);let sv=vec2<f32>(q,sl);
    let z=trig_mul(sv,sv);
    var coefficients=array<vec2<f32>,10>(vec2<f32>(0.05263157933950424,-3.9213582381236733e-10),vec2<f32>(0.05882352963089943,-2.1913472425527658e-10),
        vec2<f32>(0.06666667014360428,-3.47693762670076e-09),vec2<f32>(0.07692307978868484,-2.8656079731348427e-09),vec2<f32>(0.09090909361839294,-2.709302115988521e-09),
        vec2<f32>(0.1111111119389534,-8.278422947149977e-10),vec2<f32>(0.1428571492433548,-6.38621200366174e-09),vec2<f32>(0.20000000298023224,-2.9802322831784522e-09),
        vec2<f32>(0.3333333432674408,-9.934107758624577e-09),vec2<f32>(1.0,0.0));
    var polynomial=vec2<f32>(0.0476190485060215,-8.869738832295582e-10);
    for(var i=0u;i<10u;i++){polynomial=trig_coeff(coefficients[i],trig_mul(polynomial,z));}
    let lm=trig_mul(vec2<f32>(2.0*sv.x,2.0*sv.y),polynomial);
    let le=trig_product(e,0.6931471824645996);let tail=rounded_add(le.y,e*-1.9046542121259336e-09);
    let sum=trig_two_sum(le.x,lm.x);return vec2<f32>(sum.x,rounded_add(rounded_add(sum.y,lm.y),tail));
}
// powf(x, y) for positive normal x: expf(y logf(x)) in double-float, rounded once.
fn family_pow(x:f32,y:f32)->f32{
    if(x==1.0||y==0.0){return 1.0;}
    let t=trig_mul(vec2<f32>(y,0.0),family_log_parts(x));let e=family_exp_parts(t);return ldexp(e.x,i32(e.y));
}
//#endif
//#if SVM_BSDF_BECKMANN
// Beckmann microfacet reflection, refraction and glass (pinned
// closure/bsdf_microfacet.h bsdf_microfacet_eval/_sample<BECKMANN>,
// microfacet_beckmann_sample_vndf; util/math_fast.h fast_erff, fast_ierff,
// fast_logf). In modules with Beckmann closures every microfacet record (GGX too)
// is evaluated and sampled here, so the Fresnel models inline once: the GGX
// arithmetic of ggx_evaluate / ggx_sample, with Beckmann's distribution, Smith term
// and visible-normal sampling, and no energy scale, for Beckmann records. madd is a
// rounded product before a rounded sum (no contraction), as in the oracle build.
fn surf_beckmann(kind:u32)->bool{return kind==CLOSURE_BSDF_MICROFACET_BECKMANN_ID||kind==CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID||kind==CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID;}
// The GGX kind with the same lobes (has_reflection / has_transmission), for ggx_fresnel.
fn beckmann_lobes(c:SvmClosure)->SvmClosure{
    var g=c;if(!surf_beckmann(c.kind)){return g;}
    g.kind=select(select(CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID,CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID,c.kind==CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID),
        CLOSURE_BSDF_MICROFACET_GGX_ID,c.kind==CLOSURE_BSDF_MICROFACET_BECKMANN_ID);g.energy_scale=1.0;return g;
}
fn beckmann_copysign(m:f32,x:f32)->f32{return bitcast<f32>((bitcast<u32>(m)&0x7fffffffu)|(bitcast<u32>(x)&0x80000000u));}
fn beckmann_lambda_tan(x:f32)->f32{
    if(x<0.39){return 0.0;}let a=rounded_divide(1.0,rounded_sqrt(x));
    return rounded_divide(rounded_add(rounded_add(0.396*a,-1.259)*a,1.0),rounded_add(2.181*a,3.535)*a);
}
// bsdf_lambda / bsdf_aniso_lambda<BECKMANN> of a local direction v (isotropic: v.z is cos).
fn beckmann_smith(c:SvmClosure,alpha2:f32,v:vec3<f32>,iso:bool)->f32{
    let x=c.alpha_x*v.x;let y=c.alpha_y*v.y;
    let tan2=select(rounded_divide(rounded_add(x*x,y*y),v.z*v.z),alpha2*max(rounded_add(rounded_divide(1.0,v.z*v.z),-1.0),0.0),iso);
    return beckmann_lambda_tan(tan2);
}
// The Smith terms of the incoming and outgoing local directions through one body.
fn beckmann_smith2(c:SvmClosure,alpha2:f32,ii:vec3<f32>,oo:vec3<f32>,iso:bool)->vec2<f32>{
    var l=vec2<f32>(0);let count=rounded_steps(2u);
    for(var k=0u;k<count;k++){let value=beckmann_smith(c,alpha2,select(ii,oo,k==1u),iso);l=select(l,vec2<f32>(value),vec2<u32>(0u,1u)==vec2<u32>(k));}
    return l;
}
// bsdf_D / bsdf_aniso_D<BECKMANN> of the local half vector h (isotropic: h.z is cos), one expf.
fn beckmann_distribution(c:SvmClosure,alpha2:f32,h:vec3<f32>,iso:bool)->f32{
    var x=0.0;var y=0.0;if(!iso){x=rounded_divide(h.x,c.alpha_x);y=rounded_divide(h.y,c.alpha_y);}
    let z=select(h.z*h.z,min(h.z*h.z,1.0),iso);
    let e=family_exp(rounded_divide(select(-rounded_add(x*x,y*y),rounded_add(1.0,-z),iso),select(z,z*alpha2,iso)));
    return rounded_divide(select(e,1.0,iso),select((PI*alpha2)*(z*z),((e*PI)*alpha2)*(z*z),iso));
}
fn beckmann_erf(x:f32)->f32{
    let a=abs(x);if(a>=12.3){return beckmann_copysign(1.0,x);}
    let b=rounded_add(1.0,-rounded_add(1.0,-a));
    var coefficients=array<f32,6>(0.0002765672,0.0001520143,0.0092705272,0.0422820123,0.0705230784,1.0);var r=0.0000430638;
    for(var i=0u;i<6u;i++){r=rounded_add(r*b,coefficients[i]);}
    let s=r*r;let t=s*s;let u=t*t;let v=u*u;return beckmann_copysign(rounded_add(1.0,-rounded_divide(1.0,v)),x);
}
fn beckmann_log(value:f32)->f32{
    let x=clamp(value,1.17549435e-38,3.40282347e+38);let bits=bitcast<u32>(x);let exponent=i32(bits>>23u)-127;
    let f=rounded_add(bitcast<f32>((bits&0x007fffffu)|0x3f800000u),-1.0);let f2=f*f;let f4=f2*f2;
    var coefficients=array<f32,6>(0.05206469089414,-0.13753123777116,0.24187369696082,-0.34730547155299,-0.72116591947498,1.442689881667200);
    var hi=-0.00931049621349;var lo=0.47868480909345;
    for(var i=0u;i<6u;i++){if(i<4u){hi=rounded_add(f*hi,coefficients[i]);}else{lo=rounded_add(f*lo,coefficients[i]);}}
    return rounded_add(rounded_add(f4*hi,f*lo),f32(exponent))*0.69314718056;
}
fn beckmann_ierf(x:f32)->f32{
    let a=min(abs(x),0.99999994);var w=-beckmann_log(rounded_add(1.0,-a)*rounded_add(1.0,a));
    var coefficients=array<f32,8>(3.43273939e-07,-3.5233877e-06,-4.39150654e-06,0.00021858087,-0.00125372503,-0.00417768164,0.246640727,1.50140941);var p=2.81022636e-08;
    if(w<5.0){w=rounded_add(w,-2.5);}
    else{w=rounded_add(rounded_sqrt(w),-3.0);p=-0.000200214257;
        coefficients=array<f32,8>(0.000100950558,0.00134934322,-0.00367342844,0.00573950773,-0.0076224613,0.00943887047,1.00167406,2.83297682);}
    for(var i=0u;i<8u;i++){p=rounded_add(p*w,coefficients[i]);}
    return p*x;
}
fn beckmann_signf(x:f32)->f32{return select(1.0,-1.0,x<0.0);}
// microfacet_beckmann_sample_vndf (Heitz and d'Eon; Jakob's slope sampling with the
// OSL reformulation, three Newton/bisection steps). fast_ierff runs at one call
// site: step 0 the start b, steps 1-3 the Newton iterate, the last step the y slope.
// The visible-normal sampling of both distributions (microfacet_ggx_sample_vndf and
// microfacet_beckmann_sample_vndf) with their shared first and last normalize at one
// call site each; the GGX disk sample is surf_sample's (sample_uniform_disk of the
// same rand.xy).
fn microfacet_vndf(wi:vec3<f32>,ax:f32,ay:f32,random:vec2<f32>,disk:vec2<f32>,beckmann:bool)->vec3<f32>{
    let v=ggx_normalize(vec3<f32>(ax*wi.x,ay*wi.y,wi.z));var result:vec3<f32>;
    if(!beckmann){
        let lensq=ggx_positive_add(v.x*v.x,v.y*v.y);var x=vec3<f32>(1,0,0);var y=vec3<f32>(0,1,0);
        if(lensq>1e-7){x=vec3<f32>(-v.y,v.x,0)*rounded_divide(1.0,rounded_sqrt(lensq));y=rounded_cross(v,x);}
        var t=disk;let blend=0.5*rounded_add(1.0,v.z);let edge=ggx_unit_sqrt(t.x*t.x);t.y=rounded_add(edge,blend*rounded_add(t.y,-edge));
        let h=rounded_add3(rounded_add3(t.x*x,t.y*y),ggx_unit_sqrt(ggx_positive_add(t.x*t.x,t.y*t.y))*v);
        result=vec3<f32>(ax*h.x,ay*h.y,max(0.0,h.z));
    }else{
    var slope:vec2<f32>;var cos_phi=1.0;var sin_phi=0.0;
    if(v.z>=0.99999){let radius=rounded_sqrt(-log(random.x));let sc=trig_sincos_wide(6.2831855*random.y);slope=vec2<f32>(radius*sc.y,radius*sc.x);}
    else{
        let sin_t=rounded_sqrt(max(rounded_add(1.0,-(v.z*v.z)),0.0));let tan_t=rounded_divide(sin_t,v.z);let cot_t=rounded_divide(1.0,tan_t);
        let erf_a=beckmann_erf(cot_t);let invlen=rounded_divide(1.0,sin_t);cos_phi=v.x*invlen;sin_phi=v.y*invlen;
        let k=tan_t*0.56418958354;let head=rounded_add(1.0,erf_a);
        let y_approx=random.x*rounded_add(head,k*rounded_add(1.0,-(erf_a*erf_a)));
        var b=rounded_add(y_approx,-1.0);if(k>0.0){b=rounded_divide(rounded_add(0.5,-rounded_sqrt(rounded_add(k*rounded_add(rounded_add(k,-y_approx),1.0),0.25))),k);}
        // Step 0: expf(-cot^2) and the bracket; then fast_ierff and expf(-inv_erf^2) at one site each.
        var y_exact=0.0;var begin=vec2<f32>(-1.0,0.0);var end=vec2<f32>(erf_a,0.0);
        var current=vec2<f32>(b,0.0);var inv_erf=0.0;var argument=b;var newton=true;var exponent=-(cot_t*cot_t);
        for(var step=0u;step<6u;step++){
            let e=family_exp(exponent);
            if(step==0u){y_exact=random.x*rounded_add(head,k*e);begin.y=-y_exact;end.y=rounded_add(rounded_add(head,k*e),-y_exact);continue;}
            if(step>1u){current.y=rounded_add(rounded_add(rounded_add(1.0,current.x),k*e),-y_exact);
                if(step<5u&&abs(current.y)>1e-6){
                    if(beckmann_signf(begin.y)==beckmann_signf(current.y)){begin=current;}else{end.x=current.x;}
                    let x=rounded_add(current.x,-rounded_divide(current.y,rounded_add(1.0,-(inv_erf*tan_t))));
                    current.x=select(0.5*rounded_add(begin.x,end.x),x,x>=begin.x&&x<=end.x);argument=current.x;
                }else{newton=false;argument=rounded_add(2.0*random.y,-1.0);}
            }
            let value=beckmann_ierf(argument);
            if(!newton){slope=vec2<f32>(inv_erf,value);break;}
            inv_erf=value;exponent=-(inv_erf*inv_erf);
        }
    }
    slope=vec2<f32>(rounded_add(cos_phi*slope.x,-(sin_phi*slope.y)),rounded_add(sin_phi*slope.x,cos_phi*slope.y))*vec2<f32>(ax,ay);
    result=vec3<f32>(-slope.x,-slope.y,1.0);
    }
    return ggx_normalize(result);
}
// ggx_evaluate for every microfacet record (D and Smith terms by distribution).
fn microfacet_evaluate(c:SvmClosure,wi:vec3<f32>,wo:vec3<f32>)->vec4<f32>{
    let beckmann=surf_beckmann(c.kind);let g=beckmann_lobes(c);
    let ci=rounded_dot(c.N,wi);let co=rounded_dot(c.N,wo);let alpha2=c.alpha_x*c.alpha_y;let transmit=co<0.0;
    if(ci<=0.0||alpha2<=2e-10||(transmit&&g.kind==CLOSURE_BSDF_MICROFACET_GGX_ID)||(!transmit&&g.kind==CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID)){return vec4<f32>(0);}
    let unnormalized=select(wi+wo,-rounded_add3(c.ior*wo,wi),transmit);let length=rounded_sqrt(rounded_dot(unnormalized,unnormalized));
    let inverse=select(0.0,rounded_divide(1.0,length),length!=0.0);let h=unnormalized*inverse;
    let cos_hi=rounded_dot(h,wi);let f=ggx_fresnel(g,cos_hi);if(all(f.reflection==vec3<f32>(0))&&all(f.transmission==vec3<f32>(0))){return vec4<f32>(0);}
    let ch=rounded_dot(c.N,h);var d:f32;var li:f32;var lo:f32;let iso=c.alpha_x==c.alpha_y||transmit;
    var hi=vec3<f32>(0,0,ch);var ii=vec3<f32>(0,0,ci);var oo=vec3<f32>(0,0,co);
    if(!iso){let b=ggx_basis(c);hi=vec3<f32>(rounded_dot(b.X,h),rounded_dot(b.Y,h),ch);ii=vec3<f32>(rounded_dot(b.X,wi),rounded_dot(b.Y,wi),ci);oo=vec3<f32>(rounded_dot(b.X,wo),rounded_dot(b.Y,wo),co);}
    if(beckmann){d=beckmann_distribution(c,alpha2,hi,iso);let l=beckmann_smith2(c,alpha2,ii,oo,iso);li=l.x;lo=l.y;}
    else{
        // Both Smith lambdas through one call site each (drivers inline every call).
        if(iso){d=ggx_d(alpha2,ch);}else{d=ggx_aniso_d(c,hi);}
        let directions=array<vec3<f32>,2>(ii,oo);var lambdas=vec2<f32>(0);
        for(var k=0u;k<2u;k++){if(iso){lambdas[k]=ggx_lambda(alpha2,directions[k].z);}else{lambdas[k]=ggx_aniso_lambda(c,directions[k]);}}
        li=lambdas.x;lo=lambdas.y;
    }
    var factor=d/ci*0.25;if(transmit){let eta_inverse=c.ior*inverse;factor=d/ci*(eta_inverse*eta_inverse)*abs(cos_hi*rounded_dot(h,wo));}
    let reflection_pdf=ggx_reflection_pdf(f);let lobe_pdf=select(reflection_pdf,1.0-reflection_pdf,transmit);
    return vec4<f32>(select(f.reflection,f.transmission,transmit)*(factor/(1.0+lo+li)*g.energy_scale),factor*lobe_pdf/(1.0+li));
}
// ggx_sample for every microfacet record (visible normals, D and Smith terms by distribution).
fn microfacet_sample(sd:SurfaceData,c:SvmClosure,random:vec3<f32>,disk:vec2<f32>)->SurfaceSample{
    var out:SurfaceSample;let beckmann=surf_beckmann(c.kind);let g=beckmann_lobes(c);let ci=rounded_dot(c.N,sd.wi);if(ci<=0.0){return out;}
    let alpha2=c.alpha_x*c.alpha_y;var singular=alpha2<=2e-10;
    var h=c.N;var hi:vec3<f32>;var ii:vec3<f32>;
    if(!singular){let b=ggx_basis(c);ii=vec3<f32>(rounded_dot(b.X,sd.wi),rounded_dot(b.Y,sd.wi),ci);
        hi=microfacet_vndf(ii,c.alpha_x,c.alpha_y,random.xy,disk,beckmann);
        h=rounded_add3(rounded_add3(hi.x*b.X,hi.y*b.Y),hi.z*c.N);}
    let ch=rounded_dot(h,sd.wi);let f=ggx_fresnel(g,ch);if(all(f.reflection==vec3<f32>(0))&&all(f.transmission==vec3<f32>(0))){return out;}
    let reflection_pdf=ggx_reflection_pdf(f);let transmit=random.z>=reflection_pdf;let inv_eta=rounded_divide(1.0,c.ior);
    out.wo=rounded_add3(2.0*ch*h,-sd.wi);if(transmit){out.wo=rounded_add3(rounded_add(inv_eta*ch,f.cos_transmit)*h,-inv_eta*sd.wi);}
    let co=rounded_dot(c.N,out.wo);if((rounded_dot(sd.Ng,out.wo)<0.0)!=transmit||(co<0.0)!=transmit){return out;}
    out.pdf=select(reflection_pdf,1.0-reflection_pdf,transmit);out.eval=select(f.reflection,f.transmission,transmit);
    singular=singular||(transmit&&abs(c.ior-1.0)<1e-4);
    if(singular){out.pdf*=1e6;out.eval*=1e6;}
    else{var d:f32;var li:f32;var lo:f32;let iso=c.alpha_x==c.alpha_y||transmit;
        if(beckmann){var local_i=vec3<f32>(0,0,ci);var local_o=vec3<f32>(0,0,co);if(!iso){local_i=ii;local_o=2.0*ch*hi-ii;}
            d=beckmann_distribution(c,alpha2,select(hi,vec3<f32>(0,0,hi.z),iso),iso);let l=beckmann_smith2(c,alpha2,local_i,local_o,iso);li=l.x;lo=l.y;}
        else{
            if(iso){d=ggx_d(alpha2,hi.z);}else{d=ggx_aniso_d(c,hi);}
            let directions=array<vec3<f32>,2>(select(ii,vec3<f32>(0,0,ci),iso),select(2.0*ch*hi-ii,vec3<f32>(0,0,co),iso));var lambdas=vec2<f32>(0);
            for(var k=0u;k<2u;k++){if(iso){lambdas[k]=ggx_lambda(alpha2,directions[k].z);}else{lambdas[k]=ggx_aniso_lambda(c,directions[k]);}}
            li=lambdas.x;lo=lambdas.y;
        }
        var factor=d/ci*0.25;if(transmit){let denominator=rounded_add(f.cos_transmit,ch*inv_eta);factor=d/ci*(abs(ch*f.cos_transmit)/(denominator*denominator));}
        out.pdf*=factor/(1.0+li);out.eval*=factor/(1.0+li+lo);
    }
    out.eval*=g.energy_scale;out.roughness=alpha2;out.label=select(LABEL_REFLECT,LABEL_TRANSMIT,transmit)|select(LABEL_GLOSSY,LABEL_SINGULAR,singular);return out;
}
// The microfacet branch of surf_evaluate_closure (thin-walled transmission included).
fn microfacet_evaluate_any(sd:SurfaceData,c:SvmClosure,wo:vec3<f32>)->vec4<f32>{
    var g=c;var wi=sd.wi;
//#if SVM_BSDF_THIN_WALL
    // bsdf_thin_glass_transmission_eval: GGX reflection of the mirrored incoming direction.
    if(c.kind==CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID){g.kind=CLOSURE_BSDF_MICROFACET_GGX_ID;wi=rounded_add3(sd.wi,-((2.0*c.N)*rounded_dot(sd.wi,c.N)));}
//#endif
    return microfacet_evaluate(g,wi,wo);
}
// The microfacet branch of surf_sample before its shared label and bump handling.
fn microfacet_sample_any(sd:SurfaceData,c:SvmClosure,random:vec3<f32>,disk:vec2<f32>)->SurfaceSample{
    var out:SurfaceSample;var g=c;var gs=sd;var thin=false;
//#if SVM_BSDF_THIN_WALL
    // bsdf_thin_glass_transmission_sample: singular pass-through, or GGX on the
    // mirrored direction against the flipped geometric normal.
    thin=c.kind==CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID;
    if(thin){g.kind=CLOSURE_BSDF_MICROFACET_GGX_ID;gs.Ng=-sd.Ng;gs.wi=rounded_add3(sd.wi,-((2.0*c.N)*rounded_dot(sd.wi,c.N)));}
    if(thin&&c.alpha_x*c.alpha_y<=2e-10){out.wo=-sd.wi;out.pdf=1e6;out.eval=vec3<f32>(1e6);out.label=LABEL_TRANSMIT|LABEL_SINGULAR;return out;}
//#endif
    out=microfacet_sample(gs,g,random,disk);if(thin&&out.label!=0u){out.label=LABEL_TRANSMIT|LABEL_GLOSSY;}return out;
}
//#endif
//#if SVM_BSDF_ASHIKHMIN_SHIRLEY
// Ashikhmin-Shirley glossy (pinned closure/bsdf_ashikhmin_shirley.h) on the
// microfacet record: alpha in [1e-4, 1], exponents 2/alpha^2 - 2 (up to about 2e8).
// The lobe hn^n turns one ULP of hn into a relative change of about n*6e-8, so the
// sampled half vector must match the host's to the bit: the host's dot products,
// normalize and quotients, correctly rounded tanf, atanf, sinf and cosf (family_tan, family_atan,
// family_sincos; glibc's tanf and atanf are, its sinf and cosf are not for a small share of arguments)
// and powf through family_pow.
fn as_exponent(alpha:f32)->f32{return rounded_add(rounded_divide(2.0,alpha*alpha),-2.0);}
// make_orthonormals for equal exponents, make_orthonormals_tangent otherwise.
fn as_basis(c:SvmClosure,nx:f32,ny:f32)->MicrofacetBasis{
    if(nx==ny){var b:SvmClosure;b.N=c.N;return ggx_basis(b);}
    var t:MicrofacetBasis;t.Y=ggx_normalize(rounded_cross(c.N,c.T));t.X=rounded_cross(t.Y,c.N);return t;
}
fn as_evaluate(c:SvmClosure,wi:vec3<f32>,wo:vec3<f32>)->vec4<f32>{
    // The lobe hn^n is very sensitive to hn (n up to about 2e8): the host's normalize,
    // dot products and quotients, and powf through family_pow.
    var ni=rounded_dot(c.N,wi);var no=rounded_dot(c.N,wo);
    if(max(c.alpha_x,c.alpha_y)<=1e-4||ni<0.0||no<0.0){return vec4<f32>(0);}
    ni=max(ni,1e-6);no=max(no,1e-6);let h=surface_native_normalize(wi+wo);
    let hi=max(abs(rounded_dot(h,wi)),1e-6);let hn=max(rounded_dot(h,c.N),1e-6);
    let pump=rounded_divide(1.0,max(1e-6,hi*max(ni,no)));let nx=as_exponent(c.alpha_x);let ny=as_exponent(c.alpha_y);
    // Isotropic: lobe hn^nx, norm (nx+1)/(8 pi); anisotropic: the per-direction exponent
    // (1 when hn >= 1), norm sqrt((nx+1)(ny+1))/(8 pi). One powf and one quotient.
    var exponent=nx;var numerator=rounded_add(nx,1.0);var lobe=1.0;
    if(nx!=ny){let Y=ggx_normalize(rounded_cross(c.N,c.T));let hx=rounded_dot(h,rounded_cross(Y,c.N));let hy=rounded_dot(h,Y);
        exponent=rounded_divide(rounded_add((nx*hx)*hx,(ny*hy)*hy),rounded_add(1.0,-(hn*hn)));numerator=rounded_sqrt(rounded_add(nx,1.0)*rounded_add(ny,1.0));}
    if(nx==ny||hn<1.0){lobe=family_pow(hn,exponent);}
    let norm=rounded_divide(numerator,8.0*PI);
    return vec4<f32>(vec3<f32>(((no*norm)*lobe)*pump),rounded_divide(norm*lobe,hi));
}
// tanf (glibc's is correctly rounded): the double-float sine and cosine of the
// two-part reduced angle (trig_sincos_series), their quotient rounded once.
fn family_tan(angle:f32)->f32{
    let n=i32(floor(angle*0.6366197723675814+0.5));let product=trig_product(f32(n),1.5707963705062866);
    let head=trig_two_sum(angle,-product.x);let r=trig_two_sum(head.x,rounded_add(head.y,rounded_add(-product.y,f32(n)*4.3711390001862426e-8)));
    let sc=select(trig_sincos_series(r),vec4<f32>(r,1.0,0.0),abs(r.x)<1e-6);var num=sc.xy;var den=sc.zw;if((n&1)!=0){num=sc.zw;den=-sc.xy;}
    let q=rounded_divide(num.x,den.x);let qd=trig_product(q,den.x);
    let correction=rounded_divide(rounded_add(rounded_add(rounded_add(num.x,-qd.x),-qd.y),rounded_add(num.y,-(q*den.y))),den.x);
    return rounded_add(q,correction);
}
// Correctly rounded sinf and cosf (glibc's are not for about 0.65% / 0.015% of arguments in [1e-4, 0.5]): the double-float series of the
// two-part reduced angle, each rounded once (trig_sincos_wide's rounded reduction is
// one ULP off for some angles, e.g. cos(2.5302267) on a 2e5-exponent lobe); below 1e-6
// the reduced angle's sine is itself and its cosine 1 (the series' exact products
// need larger exponents).
fn family_sincos(angle:f32)->vec2<f32>{
    let n=i32(floor(angle*0.6366197723675814+0.5));let product=trig_product(f32(n),1.5707963705062866);
    let head=trig_two_sum(angle,-product.x);let r=trig_two_sum(head.x,rounded_add(head.y,rounded_add(-product.y,f32(n)*4.3711390001862426e-8)));
    let sc=select(trig_sincos_series(r),vec4<f32>(r,1.0,0.0),abs(r.x)<1e-6);let q=((n%4)+4)%4;var sn=sc.xy;var cs=sc.zw;
    if(q==1){sn=sc.zw;cs=-sc.xy;}else if(q==2){sn=-sc.xy;cs=-sc.zw;}else if(q==3){sn=-sc.zw;cs=sc.xy;}
    return vec2<f32>(rounded_add(sn.x,sn.y),rounded_add(cs.x,cs.y));
}
// atanf through trig_atan2(x, 1): within 1e-6 of 0 or of the pole the reduced angle is
// too small for its exact products (trig_product needs exponent sums of at least 174),
// and atanf is x, or pi/2 minus the correctly rounded 1/x with two-part pi/2.
fn family_atan(x:f32)->f32{
    if(abs(x)<1e-6){return x;}
    if(abs(x)>1e6){let q=rounded_divide(1.0,x);return select(rounded_add(-1.5707963705062866,rounded_add(4.371139000186243e-8,-q)),rounded_add(1.5707963705062866,rounded_add(-4.371139000186243e-8,-q)),x>0.0);}
    return trig_atan2(x,1.0);
}
fn as_sample(sd:SurfaceData,c:SvmClosure,random:vec2<f32>)->SurfaceSample{
    var out:SurfaceSample;let ni=rounded_dot(c.N,sd.wi);if(!(ni>0.0)){return out;}
    let nx=as_exponent(c.alpha_x);let ny=as_exponent(c.alpha_y);let b=as_basis(c,nx,ny);var phi=6.2831855*random.x;
    // Anisotropic: the four quadrants map rand.x into the first one
    // (bsdf_ashikhmin_shirley_sample_first_quadrant): tanf of the quadrant angle, then
    // the sines and cosines of the first-quadrant phi and of the final phi through one
    // adapter; the one powf follows the loop (cos_theta = rand.y^(1/(n+1)), anisotropic n).
    var x=random.x;var quadrant=0u;
    if(x<0.25){x*=4.0;}else if(x<0.5){x=4.0*rounded_add(0.5,-x);quadrant=1u;}else if(x<0.75){x=4.0*rounded_add(x,-0.5);quadrant=2u;}else{x=4.0*rounded_add(1.0,-x);quadrant=3u;}
    var angle=1.5707963267948966*x;var sc=vec2<f32>(0);var stage=select(0u,2u,nx==ny);var exponent=nx;
    for(var k=0u;k<3u;k++){
        if(stage==2u){angle=phi;stage=3u;}
        if(stage==0u){angle=family_atan(rounded_sqrt(rounded_divide(rounded_add(nx,1.0),rounded_add(ny,1.0)))*family_tan(angle));stage=1u;continue;}
        sc=family_sincos(angle);
        if(stage==1u){exponent=rounded_add((nx*sc.y)*sc.y,(ny*sc.x)*sc.x);
            let base=select(select(select(0.0,PI,quadrant==1u),PI,quadrant==2u),2.0*PI,quadrant==3u);
            phi=select(rounded_add(base,select(-angle,angle,quadrant==2u)),angle,quadrant==0u);angle=phi;stage=3u;continue;}
        break;
    }
    let cos_theta=family_pow(random.y,rounded_divide(1.0,rounded_add(exponent,1.0)));
    let sin_theta=rounded_sqrt(max(rounded_add(1.0,-(cos_theta*cos_theta)),0.0));
    var h=rounded_add3(rounded_add3((sin_theta*sc.y)*b.X,(sin_theta*sc.x)*b.Y),cos_theta*c.N);
    let hi=rounded_dot(h,sd.wi);if(hi<0.0){h=-h;}
    out.wo=rounded_add3(-sd.wi,(2.0*hi)*h);out.label=LABEL_REFLECT|LABEL_GLOSSY;
    if(rounded_dot(sd.Ng,out.wo)<0.0){out.label=0u;return out;}
    if(max(c.alpha_x,c.alpha_y)<=1e-4){out.pdf=1e6;out.eval=vec3<f32>(1e6);out.label=LABEL_REFLECT|LABEL_SINGULAR;}
    else{let e=as_evaluate(c,sd.wi,out.wo);out.eval=e.xyz;out.pdf=e.w;}
    return out;
}
//#endif
//#if SVM_BSDF_VELVET
// Ashikhmin velvet (pinned closure/bsdf_ashikhmin_velvet.h): sigma in `roughness`,
// 1/max(sigma, 0.01)^2 in `a`; expf through family_exp; plain binary32 operations in
// the pinned order. At grazing half vectors expf and the products after it reach the
// subnormal range. The GPU flushes subnormals, but where depends on how the driver
// compiles the chain (a normal 1.4e-38 expf times `a` came out zero in one module), so
// the chain runs scaled by expf's power of two (exact for normal values) and applies
// flush-to-zero explicitly to every intermediate whose unscaled value is below FLT_MIN
// (velvet_ftz): the result of the pinned chain on a flush-to-zero processor. The CPU
// keeps such values subnormal; see native/DEVICE.md open item 29.
fn velvet_ftz(s:f32,k:i32)->f32{
    if(s==0.0||i32((bitcast<u32>(s)>>23u)&255u)-127+k< -126){return 0.0;}
    return s;
}
fn velvet_value(c:SvmClosure,wi:vec3<f32>,wo:vec3<f32>,ni:f32,no:f32,sample:bool)->f32{
    let h=normalize(wi+wo);let nh=dot(c.N,h);let hi=abs(dot(wi,h));
    if(!((!sample||ni>1e-5)&&abs(nh)<0.99999&&hi>1e-5)){return -1.0;}
    let ratio=max(nh/hi,1e-5);let fac1=2.0*abs(ratio*ni);let fac2=2.0*abs(ratio*no);
    let sin2=1.0-nh*nh;let sin4=sin2*sin2;let cot2=(nh*nh)/sin2;
    let e=family_exp_parts(vec2<f32>(-cot2*c.a,0.0));let k=i32(e.y);let g=min(1.0,min(fac1,fac2));
    var d=velvet_ftz(e.x,k);d=velvet_ftz(d*c.a,k);d=velvet_ftz(d*INV_PI,k);d=velvet_ftz(d/sin4,k);
    var scaled=velvet_ftz(d*g,k);scaled=velvet_ftz(0.25*scaled,k);scaled=velvet_ftz(scaled/ni,k);
    return ldexp(scaled,k);
}
fn velvet_evaluate(c:SvmClosure,wi:vec3<f32>,wo:vec3<f32>)->vec4<f32>{
    let ni=dot(c.N,wi);let no=dot(c.N,wo);if(!(ni>0.0&&no>0.0)){return vec4<f32>(0);}
    let value=velvet_value(c,wi,wo,ni,no,false);if(value<0.0){return vec4<f32>(0);}
    return vec4<f32>(vec3<f32>(value),0.5*INV_PI);
}
// sample_uniform_hemisphere through the concentric disk and make_orthonormals.
fn velvet_sample(sd:SurfaceData,c:SvmClosure,d:vec2<f32>)->SurfaceSample{
    var out:SurfaceSample;let z=1.0-(d.x*d.x+d.y*d.y);let xy=d*sqrt(max(z+1.0,0.0));
    var b:SvmClosure;b.N=c.N;let basis=ggx_basis(b);
    out.wo=xy.x*basis.X+xy.y*basis.Y+z*c.N;out.pdf=0.5*INV_PI;
    if(!(dot(sd.Ng,out.wo)>0.0)){out.pdf=0.0;return out;}
    let value=velvet_value(c,sd.wi,out.wo,dot(c.N,sd.wi),dot(c.N,out.wo),true);if(value<0.0){out.pdf=0.0;return out;}
    out.eval=vec3<f32>(value);out.label=LABEL_REFLECT|LABEL_DIFFUSE;return out;
}
//#endif
//#if SVM_BSDF_TOON
// Diffuse and glossy Toon (pinned closure/bsdf_toon.h): size and smooth already
// scaled by pi/2 in `a` and `b`. acosf/safe_acosf are glibc's correctly rounded
// acosf (trig_acos), cosf the precise adapter; plain binary32 operations otherwise.
fn toon_intensity(max_angle:f32,smoothing:f32,angle:f32)->f32{
    if(angle<max_angle){return 1.0;}
    if(angle<max_angle+smoothing&&smoothing!=0.0){return 1.0-(angle-max_angle)/smoothing;}
    return 0.0;
}
fn toon_one_minus_cos(angle:f32)->f32{if(angle>0.02){return 1.0-precise_disk_sincos(angle).y;}return 0.5*(angle*angle);}
fn toon_evaluate(c:SvmClosure,wi:vec3<f32>,wo:vec3<f32>)->vec4<f32>{
    let sample_angle=min(c.a+c.b,1.5707963267948966);var cosine:f32;
    if(c.kind==CLOSURE_BSDF_DIFFUSE_TOON_ID){cosine=dot(c.N,wo);if(!(cosine>=0.0)){return vec4<f32>(0);}}
    else{let ni=dot(c.N,wi);let no=dot(c.N,wo);if(!(ni>0.0&&no>0.0)){return vec4<f32>(0);}cosine=dot((2.0*ni)*c.N-wi,wo);}
    let angle=trig_acos(max(cosine,0.0));if(!(angle<sample_angle)){return vec4<f32>(0);}
    let pdf=0.15915494309189535/toon_one_minus_cos(sample_angle);
    return vec4<f32>(vec3<f32>(pdf*toon_intensity(c.a,c.b,angle)),pdf);
}
// sample_uniform_cone around N (diffuse) or the mirror direction (glossy).
fn toon_sample(sd:SurfaceData,c:SvmClosure,d:vec2<f32>)->SurfaceSample{
    var out:SurfaceSample;let glossy=c.kind==CLOSURE_BSDF_GLOSSY_TOON_ID;let ni=dot(c.N,sd.wi);if(glossy&&!(ni>0.0)){return out;}
    var axis=c.N;if(glossy){axis=(2.0*ni)*c.N-sd.wi;}
    let sample_angle=min(c.a+c.b,1.5707963267948966);let omc=toon_one_minus_cos(sample_angle);var cos_theta=1.0;out.pdf=1.0;out.wo=axis;
    if(omc>0.0){let r2=d.x*d.x+d.y*d.y;cos_theta=1.0-r2*omc;
        let xy=d*sqrt(max(omc*(2.0-omc*r2),0.0));out.pdf=0.15915494309189535/omc;
        var b:SvmClosure;b.N=axis;let basis=ggx_basis(b);out.wo=xy.x*basis.X+xy.y*basis.Y+cos_theta*axis;}
    if(!(dot(sd.Ng,out.wo)>0.0)||(glossy&&!(dot(c.N,out.wo)>0.0))){out.pdf=0.0;return out;}
    out.eval=vec3<f32>(out.pdf*toon_intensity(c.a,c.b,trig_acos(cos_theta)));out.label=LABEL_REFLECT|select(LABEL_DIFFUSE,LABEL_GLOSSY,glossy);return out;
}
//#endif
