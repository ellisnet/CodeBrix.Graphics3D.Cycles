//#if CURVES
// SPDX-FileCopyrightText: 2009-2021 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles kernel/geom/curve_intersect.h curve_shader_setup and kernel/geom/shader_data.h
// shader_setup_from_ray (the curve branch), for static curves. Arithmetic as in wavefront_curve_math.wgsl.
// differential_dudv_compact with every sum and product through the order word: a ribbon's dPdu and dPdv can be nearly
// parallel in the projection, where a fused determinant moves du and dv far past the last bit.
fn curve_differentials(sd:ptr<function,SurfaceData>,o:u32){
    let n=(*sd).Ng;let tangent=surface_orthogonal(n);
    var dx=cvb3(tangent*(*sd).dP,o);var dy=cvb3(cv_cross(n,tangent,o)*(*sd).dP,o);
    var a=(*sd).dPdu;var b=(*sd).dPdv;let axis=abs(n);
    if(axis.z<axis.x||axis.z<axis.y){
        if(axis.y<axis.x||axis.y<axis.z){a.x=a.y;b.x=b.y;dx.x=dx.y;dy.x=dy.y;}
        a.y=a.z;b.y=b.z;dx.y=dx.z;dy.y=dy.z;
    }
    var det=cvb(cvb(a.x*b.y,o)-cvb(b.x*a.y,o),o);if(det!=0.0){det=rounded_divide(1.0,det);}
    (*sd).du=vec2<f32>(cvb(cvb(cvb(dx.x*b.y,o)-cvb(dx.y*b.x,o),o)*det,o),cvb(cvb(cvb(dy.x*b.y,o)-cvb(dy.y*b.x,o),o)*det,o));
    (*sd).dv=vec2<f32>(cvb(cvb(cvb(dx.y*a.x,o)-cvb(dx.x*a.y,o),o)*det,o),cvb(cvb(cvb(dy.y*a.x,o)-cvb(dy.x*a.y,o),o)*det,o));
}
fn curve_setup_from_ray(index:u32)->SurfaceData{
    var sd:SurfaceData;let o=cv_order();
    sd.primitive_type=state_word(S_isect_type,index);sd.object_id=state_word(S_isect_object,index);sd.prim=state_word(S_isect_prim,index);
    if((sd.primitive_type&PRIMITIVE_MOTION)!=0u||sd.object_id>=table_count(G_objects)||sd.object_id>=table_count(G_object_flag)||sd.prim>=table_count(G_curves)){surface_failure(&sd);return sd;}
    sd.object_flag=load_word(table_at(G_object_flag,sd.object_id,4u));
    sd.u=heap_float(state_address(S_isect_u,index,4u));sd.v=heap_float(state_address(S_isect_v,index,4u));
    var t=heap_float(state_address(S_isect_t,index,4u));sd.ray_length=t;sd.time=heap_float(state_address(S_ray_time,index,4u));
    let ray_D=heap_vec3(state_address(S_ray_D,index,12u));sd.wi=-ray_D;
    var P=heap_vec3(state_address(S_ray_P,index,12u));var D=ray_D;
    let applied=(sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
    if(!applied){
        P=surface_object_point(sd,P,true);D=surface_object_direction(sd,cvb3(D*t,o),true);
        // safe_normalize_len.
        t=cv_len(D,o);if(t!=0.0){D=cv_div3(D,t,o);}
    }
    let curve=table_at(G_curves,sd.prim,16u);let first=load_word(curve+4u);let keys=load_word(curve+8u);
    let k0=first+(sd.primitive_type>>PRIMITIVE_NUM_BITS);let k1=k0+1u;let ka=select(k0-1u,first,k0==first);let kb=min(k1+1u,first+keys-1u);
    let position_offset=bitcast<i32>(load_word(table_at(G_objects,sd.object_id,SIZE_objects)+O_position_offset));
    let c0=curve_key(position_offset,ka);let c1=curve_key(position_offset,k0);let c2=curve_key(position_offset,k1);let c3=curve_key(position_offset,kb);
    if(load_word(p(17u))!=0u){sd.error=1u;return sd;}
    P=cvb3(P+cvb3(D*t,o),o);
    let shape=sd.primitive_type&PRIMITIVE_CURVE;let linear=shape==PRIMITIVE_CURVE_THICK_LINEAR;let ribbon=shape==PRIMITIVE_CURVE_RIBBON;
    var dPdu=vec3<f32>(0);
    if(linear){dPdu=cvb3(c2.xyz-c1.xyz,o);}else{dPdu=cv_derivative(c0,c1,c2,c3,sd.u,o).xyz;}
    if(ribbon){
        // Rounded smooth normals for ribbons, to approximate the thick curve shape.
        let tangent=cv_normalize(dPdu,o);let bitangent=cv_normalize(cv_cross(tangent,-D,o),o);
        let sine=sd.v;let cosine=rounded_sqrt(cv_max(cvb(1.0-cvb(sine*sine,o),o),0.0));
        sd.N=cv_normalize(cvb3(cvb3(sine*bitangent,o)-cvb3(cosine*cv_normalize(cv_cross(tangent,bitangent,o),o),o),o),o);
    }else{
        // Thick curves: the direction from inside the curve (the view direction when P is the inside point).
        var inside=vec3<f32>(0);
        if(linear){inside=cv_mix4(c1,c2,sd.u,o).xyz;}else{inside=cv_eval(c0,c1,c2,c3,sd.u,o).xyz;}
        if(any(P!=inside)){sd.N=cv_normalize(cvb3(P-inside,o),o);}else{sd.N=-sd.wi;}
        sd.v=0.0;
    }
    sd.dPdu=dPdu;
    if(!applied){
        P=surface_object_point(sd,P,false);sd.N=surface_object_normal(sd,sd.N,false);sd.dPdu=surface_object_direction(sd,sd.dPdu,false);
    }
    sd.P=P;sd.Ng=select(sd.N,sd.wi,ribbon);sd.dPdv=cv_cross(sd.dPdu,sd.Ng,o);
    sd.shader=load_word(curve);let shader_id=sd.shader&SHADER_MASK;
    if(shader_id>=table_count(G_shaders)){surface_failure(&sd);return sd;}
    sd.shader_flag=load_word(table_at(G_shaders,shader_id,SIZE_shaders)+SH_flags);
    if(cv_dot(sd.Ng,sd.wi,o)<0.0){sd.runtime_flag|=SR_BACKFACING;sd.N=-sd.N;sd.Ng=-sd.Ng;sd.dPdu=-sd.dPdu;sd.dPdv=-sd.dPdv;}
    sd.dI=heap_float(state_address(S_ray_dD,index,4u));
    sd.dP=rounded_add(heap_float(state_address(S_ray_dP,index,4u)),sd.ray_length*sd.dI);curve_differentials(&sd,o);
    return sd;
}
//#endif
