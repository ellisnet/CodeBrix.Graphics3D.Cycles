// SPDX-FileCopyrightText: 2011-2025 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Volume direct light and shadow paths through volumes, adapted from the pinned
// kernel/integrator/shade_volume.h (volume_direct_sample_method, integrate_volume_sample_direct_light,
// integrate_volume_direct_light, integrate_volume_phase_scatter), volume_stack.h
// (volume_stack_sample_method), state_util.h (integrator_state_copy_volume_stack_to_shadow) and
// closure/volume_util.h (phase_henyey_greenstein). Part of the surface module (light sampling and shadow
// paths live there); shade_volume (wavefront_volume.wgsl) integrates the segment between the two entries.
// Record per input (push word 24, VL_RECORD_BYTES): 0 the direct sample method (0 none, 1 distance,
// 2 equiangular, 3 MIS), 4 the equiangular point, 16 and 20 the valid ray segment, 32 the direct light
// pending, 36 its scatter position, 48 its throughput, 60 the closure count, 64 the indirect scatter pending,
// 68 its distance, 72 the shading object.
//#if VOLUME_SHADOW
fn vl_shadow_stack_address(shadow:u32,slot:u32,member:u32)->u32{return load_word(p(7u)+VL_S_shadow_volume_stack+slot*VL_SVS_stride+member)+shadow*4u;}
// integrator_state_copy_volume_stack_to_shadow: every entry up to and including the terminator.
fn vl_copy_volume_stack(index:u32,shadow:u32){
    // Regions stay after a volume scene on the same device; a scene without volumes has no stack allocated.
    if(!volume_enabled()){return;}
    let limit=data_word(D_volume_stack_size);
    for(var slot=0u;slot<limit;slot++){
        let shader=load_word(volume_stack_address(index,slot,VS_shader));
        store_word(vl_shadow_stack_address(shadow,slot,VL_SVS_object),load_word(volume_stack_address(index,slot,VS_object)));
        store_word(vl_shadow_stack_address(shadow,slot,VL_SVS_shader),shader);
        if(shader==NONE){return;}
    }
}
//#endif
//#if VOLUME_LIGHT
// volume_stack_sample_method over the entries shade_volume integrates.
fn vl_sample_method(index:u32,entries:u32)->u32{
    var method=0u;
    for(var slot=0u;slot<entries;slot++){
        let shader=load_word(volume_stack_address(index,slot,VS_shader));if(shader==NONE){break;}
        let flags=load_word(table_at(G_shaders,shader&SHADER_MASK,SIZE_shaders)+SH_flags);
        if((flags&VL_SD_VOLUME_MIS)!=0u){return 3u;}
        else if((flags&VL_SD_VOLUME_EQUIANGULAR)!=0u){if(method==1u){return 3u;}method=2u;}
        else{if(method==2u){return 3u;}method=1u;}
    }
    return method;
}
//#if VOLUME_LIGHT_TREE
// DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME with the light tree, before shade_volume_segment (compile envelope): the
// emitter of light_tree_sample<true> for the segment, in the record's light selection words.
@compute @workgroup_size(1) fn shade_volume_tree_select(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    let vl=p(24u)+id.x*VL_RECORD_BYTES;store_word(vl+VL_TREE_SELECTION,0u);
//#if VOLUME_RAY_MARCHING else     if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=VL_K_SHADE_VOLUME){return;}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=vr_volume_kernel()){return;}
//#endif
    if(data_word(D_integrator_use_direct_light)==0u||(state_word(S_path_flag,index)&PATH_RAY_TERMINATE)!=0u){return;}
    let prim=state_word(S_isect_prim,index);let count=volume_stack_count(index);if(count==NONE){return;}
    let entries=select(count,select(0u,1u,data_word(D_background_volume_shader)!=NONE),prim==NONE);if(entries==0u){return;}
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));
    let tmin=heap_float(state_address(S_ray_tmin,index,4u));var tmax=bitcast<f32>(0x7f7fffffu);if(prim!=NONE){tmax=heap_float(state_address(S_isect_t,index,4u));}
//#if SAMPLING_SOBOL_BURLEY else     let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_LIGHT).xyz;
    let random=native_rng_pattern(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_LIGHT,3u).xyz;
//#endif
    lt_segment=true;lt_t=tmax-tmin;
//#if LIGHT_LINKING else     let walk=light_tree_walk(true,rounded_add3(origin,direction*tmin),direction,true,NONE,random.z,NONE,0u);
    // light_link_receiver_nee of the segment's ShaderData (shader_setup_from_volume: the first stack entry's object).
    let walk=light_tree_walk(true,rounded_add3(origin,direction*tmin),direction,true,select(NONE,load_word(volume_stack_address(index,0u,VS_object)),light_link_enabled()),random.z,NONE,0u);
//#endif
    lt_segment=false;if(load_word(p(17u))!=0u||walk.emitter<0){return;}
    let e=lt_emitter(u32(walk.emitter));let emitter_prim=bitcast<i32>(load_word(e+LTE_id));
    light_selection_write(vl+VL_TREE_SELECTION-LIGHT_SELECT_RECORD,LightSelection(random,u32(walk.emitter),emitter_prim,select(load_word(e+LTE_object),walk.object,emitter_prim>=0),load_word(e+LTE_visibility),walk.pdf,true,true),0.0);
}
//#endif
// DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME, part 1: light_sample_from_volume_segment for the direct light and
// the method of volume_direct_sample_method, from the segment's start sd->P = ray->P + ray->D * ray->tmin.
@compute @workgroup_size(1) fn shade_volume_segment(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    let vl=p(24u)+id.x*VL_RECORD_BYTES;store_word(vl,0u);store_word(vl+32u,0u);store_word(vl+64u,0u);
//#if VOLUME_HETEROGENEOUS
    store_word(vl+76u,0u);
//#endif
//#if VOLUME_RAY_MARCHING else     if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=VL_K_SHADE_VOLUME){return;}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=vr_volume_kernel()){return;}
//#endif
    if(data_word(D_integrator_use_direct_light)==0u||(state_word(S_path_flag,index)&PATH_RAY_TERMINATE)!=0u){return;}
    let prim=state_word(S_isect_prim,index);let count=volume_stack_count(index);if(count==NONE){return;}
    let entries=select(count,select(0u,1u,data_word(D_background_volume_shader)!=NONE),prim==NONE);if(entries==0u){return;}
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));
    let tmin=heap_float(state_address(S_ray_tmin,index,4u));var tmax=bitcast<f32>(0x7f7fffffu);if(prim!=NONE){tmax=heap_float(state_address(S_isect_t,index,4u));}
