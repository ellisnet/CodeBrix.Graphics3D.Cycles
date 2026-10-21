// SPDX-FileCopyrightText: 2011-2025 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles svm/displace.h (svm_node_set_bump), svm/bump.h,
// svm/geometry.h, svm/tex_coord.h, svm/value.h, svm/convert.h,
// svm/sepcomb_vector.h, svm/math.h, svm/math_util.h, svm/util.h,
// geom/triangle.h (triangle_smooth_normal), kernel/util/differential.h and
// util/math_dual.h. Dual numbers are explicit (value, d/dx, d/dy) records:
// NormalDual for dual3 and vec3<f32>(value, dx, dy) for dual1. Derivative
// variants keep derivatives in the adjacent stack slots, as the CPU kernel does.
// Each handler has its own feature region and a single call site.
//#if SVM_NODE_BUMP
// displace.h svm_node_set_bump. Background evaluation has no BUMP node feature
// on the host (KERNEL_FEATURE_NODE_MASK_SURFACE_BACKGROUND): it stores zero.
fn svm_set_bump(s:ptr<function,SvmState>,node:u32){
    let out=svm_byte(node+N_SetBump_out_offset);
    if(!SVM_SURFACE){svm_store3(s,out,vec3<f32>(0));return;}
    let invert=svm_byte(node+N_SetBump_invert);let object_space=svm_byte(node+N_SetBump_use_object_space);
    if(invert>1u||object_space>1u){svm_error(s,16u);return;}
    let sd=(*s).geometry;let normal_index=svm_byte(node+N_SetBump_normal_offset);
    var n=sd.N;if(normal_index!=SVM_STACK_INVALID){n=svm_load3(s,normal_index);}
    // differential_from_compact(Ng, dP), or the full differential saved by enter_bump_eval.
    let state=svm_byte(node+N_SetBump_bump_state_offset);var dPdx:vec3<f32>;var dPdy:vec3<f32>;
    if(state==SVM_STACK_INVALID){let tangent=surface_orthogonal(sd.Ng);dPdx=sd.dP*tangent;dPdy=sd.dP*cross(sd.Ng,tangent);}
    else{dPdx=svm_load3(s,state+4u);dPdy=svm_load3(s,state+7u);}
    let h_c=svm_load(s,svm_byte(node+N_SetBump_center_offset));
    let h_x=svm_load(s,svm_byte(node+N_SetBump_dx_offset));
    let h_y=svm_load(s,svm_byte(node+N_SetBump_dy_offset));
    var strength=svm_input(s,node+N_SetBump_strength);var scale=svm_input(s,node+N_SetBump_scale);
    if((*s).error!=0u){return;}
    let transform=object_space!=0u&&sd.object_id!=NONE;
    if(transform){dPdx=surface_object_direction(sd,dPdx,true);dPdy=surface_object_direction(sd,dPdy,true);}
    // Pass 0 takes the input normal to object space; pass 1 takes the result back
    // (one object normal transform call site: drivers inline every call).
    var inverse=true;
    loop{
        if(transform){n=surface_object_normal(sd,n,inverse);}
        if(!inverse){break;}
        let normal_in=n;let Rx=cross(dPdy,normal_in);let Ry=cross(normal_in,dPdx);
        let det=dot(dPdx,Rx);let surfgrad=(h_x-h_c)*Rx+(h_y-h_c)*Ry;let absdet=abs(det);
        if(invert!=0u){scale*=-1.0;}
        strength=max(strength,0.0);
        let sign=select(1.0,-1.0,det<0.0);
        n=surface_normalize(heap_float(node+N_SetBump_bump_filter_width)*absdet*normal_in-scale*sign*surfgrad);
        if(all(n==vec3<f32>(0))){n=normal_in;}
        else{n=surface_native_normalize(strength*n+(1.0-strength)*normal_in);}
        inverse=false;
    }
    svm_store3(s,out,n);
}
//#endif
//#if SVM_NODE_BUMP_STATE
// bump.h enter/leave_bump_eval: save and restore P and the compact dP. The
// undisplaced-position branch needs true displacement, which this device does
// not implement: such a scene reports unsupported (16), never an approximation.
fn svm_bump_state(s:ptr<function,SvmState>,node:u32,enter:bool){
    if(!SVM_SURFACE){return;}
    if(enter){
        let offset=svm_byte(node+N_EnterBumpEval_state_offset);
        svm_store3(s,offset,(*s).geometry.P);svm_store(s,offset+3u,(*s).geometry.dP);
        let desc=attribute_find(s,ATTR_STD_POSITION_UNDISPLACED,0u);if(desc.found){svm_error(s,16u);}
    }else{
        let offset=svm_byte(node+N_LeaveBumpEval_state_offset);
        let P=svm_load3(s,offset);let dP=svm_load(s,offset+3u);if((*s).error!=0u){return;}
        (*s).geometry.P=P;(*s).geometry.dP=dP;
    }
}
//#endif
//#if SVM_DERIVATIVE_ANY
fn svmd_load3(s:ptr<function,SvmState>,index:u32)->NormalDual{return NormalDual(svm_load3(s,index),svm_load3(s,index+3u),svm_load3(s,index+6u));}
fn svmd_store3(s:ptr<function,SvmState>,index:u32,d:NormalDual){svm_store3(s,index,d.value);svm_store3(s,index+3u,d.dx);svm_store3(s,index+6u,d.dy);}
fn svmd_load1(s:ptr<function,SvmState>,index:u32)->vec3<f32>{return vec3<f32>(svm_load(s,index),svm_load(s,index+1u),svm_load(s,index+2u));}
fn svmd_store1(s:ptr<function,SvmState>,index:u32,d:vec3<f32>){svm_store(s,index,d.x);svm_store(s,index+1u,d.y);svm_store(s,index+2u,d.z);}
fn svmd_linked(bits:u32)->bool{return (bits>>8u)==(SVM_INPUT_STACK_OFFSET_MASK>>8u);}
fn svmd_input3(s:ptr<function,SvmState>,address:u32)->NormalDual{
    let bits=load_word(address);if(svmd_linked(bits)){return svmd_load3(s,bits&255u);}
    return NormalDual(heap_vec3(address),vec3<f32>(0),vec3<f32>(0));
}
fn svmd_input1(s:ptr<function,SvmState>,address:u32)->vec3<f32>{
    let bits=load_word(address);if(svmd_linked(bits)){return svmd_load1(s,bits&255u);}
    return vec3<f32>(bitcast<f32>(bits),0,0);
}
// util/svm/util.h dPdx/dPdy and shading_position/shading_incoming.
fn svmd_position(sd:SurfaceData)->NormalDual{return NormalDual(sd.P,sd.dPdu*sd.du.x+sd.dPdv*sd.dv.x,sd.dPdu*sd.du.y+sd.dPdv*sd.dv.y);}
fn svmd_incoming(sd:SurfaceData)->NormalDual{let a=surface_orthogonal(sd.wi);return NormalDual(sd.wi,sd.dI*a,sd.dI*cross(sd.wi,a));}
fn svmd_const(v:vec3<f32>)->NormalDual{return NormalDual(v,vec3<f32>(0),vec3<f32>(0));}
// First-order bump offset, then the full dual or only its value.
fn svmd_output(s:ptr<function,SvmState>,data_in:NormalDual,bump:u32,width:f32,store:u32,out:u32,normalize:bool){
    var data=data_in;
    if(bump==NODE_BUMP_OFFSET_DX){data.value+=data.dx*width;}else if(bump==NODE_BUMP_OFFSET_DY){data.value+=data.dy*width;}
    if(normalize){data=svmd_safe_normalize(data);}
    if(store!=0u){svmd_store3(s,out,data);}else{svm_store3(s,out,data.value);}
}
fn svmd_mul(a:NormalDual,b:NormalDual)->NormalDual{return NormalDual(a.value*b.value,a.value*b.dx+a.dx*b.value,a.value*b.dy+a.dy*b.value);}
// dual3 * dual1 (product rule).
fn svmd_scale(a:NormalDual,f:vec3<f32>)->NormalDual{return NormalDual(a.value*f.x,a.value*f.y+a.dx*f.x,a.value*f.z+a.dy*f.x);}
fn svmd_add(a:NormalDual,b:NormalDual)->NormalDual{return NormalDual(a.value+b.value,a.dx+b.dx,a.dy+b.dy);}
fn svmd_sub(a:NormalDual,b:NormalDual)->NormalDual{return NormalDual(a.value-b.value,a.dx-b.dx,a.dy-b.dy);}
fn svmd_neg(a:NormalDual)->NormalDual{return NormalDual(-a.value,-a.dx,-a.dy);}
fn svmd_reduce(a:vec3<f32>)->f32{return (a.x+a.y)+a.z;}
fn svmd_dot(a:NormalDual,b:NormalDual)->vec3<f32>{let m=svmd_mul(a,b);return vec3<f32>(svmd_reduce(m.value),svmd_reduce(m.dx),svmd_reduce(m.dy));}
fn svmd_mul1(u:vec3<f32>,v:vec3<f32>)->vec3<f32>{return vec3<f32>(u.x*v.x,u.x*v.y+u.y*v.x,u.x*v.z+u.z*v.x);}
fn svmd_div1(u:vec3<f32>,v:vec3<f32>)->vec3<f32>{let inv=1.0/v.x;let q=u.x/v.x;return vec3<f32>(q,(u.y-q*v.y)*inv,(u.z-q*v.z)*inv);}
fn svmd_sqrt1(u:vec3<f32>)->vec3<f32>{let f=sqrt(u.x);let k=0.5/f;return vec3<f32>(f,k*u.y,k*u.z);}
fn svmd_inversesqrt1(u:vec3<f32>)->vec3<f32>{
    var f=0.0;if(u.x>0.0){f=1.0/sqrt(u.x);}
    var q=0.0;if(u.x!=0.0){q=f/u.x;}let k=-0.5*q;return vec3<f32>(f,k*u.y,k*u.z);
}
fn svmd_safe_normalize(a:NormalDual)->NormalDual{
    let len_sq=svmd_dot(a,a);if(len_sq.x==0.0){return svmd_const(vec3<f32>(0));}
    return svmd_scale(a,svmd_inversesqrt1(len_sq));
}
fn svmd_select(mask:vec3<bool>,a:NormalDual,b:NormalDual)->NormalDual{return NormalDual(select(b.value,a.value,mask),select(b.dx,a.dx,mask),select(b.dy,a.dy,mask));}
fn svmd_zero_if(mask:vec3<bool>,a:NormalDual)->NormalDual{return svmd_select(mask,svmd_const(vec3<f32>(0)),a);}
fn svmd_floor(a:NormalDual)->NormalDual{return svmd_const(floor(a.value));}
//#endif
//#if SVM_NODE_GEOMETRY_DERIVATIVE
// geometry.h svm_node_geometry<dual3>. The tangent output (primitive_tangent)
// is not ported for derivatives and reports unsupported (16).
fn svm_geometry_derivative(s:ptr<function,SvmState>,node:u32){
    let sd=(*s).geometry;let kind=svm_byte(node+N_Geometry_geom_type);var data=svmd_const(vec3<f32>(0));
    switch(kind){
        case NODE_GEOM_P:{data=svmd_position(sd);}
        case NODE_GEOM_N:{data=svmd_const(sd.N);}
        case NODE_GEOM_Ng:{data=svmd_const(sd.Ng);}
        case NODE_GEOM_I:{data=svmd_incoming(sd);}
        case NODE_GEOM_uv:{data=NormalDual(vec3<f32>(1.0-sd.u-sd.v,sd.u,0),vec3<f32>(-sd.du.x-sd.dv.x,sd.du.x,0),vec3<f32>(-sd.du.y-sd.dv.y,sd.du.y,0));}
        default:{svm_error(s,16u);return;}
    }
    let bump=svm_byte(node+N_Geometry_bump_offset);let store=svm_byte(node+N_Geometry_store_derivatives);
    if(bump>NODE_BUMP_OFFSET_DY||store>1u){svm_error(s,16u);return;}
    svmd_output(s,data,bump,heap_float(node+N_Geometry_bump_filter_width),store,svm_byte(node+N_Geometry_out_offset),false);
}
//#endif
//#if SVM_NODE_TEX_COORD_DERIVATIVE
// tex_coord.h svm_node_tex_coord_derivative. Values reuse svm_texco (the
// non-derivative evaluation of the same coordinate); derivatives follow
// svm_node_tex_coord_eval<dual3>. Smooth normals use triangle_smooth_normal
// with the du/dv offsets; motion-triangle is_smooth normals report unsupported (16).
fn svmd_smooth_normal(s:ptr<function,SvmState>)->NormalDual{
    let sd=(*s).geometry;var v:array<vec3<f32>,3>;v[0]=sd.N;var count=1u;
    let is_smooth=(sd.primitive_type&PRIMITIVE_TRIANGLE)!=0u&&(sd.shader&SHADER_SMOOTH_NORMAL)!=0u;
    var transform=sd.object_id!=NONE;
    if(is_smooth){
        if(sd.primitive_type!=PRIMITIVE_TRIANGLE){svm_error(s,16u);return svmd_const(sd.N);}
        let object=table_at(G_objects,sd.object_id,SIZE_objects);var indices:vec3<u32>;
        if((sd.object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0u){
            if(sd.prim>(0xffffffffu-2u)/3u){svm_error(s,64u);return svmd_const(sd.N);}
            indices=vec3<u32>(3u*sd.prim)+vec3<u32>(0,1,2);
        }else{
            if(sd.prim>=table_count(G_tri_vindex)){svm_error(s,64u);return svmd_const(sd.N);}
            let at=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);
            indices=vec3<u32>(load_word(at),load_word(at+4u),load_word(at+8u));
        }
        let desc=AttributeDesc(ATTR_ELEMENT_VERTEX_NORMAL,NODE_ATTR_FLOAT3,bitcast<i32>(load_word(object+O_normal_offset)),true);
        var n:array<vec3<f32>,3>;for(var j=0u;j<3u;j++){n[j]=attribute_fetch(s,desc,indices[j]).xyz;}
        // triangle_smooth_normal at (u,v), (u+du.dx,v+dv.dx) and (u+du.dy,v+dv.dy).
        let uv=array<vec2<f32>,3>(vec2<f32>(sd.u,sd.v),vec2<f32>(sd.u+sd.du.x,sd.v+sd.dv.x),vec2<f32>(sd.u+sd.du.y,sd.v+sd.dv.y));
        for(var j=0u;j<3u;j++){
            var r=surface_normalize(((1.0-uv[j].x)-uv[j].y)*n[0]+uv[j].x*n[1]+uv[j].y*n[2]);
            if(all(r==vec3<f32>(0))){r=sd.Ng;}
            if((sd.runtime_flag&SR_BACKFACING)!=0u){r=-r;}
            v[j]=r;
        }
        count=3u;transform=(sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
    }
    if(transform){for(var j=0u;j<count;j++){v[j]=surface_object_normal(sd,v[j],true);}}
    if(!is_smooth){return svmd_const(v[0]);}
    return NormalDual(v[0],v[1]-v[0],v[2]-v[0]);
}
fn svm_tex_coord_derivative(s:ptr<function,SvmState>,node:u32,index:u32,value:vec3<f32>,transform_address:u32){
    let sd=(*s).geometry;let kind=svm_byte(node+N_TexCoord_texco_type);let world=sd.object_id==NONE;
    var data:NormalDual;
    switch(kind){
        case NODE_TEXCO_NORMAL:{data=svmd_smooth_normal(s);}
        case NODE_TEXCO_OBJECT,NODE_TEXCO_VOLUME_GENERATED:{
            data=svmd_position(sd);data.value=value;
            if(!world&&kind==NODE_TEXCO_OBJECT){data.dx=surface_object_direction(sd,data.dx,true);data.dy=surface_object_direction(sd,data.dy,true);}
        }
        case NODE_TEXCO_OBJECT_WITH_TRANSFORM:{
            data=svmd_position(sd);data.value=value;data.dx=surface_transform_direction(transform_address,data.dx);data.dy=surface_transform_direction(transform_address,data.dy);
        }
        case NODE_TEXCO_CAMERA:{
            data=svmd_position(sd);data.value=value;let tfm=p(6u)+D_cam_worldtocamera;
            data.dx=surface_transform_direction(tfm,data.dx);data.dy=surface_transform_direction(tfm,data.dy);
        }
        case NODE_TEXCO_WINDOW:{
            data=svmd_const(value);
            let camera_ray=world&&data_word(D_cam_type)==CAMERA_ORTHOGRAPHIC&&(state_byte(S_path_visibility,index)&VIS_CAMERA)!=0u;
            if(!camera_ray){data.dx.x=1.0/heap_float(p(6u)+D_cam_width);data.dy.y=1.0/heap_float(p(6u)+D_cam_height);}
        }
        case NODE_TEXCO_REFLECTION:{
            let I=svmd_incoming(sd);data=I;data.value=value;
            if(!world){
                // -reflect(I, N): I - N * dot(I, N) * 2, negated.
                let d=svmd_dot(I,svmd_const(sd.N));data.dx=-(I.dx-sd.N*d.y*2.0);data.dy=-(I.dy-sd.N*d.z*2.0);
            }
        }
        case NODE_TEXCO_DUPLI_GENERATED,NODE_TEXCO_DUPLI_UV:{data=svmd_const(value);}
        default:{svm_error(s,16u);return;}
    }
    if((*s).error!=0u){return;}
    let bump=svm_byte(node+N_TexCoord_bump_offset);let store=svm_byte(node+N_TexCoord_store_derivatives);
    if(bump>NODE_BUMP_OFFSET_DY||store>1u){svm_error(s,16u);return;}
    svmd_output(s,data,bump,heap_float(node+N_TexCoord_bump_filter_width),store,svm_byte(node+N_TexCoord_out_offset),kind==NODE_TEXCO_NORMAL);
}
//#endif
//#if SVM_NODE_VALUE_DERIVATIVE
// value.h, convert.h and sepcomb_vector.h with dual1/dual3: constants have zero
// derivatives; integer conversions keep the plain behaviour, as on the host.
fn svm_value_derivative(s:ptr<function,SvmState>,node:u32,opcode:u32){
    switch(opcode){
        case NODE_VALUE_F_DERIVATIVE:{svmd_store1(s,svm_byte(node+N_ValueF_out_offset),vec3<f32>(bitcast<f32>(load_word(node+N_ValueF_value)),0,0));}
        case NODE_VALUE_V_DERIVATIVE:{svmd_store3(s,svm_byte(node+N_ValueV_out_offset),svmd_const(heap_vec3(node+N_ValueV_value)));}
        case NODE_SEPARATE_VECTOR_DERIVATIVE:{
            let component=svm_byte(node+N_SeparateVector_vector_index);if(component>2u){svm_error(s,16u);return;}
            let v=svmd_input3(s,node+N_SeparateVector_vector);let out=svm_byte(node+N_SeparateVector_out_offset);
            if(out!=SVM_STACK_INVALID){svmd_store1(s,out,vec3<f32>(v.value[component],v.dx[component],v.dy[component]));}
        }
        case NODE_COMBINE_VECTOR_DERIVATIVE:{
            let component=svm_byte(node+N_CombineVector_vector_index);let out=svm_byte(node+N_CombineVector_out_offset);if(component>2u){svm_error(s,16u);return;}
            let f=svmd_input1(s,node+N_CombineVector_in);
            if(out!=SVM_STACK_INVALID){svm_store(s,out+component,f.x);svm_store(s,out+component+3u,f.y);svm_store(s,out+component+6u,f.z);}
        }
        default:{
            let kind=load_word(node+N_Convert_convert_type);let source_index=svm_byte(node+N_Convert_from_offset);let dest=svm_byte(node+N_Convert_to_offset);
            switch(kind){
                case NODE_CONVERT_FI:{svm_store_bits(s,dest,svm_int(s,svm_load(s,source_index)));}
                case NODE_CONVERT_FV:{let f=svmd_load1(s,source_index);svmd_store3(s,dest,NormalDual(vec3<f32>(f.x),vec3<f32>(f.y),vec3<f32>(f.z)));}
                case NODE_CONVERT_CF:{let rgb=heap_vec3(p(6u)+D_film_rgb_to_y);let d=svmd_dot(svmd_load3(s,source_index),svmd_const(rgb));svmd_store1(s,dest,d);}
                case NODE_CONVERT_VF:{let v=svmd_load3(s,source_index);svmd_store1(s,dest,vec3<f32>(svmd_reduce(v.value),svmd_reduce(v.dx),svmd_reduce(v.dy))*(1.0/3.0));}
                case NODE_CONVERT_CI,NODE_CONVERT_VI:{
                    let v=svm_load3(s,source_index);var f=(v.x+v.y+v.z)*(1.0/3.0);
                    if(kind==NODE_CONVERT_CI){f=dot(v,heap_vec3(p(6u)+D_film_rgb_to_y));}
                    svm_store_bits(s,dest,svm_int(s,f));
                }
                case NODE_CONVERT_IF:{svmd_store1(s,dest,vec3<f32>(f32(bitcast<i32>(svm_load_bits(s,source_index))),0,0));}
                case NODE_CONVERT_IV:{svmd_store3(s,dest,svmd_const(vec3<f32>(f32(bitcast<i32>(svm_load_bits(s,source_index))))));}
                default:{svm_error(s,16u);}
            }
        }
    }
}
//#endif
//#if SVM_NODE_VECTOR_MATH_DERIVATIVE
// math.h svm_node_vector_math<dual3> with math_util.h svm_vector_math and the
// util/math_dual.h operators (product, quotient and chain rules).
fn svm_vector_math_derivative(s:ptr<function,SvmState>,node:u32){
    let kind=load_word(node+N_VectorMath_math_type);
    let a=svmd_input3(s,node+N_VectorMath_a);let b=svmd_input3(s,node+N_VectorMath_b);let c=svmd_input3(s,node+N_VectorMath_c);
    let param1=svmd_input1(s,node+N_VectorMath_param1);
    var v=svmd_const(vec3<f32>(0));var value=vec3<f32>(0);let zero=svmd_const(vec3<f32>(0));
    switch(kind){
        case NODE_VECTOR_MATH_ADD:{v=svmd_add(a,b);}
        case NODE_VECTOR_MATH_SUBTRACT:{v=svmd_sub(a,b);}
        case NODE_VECTOR_MATH_MULTIPLY:{v=svmd_mul(a,b);}
        case NODE_VECTOR_MATH_DIVIDE:{
            let inv=1.0/b.value;let q=a.value/b.value;
            v=svmd_zero_if(b.value==vec3<f32>(0),NormalDual(q,(a.dx-q*b.dx)*inv,(a.dy-q*b.dy)*inv));
        }
        case NODE_VECTOR_MATH_CROSS_PRODUCT:{v=NormalDual(cross(a.value,b.value),cross(a.value,b.dx)+cross(a.dx,b.value),cross(a.value,b.dy)+cross(a.dy,b.value));}
        case NODE_VECTOR_MATH_PROJECT:{
            let len_sq=svmd_dot(b,b);if(len_sq.x!=0.0){v=svmd_scale(b,svmd_div1(svmd_dot(a,b),len_sq));}
        }
        case NODE_VECTOR_MATH_REFLECT:{
            let n=svmd_safe_normalize(b);let d=svmd_dot(a,n);
            v=svmd_sub(a,NormalDual(n.value*d.x*2.0,(n.value*d.y+n.dx*d.x)*2.0,(n.value*d.z+n.dy*d.x)*2.0));
        }
        case NODE_VECTOR_MATH_REFRACT:{
            let n=svmd_safe_normalize(b);let NI=svmd_dot(a,n);let eta=param1;
            let one_minus=vec3<f32>(1.0-svmd_mul1(NI,NI).x,-svmd_mul1(NI,NI).y,-svmd_mul1(NI,NI).z);
            let e2=svmd_mul1(eta,eta);let t=svmd_mul1(e2,one_minus);let k=vec3<f32>(1.0-t.x,-t.y,-t.z);
            if(k.x>=0.0){v=svmd_sub(svmd_scale(a,eta),svmd_scale(n,svmd_mul1(eta,NI)+svmd_sqrt1(k)));}
        }
        case NODE_VECTOR_MATH_FACEFORWARD:{if(svmd_dot(c,b).x<0.0){v=a;}else{v=svmd_neg(a);}}
        case NODE_VECTOR_MATH_MULTIPLY_ADD:{v=svmd_add(svmd_mul(a,b),c);}
        case NODE_VECTOR_MATH_DOT_PRODUCT:{value=svmd_dot(a,b);}
        case NODE_VECTOR_MATH_DISTANCE:{let d=svmd_sub(a,b);value=svmd_sqrt1(svmd_dot(d,d));}
        case NODE_VECTOR_MATH_LENGTH:{value=svmd_sqrt1(svmd_dot(a,a));}
        case NODE_VECTOR_MATH_SCALE:{v=svmd_scale(a,param1);}
        case NODE_VECTOR_MATH_NORMALIZE:{v=svmd_safe_normalize(a);}
        case NODE_VECTOR_MATH_SNAP:{
            let inv=1.0/b.value;let q=a.value/b.value;
            let divided=svmd_zero_if(b.value==vec3<f32>(0),NormalDual(q,(a.dx-q*b.dx)*inv,(a.dy-q*b.dy)*inv));
            v=svmd_mul(svmd_floor(divided),b);
        }
        case NODE_VECTOR_MATH_ROUND:{v=svmd_const(floor(a.value+vec3<f32>(0.5)));}
        case NODE_VECTOR_MATH_FLOOR:{v=svmd_floor(a);}
        case NODE_VECTOR_MATH_CEIL:{v=svmd_const(ceil(a.value));}
        case NODE_VECTOR_MATH_MODULO:{v=NormalDual(vec3<f32>(c_safe_modulo(a.value.x,b.value.x),c_safe_modulo(a.value.y,b.value.y),c_safe_modulo(a.value.z,b.value.z)),a.dx,a.dy);}
        case NODE_VECTOR_MATH_WRAP:{
            // wrap(value, max, min) = safe_floored_fmod(value - min, max - min) + min.
            let x=svmd_sub(a,c);let r=svmd_sub(b,c);
            let m=svmd_zero_if(r.value==vec3<f32>(0),svmd_sub(x,svmd_mul(svmd_const(floor(x.value/r.value)),r)));
            v=NormalDual(m.value+c.value,m.dx+c.dx,m.dy+c.dy);
        }
        case NODE_VECTOR_MATH_FRACTION:{v=NormalDual(a.value-floor(a.value),a.dx,a.dy);}
        case NODE_VECTOR_MATH_ABSOLUTE:{v=svmd_select(a.value>vec3<f32>(0),a,svmd_neg(a));}
        case NODE_VECTOR_MATH_POWER:{
            let pm1=vec3<f32>(c_safe_powf(a.value.x,b.value.x-1.0),c_safe_powf(a.value.y,b.value.y-1.0),c_safe_powf(a.value.z,b.value.z-1.0));
            let f=a.value*pm1;let dfdu=b.value*pm1;
            let dfdv=f*select(vec3<f32>(0),log(a.value),a.value>vec3<f32>(0));
            v=NormalDual(f,dfdu*a.dx+dfdv*b.dx,dfdu*a.dy+dfdv*b.dy);
        }
        case NODE_VECTOR_MATH_SIGN:{v=svmd_const(vec3<f32>(c_compatible_signf(a.value.x),c_compatible_signf(a.value.y),c_compatible_signf(a.value.z)));}
        case NODE_VECTOR_MATH_MINIMUM:{v=svmd_select(a.value<b.value,a,b);}
        case NODE_VECTOR_MATH_MAXIMUM:{v=svmd_select(a.value>b.value,a,b);}
        case NODE_VECTOR_MATH_SINE:{let k=cos(a.value);v=NormalDual(sin(a.value),k*a.dx,k*a.dy);}
        case NODE_VECTOR_MATH_COSINE:{let k=-sin(a.value);v=NormalDual(cos(a.value),k*a.dx,k*a.dy);}
        case NODE_VECTOR_MATH_TANGENT:{
            let co=cos(a.value);let sec=select(vec3<f32>(0),vec3<f32>(1)/co,co!=vec3<f32>(0));let k=sec*sec;
            v=NormalDual(tan(a.value),k*a.dx,k*a.dy);
        }
        default:{svm_error(s,16u);}
    }
    let value_offset=svm_byte(node+N_VectorMath_value_offset);let vector_offset=svm_byte(node+N_VectorMath_vector_offset);
    if(value_offset!=SVM_STACK_INVALID){svmd_store1(s,value_offset,value);}
    if(vector_offset!=SVM_STACK_INVALID){svmd_store3(s,vector_offset,v);}
}
//#endif
