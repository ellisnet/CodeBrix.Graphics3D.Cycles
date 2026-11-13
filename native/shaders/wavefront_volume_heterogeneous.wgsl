// SPDX-FileCopyrightText: 2011-2025 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
//#if VOLUME_HETEROGENEOUS
// Pinned Cycles heterogeneous null scattering (integrator/shade_volume.h volume_integrate_heterogeneous): the majorant
// octree's hierarchical DDA (OctreeTracing), delta tracking with the reservoir of scatter candidates, the equiangular
// direct scatter with its transmittance (volume_transmittance's telescoping estimator) and the scatter probability
// guidance of primary rays. Every shader evaluation runs through one call site (vh_eval inside the loop of
// shade_volume_heterogeneous), driven by the state machine vh_step (compile envelope).
// The path volume stack for the shared octree tracer (wavefront_volume_octree.wgsl).
fn vh_stack_shader(index:u32,slot:u32)->u32{return load_word(volume_stack_address(index,slot,VS_shader));}
fn vh_stack_object(index:u32,slot:u32)->u32{return load_word(volume_stack_address(index,slot,VS_object));}
// Whether a path's volume stack holds a heterogeneous volume (volume_is_homogeneous<false> negated).
fn vh_is_heterogeneous(index:u32)->bool{
    let limit=data_word(D_volume_stack_size);
    for(var i=0u;i<limit;i++){
        let shader=load_word(volume_stack_address(index,i,VS_shader));if(shader==NONE){return false;}
        if((load_word(table_at(G_shaders,shader&SHADER_MASK,SIZE_shaders)+SH_flags)&VH_HETEROGENEOUS)!=0u){return true;}
    }
    return false;
}
// The shader coefficients at P over the path's volume stack (volume_shader_eval, closures merged as shade_volume):
// x sigma_t, y sigma_s (sum of the scatter closure weights), z emission; w (x: runtime flags, y: closure count,
// z: error).
struct VhCoefficients {sigma_t:vec3<f32>, sigma_s:vec3<f32>, emission:vec3<f32>, flag:u32, count:u32, error:u32}
//#if !VOLUME_LIGHT_PATH
fn vh_eval(index:u32,P:vec3<f32>,direction:vec3<f32>,base:u32)->VhCoefficients{
    var out:VhCoefficients;
    var sd:SurfaceData;sd.P=P;sd.N=-direction;sd.Ng=-direction;sd.wi=-direction;sd.prim=NONE;sd.primitive_type=V_PRIMITIVE_VOLUME;
    sd.time=heap_float(state_address(S_ray_time,index,4u));sd.runtime_flag=SR_IS_VOLUME_SHADER_EVAL;
    var r:SvmResult;r.runtime_flag=sd.runtime_flag;r.remaining=data_word(D_max_closures);r.closure_base=base;
    let limit=data_word(D_volume_stack_size);
    for(var slot=0u;slot<limit;slot++){
        sd.shader=load_word(volume_stack_address(index,slot,VS_shader));if(sd.shader==NONE){break;}
        sd.object_id=load_word(volume_stack_address(index,slot,VS_object));
        sd.shader_flag=load_word(table_at(G_shaders,sd.shader&SHADER_MASK,SIZE_shaders)+SH_flags);sd.object_flag=0u;
        if(sd.object_id!=NONE){sd.object_flag=load_word(table_at(G_object_flag,sd.object_id,SIZE_object_flag));}
//#if VOLUME_MOTION
        // volume_shader_eval_entry: shader_setup_object_transforms at the ray's time for a moving stack object.
        if((sd.object_flag&SD_OBJECT_MOTION)!=0u){sd.motion_tfm=motion_object(sd.object_id,sd.time);sd.motion_itfm=motion_inverse(sd.motion_tfm);}
//#endif
        sd.runtime_flag=r.runtime_flag;r=svm_evaluate_seeded(index,sd,r.remaining,r.closure_base,true,r);if(r.error!=0u){out.error=r.error;return out;}
        if(slot>0u){
            for(var a=0u;a<r.count;a++){let left=r.closure_base+a*SVM_CLOSURE_BYTES;var b=a+1u;
                while(b<r.count){let right=r.closure_base+b*SVM_CLOSURE_BYTES;
//#if VOLUME_PHASES else                     if(load_word(left+28u)==load_word(right+28u)&&heap_float(left+32u)==heap_float(right+32u)){volume_closure_add(left,right);
                    if(vp_equal(left,right)){volume_closure_add(left,right);
//#endif
                        for(var j=b;j+1u<r.count;j++){volume_closure_copy(r.closure_base+j*SVM_CLOSURE_BYTES,r.closure_base+(j+1u)*SVM_CLOSURE_BYTES);}r.count--;
                    }else{b++;}
                }
            }
        }
    }
    out.flag=r.runtime_flag;out.count=r.count;
    if((r.runtime_flag&SR_EXTINCTION)!=0u){out.sigma_t=r.transparency;}
    if((r.runtime_flag&SR_EMISSION)!=0u){out.emission=r.emission;}
    if((r.runtime_flag&SR_SCATTER)!=0u){for(var i=0u;i<r.count;i++){out.sigma_s+=svm_closure_read(r,i).weight;}}
    return out;
}
//#endif
//#if VOLUME_LIGHT_PATH
fn vh_eval(index:u32,P:vec3<f32>,direction:vec3<f32>,base:u32,only_object:u32,only_shader:u32)->VhCoefficients{
    var out:VhCoefficients;
    var sd:SurfaceData;sd.P=P;sd.N=-direction;sd.Ng=-direction;sd.wi=-direction;sd.prim=NONE;sd.primitive_type=V_PRIMITIVE_VOLUME;
    sd.time=heap_float(state_address(S_ray_time,index,4u));sd.runtime_flag=SR_IS_VOLUME_SHADER_EVAL;
    // volume_estimate_extrema evaluates one entry and allocates no closures (num_closure_left = 0).
    var r:SvmResult;r.runtime_flag=sd.runtime_flag;r.remaining=select(data_word(D_max_closures),0u,only_shader!=NONE);r.closure_base=base;
    let limit=data_word(D_volume_stack_size);
    for(var slot=0u;slot<limit;slot++){
        sd.shader=load_word(volume_stack_address(index,slot,VS_shader));if(sd.shader==NONE){break;}
        sd.object_id=load_word(volume_stack_address(index,slot,VS_object));
        if(only_shader!=NONE&&(sd.shader!=only_shader||sd.object_id!=only_object)){continue;}
        sd.shader_flag=load_word(table_at(G_shaders,sd.shader&SHADER_MASK,SIZE_shaders)+SH_flags);sd.object_flag=0u;
        if(sd.object_id!=NONE){sd.object_flag=load_word(table_at(G_object_flag,sd.object_id,SIZE_object_flag));}
//#if VOLUME_MOTION
        // volume_shader_eval_entry: shader_setup_object_transforms at the ray's time for a moving stack object.
        if((sd.object_flag&SD_OBJECT_MOTION)!=0u){sd.motion_tfm=motion_object(sd.object_id,sd.time);sd.motion_itfm=motion_inverse(sd.motion_tfm);}
//#endif
        sd.runtime_flag=r.runtime_flag;r=svm_evaluate_seeded(index,sd,r.remaining,r.closure_base,true,r);if(r.error!=0u){out.error=r.error;return out;}
        if(slot>0u){
            for(var a=0u;a<r.count;a++){let left=r.closure_base+a*SVM_CLOSURE_BYTES;var b=a+1u;
                while(b<r.count){let right=r.closure_base+b*SVM_CLOSURE_BYTES;
//#if VOLUME_PHASES else                     if(load_word(left+28u)==load_word(right+28u)&&heap_float(left+32u)==heap_float(right+32u)){volume_closure_add(left,right);
                    if(vp_equal(left,right)){volume_closure_add(left,right);
//#endif
                        for(var j=b;j+1u<r.count;j++){volume_closure_copy(r.closure_base+j*SVM_CLOSURE_BYTES,r.closure_base+(j+1u)*SVM_CLOSURE_BYTES);}r.count--;
                    }else{b++;}
                }
            }
        }
    }
    out.flag=r.runtime_flag;out.count=r.count;
    if((r.runtime_flag&SR_EXTINCTION)!=0u){out.sigma_t=r.transparency;}
    if((r.runtime_flag&SR_EMISSION)!=0u){out.emission=r.emission;}
    if((r.runtime_flag&SR_SCATTER)!=0u){for(var i=0u;i<r.count;i++){out.sigma_s+=svm_closure_read(r,i).weight;}}
    return out;
}
//#endif
fn vh_copy_closures(src_base:u32,dst_base:u32,count:u32){
    for(var i=0u;i<count*SVM_CLOSURE_BYTES/4u;i++){store_word(dst_base+i*4u,load_word(src_base+i*4u));}
}
//#if VOLUME_EMISSION
// film_write_volume_emission (its callers are VOLUME_EMISSION regions; the VE_ layout exists only there).
fn vh_write_emission(index:u32,base:u32,emission:vec3<f32>){
//#if LIGHT_LINKING
    if(!vll_emission_visible(index)){return;}
//#endif
    var value=select(emission,vec3<f32>(0),(bitcast<vec3<u32>>(emission)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
    let bounce=state_half(S_path_bounce,index);let limit=select(data_float(VE_D_sample_clamp_direct),data_float(VE_D_sample_clamp_indirect),i32(bounce)-1>0);
    let total=abs(value.x)+abs(value.y)+abs(value.z);if(total>limit){value*=limit/total;}
//#if VOLUME_SHADOW_CATCHER
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
fn vh_max(v:vec3<f32>)->f32{return max(v.x,max(v.y,v.z));}
fn vh_safe_divide(a:vec3<f32>,b:vec3<f32>,fallback:f32)->vec3<f32>{return select(vec3<f32>(fallback),vh_divide(a,b),b!=vec3<f32>(0));}
// volume_sample_channel_pdf with correctly rounded divisions.
fn vh_channel_pdf(albedo:vec3<f32>,throughput:vec3<f32>)->vec3<f32>{
    let weights=abs(throughput*albedo);let sum=rounded_add(rounded_add(weights.x,weights.y),weights.z);
    if(rounded_add(1.0,-sum)<1.0){return vh_divide(weights,vec3<f32>(sum));}return vec3<f32>(1.0/3.0);
}
// volume_sample_channel (vl_sample_channel) with correctly rounded divisions: (the channel, the rescaled number).
fn vh_sample_channel(albedo:vec3<f32>,throughput:vec3<f32>,rand:f32)->vec2<f32>{
    let pdf=vh_channel_pdf(albedo,throughput);var sum=0.0;
    for(var c=0u;c<3u;c++){if(rand<rounded_add(sum,pdf[c])){return vec2<f32>(f32(c),rounded_divide(rounded_add(rand,-sum),pdf[c]));}sum=rounded_add(sum,pdf[c]);}
    return vec2<f32>(2.0,rand);
}
// Correctly rounded component divisions (one loop, one rounded_divide copy).
fn vh_divide(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{var out:vec3<f32>;for(var k=0u;k<3u;k++){out[k]=rounded_divide(a[k],b[k]);}return out;}
// The integration state (VolumeIntegrateState, VolumeIntegrateResult, VolumeSampleReservoir, the transmittance
// estimator's counters) and the stage of vh_step.
struct VhState {
    o:VhOctree, stage:u32, resume:u32, t:f32, dt:f32, rscatter:f32, residual:f32, transmittance:f32, optical_depth:f32, step:u32,
    sigma_max:f32, step_sigma_max:f32, sample_dt:f32, distance_pdf:f32, equiangular_pdf:f32, majorant_scale:f32, scatter_prob:f32,
    direct_rr_scale:f32, emission:vec3<f32>, vspg:bool, method:u32, use_mis:bool, direct_t:f32, indirect_t:f32, direct_scatter:bool,
    indirect_scatter:bool, direct:vec3<f32>, indirect:vec3<f32>, res_total:f32, res_rand:f32, cand_emission:vec3<f32>, cand_t:f32,
    cand_throughput:vec3<f32>, cand_pdf:f32, offset:u32, tr_N:u32, tr_k:u32, tr_n:u32, tr_i:u32, tr_step:f32, tr_shade:f32,
    tr_t:vec2<f32>, tr_pmf:f32, tau_k:vec3<f32>, tau_j0:vec3<f32>, tau_j1:vec3<f32>, tau_j_1:vec3<f32>, T_k:vec3<f32>,
    origin:vec3<f32>, direction:vec3<f32>, tmax:f32, index:u32, sample:u32, pixel:u32, scratch:u32, direct_base:u32,
    indirect_base:u32, direct_count:u32, indirect_count:u32, terminate:bool, request:f32, setup_from:u32, trans_resume:u32,
    add:bool, add_weight:f32, add_t:f32, add_throughput:vec3<f32>, add_pdf:f32, copy_direct:bool, copy_indirect:bool, copy_count:u32, add_clamp:bool, film:u32, add_guided:f32, add_unguided:f32, add_sample_distance:bool,
//#if FILM_DENOISING
    // Denoising features: the path writes them, vstate.albedo and the reservoir candidate's albedo.
    denoise:bool, albedo:vec3<f32>, cand_albedo:vec3<f32>,
//#endif
//#if VOLUME_LIGHT_PATH
    // Light Path extrema: the path's visibility, the resumable volume_octree_setup (done, result, entry index, skipped entry,
    // the entry's local octree) and the estimate in progress (its entry is the one vh_eval evaluates; NONE: the stack).
    visibility:u32, set_done:bool, set_ok:bool, set_i:u32, set_skip_object:u32, set_skip_shader:u32, set_local:VhOctree, est:VlpEstimate,
    estimating:bool,
//#endif
}
const VH_INIT=0u;const VH_ADVANCE=1u;const VH_ADVANCE_WHILE=2u;const VH_ADVANCE_AFTER=3u;const VH_STEP=4u;const VH_STEP_EVAL=5u;
const VH_FINAL=6u;const VH_FINAL_EVAL=7u;const VH_DIRECT=8u;const VH_DIRECT_EVAL=9u;const VH_TRANS=10u;const VH_DONE=11u;
const VH_SETUP=12u;const VH_TRANS_START=13u;const VH_FINAL2=14u;
// volume_reservoir add_sample.
fn vh_reservoir_add(st:ptr<function,VhState>,weight:f32,emission:vec3<f32>,t:f32,throughput:vec3<f32>,pdf:f32){
    if(!(weight>0.0)){return;}
    (*st).res_total+=weight;let thresh=rounded_divide(weight,(*st).res_total);
//#if FILM_DENOISING else     if(((*st).res_rand<=thresh)||((*st).res_total==weight)){(*st).cand_emission=emission;(*st).cand_t=t;(*st).cand_throughput=throughput;(*st).cand_pdf=pdf;(*st).res_rand=rounded_divide((*st).res_rand,thresh);}
    if(((*st).res_rand<=thresh)||((*st).res_total==weight)){(*st).cand_emission=emission;(*st).cand_t=t;(*st).cand_throughput=throughput;(*st).cand_pdf=pdf;(*st).res_rand=rounded_divide((*st).res_rand,thresh);(*st).cand_albedo=(*st).albedo;}
//#endif
    else{(*st).res_rand=rounded_divide((*st).res_rand-thresh,1.0-thresh);}
    (*st).res_rand=clamp((*st).res_rand,0.0,1.0);
}
// volume_equiangular_transmittance: starts the transmittance estimator (VH_TRANS) when it applies; returns false
// when no shader evaluation is needed.
fn vh_equiangular_transmittance(st:ptr<function,VhState>,interval:vec2<f32>,sigma:vec2<f32>,resume:u32)->bool{
    (*st).stage=resume;
    if((*st).method!=2u||(*st).use_mis||(*st).direct_scatter){return false;}
    var t=interval;if((*st).direct_t>=interval.x&&(*st).direct_t<=interval.y){t=vec2<f32>(interval.x,(*st).direct_t);(*st).direct_scatter=true;}
    let sigma_c=sigma.y-sigma.x;let length=t.y-t.x;
    // roundf (halves away from zero).
    let expected=sigma_c*length;let whole=floor(expected);let k=u32(clamp(i32(select(whole,whole+1.0,expected-whole>=0.5)),1,VH_MAX_STEPS));
//#if VOLUME_SAMPLING_PATTERN else     let rand=native_rng((*st).sample,0u,(*st).offset+VH_PRNG_EXPANSION_ORDER).x;
    let rand=native_rng_pattern((*st).sample,0u,(*st).offset+VH_PRNG_EXPANSION_ORDER,1u).x;
//#endif
    var N=1u;(*st).tr_pmf=1.0;
    if(sigma_c!=0.0){
        // sample_geometric_distribution(rand, 0.9, pmf, 4).
        let r=vh_geometric_r();let q=1.0-r;let n=min(i32(floor(log(rand)/log(q))),4);
        (*st).tr_pmf=select(r*pow(q,f32(n)),pow(q,f32(n)),n==4);N=1u<<u32(max(n,0));
    }
    (*st).tr_N=N;(*st).tr_k=k;(*st).tr_n=0u;(*st).tr_i=0u;(*st).tr_t=t;
//#if VOLUME_SAMPLING_PATTERN else     (*st).tr_shade=native_rng((*st).sample,(*st).pixel,(*st).offset+VH_PRNG_SHADE_OFFSET).x;(*st).tr_step=length/f32(N*k);
    (*st).tr_shade=native_rng_pattern((*st).sample,(*st).pixel,(*st).offset+VH_PRNG_SHADE_OFFSET,1u).x;(*st).tr_step=length/f32(N*k);
//#endif
    (*st).tau_k=vec3<f32>(0);(*st).tau_j0=vec3<f32>(0);(*st).tau_j1=vec3<f32>(0);(*st).tau_j_1=vec3<f32>(0);(*st).T_k=vec3<f32>(0);
    (*st).stage=VH_TRANS;(*st).resume=resume;return true;
}
fn vh_trans_t(st:VhState)->f32{let step=st.tr_i*st.tr_N+st.tr_n;return min(st.tr_t.y,st.tr_t.x+(st.tr_shade+f32(step))*st.tr_step);}
// The next shader evaluation: returns its distance along the ray (NaN-free), or a negative value when done.
fn vh_step(st:ptr<function,VhState>,c:VhCoefficients,have:bool)->f32{
    var evaluated=have;
    for(var guard=0u;guard<65536u;guard++){
        let stage=(*st).stage;
            if(stage==10u){ // VH_TRANS: one extinction sample per evaluation.
                if(!evaluated){return vh_trans_t(*st);}evaluated=false;
                let step=(*st).tr_i*(*st).tr_N+(*st).tr_n;
                if((*st).tr_N==1u){(*st).tau_k+=c.sigma_t;}
                else{let tau=(*st).tr_step*c.sigma_t;(*st).tau_k+=tau*f32((*st).tr_N);
                    if(step%2u==0u){(*st).tau_j0+=tau*2.0;}else{(*st).tau_j1+=tau*2.0;}(*st).tau_j_1+=tau;}
                (*st).tr_i++;
                if((*st).tr_i<(*st).tr_k){continue;}
                if((*st).tr_N==1u){
                    let T=select(exp(-(*st).tau_k*(*st).tr_step),vec3<f32>(1),all((*st).tau_k==vec3<f32>(0)));(*st).direct*=T;(*st).stage=(*st).resume;continue;
                }
                (*st).T_k+=exp(-(*st).tau_k);(*st).tau_k=vec3<f32>(0);(*st).tr_i=0u;(*st).tr_n++;
                if((*st).tr_n<(*st).tr_N){continue;}
                let T_j_1=exp(-(*st).tau_j_1);let T_k=(*st).T_k/f32((*st).tr_N);let T_j=0.5*(exp(-(*st).tau_j0)+exp(-(*st).tau_j1));
                (*st).direct*=T_k+(T_j_1-T_j)/(*st).tr_pmf;(*st).stage=(*st).resume;
            }
            else if(stage==1u){ // VH_ADVANCE: volume_integrate_advance.
                if((*st).step>VH_MAX_STEPS_U){(*st).stage=VH_FINAL;continue;}(*st).step++;
//#if VOLUME_SAMPLING_PATTERN else                 (*st).rscatter=native_rng((*st).sample,(*st).pixel,(*st).offset+V_PRNG_VOLUME_SCATTER_DISTANCE).x;(*st).stage=VH_ADVANCE_WHILE;
                (*st).rscatter=native_rng_pattern((*st).sample,(*st).pixel,(*st).offset+V_PRNG_VOLUME_SCATTER_DISTANCE,1u).x;(*st).stage=VH_ADVANCE_WHILE;
//#endif
            }
            else if(stage==2u){ // VH_ADVANCE_WHILE: volume_indirect_scatter_advance, then the octree advance.
                let equiangular=((*st).method==2u)&&(*st).use_mis;
                let sigma_max=(*st).o.sigma.y*(*st).majorant_scale;
                (*st).residual=((*st).o.t.y-(*st).t)*sigma_max;
                var advance=true;
                if(sigma_max!=0.0){
                    (*st).dt=rounded_divide(-log(1.0-(*st).rscatter),sigma_max);(*st).t+=(*st).dt;
                    let has=equiangular&&((*st).direct_t>=(*st).o.t.x&&(*st).direct_t<=(*st).o.t.y);
                    if(has&&(*st).t>(*st).direct_t&&!(*st).direct_scatter){
                        (*st).direct_scatter=true;(*st).direct=(*st).indirect*(*st).transmittance*(*st).direct_rr_scale;
                        (*st).sample_dt=(*st).direct_t-(*st).t+(*st).dt;(*st).distance_pdf=(*st).transmittance*sigma_max;(*st).sigma_max=sigma_max;
                    }
                    advance=(*st).t>(*st).o.t.y;
                }
                if(!advance){(*st).offset+=VH_PRNG_BOUNCE_NUM;(*st).step_sigma_max=(*st).o.sigma.y*(*st).majorant_scale;(*st).stage=VH_STEP;continue;}
                var o=(*st).o;let moved=vh_octree_advance((*st).index,(*st).origin,(*st).direction,(*st).tmax,&o);(*st).o=o;if(!moved){(*st).stage=VH_FINAL;continue;}
//#if VOLUME_LIGHT_PATH
                // volume_octree_advance: the advanced octree's extrema, estimated (stage 18 resumes).
                if(vlp_estimates((*st).visibility,(*st).o.shader)){(*st).est=vlp_start((*st).o.t,(*st).o.object,(*st).o.shader,(*st).sample,(*st).pixel,(*st).offset,18u);(*st).estimating=true;(*st).stage=15u;continue;}
//#endif
                (*st).setup_from=1u;(*st).stage=VH_SETUP;
            }
            else if(stage==3u){ // VH_ADVANCE_AFTER: the residual optical depth carries the random number over.
                (*st).rscatter=clamp(1.0-(1.0-(*st).rscatter)*exp((*st).residual),0.0,1.0);(*st).stage=VH_ADVANCE_WHILE;
            }
            else if(stage==4u){ // VH_STEP: volume_russian_roulette_termination, then the shader at t.
                if((*st).direct_scatter&&(*st).indirect_scatter){(*st).stage=VH_FINAL;continue;}
                let thresh=vh_max(abs((*st).indirect));
                if(thresh<=0.05){
                    let equiangular=((*st).method==2u)&&(*st).use_mis&&!(*st).direct_scatter;
                    let samples=!((*st).res_total==0.0)&&!equiangular;
                    if((*st).terminate||samples){
                        if((*st).res_rand>thresh){
                            (*st).indirect=vec3<f32>(0);if(equiangular||(*st).method==1u){(*st).direct=vec3<f32>(0);}
                            // The step returns without a shader evaluation; then volume_integrate_should_stop.
                            if(all((*st).direct==vec3<f32>(0))||((*st).direct_scatter&&(*st).indirect_scatter)){(*st).stage=VH_FINAL;}else{(*st).stage=VH_ADVANCE;}
                            continue;
                        }
                        (*st).res_rand=clamp(rounded_divide((*st).res_rand,thresh),0.0,1.0);(*st).indirect*=rounded_divide(1.0,thresh);
                    }
                    if(equiangular){
                        if((*st).res_rand>thresh){(*st).direct_scatter=true;(*st).direct=vec3<f32>(0);(*st).res_rand=rounded_divide((*st).res_rand-thresh,1.0-thresh);}
                        else{(*st).res_rand=rounded_divide((*st).res_rand,thresh);(*st).direct_rr_scale=rounded_divide((*st).direct_rr_scale,thresh);}
                        (*st).res_rand=clamp((*st).res_rand,0.0,1.0);
                    }
                }
                (*st).stage=VH_STEP_EVAL;return (*st).t;
            }
            else if(stage==5u){ // VH_STEP_EVAL: volume_integrate_step_scattering with the coefficients at t.
                if(!evaluated){return (*st).t;}evaluated=false;
                if((c.flag&(SR_EXTINCTION|SR_SCATTER|SR_EMISSION))!=0u){
                    let sigma_max=(*st).step_sigma_max;let majorant=max(vh_max(c.sigma_t),sigma_max);let sigma_n=vec3<f32>(majorant)-c.sigma_t;
                    if(majorant!=sigma_max){(*st).indirect*=rounded_divide(exp((sigma_max-majorant)*(*st).dt),sigma_max);}else{(*st).indirect*=rounded_divide(1.0,majorant);}
                    if((c.flag&SR_EMISSION)!=0u){(*st).emission=rounded_add3((*st).emission,(*st).indirect*c.emission);}
                    if(c.sigma_s.x+c.sigma_s.y+c.sigma_s.z==0.0){(*st).indirect*=sigma_n;}
                    else{
//#if FILM_DENOISING
                        if((*st).denoise){(*st).albedo=rounded_add3((*st).albedo,(*st).indirect*c.sigma_s);}
//#endif
                        let albedo=vh_safe_divide(c.sigma_s,c.sigma_t,1.0);let channel_pdf=vr_channel_pdf(albedo,(*st).indirect);
                        let prob_s=rounded_dot(vh_divide(c.sigma_s,c.sigma_s+sigma_n),channel_pdf);
                        let weight=(*st).transmittance*prob_s;let throughput=vr_div3((*st).indirect*c.sigma_s,prob_s);
                        if((*st).vspg){(*st).add=true;(*st).add_weight=weight;(*st).add_t=(*st).t;(*st).add_throughput=throughput;(*st).add_pdf=weight*sigma_max;}
                        else if(!(*st).indirect_scatter){
                            if((*st).res_rand<=prob_s){
                                (*st).res_rand=rounded_divide((*st).res_rand,prob_s);(*st).indirect_scatter=true;
                                (*st).copy_indirect=true;(*st).copy_count=c.count;
                                (*st).add=true;(*st).add_clamp=true;(*st).add_weight=weight;(*st).add_t=(*st).t;(*st).add_throughput=throughput;(*st).add_pdf=weight*sigma_max;
                                if((*st).method==1u){(*st).direct_scatter=true;(*st).copy_direct=true;}
                            }else{(*st).res_rand=clamp(rounded_divide((*st).res_rand-prob_s,1.0-prob_s),0.0,1.0);}
                        }
                        let prob_n=1.0-prob_s;(*st).indirect*=select(vec3<f32>(0),vr_div3(sigma_n,prob_n),vec3<f32>(prob_n)!=vec3<f32>(0));
                        (*st).transmittance*=prob_n;
                    }
                }
                // volume_integrate_should_stop.
                if((all((*st).indirect==vec3<f32>(0))&&all((*st).direct==vec3<f32>(0)))||((*st).direct_scatter&&(*st).indirect_scatter)){(*st).stage=VH_FINAL;}
                else{(*st).stage=VH_ADVANCE;}
            }
            else if(stage==6u){ // VH_FINAL: volume_distance_sampling_finalize.
                (*st).stage=VH_DIRECT;
                if((*st).res_total==0.0){continue;}
                let sample_distance=!(*st).terminate&&((*st).method==1u);
                if(!(*st).vspg){
                    (*st).indirect=(*st).cand_throughput;(*st).emission=(*st).cand_emission;(*st).indirect_t=(*st).cand_t;
//#if FILM_DENOISING
                    (*st).albedo=(*st).cand_albedo;
//#endif
                    if(sample_distance){(*st).direct_scatter=true;(*st).direct_t=(*st).indirect_t;(*st).direct=(*st).indirect;if((*st).use_mis){(*st).distance_pdf=(*st).cand_pdf;}}
                    continue;
                }
                if(sample_distance){(*st).direct=(*st).cand_throughput*(*st).res_total;(*st).distance_pdf=(*st).cand_pdf;}
                // util/math_base.h mix(a, b, t) = a + t * (b - a), each operation rounded once (WGSL's built-in mix is not that form).
                (*st).emission=rounded_add3((*st).emission,(*st).res_total*rounded_add3((*st).cand_emission,-(*st).emission));
//#if FILM_DENOISING
                (*st).albedo=rounded_add3((*st).albedo,(*st).res_total*rounded_add3((*st).cand_albedo,-(*st).albedo));
//#endif
                let unguided=(*st).res_total;var guided:f32;
                if(all((*st).indirect==vec3<f32>(0))){guided=1.0;}
                else{(*st).res_total=rounded_add((*st).res_total,0.75*rounded_add((*st).scatter_prob,-(*st).res_total));guided=(*st).res_total;
                    (*st).add=true;(*st).add_weight=1.0-guided;(*st).add_t=(*st).cand_t;(*st).add_throughput=(*st).indirect;(*st).add_pdf=0.0;}
                (*st).add_guided=guided;(*st).add_unguided=unguided;(*st).add_sample_distance=sample_distance;(*st).stage=VH_FINAL2;
            }
            else if(stage==14u){ // VH_FINAL2: after the guided sample joined the reservoir.
                (*st).stage=VH_DIRECT;
                let guided=(*st).add_guided;let unguided=(*st).add_unguided;let sample_distance=(*st).add_sample_distance;
                let scatter=(*st).cand_pdf>0.0;
                let scale=select(rounded_divide(1.0-unguided,1.0-guided),rounded_divide(unguided,guided),scatter);
                (*st).indirect=(*st).cand_throughput*scale;
                if(!scatter&&!sample_distance){continue;}
                (*st).request=select(0.0,1.0,scatter)+select(0.0,2.0,sample_distance);(*st).stage=VH_FINAL_EVAL;return (*st).cand_t;
            }
            else if(stage==7u){ // VH_FINAL_EVAL: the phases at the chosen candidate.
                if(!evaluated){return (*st).cand_t;}evaluated=false;
                if((c.flag&(SR_EXTINCTION|SR_SCATTER|SR_EMISSION))==0u){(*st).stage=VH_DIRECT;continue;}
                if(((*st).request==2.0||(*st).request==3.0)){(*st).direct_scatter=true;(*st).direct_t=(*st).cand_t;(*st).copy_direct=true;}
                if(((*st).request==1.0||(*st).request==3.0)){(*st).indirect_scatter=true;(*st).indirect_t=(*st).cand_t;(*st).copy_indirect=true;}
                (*st).copy_count=c.count;
                (*st).stage=VH_DIRECT;
            }
            else if(stage==8u){ // VH_DIRECT: volume_equiangular_direct_scatter.
                if((*st).method!=2u||!(*st).direct_scatter){(*st).stage=VH_DONE;continue;}
                (*st).stage=VH_DIRECT_EVAL;return (*st).direct_t;
            }
            else if(stage==9u){
                if(!evaluated){return (*st).direct_t;}evaluated=false;(*st).stage=VH_DONE;
                if((c.flag&(SR_EXTINCTION|SR_SCATTER|SR_EMISSION))!=0u&&(c.flag&SR_SCATTER)!=0u){
                    (*st).copy_direct=true;(*st).copy_count=c.count;
                    if((*st).use_mis){
                        let majorant=max(vh_max(c.sigma_t),(*st).sigma_max);let sigma_n=vec3<f32>(majorant)-c.sigma_t;
                        if(((*st).sample_dt!=bitcast<f32>(0x7f7fffffu))&&(majorant!=(*st).sigma_max)){(*st).direct*=exp(((*st).sigma_max-majorant)*(*st).sample_dt);}
                        let albedo=vh_safe_divide(c.sigma_s,c.sigma_t,1.0);let channel_pdf=vr_channel_pdf(albedo,(*st).direct);
                        (*st).distance_pdf*=rounded_dot(vh_divide(c.sigma_s,c.sigma_s+sigma_n),channel_pdf);
                    }
                    (*st).direct*=select(vec3<f32>(0),vr_div3(c.sigma_s,(*st).equiangular_pdf),vec3<f32>((*st).equiangular_pdf)!=vec3<f32>(0));
                }else{(*st).direct_scatter=false;}
            }
            else if(stage==12u){ // VH_SETUP: volume_octree_setup<false> (after the first segment or an octree advance).
//#if VOLUME_LIGHT_PATH else                 var o=(*st).o;let ok=vh_octree_setup((*st).index,(*st).origin,(*st).direction,(*st).tmax,&o);(*st).o=o;
                // The resumable set-up (stages 16 and 17): its entries' estimates need evaluations.
                if(!(*st).set_done){
                    if((*st).o.no_overlap){(*st).set_ok=(*st).o.t.x<(*st).o.t.y;(*st).set_done=true;}
                    else{(*st).set_i=0u;(*st).set_skip_object=(*st).o.object;(*st).set_skip_shader=(*st).o.shader;(*st).stage=16u;}
                    continue;
                }
                (*st).set_done=false;let ok=(*st).set_ok;
//#endif
                if((*st).setup_from==1u){
                    if(!ok){(*st).stage=VH_FINAL;continue;}
                    (*st).optical_depth+=(*st).o.sigma.y*((*st).o.t.y-(*st).o.t.x);(*st).t=(*st).o.t.x;(*st).trans_resume=VH_ADVANCE_AFTER;(*st).stage=VH_TRANS_START;continue;
                }
                if(!ok){(*st).stage=VH_DONE;continue;}
                (*st).optical_depth=(*st).o.sigma.y*((*st).o.t.y-(*st).o.t.x);(*st).stage=VH_INIT;
            }
            else if(stage==0u){ // VH_INIT: the guidance, the reservoir's random number and the scrambled sequence.
                if((*st).vspg){
                    let L_s=volume_rgbe((*st).film+data_word(V_film_pass_volume_scatter_denoised)*4u);let L_t=volume_rgbe((*st).film+data_word(V_film_pass_volume_transmit_denoised)*4u);
                    let scattered=L_s.x+L_s.y+L_s.z;let volume=(L_t.x+L_t.y+L_t.z)+scattered;
                    (*st).scatter_prob=select(min(rounded_divide(scattered,volume),0.9999),0.5,volume==0.0);
                    let count_pass=heap_float((*st).film+data_word(D_film_pass_volume_majorant_sample_count)*4u);var depth=bitcast<f32>(0x7f7fffffu);
                    if(count_pass!=0.0){depth=rounded_divide(heap_float((*st).film+data_word(D_film_pass_volume_majorant)*4u),count_pass);}
                    (*st).majorant_scale=select(rounded_divide(-vh_fast_log(1.0-(*st).scatter_prob),depth),1.0,depth==0.0);
                    if((*st).majorant_scale<1.0){(*st).majorant_scale=1.0;let d=1.0-vh_fast_exp(-depth);(*st).scatter_prob=select(0.0,rounded_divide((*st).scatter_prob,d),d!=0.0);}
                    else{(*st).scatter_prob=1.0;}
                }
//#if VOLUME_SAMPLING_PATTERN else                 (*st).res_rand=native_rng((*st).sample,(*st).pixel,(*st).offset+V_PRNG_VOLUME_RESERVOIR).x;
                (*st).res_rand=native_rng_pattern((*st).sample,(*st).pixel,(*st).offset+V_PRNG_VOLUME_RESERVOIR,1u).x;
//#endif
                (*st).offset=vh_scramble((*st).offset,0xe35fad82u);
                (*st).trans_resume=VH_ADVANCE;(*st).stage=VH_TRANS_START;
            }
            else if(stage==13u){ // VH_TRANS_START: volume_equiangular_transmittance of the current segment.
                vh_equiangular_transmittance(st,(*st).o.t,(*st).o.sigma,(*st).trans_resume);
            }
//#if VOLUME_LIGHT_PATH
            else if(stage==15u){ // volume_estimate_extrema: the entry's evaluations along its octree segment.
                if(!evaluated){return vlp_shade_t((*st).est);}evaluated=false;
                var e=(*st).est;let done=vlp_add(&e,c.sigma_t,c.emission);(*st).est=e;
                if(done){(*st).estimating=false;(*st).stage=(*st).est.resume;}
            }
            else if(stage==16u){ // volume_octree_setup<false>: the next stack entry's octree.
                let limit=data_word(D_volume_stack_size);var shader=NONE;if((*st).set_i<limit){shader=vh_stack_shader((*st).index,(*st).set_i);}
                if(shader==NONE){
                    if((*st).set_i==1u){(*st).o.no_overlap=true;}
                    (*st).set_ok=(*st).o.node!=NONE&&(*st).o.t.x<(*st).o.t.y;(*st).set_done=true;(*st).stage=VH_SETUP;continue;
                }
                let object=vh_stack_object((*st).index,(*st).set_i);
                if(object==(*st).set_skip_object&&shader==(*st).set_skip_shader){(*st).set_i++;continue;}
                (*st).set_local=vlp_local((*st).index,(*st).origin,(*st).direction,(*st).tmax,(*st).o.t.x,object,shader);
                if(vlp_estimates((*st).visibility,shader)){(*st).est=vlp_start((*st).set_local.t,object,shader,(*st).sample,(*st).pixel,(*st).offset,17u);(*st).estimating=true;(*st).stage=15u;continue;}
                (*st).est.ext=vh_extrema((*st).set_local);(*st).stage=17u;
            }
            else if(stage==17u){ // The entry's extrema join the active segment's; the entry with the smallest exit becomes active.
                (*st).o.sigma=(*st).o.sigma+(*st).est.ext;var local=(*st).set_local;
                if(local.t.y<=(*st).o.t.y){local.sigma=(*st).o.sigma;(*st).o=local;}
                (*st).set_i++;(*st).stage=16u;
            }
            else if(stage==18u){ // volume_octree_advance's estimated extrema, then the set-up.
                (*st).o.sigma=(*st).est.ext;(*st).setup_from=1u;(*st).stage=VH_SETUP;
            }
//#endif
            else{return -1.0;}
        // One call site each: the reservoir sample and the closure copies of the stage that just ran.
        if((*st).add){vh_reservoir_add(st,(*st).add_weight,(*st).emission,(*st).add_t,(*st).add_throughput,(*st).add_pdf);(*st).add=false;
            if((*st).add_clamp){(*st).res_rand=clamp((*st).res_rand,0.0,1.0);(*st).add_clamp=false;}}
        for(var k=0u;k<2u;k++){
            let wanted=select((*st).copy_indirect,(*st).copy_direct,k==0u);if(!wanted){continue;}
            vh_copy_closures((*st).scratch,select((*st).indirect_base,(*st).direct_base,k==0u),(*st).copy_count);
            if(k==0u){(*st).direct_count=(*st).copy_count;(*st).copy_direct=false;}else{(*st).indirect_count=(*st).copy_count;(*st).copy_indirect=false;}
        }
    }
    return -1.0;
}
// util/math_fast.h fast_logf and fast_expf (as the Mie fit's helpers).
fn vh_madd(a:f32,b:f32,c:f32)->f32{return rounded_add(a*b,c);}
fn vh_fast_log(x_in:f32)->f32{
    let x=clamp(x_in,1.17549435e-38,3.40282347e+38);let bits=bitcast<u32>(x);let exponent=f32(i32(bits>>23u)-127);
    let f=rounded_add(bitcast<f32>((bits&0x007FFFFFu)|0x3f800000u),-1.0);let f2=f*f;let f4=f2*f2;
    var hi=vh_madd(f,-0.00931049621349,0.05206469089414);var lo=vh_madd(f,0.47868480909345,-0.72116591947498);
    hi=vh_madd(f,hi,-0.13753123777116);hi=vh_madd(f,hi,0.24187369696082);hi=vh_madd(f,hi,-0.34730547155299);lo=vh_madd(f,lo,1.442689881667200);
    return rounded_add(rounded_add(f4*hi,f*lo),exponent)*bitcast<f32>(0x3f317218u);
}
fn vh_fast_exp(x_in:f32)->f32{
    var x=clamp(rounded_divide(x_in,bitcast<f32>(0x3f317218u)),-126.0,126.0);let m=i32(x);x=rounded_add(x,-f32(m));x=rounded_add(1.0,-rounded_add(1.0,-x));
    var r=1.33336498402e-3;r=vh_madd(x,r,9.810352697968e-3);r=vh_madd(x,r,5.551834031939e-2);r=vh_madd(x,r,0.2401793301105);
    r=vh_madd(x,r,0.693144857883);r=vh_madd(x,r,1.0);return bitcast<f32>(bitcast<u32>(r)+(u32(m)<<23u));
}
// DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME for paths whose volume stack holds a heterogeneous volume
// (volume_integrate_null_scattering with volume_integrate_heterogeneous, then volume_integrate_event): shade_volume
// leaves these paths to this entry. Closures: the evaluation scratch is the input's shade_volume slot (push word 23),
// the direct and indirect phases the input's slots in the second and third thirds of that arena (record words 76 and
// 80 name them for shade_volume_light and shade_volume_phase).
// native/DEVICE.md 52: the null-scattering integration is split at the pinned loop's shader evaluation
// (volume_integrate_heterogeneous calls volume_shader_eval once per step) into two entries that alternate:
// shade_volume_heterogeneous runs the state machine (vh_step) until it asks for an evaluation, stores the state and the
// request in the input's split record (push word 12) and counts the request; shade_volume_heterogeneous_eval evaluates
// the shader there (vh_eval). The host repeats the pair until no input asks (volume.cpp). The integration's operations
// and their order are those of the single loop; the record holds every VhState field.
const VH_SPLIT_FRESH=0u;const VH_SPLIT_EVAL=1u;const VH_SPLIT_RESUME=2u;const VH_SPLIT_DONE=3u;
const VH_SPLIT_STATE=160u;const VH_SPLIT_BYTES=VH_SPLIT_STATE+VH_STATE_WORDS*4u;
fn vhs_f(a:u32,v:f32){store_word(a,bitcast<u32>(v));}
fn vhs_3(a:u32,v:vec3<f32>){vhs_f(a,v.x);vhs_f(a+4u,v.y);vhs_f(a+8u,v.z);}
// VhState at fixed word offsets (168 words): written before an evaluation request, read when the step resumes.
const VH_STATE_WORDS:u32=168u;
fn vh_state_save(at:u32,st:ptr<function,VhState>){
    store_word(at+0u,(*st).o.node);vhs_f(at+4u,(*st).o.t.x);vhs_f(at+8u,(*st).o.t.y);vhs_3(at+12u,(*st).o.ray_P);vhs_3(at+24u,(*st).o.ray_D);store_word(at+36u,(*st).o.current.x);store_word(at+40u,(*st).o.current.y);store_word(at+44u,(*st).o.current.z);store_word(at+48u,(*st).o.object);store_word(at+52u,(*st).o.shader);store_word(at+56u,(*st).o.scale);store_word(at+60u,(*st).o.next_scale);store_word(at+64u,(*st).o.octant_mask);store_word(at+68u,select(0u,1u,(*st).o.no_overlap));vhs_f(at+72u,(*st).o.sigma.x);vhs_f(at+76u,(*st).o.sigma.y);store_word(at+80u,(*st).stage);store_word(at+84u,(*st).resume);vhs_f(at+88u,(*st).t);vhs_f(at+92u,(*st).dt);vhs_f(at+96u,(*st).rscatter);vhs_f(at+100u,(*st).residual);vhs_f(at+104u,(*st).transmittance);vhs_f(at+108u,(*st).optical_depth);store_word(at+112u,(*st).step);vhs_f(at+116u,(*st).sigma_max);vhs_f(at+120u,(*st).step_sigma_max);vhs_f(at+124u,(*st).sample_dt);vhs_f(at+128u,(*st).distance_pdf);vhs_f(at+132u,(*st).equiangular_pdf);vhs_f(at+136u,(*st).majorant_scale);vhs_f(at+140u,(*st).scatter_prob);vhs_f(at+144u,(*st).direct_rr_scale);vhs_3(at+148u,(*st).emission);store_word(at+160u,select(0u,1u,(*st).vspg));store_word(at+164u,(*st).method);store_word(at+168u,select(0u,1u,(*st).use_mis));vhs_f(at+172u,(*st).direct_t);vhs_f(at+176u,(*st).indirect_t);store_word(at+180u,select(0u,1u,(*st).direct_scatter));store_word(at+184u,select(0u,1u,(*st).indirect_scatter));vhs_3(at+188u,(*st).direct);vhs_3(at+200u,(*st).indirect);vhs_f(at+212u,(*st).res_total);vhs_f(at+216u,(*st).res_rand);vhs_3(at+220u,(*st).cand_emission);vhs_f(at+232u,(*st).cand_t);vhs_3(at+236u,(*st).cand_throughput);vhs_f(at+248u,(*st).cand_pdf);store_word(at+252u,(*st).offset);store_word(at+256u,(*st).tr_N);store_word(at+260u,(*st).tr_k);store_word(at+264u,(*st).tr_n);store_word(at+268u,(*st).tr_i);vhs_f(at+272u,(*st).tr_step);vhs_f(at+276u,(*st).tr_shade);vhs_f(at+280u,(*st).tr_t.x);vhs_f(at+284u,(*st).tr_t.y);vhs_f(at+288u,(*st).tr_pmf);vhs_3(at+292u,(*st).tau_k);vhs_3(at+304u,(*st).tau_j0);vhs_3(at+316u,(*st).tau_j1);vhs_3(at+328u,(*st).tau_j_1);vhs_3(at+340u,(*st).T_k);vhs_3(at+352u,(*st).origin);vhs_3(at+364u,(*st).direction);vhs_f(at+376u,(*st).tmax);store_word(at+380u,(*st).index);store_word(at+384u,(*st).sample);store_word(at+388u,(*st).pixel);store_word(at+392u,(*st).scratch);store_word(at+396u,(*st).direct_base);store_word(at+400u,(*st).indirect_base);store_word(at+404u,(*st).direct_count);store_word(at+408u,(*st).indirect_count);store_word(at+412u,select(0u,1u,(*st).terminate));vhs_f(at+416u,(*st).request);store_word(at+420u,(*st).setup_from);store_word(at+424u,(*st).trans_resume);store_word(at+428u,select(0u,1u,(*st).add));vhs_f(at+432u,(*st).add_weight);vhs_f(at+436u,(*st).add_t);vhs_3(at+440u,(*st).add_throughput);vhs_f(at+452u,(*st).add_pdf);store_word(at+456u,select(0u,1u,(*st).copy_direct));store_word(at+460u,select(0u,1u,(*st).copy_indirect));store_word(at+464u,(*st).copy_count);store_word(at+468u,select(0u,1u,(*st).add_clamp));store_word(at+472u,(*st).film);vhs_f(at+476u,(*st).add_guided);vhs_f(at+480u,(*st).add_unguided);store_word(at+484u,select(0u,1u,(*st).add_sample_distance));
//#if FILM_DENOISING
    store_word(at+488u,select(0u,1u,(*st).denoise));vhs_3(at+492u,(*st).albedo);vhs_3(at+504u,(*st).cand_albedo);
//#endif
//#if VOLUME_LIGHT_PATH
    store_word(at+516u,(*st).visibility);store_word(at+520u,select(0u,1u,(*st).set_done));store_word(at+524u,select(0u,1u,(*st).set_ok));store_word(at+528u,(*st).set_i);store_word(at+532u,(*st).set_skip_object);store_word(at+536u,(*st).set_skip_shader);store_word(at+540u,(*st).set_local.node);vhs_f(at+544u,(*st).set_local.t.x);vhs_f(at+548u,(*st).set_local.t.y);vhs_3(at+552u,(*st).set_local.ray_P);vhs_3(at+564u,(*st).set_local.ray_D);store_word(at+576u,(*st).set_local.current.x);store_word(at+580u,(*st).set_local.current.y);store_word(at+584u,(*st).set_local.current.z);store_word(at+588u,(*st).set_local.object);store_word(at+592u,(*st).set_local.shader);store_word(at+596u,(*st).set_local.scale);store_word(at+600u,(*st).set_local.next_scale);store_word(at+604u,(*st).set_local.octant_mask);store_word(at+608u,select(0u,1u,(*st).set_local.no_overlap));vhs_f(at+612u,(*st).set_local.sigma.x);vhs_f(at+616u,(*st).set_local.sigma.y);vhs_f(at+620u,(*st).est.t.x);vhs_f(at+624u,(*st).est.t.y);vhs_f(at+628u,(*st).est.ext.x);vhs_f(at+632u,(*st).est.ext.y);store_word(at+636u,(*st).est.k);store_word(at+640u,(*st).est.n);vhs_f(at+644u,(*st).est.shade);vhs_f(at+648u,(*st).est.step);store_word(at+652u,(*st).est.object);store_word(at+656u,(*st).est.shader);store_word(at+660u,select(0u,1u,(*st).est.homogeneous));store_word(at+664u,(*st).est.resume);store_word(at+668u,select(0u,1u,(*st).estimating));
//#endif
}
fn vh_state_load(at:u32,st:ptr<function,VhState>){
    (*st).o.node=load_word(at+0u);(*st).o.t=vec2<f32>(heap_float(at+4u),heap_float(at+8u));(*st).o.ray_P=heap_vec3(at+12u);(*st).o.ray_D=heap_vec3(at+24u);(*st).o.current=vec3<u32>(load_word(at+36u),load_word(at+40u),load_word(at+44u));(*st).o.object=load_word(at+48u);(*st).o.shader=load_word(at+52u);(*st).o.scale=load_word(at+56u);(*st).o.next_scale=load_word(at+60u);(*st).o.octant_mask=load_word(at+64u);(*st).o.no_overlap=load_word(at+68u)!=0u;(*st).o.sigma=vec2<f32>(heap_float(at+72u),heap_float(at+76u));(*st).stage=load_word(at+80u);(*st).resume=load_word(at+84u);(*st).t=heap_float(at+88u);(*st).dt=heap_float(at+92u);(*st).rscatter=heap_float(at+96u);(*st).residual=heap_float(at+100u);(*st).transmittance=heap_float(at+104u);(*st).optical_depth=heap_float(at+108u);(*st).step=load_word(at+112u);(*st).sigma_max=heap_float(at+116u);(*st).step_sigma_max=heap_float(at+120u);(*st).sample_dt=heap_float(at+124u);(*st).distance_pdf=heap_float(at+128u);(*st).equiangular_pdf=heap_float(at+132u);(*st).majorant_scale=heap_float(at+136u);(*st).scatter_prob=heap_float(at+140u);(*st).direct_rr_scale=heap_float(at+144u);(*st).emission=heap_vec3(at+148u);(*st).vspg=load_word(at+160u)!=0u;(*st).method=load_word(at+164u);(*st).use_mis=load_word(at+168u)!=0u;(*st).direct_t=heap_float(at+172u);(*st).indirect_t=heap_float(at+176u);(*st).direct_scatter=load_word(at+180u)!=0u;(*st).indirect_scatter=load_word(at+184u)!=0u;(*st).direct=heap_vec3(at+188u);(*st).indirect=heap_vec3(at+200u);(*st).res_total=heap_float(at+212u);(*st).res_rand=heap_float(at+216u);(*st).cand_emission=heap_vec3(at+220u);(*st).cand_t=heap_float(at+232u);(*st).cand_throughput=heap_vec3(at+236u);(*st).cand_pdf=heap_float(at+248u);(*st).offset=load_word(at+252u);(*st).tr_N=load_word(at+256u);(*st).tr_k=load_word(at+260u);(*st).tr_n=load_word(at+264u);(*st).tr_i=load_word(at+268u);(*st).tr_step=heap_float(at+272u);(*st).tr_shade=heap_float(at+276u);(*st).tr_t=vec2<f32>(heap_float(at+280u),heap_float(at+284u));(*st).tr_pmf=heap_float(at+288u);(*st).tau_k=heap_vec3(at+292u);(*st).tau_j0=heap_vec3(at+304u);(*st).tau_j1=heap_vec3(at+316u);(*st).tau_j_1=heap_vec3(at+328u);(*st).T_k=heap_vec3(at+340u);(*st).origin=heap_vec3(at+352u);(*st).direction=heap_vec3(at+364u);(*st).tmax=heap_float(at+376u);(*st).index=load_word(at+380u);(*st).sample=load_word(at+384u);(*st).pixel=load_word(at+388u);(*st).scratch=load_word(at+392u);(*st).direct_base=load_word(at+396u);(*st).indirect_base=load_word(at+400u);(*st).direct_count=load_word(at+404u);(*st).indirect_count=load_word(at+408u);(*st).terminate=load_word(at+412u)!=0u;(*st).request=heap_float(at+416u);(*st).setup_from=load_word(at+420u);(*st).trans_resume=load_word(at+424u);(*st).add=load_word(at+428u)!=0u;(*st).add_weight=heap_float(at+432u);(*st).add_t=heap_float(at+436u);(*st).add_throughput=heap_vec3(at+440u);(*st).add_pdf=heap_float(at+452u);(*st).copy_direct=load_word(at+456u)!=0u;(*st).copy_indirect=load_word(at+460u)!=0u;(*st).copy_count=load_word(at+464u);(*st).add_clamp=load_word(at+468u)!=0u;(*st).film=load_word(at+472u);(*st).add_guided=heap_float(at+476u);(*st).add_unguided=heap_float(at+480u);(*st).add_sample_distance=load_word(at+484u)!=0u;
//#if FILM_DENOISING
    (*st).denoise=load_word(at+488u)!=0u;(*st).albedo=heap_vec3(at+492u);(*st).cand_albedo=heap_vec3(at+504u);
//#endif
//#if VOLUME_LIGHT_PATH
    (*st).visibility=load_word(at+516u);(*st).set_done=load_word(at+520u)!=0u;(*st).set_ok=load_word(at+524u)!=0u;(*st).set_i=load_word(at+528u);(*st).set_skip_object=load_word(at+532u);(*st).set_skip_shader=load_word(at+536u);(*st).set_local.node=load_word(at+540u);(*st).set_local.t=vec2<f32>(heap_float(at+544u),heap_float(at+548u));(*st).set_local.ray_P=heap_vec3(at+552u);(*st).set_local.ray_D=heap_vec3(at+564u);(*st).set_local.current=vec3<u32>(load_word(at+576u),load_word(at+580u),load_word(at+584u));(*st).set_local.object=load_word(at+588u);(*st).set_local.shader=load_word(at+592u);(*st).set_local.scale=load_word(at+596u);(*st).set_local.next_scale=load_word(at+600u);(*st).set_local.octant_mask=load_word(at+604u);(*st).set_local.no_overlap=load_word(at+608u)!=0u;(*st).set_local.sigma=vec2<f32>(heap_float(at+612u),heap_float(at+616u));(*st).est.t=vec2<f32>(heap_float(at+620u),heap_float(at+624u));(*st).est.ext=vec2<f32>(heap_float(at+628u),heap_float(at+632u));(*st).est.k=load_word(at+636u);(*st).est.n=load_word(at+640u);(*st).est.shade=heap_float(at+644u);(*st).est.step=heap_float(at+648u);(*st).est.object=load_word(at+652u);(*st).est.shader=load_word(at+656u);(*st).est.homogeneous=load_word(at+660u)!=0u;(*st).est.resume=load_word(at+664u);(*st).estimating=load_word(at+668u)!=0u;
//#endif
}
// The split record of input id: status, the entry's context, the evaluation's request and result, the state.
fn vh_split_record(id:u32)->u32{return p(12u)+16u+id*VH_SPLIT_BYTES;}
struct VhContext {index:u32, base:u32, flag:u32, bounce:u32, entries:u32, origin:vec3<f32>, direction:vec3<f32>, eq_point:vec3<f32>, segment:vec2<f32>, vl:u32}
fn vh_context_save(at:u32,x:VhContext){
    store_word(at+4u,x.index);store_word(at+8u,x.base);store_word(at+12u,x.flag);store_word(at+16u,x.bounce);store_word(at+20u,x.entries);
    vhs_3(at+24u,x.origin);vhs_3(at+36u,x.direction);vhs_3(at+48u,x.eq_point);vhs_f(at+60u,x.segment.x);vhs_f(at+64u,x.segment.y);store_word(at+68u,x.vl);
}
fn vh_context_load(at:u32)->VhContext{
    var x:VhContext;x.index=load_word(at+4u);x.base=load_word(at+8u);x.flag=load_word(at+12u);x.bounce=load_word(at+16u);x.entries=load_word(at+20u);
    x.origin=heap_vec3(at+24u);x.direction=heap_vec3(at+36u);x.eq_point=heap_vec3(at+48u);x.segment=vec2<f32>(heap_float(at+60u),heap_float(at+64u));x.vl=load_word(at+68u);return x;
}
fn vh_coefficients_save(at:u32,c:VhCoefficients){
    vhs_3(at+80u,c.sigma_t);vhs_3(at+92u,c.sigma_s);vhs_3(at+104u,c.emission);store_word(at+116u,c.flag);store_word(at+120u,c.count);store_word(at+124u,c.error);
}
fn vh_coefficients_load(at:u32)->VhCoefficients{
    var c:VhCoefficients;c.sigma_t=heap_vec3(at+80u);c.sigma_s=heap_vec3(at+92u);c.emission=heap_vec3(at+104u);c.flag=load_word(at+116u);c.count=load_word(at+120u);c.error=load_word(at+124u);return c;
}
@compute @workgroup_size(64) fn shade_volume_heterogeneous(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)||load_word(p(17u))!=0u){return;}
    if(p(13u)!=VH_SPLIT_BYTES){fail();return;}
    let record=vh_split_record(id.x);let status=load_word(record);
    if(status==VH_SPLIT_EVAL||status==VH_SPLIT_DONE){return;}
    var st:VhState;var x:VhContext;var c:VhCoefficients;var have=false;
    if(status==VH_SPLIT_RESUME){
        vh_state_load(record+VH_SPLIT_STATE,&st);x=vh_context_load(record);c=vh_coefficients_load(record);have=true;
        if(c.error!=0u){store_word(record,VH_SPLIT_DONE);return;}
    }else{
    store_word(record,VH_SPLIT_DONE);
    var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME){return;}
    if(!vh_is_heterogeneous(index)){return;}
    let stride=data_word(D_film_pass_stride);let pixel_index=state_word(S_path_render_pixel_index,index);if(pixel_index>=p(11u)/stride){fail();return;}
    let base=p(4u)+pixel_index*stride*4u;
    let prim=state_word(S_isect_prim,index);var flag=state_word(S_path_flag,index);let bounce=state_half(S_path_bounce,index);
    if(bounce==0u){flag|=PATH_RAY_VOLUME_PRIMARY_TRANSMIT;}
    let count=volume_stack_count(index);if(count==NONE){return;}
    let entries=select(count,select(0u,1u,data_word(D_background_volume_shader)!=NONE),prim==NONE);
    if(entries==0u){if(prim==NONE){store_word(volume_stack_address(index,0u,VS_shader),NONE);}state_u32(S_path_flag,index,flag);volume_after(index);return;}
    if(prim==NONE){store_word(volume_stack_address(index,entries,VS_shader),NONE);}
    state_u32(S_path_flag,index,flag);
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));let tmin=heap_float(state_address(S_ray_tmin,index,4u));
    var tmax=bitcast<f32>(0x7f7fffffu);if(prim!=NONE){tmax=heap_float(state_address(S_isect_t,index,4u));}
    let throughput=heap_vec3(state_address(S_path_throughput,index,12u));
    let sample=state_word(S_path_sample,index);let pixel=state_word(S_path_rng_pixel,index);let offset=state_half(S_path_rng_offset,index);
    let slot=SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES;
    st.index=index;st.origin=origin;st.direction=direction;st.tmax=tmax;st.sample=sample;st.pixel=pixel;
    st.scratch=p(23u)+id.x*slot;st.direct_base=p(23u)+(p(1u)+id.x)*slot;st.indirect_base=p(23u)+(2u*p(1u)+id.x)*slot;
    st.terminate=(flag&PATH_RAY_TERMINATE)!=0u;st.sample_dt=bitcast<f32>(0x7f7fffffu);
    // volume_direct_sample_method from shade_volume_segment (0 none, 1 distance, 2 equiangular, 3 MIS).
    var method=0u;var eq_point=vec3<f32>(0);var segment=vec2<f32>(0);var vl=0u;
//#if VOLUME_LIGHT
    vl=p(24u)+id.x*VL_RECORD_BYTES;method=load_word(vl);eq_point=heap_vec3(vl+4u);segment=vec2<f32>(heap_float(vl+16u),heap_float(vl+20u));
//#endif
    // volume_integrate_state_init and volume_integrate_result_init.
//#if VOLUME_SAMPLING_PATTERN else     st.rscatter=native_rng(sample,pixel,offset+V_PRNG_VOLUME_SCATTER_DISTANCE).x;st.use_mis=method==3u;
    st.rscatter=native_rng_pattern(sample,pixel,offset+V_PRNG_VOLUME_SCATTER_DISTANCE,1u).x;st.use_mis=method==3u;
//#endif
    if(st.use_mis){if(st.rscatter<0.5){method=1u;st.rscatter*=2.0;}else{st.rscatter=(st.rscatter-0.5)*2.0;method=2u;}}
    st.method=method;st.transmittance=1.0;st.t=tmin;st.vspg=bounce==0u;st.scatter_prob=1.0;st.majorant_scale=1.0;st.direct_rr_scale=1.0;
    st.direct=select(throughput,vec3<f32>(0),method==0u);st.indirect=throughput;
    if(method==2u){let s=vl_equiangular_sample(origin,direction,eq_point,segment,st.rscatter);st.direct_t=s.x;st.equiangular_pdf=s.y;}
    // volume_integrate_heterogeneous (the octree setup and the guidance run as the first stages).
    st.o.t=vec2<f32>(tmin,bitcast<f32>(0x7f7fffffu));st.o.node=NONE;st.o.object=NONE;st.o.shader=NONE;st.o.scale=VH_MANTISSA_BITS;
    st.film=base;st.offset=offset;st.setup_from=0u;st.stage=VH_SETUP;
//#if VOLUME_LIGHT_PATH
    st.visibility=state_byte(S_path_visibility,index);
//#endif
//#if FILM_DENOISING
    st.denoise=(flag&DN_PATH_RAY_DENOISING_FEATURES)!=0u;
//#endif
    x.index=index;x.base=base;x.flag=flag;x.bounce=bounce;x.entries=entries;x.origin=origin;x.direction=direction;x.eq_point=eq_point;x.segment=segment;x.vl=vl;
    }
    // One step of the null-scattering loop: an evaluation request ends this dispatch.
    let t=vh_step(&st,c,have);
    if(t>=0.0){
        vh_state_save(record+VH_SPLIT_STATE,&st);vh_context_save(record,x);
        vhs_f(record+128u,t);store_word(record+132u,st.scratch);
//#if VOLUME_LIGHT_PATH
        store_word(record+136u,select(NONE,st.est.object,st.estimating));store_word(record+140u,select(NONE,st.est.shader,st.estimating));
//#endif
        store_word(record,VH_SPLIT_EVAL);atomicAdd(&heap[p(12u)/4u],1u);return;
    }
    store_word(record,VH_SPLIT_DONE);
    let index=x.index;let base=x.base;var flag=x.flag;let bounce=x.bounce;let entries=x.entries;let origin=x.origin;let direction=x.direction;
    let eq_point=x.eq_point;let segment=x.segment;
    // volume_direct_scatter_mis.
    if(st.use_mis&&st.direct_scatter){
        var weight:f32;
        if(st.method==1u){weight=vl_power(st.distance_pdf,vl_equiangular_pdf(origin,direction,eq_point,segment,st.direct_t));}else{weight=vl_power(st.equiangular_pdf,st.distance_pdf);}
        st.direct*=2.0*weight;
    }
