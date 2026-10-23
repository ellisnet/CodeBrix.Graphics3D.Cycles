// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Original two-component Taylor evaluation for disk angles in [-pi/4,3*pi/4].
// Reduction to [-pi/4,pi/4] uses a split pi/2; sine degree 15 and cosine degree
// 16 use signed reciprocal factorials split into high/low binary32 parts.
// This is a bounded adapter, not a general-purpose transcendental library.
// The small-angle return also keeps the product-residual scales normal.
// The low-word product difference recovers its exact rounding residual: both
// 24-bit significands are integers and the residual fits in signed 24 bits.
fn trig_product(a:f32,b:f32)->vec2<f32>{
    let product=a*b;
    let ab=bitcast<u32>(a);let bb=bitcast<u32>(b);let pb=bitcast<u32>(product);
    if((ab&0x7fffffffu)==0u||(bb&0x7fffffffu)==0u){return vec2<f32>(product,0);}
    let ae=(ab>>23u)&255u;let be=(bb>>23u)&255u;let pe=(pb>>23u)&255u;
    let ma=(ab&0x7fffffu)|0x800000u;let mb=(bb&0x7fffffu)|0x800000u;let mp=(pb&0x7fffffu)|0x800000u;
    let shift=pe+150u-ae-be;
    let residual=bitcast<i32>(ma*mb-(mp<<shift));
    let scale=bitcast<f32>(((ae+be-173u)<<23u)|((ab^bb)&0x80000000u));
    return vec2<f32>(product,f32(residual)*scale);
}
fn trig_mul(a:vec2<f32>,b:vec2<f32>)->vec2<f32>{
    let p=trig_product(a.x,b.x);
    return vec2<f32>(p.x,(p.y+a.x*b.y)+a.y*b.x);
}
// The Taylor coefficient is larger than the incoming Horner product, so the
// first rounded sum plus its recovered residual retains the low component.
fn trig_coeff(a:vec2<f32>,b:vec2<f32>)->vec2<f32>{
    let high=rounded_add(a.x,b.x);
    let residual=rounded_add(b.x,rounded_add(a.x,-high));
    return vec2<f32>(high,(residual+a.y)+b.y);
}
// The Horner steps run as loops over the coefficient pairs (lane L, 2026-10-22):
// the same operations in the same order as the unrolled form, so results are
// bit-identical, but each call site inlines one step instead of all of them.
// This keeps the entry points that use the precise trigonometry inside the Intel
// Mesa 25.0.7 backend's 16-bit instruction/register range (native/DEVICE.md,
// "Linux reference fixes on the updated Mesa").
fn precise_disk_sincos(angle:f32)->vec2<f32>{return precise_disk_sincos_pair(vec2<f32>(angle,0));}
// The same evaluation for a two-component angle (high part plus a small residual), so a caller's
// quadrant reduction keeps its residual (lane S2: principled_rotate lost one ULP of cos there).
fn precise_disk_sincos_pair(angle_pair:vec2<f32>)->vec2<f32>{
    let angle=angle_pair.x;if(angle==0.0&&angle_pair.y==0.0){return vec2<f32>(angle,1);}
    var x=angle_pair;let swap=angle>0.7853981633974483;
    if(swap){x=vec2<f32>(rounded_add(1.5707963705062866,-angle),rounded_add(-4.3711390001862426e-8,-angle_pair.y));}
    if(abs(x.x)<0.000244140625){return select(vec2<f32>(rounded_add(x.x,x.y),1),vec2<f32>(1,rounded_add(x.x,x.y)),swap);}
    let z=trig_mul(x,x);
    var s=vec2<f32>(-7.647163609812713e-13,-1.2200710471178288e-20);
    // 7 Horner steps as one loop over the same coefficients (same operations, same order).
    var s_terms=array<vec2<f32>,7>(
        vec2<f32>(1.6059044372074283e-10,-5.352526511562726e-18),
        vec2<f32>(-2.5052107943679403e-08,-4.4176230446483665e-16),
        vec2<f32>(2.7557318844628753e-06,3.793571224297229e-14),
        vec2<f32>(-0.00019841270113829523,2.725596874933456e-12),
        vec2<f32>(0.008333333767950535,-4.34617203337595e-10),
        vec2<f32>(-0.1666666716337204,4.967053879312289e-09),
        vec2<f32>(1.0,0.0));
    for(var step=0u;step<7u;step++){s=trig_coeff(s_terms[step],trig_mul(s,z));}
    var c=vec2<f32>(4.7794772561329454e-14,7.62544404448643e-22);
    // 8 Horner steps as one loop over the same coefficients (same operations, same order).
    var c_terms=array<vec2<f32>,8>(
        vec2<f32>(-1.147074536050896e-11,-2.372207689231238e-19),
        vec2<f32>(2.0876755879584152e-09,1.1082839809204342e-16),
        vec2<f32>(-2.755731998149713e-07,7.575112209051195e-15),
        vec2<f32>(2.4801587642286904e-05,-3.40699609366682e-13),
        vec2<f32>(-0.0013888889225199819,3.3631094437103215e-11),
        vec2<f32>(0.0416666679084301,-1.2417634698280722e-09),
        vec2<f32>(-0.5,0.0),
        vec2<f32>(1.0,0.0));
    for(var step=0u;step<8u;step++){c=trig_coeff(c_terms[step],trig_mul(c,z));}
    s=trig_mul(s,x);
    let sine=rounded_add(s.x,s.y);let cosine=rounded_add(c.x,c.y);
    return select(vec2<f32>(sine,cosine),vec2<f32>(cosine,sine),swap);
}
