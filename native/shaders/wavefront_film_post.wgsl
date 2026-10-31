// SPDX-FileCopyrightText: 2018-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Film post-processing (pinned kernel/device/gpu/kernel.h cryptomatte_postprocess,
// film/cryptomatte_passes.h film_cryptomatte_post and film_sort_cryptomatte_slots).
// Prefixed with the controls module header (heap, params, p, load/store, fail).
// p(1) pixel count, p(4) render buffer, p(5) pass stride, p(6) pass_cryptomatte,
// p(7) slot count (2 * cryptomatte_depth).
@compute @workgroup_size(64) fn cryptomatte_postprocess(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)||load_word(p(17u))!=0u){return;}
    let buffer=p(4u)+(id.x*p(5u)+p(6u))*4u;
    // Insertion sort of the (id, weight) slots by descending weight, up to the first empty slot.
    for(var slot=1u;slot<p(7u);slot++){
        if(bitcast<f32>(load_word(buffer+slot*8u))==0.0){return;}
        var i=slot;
        loop{
            if(i==0u){break;}
            let a=buffer+i*8u;let b=a-8u;
            if(!(bitcast<f32>(load_word(a+4u))>bitcast<f32>(load_word(b+4u)))){break;}
            let id_a=load_word(a);let w_a=load_word(a+4u);store_word(a,load_word(b));store_word(a+4u,load_word(b+4u));store_word(b,id_a);store_word(b+4u,w_a);
            i--;
        }
    }
}
