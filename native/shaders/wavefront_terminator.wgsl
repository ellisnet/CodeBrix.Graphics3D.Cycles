// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2004 NVIDIA Corporation
// SPDX-FileCopyrightText: 2008-2014 Larry Gritz
// SPDX-FileCopyrightText: 2009-2014 Sony Pictures Imageworks Inc., et al.
// Copyright (c) 2019 Francois Beaune, The appleseedhq Organization
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause AND MIT
// Modified from Cycles: object shadow terminator offsets in WGSL.
// Geometry offset: kernel/light/sample.h shadow_ray_smooth_surface_offset and
// shadow_ray_offset (Apache-2.0; shadow-ray origin lifted along the
// interpolated normal). Shading offset: kernel/closure/bsdf.h shift_cos_in,
// which Cycles took from Appleseed (MIT, notice above), and util/math_fast.h
// fast_acosf (BSD-3-Clause, adapted from OpenImageIO). Floating
// operations keep the native order with the reviewed binary32 adapters.
// Scene specialization (pipelines.h SHADOW_TERMINATOR_*) removes the callers'
// regions when no object needs an offset. See native/THIRD-PARTY-NOTICES.txt.
fn terminator_object(sd:SurfaceData)->u32{return table_at(G_objects,sd.object_id,SIZE_objects);}
// Native kobject.shadow_terminator_shading_offset is the frequency multiplier
// 1/(1-offset/2); values above 1 enable the shading term.
fn terminator_frequency(sd:SurfaceData)->f32{return heap_float(terminator_object(sd)+O_shadow_terminator_shading_offset);}
fn terminator_fast_acos(x:f32)->f32{
    let f=abs(x);
    // Clamp and crush denormals: 1-(1-f), kept as two binary32 additions.
    let m=select(1.0,rounded_add(1.0,-rounded_add(1.0,-f)),f<1.0);
    let polynomial=rounded_add(1.5707963267,m*rounded_add(-0.213300989,m*rounded_add(0.077980478,m*-0.02164095)));
    let a=rounded_sqrt(rounded_add(1.0,-m))*polynomial;
    return select(a,rounded_add(3.1415927410125732,-a),x<0.0);
}
// shift_cos_in. Callers pass frequency in (1,2] (the device rejects larger
// multipliers), so angle*frequency stays inside native_disk_sincos' [-pi,pi].
fn terminator_shift_cos_in(cos_value:f32,frequency:f32)->f32{
    let cos_in=min(cos_value,1.0);let angle=terminator_fast_acos(cos_in);
//#if SHADOW_TERMINATOR_WIDE else     return rounded_divide(max(native_disk_sincos(angle*frequency).y,0.0),cos_in);
    return rounded_divide(max(terminator_wide_cos(angle*frequency),0.0),cos_in);
//#endif
}
//#if SHADOW_TERMINATOR_WIDE
// Shading offsets in (1, 2] (multiplier above 2, infinite at 2; above 2 the multiplier is
// negative and the kernel skips the term): cosf of
// angle * multiplier beyond [-pi, pi] is a correctly rounded cosf (glibc's is, but for a small share of arguments), the double-float
// series of the two-part reduced angle rounded once; inside [-pi, pi] the platform
// polynomial as for offsets up to 1. A non-finite argument (offset 2) gives NaN on the
// CPU, which fmaxf(NaN, 0) turns into 0: returned as -1 here, so max(.., 0) is 0.
fn terminator_wide_cos(x:f32)->f32{
    if(!(abs(x)<=3.4028234663852886e38)){return -1.0;}
    if(abs(x)<=3.1415927410125732){return native_disk_sincos(x).y;}
    let n=i32(floor(x*0.6366197723675814+0.5));let product=trig_product(f32(n),1.5707963705062866);
    let head=trig_two_sum(x,-product.x);let r=trig_two_sum(head.x,rounded_add(head.y,rounded_add(-product.y,f32(n)*4.3711390001862426e-8)));
    let sc=select(trig_sincos_series(r),vec4<f32>(r,1.0,0.0),abs(r.x)<1e-6);let q=((n%4)+4)%4;var cs=sc.zw;
    if(q==1){cs=-sc.xy;}else if(q==2){cs=-sc.zw;}else if(q==3){cs=sc.xy;}
    return rounded_add(cs.x,cs.y);
}
//#endif
// Raw triangle normals for the offset: center frame, or the interpolated and
// normalized pair of frames for deforming triangles (motion_triangle_normals).
fn terminator_normals(sd:SurfaceData)->array<vec3<f32>,3>{
    var normals:array<vec3<f32>,3>;let object=terminator_object(sd);
    let vi_address=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);
    var ni=vec3<u32>(load_word(vi_address),load_word(vi_address+4u),load_word(vi_address+8u));
    if((sd.object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0u){ni=vec3<u32>(sd.prim*3u)+vec3<u32>(0,1,2);}
    let offset=bitcast<i32>(load_word(object+O_normal_offset));let nc=table_count(G_attributes_normal);
    var stride=load_word(object+O_numverts);if((sd.object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0u){stride=3u*load_word(object+O_numprims);}
    var frame:TriangleTime;if(sd.primitive_type==PRIMITIVE_MOTION_TRIANGLE){frame=triangle_motion_time(sd.object_id,sd.time);}
    for(var j=0u;j<3u;j++){
        let at=triangle_frame_index(ni[j],offset,frame.a,stride,nc);if(at==NONE){return normals;}
        normals[j]=surface_decode_normal(load_word(table_at(G_attributes_normal,at,SIZE_attributes_normal)));
        if(sd.primitive_type==PRIMITIVE_MOTION_TRIANGLE){
            let next=triangle_frame_index(ni[j],offset,frame.b,stride,nc);if(next==NONE){return normals;}
            let n=surface_decode_normal(load_word(table_at(G_attributes_normal,next,SIZE_attributes_normal)));
            normals[j]=surface_native_normalize(rounded_add3((1.0-frame.t)*normals[j],frame.t*n));
        }
    }
    return normals;
}
fn terminator_mix(a:vec3<f32>,b:vec3<f32>,c:vec3<f32>,u:f32,v:f32,w:f32)->vec3<f32>{return rounded_add3(rounded_add3(a*u,b*v),c*w);}
// shadow_ray_smooth_surface_offset: local-space parabolic lift of the smooth
// surface, returned along the (world-space) interpolated normal.
fn terminator_smooth_offset(sd:SurfaceData,Ng:vec3<f32>)->vec3<f32>{
    let vertices=native_triangle_vertices(sd.object_id,sd.prim,sd.primitive_type,sd.time);
    let N=terminator_normals(sd);let V=array<vec3<f32>,3>(vertices.a,vertices.b,vertices.c);
    let u=rounded_add(rounded_add(1.0,-sd.u),-sd.v);let v=sd.u;let w=sd.v;
    let P=terminator_mix(V[0],V[1],V[2],u,v,w);var n=terminator_mix(N[0],N[1],N[2],u,v,w);
    if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)==0u){n=surface_object_direction(sd,n,false);}
    let a=rounded_dot(N[2]-N[0],V[0]-V[2]);let b=rounded_dot(N[2]-N[1],V[1]-V[2]);let c=rounded_dot(N[1]-N[0],V[1]-V[0]);
    var h=rounded_add(rounded_add((a*u)*rounded_add(u,-1.0),((rounded_add(rounded_add(a,b),c))*u)*v),(b*v)*rounded_add(v,-1.0));
    if(rounded_dot(n,Ng)>0.0){
        var h0=max(max(rounded_dot(V[1]-V[0],N[0]),rounded_dot(V[2]-V[0],N[0])),0.0);
        var h1=max(max(rounded_dot(V[0]-V[1],N[1]),rounded_dot(V[2]-V[1],N[1])),0.0);
        var h2=max(max(rounded_dot(V[0]-V[2],N[2]),rounded_dot(V[1]-V[2],N[2])),0.0);
        h0=max(rounded_add(rounded_dot(V[0]-P,N[0]),h0),0.0);h1=max(rounded_add(rounded_dot(V[1]-P,N[1]),h1),0.0);h2=max(rounded_add(rounded_dot(V[2]-P,N[2]),h2),0.0);
        h=max(min(min(h0,h1),h2),h*0.5);
    }else{
        var h0=max(max(rounded_dot(V[0]-V[1],N[0]),rounded_dot(V[0]-V[2],N[0])),0.0);
        var h1=max(max(rounded_dot(V[1]-V[0],N[1]),rounded_dot(V[1]-V[2],N[1])),0.0);
        var h2=max(max(rounded_dot(V[2]-V[0],N[2]),rounded_dot(V[2]-V[1],N[2])),0.0);
        h0=max(rounded_add(rounded_dot(P-V[0],N[0]),h0),0.0);h1=max(rounded_add(rounded_dot(P-V[1],N[1]),h1),0.0);h2=max(rounded_add(rounded_dot(P-V[2],N[2]),h2),0.0);
        h=min(-min(min(h0,h1),h2),h*0.5);
    }
    return n*h;
}
// True when shadow_ray_offset applies the geometry offset to this hit.
fn terminator_geometry_active(sd:SurfaceData)->bool{
    return native_triangle_kind(sd.primitive_type)&&(sd.shader&SHADER_SMOOTH_NORMAL)!=0u&&heap_float(terminator_object(sd)+O_shadow_terminator_geometry_offset)>0.0;
}
// shadow_ray_offset: xyz is the shadow-ray origin, w is 1 when the ray keeps
// skipping its own primitive (Cycles' skip_self).
fn terminator_shadow_origin(sd:SurfaceData,L:vec3<f32>)->vec4<f32>{
    let cutoff=heap_float(terminator_object(sd)+O_shadow_terminator_geometry_offset);
    var NL=rounded_dot(sd.N,L);let transmit=NL<0.0;if(NL<0.0){NL=-NL;}
    let Ng=select(sd.Ng,-sd.Ng,transmit);let NgL=rounded_dot(Ng,L);
    let amount=select(clamp(rounded_add(1.0,-rounded_divide(NgL,cutoff)),0.0,1.0),
                      clamp(rounded_add(2.0,-rounded_divide(rounded_add(NgL,NL),cutoff)),0.0,1.0),NL<cutoff);
    if(amount>0.0){return vec4<f32>(rounded_add3(sd.P,terminator_smooth_offset(sd,Ng)*amount),select(0.0,1.0,NgL>0.0));}
    return vec4<f32>(sd.P,1.0);
}
