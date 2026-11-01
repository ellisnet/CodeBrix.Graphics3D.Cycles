// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Background (world) light: equirectangular importance-map sampling and PDF,
// uniform-sphere fallback and the background MIS PDF, adapted from the pinned
// kernel/light/background.h, kernel/camera/projection.h, util/projection.h and
// kernel/sample/mapping.h. The importance map is Cycles' own
// light_background_{marginal,conditional}_cdf (float2 pairs: function value,
// CDF), built by the unmodified LightManager from the device's
// shader_eval_background. Portals and sky-texture sun guiding are refused on the
// host (lighting.cpp), so the map is the only strategy when map_weight > 0.
//#if LIGHT_BACKGROUND
const BGL_2PI:f32=6.2831854820251465;
const BGL_PI:f32=3.1415927410125732;
fn bgl_2pi_pi()->f32{return bitcast<f32>(0x419de9e7u);} // M_2PI_F * M_PI_F rounded to binary32
fn bgl_inv_4pi()->f32{return bitcast<f32>(0x3da2f983u);} // 1.0f / M_4PI_F
// equirectangular_to_direction (range -2pi, pi, -pi, pi).
fn bgl_direction(u:f32,v:f32)->vec3<f32>{
    let phi=rounded_add(-BGL_2PI*u,BGL_PI);let theta=rounded_add(-BGL_PI*v,BGL_PI);
    let st=native_disk_sincos(theta);let sp=native_disk_sincos(phi);
    return vec3<f32>(st.x*sp.y,st.x*sp.x,st.y);
}
// direction_to_equirectangular (wavefront_images.wgsl).
fn bgl_equirectangular(direction:vec3<f32>)->vec2<f32>{return image_equirectangular(direction);}
fn bgl_marginal(i:u32)->vec2<f32>{let at=table_at(G_light_background_marginal_cdf,i,SIZE_light_background_marginal_cdf);return vec2<f32>(heap_float(at),heap_float(at+4u));}
fn bgl_conditional(i:u32)->vec2<f32>{let at=table_at(G_light_background_conditional_cdf,i,SIZE_light_background_conditional_cdf);return vec2<f32>(heap_float(at),heap_float(at+4u));}
fn bgl_map_valid()->bool{
    let rx=data_word(D_background_map_res_x);let ry=data_word(D_background_map_res_y);
    return rx>0u&&ry>0u&&table_count(G_light_background_marginal_cdf)>=ry+1u&&table_count(G_light_background_conditional_cdf)/(rx+1u)>=ry;
}
// background_map_sample: returns the direction and its PDF (w).
fn bgl_map_sample(random:vec2<f32>)->vec4<f32>{
    if(!bgl_map_valid()){fail();return vec4<f32>(0);}
    let res_x=data_word(D_background_map_res_x);let res_y=data_word(D_background_map_res_y);let width=res_x+1u;
    var first=0u;var count=res_y;
    loop{if(count==0u){break;}let step=count>>1u;let middle=first+step;
        if(bgl_marginal(middle).y<random.y){first=middle+1u;count-=step+1u;}else{count=step;}}
    let index_v=select(first-1u,0u,first==0u);
    let cdf_v=bgl_marginal(index_v);let cdf_next_v=bgl_marginal(index_v+1u);let cdf_last_v=bgl_marginal(res_y);
    let dv=rounded_divide(rounded_add(random.y,-cdf_v.y),rounded_add(cdf_next_v.y,-cdf_v.y));
    let v=rounded_divide(rounded_add(f32(index_v),dv),f32(res_y));
    first=0u;count=res_x;let row=index_v*width;
    loop{if(count==0u){break;}let step=count>>1u;let middle=first+step;
        if(bgl_conditional(row+middle).y<random.x){first=middle+1u;count-=step+1u;}else{count=step;}}
    let index_u=select(first-1u,0u,first==0u);
    let cdf_u=bgl_conditional(row+index_u);let cdf_next_u=bgl_conditional(row+index_u+1u);let cdf_last_u=bgl_conditional(row+res_x);
    let du=rounded_divide(rounded_add(random.x,-cdf_u.y),rounded_add(cdf_next_u.y,-cdf_u.y));
    let u=rounded_divide(rounded_add(f32(index_u),du),f32(res_x));
    let sin_theta=native_disk_sincos(BGL_PI*v).x;
    let denom=((bgl_2pi_pi()*sin_theta)*cdf_last_u.x)*cdf_last_v.x;
    var pdf=0.0;if(!(sin_theta==0.0||denom==0.0)){pdf=rounded_divide(cdf_u.x*cdf_v.x,denom);}
    return vec4<f32>(bgl_direction(u,v),pdf);
}
// background_map_pdf.
fn bgl_map_pdf(direction:vec3<f32>)->f32{
    if(!bgl_map_valid()){fail();return 0.0;}
    let uv=bgl_equirectangular(direction);
    let res_x=data_word(D_background_map_res_x);let res_y=data_word(D_background_map_res_y);let width=res_x+1u;
    let sin_theta=native_disk_sincos(uv.y*BGL_PI).x;if(sin_theta==0.0){return 0.0;}
    let index_u=u32(clamp(i32(uv.x*f32(res_x)),0,i32(res_x)-1));let index_v=u32(clamp(i32(uv.y*f32(res_y)),0,i32(res_y)-1));
    let cdf_last_u=bgl_conditional(index_v*width+res_x);let cdf_last_v=bgl_marginal(res_y);
    let denom=((bgl_2pi_pi()*sin_theta)*cdf_last_u.x)*cdf_last_v.x;if(denom==0.0){return 0.0;}
    let cdf_u=bgl_conditional(index_v*width+index_u);let cdf_v=bgl_marginal(index_v);
    return rounded_divide(cdf_u.x*cdf_v.x,denom);
}
//#if BACKGROUND_SUN
// background_sun_sample / background_sun_pdf: sample_uniform_cone and pdf_uniform_cone around the sun direction
// (kernel_data.background.sun: direction and half angle), with one_minus_cos and precise_angle. For the MIS PDFs the
// map's direction_to_equirectangular and the sun's precise_angle share one arctangent body (bgl_sun_map_pdfs).
fn bgl_sun()->vec4<f32>{return vec4<f32>(data_float(D_background_sun),data_float(D_background_sun+4u),data_float(D_background_sun+8u),data_float(D_background_sun+12u));}
fn bgl_one_minus_cos(angle:f32)->f32{if(angle>0.02){return rounded_add(1.0,-trig_sincos_wide(angle).y);}return 0.5*(angle*angle);}
fn bgl_sun_sample(random:vec2<f32>,sun:vec4<f32>,omc:f32)->vec4<f32>{
    let n=sun.xyz;
    if(!(omc>0.0)){return vec4<f32>(n,1.0);}
    // sample_uniform_disk (concentric mapping).
    let a=rounded_add(2.0*random.x,-1.0);let b=rounded_add(2.0*random.y,-1.0);var xy=vec2<f32>(0);
    if(!(a==0.0&&b==0.0)){var radius:f32;var phi:f32;
        if(a*a>b*b){radius=a;phi=0.7853981852531433*rounded_divide(b,a);}else{radius=b;phi=rounded_add(1.5707963705062866,-(0.7853981852531433*rounded_divide(a,b)));}
        let sc=native_disk_sincos(phi);xy=vec2<f32>(radius*sc.y,radius*sc.x);}
    let r2=rounded_add(xy.x*xy.x,xy.y*xy.y);let cos_theta=rounded_add(1.0,-(r2*omc));
    xy*=rounded_sqrt(max(omc*rounded_add(2.0,-(omc*r2)),0.0));
    // make_orthonormals(N).
    var t=vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x);if(n.x==n.y&&n.x==n.z){t=vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x);}
    t=surface_native_normalize(t);let bt=rounded_cross(n,t);
    return vec4<f32>(rounded_add3(rounded_add3(xy.x*t,xy.y*bt),cos_theta*n),rounded_divide(0.15915493667125702,omc));
}
// (background_sun_pdf, background_map_pdf when map is set) of one direction; bgl_map_pdf from its equirectangular
// coordinates.
fn bgl_sun_map_pdfs(direction:vec3<f32>,sun:vec4<f32>,omc:f32,map:bool)->vec2<f32>{
    let d=rounded_add3(sun.xyz,-direction);let e=rounded_add3(sun.xyz,direction);let ld=rounded_sqrt(rounded_dot(d,d));let le=rounded_sqrt(rounded_dot(e,e));
    var angles=array<f32,2>();let pair=rounded_steps(2u);
    for(var i=0u;i<pair;i++){let j=min(i,1u);angles[j]=trig_atan2_guarded(select(direction.y,ld,j==1u),select(direction.x,le,j==1u));}
    var pdfs=vec2<f32>(0);
    if(2.0*angles[1]<sun.w){pdfs.x=rounded_divide(0.15915493667125702,omc);}
    if(!map){return pdfs;}
    // direction_to_equirectangular (image_equirectangular with the arctangent above).
    var uv=vec2<f32>(0);
    if(!all(direction==vec3<f32>(0))){
        let length=rounded_sqrt(rounded_dot(direction,direction));
        uv=vec2<f32>(rounded_divide(rounded_add(angles[0],-3.1415927410125732),-6.2831854820251465),
                     rounded_divide(rounded_add(trig_acos(rounded_divide(direction.z,length)),-3.1415927410125732),-3.1415927410125732));
    }
    if(!bgl_map_valid()){fail();return pdfs;}
    let res_x=data_word(D_background_map_res_x);let res_y=data_word(D_background_map_res_y);let width=res_x+1u;
    let sin_theta=native_disk_sincos(uv.y*BGL_PI).x;if(sin_theta==0.0){return pdfs;}
    let index_u=u32(clamp(i32(uv.x*f32(res_x)),0,i32(res_x)-1));let index_v=u32(clamp(i32(uv.y*f32(res_y)),0,i32(res_y)-1));
    let cdf_last_u=bgl_conditional(index_v*width+res_x);let cdf_last_v=bgl_marginal(res_y);
    let denom=((bgl_2pi_pi()*sin_theta)*cdf_last_u.x)*cdf_last_v.x;if(denom==0.0){return pdfs;}
    let cdf_u=bgl_conditional(index_v*width+index_u);let cdf_v=bgl_marginal(index_v);
    pdfs.y=rounded_divide(cdf_u.x*cdf_v.x,denom);return pdfs;
}
// With BACKGROUND_SUN the sun strategy is the second one (sky-texture sun guiding).
//#endif
// background_light_sample (portal and sun weights are zero): the map, or the
// uniform sphere when no strategy is enabled.
fn bgl_sample(random:vec2<f32>)->vec4<f32>{
    let map=data_float(D_background_map_weight);
//#if BACKGROUND_SUN else     if(data_float(D_background_portal_weight)!=0.0||data_float(D_background_sun_weight)!=0.0){fail();return vec4<f32>(0);}
    let sun=data_float(D_background_sun_weight);if(data_float(D_background_portal_weight)!=0.0){fail();return vec4<f32>(0);}
    let total=rounded_add(sun,map);
    if(total==0.0){
        let z=rounded_add(1.0,-2.0*random.x);let r=rounded_sqrt(max(rounded_add(1.0,-z*z),0.0));let phi=BGL_2PI*random.y;let sc=native_phase_sincos(phi);
        return vec4<f32>(r*sc.y,r*sc.x,z,bgl_inv_4pi());
    }
    let fac=rounded_divide(1.0,total);let sun_method=sun*fac;let map_method=map*fac;var x=random.x;var s:vec4<f32>;
    let sun_light=bgl_sun();let omc=bgl_one_minus_cos(sun_light.w);let sun_first=x<sun_method;
    if(sun_first){
        if(sun_method!=1.0){x=rounded_divide(x,sun_method);}
        s=bgl_sun_sample(vec2<f32>(x,random.y),sun_light,omc);if(sun_method==1.0){return s;}s.w*=sun_method;
        if(map_method==0.0){return s;}
    }else{
        if(map_method!=1.0){x=rounded_divide(rounded_add(x,-sun_method),map_method);}
        s=bgl_map_sample(vec2<f32>(x,random.y));if(map_method==1.0){return s;}s.w*=map_method;
        if(sun_method==0.0){return s;}
    }
    // MIS with the other strategy.
    let pdfs=bgl_sun_map_pdfs(s.xyz,sun_light,omc,sun_first);
    s.w=rounded_add(s.w,select(sun_method*pdfs.x,map_method*pdfs.y,sun_first));
    return s;
//#endif
//#if !BACKGROUND_SUN
    if(map==0.0){
        let z=rounded_add(1.0,-2.0*random.x);let r=rounded_sqrt(max(rounded_add(1.0,-z*z),0.0));let phi=BGL_2PI*random.y;let sc=native_phase_sincos(phi);
        return vec4<f32>(r*sc.y,r*sc.x,z,bgl_inv_4pi());
    }
    // Only strategy: pdf_fac normalizes the weight to exactly one.
    let method=map*rounded_divide(1.0,map);var x=random.x;if(method!=1.0){x=rounded_divide(x,method);}
    var s=bgl_map_sample(vec2<f32>(x,random.y));if(method!=1.0){s.w*=method;}
    return s;
//#endif
}
// shader_setup_from_background for an explicit direction (svm_world reads the path ray).
fn bgl_world_data(direction:vec3<f32>,dD:f32,time:f32)->SurfaceData{
    var sd:SurfaceData;let shader_id=data_word(D_background_surface_shader)&SHADER_MASK;
    if(shader_id>=table_count(G_shaders)){surface_failure(&sd);return sd;}
    sd.P=direction;sd.N=-direction;sd.Ng=-direction;sd.wi=-direction;sd.object_id=NONE;sd.prim=NONE;
    sd.shader=data_word(D_background_surface_shader);sd.shader_flag=load_word(table_at(G_shaders,shader_id,SIZE_shaders)+SH_flags);
    sd.dPdu=surface_orthogonal(sd.Ng);sd.dPdv=rounded_cross(sd.Ng,sd.dPdu);
    sd.time=time;sd.ray_length=bitcast<f32>(0x7f7fffffu);
    sd.dP=dD;sd.dI=sd.dP;sd.du=vec2<f32>(sd.dP,0);sd.dv=vec2<f32>(0,sd.dP);
    return sd;
}
// background_light_pdf (portal and sun weights are zero).
fn bgl_pdf(direction:vec3<f32>)->f32{
//#if BACKGROUND_SUN else     let map=data_float(D_background_map_weight);if(map==0.0){return bgl_inv_4pi();}
    let map=data_float(D_background_map_weight);let sun=data_float(D_background_sun_weight);let total=rounded_add(sun,map);
    if(total==0.0){return bgl_inv_4pi();}
    let fac=rounded_divide(1.0,total);let sun_method=sun*fac;let map_method=map*fac;var pdf=0.0;
    let sun_light=bgl_sun();let pdfs=bgl_sun_map_pdfs(direction,sun_light,bgl_one_minus_cos(sun_light.w),map_method!=0.0);
    if(sun_method!=0.0){pdf=rounded_add(pdf,pdfs.x*sun_method);}
    if(map_method!=0.0){pdf=rounded_add(pdf,pdfs.y*map_method);}
    return pdf;
//#endif
//#if !BACKGROUND_SUN
    let method=map*rounded_divide(1.0,map);
    return 0.0+bgl_map_pdf(direction)*method;
//#endif
}
//#endif
