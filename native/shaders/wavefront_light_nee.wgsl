// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_NEE, adapted from the pinned
// kernel/integrator/shade_light.h (integrate_light_nee), kernel/light/triangle.h
// (triangle_light_uv) and util/math_intersect.h (ray_triangle_uv). A shadow path
// whose sampled light has no constant emission arrives here from
// connect_surface_light: the emission shader is evaluated at the light point
// (the world shader for the background light, the triangle's surface shader for
// an emissive triangle), the light-threshold roulette runs with the stored
// bsdf_eval_average and the shadow throughput is multiplied before the shadow
// ray is traced. Non-constant point/spot/area/sun shaders are refused on the
// host (lighting.cpp) and report an error here.
//#if LIGHT_NEE
// ray_triangle_uv with the oracle-ordered intersection arithmetic.
fn light_nee_triangle_uv(P:vec3<f32>,D:vec3<f32>,a:vec3<f32>,b:vec3<f32>,c:vec3<f32>)->vec2<f32>{
    let v0=a-P;let v1=b-P;let v2=c-P;
    let edges=array<vec3<f32>,3>(v2-v0,v0-v1,v1-v2);let sums=array<vec3<f32>,3>(v2+v0,v0+v1,v1+v2);var uvw:array<f32,3>;
    for(var i=0u;i<3u;i++){uvw[i]=exact_tri_dot(exact_tri_cross(edges[i],sums[i]),D);}
    let UVW=rounded_add(rounded_add(uvw[0],uvw[1]),uvw[2]);
    var rcp_uvw=0.0;if(!(abs(UVW)<1e-18)){rcp_uvw=exact_tri_reciprocal(UVW);}
    return vec2<f32>(clamp(uvw[0]*rcp_uvw,0.0,1.0),clamp(uvw[1]*rcp_uvw,0.0,1.0));
}
// ShaderData at the light point (shade_light.h): the world for the background
// light, shader_setup_from_ray with triangle_light_uv for an emissive triangle.
fn light_nee_setup(object:u32,prim:u32,P:vec3<f32>,D:vec3<f32>,tmax:f32,time:f32,dP:f32,dD:f32)->SurfaceData{
    var sd:SurfaceData;
    if(object>=table_count(G_objects)){surface_failure(&sd);return sd;}
    let kind=load_word(table_at(G_objects,object,SIZE_objects)+O_primitive_type);
    if(kind==PRIMITIVE_LAMP){
        if(prim>=table_count(G_lights)||load_word(table_at(G_lights,prim,SIZE_lights)+L_type)!=LIGHT_BACKGROUND){surface_failure(&sd);return sd;}
//#if LIGHT_BACKGROUND else surface_failure(&sd);return sd;
        sd=bgl_world_data(D,min(dD,data_float(D_background_map_dD)),time);
//#endif
    }else{
//#if LIGHT_TRIANGLE else surface_failure(&sd);return sd;
        let v=triangle_light_vertices_compute(object,prim,time);if(v.error!=0u){sd.error=1u;return sd;}
        let uv=light_nee_triangle_uv(P,D,v.a,v.b,v.c);
        sd=surface_setup_hit(kind,object,prim,uv.x,uv.y,tmax,time,D,dP,dD);
//#endif
    }
    return sd;
}
fn light_nee_path(id:vec3<u32>)->u32{
    if(id.x>=p(1u)){return NONE;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return NONE;}
    if(state_half(S_shadow_path_queued_kernel,index)!=K_LIGHT_NEE){fail();return NONE;}
    return index;
}
// Two dispatches keep each entry within the compile envelope: prepare_light_nee
// writes the light point's ShaderData to the GPU-private cache (p(26), stride
// p(27)); shade_light_nee evaluates the emission shader from it.
@compute @workgroup_size(64) fn prepare_light_nee(@builtin(global_invocation_id) id:vec3<u32>){
    let index=light_nee_path(id);if(index==NONE){return;}let at=surface_cache_address(id.x);if(at==0u){return;}
    let sd=light_nee_setup(state_word(S_shadow_ray_self_light_object,index),state_word(S_shadow_ray_self_light_prim,index),
        heap_vec3(state_address(S_shadow_ray_P,index,12u)),heap_vec3(state_address(S_shadow_ray_D,index,12u)),
        heap_float(state_address(S_shadow_ray_tmax,index,4u)),heap_float(state_address(S_shadow_ray_time,index,4u)),
        heap_float(state_address(S_shadow_ray_dP,index,4u)),heap_float(state_address(S_shadow_ray_dD,index,4u)));
    surface_data_write(at,sd);
}
@compute @workgroup_size(64) fn shade_light_nee(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    let index=light_nee_path(id);if(index==NONE){return;}
    let sd=surface_cached(id.x);if(sd.error!=0u||load_word(p(17u))!=0u){return;}
    // No path flag beyond emission: evaluated for all closures (shade_light.h).
    let r=svm_evaluate_context(NONE,sd,0u,0u,PATH_RAY_EMISSION,0u,0u,false);if(r.error!=0u){return;}
    // light_sample_terminate with the stored average BSDF evaluation.
    var light=r.emission;var next=K_SHADOW_INTERSECT;
    if(all(light==vec3<f32>(0))){next=0u;}
//#if LIGHT_TREE
    // No light threshold roulette with the light tree (integrator_shade_light_nee).
    else if((data_word(D_kernel_features)&LT_FEATURE_LIGHT_TREE)!=0u){}
//#endif
    else{
        let rr=data_float(D_integrator_light_inv_rr_threshold);
        let weighted=light*heap_float(state_address(S_shadow_path_bsdf_eval_average,index,4u));
        let probability=select(1.0,max(abs(weighted.x),max(abs(weighted.y),abs(weighted.z)))*rr,rr>0.0);
        if(probability<1.0){
            let random=native_rng(state_word(S_shadow_path_sample,index),state_word(S_shadow_path_rng_pixel,index),state_half(S_shadow_path_rng_offset,index)+PRNG_LIGHT_TERMINATE).x;
            if(random>=probability){next=0u;}else{light*=rounded_divide(1.0,probability);}
        }
    }
    if(next!=0u){state_vec3(S_shadow_path_throughput,index,heap_vec3(state_address(S_shadow_path_throughput,index,12u))*light);}
    let counter=load_word(p(7u)+S_queue_counter)/4u;atomicSub(&heap[counter+K_LIGHT_NEE],1u);
    if(next!=0u){atomicAdd(&heap[counter+next],1u);}
    state_u16(S_shadow_path_queued_kernel,index,next);
}
// Light shader evaluation for probes: setup and evaluation in one call; w is 0 after an error.
fn light_nee_eval(object:u32,prim:u32,P:vec3<f32>,D:vec3<f32>,tmax:f32,time:f32,dP:f32,dD:f32)->vec4<f32>{
    let sd=light_nee_setup(object,prim,P,D,tmax,time,dP,dD);if(sd.error!=0u){return vec4<f32>(0);}
    let r=svm_evaluate_context(NONE,sd,0u,0u,PATH_RAY_EMISSION,0u,0u,false);if(r.error!=0u){return vec4<f32>(0);}
    return vec4<f32>(r.emission,1.0);
}
//#endif
