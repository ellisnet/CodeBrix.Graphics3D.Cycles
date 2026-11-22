//#if DISPERSION
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles kernel/geom/shader_data.h (shader_setup_wavelength),
// kernel/closure/bsdf_microfacet.h (bsdf_glass_ior), kernel/util/colorspace.h
// (wavelength_to_rgb_d65, sample_wavelength), kernel/integrator/path_state.h
// (dispersion_throughput_weight, update_path_throughput_for_dispersion) and
// util/math_fast.h (fast_logf): the kernel code under __SPECTRAL__ in WGSL.
// The whole file is one DISPERSION specialization region, so a scene without a
// shader flagged SD_REQUIRES_WAVELENGTH compiles byte-identical WGSL. The
// constants and disp_table (row i of cie_color_match in xyz and of cie_d65_spd
// in w) are written by svm_layout.cpp. Every sum, difference and division and
// the dot products go through the correctly rounded adapters in the pinned
// operation order: a driver may contract or rewrite a plain sum beside a
// product. A product that feeds another product leaves through an ordering
// barrier (disp_fence): a driver may otherwise reassociate the chain. Sums that
// repeat run through one adapter body in a loop (compiler pressure, as
// rounded_add3).
//
// ShaderData::rand_wavelength of the evaluation that follows, as sample_wavelength returns it for that number: x the
// wavelength in um, y the probability (*prob). The surface stage and the MNEE entries set it where the pinned code
// calls shader_setup_wavelength; it is the same number for every bounce of a path, so one sample serves the index of
// refraction and the throughput weight. The other evaluations (transparent shadows) allocate no closure, so no code
// of theirs reads it.
var<private> disp_sampled:vec2<f32>=vec2<f32>(0.0);
// shader_setup_wavelength's draw: path_rng_1D(rng_pixel, sample, PRNG_BOUNCE_NUM + PRNG_WAVELENGTH).
fn disp_path_rand(index:u32)->f32{
//#if SAMPLING_SOBOL_BURLEY else     return native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),DISP_PRNG_WAVELENGTH).x;
    return native_rng_pattern(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),DISP_PRNG_WAVELENGTH,1u).x;
