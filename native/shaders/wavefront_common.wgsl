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
        // Cycles: x += hash_wang_seeded_float(...)*scrambling_distance; the jitter
        // product is rounded before the sum (integer add), so no driver can fuse it.
        let jitter=(vec4<f32>(hashes)*(1.0/4294967808.0))*distance;
        value=vec4<f32>(rounded_add(value.x,jitter.x),rounded_add(value.y,jitter.y),rounded_add(value.z,jitter.z),rounded_add(value.w,jitter.w));
        value-=floor(value);
    }
    return value;
}
//#if SAMPLING_SOBOL_BURLEY
// Sobol-Burley and the blue-noise patterns (pinned kernel/sample/pattern.h path_rng_1D-4D,
// blue_noise_indexing; sample/sobol_burley.h; sample/util.h; util/hash.h hash_hp_uint and
// uint_to_float_excl). Integer arithmetic throughout; the final conversion is the correctly
// rounded binary32 of the integer (cvtsi2ss) times 1/4294967808, made opaque to the compiler (OR with
// push word 31, which no dispatch sets) so no consumer's sum can contract the product.
fn sb_hash_hp(value:u32)->u32{var i=value;i^=i>>16u;i*=0x21f0aaadu;i^=i>>15u;i*=0xd35a2d97u;i^=i>>15u;return i^0xe6fe3bebu;}
fn sb_reversed_bit_owen(value:u32,seed:u32)->u32{var n=value;n^=n*0x3d20adeau;n+=seed;n*=(seed>>16u)|1u;n^=n*0x05526c56u;n^=n*0x53a22864u;return n;}
fn sb_unit(n:u32)->f32{
    // (float)n rounded to nearest even, then the product with 1/4294967808 (correctly rounded).
    var f=f32(n&0xffffffu);
    if(n>=0x1000000u){let shift=8u-countLeadingZeros(n);var m=n>>shift;let tail=n&((1u<<shift)-1u);let half=1u<<(shift-1u);
        if(tail>half||(tail==half&&(m&1u)!=0u)){m++;}f=f32(m)*bitcast<f32>((127u+shift)<<23u);}
    return bitcast<f32>(bitcast<u32>(f*(1.0/4294967808.0))|p(31u));
}
fn sb_sample(rev_index:u32,dimension:u32,scramble:u32)->f32{
    var result=0u;
    if(dimension==0u){result=reverseBits(rev_index);}
    else{var index=rev_index;var i=0u;
        loop{if(index==0u){break;}let j=countLeadingZeros(index);result^=SB_TABLE[dimension*32u+i+j];i+=j+1u;index=(index<<j)<<1u;}}
    return sb_unit(reverseBits(sb_reversed_bit_owen(result,scramble)));
}
// path_rng_1D..4D (count 1-4) for the patterns other than tabulated Sobol; unused components are 0.
fn native_rng_pattern(sample_id:u32,pixel:u32,dimension:u32,count:u32)->vec4<f32>{
    let pattern=data_word(SB_D_sampling_pattern);
    if(pattern==SB_TABULATED_SOBOL){return native_rng(sample_id,pixel,dimension);}
    var index=sample_id;var seed=pixel;var mask=data_word(SB_D_sobol_index_mask);
    if(pattern!=SB_SOBOL_BURLEY){
        let length=data_word(SB_D_blue_noise_sequence_length);mask=0xffffffffu;
        if(pattern==SB_BLUE_NOISE_FIRST&&sample_id==0u){index=pixel;seed=0x0cd0519fu;}
        else{index=select(sample_id,sample_id-1u,pattern==SB_BLUE_NOISE_FIRST)+pixel*length;seed=0u;}
    }
    seed^=sb_hash_hp(dimension);
    // Per-dimension-count salts of sobol_burley_sample_1D..4D.
    var shuffle=0xbff95bfeu;var salts=vec4<u32>(0x635c77bdu,0u,0u,0u);
    if(count==2u){shuffle=0xf8ade99au;salts=vec4<u32>(0xe0aaaf76u,0x94964d4eu,0u,0u);}
    else if(count==3u){shuffle=0xcaa726acu;salts=vec4<u32>(0x9e78e391u,0x67c33241u,0x78c395c5u,0u);}
    else if(count==4u){shuffle=0xc2c1a055u;salts=vec4<u32>(0x39468210u,0xe9d8a845u,0x5f32b482u,0x1524cc56u);}
    let shuffled=sb_reversed_bit_owen(reverseBits(index),seed^shuffle)&mask;
    var out=vec4<f32>(0);
    // One sb_sample body for every component (one inlined copy).
    for(var k=0u;k<count;k++){let salt=select(select(select(salts.w,salts.z,k==2u),salts.y,k==1u),salts.x,k==0u);
        out=select(out,vec4<f32>(sb_sample(shuffled,k,seed^salt)),vec4<u32>(0u,1u,2u,3u)==vec4<u32>(k));}
    return out;
}
// path_rng_pixel_init for the blue-noise patterns: nested_uniform_scramble_base4(morton2d(x, y), seed).
fn sb_expand(value:u32)->u32{var x=value&0xffffu;x=(x^(x<<8u))&0x00ff00ffu;x=(x^(x<<4u))&0x0f0f0f0fu;x=(x^(x<<2u))&0x33333333u;x=(x^(x<<1u))&0x55555555u;return x;}
fn sb_pixel(x:u32,y:u32,seed:u32)->u32{
    var n=reverseBits((sb_expand(x)<<1u)|sb_expand(y));
    n^=n*0x3d20adeau;n^=(n>>1u)&(n<<1u)&0x55555555u;n+=seed;n*=(seed>>16u)|1u;
    n^=(n>>1u)&(n<<1u)&0x55555555u;n^=n*0x05526c56u;n^=n*0x53a22864u;return reverseBits(n);
}
//#endif

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