//#if VOLUME_LIGHT_TREE
    // The selection of shade_volume_tree_select (light_tree_sample<true>): none means no direct light.
    light_select_record=vl+VL_TREE_SELECTION-LIGHT_SELECT_RECORD;
//#endif
//#if VOLUME_LIGHT_SHAPES else     let selection=light_select(index);if(selection.prim>=0){atomicOr(&heap[p(17u)/4u],2u);return;}
    let selection=light_select(index);
//#if VOLUME_LIGHT_TREE
    if(!selection.valid){return;}
//#endif
//#if VOLUME_SHADOW_CATCHER
    // light_sample: a catcher path skips mesh lights of other objects and lamps that exclude it.
    if(selection.prim>=0&&sc_triangle_excluded(index,selection)){return;}
    if(selection.prim<0&&(state_word(S_path_flag,index)&SC_PATH_PASS)!=0u&&(load_word(table_at(G_lights,~bitcast<u32>(selection.prim),SIZE_lights)+L_shader_id)&SC_EXCLUDE)!=0u){return;}
//#endif
    if(selection.prim>=0){
//#if LIGHT_TRIANGLE else         atomicOr(&heap[p(17u)/4u],2u);return;
        // triangle_light_sample<true>: a uniform point of the triangle (area sampling), the triangle's shader with the
        // object's visibility, then triangle_light_valid_ray_segment (the emitting side's half-space).
//#if LIGHT_SELECT else         let object=load_word(selection.distribution+LD_object);let tri=u32(selection.prim);
        let object=selection.object;let tri=u32(selection.prim);
//#endif
//#if LIGHT_LINKING
        // light_sample<true>: light_link_object_match(light_link_receiver_nee, the emitter's object).
        if(!light_link_match(load_word(volume_stack_address(index,0u,VS_object)),object)){return;}
//#endif
        let v=triangle_light_vertices_compute(object,tri,heap_float(state_address(S_ray_time,index,4u)));if(v.error!=0u){return;}
        var n=rounded_cross(v.b-v.a,v.c-v.a);if((load_word(table_at(G_object_flag,object,SIZE_object_flag))&SD_OBJECT_NEGATIVE_SCALE)!=0u){n=-n;}
//#if LIGHT_SELECT else         let tri_shader=load_word(table_at(G_tri_shader,tri,SIZE_tri_shader));let shader=tri_shader|SHADER_USE_MIS|load_word(selection.distribution+LD_visibility);
        let tri_shader=load_word(table_at(G_tri_shader,tri,SIZE_tri_shader));let shader=tri_shader|SHADER_USE_MIS|selection.visibility;
//#endif
        let normal=light_normalize_len(n);let area=0.5*normal.w;if(area==0.0){return;}
        var u=selection.random.x;var w=selection.random.y;if(w>u){u*=0.5;w-=u;}else{w*=0.5;u-=w;}
        let t=rounded_add(rounded_add(1.0,-u),-w);let tpoint=rounded_add3(rounded_add3(t*v.a,u*v.b),w*v.c);
        let start=rounded_add3(origin,direction*tmin);let delta=light_normalize_len(tpoint-start);
        if(rounded_dot(normal.xyz,-delta.xyz)==0.0){return;}
        if((shader&VL_SHADER_EXCLUDE_SCATTER)!=0u){return;}
        let flags=load_word(table_at(G_shaders,tri_shader&SHADER_MASK,SIZE_shaders)+SH_flags);var range=vec2<f32>(tmin,tmax);
        if((flags&(SD_MIS_FRONT|SD_MIS_BACK))!=(SD_MIS_FRONT|SD_MIS_BACK)){range=vl_plane_segment(select(normal.xyz,-normal.xyz,(flags&SD_MIS_BACK)!=0u),rounded_add3(origin,-tpoint),direction,range);}
        if(!(range.x<range.y)){return;}
        store_word(vl+4u,bitcast<u32>(tpoint.x));store_word(vl+8u,bitcast<u32>(tpoint.y));store_word(vl+12u,bitcast<u32>(tpoint.z));
        store_word(vl+16u,bitcast<u32>(range.x));store_word(vl+20u,bitcast<u32>(range.y));store_word(vl,vl_sample_method(index,entries));return;
//#endif
    }
//#endif
    let light=table_at(G_lights,~bitcast<u32>(selection.prim),SIZE_lights);
//#if LIGHT_LINKING
    if(!light_link_match(load_word(volume_stack_address(index,0u,VS_object)),load_word(light+L_object_id))){return;}
//#endif
    if(f32(state_half(S_path_bounce,index))>heap_float(light+L_max_bounces)){return;}
    let kind=load_word(light+L_type);var point=vec3<f32>(0);var distant=true;
//#if VOLUME_LIGHT_SHAPES else     if(kind==LIGHT_POINT){
//#endif
//#if VOLUME_LIGHT_SHAPES else         // light_sample<true>: the point light sampled from the segment start with N = ray->D and transmission.
//#endif
//#if VOLUME_LIGHT_SHAPES else         var sd:SurfaceData;sd.P=rounded_add3(origin,direction*tmin);sd.N=direction;var r:SvmResult;r.runtime_flag=VL_SR_BSDF_HAS_TRANSMISSION;
//#endif
//#if VOLUME_LIGHT_SHAPES else         let ls=light_sample_lamp(index,sd,r,selection);point=ls.P;distant=false;
//#endif
//#if VOLUME_LIGHT_SHAPES else     }else if(kind!=LIGHT_SUN&&kind!=LIGHT_BACKGROUND){atomicOr(&heap[p(17u)/4u],2u);return;}
//#endif
//#if VOLUME_LIGHT_SHAPES
    if(kind==LIGHT_POINT||kind==LIGHT_SPOT){
        // light_sample<true> of a point or spot light from the segment start (point.h, spot.h).
        point=vl_lamp_point(light,rounded_add3(origin,direction*tmin),selection.random.xy);distant=false;
    }else if(kind==LIGHT_AREA){
        // area_light_sample<true>: a uniform point of the rectangle or the ellipse.
        point=vl_area_point(light,selection.random.xy);distant=false;
    }else if(kind!=LIGHT_SUN&&kind!=LIGHT_BACKGROUND){atomicOr(&heap[p(17u)/4u],2u);return;}
//#endif
    if((load_word(light+L_shader_id)&VL_SHADER_EXCLUDE_SCATTER)!=0u){return;}
