// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native comparison uses Cycles' SSE4.2 CPU oracle. Its ray_triangle_dot
// groups y/z before x, unlike the generic scalar AST branch. This adapter
// preserves that grouping for the surface self-intersection test.
fn c_ray_triangle_dot(a:vec3<f32>,b:vec3<f32>)->f32{
    return a.x*b.x+(a.y*b.y+a.z*b.z);
}
// Contraction-proof ray/triangle intersection (util/math_intersect.h
// ray_triangle_intersect, Plücker form). Every product is rounded before it
// reaches a sum (rounded_add is integer arithmetic), so no driver can fuse it,
// and every sum keeps the originating CPU oracle's grouping:
// NATIVE_TRIANGLE_ARITHMETIC 0 = scalar build (no __KERNEL_SSE__, e.g. ARM64
// without SSE2NEON): dot (x+y)+z, cross a*b-c*d, reciprocal 1/x correctly
// rounded. 1 = x86 SSE4.2 build: ray_triangle_dot x+(y+z), unfused madd/msub,
// reciprocal = the host's RCPSS estimate plus one Newton step r*(2-r*x), each
// operation rounded. The estimate model below was verified bit for bit on the
// generating host when the layout was emitted (2 = SSE build whose RCPSS
// differs from the model: correctly rounded 1/x, not oracle-exact).
fn exact_tri_dot(a:vec3<f32>,b:vec3<f32>)->f32{
    if(NATIVE_TRIANGLE_ARITHMETIC==0u){return rounded_add(rounded_add(a.x*b.x,a.y*b.y),a.z*b.z);}
    return rounded_add(a.x*b.x,rounded_add(a.y*b.y,a.z*b.z));
}
fn exact_tri_cross(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{
    return vec3<f32>(rounded_add(a.y*b.z,-(a.z*b.y)),rounded_add(a.z*b.x,-(a.x*b.z)),rounded_add(a.x*b.y,-(a.y*b.x)));
}
// RCPSS model: the result depends only on the sign, the exponent and the top
// 11 mantissa bits; its 12-bit mantissa is the nearest value to 2^25/(4097+2i).
// Zero/denormal inputs give infinity, infinities give zero, underflow flushes.
fn exact_rcpss(x:f32)->f32{
    let bits=bitcast<u32>(x);let sign=bits&0x80000000u;let e=(bits>>23u)&255u;let m=bits&0x7fffffu;
    if(e==255u){return bitcast<f32>(select(sign,bits|0x400000u,m!=0u));}
    if(e==0u){return bitcast<f32>(sign|0x7f800000u);}
    if(e>=253u){return bitcast<f32>(sign);}
    let d=4097u+2u*(m>>12u);let q=((1u<<26u)+d)/(2u*d);
    return bitcast<f32>(sign|((253u-e)<<23u)|((q-4096u)<<11u));
}
fn exact_tri_reciprocal(x:f32)->f32{
    if(NATIVE_TRIANGLE_ARITHMETIC==1u){let r=exact_rcpss(x);return r*rounded_add(2.0,-(r*x));}
    return rounded_divide(1.0,x);
}
fn c_ray_triangle_intersect(ray_P:vec3<f32>,ray_D:vec3<f32>,ray_tmin:f32,ray_tmax:f32,tri_a:vec3<f32>,tri_b:vec3<f32>,tri_c:vec3<f32>,isect_u:ptr<function,f32>,isect_v:ptr<function,f32>,isect_t:ptr<function,f32>)->bool{
    let v0=tri_a-ray_P;let v1=tri_b-ray_P;let v2=tri_c-ray_P;
    let e0=v2-v0;let e1=v0-v1;let e2=v1-v2;
    // One call site for the three edge functions keeps the inlined size small.
    let edges=array<vec3<f32>,3>(e0,e1,e2);let sums=array<vec3<f32>,3>(v2+v0,v0+v1,v1+v2);var uvw:array<f32,3>;
    for(var i=0u;i<3u;i++){uvw[i]=exact_tri_dot(exact_tri_cross(edges[i],sums[i]),ray_D);}
    let U=uvw[0];let V=uvw[1];let W=uvw[2];
    let UVW=U+V+W;let eps=1.1920929e-7*abs(UVW);
    // Builtin min/max equal Cycles' (a<b)?a:b for every non-NaN input. A select()
    // form also mirrors NaN, but it triggered an intermittent Intel Mesa 25.0.7
    // backend crash (brw_opt_combine_constants) in the triangle-light module.
    let minUVW=min(U,min(V,W));let maxUVW=max(U,max(V,W));
    if(!(minUVW>=-eps||maxUVW<=eps)){return false;}
    let Ng1=exact_tri_cross(e1,e0);let Ng=Ng1+Ng1;
    let den=rounded_dot(Ng,ray_D);if(den==0.0){return false;}
    let T=rounded_dot(v0,Ng);let t=rounded_divide(T,den);
    if(!(t>=ray_tmin&&t<=ray_tmax)){return false;}
    var rcp_uvw=0.0;if(!(abs(UVW)<1e-18)){rcp_uvw=exact_tri_reciprocal(UVW);}
    let u=U*rcp_uvw;let v=V*rcp_uvw;
    *isect_u=min(u,1.0);*isect_v=min(v,1.0);*isect_t=t;
    return true;
}
