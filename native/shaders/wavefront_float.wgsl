// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Original binary32 adapters for rounding-sensitive normal and closure math.
// Integer addition makes the rounding boundary observable to GPU optimizers:
// a product passed here cannot be contracted into a later floating-point sum.
fn rounded_add(a:f32,b:f32)->f32{
    var ax=bitcast<u32>(a);var bx=bitcast<u32>(b);
    if((ax&0x7fffffffu)<(bx&0x7fffffffu)){let swap=ax;ax=bx;bx=swap;}
    let sign=ax&0x80000000u;let am=ax&0x7fffffffu;let bm=bx&0x7fffffffu;
    if(am==0u){return bitcast<f32>((ax&bx)&0x80000000u);}
    if(bm==0u){return bitcast<f32>(ax);}
    if(am>=0x7f800000u){return a+b;}
    if(am==bm&&((ax^bx)&0x80000000u)!=0u){return 0.0;}
    let ae=am>>23u;let be=bm>>23u;var exponent=max(ae,1u);
    let delta=exponent-max(be,1u);
    let ma=((am&0x7fffffu)|select(0u,0x800000u,ae!=0u))<<3u;
    var mb=((bm&0x7fffffu)|select(0u,0x800000u,be!=0u))<<3u;
    if(delta>=31u){mb=1u;}
    else if(delta>0u){mb=(mb>>delta)|select(0u,1u,(mb&((1u<<delta)-1u))!=0u);}
    var sum=ma+mb;if(((ax^bx)&0x80000000u)!=0u){sum=ma-mb;}
    if(sum>=0x8000000u){sum=(sum>>1u)|(sum&1u);exponent++;}
    else if(sum<0x4000000u){let shift=min(countLeadingZeros(sum)-5u,exponent-1u);sum<<=shift;exponent-=shift;}
    let tail=sum&7u;var mantissa=sum>>3u;
    if(tail>4u||(tail==4u&&(mantissa&1u)!=0u)){mantissa++;}
    if(mantissa>=0x1000000u){mantissa>>=1u;exponent++;}
    if(mantissa<0x800000u){exponent=0u;}
    if(exponent>=255u){return bitcast<f32>(sign|0x7f800000u);}
    return bitcast<f32>(sign|(exponent<<23u)|(mantissa&0x7fffffu));
}
// Single-body loops (compiler pressure, native/DEVICE.md "Surface entries under the
// Intel compile envelope"): every rounded_add body is inlined at each call site,
// and before these loops rounded_add3, rounded_dot and the helpers below inlined
// two to six copies per call (about 56 Intel backend instructions each; a third
// of the textured-Principled prepare_surface). Each helper now runs its sums
// through ONE body in a loop whose bound is opaque to the compiler: push word 31,
// which no dispatch sets (always 0), so rounded_steps(n) is n. Iterations past n
// write nothing. The operands, the operations and their order are those of the
// unrolled form, so results are bit-identical.
fn rounded_steps(n:u32)->u32{return n|p(31u);}
fn rounded_add3(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{
    var out=vec3<f32>(0);let count=rounded_steps(3u);
    for(var k=0u;k<count;k++){
        let r=rounded_add(select(select(a.z,a.y,k==1u),a.x,k==0u),select(select(b.z,b.y,k==1u),b.x,k==0u));
        out=select(out,vec3<f32>(r),vec3<u32>(0u,1u,2u)==vec3<u32>(k));
    }
    return out;
}
// rounded_add(rounded_add(a.x*b.x,a.y*b.y),a.z*b.z).
fn rounded_dot(a:vec3<f32>,b:vec3<f32>)->f32{
    var sum=a.x*b.x;let count=rounded_steps(3u);
    for(var k=1u;k<count;k++){sum=select(sum,rounded_add(sum,select(a.z*b.z,a.y*b.y,k==1u)),k<3u);}
    return sum;
}
fn rounded_cross(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{return rounded_add3(a.yzx*b.zxy,-a.zxy*b.yzx);}
// The state contains an integer floor candidate and its exact remainder.
fn rounded_root_step(state:vec2<u32>,step:u32)->vec2<u32>{
    let cost=(2u*state.x+step)*step;
    return select(state,vec2<u32>(state.x+step,state.y-cost),state.y>=cost);
}
fn rounded_quotient_step(state:vec2<u32>,denominator:u32,step:u32)->vec2<u32>{
    let cost=denominator*step;
    return select(state,vec2<u32>(state.x+step,state.y-cost),state.y>=cost);
}
// WGSL permits 2.5 ULP division error. Its inherited sqrt bound combines
// 2 ULP inverseSqrt with that division bound. For these normalized arguments,
// +/-4 quotient units and +/-16 root units contain the rounded exact result,
// including the wider ULP if an intermediate crosses the upper binade.
// Starting one integer below the lower bound gives a nonnegative remainder.
// That remainder fits in one word even though the numerator/product does not:
// subtracting their low words recovers it exactly, modulo 2^32. Fixed steps
// then recover floor and remainder before nearest-even rounding. This avoids
// repeated wide products and data-dependent loops inside the larger SVM.
// https://www.w3.org/TR/WGSL/#floating-point-accuracy
fn rounded_sqrt(value:f32)->f32{
    let bits=bitcast<u32>(value);if((bits&0x7fffffffu)==0u){return value;}
    if((bits&0x80000000u)!=0u||(bits&0x7f800000u)==0x7f800000u){return sqrt(value);}
    var exponent=i32(bits>>23u);var mantissa=bits&0x7fffffu;
    if(exponent==0){let shift=countLeadingZeros(mantissa)-8u;mantissa<<=shift;exponent=1-i32(shift);}
    else{mantissa|=0x800000u;}
    let unbiased=exponent-127;let shift=23u+u32(unbiased&1);
    let low=mantissa<<shift;
    let scaled=bitcast<f32>((mantissa&0x7fffffu)|(u32(127+(unbiased&1))<<23u));
    let estimate=bitcast<u32>(sqrt(scaled));
    var root=((estimate&0x7fffffu)|0x800000u)<<select(0u,1u,(estimate>>23u)>127u);
    if((estimate>>23u)<127u){root>>=1u;}
    root-=17u;var state=vec2<u32>(root,low-root*root);
    // Steps 32, 16, 8, 4, 2, 1 through one body (rounded_steps).
    let count=rounded_steps(6u);
    for(var k=0u;k<count;k++){state=rounded_root_step(state,select(0u,32u>>k,k<6u));}
    // An integer radicand cannot lie exactly at a squared half-integer.
    root=state.x+select(0u,1u,4u*state.y>4u*state.x+1u);
    var result_exponent=(unbiased>>1u)+127;
    if(root==0x1000000u){root>>=1u;result_exponent++;}
    return bitcast<f32>((u32(result_exponent)<<23u)|(root&0x7fffffu));
}
fn rounded_divide(a:f32,b:f32)->f32{
    let ab=bitcast<u32>(a);let bb=bitcast<u32>(b);let sign=(ab^bb)&0x80000000u;
    let am=ab&0x7fffffffu;let bm=bb&0x7fffffffu;
    if(bm==0u||am>=0x7f800000u||bm>=0x7f800000u){return a/b;}
    if(am==0u){return bitcast<f32>(sign);}
    var ae=i32(am>>23u);var be=i32(bm>>23u);
    var ma=am&0x7fffffu;var mb=bm&0x7fffffu;
    if(ae==0){let shift=countLeadingZeros(ma)-8u;ma<<=shift;ae=1-i32(shift);}else{ma|=0x800000u;}
    if(be==0){let shift=countLeadingZeros(mb)-8u;mb<<=shift;be=1-i32(shift);}else{mb|=0x800000u;}
    var exponent=ae-be+127;if(ma<mb){ma<<=1u;exponent--;}
    let estimate=bitcast<u32>(f32(ma)/f32(mb));
    var quotient=((estimate&0x7fffffu)|0x800000u)<<select(0u,1u,(estimate>>23u)>127u);
    if((estimate>>23u)<127u){quotient>>=1u;}
    quotient-=5u;var state=vec2<u32>(quotient,(ma<<23u)-mb*quotient);
    // Steps 8, 4, 2, 1 through one body (rounded_steps).
    let count=rounded_steps(4u);
    for(var k=0u;k<count;k++){state=rounded_quotient_step(state,mb,select(0u,8u>>k,k<4u));}
    quotient=state.x;let remainder=state.y;
    if(exponent<=0){
        // Use floor and remainder directly to avoid double rounding.
        let shift=u32(1-exponent);if(shift>24u){return bitcast<f32>(sign);}
        let tail=quotient&((1u<<shift)-1u);let midpoint=1u<<(shift-1u);quotient>>=shift;
        if(tail>midpoint||(tail==midpoint&&(remainder!=0u||(quotient&1u)!=0u))){quotient++;}
        return bitcast<f32>(sign|quotient);
    }
    if(2u*remainder>mb||(2u*remainder==mb&&(quotient&1u)!=0u)){quotient++;}
    if(quotient==0x1000000u){quotient>>=1u;exponent++;}
    if(exponent>=255){return bitcast<f32>(sign|0x7f800000u);}
    return bitcast<f32>(sign|(u32(exponent)<<23u)|(quotient&0x7fffffu));
}
fn rounded_normalize(v:vec3<f32>)->vec3<f32>{
    // vec3(v.x/norm, v.y/norm, v.z/norm), the three quotients through one body (rounded_steps).
    let norm=rounded_sqrt(rounded_dot(v,v));var out=vec3<f32>(0);let count=rounded_steps(3u);
    for(var k=0u;k<count;k++){
        let q=rounded_divide(select(select(v.z,v.y,k==1u),v.x,k==0u),norm);
        out=select(out,vec3<f32>(q),vec3<u32>(0u,1u,2u)==vec3<u32>(k));
    }
    return out;
}
fn rounded_safe_normalize(v:vec3<f32>)->vec3<f32>{
    let norm=rounded_sqrt(rounded_dot(v,v));if(norm==0.0){return v;}
    return v*rounded_divide(1.0,norm);
}
