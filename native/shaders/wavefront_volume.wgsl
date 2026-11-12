// SPDX-FileCopyrightText: 2011-2025 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Pinned Cycles homogeneous null-scattering integration, spectral distance
// sampling, primary-ray probability guidance and Henyey–Greenstein phase.
fn volume_rgbe(address:u32)->vec3<f32>{
    let bits=load_word(address);if((bits&0x7fffffffu)==0u){return vec3<f32>(0);}
    let e=bits>>24u;let factor=bitcast<f32>(((e&31u)+104u)<<23u);
    let v=vec3<f32>(f32(bits&255u),f32((bits>>8u)&255u),f32((bits>>16u)&255u))*factor;
    return select(v,-v,(vec3<u32>(e)&vec3<u32>(128u,64u,32u))!=vec3<u32>(0u));
}
fn volume_scatter_probability(index:u32,base:u32,sigma:vec3<f32>,transmittance:vec3<f32>)->vec3<f32>{
    let attenuation=vec3<f32>(1)-transmittance;if(state_half(S_path_bounce,index)!=0u){return attenuation;}
    let scattered=volume_rgbe(base+data_word(V_film_pass_volume_scatter_denoised)*4u);
    let total=scattered+volume_rgbe(base+data_word(V_film_pass_volume_transmit_denoised)*4u);var guided:vec3<f32>;
    if(all(total==vec3<f32>(0))){guided=select(vec3<f32>(0),vec3<f32>(0.5),sigma>vec3<f32>(0));}
    else{
        let count=heap_float(base+data_word(D_film_pass_volume_majorant_sample_count)*4u);var optical_depth=bitcast<f32>(0x7f7fffffu);
        if(count!=0.0){optical_depth=heap_float(base+data_word(D_film_pass_volume_majorant)*4u)/count;}
        let scale=max(attenuation.x,max(attenuation.y,attenuation.z))/(1.0-exp(-optical_depth));
        guided=clamp(svm_divide(scattered,total)*scale,vec3<f32>(0),vec3<f32>(1));
    }
    return attenuation*0.25+guided*0.75;
}
//#if VOLUME_PHASES
// volume_phase_get_g (the lobe's anisotropy for the ray differential widening): Fournier-Forand 1, Rayleigh 0.
fn vp_get_g(c:SvmClosure)->f32{
    if(c.kind==VP_CLOSURE_VOLUME_FOURNIER_FORAND_ID){return 1.0;}if(c.kind==VP_CLOSURE_VOLUME_RAYLEIGH_ID){return 0.0;}return c.roughness;
}
// phase_*_sample: the lobe's cosine, its pdf and phase_sample_direction (volume_phase's frame).
fn vp_phase_sample(direction:vec3<f32>,c:SvmClosure,random:vec2<f32>)->vec4<f32>{
    let cosine=vp_sample_cos(c,random.x);let pdf=vp_eval(c,cosine);
    let sine=rounded_sqrt(max(rounded_add(1.0,-cosine*cosine),0.0));let phi=6.283185307179586*random.y;
    let sc=native_phase_sincos(phi);let local=vec3<f32>(sine*sc.y,sine*sc.x,cosine);
    var tangent=vec3<f32>(direction.z-direction.y,direction.x-direction.z,direction.y-direction.x);
    if(direction.x==direction.y&&direction.x==direction.z){tangent=vec3<f32>(direction.z-direction.y,direction.x+direction.z,-direction.y-direction.x);}
    tangent=surface_native_normalize(tangent);let bitangent=rounded_cross(direction,tangent);
    return vec4<f32>(rounded_add3(rounded_add3(local.x*tangent,local.y*bitangent),local.z*direction),pdf);
}
//#endif
//#if FILM_DENOISING
// film_write_denoising_features_volume (film/denoising_passes.h) for a path with PATH_RAY_DENOISING_FEATURES: a scatter
// stops the features (the view direction as the normal), and the albedo pass takes the feature throughput times the
// volume albedo. Returns the path flag (written to the state as well).
fn volume_denoising_features(index:u32,base:u32,flag_in:u32,albedo:vec3<f32>,scatter:bool)->u32{
    var flag=flag_in;if((flag&DN_PATH_RAY_DENOISING_FEATURES)==0u){return flag;}
    let throughput=heap_vec3(state_address(S_path_denoising_feature_throughput,index,12u));
    let normal_pass=data_word(DN_D_pass_denoising_normal);let albedo_pass=data_word(DN_D_pass_denoising_albedo);
    if(scatter&&normal_pass!=PASS_NONE){
        flag&=~DN_PATH_RAY_DENOISING_FEATURES;state_u32(S_path_flag,index,flag);
        volume_atomic_add(base+normal_pass*4u,0.0);volume_atomic_add(base+(normal_pass+1u)*4u,0.0);volume_atomic_add(base+(normal_pass+2u)*4u,-1.0);
    }
    if(albedo_pass!=PASS_NONE){
        var value=throughput*albedo;value=select(value,vec3<f32>(0),(bitcast<vec3<u32>>(value)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
        volume_atomic_add(base+albedo_pass*4u,value.x);volume_atomic_add(base+(albedo_pass+1u)*4u,value.y);volume_atomic_add(base+(albedo_pass+2u)*4u,value.z);
    }
    return flag;
}
//#endif
//#if VOLUME_EMISSION
fn vl_film_add3(address:u32,value:vec3<f32>){volume_atomic_add(address,value.x);volume_atomic_add(address+4u,value.y);volume_atomic_add(address+8u,value.z);}
//#if VOLUME_LIGHT_PASSES
fn ve_film(base:u32,offset:u32,value:vec3<f32>){if(offset!=PASS_NONE){vl_film_add3(base+offset*4u,value);}}
// film_write_emission_or_background_pass of a volume's emission; the lightgroup is the last stack entry's object
// (object_lightgroup of the ShaderData the stack's evaluation left).
fn ve_emission_passes(index:u32,base:u32,value:vec3<f32>){
    if(data_word(VE_D_light_pass_flag)==0u){return;}let flag=state_word(S_path_flag,index);let bounce=state_half(S_path_bounce,index);
    var object=NONE;for(var slot=0u;slot<data_word(D_volume_stack_size);slot++){if(load_word(volume_stack_address(index,slot,VS_shader))==NONE){break;}object=load_word(volume_stack_address(index,slot,VS_object));}
    let group_offset=data_word(VE_D_pass_lightgroup);
//#if VOLUME_SHADOW_CATCHER
    // Light passes and light groups are not written for shadow catcher paths (the directly visible pass is).
    if(!sc_light_passes(flag)){if((flag&VE_PATH_RAY_ANY_PASS)==0u){ve_film(base,data_word(VE_D_pass_emission),value);}return;}
//#endif
    if(object!=NONE&&group_offset!=PASS_NONE){let group=load_word(table_at(G_objects,object,SIZE_objects)+VE_O_lightgroup);if(group!=NONE){ve_film(base,group_offset+3u*group,value);}}
    if((flag&VE_PATH_RAY_ANY_PASS)==0u){ve_film(base,data_word(VE_D_pass_emission),value);}
    else if(p(20u)!=0u&&(flag&VE_PATH_RAY_SURFACE_PASS)!=0u){
        let diffuse=heap_vec3(state_address(S_path_pass_diffuse_weight,index,12u));let glossy=heap_vec3(state_address(S_path_pass_glossy_weight,index,12u));
        ve_film(base,select(data_word(VE_D_pass_glossy_indirect),data_word(VE_D_pass_glossy_direct),bounce==1u),glossy*value);
        ve_film(base,select(data_word(VE_D_pass_transmission_indirect),data_word(VE_D_pass_transmission_direct),bounce==1u),(vec3<f32>(1)-diffuse-glossy)*value);
        ve_film(base,select(data_word(VE_D_pass_diffuse_indirect),data_word(VE_D_pass_diffuse_direct),bounce==1u),diffuse*value);
    }else if(p(20u)!=0u&&(flag&VE_PATH_RAY_VOLUME_PASS)!=0u){ve_film(base,select(data_word(VE_D_pass_volume_indirect),data_word(VE_D_pass_volume_direct),bounce==1u),value);}
}
//#endif
//#if LIGHT_LINKING
// light_link_object_match(light_link_receiver_forward, sd->object) before film_write_volume_emission: sd->object is the
// last stack entry's object (the stack's evaluation leaves it there). Every pair matches without KERNEL_FEATURE_LIGHT_LINKING.
fn vll_emission_visible(index:u32)->bool{
    if((data_word(D_kernel_features)&KF_LIGHT_LINKING)==0u){return true;}
    var object=NONE;for(var slot=0u;slot<data_word(D_volume_stack_size);slot++){if(load_word(volume_stack_address(index,slot,VS_shader))==NONE){break;}object=load_word(volume_stack_address(index,slot,VS_object));}
    let receiver=state_word(S_path_mis_ray_object,index);
    // The pinned kernel asserts both objects exist.
    if(receiver==NONE||object==NONE){fail();return false;}
    let light_set=load_word(table_at(G_objects,receiver,SIZE_objects)+VLL_O_receiver_light_set);if(light_set>=VLL_LINK_SET_MAX){fail();return false;}
    let word=load_word(table_at(G_objects,object,SIZE_objects)+VLL_O_light_set_membership+select(0u,4u,light_set>=32u));
    return ((word>>(light_set&31u))&1u)!=0u;
}
//#endif
fn vl_volume_emission(index:u32,base:u32,r:SvmResult,distance:f32){
//#if LIGHT_LINKING
    if(!vll_emission_visible(index)){return;}
//#endif
    var emission=r.emission;
    if((r.runtime_flag&SR_EXTINCTION)!=0u){
        let depth=r.transparency*distance;
        emission*=select(distance*rounded_add3(vec3<f32>(1),-0.5*depth),(vec3<f32>(1)-exp(-depth))/r.transparency,depth>vec3<f32>(1e-5));
    }else{emission*=distance;}
    var value=heap_vec3(state_address(S_path_throughput,index,12u))*emission;
    if(all(value==vec3<f32>(0))){return;}
    value=select(value,vec3<f32>(0),(bitcast<vec3<u32>>(value)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
    let bounce=state_half(S_path_bounce,index);let limit=select(data_float(VE_D_sample_clamp_direct),data_float(VE_D_sample_clamp_indirect),i32(bounce)-1>0);
    let total=abs(value.x)+abs(value.y)+abs(value.z);if(total>limit){value*=limit/total;}
//#if VOLUME_SHADOW_CATCHER
    // film_write_combined_pass: the shadow catcher's matte, its pass (then nothing else).
    if(!sc_combined(state_word(S_path_flag,index),base,value,select(PASS_NONE,data_word(VE_D_pass_combined),(data_word(VE_D_light_pass_flag)&VE_MASK_COMBINED)!=0u))){
//#endif
//#if !VOLUME_SHADOW_CATCHER
    if((data_word(VE_D_light_pass_flag)&VE_MASK_COMBINED)!=0u){vl_film_add3(base+data_word(VE_D_pass_combined)*4u,value);}
//#endif
    if(data_word(VE_D_pass_adaptive_aux_buffer)!=PASS_NONE&&(countOneBits(state_word(S_path_sample,index)&0xaaaaaaaau)&1u)!=0u){vl_film_add3(base+data_word(VE_D_pass_adaptive_aux_buffer)*4u,value*2.0);}
    volume_guidance_pass(index,base,value);
//#if VOLUME_SHADOW_CATCHER
    }
//#endif
//#if VOLUME_LIGHT_PASSES
    ve_emission_passes(index,base,value);
//#endif
}
//#endif
//#if VL_SAMPLING
// volume_sample_channel: (the channel, the rescaled random number); its pdf is vl_channel_pdf.
fn vl_channel_pdf(albedo:vec3<f32>,throughput:vec3<f32>)->vec3<f32>{
    let weights=abs(throughput*albedo);let sum=weights.x+weights.y+weights.z;
    if(rounded_add(1.0,-sum)<1.0){return weights/sum;}return vec3<f32>(1.0/3.0);
}
fn vl_sample_channel(albedo:vec3<f32>,throughput:vec3<f32>,rand:f32)->vec2<f32>{
    let pdf=vl_channel_pdf(albedo,throughput);var sum=0.0;
    for(var c=0u;c<3u;c++){if(rand<sum+pdf[c]){return vec2<f32>(f32(c),(rand-sum)/pdf[c]);}sum+=pdf[c];}
    return vec2<f32>(2.0,rand);
}
// pdf_exponential_distribution.
fn vl_exponential_pdf(x:f32,lambda:vec3<f32>,t:vec2<f32>)->vec3<f32>{
    let attenuation=exp(-lambda*t.x)-exp(-lambda*t.y);
    return svm_divide(lambda*exp(-lambda*clamp(x,t.x,t.y)),attenuation);
}
//#if VOLUME_LIGHT
fn vl_expf(x:f32)->f32{let e=vp_exp_parts(vec2<f32>(x,0.0));return ldexp(e.x,i32(e.y));}
fn vl_logf(x:f32)->f32{let l=vp_log_parts(x);return rounded_add(l.x,l.y);}
// volume_scatter_probability_homogeneous: expf(-optical_depth) comes from the caller's exponentials; quotients are
// correctly rounded. The defensive mix(attenuation, guided, 0.75) is util/math_base.h mix, a + t * (b - a), in that order:
// a last bit of this probability moves the scatter distance, which grazing light samples and the guiding buffers amplify.
fn vl_scatter_probability(index:u32,base:u32,sigma:vec3<f32>,transmittance:vec3<f32>,majorant_transmittance:f32)->vec3<f32>{
    let attenuation=vec3<f32>(1)-transmittance;if(state_half(S_path_bounce,index)!=0u){return attenuation;}
    let scattered=volume_rgbe(base+data_word(V_film_pass_volume_scatter_denoised)*4u);
    let total=scattered+volume_rgbe(base+data_word(V_film_pass_volume_transmit_denoised)*4u);var guided:vec3<f32>;
    if(all(total==vec3<f32>(0))){guided=select(vec3<f32>(0),vec3<f32>(0.5),sigma>vec3<f32>(0));}
    else{
        let scale=rounded_divide(max(attenuation.x,max(attenuation.y,attenuation.z)),1.0-majorant_transmittance);
        guided=clamp(svm_divide(scattered,total)*scale,vec3<f32>(0),vec3<f32>(1));
    }
    return rounded_add3(attenuation,0.75*rounded_add3(guided,-attenuation));
}
//#endif
fn vl_power(a:f32,b:f32)->f32{let a2=a*a;return rounded_divide(a2,rounded_add(a2,b*b));}
// tanf, correctly rounded (the camera stage's cp_tan): the double-float sine and cosine of the reduced angle.
fn vl_tan(value:f32)->f32{
    let swap=abs(value)>0.7853981633974483;var x=vec2<f32>(value,0.0);
    if(swap){x=vec2<f32>(rounded_add(1.5707963705062866,-abs(value)),-4.3711390001862426e-8);}
    let sc=trig_sincos_series(x);
    var n=sc.xy;var d=sc.zw;if(swap){n=sc.zw;d=sc.xy;}
    let q=rounded_divide(n.x,d.x);let product=trig_product(q,d.x);
    let residual=rounded_add(rounded_add(rounded_add(n.x,-product.x),-product.y),rounded_add(n.y,-(q*d.y)));
    let t=rounded_add(q,rounded_divide(residual,d.x));
    return select(t,select(t,-t,value<0.0),swap);
}
// The equiangular distance D of volume_equiangular_sample/_pdf, and delta: (delta, D).
fn vl_equiangular_frame(P:vec3<f32>,D:vec3<f32>,point:vec3<f32>)->vec2<f32>{
    let to=rounded_add3(point,-P);let delta=rounded_dot(to,D);let v=rounded_add3(to,-(D*delta));
    return vec2<f32>(delta,rounded_sqrt(rounded_dot(v,v)));
}
// volume_equiangular_sample: (t, pdf).
fn vl_equiangular_sample(P:vec3<f32>,D:vec3<f32>,point:vec3<f32>,t:vec2<f32>,xi:f32)->vec2<f32>{
    let frame=vl_equiangular_frame(P,D,point);let delta=frame.x;let d=frame.y;if(d==0.0){return vec2<f32>(0);}
    let theta_a=trig_atan2(rounded_add(t.x,-delta),d);let theta_b=trig_atan2(rounded_add(t.y,-delta),d);let theta_d=rounded_add(theta_b,-theta_a);
    if(theta_d<1e-6){let length=rounded_add(t.y,-t.x);return vec2<f32>(rounded_add(t.x,xi*length),select(0.0,rounded_divide(1.0,length),length!=0.0));}
    let t_=d*vl_tan(rounded_add(xi*theta_b,rounded_add(1.0,-xi)*theta_a));
    let pdf=rounded_divide(d,theta_d*rounded_add(d*d,t_*t_));
    return vec2<f32>(clamp(rounded_add(delta,t_),t.x,t.y),pdf);
}
fn vl_equiangular_pdf(P:vec3<f32>,D:vec3<f32>,point:vec3<f32>,t:vec2<f32>,sample_t:f32)->f32{
    let frame=vl_equiangular_frame(P,D,point);let delta=frame.x;let d=frame.y;if(d==0.0){return 0.0;}
    let theta_a=trig_atan2(rounded_add(t.x,-delta),d);let theta_b=trig_atan2(rounded_add(t.y,-delta),d);let theta_d=rounded_add(theta_b,-theta_a);
    if(theta_d<1e-6){let length=rounded_add(t.y,-t.x);return select(0.0,rounded_divide(1.0,length),length!=0.0);}
    let t_=rounded_add(sample_t,-delta);
    return rounded_divide(d,theta_d*rounded_add(d*d,t_*t_));
}
//#endif
fn volume_phase(direction:vec3<f32>,g:f32,random:vec2<f32>)->vec4<f32>{
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
//#if SHADOW_LINKING else fn volume_finish(index:u32,next:u32){
fn volume_finish(index:u32,requested:u32){
    // integrator_shade_volume: a phase scatter (the only finish towards intersect_closest) looks for its dedicated lights first.
    // shadow_linking_scene_need_shadow_ray: only while the scene has KERNEL_FEATURE_SHADOW_LINKING.
    let next=select(requested,DL_K_INTERSECT,requested==K_CLOSEST&&(data_word(D_kernel_features)&DL_FEATURE_SHADOW_LINKING)!=0u);
//#endif
    if(next==0u){volume_write_depth(index);}
//#if VOLUME_RAY_MARCHING else     let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[counter/4u+DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME],1u);
    let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[counter/4u+vr_volume_kernel()],1u);
//#endif
    if(next!=0u){atomicAdd(&heap[counter/4u+next],1u);}state_u16(S_path_queued_kernel,index,next);
}
//#if VOLUME_SHADOW_CATCHER
// integrator_split_shadow_catcher up to the state copy (wavefront_closest.wgsl sc_split_mark): the copy goes to the
// background first, or re-inits its volume stack when the stack is not empty, or shades the surface.
fn vl_split_mark(index:u32,object:u32,shader:u32,next:u32){
    if(!sc_enabled()){return;}
    let object_flag=load_word(table_at(G_object_flag,object,4u));
    if((object_flag&SC_OBJECT)==0u||(object_flag&SC_HOLDOUT_MASK)!=0u){return;}
    let flag=state_word(S_path_flag,index);
    if((flag&SC_TRANSPARENT_BACKGROUND)==0u||(flag&SC_PATH_HIT)!=0u){return;}
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_path_render_pixel_index,index);
    if(pixel>=p(11u)/stride||p(29u)==0u||shader>0xffffffu){fail();return;}let base=p(4u)+pixel*stride*4u;
    let throughput=heap_vec3(state_address(S_path_throughput,index,12u));
    sc_film_add(base+data_word(SC_D_pass_shadow_catcher_sample_count)*4u,1.0);
    sc_film_add(base+(data_word(SC_D_pass_shadow_catcher_matte)+3u)*4u,(throughput.x+throughput.y+throughput.z)*(1.0/3.0));
    state_u32(S_path_flag,index,flag|SC_PATH_HIT);
    var kernel=next;
    if(data_word(SC_D_pass_background)!=PASS_NONE&&data_word(SC_D_background_transparent)==0u){kernel=SC_K_BACKGROUND;}
    else if(load_word(volume_stack_address(index,0u,VS_shader))!=NONE){kernel=SC_K_VOLUME_STACK;}
    store_word(p(29u)+index*8u,0x80000000u|(kernel<<24u)|shader);
}
//#endif
fn volume_after(index:u32){
    let prim=state_word(S_isect_prim,index);var next=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;
    if(prim!=NONE){
        if((state_word(S_isect_type,index)&PRIMITIVE_LAMP)!=0u){next=DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD;}
        else{
//#if !CURVES
//#if POINTS else             let shader=load_word(table_at(G_tri_shader,prim,SIZE_tri_shader))&SHADER_MASK;
            let shader=point_or_triangle_shader(prim,state_word(S_isect_type,index))&SHADER_MASK;
//#endif
//#endif
//#if CURVES
            var shader=0u;
            if((state_word(S_isect_type,index)&PRIMITIVE_CURVE)!=0u){shader=load_word(table_at(G_curves,prim,16u))&SHADER_MASK;}
//#if POINTS else             else{shader=load_word(table_at(G_tri_shader,prim,SIZE_tri_shader))&SHADER_MASK;}
            else{shader=point_or_triangle_shader(prim,state_word(S_isect_type,index))&SHADER_MASK;}
//#endif
//#endif
            let flags=load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags);
//#if MNEE_VOLUME
            // integrator_intersect_next_kernel_after_volume: a caustic receiver goes to INTERSECT_MNEE, unsorted.
            if(data_word(V_D_use_caustics)!=0u&&(load_word(table_at(G_object_flag,state_word(S_isect_object,index),4u))&V_SD_OBJECT_CAUSTICS_RECEIVER)!=0u){
                volume_finish(index,V_K_INTERSECT_MNEE);return;}
//#endif
            next=select(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,(flags&SD_HAS_RAYTRACE)!=0u);
            let divisor=load_word(p(7u)+S_sort_partition_divisor);let key=shader+data_word(D_max_shaders)*(index/divisor);
            let limit=select(p(15u),p(16u),next==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE);if(key>=limit){fail();return;}
            let counter=load_word(p(7u)+S_sort_key_counter+next*POINTER_SIZE);atomicAdd(&heap[(counter+key*4u)/4u],1u);state_u32(S_path_shader_sort_key,index,key);
//#if VOLUME_SHADOW_CATCHER
            // integrator_split_shadow_catcher (integrator_intersect_next_kernel_after_volume).
            vl_split_mark(index,state_word(S_isect_object,index),shader,next);if(load_word(p(17u))!=0u){return;}
//#endif
        }
    }else if((data_word(D_integrator_filter_closures)&FILTER_CLOSURE_DIRECT_LIGHT)!=0u&&state_half(S_path_bounce,index)==1u){next=0u;}
    volume_finish(index,next);
}
// shader_merge_volume_closures: the merge and its memmove work on the closures' storage, with the same comparisons and
// additions in the same order. With whole closures as function-scope values the Adreno 730 (EV031.36.08.33) fails to
// link shade_volume, shade_volume_heterogeneous and shade_volume_ray_marching once a volume shader's noise handler is in
// the module (AdrenoVK-0 "Failed to link shaders", reported as a lost device; native/DEVICE.md).
fn volume_closure_add(a:u32,b:u32){
    store_word(a,bitcast<u32>(heap_float(a)+heap_float(b)));store_word(a+4u,bitcast<u32>(heap_float(a+4u)+heap_float(b+4u)));
    store_word(a+8u,bitcast<u32>(heap_float(a+8u)+heap_float(b+8u)));store_word(a+12u,bitcast<u32>(heap_float(a+12u)+heap_float(b+12u)));
}
fn volume_closure_copy(dst:u32,src:u32){for(var word=0u;word<SVM_CLOSURE_BYTES/4u;word++){store_word(dst+word*4u,load_word(src+word*4u));}}
@compute @workgroup_size(64) fn shade_volume(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME){fail();return;}
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_path_render_pixel_index,index);if(pixel>=p(11u)/stride){fail();return;}let base=p(4u)+pixel*stride*4u;
    let prim=state_word(S_isect_prim,index);var flag=state_word(S_path_flag,index);let bounce=state_half(S_path_bounce,index);
    if(bounce==0u){flag|=PATH_RAY_VOLUME_PRIMARY_TRANSMIT;}
//#if PHASE_STAGE
    store_word(p(24u)+id.x*VL_RECORD_BYTES+64u,0u);
//#endif
//#if VOLUME_HETEROGENEOUS
    // Paths through a heterogeneous volume are shade_volume_heterogeneous's.
    store_word(p(24u)+id.x*VL_RECORD_BYTES+80u,0u);if(vh_is_heterogeneous(index)){return;}
//#endif
    let count=volume_stack_count(index);if(count==NONE){return;}
    // Native cleanup discards finite-object media on a missed scene ray.
    let entries=select(count,select(0u,1u,data_word(D_background_volume_shader)!=NONE),prim==NONE);
    if(entries==0u){if(prim==NONE){store_word(volume_stack_address(index,0u,VS_shader),NONE);}state_u32(S_path_flag,index,flag);volume_after(index);return;}
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));let tmin=heap_float(state_address(S_ray_tmin,index,4u));
    var tmax=bitcast<f32>(0x7f7fffffu);if(prim!=NONE){tmax=heap_float(state_address(S_isect_t,index,4u));}let distance=tmax-tmin;
    var sd:SurfaceData;sd.P=origin+direction*tmin;sd.N=-direction;sd.Ng=-direction;sd.wi=-direction;sd.prim=NONE;sd.primitive_type=V_PRIMITIVE_VOLUME;
    sd.time=heap_float(state_address(S_ray_time,index,4u));sd.runtime_flag=SR_IS_VOLUME_SHADER_EVAL;
    var r:SvmResult;r.runtime_flag=sd.runtime_flag;r.remaining=data_word(D_max_closures);r.closure_base=p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES;
    for(var slot=0u;slot<entries;slot++){
        sd.object_id=load_word(volume_stack_address(index,slot,VS_object));sd.shader=load_word(volume_stack_address(index,slot,VS_shader));
        sd.shader_flag=load_word(table_at(G_shaders,sd.shader&SHADER_MASK,SIZE_shaders)+SH_flags);sd.object_flag=0u;
        if(sd.object_id!=NONE){sd.object_flag=load_word(table_at(G_object_flag,sd.object_id,SIZE_object_flag));}
//#if VOLUME_MOTION
        // volume_shader_eval_entry: shader_setup_object_transforms at the ray's time for a moving stack object.
        if((sd.object_flag&SD_OBJECT_MOTION)!=0u){sd.motion_tfm=motion_object(sd.object_id,sd.time);sd.motion_itfm=motion_inverse(sd.motion_tfm);}
//#endif
        sd.runtime_flag=r.runtime_flag;r=svm_evaluate_seeded(index,sd,r.remaining,r.closure_base,true,r);if(r.error!=0u){return;}
        if(slot>0u){
            for(var a=0u;a<r.count;a++){let left=r.closure_base+a*SVM_CLOSURE_BYTES;var b=a+1u;
                while(b<r.count){let right=r.closure_base+b*SVM_CLOSURE_BYTES;
//#if VOLUME_PHASES else                     if(load_word(left+28u)==load_word(right+28u)&&heap_float(left+32u)==heap_float(right+32u)){volume_closure_add(left,right);
                    if(vp_equal(left,right)){volume_closure_add(left,right);
//#endif
                        for(var j=b;j+1u<r.count;j++){
                            let current=r.closure_base+j*SVM_CLOSURE_BYTES;let moved=current+SVM_CLOSURE_BYTES;
                            // Native generic ShaderClosure assignment leaves HG anisotropy
                            // in C++ padding unchanged on the reviewed CPU build. Do not
                            // silently reproduce that mixed-phase transport error.
//#if VOLUME_PHASES else                             if(heap_float(current+32u)!=heap_float(moved+32u)){atomicOr(&heap[p(17u)/4u],16u);return;}
                            if(heap_float(current+32u)!=heap_float(moved+32u)||heap_float(current+36u)!=heap_float(moved+36u)||heap_float(current+40u)!=heap_float(moved+40u)||heap_float(current+C_ior)!=heap_float(moved+C_ior)){atomicOr(&heap[p(17u)/4u],16u);return;}
//#endif
                            volume_closure_copy(current,moved);
                        }r.count--;
                    }else{b++;}
                }
            }
        }
    }
