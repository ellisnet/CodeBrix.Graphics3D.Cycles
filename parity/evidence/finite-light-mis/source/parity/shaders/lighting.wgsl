// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Adapted from Cycles light/sample.h and integrator/shade_surface.h/shade_light.h.
// Constant finite spherical lights, flat opaque geometry; other modes rejected by exporter.
@group(0) @binding(3) var<storage,read> light_data: array<vec4<u32>>;
@group(0) @binding(4) var<storage,read_write> shadow_rays: array<vec4<f32>>;
fn lighting_begin(base: u32) {
    for(var b=0u;b<MAX_BOUNCES;b++) {
        outputs[base+MAX_BOUNCES+3u+2u*b]=vec4<f32>(0);
        outputs[base+MAX_BOUNCES+4u+2u*b]=vec4<f32>(-1,-1,0,0);
    }
    outputs[base+3u*MAX_BOUNCES+3u]=vec4<f32>(0);
}
fn lighting_forward(p: vec3<f32>, d: vec3<f32>, tmin: f32, tmax: f32,
    previous_pdf: f32, previous_n: vec3<f32>, transmission: bool, bounce: u32,
    count: u32, throughput: vec3<f32>, base: u32) -> ForwardLighting {
    var result=ForwardLighting(vec3<f32>(0),0u,false); var start=tmin;
    var last= -1; let limit=u32(light_float(1).z);
    for(var step=0u;step<light_data[0].x;step++) {
        var closest=tmax; var selected= -1;
        for(var id=0u;id<light_data[0].x;id++) {
            if(i32(id)==last) { continue; }
            let t=sphere_intersect(p,d,start,closest,light_float(light_row(id)));
            if(t>=0.0) { closest=t; selected=i32(id); }
        }
        if(selected<0) { break; }
        let row=light_row(u32(selected)); let light=light_float(row); let emission=light_float(row+1u);
        var weight=1.0;
        if(bounce>0u) {
            let delta=p-light.xyz;
            let pdf=sphere_pdf(dot(delta,delta),light.w*light.w,previous_n,d,transmission)*light_float(1).x;
            weight=power_weight(previous_pdf,pdf);
        }
        let contribution=throughput*(emission.xyz*emission.w)*weight;
        result.color+=contribution; result.hits++;
        if(count+result.hits>=limit) { result.terminated=true; break; }
        start=bitcast<f32>(bitcast<u32>(closest)+1u); last=selected;
    }
    outputs[base+3u*MAX_BOUNCES+3u]+=vec4<f32>(result.color,f32(result.hits));
    return result;
}
fn lighting_direct(i: u32, bounce: u32, base: u32, p: vec3<f32>, n: vec3<f32>, wi: vec3<f32>,
    closure: Closure, throughput: vec3<f32>, a: vec3<f32>, b: vec3<f32>, c: vec3<f32>, prim: i32) -> vec3<f32> {
    let pixel=pixel_sample(i); let dimension=(bounce+1u)*camera_data[3].y+light_data[2].x;
    let random=sobol_sample(pixel.z,pixel.w,dimension,camera_data[1].x,camera_float(2).x).xyz;
    let sample=sample_light(p,n,closure.kind==TRANSLUCENT,random);
    let row=light_row(sample.id);
    if(bounce>light_data[row+2u].y || sample.pdf<=0.0) { return vec3<f32>(0); }
    let evaluated=bsdf_eval(closure,wi,sample.d); let emission=light_float(row+1u);
    let weight=power_weight(sample.pdf,evaluated.w);
    let light_factor=emission.xyz*sample.eval_fac/sample.pdf*weight;
    let value=(evaluated.xyz*closure.weight)*light_factor;
    if(all(value==vec3<f32>(0))) { return vec3<f32>(0); }
    let delta=sample.p-p; let distance=length(delta); let direction=delta/distance;
    var origin=p;
    if(!self_intersection(p,direction,a,b,c)) { origin=offset_ray(p,n); }
    let contribution=throughput*value;
    let record=base+MAX_BOUNCES+3u+2u*bounce;
    outputs[record]=vec4<f32>(contribution,distance);
    outputs[record+1u]=vec4<f32>(f32(sample.id),-1,0,0);
    let shadow=(i*MAX_BOUNCES+bounce)*2u;
    shadow_rays[shadow]=vec4<f32>(origin,f32(prim));
    shadow_rays[shadow+1u]=vec4<f32>(direction,distance);
    return vec3<f32>(0);
}
// A separate dispatch keeps shadow BVH traversal out of the surface integrator.
// Dispatch boundaries provide visibility for the GPU-written ray/state buffers.
@compute @workgroup_size(64) fn shadow_resolve(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if(i>=path_count()) { return; }
    let base=i*OUTPUT_ROWS; var contribution=vec3<f32>(0);
    for(var b=0u;b<MAX_BOUNCES;b++) {
        let record=base+MAX_BOUNCES+3u+2u*b;
        if(outputs[record+1u].x<0.0) { continue; }
        let shadow=(i*MAX_BOUNCES+b)*2u; let p=shadow_rays[shadow]; let d=shadow_rays[shadow+1u];
        let blocked=trace(p.xyz,d.xyz,0.0,d.w,i32(p.w)).w>=0.0;
        outputs[record+1u].y=select(0.0,1.0,blocked);
        if(!blocked) { contribution+=outputs[record].xyz; }
    }
    outputs[base]=vec4<f32>(outputs[base].xyz+contribution,1);
}
