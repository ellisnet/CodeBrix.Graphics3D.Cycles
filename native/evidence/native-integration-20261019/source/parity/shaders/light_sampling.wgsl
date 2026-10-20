// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Ported from Cycles light/point.h, light/distribution.h, sample/mapping.h,
// sample/mis.h and util/math_intersect.h. Mapping/MIS retain the OSL BSD notice:
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
struct LightSample { p: vec3<f32>, t: f32, d: vec3<f32>, pdf: f32,
                     n: vec3<f32>, eval_fac: f32, id: u32 }
fn light_float(row: u32) -> vec4<f32> { return bitcast<vec4<f32>>(light_data[row]); }
fn light_row(id: u32) -> u32 { return light_data[0].z+4u*id; }
fn power_weight(a: f32, b: f32) -> f32 { return (a*a)/(a*a+b*b); }
fn cone_angle(sine_squared: f32) -> f32 {
    if(sine_squared>0.0004) { return 1.0-sqrt(max(1.0-sine_squared,0.0)); }
    return 0.5*sine_squared;
}
fn light_tangent(n: vec3<f32>) -> vec3<f32> {
    if(n.x!=n.y || n.x!=n.z) { return normalize(vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x)); }
    return normalize(vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x));
}
fn sphere_pdf(distance_squared: f32, radius_squared: f32, n: vec3<f32>, d: vec3<f32>, transmission: bool) -> f32 {
    if(distance_squared>radius_squared) { return (INV_PI*.5)/cone_angle(radius_squared/distance_squared); }
    if(transmission) { return INV_PI*.25; }
    return max(dot(n,d),0.0)*INV_PI;
}
fn sphere_intersect(p: vec3<f32>, d: vec3<f32>, tmin: f32, tmax: f32, light: vec4<f32>) -> f32 {
    let delta=light.xyz-p; let r2=light.w*light.w; let d2=dot(delta,delta); let dc=dot(delta,d);
    if(d2>r2 && dc<0.0) { return -1; }
    let perpendicular=delta-dc*d; let ds2=dot(perpendicular,perpendicular);
    if(ds2>r2) { return -1; }
    let t=dc-select(-1.0,1.0,d2-r2>=0.0)*sqrt(r2-ds2);
    if(t>tmin && t<tmax) { return t; } return -1;
}
fn select_light(random: f32) -> u32 {
    var first=0u; var len=light_data[0].y+1u; let base=light_data[0].w;
    loop {
        let half=len>>1u; let middle=first+half;
        if(random<light_float(base+middle).x) { len=half; }
        else { first=middle+1u; len-=half+1u; }
        if(len==0u) { break; }
    }
    let index=clamp(i32(first)-1,0,i32(light_data[0].y)-1);
    return light_data[base+u32(index)].y;
}
fn sample_light(p: vec3<f32>, n: vec3<f32>, transmission: bool, random: vec3<f32>) -> LightSample {
    let id=select_light(random.z); let base=light_row(id); let light=light_float(base);
    let radius_squared=light.w*light.w; let delta=p-light.xyz;
    let distance_squared=dot(delta,delta); let distance=sqrt(distance_squared); let light_n=delta/distance;
    var d: vec3<f32>; var pdf: f32; var cosine: f32;
    if(distance_squared>radius_squared) {
        let angle=cone_angle(radius_squared/distance_squared);
        var xy=disk_sample(random.xy); let r2=dot(xy,xy);
        cosine=1.0-r2*angle; xy*=sqrt(max(angle*(2.0-angle*r2),0.0));
        let axis= -light_n; let t=light_tangent(axis); let b=cross(axis,t);
        d=xy.x*t+xy.y*b+cosine*axis; pdf=(INV_PI*.5)/angle;
    } else {
        if(transmission) {
            let z=1.0-2.0*random.x; let radius=sqrt(max(1.0-z*z,0.0)); let phi=2.0*PI*random.y;
            d=vec3<f32>(radius*cos(phi),radius*sin(phi),z); pdf=INV_PI*.25;
        } else { let h=hemisphere(n,random.xy); d=h.xyz; pdf=h.w; }
        cosine= -dot(d,light_n);
    }
    let t=distance*cosine-select(-1.0,1.0,distance_squared-radius_squared>=0.0)*
          sqrt(max(radius_squared-distance_squared+distance_squared*(cosine*cosine),0.0));
    let sampled=p+d*t; let normal=normalize(sampled-light.xyz);
    return LightSample(normal*light.w+light.xyz,t,d,pdf*light_float(1).x,normal,light_float(base+1u).w,id);
}