//#if VOLUME_LIGHT_SHAPES else     // volume_valid_direct_ray_segment for point and distant lights: the whole ray range.
//#endif
//#if VOLUME_LIGHT_SHAPES else     if(!(tmin<tmax)){return;}
//#endif
//#if VOLUME_LIGHT_SHAPES else     store_word(vl+4u,bitcast<u32>(point.x));store_word(vl+8u,bitcast<u32>(point.y));store_word(vl+12u,bitcast<u32>(point.z));
//#endif
//#if VOLUME_LIGHT_SHAPES else     store_word(vl+16u,bitcast<u32>(tmin));store_word(vl+20u,bitcast<u32>(tmax));
//#endif
//#if VOLUME_LIGHT_SHAPES
    // volume_valid_direct_ray_segment: the spot light's cone, the area light's slab or cylinder (or spread cone) and
    // its positive side; point and distant lights keep the whole ray range.
    var range=vec2<f32>(tmin,tmax);
    if(kind==LIGHT_SPOT){range=vl_spot_segment(light,origin,direction,range);}
    else if(kind==LIGHT_AREA){range=vl_area_segment(light,rounded_add3(origin,-heap_vec3(light+L_co)),direction,range);}
    if(!(range.x<range.y)){return;}
    store_word(vl+4u,bitcast<u32>(point.x));store_word(vl+8u,bitcast<u32>(point.y));store_word(vl+12u,bitcast<u32>(point.z));
    store_word(vl+16u,bitcast<u32>(range.x));store_word(vl+20u,bitcast<u32>(range.y));
