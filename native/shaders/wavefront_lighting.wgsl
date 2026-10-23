// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Native Cycles light distribution, point/sun sampling, BSDF evaluation and
// shadow-path initialization. See native/THIRD-PARTY-NOTICES.txt.
struct NativeLightSample {P:vec3<f32>,D:vec3<f32>,t:f32,pdf:f32,eval_fac:f32,shader:u32,object_id:u32,prim:u32,is_light:bool}
// GPU-private sample cache; 13 scalar words avoid host shading or readback.
fn light_sample_write(at:u32,ls:NativeLightSample){
    let words=array<u32,13>(bitcast<u32>(ls.P.x),bitcast<u32>(ls.P.y),bitcast<u32>(ls.P.z),bitcast<u32>(ls.D.x),bitcast<u32>(ls.D.y),bitcast<u32>(ls.D.z),bitcast<u32>(ls.t),bitcast<u32>(ls.pdf),bitcast<u32>(ls.eval_fac),ls.shader,ls.object_id,ls.prim,select(0u,1u,ls.is_light));
    for(var j=0u;j<13u;j++){store_word(at+j*4u,words[j]);}
}
fn light_sample_read(at:u32)->NativeLightSample{
    var ls:NativeLightSample;ls.P=heap_vec3(at);ls.D=heap_vec3(at+12u);ls.t=heap_float(at+24u);ls.pdf=heap_float(at+28u);ls.eval_fac=heap_float(at+32u);
    ls.shader=load_word(at+36u);ls.object_id=load_word(at+40u);ls.prim=load_word(at+44u);ls.is_light=load_word(at+48u)!=0u;return ls;
}
fn light_normalize_len(v:vec3<f32>)->vec4<f32>{
    let length=rounded_sqrt(rounded_dot(v,v));if(length==0.0){return vec4<f32>(0);}
    return vec4<f32>(rounded_divide(v.x,length),rounded_divide(v.y,length),rounded_divide(v.z,length),length);
}
fn light_basis(n:vec3<f32>)->MicrofacetBasis{var c:SvmClosure;c.N=n;return ggx_basis(c);}
fn light_cone(n:vec3<f32>,angle:f32,random:vec2<f32>)->vec4<f32>{
    if(angle==0.0){return vec4<f32>(n,1.0);}
    var xy=ggx_disk_sample(random);let r2=dot(xy,xy);let cosine=1.0-r2*angle;
    xy*=sqrt(max(angle*(2.0-angle*r2),0.0));let b=light_basis(n);
    return vec4<f32>(xy.x*b.X+xy.y*b.Y+cosine*n,(0.5*INV_PI)/angle);
}
// Light CDF selection (shared with triangle_light_prefill).
struct LightSelection {random:vec3<f32>,distribution:u32,prim:i32}
fn light_select(index:u32)->LightSelection{
    let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_LIGHT).xyz;
    let n=data_word(D_integrator_num_distribution);var first=0u;var length=n+1u;
    loop{let half_count=length>>1u;let middle=first+half_count;
        if(random.z<heap_float(table_at(G_light_distribution,middle,SIZE_light_distribution)+LD_totarea)){length=half_count;}
        else{first=middle+1u;length-=half_count+1u;}if(length==0u){break;}}
    let emitter=u32(clamp(i32(first)-1,0,i32(n)-1));
    let distribution=table_at(G_light_distribution,emitter,SIZE_light_distribution);
    return LightSelection(random,distribution,bitcast<i32>(load_word(distribution+LD_prim)));
}
// Emissive-triangle vertices come from triangle_light_prefill, which the caller
// runs first for the same invocation.
fn light_sample_native(index:u32,sd:SurfaceData,r:SvmResult)->NativeLightSample{
    let selection=light_select(index);
    if(selection.prim>=0){
        // Removed by scene specialization when no emissive triangle is in the light
        // distribution (pipelines.h); a reached removed handler reports error 2.
//#if LIGHT_TRIANGLE else var none:NativeLightSample;atomicOr(&heap[p(17u)/4u],2u);return none;
        return light_sample_triangle(sd,selection);
//#endif
    }
    return light_sample_lamp(index,sd,r,selection);
}
// The emissive triangle chosen by light_select (selection.prim >= 0).
fn light_sample_triangle(sd:SurfaceData,selection:LightSelection)->NativeLightSample{
    var ls=triangle_light_sample_native(load_word(selection.distribution+LD_object),u32(selection.prim),sd.time,sd.P,selection.random.xy);
    ls.shader|=load_word(selection.distribution+LD_visibility);return ls;
}
// The lamp chosen by light_select (selection.prim < 0). With emissive triangles in
// the scene, shade_surface_light calls this directly and the triangle work runs in
// shade_surface_light_triangle, so neither entry inlines both samplers.
fn light_sample_lamp(index:u32,sd:SurfaceData,r:SvmResult,selection:LightSelection)->NativeLightSample{
    var ls:NativeLightSample;
    let random=selection.random;let prim=selection.prim;
    ls.is_light=true;ls.prim=~bitcast<u32>(prim);
    let light=table_at(G_lights,ls.prim,SIZE_lights);
    if(f32(state_half(S_path_bounce,index))>heap_float(light+L_max_bounces)){return ls;}
    ls.shader=load_word(light+L_shader_id);ls.object_id=load_word(light+L_object_id);
    let position=heap_vec3(light+L_co);
    if(load_word(light+L_type)==LIGHT_SUN){
        let cone=light_cone(position,heap_float(light+L_sun_angle),random.xy);
        ls.P=cone.xyz;ls.D=-cone.xyz;ls.pdf=cone.w;ls.t=bitcast<f32>(0x7f7fffffu);ls.eval_fac=heap_float(light+L_sun_eval);
    }else if(load_word(light+L_type)==LIGHT_AREA){
        // Removed by scene specialization when no area light is uploaded (pipelines.h).
//#if LIGHT_AREA else atomicOr(&heap[p(17u)/4u],2u);return ls;
        let area=area_evaluate(light,sd.P,vec3<f32>(0),0.0,random.xy,true);if(!area.valid){return ls;}
        ls.P=area.P;ls.D=area.D;ls.t=area.t;ls.pdf=area.pdf;ls.eval_fac=area.eval_fac;
//#endif
    }else{
        let spot=load_word(light+L_type)==LIGHT_SPOT;
        let radius=heap_float(light+L_radius);let r2=radius*radius;let delta=sd.P-position;
        let d2=rounded_dot(delta,delta);let distance=rounded_sqrt(d2);let normal=delta/distance;
        ls.eval_fac=heap_float(light+L_point_eval);
        if(load_word(light+L_is_sphere)!=0u){
            var cosine:f32;var sampled_t= -1.0;
            if(d2>r2){let ratio=r2/d2;let angle=select(0.5*ratio,1.0-sqrt(max(1.0-ratio,0.0)),ratio>0.0004);
                let spot_angle=1.0-heap_float(light+L_spot_spread_cos);
                if(spot&&angle>=spot_angle){let cone=light_cone(-heap_vec3(light+L_spot_dir),spot_angle,random.xy);ls.D=cone.xyz;ls.pdf=cone.w;
                    sampled_t=emitter_sphere_hit(sd.P,ls.D,position,radius);if(sampled_t<0.0){ls.pdf=0.0;return ls;}
                }else{let cone=light_cone(-normal,angle,random.xy);ls.D=cone.xyz;ls.pdf=cone.w;
                    // The sampler's cosine is derived before transforming its direction.
                    let disk=ggx_disk_sample(random.xy);cosine=1.0-dot(disk,disk)*angle;}

            }else{
                if((r.runtime_flag&SR_BSDF_HAS_TRANSMISSION)!=0u){let z=1.0-2.0*random.x;let radial=sqrt(max(1.0-z*z,0.0));let phi=2.0*PI*random.y;
                    ls.D=vec3<f32>(radial*cos(phi),radial*sin(phi),z);ls.pdf=0.25*INV_PI;
                }else{let sampled=surface_cos_hemisphere(sd.N,random.xy);ls.D=sampled.xyz;ls.pdf=sampled.w;}
                cosine=-dot(ls.D,normal);
            }
            if(spot&&d2>r2){ls.eval_fac*=spot_attenuation(light,-ls.D);if(ls.eval_fac==0.0){ls.pdf=0.0;return ls;}}
            let root=sqrt(max(r2-d2+d2*cosine*cosine,0.0));ls.t=select(distance*cosine-select(-root,root,d2-r2>=0.0),sampled_t,sampled_t>=0.0);
            ls.P=sd.P+ls.D*ls.t;let ng=surface_normalize(ls.P-position);ls.P=ng*radius+position;
        }else{
            ls.P=position;if(radius>0.0){let b=light_basis(normal);let disk=ggx_disk_sample(random.xy);ls.P+=(b.X*disk.x+b.Y*disk.y)*radius;}
            let direction=light_normalize_len(ls.P-sd.P);ls.D=direction.xyz;ls.t=direction.w;
            if(spot){ls.eval_fac*=spot_attenuation(light,-ls.D);if(ls.eval_fac==0.0){ls.pdf=0.0;return ls;}}
            let cosine=dot(normal,-ls.D);let inverse_area=select(1.0,1.0/(r2*PI),r2>0.0);
            if(cosine>0.0){ls.pdf=inverse_area*(ls.t*ls.t/cosine);}
        }
    }
    ls.pdf*=data_float(D_integrator_distribution_pdf_lights);return ls;
}
fn light_bsdf(sd:SurfaceData,r:SvmResult,direction:vec3<f32>,shader:u32)->SurfaceSample{
    var result:SurfaceSample;var weight_sum=0.0;var pdf_sum=0.0;var rough_sum=0.0;
    for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(!surf_bsdf(c.kind)){continue;}
        let eval=surf_evaluate(sd,c,direction);
        if(eval.w!=0.0){
            let excluded=(surf_diffuse(c.kind)&&(shader&SHADER_EXCLUDE_DIFFUSE)!=0u)||
                (surf_glossy(c.kind)&&(shader&SHADER_EXCLUDE_GLOSSY)!=0u)||
                (surf_refraction(c.kind)&&(shader&SHADER_EXCLUDE_TRANSMIT)!=0u)||
                (surf_glass(c.kind)&&(shader&(SHADER_EXCLUDE_GLOSSY|SHADER_EXCLUDE_TRANSMIT))==(SHADER_EXCLUDE_GLOSSY|SHADER_EXCLUDE_TRANSMIT));
            if(!excluded){let value=eval.xyz*c.weight;result.eval+=value;if(surf_diffuse(c.kind)){result.diffuse+=value;}if(surf_glossy_direction(c,direction)){result.glossy+=value;}}
            let weight=eval.w*c.sample_weight;pdf_sum+=weight;rough_sum+=weight*surf_roughness(c);
        }weight_sum+=c.sample_weight;
    }
    result.roughness=select(0.0,rough_sum/pdf_sum,pdf_sum>0.0);
    if((shader&SHADER_USE_MIS)!=0u&&weight_sum>0.0){result.pdf=pdf_sum/weight_sum;}
    return result;
}
// Called by connect_surface_light with the sample stored by shade_surface_light;
// the split keeps emitter sampling and BSDF evaluation in separate pipelines.
fn surface_direct_light(index:u32,slot:u32,sd:SurfaceData,r:SvmResult,ls:NativeLightSample){
    if(data_word(D_integrator_use_direct_light)==0u||(r.runtime_flag&SR_BSDF_HAS_EVAL)==0u){return;}
    if(!(ls.pdf>0.0)||load_word(p(17u))!=0u){return;}
    let shader=table_at(G_shaders,ls.shader&SHADER_MASK,SIZE_shaders);
    var emission=heap_vec3(shader+SH_constant_emission);
    if(ls.is_light){emission*=heap_vec3(table_at(G_lights,ls.prim,SIZE_lights)+L_strength);}
    var bsdf=light_bsdf(sd,r,ls.D,ls.shader);let mis=(ls.pdf*ls.pdf)/(ls.pdf*ls.pdf+bsdf.pdf*bsdf.pdf);
    let factor=emission*ls.eval_fac/ls.pdf*mis;bsdf.eval*=factor;bsdf.diffuse*=factor;bsdf.glossy*=factor;
    if(all(bsdf.eval==vec3<f32>(0))){return;}
    let rr=data_float(D_integrator_light_inv_rr_threshold);
    let probability=select(1.0,max(abs(bsdf.eval.x),max(abs(bsdf.eval.y),abs(bsdf.eval.z)))*rr,rr>0.0);
    if(probability<1.0){let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_LIGHT_TERMINATE).x;
        if(random>=probability){return;}let inverse=1.0/probability;bsdf.eval*=inverse;bsdf.diffuse*=inverse;bsdf.glossy*=inverse;}
    // Shadow terminator geometry offset (shadow_ray_offset) moves the origin of
    // smooth-shaded triangles first; w=0 means the ray no longer skips its own
    // primitive and takes no self-intersection offset (light/sample.h).
    var start=vec4<f32>(sd.P,1.0);
    if(terminator_geometry_active(sd)){
//#if SHADOW_TERMINATOR_GEOMETRY else atomicOr(&heap[p(17u)/4u],2u);return;
        start=terminator_shadow_origin(sd,ls.D);
//#endif
    }
    var direction=ls.D;var tmax=ls.t;if(ls.t!=bitcast<f32>(0x7f7fffffu)){let ray=light_normalize_len(ls.P-start.xyz);direction=ray.xyz;tmax=ray.w;}
    var origin=start.xyz;if(start.w!=0.0){origin=surf_ray_offset_at(sd,start.xyz,direction);}let allocator=load_word(p(7u)+S_next_shadow_path_index)/4u;var shadow:u32;
    if(p(28u)!=0u){shadow=load_word(p(28u))+slot;if(shadow>=p(25u)){fail();return;}}
    else{loop{shadow=atomicLoad(&heap[allocator]);if(shadow>=p(25u)){fail();return;}
        if(atomicCompareExchangeWeak(&heap[allocator],shadow,shadow+1u).exchanged){break;}}}
    state_vec3(S_shadow_ray_P,shadow,origin);state_vec3(S_shadow_ray_D,shadow,direction);
    state_f32(S_shadow_ray_tmin,shadow,select(bitcast<f32>(0x7f7fffffu),0.0,(ls.shader&SHADER_CAST_SHADOW)!=0u));state_f32(S_shadow_ray_tmax,shadow,tmax);
    state_f32(S_shadow_ray_time,shadow,sd.time);state_f32(S_shadow_ray_dP,shadow,sd.dP);
    state_f32(S_shadow_ray_dD,shadow,select(sd.dI,max(sd.dI,sqrt(bsdf.roughness)),bsdf.roughness>0.0));
    state_u32(S_shadow_self_object,shadow,select(NONE,sd.object_id,start.w!=0.0));state_u32(S_shadow_self_prim,shadow,select(NONE,sd.prim,start.w!=0.0));
    state_u32(S_shadow_ray_self_light_object,shadow,ls.object_id);state_u32(S_shadow_ray_self_light_prim,shadow,ls.prim);
    state_f32(S_shadow_path_bsdf_eval_average,shadow,(bsdf.eval.x+bsdf.eval.y+bsdf.eval.z)*(1.0/3.0));
    state_u32(S_shadow_path_render_pixel_index,shadow,state_word(S_path_render_pixel_index,index));
    state_u32(S_shadow_path_sample,shadow,state_word(S_path_sample,index));state_u32(S_shadow_path_rng_pixel,shadow,state_word(S_path_rng_pixel,index));
    state_u16(S_shadow_path_rng_offset,shadow,state_half(S_path_rng_offset,index));state_u16(S_shadow_path_bounce,shadow,state_half(S_path_bounce,index));
    state_u16(S_shadow_path_diffuse_bounce,shadow,state_half(S_path_diffuse_bounce,index));state_u16(S_shadow_path_glossy_bounce,shadow,state_half(S_path_glossy_bounce,index));
    state_u16(S_shadow_path_transmission_bounce,shadow,state_half(S_path_transmission_bounce,index));state_u16(S_shadow_path_transparent_bounce,shadow,state_half(S_path_transparent_bounce,index));
    state_u16(S_shadow_path_volume_bounds_bounce,shadow,state_half(S_path_volume_bounds_bounce,index));
    state_vec3(S_shadow_path_throughput,shadow,heap_vec3(state_address(S_path_throughput,index,12u))*bsdf.eval);
    let group=load_word(table_at(G_objects,ls.object_id,SIZE_objects)+O_lightgroup);state_u8(S_shadow_path_lightgroup,shadow,group+1u);
    var flag=state_word(S_path_flag,index);
    if(p(20u)!=0u){var diffuse=svm_divide(bsdf.diffuse,bsdf.eval);var glossy=svm_divide(bsdf.glossy,bsdf.eval);
        if((flag&PATH_RAY_ANY_PASS)!=0u){diffuse=heap_vec3(state_address(S_path_pass_diffuse_weight,index,12u));glossy=heap_vec3(state_address(S_path_pass_glossy_weight,index,12u));}
        else{flag|=PATH_RAY_SURFACE_PASS;}
        state_vec3(S_shadow_path_pass_diffuse_weight,shadow,diffuse);state_vec3(S_shadow_path_pass_glossy_weight,shadow,glossy);
    }
    state_u8(S_shadow_path_visibility,shadow,state_byte(S_path_visibility,index));state_u32(S_shadow_path_flag,shadow,flag);
    state_u16(S_shadow_path_queued_kernel,shadow,K_SHADOW_INTERSECT);let counter=load_word(p(7u)+S_queue_counter)/4u;atomicAdd(&heap[counter+K_SHADOW_INTERSECT],1u);
}
