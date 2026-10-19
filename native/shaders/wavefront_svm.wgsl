// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from Cycles: SVM bytecode interpretation ported to WGSL.
// Node layouts/enums come from the compiled
// Cycles ABI; scalar math/gamma functions are generated from upstream AST.
// Background emission accumulation also derives from closure/emissive.h (BSD).
struct SvmState {
    stack:array<u32,255>, initialized:array<u32,8>, pc:u32, error:u32,
    weight:vec3<f32>, geometry:SurfaceData, output:SvmResult,
}
// Error bits: 2 unsupported opcode; 4 bytecode bounds; 8 invalid/uninitialized
// stack; 16 unsupported parameter; 32 instruction limit. Bit 1 is shared with
// other wavefront checks. An error never supplies substitute shader radiance.
fn svm_error(s:ptr<function,SvmState>,code:u32){(*s).error|=code;(*s).output.error|=code;atomicOr(&heap[p(17u)/4u],code);}
fn svm_take(s:ptr<function,SvmState>,bytes:u32)->u32{
    let count=bytes/4u;
    if((*s).pc>p(22u)||count>p(22u)-(*s).pc){svm_error(s,4u);return 0u;}
    let address=p(21u)+(*s).pc*4u;(*s).pc+=count;return address;
}
fn svm_byte(address:u32)->u32{return (load_word(address)>>(8u*(address%4u)))&255u;}
fn svm_load_bits(s:ptr<function,SvmState>,index:u32)->u32{
    if(index>=SVM_STACK_SIZE){svm_error(s,8u);return 0u;}
    if(((*s).initialized[index/32u]&(1u<<(index%32u)))==0u){svm_error(s,8u);return 0u;}
    return (*s).stack[index];
}
fn svm_load(s:ptr<function,SvmState>,index:u32)->f32{return bitcast<f32>(svm_load_bits(s,index));}
fn svm_store_bits(s:ptr<function,SvmState>,index:u32,value:u32){
    if(index>=SVM_STACK_SIZE){svm_error(s,8u);return;}
    (*s).stack[index]=value;(*s).initialized[index/32u]|=1u<<(index%32u);
}
fn svm_store(s:ptr<function,SvmState>,index:u32,value:f32){svm_store_bits(s,index,bitcast<u32>(value));}
fn svm_load3(s:ptr<function,SvmState>,index:u32)->vec3<f32>{return vec3<f32>(svm_load(s,index),svm_load(s,index+1u),svm_load(s,index+2u));}
fn svm_store3(s:ptr<function,SvmState>,index:u32,value:vec3<f32>){svm_store(s,index,value.x);svm_store(s,index+1u,value.y);svm_store(s,index+2u,value.z);}
fn svm_input(s:ptr<function,SvmState>,address:u32)->f32{
    let bits=load_word(address);if((bits>>8u)==(SVM_INPUT_STACK_OFFSET_MASK>>8u)){return svm_load(s,bits&255u);}return bitcast<f32>(bits);
}
fn svm_input3(s:ptr<function,SvmState>,address:u32)->vec3<f32>{
    let bits=load_word(address);if((bits>>8u)==(SVM_INPUT_STACK_OFFSET_MASK>>8u)){return svm_load3(s,bits&255u);}return heap_vec3(address);
}
fn svm_optional(s:ptr<function,SvmState>,index:u32,default_value:f32)->f32{if(index==SVM_STACK_INVALID){return default_value;}return svm_load(s,index);}
fn svm_optional_store(s:ptr<function,SvmState>,index:u32,value:f32){if(index!=SVM_STACK_INVALID){svm_store(s,index,value);}}
fn svm_optional_store3(s:ptr<function,SvmState>,index:u32,value:vec3<f32>){if(index!=SVM_STACK_INVALID){svm_store3(s,index,value);}}
fn svm_int(s:ptr<function,SvmState>,value:f32)->u32{
    if((bitcast<u32>(value)&0x7f800000u)==0x7f800000u||value>=2147483648.0||value< -2147483648.0){svm_error(s,16u);return 0u;}
    return bitcast<u32>(i32(value));
}
fn svm_normalize(v:vec3<f32>)->vec3<f32>{let len=length(v);if(len!=0.0){return v*(1.0/len);}return v;}
fn svm_divide(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{return vec3<f32>(c_safe_divide(a.x,b.x),c_safe_divide(a.y,b.y),c_safe_divide(a.z,b.z));}
fn svm_vector(s:ptr<function,SvmState>,node:u32){
    let kind=load_word(node+N_VectorMath_math_type);
    let a=svm_input3(s,node+N_VectorMath_a);let b=svm_input3(s,node+N_VectorMath_b);let c=svm_input3(s,node+N_VectorMath_c);let scale=svm_input(s,node+N_VectorMath_param1);
    var v=vec3<f32>(0);var value=0.0;
    switch(kind){
        case NODE_VECTOR_MATH_ADD:{v=a+b;}
        case NODE_VECTOR_MATH_SUBTRACT:{v=a-b;}
        case NODE_VECTOR_MATH_MULTIPLY:{v=a*b;}
        case NODE_VECTOR_MATH_DIVIDE:{v=svm_divide(a,b);}
        case NODE_VECTOR_MATH_CROSS_PRODUCT:{v=cross(a,b);}
        case NODE_VECTOR_MATH_PROJECT:{let len2=dot(b,b);if(len2!=0.0){v=(dot(a,b)/len2)*b;}}
        case NODE_VECTOR_MATH_REFLECT:{let n=svm_normalize(b);v=a-2.0*dot(a,n)*n;}
        case NODE_VECTOR_MATH_REFRACT:{let n=svm_normalize(b);let ndot=dot(n,a);let k=1.0-scale*scale*(1.0-ndot*ndot);if(k>=0.0){v=scale*a-(scale*ndot+sqrt(k))*n;}}
        case NODE_VECTOR_MATH_FACEFORWARD:{v=select(-a,a,dot(c,b)<0.0);}
        case NODE_VECTOR_MATH_MULTIPLY_ADD:{v=a*b+c;}
        case NODE_VECTOR_MATH_DOT_PRODUCT:{value=dot(a,b);}
        case NODE_VECTOR_MATH_DISTANCE:{value=length(a-b);}
        case NODE_VECTOR_MATH_LENGTH:{value=length(a);}
        case NODE_VECTOR_MATH_SCALE:{v=a*scale;}
        case NODE_VECTOR_MATH_NORMALIZE:{v=svm_normalize(a);}
        case NODE_VECTOR_MATH_SNAP:{v=floor(svm_divide(a,b))*b;}
        case NODE_VECTOR_MATH_ROUND:{v=floor(a+vec3<f32>(0.5));}
        case NODE_VECTOR_MATH_FLOOR:{v=floor(a);}
        case NODE_VECTOR_MATH_CEIL:{v=ceil(a);}
        case NODE_VECTOR_MATH_MODULO:{v=vec3<f32>(c_safe_modulo(a.x,b.x),c_safe_modulo(a.y,b.y),c_safe_modulo(a.z,b.z));}
        case NODE_VECTOR_MATH_WRAP:{v=vec3<f32>(c_wrapf(a.x,b.x,c.x),c_wrapf(a.y,b.y,c.y),c_wrapf(a.z,b.z,c.z));}
        case NODE_VECTOR_MATH_FRACTION:{v=a-floor(a);}
        case NODE_VECTOR_MATH_ABSOLUTE:{v=abs(a);}
        case NODE_VECTOR_MATH_POWER:{v=vec3<f32>(c_safe_powf(a.x,b.x),c_safe_powf(a.y,b.y),c_safe_powf(a.z,b.z));}
        case NODE_VECTOR_MATH_SIGN:{v=vec3<f32>(c_compatible_signf(a.x),c_compatible_signf(a.y),c_compatible_signf(a.z));}
        case NODE_VECTOR_MATH_MINIMUM:{v=min(a,b);}
        case NODE_VECTOR_MATH_MAXIMUM:{v=max(a,b);}
        case NODE_VECTOR_MATH_SINE:{v=sin(a);}
        case NODE_VECTOR_MATH_COSINE:{v=cos(a);}
        case NODE_VECTOR_MATH_TANGENT:{v=tan(a);}
        default:{svm_error(s,16u);}
    }
    svm_optional_store(s,svm_byte(node+N_VectorMath_value_offset),value);svm_optional_store3(s,svm_byte(node+N_VectorMath_vector_offset),v);
}
fn svm_transform(address:u32,v:vec3<f32>)->vec3<f32>{
    let x=heap_vec4(address);let y=heap_vec4(address+16u);let z=heap_vec4(address+32u);
    return vec3<f32>(x.x,y.x,z.x)*v.x+(vec3<f32>(x.y,y.y,z.y)*v.y+vec3<f32>(x.z,y.z,z.z)*v.z)+vec3<f32>(x.w,y.w,z.w);
}
fn svm_texco(s:ptr<function,SvmState>,node:u32,index:u32)->vec3<f32>{
    let kind=svm_byte(node+N_TexCoord_texco_type);let sd=(*s).geometry;let world=sd.object_id==NONE;
    var object=0u;if(!world){object=table_at(G_objects,sd.object_id,SIZE_objects);}
    switch(kind){
        case NODE_TEXCO_OBJECT:{if(world){return sd.P;}return surface_transform_point(object+O_itfm,sd.P);}
        case NODE_TEXCO_VOLUME_GENERATED:{if(!world){svm_error(s,16u);}return sd.P;}
        case NODE_TEXCO_NORMAL:{if(world){return sd.N;}return surface_transform_normal(object+O_tfm,sd.N);}
        case NODE_TEXCO_REFLECTION:{if(world){return sd.wi;}return 2.0*dot(sd.wi,sd.N)*sd.N-sd.wi;}
        case NODE_TEXCO_DUPLI_GENERATED:{if(world){return vec3<f32>(0);}return heap_vec3(object+O_dupli_generated);}
        case NODE_TEXCO_DUPLI_UV:{if(world){return vec3<f32>(0);}return vec3<f32>(heap_float(object+O_dupli_uv),heap_float(object+O_dupli_uv+4u),0);}
        case NODE_TEXCO_OBJECT_WITH_TRANSFORM:{let address=svm_take(s,SZ_PackedTransform);if((*s).error!=0u){return vec3<f32>(0);}return svm_transform(address,sd.P);}
        case NODE_TEXCO_CAMERA,NODE_TEXCO_WINDOW:{
            let matrix_world=p(6u)+D_cam_cameratoworld;let camera_pos=vec3<f32>(heap_float(matrix_world+12u),heap_float(matrix_world+28u),heap_float(matrix_world+44u));
            if(kind==NODE_TEXCO_CAMERA){return svm_transform(p(6u)+D_cam_worldtocamera,sd.P+select(vec3<f32>(0),camera_pos,world));}
            let camera_type=data_word(D_cam_type);var point=sd.P;
            if(camera_type==CAMERA_PERSPECTIVE){if(world){point+=camera_pos;}}
            else if(camera_type==CAMERA_ORTHOGRAPHIC){if(world&&(state_byte(S_path_visibility,index)&VIS_CAMERA)!=0u){point=heap_vec3(state_address(S_ray_P,index,12u));}}
            else{svm_error(s,16u);return vec3<f32>(0);}
            let matrix=p(6u)+D_cam_worldtondc;let v=vec4<f32>(point,1);let w=dot(heap_vec4(matrix+48u),v);
            return vec3<f32>(dot(heap_vec4(matrix),v)/w,dot(heap_vec4(matrix+16u),v)/w,0);
        }
        default:{svm_error(s,16u);return vec3<f32>(0);}
    }
}
fn svm_gradient(s:ptr<function,SvmState>,kind:u32,v:vec3<f32>)->f32{
    switch(kind){
        case NODE_BLEND_LINEAR:{return v.x;}
        case NODE_BLEND_QUADRATIC:{let r=max(v.x,0.0);return r*r;}
        case NODE_BLEND_EASING:{let r=clamp(v.x,0.0,1.0);let t=r*r;return 3.0*t-2.0*t*r;}
        case NODE_BLEND_DIAGONAL:{return (v.x+v.y)*0.5;}
        case NODE_BLEND_RADIAL:{return atan2(v.y,v.x)/6.2831855+0.5;}
        case NODE_BLEND_QUADRATIC_SPHERE,NODE_BLEND_SPHERICAL:{let r=max(0.999999-sqrt(v.x*v.x+v.y*v.y+v.z*v.z),0.0);return select(r,r*r,kind==NODE_BLEND_QUADRATIC_SPHERE);}
        default:{svm_error(s,16u);return 0.0;}
    }
}
fn svm_evaluate(index:u32,geometry:SurfaceData,closure_limit:u32,closure_base:u32)->SvmResult{
    var s:SvmState;s.geometry=geometry;s.pc=(geometry.shader&SHADER_MASK)*(1u+SZ_ShaderJump/4u);
    s.output.runtime_flag=geometry.runtime_flag;s.output.N=geometry.N;s.output.lcg_state=svm_lcg(index);
    let flag=state_word(S_path_flag,index);let visibility=state_byte(S_path_visibility,index);
    if(closure_limit>SVM_CLOSURE_CAPACITY){svm_error(&s,16u);return s.output;}
    s.output.closure_base=closure_base;
    if(closure_base!=0u){let bytes=arrayLength(&heap)*4u;let required=SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES;
        if(closure_base%4u!=0u||closure_base>bytes||required>bytes-closure_base){svm_error(&s,1u);return s.output;}}
    else if(closure_limit!=0u){svm_error(&s,1u);return s.output;}

    if((visibility&PATH_RAY_VISIBILITY_SHADOW)==0u&&(flag&(PATH_RAY_TERMINATE|PATH_RAY_EMISSION))==0u){s.output.remaining=closure_limit;}

    for(var instructions=0u;instructions<65536u;instructions++){
        let opcode_address=svm_take(&s,4u);if(s.error!=0u){return s.output;}let opcode=load_word(opcode_address);
        if(opcode==NODE_END){s.output.N=s.geometry.N;return s.output;}
        var size=0u;
        switch(opcode){
            case NODE_SHADER_JUMP:{size=SZ_ShaderJump;}
            case NODE_JUMP_IF_ZERO:{size=SZ_JumpIfZero;}
            case NODE_JUMP_IF_ONE:{size=SZ_JumpIfOne;}
            case NODE_MATH:{size=SZ_Math;}
            case NODE_CLAMP:{size=SZ_Clamp;}
            case NODE_VALUE_F:{size=SZ_ValueF;}
            case NODE_VALUE_V:{size=SZ_ValueV;}
            case NODE_CONVERT:{size=SZ_Convert;}
            case NODE_GEOMETRY:{size=SZ_Geometry;}
            case NODE_TEX_COORD:{size=SZ_TexCoord;}
            case NODE_VECTOR_MATH:{size=SZ_VectorMath;}
            case NODE_SEPARATE_VECTOR:{size=SZ_SeparateVector;}
            case NODE_COMBINE_VECTOR:{size=SZ_CombineVector;}
            case NODE_CLOSURE_BSDF:{size=SZ_ClosureBsdf;}
            case NODE_CLOSURE_HOLDOUT:{size=SZ_ClosureHoldout;}
            case NODE_CLOSURE_SET_NORMAL:{size=SZ_ClosureSetNormal;}
            case NODE_CLOSURE_SET_WEIGHT:{size=SZ_ClosureSetWeight;}
            case NODE_CLOSURE_WEIGHT:{size=SZ_ClosureWeight;}
            case NODE_CLOSURE_BACKGROUND:{size=SZ_ClosureBackground;}
            case NODE_CLOSURE_EMISSION:{size=SZ_ClosureEmission;}
            case NODE_EMISSION_WEIGHT:{size=SZ_EmissionWeight;}
            case NODE_MIX_CLOSURE:{size=SZ_MixClosure;}
            case NODE_TEX_CHECKER:{size=SZ_TexChecker;}
            case NODE_TEX_GRADIENT:{size=SZ_TexGradient;}
            case NODE_GAMMA:{size=SZ_Gamma;}
            case NODE_INVERT:{size=SZ_Invert;}
            case NODE_BRIGHTCONTRAST:{size=SZ_BrightContrast;}
            default:{svm_error(&s,2u);return s.output;}
        }
        let node=svm_take(&s,size);if(s.error!=0u){return s.output;}
        switch(opcode){
            case NODE_SHADER_JUMP:{s.pc=load_word(node+N_ShaderJump_offset_surface);}
            case NODE_JUMP_IF_ZERO,NODE_JUMP_IF_ONE:{
                var offset=N_JumpIfZero_stack_offset;var jump_offset=N_JumpIfZero_jump_offset;
                if(opcode==NODE_JUMP_IF_ONE){offset=N_JumpIfOne_stack_offset;jump_offset=N_JumpIfOne_jump_offset;}
                let f=svm_load(&s,svm_byte(node+offset));if((opcode==NODE_JUMP_IF_ZERO&&f<=0.0)||(opcode==NODE_JUMP_IF_ONE&&f>=1.0)){s.pc+=load_word(node+jump_offset);}
            }
            case NODE_VALUE_F:{svm_store_bits(&s,svm_byte(node+N_ValueF_out_offset),load_word(node+N_ValueF_value));}
            case NODE_VALUE_V:{svm_store3(&s,svm_byte(node+N_ValueV_out_offset),heap_vec3(node+N_ValueV_value));}
            case NODE_GEOMETRY:{
                let kind=svm_byte(node+N_Geometry_geom_type);var v=vec3<f32>(0);
                switch(kind){
                    case NODE_GEOM_P:{v=s.geometry.P;}
                    case NODE_GEOM_N:{v=s.geometry.N;}
                    case NODE_GEOM_Ng:{v=s.geometry.Ng;}
                    case NODE_GEOM_I:{v=s.geometry.wi;}
                    case NODE_GEOM_uv:{v=vec3<f32>(1.0-s.geometry.u-s.geometry.v,s.geometry.u,0);}
                    case NODE_GEOM_T:{if(s.geometry.object_id!=NONE){svm_error(&s,16u);}else{v=normalize(s.geometry.dPdu);}}
                    default:{svm_error(&s,16u);}
                }
                svm_store3(&s,svm_byte(node+N_Geometry_out_offset),v);
            }
            case NODE_TEX_COORD:{svm_store3(&s,svm_byte(node+N_TexCoord_out_offset),svm_texco(&s,node,index));}
            case NODE_MATH:{
                let kind=load_word(node+N_Math_math_type);if(kind>NODE_MATH_FLOORED_MODULO){svm_error(&s,16u);}
                else{let value=c_svm_math(kind,svm_input(&s,node+N_Math_value1),svm_input(&s,node+N_Math_value2),svm_input(&s,node+N_Math_value3));svm_store(&s,svm_byte(node+N_Math_result_offset),value);}
            }
            case NODE_VECTOR_MATH:{svm_vector(&s,node);}
            case NODE_CLAMP:{
                let kind=load_word(node+N_Clamp_clamp_type);var lo=svm_input(&s,node+N_Clamp_min);var hi=svm_input(&s,node+N_Clamp_max);let v=svm_input(&s,node+N_Clamp_value);
                if(kind==NODE_CLAMP_RANGE&&lo>hi){let tmp=lo;lo=hi;hi=tmp;}
                else if(kind!=NODE_CLAMP_RANGE&&kind!=NODE_CLAMP_MINMAX){svm_error(&s,16u);}
                // Cycles permits reversed MINMAX inputs; WGSL clamp requires ordered bounds.
                svm_store(&s,svm_byte(node+N_Clamp_result_offset),min(max(v,lo),hi));
            }
            case NODE_CONVERT:{
                let kind=load_word(node+N_Convert_convert_type);let source_index=svm_byte(node+N_Convert_from_offset);let dest=svm_byte(node+N_Convert_to_offset);
                switch(kind){
                    case NODE_CONVERT_FI:{svm_store_bits(&s,dest,svm_int(&s,svm_load(&s,source_index)));}
                    case NODE_CONVERT_FV:{svm_store3(&s,dest,vec3<f32>(svm_load(&s,source_index)));}
                    case NODE_CONVERT_IF:{svm_store(&s,dest,f32(bitcast<i32>(svm_load_bits(&s,source_index))));}
                    case NODE_CONVERT_IV:{svm_store3(&s,dest,vec3<f32>(f32(bitcast<i32>(svm_load_bits(&s,source_index)))));}
                    case NODE_CONVERT_CF,NODE_CONVERT_CI,NODE_CONVERT_VF,NODE_CONVERT_VI:{
                        let v=svm_load3(&s,source_index);var f=(v.x+v.y+v.z)*(1.0/3.0);
                        if(kind==NODE_CONVERT_CF||kind==NODE_CONVERT_CI){f=dot(v,heap_vec3(p(6u)+D_film_rgb_to_y));}
                        if(kind==NODE_CONVERT_CI||kind==NODE_CONVERT_VI){svm_store_bits(&s,dest,svm_int(&s,f));}else{svm_store(&s,dest,f);}
                    }
                    default:{svm_error(&s,16u);}
                }
            }
            case NODE_SEPARATE_VECTOR:{let component=svm_byte(node+N_SeparateVector_vector_index);if(component>2u){svm_error(&s,16u);}else{let v=svm_input3(&s,node+N_SeparateVector_vector);svm_optional_store(&s,svm_byte(node+N_SeparateVector_out_offset),v[component]);}}
            case NODE_COMBINE_VECTOR:{let component=svm_byte(node+N_CombineVector_vector_index);let dest=svm_byte(node+N_CombineVector_out_offset);if(component>2u){svm_error(&s,16u);}else if(dest!=SVM_STACK_INVALID){svm_store(&s,dest+component,svm_input(&s,node+N_CombineVector_in));}}
            case NODE_CLOSURE_BSDF:{if(SVM_SURFACE){svm_bsdf(&s,node,flag);}else{svm_error(&s,2u);}}
            case NODE_CLOSURE_HOLDOUT:{
                if(!SVM_SURFACE){svm_error(&s,2u);return s.output;}
                let mix_index=svm_byte(node+N_ClosureHoldout_mix_weight_offset);let mix_weight=svm_optional(&s,mix_index,1.0);
                if(mix_index==SVM_STACK_INVALID||mix_weight!=0.0){_ = svm_allocate(&s,CLOSURE_HOLDOUT_ID,s.weight*mix_weight);s.output.runtime_flag|=SR_HOLDOUT;}
            }
            case NODE_CLOSURE_SET_NORMAL:{if(!SVM_SURFACE){svm_error(&s,2u);return s.output;}let n=svm_load3(&s,svm_byte(node+N_ClosureSetNormal_direction_offset));s.geometry.N=n;svm_store3(&s,svm_byte(node+N_ClosureSetNormal_normal_offset),n);}
            case NODE_CLOSURE_SET_WEIGHT:{s.weight=heap_vec3(node+N_ClosureSetWeight_rgb);}
            case NODE_CLOSURE_WEIGHT:{s.weight=svm_load3(&s,svm_byte(node+N_ClosureWeight_weight_offset));}
            case NODE_EMISSION_WEIGHT:{s.weight=svm_input3(&s,node+N_EmissionWeight_color)*svm_input(&s,node+N_EmissionWeight_strength);}
            case NODE_CLOSURE_BACKGROUND,NODE_CLOSURE_EMISSION:{
                let offset=select(N_ClosureBackground_mix_weight_offset,N_ClosureEmission_mix_weight_offset,opcode==NODE_CLOSURE_EMISSION);
                let mix_index=svm_byte(node+offset);let mix_weight=svm_optional(&s,mix_index,1.0);
                if(mix_index==SVM_STACK_INVALID||mix_weight!=0.0){let weight=s.weight*mix_weight;if((s.output.runtime_flag&SR_EMISSION)!=0u){s.output.emission+=weight;}else{s.output.emission=weight;s.output.runtime_flag|=SR_EMISSION;}}
            }
            case NODE_MIX_CLOSURE:{
                let f=clamp(svm_input(&s,node+N_MixClosure_fac),0.0,1.0);let weight=svm_optional(&s,svm_byte(node+N_MixClosure_in_weight_offset),1.0);
                svm_optional_store(&s,svm_byte(node+N_MixClosure_weight1_offset),weight*(1.0-f));svm_optional_store(&s,svm_byte(node+N_MixClosure_weight2_offset),weight*f);
            }
            case NODE_TEX_CHECKER:{
                let co=svm_load3(&s,svm_byte(node+N_TexChecker_co))*svm_input(&s,node+N_TexChecker_scale);
                let adjusted=(co+vec3<f32>(0.000001))*0.999999;let parity=abs(vec3<i32>(floor(adjusted)))%vec3<i32>(2);
                let f=select(0.0,1.0,i32(parity.x==parity.y)==parity.z);
                let color1=svm_input3(&s,node+N_TexChecker_color1);let color2=svm_input3(&s,node+N_TexChecker_color2);
                svm_optional_store3(&s,svm_byte(node+N_TexChecker_color_offset),select(color2,color1,f==1.0));svm_optional_store(&s,svm_byte(node+N_TexChecker_fac_offset),f);
            }
            case NODE_TEX_GRADIENT:{let f=clamp(svm_gradient(&s,load_word(node+N_TexGradient_gradient_type),svm_load3(&s,svm_byte(node+N_TexGradient_co))),0.0,1.0);
                svm_optional_store(&s,svm_byte(node+N_TexGradient_fac_offset),f);svm_optional_store3(&s,svm_byte(node+N_TexGradient_color_offset),vec3<f32>(f));}
            case NODE_GAMMA:{svm_optional_store3(&s,svm_byte(node+N_Gamma_out_offset),c_svm_math_gamma_color(svm_input3(&s,node+N_Gamma_color),svm_input(&s,node+N_Gamma_gamma)));}
            case NODE_INVERT:{let color=svm_input3(&s,node+N_Invert_color);let f=svm_input(&s,node+N_Invert_fac);svm_optional_store3(&s,svm_byte(node+N_Invert_out_offset),f*(vec3<f32>(1)-color)+(1.0-f)*color);}
            case NODE_BRIGHTCONTRAST:{let color=svm_input3(&s,node+N_BrightContrast_color);let bright=svm_input(&s,node+N_BrightContrast_bright);let contrast=svm_input(&s,node+N_BrightContrast_contrast);
                svm_optional_store3(&s,svm_byte(node+N_BrightContrast_out_offset),max((1.0+contrast)*color+vec3<f32>(bright-contrast*0.5),vec3<f32>(0)));}
            default:{svm_error(&s,2u);}
        }
        if(s.error!=0u){return s.output;}
    }
    svm_error(&s,32u);return s.output;
}

fn svm_world(index:u32,shader_id:u32)->vec3<f32>{
    var sd:SurfaceData;let direction=heap_vec3(state_address(S_ray_D,index,12u));
    sd.P=direction;sd.N=-direction;sd.Ng=-direction;sd.wi=-direction;sd.object_id=NONE;sd.prim=NONE;
    sd.shader=shader_id;sd.shader_flag=load_word(p(18u)+shader_id*SIZE_shaders+SH_flags);
    sd.dPdu=surface_orthogonal(sd.Ng);sd.dPdv=cross(sd.Ng,sd.dPdu);
    sd.time=heap_float(state_address(S_ray_time,index,4u));sd.ray_length=bitcast<f32>(0x7f7fffffu);
    sd.dP=heap_float(state_address(S_ray_dD,index,4u));sd.dI=sd.dP;sd.du=vec2<f32>(sd.dP,0);sd.dv=vec2<f32>(0,sd.dP);
    return svm_evaluate(index,sd,0u,0u).emission;
}
