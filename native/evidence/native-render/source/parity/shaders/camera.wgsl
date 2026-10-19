// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Adapted from Cycles camera/camera.h, integrator/init_from_camera.h,
// integrator/path_state.h, util/lookup_table.h, util/transform.h and projection.h.
// Static perspective pinhole camera only; host rejects other configurations.
// Differentials are not consumed by the current texture-free integration gate.
struct InitialPath { origin: vec4<f32>, direction: vec4<f32>, throughput: vec3<f32> }
// The caller supplies path_count() and path_sample_index() for full-frame or
// bounded native queue batches; camera mathematics is shared by both.
fn pixel_sample(index: u32) -> vec4<u32> {
    let i=path_sample_index(index);
    let c=camera_data[0]; let x=i%c.x; let y=(i/c.x)%c.y; let s=i/(c.x*c.y);
    return vec4<u32>(x,y,s,pixel_hash(x,y,camera_data[1].x));
}
fn path_random(i: u32, bounce: u32) -> vec3<f32> {
    let p=pixel_sample(i); let c=camera_data[3];
    return sobol_sample(p.z,p.w,(bounce+1u)*c.y+c.z,camera_data[1].x,camera_float(2).x).xyz;
}
fn filter_lookup(value: f32) -> f32 {
    let count=camera_data[3].w; let base=camera_data[4].y;
    let x=clamp(value,0.0,1.0)*f32(count-1u); let index=min(u32(x),count-1u);
    let t=x-f32(index); let a=camera_scalar(base+index);
    if(t==0.0) { return a; }
    let b=camera_scalar(base+min(index+1u,count-1u)); return (1.0-t)*a+t*b;
}
fn path_initial(i: u32) -> InitialPath {
    let p=pixel_sample(i); var uv=vec2<f32>(0.5);
    if(p.z!=0u) { uv=sobol_sample(p.z,p.w,camera_data[3].x,camera_data[1].x,camera_float(2).x).xy; }
    let raster=vec4<f32>(vec2<f32>(p.xy)+vec2<f32>(filter_lookup(uv.x),filter_lookup(uv.y)),0,1);
    var camera_point=vec3<f32>(dot(camera_float(5),raster),dot(camera_float(6),raster),dot(camera_float(7),raster));
    let w=dot(camera_float(8),raster);
    if(w!=0.0) { camera_point/=w; } else { camera_point=vec3<f32>(0); }
    let tx=camera_float(9); let ty=camera_float(10); let tz=camera_float(11);
    // Cycles CPU transform_direction accumulates z, then y, then x.
    let direction=normalize(camera_point.x*vec3<f32>(tx.x,ty.x,tz.x)+
                          (camera_point.y*vec3<f32>(tx.y,ty.y,tz.y)+camera_point.z*vec3<f32>(tx.z,ty.z,tz.z)));
    let z_inverse=1.0/normalize(camera_point).z; let clip=camera_float(2);
    let origin=vec3<f32>(tx.w,ty.w,tz.w)+(clip.y*z_inverse)*direction;
    return InitialPath(vec4<f32>(origin,0),vec4<f32>(direction,clip.z*z_inverse),vec3<f32>(1));
}
