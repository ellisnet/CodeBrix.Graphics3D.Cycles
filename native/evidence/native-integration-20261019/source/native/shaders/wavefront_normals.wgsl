// SPDX-FileCopyrightText: 2011-2025 Blender Foundation
// SPDX-FileCopyrightText: 2011-2013 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles svm/tex_coord.h, geom/triangle.h, geom/object.h,
// svm/util.h and util/math_dual.h. Native payload layouts, checked attribute
// loads and explicit dual vectors replace C++ pointers and templates.
struct NormalDual {value:vec3<f32>, dx:vec3<f32>, dy:vec3<f32>,}
fn normal_transpose(address:u32,v:vec3<f32>)->vec3<f32>{
    return v.x*heap_vec3(address)+v.y*heap_vec3(address+16u)+v.z*heap_vec3(address+32u);
}
fn normal_object_transpose(sd:SurfaceData,v:vec3<f32>,inverse:bool)->vec3<f32>{
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){if(inverse){return motion_transpose(sd.motion_itfm,v);}return motion_transpose(sd.motion_tfm,v);}
    let object=table_at(G_objects,sd.object_id,SIZE_objects);return normal_transpose(object+select(O_tfm,O_itfm,inverse),v);
}
fn normal_attribute(s:ptr<function,SvmState>,id:u32)->AttributeDesc{
    return attribute_find(s,id,select(0u,0xffffffffu,(id&0x80000000u)!=0u));
}
fn normal_finite(v:vec3<f32>)->bool{
    return all((bitcast<vec3<u32>>(v)&vec3<u32>(0x7f800000u))!=vec3<u32>(0x7f800000u));
}
fn normal_smooth_object(s:ptr<function,SvmState>,object:u32)->vec3<f32>{
    let sd=(*s).geometry;var indices:vec3<u32>;
    if((sd.object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0u){
        if(sd.prim>(0xffffffffu-2u)/3u){svm_error(s,64u);return sd.N;}
        indices=vec3<u32>(3u*sd.prim)+vec3<u32>(0,1,2);
    }else{
        if(sd.prim>=table_count(G_tri_vindex)){svm_error(s,64u);return sd.N;}
        let at=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);
        indices=vec3<u32>(load_word(at),load_word(at+4u),load_word(at+8u));
    }
    let desc=AttributeDesc(ATTR_ELEMENT_VERTEX_NORMAL,NODE_ATTR_FLOAT3,bitcast<i32>(load_word(object+O_normal_offset)),true);
    var n:array<vec3<f32>,3>;
    for(var j=0u;j<3u;j++){
        n[j]=attribute_fetch(s,desc,indices[j]).xyz;
        if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u){n[j]=surface_normalize(normal_object_transpose(sd,n[j],false));}
    }
    let result=surface_normalize(((1.0-sd.u)-sd.v)*n[0]+sd.u*n[1]+sd.v*n[2]);
    return select(result,sd.Ng,all(result==vec3<f32>(0)));
}
fn svm_normal_map(s:ptr<function,SvmState>,node:u32){
    let space=load_word(node+N_NormalMap_space);let invert=load_word(node+N_NormalMap_invert_green);
    let original=load_word(node+N_NormalMap_use_original_base);let out=svm_byte(node+N_NormalMap_normal_offset);
    if(space>NODE_NORMAL_MAP_BLENDER_WORLD||invert>1u||original>1u){svm_error(s,16u);return;}
    var color=2.0*(svm_input3(s,node+N_NormalMap_color)-vec3<f32>(0.5));
    var strength=svm_input(s,node+N_NormalMap_strength);
    if(invert!=0u){color.y=-color.y;}
    let sd=(*s).geometry;let backface=(sd.runtime_flag&SR_BACKFACING)!=0u;
    var object=0u;
    if(sd.object_id!=NONE){
        if(sd.object_id>=table_count(G_objects)){svm_error(s,64u);return;}
        object=table_at(G_objects,sd.object_id,SIZE_objects);
    }
    var N:vec3<f32>;var linear=false;var invalid_normal=false;
    if(space==NODE_NORMAL_MAP_TANGENT){
        if(sd.object_id==NONE||(sd.primitive_type&PRIMITIVE_TRIANGLE)==0u){svm_store3(s,out,sd.N);return;}
        let attr=normal_attribute(s,load_word(node+N_NormalMap_attr));
        let sign_attr=normal_attribute(s,load_word(node+N_NormalMap_attr_sign));
        if(!attr.found||!sign_attr.found){svm_store3(s,out,sd.N);return;}
        if(attr.kind!=NODE_ATTR_FLOAT3||sign_attr.kind!=NODE_ATTR_FLOAT){svm_error(s,16u);return;}
        let tangent=attribute_read(s,attr).value.xyz;let sign=attribute_read(s,sign_attr).value.x;
        var normal:vec3<f32>;
        if((sd.shader&SHADER_SMOOTH_NORMAL)!=0u){
            var undisplaced:AttributeDesc;
            if(original!=0u){undisplaced=attribute_find(s,ATTR_STD_NORMAL_UNDISPLACED,0u);}
            if(undisplaced.found){
                if(undisplaced.kind!=NODE_ATTR_FLOAT3){svm_error(s,16u);return;}
                normal=attribute_read(s,undisplaced).value.xyz;linear=true;
            }else{normal=normal_smooth_object(s,object);}
        }else{
            normal=select(sd.Ng,-sd.Ng,backface);
            normal=surface_normalize(normal_object_transpose(sd,normal,false));
        }
        if(!linear){color.x*=strength;color.y*=strength;color.z=1.0+clamp(strength,0.0,1.0)*(color.z-1.0);}
        let B=sign*cross(normal,tangent);
        N=surface_normalize(color.x*tangent+color.y*B+color.z*normal);
        invalid_normal=all(N==vec3<f32>(0));
        N=surface_normalize(normal_object_transpose(sd,N,true));
    }else{
        linear=true;
        if(space==NODE_NORMAL_MAP_BLENDER_OBJECT||space==NODE_NORMAL_MAP_BLENDER_WORLD){color.y=-color.y;color.z=-color.z;}
        N=color;
        if(space==NODE_NORMAL_MAP_OBJECT||space==NODE_NORMAL_MAP_BLENDER_OBJECT){
            if(sd.object_id!=NONE){invalid_normal=all(N==vec3<f32>(0));N=surface_normalize(normal_object_transpose(sd,N,true));}
        }else{N=surface_normalize(N);}
    }
    if(backface){N=-N;}
    if(linear&&strength!=1.0){strength=max(strength,0.0);N=surface_normalize(sd.N+(N-sd.N)*strength);}
    if(invalid_normal||all(N==vec3<f32>(0))||!normal_finite(N)){N=sd.N;}
    svm_store3(s,out,N);
}
fn normal_dual_normalize(a:NormalDual)->NormalDual{
    // Cycles dual inversesqrt returns zero for nonpositive squared length.
    let len2=dot(a.value,a.value);var inv=0.0;var slope=0.0;
    if(len2>0.0){inv=1.0/sqrt(len2);slope=-0.5*(inv/len2);}
    let dx=dot(a.value,a.dx)+dot(a.dx,a.value);let dy=dot(a.value,a.dy)+dot(a.dy,a.value);
    return NormalDual(a.value*inv,a.value*(dx*slope)+a.dx*inv,a.value*(dy*slope)+a.dy*inv);
}
fn normal_radial(v:vec3<f32>,axis:u32,center:f32)->vec3<f32>{
    if(axis==NODE_TANGENT_AXIS_X){return vec3<f32>(0,-(v.z-center),v.y-center);}
    if(axis==NODE_TANGENT_AXIS_Y){return vec3<f32>(-(v.z-center),0,v.x-center);}
    return vec3<f32>(-(v.y-center),v.x-center,0);
}
fn svm_tangent(s:ptr<function,SvmState>,node:u32,derivative:bool){
    let kind=load_word(node+N_Tangent_direction_type);let axis=load_word(node+N_Tangent_axis);
    if(kind>NODE_TANGENT_UVMAP||axis>NODE_TANGENT_AXIS_Z){svm_error(s,16u);return;}
    let sd=(*s).geometry;let out=svm_byte(node+N_Tangent_tangent_offset);
    let desc=normal_attribute(s,load_word(node+N_Tangent_attr));var tangent:NormalDual;
    if(kind==NODE_TANGENT_UVMAP&&!desc.found){
        svm_store3(s,out,vec3<f32>(0));
        if(derivative){svm_store3(s,out+3u,vec3<f32>(0));svm_store3(s,out+6u,vec3<f32>(0));}return;
    }
    if(desc.found){
        if(desc.kind!=NODE_ATTR_FLOAT2&&desc.kind!=NODE_ATTR_FLOAT3){svm_error(s,16u);return;}
        let data=attribute_read(s,desc);tangent=NormalDual(data.value.xyz,data.dx.xyz,data.dy.xyz);
    }else{tangent=NormalDual(sd.P,sd.dPdu*sd.du.x+sd.dPdv*sd.dv.x,sd.dPdu*sd.du.y+sd.dPdv*sd.dv.y);}
    if(kind==NODE_TANGENT_RADIAL){tangent=NormalDual(normal_radial(tangent.value,axis,0.5),normal_radial(tangent.dx,axis,0.0),normal_radial(tangent.dy,axis,0.0));}
    if((*s).error!=0u){return;}
    if(sd.object_id!=NONE){
        let object=table_at(G_objects,sd.object_id,SIZE_objects);
        tangent=NormalDual(normal_object_transpose(sd,tangent.value,true),normal_object_transpose(sd,tangent.dx,true),normal_object_transpose(sd,tangent.dy,true));
        if(derivative){tangent=normal_dual_normalize(tangent);}else{tangent.value=surface_normalize(tangent.value);}
    }
    tangent=NormalDual(cross(tangent.value,sd.N),cross(tangent.dx,sd.N),cross(tangent.dy,sd.N));
    if(derivative){tangent=normal_dual_normalize(tangent);}else{
        // The plain native path produces nonfinite values at a degenerate frame.
        // Reject that case explicitly rather than returning a fabricated tangent.
        if(dot(tangent.value,tangent.value)==0.0){svm_error(s,16u);return;}
        tangent.value=surface_normalize(tangent.value);
    }
    svm_store3(s,out,cross(sd.N,tangent.value));
    if(derivative){svm_store3(s,out+3u,cross(sd.N,tangent.dx));svm_store3(s,out+6u,cross(sd.N,tangent.dy));}
}
