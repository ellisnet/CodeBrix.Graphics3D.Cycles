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
//#if BACKGROUND_PORTAL
    // With portals: background_light_pdf from the path's ray origin (bgl_portal_origin, set by the caller).
    return bgl_portal_light_pdf(bgl_portal_origin,direction);
//#endif
//#if !BACKGROUND_PORTAL
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
//#endif
}
//#if BACKGROUND_PORTAL
// Background light portals (light/background.h): area lights with is_portal, stored after the lamps of the light
// table (integrator.portal_offset). A portal is possible from P when P lies on the side its direction points to;
// the portal strategy samples one of the possible portals uniformly (the rectangle by spherical rectangle sampling,
// area_rectangle; the ellipse uniformly over its area), and its pdf is the mean over the possible portals the
// direction passes through. Every sum, product before a sum, quotient and square root is correctly rounded in the
// pinned order (wavefront_float.wgsl), as the oracle kernels compute them with -ffp-contract=off.
var<private> bgl_portal_origin:vec3<f32>;
fn bgl_portal(index:u32)->u32{return table_at(G_lights,data_word(D_integrator_portal_offset)+index,SIZE_lights);}
// background_portal_data_fetch_and_check_side.
fn bgl_portal_side(light:u32,P:vec3<f32>)->bool{return rounded_dot(heap_vec3(light+L_area_dir),P-heap_vec3(light+L_co))>1e-4;}
// normalize_len: the length, then a product with its reciprocal (every host).
fn bgl_normalize_len(v:vec3<f32>)->vec4<f32>{let t=rounded_sqrt(rounded_dot(v,v));return vec4<f32>(v*rounded_divide(1.0,t),t);}
// float3 / float: per-component division (SSE) or the reciprocal product (scalar float3 hosts).
fn bgl_div3(v:vec3<f32>,f:f32)->vec3<f32>{
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return v*rounded_divide(1.0,f);}
    return vec3<f32>(rounded_divide(v.x,f),rounded_divide(v.y,f),rounded_divide(v.z,f));
}
// light_pdf_area_to_solid_angle.
fn bgl_area_to_solid_angle(Ng:vec3<f32>,I:vec3<f32>,t:f32)->f32{let c=rounded_dot(Ng,I);if(c<=0.0){return 0.0;}return rounded_divide(t*t,c);}
fn bgl_portal_region(light:u32)->AreaRegion{
    return AreaRegion(heap_vec3(light+L_co),heap_vec3(light+L_area_axis_u),heap_vec3(light+L_area_axis_v),heap_float(light+L_area_len_u),heap_float(light+L_area_len_v),true,true);
}
// background_num_possible_portals.
fn bgl_portal_count(P:vec3<f32>)->u32{
    var count=0u;for(var p=0u;p<data_word(D_integrator_num_portals);p++){if(bgl_portal_side(bgl_portal(p),P)){count++;}}return count;
}
// background_portal_pdf: (pdf, whether a portal is possible from P); `ignore` NONE ignores no portal.
fn bgl_portal_pdf(P:vec3<f32>,direction:vec3<f32>,ignore:u32)->vec2<f32>{
    var pdf=0.0;var possible=0u;var reachable=false;
    for(var p=0u;p<data_word(D_integrator_num_portals);p++){
        if(p==ignore){continue;}
        let light=bgl_portal(p);if(!bgl_portal_side(light,P)){continue;}
        reachable=true;possible++;
        let r=bgl_portal_region(light);let N=heap_vec3(light+L_area_dir);let invarea=heap_float(light+L_area_invarea);let round=invarea<0.0;
        // ray_quad_intersect(P, direction, 1e-4, FLT_MAX, ...).
        let t=rounded_divide(-rounded_add(rounded_dot(P,N),-rounded_dot(r.P,N)),rounded_dot(direction,N));
        if(!(t>1e-4&&t<bitcast<f32>(0x7f7fffffu))){continue;}
        let plane=rounded_add3(P,t*direction)-r.P;
        let u=rounded_dot(plane,bgl_div3(r.U,r.len_u));if(u< -0.5||u>0.5){continue;}
        let v=rounded_dot(plane,bgl_div3(r.V,r.len_v));if(v< -0.5||v>0.5){continue;}
        if(round&&rounded_add(u*u,v*v)>0.25){continue;}
        if(round){let to=bgl_normalize_len(r.P-P);pdf=rounded_add(pdf,abs(invarea)*bgl_area_to_solid_angle(N,-to.xyz,to.w));}
        else{pdf=rounded_add(pdf,area_rectangle(P,r,vec2<f32>(0),false).pdf);}
    }
    if(ignore!=NONE){possible++;}
    return vec2<f32>(select(0.0,rounded_divide(pdf,f32(possible)),possible>0u),select(0.0,1.0,reachable));
}
// background_portal_sample: the direction and its pdf (w), and the sampled portal.
struct BglPortalSample {s:vec4<f32>,portal:u32}
fn bgl_portal_sample(P:vec3<f32>,random:vec2<f32>,count:u32)->BglPortalSample{
    var out=BglPortalSample(vec4<f32>(0),NONE);
    var r=random;r.y=r.y*f32(count);var portal=i32(r.y);r.y=rounded_add(r.y,-f32(portal));
    for(var p=0u;p<data_word(D_integrator_num_portals);p++){
        let light=bgl_portal(p);if(!bgl_portal_side(light,P)){continue;}
        if(portal==0){
            let region=bgl_portal_region(light);let N=heap_vec3(light+L_area_dir);let invarea=heap_float(light+L_area_invarea);var pdf:f32;var D:vec3<f32>;
            if(invarea<0.0){
                // ellipse_sample(axis_u * len_u * 0.5, axis_v * len_v * 0.5, rand): sample_uniform_disk.
                let disk=emitter_disk(r);let position=rounded_add3(region.P,rounded_add3(((region.U*region.len_u)*0.5)*disk.x,((region.V*region.len_v)*0.5)*disk.y));
                let to=bgl_normalize_len(position-P);D=to.xyz;pdf=abs(invarea)*bgl_area_to_solid_angle(N,-D,to.w);
            }else{
                let rect=area_rectangle(P,region,r,true);pdf=rect.pdf;D=emitter_native_normalize(rect.P-P);
            }
            out.s=vec4<f32>(D,rounded_divide(pdf,f32(count)));out.portal=p;return out;
        }
        portal--;
    }
    return out;
}
// background_light_sample with the portal strategy (and the sun and map strategies, as bgl_sample).
fn bgl_portal_light_sample(P:vec3<f32>,random:vec2<f32>)->vec4<f32>{
    var portal_method=data_float(D_background_portal_weight);var sun_method=data_float(D_background_sun_weight);var map_method=data_float(D_background_map_weight);
    var count=0u;if(portal_method>0.0){count=bgl_portal_count(P);if(count==0u){portal_method=0.0;}}
    let total=rounded_add(rounded_add(portal_method,sun_method),map_method);
    if(total==0.0){
        let z=rounded_add(1.0,-2.0*random.x);let radial=rounded_sqrt(max(rounded_add(1.0,-z*z),0.0));let phi=BGL_2PI*random.y;let sc=native_phase_sincos(phi);
        return vec4<f32>(radial*sc.y,radial*sc.x,z,bgl_inv_4pi());
    }
    let fac=rounded_divide(1.0,total);portal_method*=fac;sun_method*=fac;map_method*=fac;
    let sun_cdf=rounded_add(portal_method,sun_method);
    var x=random.x;var s:vec4<f32>;var method=2u;var sampled=NONE;
//#if BACKGROUND_SUN
    let sun_light=bgl_sun();let omc=bgl_one_minus_cos(sun_light.w);
//#endif
    if(x<portal_method){
        method=0u;if(portal_method!=1.0){x=rounded_divide(x,portal_method);}
        let ps=bgl_portal_sample(P,vec2<f32>(x,random.y),count);s=ps.s;sampled=ps.portal;
    }else if(x<sun_cdf){
        method=1u;if(sun_method!=1.0){x=rounded_divide(rounded_add(x,-portal_method),sun_method);}
//#if BACKGROUND_SUN else         fail();return vec4<f32>(0);
        s=bgl_sun_sample(vec2<f32>(x,random.y),sun_light,omc);
//#endif
    }else{
        if(map_method!=1.0){x=rounded_divide(rounded_add(x,-sun_cdf),map_method);}
        s=bgl_map_sample(vec2<f32>(x,random.y));
    }
    if(method==1u){if(sun_method==1.0){return s;}s.w*=sun_method;}
    if(method==2u){if(map_method==1.0){return s;}s.w*=map_method;}
    // One portal pdf: the other portals of a portal sample, or every portal for the other strategies' MIS.
    var portal_pdf=0.0;
    if((method==0u&&count>1u)||(method!=0u&&portal_method!=0.0)){portal_pdf=bgl_portal_pdf(P,s.xyz,select(NONE,sampled,method==0u)).x;}
    if(method==0u){if(count>1u){s.w=rounded_add(s.w,portal_pdf);}if(portal_method==1.0){return s;}s.w*=portal_method;}
    else if(portal_method!=0.0){s.w=rounded_add(s.w,portal_method*portal_pdf);}
//#if BACKGROUND_SUN
    let pdfs=bgl_sun_map_pdfs(s.xyz,sun_light,omc,method!=2u&&map_method!=0.0);
    if(method!=1u&&sun_method!=0.0){s.w=rounded_add(s.w,sun_method*pdfs.x);}
    if(method!=2u&&map_method!=0.0){s.w=rounded_add(s.w,map_method*pdfs.y);}
//#endif
//#if !BACKGROUND_SUN
    if(method!=1u&&sun_method!=0.0){fail();return vec4<f32>(0);}
    if(method!=2u&&map_method!=0.0){s.w=rounded_add(s.w,map_method*bgl_map_pdf(s.xyz));}
//#endif
    return s;
}
// background_light_pdf with the portal strategy.
fn bgl_portal_light_pdf(P:vec3<f32>,direction:vec3<f32>)->f32{
    var portal_method=data_float(D_background_portal_weight);let sun=data_float(D_background_sun_weight);let map=data_float(D_background_map_weight);var portal_pdf=0.0;
    if(portal_method>0.0){let r=bgl_portal_pdf(P,direction,NONE);portal_pdf=r.x;if(r.y==0.0){portal_method=0.0;}}
    let total=rounded_add(rounded_add(portal_method,sun),map);if(total==0.0){return bgl_inv_4pi();}
    let fac=rounded_divide(1.0,total);let sun_method=sun*fac;let map_method=map*fac;
    var pdf=portal_pdf*(portal_method*fac);
//#if BACKGROUND_SUN
    let sun_light=bgl_sun();let pdfs=bgl_sun_map_pdfs(direction,sun_light,bgl_one_minus_cos(sun_light.w),map_method!=0.0);
    if(sun_method!=0.0){pdf=rounded_add(pdf,pdfs.x*sun_method);}
    if(map_method!=0.0){pdf=rounded_add(pdf,pdfs.y*map_method);}
//#endif
//#if !BACKGROUND_SUN
    if(sun_method!=0.0){fail();return 0.0;}
    if(map_method!=0.0){pdf=rounded_add(pdf,bgl_map_pdf(direction)*map_method);}
//#endif
    return pdf;
}
//#endif
//#endif