//#if VOLUME_EMISSION else     if((r.runtime_flag&SR_EMISSION)!=0u||r.count>8u){fail();return;}
    if(r.count>8u){fail();return;}
//#endif
//#if VOLUME_PHASES else     let sigma=r.transparency;var scatter=vec3<f32>(0);for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(c.kind!=CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID){fail();return;}scatter+=c.weight;}
    let sigma=r.transparency;var scatter=vec3<f32>(0);for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(c.kind!=CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID&&!vp_scatter_kind(c.kind)){fail();return;}scatter+=c.weight;}
//#endif
    if(any(sigma<vec3<f32>(0))||any(scatter<vec3<f32>(0))){fail();return;}
    state_u32(S_path_flag,index,flag);
    if(prim==NONE){store_word(volume_stack_address(index,entries,VS_shader),NONE);}
    if(bounce==0u){state_f32(S_path_optical_depth,index,heap_float(state_address(S_path_optical_depth,index,4u))+max(sigma.x,max(sigma.y,sigma.z))*distance);}
//#if VOLUME_EMISSION
    // volume_emission_integrate and film_write_volume_emission (homogeneous: the whole segment).
    if((r.runtime_flag&SR_EMISSION)!=0u){vl_volume_emission(index,base,r,distance);}
//#endif
    let throughput=heap_vec3(state_address(S_path_throughput,index,12u));
