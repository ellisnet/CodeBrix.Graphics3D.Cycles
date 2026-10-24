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
// Sine/cosine of angles in about [-pi, 2pi] (environment light, lane m1-lighting): quadrant reduction with a
// two-component pi/2 feeding the bounded precise adapter (the node-texture
// spherical_to_direction reduction). Driver sin/cos/acos differed from libm by up
// to 2e-3 relative on Intel.
fn trig_sincos_wide(angle:f32)->vec2<f32>{
    let n=i32(floor(angle*0.6366197723675814+0.5));let product=trig_product(f32(n),1.5707963705062866);
    let r=rounded_add(rounded_add(angle,-product.x),rounded_add(-product.y,f32(n)*4.3711390001862426e-8));
    let sc=precise_disk_sincos(r);let q=((n%4)+4)%4;
    if(q==1){return vec2<f32>(sc.y,-sc.x);}
    if(q==2){return vec2<f32>(-sc.x,-sc.y);}
    if(q==3){return vec2<f32>(-sc.y,sc.x);}
    return sc;
}
// Correctly rounded acosf for the environment light (glibc 2.41 acosf is correctly
// rounded): the triangle-light double-float arccosine, whose pi - acos(|x|) keeps the
// subtraction residual before the final rounding (motion_acos rounds the high part
// first and is one ULP off for about 9% of negative inputs).
fn trig_acos(value:f32)->f32{
    let absolute=abs(clamp(value,-1.0,1.0));let high=rounded_add(1.0,-absolute);
    let low=rounded_add(rounded_add(1.0,-high),-absolute);
    let x=vec2<f32>(high*.5,low*.5);let root=rounded_sqrt(x.x);
    if(root==0.0){return select(0.0,3.141592653589793,value<0.0);}
    let product=trig_product(root,root);
    let residual=rounded_add(rounded_add(x.x,-product.x),rounded_add(x.y,-product.y));
    let z=vec2<f32>(root,rounded_divide(residual,2.0*root));
    // One Horner step per iteration: the same operations in the same order as
    // the unrolled form, but one inlined copy instead of 29 per call site.
    var coefficients=array<vec2<f32>,29>(vec2<f32>(0.0018622264033183455,3.0847819958157441e-12),vec2<f32>(0.0019650335889309645,2.7346319219495596e-11),vec2<f32>(0.0020776609890162945,4.3501873110696199e-11),vec2<f32>(0.0022014740388840437,-6.5173905335058624e-11),vec2<f32>(0.0023380918428301811,4.9281793923494632e-11),vec2<f32>(0.0024894487578421831,-7.9595299535412112e-11),vec2<f32>(0.0026578705292195082,1.0898778190701619e-10),vec2<f32>(0.0028461783658713102,3.5237631822865323e-11),vec2<f32>(0.0030578216537833214,-4.5252907324155878e-12),vec2<f32>(0.0032970595639199018,-6.0446416969006656e-11),vec2<f32>(0.0035692052915692329,1.0225670180399882e-10),vec2<f32>(0.0038809645920991898,-3.3261520707844605e-11),vec2<f32>(0.0042409072630107403,-1.6933137704511214e-10),vec2<f32>(0.0046601435169577599,-3.004266366929409e-11),vec2<f32>(0.005153309553861618,1.2845828654267644e-10),vec2<f32>(0.0057400376535952091,1.7246714473229119e-11),vec2<f32>(0.0064472104422748089,-1.3038516013391677e-10),vec2<f32>(0.0073125259950757027,-1.2147685721913826e-10),vec2<f32>(0.0083903353661298752,4.4348693988005561e-10),vec2<f32>(0.0097616091370582581,3.9213582034292038e-10),vec2<f32>(0.011551801115274429,-2.1913472425527658e-10),vec2<f32>(0.013964843936264515,-1.8626451422920631e-10),vec2<f32>(0.017352763563394547,8.5968237736877562e-10),vec2<f32>(0.022372158244252205,8.4665688696028418e-10),vec2<f32>(0.0303819440305233,4.139211438880519e-10),vec2<f32>(0.0446428582072258,-1.0643686557121335e-09),vec2<f32>(0.075000002980232239,-2.9802322415450888e-09),vec2<f32>(0.1666666716337204,-4.9670537405344106e-09),vec2<f32>(1,0));
    var polynomial=vec2<f32>(0.0017680810997262597,2.0789158589051837e-11);
    for(var k=0u;k<29u;k++){polynomial=trig_coeff(coefficients[k],trig_mul(polynomial,x));}
    let result=2.0*trig_mul(z,polynomial);
    // Retain the subtraction residual before the final rounding. Rounding the
    // high subtraction first can move acos by one ULP and amplify triangle samples.
    if(value<0.0){let reduced=trig_coeff(vec2<f32>(3.1415927410125732,-8.742278000372485e-8),-result);return rounded_add(reduced.x,reduced.y);}
    return rounded_add(result.x,result.y);
}