//#if VOLUME_EMISSION
    if(any(st.emission!=vec3<f32>(0))){vh_write_emission(index,base,st.emission);}
//#endif
//#if FILM_DENOISING
    flag=volume_denoising_features(index,base,flag,st.albedo,st.indirect_scatter);
//#endif
    if(bounce==0u){state_f32(S_path_optical_depth,index,heap_float(state_address(S_path_optical_depth,index,4u))+st.optical_depth);}
    // volume_integrate_event.
    let continuation=select(heap_float(state_address(S_path_continuation_probability,index,4u)),0.0,(flag&PATH_RAY_TERMINATE_IN_NEXT_VOLUME)!=0u);
    if(continuation==0.0){volume_finish(index,0u);return;}
//#if VOLUME_LIGHT
    let vl=x.vl;
    if(st.direct_scatter){
        let direct_P=rounded_add3(origin,st.direct_t*direction);let weight=st.direct*rounded_divide(1.0,continuation);
        store_word(vl+36u,bitcast<u32>(direct_P.x));store_word(vl+40u,bitcast<u32>(direct_P.y));store_word(vl+44u,bitcast<u32>(direct_P.z));
        store_word(vl+48u,bitcast<u32>(weight.x));store_word(vl+52u,bitcast<u32>(weight.y));store_word(vl+56u,bitcast<u32>(weight.z));
        store_word(vl+60u,st.direct_count);store_word(vl+76u,st.direct_base);store_word(vl+32u,1u);
    }
