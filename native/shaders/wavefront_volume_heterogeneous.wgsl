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
        sd.runtime_flag=r.runtime_flag;r=svm_evaluate_seeded(index,sd,r.remaining,r.closure_base,true,r);if(r.error!=0u){out.error=r.error;return out;}
        if(slot>0u){
            for(var a=0u;a<r.count;a++){var left=svm_closure_read(r,a);var b=a+1u;
                while(b<r.count){let right=svm_closure_read(r,b);
//#if VOLUME_PHASES else                     if(left.kind==right.kind&&left.roughness==right.roughness){left.weight+=right.weight;left.sample_weight+=right.sample_weight;svm_closure_write(r.closure_base+a*SVM_CLOSURE_BYTES,left);
                    if(vp_equal(left,right)){left.weight+=right.weight;left.sample_weight+=right.sample_weight;svm_closure_write(r.closure_base+a*SVM_CLOSURE_BYTES,left);
//#endif
                        for(var j=b;j+1u<r.count;j++){svm_closure_write(r.closure_base+j*SVM_CLOSURE_BYTES,svm_closure_read(r,j+1u));}r.count--;
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
fn vh_copy_closures(src_base:u32,dst_base:u32,count:u32){
    for(var i=0u;i<count*SVM_CLOSURE_BYTES/4u;i++){store_word(dst_base+i*4u,load_word(src_base+i*4u));}
}
// film_write_volume_emission (combined, adaptive and guidance passes; the emission pass is refused with volume
// emission).
fn vh_write_emission(index:u32,base:u32,emission:vec3<f32>){
    var value=select(emission,vec3<f32>(0),(bitcast<vec3<u32>>(emission)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
    let bounce=state_half(S_path_bounce,index);let limit=select(data_float(VE_D_sample_clamp_direct),data_float(VE_D_sample_clamp_indirect),i32(bounce)-1>0);
    let total=abs(value.x)+abs(value.y)+abs(value.z);if(total>limit){value*=limit/total;}
    if((data_word(VE_D_light_pass_flag)&VE_MASK_COMBINED)!=0u){vl_film_add3(base+data_word(VE_D_pass_combined)*4u,value);}
    if(data_word(VE_D_pass_adaptive_aux_buffer)!=PASS_NONE&&(countOneBits(state_word(S_path_sample,index)&0xaaaaaaaau)&1u)!=0u){vl_film_add3(base+data_word(VE_D_pass_adaptive_aux_buffer)*4u,value*2.0);}
    volume_guidance_pass(index,base,value);
}
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
}
const VH_INIT=0u;const VH_ADVANCE=1u;const VH_ADVANCE_WHILE=2u;const VH_ADVANCE_AFTER=3u;const VH_STEP=4u;const VH_STEP_EVAL=5u;
const VH_FINAL=6u;const VH_FINAL_EVAL=7u;const VH_DIRECT=8u;const VH_DIRECT_EVAL=9u;const VH_TRANS=10u;const VH_DONE=11u;
const VH_SETUP=12u;const VH_TRANS_START=13u;const VH_FINAL2=14u;
// volume_reservoir add_sample.
fn vh_reservoir_add(st:ptr<function,VhState>,weight:f32,emission:vec3<f32>,t:f32,throughput:vec3<f32>,pdf:f32){
    if(!(weight>0.0)){return;}
    (*st).res_total+=weight;let thresh=weight/(*st).res_total;
    if(((*st).res_rand<=thresh)||((*st).res_total==weight)){(*st).cand_emission=emission;(*st).cand_t=t;(*st).cand_throughput=throughput;(*st).cand_pdf=pdf;(*st).res_rand/=thresh;}
    else{(*st).res_rand=((*st).res_rand-thresh)/(1.0-thresh);}
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
    let rand=native_rng((*st).sample,0u,(*st).offset+VH_PRNG_EXPANSION_ORDER).x;
    var N=1u;(*st).tr_pmf=1.0;
    if(sigma_c!=0.0){
        // sample_geometric_distribution(rand, 0.9, pmf, 4).
        let r=vh_geometric_r();let q=1.0-r;let n=min(i32(floor(log(rand)/log(q))),4);
        (*st).tr_pmf=select(r*pow(q,f32(n)),pow(q,f32(n)),n==4);N=1u<<u32(max(n,0));
    }
    (*st).tr_N=N;(*st).tr_k=k;(*st).tr_n=0u;(*st).tr_i=0u;(*st).tr_t=t;
    (*st).tr_shade=native_rng((*st).sample,(*st).pixel,(*st).offset+VH_PRNG_SHADE_OFFSET).x;(*st).tr_step=length/f32(N*k);
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
                (*st).rscatter=native_rng((*st).sample,(*st).pixel,(*st).offset+V_PRNG_VOLUME_SCATTER_DISTANCE).x;(*st).stage=VH_ADVANCE_WHILE;
            }
            else if(stage==2u){ // VH_ADVANCE_WHILE: volume_indirect_scatter_advance, then the octree advance.
                let equiangular=((*st).method==2u)&&(*st).use_mis;
                let sigma_max=(*st).o.sigma.y*(*st).majorant_scale;
                (*st).residual=((*st).o.t.y-(*st).t)*sigma_max;
                var advance=true;
                if(sigma_max!=0.0){
                    (*st).dt=-log(1.0-(*st).rscatter)/sigma_max;(*st).t+=(*st).dt;
                    let has=equiangular&&((*st).direct_t>=(*st).o.t.x&&(*st).direct_t<=(*st).o.t.y);
                    if(has&&(*st).t>(*st).direct_t&&!(*st).direct_scatter){
                        (*st).direct_scatter=true;(*st).direct=(*st).indirect*(*st).transmittance*(*st).direct_rr_scale;
                        (*st).sample_dt=(*st).direct_t-(*st).t+(*st).dt;(*st).distance_pdf=(*st).transmittance*sigma_max;(*st).sigma_max=sigma_max;
                    }
                    advance=(*st).t>(*st).o.t.y;
                }
                if(!advance){(*st).offset+=VH_PRNG_BOUNCE_NUM;(*st).step_sigma_max=(*st).o.sigma.y*(*st).majorant_scale;(*st).stage=VH_STEP;continue;}
                var o=(*st).o;let moved=vh_octree_advance((*st).index,(*st).origin,(*st).direction,(*st).tmax,&o);(*st).o=o;if(!moved){(*st).stage=VH_FINAL;continue;}
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
                        (*st).res_rand=clamp((*st).res_rand/thresh,0.0,1.0);(*st).indirect/=thresh;
                    }
                    if(equiangular){
                        if((*st).res_rand>thresh){(*st).direct_scatter=true;(*st).direct=vec3<f32>(0);(*st).res_rand=((*st).res_rand-thresh)/(1.0-thresh);}
                        else{(*st).res_rand/=thresh;(*st).direct_rr_scale/=thresh;}
                        (*st).res_rand=clamp((*st).res_rand,0.0,1.0);
                    }
                }
                (*st).stage=VH_STEP_EVAL;return (*st).t;
            }
            else if(stage==5u){ // VH_STEP_EVAL: volume_integrate_step_scattering with the coefficients at t.
                if(!evaluated){return (*st).t;}evaluated=false;
                if((c.flag&(SR_EXTINCTION|SR_SCATTER|SR_EMISSION))!=0u){
                    let sigma_max=(*st).step_sigma_max;let majorant=max(vh_max(c.sigma_t),sigma_max);let sigma_n=vec3<f32>(majorant)-c.sigma_t;
                    if(majorant!=sigma_max){(*st).indirect*=exp((sigma_max-majorant)*(*st).dt)/sigma_max;}else{(*st).indirect/=majorant;}
                    if((c.flag&SR_EMISSION)!=0u){(*st).emission+=(*st).indirect*c.emission;}
                    if(c.sigma_s.x+c.sigma_s.y+c.sigma_s.z==0.0){(*st).indirect*=sigma_n;}
                    else{
                        let albedo=select(vec3<f32>(1.0),c.sigma_s/c.sigma_t,c.sigma_t!=vec3<f32>(0));let channel_pdf=vl_channel_pdf(albedo,(*st).indirect);
                        let prob_s=dot(c.sigma_s/(c.sigma_s+sigma_n),channel_pdf);
                        let weight=(*st).transmittance*prob_s;let throughput=(*st).indirect*c.sigma_s/prob_s;
                        if((*st).vspg){(*st).add=true;(*st).add_weight=weight;(*st).add_t=(*st).t;(*st).add_throughput=throughput;(*st).add_pdf=weight*sigma_max;}
                        else if(!(*st).indirect_scatter){
                            if((*st).res_rand<=prob_s){
                                (*st).res_rand/=prob_s;(*st).indirect_scatter=true;
                                (*st).copy_indirect=true;(*st).copy_count=c.count;
                                (*st).add=true;(*st).add_clamp=true;(*st).add_weight=weight;(*st).add_t=(*st).t;(*st).add_throughput=throughput;(*st).add_pdf=weight*sigma_max;
                                if((*st).method==1u){(*st).direct_scatter=true;(*st).copy_direct=true;}
                            }else{(*st).res_rand=clamp(((*st).res_rand-prob_s)/(1.0-prob_s),0.0,1.0);}
                        }
                        let prob_n=1.0-prob_s;(*st).indirect*=select(vec3<f32>(0),sigma_n/prob_n,vec3<f32>(prob_n)!=vec3<f32>(0));
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
                    if(sample_distance){(*st).direct_scatter=true;(*st).direct_t=(*st).indirect_t;(*st).direct=(*st).indirect;if((*st).use_mis){(*st).distance_pdf=(*st).cand_pdf;}}
                    continue;
                }
                if(sample_distance){(*st).direct=(*st).cand_throughput*(*st).res_total;(*st).distance_pdf=(*st).cand_pdf;}
                (*st).emission=mix((*st).emission,(*st).cand_emission,vec3<f32>((*st).res_total));
                let unguided=(*st).res_total;var guided:f32;
                if(all((*st).indirect==vec3<f32>(0))){guided=1.0;}
                else{(*st).res_total=mix((*st).res_total,(*st).scatter_prob,0.75);guided=(*st).res_total;
                    (*st).add=true;(*st).add_weight=1.0-guided;(*st).add_t=(*st).cand_t;(*st).add_throughput=(*st).indirect;(*st).add_pdf=0.0;}
                (*st).add_guided=guided;(*st).add_unguided=unguided;(*st).add_sample_distance=sample_distance;(*st).stage=VH_FINAL2;
            }
            else if(stage==14u){ // VH_FINAL2: after the guided sample joined the reservoir.
                (*st).stage=VH_DIRECT;
                let guided=(*st).add_guided;let unguided=(*st).add_unguided;let sample_distance=(*st).add_sample_distance;
                let scatter=(*st).cand_pdf>0.0;
                let scale=select((1.0-unguided)/(1.0-guided),unguided/guided,scatter);
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
                        let albedo=select(vec3<f32>(1.0),c.sigma_s/c.sigma_t,c.sigma_t!=vec3<f32>(0));let channel_pdf=vl_channel_pdf(albedo,(*st).direct);
                        (*st).distance_pdf*=dot(c.sigma_s/(c.sigma_s+sigma_n),channel_pdf);
                    }
                    (*st).direct*=select(vec3<f32>(0),c.sigma_s/(*st).equiangular_pdf,vec3<f32>((*st).equiangular_pdf)!=vec3<f32>(0));
                }else{(*st).direct_scatter=false;}
            }
            else if(stage==12u){ // VH_SETUP: volume_octree_setup<false> (after the first segment or an octree advance).
                var o=(*st).o;let ok=vh_octree_setup((*st).index,(*st).origin,(*st).direction,(*st).tmax,&o);(*st).o=o;
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
                    (*st).scatter_prob=select(min(scattered/volume,0.9999),0.5,volume==0.0);
                    let count_pass=heap_float((*st).film+data_word(D_film_pass_volume_majorant_sample_count)*4u);var depth=bitcast<f32>(0x7f7fffffu);
                    if(count_pass!=0.0){depth=heap_float((*st).film+data_word(D_film_pass_volume_majorant)*4u)/count_pass;}
                    (*st).majorant_scale=select(-vh_fast_log(1.0-(*st).scatter_prob)/depth,1.0,depth==0.0);
                    if((*st).majorant_scale<1.0){(*st).majorant_scale=1.0;let d=1.0-vh_fast_exp(-depth);(*st).scatter_prob=select(0.0,(*st).scatter_prob/d,d!=0.0);}
                    else{(*st).scatter_prob=1.0;}
                }
                (*st).res_rand=native_rng((*st).sample,(*st).pixel,(*st).offset+V_PRNG_VOLUME_RESERVOIR).x;
                (*st).offset=vh_scramble((*st).offset,0xe35fad82u);
                (*st).trans_resume=VH_ADVANCE;(*st).stage=VH_TRANS_START;
            }
            else if(stage==13u){ // VH_TRANS_START: volume_equiangular_transmittance of the current segment.
                vh_equiangular_transmittance(st,(*st).o.t,(*st).o.sigma,(*st).trans_resume);
            }
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
@compute @workgroup_size(64) fn shade_volume_heterogeneous(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME||load_word(p(17u))!=0u){return;}
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
    var st:VhState;st.index=index;st.origin=origin;st.direction=direction;st.tmax=tmax;st.sample=sample;st.pixel=pixel;
    st.scratch=p(23u)+id.x*slot;st.direct_base=p(23u)+(p(1u)+id.x)*slot;st.indirect_base=p(23u)+(2u*p(1u)+id.x)*slot;
    st.terminate=(flag&PATH_RAY_TERMINATE)!=0u;st.sample_dt=bitcast<f32>(0x7f7fffffu);
    // volume_direct_sample_method from shade_volume_segment (0 none, 1 distance, 2 equiangular, 3 MIS).
    var method=0u;var eq_point=vec3<f32>(0);var segment=vec2<f32>(0);
//#if VOLUME_LIGHT
    let vl=p(24u)+id.x*VL_RECORD_BYTES;method=load_word(vl);eq_point=heap_vec3(vl+4u);segment=vec2<f32>(heap_float(vl+16u),heap_float(vl+20u));
//#endif
    // volume_integrate_state_init and volume_integrate_result_init.
    st.rscatter=native_rng(sample,pixel,offset+V_PRNG_VOLUME_SCATTER_DISTANCE).x;st.use_mis=method==3u;
    if(st.use_mis){if(st.rscatter<0.5){method=1u;st.rscatter*=2.0;}else{st.rscatter=(st.rscatter-0.5)*2.0;method=2u;}}
    st.method=method;st.transmittance=1.0;st.t=tmin;st.vspg=bounce==0u;st.scatter_prob=1.0;st.majorant_scale=1.0;st.direct_rr_scale=1.0;
    st.direct=select(throughput,vec3<f32>(0),method==0u);st.indirect=throughput;
    if(method==2u){let s=vl_equiangular_sample(origin,direction,eq_point,segment,st.rscatter);st.direct_t=s.x;st.equiangular_pdf=s.y;}
    // volume_integrate_heterogeneous (the octree setup and the guidance run as the first stages).
    st.o.t=vec2<f32>(tmin,bitcast<f32>(0x7f7fffffu));st.o.node=NONE;st.o.object=NONE;st.o.shader=NONE;st.o.scale=VH_MANTISSA_BITS;
    st.film=base;st.offset=offset;st.setup_from=0u;st.stage=VH_SETUP;
    var c:VhCoefficients;var have=false;
    loop{
        let t=vh_step(&st,c,have);if(t<0.0){break;}
        c=vh_eval(index,origin+direction*t,direction,st.scratch);have=true;
        if(c.error!=0u){return;}
    }
    // volume_direct_scatter_mis.
    if(st.use_mis&&st.direct_scatter){
        var weight:f32;
        if(st.method==1u){weight=vl_power(st.distance_pdf,vl_equiangular_pdf(origin,direction,eq_point,segment,st.direct_t));}else{weight=vl_power(st.equiangular_pdf,st.distance_pdf);}
        st.direct*=2.0*weight;
    }
//#if VOLUME_EMISSION
    if(any(st.emission!=vec3<f32>(0))){vh_write_emission(index,base,st.emission);}
//#endif
    if(bounce==0u){state_f32(S_path_optical_depth,index,heap_float(state_address(S_path_optical_depth,index,4u))+st.optical_depth);}
    // volume_integrate_event.
    let continuation=select(heap_float(state_address(S_path_continuation_probability,index,4u)),0.0,(flag&PATH_RAY_TERMINATE_IN_NEXT_VOLUME)!=0u);
    if(continuation==0.0){volume_finish(index,0u);return;}
//#if VOLUME_LIGHT
    if(st.direct_scatter){
        let direct_P=rounded_add3(origin,st.direct_t*direction);let weight=st.direct/continuation;
        store_word(vl+36u,bitcast<u32>(direct_P.x));store_word(vl+40u,bitcast<u32>(direct_P.y));store_word(vl+44u,bitcast<u32>(direct_P.z));
        store_word(vl+48u,bitcast<u32>(weight.x));store_word(vl+52u,bitcast<u32>(weight.y));store_word(vl+56u,bitcast<u32>(weight.z));
        store_word(vl+60u,st.direct_count);store_word(vl+76u,st.direct_base);store_word(vl+32u,1u);
    }
//#endif
    if(st.indirect_scatter){st.indirect/=continuation;}state_vec3(S_path_throughput,index,st.indirect);
    if(!st.indirect_scatter){volume_after(index);return;}
    // The indirect phase scatter: shade_volume_light (after the direct light) or shade_volume_phase run it from the
    // record; without either, here.
    let rec=p(24u)+id.x*VL_RECORD_BYTES;
    store_word(rec+68u,bitcast<u32>(st.indirect_t));store_word(rec+72u,load_word(volume_stack_address(index,entries-1u,VS_object)));
    store_word(rec+80u,st.indirect_base);store_word(rec+84u,st.indirect_count);store_word(rec+64u,1u);
}
//#if VOLUME_RAY_MARCHING
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
        shade_offset=native_rng(sample,pixel,offset+VR_PRNG_SHADE_OFFSET).x;step_offset=native_rng(sample,pixel,offset+VR_PRNG_OFFSET).x;
    }
    // volume_ray_marching_state_init.
    var rscatter=native_rng(sample,pixel,offset+V_PRNG_VOLUME_SCATTER_DISTANCE).x;var rchannel=native_rng(sample,pixel,offset+VR_PRNG_COLOR_CHANNEL).x;
    let use_mis=method==3u;if(use_mis){if(rscatter<0.5){rscatter*=2.0;method=1u;}else{rscatter=(rscatter-0.5)*2.0;method=2u;}}
    var equiangular_pdf=0.0;var distance_pdf=1.0;
    var direct=select(throughput,vec3<f32>(0),method==0u);var indirect=throughput;
    var direct_t=0.0;var indirect_t=0.0;var direct_scatter=false;var indirect_scatter=false;var direct_count=0u;var indirect_count=0u;
    if(method==2u){let e=vl_equiangular_sample(origin,direction,eq_point,segment,rscatter);direct_t=e.x;equiangular_pdf=e.y;}
    var emission=vec3<f32>(0);
    for(var step=0;step<0x7fffffff;step++){
        // volume_ray_marching_advance.
        if(t.y==tmax){break;}
        t.x=t.y;t.y=min(tmax,rounded_add(tmin,(f32(step)+step_offset)*size));
        let shade_t=rounded_add(t.x,shade_offset*rounded_add(t.y,-t.x));
        if(step>=max_steps){break;}
        let c=vh_eval(index,rounded_add3(origin,direction*shade_t),direction,scratch);if(c.error!=0u){return;}
        if((c.flag&(SR_EXTINCTION|SR_SCATTER|SR_EMISSION))==0u){continue;}
        let dt=rounded_add(t.y,-t.x);
        // Loop-local declarations take explicit assignments (naga does not re-run constant initializers).
        var transmittance:vec3<f32>;transmittance=vec3<f32>(1);if((c.flag&SR_EXTINCTION)!=0u){transmittance=exp(-c.sigma_t*dt);}
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
            // volume_ray_marching_step_scattering.
            let albedo=vh_safe_divide(c.sigma_s,c.sigma_t,1.0);let pick=vh_sample_channel(albedo,indirect,rchannel);rchannel=pick.y;let channel=u32(pick.x);
            let channel_pdf=vh_channel_pdf(albedo,indirect);
            if(method==2u&&!direct_scatter){
                if(direct_t>=t.x&&direct_t<=t.y&&equiangular_pdf>1e-8){
                    let new_dt=rounded_add(direct_t,-t.x);let new_transmittance=exp(-c.sigma_t*new_dt);
                    direct_scatter=true;direct*=select(vec3<f32>(0),vh_divide(c.sigma_s*new_transmittance,vec3<f32>(equiangular_pdf)),vec3<f32>(equiangular_pdf)!=vec3<f32>(0));
                    vh_copy_closures(scratch,direct_base,c.count);direct_count=c.count;
                    if(use_mis){let d=distance_pdf*rounded_dot(channel_pdf,c.sigma_t*new_transmittance);direct*=2.0*vl_power(equiangular_pdf,d);}
                }else{direct*=transmittance;distance_pdf*=rounded_dot(channel_pdf,transmittance);}
            }
            // volume_sample_indirect_scatter_ray_marching.
            var found:bool;found=false;
            if(!indirect_scatter){
                let sample_transmittance=transmittance[channel];
                if(rounded_add(1.0,-rscatter)>=sample_transmittance){
                    let new_dt=rounded_divide(-log(rounded_add(1.0,-rscatter)),c.sigma_t[channel]);let new_t=rounded_add(t.x,new_dt);
                    let new_transmittance=exp(-c.sigma_t*new_dt);let pdf=rounded_dot(channel_pdf,c.sigma_t*new_transmittance);
                    if(distance_pdf*pdf>1e-8){
                        indirect_scatter=true;indirect_t=new_t;indirect*=vh_divide(c.sigma_s*new_transmittance,vec3<f32>(pdf));if(method==1u){distance_pdf*=pdf;}
                        vh_copy_closures(scratch,indirect_base,c.count);indirect_count=c.count;found=true;
                    }
                }else{
                    let pdf=rounded_dot(channel_pdf,transmittance);indirect*=vh_divide(transmittance,vec3<f32>(pdf));if(method==1u){distance_pdf*=pdf;}
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
