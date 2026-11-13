// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Subsurface scattering on the WebGPU device (pinned integrator/subsurface.h, subsurface_random_walk.h,
// closure/bssrdf.h, closure/volume.h; pipelines.h SF_SUBSURFACE, SF_SUBSURFACE_ENTRY). The surface stages pick a
// BSSRDF closure like a BSDF (surface_shader_bsdf_bssrdf_pick), subsurface_bounce sends the path into the surface
// and queues DEVICE_KERNEL_INTEGRATOR_INTERSECT_SUBSURFACE, whose entry (intersect_subsurface) runs the random walk
// against the object's own BVH (raytrace_traverse, local closest hit) and queues the exit point for surface
// shading, where a diffuse closure replaces the shader (subsurface_shader_data_setup). Every product is rounded
// before its sum and quotients, square roots, expf and logf are correctly rounded (the oracle kernels'
// -ffp-contract=off order).
//#if SUBSURFACE
fn ss_bssrdf_kind(kind:u32)->bool{return kind>=SS_CLOSURE_BURLEY&&kind<=SS_CLOSURE_RANDOM_WALK_SKIN;}
fn ss_bsdf_or_bssrdf(kind:u32)->bool{return kind!=SS_CLOSURE_NONE&&kind<=SS_CLOSURE_RANDOM_WALK_SKIN;}
// The picked BSSRDF closure of surf_sample (shade_surface reads it in the same invocation, or from the stored sample).
var<private> ss_selected:u32;
fn ss_picked()->u32{
//#if SVM_BSDF_FAMILIES else     return ss_selected;
    return surf_sampled_closure;
//#endif
}
// make_orthonormals(N) and to_global, as surface_cos_hemisphere.
fn ss_frame(n:vec3<f32>)->array<vec3<f32>,2>{
    var t=vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x);if(n.x==n.y&&n.x==n.z){t=vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x);}
    t=surface_native_normalize(t);return array<vec3<f32>,2>(t,rounded_cross(n,t));
}
// subsurface_entry_bounce: the refracted direction into the surface (w = 0 when sampling fails).
fn ss_entry_bounce(c:SvmClosure,wi:vec3<f32>,random:vec2<f32>)->vec4<f32>{
    var r=random;
    if(c.kind==SS_CLOSURE_RANDOM_WALK_SKIN){
        if(r.x<0.5){r.x*=2.0;let h=surface_cos_hemisphere(-c.N,r);return vec4<f32>(h.xyz,1.0);}
        r.x=2.0*rounded_add(r.x,-0.5);
    }
    let cos_ni=rounded_dot(c.N,wi);if(cos_ni<=0.0){return vec4<f32>(0);}
    let frame=ss_frame(c.N);let x=frame[0];let y=frame[1];
    let local_i=vec3<f32>(rounded_dot(x,wi),rounded_dot(y,wi),cos_ni);
    let local_h=ggx_vndf(local_i,c.roughness,c.roughness,r);
    let h=rounded_add3(rounded_add3(local_h.x*x,local_h.y*y),local_h.z*c.N);
    let cos_hi=rounded_dot(h,wi);let neta=rounded_divide(1.0,c.ior);
    let arg=rounded_add(1.0,-((neta*neta)*rounded_add(1.0,-(cos_hi*cos_hi))));
    let dnp=max(rounded_sqrt(arg),1e-7);let nk=rounded_add(neta*cos_hi,-dnp);
    return vec4<f32>(rounded_add3(-(neta*wi),nk*h),1.0);
}
// subsurface_bounce from shade_surface: the ray into the surface, the path weight and flags, the Bssrdf parameters
// in the subsurface state, and the path queued for INTERSECT_SUBSURFACE (a failed entry bounce ends the path).
fn ss_bounce(index:u32,sd:SurfaceData,r:SvmResult,selected:u32){
    let c=svm_closure_read(r,selected);
//#if SUBSURFACE_DISK else     if(c.kind==SS_CLOSURE_BURLEY){fail();return;}
    // Christensen-Burley: PATH_RAY_SUBSURFACE_DISK and the geometric normal for the disk, no entry bounce.
    let burley=c.kind==SS_CLOSURE_BURLEY;
//#endif
    state_vec3(S_ray_P,index,sd.P);state_f32(S_ray_tmin,index,0.0);state_u32(S_ray_tmax,index,0x7f7fffffu);state_f32(S_ray_dP,index,sd.dP);state_f32(S_ray_dD,index,0.0);
    let offset=state_half(S_path_rng_offset,index)+RNG_BOUNCE;state_u16(S_path_rng_offset,index,offset);
    // surface_shader_bssrdf_sample_weight
    var weight=c.weight;
    if(r.count>1u){var sum=0.0;for(var i=0u;i<r.count;i++){let o=svm_closure_read(r,i);if(ss_bsdf_or_bssrdf(o.kind)){sum+=o.sample_weight;}}
        weight*=rounded_divide(sum,c.sample_weight);}
    state_vec3(S_path_throughput,index,heap_vec3(state_address(S_path_throughput,index,12u))*weight);
    let visibility=state_byte(S_path_visibility,index)&~SS_PATH_RAY_VISIBILITY_CAMERA;
//#if SUBSURFACE_DISK else     var flag=state_word(S_path_flag,index)|SS_PATH_RAY_SUBSURFACE_RANDOM_WALK;
    var flag=state_word(S_path_flag,index)|select(SS_PATH_RAY_SUBSURFACE_RANDOM_WALK,SS_PATH_RAY_SUBSURFACE_DISK,burley);
    if(burley){state_vec3(S_subsurface_N,index,sd.Ng);}else{
//#endif
//#if SAMPLING_SOBOL_BURLEY else     let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),offset+SS_PRNG_BSDF).xy;
    let random=native_rng_pattern(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),offset+SS_PRNG_BSDF,2u).xy;
//#endif
    let wo=ss_entry_bounce(c,sd.wi,random);
    if(wo.w==0.0||rounded_dot(sd.Ng,wo.xyz)>=0.0){surf_finish(index,false);return;}
    state_vec3(S_ray_D,index,wo.xyz);state_vec3(S_subsurface_N,index,sd.N);
//#if SUBSURFACE_DISK
    }