//#endif
    if(st.indirect_scatter){st.indirect*=rounded_divide(1.0,continuation);}state_vec3(S_path_throughput,index,st.indirect);
    if(!st.indirect_scatter){volume_after(index);return;}
    // The indirect phase scatter: shade_volume_light (after the direct light) or shade_volume_phase run it from the
    // record; without either, here.
    let rec=p(24u)+id.x*VL_RECORD_BYTES;
    store_word(rec+68u,bitcast<u32>(st.indirect_t));store_word(rec+72u,load_word(volume_stack_address(index,entries-1u,VS_object)));
    store_word(rec+80u,st.indirect_base);store_word(rec+84u,st.indirect_count);store_word(rec+64u,1u);
}
@compute @workgroup_size(64) fn shade_volume_heterogeneous_eval(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)||load_word(p(17u))!=0u){return;}
    let record=vh_split_record(id.x);if(load_word(record)!=VH_SPLIT_EVAL){return;}
    let index=load_word(record+4u);let origin=heap_vec3(record+24u);let direction=heap_vec3(record+36u);let t=heap_float(record+128u);let scratch=load_word(record+132u);
//#if VOLUME_LIGHT_PATH else     let c=vh_eval(index,rounded_add3(origin,direction*t),direction,scratch);
    let c=vh_eval(index,rounded_add3(origin,direction*t),direction,scratch,load_word(record+136u),load_word(record+140u));
