//#if SHADOW_CATCHER
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Shadow catcher film semantics of the pinned Cycles kernel/film/light_passes.h
// (film_write_shadow_catcher, film_write_shadow_catcher_transparent,
// film_write_shadow_catcher_transparent_only, film_write_shadow_catcher_bounce_data)
// and the split decision of kernel/integrator/intersect_closest.h
// (kernel_shadow_catcher_is_path_split_bounce). The whole file is the
// SHADOW_CATCHER specialization region: modules of scenes without a shadow
// catcher do not contain it. Constants: shadow_catcher_layout() (shadow_catcher.cpp).
fn sc_film_add(address:u32,value:f32){
    var old=load_word(address);
    loop{let next=bitcast<u32>(bitcast<f32>(old)+value);let result=atomicCompareExchangeWeak(&heap[address/4u],old,next);
        if(result.exchanged){break;}old=result.old_value;}
}
fn sc_film_rgb(base:u32,offset:u32,value:vec3<f32>){
    if(offset==PASS_NONE){return;}
    sc_film_add(base+offset*4u,value.x);sc_film_add(base+(offset+1u)*4u,value.y);sc_film_add(base+(offset+2u)*4u,value.z);
}
fn sc_enabled()->bool{return data_word(SC_D_has_shadow_catcher)!=0u;}
// The matte and the combined pass of a matte path (no PATH_RAY_SHADOW_CATCHER_HIT)
// receive the same contributions. The pinned CPU device adds them in one order
// per pixel, so where every contribution is a matte one the two sums are equal and
// the Shadow Catcher pass (combined - matte) / catcher is exactly 0. Float atomics
// of two paths of one pixel in one dispatch (samples of one update, shadow paths of
// several bounces) take an undefined order per word, so the two sums could differ
// by an ulp, which the division amplifies (DEVICE.md 122). Each matte path adds
// all six words while it holds the matte pass's first word: it exchanges that word
// for SC_FILM_LOCK, adds the other five (atomic adds) and stores the sum last, so
// every word of both passes takes the contributions in the same order. The holder
// does its work in the iteration that won the exchange (no lane waits on a lane of
// its own subgroup). Every write of the matte colour goes through it. Contributions
// are finite (callers clear non-finite values), so no sum is the lock pattern.
const SC_FILM_LOCK:u32=0xffffffffu;
fn sc_matte_and_combined(base:u32,combined:u32,value:vec3<f32>){
    let first=base+data_word(SC_D_pass_shadow_catcher_matte)*4u;
    loop{
        let old=atomicLoad(&heap[first/4u]);
        if(old!=SC_FILM_LOCK){
            let result=atomicCompareExchangeWeak(&heap[first/4u],old,SC_FILM_LOCK);
            if(result.exchanged){
                sc_film_add(first+4u,value.y);sc_film_add(first+8u,value.z);sc_film_rgb(base,combined,value);
                atomicStore(&heap[first/4u],bitcast<u32>(bitcast<f32>(old)+value.x));break;
            }
        }
    }
}
// film_write_shadow_catcher and the caller's combined pass write (combined: the
// combined pass offset, PASS_NONE when the light pass flags exclude it): true when
// the contribution is fully handled (the caller then writes no adaptive pass).
fn sc_combined(flag:u32,base:u32,value:vec3<f32>,combined:u32)->bool{
    if(!sc_enabled()){sc_film_rgb(base,combined,value);return false;}
    if((flag&SC_PATH_PASS)!=0u){
        if((flag&SC_PATH_HIT)==0u){sc_matte_and_combined(base,PASS_NONE,value);}
        sc_film_rgb(base,data_word(SC_D_pass_shadow_catcher),value);return true;
    }
    if((flag&SC_PATH_HIT)==0u){sc_matte_and_combined(base,combined,value);}else{sc_film_rgb(base,combined,value);}
    return false;
}
// film_write_shadow_catcher_transparent (film_write_combined_transparent_pass), with
// the caller's combined colour write as sc_combined (the caller adds the alpha).
fn sc_combined_transparent(flag:u32,base:u32,value:vec3<f32>,transparent:f32,combined:u32)->bool{
    if(!sc_enabled()){sc_film_rgb(base,combined,value);return false;}
    if((flag&SC_PATH_BACKGROUND)!=0u){return true;}
    if((flag&SC_PATH_HIT)==0u){sc_film_add(base+(data_word(SC_D_pass_shadow_catcher_matte)+3u)*4u,transparent);}
    return sc_combined(flag,base,value,combined);
}
// film_write_shadow_catcher_transparent_only (film_write_transparent).
fn sc_transparent_only(flag:u32,base:u32,transparent:f32){
    if(!sc_enabled()){return;}
    if((flag&SC_PATH_HIT)==0u){sc_film_add(base+(data_word(SC_D_pass_shadow_catcher_matte)+3u)*4u,transparent);}
}
// Light passes and light groups are not written for shadow catcher paths.
fn sc_light_passes(flag:u32)->bool{return (flag&SC_PATH_HIT)==0u;}
//#endif
