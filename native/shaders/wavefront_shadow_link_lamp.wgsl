// SPDX-FileCopyrightText: 2011-2023 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// DEVICE_KERNEL_INTEGRATOR_SHADE_DEDICATED_LIGHT, part one: shadow_linking_shade_light of the pinned
// kernel/integrator/shade_dedicated_light.h for a lamp or sun picked by intersect_dedicated_light, in the module with
// the forward lamp evaluation and its tree pdf (light_tree_forward). It leaves the shadow path's constant light value
// in the per-dispatch record that shade_dedicated_light (wavefront_shadow_link_shade.wgsl) reads. Record, 32 bytes per
// input at p(24): [0] 1 = shadow path wanted, [1] MIS weight, [2..4] light value, [5] light group, [6] 1 = constant
// emission, [7] the tree's forward pdf of a triangle (surface module).
//#if SHADOW_LINKING
@compute @workgroup_size(64) fn shade_dedicated_light_lamp(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=DL_K_SHADE){fail();return;}
    if(state_word(S_isect_type,index)!=PRIMITIVE_LAMP){return;}
    let at=p(24u)+id.x*DL_RECORD_BYTES;
    let lamp=state_word(S_isect_prim,index);if(lamp>=table_count(G_lights)){fail();return;}
    let light=table_at(G_lights,lamp,SIZE_lights);let kind=load_word(light+L_type);let light_shader=load_word(light+L_shader_id);
    let flag=state_word(S_path_flag,index);let visibility=state_byte(S_path_visibility,index);
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));
    // shadow_linking_light_eval_from_intersection: LightEval{eval_fac, pdf}.
    var factor=0.0;var pdf=0.0;
    if(kind==LIGHT_SUN){if(sun_forward_visible(light,direction)){factor=heap_float(light+L_sun_eval);pdf=heap_float(light+L_sun_pdf);}}
    else if(kind==LIGHT_AREA){
//#if LIGHT_AREA else atomicOr(&heap[p(17u)/4u],2u);fail();return;
        let area=area_evaluate(light,origin,direction,heap_float(state_address(S_isect_t,index,4u)),vec2<f32>(0),false);factor=select(0.0,area.eval_fac,area.valid);pdf=area.pdf;
//#endif
    }else{
        factor=heap_float(light+L_point_eval);pdf=point_forward_pdf(index,light);
        if(kind==LIGHT_SPOT){let delta=origin-heap_vec3(light+L_co);let radius=heap_float(light+L_radius);
            if(load_word(light+L_is_sphere)==0u||dot(delta,delta)>radius*radius){factor*=spot_attenuation(light,-direction);}}
    }
    store_word(at,0u);
    if(factor==0.0||!background_visible(light_shader,visibility,flag)){return;}
//#if LIGHT_TREE
    forward_light_slot=0u;
//#endif
    let mis=forward_mis_weight(index,pdf);
    // light_sample_shader_eval_nee_constant, then the weights in the pinned order.
    let shader=table_at(G_shaders,light_shader&SHADER_MASK,SIZE_shaders);let constant=(load_word(shader+SH_flags)&SD_HAS_CONSTANT_EMISSION)!=0u;
    let weight=factor*mis*f32(state_half(S_shadow_link_dedicated_light_weight,index));
    let value=select(vec3<f32>(1),heap_vec3(shader+SH_constant_emission),constant)*heap_vec3(light+L_strength)*weight;
    let group=load_word(table_at(G_objects,load_word(light+L_object_id),SIZE_objects)+O_lightgroup);
    store_word(at+4u,bitcast<u32>(mis));store_word(at+8u,bitcast<u32>(value.x));store_word(at+12u,bitcast<u32>(value.y));store_word(at+16u,bitcast<u32>(value.z));
    store_word(at+20u,group);store_word(at+24u,select(0u,1u,constant));store_word(at,1u);
}
//#endif
