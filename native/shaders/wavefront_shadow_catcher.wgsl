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
// film_write_shadow_catcher: true when the contribution is fully handled (the
// caller then writes neither the combined nor the adaptive pass).
fn sc_combined(flag:u32,base:u32,value:vec3<f32>)->bool{
    if(!sc_enabled()){return false;}
    if((flag&SC_PATH_HIT)==0u){sc_film_rgb(base,data_word(SC_D_pass_shadow_catcher_matte),value);}
    if((flag&SC_PATH_PASS)!=0u){sc_film_rgb(base,data_word(SC_D_pass_shadow_catcher),value);return true;}
    return false;
}
// film_write_shadow_catcher_transparent (film_write_combined_transparent_pass).
fn sc_combined_transparent(flag:u32,base:u32,value:vec3<f32>,transparent:f32)->bool{
    if(!sc_enabled()){return false;}
    if((flag&SC_PATH_BACKGROUND)!=0u){return true;}
    if((flag&SC_PATH_HIT)==0u){let matte=data_word(SC_D_pass_shadow_catcher_matte);sc_film_rgb(base,matte,value);sc_film_add(base+(matte+3u)*4u,transparent);}
    if((flag&SC_PATH_PASS)!=0u){sc_film_rgb(base,data_word(SC_D_pass_shadow_catcher),value);return true;}
    return false;
}
// film_write_shadow_catcher_transparent_only (film_write_transparent).
fn sc_transparent_only(flag:u32,base:u32,transparent:f32){
    if(!sc_enabled()){return;}
    if((flag&SC_PATH_HIT)==0u){sc_film_add(base+(data_word(SC_D_pass_shadow_catcher_matte)+3u)*4u,transparent);}
}
// Light passes and light groups are not written for shadow catcher paths.
fn sc_light_passes(flag:u32)->bool{return (flag&SC_PATH_HIT)==0u;}
//#endif
