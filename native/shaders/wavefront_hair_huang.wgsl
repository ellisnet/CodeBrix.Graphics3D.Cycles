// SPDX-FileCopyrightText: 2023 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles closure/bsdf_principled_hair_huang.h, the numeric helpers it uses from
// closure/bsdf_microfacet.h, closure/bsdf_util.h, closure/bsdf_principled_hair_chiang.h, util/math_fast.h,
// util/lookup_table.h and sample/lcg.h: evaluation and sampling of the Principled Hair BSDF's Huang model on
// the surface stage's canonical closure record.
//
// The record (svm_hair_huang, wavefront_svm_hair_huang.wgsl) holds HuangHairBSDF and HuangHairExtra in one slot:
// roughness: roughness; a: tilt; b: aspect_ratio; ior: eta; multiscatter: sigma; alpha_x: h; alpha_y: pixel_coverage;
// energy_scale: radius; T: Y; reflection_tint: Z; transmission_tint: R, TT, TRT; fresnel_f0: the index of the extra slot
// the CPU allocates (closure_alloc_extra, from the end of the array). extra->wi is to_local(sd->wi), recomputed from the
// unchanged sd.wi; extra->e2 is 1 - aspect_ratio^2; extra->h, which only the evaluation that writes it reads, is passed
// along. The extra slot holds the results below (HH_X_*).
//
// Entries (compiler pressure; each closure's evaluation is several hundred kilobytes of inlined WGSL and sits next to
// nothing else). shade_surface_huang_eval runs twice per stage: before connect_surface_light for the sampled light's
// direction and after shade_surface_sample for the other closures of the MIS sum. shade_surface_huang_sample runs before
// shade_surface_sample. Each stores its results in the extra slot and the surface entries read them instead of calling
// the closure, in the same places. The evaluation before connect_surface_light is speculative: its results and the
// random number position it reached are used only if connect_surface_light reaches the closure.
//
// Arithmetic. Directions and everything a branch or the LCG's position depends on is bit-exact on every driver:
// products go through rounded_add before they are summed (no fused multiply-add), quotients and square roots through
// rounded_divide and rounded_sqrt, and the arcs through trig_arc, trig_atan2_guarded. The driver's sqrt, division
// and elementary functions are not used. Compiler pressure: every helper here runs its sums through one rounded_add
// body in a loop whose bound is opaque (rounded_steps), and the Simpson loops keep values that do not change
// outside the loop. There are no arrays: lookups walk their corners with scalars, and exp's coefficients come from
// select chains. The Simpson loops have a constant bound: with roughness at its floor of 0.001, 2 * ceil(1.998 /
// 0.0007 * 0.5) = 2856 intervals.
//#if SVM_BSDF_HAIR_HUANG
const HH_MAX_INTERVALS:u32=2856u;
// sd->lcg_state of the closure's evaluations and samples, in the order the CPU kernel takes them. The entries
// load it from the stage record and store it back (surface_stage_write, surf_sample_store's caller).
var<private> huang_lcg:u32;
// lcg_step_float: (float)rng * (1.0f / (float)0xFFFFFFFF), which is rng * 2^-32 rounded to nearest even.
fn huang_random()->f32{
    huang_lcg=1103515245u*huang_lcg+12345u;let x=huang_lcg;if(x==0u){return 0.0;}
    let lz=countLeadingZeros(x);let m=x<<lz;var mantissa=m>>8u;let tail=m&255u;
    if(tail>128u||(tail==128u&&(mantissa&1u)!=0u)){mantissa++;}
    return bitcast<f32>(((126u-lz)<<23u)+(mantissa-0x800000u));
}
// fast_sincosf (util/math_fast.h), the same polynomials as fast_sinf and fast_cosf. Every madd through one rounded_add.
fn huang_sincos(angle:f32)->vec2<f32>{
    let qf=round(angle*0.318309886183790671538);let odd=(i32(qf)&1)!=0;
    var x=angle;var crush=0.0;let reduce=rounded_steps(6u);
    for(var k=0u;k<reduce;k++){
        var m=qf*bitcast<f32>(select(select(select(0xacb4611au,0xb4222000u,k==2u),0xba7da000u,k==1u),0xc0490000u,k==0u));var y=x;
        if(k==4u){m=bitcast<f32>(0x3fc90fdbu);y=-x;}else if(k==5u){m=bitcast<f32>(0x3fc90fdbu);y=-crush;}
        let r=rounded_add(m,y);if(k==4u){crush=r;}else if(k<6u){x=r;}
    }
    let s=x*x;if(odd){x=-x;}
    var su=2.6083159809786593541503e-06;var cu=-2.71811842367242206819355e-07;let poly=rounded_steps(9u);
    for(var k=0u;k<poly;k++){
        var a=su*s;var c=select(select(-0.166666597127914428710938,0.00833307858556509017944336,k==1u),-0.0001981069071916863322258,k==0u);
        if(k==3u){a=s*(su*x);c=x;}
        else if(k>3u){a=cu*s;c=select(select(select(select(1.0,-0.5,k==7u),0.0416666641831398010253906,k==6u),-0.00138888787478208541870117,k==5u),2.47990446951007470488548e-05,k==4u);}
        let r=rounded_add(a,c);if(k<4u){su=r;}else if(k<9u){cu=r;}
    }
    if(odd){cu=-cu;}
    return vec2<f32>(clamp(su,-1.0,1.0),clamp(cu,-1.0,1.0));
}
// exp(float3): expf per lane as the BSDF families' adapter (k ln2 + r in double-float binary32, Taylor degree 10, one
// rounding), its coefficients in a select chain, through one body.
fn huang_exp3(v:vec3<f32>)->vec3<f32>{
    var out=vec3<f32>(0);let lanes=rounded_steps(3u);
    for(var lane=0u;lane<lanes;lane++){
        let x=select(select(v.z,v.y,lane==1u),v.x,lane==0u);var value=0.0;
        if(x>88.72284){value=bitcast<f32>(0x7f800000u);}
        else if(x>= -103.97208){
            let k=round(x*1.4426950408889634);let p=trig_product(k,0.6931471824645996);let head=trig_two_sum(x,-p.x);
            let r=vec2<f32>(head.x,rounded_add(rounded_add(head.y,-p.y),k*1.904654323148236e-09));
            var polynomial=vec2<f32>(2.755731998149713e-07,-7.575112209051195e-15);let terms=rounded_steps(10u);
            for(var i=0u;i<terms;i++){
                var c=vec2<f32>(1.0,0.0);
                if(i==0u){c=vec2<f32>(2.7557318844628753e-06,3.793571224297229e-14);}else if(i==1u){c=vec2<f32>(2.4801587642286904e-05,-3.40699609366682e-13);}
                else if(i==2u){c=vec2<f32>(0.00019841270113829523,-2.725596874933456e-12);}else if(i==3u){c=vec2<f32>(0.0013888889225199819,-3.3631094437103215e-11);}
                else if(i==4u){c=vec2<f32>(0.008333333767950535,-4.34617203337595e-10);}else if(i==5u){c=vec2<f32>(0.0416666679084301,-1.2417634698280722e-09);}
                else if(i==6u){c=vec2<f32>(0.1666666716337204,-4.967053879312289e-09);}else if(i==7u){c=vec2<f32>(0.5,0.0);}
                polynomial=trig_coeff(c,trig_mul(polynomial,r));
            }
            value=ldexp(rounded_add(polynomial.x,polynomial.y),i32(k));
        }
        out=select(out,vec3<f32>(value),vec3<u32>(0u,1u,2u)==vec3<u32>(lane));
    }
    return out;
}
// sinf and cosf (glibc's are within one ULP of correctly rounded): the double-float series of the two-part reduced angle,
// each rounded once.
fn huang_sincos_exact(angle:f32)->vec2<f32>{
    let n=i32(floor(angle*0.6366197723675814+0.5));let product=trig_product(f32(n),1.5707963705062866);
    let head=trig_two_sum(angle,-product.x);let r=trig_two_sum(head.x,rounded_add(head.y,rounded_add(-product.y,f32(n)*4.3711390001862426e-8)));
    let sc=select(trig_sincos_series(r),vec4<f32>(r,1.0,0.0),abs(r.x)<1e-6);let q=((n%4)+4)%4;var sn=sc.xy;var cs=sc.zw;
    if(q==1){sn=sc.zw;cs=-sc.xy;}else if(q==2){sn=-sc.xy;cs=-sc.zw;}else if(q==3){sn=-sc.zw;cs=sc.xy;}
    return vec2<f32>(rounded_add(sn.x,sn.y),rounded_add(cs.x,cs.y));
}
// Reassociation fence: a product with a constant factor may otherwise be regrouped by the driver (Mesa reorders
// constant factors in a chain of inexact multiplications), so pi * sqr(v) must stay one rounding of each product. The
// header word 3 after the error word is always 0; no compiler sees through the XOR.
fn huang_fence3(x:vec3<f32>)->vec3<f32>{return bitcast<vec3<f32>>(bitcast<vec3<u32>>(x)^vec3<u32>(load_word(p(17u)+12u)));}
fn huang_fence(x:f32)->f32{return bitcast<f32>(bitcast<u32>(x)^load_word(p(17u)+12u));}
// Local frame helpers (the file's "Hair coordinate system utils").
fn huang_cos_theta(w:vec3<f32>)->f32{return rounded_sqrt(max(rounded_add(w.x*w.x,w.z*w.z),0.0));}
// dir_phi(w) and, for an ellipse, to_phi(dir_phi(w), b): (phi, to_phi), one arctangent body.
fn huang_phi(w:vec3<f32>,b:f32)->vec2<f32>{
    var y=w.x;var x=w.z;var out=vec2<f32>(0);let count=rounded_steps(2u);
    for(var k=0u;k<count;k++){
        let angle=trig_atan2_guarded(y,x);
        if(k==0u){out=vec2<f32>(angle,angle);if(b==1.0){break;}let sc=huang_sincos(angle);y=b*sc.x;x=sc.y;}else if(k==1u){out.y=angle;}
    }
    return out;
}
// The sine and cosine of phi for the ellipse's gamma (sphg_dir), from gamma's. (sin_gamma, cos_gamma, sin_phi, cos_phi).
fn huang_gamma(gamma:f32,b:f32)->vec4<f32>{
    let g=huang_sincos(gamma);if(b==1.0||abs(g.y)<1e-6){return vec4<f32>(g,g);}
    let tan_phi=b*rounded_divide(g.x,g.y);let square=rounded_add(tan_phi*tan_phi,1.0);
    let cos_phi=select(1.0,-1.0,g.y<0.0)*rounded_divide(1.0,rounded_sqrt(square));
    return vec4<f32>(g,cos_phi*tan_phi,cos_phi);
}
// bsdf_lambda<GGX> of three cosines, one body.
fn huang_lambdas(alpha2:f32,cosines:vec3<f32>)->vec3<f32>{
    var out=vec3<f32>(0);let count=rounded_steps(3u);
    for(var k=0u;k<count;k++){let cosine=select(select(cosines.z,cosines.y,k==1u),cosines.x,k==0u);
        let tangent=max(rounded_add(rounded_divide(1.0,cosine*cosine),-1.0),0.0);
        let lambda=0.5*rounded_add(rounded_sqrt(rounded_add(1.0,alpha2*tangent)),-1.0);
        out=select(out,vec3<f32>(lambda),vec3<u32>(0u,1u,2u)==vec3<u32>(k));}
    return out;
}
// bsdf_Go(alpha2, cos_i, cos_a) and bsdf_Go(alpha2, cos_i, cos_b), or bsdf_G(alpha2, cos_i, cos_a) for `single`.
fn huang_Go2(alpha2:f32,cos_i:f32,cos_a:f32,cos_b:f32)->vec2<f32>{
    let l=huang_lambdas(alpha2,vec3<f32>(cos_i,cos_a,cos_b));let a=rounded_add(1.0,l.x);
    var out=vec2<f32>(0);let count=rounded_steps(2u);
    for(var k=0u;k<count;k++){let q=rounded_divide(a,rounded_add(a,select(l.z,l.y,k==0u)));if(k==0u){out.x=q;}else if(k==1u){out.y=q;}}
    return out;
}
// bsdf_D<GGX> and bsdf_G<GGX>(alpha2, cos_a, cos_b): (D, G).
fn huang_DG(alpha2:f32,cos_h:f32,cos_a:f32,cos_b:f32)->vec2<f32>{
    let z=min(cos_h*cos_h,1.0);let v=rounded_add(rounded_add(1.0,-z),alpha2*z);
    let l=huang_lambdas(alpha2,vec3<f32>(cos_a,cos_b,cos_b));let sum=rounded_add(rounded_add(1.0,l.x),l.y);
    var out=vec2<f32>(0);let count=rounded_steps(2u);
    for(var k=0u;k<count;k++){let q=rounded_divide(select(1.0,alpha2,k==0u),select(sum,3.14159265358979323846*huang_fence(v*v),k==0u));if(k==0u){out.x=q;}else if(k==1u){out.y=q;}}
    return out;
}
// fresnel_dielectric: (F, cos_theta_t); cos_theta_t is left at 0 on total internal reflection (the CPU leaves its
// variable unset; every use of it is behind T > 0).
fn huang_fresnel(cosine:f32,eta:f32)->vec2<f32>{
    let square=rounded_add(eta*eta,-rounded_add(1.0,-(cosine*cosine)));if(square<=0.0){return vec2<f32>(1.0,0.0);}
    let ci=abs(cosine);let ct=-rounded_divide(rounded_sqrt(square),eta);let a=eta*ct;let d=eta*ci;
    let rs=rounded_divide(rounded_add(ci,a),rounded_add(ci,-a));let rp=rounded_divide(rounded_add(ct,d),rounded_add(d,-ct));
    return vec2<f32>(rounded_add(rs*rs,rp*rp)*0.5,ct);
}
// fresnel_dielectric_cos.
fn huang_fresnel_cos(cosine:f32,eta:f32)->f32{
    let c=abs(cosine);let square=rounded_add(rounded_add(eta*eta,-1.0),c*c);if(!(square>0.0)){return 1.0;}
    let g=rounded_sqrt(square);let sum=rounded_add(g,c);let difference=rounded_add(g,-c);let a=rounded_divide(difference,sum);
    let d=rounded_divide(rounded_add(c*sum,-1.0),rounded_add(c*difference,1.0));
    return ((0.5*a)*a)*rounded_add(1.0,d*d);
}
// refract_angle: (inv_eta * dot(normal, incident) + cos_theta_t) * normal - inv_eta * incident, with dot(normal, incident) taken.
fn huang_refract(incident:vec3<f32>,normal:vec3<f32>,cos_normal:f32,cos_theta_t:f32,inv_eta:f32)->vec3<f32>{
    return rounded_add3(rounded_add(inv_eta*cos_normal,cos_theta_t)*normal,-(inv_eta*incident));
}
// lookup_table_read_3D (LUT3_RES_ROUGH x LUT3_RES_MU x LUT3_RES_IOR) of the energy compensation table: the eight corners with scalars, each
// interpolation (1 - t) * a + t * b through one rounded_add body. A corner with t == 0 weighs 0: the same value.
fn huang_table(x:f32,y:f32,z:f32,offset:u32)->f32{
    let count=table_count(G_lookup_table);let slice=LUT3_RES_ROUGH*LUT3_RES_MU;if(offset>count||slice*LUT3_RES_IOR>count-offset){fail();return 0.0;}
    let mx=LUT3_RES_ROUGH-1u;let my=LUT3_RES_MU-1u;let mz=LUT3_RES_IOR-1u;
    let vx=clamp(x,0.0,1.0)*f32(mx);let vy=clamp(y,0.0,1.0)*f32(my);let vz=clamp(z,0.0,1.0)*f32(mz);
    let ix=min(u32(vx),mx);let iy=min(u32(vy),my);let iz=min(u32(vz),mz);
    let t=vec3<f32>(vx-f32(ix),vy-f32(iy),vz-f32(iz));
    var held=vec3<f32>(0);var result=0.0;let corners=rounded_steps(8u);let levels=rounded_steps(3u);
    for(var corner=0u;corner<corners;corner++){
        let at=offset+slice*min(iz+(corner>>2u),mz)+LUT3_RES_ROUGH*min(iy+((corner>>1u)&1u),my)+min(ix+(corner&1u),mx);
        var value=heap_float(table_at(G_lookup_table,at,SIZE_lookup_table));var level=0u;var open=true;
        for(var step=0u;step<levels&&open;step++){
            // This axis' bit: 0 holds the value for its pair, 1 completes the pair.
            let axis=select(select(t.z,t.y,level==1u),t.x,level==0u);let bit=(corner>>level)&1u;
            if(bit==0u){if(level==0u){held.x=value;}else if(level==1u){held.y=value;}else{held.z=value;}open=false;}
            else{let first=select(select(held.z,held.y,level==1u),held.x,level==0u);
                value=rounded_add(rounded_add(1.0,-axis)*first,axis*value);level++;}
        }
        if(open){result=value;}
    }
    return result;
}
// bsdf_hair_huang_energy_scale: `rough` is sqrtf(roughness), z and offset the eta's.
fn huang_energy(rough:f32,mu:f32,z:f32,offset:u32)->f32{return rounded_divide(1.0,huang_table(rough,rounded_sqrt(max(mu,0.0)),z,offset));}
fn huang_energy_z(eta:f32)->f32{return rounded_sqrt(abs(rounded_divide(rounded_add(eta,-1.0),rounded_add(eta,1.0))));}
fn huang_energy_table(eta:f32)->u32{return data_word(select(HH_D_ggx_glass_E,HH_D_ggx_glass_inv_E,eta<1.0));}
// native_disk_sincos with its Horner steps through one body (the same operations in the same order).
fn huang_disk_sincos(angle:f32)->vec2<f32>{
    if(angle==0.0){return vec2<f32>(angle,1);}
    var n=0;
    if(angle>=0.7853981633974483){n=1;}
    if(angle>=2.356194490192345){n=2;}
    if(angle<=-0.7853981633974483){n=-1;}
    if(angle<=-2.356194490192345){n=-2;}
    var x=vec2<f32>(angle,0);
    if(n!=0){x=vec2<f32>(rounded_add(angle,-f32(n)*1.5707963705062866),f32(n)*4.3711390001862426e-8);}
    var sine=rounded_add(x.x,x.y);var cosine=1.0;
    if(abs(x.x)>=0.000244140625){
        let z=trig_mul(x,x);var u=vec2<f32>(-0.0001951729936990887,3.885231546207901e-12);var sn=vec2<f32>(0);let count=rounded_steps(7u);
        for(var k=0u;k<count;k++){
            var c=vec2<f32>(1.0,0.0);
            if(k==0u){c=vec2<f32>(0.008332177996635437,1.4950340965214082e-10);}else if(k==1u){c=vec2<f32>(-0.16666655242443085,2.9874200890844804e-09);}
            else if(k==3u){c=vec2<f32>(-0.0013886763481423259,-3.129527886835959e-11);u=vec2<f32>(2.4390450562350452e-05,1.4121409391224665e-13);}
            else if(k==4u){c=vec2<f32>(0.04166662320494652,1.1939799149374153e-10);}else if(k==5u){c=vec2<f32>(-0.5,2.7489177600870107e-09);}
            let r=trig_coeff(c,trig_mul(z,u));if(k==2u){sn=r;}if(k<7u){u=r;}
        }
        sn=trig_mul(x,sn);sine=rounded_add(sn.x,sn.y);cosine=rounded_add(u.x,u.y);
    }
    if(n==1){return vec2<f32>(cosine,-sine);}
    if(n==2||n== -2){return vec2<f32>(-sine,-cosine);}
    if(n== -1){return vec2<f32>(-cosine,sine);}
    return vec2<f32>(sine,cosine);
}
// sample_uniform_disk, as ggx_disk_sample.
fn huang_disk(r:vec2<f32>)->vec2<f32>{
    let a=rounded_add(2.0*r.x,-1.0);let b=rounded_add(2.0*r.y,-1.0);
    if(a==0.0&&b==0.0){return vec2<f32>(0);}
    var radius:f32;var phi:f32;
    if(a*a>b*b){radius=a;phi=(PI*0.25)*rounded_divide(b,a);}
    else{radius=b;phi=rounded_add(PI*0.5,-(PI*0.25)*rounded_divide(a,b));}
    let sc=huang_disk_sincos(phi);return vec2<f32>(radius*sc.y,radius*sc.x);
}
// sample_wh: make_orthonormals(wm), to_local, microfacet_ggx_sample_vndf, to_global.
fn huang_sample_wh(roughness:f32,wi:vec3<f32>,wm:vec3<f32>,random:vec2<f32>)->vec3<f32>{
    var a=vec3<f32>(wm.z-wm.y,wm.x-wm.z,wm.y-wm.x);if(wm.x==wm.y&&wm.x==wm.z){a=vec3<f32>(wm.z-wm.y,wm.x+wm.z,-wm.y-wm.x);}
    let s=surface_native_normalize(a);let t=rounded_cross(wm,s);
    let local=vec3<f32>(rounded_dot(wi,s),rounded_dot(wi,t),rounded_dot(wi,wm));
    let h=ggx_vndf_disk(local,roughness,roughness,huang_disk(random));
    return rounded_add3(rounded_add3(h.x*s,h.y*t),h.z*wm);
}
fn huang_is_finite(v:vec3<f32>)->bool{return all((bitcast<vec3<u32>>(v)&vec3<u32>(0x7f800000u))!=vec3<u32>(0x7f800000u));}
// arc_length from sinf(gamma).
fn huang_arc(e2:f32,s:f32)->f32{if(e2==0.0){return 1.0;}return rounded_sqrt(rounded_add(1.0,-(e2*(s*s))));}
// float3 / float3 per lane.
fn huang_div_lanes(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{
    var out=vec3<f32>(0);let count=rounded_steps(3u);
    for(var k=0u;k<count;k++){
        let q=rounded_divide(select(select(a.z,a.y,k==1u),a.x,k==0u),select(select(b.z,b.y,k==1u),b.x,k==0u));
        out=select(out,vec3<f32>(q),vec3<u32>(0u,1u,2u)==vec3<u32>(k));
    }
    return out;
}
// bsdf_hair_huang_eval_trrt.
fn huang_trrt(T:f32,R:f32,A:vec3<f32>)->vec3<f32>{
    let T_avg=max(rounded_add(1.0,-R),1e-5);let inner=rounded_add(1.0,-T_avg);
    return huang_div_lanes(((((T*(R*R))*T_avg)*A)*A)*A,rounded_add3(vec3<f32>(1),-(A*inner)));
}
// What is constant over one closure's evaluation or sample.
struct HuangFrame {b:f32,e2:f32,roughness:f32,alpha2:f32,sqrt_roughness:f32,eta:f32,inv_eta:f32,sin_tilt:f32,cos_tilt:f32,z:f32,z_inv:f32,table:u32,table_inv:u32,base:f32,sincos_phi_i:vec2<f32>}
fn huang_frame(c:SvmClosure,wi:vec3<f32>,cos_i:f32,base:f32)->HuangFrame{
    var f:HuangFrame;f.b=c.b;f.e2=rounded_add(1.0,-(c.b*c.b));f.roughness=c.roughness;f.alpha2=c.roughness*c.roughness;f.sqrt_roughness=rounded_sqrt(c.roughness);
    f.eta=c.ior;f.inv_eta=rounded_divide(1.0,c.ior);let st=huang_sincos(c.a);f.sin_tilt=st.x;f.cos_tilt=st.y;
    f.z=huang_energy_z(f.eta);f.z_inv=huang_energy_z(f.inv_eta);f.table=huang_energy_table(f.eta);f.table_inv=huang_energy_table(f.inv_eta);
    // base: atan2f(wi.z, -b * wi.x) of h_to_gamma, taken by the caller. sincos_phi(wi): (w.x / c, w.z / c).
    f.base=base;
    f.sincos_phi_i=vec2<f32>(rounded_divide(wi.x,cos_i),rounded_divide(wi.z,cos_i));
    return f;
}
// h_to_gamma: one arc body.
fn huang_h_to_gamma(h:f32,b:f32,base:f32)->f32{
    let arc=trig_arc(select(-h,h,b==1.0),b!=1.0);
    return select(rounded_add(base,-arc),-arc,b==1.0);
}
// d_gamma_d_h from gamma's sine and cosine.
fn huang_jacobian(sincos_phi_i:vec2<f32>,g:vec4<f32>,b:f32)->f32{
    let d=select(rounded_add(sincos_phi_i.y*g.y,(b*sincos_phi_i.x)*g.x),g.y,b==1.0);
    return select(0.0,rounded_divide(1.0,d),d!=0.0);
}
// Simpson's weight of interval point i, before the Jacobian.
fn huang_simpson(i:u32,intervals:u32)->f32{return select(f32(i%2u+1u),0.5,i==0u||i==intervals);}
// 2 * (size_t)ceilf(h_range / res * 0.5f).
fn huang_intervals(h_range:f32,res:f32)->u32{return min(2u*u32(ceil(rounded_divide(h_range,res)*0.5)),HH_MAX_INTERVALS);}
// The distance between two points of the ellipse, to_point(gamma, b), from their sines and cosines.
fn huang_point_distance(a:vec2<f32>,c:vec2<f32>,b:f32)->f32{
    let dx=rounded_add(a.x,-c.x);let dy=rounded_add(b*a.y,-(b*c.y));
    return rounded_sqrt(rounded_add(dx*dx,dy*dy));
}
// bsdf_hair_huang_eval_r over the valid interval.
fn huang_eval_r(c:SvmClosure,f:HuangFrame,wi:vec3<f32>,wo:vec3<f32>,h_min:f32,h_max:f32)->vec3<f32>{
    let R=c.transmission_tint.x;if(R<=0.0){return vec3<f32>(0);}
    let b=f.b;let wh=surface_native_normalize(wi+wo);
    let h_range=rounded_add(h_max,-h_min);let intervals=huang_intervals(h_range,f.roughness*0.7);let res=rounded_divide(h_range,f32(intervals));
    let hi=rounded_dot(wi,wh);let ho=rounded_dot(wo,wh);var integral=0.0;
    if(hi>0.0&&ho>0.0){for(var i=0u;i<=intervals&&i<=HH_MAX_INTERVALS;i++){
        let h=rounded_add(h_min,f32(i)*res);let h_gamma=huang_h_to_gamma(h,b,f.base);let g=huang_gamma(h_gamma,b);
        let wm=vec3<f32>(g.z*f.cos_tilt,f.sin_tilt,g.w*f.cos_tilt);let flat_m=vec3<f32>(wm.x,0.0,wm.z);
        if(rounded_dot(wi,flat_m)>0.0&&rounded_dot(wo,flat_m)>0.0){
            let weight=huang_simpson(i,intervals)*huang_jacobian(f.sincos_phi_i,g,b);
            let cos_mi=rounded_dot(wm,wi);let dg=huang_DG(f.alpha2,rounded_dot(wm,wh),cos_mi,rounded_dot(wm,wo));
            var s=0.0;if(f.e2!=0.0){s=huang_sincos_exact(h_gamma).x;}
            let term=(((weight*dg.x)*dg.y)*huang_arc(f.e2,s))*huang_energy(f.sqrt_roughness,cos_mi,f.z,f.table);
            integral=rounded_add(integral,term);
        }
    }}
    integral=integral*huang_fence((2.0/3.0)*res);
    return vec3<f32>(((R*0.25)*huang_fresnel_cos(hi,f.eta))*integral);
}
// bsdf_hair_huang_eval_residual over the valid interval. The two segments inside the strand share one body: pass 0 is the
// first interface and the TT exit (wt, gamma_mt, wmt), pass 1 the internal reflection and the TRT exit (wtr, gamma_mtr,
// wmtr). The pinned function's `continue`s end the pass loop; what they skip has no side effect but the random numbers,
// which are drawn in the same order.
fn huang_eval_residual(c:SvmClosure,f:HuangFrame,wi:vec3<f32>,wo:vec3<f32>,h_min:f32,h_max:f32)->vec3<f32>{
    let TT=c.transmission_tint.y;let TRT=c.transmission_tint.z;if(TT<=0.0&&TRT<=0.0){return vec3<f32>(0);}
    let b=f.b;let eta=f.eta;let inv_eta=f.inv_eta;let mu_a=c.multiscatter;let limit=rounded_add(inv_eta,-1e-5);
    let h_range=rounded_add(h_max,-h_min);let intervals=huang_intervals(h_range,f.roughness*0.8);let res=rounded_divide(h_range,f32(intervals));
    var S_tt=vec3<f32>(0);var S_trt=vec3<f32>(0);var S_trrt=vec3<f32>(0);let passes=rounded_steps(2u);
    for(var i=0u;i<=intervals&&i<=HH_MAX_INTERVALS;i++){
        let h=rounded_add(h_min,f32(i)*res);let gamma_mi=huang_h_to_gamma(h,b,f.base);let gi=huang_gamma(gamma_mi,b);
        var v=wi;var m=vec3<f32>(gi.z*f.cos_tilt,f.sin_tilt,gi.w*f.cos_tilt);var m_flat=vec3<f32>(gi.z,0.0,gi.w);var gamma=gamma_mi;var g=gi;
        var wt=vec3<f32>(0);var phi_t=vec2<f32>(0);var A_t=vec3<f32>(0);var weight=0.0;var T1=0.0;var scale1=0.0;var scale2=0.0;var cos_mi1=0.0;var cos_mo1=0.0;var G1o=0.0;
        for(var layer=0u;layer<passes;layer++){
            let first=layer==0u;if(!first&&!(TRT>0.0)){break;}
            // make_float2(lcg_step_float, lcg_step_float): the order of the two draws is the oracle compiler's. GCC for x86-64
            // evaluates the second argument first, GCC for AArch64 (the scalar ARM64 oracle, NATIVE_FLOAT3_RECIPROCAL_NORMALIZE) the first.
            let d0=huang_random();let d1=huang_random();let first_x=NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u;
            let r0=select(d1,d0,first_x);let r1=select(d0,d1,first_x);
            let wh=huang_sample_wh(f.roughness,v,m,vec2<f32>(r0,r1));let cos_h=rounded_dot(v,wh);if(!(cos_h>0.0)){break;}
            let cos_m=rounded_dot(v,m);var next=vec3<f32>(0);var R2=0.0;
            if(first){let fresnel=huang_fresnel(cos_h,eta);T1=rounded_add(1.0,-fresnel.x);scale1=huang_energy(f.sqrt_roughness,cos_m,f.z,f.table);next=huang_refract(wi,wh,cos_h,fresnel.y,inv_eta);}
            else{
                R2=huang_fresnel_cos(cos_h,inv_eta);
                // -reflect(wt, wh2), with dot(wt, wh2) = -cos_h.
                next=rounded_add3((2.0*wh)*(-cos_h),-wt);
            }
            let back=-next;let total_internal=!first&&rounded_dot(back,wo)<limit;
            var trrt=!first;
            if(!total_internal){
                let cos_mo=rounded_dot(back,m);
                // microfacet_visible(v, -next, m, wh) && microfacet_visible(v, -next, m_flat, wh).
                if(!(cos_m>0.0&&rounded_dot(back,wh)>0.0&&cos_mo>0.0&&rounded_dot(v,m_flat)>0.0&&rounded_dot(back,m_flat)>0.0)){break;}
                let phi=huang_phi(next,b);
                let gamma_next=select(rounded_add(rounded_add(gamma_mi,-(2.0*rounded_add(phi_t.y,-phi.y))),3.14159265358979323846),rounded_add(2.0*phi.y,-gamma_mi),first);
                let gn=huang_gamma(gamma_next,b);
                let m_next=vec3<f32>(gn.z*f.cos_tilt,-f.sin_tilt,gn.w*f.cos_tilt);let m_next_flat=vec3<f32>(gn.z,0.0,gn.w);
                let G_out=huang_Go2(f.alpha2,cos_m,cos_mo,cos_mo).x;
                // The segment's absorption.
                // libm's cosf of the chord angle (circular) and sinf(gamma_next) of arc_length (ellipse), one body.
                var cos_chord=0.0;var sin_next=0.0;let exact=rounded_steps(2u);
                for(var k=0u;k<exact;k++){
                    if((k==0u&&b!=1.0)||(k==1u&&f.e2==0.0)){continue;}
                    let v=huang_sincos_exact(select(gamma_next,select(rounded_add(phi.x,-gamma),rounded_add(gamma_mi,-phi.x),first),k==0u));
                    if(k==0u){cos_chord=v.y;}else{sin_next=v.x;}
                }
                var path=0.0;
                if(b==1.0){path=select(-(2.0*abs(cos_chord)),2.0*cos_chord,first);}
                else{path=-huang_point_distance(select(gn.xy,gi.xy,first),select(g.xy,huang_sincos(rounded_add(gamma_next,3.14159265358979323846)),first),b);}
                let A_segment=huang_exp3(surf_native_div3(mu_a,huang_cos_theta(next))*path);
                if(first){wt=next;phi_t=phi;A_t=A_segment;weight=huang_simpson(i,intervals)*huang_jacobian(f.sincos_phi_i,gi,b);cos_mi1=cos_m;cos_mo1=cos_mo;G1o=G_out;}
                // The exit through (exit_in, m_next).
                let exit_in=select(next,back,first);let cos_mi_exit=rounded_dot(exit_in,m_next);
                let scale_next=huang_energy(f.sqrt_roughness,cos_mi_exit,f.z_inv,f.table_inv);
                let lobe=select(TRT,TT,first);
                if(lobe>0.0&&(!first||rounded_dot(wo,next)>=limit)){
                    var wh_exit=rounded_add3(exit_in,inv_eta*wo);let rcp=rounded_divide(1.0,rounded_sqrt(rounded_dot(wh_exit,wh_exit)));wh_exit=wh_exit*rcp;
                    let cos_mh=rounded_dot(m_next,wh_exit);let cos_hi=rounded_dot(exit_in,wh_exit);let cos_ho=rounded_dot(-wo,wh_exit);let cos_mo_exit=rounded_dot(-wo,m_next);
                    var open=cos_mh>=0.0;
                    if(!first&&open){
                        // microfacet_visible(wtr, -wo, wmtr, wh3) && microfacet_visible(wtr, -wo, wmtr_, wh3).
                        open=cos_hi>0.0&&cos_mi_exit>0.0&&cos_ho>0.0&&cos_mo_exit>0.0&&rounded_dot(exit_in,m_next_flat)>0.0&&rounded_dot(-wo,m_next_flat)>0.0;
                    }
                    if(open){
                        let T=rounded_add(1.0,-huang_fresnel_cos(cos_hi,inv_eta))*scale_next;let dg=huang_DG(f.alpha2,cos_mh,cos_mi_exit,cos_mo_exit);
                        var scalar=(weight*T1)*scale1;if(!first){scalar=(scalar*R2)*scale2;}
                        scalar=((scalar*T)*dg.x)*G1o;if(!first){scalar=scalar*G_out;}scalar=scalar*dg.y;
                        var A=scalar*A_t;if(!first){A=A*A_segment;}
                        var result=surf_native_div3(A,select(cos_mo1*cos_mo,cos_mo1,first))*cos_mi1;if(!first){result=result*cos_m;}
                        result=((result*cos_hi)*cos_ho)*(rcp*rcp);
                        if(huang_is_finite(result)){let sum=rounded_add3(select(S_trt,S_tt,first),(lobe*result)*huang_arc(f.e2,sin_next));if(first){S_tt=sum;}else{S_trt=sum;}}
                    }
                }
                if(first){scale2=scale_next;v=back;m=m_next;m_flat=m_next_flat;gamma=gamma_next;g=gn;}
            }
            if(trrt){S_trrt=rounded_add3(S_trrt,weight*huang_trrt(T1,R2,A_t));}
        }
    }
    let M=huang_longitudinal(wi.y,huang_cos_theta(wi),wo.y,huang_cos_theta(wo),4.0*f.roughness);
    let tail=huang_fence3(huang_fence3(S_trrt*M)*0.15915494309189535)*0.6366197723675814;
    return rounded_add3(rounded_add3(S_tt,S_trt)*(inv_eta*inv_eta),tail)*huang_fence((2.0/3.0)*res);
}
//#endif
//#if SVM_BSDF_HAIR_HUANG
// bessel_I0 (closure/bsdf_principled_hair_chiang.h).
fn huang_bessel_I0(x_in:f32)->f32{
    let x=x_in*x_in;var value=rounded_add(1.0,0.25*x);var power=x*x;
    for(var i=2u;i<10u;i++){
        // pow_4_i * i_fac_2 as a binary32 divisor.
        let divisor=select(select(select(select(select(select(select(34519618525593600.0,106542032486400.0,i==8u),416179814400.0,i==7u),2123366400.0,i==6u),14745600.0,i==5u),147456.0,i==4u),2304.0,i==3u),64.0,i==2u);
        let next=rounded_add(value,rounded_divide(power,divisor));if(value==next){return value;}
        value=next;power*=x;
    }
    return value;
}
//#endif
//#if SVM_BSDF_HAIR_HUANG
// longitudinal_scattering (closure/bsdf_principled_hair_chiang.h), v = 4 * roughness. One log, one Bessel and one exp body.
fn huang_longitudinal(sin_i:f32,cos_i:f32,sin_o:f32,cos_o:f32,v:f32)->f32{
    let inv_v=rounded_divide(1.0,v);let cos_arg=(cos_i*cos_o)*inv_v;let sin_arg=(sin_i*sin_o)*inv_v;
    let low=v<=0.1;let large=low&&cos_arg>12.0;
    var i0=0.0;if(!large){i0=huang_bessel_I0(cos_arg);}
    if(low){
        // log_bessel_I0, then logf(0.5f * inv_v), through one log.
        var log_a=0.0;var log_b=0.0;let count=rounded_steps(2u);
        for(var k=0u;k<count;k++){let l=huang_log(select(0.5*inv_v,select(i0,cos_arg,large),k==0u));if(k==0u){log_a=l;}else if(k==1u){log_b=l;}}
        var log_i0=log_a;if(large){log_i0=rounded_add(cos_arg,0.5*rounded_add(rounded_add(rounded_divide(1.0,8.0*cos_arg),-1.8378770664093454),-log_a));}
        let exponent=rounded_add(rounded_add(rounded_add(rounded_add(log_i0,-sin_arg),-inv_v),0.6931),log_b);
        return huang_exp3(vec3<f32>(exponent)).x;
    }
    let e=huang_exp3(vec3<f32>(-sin_arg,inv_v,-inv_v));
    // sinhf(inv_v) * 2.0f * v.
    return rounded_divide(e.x*i0,(0.5*rounded_add(e.y,-e.z)*2.0)*v);
}
// bsdf_hair_huang_eval: (value, pdf). The pdf is 1 from the start.
fn huang_evaluate(sd:SurfaceData,c:SvmClosure,wo:vec3<f32>)->vec4<f32>{
    let out=vec4<f32>(0,0,0,1);
    let Y=c.T;let Z=c.reflection_tint;
    let I=vec3<f32>(rounded_dot(sd.wi,c.N),rounded_dot(sd.wi,Y),rounded_dot(sd.wi,Z));
    let O=vec3<f32>(rounded_dot(wo,c.N),rounded_dot(wo,Y),rounded_dot(wo,Z));
    let cos_i=huang_cos_theta(I);let cos_o=huang_cos_theta(O);
    let tan_tilt=huang_tan(c.a);
    if(tan_tilt*rounded_divide(O.y,cos_o)< -1.0){return out;}
    let span_arg=max(-(tan_tilt*rounded_divide(I.y,cos_i)),0.0);if(span_arg>1.0){return out;}
    let half_span=trig_arc(span_arg,true);
    let r=c.energy_scale;let b=c.b;let circular=b==1.0;
    // One arctangent body: dir_phi(I) and h_to_gamma's atan2f(I.z, -b * I.x) (ellipse), then phi_to_h of phi_i + half_span
    // and phi_i - half_span (to_gamma's arctangent, then gamma's sine and cosine).
    var phi_i=0.0;var base=0.0;var h0=0.0;var h1=0.0;var sincos_i=vec2<f32>(0);let count=rounded_steps(4u);
    for(var k=select(2u,0u,!circular);k<count;k++){
        var y=I.x;var x=I.z;var sc=vec2<f32>(0);
        if(k==1u){y=I.z;x=-(b*I.x);}
        else if(k>=2u){let phi=rounded_add(phi_i,select(-half_span,half_span,k==2u));sc=huang_sincos(phi);y=sc.x;x=b*sc.y;}
        var angle=0.0;if(!circular){angle=trig_atan2_guarded(y,x);}
        if(k==0u){phi_i=angle;}else if(k==1u){base=angle;sincos_i=vec2<f32>(rounded_divide(I.x,cos_i),rounded_divide(I.z,cos_i));}
        else{var value=-sc.x;
            if(!circular){let gamma=huang_sincos(angle);value=rounded_add(-(sincos_i.y*gamma.x),(b*sincos_i.x)*gamma.y);}
            if(k==2u){h0=value;}else if(k==3u){h1=value;}}
    }
    var dh=2.0;var h_min=h0;var h_max=h1;let half_pixel=c.alpha_y;
    if(r>half_pixel){
        let near_min=max(-r,rounded_add(c.alpha_x,-half_pixel));let near_max=min(r,rounded_add(c.alpha_x,half_pixel));
        dh=rounded_divide(rounded_add(near_max,-near_min),r);h_min=max(h_min,near_min);h_max=min(h_max,near_max);
    }
    h_min=max(rounded_divide(h_min,r),-0.999);h_max=min(rounded_divide(h_max,r),0.999);
    if(!(h_min<h_max)){return out;}
    let f=huang_frame(c,I,cos_i,base);
    let value=rounded_add3(huang_eval_r(c,f,I,O,h_min,h_max),huang_eval_residual(c,f,I,O,h_min,h_max));
    return vec4<f32>(surf_native_div3(value,cos_i*dh),1.0);
}
// average(float3): reduce_add(a) * (1 / 3).
fn huang_average(v:vec3<f32>)->f32{return rounded_add(rounded_add(v.x,v.y),v.z)*(1.0/3.0);}
// bsdf_hair_huang_albedo: the geometric series, circular cross-section and specular reflection assumed.
fn huang_albedo(sd:SurfaceData,c:SvmClosure)->vec3<f32>{
    let wi=vec3<f32>(rounded_dot(sd.wi,c.N),rounded_dot(sd.wi,c.T),rounded_dot(sd.wi,c.reflection_tint));
    let wmi=vec3<f32>(c.alpha_x,0.0,rounded_sqrt(max(rounded_add(1.0,-(c.alpha_x*c.alpha_x)),0.0)));
    let cos_m=rounded_dot(wmi,wi);let fresnel=huang_fresnel(cos_m,c.ior);let f=fresnel.x;let wt=huang_refract(wi,wmi,cos_m,fresnel.y,rounded_divide(1.0,c.ior));
    let A=huang_exp3(surf_native_div3((2.0*c.multiscatter)*fresnel.y,rounded_add(1.0,-(wt.y*wt.y))));
    return surf_safe_divide3(rounded_add3(rounded_add3(A,-((2.0*f)*A)),vec3<f32>(f)),rounded_add3(vec3<f32>(1),-(f*A)));
}
// bsdf_hair_huang_sample before bsdf_sample's shared handling. The pdf is 1: the importance sampling pdf is already in the
// value. The three interfaces share one body (pass 0: wh1, wr, wt; pass 1: wh2, wtt, wtr; pass 2: wh3, wtrt and the mirror
// of TRRT's shadowing term); what the pinned function computes and does not use on a path has no side effect.
fn huang_sample(sd:SurfaceData,c:SvmClosure,random:vec3<f32>)->SurfaceSample{
    var out:SurfaceSample;let Y=c.T;let Z=c.reflection_tint;
    // make_float2(lcg_step_float, lcg_step_float): the order of the two draws is the oracle compiler's. GCC for x86-64 evaluates
    // the second argument first, GCC for AArch64 (the scalar ARM64 oracle, NATIVE_FLOAT3_RECIPROCAL_NORMALIZE) the first.
    let h1=huang_random();let d2a=huang_random();let d2b=huang_random();let d3a=huang_random();let d3b=huang_random();
    let first_x=NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u;
    let h2x=select(d2b,d2a,first_x);let h2y=select(d2a,d2b,first_x);let h3x=select(d3b,d3a,first_x);let h3y=select(d3a,d3b,first_x);
    let wi=vec3<f32>(rounded_dot(sd.wi,c.N),rounded_dot(sd.wi,Y),rounded_dot(sd.wi,Z));let b=c.b;let cos_i=huang_cos_theta(wi);
    var base=0.0;if(b!=1.0){base=trig_atan2_guarded(wi.z,-(b*wi.x));}let f=huang_frame(c,wi,cos_i,base);
    let h_div_r=select(rounded_add(random.y*2.0,-1.0),rounded_divide(c.alpha_x,c.energy_scale),c.energy_scale>c.alpha_y);
    let gamma_mi=huang_h_to_gamma(h_div_r,b,f.base);let gi=huang_gamma(gamma_mi,b);
    let wmi_=vec3<f32>(gi.z,0.0,gi.w);let wmi=vec3<f32>(gi.z*f.cos_tilt,f.sin_tilt,gi.w*f.cos_tilt);
    if(rounded_dot(wmi,wi)<0.0||rounded_dot(wmi_,wi)<0.0){return out;}
    let inv_eta=f.inv_eta;let mu_a=c.multiscatter;let passes=rounded_steps(3u);
    var v=wi;var m=wmi;var segment=wi;var gamma=gamma_mi;var g=gi;var gamma_previous=gamma_mi;var g_previous=gi;var phi_previous=vec2<f32>(0);
    var wr=vec3<f32>(0);var wtt=vec3<f32>(0);var wtr=vec3<f32>(0);var wtrt=vec3<f32>(0);
    var R=0.0;var T1=0.0;var R2=0.0;var T2=0.0;var scale2=0.0;var G2a=0.0;var G2b=0.0;var R3=0.0;var T3=0.0;var scale3=0.0;var G3a=0.0;var G3b=0.0;var cos_mi3=0.0;
    var A_t=vec3<f32>(0);var A_tr=vec3<f32>(0);var wh1=vec3<f32>(0);var wh2=vec3<f32>(0);var wh3=vec3<f32>(0);var wmt=vec3<f32>(0);var wmt_=vec3<f32>(0);var wmtr=vec3<f32>(0);var inner=false;
    for(var layer=0u;layer<passes;layer++){
        let wh=huang_sample_wh(f.roughness,v,m,select(select(vec2<f32>(h3x,h3y),vec2<f32>(h2x,h2y),layer==1u),vec2<f32>(random.z,h1),layer==0u));
        let cos_h=rounded_dot(v,wh);let cos_m=rounded_dot(v,m);
        if(layer==0u){
            // microfacet_visible(wi, wmi_, wh1).
            wh1=wh;if(!(cos_h>0.0&&rounded_dot(wi,wmi_)>0.0)){return out;}
        }else if(layer==1u){
            // cos_mi2 > 0 && microfacet_visible(-wt, wmi_, wh1) && microfacet_visible(-wt, wmt_, wh2).
            wh2=wh;inner=cos_m>0.0&&rounded_dot(v,wh1)>0.0&&rounded_dot(v,wmi_)>0.0&&cos_h>0.0&&rounded_dot(v,wmt_)>0.0;if(!inner){break;}
        }else{wh3=wh;cos_mi3=cos_m;}
        let fresnel=huang_fresnel(cos_h,select(inv_eta,f.eta,layer==0u));
        let refracted=huang_refract(v,wh,cos_h,fresnel.y,select(f.eta,inv_eta,layer==0u));
        // -reflect(segment, wh): at pass 1 the segment is wt, and v is -wt.
        let mirror=rounded_add3((2.0*wh)*select(cos_h,-cos_h,layer==1u),-segment);
        let energy=huang_energy(f.sqrt_roughness,cos_m,select(f.z_inv,f.z,layer==0u),select(f.table_inv,f.table,layer==0u));
        // The absorption of the segment that ends at this interface: A_t at pass 1, A_tr at pass 2.
        if(layer>0u){
            var path=0.0;
            if(b==1.0){let chord=huang_sincos_exact(rounded_add(phi_previous.x,-gamma_previous)).y;path=select(-(2.0*abs(chord)),2.0*chord,layer==1u);}
            else{path=-huang_point_distance(g_previous.xy,select(g.xy,huang_sincos(rounded_add(gamma,3.14159265358979323846)),layer==1u),b);}
            let A=huang_exp3(surf_native_div3(mu_a,huang_cos_theta(segment))*path);
            if(layer==1u){A_t=A;}else{A_tr=A;}
        }
        // bsdf_Go at dot(m, -refracted), and at dot(m, wr), dot(m, -wtr) or dot(m, -reflect(wtr, wh3)).
        let go=huang_Go2(f.alpha2,cos_m,rounded_dot(m,-refracted),rounded_dot(m,select(mirror,-mirror,layer==1u)));
        if(layer==0u){
            wr=mirror;
            R=(((c.transmission_tint.x*fresnel.x)*energy)*select(0.0,1.0,rounded_dot(mirror,wh)>0.0&&rounded_dot(mirror,wmi_)>0.0))*go.y;
            T1=(rounded_add(1.0,-fresnel.x)*energy)*go.x;
        }else if(layer==1u){R2=fresnel.x;T2=rounded_add(1.0,-R2);scale2=energy;wtt=refracted;wtr=mirror;G2a=go.x;G2b=go.y;}
        else{R3=fresnel.x;T3=rounded_add(1.0,-R3);scale3=energy;wtrt=refracted;G3a=go.x;G3b=go.y;}
        if(layer<2u){
            // The next interface, past wt or wtr.
            let next=select(mirror,refracted,layer==0u);let phi=huang_phi(next,b);
            let gamma_next=select(rounded_add(rounded_add(gamma_mi,-(2.0*rounded_add(phi_previous.y,-phi.y))),3.14159265358979323846),rounded_add(2.0*phi.y,-gamma_mi),layer==0u);
            let gn=huang_gamma(gamma_next,b);let m_next=vec3<f32>(gn.z*f.cos_tilt,-f.sin_tilt,gn.w*f.cos_tilt);
            if(layer==0u){wmt=m_next;wmt_=vec3<f32>(gn.z,0.0,gn.w);}else{wmtr=m_next;}
            v=select(next,-next,layer==0u);m=m_next;segment=next;phi_previous=phi;gamma_previous=gamma;g_previous=g;gamma=gamma_next;g=gn;
        }
    }
    var TT=vec3<f32>(0);var TRT=vec3<f32>(0);var TRRT=vec3<f32>(0);var wtrrt=vec3<f32>(0);
    if(inner){
        if(rounded_dot(wmt,-wtt)>0.0&&T2>0.0&&rounded_dot(-wtt,wh2)>0.0&&rounded_dot(-wtt,wmt_)>0.0){
            TT=((((c.transmission_tint.y*T1)*A_t)*T2)*scale2)*G2a;
        }
        if(cos_mi3>0.0){
            let TR=((((((T1*R2)*scale2)*A_t)*A_tr)*scale3))*G2b;
            let flat_m=vec3<f32>(wmtr.x,0.0,wmtr.z);
            if(T3>0.0&&rounded_dot(wtr,wh3)>0.0&&rounded_dot(wtr,flat_m)>0.0&&rounded_dot(-wtrt,wh3)>0.0&&rounded_dot(-wtrt,flat_m)>0.0){
                TRT=((c.transmission_tint.z*TR)*T3)*G3a;
            }
            // TRRT+: sample theta_o, then phi_o.
            let rand_theta=max(huang_random(),1e-5);
            let inner_log=rounded_add(rand_theta,rounded_add(1.0,-rand_theta)*huang_exp3(vec3<f32>(rounded_divide(-0.5,f.roughness))).x);
            let fac=rounded_add(1.0,(4.0*f.roughness)*huang_log(inner_log));
            let cos_fac=rounded_sqrt(max(rounded_add(1.0,-(fac*fac)),0.0));
            let cosine=huang_sincos_exact(6.28318530717958647693*huang_random()).y;
            let sin_theta_o=rounded_add((-fac)*wi.y,(cos_fac*cosine)*cos_i);
            let cos_theta_o=rounded_sqrt(max(rounded_add(1.0,-(sin_theta_o*sin_theta_o)),0.0));
            let phi_o=huang_sincos(6.28318530717958647693*huang_random());
            wtrrt=vec3<f32>(phi_o.x*cos_theta_o,sin_theta_o,phi_o.y*cos_theta_o);
            let product=A_t*A_tr;let A_avg=vec3<f32>(rounded_sqrt(product.x),rounded_sqrt(product.y),rounded_sqrt(product.z));
            let T_avg=max(0.5*rounded_add(T2,T3),1e-5);let A_res=huang_div_lanes(A_avg*T_avg,rounded_add3(vec3<f32>(1),-(A_avg*rounded_add(1.0,-T_avg))));
            TRRT=((TR*R3)*A_res)*G3b;
        }
    }
    let r=R;let tt=huang_average(TT);let trt=huang_average(TRT);let trrt=huang_average(TRRT);
    let total=rounded_add(rounded_add(rounded_add(r,tt),trt),trrt);if(total==0.0){return out;}
    var local_O=wtrrt;let pick=random.x*total;let r_tt=rounded_add(r,tt);var numerator=TRRT;var denominator=trrt;
    if(pick<r){local_O=wr;numerator=vec3<f32>(1);denominator=1.0;}
    else if(pick<r_tt){local_O=wtt;numerator=TT;denominator=tt;}
    else if(pick<rounded_add(r_tt,trt)){local_O=wtrt;numerator=TRT;denominator=trt;}
    out.eval=select(surf_native_div3(numerator,denominator)*total,vec3<f32>(total),pick<r);
    out.wo=rounded_add3(rounded_add3(local_O.x*c.N,local_O.y*Y),local_O.z*Z);
    out.pdf=1.0;out.label=LABEL_GLOSSY|LABEL_REFLECT;return out;
}
// The extra slot: results for the surface entries.
var<private> huang_base:u32;
const HH_X_eval:u32=4u;const HH_X_lcg:u32=16u;const HH_X_wo:u32=20u;const HH_X_pdf:u32=32u;const HH_X_sample:u32=36u;const HH_X_label:u32=48u;
fn huang_extra(c:SvmClosure)->u32{return huang_base+u32(c.fresnel_f0)*SVM_CLOSURE_BYTES;}
fn huang_store3(at:u32,v:vec3<f32>){store_word(at,bitcast<u32>(v.x));store_word(at+4u,bitcast<u32>(v.y));store_word(at+8u,bitcast<u32>(v.z));}
// surf_evaluate_closure's bump test, then bsdf_hair_huang_eval and the random number position after it.
fn huang_eval_store(sd:SurfaceData,c:SvmClosure,wo:vec3<f32>){
    if(surf_bump(sd,c,wo,true)==0.0){return;}
    let e=huang_evaluate(sd,c,wo);let x=huang_extra(c);huang_store3(x+HH_X_eval,e.xyz);store_word(x+HH_X_lcg,huang_lcg);
}
// What surf_family_evaluate returns for the closure: the stored evaluation, the position it reached.
fn huang_read_eval(c:SvmClosure)->vec4<f32>{let x=huang_extra(c);huang_lcg=load_word(x+HH_X_lcg);return vec4<f32>(heap_vec3(x+HH_X_eval),1.0);}
fn huang_sample_store(c:SvmClosure,s:SurfaceSample){
    let x=huang_extra(c);huang_store3(x+HH_X_wo,s.wo);store_word(x+HH_X_pdf,bitcast<u32>(s.pdf));huang_store3(x+HH_X_sample,s.eval);store_word(x+HH_X_label,s.label);
}
fn huang_read_sample(c:SvmClosure)->SurfaceSample{
    var s:SurfaceSample;let x=huang_extra(c);s.wo=heap_vec3(x+HH_X_wo);s.pdf=heap_float(x+HH_X_pdf);s.eval=heap_vec3(x+HH_X_sample);s.label=load_word(x+HH_X_label);return s;
}
@compute @workgroup_size(1) fn shade_surface_huang_eval(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)||(p(28u)!=0u&&load_word(p(17u))!=0u)){return;}let at=p(24u)+id.x*SURFACE_STAGE_BYTES;
    let state=load_word(at);if(state!=3u&&state!=2u){return;}
    let stage=surface_stage_read(at);let r=stage.r;var wo=vec3<f32>(0);var skipped=NONE;
    if(state==3u){
        // The sampled light's direction, for surface_direct_light's light_bsdf.
        if(data_word(D_integrator_use_direct_light)==0u||(r.runtime_flag&SR_BSDF_HAS_EVAL)==0u){return;}
//#if MNEE_HUANG else         let ls=light_sample_read(at+308u);if(!(ls.pdf>0.0)){return;}wo=ls.D;
        // A path MNEE sampled (integrate_surface_direct_light with PATH_MNEE_SAMPLED): the direction of the MNEE slot, the
        // receiver's direction to the first manifold vertex (mnee_direct_light, wavefront_mnee.wgsl).
        var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
        if((data_word(D_kernel_features)&MNEE_KERNEL_FEATURE)!=0u&&(state_byte(S_path_mnee,index)&MNEE_PATH_SAMPLED)!=0u){
            let shadow=state_word(S_path_mnee_shadow_state,index);if(shadow>=p(25u)){fail();return;}
            wo=vec3<f32>(heap_float(state_address(MNEE_S_I1_t,shadow,4u)),heap_float(state_address(MNEE_S_I1_u,shadow,4u)),heap_float(state_address(MNEE_S_I1_v,shadow,4u)));
        }else{let ls=light_sample_read(at+308u);if(!(ls.pdf>0.0)){return;}wo=ls.D;}
//#endif
    }else{
        // The sampled direction, for surf_sample_mis: every closure but the sampled one.
        if(r.count<=1u){return;}let sampled=surf_sample_load(p(0u)+id.x*SURF_SAMPLE_BYTES);if(sampled.pdf==0.0){return;}
//#if SUBSURFACE
        if((sampled.label&SS_LABEL_SUBSURFACE_SCATTER)!=0u){return;}
//#endif
        wo=sampled.wo;skipped=surf_sampled_closure;
    }
    huang_base=r.closure_base;huang_lcg=r.lcg_state;
    for(var i=0u;i<r.count;i++){if(i==skipped){continue;}let c=svm_closure_read(r,i);if(c.kind!=HH_CLOSURE_BSDF_HAIR_HUANG_ID){continue;}huang_eval_store(stage.sd,c,wo);}
}
@compute @workgroup_size(1) fn shade_surface_huang_sample(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)||(p(28u)!=0u&&load_word(p(17u))!=0u)){return;}let at=p(24u)+id.x*SURFACE_STAGE_BYTES;if(load_word(at)!=2u){return;}
    var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    let stage=surface_stage_read(at);let r=stage.r;if(r.count==0u){return;}