//#endif
    vh_coefficients_save(record,c);store_word(record,VH_SPLIT_RESUME);
}
// float3 / float and volume_sample_channel_pdf per ISA, shared by the null-scattering loop and ray marching (see below).
fn vr_div3(v:vec3<f32>,f:f32)->vec3<f32>{if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return v*rounded_divide(1.0,f);}return vh_divide(v,vec3<f32>(f));}
fn vr_channel_pdf(albedo:vec3<f32>,throughput:vec3<f32>)->vec3<f32>{
    let weights=abs(throughput*albedo);let sum=rounded_add(rounded_add(weights.x,weights.y),weights.z);
    if(rounded_add(1.0,-sum)<1.0){return vr_div3(weights,sum);}return vec3<f32>(1.0/3.0);
}
//#if VOLUME_RAY_MARCHING
// The oracle kernels' arithmetic on this path. volume_sample_channel rescales rchannel by 1/pdf at every step, so a last-bit
// difference in the throughput or in sigma grows about threefold per step until the channel pick flips
// (volumes/ray-marching-heterogeneous-point, native/DEVICE.md). So this path computes the oracle's values exactly on every
// host: expf and logf are the C library's (correctly rounded here, not the driver's exp and log); on the scalar ARM64 oracle
// (the generated NATIVE_FLOAT3_RECIPROCAL_NORMALIZE, arm64-scalar) float3 / float multiplies by the reciprocal, as the
// scalar float3 operator does, and on SSE hosts it is the correctly rounded quotient. The double-float forms are
// wavefront_svm_closures.wgsl's vp_exp_parts and vp_log_parts, copied for the modules without VP_MATH.
fn vr_exp_parts(x:f32)->vec2<f32>{
    if(!(x==x)){return vec2<f32>(x,0.0);}if(x>88.72284){return vec2<f32>(bitcast<f32>(0x7f800000u),0.0);}if(x< -103.97208){return vec2<f32>(0.0,0.0);}
    let k=round(x*1.4426950408889634);let p=trig_product(k,0.6931471824645996);let head=trig_two_sum(x,-p.x);
    let r=vec2<f32>(head.x,rounded_add(rounded_add(head.y,-p.y),k*1.904654323148236e-09));
    var coefficients=array<vec2<f32>,10>(vec2<f32>(2.7557318844628753e-06,3.793571224297229e-14),vec2<f32>(2.4801587642286904e-05,-3.40699609366682e-13),
        vec2<f32>(0.00019841270113829523,-2.725596874933456e-12),vec2<f32>(0.0013888889225199819,-3.3631094437103215e-11),vec2<f32>(0.008333333767950535,-4.34617203337595e-10),
        vec2<f32>(0.0416666679084301,-1.2417634698280722e-09),vec2<f32>(0.1666666716337204,-4.967053879312289e-09),vec2<f32>(0.5,0.0),vec2<f32>(1.0,0.0),vec2<f32>(1.0,0.0));
    var polynomial=vec2<f32>(2.755731998149713e-07,-7.575112209051195e-15);
    for(var i=0u;i<10u;i++){polynomial=trig_coeff(coefficients[i],trig_mul(polynomial,r));}
    return vec2<f32>(rounded_add(polynomial.x,polynomial.y),k);
}
fn vr_exp3(v:vec3<f32>)->vec3<f32>{
    var out=vec3<f32>(0);let count=rounded_steps(3u);
    for(var k=0u;k<count;k++){let e=vr_exp_parts(v[k]);out[k]=ldexp(e.x,i32(e.y));}
    return out;
}
fn vr_log_parts(x:f32)->vec2<f32>{
    var bits=bitcast<u32>(x);var e=f32(i32(bits>>23u)-127);var m=bitcast<f32>((bits&0x007fffffu)|0x3f800000u);
    if(m>1.4142135){m*=0.5;e+=1.0;}
    let num=m-1.0;let den=trig_two_sum(m,1.0);let q=rounded_divide(num,den.x);let qd=trig_product(q,den.x);
    let sl=rounded_divide(rounded_add(rounded_add(rounded_add(num,-qd.x),-qd.y),-(q*den.y)),den.x);let sv=vec2<f32>(q,sl);
    let z=trig_mul(sv,sv);
    var coefficients=array<vec2<f32>,10>(vec2<f32>(0.05263157933950424,-3.9213582381236733e-10),vec2<f32>(0.05882352963089943,-2.1913472425527658e-10),
        vec2<f32>(0.06666667014360428,-3.47693762670076e-09),vec2<f32>(0.07692307978868484,-2.8656079731348427e-09),vec2<f32>(0.09090909361839294,-2.709302115988521e-09),
        vec2<f32>(0.1111111119389534,-8.278422947149977e-10),vec2<f32>(0.1428571492433548,-6.38621200366174e-09),vec2<f32>(0.20000000298023224,-2.9802322831784522e-09),
        vec2<f32>(0.3333333432674408,-9.934107758624577e-09),vec2<f32>(1.0,0.0));
    var polynomial=vec2<f32>(0.0476190485060215,-8.869738832295582e-10);
    for(var i=0u;i<10u;i++){polynomial=trig_coeff(coefficients[i],trig_mul(polynomial,z));}
    let lm=trig_mul(vec2<f32>(2.0*sv.x,2.0*sv.y),polynomial);
    let le=trig_product(e,0.6931471824645996);let tail=rounded_add(le.y,e*-1.9046542121259336e-09);
    let sum=trig_two_sum(le.x,lm.x);return vec2<f32>(sum.x,rounded_add(rounded_add(sum.y,lm.y),tail));
}
fn vr_logf(x:f32)->f32{let l=vr_log_parts(x);return rounded_add(l.x,l.y);}
fn vr_sample_channel(pdf:vec3<f32>,rand:f32)->vec2<f32>{
    var sum=0.0;
    for(var c=0u;c<3u;c++){if(rand<rounded_add(sum,pdf[c])){return vec2<f32>(f32(c),rounded_divide(rounded_add(rand,-sum),pdf[c]));}sum=rounded_add(sum,pdf[c]);}
    return vec2<f32>(2.0,rand);
}
// Biased ray marching (pinned integrator/shade_volume.h volume_integrate_ray_marching with VolumeStep,
// volume_step_init<false>, volume_ray_marching_advance, volume_ray_marching_state_init,
// volume_ray_marching_step_scattering and volume_sample_indirect_scatter_ray_marching; volume_stack.h
// volume_stack_step_size<false>). Every path of DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME_RAY_MARCHING runs here (the
// homogeneous media too, with one step); the shader evaluations (vh_eval, one call site), the phase copies and the
// records for shade_volume_light and shade_volume_phase are the heterogeneous entry's.
@compute @workgroup_size(64) fn shade_volume_ray_marching(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=vr_volume_kernel()){fail();return;}
    let rec=p(24u)+id.x*VL_RECORD_BYTES;store_word(rec+64u,0u);store_word(rec+80u,0u);
    if(load_word(p(17u))!=0u){return;}
    let stride=data_word(D_film_pass_stride);let pixel_index=state_word(S_path_render_pixel_index,index);if(pixel_index>=p(11u)/stride){fail();return;}
    let base=p(4u)+pixel_index*stride*4u;
    let prim=state_word(S_isect_prim,index);var flag=state_word(S_path_flag,index);let bounce=state_half(S_path_bounce,index);
    if(bounce==0u){flag|=PATH_RAY_VOLUME_PRIMARY_TRANSMIT;}
    let count=volume_stack_count(index);if(count==NONE){return;}
    let entries=select(count,select(0u,1u,data_word(D_background_volume_shader)!=NONE),prim==NONE);
    if(entries==0u){if(prim==NONE){store_word(volume_stack_address(index,0u,VS_shader),NONE);}state_u32(S_path_flag,index,flag);volume_after(index);return;}
    if(prim==NONE){store_word(volume_stack_address(index,entries,VS_shader),NONE);}
    state_u32(S_path_flag,index,flag);
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));let tmin=heap_float(state_address(S_ray_tmin,index,4u));
    var tmax=bitcast<f32>(0x7f7fffffu);if(prim!=NONE){tmax=heap_float(state_address(S_isect_t,index,4u));}
    let throughput=heap_vec3(state_address(S_path_throughput,index,12u));
    let sample=state_word(S_path_sample,index);let pixel=state_word(S_path_rng_pixel,index);let offset=state_half(S_path_rng_offset,index);
    let slot_bytes=SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES;let scratch=p(23u)+id.x*slot_bytes;
    let direct_base=p(23u)+(p(1u)+id.x)*slot_bytes;let indirect_base=p(23u)+(2u*p(1u)+id.x)*slot_bytes;
    // volume_direct_sample_method from shade_volume_segment (0 none, 1 distance, 2 equiangular, 3 MIS).
    var method=0u;var eq_point=vec3<f32>(0);var segment=vec2<f32>(0);
