// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Light linking (pinned kernel/light/light.h light_link_receiver_*,
// light_link_object_match): with KERNEL_FEATURE_LIGHT_LINKING an emitter object
// lights a receiver only when the emitter's 64-bit light_set_membership holds the
// receiver's receiver_light_set. Without the feature every pair matches.
fn light_link_enabled()->bool{return (data_word(D_kernel_features)&LT_FEATURE_LIGHT_LINKING)!=0u;}
fn light_link_receiver_set(receiver:u32)->u32{
    if(receiver==NONE){return 0u;}
    let light_set=load_word(table_at(G_objects,receiver,SIZE_objects)+LT_O_receiver_light_set);if(light_set>=LT_LINK_SET_MAX){fail();return 0u;}
    return light_set;
}
fn light_link_match(receiver:u32,emitter:u32)->bool{
    if(!light_link_enabled()){return true;}
    // The pinned kernel asserts both objects exist.
    if(receiver==NONE||emitter==NONE){fail();return false;}
    let light_set=light_link_receiver_set(receiver);let membership=table_at(G_objects,emitter,SIZE_objects)+LT_O_light_set_membership;
    let word=load_word(membership+select(0u,4u,light_set>=32u));
    return ((word>>(light_set&31u))&1u)!=0u;
}
// light_link_receiver_forward: the object the current path last scattered from.
fn light_link_receiver_forward(index:u32)->u32{
    if(!light_link_enabled()){return NONE;}
    return state_word(S_path_mis_ray_object,index);
}
// integrate_surface_emission / lights_intersect / integrate_sun_lights: indirect
// rays see only the emitters linked to their receiver.
fn light_link_forward_visible(index:u32,emitter:u32)->bool{
    if(!light_link_enabled()||(state_byte(S_path_visibility,index)&LT_PATH_RAY_VISIBILITY_CAMERA)!=0u){return true;}
    return light_link_match(light_link_receiver_forward(index),emitter);
}