//#endif
    if((sd.runtime_flag&SR_BACKFACING)!=0u){flag|=SS_PATH_RAY_SUBSURFACE_BACKFACING;}
    state_u8(S_path_visibility,index,visibility);state_u32(S_path_flag,index,flag);
    if(p(20u)!=0u&&state_half(S_path_bounce,index)==0u){state_vec3(S_path_pass_diffuse_weight,index,vec3<f32>(1));state_vec3(S_path_pass_glossy_weight,index,vec3<f32>(0));}
    state_vec3(S_subsurface_albedo,index,c.T);state_vec3(S_subsurface_radius,index,c.multiscatter);
    state_f32(S_subsurface_anisotropy,index,select(c.a+2.0,c.a,c.kind==SS_CLOSURE_RANDOM_WALK));
    let counter=load_word(p(7u)+S_queue_counter)/4u;atomicSub(&heap[counter+surf_queue()],1u);atomicAdd(&heap[counter+SS_K_INTERSECT],1u);
    state_u16(S_path_queued_kernel,index,SS_K_INTERSECT);
}
// The exit point (shade_surface with PATH_RAY_SUBSURFACE): the shader is evaluated only for a bump-mapped BSSRDF
// normal (surface_shader_bssrdf_normal), then one diffuse closure of weight 1 replaces the closures
// (subsurface_shader_data_setup, the runtime flags reset). Holdout, emission, termination and the data passes are
// skipped (connect_surface_light), as at the pinned exit point.
fn ss_exit(slot:u32,geometry:SurfaceData,e:SvmResult,evaluated:bool){
    var sd=geometry;var n=sd.N;let base=p(23u)+slot*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES;
    if(evaluated){
        sd.N=e.N;var sum=vec3<f32>(0);
        for(var i=0u;i<e.count;i++){let c=svm_closure_read(e,i);if(ss_bssrdf_kind(c.kind)){sum=rounded_add3(sum,c.N*abs((c.weight.x+c.weight.y+c.weight.z)*(1.0/3.0)));}}
        n=select(surface_native_normalize(sum),sd.N,all(sum==vec3<f32>(0)));
    }
    var r:SvmResult;r.closure_base=base;r.count=1u;r.remaining=data_word(D_max_closures)-1u;r.runtime_flag=SR_BSDF|SR_BSDF_HAS_EVAL;r.N=sd.N;
    var c:SvmClosure;c.kind=CLOSURE_BSDF_DIFFUSE_ID;c.weight=vec3<f32>(1);c.sample_weight=1.0;c.N=n;svm_closure_write(base,c);
    // subsurface_shader_data_setup: the closure flags go (they are the new record's), SR_BACKFACING stays.
    sd.runtime_flag&=~SR_CLOSURE_FLAG;
//#if FILM_PASSES_ANY
    // integrate_surface writes no data passes and no denoising features at the exit point: the record carries
    // SS_SR_EXIT_POINT for shade_surface_passes (no SR_* flag uses bit 31; the stages test sd.runtime_flag bit by bit).
    sd.runtime_flag|=SS_SR_EXIT_POINT;
//#endif
    surface_stage_write(p(24u)+slot*SURFACE_STAGE_BYTES,sd,r);
}
//#if FILM_PASSES_ANY
const SS_SR_EXIT_POINT:u32=0x80000000u;
//#endif
//#endif
//#if SUBSURFACE_ENTRY
// powf (correctly rounded) with powf of 0 as the C library.
fn ss_pow(x:f32,y:f32)->f32{if(x==0.0){return select(select(1.0,bitcast<f32>(0x7f800000u),y<0.0),0.0,y>0.0);}return vp_pow(x,y);}
// volume_sample_channel_pdf and volume_sample_channel (closure/volume.h), as wavefront_volume.wgsl's (a separate
// module) with float3 / float as the host's SSE division (surf_native_div3).
fn ss_channel_pdf(albedo:vec3<f32>,throughput:vec3<f32>)->vec3<f32>{
    let weights=abs(throughput*albedo);let sum=weights.x+weights.y+weights.z;
    if(rounded_add(1.0,-sum)<1.0){return surf_native_div3(weights,sum);}return vec3<f32>(1.0/3.0);
}
// subsurface_random_walk_remap with the polynomial fit (legacy random walk and the skin model): the powers of g
// as successive products, each sum in source order.
fn ss_poly(c:array<f32,8>,g:f32)->f32{var power=g;var sum=c[0];for(var k=1u;k<8u;k++){sum=rounded_add(sum,c[k]*power);power*=g;}return sum;}
fn ss_exp_poly(c:array<f32,7>,g:f32)->f32{var power=g*g;var sum=c[0]*g;for(var k=1u;k<7u;k++){sum=rounded_add(sum,c[k]*power);power*=g;}return ss_exp(sum);}
fn ss_remap_fit(albedo:f32,d:f32,g:f32)->vec2<f32>{
    let a=ss_poly(array<f32,8>(1.8260523782,-1.28451056436,-1.79904629312,9.19393289202,-22.8215585862,32.0234874259,-23.6264803333,7.21067002658),g);
    let b=rounded_add(4.98511194385,0.127355959438*ss_exp_poly(array<f32,7>(31.1491581433,-201.847017512,841.576016723,-2018.09288505,2731.71560286,-1935.41424244,559.009054474),g));
    let c=ss_poly(array<f32,8>(1.09686102424,-0.394704063468,1.05258115941,-8.83963712726,28.8643230661,-46.8802913581,38.5402837518,-12.7181042538),g);
    let dd=ss_poly(array<f32,8>(0.496310210422,0.360146581622,-2.15139309747,17.8896899217,-55.2984010333,82.065982243,-58.5106008578,15.8478295021),g);
    let e=rounded_add(4.23190299701,0.00310603949088*ss_exp_poly(array<f32,7>(76.7316253952,-594.356773233,2448.8834203,-5576.68528998,7116.60171912,-4763.54467887,1303.5318055),g));
    let f=ss_poly(array<f32,8>(2.40602999408,-2.51814844609,9.18494908356,-79.2191708682,259.082868209,-403.613804597,302.85712436,-87.4370473567),g);
    let blend=ss_pow(albedo,0.25);
    let alpha=rounded_add((rounded_add(1.0,-blend)*a)*ss_pow(trig_atan2(b*albedo,1.0),c),(blend*dd)*ss_pow(trig_atan2(e*albedo,1.0),f));
    let sigma_t_prime=rounded_divide(1.0,max(d,1e-16));
    return vec2<f32>(rounded_divide(sigma_t_prime,rounded_add(1.0,-g)),clamp(alpha,0.0,0.999999));
}
// eval_phase_dwivedi, sample_phase_dwivedi.
fn ss_dwivedi_eval(v:f32,phase_log:f32,cosine:f32)->f32{return rounded_divide(1.0,rounded_add(v,-cosine)*phase_log);}
fn ss_dwivedi_sample(v:f32,phase_log:f32,rand:f32)->f32{return rounded_add(v,-(rounded_add(v,1.0)*ss_exp(-rand*phase_log)));}
// direction_from_cosine: spherical_cos_to_direction about D (make_orthonormals, to_global).
fn ss_direction_from_cosine(d:vec3<f32>,cosine:f32,randv:f32)->vec3<f32>{
    let sine=rounded_sqrt(max(rounded_add(1.0,-cosine*cosine),0.0));let sc=native_phase_sincos(6.283185307179586*randv);
    let frame=ss_frame(d);return rounded_add3(rounded_add3((sine*sc.y)*frame[0],(sine*sc.x)*frame[1]),cosine*d);
}
//#endif
//#if SUBSURFACE_ENTRIES
// path_state_rng_1D / _2D with the walk's scrambled dimension offset.
fn ss_rng(index:u32,dimension:u32,count:u32)->vec2<f32>{
//#if SAMPLING_SOBOL_BURLEY else     return native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),dimension).xy;
    return native_rng_pattern(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),dimension,count).xy;
