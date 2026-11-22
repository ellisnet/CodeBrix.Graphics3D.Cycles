// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native spot attenuation and area rectangle/ellipse sampling and evaluation.
// Adapted from the reviewed Cycles kernel/light/{spot,area,common}.h snapshot.
const EMITTER_PI:f32=3.141592653589793;
const EMITTER_INV_PI:f32=0.3183098861837907;
// safe_normalize_len and normalize of the oracle host: per-component division
// (SSE) or reciprocal multiplication (scalar float3, ARM64), selected by the
// generated NATIVE_FLOAT3_RECIPROCAL_NORMALIZE.
fn emitter_normalize_len(v:vec3<f32>)->vec4<f32>{
    let t=rounded_sqrt(rounded_dot(v,v));if(t==0.0){return vec4<f32>(0);}
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return vec4<f32>(v*rounded_divide(1.0,t),t);}
    return vec4<f32>(rounded_divide(v.x,t),rounded_divide(v.y,t),rounded_divide(v.z,t),t);
}
fn emitter_native_normalize(v:vec3<f32>)->vec3<f32>{
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return v*rounded_divide(1.0,rounded_sqrt(rounded_dot(v,v)));}
    return rounded_normalize(v);
}
fn emitter_sphere_hit(origin:vec3<f32>,direction:vec3<f32>,position:vec3<f32>,radius:f32)->f32{
    let delta=position-origin;let d2=dot(delta,delta);let r2=radius*radius;let cosine=dot(delta,direction);
    if(d2>r2&&cosine<0.0){return -1.0;}let perpendicular=delta-cosine*direction;let s2=dot(perpendicular,perpendicular);
    if(s2>r2){return -1.0;}let root=sqrt(max(r2-s2,0.0));let t=cosine-select(-root,root,d2-r2>=0.0);
    return select(-1.0,t,t>0.0&&t<bitcast<f32>(0x7f7fffffu));
}
//#if LIGHT_SPOT
// spot_light_to_local and spot_light_attenuation (light/spot.h) in the oracle's float3 order: transform_direction(itfm, ray)
// as the host computes it (SSE: x*c0 + (y*c1 + z*c2); scalar float4 dots: (x*c0 + y*c1) + z*c2), safe_normalize (the
// reciprocal of the length), and smoothstepf, every sum and product rounded on its own. A steep blend (spot_smooth near 1e4)
// turns one last bit of the local direction into about 1e-3 of attenuation (native/DEVICE.md, item 151).
fn spot_attenuation(light:u32,direction:vec3<f32>)->f32{
    let object=table_at(G_objects,load_word(light+L_object_id),SIZE_objects);let a=object+O_itfm;
    let x=heap_vec4(a);let y=heap_vec4(a+16u);let z=heap_vec4(a+32u);
    let c0=vec3<f32>(x.x,y.x,z.x)*direction.x;let c1=vec3<f32>(x.y,y.y,z.y)*direction.y;let c2=vec3<f32>(x.z,y.z,z.z)*direction.z;
    var d=rounded_add3(rounded_add3(c0,c1),c2);if(CAMERA_NATIVE_VECTOR_DIVIDE!=0u){d=rounded_add3(c0,rounded_add3(c1,c2));}
    let transformed=rounded_safe_normalize(d);
    let f=rounded_add(-transformed.z,-heap_float(light+L_spot_cos))*heap_float(light+L_spot_smooth);
    if(f<=0.0){return 0.0;}if(f>=1.0){return 1.0;}let ff=f*f;return rounded_add(3.0*ff,-((2.0*ff)*f));
}
//#endif
//#if !LIGHT_SPOT
fn spot_attenuation(light:u32,direction:vec3<f32>)->f32{
    let object=table_at(G_objects,load_word(light+L_object_id),SIZE_objects);let a=object+O_itfm;
    let x=heap_vec4(a);let y=heap_vec4(a+16u);let z=heap_vec4(a+32u);
    let transformed=rounded_normalize(vec3<f32>(dot(x.xyz,direction),dot(y.xyz,direction),dot(z.xyz,direction)));
    let f=(-transformed.z-heap_float(light+L_spot_cos))*heap_float(light+L_spot_smooth);
    if(f<=0.0){return 0.0;}if(f>=1.0){return 1.0;}let ff=f*f;return 3.0*ff-2.0*ff*f;
}
//#endif
fn emitter_disk(random:vec2<f32>)->vec2<f32>{
    let a=rounded_add(2.0*random.x,-1.0);let b=rounded_add(2.0*random.y,-1.0);if(a==0.0&&b==0.0){return vec2<f32>(0);}
    var radius:f32;var phi:f32;if(a*a>b*b){radius=a;phi=(EMITTER_PI*0.25)*rounded_divide(b,a);}
    else{radius=b;phi=rounded_add(EMITTER_PI*0.5,-(EMITTER_PI*0.25)*rounded_divide(a,b));}
    let sc=native_disk_sincos(phi);return radius*vec2<f32>(sc.y,sc.x);
}
// Original quarter-turn rotation retains the float32 cosine of pi/2.
fn emitter_quarter_turn(p:vec3<f32>,axis:vec3<f32>)->vec3<f32>{
    let c= -4.371138828673793e-8;let s=1.0;let m=1.0-c;
    return vec3<f32>((c+m*axis.x*axis.x)*p.x+(m*axis.x*axis.y-axis.z*s)*p.y+(m*axis.x*axis.z+axis.y*s)*p.z,
      (m*axis.x*axis.y+axis.z*s)*p.x+(c+m*axis.y*axis.y)*p.y+(m*axis.y*axis.z-axis.x*s)*p.z,
      (m*axis.x*axis.z-axis.y*s)*p.x+(m*axis.y*axis.z+axis.x*s)*p.y+(c+m*axis.z*axis.z)*p.z);
}
// Original bounded arcsine adapter. The binomial Taylor series is evaluated
// with paired binary32 values after reducing its argument to [0, 1/2].
// Seventeen terms bound the truncation tail below 2e-13.
fn emitter_asin(value:f32)->f32{
    let a=abs(clamp(value,-1.0,1.0));let reduce=a>0.5;var z=vec2<f32>(a,0);var x=trig_product(a,a);
    if(reduce){x=vec2<f32>((1.0-a)*0.5,0);let root=rounded_sqrt(x.x);z=vec2<f32>(root,0);
      if(root>0.0){let product=trig_product(root,root);z.y=rounded_divide(rounded_add(rounded_add(x.x,-product.x),-product.y),2.0*root);}}
    // Horner steps in a loop (the same 16 operations in order) rather than 16
    // inlined copies; with area_rectangle's loop over its four arcsines this
    // shrinks the area-light shade_surface_light module, whose cold compile
    // crashed the Intel Mesa 25.0.7 compiler in 3 of 4 runs before and in none
    // of 7 after (brw_opt_combine_constants driver bug, phaseE3 report).
    var terms=array<vec2<f32>,16>(
    vec2<f32>(0.0046601435169577599,-3.00426636972735e-11),
    vec2<f32>(0.005153309553861618,1.284582861538591e-10),
    vec2<f32>(0.0057400376535952091,1.7246714344731083e-11),
    vec2<f32>(0.0064472104422748089,-1.30385160446167e-10),
    vec2<f32>(0.0073125259950757027,-1.2147685755854067e-10),
    vec2<f32>(0.0083903353661298752,4.4348694029308498e-10),
    vec2<f32>(0.0097616091370582581,3.9213582089072777e-10),
    vec2<f32>(0.011551801115274429,-2.1913472343893612e-10),
    vec2<f32>(0.013964843936264515,-1.8626451492309571e-10),
    vec2<f32>(0.017352763563394547,8.5968237656813406e-10),
    vec2<f32>(0.022372158244252205,8.4665688601407135e-10),
    vec2<f32>(0.0303819440305233,4.1392114427354598e-10),
    vec2<f32>(0.0446428582072258,-1.064368656703404e-09),
    vec2<f32>(0.075000002980232239,-2.9802322387695314e-09),
    vec2<f32>(0.1666666716337204,-4.9670537312825518e-09),
    vec2<f32>(1,0));
    var polynomial=vec2<f32>(0.0042409072630107403,-1.6933137720281428e-10);
    for(var i=0u;i<16u;i++){polynomial=trig_coeff(terms[i],trig_mul(polynomial,x));}
    var result=trig_mul(z,polynomial);if(reduce){result=trig_coeff(vec2<f32>(1.5707963705062866,-4.3711390001862426e-8),-2.0*result);}
    let answer=rounded_add(result.x,result.y);return select(answer,-answer,value<0.0);
}
struct AreaRegion {P:vec3<f32>,U:vec3<f32>,V:vec3<f32>,len_u:f32,len_v:f32,rectangle:bool,valid:bool}
fn area_region(light:u32,P:vec3<f32>)->AreaRegion{
    var r=AreaRegion(heap_vec3(light+L_co),heap_vec3(light+L_area_axis_u),heap_vec3(light+L_area_axis_v),heap_float(light+L_area_len_u),heap_float(light+L_area_len_v),heap_float(light+L_area_invarea)>0.0,true);
    if(heap_float(light+L_area_normalize_spread)<=0.0){return r;}
    // area_light_spread_clamp_light in the pinned association, every product rounded before its sum (rounded_dot,
    // rounded_add, rounded_add3: the oracle kernels' -ffp-contract=off order), so no driver contraction changes it.
    let N=heap_vec3(light+L_area_dir);let t=rounded_dot(N,P-r.P);let closest=rounded_add3(P,-(t*N));
    let spread=t*heap_float(light+L_area_tan_half_spread);let su=rounded_dot(r.U,closest-r.P);let sv=rounded_dot(r.V,closest-r.P);
    let round= !r.rectangle&&r.len_u==r.len_v;var sample_spread=spread==0.0;
    if(round&&!sample_spread){
        let distance=rounded_sqrt(rounded_add(su*su,sv*sv));let radius=r.len_u*0.5;
        if(distance>=radius+spread){r.valid=false;return r;}
        sample_spread=distance<=abs(radius-spread)&&spread<radius;
        if(distance>abs(radius-spread)){
            let len_u=radius+spread-distance;var len_v=2.0*min(radius,spread);
            if(abs(rounded_add(radius*radius,-(spread*spread)))<distance*distance){let x=rounded_add(distance,rounded_divide(rounded_add(spread*spread,-(radius*radius)),distance));
                len_v=rounded_sqrt(max(rounded_add((2.0*spread)*(2.0*spread),-(x*x)),0.0));}
            let rectangle_area=len_u*len_v;let circle_area=EMITTER_PI*(radius*radius);let spread_area=EMITTER_PI*(spread*spread);
            if(rectangle_area<min(circle_area,spread_area)){
                r.rectangle=true;r.U=emitter_native_normalize(r.P-closest);r.V=emitter_quarter_turn(r.U,N);r.len_u=len_u;r.len_v=len_v;
                r.P=0.5*rounded_add3(r.P+closest,r.U*(spread-radius));return r;
            }sample_spread=spread_area<circle_area;
        }
    }else if(!round&&!sample_spread){
        let min_u=max(su-spread,-r.len_u*0.5);let max_u=min(su+spread,r.len_u*0.5);
        let min_v=max(sv-spread,-r.len_v*0.5);let max_v=min(sv+spread,r.len_v*0.5);
        if(min_u>=max_u||min_v>=max_v){r.valid=false;return r;}
        let lu=max_u-min_u;let lv=max_v-min_v;let rectangle_area=lu*lv;
        let ellipse_area=select((EMITTER_PI*0.25)*r.len_u*r.len_v,bitcast<f32>(0x7f7fffffu),r.rectangle);let spread_area=EMITTER_PI*(spread*spread);
        if(r.rectangle||rectangle_area<min(ellipse_area,spread_area)){
            r.rectangle=true;r.len_u=lu;r.len_v=lv;r.P=rounded_add3(rounded_add3(r.P,r.U*(0.5*(min_u+max_u))),r.V*(0.5*(min_v+max_v)));return r;
        }r.rectangle=false;sample_spread=spread_area<ellipse_area;
    }
    if(sample_spread){r.rectangle=false;r.P=rounded_add3(rounded_add3(r.P,r.U*su),r.V*sv);r.len_u=spread*2.0;r.len_v=spread*2.0;}
    return r;
}
struct AreaRect {P:vec3<f32>,pdf:f32}
fn area_rectangle(P:vec3<f32>,r:AreaRegion,random:vec2<f32>,sample_coord:bool)->AreaRect{
    var out=AreaRect(r.P,0.0);var Z=rounded_cross(r.U,r.V);let direction=r.P-P;var z0=rounded_dot(direction,Z);if(z0>0.0){Z= -Z;z0= -z0;}
    let xc=rounded_dot(direction,r.U);let yc=rounded_dot(direction,r.V);let x0=rounded_add(xc,-0.5*r.len_u);let x1=rounded_add(xc,0.5*r.len_u);let y0=rounded_add(yc,-0.5*r.len_v);let y1=rounded_add(yc,0.5*r.len_v);
    var nz=vec4<f32>(-y0,x1,y1,-x0);for(var i=0u;i<4u;i++){nz[i]=rounded_divide(nz[i],rounded_sqrt(rounded_add(nz[i]*nz[i],z0*z0)));}
    // One emitter_asin body evaluated four times (see emitter_asin).
    var asin_inputs=vec4<f32>(-nz.x*nz.y,-nz.y*nz.z,-nz.z*nz.w,-nz.w*nz.x);var angles:vec4<f32>;
    for(var i=0u;i<4u;i++){angles[i]=emitter_asin(asin_inputs[i]);}
    let g0=angles.x;let g1=angles.y;let g2=angles.z;let g3=angles.w;
    let S= -rounded_add(rounded_add(rounded_add(g0,g1),g2),g3);
    if(sample_coord){
        let b0=nz.x;let b1=nz.z;let au=rounded_add(rounded_add(random.x*S,g2),g3);let sc=native_disk_sincos(au);var fu=0.0;if(sc.x!=0.0){fu=rounded_divide(rounded_add(sc.y*b0,b1),sc.x);}
        let inverse=rounded_divide(1.0,rounded_sqrt(rounded_add(fu*fu,b0*b0)));let cu=clamp(bitcast<f32>((bitcast<u32>(inverse)&0x7fffffffu)|(bitcast<u32>(fu)&0x80000000u)),-1.0,1.0);
        let xu=clamp(rounded_divide(-(cu*z0),max(rounded_sqrt(max(rounded_add(1.0,-cu*cu),0.0)),1e-7)),x0,x1);let d2=rounded_add(xu*xu,z0*z0);
        let h0=rounded_divide(y0,rounded_sqrt(rounded_add(d2,y0*y0)));let h1=rounded_divide(y1,rounded_sqrt(rounded_add(d2,y1*y1)));let hv=rounded_add(h0,random.y*rounded_add(h1,-h0));let hv2=hv*hv;
        var yv=y1;if(hv2<1.0-1e-6){yv=hv*rounded_sqrt(rounded_divide(d2,rounded_add(1.0,-hv2)));}out.P=rounded_add3(rounded_add3(rounded_add3(P,xu*r.U),yv*r.V),z0*Z);
    }
    if(S<1e-5||min(min(nz.x*nz.x,nz.y*nz.y),min(nz.z*nz.z,nz.w*nz.w))>0.99999){
        let t=rounded_sqrt(rounded_dot(direction,direction));let denom=z0*r.len_u*r.len_v;out.pdf=select(0.0,rounded_divide(-t*t*t,denom),denom!=0.0);
    }else{out.pdf=rounded_divide(1.0,S);}return out;
}
struct AreaEval {P:vec3<f32>,D:vec3<f32>,t:f32,pdf:f32,eval_fac:f32,valid:bool}
//#if LIGHT_AREA_OVERFLOW
// An area light with an axis whose length OVERFLOWS (a size or object scale above about 1.8e19; native/DEVICE.md, item 233).
// AreaLight::copy_to_kernel stores normalize_len of that vector: a zero axis of infinite length (the device's copy holds
// the zero axis with a length of -0, which marks it), an inverse area of 0 when the light is normalized. The pinned
// arithmetic of light/area.h then runs on infinities, and where an infinity meets a zero, on NaNs; WGSL defines neither, so
// every operation that can see one is decided on the bits here (ovf_*), as the light tree does (wavefront_light_tree.wgsl).
fn ovf_axis(len:f32)->bool{return bitcast<u32>(len)==0x80000000u;}
fn ovf_nan()->f32{return bitcast<f32>(rounded_steps(0x7fc00000u));}
fn ovf_is_nan(x:f32)->bool{return (bitcast<u32>(x)&0x7fffffffu)>0x7f800000u;}
fn ovf_is_inf(x:f32)->bool{return (bitcast<u32>(x)&0x7fffffffu)==0x7f800000u;}
fn ovf_is_zero(x:f32)->bool{return (bitcast<u32>(x)&0x7fffffffu)==0u;}
fn ovf_signed(sign:u32,magnitude:u32)->f32{return bitcast<f32>((sign&0x80000000u)|magnitude);}
// a < b: both keys ascend with the value, the two zeros share one; false for a NaN.
fn ovf_key(x:f32)->u32{let b=bitcast<u32>(x);if((b&0x7fffffffu)==0u){return 0x80000000u;}return select(b|0x80000000u,~b,(b&0x80000000u)!=0u);}
fn ovf_less(a:f32,b:f32)->bool{return !ovf_is_nan(a)&&!ovf_is_nan(b)&&ovf_key(a)<ovf_key(b);}
// A product of finite operands that overflows is an infinity: decided on half the product, which is exact to the same
// rounding (RN(a * b / 2) >= 2^127 exactly when RN(a * b) is not finite), never on an overflowed WGSL product.
fn ovf_mul(a:f32,b:f32)->f32{
    if(ovf_is_nan(a)||ovf_is_nan(b)){return ovf_nan();}
    let sign=bitcast<u32>(a)^bitcast<u32>(b);
    if(ovf_is_inf(a)||ovf_is_inf(b)){if(ovf_is_zero(a)||ovf_is_zero(b)){return ovf_nan();}return ovf_signed(sign,0x7f800000u);}
    if(ovf_is_zero(a)||ovf_is_zero(b)){return ovf_signed(sign,0u);}
    let ea=(bitcast<u32>(a)>>23u)&0xffu;let eb=(bitcast<u32>(b)>>23u)&0xffu;
    if(ea+eb>=382u){return ovf_signed(sign,0x7f800000u);}
    if(ea+eb>=380u){let half=select((a*0.5)*b,a*(b*0.5),ea<eb);if((bitcast<u32>(half)&0x7fffffffu)>=0x7f000000u){return ovf_signed(sign,0x7f800000u);}}
    return a*b;
}
// area_light_rect_sample in the frame of a zero axis: z = cross(x, y) and z0 are zero, so `nz /= sqrt(nz * nz + z0 * z0)`
// is a sign, or 0 / 0 = NaN in a lane whose coordinate is zero; safe_asinf of a NaN product is asinf(clamp(NaN, -1, 1)) =
// asinf(-1), fu and cu follow the NaN (clamp(NaN, -1, 1) = -1), and reduce_min(sqr(nz)) is taken as the oracle's ISA takes
// it (SSE: h = min(shuffle<1, 0, 3, 2>(a), a), then min(shuffle<2, 3, 0, 1>(h), h), _mm_min_ps returning its second
// operand for a NaN; scalar: min(min(a.x, a.y), min(a.z, a.w)) with `(a < b) ? a : b`). Without a NaN lane the squares are
// all 1, the planar fallback divides by z0 * len_u * len_v = 0 and safe_divide gives a pdf of 0.
// An infinite length (lu_inf, lv_inf: twice an r_spread above FLT_MAX / 2): that axis's bounds are infinities, its two nz
// lanes inf / inf = NaN, the planar fallback's denominator z0 * len_u * len_v is 0 * inf = NaN (safe_divide then divides)
// and a sampled yv = y1 is infinite. ovf_rect_flags: 1 the pdf is NaN, 2 the sampled point is NaN.
var<private> ovf_rect_flags:u32;
fn ovf_rectangle(P:vec3<f32>,C:vec3<f32>,U:vec3<f32>,lu:f32,lu_inf:bool,V:vec3<f32>,lv:f32,lv_inf:bool,random:vec2<f32>,sample_coord:bool)->AreaRect{
    ovf_rect_flags=0u;
    var out=AreaRect(C,0.0);var Z=rounded_cross(U,V);let direction=C-P;var z0=rounded_dot(direction,Z);if(z0>0.0){Z= -Z;z0= -z0;}
    let xc=rounded_dot(direction,U);let yc=rounded_dot(direction,V);let x0=rounded_add(xc,-0.5*lu);let x1=rounded_add(xc,0.5*lu);let y0=rounded_add(yc,-0.5*lv);let y1=rounded_add(yc,0.5*lv);
    var nz=vec4<f32>(-y0,x1,y1,-x0);var bad=vec4<bool>(lv_inf,lu_inf,lv_inf,lu_inf);
    // A coordinate whose square overflows is divided by an infinity (a zero lane); one whose square underflows by zero (an
    // infinite lane, which every later operation takes like its sign).
    for(var i=0u;i<4u;i++){
        if(bad[i]){nz[i]=0.0;continue;}
        if(ovf_is_inf(ovf_mul(nz[i],nz[i]))){nz[i]=0.0;continue;}
        let d=rounded_sqrt(rounded_add(nz[i]*nz[i],z0*z0));if(d==0.0){if(nz[i]==0.0){bad[i]=true;}else{nz[i]=select(1.0,-1.0,nz[i]<0.0);}}else{nz[i]=rounded_divide(nz[i],d);}}
    // Every lane is a sign or zero here (z0 is zero), so each product is 1, -1 or 0 and its arcsine the float pi / 2 with
    // that sign, or 0.
    var angles:vec4<f32>;let half_pi=bitcast<f32>(rounded_steps(0x3fc90fdbu));
    for(var i=0u;i<4u;i++){let j=(i+1u)&3u;var value= -1.0;if(!bad[i]&&!bad[j]){value= -nz[i]*nz[j];}angles[i]=select(select(half_pi,-half_pi,value<0.0),0.0,value==0.0);}
    let g2=angles.z;let g3=angles.w;let S= -rounded_add(rounded_add(rounded_add(angles.x,angles.y),g2),g3);
    if(sample_coord){
        // cu = clamp(copysignf(1.0f / sqrtf(fu * fu + b0sq), fu), -1.0f, 1.0f) is a number in [-1, 1] whatever au, fu and b0
        // are (an infinity clamps to 1 or -1, clamp(NaN, -1, 1) is -1), and z0 is zero, so
        // xu = -(cu * z0) / max(sqrtf(1.0f - cu * cu), 1e-7f) is a zero, clamped to [x0, x1], and d2 = sqr(xu) + sqr(z0).
        var xu=0.0;if(!lu_inf){xu=clamp(xu,x0,x1);}let d2=xu*xu;
        let dh0=rounded_sqrt(rounded_add(d2,y0*y0));let dh1=rounded_sqrt(rounded_add(d2,y1*y1));var yv=y1;
        // h0, h1, hv: 0 / 0 (and inf / inf) is NaN, and `hv2 < 1.0f - 1e-6f` is then false (yv = y1, an infinity times the
        // zero axis for an infinite length).
        if(lv_inf){ovf_rect_flags|=2u;yv=0.0;}
        else if(dh0!=0.0&&dh1!=0.0){let h0=rounded_divide(y0,dh0);let h1=rounded_divide(y1,dh1);let hv=rounded_add(h0,random.y*rounded_add(h1,-h0));let hv2=hv*hv;
            if(hv2<1.0-1e-6){yv=hv*rounded_sqrt(rounded_divide(d2,rounded_add(1.0,-hv2)));}}
        out.P=rounded_add3(rounded_add3(rounded_add3(P,xu*U),yv*V),z0*Z);
    }
    let sq=nz*nz;var smallest:f32;var smallest_nan:bool;
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE==0u){
        let h0_nan=bad.x;var h0=sq.x;if(!bad.x&&!bad.y){h0=min(sq.x,sq.y);}
        let h2_nan=bad.z;var h2=sq.z;if(!bad.z&&!bad.w){h2=min(sq.z,sq.w);}
        smallest_nan=h0_nan;smallest=h0;if(!h0_nan&&!h2_nan){smallest=min(h0,h2);}
    }else{
        let m0_nan=bad.y;var m0=sq.y;if(!bad.x&&!bad.y){m0=min(sq.x,sq.y);}
        let m1_nan=bad.w;var m1=sq.w;if(!bad.z&&!bad.w){m1=min(sq.z,sq.w);}
        smallest_nan=m1_nan;smallest=m1;if(!m0_nan&&!m1_nan){smallest=min(m0,m1);}
    }
    if(S<1e-5||(!smallest_nan&&smallest>0.99999)){
        if(lu_inf||lv_inf){ovf_rect_flags|=1u;}
        else{let t=rounded_sqrt(rounded_dot(direction,direction));let denom=z0*lu*lv;out.pdf=select(0.0,rounded_divide(-t*t*t,denom),denom!=0.0);}
    }else{out.pdf=rounded_divide(1.0,S);}
    return out;
}
// area_light_eval<false> and the tail of area_light_sample<false> (light/area.h) for a light with an overflowed axis:
// area_light_spread_clamp_light with spread_u = dot(0, x) = 0 and len_u infinite on that axis, then the rectangle (above)
// or the ellipse, the direction, the spread attenuation and the coordinate test.
fn area_evaluate_overflow(light:u32,origin:vec3<f32>,direction:vec3<f32>,distance:f32,random:vec2<f32>,disk:vec2<f32>,sample_coord:bool)->AreaEval{
    var out:AreaEval;let N=heap_vec3(light+L_area_dir);let center=heap_vec3(light+L_co);
    let signed_invarea=heap_float(light+L_area_invarea);let invarea=abs(signed_invarea);
    // A normalized light: its area is infinite, its inverse area zero, so eval_fac = M_1_PI_F * invarea * attenuation is
    // zero and `ls->eval_fac > 0` fails (and area_light_intersect returns false for `invarea == 0.0f`).
    if(invarea==0.0){return out;}
    if(sample_coord&&dot(center-origin,N)>0.0){return out;}
    let tan=heap_float(light+L_area_tan_half_spread);let normalization=heap_float(light+L_area_normalize_spread);
    let ou=ovf_axis(heap_float(light+L_area_len_u));let ov=ovf_axis(heap_float(light+L_area_len_v));
    let U=heap_vec3(light+L_area_axis_u);let V=heap_vec3(light+L_area_axis_v);
    var lu=heap_float(light+L_area_len_u);if(ou){lu=0.0;}var lv=heap_float(light+L_area_len_v);if(ov){lv=0.0;}
    var rectangle=signed_invarea>0.0;var P=center;var P_nan=false;
    // The lengths after the clamp: finite values, or infinite (lu_inf, lv_inf).
    var lu_inf=ou;var lv_inf=ov;
    // normalize_spread > 0, on the bits: the default spread's 1 / (FLT_MAX - pi / 2) is a subnormal number, which a device
    // may read as zero (the ordinary light is the same with or without its clamp; this one is not).
    let spread_bits=load_word(light+L_area_normalize_spread);let normalized=spread_bits!=0u&&spread_bits<=0x7f800000u;
    if(normalized){
        let t=rounded_dot(N,origin-center);let closest=rounded_add3(origin,-(t*N));
        // r_spread = t * tan_half_spread, which overflows for the default spread (tan_half_spread FLT_MAX) further than one
        // unit from the light's plane.
        let r_ieee=ovf_mul(t,tan);let r_inf=ovf_is_inf(r_ieee);var r=0.0;if(!r_inf){r=r_ieee;}
        var su=0.0;if(!ou){su=rounded_dot(U,closest-center);}var sv=0.0;if(!ov){sv=rounded_dot(V,closest-center);}
        // is_round: `!(*sample_rectangle) && (*len_u == *len_v)`, both lengths infinite. Then dist is 0 and the disk's
        // radius infinite: `dist >= r + r_spread` and `dist > fabsf(r - r_spread)` are false, and sample_spread =
        // `(dist <= fabsf(r - r_spread)) && (r_spread < r)` is true unless r_spread is infinite too (a NaN difference).
        let round= !rectangle&&ou&&ov;var sample_spread= !r_inf&&r==0.0;
        if(round&&!sample_spread){sample_spread= !r_inf;}
        else if(!round&&!sample_spread){
            // The clamped range of each axis. Overflowed: max(0 - r_spread, -inf) = -r_spread and min(0 + r_spread, inf) =
            // r_spread, its centre 0, or 0.5f * (-inf + inf) = NaN for an infinite r_spread. Finite: the usual range, or
            // the whole length for an infinite r_spread. `min_u >= max_u || min_v >= max_v` returns false.
            var min_u= -r;var max_u=r;if(!ou){min_u= -lu*0.5;max_u=lu*0.5;if(!r_inf){min_u=max(su-r,min_u);max_u=min(su+r,max_u);}}
            var min_v= -r;var max_v=r;if(!ov){min_v= -lv*0.5;max_v=lv*0.5;if(!r_inf){min_v=max(sv-r,min_v);max_v=min(sv+r,max_v);}}
            if(!(ou&&r_inf)&&min_u>=max_u){return out;}if(!(ov&&r_inf)&&min_v>=max_v){return out;}
            // rect_len = max - min: twice r_spread on an overflowed axis, an infinity above FLT_MAX / 2.
            let twice_inf=r_inf||ovf_is_inf(ovf_mul(r,2.0));let rlu_inf=ou&&twice_inf;let rlv_inf=ov&&twice_inf;
            var rlu=0.0;if(!rlu_inf){rlu=max_u-min_u;}var rlv=0.0;if(!rlv_inf){rlv=max_v-min_v;}
            // rect_area < fminf(ellipse_area, spread_area): the ellipse's area is infinite (an infinite length).
            var take=rectangle;
            if(!take&&!rlu_inf&&!rlv_inf&&!r_inf){take=ovf_less(ovf_mul(rlu,rlv),ovf_mul(EMITTER_PI,ovf_mul(r,r)));}
            if(take){
                rectangle=true;lu=rlu;lv=rlv;lu_inf=rlu_inf;lv_inf=rlv_inf;
                // *lightP + *axis_u * new_center_u + *axis_v * new_center_v: the zero axis times the NaN centre is NaN.
                if((ou||ov)&&r_inf){P_nan=true;}else{P=rounded_add3(rounded_add3(P,U*(0.5*(min_u+max_u))),V*(0.5*(min_v+max_v)));}
            }else{rectangle=false;sample_spread= !r_inf&&!ovf_is_inf(ovf_mul(EMITTER_PI,ovf_mul(r,r)));}
        }
        // *len_u = *len_v = r_spread * 2.0f, an infinity above FLT_MAX / 2 (the round light alone reaches that).
        if(sample_spread){rectangle=false;P=rounded_add3(rounded_add3(P,U*su),V*sv);
            lu_inf=ovf_is_inf(ovf_mul(r,2.0));lv_inf=lu_inf;lu=0.0;if(!lu_inf){lu=r*2.0;}lv=lu;}
    }
    // pdf_nan / pdf_inf: the pdf as the pinned arithmetic leaves it when it is not a finite number.
    var pdf=0.0;var pdf_nan=false;var pdf_inf=false;
    if(rectangle){
        // A NaN centre: area_light_rect_sample's nz is NaN in every lane, S is four asinf(-1) negated and the pdf 1.0f / S.
        if(P_nan){pdf=rounded_divide(1.0,bitcast<f32>(0x40c90fdbu));}
        else{let rect=ovf_rectangle(origin,P,U,lu,lu_inf,V,lv,lv_inf,random,sample_coord);P=rect.P;pdf=rect.pdf;
            pdf_nan=(ovf_rect_flags&1u)!=0u;if(sample_coord&&(ovf_rect_flags&2u)!=0u){P_nan=true;}}
    }else if(tan==0.0){pdf=1.0;}
    else{
        if(sample_coord){
            // ellipse_sample(axis_u * len_u * 0.5f, axis_v * len_v * 0.5f, rand): a zero axis times an infinite length is NaN.
            if(lu_inf||lv_inf){P_nan=true;}
            else{P+=U*(lu*0.5*disk.x)+V*(lv*0.5*disk.y);}
        }
        // 4.0f * M_1_PI_F / (len_u * len_v): 0 for an infinite length, an infinity for a zero one.
        if(!lu_inf&&!lv_inf){let area=ovf_mul(lu,lv);if(ovf_is_zero(area)){pdf_inf=true;}else if(!ovf_is_inf(area)){pdf=(4.0*EMITTER_INV_PI)/area;}}
    }
    var D=direction;var tt=distance;var D_nan=false;
    if(sample_coord){
        // safe_normalize_len: a zero vector stays zero with t = 0 (tan_angle of it is 0 / 0).
        if(P_nan){D_nan=true;}else{let ray=emitter_normalize_len(P-origin);D=ray.xyz;tt=ray.w;if(tt==0.0){D_nan=true;}}
    }
    out.eval_fac=EMITTER_INV_PI*invarea;
    if(normalized){
        // area_light_spread_attenuation: tan_a = len(cross(-D, Ng)) / dot(-D, Ng) is NaN for a NaN (or zero) direction and
        // an infinity for a direction in the light's plane; `(tan_a > 1e-5f) ? 0.0f : M_PI_F` for a spread of zero,
        // max((tan_half_spread - tan_a) * normalize_spread, 0.0f) otherwise (0 for a NaN and for a positive infinity).
        var tangent_nan=D_nan;var tangent_inf=false;var tangent=0.0;
        if(!D_nan){let cosine=dot(-D,N);let sine=length(cross(-D,N));
            if(cosine==0.0){if(sine==0.0){tangent_nan=true;}else{tangent_inf=true;}}else{tangent=sine/cosine;}}
        var attenuation=0.0;
        if(tan==0.0){attenuation=select(EMITTER_PI,0.0,!tangent_nan&&(tangent_inf||tangent>1e-5));}
        else if(!tangent_nan&&!tangent_inf){
            // (tan_half_spread - tan_a) * normalize_spread. A subnormal factor is taken as its integer mantissa times
            // 2^-149: the product scaled by 2^-64 before and 2^-85 after (powers of two, read at run time so that no
            // compiler folds them into a subnormal constant), which is the same single rounding.
            let x=tan-tangent;var product=x*normalization;
            if((spread_bits&0x7f800000u)==0u){
                let down=bitcast<f32>(rounded_steps(0x1f800000u));let rest=bitcast<f32>(rounded_steps(0x15000000u));
                product=((x*down)*f32(spread_bits&0x7fffffu))*rest;}
            attenuation=max(product,0.0);}
        out.eval_fac*=attenuation;
    }
    if(!rectangle&&tan>0.0){
        // light_pdf_area_to_solid_angle(Ng, -D, t): `cos_pi <= 0.0f` is false for a NaN, and t * t / cos_pi is NaN then; an
        // infinite pdf times a zero factor is NaN too.
        if(D_nan){pdf_nan=true;}else{let cosine=dot(N,-D);let factor=select(0.0,tt*tt/cosine,cosine>0.0);
            if(pdf_inf){if(factor==0.0){pdf_nan=true;}}else{pdf*=factor;}}
    }
    if(!(out.eval_fac>0.0)){return out;}
    if(sample_coord){
        // The coordinate test of area_light_sample: light_u = dot(inplane, 0) = 0 on the overflowed axis (its epsilon is
        // infinite), and no comparison rejects a NaN point. Then light_sample's `ls->pdf > 0.0f`.
        if(normalized&&!P_nan){
            let plane=P-center;var a=0.0;var b=0.0;var outside=false;
            if(!ou){let u=dot(plane,U);let eu=(0.5+1e-7)*heap_float(light+L_area_len_u)+1e-6;a=u/eu;outside=outside||abs(u)>eu;}
            if(!ov){let v=dot(plane,V);let ev=(0.5+1e-7)*heap_float(light+L_area_len_v)+1e-6;b=v/ev;outside=outside||abs(v)>ev;}
            if(signed_invarea<0.0){if(a*a+b*b>1.0){return out;}}else if(outside){return out;}
        }
        if(P_nan||pdf_nan||pdf_inf||!(pdf>0.0)){return out;}
        out.P=P;out.D=D;out.t=tt;out.pdf=pdf;out.valid=true;return out;
    }
    // A hit: the pdf goes to the forward MIS weight; a NaN one (kept as a NaN) makes that weight NaN on the CPU, and
    // film_clamp_light drops the emission (wavefront_light_eval.wgsl).
    out.P=center;out.D=direction;out.t=distance;out.pdf=pdf;if(pdf_nan||pdf_inf){out.pdf=ovf_nan();}out.valid=true;return out;
}
//#endif
fn area_evaluate(light:u32,origin:vec3<f32>,direction:vec3<f32>,distance:f32,random:vec2<f32>,sample_coord:bool)->AreaEval{
    var out:AreaEval;let N=heap_vec3(light+L_area_dir);let center=heap_vec3(light+L_co);
//#if LIGHT_AREA_OVERFLOW
    // The disk sample of an ellipse, taken once here for the ordinary light and for the overflowed one (one inlined copy).
    var area_disk=vec2<f32>(0);if(sample_coord){area_disk=emitter_disk(random);}
//#endif
//#if LIGHT_AREA_DEGENERATE
    // An axis whose length underflows to zero (native/DEVICE.md, item 148; the host refuses a spread of zero). The CPU's
    // axis holds infinities and NaNs (normalize_len of a zero vector), the device's copy a zero axis. area_light_spread_clamp_light
    // then rejects every point off the light's plane (min_u = max(NaN, -0) = -0 >= max_u = min(NaN, 0) = 0), except for a
    // disk with both axes zero, whose NaN distance takes neither branch; a next-event sample there has a NaN direction and a
    // zero spread attenuation (max(NaN, 0) = 0), so no sample is valid. A hit from a camera ray keeps eval_fac with the ray's
    // direction (area_light_eval with sample_coord false, invarea 1 since the area is zero).
    if(heap_float(light+L_area_len_u)==0.0||heap_float(light+L_area_len_v)==0.0){
//#if LIGHT_AREA_OVERFLOW
        if(ovf_axis(heap_float(light+L_area_len_u))||ovf_axis(heap_float(light+L_area_len_v))){return area_evaluate_overflow(light,origin,direction,distance,random,area_disk,sample_coord);}
//#endif
//#if LIGHT_NAN_DIRECTION
        // A spread of zero (tan_half_spread 0, normalize_spread FLT_MAX). area_light_spread_clamp_light: r_spread = t * 0 is
        // zero, so sample_spread is true, both shape branches are skipped and *lightP = *lightP + *axis_u * spread_u +
        // *axis_v * spread_v is NaN in every lane (spread_u is a dot product with the NaN axis); it returns true. Then
        // ls->pdf = 1.0f (not a rectangle, tan_half_spread == 0.0f), ls->D = safe_normalize_len(NaN) and ls->t are NaN, and
        // area_light_spread_attenuation returns `(tan_a > 1e-5f) ? 0.0f : M_PI_F`: pi for the NaN tangent. eval_fac is
        // M_1_PI_F * invarea * M_PI_F > 0 and neither `fabsf(light_u) > len_u_epsilon` nor the ellipse's sum test rejects a
        // NaN coordinate: the sample is valid with a NaN point, direction and distance (native/DEVICE.md, item 148).
        if(sample_coord&&heap_float(light+L_area_tan_half_spread)==0.0){
            if(dot(center-origin,N)>0.0){return out;}
            let nan=bitcast<f32>(rounded_steps(0x7fc00000u));out.P=vec3<f32>(nan);out.D=vec3<f32>(nan);out.t=nan;out.pdf=1.0;
            out.eval_fac=(EMITTER_INV_PI*abs(heap_float(light+L_area_invarea)))*EMITTER_PI;out.valid=out.eval_fac>0.0;return out;
        }
//#endif
        if(sample_coord){return out;}
        let invarea=heap_float(light+L_area_invarea);let spread=heap_float(light+L_area_tan_half_spread);
        let round=invarea<0.0&&heap_float(light+L_area_len_u)==0.0&&heap_float(light+L_area_len_v)==0.0;
        if(!round&&rounded_dot(N,origin-center)*spread!=0.0){return out;}
        out.P=center;out.D=direction;out.t=distance;out.eval_fac=EMITTER_INV_PI*abs(invarea);let normalization=heap_float(light+L_area_normalize_spread);
//#if LIGHT_NAN_DIRECTION else         if(normalization>0.0){let tangent=length(cross(-out.D,N))/dot(-out.D,N);out.eval_fac*=max((spread-tangent)*normalization,0.0);}
        // A hit with a spread of zero: the attenuation is pi where the ray's tangent to the light's normal is at most 1e-5.
        if(normalization>0.0){let tangent=length(cross(-out.D,N))/dot(-out.D,N);var attenuation=max((spread-tangent)*normalization,0.0);
            if(spread==0.0){attenuation=select(EMITTER_PI,0.0,tangent>1e-5);}out.eval_fac*=attenuation;}
//#endif
        out.valid=out.eval_fac>0.0;return out;
    }
//#endif
    if(sample_coord&&dot(center-origin,N)>0.0){return out;}
    let region=area_region(light,origin);if(!region.valid){return out;}
    out.P=region.P;let spread=heap_float(light+L_area_tan_half_spread);let normalization=heap_float(light+L_area_normalize_spread);
    if(region.rectangle){let rect=area_rectangle(origin,region,random,sample_coord);out.P=rect.P;out.pdf=rect.pdf;}
    else if(spread==0.0){out.pdf=1.0;}
//#if LIGHT_AREA_OVERFLOW else     else{if(sample_coord){let disk=emitter_disk(random);out.P+=region.U*(region.len_u*0.5*disk.x)+region.V*(region.len_v*0.5*disk.y);}
    else{if(sample_coord){let disk=area_disk;out.P+=region.U*(region.len_u*0.5*disk.x)+region.V*(region.len_v*0.5*disk.y);}
//#endif
        out.pdf=(4.0*EMITTER_INV_PI)/(region.len_u*region.len_v);}
    out.D=direction;out.t=distance;if(sample_coord){let ray=emitter_normalize_len(out.P-origin);out.D=ray.xyz;out.t=ray.w;}
    out.eval_fac=EMITTER_INV_PI*abs(heap_float(light+L_area_invarea));
    if(normalization>0.0){let tangent=length(cross(-out.D,N))/dot(-out.D,N);var attenuation=max((spread-tangent)*normalization,0.0);
        if(spread==0.0){attenuation=select(EMITTER_PI,0.0,tangent>1e-5);}out.eval_fac*=attenuation;}
    if(!region.rectangle&&spread>0.0){let cosine=dot(N,-out.D);out.pdf*=select(0.0,out.t*out.t/cosine,cosine>0.0);}
    if(out.eval_fac<=0.0){return out;}
    if(sample_coord&&normalization>0.0){let plane=out.P-center;let u=dot(plane,heap_vec3(light+L_area_axis_u));let v=dot(plane,heap_vec3(light+L_area_axis_v));
        let eu=(0.5+1e-7)*heap_float(light+L_area_len_u)+1e-6;let ev=(0.5+1e-7)*heap_float(light+L_area_len_v)+1e-6;
        if(heap_float(light+L_area_invarea)<0.0){if((u/eu)*(u/eu)+(v/ev)*(v/ev)>1.0){return out;}}
        else if(abs(u)>eu||abs(v)>ev){return out;}
    }out.valid=true;return out;
}
//#if LIGHT_LAMP_SHADER
// Point, spot, area and sun lights whose emission shader is not constant: the ShaderData at the light point,
// light_normal_uv_from_position (light/light.h with point.h point_light_uv, spot.h spot_light_to_local and
// spot_light_uv, area.h area_light_uv, sun.h sun_light_uv) and shader_setup_from_sample for a lamp
// (geom/shader_data.h), every sum, quotient and square root correctly rounded in the host's order.
// The host's transform_direction (SSE: x*c0 + (y*c1 + z*c2); scalar float4 dots: (x*c0 + y*c1) + z*c2).
fn lamp_direction(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    let c0=vec3<f32>(t.x.x,t.y.x,t.z.x)*v.x;let c1=vec3<f32>(t.x.y,t.y.y,t.z.y)*v.y;let c2=vec3<f32>(t.x.z,t.y.z,t.z.z)*v.z;
    if(CAMERA_NATIVE_VECTOR_DIVIDE!=0u){return rounded_add3(c0,rounded_add3(c1,c2));}
    return rounded_add3(rounded_add3(c0,c1),c2);
}
fn lamp_itfm(light:u32)->NativeTransform{return motion_transform_load(table_at(G_objects,load_word(light+L_object_id),SIZE_objects)+O_itfm);}
// float3 / float: SSE hosts divide each lane, the scalar operator multiplies by 1/f.
fn lamp_div3(v:vec3<f32>,f:f32)->vec3<f32>{
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return v*rounded_divide(1.0,f);}
    return vec3<f32>(rounded_divide(v.x,f),rounded_divide(v.y,f),rounded_divide(v.z,f));
}
struct LampNormalUV {Ng:vec3<f32>,uv:vec2<f32>}
fn lamp_normal_uv(light:u32,P:vec3<f32>,D:vec3<f32>)->LampNormalUV{
    var out:LampNormalUV;let kind=load_word(light+L_type);
    if(kind==LIGHT_SPOT||kind==LIGHT_POINT){
        out.Ng=-D;if(load_word(light+L_is_sphere)!=0u){out.Ng=surface_native_normalize(rounded_add3(P,-heap_vec3(light+L_co)));}
        if(kind==LIGHT_SPOT){
            var ray=rounded_safe_normalize(lamp_direction(lamp_itfm(light),-D));ray.z=-ray.z;
            let factor=rounded_divide(heap_float(light+LS_half_cot_half_spot_angle),ray.z);
            out.uv=vec2<f32>(rounded_add(ray.y*factor,0.5),-(rounded_add(ray.x,ray.y)*factor));
        }else{
            // map_to_sphere of the normal in the light's object space.
            let co=lamp_direction(lamp_itfm(light),out.Ng);let l=rounded_dot(co,co);var u=0.0;var v=0.0;
            if(l>0.0){
                if(!(co.x==0.0&&co.y==0.0)){u=rounded_add(0.5,-(trig_atan2_guarded(co.x,co.y)*0.15915493667125702));}
                v=rounded_add(1.0,-(trig_acos(rounded_divide(co.z,rounded_sqrt(l)))*0.31830987334251404));
            }
            out.uv=vec2<f32>(v,rounded_add(rounded_add(1.0,-u),-v));
        }
    }else if(kind==LIGHT_AREA){
        out.Ng=heap_vec3(light+L_area_dir);
        let inv_u=lamp_div3(heap_vec3(light+L_area_axis_u),heap_float(light+L_area_len_u));
        let inv_v=lamp_div3(heap_vec3(light+L_area_axis_v),heap_float(light+L_area_len_v));
        let inplane=rounded_add3(P,-heap_vec3(light+L_co));
//#if LIGHT_AREA_DEGENERATE
        // A zero-length axis (item 148): the CPU's clamp(NaN, -0.5, 0.5) is min(max(NaN, -0.5), 0.5) = -0.5.
        var u=-0.5;if(heap_float(light+L_area_len_u)!=0.0){u=clamp(rounded_dot(inplane,inv_u),-0.5,0.5);}
        var v=-0.5;if(heap_float(light+L_area_len_v)!=0.0){v=clamp(rounded_dot(inplane,inv_v),-0.5,0.5);}
//#if LIGHT_AREA_OVERFLOW
        // An overflowed axis (item 233): the inverse extent is 0 / inf = 0, so the coordinate is clamp(0, -0.5, 0.5) = 0.
        if(ovf_axis(heap_float(light+L_area_len_u))){u=0.0;}if(ovf_axis(heap_float(light+L_area_len_v))){v=0.0;}
//#endif
//#endif
//#if !LIGHT_AREA_DEGENERATE
        let u=clamp(rounded_dot(inplane,inv_u),-0.5,0.5);let v=clamp(rounded_dot(inplane,inv_v),-0.5,0.5);
//#endif
        out.uv=vec2<f32>(rounded_add(v,0.5),rounded_add(-u,-v));
    }else{
        out.Ng=-D;let delta=rounded_add3(D,-heap_vec3(light+L_co));
        let fac=rounded_divide(heap_float(light+LS_half_inv_sin_half_angle),rounded_sqrt(rounded_dot(delta,delta)));
        let t=lamp_itfm(light);let u=rounded_dot(D,t.x.xyz)*fac;let v=rounded_dot(D,t.y.xyz)*fac;
        out.uv=vec2<f32>(rounded_add(v,0.5),rounded_add(-u,-v));
    }
    return out;
}
// shader_setup_from_sample(P, Ng, -D, shader, object, light, u, v, t, time, object_space false, is_lamp true).
fn lamp_shader_data(light_index:u32,P:vec3<f32>,D:vec3<f32>,t:f32,time:f32)->SurfaceData{
    var sd:SurfaceData;
    if(light_index>=table_count(G_lights)){surface_failure(&sd);return sd;}
    let light=table_at(G_lights,light_index,SIZE_lights);let shader=load_word(light+L_shader_id);
    if((shader&SHADER_MASK)>=table_count(G_shaders)){surface_failure(&sd);return sd;}
    let nuv=lamp_normal_uv(light,P,D);
    sd.P=P;sd.N=nuv.Ng;sd.Ng=nuv.Ng;sd.wi=-D;sd.shader=shader;sd.primitive_type=PRIMITIVE_LAMP;
    sd.object_id=load_word(light+L_object_id);sd.prim=light_index;sd.u=nuv.uv.x;sd.v=nuv.uv.y;sd.time=time;sd.ray_length=t;
    sd.runtime_flag=0u;sd.shader_flag=load_word(table_at(G_shaders,shader&SHADER_MASK,SIZE_shaders)+SH_flags);
    sd.object_flag=0u;if(sd.object_id!=NONE){sd.object_flag=load_word(table_at(G_object_flag,sd.object_id,4u));}
//#if LIGHT_MOTION
    // shader_setup_object_transforms: a moving light object's motion transform at the sample's time (object-space inputs
    // of its emission shader).
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){sd.motion_tfm=motion_object(sd.object_id,time);sd.motion_itfm=motion_inverse(sd.motion_tfm);
        if(load_word(p(17u))!=0u){surface_failure(&sd);return sd;}}
//#endif
    if(rounded_dot(sd.Ng,sd.wi)<0.0){sd.runtime_flag|=SR_BACKFACING;sd.Ng=-sd.Ng;sd.N=-sd.N;}
    return sd;
}
//#endif
//#if LIGHT_LAMP_SHADER
// The emission of a lamp's (non-constant) shader at a light point (shade_light.h integrate_light_nee and
// light/sample.h light_sample_shader_eval_forward: surface_shader_eval with PATH_RAY_EMISSION for all closures).
fn lamp_shader_emission(light_index:u32,P:vec3<f32>,D:vec3<f32>,t:f32,time:f32)->vec3<f32>{
    let sd=lamp_shader_data(light_index,P,D,t,time);if(sd.error!=0u){return vec3<f32>(0);}
    let r=svm_evaluate_context(NONE,sd,0u,0u,PATH_RAY_EMISSION,0u,0u,false);if(r.error!=0u){return vec3<f32>(0);}
    return r.emission;
}
//#endif
