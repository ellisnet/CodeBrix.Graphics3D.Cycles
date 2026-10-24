// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Shadow catcher path split (pinned kernel/integrator/state_util.h
// integrator_state_shadow_catcher_split, intersect_closest.h
// integrator_split_shadow_catcher) and kernel/device/gpu/kernel.h
// integrator_shadow_catcher_count_possible_splits. Prefixed with the controls
// module header (heap, params, p, load/store, masked, fail).
//
// intersect_closest records each split in a per-path mark pair (code, slot):
// code = 0x80000000 | next kernel << 24 | shader. The upstream GPU kernel takes
// the new path index from an atomic counter in arrival order; here
// shadow_catcher_assign takes the indices in ascending path order (one of the
// orders the upstream atomic allows), which keeps renders and the integer state
// reproducible run to run. shadow_catcher_split then copies every main-path
// field (the descriptors of the compaction kernel) and initializes the copy:
// PATH_RAY_SHADOW_CATCHER_PASS, and either SHADE_BACKGROUND with
// PATH_RAY_SHADOW_CATCHER_BACKGROUND (integrator_path_init) or the sorted
// SHADE_SURFACE (integrator_path_init_sorted, key from the new index).
// p(1) capacity, p(2) path.queued_kernel, p(3) path.flag, p(4) marks,
// p(5) next_main_path_index, p(8) path.shader_sort_key, p(9) SHADE_SURFACE sort
// key counters, p(10) their count, p(11) max_shaders, p(12) sort partition
// divisor, p(13) queue counter, p(14)/p(15) state field descriptors.
@compute @workgroup_size(1) fn shadow_catcher_assign(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x!=0u||load_word(p(17u))!=0u){return;}let n=p(1u);
    for(var j=0u;j<n;j++){
        if(load_word(p(4u)+j*8u)==0u){continue;}
        let slot=load_word(p(5u));if(slot>=n||load_half(p(2u)+slot*2u)!=0u){fail();return;}
        store_word(p(5u),slot+1u);store_word(p(4u)+j*8u+4u,slot);
    }
}
@compute @workgroup_size(64) fn shadow_catcher_split(@builtin(global_invocation_id) id:vec3<u32>){
    let i=id.x;if(i>=p(1u)||load_word(p(17u))!=0u){return;}
    let code=load_word(p(4u)+i*8u);if(code==0u){return;}let slot=load_word(p(4u)+i*8u+4u);
    store_word(p(4u)+i*8u,0u);store_word(p(4u)+i*8u+4u,0u);
    if(slot>=p(1u)||slot==i){fail();return;}
    for(var f=0u;f<p(15u);f++){
        let descriptor=p(14u)+f*16u;
        if(load_word(descriptor+8u)!=0u){continue;}
        let address=load_word(descriptor);let stride=load_word(descriptor+4u);let count=load_word(descriptor+12u);
        if(i>=count||slot>=count){fail();continue;}
        let source=address+i*stride;let destination=address+slot*stride;
        if(stride==1u){masked(destination,(load_word(source)>>(8u*(source%4u)))&255u,8u);}
        else if(stride==2u){masked(destination,load_half(source),16u);}
        else{for(var b=0u;b<stride;b+=4u){store_word(destination+b,load_word(source+b));}}
    }
    let kernel=(code>>24u)&63u;let shader=code&0xffffffu;var flag=load_word(p(3u)+slot*4u)|SC_PATH_PASS;
    if(kernel==SC_K_BACKGROUND){flag|=SC_PATH_BACKGROUND;}
    else if(kernel==SC_K_SURFACE){
        let key=shader+p(11u)*(slot/p(12u));if(key>=p(10u)){fail();return;}
        store_word(p(8u)+slot*4u,key);atomicAdd(&heap[(p(9u)/4u)+key],1u);
    }else{fail();return;}
    store_word(p(3u)+slot*4u,flag);
    atomicAdd(&heap[(p(13u)/4u)+kernel],1u);masked(p(2u)+slot*2u,kernel,16u);
}
// p(1) work size, p(2) path.queued_kernel, p(3) path.flag, p(5) counter.
@compute @workgroup_size(64) fn shadow_catcher_count(@builtin(global_invocation_id) id:vec3<u32>){
    let i=id.x;if(i>=p(1u)){return;}
    if(load_half(p(2u)+i*2u)==0u){return;}let flag=load_word(p(3u)+i*4u);
    if((flag&SC_PATH_HIT)==0u&&(flag&SC_TRANSPARENT_BACKGROUND)!=0u){atomicAdd(&heap[p(5u)/4u],1u);}
}
