// SPDX-FileCopyrightText: 2011-2023 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// DEVICE_KERNEL_INTEGRATOR_INTERSECT_DEDICATED_LIGHT, adapted from the pinned
// kernel/integrator/intersect_dedicated_light.h and lights_intersect_impl<false>
// (kernel/light/light.h): the ray a surface bounce just configured looks for
// emitters of the shadow-linked sets, past blockers the main path stops at; one
// hit is picked with the path's LCG and stays in the path's intersection state for
// shade_dedicated_light. The whole region is SHADOW_LINKING: other scenes compile without it.
//#if SHADOW_LINKING
// lcg_state_init(rng_pixel, rng_offset, sample, 0x68bc21eb): hash_uint3 (Jenkins).
fn dedicated_lcg_init(index:u32)->u32{
    var a=0xdeadbf08u+(state_word(S_path_rng_pixel,index)^0x68bc21ebu);
    var b=0xdeadbf08u+state_half(S_path_rng_offset,index);var c=0xdeadbf08u+state_word(S_path_sample,index);
    c^=b;c-=(b<<14u)|(b>>18u);a^=c;a-=(c<<11u)|(c>>21u);
    b^=a;b-=(a<<25u)|(a>>7u);c^=b;c-=(b<<16u)|(b>>16u);
    a^=c;a-=(c<<4u)|(c>>28u);b^=a;b-=(a<<14u)|(a>>18u);c^=b;c-=(b<<24u)|(b>>8u);return c;
}
// lcg_step_float: (float)state, correctly rounded, times 2^-32 (exact: never subnormal).
fn dedicated_lcg_step(lcg:ptr<function,u32>)->f32{
    *lcg=1103515245u*(*lcg)+12345u;let value=*lcg;if(value==0u){return 0.0;}
    let shift=countLeadingZeros(value);let bits=value<<shift;let low=bits&0xffu;var mantissa=bits>>8u;
    if(low>0x80u||(low==0x80u&&(mantissa&1u)!=0u)){mantissa+=1u;}
    return bitcast<f32>(((158u-shift)<<23u)+mantissa-0x800000u)*2.3283064365386963e-10;
}
// lights_intersect_impl: the lamp's distance along the ray, with the arithmetic of intersect_point_lights; a negative
// value is a miss. Suns: sun_light_intersect, only for a ray that reaches infinity.
fn dedicated_lamp_distance(light:u32,kind:u32,origin:vec3<f32>,direction:vec3<f32>,tmin:f32,tmax:f32)->f32{
    let position=heap_vec3(light+L_co);var t=0.0;
    if(kind==LIGHT_SUN){
        if(tmax!=bitcast<f32>(0x7f7fffffu)){return -1.0;}
        let angle=heap_float(light+L_sun_half_angle);if(angle==0.0){return -1.0;}
        let axis= -position;let a=rounded_sqrt(rounded_dot(axis+direction,axis+direction));
        let b=rounded_sqrt(rounded_dot(axis-direction,axis-direction));
        var k=0.0;if(b!=0.0){k=select(select(b/a,a/b,b>a),1.0,a==b);}
        let s=rounded_add(1.0,-rounded_add(1.0,-k));let s2=s*s;
        let numerator=s*rounded_add(0.43157974*s2,1.0);
        let denominator=rounded_add(rounded_add(0.05831938*s2,0.76443945)*s2,1.0);
        var half_angle=rounded_divide(numerator,denominator);if(b>a){half_angle=rounded_add(1.5707963267948966,-half_angle);}
        if(2.0*half_angle>angle){return -1.0;}
        return bitcast<f32>(0x7f7fffffu);
    }
    if(kind==LIGHT_AREA){
        let N=heap_vec3(light+L_area_dir);let invarea=abs(heap_float(light+L_area_invarea));let facing=rounded_dot(direction,N);if(invarea==0.0||facing>=0.0){return -1.0;}
        t=rounded_divide(-rounded_add(rounded_dot(origin,N),-rounded_dot(position,N)),facing);if(!(t>tmin&&t<tmax)){return -1.0;}
        let plane=rounded_add3(origin,t*direction)-position;
        let axis_u=heap_vec3(light+L_area_axis_u);let len_u=heap_float(light+L_area_len_u);let axis_v=heap_vec3(light+L_area_axis_v);let len_v=heap_float(light+L_area_len_v);
        let u=rounded_dot(plane,closest_native_div3(axis_u,len_u));
        if(u< -0.5||u>0.5){return -1.0;}
        let v=rounded_dot(plane,closest_native_div3(axis_v,len_v));
        if(v< -0.5||v>0.5){return -1.0;}if(heap_float(light+L_area_invarea)<0.0&&rounded_add(u*u,v*v)>0.25){return -1.0;}
        return t;
    }
    if(kind==LIGHT_SPOT&&rounded_dot(direction,origin-position)>=0.0){return -1.0;}
    let radius=heap_float(light+L_radius);if(radius==0.0){return -1.0;}
    if(load_word(light+L_is_sphere)!=0u){
        let delta=position-origin;let r2=radius*radius;let d2=rounded_dot(delta,delta);let cosine=rounded_dot(delta,direction);
        if(d2>r2&&cosine<0.0){return -1.0;}let perpendicular=rounded_add3(delta,-cosine*direction);let sine2=rounded_dot(perpendicular,perpendicular);
        if(sine2>r2){return -1.0;}let root=rounded_sqrt(rounded_add(r2,-sine2));
        t=rounded_add(cosine,-bitcast<f32>((bitcast<u32>(root)&0x7fffffffu)|(bitcast<u32>(rounded_add(d2,-r2))&0x80000000u)));
        if(!(t>tmin&&t<tmax)){return -1.0;}
    }else{
        let vp=origin-position;let normal=closest_native_normalize(vp);let dp=rounded_dot(vp,normal);let cosine=rounded_dot(normal,-direction);
        if(!(dp*cosine>0.0)){return -1.0;}t=rounded_divide(dp,cosine);if(t<0.0){return -1.0;}
        let point=rounded_add3(origin,t*direction);let delta=point-position;
        if(!(rounded_dot(delta,delta)<radius*radius&&t>tmin&&t<tmax)){return -1.0;}
    }
    return t;
}
@compute @workgroup_size(64) fn intersect_dedicated_light(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
    if(state_half(S_path_queued_kernel,index)!=DL_K_INTERSECT){fail();return;}
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));
    let ray_tmin=heap_float(state_address(S_ray_tmin,index,4u));var far=heap_float(state_address(S_ray_tmax,index,4u));let time=heap_float(state_address(S_ray_time,index,4u));
    let self_prim=state_word(S_isect_prim,index);let self_object=state_word(S_isect_object,index);let last_type=state_word(S_isect_type,index);
    let path_visibility=state_byte(S_path_visibility,index);let flag=state_word(S_path_flag,index);
    // path_state_ray_visibility, as intersect_closest.
    var visibility=path_visibility;
    if((visibility&PATH_RAY_VISIBILITY_TRANSMIT)!=0u){visibility&=~(PATH_RAY_VISIBILITY_DIFFUSE|PATH_RAY_VISIBILITY_GLOSSY);}
    if((flag&PATH_RAY_SHADOW_CATCHER_PASS)!=0u){visibility<<=16u;}
    let receiver=light_link_receiver_forward(index);var lcg=dedicated_lcg_init(index);
    var pick=NativeHit(0.0,0.0,0.0,NONE,NONE,PRIM_TYPE_NONE);var hits=0u;
    var transparent=state_half(S_path_transparent_bounce,index);var bounds=state_half(S_path_volume_bounds_bounce,index);
    // shadow_linking_pick_mesh_intersection. The early validity test of scene_intersect, as intersect_closest.
    var tmin=ray_tmin;
    if((bitcast<u32>(origin.x)&0x7f800000u)!=0x7f800000u&&(bitcast<u32>(direction.x)&0x7f800000u)!=0x7f800000u&&dot(direction,direction)!=0.0){
    for(var i=0u;i<DL_MAX_INTERSECTIONS;i++){
        if(!(tmin<bitcast<f32>(0x7f7fffffu))){break;}
        let hit=trace_native(origin,direction,tmin,far,time,visibility,self_object,self_prim);
        if(load_word(p(17u))!=0u){return;}
        if(hit.prim==NONE){break;}
//#if !CURVES
//#if POINTS else         let shader=load_word(table_at(G_tri_shader,hit.prim,SIZE_tri_shader))&SHADER_MASK;
        let shader=point_or_triangle_shader(hit.prim,hit.kind)&SHADER_MASK;
//#endif
//#endif
//#if CURVES
        let shader=closest_hit_shader(hit)&SHADER_MASK;
//#endif
        let flags=load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags);
        if((flags&SD_HAS_EMISSION)!=0u&&light_link_match(receiver,hit.object)&&!shadow_link_membership_all(hit.object)){
            hits++;
            if(pick.prim==NONE||dedicated_lcg_step(&lcg)<rounded_divide(1.0,f32(hits))){pick=hit;}
        }
        // Lights past the default opaque blocker are the main path's; past the transparency limits, nobody's.
        if((flags&(DL_SD_HAS_ONLY_VOLUME|SD_HAS_TRANSPARENT_SHADOW))==0u){
            if(load_word(table_at(G_objects,hit.object,SIZE_objects)+DL_O_blocker_shadow_set)==0u){far=hit.t;break;}
        }else if((flags&DL_SD_HAS_ONLY_VOLUME)!=0u){
            bounds++;if(bounds>=DL_VOLUME_BOUNDS_MAX){far=hit.t;break;}
        }else{
            transparent++;if(transparent>=data_word(DL_D_transparent_max_bounce)){far=hit.t;break;}
        }
        // intersection_t_offset
        tmin=select(bitcast<f32>(bitcast<u32>(hit.t)+1u),1.1754943508222875e-38,hit.t==0.0);
    }
    }
    // lights_intersect_shadow_linked, from the ray's own tmin.
    for(var lamp=0u;lamp<data_word(D_integrator_num_lights);lamp++){
        let light=table_at(G_lights,lamp,SIZE_lights);let kind=load_word(light+L_type);let shader=load_word(light+L_shader_id);let object=load_word(light+L_object_id);
        if((path_visibility&VIS_CAMERA)!=0u){if((shader&LIGHT_EXCLUDE_CAMERA)!=0u){continue;}}
        else if((shader&SHADER_USE_MIS)==0u){continue;}
        if((flag&PATH_RAY_SHADOW_CATCHER_PASS)!=0u&&(shader&LIGHT_EXCLUDE_SHADOW_CATCHER)!=0u){continue;}
        // This ray serves only the shadow-linked lights.
        if(shadow_link_membership_all(object)){continue;}
        if((path_visibility&VIS_CAMERA)==0u&&!light_link_match(receiver,object)){continue;}
        if(kind!=LIGHT_POINT&&kind!=LIGHT_SPOT&&kind!=LIGHT_AREA&&kind!=LIGHT_SUN){continue;}
        let t=dedicated_lamp_distance(light,kind,origin,direction,ray_tmin,far);if(t<0.0){continue;}
        if(self_prim==lamp&&self_object==object&&last_type==PRIMITIVE_LAMP){continue;}
        hits++;
        if(pick.prim!=NONE&&dedicated_lcg_step(&lcg)>rounded_divide(1.0,f32(hits))){continue;}
        pick=NativeHit(t,0.0,0.0,object,lamp,PRIMITIVE_LAMP);
    }
    if(load_word(p(17u))!=0u){return;}
    var next=K_CLOSEST;
    if(hits!=0u){
        // shadow_linking_store_last_primitives, integrator_state_write_isect: the intersection
        // state now names the emitter; shade_dedicated_light restores the two self fields.
        state_f32(S_shadow_link_dedicated_light_weight,index,f32(hits));
        state_u32(S_shadow_link_last_isect_prim,index,self_prim);state_u32(S_shadow_link_last_isect_object,index,self_object);
        state_f32(S_isect_t,index,pick.t);state_f32(S_isect_u,index,pick.u);state_f32(S_isect_v,index,pick.v);
        state_u32(S_isect_object,index,pick.object);state_u32(S_isect_prim,index,pick.prim);state_u32(S_isect_type,index,pick.kind);
        next=DL_K_SHADE;
    }
    let counter=load_word(p(7u)+S_queue_counter)/4u;
    atomicSub(&heap[counter+DL_K_INTERSECT],1u);atomicAdd(&heap[counter+next],1u);state_u16(S_path_queued_kernel,index,next);
}
//#endif
