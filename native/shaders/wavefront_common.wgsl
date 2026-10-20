// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native Cycles state/table access and tabulated Sobol sampling.
fn data_word(offset:u32)->u32{return load_word(p(6u)+offset);}
fn data_float(offset:u32)->f32{return bitcast<f32>(data_word(offset));}
fn heap_float(address:u32)->f32{return bitcast<f32>(load_word(address));}
fn heap_vec3(address:u32)->vec3<f32>{return vec3<f32>(heap_float(address),heap_float(address+4u),heap_float(address+8u));}
fn heap_vec4(address:u32)->vec4<f32>{return vec4<f32>(heap_vec3(address),heap_float(address+12u));}
fn state_address(offset:u32,index:u32,stride:u32)->u32{return load_word(p(7u)+offset)+index*stride;}
fn state_u32(offset:u32,index:u32,value:u32){store_word(state_address(offset,index,4u),value);}
fn state_f32(offset:u32,index:u32,value:f32){state_u32(offset,index,bitcast<u32>(value));}
fn state_u16(offset:u32,index:u32,value:u32){masked(state_address(offset,index,2u),value,16u);}
fn state_u8(offset:u32,index:u32,value:u32){masked(state_address(offset,index,1u),value,8u);}
fn state_vec3(offset:u32,index:u32,value:vec3<f32>){let a=state_address(offset,index,12u);store_word(a,bitcast<u32>(value.x));store_word(a+4u,bitcast<u32>(value.y));store_word(a+8u,bitcast<u32>(value.z));}
fn native_rng(sample_id:u32,pixel:u32,dimension:u32)->vec4<f32>{
    let count=data_word(D_integrator_tabulated_sobol_sequence_size);
    let distance=data_float(D_integrator_scrambling_distance);
    let seed=select(pixel,data_word(D_integrator_seed),distance<1.0);
    let pattern=shuffle_pattern(dimension,SOBOL_PATTERNS,seed);
    let mask=count-1u;let shuffled=owen_scramble(sample_id,wang_hash(dimension,seed));
    let sample_value=(sample_id&~mask)|(shuffled&mask);
    let index=(pattern*count+sample_value)%(count*SOBOL_PATTERNS);
    var value=heap_vec4(p(9u)+index*16u);
    if(distance<1.0){
        let hashes=vec4<u32>(wang_hash(dimension,pixel),wang_hash(dimension,pixel^0xca0e1151u),wang_hash(dimension,pixel^0xbf604c5au),wang_hash(dimension,pixel^0x99634d1du));
        value+=(vec4<f32>(hashes)*(1.0/4294967808.0))*distance;value-=floor(value);
    }
    return value;
}

fn table_count(table:u32)->u32{return load_word(p(14u)+table*8u+4u);}
fn table_at(table:u32,item:u32,stride:u32)->u32{
    if(item>=table_count(table)){fail();return 0u;}
    return load_word(p(14u)+table*8u)+item*stride;
}
fn state_word(field:u32,index:u32)->u32{return load_word(state_address(field,index,4u));}
fn state_half(field:u32,index:u32)->u32{return load_half(state_address(field,index,2u));}
fn state_byte(field:u32,index:u32)->u32{let a=state_address(field,index,1u);return (load_word(a)>>(8u*(a%4u)))&255u;}
fn path_ao(index:u32)->bool{
    let limit=data_word(D_integrator_ao_bounces);if(limit==0u){return false;}
    let bounce=i32(state_half(S_path_bounce,index))-i32(state_half(S_path_transmission_bounce,index))-select(0i,1i,state_half(S_path_glossy_bounce,index)>0u)+1;
    return bounce>i32(limit);
}