//#endif
}
//#endif
//#if MOTION_OBJECT
//#if SUBSURFACE_ENTRIES
// The motion record of an input whose object moves (push word 13, SS_MOTION_RECORD_BYTES apart): the object's motion
// transform at the ray's time and its inverse, written by intersect_subsurface_motion. A walk or disk probe traverses
// with them as the shading object's transforms (raytrace_traverse), and the disk turns its hit normals with them.
fn ss_motion_geometry(geometry:ptr<function,SurfaceData>,slot:u32,object:u32){
    (*geometry).object_flag=load_word(table_at(G_object_flag,object,SIZE_object_flag));if(((*geometry).object_flag&SD_OBJECT_MOTION)==0u){return;}
    if(p(13u)==0u){fail();return;}let at=p(13u)+slot*SS_MOTION_RECORD_BYTES;
    (*geometry).object_id=object;(*geometry).motion_tfm=motion_transform_load(at);(*geometry).motion_itfm=motion_transform_load(at+48u);
}
//#endif
//#if SUBSURFACE_ENTRY
// object_fetch_transform_motion_test of each input's object at the ray's time (motion_object, transform_inverse), for the
// walk and the disk: its own entry, dispatched before both (with the two inlined into the walk's traversal intersect_subsurface
// reached 984,020 B, over the size guard).
@compute @workgroup_size(64) fn intersect_subsurface_motion(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)||load_word(p(17u))!=0u){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}if(state_half(S_path_queued_kernel,index)!=SS_K_INTERSECT){return;}
    let object=state_word(S_isect_object,index);if(object>=table_count(G_object_flag)){fail();return;}
    if((load_word(table_at(G_object_flag,object,SIZE_object_flag))&SD_OBJECT_MOTION)==0u){return;}
    if(p(13u)==0u){fail();return;}let at=p(13u)+id.x*SS_MOTION_RECORD_BYTES;
    let tfm=motion_object(object,heap_float(state_address(S_ray_time,index,4u)));let itfm=motion_inverse(tfm);
    for(var k=0u;k<4u;k++){store_word(at+k*4u,bitcast<u32>(tfm.x[k]));store_word(at+16u+k*4u,bitcast<u32>(tfm.y[k]));store_word(at+32u+k*4u,bitcast<u32>(tfm.z[k]));
        store_word(at+48u+k*4u,bitcast<u32>(itfm.x[k]));store_word(at+64u+k*4u,bitcast<u32>(itfm.y[k]));store_word(at+80u+k*4u,bitcast<u32>(itfm.z[k]));}
}
//#endif
//#endif
//#if SUBSURFACE_ENTRY
// hash_hp_seeded_uint(i, seed) (util/hash.h).
fn ss_hash_seeded(i:u32,seed:u32)->u32{
    var x=i^(seed^(seed<<19u));x^=x>>16u;x*=0x21f0aaadu;x^=x>>15u;x*=0xd35a2d97u;x^=x>>15u;return x^0xe6fe3bebu;
}
//#endif
//#if SUBSURFACE_ENTRIES
// integrator_path_terminate (no render buffer: no optical depth pass).
fn ss_terminate(index:u32){
    let counter=load_word(p(7u)+S_queue_counter)/4u;atomicSub(&heap[counter+SS_K_INTERSECT],1u);state_u16(S_path_queued_kernel,index,0u);
}
//#endif
//#if SUBSURFACE_VOLUME
//#if SUBSURFACE_ENTRIES
// The inputs of integrator_volume_stack_update_for_subsurface for intersect_subsurface_volume_stack (the volume stack
// module), in this input's traversal stack slot once the traversals are done: the entry point (the state's ray P), the
// exit point (the ray P before subsurface_scatter reverses it) and the entry primitive (the volume ray's self prim).
fn ss_volume_record(from_P:vec3<f32>,to_P:vec3<f32>,self_prim:u32){
    let at=p(30u)+svm_slot*AO_STACK_BYTES;
    store_word(at,bitcast<u32>(from_P.x));store_word(at+4u,bitcast<u32>(from_P.y));store_word(at+8u,bitcast<u32>(from_P.z));
    store_word(at+12u,bitcast<u32>(to_P.x));store_word(at+16u,bitcast<u32>(to_P.y));store_word(at+20u,bitcast<u32>(to_P.z));store_word(at+24u,self_prim);
}
//#endif
//#endif
//#if SUBSURFACE_ENTRY
// DEVICE_KERNEL_INTEGRATOR_INTERSECT_SUBSURFACE: subsurface_scatter with subsurface_random_walk.
@compute @workgroup_size(64) fn intersect_subsurface(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)||load_word(p(17u))!=0u){return;}
    var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