//#if VOLUME_LIGHT
    method=load_word(rec);eq_point=heap_vec3(rec+4u);segment=vec2<f32>(heap_float(rec+16u),heap_float(rec+20u));
//#endif
    // volume_step_init<false>.
    let object_step=vr_step_size(index,entries);
    var t=vec2<f32>(tmin,tmin);var size:f32;var shade_offset=0.0;var step_offset=1.0;var max_steps=1;
    if(object_step==bitcast<f32>(0x7f7fffffu)){size=rounded_add(tmax,-tmin);}
    else{
        max_steps=bitcast<i32>(data_word(VR_D_volume_max_steps));let length=rounded_add(tmax,-tmin);size=min(object_step,length);
        if(length>f32(max_steps)*size){size=rounded_divide(length,f32(max_steps));}
//#if VOLUME_SAMPLING_PATTERN else         shade_offset=native_rng(sample,pixel,offset+VR_PRNG_SHADE_OFFSET).x;step_offset=native_rng(sample,pixel,offset+VR_PRNG_OFFSET).x;
        shade_offset=native_rng_pattern(sample,pixel,offset+VR_PRNG_SHADE_OFFSET,1u).x;step_offset=native_rng_pattern(sample,pixel,offset+VR_PRNG_OFFSET,1u).x;
//#endif
    }
    // volume_ray_marching_state_init.
