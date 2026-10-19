// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Instrument the prototype's existing camera, traversal and shading expressions.
@group(0) @binding(0) var<storage,read> inputs: array<vec4<f32>>;
@group(0) @binding(1) var<storage,read_write> outputs: array<vec4<f32>>;
@group(0) @binding(2) var<storage,read> scene: array<vec4<u32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i=id.x; if (i*8u>=arrayLength(&inputs)) { return; }
    let origin=inputs[i*8u]; let forward=inputs[i*8u+1u]; let right=inputs[i*8u+2u];
    let up=inputs[i*8u+3u]; let light=inputs[i*8u+4u]; let background=inputs[i*8u+5u];
    let dims=vec4<u32>(inputs[i*8u+6u]); let pixel=u32(inputs[i*8u+7u].x);
    let jitter=sample_jitter(dims.z);
    let xy=vec2<f32>(f32(pixel%dims.x),f32(pixel/dims.x))+jitter;
    let uv=xy/vec2<f32>(f32(dims.x),f32(dims.y));
    var direction=normalize(forward.xyz+right.xyz*((2.0*uv.x-1.0)*right.w*forward.w)
        +up.xyz*((1.0-2.0*uv.y)*forward.w));
    if (SHARED_RAYS) { direction=inputs[i*8u+7u].yzw; }
    let hit=trace(origin.xyz,direction,origin.w,up.w,-1);
    var color=background.xyz; var normal=vec3<f32>(0); var cosine=0.0;
    var distance_squared=0.0; var blocker_id= -2.0;
    if (hit.w>=0.0) {
        let prim=i32(hit.w); let scene_info=scene_float(0); let vbase=i32(scene_info.w)+3*prim;
        let a=scene_float(vbase).xyz; let b=scene_float(vbase+1).xyz; let c=scene_float(vbase+2).xyz;
        normal=normalize(cross(b-a,c-a)); if (dot(normal,direction)>0.0) { normal=-normal; }
        let p=origin.xyz+direction*hit.x; let delta=light.xyz-p;
        distance_squared=max(dot(delta,delta),0.0001); let distance=sqrt(distance_squared);
        let light_direction=delta/distance; let albedo=scene_float(i32(scene_info.w+3.0*scene_info.x)+prim).xyz;
        color=vec3<f32>(0); cosine=max(dot(normal,light_direction),0.0);
        if (cosine>0.0 && distance>0.0002) {
            let blocker=trace(p+normal*0.0001,light_direction,0.0001,distance-0.0001,prim);
            blocker_id=blocker.w;
            if (blocker.w<0.0) { color=albedo*(cosine*0.3183098861837907)*(light.w/distance_squared); }
        }
    }
    outputs[i*4u]=vec4<f32>(direction,hit.x); outputs[i*4u+1u]=vec4<f32>(normal,hit.w);
    outputs[i*4u+2u]=vec4<f32>(cosine,distance_squared,blocker_id,0);
    outputs[i*4u+3u]=vec4<f32>(color,1);
}
