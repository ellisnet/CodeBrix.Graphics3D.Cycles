//#if CURVES
// SPDX-FileCopyrightText: 2009-2021 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles kernel/geom/curve_intersect.h (adapted there from Embree's
// curve_intersector_sweep.h) and kernel/bvh/nodes.h. See native/THIRD-PARTY-NOTICES.txt.
//
// Static thick curves, thick linear curves and ribbons, and BVH2's unaligned nodes. Every result of a
// sum, difference or product passes the order word (cvb: XOR with header word 3 after the error word, always 0,
// which the compiler cannot see through), so no driver can fuse a product into a sum or reassociate. Quotients
// and square roots are correctly rounded (rounded_divide, rounded_sqrt). float3 / float follows the oracle:
// per-component quotients (SSE) or reciprocal multiplication (scalar float3), NATIVE_FLOAT3_RECIPROCAL_NORMALIZE.
// min and max are (a<b)?a:b and (a>b)?a:b, with their NaN behaviour. No arrays: the Adreno 730 miscompiles
// reassigned private arrays and dynamically indexed function-scope aggregates, so the curve is four values and
// the subdivision stack lives in vectors read through select chains.
//#if CURVE_THICK
fn cv_derivative2(c0:vec4<f32>,c1:vec4<f32>,c2:vec4<f32>,c3:vec4<f32>,u:f32,o:u32)->vec4<f32>{
    let t=u;
    let n0=cvb(cvb(-3.0*t,o)+2.0,o);let n1=cvb(cvb(9.0*t,o)-5.0,o);let n2=cvb(cvb(-9.0*t,o)+4.0,o);let n3=cvb(cvb(3.0*t,o)-1.0,o);
    return cv_blend(c0,c1,c2,c3,vec4<f32>(n0,n1,n2,n3),o);
}
fn cv_sqr_point_line(a:vec3<f32>,b:vec3<f32>,o:u32)->f32{
    let N=cv_cross(a,b,o);return rounded_divide(cv_dot(N,N,o),cv_dot(b,b,o));
}
struct CvCylinder {t:vec2<f32>,u:vec2<f32>,valid:bool}
fn cv_cylinder(start:vec3<f32>,end:vec3<f32>,radius:f32,ray_D:vec3<f32>,o:u32)->CvCylinder{
    var out:CvCylinder;let delta=cvb3(end-start,o);
    let rl=rounded_divide(1.0,cv_len(delta,o));
    let dP=cvb3(delta*rl,o);let O=-start;let dO=ray_D;
    let dOdO=cv_dot(dO,dO,o);let OdO=cv_dot(dO,O,o);let OO=cv_dot(O,O,o);let dOz=cv_dot(dP,dO,o);let Oz=cv_dot(dP,O,o);
    let dOz2=cvb(dOz*dOz,o);
    let A=cvb(dOdO-dOz2,o);
    let B=cvb(2.0*cvb(OdO-cvb(dOz*Oz,o),o),o);
    let C=cvb(cvb(OO-cvb(Oz*Oz,o),o)-cvb(radius*radius,o),o);
    let D=cvb(cvb(B*B,o)-cvb(cvb(4.0*A,o)*C,o),o);
    if(!(D>=0.0)){out.t=vec2<f32>(3.4028234663852886e38,-3.4028234663852886e38);return out;}
    let eps=cvb(1.9073486328125e-6*cv_max(abs(dOdO),abs(dOz2)),o);
    if(abs(A)<eps){out.t=vec2<f32>(-3.4028234663852886e38,3.4028234663852886e38);out.valid=C<=0.0;return out;}
    let Q=rounded_sqrt(D);let rcp_2A=rounded_divide(1.0,cvb(2.0*A,o));
    out.t=vec2<f32>(cvb(cvb(-B-Q,o)*rcp_2A,o),cvb(cvb(-B+Q,o)*rcp_2A,o));
    out.u=vec2<f32>(cvb(cvb(cvb(out.t.x*dOz,o)+Oz,o)*rl,o),cvb(cvb(cvb(out.t.y*dOz,o)+Oz,o)*rl,o));
    out.valid=true;return out;
}
fn cv_half_plane(P:vec3<f32>,N:vec3<f32>,ray_D:vec3<f32>,o:u32)->vec2<f32>{
    let ON=cv_dot(-P,N,o);let DN=cv_dot(ray_D,N,o);let tiny=abs(DN)<1e-18;let t=rounded_divide(-ON,DN);
    return vec2<f32>(select(t,-3.4028234663852886e38,tiny||DN<0.0),select(t,3.4028234663852886e38,tiny||DN>0.0));
}
// dnormalize: (pp*dp-pdp*p)/(pp*sqrtf(pp)).
fn cv_dnormalize(p0:vec3<f32>,dp:vec3<f32>,o:u32)->vec3<f32>{
    let pp=cv_dot(p0,p0,o);let pdp=cv_dot(p0,dp,o);
    return cv_div3(cvb3(cvb3(pp*dp,o)-cvb3(pdp*p0,o),o),cvb(pp*rounded_sqrt(pp),o),o);
}
struct CvIterative {found:bool,t:f32,u:f32}
// curve_intersect_iterative with use_backfacing false; box_* below keep the pinned max(min(c0,c1),max(c2,c3)).
fn cv_iterative(ray_D:vec3<f32>,ray_tmin:f32,ray_tmax:f32,dt:f32,c0:vec4<f32>,c1:vec4<f32>,c2:vec4<f32>,c3:vec4<f32>,start_u:f32,start_t:f32,o:u32)->CvIterative{
    var out:CvIterative;var u=start_u;var t=start_t;
    let length_ray_D=cv_len(ray_D,o);
    let box_min=cv_min4(cv_min4(c0,c1),cv_min4(c2,c3));let box_max=cv_max4(cv_min4(c0,c1),cv_max4(c2,c3));
    let box_abs=cv_max4(abs(box_min),abs(box_max));
    let P_err=cvb(1.9073486328125e-6*cv_max(box_abs.x,cv_max(box_abs.y,cv_max(box_abs.z,box_abs.w))),o);
    let radius_max=box_max.w;let radius_err=cvb(1.9073486328125e-6*radius_max,o);
    for(var i=0u;i<5u;i++){
        let Q=cvb3(ray_D*t,o);let Q_err=cvb(cvb(1.9073486328125e-6*length_ray_D,o)*t,o);
        let P4=cv_eval(c0,c1,c2,c3,u,o);let dPdu4=cv_derivative(c0,c1,c2,c3,u,o);
        let P=P4.xyz;let dPdu=dPdu4.xyz;let radius=P4.w;let dradiusdu=dPdu4.w;
        let ddPdu=cv_derivative2(c0,c1,c2,c3,u,o).xyz;
        let R=cvb3(Q-P,o);let len_R=cv_len(R,o);let R_err=cv_max(Q_err,P_err);let dRdu=-dPdu;
        let T=cv_normalize(dPdu,o);let dTdu=cv_dnormalize(dPdu,ddPdu,o);
        let cos_err=rounded_divide(P_err,cv_len(dPdu,o));
        let f=cv_dot(R,T,o);
        let f_err=cvb(cvb(cvb(len_R*P_err,o)+R_err,o)+cvb(cos_err*cvb(1.0+len_R,o),o),o);
        let dfdu=cvb(cv_dot(dRdu,T,o)+cv_dot(R,dTdu,o),o);let dfdt=cv_dot(ray_D,T,o);
        let K=cvb(cv_dot(R,R,o)-cvb(f*f,o),o);
        let dKdu=cvb(cv_dot(R,dRdu,o)-cvb(f*dfdu,o),o);let dKdt=cvb(cv_dot(R,ray_D,o)-cvb(f*dfdt,o),o);
        let root_K=cv_sqrt(K);
        let rsqrt_K=select(0.0,rounded_divide(1.0,root_K),K>0.0);
        let g=cvb(root_K-radius,o);
        let g_err=cvb(cvb(R_err+f_err,o)+radius_err,o);
        let dgdu=cvb(cvb(dKdu*rsqrt_K,o)-dradiusdu,o);let dgdt=cvb(dKdt*rsqrt_K,o);
        let invdet=rounded_divide(1.0,cvb(cvb(dfdu*dgdt,o)-cvb(dgdu*dfdt,o),o));
        u=cvb(u-cvb(cvb(cvb(dgdt*f,o)-cvb(dfdt*g,o),o)*invdet,o),o);
        t=cvb(t-cvb(cvb(cvb(-dgdu*f,o)+cvb(dfdu*g,o),o)*invdet,o),o);
        if(abs(f)<f_err&&abs(g)<g_err){
            t=cvb(t+dt,o);
            if(!(t>=ray_tmin&&t<=ray_tmax)){return out;}
            if(!(u>=0.0&&u<=1.0)){return out;}
            // Back-face culling.
            let R2=cv_normalize(R,o);let U=cvb3(cvb3(dradiusdu*R2,o)+dPdu,o);let V=cv_cross(dPdu,R2,o);let Ng=cv_cross(V,U,o);
            if(cv_dot(ray_D,Ng,o)>0.0){return out;}
            out.found=true;out.t=t;out.u=u;return out;
        }
    }
    return out;
}
fn cv_lane(v:vec4<f32>,i:u32)->f32{return select(select(select(v.w,v.z,i==2u),v.y,i==1u),v.x,i==0u);}
fn cv_store(v:vec4<f32>,i:u32,x:f32)->vec4<f32>{return select(v,vec4<f32>(x),vec4<u32>(0u,1u,2u,3u)==vec4<u32>(i));}
// curve_intersect_recursive: CURVE_NUM_BEZIER_STEPS 2, depth 4 (always the unstable depth, as pinned), four stack levels.
fn cv_thick(ray_P:vec3<f32>,ray_D:vec3<f32>,ray_tmin:f32,tmax_in:f32,k0:vec4<f32>,k1:vec4<f32>,k2:vec4<f32>,k3:vec4<f32>,o:u32)->CurveHit{
    var out:CurveHit;var ray_tmax=tmax_in;
    let sum=cvb4(cvb4(cvb4(k0+k1,o)+k2,o)+k3,o);let center=cvb4(0.25*sum,o).xyz;
    let dt=rounded_divide(cv_dot(cvb3(center-ray_P,o),ray_D,o),cv_dot(ray_D,ray_D,o));
    let ref3=cvb3(ray_P+cvb3(ray_D*dt,o),o);let ref4=vec4<f32>(ref3,0.0);
    let c0=cvb4(k0-ref4,o);let c1=cvb4(k1-ref4,o);let c2=cvb4(k2-ref4,o);let c3=cvb4(k3-ref4,o);
    var depth=0u;var u0=0.0;var u1=1.0;var i=0u;
    var stack_u0=vec4<f32>(0);var stack_u1=vec4<f32>(0);var stack_i=vec4<u32>(0u);
    loop{
        while(i<2u){
            let step=select(0.0,0.5,i==1u);
            let dscale=cvb(cvb(cvb(u1-u0,o)*0.3333333432674407958984375,o)*0.5,o);
            let vu0=cv_mix(u0,u1,step,o);let vu1=cv_mix(u0,u1,cvb(step+0.5,o),o);
            let P0=cv_eval(c0,c1,c2,c3,vu0,o);let dP0du=cvb4(dscale*cv_derivative(c0,c1,c2,c3,vu0,o),o);
            let P3=cv_eval(c0,c1,c2,c3,vu1,o);let dP3du=cvb4(dscale*cv_derivative(c0,c1,c2,c3,vu1,o),o);
            let P1=cvb4(P0+dP0du,o);let P2=cvb4(P3-dP3du,o);
            let chord=cvb3(P3.xyz-P0.xyz,o);
            let rr1=cv_sqr_point_line(dP0du.xyz,chord,o);let rr2=cv_sqr_point_line(dP3du.xyz,chord,o);
            let maxr12=cv_sqrt(cv_max(rr1,rr2));
            var r_outer=cvb(cv_max(cv_max(P0.w,P1.w),cv_max(P2.w,P3.w))+maxr12,o);
            var r_inner=cvb(cv_min(cv_min(P0.w,P1.w),cv_min(P2.w,P3.w))-maxr12,o);
            r_outer=cvb(1.0000002384185791015625*r_outer,o);
            r_inner=cv_max(0.0,cvb(0.99999976158142089844*r_inner,o));
            let outer=cv_cylinder(P0.xyz,P3.xyz,r_outer,ray_D,o);
            if(!outer.valid){i++;continue;}
            var tp=vec2<f32>(cvb(ray_tmin-dt,o),cvb(ray_tmax-dt,o));
            tp=vec2<f32>(cv_max(tp.x,outer.t.x),cv_min(tp.y,outer.t.y));
            let h0=cv_half_plane(P0.xyz,dP0du.xyz,ray_D,o);tp=vec2<f32>(cv_max(tp.x,h0.x),cv_min(tp.y,h0.y));
            let h1=cv_half_plane(P3.xyz,-dP3du.xyz,ray_D,o);tp=vec2<f32>(cv_max(tp.x,h1.x),cv_min(tp.y,h1.y));
            if(!(tp.x<=tp.y)){i++;continue;}
            let u_outer0=cv_mix(u0,u1,cvb(cvb(step+cv_clamp01(outer.u.x),o)*0.3333333432674407958984375,o),o);
            let u_outer1=cv_mix(u0,u1,cvb(cvb(step+cv_clamp01(outer.u.y),o)*0.3333333432674407958984375,o),o);
            let inner=cv_cylinder(P0.xyz,P3.xyz,r_inner,ray_D,o);
            let tp0=vec2<f32>(tp.x,cv_min(tp.y,inner.t.x));let tp1=vec2<f32>(cv_max(tp.x,inner.t.y),tp.y);
            let valid0=cvb(tp0.x-tp0.y,o)<0.001;let valid1=cvb(tp1.x-tp1.y,o)<0.001;
            if(!(valid0||valid1)){i++;continue;}
            let t1=cvb(tp1.x+dt,o);var recurse=false;
            // The two hits share one cv_iterative body (rounded_steps: the bound is opaque, the loop never unrolls).
            let hits=rounded_steps(2u);
            for(var h=0u;h<hits;h++){
                if(h>=2u){continue;}
                let wanted=select(valid1&&t1>=ray_tmin&&t1<=ray_tmax,valid0,h==0u);
                if(!wanted){continue;}
                if(depth>=4u){
                    let found=cv_iterative(ray_D,ray_tmin,ray_tmax,dt,c0,c1,c2,c3,select(u_outer1,u_outer0,h==0u),select(tp1.y,tp0.x,h==0u),o);
                    if(found.found){ray_tmax=found.t;out.hit=true;out.t=found.t;out.u=found.u;out.v=0.0;}
                }else{recurse=true;}
            }
            if(recurse){
                stack_u0=cv_store(stack_u0,depth,u0);stack_u1=cv_store(stack_u1,depth,u1);
                stack_i=select(stack_i,vec4<u32>(i+1u),vec4<u32>(0u,1u,2u,3u)==vec4<u32>(depth));
                depth++;u0=vu0;u1=vu1;i=0u;continue;
            }
            i++;
        }
        if(depth==0u){break;}
        depth--;u0=cv_lane(stack_u0,depth);u1=cv_lane(stack_u1,depth);
        i=select(select(select(stack_i.w,stack_i.z,depth==2u),stack_i.y,depth==1u),stack_i.x,depth==0u);
    }
    return out;
}
//#endif
//#if CURVE_THICK_LINEAR
// linear_curve_intersect + cone_sphere_intersect.
fn cv_linear(ray_P:vec3<f32>,ray_D:vec3<f32>,ray_tmin:f32,ray_tmax:f32,k0:vec4<f32>,k1:vec4<f32>,k2:vec4<f32>,k3:vec4<f32>,o:u32)->CurveHit{
    var out:CurveHit;
    let center=cvb4(0.5*cvb4(k1+k2,o),o).xyz;
    let dt=rounded_divide(cv_dot(cvb3(center-ray_P,o),ray_D,o),cv_dot(ray_D,ray_D,o));
    let ref3=cvb3(ray_P+cvb3(ray_D*dt,o),o);let ref4=vec4<f32>(ref3,0.0);
    let c0=cvb4(k0-ref4,o);let c1=cvb4(k1-ref4,o);let c2=cvb4(k2-ref4,o);let c3=cvb4(k3-ref4,o);
    let r0=c1.w;let r1=c2.w;let dr=cvb(r1-r0,o);let r0dr=cvb(r0*dr,o);
    let P0=c1.xyz;let P1=c2.xyz;let dP=cvb3(P1-P0,o);let O=-P0;let dO=ray_D;
    let dOdO=cv_dot(dO,dO,o);let OdO=cv_dot(dO,O,o);let OO=cv_dot(O,O,o);let dOz=cv_dot(dP,dO,o);let Oz=cv_dot(dP,O,o);
    let dPdP=cv_dot(dP,dP,o);let yp=cvb(Oz+r0dr,o);let g=cvb(dPdP-cvb(dr*dr,o),o);
    let A=cvb(cvb(g*dOdO,o)-cvb(dOz*dOz,o),o);
    let B=cvb(2.0*cvb(cvb(g*OdO,o)-cvb(dOz*yp,o),o),o);
    let C=cvb(cvb(cvb(cvb(g*OO,o)-cvb(Oz*Oz,o),o)-cvb(cvb(r0*r0,o)*dPdP,o),o)-cvb(cvb(2.0*r0dr,o)*Oz,o),o);
    let D=cvb(cvb(B*B,o)-cvb(cvb(4.0*A,o)*C,o),o);
    if(!(D>=0.0)){return out;}
    if(abs(A)<1e-18){return out;}
    let Q=rounded_sqrt(D);let rcp_2A=rounded_divide(1.0,cvb(2.0*A,o));
    let t0=cvb(cvb(-B-Q,o)*rcp_2A,o);let y0=cvb(yp+cvb(t0*dOz,o),o);
    var t=3.4028234663852886e38;var u=0.0;
    if(y0>-1.1920928955078125e-7&&y0<=g&&g>0.0){t=t0;u=cv_clamp01(rounded_divide(y0,g));}
    // The ending sphere, then the start sphere when the curve starts here, through one body each.
    let rcp_dOdO=rounded_divide(1.0,dOdO);
    {
        let O1=-P1;let O1dO=cv_dot(O1,dO,o);let h2=cvb(cvb(O1dO*O1dO,o)-cvb(dOdO*cvb(cv_dot(O1,O1,o)-cvb(r1*r1,o),o),o),o);
        if(h2>=0.0){
            let t_sph1=cvb(cvb(-O1dO-rounded_sqrt(h2),o)*rcp_dOdO,o);
            let r2=c3.w;let y2=cv_dot(cvb3(cvb3(t_sph1*dO,o)-P1,o),cvb3(c3.xyz-P1,o),o);let cap2=-cvb(r1*cvb(r2-r1,o),o);
            if(t_sph1<=t&&cvb(yp+cvb(t_sph1*dOz,o),o)>g&&!(y2>cap2)){t=t_sph1;u=1.0;}
        }
    }
    if(all(c0==c1)){
        let h2=cvb(cvb(OdO*OdO,o)-cvb(dOdO*cvb(cv_dot(O,O,o)-cvb(r0*r0,o),o),o),o);
        if(h2>=0.0){
            let t_sph0=cvb(cvb(-OdO-rounded_sqrt(h2),o)*rcp_dOdO,o);
            if(t_sph0<=t&&cvb(yp+cvb(t_sph0*dOz,o),o)<0.0){t=t_sph0;u=0.0;}
        }
    }
    if(t==3.4028234663852886e38){return out;}
    let t_final=cvb(t+dt,o);
    if(!(t_final>=ray_tmin&&t_final<=ray_tmax)){return out;}
    out.hit=true;out.t=t_final;out.u=u;out.v=0.0;return out;
}
//#endif
//#if CURVE_RIBBON
struct CvQuad {hit:bool,t:f32,u:f32,v:f32}
// ribbon_intersect_quad: the ray is +z from the origin, so the dots with D are z components.
fn cv_quad(ray_tmin:f32,ray_tmax:f32,va:vec3<f32>,vb:vec3<f32>,vc:vec3<f32>,vd:vec3<f32>,o:u32)->CvQuad{
    var out:CvQuad;
    let edb=cvb3(vb-vd,o);let WW=cv_cross(vd,edb,o).z;let first=WW<=0.0;
    let v0=select(vc,va,first);let v1=select(vd,vb,first);let v2=select(vb,vd,first);
    let e0=cvb3(v2-v0,o);let e1=cvb3(v0-v1,o);
    let U=cv_cross(v0,e0,o).z;let V=cv_cross(v1,e1,o).z;
    if(!(cv_max(U,V)<=0.0)){return out;}
    let Ng=cv_cross(e1,e0,o);let den=Ng.z;let rcpDen=rounded_divide(1.0,den);
    let t=cvb(rcpDen*cv_dot(v0,Ng,o),o);
    if(!(t>=ray_tmin&&t<=ray_tmax)){return out;}
    if(!(den!=0.0)){return out;}
    let u=cvb(U*rcpDen,o);let v=cvb(V*rcpDen,o);
    out.hit=true;out.t=t;out.u=select(cvb(1.0-u,o),u,first);out.v=select(cvb(1.0-v,o),v,first);return out;
}
fn cv_ribbon(ray_org:vec3<f32>,ray_D:vec3<f32>,ray_tmin:f32,ray_tmax:f32,N:u32,k0:vec4<f32>,k1:vec4<f32>,k2:vec4<f32>,k3:vec4<f32>,o:u32)->CurveHit{
    var out:CurveHit;
    let invlen=rounded_divide(1.0,cv_len(ray_D,o));
    let D=cvb3(ray_D*invlen,o);let dx0=vec3<f32>(0.0,D.z,-D.y);let dx1=vec3<f32>(-D.z,0.0,D.x);
    let space0=cv_normalize(select(dx1,dx0,cv_dot(dx0,dx0,o)>cv_dot(dx1,dx1,o)),o);
    let space1=cv_normalize(cv_cross(D,space0,o),o);
    let space2=cvb3(D*invlen,o);
    // ribbon_to_ray_space of the four keys.
    let p_0=cvb3(k0.xyz-ray_org,o);let p_1=cvb3(k1.xyz-ray_org,o);let p_2=cvb3(k2.xyz-ray_org,o);let p_3=cvb3(k3.xyz-ray_org,o);
    let c0=vec4<f32>(cv_dot(space0,p_0,o),cv_dot(space1,p_0,o),cv_dot(space2,p_0,o),k0.w);
    let c1=vec4<f32>(cv_dot(space0,p_1,o),cv_dot(space1,p_1,o),cv_dot(space2,p_1,o),k1.w);
    let c2=vec4<f32>(cv_dot(space0,p_2,o),cv_dot(space1,p_2,o),cv_dot(space2,p_2,o),k2.w);
    let c3=vec4<f32>(cv_dot(space0,p_3,o),cv_dot(space1,p_3,o),cv_dot(space2,p_3,o),k3.w);
    let mx=cv_max4(cv_max4(abs(c0),abs(c1)),cv_max4(abs(c2),abs(c3)));
    let eps=cvb(4.76837158203125e-7*cv_max(cv_max(mx.x,mx.y),cv_max(mx.z,mx.w)),o);
    let step_size=rounded_divide(1.0,f32(N));
    var p0=cv_eval(c0,c1,c2,c3,0.0,o);var dp0dt=cv_derivative(c0,c1,c2,c3,0.0,o).xyz;
    if(cv_max(cv_max(abs(dp0dt.x),abs(dp0dt.y)),abs(dp0dt.z))<eps){dp0dt=cvb4(cv_eval(c0,c1,c2,c3,step_size,o)-p0,o).xyz;}
    var wn0=cvb3(cv_normalize(vec3<f32>(dp0dt.y,-dp0dt.x,0.0),o)*p0.w,o);
    for(var i=0u;i<N;i++){
        let u=cvb(f32(i)*step_size,o);let next=cvb(u+step_size,o);
        let p1=cv_eval(c0,c1,c2,c3,next,o);
        // cylinder_culling_test on the xy of both points with the larger radius.
        let dp=cvb4(p1-p0,o);let r=cv_max(p0.w,p1.w);
        let num=cvb(cvb(dp.x*p0.y,o)-cvb(dp.y*p0.x,o),o);
        let den2=cvb(cvb(dp.x*dp.x,o)+cvb(dp.y*dp.y,o),o);
        let valid=cvb(num*num,o)<=cvb(cvb(r*r,o)*den2,o);
        var dp1dt=cv_derivative(c0,c1,c2,c3,next,o).xyz;
        if(cv_max(cv_max(abs(dp1dt.x),abs(dp1dt.y)),abs(dp1dt.z))<eps){dp1dt=dp.xyz;}
        let wn1=cvb3(cv_normalize(vec3<f32>(dp1dt.y,-dp1dt.x,0.0),o)*p1.w,o);
        if(valid){
            let quad=cv_quad(ray_tmin,ray_tmax,cvb3(p0.xyz+wn0,o),cvb3(p1.xyz+wn1,o),cvb3(p1.xyz-wn1,o),cvb3(p0.xyz-wn0,o),o);
            if(quad.hit){
                // Ignore self intersections (avoidance factor 2).
                let radius=cv_mix(p0.w,p1.w,quad.u,o);
                if(quad.t>cvb(cvb(2.0*radius,o)*invlen,o)){
                    out.hit=true;out.t=quad.t;out.u=cvb(u+cvb(quad.u*step_size,o),o);out.v=cvb(cvb(2.0*quad.v,o)-1.0,o);return out;
                }
            }
        }
        p0=p1;wn0=wn1;
    }
    return out;
}
//#endif
struct CurveHit {hit:bool,t:f32,u:f32,v:f32}
// curve_intersect: the ray is in the space of the curve's object.
//#if CURVE_MOTION else fn curve_intersect(ray_P:vec3<f32>,ray_D:vec3<f32>,tmin:f32,tmax:f32,object:u32,prim:u32,kind:u32)->CurveHit{
fn curve_intersect(ray_P:vec3<f32>,ray_D:vec3<f32>,tmin:f32,tmax:f32,object:u32,prim:u32,kind:u32,time:f32)->CurveHit{
//#endif
    var out:CurveHit;
//#if CURVE_MOTION else     if(prim>=table_count(G_curves)||(kind&PRIMITIVE_MOTION)!=0u){fail();return out;}
    if(prim>=table_count(G_curves)){fail();return out;}
//#endif
    let curve=table_at(G_curves,prim,16u);let first=load_word(curve+4u);let keys=load_word(curve+8u);
    let k0=first+(kind>>PRIMITIVE_NUM_BITS);let k1=k0+1u;let ka=select(k0-1u,first,k0==first);let kb=min(k1+1u,first+keys-1u);
//#if !CURVE_MOTION
    let position_offset=bitcast<i32>(load_word(table_at(G_objects,object,SIZE_objects)+O_position_offset));
    let c0=curve_key(position_offset,ka);let c1=curve_key(position_offset,k0);let c2=curve_key(position_offset,k1);let c3=curve_key(position_offset,kb);
    let o=cv_order();let shape=kind&PRIMITIVE_CURVE;
//#endif
//#if CURVE_MOTION
    let o=cv_order();let shape=kind&PRIMITIVE_CURVE;var c0:vec4<f32>;var c1:vec4<f32>;var c2:vec4<f32>;var c3:vec4<f32>;
    if((kind&PRIMITIVE_MOTION)!=0u){
        let frames=triangle_motion_time(object,time);if(frames.error!=0u){return out;}
        c0=curve_motion_key(object,frames,ka,o);c1=curve_motion_key(object,frames,k0,o);c2=curve_motion_key(object,frames,k1,o);c3=curve_motion_key(object,frames,kb,o);
    }else{
        let position_offset=bitcast<i32>(load_word(table_at(G_objects,object,SIZE_objects)+O_position_offset));
        c0=curve_key(position_offset,ka);c1=curve_key(position_offset,k0);c2=curve_key(position_offset,k1);c3=curve_key(position_offset,kb);
    }
//#endif
//#if CURVE_RIBBON
    if(shape==PRIMITIVE_CURVE_RIBBON){out=cv_ribbon(ray_P,ray_D,tmin,tmax,data_word(D_bvh_curve_subdivisions),c0,c1,c2,c3,o);}
//#endif
//#if CURVE_THICK
    if(shape==PRIMITIVE_CURVE_THICK){out=cv_thick(ray_P,ray_D,tmin,tmax,c0,c1,c2,c3,o);}
//#endif
//#if CURVE_THICK_LINEAR
    if(shape==PRIMITIVE_CURVE_THICK_LINEAR){out=cv_linear(ray_P,ray_D,tmin,tmax,c0,c1,c2,c3,o);}
//#endif
    return out;
}
// bvh_node_intersect: BVH2's unaligned nodes (bvh/bvh2.cpp pack_unaligned_node, kernel/bvh/nodes.h) hold each child's
// space, three rows from int4 1 and three from int4 4, and set PATH_RAY_VISIBILITY_NODE_UNALIGNED on the first word.
fn curve_node_hit(P:vec3<f32>,dir:vec3<f32>,inv:vec3<f32>,tmin:f32,tmax:f32,node:u32,visibility:u32)->vec3<f32>{
    if(node>=table_count(G_bvh_nodes)||table_count(G_bvh_nodes)-node<4u){fail();return vec3<f32>(0);}
    let a=table_at(G_bvh_nodes,node,SIZE_bvh_nodes);
    if((load_word(a)&PATH_RAY_VISIBILITY_NODE_UNALIGNED)==0u){return bounds_hit(P,inv,tmin,tmax,node,visibility);}
    if(table_count(G_bvh_nodes)-node<7u){fail();return vec3<f32>(0);}
    var mask=0u;var dist=vec2<f32>(0);let o=cv_order();
    // The two children through one body (rounded_steps).
    let count=rounded_steps(2u);
    for(var child=0u;child<count;child++){
        if(child>=2u){continue;}
        let base=a+16u+child*48u;let x=heap_vec4(base);let y=heap_vec4(base+16u);let z=heap_vec4(base+32u);
        let aligned_dir=transform_rows(x,y,z,dir,false);let aligned_P=transform_rows(x,y,z,P,true);
        var nrdir=vec3<f32>(0);let lanes=rounded_steps(3u);
        for(var k=0u;k<lanes;k++){
            let q=rounded_divide(1.0,select(select(aligned_dir.z,aligned_dir.y,k==1u),aligned_dir.x,k==0u));
            nrdir=select(nrdir,vec3<f32>(-q),vec3<u32>(0u,1u,2u)==vec3<u32>(k));
        }
        let lower=cvb3(aligned_P*nrdir,o);let upper=cvb3(lower-nrdir,o);
        let near_v=select(upper,lower,lower<upper);let far_v=select(upper,lower,lower>upper);
        let tnear=cv_max(cv_max(tmin,near_v.x),cv_max(near_v.y,near_v.z));
        let tfar=cv_min(cv_min(tmax,far_v.x),cv_min(far_v.y,far_v.z));
        dist=select(vec2<f32>(dist.x,tnear),vec2<f32>(tnear,dist.y),child==0u);
        if(tnear<=tfar&&(load_word(a+child*4u)&visibility)!=0u){mask|=1u<<child;}
    }
    return vec3<f32>(f32(mask),dist.x,dist.y);
}
//#endif