//#if SUBSURFACE_DISK else     if(index>=p(10u)){fail();return;}if(state_half(S_path_queued_kernel,index)!=SS_K_INTERSECT){fail();return;}
    // A Christensen-Burley path is the disk entry's (intersect_subsurface_disk, dispatched after this one).
    if(index>=p(10u)){fail();return;}if((state_word(S_path_flag,index)&SS_PATH_RAY_SUBSURFACE_DISK)!=0u){return;}if(state_half(S_path_queued_kernel,index)!=SS_K_INTERSECT){fail();return;}
//#endif
    let P=heap_vec3(state_address(S_ray_P,index,12u));let D=heap_vec3(state_address(S_ray_D,index,12u));
    let ray_dP=heap_float(state_address(S_ray_dP,index,4u));let N=heap_vec3(state_address(S_subsurface_N,index,12u));
    let object=state_word(S_isect_object,index);let prim=state_word(S_isect_prim,index);
    var geometry:SurfaceData;geometry.object_id=NONE;geometry.time=heap_float(state_address(S_ray_time,index,4u));
//#if MOTION_OBJECT
    ss_motion_geometry(&geometry,id.x,object);if(load_word(p(17u))!=0u){return;}
//#endif
    let albedo=heap_vec3(state_address(S_subsurface_albedo,index,12u));let radius=heap_vec3(state_address(S_subsurface_radius,index,12u));
    var anisotropy=heap_float(state_address(S_subsurface_anisotropy,index,4u));
    var van_de_hulst=true;if(anisotropy>=1.0){anisotropy-=2.0;van_de_hulst=anisotropy<0.0;}
    // subsurface_random_walk_coefficients
    var sigma_t=vec3<f32>(0);var alpha=vec3<f32>(0);var throughput=heap_vec3(state_address(S_path_throughput,index,12u));
    if(van_de_hulst){
        // subsurface_random_walk_remap (van de Hulst): every float3 operation per lane.
        let root=vec3<f32>(rounded_sqrt(rounded_add(rounded_add(9.59217,41.6808*albedo.x),17.7126*(albedo.x*albedo.x))),
                           rounded_sqrt(rounded_add(rounded_add(9.59217,41.6808*albedo.y),17.7126*(albedo.y*albedo.y))),
                           rounded_sqrt(rounded_add(rounded_add(9.59217,41.6808*albedo.z),17.7126*(albedo.z*albedo.z))));
        let q=rounded_add3(rounded_add3(4.20863*albedo,-root),vec3<f32>(4.09712));let s_sq=q*q;
        let num=vec3<f32>(1)-s_sq;let den=rounded_add3(vec3<f32>(1),-(anisotropy*s_sq));
        for(var k=0u;k<3u;k++){alpha[k]=select(0.0,rounded_divide(num[k],den[k]),den[k]!=0.0);sigma_t[k]=rounded_divide(1.0,max(radius[k],1e-16));}
        alpha=clamp(alpha,vec3<f32>(0),vec3<f32>(0.999999));
    }else{
        for(var k=0u;k<3u;k++){let m=ss_remap_fit(albedo[k],radius[k],anisotropy);sigma_t[k]=m.x;alpha[k]=m.y;}
    }
    for(var k=0u;k<3u;k++){throughput[k]=select(0.0,rounded_divide(throughput[k],albedo[k]),albedo[k]!=0.0);}
    for(var k=0u;k<3u;k++){if(alpha[k]<0.2){throughput[k]*=rounded_divide(alpha[k],0.2);alpha[k]=0.2;}}
    let sigma_s_org=sigma_t*alpha;let alpha_max=max(max(alpha.x,alpha.y),alpha.z);
    // diffusion_length_dwivedi
    let exponent=rounded_add(rounded_add(2.44294,-(0.0215813*alpha_max)),rounded_divide(0.578637,alpha_max));
    let diffusion_length=rounded_divide(1.0,rounded_sqrt(rounded_add(1.0,-ss_pow(alpha_max,exponent))));
    if(diffusion_length==1.0){ss_terminate(index);return;}
    let phase_log=vp_logf(rounded_divide(rounded_add(diffusion_length,1.0),rounded_add(diffusion_length,-1.0)));
    var offset=ss_hash_seeded(state_half(S_path_rng_offset,index),0xdeadbeefu)&~3u;
    var hit=false;var have_opposite=false;var opposite_distance=0.0;
    let guided_fraction_org=rounded_add(1.0,-max(0.5,ss_pow(abs(anisotropy),0.125)));
    let sigma_s_star=sigma_s_org*rounded_add(1.0,-anisotropy);let sigma_t_star=rounded_add3(sigma_t-sigma_s_org,sigma_s_star);
    var ray_P=P;var ray_D=D;var ray_tmax=0.0;var self_prim=prim;var hits:LocalHits;
    for(var bounce=0u;bounce<SS_MAX_BOUNCES;bounce++){
        offset+=RNG_BOUNCE;
        var g=anisotropy;var guided_fraction=guided_fraction_org;var st=sigma_t;var ss=sigma_s_org;
        if(bounce>SS_SIMILARITY_LEVEL){g=0.0;guided_fraction=0.75;st=sigma_t_star;ss=sigma_s_star;}
        // The bounce's random numbers (path_state_rng_1D/_2D are pure functions of the dimension) through one call
        // site in a loop the compiler cannot unroll (header word 3 after the error word, always 0): colour channel,
        // scatter distance, guide strategy, guide direction, then the 2D scatter direction.
        var rv=array<f32,6>(0.0,0.0,0.0,0.0,0.0,0.0);let dims=array<u32,5>(SS_PRNG_COLOR_CHANNEL,SS_PRNG_SCATTER_DISTANCE,SS_PRNG_GUIDE_STRATEGY,SS_PRNG_GUIDE_DIRECTION,SS_PRNG_BSDF);
        for(var k=0u;k<(5u|load_word(p(17u)+12u));k++){if(k>=5u){break;}let v=ss_rng(index,offset+dims[k],select(1u,2u,k==4u));rv[k]=v.x;if(k==4u){rv[5]=v.y;}}
        let channel_pdf=ss_channel_pdf(alpha,throughput);
        var rchannel=rv[0];var channel=2u;var pdf_sum=0.0;
        for(var k=0u;k<3u;k++){if(rchannel<pdf_sum+channel_pdf[k]){channel=k;break;}pdf_sum+=channel_pdf[k];}
        var sample_sigma_t=st[channel];
        let randt=rv[1];
        var backward_fraction=0.0;var forward_pdf_factor=0.0;var forward_stretching=1.0;var backward_pdf_factor=0.0;var backward_stretching=1.0;
        if(bounce>0u){
            let guided=rv[2]<guided_fraction;
            var guide_backward=false;
            if(have_opposite){
                let x=clamp(rounded_dot(ray_P-P,-N),0.0,opposite_distance);
                backward_fraction=rounded_divide(1.0,rounded_add(1.0,ss_exp(rounded_divide(rounded_add(opposite_distance,-(2.0*x)),diffusion_length))));
                guide_backward=rv[3]<backward_fraction;
            }
            let rand_scatter=vec2<f32>(rv[4],rv[5]);
            var cos_theta=0.0;var hg_pdf=0.0;
            if(guided){
                cos_theta=ss_dwivedi_sample(diffusion_length,phase_log,rand_scatter.x);if(guide_backward){cos_theta=-cos_theta;}
                let new_d=ss_direction_from_cosine(N,cos_theta,rand_scatter.y);hg_pdf=vl_hg_pdf(rounded_dot(ray_D,new_d),g);ray_D=new_d;
            }else{
                let sampled=vl_phase_sample(ray_D,g,rand_scatter);hg_pdf=sampled.w;cos_theta=rounded_dot(sampled.xyz,N);ray_D=sampled.xyz;
            }
            forward_pdf_factor=rounded_divide(0.15915494309189535*ss_dwivedi_eval(diffusion_length,phase_log,cos_theta),hg_pdf);
            backward_pdf_factor=rounded_divide(0.15915494309189535*ss_dwivedi_eval(diffusion_length,phase_log,-cos_theta),hg_pdf);
            forward_stretching=rounded_add(1.0,-rounded_divide(cos_theta,diffusion_length));
            backward_stretching=rounded_add(1.0,rounded_divide(cos_theta,diffusion_length));
            if(guided){sample_sigma_t*=select(forward_stretching,backward_stretching,guide_backward);}
        }
        var t=rounded_divide(-vp_logf(rounded_add(1.0,-randt)),sample_sigma_t);
        if(bounce==0u){ray_tmax=max(t,rounded_divide(10.0,min(min(sigma_t.x,sigma_t.y),sigma_t.z)));}
        else{ray_tmax=t;self_prim=NONE;}
        hits=raytrace_traverse(geometry,ray_P,ray_D,0.0,ray_tmax,object,NONE,self_prim,AO_VISIBILITY_OBJECT_ALL,0u,false);
        if(load_word(p(17u))!=0u){return;}
        hit=hits.count>0u;if(hit){ray_tmax=hits.t[0];}
        if(bounce==0u){
            if(hit){have_opposite=true;opposite_distance=rounded_dot(rounded_add3(ray_P,ray_tmax*ray_D)-P,-N);}
            hit=ray_tmax<t;
        }
        if(hit){t=ray_tmax;}
        ray_P=rounded_add3(ray_P,t*ray_D);
        // subsurface_random_walk_pdf of sigma_t and of the forward and backward stretched sigma_t: the nine
        // exponentials through one call site (an opaque loop bound, as above).
        let sigmas=array<vec3<f32>,3>(st,forward_stretching*st,backward_stretching*st);var e=array<vec3<f32>,3>(vec3<f32>(0),vec3<f32>(0),vec3<f32>(0));
        for(var k=0u;k<(9u|load_word(p(17u)+12u));k++){if(k>=9u){break;}e[k/3u][k%3u]=ss_exp(-sigmas[k/3u][k%3u]*t);}
        let transmittance=e[0];
        var pdf=select(st*transmittance,transmittance,hit);
        if(bounce>0u){
            var guided_pdf=select(sigmas[1]*e[1],e[1],hit);
            if(have_opposite){
                let back_pdf=select(sigmas[2]*e[2],e[2],hit);let a=guided_pdf*forward_pdf_factor;
                guided_pdf=rounded_add3(a,backward_fraction*rounded_add3(back_pdf*backward_pdf_factor,-a));
            }else{guided_pdf*=forward_pdf_factor;}
            pdf=rounded_add3(pdf,guided_fraction*(guided_pdf-pdf));
        }
        throughput*=surf_native_div3(select(ss*transmittance,transmittance,hit),rounded_dot(channel_pdf,pdf));
        if(hit){break;}
        if(max(max(throughput.x,throughput.y),throughput.z)<1e-6){break;}
    }
    if(!hit){ss_terminate(index);return;}