//#endif
    store_word(vl,select(vl_sample_method(index,entries),1u,distant));
}
//#endif
//#if PHASE_HG
// phase_henyey_greenstein (volume_henyey_greenstein_eval's pdf).
fn vl_hg_pdf(cosine:f32,g:f32)->f32{
    if(abs(g)<0.001){return 0.07957747154594767;}
    let fac=rounded_add(1.0,g*rounded_add(g,-2.0*cosine));
    return rounded_divide(rounded_add(1.0,-g*g),(12.566370614359172*fac)*rounded_sqrt(max(fac,0.0)));
}
//#endif
//#if VOLUME_LIGHT
// volume_shader_phase_eval (_volume_shader_phase_eval_mis): eval accumulates eval * sample_weight, then
// times 1 / sum_sample_weight; the pdf is the sample-weighted average.
struct VlPhase {eval:vec3<f32>,pdf:f32}
fn vl_phase_eval(r:SvmResult,D:vec3<f32>,wo:vec3<f32>)->VlPhase{
    var out:VlPhase;var sum_pdf=0.0;var sum_weight=0.0;
    for(var i=0u;i<r.count;i++){
//#if VOLUME_PHASES else         let c=svm_closure_read(r,i);let pdf=vl_hg_pdf(rounded_dot(D,wo),c.roughness);
        let c=svm_closure_read(r,i);let pdf=vp_eval(c,rounded_dot(D,wo));
//#endif
        if(pdf!=0.0){out.eval=rounded_add3(out.eval,vec3<f32>(pdf)*c.sample_weight);sum_pdf=rounded_add(sum_pdf,pdf*c.sample_weight);}
        sum_weight=rounded_add(sum_weight,c.sample_weight);
    }
    out.eval*=rounded_divide(1.0,sum_weight);
    out.pdf=select(0.0,rounded_divide(sum_pdf,sum_weight),sum_weight>0.0);
    return out;
}
//#if LIGHT_LINKING
// light_link_receiver_nee at the scatter position: the ShaderData the stack's evaluation left, the last entry's object.
fn vl_link_receiver(index:u32)->u32{
    var object=NONE;for(var slot=0u;slot<data_word(D_volume_stack_size);slot++){if(load_word(volume_stack_address(index,slot,VS_shader))==NONE){break;}object=load_word(volume_stack_address(index,slot,VS_object));}
    return object;
}
//#endif
// integrate_volume_direct_light: the light sampled again from the scatter position (N = 0, transmission), the
// phase function with MIS, the light termination and the shadow path with the path's volume stack.
fn vl_direct_light(index:u32,slot:u32,r:SvmResult,P:vec3<f32>,throughput:vec3<f32>,D:vec3<f32>){
    var sd:SurfaceData;sd.P=P;sd.N=vec3<f32>(0);sd.time=heap_float(state_address(S_ray_time,index,4u));
    var rr:SvmResult;rr.runtime_flag=VL_SR_BSDF_HAS_TRANSMISSION;
//#if VOLUME_LIGHT_SHAPES else     let selection=light_select(index);if(selection.prim>=0){atomicOr(&heap[p(17u)/4u],2u);return;}
//#endif
//#if VOLUME_LIGHT_SHAPES else     let ls=light_sample_lamp(index,sd,rr,selection);
//#endif
//#if VOLUME_LIGHT_SHAPES
    // light_sample<false> from the scatter position, a lamp (an emissive triangle: shade_volume_light_triangle).
    let selection=light_select(index);if(selection.prim>=0){atomicOr(&heap[p(17u)/4u],2u);return;}
//#if LIGHT_LINKING
    if(!light_link_match(vl_link_receiver(index),load_word(table_at(G_lights,~bitcast<u32>(selection.prim),SIZE_lights)+L_object_id))){return;}
//#endif
    let ls=light_sample_lamp(index,sd,rr,selection);
//#endif
//#if !BACKGROUND_PORTAL
    vl_direct_finish(index,slot,r,P,throughput,D,sd,ls);
//#endif
//#if BACKGROUND_PORTAL
//#if BACKGROUND_SAMPLE_ENTRY else     vl_direct_finish(index,slot,r,P,throughput,D,sd,ls);
    // A background lamp (light_sample_lamp left it pending): its sample from shade_volume_light_background.
    var sample=ls;if(light_background_pending){light_background_pending=false;if(load_word(vl_background_record)!=1u){fail();return;}sample=light_sample_read(vl_background_record+4u);}
    vl_direct_finish(index,slot,r,P,throughput,D,sd,sample);
//#endif
//#endif
}
//#if BACKGROUND_PORTAL
// Background light portals: the background light's sample of the volume direct light in its own entry,
// shade_volume_light_background, dispatched before shade_volume_light (compile envelope, as
// shade_surface_light_background for the surfaces): record word 128 marks it, words 132 to 183 hold it.
var<private> vl_background_record:u32=0xffffffffu;
// light_sample_lamp for a background lamp at the scatter position (light_sample<false>, N = 0): its preamble,
// background_light_sample with the portal strategy, then the selection's pdf factor, the same operations in the same order.
fn vl_background_light_sample(index:u32,P:vec3<f32>,selection:LightSelection)->NativeLightSample{
    var ls:NativeLightSample;
//#if LIGHT_SELECT
    if(!selection.valid){return ls;}
//#endif
    ls.is_light=true;ls.prim=~bitcast<u32>(selection.prim);
    let light=table_at(G_lights,ls.prim,SIZE_lights);
    if(f32(state_half(S_path_bounce,index))>heap_float(light+L_max_bounces)){return ls;}
//#if SHADOW_CATCHER
    if((state_word(S_path_flag,index)&SC_PATH_PASS)!=0u&&(load_word(light+L_shader_id)&SC_EXCLUDE)!=0u){return ls;}
//#endif
    ls.shader=load_word(light+L_shader_id);ls.object_id=load_word(light+L_object_id);
    let sampled=bgl_portal_light_sample(P,selection.random.xy);
    ls.D=sampled.xyz;ls.P=-sampled.xyz;ls.pdf=sampled.w;ls.t=bitcast<f32>(0x7f7fffffu);ls.eval_fac=1.0;
//#if LIGHT_SELECT else     ls.pdf*=data_float(D_integrator_distribution_pdf_lights);
    ls.pdf*=select(data_float(D_integrator_distribution_pdf_lights),selection.pdf,selection.tree);
//#endif
    return ls;
}
@compute @workgroup_size(1) fn shade_volume_light_background(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    let vl=p(24u)+id.x*VL_RECORD_BYTES;store_word(vl+128u,0u);if(load_word(vl+32u)==0u){return;}
//#if VOLUME_LIGHT_TREE
    light_select_record=vl+VL_TREE_SELECTION-LIGHT_SELECT_RECORD;
//#endif
    if(index>=p(10u)||load_word(p(17u))!=0u){return;}
    let selection=light_select(index);
//#if LIGHT_SELECT
    if(!selection.valid){return;}
//#endif
    if(selection.prim>=0){return;}
    if(load_word(table_at(G_lights,~bitcast<u32>(selection.prim),SIZE_lights)+L_type)!=LIGHT_BACKGROUND){return;}
    light_sample_write(vl+132u,vl_background_light_sample(index,heap_vec3(vl+36u),selection));store_word(vl+128u,1u);
}
//#endif
//#if VOLUME_LIGHT_SHAPES
//#if LIGHT_TRIANGLE
// DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME with emissive triangles, before shade_volume_light (compile envelope): the
// direct light of the paths whose light is a triangle, light_sample<false> from the scatter position with the vertices
// prefetched. It takes the record's direct flag, so shade_volume_light sees only lamps.
@compute @workgroup_size(1) fn shade_volume_light_triangle(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    let vl=p(24u)+id.x*VL_RECORD_BYTES;if(load_word(vl+32u)==0u){return;}
//#if VOLUME_LIGHT_TREE
    light_select_record=vl+VL_TREE_SELECTION-LIGHT_SELECT_RECORD;
//#endif
    if(index>=p(10u)||load_word(p(17u))!=0u){return;}
    let selection=light_select(index);if(selection.prim<0){return;}
    store_word(vl+32u,0u);
//#if LIGHT_LINKING
    if(!light_link_match(vl_link_receiver(index),selection.object)){return;}
//#endif
//#if VOLUME_SHADOW_CATCHER
    if(sc_triangle_excluded(index,selection)){return;}
//#endif
    var r:SvmResult;r.closure_base=p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES;r.count=load_word(vl+60u);
//#if VOLUME_HETEROGENEOUS
    if(load_word(vl+76u)!=0u){r.closure_base=load_word(vl+76u);}
//#endif
    let P=heap_vec3(vl+36u);var sd:SurfaceData;sd.P=P;sd.N=vec3<f32>(0);sd.time=heap_float(state_address(S_ray_time,index,4u));
    triangle_light_prefill(index,sd,false,true);let ls=light_sample_triangle(sd,selection);
    vl_direct_finish(index,id.x,r,P,heap_vec3(vl+48u),heap_vec3(state_address(S_ray_D,index,12u)),sd,ls);
}
//#endif
//#endif
// The rest of integrate_volume_direct_light from the light sample.
fn vl_direct_finish(index:u32,slot:u32,r:SvmResult,P:vec3<f32>,throughput:vec3<f32>,D:vec3<f32>,sd:SurfaceData,ls:NativeLightSample){
    if(!(ls.pdf>0.0)||load_word(p(17u))!=0u){return;}
    if((ls.shader&VL_SHADER_EXCLUDE_SCATTER)!=0u){return;}
    let shader=table_at(G_shaders,ls.shader&SHADER_MASK,SIZE_shaders);
//#if LIGHT_NEE else     let constant=true;var emission=heap_vec3(shader+SH_constant_emission);
    let constant=(load_word(shader+SH_flags)&SD_HAS_CONSTANT_EMISSION)!=0u;
    var emission=select(vec3<f32>(1),heap_vec3(shader+SH_constant_emission),constant);
//#endif
    if(ls.is_light){emission*=heap_vec3(table_at(G_lights,ls.prim,SIZE_lights)+L_strength);}
    var phase=vl_phase_eval(r,D,ls.D);if((ls.shader&SHADER_USE_MIS)==0u){phase.pdf=0.0;}
    let mis=(ls.pdf*ls.pdf)/(ls.pdf*ls.pdf+phase.pdf*phase.pdf);
    var eval=phase.eval*(emission*ls.eval_fac/ls.pdf*mis);
    if(all(eval==vec3<f32>(0))){return;}
    // Path termination for constant light shaders (non-constant: shade_light_nee), never with the light tree.
//#if LIGHT_TREE else     if(constant){
    if(constant&&(data_word(D_kernel_features)&LT_FEATURE_LIGHT_TREE)==0u){
//#endif
        let rr_threshold=data_float(D_integrator_light_inv_rr_threshold);
        let probability=select(1.0,max(abs(eval.x),max(abs(eval.y),abs(eval.z)))*rr_threshold,rr_threshold>0.0);
//#if VOLUME_SAMPLING_PATTERN else         if(probability<1.0){let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_LIGHT_TERMINATE).x;
        if(probability<1.0){let random=native_rng_pattern(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_LIGHT_TERMINATE,1u).x;
//#endif
            if(random>=probability){return;}eval*=1.0/probability;}
    }
    // light_sample_to_volume_shadow_ray (shadow_ray_setup without self skip).
    var direction=ls.D;var tmax=ls.t;if(ls.t!=bitcast<f32>(0x7f7fffffu)){let ray=light_normalize_len(ls.P-P);direction=ray.xyz;tmax=ray.w;}
    var shadow:u32;
    if(p(28u)!=0u){shadow=load_word(p(28u))+slot;if(shadow>=p(25u)){fail();return;}}else{fail();return;}
    state_vec3(S_shadow_ray_P,shadow,P);state_vec3(S_shadow_ray_D,shadow,direction);
    state_f32(S_shadow_ray_tmin,shadow,select(bitcast<f32>(0x7f7fffffu),0.0,(ls.shader&SHADER_CAST_SHADOW)!=0u));state_f32(S_shadow_ray_tmax,shadow,tmax);
    state_f32(S_shadow_ray_time,shadow,sd.time);state_f32(S_shadow_ray_dP,shadow,0.0);state_f32(S_shadow_ray_dD,shadow,0.0);
    state_u32(S_shadow_self_object,shadow,NONE);state_u32(S_shadow_self_prim,shadow,NONE);
    state_u32(S_shadow_ray_self_light_object,shadow,ls.object_id);state_u32(S_shadow_ray_self_light_prim,shadow,ls.prim);
    let bounce=state_half(S_path_bounce,index);var visibility=state_byte(S_path_visibility,index);var flag=state_word(S_path_flag,index);
//#if VOLUME_SHADOW_CATCHER
    // integrate_volume_direct_light runs before integrator_split_shadow_catcher: a split of this dispatch (its mark is
    // pending, the path already carries PATH_RAY_SHADOW_CATCHER_HIT) must not reach the shadow path.
    if(p(29u)!=0u&&load_word(p(29u)+index*8u)!=0u){flag&=~SC_PATH_HIT;}
//#endif
//#if LIGHT_TREE else     state_f32(S_shadow_path_bsdf_eval_average,shadow,(eval.x+eval.y+eval.z)*(1.0/3.0));
    if((data_word(D_kernel_features)&LT_FEATURE_LIGHT_TREE)==0u){state_f32(S_shadow_path_bsdf_eval_average,shadow,(eval.x+eval.y+eval.z)*(1.0/3.0));}
//#endif
    if(p(20u)!=0u){var diffuse=vec3<f32>(1);var glossy=vec3<f32>(0);
        if((flag&VL_PATH_RAY_ANY_PASS)!=0u){diffuse=heap_vec3(state_address(S_path_pass_diffuse_weight,index,12u));glossy=heap_vec3(state_address(S_path_pass_glossy_weight,index,12u));}
        else{flag|=VL_PATH_RAY_VOLUME_PASS;}
        state_vec3(S_shadow_path_pass_diffuse_weight,shadow,diffuse);state_vec3(S_shadow_path_pass_glossy_weight,shadow,glossy);
    }
    if(bounce==0u){visibility|=VL_PATH_RAY_VISIBILITY_VOLUME_SCATTER;flag&=~VL_PATH_RAY_VOLUME_PRIMARY_TRANSMIT;}
    state_u32(S_shadow_path_render_pixel_index,shadow,state_word(S_path_render_pixel_index,index));
    state_u16(S_shadow_path_rng_offset,shadow,state_half(S_path_rng_offset,index));state_u32(S_shadow_path_rng_pixel,shadow,state_word(S_path_rng_pixel,index));
    state_u32(S_shadow_path_sample,shadow,state_word(S_path_sample,index));
    state_u8(S_shadow_path_visibility,shadow,visibility);state_u32(S_shadow_path_flag,shadow,flag);
    state_u16(S_shadow_path_bounce,shadow,bounce);state_u16(S_shadow_path_transparent_bounce,shadow,state_half(S_path_transparent_bounce,index));
    state_u16(S_shadow_path_diffuse_bounce,shadow,state_half(S_path_diffuse_bounce,index));state_u16(S_shadow_path_glossy_bounce,shadow,state_half(S_path_glossy_bounce,index));
    state_u16(S_shadow_path_transmission_bounce,shadow,state_half(S_path_transmission_bounce,index));
    state_u16(S_shadow_path_volume_bounds_bounce,shadow,state_half(S_path_volume_bounds_bounce,index));
    state_vec3(S_shadow_path_throughput,shadow,throughput*eval);
    let group=load_word(table_at(G_objects,ls.object_id,SIZE_objects)+O_lightgroup);state_u8(S_shadow_path_lightgroup,shadow,group+1u);
    vl_copy_volume_stack(index,shadow);
//#if LIGHT_NEE else     state_u16(S_shadow_path_queued_kernel,shadow,K_SHADOW_INTERSECT);let counter=load_word(p(7u)+S_queue_counter)/4u;atomicAdd(&heap[counter+K_SHADOW_INTERSECT],1u);
    let next=select(K_LIGHT_NEE,K_SHADOW_INTERSECT,constant);
    state_u16(S_shadow_path_queued_kernel,shadow,next);let counter=load_word(p(7u)+S_queue_counter)/4u;atomicAdd(&heap[counter+next],1u);
//#endif
}
//#endif
//#if PHASE_HG
// Henyey-Greenstein sampling as shade_volume's volume_phase (phase_henyey_greenstein_sample).
fn vl_phase_sample(direction:vec3<f32>,g:f32,random:vec2<f32>)->vec4<f32>{
    var cosine=rounded_add(1.0,-2.0*random.x);let g2=g*g;
    if(abs(g)>=0.001){let k=rounded_divide(rounded_add(1.0,-g2),rounded_add(1.0,-g*cosine));cosine=rounded_divide(rounded_add(rounded_add(1.0,g2),-k*k),2.0*g);}
    var pdf=0.07957747154594767;
    if(abs(g)>=0.001){let fac=rounded_add(1.0,g*rounded_add(g,-2.0*cosine));pdf=rounded_divide(rounded_add(1.0,-g2),(12.566370614359172*fac)*rounded_sqrt(max(fac,0.0)));}
    let sine=rounded_sqrt(max(rounded_add(1.0,-cosine*cosine),0.0));let phi=6.283185307179586*random.y;
    let sc=native_phase_sincos(phi);let local=vec3<f32>(sine*sc.y,sine*sc.x,cosine);
    var tangent=vec3<f32>(direction.z-direction.y,direction.x-direction.z,direction.y-direction.x);
    if(direction.x==direction.y&&direction.x==direction.z){tangent=vec3<f32>(direction.z-direction.y,direction.x+direction.z,-direction.y-direction.x);}
    tangent=surface_native_normalize(tangent);let bitangent=rounded_cross(direction,tangent);
    let wo=rounded_add3(rounded_add3(local.x*tangent,local.y*bitangent),local.z*direction);
    return vec4<f32>(wo,pdf);
}
//#endif
//#if VOLUME_LIGHT
//#if VOLUME_PHASES
// The non-Henyey-Greenstein lobes (shade_volume's vp_phase_sample).
fn vl_phase_sample_lobe(direction:vec3<f32>,c:SvmClosure,random:vec2<f32>)->vec4<f32>{
    let cosine=vp_sample_cos(c,random.x);let pdf=vp_eval(c,cosine);
    let sine=rounded_sqrt(max(rounded_add(1.0,-cosine*cosine),0.0));let phi=6.283185307179586*random.y;
    let sc=native_phase_sincos(phi);let local=vec3<f32>(sine*sc.y,sine*sc.x,cosine);
    var tangent=vec3<f32>(direction.z-direction.y,direction.x-direction.z,direction.y-direction.x);
    if(direction.x==direction.y&&direction.x==direction.z){tangent=vec3<f32>(direction.z-direction.y,direction.x+direction.z,-direction.y-direction.x);}
    tangent=surface_native_normalize(tangent);let bitangent=rounded_cross(direction,tangent);
    return vec4<f32>(rounded_add3(rounded_add3(local.x*tangent,local.y*bitangent),local.z*direction),pdf);
}
//#endif
fn vl_finish(index:u32,next:u32){
    if(next==0u){volume_write_depth(index);}
//#if VOLUME_RAY_MARCHING else     let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[counter/4u+VL_K_SHADE_VOLUME],1u);
    let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[counter/4u+vr_volume_kernel()],1u);