//#if VOLUME_SAMPLING_PATTERN else     var rscatter=native_rng(sample,pixel,offset+V_PRNG_VOLUME_SCATTER_DISTANCE).x;var rchannel=native_rng(sample,pixel,offset+VR_PRNG_COLOR_CHANNEL).x;
    var rscatter=native_rng_pattern(sample,pixel,offset+V_PRNG_VOLUME_SCATTER_DISTANCE,1u).x;var rchannel=native_rng_pattern(sample,pixel,offset+VR_PRNG_COLOR_CHANNEL,1u).x;
//#endif
    let use_mis=method==3u;if(use_mis){if(rscatter<0.5){rscatter*=2.0;method=1u;}else{rscatter=(rscatter-0.5)*2.0;method=2u;}}
    var equiangular_pdf=0.0;var distance_pdf=1.0;
    var direct=select(throughput,vec3<f32>(0),method==0u);var indirect=throughput;
    var direct_t=0.0;var indirect_t=0.0;var direct_scatter=false;var indirect_scatter=false;var direct_count=0u;var indirect_count=0u;
    if(method==2u){let e=vl_equiangular_sample(origin,direction,eq_point,segment,rscatter);direct_t=e.x;equiangular_pdf=e.y;}
    var emission=vec3<f32>(0);
