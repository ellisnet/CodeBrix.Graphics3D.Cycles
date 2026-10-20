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
fn volume_phase(direction:vec3<f32>,g:f32,random:vec2<f32>)->vec4<f32>{
    var cosine=rounded_add(1.0,-2.0*random.x);let g2=g*g;
    if(abs(g)>=0.001){let k=rounded_divide(rounded_add(1.0,-g2),rounded_add(1.0,-g*cosine));cosine=rounded_divide(rounded_add(rounded_add(1.0,g2),-k*k),2.0*g);}
    var pdf=0.07957747154594767;
    if(abs(g)>=0.001){let fac=rounded_add(1.0,g*rounded_add(g,-2.0*cosine));pdf=rounded_divide(rounded_add(1.0,-g2),(12.566370614359172*fac)*rounded_sqrt(max(fac,0.0)));}
    let sine=rounded_sqrt(max(rounded_add(1.0,-cosine*cosine),0.0));let phi=6.283185307179586*random.y;
    let sc=native_phase_sincos(phi);let local=vec3<f32>(sine*sc.y,sine*sc.x,cosine);
    var tangent=vec3<f32>(direction.z-direction.y,direction.x-direction.z,direction.y-direction.x);
    if(direction.x==direction.y&&direction.x==direction.z){tangent=vec3<f32>(direction.z-direction.y,direction.x+direction.z,-direction.y-direction.x);}
    tangent=rounded_normalize(tangent);let bitangent=rounded_cross(direction,tangent);
    let wo=rounded_add3(rounded_add3(local.x*tangent,local.y*bitangent),local.z*direction);
    return vec4<f32>(wo,pdf);
}
fn volume_finish(index:u32,next:u32){
    if(next==0u){volume_write_depth(index);}
    let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[counter/4u+DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME],1u);
    if(next!=0u){atomicAdd(&heap[counter/4u+next],1u);}state_u16(S_path_queued_kernel,index,next);
}
fn volume_after(index:u32){
    let prim=state_word(S_isect_prim,index);var next=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;
    if(prim!=NONE){
        if((state_word(S_isect_type,index)&PRIMITIVE_LAMP)!=0u){next=DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD;}
        else{
            let shader=load_word(table_at(G_tri_shader,prim,SIZE_tri_shader))&SHADER_MASK;
            let flags=load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags);
            next=select(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,(flags&SD_HAS_RAYTRACE)!=0u);
            let divisor=load_word(p(7u)+S_sort_partition_divisor);let key=shader+data_word(D_max_shaders)*(index/divisor);
            let limit=select(p(15u),p(16u),next==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE);if(key>=limit){fail();return;}
            let counter=load_word(p(7u)+S_sort_key_counter+next*POINTER_SIZE);atomicAdd(&heap[(counter+key*4u)/4u],1u);state_u32(S_path_shader_sort_key,index,key);
        }
    }else if((data_word(D_integrator_filter_closures)&FILTER_CLOSURE_DIRECT_LIGHT)!=0u&&state_half(S_path_bounce,index)==1u){next=0u;}
    volume_finish(index,next);
}
@compute @workgroup_size(64) fn shade_volume(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME){fail();return;}
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_path_render_pixel_index,index);if(pixel>=p(11u)/stride){fail();return;}let base=p(4u)+pixel*stride*4u;
    let prim=state_word(S_isect_prim,index);var flag=state_word(S_path_flag,index);let bounce=state_half(S_path_bounce,index);
    if(bounce==0u){flag|=PATH_RAY_VOLUME_PRIMARY_TRANSMIT;}
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
        sd.runtime_flag=r.runtime_flag;r=svm_evaluate_seeded(index,sd,r.remaining,r.closure_base,true,r);if(r.error!=0u){return;}
        if(slot>0u){
            for(var a=0u;a<r.count;a++){var left=svm_closure_read(r,a);var b=a+1u;
                while(b<r.count){let right=svm_closure_read(r,b);
                    if(left.kind==right.kind&&left.roughness==right.roughness){left.weight+=right.weight;left.sample_weight+=right.sample_weight;svm_closure_write(r.closure_base+a*SVM_CLOSURE_BYTES,left);
                        for(var j=b;j+1u<r.count;j++){
                            let current=svm_closure_read(r,j);let moved=svm_closure_read(r,j+1u);
                            // Native generic ShaderClosure assignment leaves HG anisotropy
                            // in C++ padding unchanged on the reviewed CPU build. Do not
                            // silently reproduce that mixed-phase transport error.
                            if(current.roughness!=moved.roughness){atomicOr(&heap[p(17u)/4u],16u);return;}
                            svm_closure_write(r.closure_base+j*SVM_CLOSURE_BYTES,moved);
                        }r.count--;
                    }else{b++;}
                }
            }
        }
    }
    if((r.runtime_flag&SR_EMISSION)!=0u||r.count>8u){fail();return;}
    let sigma=r.transparency;var scatter=vec3<f32>(0);for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(c.kind!=CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID){fail();return;}scatter+=c.weight;}
    if(any(sigma<vec3<f32>(0))||any(scatter<vec3<f32>(0))){fail();return;}
    state_u32(S_path_flag,index,flag);
    if(prim==NONE){store_word(volume_stack_address(index,entries,VS_shader),NONE);}
    if(bounce==0u){state_f32(S_path_optical_depth,index,heap_float(state_address(S_path_optical_depth,index,4u))+max(sigma.x,max(sigma.y,sigma.z))*distance);}
    let throughput=heap_vec3(state_address(S_path_throughput,index,12u));let transmittance=exp(-sigma*distance);var indirect=throughput;var sampled=false;var sampled_t=0.0;
    if((flag&PATH_RAY_TERMINATE)!=0u||all(scatter==vec3<f32>(0))){indirect*=transmittance;}
    else{
        let sample_id=state_word(S_path_sample,index);let pixel_rng=state_word(S_path_rng_pixel,index);let offset=state_half(S_path_rng_offset,index);
        let albedo=svm_divide(scatter,sigma)*(vec3<f32>(1)-transmittance)*throughput;
        let weights=abs(throughput*(albedo+transmittance));let sum=weights.x+weights.y+weights.z;var channel_pdf=vec3<f32>(1.0/3.0);
        if(rounded_add(1.0,-sum)<1.0){channel_pdf=weights/sum;}
        let rchannel=native_rng(sample_id,pixel_rng,offset+V_PRNG_VOLUME_RESERVOIR).x;var channel=2u;var partial=0.0;
        for(var c=0u;c<3u;c++){if(rchannel<partial+channel_pdf[c]){channel=c;break;}partial+=channel_pdf[c];}
        let probability=volume_scatter_probability(index,base,sigma,transmittance);var random=native_rng(sample_id,pixel_rng,offset+V_PRNG_VOLUME_SCATTER_DISTANCE).x;
        if(random<probability[channel]){
            random/=probability[channel];sampled=distance>0.0;
            let dt=clamp(-log(1.0-random*(1.0-exp(-sigma[channel]*distance)))/sigma[channel],0.0,distance);sampled_t=tmin+dt;
            let attenuation=exp(-sigma*dt);let distance_pdf=svm_divide(sigma*attenuation,vec3<f32>(1)-transmittance);
            let pdf=dot(distance_pdf*probability,channel_pdf);indirect*=svm_divide(scatter*attenuation,vec3<f32>(pdf));
        }else{let pdf=dot(vec3<f32>(1)-probability,channel_pdf);indirect*=svm_divide(transmittance,vec3<f32>(pdf));}
    }
    let continuation=select(heap_float(state_address(S_path_continuation_probability,index,4u)),0.0,(flag&PATH_RAY_TERMINATE_IN_NEXT_VOLUME)!=0u);
    if(continuation==0.0){volume_finish(index,0u);return;}
    if(sampled){indirect/=continuation;}state_vec3(S_path_throughput,index,indirect);
    if(!sampled){volume_after(index);return;}
    var random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+V_PRNG_VOLUME_PHASE).xy;
    var selected=0u;var sum=svm_closure_read(r,0u).sample_weight;
    for(var i=1u;i<r.count;i++){let weight=svm_closure_read(r,i).sample_weight;sum+=weight;let threshold=weight/sum;
        if(random.x<threshold){selected=i;random.x/=threshold;}else{random.x=(random.x-threshold)/(1.0-threshold);}}
    let g=svm_closure_read(r,selected).roughness;let phase=volume_phase(direction,g,random);if(phase.w==0.0){volume_finish(index,0u);return;}
    let position=origin+sampled_t*direction;state_vec3(S_ray_P,index,position);state_vec3(S_ray_D,index,rounded_normalize(phase.xyz));
    state_f32(S_ray_tmin,index,0.0);state_u32(S_ray_tmax,index,0x7f7fffffu);state_f32(S_ray_dP,index,0.0);state_f32(S_ray_dD,index,max(heap_float(state_address(S_ray_dD,index,4u)),1.0-abs(g)));
    state_u32(S_isect_prim,index,NONE);state_u32(S_isect_object,index,sd.object_id);
    if(p(20u)!=0u&&bounce==0u){state_vec3(S_path_pass_diffuse_weight,index,vec3<f32>(1));state_vec3(S_path_pass_glossy_weight,index,vec3<f32>(0));}
    state_f32(S_path_mis_ray_pdf,index,phase.w);state_vec3(S_path_mis_origin_n,index,position-(origin+direction*tmin));
    state_f32(S_path_min_ray_pdf,index,min(phase.w,heap_float(state_address(S_path_min_ray_pdf,index,4u))));
    let next_bounce=bounce+1u;let volume_bounce=state_half(S_path_volume_bounce,index)+1u;
    if(next_bounce>=data_word(V_integrator_max_bounce)||volume_bounce>=data_word(V_integrator_max_volume_bounce)){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;}
    flag&=~(V_PATH_RAY_REFLECT|V_PATH_RAY_SINGULAR|PATH_RAY_TRANSPARENT|V_PATH_RAY_IMPORTANCE_BAKE|V_PATH_RAY_MIS_SKIP|V_PATH_RAY_MIS_HAD_TRANSMISSION);
    flag|=V_PATH_RAY_MIS_HAD_TRANSMISSION;flag&=~V_PATH_RAY_TRANSPARENT_BACKGROUND;if((flag&V_PATH_RAY_ANY_PASS)==0u){flag|=PATH_RAY_VOLUME_PASS;}
    if(next_bounce==1u){flag&=~PATH_RAY_VOLUME_PRIMARY_TRANSMIT;}
    state_u32(S_path_flag,index,flag);state_u8(S_path_visibility,index,V_PATH_RAY_VISIBILITY_VOLUME_SCATTER);state_u16(S_path_bounce,index,next_bounce);state_u16(S_path_volume_bounce,index,volume_bounce);
    state_u16(S_path_rng_offset,index,state_half(S_path_rng_offset,index)+RNG_BOUNCE);volume_finish(index,K_CLOSEST);
}