//#if !VOLUME_LIGHT
    let transmittance=exp(-sigma*distance);
//#endif
    var indirect=throughput;var sampled=false;var sampled_t=0.0;
//#if FILM_DENOISING
    // vstate.albedo of volume_integrate_homogeneous (zero without a scatter estimate).
    var denoising_albedo=vec3<f32>(0);
//#endif
//#if VOLUME_LIGHT
    // Direct light (volume_integrate_null_scattering): the method and segment of shade_volume_segment, the
    // MIS split of the scatter random number, the equiangular sample in advance (volume_integrate_result_init).
    let vl=p(24u)+id.x*VL_RECORD_BYTES;var method=load_word(vl);let eq_point=heap_vec3(vl+4u);let segment=vec2<f32>(heap_float(vl+16u),heap_float(vl+20u));
    let rng_sample=state_word(S_path_sample,index);let rng_pixel=state_word(S_path_rng_pixel,index);let rng_offset=state_half(S_path_rng_offset,index);
//#if VOLUME_SAMPLING_PATTERN else     var rscatter=native_rng(rng_sample,rng_pixel,rng_offset+V_PRNG_VOLUME_SCATTER_DISTANCE).x;let use_mis=method==3u;
    var rscatter=native_rng_pattern(rng_sample,rng_pixel,rng_offset+V_PRNG_VOLUME_SCATTER_DISTANCE,1u).x;let use_mis=method==3u;