//#if FILM_DENOISING
    var denoising_albedo=vec3<f32>(0);let denoise=(flag&DN_PATH_RAY_DENOISING_FEATURES)!=0u;
//#endif
    for(var step=0;step<0x7fffffff;step++){
        // volume_ray_marching_advance.
        if(t.y==tmax){break;}
        t.x=t.y;t.y=min(tmax,rounded_add(tmin,(f32(step)+step_offset)*size));
        let shade_t=rounded_add(t.x,shade_offset*rounded_add(t.y,-t.x));
        if(step>=max_steps){break;}
//#if VOLUME_LIGHT_PATH else         let c=vh_eval(index,rounded_add3(origin,direction*shade_t),direction,scratch);if(c.error!=0u){return;}
        // Ray marching evaluates the whole stack at every step (volume_integrate_ray_marching: volume_shader_sample, no octree
        // and so no extrema estimate).
        let c=vh_eval(index,rounded_add3(origin,direction*shade_t),direction,scratch,NONE,NONE);if(c.error!=0u){return;}
//#endif
        if((c.flag&(SR_EXTINCTION|SR_SCATTER|SR_EMISSION))==0u){continue;}
        let dt=rounded_add(t.y,-t.x);
        // Loop-local declarations take explicit assignments (naga does not re-run constant initializers).
        var transmittance:vec3<f32>;transmittance=vec3<f32>(1);if((c.flag&SR_EXTINCTION)!=0u){transmittance=vr_exp3(-c.sigma_t*dt);}
//#if VOLUME_EMISSION
        // volume_emission_integrate before the indirect scatter position.
        if((c.flag&SR_EMISSION)!=0u&&!indirect_scatter){
            var e=c.emission;
            if((c.flag&SR_EXTINCTION)!=0u){let depth=c.sigma_t*dt;e*=select(dt*rounded_add3(vec3<f32>(1),-0.5*depth),vh_divide(vec3<f32>(1)-exp(-depth),c.sigma_t),depth>vec3<f32>(1e-5));}
            else{e*=dt;}
            emission+=indirect*e;
        }
//#endif
        if((c.flag&SR_SCATTER)!=0u){
//#if FILM_DENOISING
            if(denoise){denoising_albedo+=indirect*vh_safe_divide(c.sigma_s,c.sigma_t,0.0)*(vec3<f32>(1)-transmittance);}
//#endif
            // volume_ray_marching_step_scattering.
            let albedo=vh_safe_divide(c.sigma_s,c.sigma_t,1.0);let channel_pdf=vr_channel_pdf(albedo,indirect);let pick=vr_sample_channel(channel_pdf,rchannel);rchannel=pick.y;let channel=u32(pick.x);
            if(method==2u&&!direct_scatter){
                if(direct_t>=t.x&&direct_t<=t.y&&equiangular_pdf>1e-8){
                    let new_dt=rounded_add(direct_t,-t.x);let new_transmittance=vr_exp3(-c.sigma_t*new_dt);
                    direct_scatter=true;direct*=select(vec3<f32>(0),vr_div3(c.sigma_s*new_transmittance,equiangular_pdf),vec3<f32>(equiangular_pdf)!=vec3<f32>(0));
                    vh_copy_closures(scratch,direct_base,c.count);direct_count=c.count;
                    if(use_mis){let d=distance_pdf*rounded_dot(channel_pdf,c.sigma_t*new_transmittance);direct*=2.0*vl_power(equiangular_pdf,d);}
                }else{direct*=transmittance;distance_pdf*=rounded_dot(channel_pdf,transmittance);}
            }
            // volume_sample_indirect_scatter_ray_marching.
            var found:bool;found=false;
            if(!indirect_scatter){
                let sample_transmittance=transmittance[channel];
                if(rounded_add(1.0,-rscatter)>=sample_transmittance){
                    let new_dt=rounded_divide(-vr_logf(rounded_add(1.0,-rscatter)),c.sigma_t[channel]);let new_t=rounded_add(t.x,new_dt);
                    let new_transmittance=vr_exp3(-c.sigma_t*new_dt);let pdf=rounded_dot(channel_pdf,c.sigma_t*new_transmittance);
                    if(distance_pdf*pdf>1e-8){
                        indirect_scatter=true;indirect_t=new_t;indirect*=vr_div3(c.sigma_s*new_transmittance,pdf);if(method==1u){distance_pdf*=pdf;}
                        vh_copy_closures(scratch,indirect_base,c.count);indirect_count=c.count;found=true;
                    }
                }else{
                    let pdf=rounded_dot(channel_pdf,transmittance);indirect*=vr_div3(transmittance,pdf);if(method==1u){distance_pdf*=pdf;}
                    rscatter=rounded_add(1.0,-rounded_divide(rounded_add(1.0,-rscatter),sample_transmittance));
                }
            }
            if(found&&method==1u){
                direct_scatter=true;direct_t=indirect_t;direct=indirect;vh_copy_closures(scratch,direct_base,c.count);direct_count=c.count;
                if(use_mis){direct*=2.0*vl_power(distance_pdf,vl_equiangular_pdf(origin,direction,eq_point,segment,indirect_t));}
            }
        }else if((c.flag&SR_EXTINCTION)!=0u){indirect*=transmittance;direct*=transmittance;}
        // volume_integrate_should_stop.
        if((all(indirect==vec3<f32>(0))&&all(direct==vec3<f32>(0)))||(direct_scatter&&indirect_scatter)){break;}
    }
