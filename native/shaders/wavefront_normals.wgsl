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
//#if SVM_GEOMETRY_TANGENT
// primitive_tangent (geom/primitive.h) for the Geometry node's Tangent output, plain or with derivatives (dual3): on a
// curve or a point normalize(dPdu); on a triangle the radial tangent of the generated coordinates, (-(y - 0.5), x - 0.5, 0) through
// object_normal_transform, then cross(N, normalize(cross(t, N))); without generated coordinates normalize(dPdu). The value
// follows the pinned float3 order exactly (transform_direction_transposed's dots, normalize, the SSE cross), so a frame
// that is degenerate on the CPU (t parallel to N: exactly zero, then normalize's 0/0) is degenerate here too; the
// derivatives use the dual arithmetic of the Tangent node.
fn svm_geometry_normal_transform(sd:SurfaceData,v:vec3<f32>)->vec3<f32>{
    // object_normal_transform: normalize(transform_direction_transposed(itfm, v)), the motion inverse for a moving object.
    var rows:NativeTransform;
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){rows=sd.motion_itfm;}
    else{let at=table_at(G_objects,sd.object_id,SIZE_objects)+O_itfm;rows=NativeTransform(heap_vec4(at),heap_vec4(at+16u),heap_vec4(at+32u));}
    var out=vec3<f32>(0);let n=rounded_steps(3u);
    for(var k=0u;k<n;k++){out[k]=rounded_dot(vec3<f32>(rows.x[k],rows.y[k],rows.z[k]),v);}
    return surface_native_normalize(out);
}
fn svm_geometry_tangent(s:ptr<function,SvmState>,derivative:bool)->NormalDual{
    let sd=(*s).geometry;let zero=vec3<f32>(0);
//#if CURVES
    if((sd.primitive_type&PRIMITIVE_CURVE)!=0u){return NormalDual(cv_normalize(sd.dPdu,cv_order()),zero,zero);}
//#endif
    if(sd.object_id==NONE){return NormalDual(normalize(sd.dPdu),zero,zero);}
//#if POINTS
    // On a point normalize(dPdu) too: the frame point_shader_setup builds about the normal.
    if((sd.primitive_type&PRIMITIVE_POINT)!=0u){return NormalDual(pt_normalize(sd.dPdu,pt_order()),zero,zero);}
//#endif
    if((sd.primitive_type&PRIMITIVE_TRIANGLE)==0u){svm_error(s,16u);return NormalDual(zero,zero,zero);}
    let desc=normal_attribute(s,ATTR_STD_GENERATED);
    if(!desc.found){return NormalDual(surface_native_normalize(sd.dPdu),zero,zero);}
    let a=attribute_read(s,desc);if((*s).error!=0u){return NormalDual(zero,zero,zero);}
    let tv=svm_geometry_normal_transform(sd,vec3<f32>(-rounded_add(a.value.y,-0.5),rounded_add(a.value.x,-0.5),0.0));
    let value=rounded_cross(sd.N,surface_native_normalize(rounded_cross(tv,sd.N)));
    if(!derivative){return NormalDual(value,zero,zero);}
    var t=NormalDual(vec3<f32>(-(a.value.y-0.5),a.value.x-0.5,0.0),vec3<f32>(-a.dx.y,a.dx.x,0.0),vec3<f32>(-a.dy.y,a.dy.x,0.0));
    t=NormalDual(normal_object_transpose(sd,t.value,true),normal_object_transpose(sd,t.dx,true),normal_object_transpose(sd,t.dy,true));
    t=normal_dual_normalize(t);
    t=normal_dual_normalize(NormalDual(cross(t.value,sd.N),cross(t.dx,sd.N),cross(t.dy,sd.N)));
    return NormalDual(value,cross(sd.N,t.dx),cross(sd.N,t.dy));
}
//#endif
//#if SVM_NODE_TANGENT
// util/math_dual.h for the Tangent node's form with derivatives (svm_tangent), every sum and quotient rounded on its own.
// transform_direction_transposed of a dual (to_local: dot(p, X) = reduce_add(p * X), (x + y) + z).
fn tn_transposed(rows:NativeTransform,v:vec3<f32>)->vec3<f32>{
    var o=vec3<f32>(0);let n=rounded_steps(3u);
    for(var k=0u;k<n;k++){o[k]=rounded_dot(vec3<f32>(rows.x[k],rows.y[k],rows.z[k]),v);}
    return o;
}
// The derivative part of dot(a, a): reduce_add(a.val * d + d * a.val).
fn tn_dual_dot(v:vec3<f32>,d:vec3<f32>)->f32{let t=rounded_add3(v*d,d*v);return rounded_add(rounded_add(t.x,t.y),t.z);}
// normalize(a) = a * inversesqrt(dot(a, a)); inversesqrt: f = inversesqrtf(u) (0 when u is not positive), df/du = -0.5 * safe_divide(f, u);
// a * s = {a * s, a * s' + a' * s}.
fn tn_dual_normalize(a:NormalDual)->NormalDual{
    let l=rounded_dot(a.value,a.value);let f=select(0.0,rounded_divide(1.0,rounded_sqrt(l)),l>0.0);
    let dfdu=-0.5*select(0.0,rounded_divide(f,l),l!=0.0);let sdx=dfdu*tn_dual_dot(a.value,a.dx);let sdy=dfdu*tn_dual_dot(a.value,a.dy);
    return NormalDual(a.value*f,rounded_add3(a.value*sdx,a.dx*f),rounded_add3(a.value*sdy,a.dy*f));
}
//#endif
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
//#if SVM_NODE_TANGENT
    if(!derivative){
        // svm_node_tangent in the pinned float3 order exactly (as svm_geometry_tangent): object_normal_transform's
        // normalize(transform_direction_transposed(itfm, t)), the motion inverse for a moving object, then
        // cross(N, normalize(cross(t, N))). A degenerate frame (t parallel to N, as on a rotated instance's side faces)
        // is then exactly zero as on the CPU, and normalize's 0/0 follows it (native/DEVICE.md, item 150).
        var t=tangent.value;
        if(sd.object_id!=NONE){
            var rows:NativeTransform;
            if((sd.object_flag&SD_OBJECT_MOTION)!=0u){rows=sd.motion_itfm;}
            else{let at=table_at(G_objects,sd.object_id,SIZE_objects)+O_itfm;rows=NativeTransform(heap_vec4(at),heap_vec4(at+16u),heap_vec4(at+32u));}
            var o=vec3<f32>(0);let n=rounded_steps(3u);
            for(var k=0u;k<n;k++){o[k]=rounded_dot(vec3<f32>(rows.x[k],rows.y[k],rows.z[k]),t);}
            t=surface_native_normalize(o);
        }
        svm_store3(s,out,rounded_cross(sd.N,surface_native_normalize(rounded_cross(t,sd.N))));return;
    }
    // The form with derivatives (dual3, util/math_dual.h) in the oracle's order: to_local's dots for the transposed transform,
    // normalize(a) = a * inversesqrt(dot(a, a)) with the product rule (tn_dual_normalize), cross with the float3 normal, and
    // cross(N, b) = -cross(b, N). At a degenerate frame inversesqrtf(0) is 0, so the whole dual is zero, as on the CPU.
    {
        var t=tangent;
        if(sd.object_id!=NONE){
            var rows:NativeTransform;
            if((sd.object_flag&SD_OBJECT_MOTION)!=0u){rows=sd.motion_itfm;}
            else{let at=table_at(G_objects,sd.object_id,SIZE_objects)+O_itfm;rows=NativeTransform(heap_vec4(at),heap_vec4(at+16u),heap_vec4(at+32u));}
            t=tn_dual_normalize(NormalDual(tn_transposed(rows,t.value),tn_transposed(rows,t.dx),tn_transposed(rows,t.dy)));
        }
        let inner=tn_dual_normalize(NormalDual(rounded_cross(t.value,sd.N),rounded_cross(t.dx,sd.N),rounded_cross(t.dy,sd.N)));
        svm_store3(s,out,rounded_cross(sd.N,inner.value));svm_store3(s,out+3u,rounded_cross(sd.N,inner.dx));svm_store3(s,out+6u,rounded_cross(sd.N,inner.dy));return;
    }
//#endif
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