//#endif
    if(use_mis){if(rscatter<0.5){method=1u;rscatter*=2.0;}else{rscatter=(rscatter-0.5)*2.0;method=2u;}}
    var direct=select(vec3<f32>(0),throughput,method!=0u);var direct_scatter=false;var direct_t=0.0;var equiangular_pdf=0.0;var distance_pdf=0.0;
    if(method==2u){let s=vl_equiangular_sample(origin,direction,eq_point,segment,rscatter);direct_t=s.x;equiangular_pdf=s.y;}
    // Valid segment relative to tmin; the exponentials the sampling decisions read, correctly rounded with one body
    // (vl_expf): a last-bit difference in a scatter distance moves the light sample a grazing area light or a
    // near-delta lobe then amplifies past the practical bound. expf(-sigma t) of the throughputs needs no such care.
    let range=vec2<f32>(segment.x-tmin,segment.y-tmin);let depth_count=heap_float(base+data_word(D_film_pass_volume_majorant_sample_count)*4u);
    let guide_depth=select(bitcast<f32>(0x7f7fffffu),rounded_divide(heap_float(base+data_word(D_film_pass_volume_majorant)*4u),depth_count),depth_count!=0.0);
    let exponents=array<f32,7>(-sigma.x*distance,-sigma.y*distance,-sigma.z*distance,sigma.x*(range.x-range.y),sigma.y*(range.x-range.y),sigma.z*(range.x-range.y),-guide_depth);
    var exponentials=array<f32,7>(0.0,0.0,0.0,0.0,0.0,0.0,0.0);
    for(var k=0u;k<7u;k++){exponentials[k]=vl_expf(exponents[k]);}
    let transmittance=vec3<f32>(exponentials[0],exponentials[1],exponentials[2]);
    if((flag&PATH_RAY_TERMINATE)!=0u||all(scatter==vec3<f32>(0))){indirect*=transmittance;}
    else{
//#if VOLUME_SAMPLING_PATTERN else         var rchannel=native_rng(rng_sample,rng_pixel,rng_offset+V_PRNG_VOLUME_RESERVOIR).x;
        var rchannel=native_rng_pattern(rng_sample,rng_pixel,rng_offset+V_PRNG_VOLUME_RESERVOIR,1u).x;
//#endif
        let albedo=svm_divide(scatter,sigma)*(vec3<f32>(1)-transmittance)*throughput;
//#if FILM_DENOISING
        denoising_albedo=albedo;
//#endif
        let pick=vl_sample_channel(albedo+transmittance,throughput,rchannel);rchannel=pick.y;let channel=u32(pick.x);let channel_pdf=vl_channel_pdf(albedo+transmittance,throughput);
        let probability=vl_scatter_probability(index,base,sigma,transmittance,exponentials[6]);
        // logf of the two distance samples (sample_exponential_distribution), one body (vl_logf).
        var log_inputs=vec2<f32>(1.0);let indirect_sample=rscatter<probability[channel];
        if(indirect_sample){rscatter=rounded_divide(rscatter,probability[channel]);log_inputs.x=rounded_add(1.0,-rscatter*(1.0-transmittance[channel]));}
        else{rscatter=rounded_divide(rscatter-probability[channel],1.0-probability[channel]);}
        var pick2=vec2<f32>(0.0);var direct_pdf=vec3<f32>(0);var lambda=0.0;
        if(method!=0u){
            pick2=vl_sample_channel(albedo,throughput,rchannel);direct_pdf=vl_channel_pdf(albedo,throughput);lambda=sigma[u32(pick2.x)];
            if(method==1u){log_inputs.y=rounded_add(1.0,-rscatter*(1.0-exponentials[3u+u32(pick2.x)]));}
        }
        var logs=vec2<f32>(0.0);for(var k=0u;k<2u;k++){logs[k]=vl_logf(log_inputs[k]);}
        if(indirect_sample){
            sampled=distance>0.0;
            let dt=clamp(-rounded_divide(logs.x,sigma[channel]),0.0,distance);sampled_t=tmin+dt;
            let attenuation=exp(-sigma*dt);let scatter_pdf=svm_divide(sigma*attenuation,vec3<f32>(1)-transmittance);
            let pdf=dot(scatter_pdf*probability,channel_pdf);indirect*=svm_divide(scatter*attenuation,vec3<f32>(pdf));
        }else{let pdf=dot(vec3<f32>(1)-probability,channel_pdf);indirect*=svm_divide(transmittance,vec3<f32>(pdf));}
        if(method!=0u){
            // Direct scatter inside the valid ray segment (relative to the ray's tmin).
            direct_scatter=range.x<range.y;
            if(method==1u){
                let dt=clamp(range.x-rounded_divide(logs.y,lambda),range.x,range.y);direct_t=tmin+dt;
                distance_pdf=dot(vl_exponential_pdf(dt,sigma,range),direct_pdf);
                direct*=svm_divide(scatter*exp(-sigma*dt),vec3<f32>(distance_pdf));
            }else{
                let dt=direct_t-tmin;direct*=svm_divide(scatter*exp(-sigma*dt),vec3<f32>(equiangular_pdf));
                if(use_mis){distance_pdf=dot(vl_exponential_pdf(dt,sigma,range),direct_pdf);}
            }
        }
    }
    // volume_direct_scatter_mis.
    if(use_mis&&direct_scatter){
        var weight:f32;
        if(method==1u){weight=vl_power(distance_pdf,vl_equiangular_pdf(origin,direction,eq_point,segment,direct_t));}else{weight=vl_power(equiangular_pdf,distance_pdf);}
        direct*=2.0*weight;
    }