//#if SUBSURFACE_VOLUME
    ss_volume_record(P,ray_P,prim);
//#endif
    state_vec3(S_path_throughput,index,throughput);
    // subsurface_scatter: the ray reversed towards the exit point, the hit as the path's intersection.
    state_vec3(S_ray_P,index,rounded_add3(ray_P,(ray_D*ray_tmax)*2.0));state_vec3(S_ray_D,index,-ray_D);
    state_f32(S_ray_tmin,index,0.0);state_f32(S_ray_tmax,index,ray_tmax);state_f32(S_ray_dP,index,ray_dP);state_f32(S_ray_dD,index,0.0);
    state_f32(S_isect_t,index,hits.t[0]);state_f32(S_isect_u,index,hits.u[0]);state_f32(S_isect_v,index,hits.v[0]);
    state_u32(S_isect_object,index,object);state_u32(S_isect_prim,index,hits.prim[0]);state_u32(S_isect_type,index,hits.kind);
    state_u16(S_path_rng_offset,index,state_half(S_path_rng_offset,index)+RNG_BOUNCE);
//#if MNEE
    // use_caustics and a caustics receiver: integrator_path_next to INTERSECT_MNEE (unsorted).
    if(data_word(MNEE_D_use_caustics)!=0u&&(load_word(table_at(G_object_flag,object,SIZE_object_flag))&SD_OBJECT_CAUSTICS_RECEIVER)!=0u){
        let queue=load_word(p(7u)+S_queue_counter)/4u;atomicSub(&heap[queue+SS_K_INTERSECT],1u);atomicAdd(&heap[queue+MNEE_K_INTERSECT],1u);
        state_u16(S_path_queued_kernel,index,MNEE_K_INTERSECT);return;}