//#endif
}
// The ordering barrier: push word 31 is 0 in every dispatch and the compiler cannot see it (as rounded_steps).
fn disp_fence(x:f32)->f32{return bitcast<f32>(bitcast<u32>(x)|p(31u));}
fn disp_fence3(x:vec3<f32>)->vec3<f32>{return bitcast<vec3<f32>>(bitcast<vec3<u32>>(x)|vec3<u32>(p(31u)));}
// util/math_fast.h fast_logf: madd is a product and a sum (no fused multiply-add in the oracle's arithmetic). The six
// madd steps run through one body (steps 0 to 3 are hi's, 4 and 5 are lo's), the last two sums through another.
fn disp_fast_log(x_in:f32)->f32{
    let x=clamp(x_in,1.17549435e-38,3.40282347e+38);let bits=bitcast<u32>(x);let exponent=f32(i32(bits>>23u)-127);
    let f=rounded_add(bitcast<f32>((bits&0x007FFFFFu)|0x3f800000u),-1.0);let f2=disp_fence(f*f);let f4=disp_fence(f2*f2);
    var hi=-0.00931049621349;var lo=0.47868480909345;let steps=rounded_steps(6u);
    for(var k=0u;k<steps;k++){
        var c=1.442689881667200;
        switch(k){case 0u:{c=0.05206469089414;}case 1u:{c=-0.13753123777116;}case 2u:{c=0.24187369696082;}case 3u:{c=-0.34730547155299;}case 4u:{c=-0.72116591947498;}default:{}}
        let r=rounded_add(f*select(lo,hi,k<4u),c);
        if(k<4u){hi=r;}else if(k<6u){lo=r;}
    }
    // ((f4 * hi) + (f * lo)) + exponent.
    var sum=f4*hi;let sums=rounded_steps(2u);
    for(var k=0u;k<sums;k++){if(k<2u){sum=rounded_add(sum,select(exponent,f*lo,k==0u));}}
    return disp_fence(sum*bitcast<f32>(0x3f317218u));
}
// sample_wavelength: x the wavelength in um, y the probability (*prob).
fn disp_sample_wavelength(rand_in:f32)->vec2<f32>{
    let rand=rounded_add(DISP_N*rand_in,DISP_Y0);
    let prob=disp_fence(disp_fence(disp_fence(DISP_A*rand)*rounded_add(1.0,-rand))*DISP_PROB_SCALE);
    let wavelength=rounded_add(rounded_divide(-disp_fast_log(rounded_add(rounded_divide(1.0,rand),-1.0)),DISP_A),DISP_X0);
    return vec2<f32>(clamp(wavelength,DISP_CIE_MIN,DISP_CIE_MAX),prob);
}
// shader_setup_wavelength, with the sample made at once.
fn disp_setup_wavelength(index:u32){disp_sampled=disp_sample_wavelength(disp_path_rand(index));}
// bsdf_glass_ior: the index of refraction at the sampled wavelength (Cauchy), for an ior that has the back-face flip applied.
fn disp_glass_ior(s:ptr<function,SvmState>,ior_in:f32,inv_abbe:f32)->f32{
    // inv_abbe == 0.0f is false for a NaN (closure/bsdf_microfacet.h): on the bits.
    if(((*s).geometry.shader_flag&DISP_SD_REQUIRES_WAVELENGTH)==0u||(bitcast<u32>(inv_abbe)&0x7fffffffu)==0u){return ior_in;}
    let backfacing=((*s).geometry.runtime_flag&SR_BACKFACING)!=0u;
    (*s).output.runtime_flag|=DISP_SR_BSDF_HAS_DISPERSION;
    // The two back-face reciprocals through one body: step 0 undoes the flip, step 1 makes the formula and flips again.
    var ior=ior_in;let wavelength=disp_sampled.x;let count=rounded_steps(2u);
    for(var k=0u;k<count;k++){
        if(k==1u){
            let B=disp_fence(disp_fence(rounded_add(ior,-1.0)*inv_abbe)*DISP_FAC);
            let A=rounded_add(ior,-(B*DISP_INV_LAMBDA_D_SQ));
            ior=rounded_add(A,rounded_divide(B,wavelength*wavelength));
        }
        if(backfacing&&k<2u){ior=rounded_divide(1.0,ior);}
    }
    return ior;
}
// wavelength_to_rgb_d65: the working-space colour of one wavelength in um.
fn disp_wavelength_to_rgb(lambda_um:f32)->vec3<f32>{
    // x - floor(x) is exact, whatever a driver makes of it.
    let x=rounded_divide(rounded_add(lambda_um,-DISP_CIE_MIN),DISP_DLAMBDA);let whole=floor(x);let i=i32(whole);let f=x-whole;
    if(i<0||i>=i32(DISP_RESOLUTION)){return vec3<f32>(0);}
    // Rows i and i + 1 through one body.
    var c0=vec4<f32>(0);var c1=vec4<f32>(0);let rows=rounded_steps(2u);
    for(var k=0u;k<rows;k++){let row=disp_table(u32(i)+k);if(k==0u){c0=row;}else{c1=row;}}
    // mix(a, b, f) = a + f * (b - a) of the three colour lanes and of the illuminant lane, through one body.
    var mixed=vec4<f32>(0);let lanes=rounded_steps(4u);
    for(var k=0u;k<lanes;k++){if(k<4u){mixed[k]=rounded_add(c0[k],f*rounded_add(c1[k],-c0[k]));}}
    let xyz=disp_fence3(disp_fence3(mixed.xyz*mixed.w)*DISP_D65_NORMALIZATION);
    // xyz_to_rgb: the three dot products through one body.
    let film=p(6u);var rgb=vec3<f32>(0);let channels=rounded_steps(3u);
    for(var k=0u;k<channels;k++){
        if(k<3u){rgb[k]=rounded_dot(heap_vec3(film+select(select(DISP_D_film_xyz_to_b,DISP_D_film_xyz_to_g,k==1u),DISP_D_film_xyz_to_r,k==0u)),xyz);}
    }
    return rgb;
}
// dispersion_throughput_weight of a sample: the colour over the probability (float3 / float as the oracle divides it).
fn disp_throughput_weight(sampled:vec2<f32>)->vec3<f32>{
    let rgb=disp_wavelength_to_rgb(sampled.x);
    // One division body: the reciprocal alone where the oracle multiplies by it, each lane otherwise.
    let reciprocal=NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u;
    var out=vec3<f32>(0);let count=rounded_steps(select(3u,1u,reciprocal));
    for(var k=0u;k<count;k++){
        if(k<3u){
            let q=rounded_divide(select(rgb[k],1.0,reciprocal),sampled.y);
            if(reciprocal){out=disp_fence3(rgb*q);}else{out[k]=q;}
        }
    }
    return out;
}
// update_path_throughput_for_dispersion: once per path, when its first dispersive closure is made.
fn disp_update_path_throughput(index:u32){
    let flag=state_word(S_path_flag,index);if((flag&DISP_PATH_RAY_SPECTRAL)!=0u){return;}
    state_vec3(S_path_throughput,index,heap_vec3(state_address(S_path_throughput,index,12u))*disp_throughput_weight(disp_sampled));
    state_u32(S_path_flag,index,flag|DISP_PATH_RAY_SPECTRAL);
}
//#endif