//#endif
//#if !VOLUME_LIGHT
    if((flag&PATH_RAY_TERMINATE)!=0u||all(scatter==vec3<f32>(0))){indirect*=transmittance;}
    else{
        let sample_id=state_word(S_path_sample,index);let pixel_rng=state_word(S_path_rng_pixel,index);let offset=state_half(S_path_rng_offset,index);
        let albedo=svm_divide(scatter,sigma)*(vec3<f32>(1)-transmittance)*throughput;
//#if FILM_DENOISING
        denoising_albedo=albedo;
//#endif
        let weights=abs(throughput*(albedo+transmittance));let sum=weights.x+weights.y+weights.z;var channel_pdf=vec3<f32>(1.0/3.0);
        if(rounded_add(1.0,-sum)<1.0){channel_pdf=weights/sum;}
//#if VOLUME_SAMPLING_PATTERN else         let rchannel=native_rng(sample_id,pixel_rng,offset+V_PRNG_VOLUME_RESERVOIR).x;var channel=2u;var partial=0.0;
        let rchannel=native_rng_pattern(sample_id,pixel_rng,offset+V_PRNG_VOLUME_RESERVOIR,1u).x;var channel=2u;var partial=0.0;
//#endif
        for(var c=0u;c<3u;c++){if(rchannel<partial+channel_pdf[c]){channel=c;break;}partial+=channel_pdf[c];}
//#if VOLUME_SAMPLING_PATTERN else         let probability=volume_scatter_probability(index,base,sigma,transmittance);var random=native_rng(sample_id,pixel_rng,offset+V_PRNG_VOLUME_SCATTER_DISTANCE).x;
        let probability=volume_scatter_probability(index,base,sigma,transmittance);var random=native_rng_pattern(sample_id,pixel_rng,offset+V_PRNG_VOLUME_SCATTER_DISTANCE,1u).x;
//#endif
        if(random<probability[channel]){
            random/=probability[channel];sampled=distance>0.0;
            let dt=clamp(-log(1.0-random*(1.0-exp(-sigma[channel]*distance)))/sigma[channel],0.0,distance);sampled_t=tmin+dt;
            let attenuation=exp(-sigma*dt);let distance_pdf=svm_divide(sigma*attenuation,vec3<f32>(1)-transmittance);
            let pdf=dot(distance_pdf*probability,channel_pdf);indirect*=svm_divide(scatter*attenuation,vec3<f32>(pdf));
        }else{let pdf=dot(vec3<f32>(1)-probability,channel_pdf);indirect*=svm_divide(transmittance,vec3<f32>(pdf));}
    }