//#endif
    // integrator_path_next_sorted to SHADE_SURFACE or SHADE_SURFACE_RAYTRACE, keyed by the exit point's shader.
    let shader=load_word(table_at(G_tri_shader,hits.prim[0],SIZE_tri_shader))&SHADER_MASK;
    let flags=load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags);
    let next=select(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,(flags&SD_HAS_RAYTRACE)!=0u);
    let divisor=load_word(p(7u)+S_sort_partition_divisor);let key=shader+data_word(D_max_shaders)*(index/divisor);
    if(key>=select(p(15u),p(16u),next==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE)){fail();return;}
    let sort=load_word(p(7u)+S_sort_key_counter+next*POINTER_SIZE);atomicAdd(&heap[(sort+key*4u)/4u],1u);state_u32(S_path_shader_sort_key,index,key);
    let counter=load_word(p(7u)+S_queue_counter)/4u;atomicSub(&heap[counter+SS_K_INTERSECT],1u);atomicAdd(&heap[counter+next],1u);
    state_u16(S_path_queued_kernel,index,next);
}
//#endif
//#if SUBSURFACE_DISK_ENTRY
// DEVICE_KERNEL_INTEGRATOR_INTERSECT_SUBSURFACE for Christensen-Burley paths (pinned integrator/subsurface_disk.h,
// closure/bssrdf.h, bvh/local.h with geom/geom_intersect.h local_intersect_get_record_index, bvh/util.h
// sort_intersections_and_normals): a point on a disk around one of three axes of the entry point's frame, a probe
// ray through the object's own BVH keeping up to BSSRDF_MAX_HITS hits (the lcg_state reservoir of raytrace_traverse),
// each hit weighted by the MIS over the axes and the Burley profile over its pdf, one hit resampled in proportion to
// its weight. Every product is rounded before its sum; quotients, square roots and expf are correctly rounded and
// float3 / float divides each lane (the oracle kernels' SSE4.2, -ffp-contract=off order).
// BURLEY_TRUNCATE_CDF and 1.0f / BURLEY_TRUNCATE_CDF as the host compiler folds them (binary32 0x3f7f12b2, 0x3f807715).
const SS_BURLEY_CDF:f32=0.996379018;
const SS_BURLEY_INV_CDF:f32=1.0036341;
// bssrdf_burley_eval and bssrdf_burley_pdf (d: the channel's scaled radius).
fn ss_burley_pdf(d:f32,r:f32)->f32{
    if(r==0.0||r>=16.0*d){return 0.0;}
    let e3=ss_exp(-rounded_divide(r,3.0*d));let e=(e3*e3)*e3;
    return rounded_divide(rounded_add(e,e3),4.0*d)*SS_BURLEY_INV_CDF;
}
// bssrdf_burley_root_find: the scaled radius whose CDF is xi (Newton-Raphson from the curve-fit guess).
fn ss_burley_root(xi:f32)->f32{
    var r=15.0;if(xi<=0.9){r=rounded_add(ss_exp((xi*xi)*2.4),-1.0);}
    for(var i=0u;i<10u;i++){
        let e3=ss_exp(-rounded_divide(r,3.0));let e=(e3*e3)*e3;
        let f=rounded_add(rounded_add(rounded_add(1.0,-(0.25*e)),-(0.75*e3)),-xi);let df=rounded_add(0.25*e,0.25*e3);
        if(abs(f)<1e-6||df==0.0){break;}
        r=max(rounded_add(r,-rounded_divide(f,df)),0.0);
    }
    return r;
}
// bssrdf_num_channels.
fn ss_disk_channels(radius:vec3<f32>)->f32{var n=0.0;for(var k=0u;k<3u;k++){if(radius[k]>0.0){n+=1.0;}}return n;}
// bssrdf_sample with bssrdf_burley_sample: the disk radius r and the half height h of the probe ray (x, y).
fn ss_disk_sample(radius:vec3<f32>,random:f32)->vec2<f32>{
    var xi=random*ss_disk_channels(radius);var d=0.0;var sum=0.0;
    for(var k=0u;k<3u;k++){if(radius[k]>0.0){let next=sum+1.0;if(xi<next){xi=rounded_add(xi,-sum);d=radius[k];break;}sum=next;}}
    let rm=16.0*d;let r=ss_burley_root(xi*SS_BURLEY_CDF)*d;
    return vec2<f32>(r,rounded_sqrt(max(rounded_add(rm*rm,-(r*r)),0.0)));
}
// bssrdf_eval: the Burley pdf of each channel at distance r.
fn ss_disk_eval(radius:vec3<f32>,r:f32)->vec3<f32>{return vec3<f32>(ss_burley_pdf(radius.x,r),ss_burley_pdf(radius.y,r),ss_burley_pdf(radius.z,r));}
// average(fabs(v)).
fn ss_disk_average(v:vec3<f32>)->f32{return ((abs(v.x)+abs(v.y))+abs(v.z))*(1.0/3.0);}
// lcg_state_init(rng_pixel, rng_offset, sample, 0x68bc21eb): hash_uint3 (Jenkins).
fn ss_disk_lcg(index:u32)->u32{
    var a=0xdeadbf08u+(state_word(S_path_rng_pixel,index)^0x68bc21ebu);
    var b=0xdeadbf08u+state_half(S_path_rng_offset,index);var c=0xdeadbf08u+state_word(S_path_sample,index);
    c^=b;c-=(b<<14u)|(b>>18u);a^=c;a-=(c<<11u)|(c>>21u);
    b^=a;b-=(a<<25u)|(a>>7u);c^=b;c-=(b<<16u)|(b>>16u);
    a^=c;a-=(c<<4u)|(c>>28u);b^=a;b-=(a<<14u)|(a>>18u);c^=b;c-=(b<<24u)|(b>>8u);return c;
}
@compute @workgroup_size(64) fn intersect_subsurface_disk(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)||load_word(p(17u))!=0u){return;}
    var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
    // Random walk paths are intersect_subsurface's (dispatched before this entry).
    let flag=state_word(S_path_flag,index);if((flag&SS_PATH_RAY_SUBSURFACE_DISK)==0u){return;}
    if(state_half(S_path_queued_kernel,index)!=SS_K_INTERSECT){fail();return;}
    let P=heap_vec3(state_address(S_ray_P,index,12u));let ray_dP=heap_float(state_address(S_ray_dP,index,4u));
    let time=heap_float(state_address(S_ray_time,index,4u));let Ng=heap_vec3(state_address(S_subsurface_N,index,12u));
    let object=state_word(S_isect_object,index);let radius=heap_vec3(state_address(S_subsurface_radius,index,12u));
    let offset=state_half(S_path_rng_offset,index);
    // The axis of the disk (rand_disk.y) and its frame (make_orthonormals of Ng, two axes swapped).
    var rand=ss_rng(index,offset+SS_PRNG_DISK,2u);
    let frame=ss_frame(Ng);var disk_N=Ng;var disk_T=frame[0];var disk_B=frame[1];var pick=vec3<f32>(0.5,0.25,0.25);
    if(rand.y<0.5){rand.y*=2.0;}
    else if(rand.y<0.75){disk_N=frame[0];disk_T=Ng;pick=vec3<f32>(0.25,0.5,0.25);rand.y=rounded_add(rand.y,-0.5)*4.0;}
    else{disk_N=frame[1];disk_B=Ng;pick=vec3<f32>(0.25,0.25,0.5);rand.y=rounded_add(rand.y,-0.75)*4.0;}
    let phi=6.2831855*rand.y;let disk=ss_disk_sample(radius,rand.x);let disk_r=disk.x;let disk_height=disk.y;
    let sc=trig_sincos_wide(phi);let disk_P=rounded_add3((disk_r*sc.y)*disk_T,(disk_r*sc.x)*disk_B);
    let ray_P=rounded_add3(rounded_add3(P,disk_N*disk_height),disk_P);let ray_D=-disk_N;
    // scene_intersect_local: intersection_ray_valid (the direction is a unit vector), then every hit of the object
    // recorded, at most BSSRDF_MAX_HITS kept by the reservoir.
    if((bitcast<u32>(ray_P.x)&0x7f800000u)==0x7f800000u){ss_terminate(index);return;}
    var geometry:SurfaceData;geometry.object_id=NONE;geometry.time=time;