//#if SAMPLING_SOBOL_BURLEY else     let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_SURFACE_BSDF).xyz;
    let random=native_rng_pattern(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_SURFACE_BSDF,3u).xyz;
//#endif
    // surf_sample's pick (surface_shader_bsdf_bssrdf_pick).
    var selected=0u;var rand=random;
//#if !SUBSURFACE
    if((r.runtime_flag&SR_BSDF)==0u){return;}
    if(r.count>1u){var sum=0.0;for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(surf_bsdf(c.kind)){sum+=c.sample_weight;}}
        let pick=random.z*sum;var partial=0.0;for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(surf_bsdf(c.kind)){
            let next=partial+c.sample_weight;if(pick<next){selected=i;rand.z=rounded_divide(pick-partial,c.sample_weight);break;}partial=next;}}}
//#endif
//#if SUBSURFACE
    if((r.runtime_flag&(SR_BSDF|SS_SR_BSSRDF))==0u){return;}
    if(r.count>1u){var sum=0.0;for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(surf_bsdf(c.kind)||ss_bssrdf_kind(c.kind)){sum+=c.sample_weight;}}
        let pick=random.z*sum;var partial=0.0;for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(surf_bsdf(c.kind)||ss_bssrdf_kind(c.kind)){
            let next=partial+c.sample_weight;if(pick<next){selected=i;rand.z=rounded_divide(pick-partial,c.sample_weight);break;}partial=next;}}}
//#endif
    let c=svm_closure_read(r,selected);if(c.kind!=HH_CLOSURE_BSDF_HAIR_HUANG_ID){return;}
    huang_base=r.closure_base;huang_lcg=r.lcg_state;
    huang_sample_store(c,huang_sample(stage.sd,c,rand));store_word(at+264u,huang_lcg);
}
//#endif