//#endif
    if(next!=0u){atomicAdd(&heap[counter/4u+next],1u);}state_u16(S_path_queued_kernel,index,next);
}
// integrate_volume_phase_scatter after the direct light (shade_volume's own phase scatter, deferred here so the
// direct light reads the path state before the bounce).
fn vl_phase_scatter(index:u32,r:SvmResult,sampled_t:f32,object:u32){
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));let tmin=heap_float(state_address(S_ray_tmin,index,4u));
    var flag=state_word(S_path_flag,index);let bounce=state_half(S_path_bounce,index);
//#if VOLUME_SAMPLING_PATTERN else     var random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+VL_PRNG_VOLUME_PHASE).xy;
    var random=native_rng_pattern(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+VL_PRNG_VOLUME_PHASE,2u).xy;
//#endif
    var selected=0u;var sum=svm_closure_read(r,0u).sample_weight;
    for(var i=1u;i<r.count;i++){let weight=svm_closure_read(r,i).sample_weight;sum+=weight;let threshold=weight/sum;
        if(random.x<threshold){selected=i;random.x/=threshold;}else{random.x=(random.x-threshold)/(1.0-threshold);}}
//#if VOLUME_PHASES else     let g=svm_closure_read(r,selected).roughness;let phase=vl_phase_sample(direction,g,random);if(phase.w==0.0){vl_finish(index,0u);return;}
    let closure=svm_closure_read(r,selected);var g=closure.roughness;var phase:vec4<f32>;
    if(closure.kind==CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID){phase=vl_phase_sample(direction,closure.roughness,random);}
    else{phase=vl_phase_sample_lobe(direction,closure,random);g=select(select(closure.roughness,0.0,closure.kind==VP_CLOSURE_VOLUME_RAYLEIGH_ID),1.0,closure.kind==VP_CLOSURE_VOLUME_FOURNIER_FORAND_ID);}
    if(phase.w==0.0){vl_finish(index,0u);return;}