//#if VOLUME_EMISSION
    if(any(emission!=vec3<f32>(0))){vh_write_emission(index,base,emission);}
//#endif
//#if FILM_DENOISING
    flag=volume_denoising_features(index,base,flag,denoising_albedo,indirect_scatter);
//#endif
    // volume_integrate_event.
    let continuation=select(heap_float(state_address(S_path_continuation_probability,index,4u)),0.0,(flag&PATH_RAY_TERMINATE_IN_NEXT_VOLUME)!=0u);
    if(continuation==0.0){volume_finish(index,0u);return;}
//#if VOLUME_LIGHT
    if(direct_scatter){
        let direct_P=rounded_add3(origin,direct_t*direction);let weight=direct/continuation;
        store_word(rec+36u,bitcast<u32>(direct_P.x));store_word(rec+40u,bitcast<u32>(direct_P.y));store_word(rec+44u,bitcast<u32>(direct_P.z));
        store_word(rec+48u,bitcast<u32>(weight.x));store_word(rec+52u,bitcast<u32>(weight.y));store_word(rec+56u,bitcast<u32>(weight.z));
        store_word(rec+60u,direct_count);store_word(rec+76u,direct_base);store_word(rec+32u,1u);
    }
//#endif
    if(indirect_scatter){indirect/=continuation;}state_vec3(S_path_throughput,index,indirect);
    if(!indirect_scatter){volume_after(index);return;}
    store_word(rec+68u,bitcast<u32>(indirect_t));store_word(rec+72u,load_word(volume_stack_address(index,entries-1u,VS_object)));
    store_word(rec+80u,indirect_base);store_word(rec+84u,indirect_count);store_word(rec+64u,1u);
}
//#endif
//#endif