//#if MOTION_OBJECT
    ss_motion_geometry(&geometry,id.x,object);if(load_word(p(17u))!=0u){return;}
//#endif
    let hits=raytrace_traverse(geometry,ray_P,ray_D,0.0,2.0*disk_height,object,NONE,NONE,AO_VISIBILITY_OBJECT_ALL,ss_disk_lcg(index),true);
    if(load_word(p(17u))!=0u){return;}
    let count=min(hits.count,SS_MAX_HITS);if(count==0u){ss_terminate(index);return;}
    // sort_intersections_and_normals (bubble sort by t).
    var order=array<u32,4>(0u,1u,2u,3u);var n=count;
    loop{
        var swapped=false;
        for(var j=0u;j+1u<n;j++){if(hits.t[order[j]]>hits.t[order[j+1u]]){let o=order[j];order[j]=order[j+1u];order[j+1u]=o;swapped=true;}}
        n--;if(!swapped||n==0u){break;}
    }
    let object_flag=load_word(table_at(G_object_flag,object,SIZE_object_flag));let applied=(object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
    var hit_object:SurfaceData;hit_object.object_id=object;hit_object.object_flag=object_flag;hit_object.time=time;
//#if MOTION_OBJECT
    // object_fetch_transform_motion_test: a moving object's transform at the ray's time and its inverse (the motion record).
    if((object_flag&SD_OBJECT_MOTION)!=0u){hit_object.motion_tfm=geometry.motion_tfm;hit_object.motion_itfm=geometry.motion_itfm;}
//#endif
    // bssrdf_pdf(radius, disk_r): the same for every hit.
    let disk_e=ss_disk_eval(radius,disk_r);let disk_pdf=rounded_divide(rounded_add(rounded_add(disk_e.x,disk_e.y),disk_e.z),ss_disk_channels(radius));
    var weights=array<vec3<f32>,4>(vec3<f32>(0),vec3<f32>(0),vec3<f32>(0),vec3<f32>(0));var normals=weights;var sum_weights=0.0;
    for(var k=0u;k<count;k++){
        let hit=order[k];
        // The hit's geometric normal as triangle_intersect_local records it, then as subsurface_disk turns it.
        let vertices=native_triangle_vertices(object,hits.prim[hit],hits.kind,time);if(vertices.error!=0u){return;}
        var hit_Ng=surface_native_normalize(rounded_cross(vertices.b-vertices.a,vertices.c-vertices.a));
        if((flag&SS_PATH_RAY_SUBSURFACE_BACKFACING)!=0u){hit_Ng=-hit_Ng;}
        if(applied&&(object_flag&SD_OBJECT_NEGATIVE_SCALE)!=0u){hit_Ng=-hit_Ng;}
        if(!applied){hit_Ng=surface_object_normal(hit_object,hit_Ng,false);}
        let hit_P=rounded_add3(ray_P,ray_D*hits.t[hit]);
        let pdf_N=pick.x*abs(rounded_dot(disk_N,hit_Ng));let pdf_T=pick.y*abs(rounded_dot(disk_T,hit_Ng));let pdf_B=pick.z*abs(rounded_dot(disk_B,hit_Ng));
        var w=rounded_divide(pdf_N,rounded_add(rounded_add(pdf_N*pdf_N,pdf_T*pdf_T),pdf_B*pdf_B));
        if(hits.count>SS_MAX_HITS){w*=rounded_divide(f32(hits.count),f32(SS_MAX_HITS));}
        let difference=hit_P-P;let r=rounded_sqrt(rounded_dot(difference,difference));
        // subsurface_disk_eval
        var eval=vec3<f32>(0);if(disk_pdf>0.0){eval=surf_native_div3(ss_disk_eval(radius,r),disk_pdf);}
        weights[k]=eval*w;normals[k]=hit_Ng;sum_weights=rounded_add(sum_weights,ss_disk_average(weights[k]));
    }
    if(sum_weights==0.0){ss_terminate(index);return;}
    // Importance resampling of one hit in proportion to its weight.
    let r=ss_rng(index,offset+SS_PRNG_DISK_RESAMPLE,1u).x*sum_weights;var partial=0.0;var selected=NONE;var sample_weight=0.0;
    for(var k=0u;k<count;k++){sample_weight=ss_disk_average(weights[k]);let next=rounded_add(partial,sample_weight);if(r<next){selected=k;break;}partial=next;}
    if(selected==NONE){ss_terminate(index);return;}
    let throughput=heap_vec3(state_address(S_path_throughput,index,12u));
    state_vec3(S_path_throughput,index,throughput*surf_native_div3(weights[selected]*sum_weights,sample_weight));
    // subsurface_scatter: the ray reversed towards the exit point (tmax 1 along the hit normal), the hit as the path's
    // intersection.
    let hit=order[selected];let exit_P=rounded_add3(ray_P,ray_D*hits.t[hit]);let exit_D=normals[selected];
//#if SUBSURFACE_VOLUME
    ss_volume_record(P,exit_P,state_word(S_isect_prim,index));
//#endif
    state_vec3(S_ray_P,index,rounded_add3(exit_P,(exit_D*1.0)*2.0));state_vec3(S_ray_D,index,-exit_D);
    state_f32(S_ray_tmin,index,0.0);state_f32(S_ray_tmax,index,1.0);state_f32(S_ray_dP,index,ray_dP);state_f32(S_ray_dD,index,0.0);
    state_f32(S_isect_t,index,hits.t[hit]);state_f32(S_isect_u,index,hits.u[hit]);state_f32(S_isect_v,index,hits.v[hit]);
    state_u32(S_isect_object,index,object);state_u32(S_isect_prim,index,hits.prim[hit]);state_u32(S_isect_type,index,hits.kind);
    state_u16(S_path_rng_offset,index,offset+RNG_BOUNCE);
//#if MNEE
    // use_caustics and a caustics receiver: integrator_path_next to INTERSECT_MNEE (unsorted).
    if(data_word(MNEE_D_use_caustics)!=0u&&(load_word(table_at(G_object_flag,object,SIZE_object_flag))&SD_OBJECT_CAUSTICS_RECEIVER)!=0u){
        let queue=load_word(p(7u)+S_queue_counter)/4u;atomicSub(&heap[queue+SS_K_INTERSECT],1u);atomicAdd(&heap[queue+MNEE_K_INTERSECT],1u);
        state_u16(S_path_queued_kernel,index,MNEE_K_INTERSECT);return;}
//#endif
    // integrator_path_next_sorted to SHADE_SURFACE or SHADE_SURFACE_RAYTRACE, keyed by the exit point's shader.
    let shader=load_word(table_at(G_tri_shader,hits.prim[hit],SIZE_tri_shader))&SHADER_MASK;
    let flags=load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags);
    let next=select(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,(flags&SD_HAS_RAYTRACE)!=0u);
    let divisor=load_word(p(7u)+S_sort_partition_divisor);let key=shader+data_word(D_max_shaders)*(index/divisor);
    if(key>=select(p(15u),p(16u),next==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE)){fail();return;}
    let sort=load_word(p(7u)+S_sort_key_counter+next*POINTER_SIZE);atomicAdd(&heap[(sort+key*4u)/4u],1u);state_u32(S_path_shader_sort_key,index,key);
    let counter=load_word(p(7u)+S_queue_counter)/4u;atomicSub(&heap[counter+SS_K_INTERSECT],1u);atomicAdd(&heap[counter+next],1u);
    state_u16(S_path_queued_kernel,index,next);
}
//#endif