//#endif
//#if VOLUME_LIGHT else     let position=origin+sampled_t*direction;state_vec3(S_ray_P,index,position);state_vec3(S_ray_D,index,surface_native_normalize(phase.xyz));
    let position=rounded_add3(origin,sampled_t*direction);state_vec3(S_ray_P,index,position);state_vec3(S_ray_D,index,surface_native_normalize(phase.xyz));
//#endif
//#if VOLUME_LIGHT_TREE
    // ray->tmax - ray->tmin of the segment.
    if(data_word(LT_D_use_light_tree)!=0u){state_f32(S_ray_previous_dt,index,select(bitcast<f32>(0x7f7fffffu),heap_float(state_address(S_isect_t,index,4u)),state_word(S_isect_prim,index)!=NONE)-tmin);}
//#endif
    state_f32(S_ray_tmin,index,0.0);state_u32(S_ray_tmax,index,0x7f7fffffu);state_f32(S_ray_dP,index,0.0);state_f32(S_ray_dD,index,max(heap_float(state_address(S_ray_dD,index,4u)),1.0-abs(g)));
    state_u32(S_isect_prim,index,NONE);state_u32(S_isect_object,index,object);
//#if LIGHT_LINKING
    if(light_link_enabled()){state_u32(S_path_mis_ray_object,index,object);}
//#endif
    if(p(20u)!=0u&&bounce==0u){state_vec3(S_path_pass_diffuse_weight,index,vec3<f32>(1));state_vec3(S_path_pass_glossy_weight,index,vec3<f32>(0));}
//#if VOLUME_LIGHT else     state_f32(S_path_mis_ray_pdf,index,phase.w);state_vec3(S_path_mis_origin_n,index,position-(origin+direction*tmin));
    state_f32(S_path_mis_ray_pdf,index,phase.w);state_vec3(S_path_mis_origin_n,index,position-rounded_add3(origin,direction*tmin));
//#endif
    state_f32(S_path_min_ray_pdf,index,min(phase.w,heap_float(state_address(S_path_min_ray_pdf,index,4u))));
    let next_bounce=bounce+1u;let volume_bounce=state_half(S_path_volume_bounce,index)+1u;
    if(next_bounce>=data_word(VL_D_max_bounce)||volume_bounce>=data_word(VL_D_max_volume_bounce)){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;}
    flag&=~(VL_PATH_RAY_REFLECT|VL_PATH_RAY_SINGULAR|PATH_RAY_TRANSPARENT|VL_PATH_RAY_IMPORTANCE_BAKE|VL_PATH_RAY_MIS_SKIP|VL_PATH_RAY_MIS_HAD_TRANSMISSION);
    flag|=VL_PATH_RAY_MIS_HAD_TRANSMISSION;flag&=~VL_PATH_RAY_TRANSPARENT_BACKGROUND;if((flag&VL_PATH_RAY_ANY_PASS)==0u){flag|=VL_PATH_RAY_VOLUME_PASS;}
    if(next_bounce==1u){flag&=~VL_PATH_RAY_VOLUME_PRIMARY_TRANSMIT;}
    state_u32(S_path_flag,index,flag);state_u8(S_path_visibility,index,VL_PATH_RAY_VISIBILITY_VOLUME_SCATTER);state_u16(S_path_bounce,index,next_bounce);state_u16(S_path_volume_bounce,index,volume_bounce);
//#if SHADOW_LINKING else     state_u16(S_path_rng_offset,index,state_half(S_path_rng_offset,index)+RNG_BOUNCE);vl_finish(index,VL_K_INTERSECT_CLOSEST);
    // integrator_shade_volume: a phase scatter looks for its dedicated lights first.
    state_u16(S_path_rng_offset,index,state_half(S_path_rng_offset,index)+RNG_BOUNCE);vl_finish(index,DL_K_INTERSECT);