// sin/cos of a reduced angle |x| <= pi/4 as double-float pairs (sin.hi, sin.lo,
// cos.hi, cos.lo): the precise_disk_sincos_pair series without the final rounding.
fn trig_sincos_series(x:vec2<f32>)->vec4<f32>{
    if(abs(x.x)<0.000244140625){let z=trig_mul(x,x);return vec4<f32>(x,trig_coeff(vec2<f32>(1.0,0.0),-0.5*z));}
    let z=trig_mul(x,x);
    var s=vec2<f32>(-7.647163609812713e-13,-1.2200710471178288e-20);
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
    return vec4<f32>(trig_mul(s,x),c);
}
// Knuth two-sum (any magnitudes): high = a + b rounded, low = its exact residual.
// high=rounded_add(a,b); bb=rounded_add(high,-a); the low part
// rounded_add(rounded_add(a,-rounded_add(high,-bb)),rounded_add(b,-bb)): the six
// sums through one body (rounded_steps, wavefront_float.wgsl), same operands.
// (trig_coeff keeps its three inlined sums: its loop form, together with the
// other loops, made the Adreno 650 compiler drop an SVM store in the Principled
// SVM probe, native/DEVICE.md "Surface entries under the Intel compile envelope".)
fn trig_two_sum(a:f32,b:f32)->vec2<f32>{
    var high=0.0;var bb=0.0;var t=0.0;var u=0.0;let count=rounded_steps(6u);
    for(var k=0u;k<count;k++){
        var x=a;var y=b;
        if(k==1u){x=high;y=-a;}else if(k==2u){x=high;y=-bb;}else if(k==3u){x=a;y=-t;}else if(k==4u){x=b;y=-bb;}else if(k==5u){x=t;y=u;}
        let r=rounded_add(x,y);
        if(k==0u){high=r;}else if(k==1u){bb=r;}else if(k==2u||k==3u||k==5u){t=r;}else if(k==4u){u=r;}
    }
    return vec2<f32>(high,t);
}
// Correctly rounded atan2f (glibc 2.41 is correctly rounded): the driver value
// theta0 refined by delta = (y cos theta0 - x sin theta0)/(x cos theta0 + y sin theta0),
// with sin/cos as double-float pairs and exact products, so the numerator keeps
// its cancellation; theta0 + delta is rounded once.
fn trig_atan2(y:f32,x:f32)->f32{
    let theta=atan2(y,x);if(y==0.0||x==0.0||theta!=theta){return theta;}
    let n=i32(floor(theta*0.6366197723675814+0.5));let product=trig_product(f32(n),1.5707963705062866);
    let head=trig_two_sum(theta,-product.x);
    let r=trig_two_sum(head.x,rounded_add(head.y,rounded_add(-product.y,f32(n)*4.3711390001862426e-8)));
    let sc=trig_sincos_series(r);let q=((n%4)+4)%4;
    var sn=sc.xy;var cs=sc.zw;
    if(q==1){sn=sc.zw;cs=-sc.xy;}else if(q==2){sn=-sc.xy;cs=-sc.zw;}else if(q==3){sn=-sc.zw;cs=sc.xy;}
    let a=trig_mul(vec2<f32>(y,0.0),cs);let b=trig_mul(vec2<f32>(x,0.0),sn);
    let numerator=rounded_add(rounded_add(a.x,-b.x),rounded_add(a.y,-b.y));
    let denominator=rounded_add(x*cs.x,y*sn.x);if(denominator==0.0){return theta;}
    let delta=rounded_divide(numerator,denominator);
    let sum=trig_coeff(vec2<f32>(theta,0.0),vec2<f32>(delta,0.0));return rounded_add(sum.x,sum.y);
}
