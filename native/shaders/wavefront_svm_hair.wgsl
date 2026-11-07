//#if SVM_BSDF_HAIR
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from Cycles: svm/closure.h (the Hair BSDF node), closure/bsdf_hair.h (adapted from Open Shading
// Language; BSD-3-Clause), closure/bsdf_principled_hair_chiang.h, closure/bsdf_util.h. See
// native/THIRD-PARTY-NOTICES.txt and parity/licenses/BSD-3-Clause.txt.
// Record (surface_closure.h): T = T, alpha_x = roughness1, alpha_y = roughness2, a = offset.
fn svm_hair(s:ptr<function,SvmState>,data:u32,kind:u32,mix_weight:f32){
    let i=svm_family_allocate(s,kind,mix_weight);if(i==NONE){return;}
    var c=svm_closure_read((*s).output,i);let sd=(*s).geometry;let curve=(sd.primitive_type&HAIR_PRIMITIVE_CURVE)!=0u;
    // maybe_ensure_valid_specular_reflection(sd, sd->N).
    c.N=sd.N;if((sd.shader_flag&SD_USE_BUMP_MAP_CORRECTION)!=0u&&!curve&&any(sd.Ng!=sd.N)){c.N=svm_valid_normal(sd.Ng,sd.wi,sd.N);}
    c.alpha_x=clamp(svm_input(s,data+N_HairBsdfData_roughness1),0.001,1.0);c.alpha_y=clamp(svm_input(s,data+N_HairBsdfData_roughness2),0.001,1.0);
    c.a=-svm_input(s,data+N_HairBsdfData_offset);
    let tangent=svm_byte(data+N_HairBsdfData_tangent_offset);
    if(tangent!=SVM_STACK_INVALID){c.T=surface_native_normalize(svm_load3(s,tangent));}
    else if(!curve){c.T=surface_native_normalize(sd.dPdv);c.a=0.0;}
    else{c.T=surface_native_normalize(sd.dPdu);}
    (*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_EVAL;
    if(kind==CLOSURE_BSDF_HAIR_TRANSMISSION_ID){(*s).output.runtime_flag|=SR_BSDF_HAS_TRANSMISSION;}
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);
}
//#endif
//#if SVM_BSDF_HAIR_PRINCIPLED
// Shared by the Principled Hair BSDF node's models (svm/closure.h, closure/bsdf_util.h).
// logf of a colour lane: zero gives minus infinity, as on the host.
fn svm_hair_logf(x:f32)->f32{if(x==0.0){return bitcast<f32>(0xff800000u);}return vp_logf(x);}
// The absorption coefficient of the three colour parametrizations. Direct colouring and the melanin tint share one
// bsdf_principled_hair_sigma_from_reflectance (every product rounded before its sum; the float3 / float follows the host's
// arithmetic: per-lane division on SSE, reciprocal multiplication on scalar float3), and its three lane logarithms and the
// melanin remap's go through one logf body.
fn svm_hair_sigma(s:ptr<function,SvmState>,data:u32,random:f32,radial_roughness:f32)->vec3<f32>{
    let parametrization=load_word(data+N_PrincipledHairBsdfData_parametrization);
    if(parametrization==HAIR_PARAM_DIRECT_ABSORPTION){return svm_input3(s,data+N_PrincipledHairBsdfData_absorption_coefficient);}
    let melanin_case=parametrization==HAIR_PARAM_PIGMENT_CONCENTRATION;
    var melanin=0.0;var redness=0.0;
    if(melanin_case){
        melanin=svm_input(s,data+N_PrincipledHairBsdfData_melanin);redness=svm_input(s,data+N_PrincipledHairBsdfData_melanin_redness);
        let random_color=clamp(svm_input(s,data+N_PrincipledHairBsdfData_random_color),0.0,1.0);
        melanin*=rounded_add(1.0,(2.0*rounded_add(random,-0.5))*random_color);
    }
    var color=vec3<f32>(0);
    if(parametrization==HAIR_PARAM_REFLECTANCE){color=svm_input3(s,data+N_PrincipledHairBsdfData_color);}
    else if(melanin_case){color=svm_input3(s,data+N_PrincipledHairBsdfData_tint);}
    let lanes=max(color,vec3<f32>(0));var l=vec3<f32>(0);var log_melanin=0.0;let count=rounded_steps(select(3u,4u,melanin_case));
    for(var k=0u;k<count;k++){
        // Map melanin 0..inf from more perceptually linear 0..1 (the fourth logarithm).
        let x=select(select(select(lanes.z,lanes.y,k==1u),lanes.x,k==0u),max(rounded_add(1.0,-melanin),0.0001),k==3u);
        let v=svm_hair_logf(x);if(k==3u){log_melanin=v;}
        l=select(l,vec3<f32>(v),vec3<u32>(0u,1u,2u)==vec3<u32>(k));
    }
    let x=radial_roughness;
    let scale=rounded_add(rounded_add(rounded_add(rounded_add(rounded_add(0.245*x,5.574)*x,-10.73)*x,2.532)*x,-0.215)*x,5.969);
    var q=vec3<f32>(0);
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){q=l*rounded_divide(1.0,scale);}
    else{let lanes3=rounded_steps(3u);for(var k=0u;k<lanes3;k++){let r=rounded_divide(select(select(l.z,l.y,k==1u),l.x,k==0u),scale);q=select(q,vec3<f32>(r),vec3<u32>(0u,1u,2u)==vec3<u32>(k));}}
    let reflectance_sigma=q*q;
    if(parametrization==HAIR_PARAM_REFLECTANCE){return reflectance_sigma;}
    // Benedikt Bitterli's melanin ratio remapping; the invalid-parametrization fallback is brownish hair (0, 0.8054375).
    var eumelanin=0.0;var pheomelanin=0.8054375;
    if(melanin_case){melanin=-log_melanin;eumelanin=melanin*rounded_add(1.0,-redness);pheomelanin=melanin*redness;}
    let melanin_sigma=rounded_add3(eumelanin*vec3<f32>(0.506,0.841,1.653),pheomelanin*vec3<f32>(0.343,0.733,1.924));
    if(melanin_case){return rounded_add3(melanin_sigma,reflectance_sigma);}
    return melanin_sigma;
}
//#endif
//#if SVM_BSDF_HAIR_CHIANG
// Principled Hair BSDF node, Chiang model. Record (surface_closure.h): multiscatter = sigma,
// roughness = v, a = s, b = alpha, ior = eta, alpha_x = m0_roughness, alpha_y = h.
fn svm_hair_chiang(s:ptr<function,SvmState>,data:u32,mix_weight:f32){
    let sd=(*s).geometry;
    let alpha=svm_input(s,data+N_PrincipledHairBsdfData_offset);let ior=svm_input(s,data+N_PrincipledHairBsdfData_ior);
    // find_attribute and primitive_surface_attribute<float> (Hair Info's Random), or the input.
    var random=0.0;let random_attribute=attribute_find(s,load_word(data+N_PrincipledHairBsdfData_attr_random),0u);if((*s).error!=0u){return;}
    if(random_attribute.found){random=attribute_read(s,random_attribute).value.x;if((*s).error!=0u){return;}}
    else{random=svm_input(s,data+N_PrincipledHairBsdfData_random);}
    // Random factors range: [-randomization/2, +randomization/2].
    let factor=rounded_add(1.0,(2.0*rounded_add(random,-0.5))*svm_input(s,data+N_PrincipledHairBsdfData_random_roughness));
    let roughness=svm_input(s,data+N_PrincipledHairBsdfData_roughness)*factor;
    let radial_roughness=svm_input(s,data+N_PrincipledHairBsdfData_radial_roughness)*factor;
    let sigma=svm_hair_sigma(s,data,random,radial_roughness);
    let i=svm_family_allocate(s,CLOSURE_BSDF_HAIR_CHIANG_ID,mix_weight);if(i==NONE){return;}
    var c=svm_closure_read((*s).output,i);
    // bsdf_hair_chiang_setup. Coat remaps to [0, 100]% of Roughness.
    let m0_input=rounded_add(1.0,-clamp(svm_input(s,data+N_PrincipledHairBsdfData_coat),0.0,1.0));
    let v=clamp(roughness,0.001,1.0);let radial=clamp(radial_roughness,0.001,1.0);let m0=clamp(m0_input*v,0.001,1.0);
    // Map from roughness_u and roughness_v to variance and scale factor (the three fits through one body).
    var fit=vec3<f32>(0);let count=rounded_steps(3u);
    for(var k=0u;k<count;k++){
        let x=select(select(m0,radial,k==1u),v,k==0u);let x2=x*x;let x4=x2*x2;let x5=x4*x;let x10=x5*x5;let x20=x10*x10;
        // pow20(a) = sqr(sqr(sqr(sqr(a)) * a)); pow22(a) = sqr(a * sqr(sqr(sqr(a)) * a)).
        let far=select(3.700*x20,5.372*((x*x10)*(x*x10)),k==1u);
        let near=rounded_add(rounded_add(select(0.726,0.265,k==1u)*x,select(0.812,1.194,k==1u)*x2),far);
        fit=select(fit,vec3<f32>(select(near*near,near*0.6266570686577501,k==1u)),vec3<u32>(0u,1u,2u)==vec3<u32>(k));
    }
    c.roughness=fit.x;c.a=fit.y;c.alpha_x=fit.z;c.b=-alpha;c.ior=ior;c.multiscatter=sigma;
    // Local frame, aligned to curve tangent and ray direction.
    let X=surface_normalize(sd.dPdu);let Y=surface_normalize(rounded_cross(X,sd.wi));let Z=surface_normalize(rounded_cross(X,Y));
    // h -1..0..1 means the rays goes from grazing the hair, to hitting it at the center, to grazing the other edge.
    c.alpha_y=select(rounded_dot(rounded_cross(sd.Ng,X),Z),-sd.v,(sd.primitive_type&CHIANG_PRIMITIVE_CURVE)==CHIANG_PRIMITIVE_CURVE_RIBBON);
    c.N=Y;
    (*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_EVAL|SR_BSDF_HAS_TRANSMISSION;
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);
}
//#endif