// Native homogeneous branch of kernel_volume_density_evaluate. The shader's
// spatially constant coefficient makes voxel position sampling unnecessary.
@compute @workgroup_size(64) fn shader_eval_volume_density(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}let offset=id.x+p(3u);let input=p(2u)+offset*VD_INPUT_SIZE*2u;
    let shader=load_word(input+VD_INPUT_SIZE+VD_INPUT_OBJECT);if(shader==NONE){return;}
    let object=load_word(input+VD_INPUT_OBJECT);let flags=load_word(table_at(G_shaders,shader&SHADER_MASK,SIZE_shaders)+SH_flags);
    if((flags&VS_HAS_VOLUME)==0u||(flags&VD_HETEROGENEOUS)!=0u){fail();return;}
    var sd:SurfaceData;sd.object_id=object;sd.shader=shader;sd.prim=NONE;sd.primitive_type=V_PRIMITIVE_VOLUME;sd.time=0.5;
    sd.object_flag=load_word(table_at(G_object_flag,object,SIZE_object_flag));sd.shader_flag=flags;sd.runtime_flag=SR_IS_VOLUME_SHADER_EVAL;
    let result=svm_evaluate_mode(NONE,sd,0u,0u,true);if(result.error!=0u){return;}
    if((result.runtime_flag&SR_EMISSION)!=0u){fail();return;}
    let scale=heap_float(table_at(G_objects,object,SIZE_objects)+O_volume_density);
    if(!(scale>0.0)){fail();return;}
    let value=max(result.transparency.x,max(result.transparency.y,result.transparency.z))/scale;
    store_word(p(4u)+offset*8u,bitcast<u32>(value));store_word(p(4u)+offset*8u+4u,bitcast<u32>(value));
}