//#endif
}
// DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME, part 3: the direct light at the scatter position shade_volume chose,
// then a deferred indirect scatter.
@compute @workgroup_size(1) fn shade_volume_light(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    let vl=p(24u)+id.x*VL_RECORD_BYTES;let direct=load_word(vl+32u);let indirect=load_word(vl+64u);
//#if VOLUME_LIGHT_TREE
    light_select_record=vl+VL_TREE_SELECTION-LIGHT_SELECT_RECORD;
//#endif
//#if PHASE_STAGE else     if(direct==0u&&indirect==0u){return;}store_word(vl+32u,0u);store_word(vl+64u,0u);
    // The indirect phase scatter is shade_volume_phase's (it reads and clears word 64).
    if(direct==0u){return;}store_word(vl+32u,0u);
//#endif
    if(index>=p(10u)||load_word(p(17u))!=0u){return;}
    var r:SvmResult;r.closure_base=p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES;r.count=load_word(vl+60u);
//#if VOLUME_HETEROGENEOUS
    // A heterogeneous path's direct phases (shade_volume_heterogeneous; word 76 is zero for the others).
    if(load_word(vl+76u)!=0u){r.closure_base=load_word(vl+76u);}
//#endif
//#if BACKGROUND_PORTAL
    vl_background_record=vl+128u;
//#endif
    if(direct!=0u){vl_direct_light(index,id.x,r,heap_vec3(vl+36u),heap_vec3(vl+48u),heap_vec3(state_address(S_ray_D,index,12u)));if(load_word(p(17u))!=0u){return;}}
//#if !PHASE_STAGE
    if(indirect!=0u){vl_phase_scatter(index,r,heap_float(vl+68u),load_word(vl+72u));}
//#endif
}
//#endif
//#if VOLUME_LIGHT_SHAPES
// Volume direct light from spot and area lights (pinned light/spot.h spot_light_valid_ray_segment, light/area.h
// area_light_eval<true> and area_light_valid_ray_segment, util/math_intersect.h ray_plane_intersect,
// ray_aabb_intersect, ray_infinite_cylinder_intersect and ray_cone_intersect, util/math_base.h solve_quadratic):
// products rounded before their sums, correctly rounded quotients and square roots. An empty range is x >= y.
// point_light_sample and spot_light_sample<true>: only the position (the equiangular point) is used; the spot light
// samples the visible part of its sphere always, and a zero spot attenuation does not end the sample.
fn vl_lamp_point(light:u32,P:vec3<f32>,random:vec2<f32>)->vec3<f32>{
    let position=heap_vec3(light+L_co);let radius=heap_float(light+L_radius);let r2=radius*radius;let delta=P-position;
    let d2=rounded_dot(delta,delta);let distance=rounded_sqrt(d2);let normal=delta*rounded_divide(1.0,distance);
    if(load_word(light+L_is_sphere)==0u){
        // disk_light_sample: ellipse_sample of the normal's orthonormals, scaled by the radius.
        if(!(r2>0.0)){return position;}
        let b=light_basis(normal);let uv=ggx_disk_sample(random);return rounded_add3(position,rounded_add3(uv.x*b.X,uv.y*b.Y)*radius);
    }
    var D:vec3<f32>;var cosine=1.0;
    if(d2>r2){
        // sample_uniform_cone(-lightN, sin_sqr_to_one_minus_cos(r_sq / d_sq)).
        let ratio=rounded_divide(r2,d2);let angle=select(0.5*ratio,rounded_add(1.0,-rounded_sqrt(max(rounded_add(1.0,-ratio),0.0))),ratio>0.0004);
        D=-normal;
        if(angle>0.0){
            var xy=ggx_disk_sample(random);let disk2=rounded_add(xy.x*xy.x,xy.y*xy.y);cosine=rounded_add(1.0,-(disk2*angle));
            xy*=rounded_sqrt(max(angle*rounded_add(2.0,-(angle*disk2)),0.0));let b=light_basis(D);
            D=rounded_add3(rounded_add3(xy.x*b.X,xy.y*b.Y),cosine*D);
        }
    }else{
        // sample_uniform_sphere (the segment has transmission).
        let z=rounded_add(1.0,-2.0*random.x);let sine=rounded_sqrt(max(rounded_add(1.0,-(z*z)),0.0));let sc=native_phase_sincos(6.283185307179586*random.y);
        D=vec3<f32>(sine*sc.y,sine*sc.x,z);cosine=-rounded_dot(D,normal);
    }
    // Law of cosines; the point remapped onto the sphere.
    let root=rounded_sqrt(max(rounded_add(rounded_add(r2,-d2),d2*(cosine*cosine)),0.0));
    let t=rounded_add(distance*cosine,-select(-root,root,d2-r2>=0.0));
    let ng=surface_native_normalize(rounded_add3(P,D*t)-position);return rounded_add3(ng*radius,position);
}
fn vl_area_point(light:u32,random:vec2<f32>)->vec3<f32>{
    let ru=heap_vec3(light+L_area_axis_u)*(heap_float(light+L_area_len_u)*0.5);let rv=heap_vec3(light+L_area_axis_v)*(heap_float(light+L_area_len_v)*0.5);
    var offset:vec3<f32>;
    if(heap_float(light+L_area_invarea)>0.0){offset=rounded_add3(ru*rounded_add(2.0*random.x,-1.0),rv*rounded_add(2.0*random.y,-1.0));}
    else{let uv=emitter_disk(random);offset=rounded_add3(ru*uv.x,rv*uv.y);}
    return rounded_add3(heap_vec3(light+L_co),offset);
}
fn vl_solve_quadratic(a:f32,b:f32,c:f32)->vec3<f32>{
    // (x1, x2, valid).
    let valid_linear=(a==0.0)&&(b!=0.0);var x=vec2<f32>(rounded_divide(-c,b));
    let discriminant=rounded_add(b*b,-((4.0*a)*c));let valid_quadratic=(a!=0.0)&&(discriminant>-1e-5);
    if(valid_quadratic){
        let root=select(0.0,rounded_sqrt(discriminant),discriminant>0.0);let temp=-0.5*rounded_add(b,select(-root,root,(bitcast<u32>(b)&0x80000000u)==0u));
        let r1=rounded_divide(temp,a);let r2=rounded_divide(c,temp);x=vec2<f32>(min(r1,r2),max(r1,r2));
    }
    return vec3<f32>(x,select(0.0,1.0,valid_linear||valid_quadratic));
}
fn vl_plane_segment(N:vec3<f32>,P:vec3<f32>,D:vec3<f32>,range:vec2<f32>)->vec2<f32>{
    let DN=rounded_dot(D,N);let t=rounded_divide(-rounded_dot(P,N),DN);var out=range;
    if(DN>0.0){out.x=max(out.x,t);}else{out.y=min(out.y,t);}return out;
}
fn vl_cone_segment(axis:vec3<f32>,P:vec3<f32>,D_in:vec3<f32>,cos_angle_sq:f32,range:vec2<f32>)->vec2<f32>{
    if(cos_angle_sq<1e-4){return vl_plane_segment(axis,P,D_in,range);}
    let len_sq=rounded_dot(D_in,D_in);let inv_len=select(0.0,rounded_divide(1.0,rounded_sqrt(len_sq)),len_sq>0.0);let D=D_in*inv_len;
    let AD=rounded_dot(axis,D);let AP=rounded_dot(axis,P);
    let a=rounded_add(AD*AD,-cos_angle_sq);let b=2.0*rounded_add(AD*AP,-(cos_angle_sq*rounded_dot(D,P)));let c=rounded_add(AP*AP,-(cos_angle_sq*rounded_dot(P,P)));
    // solve_quadratic always writes both roots (-c/b when it finds none).
    let q=vl_solve_quadratic(a,b,c);var tmin=q.x;var tmax=q.y;
    let tmin_valid=rounded_add(AP,tmin*AD)>0.0;let tmax_valid=rounded_add(AP,tmax*AD)>0.0;
    let valid=q.z!=0.0&&(tmin_valid||tmax_valid);
    if(!tmax_valid){tmax=tmin;tmin=0.0;}else if(!tmin_valid){tmin=tmax;tmax=bitcast<f32>(0x7f7fffffu);}
    let out=vec2<f32>(max(range.x,tmin*inv_len),min(range.y,tmax*inv_len));
    if(!valid){return vec2<f32>(1.0,0.0);}return out;
}
fn vl_spot_segment(light:u32,P:vec3<f32>,D:vec3<f32>,range:vec2<f32>)->vec2<f32>{
    // lamp_get_inverse_transform: the light object's inverse transform (rows).
    let a=table_at(G_objects,load_word(light+L_object_id),SIZE_objects)+O_itfm;let x=heap_vec4(a);let y=heap_vec4(a+16u);let z=heap_vec4(a+32u);
    let shifted=rounded_add3(P,heap_vec3(light+L_spot_dir)*heap_float(light+L_spot_ray_segment_dp));
    let local_P=vec3<f32>(rounded_add(rounded_dot(x.xyz,shifted),x.w),rounded_add(rounded_dot(y.xyz,shifted),y.w),rounded_add(rounded_dot(z.xyz,shifted),z.w));
    let local_D=vec3<f32>(rounded_dot(x.xyz,D),rounded_dot(y.xyz,D),rounded_dot(z.xyz,D));
    let c=heap_float(light+L_spot_cos);return vl_cone_segment(vec3<f32>(0.0,0.0,-1.0),local_P,local_D,c*c,range);
}
fn vl_area_segment(light:u32,P_in:vec3<f32>,D_in:vec3<f32>,range:vec2<f32>)->vec2<f32>{
    let tan_half_spread=heap_float(light+L_area_tan_half_spread);var axis=heap_vec3(light+L_area_dir);var out=range;var valid=true;
    let u=heap_vec3(light+L_area_axis_u);let v=heap_vec3(light+L_area_axis_v);let len_u=heap_float(light+L_area_len_u);let len_v=heap_float(light+L_area_len_v);
    var P=P_in;var D=D_in;
    if(tan_half_spread<1e-5){
        // make_transform(axis_u, axis_v, axis): rows u, v, axis.
        P=vec3<f32>(rounded_dot(u,P_in),rounded_dot(v,P_in),rounded_dot(axis,P_in));D=vec3<f32>(rounded_dot(u,D_in),rounded_dot(v,D_in),rounded_dot(axis,D_in));
        axis=vec3<f32>(0.0,0.0,1.0);let half_u=0.5*len_u;let half_v=0.5*len_v;
        if(heap_float(light+L_area_invarea)<0.0){
            // ray_infinite_cylinder_intersect.
            let inv_len=vec2<f32>(rounded_divide(1.0,half_u),rounded_divide(1.0,half_v));var Pp=P.xy*inv_len;let Dp=D.xy*inv_len;
            let a=rounded_add(Dp.x*Dp.x,Dp.y*Dp.y);var b=rounded_add(Pp.x*Dp.x,Pp.y*Dp.y);let t_mid=rounded_divide(-b,a);
            Pp=vec2<f32>(rounded_add(Pp.x,Dp.x*t_mid),rounded_add(Pp.y,Dp.y*t_mid));b=rounded_add(Pp.x*Dp.x,Pp.y*Dp.y);
            let c=rounded_add(rounded_add(Pp.x*Pp.x,Pp.y*Pp.y),-1.0);let q=vl_solve_quadratic(a,2.0*b,c);
            out=vec2<f32>(max(out.x,rounded_add(q.x,t_mid)),min(out.y,rounded_add(q.y,t_mid)));valid=q.z!=0.0&&out.x<out.y;
        }else{
            // ray_aabb_intersect.
            let inv=vec3<f32>(rounded_divide(1.0,D.x),rounded_divide(1.0,D.y),rounded_divide(1.0,D.z));
            let lower=vec3<f32>(rounded_add(-half_u,-P.x),rounded_add(-half_v,-P.y),rounded_add(0.0,-P.z))*inv;
            let upper=vec3<f32>(rounded_add(half_u,-P.x),rounded_add(half_v,-P.y),rounded_add(bitcast<f32>(0x7f7fffffu),-P.z))*inv;
            let mins=min(lower,upper);let maxs=max(lower,upper);
            out=vec2<f32>(max(max(max(mins.x,mins.y),mins.z),out.x),min(min(min(maxs.x,maxs.y),maxs.z),out.y));valid=out.x<out.y;
        }
    }else{
        // The smallest cone covering the spread (area_light_max_extent: the half diagonal or the larger half axis).
        var extent=0.5*max(len_u,len_v);if(heap_float(light+L_area_invarea)>0.0){extent=0.5*rounded_sqrt(rounded_add(len_u*len_u,len_v*len_v));}
        let apex_to_point=rounded_add3(P,axis*rounded_divide(extent,tan_half_spread));
        let cos_angle_sq=rounded_divide(1.0,rounded_add(1.0,tan_half_spread*tan_half_spread));
        out=vl_cone_segment(axis,apex_to_point,D,cos_angle_sq,out);valid=out.x<out.y;
    }
    if(!valid){return vec2<f32>(1.0,0.0);}
    return vl_plane_segment(axis,P,D,out);
}
//#endif