//#endif
//#if FILM_DENOISING
    flag=volume_denoising_features(index,base,flag,denoising_albedo,sampled);
//#endif
    let continuation=select(heap_float(state_address(S_path_continuation_probability,index,4u)),0.0,(flag&PATH_RAY_TERMINATE_IN_NEXT_VOLUME)!=0u);
    if(continuation==0.0){volume_finish(index,0u);return;}
//#if VOLUME_LIGHT
    // integrate_volume_direct_light runs in shade_volume_light (surface module), before the indirect scatter
    // changes the path state: this entry records the scatter position and throughput and, when the path
    // also scatters, leaves the phase scatter to that entry as well.
    if(direct_scatter){
        let direct_P=rounded_add3(origin,direct_t*direction);let weight=direct/continuation;
        store_word(vl+36u,bitcast<u32>(direct_P.x));store_word(vl+40u,bitcast<u32>(direct_P.y));store_word(vl+44u,bitcast<u32>(direct_P.z));
        store_word(vl+48u,bitcast<u32>(weight.x));store_word(vl+52u,bitcast<u32>(weight.y));store_word(vl+56u,bitcast<u32>(weight.z));
        store_word(vl+60u,r.count);store_word(vl+32u,1u);
    }
//#endif
    if(sampled){indirect/=continuation;}state_vec3(S_path_throughput,index,indirect);
    if(!sampled){volume_after(index);return;}
//#if PHASE_STAGE
    // The phase scatter runs in shade_volume_phase (after shade_volume_light's direct light when there is one):
    // record words 60 closure count, 64 pending, 68 sampled t, 72 object.
    let vp=p(24u)+id.x*VL_RECORD_BYTES;
    store_word(vp+60u,r.count);store_word(vp+68u,bitcast<u32>(sampled_t));store_word(vp+72u,sd.object_id);store_word(vp+64u,1u);
//#endif
//#if !PHASE_STAGE
//#if VOLUME_LIGHT
    if(direct_scatter){store_word(vl+60u,r.count);store_word(vl+68u,bitcast<u32>(sampled_t));store_word(vl+72u,sd.object_id);store_word(vl+64u,1u);return;}
//#endif
//#if VOLUME_SAMPLING_PATTERN else     var random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+V_PRNG_VOLUME_PHASE).xy;
    var random=native_rng_pattern(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+V_PRNG_VOLUME_PHASE,2u).xy;
//#endif
    var selected=0u;var sum=svm_closure_read(r,0u).sample_weight;
    for(var i=1u;i<r.count;i++){let weight=svm_closure_read(r,i).sample_weight;sum+=weight;let threshold=weight/sum;
        if(random.x<threshold){selected=i;random.x/=threshold;}else{random.x=(random.x-threshold)/(1.0-threshold);}}
//#if VOLUME_PHASES else     let g=svm_closure_read(r,selected).roughness;let phase=volume_phase(direction,g,random);if(phase.w==0.0){volume_finish(index,0u);return;}
    let closure=svm_closure_read(r,selected);let g=vp_get_g(closure);var phase:vec4<f32>;
    if(closure.kind==CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID){phase=volume_phase(direction,closure.roughness,random);}else{phase=vp_phase_sample(direction,closure,random);}
    if(phase.w==0.0){volume_finish(index,0u);return;}
//#endif
//#if VOLUME_LIGHT else     let position=origin+sampled_t*direction;state_vec3(S_ray_P,index,position);state_vec3(S_ray_D,index,surface_native_normalize(phase.xyz));
    let position=rounded_add3(origin,sampled_t*direction);state_vec3(S_ray_P,index,position);state_vec3(S_ray_D,index,surface_native_normalize(phase.xyz));
//#endif
//#if VOLUME_LIGHT_TREE
    // ray->tmax - ray->tmin of the segment.
    if(data_word(VL_D_use_light_tree)!=0u){state_f32(S_ray_previous_dt,index,select(bitcast<f32>(0x7f7fffffu),heap_float(state_address(S_isect_t,index,4u)),state_word(S_isect_prim,index)!=NONE)-tmin);}