fn volume_enabled()->bool{return (data_word(D_kernel_features)&KERNEL_FEATURE_VOLUME)!=0u;}
fn volume_stack_address(index:u32,slot:u32,member:u32)->u32{return load_word(p(7u)+S_volume_stack+slot*VS_stride+member)+index*4u;}
fn volume_active(index:u32)->bool{return volume_enabled()&&load_word(volume_stack_address(index,0u,VS_shader))!=NONE;}
fn volume_atomic_add(address:u32,value:f32){
    var old=load_word(address);loop{let next=bitcast<u32>(bitcast<f32>(old)+value);let result=atomicCompareExchangeWeak(&heap[address/4u],old,next);if(result.exchanged){break;}old=result.old_value;}
}
fn volume_write_depth(index:u32){
    if(!volume_enabled()||(state_word(S_path_flag,index)&PATH_RAY_VOLUME_PRIMARY_TRANSMIT)==0u){return;}
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_path_render_pixel_index,index);
    if(pixel>=p(11u)/stride){fail();return;}let base=p(4u)+pixel*stride*4u;
    volume_atomic_add(base+data_word(D_film_pass_volume_majorant)*4u,heap_float(state_address(S_path_optical_depth,index,4u)));
    volume_atomic_add(base+data_word(D_film_pass_volume_majorant_sample_count)*4u,1.0);
}
fn volume_guidance_pass(index:u32,base:u32,value:vec3<f32>){
    if(!volume_enabled()){return;}var offset=PASS_NONE;
    if((state_word(S_path_flag,index)&PATH_RAY_VOLUME_PRIMARY_TRANSMIT)!=0u){offset=data_word(D_film_pass_volume_transmit);}
    else if((state_byte(S_path_visibility,index)&VIS_VOLUME)!=0u){offset=data_word(D_film_pass_volume_scatter);}
    if(offset!=PASS_NONE){for(var k=0u;k<3u;k++){volume_atomic_add(base+(offset+k)*4u,value[k]);}}
}

fn volume_transparent_pass(index:u32,base:u32,transparency:f32){
    if(volume_enabled()&&(state_word(S_path_flag,index)&PATH_RAY_VOLUME_PRIMARY_TRANSMIT)!=0u){
        let offset=data_word(D_film_pass_volume_transmit);for(var k=0u;k<3u;k++){volume_atomic_add(base+(offset+k)*4u,transparency);}
    }
}

// Native volume stack mutation: bounded scan, duplicate suppression, swap-last
// exit, and ignore a valid enter when the native stack is already full.
fn volume_stack_count(index:u32)->u32{
    let limit=data_word(D_volume_stack_size);
    for(var slot=0u;slot<limit;slot++){
        let shader=load_word(volume_stack_address(index,slot,VS_shader));if(shader==NONE){return slot;}
        let object=load_word(volume_stack_address(index,slot,VS_object));
        if((shader&SHADER_MASK)>=table_count(G_shaders)||(object!=NONE&&object>=table_count(G_objects))){fail();return NONE;}
        if((load_word(table_at(G_shaders,shader&SHADER_MASK,SIZE_shaders)+SH_flags)&VS_HAS_VOLUME)==0u){fail();return NONE;}
        for(var prior=0u;prior<slot;prior++){
            if(load_word(volume_stack_address(index,prior,VS_object))==object&&load_word(volume_stack_address(index,prior,VS_shader))==shader){fail();return NONE;}
        }
    }
    fail();return NONE;
}
fn volume_stack_enter_exit(index:u32,object:u32,shader:u32,backfacing:bool){
    if(!volume_enabled()){return;}
    let flags=load_word(table_at(G_shaders,shader&SHADER_MASK,SIZE_shaders)+SH_flags);if((flags&VS_HAS_VOLUME)==0u){return;}
    let count=volume_stack_count(index);if(count==NONE){return;}
    var found=NONE;for(var slot=0u;slot<count;slot++){
        if(load_word(volume_stack_address(index,slot,VS_object))==object&&load_word(volume_stack_address(index,slot,VS_shader))==shader){found=slot;break;}}
    if(backfacing){
        if(found==NONE){return;}
        let last=count-1u;if(found!=last){
            store_word(volume_stack_address(index,found,VS_object),load_word(volume_stack_address(index,last,VS_object)));
            store_word(volume_stack_address(index,found,VS_shader),load_word(volume_stack_address(index,last,VS_shader)));}
        store_word(volume_stack_address(index,last,VS_object),NONE);store_word(volume_stack_address(index,last,VS_shader),NONE);
    }else if(found==NONE&&count<data_word(D_volume_stack_size)-1u){
        store_word(volume_stack_address(index,count,VS_object),object);store_word(volume_stack_address(index,count,VS_shader),shader);
        store_word(volume_stack_address(index,count+1u,VS_object),NONE);store_word(volume_stack_address(index,count+1u,VS_shader),NONE);
    }
}