//#endif
    state_f32(S_ray_tmin,index,0.0);state_u32(S_ray_tmax,index,0x7f7fffffu);state_f32(S_ray_dP,index,0.0);state_f32(S_ray_dD,index,max(heap_float(state_address(S_ray_dD,index,4u)),1.0-abs(g)));
    state_u32(S_isect_prim,index,NONE);state_u32(S_isect_object,index,sd.object_id);
//#if LIGHT_LINKING
    if((data_word(D_kernel_features)&KF_LIGHT_LINKING)!=0u){state_u32(S_path_mis_ray_object,index,sd.object_id);}
//#endif
    if(p(20u)!=0u&&bounce==0u){state_vec3(S_path_pass_diffuse_weight,index,vec3<f32>(1));state_vec3(S_path_pass_glossy_weight,index,vec3<f32>(0));}
//#if VOLUME_LIGHT else     state_f32(S_path_mis_ray_pdf,index,phase.w);state_vec3(S_path_mis_origin_n,index,position-(origin+direction*tmin));
    state_f32(S_path_mis_ray_pdf,index,phase.w);state_vec3(S_path_mis_origin_n,index,position-rounded_add3(origin,direction*tmin));
//#endif
    state_f32(S_path_min_ray_pdf,index,min(phase.w,heap_float(state_address(S_path_min_ray_pdf,index,4u))));
    let next_bounce=bounce+1u;let volume_bounce=state_half(S_path_volume_bounce,index)+1u;
    if(next_bounce>=data_word(V_integrator_max_bounce)||volume_bounce>=data_word(V_integrator_max_volume_bounce)){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;}
    flag&=~(V_PATH_RAY_REFLECT|V_PATH_RAY_SINGULAR|PATH_RAY_TRANSPARENT|V_PATH_RAY_IMPORTANCE_BAKE|V_PATH_RAY_MIS_SKIP|V_PATH_RAY_MIS_HAD_TRANSMISSION);
    flag|=V_PATH_RAY_MIS_HAD_TRANSMISSION;flag&=~V_PATH_RAY_TRANSPARENT_BACKGROUND;if((flag&V_PATH_RAY_ANY_PASS)==0u){flag|=PATH_RAY_VOLUME_PASS;}
    if(next_bounce==1u){flag&=~PATH_RAY_VOLUME_PRIMARY_TRANSMIT;}
    state_u32(S_path_flag,index,flag);state_u8(S_path_visibility,index,V_PATH_RAY_VISIBILITY_VOLUME_SCATTER);state_u16(S_path_bounce,index,next_bounce);state_u16(S_path_volume_bounce,index,volume_bounce);
    state_u16(S_path_rng_offset,index,state_half(S_path_rng_offset,index)+RNG_BOUNCE);volume_finish(index,K_CLOSEST);
//#endif
}
//#if PHASE_STAGE
// DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME, last part with the phase functions beyond Henyey-Greenstein: the indirect
// phase scatter (integrate_volume_phase_scatter) that shade_volume recorded, run after shade_volume_light's direct
// light. A separate entry for the compile envelope; its closures are shade_volume's (push word 23).
@compute @workgroup_size(64) fn shade_volume_phase(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    let vp=p(24u)+id.x*VL_RECORD_BYTES;if(load_word(vp+64u)==0u){return;}store_word(vp+64u,0u);
    if(index>=p(10u)||load_word(p(17u))!=0u){return;}
    var r:SvmResult;r.closure_base=p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES;r.count=load_word(vp+60u);
//#if VOLUME_HETEROGENEOUS
    // A heterogeneous path's indirect phases (shade_volume_heterogeneous; word 80 is zero for the others).
    if(load_word(vp+80u)!=0u){r.closure_base=load_word(vp+80u);r.count=load_word(vp+84u);}
//#endif
    let sampled_t=heap_float(vp+68u);let object=load_word(vp+72u);
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));let tmin=heap_float(state_address(S_ray_tmin,index,4u));
    var flag=state_word(S_path_flag,index);let bounce=state_half(S_path_bounce,index);
//#if VOLUME_SAMPLING_PATTERN else     var random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+V_PRNG_VOLUME_PHASE).xy;
    var random=native_rng_pattern(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+V_PRNG_VOLUME_PHASE,2u).xy;
//#endif
    var selected=0u;var sum=svm_closure_read(r,0u).sample_weight;
    for(var i=1u;i<r.count;i++){let weight=svm_closure_read(r,i).sample_weight;sum+=weight;let threshold=weight/sum;
        if(random.x<threshold){selected=i;random.x/=threshold;}else{random.x=(random.x-threshold)/(1.0-threshold);}}
    let closure=svm_closure_read(r,selected);
//#if VOLUME_PHASES else     let g=closure.roughness;let phase=volume_phase(direction,closure.roughness,random);
    let g=vp_get_g(closure);var phase:vec4<f32>;
    if(closure.kind==CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID){phase=volume_phase(direction,closure.roughness,random);}else{phase=vp_phase_sample(direction,closure,random);}
//#endif
    if(phase.w==0.0){volume_finish(index,0u);return;}
//#if VOLUME_LIGHT else     let position=origin+sampled_t*direction;state_vec3(S_ray_P,index,position);state_vec3(S_ray_D,index,surface_native_normalize(phase.xyz));
    let position=rounded_add3(origin,sampled_t*direction);state_vec3(S_ray_P,index,position);state_vec3(S_ray_D,index,surface_native_normalize(phase.xyz));
//#endif
//#if VOLUME_LIGHT_TREE
    // ray->tmax - ray->tmin of the segment.
    if(data_word(VL_D_use_light_tree)!=0u){state_f32(S_ray_previous_dt,index,select(bitcast<f32>(0x7f7fffffu),heap_float(state_address(S_isect_t,index,4u)),state_word(S_isect_prim,index)!=NONE)-tmin);}
//#endif
    state_f32(S_ray_tmin,index,0.0);state_u32(S_ray_tmax,index,0x7f7fffffu);state_f32(S_ray_dP,index,0.0);state_f32(S_ray_dD,index,max(heap_float(state_address(S_ray_dD,index,4u)),1.0-abs(g)));
    state_u32(S_isect_prim,index,NONE);state_u32(S_isect_object,index,object);
//#if LIGHT_LINKING
    if((data_word(D_kernel_features)&KF_LIGHT_LINKING)!=0u){state_u32(S_path_mis_ray_object,index,object);}
//#endif
    if(p(20u)!=0u&&bounce==0u){state_vec3(S_path_pass_diffuse_weight,index,vec3<f32>(1));state_vec3(S_path_pass_glossy_weight,index,vec3<f32>(0));}
//#if VOLUME_LIGHT else     state_f32(S_path_mis_ray_pdf,index,phase.w);state_vec3(S_path_mis_origin_n,index,position-(origin+direction*tmin));
    state_f32(S_path_mis_ray_pdf,index,phase.w);state_vec3(S_path_mis_origin_n,index,position-rounded_add3(origin,direction*tmin));
//#endif
    state_f32(S_path_min_ray_pdf,index,min(phase.w,heap_float(state_address(S_path_min_ray_pdf,index,4u))));
    let next_bounce=bounce+1u;let volume_bounce=state_half(S_path_volume_bounce,index)+1u;
    if(next_bounce>=data_word(V_integrator_max_bounce)||volume_bounce>=data_word(V_integrator_max_volume_bounce)){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;}
    flag&=~(V_PATH_RAY_REFLECT|V_PATH_RAY_SINGULAR|PATH_RAY_TRANSPARENT|V_PATH_RAY_IMPORTANCE_BAKE|V_PATH_RAY_MIS_SKIP|V_PATH_RAY_MIS_HAD_TRANSMISSION);
    flag|=V_PATH_RAY_MIS_HAD_TRANSMISSION;flag&=~V_PATH_RAY_TRANSPARENT_BACKGROUND;if((flag&V_PATH_RAY_ANY_PASS)==0u){flag|=PATH_RAY_VOLUME_PASS;}
//#if VOLUME_RAY_MARCHING
//#if !VOLUME_LIGHT
    // sd->P = ray->P + indirect_t * ray->D, never contracted (the driver fuses the sum above; the scalar ARM64 path needs the oracle's bits).
    // With VOLUME_LIGHT the position above is already that sum (rounded_add3), so this line is only for scenes without it.
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){state_vec3(S_ray_P,index,rounded_add3(origin,sampled_t*direction));}
//#endif
//#endif
    if(next_bounce==1u){flag&=~PATH_RAY_VOLUME_PRIMARY_TRANSMIT;}
    state_u32(S_path_flag,index,flag);state_u8(S_path_visibility,index,V_PATH_RAY_VISIBILITY_VOLUME_SCATTER);state_u16(S_path_bounce,index,next_bounce);state_u16(S_path_volume_bounce,index,volume_bounce);
    state_u16(S_path_rng_offset,index,state_half(S_path_rng_offset,index)+RNG_BOUNCE);volume_finish(index,K_CLOSEST);
}
//#endif

//#if VOLUME_HETEROGENEOUS
// sobol_burley_sample_3D(index, dimension, 0, 0xffffffff) (sample/sobol_burley.h).
fn vd_sample_3d(index:u32,dimension:u32)->vec3<f32>{
    let seed=sb_hash_hp(dimension);let shuffled=sb_reversed_bit_owen(reverseBits(index),seed^0xcaa726acu);
    return vec3<f32>(sb_sample(shuffled,0u,seed^0x9e78e391u),sb_sample(shuffled,1u,seed^0x67c33241u),sb_sample(shuffled,2u,seed^0x78c395c5u));
}
//#endif
// Native homogeneous branch of kernel_volume_density_evaluate. The shader's
// spatially constant coefficient makes voxel position sampling unnecessary.
@compute @workgroup_size(64) fn shader_eval_volume_density(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}let offset=id.x+p(3u);let input=p(2u)+offset*VD_INPUT_SIZE*2u;
    let shader=load_word(input+VD_INPUT_SIZE+VD_INPUT_OBJECT);if(shader==NONE){return;}
    let object=load_word(input+VD_INPUT_OBJECT);let flags=load_word(table_at(G_shaders,shader&SHADER_MASK,SIZE_shaders)+SH_flags);
//#if VOLUME_HETEROGENEOUS else     if((flags&VS_HAS_VOLUME)==0u||(flags&VD_HETEROGENEOUS)!=0u){fail();return;}
    if((flags&VS_HAS_VOLUME)==0u){fail();return;}
//#endif
    var sd:SurfaceData;sd.object_id=object;sd.shader=shader;sd.prim=NONE;sd.primitive_type=V_PRIMITIVE_VOLUME;sd.time=0.5;
    sd.object_flag=load_word(table_at(G_object_flag,object,SIZE_object_flag));sd.shader_flag=flags;sd.runtime_flag=SR_IS_VOLUME_SHADER_EVAL;
//#if VOLUME_MOTION
    // kernel_volume_density_evaluate's volume_shader_eval_entry: the object motion transforms at time 0.5 (ray.time).
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){sd.motion_tfm=motion_object(sd.object_id,sd.time);sd.motion_itfm=motion_inverse(sd.motion_tfm);}
//#endif
//#if VOLUME_HETEROGENEOUS
    // kernel_volume_density_evaluate: 16 positions per voxel for heterogeneous shaders (1 otherwise), each
    // ray.P + rand_p * voxel_size (Sobol-Burley, blue-noise indexing), in world space through the object transform
    // unless it is applied; the extrema of max(sigma, emission), divided by the object's density scale.
    let samples=select(1u,16u,(flags&VD_HETEROGENEOUS)!=0u);
    let corner=vec3<f32>(bitcast<f32>(load_word(input+VD_INPUT_PRIM)),heap_float(input+VD_INPUT_U),heap_float(input+VD_INPUT_V));
    let size=vec3<f32>(bitcast<f32>(load_word(input+VD_INPUT_SIZE+VD_INPUT_PRIM)),heap_float(input+VD_INPUT_SIZE+VD_INPUT_U),heap_float(input+VD_INPUT_SIZE+VD_INPUT_V));
    let applied=(sd.object_flag&VD_TRANSFORM_APPLIED)!=0u;let tfm=table_at(G_objects,object,SIZE_objects)+O_tfm;
    var lo=bitcast<f32>(0x7f7fffffu);var hi=-lo;
    for(var sample=0u;sample<samples;sample++){
        var P=rounded_add3(corner,vd_sample_3d(sample+offset*samples,VD_PRNG_DENSITY)*size);if(!applied){P=surface_transform_point(tfm,P);}
        sd.P=P;sd.runtime_flag=SR_IS_VOLUME_SHADER_EVAL;
        let result=svm_evaluate_mode(NONE,sd,0u,0u,true);if(result.error!=0u){return;}
        var emission=0.0;if((result.runtime_flag&SR_EMISSION)!=0u){emission=max(result.emission.x,max(result.emission.y,result.emission.z));}
        var sigma=0.0;if((result.runtime_flag&SR_EXTINCTION)!=0u){sigma=max(result.transparency.x,max(result.transparency.y,result.transparency.z));}
        let value=max(sigma,emission);lo=min(lo,value);hi=max(hi,value);
    }
    let scale=heap_float(table_at(G_objects,object,SIZE_objects)+O_volume_density);
    if(!(scale>0.0)){fail();return;}
    store_word(p(4u)+offset*8u,bitcast<u32>(rounded_divide(lo,scale)));store_word(p(4u)+offset*8u+4u,bitcast<u32>(rounded_divide(hi,scale)));
//#endif
//#if !VOLUME_HETEROGENEOUS
    let result=svm_evaluate_mode(NONE,sd,0u,0u,true);if(result.error!=0u){return;}
//#if VOLUME_EMISSION else     if((result.runtime_flag&SR_EMISSION)!=0u){fail();return;}
//#endif
    let scale=heap_float(table_at(G_objects,object,SIZE_objects)+O_volume_density);
    if(!(scale>0.0)){fail();return;}
//#if VOLUME_EMISSION else     let value=max(result.transparency.x,max(result.transparency.y,result.transparency.z))/scale;
    // max(sigma, emission) of the extinction and the emission (zero without one).
    var emission=0.0;if((result.runtime_flag&SR_EMISSION)!=0u){emission=max(result.emission.x,max(result.emission.y,result.emission.z));}
    let value=max(max(result.transparency.x,max(result.transparency.y,result.transparency.z)),emission)/scale;
//#endif
    store_word(p(4u)+offset*8u,bitcast<u32>(value));store_word(p(4u)+offset*8u+4u,bitcast<u32>(value));
//#endif
}
