R"CBWGSL(// Copyright (c) 2026 Jeremy Ellis and contributors
// Derived source_index Blender Cycles kernel/device/gpu/kernel.h, parallel_prefix_sum.h,
// parallel_active_index.h, parallel_sorted_index.h, integrator/state_util.h.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2021-2022 Blender Foundation
// SPDX-License-Identifier: Apache-2.0
// Changes: virtual byte addresses, portable atomic index reservation, and
// byte/halfword CAS writes preserve the original native SoA layout in WGSL.
@group(0) @binding(0) var<storage,read_write> heap: array<atomic<u32>>;
struct Parameters { words: array<vec4<u32>,8> }
@group(0) @binding(1) var<uniform> params: Parameters;
fn p(i:u32)->u32{return params.words[i/4u][i%4u];}
fn load_word(address:u32)->u32{return atomicLoad(&heap[address/4u]);}
fn store_word(address:u32,value:u32){atomicStore(&heap[address/4u],value);}
fn load_half(address:u32)->u32{return (load_word(address)>>(8u*(address%4u)))&65535u;}
fn masked(address:u32,value:u32,bits:u32){
    let shift=8u*(address%4u);let mask=((1u<<bits)-1u)<<shift;
    var old=load_word(address);
    loop { let result=atomicCompareExchangeWeak(&heap[address/4u],old,(old&~mask)|((value<<shift)&mask));
        if(result.exchanged){break;} old=result.old_value; }
}
fn fail(){atomicOr(&heap[p(17u)/4u],1u);}
@compute @workgroup_size(64) fn control(@builtin(global_invocation_id) id:vec3<u32>){
    let i=id.x;let op=p(0u);let n=p(1u);
    if(op==1u){
        if(i!=0u){return;}var offset=0u;
        for(var k=0u;k<n;k++){let value=load_word(p(8u)+k*4u);store_word(p(9u)+k*4u,offset);store_word(p(8u)+k*4u,0u);offset+=value;}
        return;
    }
    // Stable GPU-only reservation/selection for resumable transparent shadows.
    // The native scheduler has already budgeted one possible shadow per input.
    if(op==5u){
        if(i!=0u||load_word(p(17u))!=0u){return;}let base=load_word(p(2u));
        if(base>p(18u)||n>p(18u)-base){fail();return;}
        store_word(p(4u),base);
        for(var slot=base;slot<base+n;slot++){masked(p(3u)+slot*2u,0u,16u);}
        store_word(p(2u),base+n);return;
    }
    // A serial bounded scan keeps queue order stable without CPU decisions.
    // This correctness path is O(path capacity); parallel stable scan is future work.
    if(p(21u)!=0u&&(op==2u||op==3u)){
        if(i!=0u){return;}
        for(var j=0u;j<n;j++){
            let queued=load_half(p(2u)+j*2u);
            if(op==2u){let mode=p(19u);
                let selected=select(select(select(queued==p(6u),queued!=0u,mode==1u),queued==0u,mode==2u),j>=p(6u)&&queued!=0u,mode==3u);
                if(selected){let at=load_word(p(5u));if(at>=n){fail();return;}store_word(p(5u),at+1u);store_word(p(4u)+(at+p(7u))*4u,j);}
            }else if(queued==p(6u)){
                let key=load_word(p(10u)+j*4u);if(key>=p(20u)){fail();return;}let at=load_word(p(9u)+key*4u);store_word(p(9u)+key*4u,at+1u);
                if(at<p(11u)){if(at>=n){fail();return;}store_word(p(4u)+at*4u,j);}
                else{let address=p(8u)+key*4u;store_word(address,load_word(address)+1u);}
            }
        }return;
    }
    if(i>=n){return;}
    if(op==0u){masked(p(2u)+i*2u,0u,16u);masked(p(3u)+i*2u,0u,16u);return;}
    if(op==2u){
        let queued=load_half(p(2u)+i*2u);let mode=p(19u);
        let selected=select(select(select(queued==p(6u),queued!=0u,mode==1u),queued==0u,mode==2u),i>=p(6u)&&queued!=0u,mode==3u);
        if(selected){let index=atomicAdd(&heap[p(5u)/4u],1u);if(index>=n){fail();return;}store_word(p(4u)+(index+p(7u))*4u,i);}
        return;
    }
    if(op==3u){
        if(load_half(p(2u)+i*2u)!=p(6u)){return;}
        let key=load_word(p(10u)+i*4u);if(key>=p(20u)){fail();return;}
        let index=atomicAdd(&heap[(p(9u)/4u)+key],1u);
        if(index<p(11u)){if(index>=n){fail();return;}store_word(p(4u)+index*4u,i);}
        else{atomicAdd(&heap[(p(8u)/4u)+key],1u);}
        return;
    }
    if(op==4u){
        let source_index=load_word(p(4u)+(p(12u)+i)*4u);let destination_index=load_word(p(4u)+(p(13u)+i)*4u);
        if(source_index>=p(18u)||destination_index>=p(18u)||source_index==destination_index){fail();return;}
        // Upstream compaction moves selected states above the selected-count boundary
        // into distinct terminated slots below it; source and destination sets
        // are disjoint. Scalar byte writes still share words and require CAS.
        for(var f=0u;f<p(15u);f++){
            let descriptor=p(14u)+f*16u;
            if(load_word(descriptor+8u)!=p(16u)){continue;}
            let address=load_word(descriptor);let stride=load_word(descriptor+4u);let count=load_word(descriptor+12u);
            if(source_index>=count||destination_index>=count){fail();continue;}
            let source=address+source_index*stride;let destination=address+destination_index*stride;
            if(stride==1u){masked(destination,(load_word(source)>>(8u*(source%4u)))&255u,8u);}
            else if(stride==2u){masked(destination,load_half(source),16u);}
            else{for(var b=0u;b<stride;b+=4u){store_word(destination+b,load_word(source+b));}}
        }
        masked(p(2u)+source_index*2u,0u,16u);
    }
}
fn wang_hash(value: u32, seed: u32) -> u32 {
    var i=(value^61u)^seed; i+=i<<3u; i^=i>>4u; i*=0x27d4eb2du; return i;
}
fn pixel_hash(x: u32, y: u32, seed: u32) -> u32 {
    let qx=1103515245u*((x>>1u)^y); let qy=1103515245u*((y>>1u)^x);
    return (1103515245u*(qx^(qy>>3u)))^seed;
}
fn owen_scramble(value: u32, seed: u32) -> u32 {
    var n=reverseBits(value); n^=n*0x3d20adeau; n+=seed;
    n*=(seed>>16u)|1u; n^=n*0x05526c56u; n^=n*0x53a22864u; return reverseBits(n);
}
fn shuffle_pattern(value: u32, length: u32, seed: u32) -> u32 {
    var i=value%length;
    let mask=(1u<<(32u-countLeadingZeros(length-1u)))-1u;
    loop {
        i^=seed; i*=0xe170893du; i^=seed>>16u; i^=(i&mask)>>4u;
        i^=seed>>8u; i*=0x0929eb3fu; i^=seed>>23u; i^=(i&mask)>>1u;
        i*=1u|(seed>>27u); i*=0x6935fa69u; i^=(i&mask)>>11u;
        i*=0x74dcb303u; i^=(i&mask)>>2u; i*=0x9e501cc3u;
        i^=(i&mask)>>2u; i*=0xc860a3dfu; i&=mask; i^=i>>5u;
        if(i<length) { break; }
    }
    return i;
}
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
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Original binary32 adapters for rounding-sensitive normal and closure math.
// Integer addition makes the rounding boundary observable to GPU optimizers:
// a product passed here cannot be contracted into a later floating-point sum.
fn rounded_add(a:f32,b:f32)->f32{
    var ax=bitcast<u32>(a);var bx=bitcast<u32>(b);
    if((ax&0x7fffffffu)<(bx&0x7fffffffu)){let swap=ax;ax=bx;bx=swap;}
    let sign=ax&0x80000000u;let am=ax&0x7fffffffu;let bm=bx&0x7fffffffu;
    if(am==0u){return bitcast<f32>((ax&bx)&0x80000000u);}
    if(bm==0u){return bitcast<f32>(ax);}
    if(am>=0x7f800000u){return a+b;}
    if(am==bm&&((ax^bx)&0x80000000u)!=0u){return 0.0;}
    let ae=am>>23u;let be=bm>>23u;var exponent=max(ae,1u);
    let delta=exponent-max(be,1u);
    let ma=((am&0x7fffffu)|select(0u,0x800000u,ae!=0u))<<3u;
    var mb=((bm&0x7fffffu)|select(0u,0x800000u,be!=0u))<<3u;
    if(delta>=31u){mb=1u;}
    else if(delta>0u){mb=(mb>>delta)|select(0u,1u,(mb&((1u<<delta)-1u))!=0u);}
    var sum=ma+mb;if(((ax^bx)&0x80000000u)!=0u){sum=ma-mb;}
    if(sum>=0x8000000u){sum=(sum>>1u)|(sum&1u);exponent++;}
    else if(sum<0x4000000u){let shift=min(countLeadingZeros(sum)-5u,exponent-1u);sum<<=shift;exponent-=shift;}
    let tail=sum&7u;var mantissa=sum>>3u;
    if(tail>4u||(tail==4u&&(mantissa&1u)!=0u)){mantissa++;}
    if(mantissa>=0x1000000u){mantissa>>=1u;exponent++;}
    if(mantissa<0x800000u){exponent=0u;}
    if(exponent>=255u){return bitcast<f32>(sign|0x7f800000u);}
    return bitcast<f32>(sign|(exponent<<23u)|(mantissa&0x7fffffu));
}
fn rounded_add3(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{return vec3<f32>(rounded_add(a.x,b.x),rounded_add(a.y,b.y),rounded_add(a.z,b.z));}
fn rounded_dot(a:vec3<f32>,b:vec3<f32>)->f32{return rounded_add(rounded_add(a.x*b.x,a.y*b.y),a.z*b.z);}
fn rounded_cross(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{return rounded_add3(a.yzx*b.zxy,-a.zxy*b.yzx);}
// The state contains an integer floor candidate and its exact remainder.
fn rounded_root_step(state:vec2<u32>,step:u32)->vec2<u32>{
    let cost=(2u*state.x+step)*step;
    return select(state,vec2<u32>(state.x+step,state.y-cost),state.y>=cost);
}
fn rounded_quotient_step(state:vec2<u32>,denominator:u32,step:u32)->vec2<u32>{
    let cost=denominator*step;
    return select(state,vec2<u32>(state.x+step,state.y-cost),state.y>=cost);
}
// WGSL permits 2.5 ULP division error. Its inherited sqrt bound combines
// 2 ULP inverseSqrt with that division bound. For these normalized arguments,
// +/-4 quotient units and +/-16 root units contain the rounded exact result,
// including the wider ULP if an intermediate crosses the upper binade.
// Starting one integer below the lower bound gives a nonnegative remainder.
// That remainder fits in one word even though the numerator/product does not:
// subtracting their low words recovers it exactly, modulo 2^32. Fixed steps
// then recover floor and remainder before nearest-even rounding. This avoids
// repeated wide products and data-dependent loops inside the larger SVM.
// https://www.w3.org/TR/WGSL/#floating-point-accuracy
fn rounded_sqrt(value:f32)->f32{
    let bits=bitcast<u32>(value);if((bits&0x7fffffffu)==0u){return value;}
    if((bits&0x80000000u)!=0u||(bits&0x7f800000u)==0x7f800000u){return sqrt(value);}
    var exponent=i32(bits>>23u);var mantissa=bits&0x7fffffu;
    if(exponent==0){let shift=countLeadingZeros(mantissa)-8u;mantissa<<=shift;exponent=1-i32(shift);}
    else{mantissa|=0x800000u;}
    let unbiased=exponent-127;let shift=23u+u32(unbiased&1);
    let low=mantissa<<shift;
    let scaled=bitcast<f32>((mantissa&0x7fffffu)|(u32(127+(unbiased&1))<<23u));
    let estimate=bitcast<u32>(sqrt(scaled));
    var root=((estimate&0x7fffffu)|0x800000u)<<select(0u,1u,(estimate>>23u)>127u);
    if((estimate>>23u)<127u){root>>=1u;}
    root-=17u;var state=vec2<u32>(root,low-root*root);
    state=rounded_root_step(state,32u);state=rounded_root_step(state,16u);
    state=rounded_root_step(state,8u);state=rounded_root_step(state,4u);
    state=rounded_root_step(state,2u);state=rounded_root_step(state,1u);
    // An integer radicand cannot lie exactly at a squared half-integer.
    root=state.x+select(0u,1u,4u*state.y>4u*state.x+1u);
    var result_exponent=(unbiased>>1u)+127;
    if(root==0x1000000u){root>>=1u;result_exponent++;}
    return bitcast<f32>((u32(result_exponent)<<23u)|(root&0x7fffffu));
}
fn rounded_divide(a:f32,b:f32)->f32{
    let ab=bitcast<u32>(a);let bb=bitcast<u32>(b);let sign=(ab^bb)&0x80000000u;
    let am=ab&0x7fffffffu;let bm=bb&0x7fffffffu;
    if(bm==0u||am>=0x7f800000u||bm>=0x7f800000u){return a/b;}
    if(am==0u){return bitcast<f32>(sign);}
    var ae=i32(am>>23u);var be=i32(bm>>23u);
    var ma=am&0x7fffffu;var mb=bm&0x7fffffu;
    if(ae==0){let shift=countLeadingZeros(ma)-8u;ma<<=shift;ae=1-i32(shift);}else{ma|=0x800000u;}
    if(be==0){let shift=countLeadingZeros(mb)-8u;mb<<=shift;be=1-i32(shift);}else{mb|=0x800000u;}
    var exponent=ae-be+127;if(ma<mb){ma<<=1u;exponent--;}
    let estimate=bitcast<u32>(f32(ma)/f32(mb));
    var quotient=((estimate&0x7fffffu)|0x800000u)<<select(0u,1u,(estimate>>23u)>127u);
    if((estimate>>23u)<127u){quotient>>=1u;}
    quotient-=5u;var state=vec2<u32>(quotient,(ma<<23u)-mb*quotient);
    state=rounded_quotient_step(state,mb,8u);state=rounded_quotient_step(state,mb,4u);
    state=rounded_quotient_step(state,mb,2u);state=rounded_quotient_step(state,mb,1u);
    quotient=state.x;let remainder=state.y;
    if(exponent<=0){
        // Use floor and remainder directly to avoid double rounding.
        let shift=u32(1-exponent);if(shift>24u){return bitcast<f32>(sign);}
        let tail=quotient&((1u<<shift)-1u);let midpoint=1u<<(shift-1u);quotient>>=shift;
        if(tail>midpoint||(tail==midpoint&&(remainder!=0u||(quotient&1u)!=0u))){quotient++;}
        return bitcast<f32>(sign|quotient);
    }
    if(2u*remainder>mb||(2u*remainder==mb&&(quotient&1u)!=0u)){quotient++;}
    if(quotient==0x1000000u){quotient>>=1u;exponent++;}
    if(exponent>=255){return bitcast<f32>(sign|0x7f800000u);}
    return bitcast<f32>(sign|(u32(exponent)<<23u)|(quotient&0x7fffffu));
}
fn rounded_normalize(v:vec3<f32>)->vec3<f32>{
    let norm=rounded_sqrt(rounded_dot(v,v));
    return vec3<f32>(rounded_divide(v.x,norm),rounded_divide(v.y,norm),rounded_divide(v.z,norm));
}
fn rounded_safe_normalize(v:vec3<f32>)->vec3<f32>{
    let norm=rounded_sqrt(rounded_dot(v,v));if(norm==0.0){return v;}
    return v*rounded_divide(1.0,norm);
}
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Original two-component Taylor evaluation for disk angles in [-pi/4,3*pi/4].
// Reduction to [-pi/4,pi/4] uses a split pi/2; sine degree 15 and cosine degree
// 16 use signed reciprocal factorials split into high/low binary32 parts.
// This is a bounded adapter, not a general-purpose transcendental library.
// The small-angle return also keeps the product-residual scales normal.
// The low-word product difference recovers its exact rounding residual: both
// 24-bit significands are integers and the residual fits in signed 24 bits.
fn trig_product(a:f32,b:f32)->vec2<f32>{
    let product=a*b;
    let ab=bitcast<u32>(a);let bb=bitcast<u32>(b);let pb=bitcast<u32>(product);
    if((ab&0x7fffffffu)==0u||(bb&0x7fffffffu)==0u){return vec2<f32>(product,0);}
    let ae=(ab>>23u)&255u;let be=(bb>>23u)&255u;let pe=(pb>>23u)&255u;
    let ma=(ab&0x7fffffu)|0x800000u;let mb=(bb&0x7fffffu)|0x800000u;let mp=(pb&0x7fffffu)|0x800000u;
    let shift=pe+150u-ae-be;
    let residual=bitcast<i32>(ma*mb-(mp<<shift));
    let scale=bitcast<f32>(((ae+be-173u)<<23u)|((ab^bb)&0x80000000u));
    return vec2<f32>(product,f32(residual)*scale);
}
fn trig_mul(a:vec2<f32>,b:vec2<f32>)->vec2<f32>{
    let p=trig_product(a.x,b.x);
    return vec2<f32>(p.x,(p.y+a.x*b.y)+a.y*b.x);
}
// The Taylor coefficient is larger than the incoming Horner product, so the
// first rounded sum plus its recovered residual retains the low component.
fn trig_coeff(a:vec2<f32>,b:vec2<f32>)->vec2<f32>{
    let high=rounded_add(a.x,b.x);
    let residual=rounded_add(b.x,rounded_add(a.x,-high));
    return vec2<f32>(high,(residual+a.y)+b.y);
}
fn precise_disk_sincos(angle:f32)->vec2<f32>{
    if(angle==0.0){return vec2<f32>(angle,1);}
    var x=vec2<f32>(angle,0);let swap=angle>0.7853981633974483;
    if(swap){x=vec2<f32>(rounded_add(1.5707963705062866,-angle),-4.3711390001862426e-8);}
    if(abs(x.x)<0.000244140625){return select(vec2<f32>(rounded_add(x.x,x.y),1),vec2<f32>(1,rounded_add(x.x,x.y)),swap);}
    let z=trig_mul(x,x);
    var s=vec2<f32>(-7.647163609812713e-13,-1.2200710471178288e-20);
    s=trig_coeff(vec2<f32>(1.6059044372074283e-10,-5.352526511562726e-18),trig_mul(s,z));
    s=trig_coeff(vec2<f32>(-2.5052107943679403e-08,-4.4176230446483665e-16),trig_mul(s,z));
    s=trig_coeff(vec2<f32>(2.7557318844628753e-06,3.793571224297229e-14),trig_mul(s,z));
    s=trig_coeff(vec2<f32>(-0.00019841270113829523,2.725596874933456e-12),trig_mul(s,z));
    s=trig_coeff(vec2<f32>(0.008333333767950535,-4.34617203337595e-10),trig_mul(s,z));
    s=trig_coeff(vec2<f32>(-0.1666666716337204,4.967053879312289e-09),trig_mul(s,z));
    s=trig_coeff(vec2<f32>(1.0,0.0),trig_mul(s,z));
    var c=vec2<f32>(4.7794772561329454e-14,7.62544404448643e-22);
    c=trig_coeff(vec2<f32>(-1.147074536050896e-11,-2.372207689231238e-19),trig_mul(c,z));
    c=trig_coeff(vec2<f32>(2.0876755879584152e-09,1.1082839809204342e-16),trig_mul(c,z));
    c=trig_coeff(vec2<f32>(-2.755731998149713e-07,7.575112209051195e-15),trig_mul(c,z));
    c=trig_coeff(vec2<f32>(2.4801587642286904e-05,-3.40699609366682e-13),trig_mul(c,z));
    c=trig_coeff(vec2<f32>(-0.0013888889225199819,3.3631094437103215e-11),trig_mul(c,z));
    c=trig_coeff(vec2<f32>(0.0416666679084301,-1.2417634698280722e-09),trig_mul(c,z));
    c=trig_coeff(vec2<f32>(-0.5,0.0),trig_mul(c,z));
    c=trig_coeff(vec2<f32>(1.0,0.0),trig_mul(c,z));
    s=trig_mul(s,x);
    let sine=rounded_add(s.x,s.y);let cosine=rounded_add(c.x,c.y);
    return select(vec2<f32>(sine,cosine),vec2<f32>(cosine,sine),swap);
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native decomposed motion transform interpolation from reviewed util/transform.h.
struct NativeTransform {x:vec4<f32>,y:vec4<f32>,z:vec4<f32>}
fn motion_transform_load(address:u32)->NativeTransform{
    return NativeTransform(heap_vec4(address),heap_vec4(address+16u),heap_vec4(address+32u));
}
fn motion_direction(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    return vec3<f32>(t.x.x,t.y.x,t.z.x)*v.x+(vec3<f32>(t.x.y,t.y.y,t.z.y)*v.y+vec3<f32>(t.x.z,t.y.z,t.z.z)*v.z);
}
// Original numerical adapter: paired binary32 sums match native float4 dot;
// arcsine Taylor coefficients derive from C(2n,n)/(4^n*(2n+1)). Reducing acos
// to 2*asin(sqrt((1-|x|)/2)) bounds the series argument by sqrt(1/2).
// Thirty terms leave a tail below 1e-11; two-component products retain the
// square-root residual and Horner roundoff instead of using loose GPU acos.
fn motion_dot4(a:vec4<f32>,b:vec4<f32>)->f32{
    return rounded_add(rounded_add(a.x*b.x,a.y*b.y),rounded_add(a.z*b.z,a.w*b.w));
}
fn motion_add4(a:vec4<f32>,b:vec4<f32>)->vec4<f32>{
    return vec4<f32>(rounded_add3(a.xyz,b.xyz),rounded_add(a.w,b.w));
}
fn motion_normalize4(a:vec4<f32>)->vec4<f32>{
    let norm=rounded_sqrt(motion_dot4(a,a));
    return vec4<f32>(rounded_divide(a.x,norm),rounded_divide(a.y,norm),rounded_divide(a.z,norm),rounded_divide(a.w,norm));
}
fn motion_acos(value:f32)->f32{
    let absolute=abs(clamp(value,-1.0,1.0));let high=rounded_add(1.0,-absolute);
    let low=rounded_add(rounded_add(1.0,-high),-absolute);
    let x=vec2<f32>(high*.5,low*.5);let root=rounded_sqrt(x.x);
    if(root==0.0){return select(0.0,3.141592653589793,value<0.0);}
    let product=trig_product(root,root);
    let residual=rounded_add(rounded_add(x.x,-product.x),rounded_add(x.y,-product.y));
    let z=vec2<f32>(root,rounded_divide(residual,2.0*root));
    var polynomial=vec2<f32>(0.0017680810997262597,2.0789158589051837e-11);
    polynomial=trig_coeff(vec2<f32>(0.0018622264033183455,3.0847819958157441e-12),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0019650335889309645,2.7346319219495596e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0020776609890162945,4.3501873110696199e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0022014740388840437,-6.5173905335058624e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0023380918428301811,4.9281793923494632e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0024894487578421831,-7.9595299535412112e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0026578705292195082,1.0898778190701619e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0028461783658713102,3.5237631822865323e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0030578216537833214,-4.5252907324155878e-12),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0032970595639199018,-6.0446416969006656e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0035692052915692329,1.0225670180399882e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0038809645920991898,-3.3261520707844605e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0042409072630107403,-1.6933137704511214e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0046601435169577599,-3.004266366929409e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.005153309553861618,1.2845828654267644e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0057400376535952091,1.7246714473229119e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0064472104422748089,-1.3038516013391677e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0073125259950757027,-1.2147685721913826e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0083903353661298752,4.4348693988005561e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0097616091370582581,3.9213582034292038e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.011551801115274429,-2.1913472425527658e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.013964843936264515,-1.8626451422920631e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.017352763563394547,8.5968237736877562e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.022372158244252205,8.4665688696028418e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0303819440305233,4.139211438880519e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0446428582072258,-1.0643686557121335e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.075000002980232239,-2.9802322415450888e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.1666666716337204,-4.9670537405344106e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(1,0),trig_mul(polynomial,x));
    let result=2.0*trig_mul(z,polynomial);
    if(value<0.0){return rounded_add(rounded_add(3.1415927410125732,-result.x),rounded_add(-8.742278000372485e-8,-result.y));}
    return rounded_add(result.x,result.y);
}
fn motion_sincos(angle:f32)->vec2<f32>{
    if(angle<=2.356194490192345){return precise_disk_sincos(angle);}
    let high=rounded_add(3.1415927410125732,-angle);let low=-8.742278000372485e-8;
    let sc=precise_disk_sincos(high);
    return vec2<f32>(rounded_add(sc.x,sc.y*low),-rounded_add(sc.y,-sc.x*low));
}
fn motion_rotation(a:vec4<f32>,b:vec4<f32>,t:f32)->vec4<f32>{
    let costheta=motion_dot4(a,b);
    if(costheta>0.9995){return motion_normalize4(motion_add4((1.0-t)*a,t*b));}
    let theta=motion_acos(costheta);let perpendicular=motion_normalize4(motion_add4(b,-a*costheta));
    let angle=theta*t;let sc=motion_sincos(angle);return motion_add4(a*sc.y,perpendicular*sc.x);
}
fn motion_compose(q:vec4<f32>,y:vec4<f32>,z:vec4<f32>,w:vec4<f32>)->NativeTransform{
    let q0=1.4142135623730951*q.w;let q1=1.4142135623730951*q.x;
    let q2=1.4142135623730951*q.y;let q3=1.4142135623730951*q.z;
    let qda=q0*q1;let qdb=q0*q2;let qdc=q0*q3;let qaa=q1*q1;let qab=q1*q2;
    let qac=q1*q3;let qbb=q2*q2;let qbc=q2*q3;let qcc=q3*q3;
    let rx=vec3<f32>(1.0-qbb-qcc,-qdc+qab,qdb+qac);
    let ry=vec3<f32>(qdc+qab,1.0-qaa-qcc,-qda+qbc);
    let rz=vec3<f32>(-qdb+qac,qda+qbc,1.0-qaa-qbb);
    let sx=vec3<f32>(y.w,z.z,w.y);let sy=vec3<f32>(z.x,z.w,w.z);let sz=vec3<f32>(z.y,w.x,w.w);
    return NativeTransform(vec4<f32>(dot(rx,sx),dot(rx,sy),dot(rx,sz),y.x),
                           vec4<f32>(dot(ry,sx),dot(ry,sy),dot(ry,sz),y.y),
                           vec4<f32>(dot(rz,sx),dot(rz,sy),dot(rz,sz),y.z));
}
fn motion_interpolate(table:u32,first:u32,steps:u32,time:f32)->NativeTransform{
    if(steps<2u||!(time>=0.0&&time<=1.0)){fail();return NativeTransform();}
    let maximum=steps-1u;let step=min(u32(time*f32(maximum)),maximum-1u);let t=time*f32(maximum)-f32(step);
    let a=table_at(table,first+step,MOTION_SIZE);let b=table_at(table,first+step+1u,MOTION_SIZE);
    let rotation=motion_rotation(heap_vec4(a+MOTION_X),heap_vec4(b+MOTION_X),t);
    let translation=motion_add4((1.0-t)*heap_vec4(a+MOTION_Y),t*heap_vec4(b+MOTION_Y));
    let z=motion_add4((1.0-t)*heap_vec4(a+MOTION_Z),t*heap_vec4(b+MOTION_Z));
    let w=motion_add4((1.0-t)*heap_vec4(a+MOTION_W),t*heap_vec4(b+MOTION_W));
    return motion_compose(rotation,translation,z,w);
}
// Native object transform/inverse evaluation. The inverse mirrors the reviewed
// Cycles determinant/adjugate implementation, including its degenerate fallback.
fn motion_point(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    return motion_direction(t,v)+vec3<f32>(t.x.w,t.y.w,t.z.w);
}
fn motion_transpose(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    return rounded_add3(rounded_add3(v.x*t.x.xyz,v.y*t.y.xyz),v.z*t.z.xyz);
}
fn motion_inverse_cross(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{
    return fma(a.yzx,b.zxy,-a.zxy*b.yzx);
}
fn motion_inverse(t:NativeTransform)->NativeTransform{
    var x=vec3<f32>(t.x.x,t.y.x,t.z.x);var y=vec3<f32>(t.x.y,t.y.y,t.z.y);var z=vec3<f32>(t.x.z,t.y.z,t.z.z);
    let w=vec3<f32>(t.x.w,t.y.w,t.z.w);var determinant=rounded_dot(x,motion_inverse_cross(y,z));
    if(determinant==0.0){
        x.x=rounded_add(x.x,1e-8);y.y=rounded_add(y.y,1e-8);z.z=rounded_add(z.z,1e-8);
        determinant=rounded_dot(x,rounded_cross(y,z));if(determinant==0.0){determinant=bitcast<f32>(0x7f7fffffu);}
    }
    let a=motion_inverse_cross(y,z);let b=motion_inverse_cross(z,x);let c=motion_inverse_cross(x,y);
    let ix=vec3<f32>(rounded_divide(a.x,determinant),rounded_divide(a.y,determinant),rounded_divide(a.z,determinant));
    let iy=vec3<f32>(rounded_divide(b.x,determinant),rounded_divide(b.y,determinant),rounded_divide(b.z,determinant));
    let iz=vec3<f32>(rounded_divide(c.x,determinant),rounded_divide(c.y,determinant),rounded_divide(c.z,determinant));
    return NativeTransform(vec4<f32>(ix,-rounded_dot(ix,w)),vec4<f32>(iy,-rounded_dot(iy,w)),vec4<f32>(iz,-rounded_dot(iz,w)));
}
fn motion_object(object:u32,time:f32)->NativeTransform{
    let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));let at=table_at(G_objects,object,SIZE_objects);
    if((flags&SD_OBJECT_MOTION)==0u){return motion_transform_load(at+O_tfm);}
    let first=load_word(at+O_motion_offset);let steps=load_half(at+O_num_tfm_steps);let count=table_count(G_object_motion);
    if(first>count||steps>count-first){fail();return NativeTransform();}
    return motion_interpolate(G_object_motion,first,steps,time);
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Adapted from reviewed Cycles geom/motion_triangle.h and
// geom/motion_triangle_intersect.h. Native arrays retain center-first packing.
struct TriangleVertices {a:vec3<f32>,b:vec3<f32>,c:vec3<f32>,error:u32}
struct TriangleTime {a:u32,b:u32,t:f32,error:u32}
fn native_triangle_kind(kind:u32)->bool{return kind==PRIMITIVE_TRIANGLE||kind==PRIMITIVE_MOTION_TRIANGLE;}
fn triangle_motion_time(object:u32,time:f32)->TriangleTime{
    let at=table_at(G_objects,object,SIZE_objects);let steps=load_half(at+O_num_geom_steps);
    if(steps<2u||!(time>=0.0&&time<=1.0)){fail();return TriangleTime(0u,0u,0.0,1u);}
    let step=min(u32(time*f32(steps-1u)),steps-2u);let t=time*f32(steps-1u)-f32(step);let center=(steps-1u)/2u;
    let a=select(select(step,step+1u,step<center),0u,step==center);
    let b=select(select(step+1u,step+2u,step+1u<center),0u,step+1u==center);
    return TriangleTime(a,b,t,0u);
}
// Signed native offsets may cancel global vertex/corner indices. Bound both
// the signed center index and the unsigned frame stride before adding them.
fn triangle_frame_index(index:u32,offset:i32,frame:u32,stride:u32,count:u32)->u32{
    if(index>0x7fffffffu||(offset>0&&index>0x7fffffffu-u32(offset))){fail();return NONE;}
    let center=bitcast<i32>(index)+offset;
    if(center<0||u32(center)>=count){fail();return NONE;}
    if(stride==0u||frame>(count-1u-u32(center))/stride){fail();return NONE;}
    return u32(center)+frame*stride;
}
fn triangle_position(object:u32,vertex:u32,frame:u32)->vec3<f32>{
    let at=table_at(G_objects,object,SIZE_objects);let stride=load_word(at+O_numverts);
    let index=triangle_frame_index(vertex,bitcast<i32>(load_word(at+O_position_offset)),frame,stride,table_count(G_tri_verts));
    if(index==NONE){return vec3<f32>(0);}
    return heap_vec3(table_at(G_tri_verts,index,SIZE_tri_verts));
}
fn native_triangle_vertices(object:u32,prim:u32,kind:u32,time:f32)->TriangleVertices{
    var result:TriangleVertices;
    if(!native_triangle_kind(kind)||object>=table_count(G_objects)||object>=table_count(G_object_flag)||prim>=table_count(G_tri_vindex)){fail();result.error=1u;return result;}
    let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));let moving=kind==PRIMITIVE_MOTION_TRIANGLE;
    if(moving!=((flags&SD_OBJECT_HAS_VERTEX_MOTION)!=0u)){fail();result.error=1u;return result;}
    let at=table_at(G_tri_vindex,prim,SIZE_tri_vindex);let vi=vec3<u32>(load_word(at),load_word(at+4u),load_word(at+8u));
    var a=0u;var b=0u;var t=0.0;
    if(moving){let mt=triangle_motion_time(object,time);if(mt.error!=0u){result.error=1u;return result;}a=mt.a;b=mt.b;t=mt.t;}
    var vertices:array<vec3<f32>,3>;
    for(var i=0u;i<3u;i++){
        vertices[i]=triangle_position(object,vi[i],a);
        if(moving){vertices[i]=rounded_add3((1.0-t)*vertices[i],t*triangle_position(object,vi[i],b));}
    }
    result.a=vertices[0];result.b=vertices[1];result.c=vertices[2];
    if(load_word(p(17u))!=0u){result.error=1u;}return result;
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2011-2026 Blender Foundation
// SPDX-FileCopyrightText: 2011-2013 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native triangle ShaderData setup, packed normals and compact differentials.
// See native/THIRD-PARTY-NOTICES.txt for source files and transformations.
struct SurfaceData {
    P:vec3<f32>, N:vec3<f32>, Ng:vec3<f32>, wi:vec3<f32>,
    dPdu:vec3<f32>, dPdv:vec3<f32>, du:vec2<f32>, dv:vec2<f32>,
    u:f32, v:f32, time:f32, ray_length:f32, dP:f32, dI:f32,
    object_id:u32, prim:u32, primitive_type:u32, shader:u32,
    runtime_flag:u32, shader_flag:u32, object_flag:u32, error:u32,
    motion_tfm:NativeTransform,motion_itfm:NativeTransform,
}
fn surface_failure(sd:ptr<function,SurfaceData>){(*sd).error=1u;fail();}
fn surface_normalize(v:vec3<f32>)->vec3<f32>{return rounded_safe_normalize(v);}
// Original adapter for Cycles normalize(float3): per-component division (SSE
// hosts) or reciprocal-length multiplication (scalar float3 hosts), selected
// by the generated NATIVE_FLOAT3_RECIPROCAL_NORMALIZE of the originating host.
fn surface_native_normalize(v:vec3<f32>)->vec3<f32>{
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return v*rounded_divide(1.0,rounded_sqrt(rounded_dot(v,v)));}
    return rounded_normalize(v);
}
fn surface_orthogonal(n:vec3<f32>)->vec3<f32>{
    if(n.x!=n.y||n.x!=n.z){return surface_normalize(vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x));}
    return surface_normalize(vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x));
}
fn surface_transform_direction(address:u32,v:vec3<f32>)->vec3<f32>{
    let x=heap_vec4(address);let y=heap_vec4(address+16u);let z=heap_vec4(address+32u);
    return v.x*vec3<f32>(x.x,y.x,z.x)+(v.y*vec3<f32>(x.y,y.y,z.y)+v.z*vec3<f32>(x.z,y.z,z.z));
}
fn surface_transform_point(address:u32,v:vec3<f32>)->vec3<f32>{
    return surface_transform_direction(address,v)+vec3<f32>(heap_float(address+12u),heap_float(address+28u),heap_float(address+44u));
}
fn surface_transform_normal(address:u32,v:vec3<f32>)->vec3<f32>{
    let x=heap_vec3(address);let y=heap_vec3(address+16u);let z=heap_vec3(address+32u);
    return surface_native_normalize(rounded_add3(rounded_add3(v.x*x,v.y*y),v.z*z));
}
fn surface_object_direction(sd:SurfaceData,v:vec3<f32>,inverse:bool)->vec3<f32>{
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){if(inverse){return motion_direction(sd.motion_itfm,v);}return motion_direction(sd.motion_tfm,v);}
    let object=table_at(G_objects,sd.object_id,SIZE_objects);return surface_transform_direction(object+select(O_tfm,O_itfm,inverse),v);
}
fn surface_object_point(sd:SurfaceData,v:vec3<f32>,inverse:bool)->vec3<f32>{
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){if(inverse){return motion_point(sd.motion_itfm,v);}return motion_point(sd.motion_tfm,v);}
    let object=table_at(G_objects,sd.object_id,SIZE_objects);return surface_transform_point(object+select(O_tfm,O_itfm,inverse),v);
}
fn surface_object_normal(sd:SurfaceData,v:vec3<f32>,inverse:bool)->vec3<f32>{
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){if(inverse){return surface_native_normalize(motion_transpose(sd.motion_tfm,v));}return surface_native_normalize(motion_transpose(sd.motion_itfm,v));}
    let object=table_at(G_objects,sd.object_id,SIZE_objects);return surface_transform_normal(object+select(O_itfm,O_tfm,inverse),v);
}
fn surface_decode_normal(value:u32)->vec3<f32>{
    let raw=vec2<f32>(f32(value&65535u),f32(value>>16u))*(2.0/65535.0);
    let xy=vec2<f32>(rounded_add(raw.x,-1.0),rounded_add(raw.y,-1.0));
    let z=rounded_add(rounded_add(1.0,-abs(xy.x)),-abs(xy.y));let t=max(-z,0.0);
    // The packed grid has no exact zero coordinate; sign comparison matches
    // both upstream scalar copysign and the CPU SIMD decoder.
    let shift=select(vec2<f32>(t),vec2<f32>(-t),xy>vec2<f32>(0));
    return surface_normalize(vec3<f32>(rounded_add(xy.x,shift.x),rounded_add(xy.y,shift.y),z));
}
fn surface_differentials(sd:ptr<function,SurfaceData>){
    let n=(*sd).Ng;let tangent=surface_orthogonal(n);
    var dx=tangent*(*sd).dP;var dy=cross(n,tangent)*(*sd).dP;
    var a=(*sd).dPdu;var b=(*sd).dPdv;let axis=abs(n);
    if(axis.z<axis.x||axis.z<axis.y){
        if(axis.y<axis.x||axis.y<axis.z){a.x=a.y;b.x=b.y;dx.x=dx.y;dy.x=dy.y;}
        a.y=a.z;b.y=b.z;dx.y=dx.z;dy.y=dy.z;
    }
    var det=a.x*b.y-b.x*a.y;if(det!=0.0){det=1.0/det;}
    (*sd).du=vec2<f32>((dx.x*b.y-dx.y*b.x)*det,(dy.x*b.y-dy.y*b.x)*det);
    (*sd).dv=vec2<f32>((dx.y*a.x-dx.x*a.y)*det,(dy.y*a.x-dy.x*a.y)*det);
}
fn surface_setup_hit(primitive_type:u32,object_id:u32,prim:u32,u:f32,v:f32,t:f32,time:f32,ray_direction:vec3<f32>,ray_dP:f32,ray_dD:f32)->SurfaceData{
    var sd:SurfaceData;
    sd.primitive_type=primitive_type;sd.object_id=object_id;sd.prim=prim;
    if(!native_triangle_kind(sd.primitive_type)||sd.object_id>=table_count(G_objects)||sd.object_id>=table_count(G_object_flag)||
       sd.prim>=table_count(G_tri_vindex)||sd.prim>=table_count(G_tri_shader)){surface_failure(&sd);return sd;}
    let object=table_at(G_objects,sd.object_id,SIZE_objects);
    sd.object_flag=load_word(table_at(G_object_flag,sd.object_id,4u));
    sd.u=u;sd.v=v;
    sd.ray_length=t;sd.time=time;
    sd.wi=-ray_direction;
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){
//#if MOTION_OBJECT else atomicOr(&heap[p(17u)/4u],2u);surface_failure(&sd);return sd;
        sd.motion_tfm=motion_object(sd.object_id,sd.time);sd.motion_itfm=motion_inverse(sd.motion_tfm);
        if(load_word(p(17u))!=0u){surface_failure(&sd);return sd;}
//#endif
    }
    let vi_address=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);
    let vi=vec3<u32>(load_word(vi_address),load_word(vi_address+4u),load_word(vi_address+8u));
    let vertices=native_triangle_vertices(sd.object_id,sd.prim,sd.primitive_type,sd.time);
    if(vertices.error!=0u){surface_failure(&sd);return sd;}let a=vertices.a;let b=vertices.b;let c=vertices.c;
    sd.dPdu=b-a;sd.dPdv=c-a;sd.P=(a+sd.u*sd.dPdu)+sd.v*sd.dPdv;
    let applied=(sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
    if(!applied){sd.P=surface_object_point(sd,sd.P,false);}
    var cross_normal=rounded_cross(sd.dPdu,sd.dPdv);
    if(applied&&(sd.object_flag&SD_OBJECT_NEGATIVE_SCALE)!=0u){cross_normal=rounded_cross(sd.dPdv,sd.dPdu);}
    if(dot(cross_normal,cross_normal)==0.0){surface_failure(&sd);return sd;}
    sd.Ng=surface_native_normalize(cross_normal);sd.N=sd.Ng;
    sd.shader=load_word(table_at(G_tri_shader,sd.prim,4u));let shader_id=sd.shader&SHADER_MASK;
    if(shader_id>=table_count(G_shaders)){surface_failure(&sd);return sd;}
    if((sd.shader&SHADER_SMOOTH_NORMAL)!=0u){
        var ni=vi;
        if((sd.object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0u){ni=vec3<u32>(sd.prim*3u)+vec3<u32>(0,1,2);}
        // Native offsets may be negative: global triangle/vertex indices cancel
        // that offset. Check signed addition before turning it into an address.
        let offset=bitcast<i32>(load_word(object+O_normal_offset));
        let nc=table_count(G_attributes_normal);var normals:array<vec3<f32>,3>;
        var frame:TriangleTime;var stride=load_word(object+O_numverts);
        if((sd.object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0u){stride=3u*load_word(object+O_numprims);}
        if(sd.primitive_type==PRIMITIVE_MOTION_TRIANGLE){frame=triangle_motion_time(sd.object_id,sd.time);}
        for(var j=0u;j<3u;j++){
            let at=triangle_frame_index(ni[j],offset,frame.a,stride,nc);
            if(at==NONE){surface_failure(&sd);return sd;}
            normals[j]=surface_decode_normal(load_word(table_at(G_attributes_normal,at,SIZE_attributes_normal)));
            if(sd.primitive_type==PRIMITIVE_MOTION_TRIANGLE){
                let next=triangle_frame_index(ni[j],offset,frame.b,stride,nc);if(next==NONE){surface_failure(&sd);return sd;}
                let n=surface_decode_normal(load_word(table_at(G_attributes_normal,next,SIZE_attributes_normal)));
                normals[j]=surface_native_normalize(rounded_add3((1.0-frame.t)*normals[j],frame.t*n));
            }
        }
        let weight=rounded_add(rounded_add(1.0,-sd.u),-sd.v);
        let interpolated=surface_normalize(rounded_add3(rounded_add3(weight*normals[0],sd.u*normals[1]),sd.v*normals[2]));
        if(any(interpolated!=vec3<f32>(0))){sd.N=interpolated;}
    }
    if(!applied){
        sd.N=surface_object_normal(sd,sd.N,false);sd.Ng=surface_object_normal(sd,sd.Ng,false);
        sd.dPdu=surface_object_direction(sd,sd.dPdu,false);sd.dPdv=surface_object_direction(sd,sd.dPdv,false);
    }
    sd.shader_flag=load_word(table_at(G_shaders,shader_id,SIZE_shaders)+SH_flags);
    if(dot(sd.Ng,sd.wi)<0.0){sd.runtime_flag|=SR_BACKFACING;sd.N=-sd.N;sd.Ng=-sd.Ng;sd.dPdu=-sd.dPdu;sd.dPdv=-sd.dPdv;}
    sd.dI=ray_dD;
    sd.dP=ray_dP+sd.ray_length*sd.dI;surface_differentials(&sd);
    return sd;
}

fn surface_from_ray(index:u32)->SurfaceData{
    if(index>=p(10u)){var sd:SurfaceData;surface_failure(&sd);return sd;}
    return surface_setup_hit(state_word(S_isect_type,index),state_word(S_isect_object,index),state_word(S_isect_prim,index),
      heap_float(state_address(S_isect_u,index,4u)),heap_float(state_address(S_isect_v,index,4u)),heap_float(state_address(S_isect_t,index,4u)),
      heap_float(state_address(S_ray_time,index,4u)),heap_vec3(state_address(S_ray_D,index,12u)),
      heap_float(state_address(S_ray_dP,index,4u)),heap_float(state_address(S_ray_dD,index,4u)));
}
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Scalar GPU-only cache separates geometric setup from SVM compilation.
// p(26) is the first record and p(27) is its stride in bytes.
fn surface_data_write(at:u32,sd:SurfaceData){
store_word(at+0u,bitcast<u32>(sd.P.x));
store_word(at+4u,bitcast<u32>(sd.P.y));
store_word(at+8u,bitcast<u32>(sd.P.z));
store_word(at+12u,bitcast<u32>(sd.N.x));
store_word(at+16u,bitcast<u32>(sd.N.y));
store_word(at+20u,bitcast<u32>(sd.N.z));
store_word(at+24u,bitcast<u32>(sd.Ng.x));
store_word(at+28u,bitcast<u32>(sd.Ng.y));
store_word(at+32u,bitcast<u32>(sd.Ng.z));
store_word(at+36u,bitcast<u32>(sd.wi.x));
store_word(at+40u,bitcast<u32>(sd.wi.y));
store_word(at+44u,bitcast<u32>(sd.wi.z));
store_word(at+48u,bitcast<u32>(sd.dPdu.x));
store_word(at+52u,bitcast<u32>(sd.dPdu.y));
store_word(at+56u,bitcast<u32>(sd.dPdu.z));
store_word(at+60u,bitcast<u32>(sd.dPdv.x));
store_word(at+64u,bitcast<u32>(sd.dPdv.y));
store_word(at+68u,bitcast<u32>(sd.dPdv.z));
store_word(at+72u,bitcast<u32>(sd.du.x));
store_word(at+76u,bitcast<u32>(sd.du.y));
store_word(at+80u,bitcast<u32>(sd.dv.x));
store_word(at+84u,bitcast<u32>(sd.dv.y));
store_word(at+88u,bitcast<u32>(sd.u));
store_word(at+92u,bitcast<u32>(sd.v));
store_word(at+96u,bitcast<u32>(sd.time));
store_word(at+100u,bitcast<u32>(sd.ray_length));
store_word(at+104u,bitcast<u32>(sd.dP));
store_word(at+108u,bitcast<u32>(sd.dI));
store_word(at+112u,sd.object_id);
store_word(at+116u,sd.prim);
store_word(at+120u,sd.primitive_type);
store_word(at+124u,sd.shader);
store_word(at+128u,sd.runtime_flag);
store_word(at+132u,sd.shader_flag);
store_word(at+136u,sd.object_flag);
store_word(at+140u,sd.error);
store_word(at+144u,bitcast<u32>(sd.motion_tfm.x.x));
store_word(at+148u,bitcast<u32>(sd.motion_tfm.x.y));
store_word(at+152u,bitcast<u32>(sd.motion_tfm.x.z));
store_word(at+156u,bitcast<u32>(sd.motion_tfm.x.w));
store_word(at+160u,bitcast<u32>(sd.motion_tfm.y.x));
store_word(at+164u,bitcast<u32>(sd.motion_tfm.y.y));
store_word(at+168u,bitcast<u32>(sd.motion_tfm.y.z));
store_word(at+172u,bitcast<u32>(sd.motion_tfm.y.w));
store_word(at+176u,bitcast<u32>(sd.motion_tfm.z.x));
store_word(at+180u,bitcast<u32>(sd.motion_tfm.z.y));
store_word(at+184u,bitcast<u32>(sd.motion_tfm.z.z));
store_word(at+188u,bitcast<u32>(sd.motion_tfm.z.w));
store_word(at+192u,bitcast<u32>(sd.motion_itfm.x.x));
store_word(at+196u,bitcast<u32>(sd.motion_itfm.x.y));
store_word(at+200u,bitcast<u32>(sd.motion_itfm.x.z));
store_word(at+204u,bitcast<u32>(sd.motion_itfm.x.w));
store_word(at+208u,bitcast<u32>(sd.motion_itfm.y.x));
store_word(at+212u,bitcast<u32>(sd.motion_itfm.y.y));
store_word(at+216u,bitcast<u32>(sd.motion_itfm.y.z));
store_word(at+220u,bitcast<u32>(sd.motion_itfm.y.w));
store_word(at+224u,bitcast<u32>(sd.motion_itfm.z.x));
store_word(at+228u,bitcast<u32>(sd.motion_itfm.z.y));
store_word(at+232u,bitcast<u32>(sd.motion_itfm.z.z));
store_word(at+236u,bitcast<u32>(sd.motion_itfm.z.w));
}
fn surface_data_read(at:u32)->SurfaceData{var sd:SurfaceData;
sd.P.x=bitcast<f32>(load_word(at+0u));
sd.P.y=bitcast<f32>(load_word(at+4u));
sd.P.z=bitcast<f32>(load_word(at+8u));
sd.N.x=bitcast<f32>(load_word(at+12u));
sd.N.y=bitcast<f32>(load_word(at+16u));
sd.N.z=bitcast<f32>(load_word(at+20u));
sd.Ng.x=bitcast<f32>(load_word(at+24u));
sd.Ng.y=bitcast<f32>(load_word(at+28u));
sd.Ng.z=bitcast<f32>(load_word(at+32u));
sd.wi.x=bitcast<f32>(load_word(at+36u));
sd.wi.y=bitcast<f32>(load_word(at+40u));
sd.wi.z=bitcast<f32>(load_word(at+44u));
sd.dPdu.x=bitcast<f32>(load_word(at+48u));
sd.dPdu.y=bitcast<f32>(load_word(at+52u));
sd.dPdu.z=bitcast<f32>(load_word(at+56u));
sd.dPdv.x=bitcast<f32>(load_word(at+60u));
sd.dPdv.y=bitcast<f32>(load_word(at+64u));
sd.dPdv.z=bitcast<f32>(load_word(at+68u));
sd.du.x=bitcast<f32>(load_word(at+72u));
sd.du.y=bitcast<f32>(load_word(at+76u));
sd.dv.x=bitcast<f32>(load_word(at+80u));
sd.dv.y=bitcast<f32>(load_word(at+84u));
sd.u=bitcast<f32>(load_word(at+88u));
sd.v=bitcast<f32>(load_word(at+92u));
sd.time=bitcast<f32>(load_word(at+96u));
sd.ray_length=bitcast<f32>(load_word(at+100u));
sd.dP=bitcast<f32>(load_word(at+104u));
sd.dI=bitcast<f32>(load_word(at+108u));
sd.object_id=load_word(at+112u);
sd.prim=load_word(at+116u);
sd.primitive_type=load_word(at+120u);
sd.shader=load_word(at+124u);
sd.runtime_flag=load_word(at+128u);
sd.shader_flag=load_word(at+132u);
sd.object_flag=load_word(at+136u);
sd.error=load_word(at+140u);
sd.motion_tfm.x.x=bitcast<f32>(load_word(at+144u));
sd.motion_tfm.x.y=bitcast<f32>(load_word(at+148u));
sd.motion_tfm.x.z=bitcast<f32>(load_word(at+152u));
sd.motion_tfm.x.w=bitcast<f32>(load_word(at+156u));
sd.motion_tfm.y.x=bitcast<f32>(load_word(at+160u));
sd.motion_tfm.y.y=bitcast<f32>(load_word(at+164u));
sd.motion_tfm.y.z=bitcast<f32>(load_word(at+168u));
sd.motion_tfm.y.w=bitcast<f32>(load_word(at+172u));
sd.motion_tfm.z.x=bitcast<f32>(load_word(at+176u));
sd.motion_tfm.z.y=bitcast<f32>(load_word(at+180u));
sd.motion_tfm.z.z=bitcast<f32>(load_word(at+184u));
sd.motion_tfm.z.w=bitcast<f32>(load_word(at+188u));
sd.motion_itfm.x.x=bitcast<f32>(load_word(at+192u));
sd.motion_itfm.x.y=bitcast<f32>(load_word(at+196u));
sd.motion_itfm.x.z=bitcast<f32>(load_word(at+200u));
sd.motion_itfm.x.w=bitcast<f32>(load_word(at+204u));
sd.motion_itfm.y.x=bitcast<f32>(load_word(at+208u));
sd.motion_itfm.y.y=bitcast<f32>(load_word(at+212u));
sd.motion_itfm.y.z=bitcast<f32>(load_word(at+216u));
sd.motion_itfm.y.w=bitcast<f32>(load_word(at+220u));
sd.motion_itfm.z.x=bitcast<f32>(load_word(at+224u));
sd.motion_itfm.z.y=bitcast<f32>(load_word(at+228u));
sd.motion_itfm.z.z=bitcast<f32>(load_word(at+232u));
sd.motion_itfm.z.w=bitcast<f32>(load_word(at+236u));
return sd;
}
fn surface_cache_address(slot:u32)->u32{
    let base=p(26u);let stride=p(27u);let bytes=arrayLength(&heap)*4u;
    if(base==0u||base%4u!=0u||stride%4u!=0u||stride<SURFACE_DATA_CACHE_BYTES||base>bytes||SURFACE_DATA_CACHE_BYTES>bytes-base){fail();return 0u;}
    if(slot>(bytes-base-SURFACE_DATA_CACHE_BYTES)/stride){fail();return 0u;}
    return base+slot*stride;
}
fn surface_cached(slot:u32)->SurfaceData{
    let at=surface_cache_address(slot);if(at==0u){var sd:SurfaceData;sd.error=1u;return sd;}
    return surface_data_read(at);
}
@compute @workgroup_size(64) fn prepare_shader_data(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}let at=surface_cache_address(id.x);if(at==0u){return;}
    var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    surface_data_write(at,surface_from_ray(index));
}
// Generated from Blender Cycles; see provenance.json and THIRD-PARTY-NOTICES.txt.
// Original code: SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-License-Identifier: Apache-2.0
// wrapf retains upstream's attribution: Adapted from godot-engine math_funcs.h.
// C library adapters below: Copyright (c) 2026 Jeremy Ellis and contributors
fn c_asin_series(x: f32) -> f32 {
    // Taylor coefficients binomial(2n,n)/(4^n*(2n+1)), n=0..11.
    // Half-angle reduction below keeps |x| <= 0.5; omitted tail < 4e-10.
    let z = x*x;
    var term = x;
    var sum = x;
    for (var n=1u; n<12u; n++) {
        let k = f32(n);
        term *= z*((2.0*k-1.0)*(2.0*k-1.0))/((2.0*k)*(2.0*k+1.0));
        sum += term;
    }
    return sum;
}
fn c_asin(x: f32) -> f32 {
    let a = abs(x);
    if (a <= 0.5) { return c_asin_series(x); }
    let result = 1.5707963267948966 - 2.0*c_asin_series(sqrt((1.0-a)*0.5));
    return select(result, -result, x < 0.0);
}
fn c_acos(x: f32) -> f32 {
    if (x >= 0.5) { return 2.0*c_asin_series(sqrt((1.0-x)*0.5)); }
    if (x <= -0.5) { return 3.141592653589793 - 2.0*c_asin_series(sqrt((1.0+x)*0.5)); }
    return 1.5707963267948966-c_asin_series(x);
}
fn c_fmod(a: f32, b: f32) -> f32 { return a - trunc(a / b) * b; }
fn c_pow(a: f32, b: f32) -> f32 {
    // C pow accepts negative bases with integer exponents; WGSL pow does not.
    if (a < 0.0) {
        let p = pow(-a, b);
        if (c_fmod(abs(b), 2.0) == 0.0) { return p; }
        return -p;
    }
    return pow(a, b);
}


fn c_ray_triangle_reciprocal(in_x: f32) -> f32 {
var v_x = in_x;
return (1.0f / v_x);
}

fn c_ray_triangle_dot_scalar(in_a: vec3<f32>, in_b: vec3<f32>) -> f32 {
var v_a = in_a;
var v_b = in_b;
return (((v_a.x * v_b.x) + (v_a.y * v_b.y)) + (v_a.z * v_b.z));
}

fn c_ray_triangle_cross(in_a: vec3<f32>, in_b: vec3<f32>) -> vec3<f32> {
var v_a = in_a;
var v_b = in_b;
return vec3<f32>(((v_a.y * v_b.z) - (v_a.z * v_b.y)), ((v_a.z * v_b.x) - (v_a.x * v_b.z)), ((v_a.x * v_b.y) - (v_a.y * v_b.x)));
}

fn c_ray_triangle_intersect(in_ray_P: vec3<f32>, in_ray_D: vec3<f32>, in_ray_tmin: f32, in_ray_tmax: f32, in_tri_a: vec3<f32>, in_tri_b: vec3<f32>, in_tri_c: vec3<f32>, v_isect_u: ptr<function, f32>, v_isect_v: ptr<function, f32>, v_isect_t: ptr<function, f32>) -> bool {
var v_ray_P = in_ray_P;
var v_ray_D = in_ray_D;
var v_ray_tmin = in_ray_tmin;
var v_ray_tmax = in_ray_tmax;
var v_tri_a = in_tri_a;
var v_tri_b = in_tri_b;
var v_tri_c = in_tri_c;
var v_v0: vec3<f32> = (v_tri_a - v_ray_P);
var v_v1: vec3<f32> = (v_tri_b - v_ray_P);
var v_v2: vec3<f32> = (v_tri_c - v_ray_P);
var v_e0: vec3<f32> = (v_v2 - v_v0);
var v_e1: vec3<f32> = (v_v0 - v_v1);
var v_e2: vec3<f32> = (v_v1 - v_v2);
var v_U: f32 = c_ray_triangle_dot(c_ray_triangle_cross(v_e0, (v_v2 + v_v0)), v_ray_D);
var v_V: f32 = c_ray_triangle_dot(c_ray_triangle_cross(v_e1, (v_v0 + v_v1)), v_ray_D);
var v_W: f32 = c_ray_triangle_dot(c_ray_triangle_cross(v_e2, (v_v1 + v_v2)), v_ray_D);
var v_UVW: f32 = ((v_U + v_V) + v_W);
var v_eps: f32 = (1.1920929E-7f * abs(v_UVW));
var v_minUVW: f32 = min(v_U, min(v_V, v_W));
var v_maxUVW: f32 = max(v_U, max(v_V, v_W));
if ((!(((v_minUVW >= (-v_eps)) || (v_maxUVW <= v_eps))))) {
return false;
}
var v_Ng1: vec3<f32> = c_ray_triangle_cross(v_e1, v_e0);
var v_Ng: vec3<f32> = (v_Ng1 + v_Ng1);
var v_den: f32 = dot(v_Ng, v_ray_D);
if (((v_den == 0.0f))) {
return false;
}
var v_T: f32 = dot(v_v0, v_Ng);
var v_t: f32 = rounded_divide(v_T, v_den);
if ((!(((v_t >= v_ray_tmin) && (v_t <= v_ray_tmax))))) {
return false;
}
var choice_1: f32;
if (((abs(v_UVW) < 1.00000005E-18f))) {
choice_1 = 0.0f;
} else {
choice_1 = c_ray_triangle_reciprocal(v_UVW);
}
var v_rcp_uvw: f32 = choice_1;
(*v_isect_u) = min((v_U * v_rcp_uvw), 1.0f);
(*v_isect_v) = min((v_V * v_rcp_uvw), 1.0f);
(*v_isect_t) = v_t;
return true;
}

fn c_fractf(in_x: f32) -> f32 {
var v_x = in_x;
return (v_x - floor(v_x));
}

fn c_wrapf(in_value: f32, in_max: f32, in_min: f32) -> f32 {
var v_value = in_value;
var v_max = in_max;
var v_min = in_min;
var v_range: f32 = (v_max - v_min);
var choice_2: f32;
if (((v_range != 0.0f))) {
choice_2 = (v_value - ((v_range * floor((((v_value - v_min)) / v_range)))));
} else {
choice_2 = v_min;
}
return choice_2;
}

fn c_pingpongf(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var choice_3: f32;
if (((v_b != 0.0f))) {
choice_3 = abs((((c_fractf((((v_a - v_b)) / ((v_b * 2.0f)))) * v_b) * 2.0f) - v_b));
} else {
choice_3 = 0.0f;
}
return choice_3;
}

fn c_smoothminf(in_a: f32, in_b: f32, in_k: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var v_k = in_k;
if ((v_k != 0.0f)) {
var v_h: f32 = (max((v_k - abs((v_a - v_b))), 0.0f) / v_k);
return (min(v_a, v_b) - ((((v_h * v_h) * v_h) * v_k) * ((1.0f / 6.0f))));
}
return min(v_a, v_b);
}

fn c_signf(in_f: f32) -> f32 {
var v_f = in_f;
var choice_4: f32;
if (((v_f < 0.0f))) {
choice_4 = (-1.0f);
} else {
choice_4 = 1.0f;
}
return choice_4;
}

fn c_compatible_atan2(in_y: f32, in_x: f32) -> f32 {
var v_y = in_y;
var v_x = in_x;
var choice_5: f32;
if ((((v_x == 0.0f) && (v_y == 0.0f)))) {
choice_5 = 0.0f;
} else {
choice_5 = atan2(v_y, v_x);
}
return choice_5;
}

fn c_compatible_signf(in_f: f32) -> f32 {
var v_f = in_f;
if ((v_f == 0.0f)) {
return 0.0f;
}
return c_signf(v_f);
}

fn c_safe_sqrtf(in_f: f32) -> f32 {
var v_f = in_f;
return sqrt(max(v_f, 0.0f));
}

fn c_inversesqrtf(in_f: f32) -> f32 {
var v_f = in_f;
var choice_6: f32;
if (((v_f > 0.0f))) {
choice_6 = (1.0f / sqrt(v_f));
} else {
choice_6 = 0.0f;
}
return choice_6;
}

fn c_safe_asinf(in_a: f32) -> f32 {
var v_a = in_a;
return c_asin(clamp(v_a, (-1.0f), 1.0f));
}

fn c_safe_acosf(in_a: f32) -> f32 {
var v_a = in_a;
return c_acos(clamp(v_a, (-1.0f), 1.0f));
}

fn c_compatible_powf(in_x: f32, in_y: f32) -> f32 {
var v_x = in_x;
var v_y = in_y;
if ((v_y == 0.0f)) {
return 1.0f;
}
if ((v_x == 0.0f)) {
return 0.0f;
}
return c_pow(v_x, v_y);
}

fn c_safe_powf(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
if ((((v_a < 0.0f) && (v_b != f32(i32(v_b)))))) {
return 0.0f;
}
return c_compatible_powf(v_a, v_b);
}

fn c_safe_divide(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var choice_7: f32;
if (((v_b != 0.0f))) {
choice_7 = (v_a / v_b);
} else {
choice_7 = 0.0f;
}
return choice_7;
}

fn c_safe_logf(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
if ((((v_a <= 0.0f) || (v_b <= 0.0f)))) {
return 0.0f;
}
return c_safe_divide(log(v_a), log(v_b));
}

fn c_safe_modulo(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var choice_8: f32;
if (((v_b != 0.0f))) {
choice_8 = c_fmod(v_a, v_b);
} else {
choice_8 = 0.0f;
}
return choice_8;
}

fn c_safe_floored_modulo(in_a: f32, in_b: f32) -> f32 {
var v_a = in_a;
var v_b = in_b;
var choice_9: f32;
if (((v_b != 0.0f))) {
choice_9 = (v_a - (floor((v_a / v_b)) * v_b));
} else {
choice_9 = 0.0f;
}
return choice_9;
}

fn c_svm_math(in_type: u32, in_a: f32, in_b: f32, in_c: f32) -> f32 {
var v_type = in_type;
var v_a = in_a;
var v_b = in_b;
var v_c = in_c;
switch (v_type) {
case 0u: {
return (v_a + v_b);
}
case 1u: {
return (v_a - v_b);
}
case 2u: {
return (v_a * v_b);
}
case 3u: {
return c_safe_divide(v_a, v_b);
}
case 10u: {
return c_safe_powf(v_a, v_b);
}
case 11u: {
return c_safe_logf(v_a, v_b);
}
case 23u: {
return c_safe_sqrtf(v_a);
}
case 24u: {
return c_inversesqrtf(v_a);
}
case 18u: {
return abs(v_a);
}
case 27u: {
return (v_a * (((3.14159274f) / 180.0f)));
}
case 28u: {
return (v_a * ((180.0f / (3.14159274f))));
}
case 12u: {
return min(v_a, v_b);
}
case 13u: {
return max(v_a, v_b);
}
case 15u: {
return select(f32(0), f32(1), (v_a < v_b));
}
case 16u: {
return select(f32(0), f32(1), (v_a > v_b));
}
case 14u: {
return floor((v_a + 0.5f));
}
case 20u: {
return floor(v_a);
}
case 21u: {
return ceil(v_a);
}
case 22u: {
return (v_a - floor(v_a));
}
case 17u: {
return c_safe_modulo(v_a, v_b);
}
case 40u: {
return c_safe_floored_modulo(v_a, v_b);
}
case 32u: {
var choice_10: f32;
if ((v_a >= 0.0f)) {
choice_10 = floor(v_a);
} else {
choice_10 = ceil(v_a);
}
return choice_10;
}
case 33u: {
return (floor(c_safe_divide(v_a, v_b)) * v_b);
}
case 34u: {
return c_wrapf(v_a, v_b, v_c);
}
case 37u: {
return c_pingpongf(v_a, v_b);
}
case 4u: {
return sin(v_a);
}
case 5u: {
return cos(v_a);
}
case 6u: {
return tan(v_a);
}
case 29u: {
return sinh(v_a);
}
case 30u: {
return cosh(v_a);
}
case 31u: {
return tanh(v_a);
}
case 7u: {
return c_safe_asinf(v_a);
}
case 8u: {
return c_safe_acosf(v_a);
}
case 9u: {
return atan(v_a);
}
case 19u: {
return c_compatible_atan2(v_a, v_b);
}
case 25u: {
return c_compatible_signf(v_a);
}
case 26u: {
return exp(v_a);
}
case 35u: {
var choice_11: f32;
if (((((v_a == v_b)) || ((abs((v_a - v_b)) <= max(v_c, 1.1920929E-7f)))))) {
choice_11 = 1.0f;
} else {
choice_11 = 0.0f;
}
return choice_11;
}
case 36u: {
return ((v_a * v_b) + v_c);
}
case 38u: {
return c_smoothminf(v_a, v_b, v_c);
}
case 39u: {
return (-c_smoothminf((-v_a), (-v_b), v_c));
}
default: {
return 0.0f;
}
}
}

fn c_svm_math_gamma_color(in_color: vec3<f32>, in_gamma: f32) -> vec3<f32> {
var v_color = in_color;
var v_gamma = in_gamma;
if ((v_gamma == 0.0f)) {
return vec3<f32>(1.0f, 1.0f, 1.0f);
}
if ((v_color.x > 0.0f)) {
v_color.x = c_pow(v_color.x, v_gamma);
}
if ((v_color.y > 0.0f)) {
v_color.y = c_pow(v_color.y, v_gamma);
}
if ((v_color.z > 0.0f)) {
v_color.z = c_pow(v_color.z, v_gamma);
}
return v_color;
}
// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native comparison uses Cycles' SSE4.2 CPU oracle. Its ray_triangle_dot
// groups y/z before x, unlike the generic scalar AST branch. This adapter
// preserves that grouping; it does not emulate SSE reciprocal approximation.
fn c_ray_triangle_dot(a:vec3<f32>,b:vec3<f32>)->f32{
    return a.x*b.x+(a.y*b.y+a.z*b.z);
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from Cycles: native SVM closure allocation/setup ported to WGSL.
// Source lineage and BSD notice are in
// native/THIRD-PARTY-NOTICES.txt and parity/licenses/BSD-3-Clause.txt.
struct SvmClosure {
    weight:vec3<f32>, sample_weight:f32, N:vec3<f32>, kind:u32,
    roughness:f32, a:f32, b:f32, multiscatter:vec3<f32>,
    alpha_x:f32, alpha_y:f32, ior:f32, energy_scale:f32, T:vec3<f32>,
    fresnel_f0:f32,reflection_tint:vec3<f32>,transmission_tint:vec3<f32>,
}
struct SvmResult {
    closure_base:u32, count:u32, remaining:u32,
    runtime_flag:u32, error:u32, lcg_state:u32, N:vec3<f32>, emission:vec3<f32>, transparency:vec3<f32>,
}
// Per-path closure storage stays in the arena, avoiding large private arrays
// that exceed register/spill limits on mobile GPU compilers. Native packed stride.
fn svm_closure_read(r:SvmResult,index:u32)->SvmClosure{
    var c:SvmClosure;if(index>=r.count){return c;}let base=r.closure_base+index*SVM_CLOSURE_BYTES;
    c.weight=heap_vec3(base);c.sample_weight=heap_float(base+12u);c.N=heap_vec3(base+16u);c.kind=load_word(base+28u);
    c.roughness=heap_float(base+32u);c.a=heap_float(base+36u);c.b=heap_float(base+40u);c.multiscatter=heap_vec3(base+44u);
    c.alpha_x=heap_float(base+C_alpha_x);c.alpha_y=heap_float(base+C_alpha_y);c.ior=heap_float(base+C_ior);c.energy_scale=heap_float(base+C_energy_scale);c.T=heap_vec3(base+C_T);c.fresnel_f0=heap_float(base+C_fresnel_f0);c.reflection_tint=heap_vec3(base+C_reflection_tint);c.transmission_tint=heap_vec3(base+C_transmission_tint);return c;
}
fn svm_closure_write(base:u32,c:SvmClosure){
    store_word(base,bitcast<u32>(c.weight.x));store_word(base+4u,bitcast<u32>(c.weight.y));store_word(base+8u,bitcast<u32>(c.weight.z));
    store_word(base+12u,bitcast<u32>(c.sample_weight));store_word(base+16u,bitcast<u32>(c.N.x));store_word(base+20u,bitcast<u32>(c.N.y));store_word(base+24u,bitcast<u32>(c.N.z));store_word(base+28u,c.kind);
    store_word(base+32u,bitcast<u32>(c.roughness));store_word(base+36u,bitcast<u32>(c.a));store_word(base+40u,bitcast<u32>(c.b));
    store_word(base+44u,bitcast<u32>(c.multiscatter.x));store_word(base+48u,bitcast<u32>(c.multiscatter.y));store_word(base+52u,bitcast<u32>(c.multiscatter.z));
    store_word(base+C_alpha_x,bitcast<u32>(c.alpha_x));store_word(base+C_alpha_y,bitcast<u32>(c.alpha_y));store_word(base+C_ior,bitcast<u32>(c.ior));store_word(base+C_energy_scale,bitcast<u32>(c.energy_scale));
    store_word(base+C_fresnel_f0,bitcast<u32>(c.fresnel_f0));
    for(var k=0u;k<3u;k++){store_word(base+C_reflection_tint+k*4u,bitcast<u32>(c.reflection_tint[k]));store_word(base+C_transmission_tint+k*4u,bitcast<u32>(c.transmission_tint[k]));}
    store_word(base+C_T,bitcast<u32>(c.T.x));store_word(base+C_T+4u,bitcast<u32>(c.T.y));store_word(base+C_T+8u,bitcast<u32>(c.T.z));
}
fn svm_lcg(index:u32)->u32{
    // Cycles hash_uint3 / Jenkins finalizer, used by lcg_state_init.
    var a=0xdeadbf08u+(state_word(S_path_rng_pixel,index)^0xb4bc3953u);
    var b=0xdeadbf08u+state_half(S_path_rng_offset,index);var c=0xdeadbf08u+state_word(S_path_sample,index);
    c^=b;c-=(b<<14u)|(b>>18u);a^=c;a-=(c<<11u)|(c>>21u);
    b^=a;b-=(a<<25u)|(a>>7u);c^=b;c-=(b<<16u)|(b>>16u);
    a^=c;a-=(c<<4u)|(c>>28u);b^=a;b-=(a<<14u)|(a>>18u);c^=b;c-=(b<<24u)|(b>>8u);return c;
}
fn svm_allocate(s:ptr<function,SvmState>,kind:u32,weight:vec3<f32>)->u32{
    if((*s).error!=0u||(*s).output.remaining==0u){return NONE;}
    let index=(*s).output.count;
    if(index>=SVM_CLOSURE_CAPACITY){svm_error(s,16u);return NONE;}
    if((*s).output.closure_base==0u){svm_error(s,1u);return NONE;}
    var c:SvmClosure;c.kind=kind;c.weight=weight;svm_closure_write((*s).output.closure_base+index*SVM_CLOSURE_BYTES,c);
    (*s).output.count++;(*s).output.remaining--;return index;
}
// Original CodeBrix arithmetic adapter: the pinned ensure_valid_specular_reflection
// formula with separately rounded binary32 products, sums, square roots and
// divisions (no driver contraction or approximate builtins). The adjusted
// normal feeds sharp GGX lobes, where one ulp changes the sampled density.
fn svm_valid_normal(ng:vec3<f32>,wi:vec3<f32>,n:vec3<f32>)->vec3<f32>{
    let reflection=rounded_add3((2.0*rounded_dot(n,wi))*n,-wi);let iz=rounded_dot(wi,ng);let threshold=min(0.9*iz,0.01);
    if(rounded_dot(ng,reflection)>=threshold){return n;}
    var x=rounded_add3(n,-(rounded_dot(n,ng)*ng));let magnitude=rounded_sqrt(rounded_dot(x,x));
    if(magnitude!=0.0){x=x*rounded_divide(1.0,magnitude);}else{x=n;}
    let ix=rounded_dot(wi,x);let a=rounded_add(ix*ix,iz*iz);let b=2.0*rounded_add(a,iz*threshold);let tz=rounded_add(threshold,iz);let c=tz*tz;
    let root=rounded_sqrt(max(rounded_add(b*b,-((4.0*a)*c)),0.0));
    let nz2=rounded_divide(0.25*rounded_add(b,select(-root,root,ix<0.0)),a);
    return rounded_add3(rounded_sqrt(max(rounded_add(1.0,-nz2),0.0))*x,rounded_sqrt(max(nz2,0.0))*ng);
}
// Original CodeBrix arithmetic adapter. Add already-rounded binary32 products
// without a driver contracting the multiply/add before 1-E cancellation.
// Oren's two calls have positive normal a, |b| < a, and a normal positive sum.
// Align significands with three guard bits and round ties to an even mantissa.
fn svm_oren_add(a:f32,b:f32)->f32{
    let ax=bitcast<u32>(a);let bx=bitcast<u32>(b);
    if((bx&0x7fffffffu)==0u){return a;}
    var exponent=(ax>>23u)&255u;let delta=exponent-((bx>>23u)&255u);
    let ma=((ax&0x7fffffu)|0x800000u)<<3u;
    var mb=((bx&0x7fffffu)|0x800000u)<<3u;
    if(delta>=31u){mb=1u;}
    else if(delta>0u){mb=(mb>>delta)|select(0u,1u,(mb&((1u<<delta)-1u))!=0u);}
    var sum=ma+mb;if((bx&0x80000000u)!=0u){sum=ma-mb;}
    if(sum>=0x8000000u){sum=(sum>>1u)|(sum&1u);exponent++;}
    else{while(sum<0x4000000u){sum<<=1u;exponent--;}}
    let tail=sum&7u;var mantissa=sum>>3u;
    if(tail>4u||(tail==4u&&(mantissa&1u)!=0u)){mantissa++;}
    if(mantissa>=0x1000000u){mantissa>>=1u;exponent++;}
    return bitcast<f32>((exponent<<23u)|(mantissa&0x7fffffu));
}
fn svm_oren_g(cosine:f32)->f32{
    if(cosine<1e-6){return (1.5707963267948966f-2.0f/3.0f)-cosine;}
    let sine=sqrt(max(1.0-cosine*cosine,0.0));let theta=c_safe_acosf(cosine);
    return sine*(theta-2.0f/3.0f-sine*cosine)+(2.0f/3.0f)*(sine/cosine)*(1.0-sine*sine*sine);
}
fn svm_transparent(s:ptr<function,SvmState>,weight:vec3<f32>,flag:u32){
    let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));if(!(sample_weight>=1e-5)){return;}
    (*s).output.transparency+=weight;
    if(((*s).output.runtime_flag&SR_TRANSPARENT)!=0u){
        for(var i=0u;i<(*s).output.count;i++){var c=svm_closure_read((*s).output,i);if(c.kind==CLOSURE_BSDF_TRANSPARENT_ID){
            c.weight+=weight;c.sample_weight+=sample_weight;svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);break;}}
    }else{
        (*s).output.runtime_flag|=SR_BSDF|SR_TRANSPARENT;
        if((flag&PATH_RAY_TERMINATE)!=0u){(*s).output.remaining=1u;}
        let i=svm_allocate(s,CLOSURE_BSDF_TRANSPARENT_ID,weight);
        if(i!=NONE){var c=svm_closure_read((*s).output,i);c.sample_weight=sample_weight;c.N=(*s).geometry.N;svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);}
        else if((flag&PATH_RAY_TERMINATE)!=0u){(*s).output.remaining=0u;}
    }
}
fn svm_bsdf(s:ptr<function,SvmState>,node:u32,flag:u32,visibility:u32){
    let kind=load_word(node+N_ClosureBsdf_closure_type);let mix_weight=svm_optional(s,svm_byte(node+N_ClosureBsdf_mix_weight_offset),1.0);
    var bytes=0u;switch(kind){case CLOSURE_BSDF_DIFFUSE_ID:{bytes=SZ_DiffuseBsdfData;}
        case CLOSURE_BSDF_TRANSLUCENT_ID,CLOSURE_BSDF_TRANSPARENT_ID:{bytes=SZ_SimpleBsdfData;}
        case CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID:{bytes=SZ_RefractionBsdfData;}
        case CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID:{bytes=SZ_GlassBsdfData;}
        case CLOSURE_BSDF_MICROFACET_GGX_ID,CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID:{bytes=SZ_GlossyBsdfData;}
        default:{svm_error(s,16u);return;}}
    let data=svm_take(s,bytes);if((*s).error!=0u||mix_weight==0.0){return;}
    // Scene specialization (pipelines.h) removes closure setup the uploaded SVM
    // programs cannot reach; reaching a removed handler is reported, never shaded.
    if(kind==CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID||kind==CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID){
//#if SVM_BSDF_GLASS else svm_error(s,2u);return;
        svm_glass(s,data,kind,mix_weight,visibility);return;
//#endif
    }
    if(kind==CLOSURE_BSDF_MICROFACET_GGX_ID||kind==CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID){
//#if SVM_BSDF_GLOSSY else svm_error(s,2u);return;
        svm_glossy(s,data,kind,mix_weight,visibility);return;
//#endif
    }
    let raw_weight=(*s).weight*mix_weight;
    if(kind==CLOSURE_BSDF_TRANSPARENT_ID){svm_transparent(s,raw_weight,flag);return;}
    let normal_index=svm_byte(data+select(N_SimpleBsdfData_normal_offset,N_DiffuseBsdfData_normal_offset,kind==CLOSURE_BSDF_DIFFUSE_ID));
    var n=(*s).geometry.N;if(normal_index!=SVM_STACK_INVALID){n=svm_load3(s,normal_index);}
    let magnitude=length(n);if(magnitude!=0.0){n*=1.0/magnitude;}else{n=(*s).geometry.N;}
    var roughness=0.0;var color=vec3<f32>(0);
    if(kind==CLOSURE_BSDF_DIFFUSE_ID){roughness=svm_input(s,data+N_DiffuseBsdfData_roughness);
        if(!(roughness<1e-5)){color=clamp(svm_input3(s,data+N_DiffuseBsdfData_color),vec3<f32>(0),vec3<f32>(1));}}
    else if(((*s).geometry.shader_flag&SD_USE_BUMP_MAP_CORRECTION)!=0u&&any((*s).geometry.Ng!=n)){
        n=svm_valid_normal((*s).geometry.Ng,(*s).geometry.wi,n);
    }
    let weight=max(raw_weight,vec3<f32>(0));let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));
    if(!(sample_weight>=1e-5)){return;}
    let i=svm_allocate(s,kind,weight);if(i==NONE){return;}
    var closure=svm_closure_read((*s).output,i);closure.N=n;closure.sample_weight=sample_weight;
    (*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_EVAL;
    if(kind==CLOSURE_BSDF_TRANSLUCENT_ID){(*s).output.runtime_flag|=SR_BSDF_HAS_TRANSMISSION;}
    else if(!(roughness<1e-5)){
        let pi=3.141592653589793;let sigma=clamp(roughness,0.0,1.0);
        let a=1.0/(pi+sigma*(1.5707963267948966f-2.0f/3.0f));let b=sigma*a;
        let average=svm_oren_add(a*pi,((6.283185307179586f-5.6f)/3.0f)*b);
        let ems=0.3183098861837907f*(color*color)*(average/(1.0-average))/(vec3<f32>(1)-color*(1.0-average));
        let ev=svm_oren_add(a*pi,b*svm_oren_g(max(dot(n,(*s).geometry.wi),0.0)));
        closure.kind=CLOSURE_BSDF_OREN_NAYAR_ID;closure.roughness=roughness;
        closure.a=a;closure.b=b;closure.multiscatter=ems*(1.0-ev);
    }
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,closure);
}

// Native svm_node_closure_volume: absorption shares the extinction accumulator.
// HG anisotropy uses the existing scalar roughness slot in the private record.
fn svm_volume(s:ptr<function,SvmState>,node:u32){
    let kind=load_word(node+N_ClosureVolume_closure_type);
    if(kind!=CLOSURE_VOLUME_ABSORPTION_ID&&kind!=CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID){svm_error(s,16u);return;}
    let mix_weight=svm_optional(s,svm_byte(node+N_ClosureVolume_mix_weight_offset),1.0);if(mix_weight==0.0){return;}
    var density=mix_weight*max(svm_input(s,node+N_ClosureVolume_density),0.0);
    if((*s).geometry.object_id!=NONE){density*=heap_float(table_at(G_objects,(*s).geometry.object_id,SIZE_objects)+O_volume_density);}
    var weight=(*s).weight;if(kind==CLOSURE_VOLUME_ABSORPTION_ID){weight=vec3<f32>(1)-weight;}weight*=density;
    if(kind==CLOSURE_VOLUME_HENYEY_GREENSTEIN_ID){
        let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));
        if(sample_weight>0.0){let i=svm_allocate(s,kind,weight);if(i!=NONE){
            var c=svm_closure_read((*s).output,i);c.sample_weight=sample_weight;
            let g=svm_input(s,node+N_ClosureVolume_param1);c.roughness=sign(g)*min(abs(g),1.0-0.001);
            svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);(*s).output.runtime_flag|=SR_SCATTER;}}
    }
    (*s).output.transparency+=weight;(*s).output.runtime_flag|=SR_EXTINCTION;
}
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2011-2013 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from pinned Cycles svm/closure.h, closure/bsdf_microfacet.h,
// util/lookup_table.h and util/math_float3.h. Typed records and bounded native
// table descriptors replace pointers; original notices are retained in
// native/THIRD-PARTY-NOTICES.txt and parity/licenses/BSD-3-Clause.txt.
fn microfacet_rotate(pnt:vec3<f32>,axis:vec3<f32>,angle:f32)->vec3<f32>{
    let c=cos(angle);let s=sin(angle);let m=1.0-c;
    return vec3<f32>((c+m*axis.x*axis.x)*pnt.x+(m*axis.x*axis.y-axis.z*s)*pnt.y+(m*axis.x*axis.z+axis.y*s)*pnt.z,
        (m*axis.x*axis.y+axis.z*s)*pnt.x+(c+m*axis.y*axis.y)*pnt.y+(m*axis.y*axis.z-axis.x*s)*pnt.z,
        (m*axis.x*axis.z-axis.y*s)*pnt.x+(m*axis.y*axis.z+axis.x*s)*pnt.y+(c+m*axis.z*axis.z)*pnt.z);
}
fn microfacet_lut1(s:ptr<function,SvmState>,x:f32,offset:u32,size:u32)->f32{
    let count=table_count(G_lookup_table);
    if(size==0u||offset>count||size>count-offset){svm_error(s,512u);return 0.0;}
    let value=clamp(x,0.0,1.0)*f32(size-1u);let index=min(u32(value),size-1u);let t=value-f32(index);
    let a=heap_float(table_at(G_lookup_table,offset+index,SIZE_lookup_table));if(t==0.0){return a;}
    let b=heap_float(table_at(G_lookup_table,offset+min(index+1u,size-1u),SIZE_lookup_table));return (1.0-t)*a+t*b;
}
fn microfacet_lut2(s:ptr<function,SvmState>,x:f32,y:f32,offset:u32)->f32{
    let count=table_count(G_lookup_table);
    if(offset>count||1024u>count-offset){svm_error(s,512u);return 0.0;}
    let value=clamp(y,0.0,1.0)*31.0;let index=min(u32(value),31u);let t=value-f32(index);
    let a=microfacet_lut1(s,x,offset+32u*index,32u);if(t==0.0){return a;}
    let b=microfacet_lut1(s,x,offset+32u*min(index+1u,31u),32u);return (1.0-t)*a+t*b;
}
fn svm_glossy(s:ptr<function,SvmState>,data:u32,kind:u32,mix_weight:f32,visibility:u32){
    if(data_word(D_caustics_reflective)==0u&&(visibility&PATH_RAY_VISIBILITY_DIFFUSE)!=0u){return;}
    let ni=svm_byte(data+N_GlossyBsdfData_normal_offset);var n=(*s).geometry.N;
    if(ni!=SVM_STACK_INVALID){n=svm_load3(s,ni);}
    if(any(n!=vec3<f32>(0))){n=surface_normalize(n);}else{n=(*s).geometry.N;}
    let weight=max((*s).weight*mix_weight,vec3<f32>(0));let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));
    if(!(sample_weight>=1e-5)){return;}
    let i=svm_allocate(s,CLOSURE_BSDF_MICROFACET_GGX_ID,weight);if(i==NONE){return;}
    var c=svm_closure_read((*s).output,i);c.sample_weight=sample_weight;c.N=n;c.ior=1.0;c.energy_scale=1.0;
    if(((*s).geometry.shader_flag&SD_USE_BUMP_MAP_CORRECTION)!=0u&&any((*s).geometry.Ng!=n)){c.N=svm_valid_normal((*s).geometry.Ng,(*s).geometry.wi,n);}
    let r=clamp(svm_input(s,data+N_GlossyBsdfData_roughness),0.0,1.0);let rough=r*r;
    let anisotropy=clamp(svm_input(s,data+N_GlossyBsdfData_anisotropy),-0.99,0.99);
    let ti=svm_byte(data+N_GlossyBsdfData_tangent_offset);
    c.alpha_x=rough;c.alpha_y=rough;
    if(ti!=SVM_STACK_INVALID&&abs(anisotropy)>1e-4){
        c.T=svm_load3(s,ti);let rotation=svm_input(s,data+N_GlossyBsdfData_rotation);
        if(rotation!=0.0){c.T=microfacet_rotate(c.T,c.N,rotation*6.283185307179586f);}
        if(anisotropy<0.0){c.alpha_x=rough/(1.0+anisotropy);c.alpha_y=rough*(1.0+anisotropy);}
        else{c.alpha_x=rough*(1.0-anisotropy);c.alpha_y=rough/(1.0-anisotropy);}
    }
    c.alpha_x=clamp(c.alpha_x,0.0,1.0);c.alpha_y=clamp(c.alpha_y,0.0,1.0);
    (*s).output.runtime_flag|=SR_BSDF;
    if(c.alpha_x*c.alpha_y>2e-10){(*s).output.runtime_flag|=SR_BSDF_HAS_EVAL;}
    if(kind==CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID){
        let color=max(svm_input3(s,data+N_GlossyBsdfData_color),vec3<f32>(0));
        let roughness=sqrt(sqrt(c.alpha_x*c.alpha_y));let mu=dot((*s).geometry.wi,c.N);
        let e=microfacet_lut2(s,roughness,mu,data_word(D_ggx_E));
        let average=microfacet_lut1(s,roughness,data_word(D_ggx_Eavg),32u);
        if((*s).error!=0u){return;}
        let missing=(1.0-e)/e;c.energy_scale=1.0+missing;
        if(any(color!=vec3<f32>(1))){
            let multi=color*average/(vec3<f32>(1)-color*(1.0-average));
            let darkening=(vec3<f32>(1)+multi*missing)/c.energy_scale;
            c.weight*=darkening;c.sample_weight*=(darkening.x+darkening.y+darkening.z)*(1.0/3.0);
        }
    }
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);
}
// Native Glass/Refraction SVM setup; Fresnel data stays in canonical arena
// records while allocation capacity follows native closure_alloc_extra.
fn ggx_f0(ior:f32)->f32{let r=rounded_divide(rounded_add(ior,-1.0),rounded_add(ior,1.0));return r*r;}
fn ggx_dielectric(cosine:f32,eta:f32)->vec2<f32>{
    let square=rounded_add(eta*eta,-rounded_add(1.0,-cosine*cosine));
    if(square<=0.0){return vec2<f32>(1,0);}
    let ci=abs(cosine);let ct=-rounded_divide(rounded_sqrt(max(square,0.0)),eta);
    let rs=rounded_divide(rounded_add(ci,eta*ct),rounded_add(ci,-eta*ct));
    let rp=rounded_divide(rounded_add(ct,eta*ci),rounded_add(eta*ci,-ct));
    return vec2<f32>(rounded_add(rs*rs,rp*rp)*0.5,ct);
}
fn microfacet_lut2_size(s:ptr<function,SvmState>,x:f32,y:f32,offset:u32,size:u32)->f32{
    let count=table_count(G_lookup_table);
    if(offset>count||size*size>count-offset){svm_error(s,512u);return 0.0;}
    let value=clamp(y,0.0,1.0)*f32(size-1u);let index=min(u32(value),size-1u);let t=value-f32(index);
    let a=microfacet_lut1(s,x,offset+size*index,size);if(t==0.0){return a;}
    let b=microfacet_lut1(s,x,offset+size*min(index+1u,size-1u),size);return (1.0-t)*a+t*b;
}
fn microfacet_lut3(s:ptr<function,SvmState>,x:f32,y:f32,z:f32,offset:u32)->f32{
    let count=table_count(G_lookup_table);
    if(offset>count||4096u>count-offset){svm_error(s,512u);return 0.0;}
    let value=clamp(z,0.0,1.0)*15.0;let index=min(u32(value),15u);let t=value-f32(index);
    let a=microfacet_lut2_size(s,x,y,offset+256u*index,16u);if(t==0.0){return a;}
    let b=microfacet_lut2_size(s,x,y,offset+256u*min(index+1u,15u),16u);return (1.0-t)*a+t*b;
}
fn svm_glass(s:ptr<function,SvmState>,data:u32,kind:u32,mix_weight:f32,visibility:u32){
    let glass=kind==CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID;
    let reflect=data_word(D_caustics_reflective)!=0u||(visibility&PATH_RAY_VISIBILITY_DIFFUSE)==0u;
    let transmit=data_word(D_caustics_refractive)!=0u||(visibility&PATH_RAY_VISIBILITY_DIFFUSE)==0u;
    if((!glass&&!transmit)||(glass&&!reflect&&!transmit)){return;}
    let normal_offset=select(N_RefractionBsdfData_normal_offset,N_GlassBsdfData_normal_offset,glass);
    let ni=svm_byte(data+normal_offset);var n=(*s).geometry.N;
    if(ni!=SVM_STACK_INVALID){n=svm_load3(s,ni);}
    if(any(n!=vec3<f32>(0))){n=surface_normalize(n);}else{n=(*s).geometry.N;}
    if(glass){let thickness=svm_input(s,data+N_GlassBsdfData_thin_film_thickness);svm_input(s,data+N_GlassBsdfData_thin_film_ior);
        if(!(thickness<=0.1)){svm_error(s,16u);return;}}
    let weight=max(select((*s).weight*mix_weight,vec3<f32>(mix_weight),glass),vec3<f32>(0));
    let sample_weight=abs((weight.x+weight.y+weight.z)*(1.0/3.0));if(!(sample_weight>=1e-5)){return;}
    let i=svm_allocate(s,kind,weight);if(i==NONE){return;}
    if(glass){if((*s).output.remaining<GLASS_EXTRA_CLOSURES){(*s).output.count--;(*s).output.remaining++;
            var empty:SvmClosure;svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,empty);return;}
        (*s).output.remaining-=GLASS_EXTRA_CLOSURES;}
    var c=svm_closure_read((*s).output,i);c.sample_weight=sample_weight;c.N=n;c.energy_scale=1.0;
    if(((*s).geometry.shader_flag&SD_USE_BUMP_MAP_CORRECTION)!=0u&&any((*s).geometry.Ng!=n)){c.N=svm_valid_normal((*s).geometry.Ng,(*s).geometry.wi,n);}
    let ior=max(svm_input(s,data+select(N_RefractionBsdfData_ior,N_GlassBsdfData_ior,glass)),1e-5);
    c.ior=select(ior,rounded_divide(1.0,ior),((*s).geometry.runtime_flag&SR_BACKFACING)!=0u);
    var rough=svm_input(s,data+select(N_RefractionBsdfData_roughness,N_GlassBsdfData_roughness,glass));
    if(glass){rough=clamp(rough,0.0,1.0);}c.alpha_x=clamp(rough*rough,0.0,1.0);c.alpha_y=c.alpha_x;
    (*s).output.runtime_flag|=SR_BSDF|SR_BSDF_HAS_TRANSMISSION;
    if(c.alpha_x*c.alpha_y>2e-10){(*s).output.runtime_flag|=SR_BSDF_HAS_EVAL;}
    if(glass){c.fresnel_f0=ggx_f0(ior);let color=max(svm_input3(s,data+N_GlassBsdfData_color),vec3<f32>(0));
        c.reflection_tint=select(vec3<f32>(0),color,reflect);c.transmission_tint=select(vec3<f32>(0),color,transmit);
        let roughness=sqrt(sqrt(c.alpha_x*c.alpha_y));let mu=dot((*s).geometry.wi,c.N);
        let z=sqrt(abs((c.ior-1.0)/(c.ior+1.0)));let v=microfacet_lut3(s,roughness,mu,z,data_word(D_ggx_gen_schlick_ior_s));
        let f=(1.0-v)*c.fresnel_f0+v;let albedo=f*c.reflection_tint+(1.0-f)*c.transmission_tint;
        c.sample_weight*=(albedo.x+albedo.y+albedo.z)*(1.0/3.0);
    }
    svm_closure_write((*s).output.closure_base+i*SVM_CLOSURE_BYTES,c);
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Modified from Cycles: SVM bytecode interpretation ported to WGSL.
// Node layouts/enums come from the compiled
// Cycles ABI; scalar math/gamma functions are generated from upstream AST.
// Background emission accumulation also derives from closure/emissive.h (BSD).
struct SvmState {
    stack:array<u32,255>, initialized:array<u32,8>, pc:u32, error:u32,
    volume:bool, weight:vec3<f32>, geometry:SurfaceData, output:SvmResult,
}
// Error bits: 2 unsupported opcode; 4 bytecode bounds; 8 invalid/uninitialized
// stack; 16 unsupported parameter; 32 instruction limit. Bit 1 is shared with
// other wavefront checks; 64 invalid attribute map/data range. An error never
// supplies substitute shader radiance. Image errors: 128 descriptor/data, 256 tiled cache.
// Microfacet lookup-table bounds: 512.
fn svm_error(s:ptr<function,SvmState>,code:u32){(*s).error|=code;(*s).output.error|=code;atomicOr(&heap[p(17u)/4u],code);}
fn svm_take(s:ptr<function,SvmState>,bytes:u32)->u32{
    let count=bytes/4u;
    if((*s).pc>p(22u)||count>p(22u)-(*s).pc){svm_error(s,4u);return 0u;}
    let address=p(21u)+(*s).pc*4u;(*s).pc+=count;return address;
}
fn svm_byte(address:u32)->u32{return (load_word(address)>>(8u*(address%4u)))&255u;}
fn svm_load_bits(s:ptr<function,SvmState>,index:u32)->u32{
    if(index>=SVM_STACK_SIZE){svm_error(s,8u);return 0u;}
    if(((*s).initialized[index/32u]&(1u<<(index%32u)))==0u){svm_error(s,8u);return 0u;}
    return (*s).stack[index];
}
fn svm_load(s:ptr<function,SvmState>,index:u32)->f32{return bitcast<f32>(svm_load_bits(s,index));}
fn svm_store_bits(s:ptr<function,SvmState>,index:u32,value:u32){
    if(index>=SVM_STACK_SIZE){svm_error(s,8u);return;}
    (*s).stack[index]=value;(*s).initialized[index/32u]|=1u<<(index%32u);
}
fn svm_store(s:ptr<function,SvmState>,index:u32,value:f32){svm_store_bits(s,index,bitcast<u32>(value));}
fn svm_load3(s:ptr<function,SvmState>,index:u32)->vec3<f32>{return vec3<f32>(svm_load(s,index),svm_load(s,index+1u),svm_load(s,index+2u));}
fn svm_store3(s:ptr<function,SvmState>,index:u32,value:vec3<f32>){svm_store(s,index,value.x);svm_store(s,index+1u,value.y);svm_store(s,index+2u,value.z);}
fn svm_input(s:ptr<function,SvmState>,address:u32)->f32{
    let bits=load_word(address);if((bits>>8u)==(SVM_INPUT_STACK_OFFSET_MASK>>8u)){return svm_load(s,bits&255u);}return bitcast<f32>(bits);
}
fn svm_input3(s:ptr<function,SvmState>,address:u32)->vec3<f32>{
    let bits=load_word(address);if((bits>>8u)==(SVM_INPUT_STACK_OFFSET_MASK>>8u)){return svm_load3(s,bits&255u);}return heap_vec3(address);
}
fn svm_optional(s:ptr<function,SvmState>,index:u32,default_value:f32)->f32{if(index==SVM_STACK_INVALID){return default_value;}return svm_load(s,index);}
fn svm_optional_store(s:ptr<function,SvmState>,index:u32,value:f32){if(index!=SVM_STACK_INVALID){svm_store(s,index,value);}}
fn svm_optional_store3(s:ptr<function,SvmState>,index:u32,value:vec3<f32>){if(index!=SVM_STACK_INVALID){svm_store3(s,index,value);}}
fn svm_int(s:ptr<function,SvmState>,value:f32)->u32{
    if((bitcast<u32>(value)&0x7f800000u)==0x7f800000u||value>=2147483648.0||value< -2147483648.0){svm_error(s,16u);return 0u;}
    return bitcast<u32>(i32(value));
}
fn svm_normalize(v:vec3<f32>)->vec3<f32>{let len=length(v);if(len!=0.0){return v*(1.0/len);}return v;}
fn svm_divide(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{return vec3<f32>(c_safe_divide(a.x,b.x),c_safe_divide(a.y,b.y),c_safe_divide(a.z,b.z));}
fn svm_vector(s:ptr<function,SvmState>,node:u32){
    let kind=load_word(node+N_VectorMath_math_type);
    let a=svm_input3(s,node+N_VectorMath_a);let b=svm_input3(s,node+N_VectorMath_b);let c=svm_input3(s,node+N_VectorMath_c);let scale=svm_input(s,node+N_VectorMath_param1);
    var v=vec3<f32>(0);var value=0.0;
    switch(kind){
        case NODE_VECTOR_MATH_ADD:{v=a+b;}
        case NODE_VECTOR_MATH_SUBTRACT:{v=a-b;}
        case NODE_VECTOR_MATH_MULTIPLY:{v=a*b;}
        case NODE_VECTOR_MATH_DIVIDE:{v=svm_divide(a,b);}
        case NODE_VECTOR_MATH_CROSS_PRODUCT:{v=cross(a,b);}
        case NODE_VECTOR_MATH_PROJECT:{let len2=dot(b,b);if(len2!=0.0){v=(dot(a,b)/len2)*b;}}
        case NODE_VECTOR_MATH_REFLECT:{let n=svm_normalize(b);v=a-2.0*dot(a,n)*n;}
        case NODE_VECTOR_MATH_REFRACT:{let n=svm_normalize(b);let ndot=dot(n,a);let k=1.0-scale*scale*(1.0-ndot*ndot);if(k>=0.0){v=scale*a-(scale*ndot+sqrt(k))*n;}}
        case NODE_VECTOR_MATH_FACEFORWARD:{v=select(-a,a,dot(c,b)<0.0);}
        case NODE_VECTOR_MATH_MULTIPLY_ADD:{v=a*b+c;}
        case NODE_VECTOR_MATH_DOT_PRODUCT:{value=dot(a,b);}
        case NODE_VECTOR_MATH_DISTANCE:{value=length(a-b);}
        case NODE_VECTOR_MATH_LENGTH:{value=length(a);}
        case NODE_VECTOR_MATH_SCALE:{v=a*scale;}
        case NODE_VECTOR_MATH_NORMALIZE:{v=svm_normalize(a);}
        case NODE_VECTOR_MATH_SNAP:{v=floor(svm_divide(a,b))*b;}
        case NODE_VECTOR_MATH_ROUND:{v=floor(a+vec3<f32>(0.5));}
        case NODE_VECTOR_MATH_FLOOR:{v=floor(a);}
        case NODE_VECTOR_MATH_CEIL:{v=ceil(a);}
        case NODE_VECTOR_MATH_MODULO:{v=vec3<f32>(c_safe_modulo(a.x,b.x),c_safe_modulo(a.y,b.y),c_safe_modulo(a.z,b.z));}
        case NODE_VECTOR_MATH_WRAP:{v=vec3<f32>(c_wrapf(a.x,b.x,c.x),c_wrapf(a.y,b.y,c.y),c_wrapf(a.z,b.z,c.z));}
        case NODE_VECTOR_MATH_FRACTION:{v=a-floor(a);}
        case NODE_VECTOR_MATH_ABSOLUTE:{v=abs(a);}
        case NODE_VECTOR_MATH_POWER:{v=vec3<f32>(c_safe_powf(a.x,b.x),c_safe_powf(a.y,b.y),c_safe_powf(a.z,b.z));}
        case NODE_VECTOR_MATH_SIGN:{v=vec3<f32>(c_compatible_signf(a.x),c_compatible_signf(a.y),c_compatible_signf(a.z));}
        case NODE_VECTOR_MATH_MINIMUM:{v=min(a,b);}
        case NODE_VECTOR_MATH_MAXIMUM:{v=max(a,b);}
        case NODE_VECTOR_MATH_SINE:{v=sin(a);}
        case NODE_VECTOR_MATH_COSINE:{v=cos(a);}
        case NODE_VECTOR_MATH_TANGENT:{v=tan(a);}
        default:{svm_error(s,16u);}
    }
    svm_optional_store(s,svm_byte(node+N_VectorMath_value_offset),value);svm_optional_store3(s,svm_byte(node+N_VectorMath_vector_offset),v);
}
fn svm_transform(address:u32,v:vec3<f32>)->vec3<f32>{
    let x=heap_vec4(address);let y=heap_vec4(address+16u);let z=heap_vec4(address+32u);
    return vec3<f32>(x.x,y.x,z.x)*v.x+(vec3<f32>(x.y,y.y,z.y)*v.y+vec3<f32>(x.z,y.z,z.z)*v.z)+vec3<f32>(x.w,y.w,z.w);
}
fn svm_texco(s:ptr<function,SvmState>,node:u32,index:u32)->vec3<f32>{
    let kind=svm_byte(node+N_TexCoord_texco_type);let sd=(*s).geometry;let world=sd.object_id==NONE;
    var object=0u;if(!world){object=table_at(G_objects,sd.object_id,SIZE_objects);}
    switch(kind){
        case NODE_TEXCO_OBJECT:{if(world){return sd.P;}return surface_object_point(sd,sd.P,true);}
        case NODE_TEXCO_VOLUME_GENERATED:{if(!world){svm_error(s,16u);}return sd.P;}
        case NODE_TEXCO_NORMAL:{if(world){return sd.N;}return surface_object_normal(sd,sd.N,true);}
        case NODE_TEXCO_REFLECTION:{if(world){return sd.wi;}return 2.0*dot(sd.wi,sd.N)*sd.N-sd.wi;}
        case NODE_TEXCO_DUPLI_GENERATED:{if(world){return vec3<f32>(0);}return heap_vec3(object+O_dupli_generated);}
        case NODE_TEXCO_DUPLI_UV:{if(world){return vec3<f32>(0);}return vec3<f32>(heap_float(object+O_dupli_uv),heap_float(object+O_dupli_uv+4u),0);}
        case NODE_TEXCO_OBJECT_WITH_TRANSFORM:{let address=svm_take(s,SZ_PackedTransform);if((*s).error!=0u){return vec3<f32>(0);}return svm_transform(address,sd.P);}
        case NODE_TEXCO_CAMERA,NODE_TEXCO_WINDOW:{
            let matrix_world=p(6u)+D_cam_cameratoworld;let camera_pos=vec3<f32>(heap_float(matrix_world+12u),heap_float(matrix_world+28u),heap_float(matrix_world+44u));
            if(kind==NODE_TEXCO_CAMERA){return svm_transform(p(6u)+D_cam_worldtocamera,sd.P+select(vec3<f32>(0),camera_pos,world));}
            let camera_type=data_word(D_cam_type);var point=sd.P;
            if(camera_type==CAMERA_PERSPECTIVE){if(world){point+=camera_pos;}}
            else if(camera_type==CAMERA_ORTHOGRAPHIC){if(world&&(state_byte(S_path_visibility,index)&VIS_CAMERA)!=0u){point=heap_vec3(state_address(S_ray_P,index,12u));}}
            else{svm_error(s,16u);return vec3<f32>(0);}
            let matrix=p(6u)+D_cam_worldtondc;let v=vec4<f32>(point,1);let w=dot(heap_vec4(matrix+48u),v);
            return vec3<f32>(dot(heap_vec4(matrix),v)/w,dot(heap_vec4(matrix+16u),v)/w,0);
        }
        default:{svm_error(s,16u);return vec3<f32>(0);}
    }
}
fn svm_gradient(s:ptr<function,SvmState>,kind:u32,v:vec3<f32>)->f32{
    switch(kind){
        case NODE_BLEND_LINEAR:{return v.x;}
        case NODE_BLEND_QUADRATIC:{let r=max(v.x,0.0);return r*r;}
        case NODE_BLEND_EASING:{let r=clamp(v.x,0.0,1.0);let t=r*r;return 3.0*t-2.0*t*r;}
        case NODE_BLEND_DIAGONAL:{return (v.x+v.y)*0.5;}
        case NODE_BLEND_RADIAL:{return atan2(v.y,v.x)/6.2831855+0.5;}
        case NODE_BLEND_QUADRATIC_SPHERE,NODE_BLEND_SPHERICAL:{let r=max(0.999999-sqrt(v.x*v.x+v.y*v.y+v.z*v.z),0.0);return select(r,r*r,kind==NODE_BLEND_QUADRATIC_SPHERE);}
        default:{svm_error(s,16u);return 0.0;}
    }
}
fn svm_evaluate_context_seeded(index:u32,geometry:SurfaceData,closure_limit:u32,closure_base:u32,flag:u32,visibility:u32,lcg:u32,volume:bool,previous:SvmResult)->SvmResult{
    var s:SvmState;s.output=previous;s.volume=volume;s.geometry=geometry;s.pc=(geometry.shader&SHADER_MASK)*(1u+SZ_ShaderJump/4u);
    s.output.runtime_flag=geometry.runtime_flag;s.output.N=geometry.N;s.output.lcg_state=lcg;
    if(closure_limit>SVM_CLOSURE_CAPACITY){svm_error(&s,16u);return s.output;}
    s.output.closure_base=closure_base;
    if(closure_base!=0u){let bytes=arrayLength(&heap)*4u;let required=SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES;
        if(closure_base%4u!=0u||closure_base>bytes||required>bytes-closure_base){svm_error(&s,1u);return s.output;}}
    else if(closure_limit!=0u){svm_error(&s,1u);return s.output;}

    s.output.remaining=0u;
    if((visibility&PATH_RAY_VISIBILITY_SHADOW)==0u&&(flag&(PATH_RAY_TERMINATE|PATH_RAY_EMISSION|select(0u,PATH_RAY_EXTINCTION,volume)))==0u){s.output.remaining=closure_limit;}

    for(var instructions=0u;instructions<65536u;instructions++){
        let opcode_address=svm_take(&s,4u);if(s.error!=0u){return s.output;}let opcode=load_word(opcode_address);
        if(opcode==NODE_END){s.output.N=s.geometry.N;return s.output;}
        var size=0u;
        switch(opcode){
            case NODE_SHADER_JUMP:{size=SZ_ShaderJump;}
            case NODE_JUMP_IF_ZERO:{size=SZ_JumpIfZero;}
            case NODE_JUMP_IF_ONE:{size=SZ_JumpIfOne;}
            case NODE_MATH:{size=SZ_Math;}
            case NODE_CLAMP:{size=SZ_Clamp;}
            case NODE_VALUE_F:{size=SZ_ValueF;}
            case NODE_VALUE_V:{size=SZ_ValueV;}
            case NODE_CONVERT:{size=SZ_Convert;}
            case NODE_GEOMETRY:{size=SZ_Geometry;}
            case NODE_TEX_COORD:{size=SZ_TexCoord;}
            case NODE_TEX_IMAGE,NODE_TEX_IMAGE_DERIVATIVE:{size=SZ_TexImage;}
            case NODE_TEX_IMAGE_BOX,NODE_TEX_IMAGE_BOX_DERIVATIVE:{size=SZ_TexImageBox;}
            case NODE_TEX_ENVIRONMENT,NODE_TEX_ENVIRONMENT_DERIVATIVE:{size=SZ_TexEnvironment;}
            case NODE_ATTR,NODE_ATTR_DERIVATIVE:{size=SZ_Attr;}
            case NODE_NORMAL_MAP:{size=SZ_NormalMap;}
            case NODE_TANGENT,NODE_TANGENT_DERIVATIVE:{size=SZ_Tangent;}
            case NODE_VERTEX_COLOR,NODE_VERTEX_COLOR_DERIVATIVE:{size=SZ_VertexColor;}
            case NODE_VECTOR_MATH:{size=SZ_VectorMath;}
            case NODE_SEPARATE_VECTOR:{size=SZ_SeparateVector;}
            case NODE_COMBINE_VECTOR:{size=SZ_CombineVector;}
            case NODE_CLOSURE_VOLUME:{size=SZ_ClosureVolume;}
            case NODE_CLOSURE_BSDF:{size=SZ_ClosureBsdf;}
            case NODE_CLOSURE_HOLDOUT:{size=SZ_ClosureHoldout;}
            case NODE_CLOSURE_SET_NORMAL:{size=SZ_ClosureSetNormal;}
            case NODE_CLOSURE_SET_WEIGHT:{size=SZ_ClosureSetWeight;}
            case NODE_CLOSURE_WEIGHT:{size=SZ_ClosureWeight;}
            case NODE_CLOSURE_BACKGROUND:{size=SZ_ClosureBackground;}
            case NODE_CLOSURE_EMISSION:{size=SZ_ClosureEmission;}
            case NODE_EMISSION_WEIGHT:{size=SZ_EmissionWeight;}
            case NODE_MIX_CLOSURE:{size=SZ_MixClosure;}
            case NODE_TEX_CHECKER:{size=SZ_TexChecker;}
            case NODE_TEX_GRADIENT:{size=SZ_TexGradient;}
            case NODE_GAMMA:{size=SZ_Gamma;}
            case NODE_INVERT:{size=SZ_Invert;}
            case NODE_BRIGHTCONTRAST:{size=SZ_BrightContrast;}
            default:{svm_error(&s,2u);return s.output;}
        }
        let node=svm_take(&s,size);if(s.error!=0u){return s.output;}
        switch(opcode){
            case NODE_SHADER_JUMP:{s.pc=load_word(node+select(N_ShaderJump_offset_surface,N_ShaderJump_offset_volume,volume));}
            case NODE_JUMP_IF_ZERO,NODE_JUMP_IF_ONE:{
                var offset=N_JumpIfZero_stack_offset;var jump_offset=N_JumpIfZero_jump_offset;
                if(opcode==NODE_JUMP_IF_ONE){offset=N_JumpIfOne_stack_offset;jump_offset=N_JumpIfOne_jump_offset;}
                let f=svm_load(&s,svm_byte(node+offset));if((opcode==NODE_JUMP_IF_ZERO&&f<=0.0)||(opcode==NODE_JUMP_IF_ONE&&f>=1.0)){s.pc+=load_word(node+jump_offset);}
            }
            case NODE_VALUE_F:{svm_store_bits(&s,svm_byte(node+N_ValueF_out_offset),load_word(node+N_ValueF_value));}
            case NODE_VALUE_V:{svm_store3(&s,svm_byte(node+N_ValueV_out_offset),heap_vec3(node+N_ValueV_value));}
            case NODE_GEOMETRY:{
//#if SVM_NODE_GEOMETRY else svm_error(&s,2u);
                let kind=svm_byte(node+N_Geometry_geom_type);var v=vec3<f32>(0);
                switch(kind){
                    case NODE_GEOM_P:{v=s.geometry.P;}
                    case NODE_GEOM_N:{v=s.geometry.N;}
                    case NODE_GEOM_Ng:{v=s.geometry.Ng;}
                    case NODE_GEOM_I:{v=s.geometry.wi;}
                    case NODE_GEOM_uv:{v=vec3<f32>(1.0-s.geometry.u-s.geometry.v,s.geometry.u,0);}
                    case NODE_GEOM_T:{if(s.geometry.object_id!=NONE){svm_error(&s,16u);}else{v=normalize(s.geometry.dPdu);}}
                    default:{svm_error(&s,16u);}
                }
                svm_store3(&s,svm_byte(node+N_Geometry_out_offset),v);
//#endif
            }
            case NODE_TEX_COORD:{
//#if SVM_NODE_TEX_COORD else svm_error(&s,2u);
                svm_store3(&s,svm_byte(node+N_TexCoord_out_offset),svm_texco(&s,node,index));
//#endif
            }
            // One call site for all image layouts: drivers inline every WGSL call,
            // so three identical svm_image calls tripled this code for the compiler.
            case NODE_TEX_IMAGE,NODE_TEX_IMAGE_DERIVATIVE,NODE_TEX_IMAGE_BOX,NODE_TEX_IMAGE_BOX_DERIVATIVE,NODE_TEX_ENVIRONMENT,NODE_TEX_ENVIRONMENT_DERIVATIVE:{
//#if SVM_NODE_IMAGE else svm_error(&s,2u);
                var image_layout=0u;if(opcode==NODE_TEX_IMAGE_BOX||opcode==NODE_TEX_IMAGE_BOX_DERIVATIVE){image_layout=1u;}
                else if(opcode==NODE_TEX_ENVIRONMENT||opcode==NODE_TEX_ENVIRONMENT_DERIVATIVE){image_layout=2u;}
                svm_image(&s,node,image_layout,opcode==NODE_TEX_IMAGE_DERIVATIVE||opcode==NODE_TEX_IMAGE_BOX_DERIVATIVE||opcode==NODE_TEX_ENVIRONMENT_DERIVATIVE);
//#endif
            }
            case NODE_ATTR,NODE_ATTR_DERIVATIVE:{
//#if SVM_NODE_ATTR else svm_error(&s,2u);
                svm_attribute(&s,node,opcode==NODE_ATTR_DERIVATIVE);
//#endif
            }
            case NODE_NORMAL_MAP:{
//#if SVM_NODE_NORMAL_MAP else svm_error(&s,2u);
                svm_normal_map(&s,node);
//#endif
            }
            case NODE_TANGENT,NODE_TANGENT_DERIVATIVE:{
//#if SVM_NODE_TANGENT else svm_error(&s,2u);
                svm_tangent(&s,node,opcode==NODE_TANGENT_DERIVATIVE);
//#endif
            }
            case NODE_VERTEX_COLOR,NODE_VERTEX_COLOR_DERIVATIVE:{
//#if SVM_NODE_VERTEX_COLOR else svm_error(&s,2u);
                svm_vertex_color(&s,node,opcode==NODE_VERTEX_COLOR_DERIVATIVE);
//#endif
            }
            case NODE_MATH:{
//#if SVM_NODE_MATH else svm_error(&s,2u);
                let kind=load_word(node+N_Math_math_type);if(kind>NODE_MATH_FLOORED_MODULO){svm_error(&s,16u);}
                else{let value=c_svm_math(kind,svm_input(&s,node+N_Math_value1),svm_input(&s,node+N_Math_value2),svm_input(&s,node+N_Math_value3));svm_store(&s,svm_byte(node+N_Math_result_offset),value);}
//#endif
            }
            case NODE_VECTOR_MATH:{
//#if SVM_NODE_VECTOR_MATH else svm_error(&s,2u);
                svm_vector(&s,node);
//#endif
            }
            case NODE_CLAMP:{
//#if SVM_NODE_CLAMP else svm_error(&s,2u);
                let kind=load_word(node+N_Clamp_clamp_type);var lo=svm_input(&s,node+N_Clamp_min);var hi=svm_input(&s,node+N_Clamp_max);let v=svm_input(&s,node+N_Clamp_value);
                if(kind==NODE_CLAMP_RANGE&&lo>hi){let tmp=lo;lo=hi;hi=tmp;}
                else if(kind!=NODE_CLAMP_RANGE&&kind!=NODE_CLAMP_MINMAX){svm_error(&s,16u);}
                // Cycles permits reversed MINMAX inputs; WGSL clamp requires ordered bounds.
                svm_store(&s,svm_byte(node+N_Clamp_result_offset),min(max(v,lo),hi));
//#endif
            }
            case NODE_CONVERT:{
//#if SVM_NODE_CONVERT else svm_error(&s,2u);
                let kind=load_word(node+N_Convert_convert_type);let source_index=svm_byte(node+N_Convert_from_offset);let dest=svm_byte(node+N_Convert_to_offset);
                switch(kind){
                    case NODE_CONVERT_FI:{svm_store_bits(&s,dest,svm_int(&s,svm_load(&s,source_index)));}
                    case NODE_CONVERT_FV:{svm_store3(&s,dest,vec3<f32>(svm_load(&s,source_index)));}
                    case NODE_CONVERT_IF:{svm_store(&s,dest,f32(bitcast<i32>(svm_load_bits(&s,source_index))));}
                    case NODE_CONVERT_IV:{svm_store3(&s,dest,vec3<f32>(f32(bitcast<i32>(svm_load_bits(&s,source_index)))));}
                    case NODE_CONVERT_CF,NODE_CONVERT_CI,NODE_CONVERT_VF,NODE_CONVERT_VI:{
                        let v=svm_load3(&s,source_index);var f=(v.x+v.y+v.z)*(1.0/3.0);
                        if(kind==NODE_CONVERT_CF||kind==NODE_CONVERT_CI){f=dot(v,heap_vec3(p(6u)+D_film_rgb_to_y));}
                        if(kind==NODE_CONVERT_CI||kind==NODE_CONVERT_VI){svm_store_bits(&s,dest,svm_int(&s,f));}else{svm_store(&s,dest,f);}
                    }
                    default:{svm_error(&s,16u);}
                }
//#endif
            }
            case NODE_SEPARATE_VECTOR:{
//#if SVM_NODE_SEPARATE_VECTOR else svm_error(&s,2u);
                let component=svm_byte(node+N_SeparateVector_vector_index);if(component>2u){svm_error(&s,16u);}else{let v=svm_input3(&s,node+N_SeparateVector_vector);svm_optional_store(&s,svm_byte(node+N_SeparateVector_out_offset),v[component]);}
//#endif
            }
            case NODE_COMBINE_VECTOR:{
//#if SVM_NODE_COMBINE_VECTOR else svm_error(&s,2u);
                let component=svm_byte(node+N_CombineVector_vector_index);let dest=svm_byte(node+N_CombineVector_out_offset);if(component>2u){svm_error(&s,16u);}else if(dest!=SVM_STACK_INVALID){svm_store(&s,dest+component,svm_input(&s,node+N_CombineVector_in));}
//#endif
            }
            case NODE_CLOSURE_VOLUME:{
//#if SVM_NODE_CLOSURE_VOLUME else svm_error(&s,2u);
                if(volume){svm_volume(&s,node);}
//#endif
            }
            case NODE_CLOSURE_BSDF:{if(SVM_SURFACE){svm_bsdf(&s,node,flag,visibility);}else{svm_error(&s,2u);}}
            case NODE_CLOSURE_HOLDOUT:{
//#if SVM_NODE_CLOSURE_HOLDOUT else svm_error(&s,2u);
                if(!SVM_SURFACE){svm_error(&s,2u);return s.output;}
                let mix_index=svm_byte(node+N_ClosureHoldout_mix_weight_offset);let mix_weight=svm_optional(&s,mix_index,1.0);
                if(mix_index==SVM_STACK_INVALID||mix_weight!=0.0){_ = svm_allocate(&s,CLOSURE_HOLDOUT_ID,s.weight*mix_weight);s.output.runtime_flag|=SR_HOLDOUT;}
//#endif
            }
            case NODE_CLOSURE_SET_NORMAL:{
//#if SVM_NODE_CLOSURE_SET_NORMAL else svm_error(&s,2u);
                if(!SVM_SURFACE){svm_error(&s,2u);return s.output;}let n=svm_load3(&s,svm_byte(node+N_ClosureSetNormal_direction_offset));s.geometry.N=n;svm_store3(&s,svm_byte(node+N_ClosureSetNormal_normal_offset),n);
//#endif
            }
            case NODE_CLOSURE_SET_WEIGHT:{s.weight=heap_vec3(node+N_ClosureSetWeight_rgb);}
            case NODE_CLOSURE_WEIGHT:{s.weight=svm_load3(&s,svm_byte(node+N_ClosureWeight_weight_offset));}
            case NODE_EMISSION_WEIGHT:{s.weight=svm_input3(&s,node+N_EmissionWeight_color)*svm_input(&s,node+N_EmissionWeight_strength);}
            case NODE_CLOSURE_BACKGROUND,NODE_CLOSURE_EMISSION:{
                let offset=select(N_ClosureBackground_mix_weight_offset,N_ClosureEmission_mix_weight_offset,opcode==NODE_CLOSURE_EMISSION);
                let mix_index=svm_byte(node+offset);let mix_weight=svm_optional(&s,mix_index,1.0);
                if(mix_index==SVM_STACK_INVALID||mix_weight!=0.0){let weight=s.weight*mix_weight;if((s.output.runtime_flag&SR_EMISSION)!=0u){s.output.emission+=weight;}else{s.output.emission=weight;s.output.runtime_flag|=SR_EMISSION;}}
            }
            case NODE_MIX_CLOSURE:{
                let f=clamp(svm_input(&s,node+N_MixClosure_fac),0.0,1.0);let weight=svm_optional(&s,svm_byte(node+N_MixClosure_in_weight_offset),1.0);
                svm_optional_store(&s,svm_byte(node+N_MixClosure_weight1_offset),weight*(1.0-f));svm_optional_store(&s,svm_byte(node+N_MixClosure_weight2_offset),weight*f);
            }
            case NODE_TEX_CHECKER:{
//#if SVM_NODE_TEX_CHECKER else svm_error(&s,2u);
                let co=svm_load3(&s,svm_byte(node+N_TexChecker_co))*svm_input(&s,node+N_TexChecker_scale);
                let adjusted=(co+vec3<f32>(0.000001))*0.999999;let parity=abs(vec3<i32>(floor(adjusted)))%vec3<i32>(2);
                let f=select(0.0,1.0,i32(parity.x==parity.y)==parity.z);
                let color1=svm_input3(&s,node+N_TexChecker_color1);let color2=svm_input3(&s,node+N_TexChecker_color2);
                svm_optional_store3(&s,svm_byte(node+N_TexChecker_color_offset),select(color2,color1,f==1.0));svm_optional_store(&s,svm_byte(node+N_TexChecker_fac_offset),f);
//#endif
            }
            case NODE_TEX_GRADIENT:{
//#if SVM_NODE_TEX_GRADIENT else svm_error(&s,2u);
                let f=clamp(svm_gradient(&s,load_word(node+N_TexGradient_gradient_type),svm_load3(&s,svm_byte(node+N_TexGradient_co))),0.0,1.0);
                svm_optional_store(&s,svm_byte(node+N_TexGradient_fac_offset),f);svm_optional_store3(&s,svm_byte(node+N_TexGradient_color_offset),vec3<f32>(f));
//#endif
            }
            case NODE_GAMMA:{
//#if SVM_NODE_GAMMA else svm_error(&s,2u);
                svm_optional_store3(&s,svm_byte(node+N_Gamma_out_offset),c_svm_math_gamma_color(svm_input3(&s,node+N_Gamma_color),svm_input(&s,node+N_Gamma_gamma)));
//#endif
            }
            case NODE_INVERT:{
//#if SVM_NODE_INVERT else svm_error(&s,2u);
                let color=svm_input3(&s,node+N_Invert_color);let f=svm_input(&s,node+N_Invert_fac);svm_optional_store3(&s,svm_byte(node+N_Invert_out_offset),f*(vec3<f32>(1)-color)+(1.0-f)*color);
//#endif
            }
            case NODE_BRIGHTCONTRAST:{
//#if SVM_NODE_BRIGHTCONTRAST else svm_error(&s,2u);
                let color=svm_input3(&s,node+N_BrightContrast_color);let bright=svm_input(&s,node+N_BrightContrast_bright);let contrast=svm_input(&s,node+N_BrightContrast_contrast);
                svm_optional_store3(&s,svm_byte(node+N_BrightContrast_out_offset),max((1.0+contrast)*color+vec3<f32>(bright-contrast*0.5),vec3<f32>(0)));
//#endif
            }
            default:{svm_error(&s,2u);}
        }
        if(s.error!=0u){return s.output;}
    }
    svm_error(&s,32u);return s.output;
}

fn svm_world(index:u32,shader_id:u32)->vec3<f32>{
    var sd:SurfaceData;let direction=heap_vec3(state_address(S_ray_D,index,12u));
    sd.P=direction;sd.N=-direction;sd.Ng=-direction;sd.wi=-direction;sd.object_id=NONE;sd.prim=NONE;
    sd.shader=shader_id;sd.shader_flag=load_word(p(18u)+shader_id*SIZE_shaders+SH_flags);
    sd.dPdu=surface_orthogonal(sd.Ng);sd.dPdv=cross(sd.Ng,sd.dPdu);
    sd.time=heap_float(state_address(S_ray_time,index,4u));sd.ray_length=bitcast<f32>(0x7f7fffffu);
    sd.dP=heap_float(state_address(S_ray_dD,index,4u));sd.dI=sd.dP;sd.du=vec2<f32>(sd.dP,0);sd.dv=vec2<f32>(0,sd.dP);
    return svm_evaluate(index,sd,0u,0u).emission;
}

fn svm_evaluate(index:u32,geometry:SurfaceData,closure_limit:u32,closure_base:u32)->SvmResult{
    return svm_evaluate_mode(index,geometry,closure_limit,closure_base,false);
}

fn svm_evaluate_mode(index:u32,geometry:SurfaceData,closure_limit:u32,closure_base:u32,volume:bool)->SvmResult{
    var empty:SvmResult;return svm_evaluate_seeded(index,geometry,closure_limit,closure_base,volume,empty);
}

// Function-local SvmState resets stack initialization on every evaluation,
// including repeated media and transparent hits. Only closure output is seeded.
fn svm_evaluate_context(index:u32,geometry:SurfaceData,closure_limit:u32,closure_base:u32,flag:u32,visibility:u32,lcg:u32,volume:bool)->SvmResult{
    var empty:SvmResult;
    return svm_evaluate_context_seeded(index,geometry,closure_limit,closure_base,flag,visibility,lcg,volume,empty);
}
fn svm_evaluate_seeded(index:u32,geometry:SurfaceData,closure_limit:u32,closure_base:u32,volume:bool,previous:SvmResult)->SvmResult{
    var flag=0u;var visibility=VIS_CAMERA;var lcg=previous.lcg_state;
    // Homogeneous density preparation has no integrator path to dereference.
    if(index!=NONE){flag=state_word(S_path_flag,index);visibility=state_byte(S_path_visibility,index);lcg=svm_lcg(index);}
    return svm_evaluate_context_seeded(index,geometry,closure_limit,closure_base,flag,visibility,lcg,volume,previous);
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles geom/attribute.h, geom/triangle.h, geom/primitive.h,
// svm/attribute.h, svm/vertex_color.h, svm/util.h, util/color.h and
// kernel/util/colorspace.h. Native layouts, bounded virtual-address loads and
// explicit unsupported-domain errors replace native pointers/templates.
struct AttributeDesc {element:u32, kind:u32, offset:i32, found:bool,}
struct AttributeValue {value:vec4<f32>, dx:vec4<f32>, dy:vec4<f32>,}

fn attribute_find(s:ptr<function,SvmState>,id_low:u32,id_high:u32)->AttributeDesc{
    var desc:AttributeDesc;
    let sd=(*s).geometry;if(sd.object_id==NONE){return desc;}
    if(sd.object_id>=table_count(G_objects)){svm_error(s,64u);return desc;}
    var at=load_word(table_at(G_objects,sd.object_id,SIZE_objects)+O_attribute_map_offset);
    let count=table_count(G_attributes_map);
    // A valid chain cannot visit more entries than the complete map contains.
    for(var steps=0u;steps<count;steps++){
        if(at>=count){svm_error(s,64u);return desc;}
        let address=table_at(G_attributes_map,at,SIZE_attributes_map);
        let lo=load_word(address+AM_id);let hi=load_word(address+AM_id+4u);
        let element=svm_byte(address+AM_element)|(svm_byte(address+AM_element+1u)<<8u);
        let offset=load_word(address+AM_offset);
        if(lo==id_low&&hi==id_high){
            desc.element=element;desc.kind=svm_byte(address+AM_type);desc.offset=bitcast<i32>(offset);
            desc.found=element!=ATTR_ELEMENT_NONE&&offset!=ATTR_STD_NOT_FOUND;
            if(sd.prim==NONE&&(element&(ATTR_ELEMENT_OBJECT|ATTR_ELEMENT_MESH|ATTR_ELEMENT_VOXEL))==0u){desc.found=false;}
            return desc;
        }
        if(lo==ATTR_STD_NONE&&hi==0u){
            if(element==0u){return desc;}at=offset;
        }else{
            if(at>0xffffffffu-ATTR_PRIM_TYPES){svm_error(s,64u);return desc;}at+=ATTR_PRIM_TYPES;
        }
    }
    svm_error(s,64u);return desc;
}
fn attribute_srgb(c:f32)->f32{
    if(c<0.04045){return max(c,0.0)*(1.0/12.92);}
    return pow((c+0.055)*(1.0/1.055),2.4);
}
fn attribute_fetch(s:ptr<function,SvmState>,desc:AttributeDesc,index:u32)->vec4<f32>{
    // Native offsets may be negative. Avoid signed overflow before conversion.
    var at=0u;
    if(desc.offset<0){let magnitude=0u-bitcast<u32>(desc.offset);if(index<magnitude){svm_error(s,64u);return vec4<f32>(0);}at=index-magnitude;}
    else{let offset=u32(desc.offset);if(index>0xffffffffu-offset){svm_error(s,64u);return vec4<f32>(0);}at=index+offset;}
    var table=G_attributes_float;var stride=SIZE_attributes_float;
    switch(desc.kind){
        case NODE_ATTR_FLOAT:{}
        case NODE_ATTR_FLOAT2:{table=G_attributes_float2;stride=SIZE_attributes_float2;}
        case NODE_ATTR_FLOAT3:{table=G_attributes_float3;stride=SIZE_attributes_float3;
            if((desc.element&ATTR_ELEMENT_IS_NORMAL)!=0u){table=G_attributes_normal;stride=SIZE_attributes_normal;}}
        case NODE_ATTR_FLOAT4,NODE_ATTR_RGBA:{table=G_attributes_float4;stride=SIZE_attributes_float4;
            if((desc.element&ATTR_ELEMENT_IS_BYTE)!=0u){table=G_attributes_uchar4;stride=SIZE_attributes_uchar4;}}
        default:{svm_error(s,16u);return vec4<f32>(0);}
    }
    if(at>=table_count(table)){svm_error(s,64u);return vec4<f32>(0);}
    let address=table_at(table,at,stride);
    if(table==G_attributes_float){return vec4<f32>(heap_float(address),0,0,0);}
    if(table==G_attributes_float2){return vec4<f32>(heap_float(address),heap_float(address+4u),0,0);}
    if(table==G_attributes_float3){return vec4<f32>(heap_vec3(address),0);}
    if(table==G_attributes_normal){return vec4<f32>(surface_decode_normal(load_word(address)),0);}
    if(table==G_attributes_uchar4){
        let bits=load_word(address);let c=vec4<f32>(f32(bits&255u),f32((bits>>8u)&255u),f32((bits>>16u)&255u),f32(bits>>24u))*(1.0/255.0);
        var rgb=vec3<f32>(attribute_srgb(c.x),attribute_srgb(c.y),attribute_srgb(c.z));
        if(data_word(D_film_is_rec709)==0u){rgb=vec3<f32>(dot(heap_vec3(p(6u)+D_film_rec709_to_r),rgb),dot(heap_vec3(p(6u)+D_film_rec709_to_g),rgb),dot(heap_vec3(p(6u)+D_film_rec709_to_b),rgb));}
        return vec4<f32>(rgb,c.w);
    }
    return heap_vec4(address);
}
fn attribute_read(s:ptr<function,SvmState>,desc:AttributeDesc)->AttributeValue{
    var result:AttributeValue;if(!desc.found){return result;}
    let sd=(*s).geometry;
    let domain=desc.element&~(ATTR_ELEMENT_IS_BYTE|ATTR_ELEMENT_IS_NORMAL);
    if((desc.element&ATTR_ELEMENT_IS_BYTE)!=0u&&desc.kind!=NODE_ATTR_RGBA&&desc.kind!=NODE_ATTR_FLOAT4){svm_error(s,16u);return result;}
    if((desc.element&ATTR_ELEMENT_IS_NORMAL)!=0u&&desc.kind!=NODE_ATTR_FLOAT3){svm_error(s,16u);return result;}
    // Drivers inline every call; this function is itself inlined into each
    // attribute-reading node, so all element kinds share one attribute_fetch call
    // site. Fetch order and values are unchanged (a, b, c in vertex order).
    var indices=vec3<u32>(0u);var count=1u;
    if(domain!=ATTR_ELEMENT_OBJECT&&domain!=ATTR_ELEMENT_MESH){
        if(!native_triangle_kind(sd.primitive_type)){svm_error(s,16u);return result;}
        if(domain==ATTR_ELEMENT_FACE){indices=vec3<u32>(sd.prim);}
        else if(domain==ATTR_ELEMENT_VERTEX){
            if(sd.prim>=table_count(G_tri_vindex)){svm_error(s,64u);return result;}
            let address=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);indices=vec3<u32>(load_word(address),load_word(address+4u),load_word(address+8u));count=3u;
        }else if(domain==ATTR_ELEMENT_CORNER){
            if(sd.prim>(0xffffffffu-2u)/3u){svm_error(s,64u);return result;}indices=vec3<u32>(sd.prim*3u)+vec3<u32>(0,1,2);count=3u;
        }else{svm_error(s,16u);return result;}
    }
    var a=vec4<f32>(0);var b=vec4<f32>(0);var c=vec4<f32>(0);
    for(var k=0u;k<count;k++){
        let value=attribute_fetch(s,desc,indices[k]);
        if(k==0u){a=value;}else if(k==1u){b=value;}else{c=value;}
    }
    if(count==1u){result.value=a;return result;}
    result.value=((1.0-sd.u)-sd.v)*a+sd.u*b+sd.v*c;
    result.dx=sd.du.x*b+sd.dv.x*c-(sd.du.x+sd.dv.x)*a;
    result.dy=sd.du.y*b+sd.dv.y*c-(sd.du.y+sd.dv.y)*a;
    return result;
}
fn attribute_convert(value:vec4<f32>,kind:u32,output:u32,derivative:bool)->vec3<f32>{
    if(kind==NODE_ATTR_FLOAT){
        if(output==NODE_ATTR_OUTPUT_FLOAT_ALPHA){return vec3<f32>(select(1.0,0.0,derivative));}return vec3<f32>(value.x);
    }
    if(kind==NODE_ATTR_FLOAT2){
        if(output==NODE_ATTR_OUTPUT_FLOAT){return vec3<f32>(value.x);}
        if(output==NODE_ATTR_OUTPUT_FLOAT_ALPHA){return vec3<f32>(select(1.0,0.0,derivative));}return vec3<f32>(value.xy,0);
    }
    if(output==NODE_ATTR_OUTPUT_FLOAT_ALPHA){
        if(kind==NODE_ATTR_FLOAT4||kind==NODE_ATTR_RGBA){return vec3<f32>(value.w);}return vec3<f32>(select(1.0,0.0,derivative));
    }
    if(output==NODE_ATTR_OUTPUT_FLOAT){return vec3<f32>((value.x+value.y+value.z)*(1.0/3.0));}
    return value.xyz;
}
fn attribute_store(s:ptr<function,SvmState>,offset:u32,output:u32,value:vec3<f32>){
    if(output==NODE_ATTR_OUTPUT_FLOAT3){svm_store3(s,offset,value);}else{svm_store(s,offset,(value.x+value.y+value.z)*(1.0/3.0));}
}
fn svm_attribute(s:ptr<function,SvmState>,node:u32,derivative:bool){
    let id=load_word(node+N_Attr_attr);let output=svm_byte(node+N_Attr_output_type);
    let bump=svm_byte(node+N_Attr_bump_offset);let store=svm_byte(node+N_Attr_store_derivatives);
    if(output>NODE_ATTR_OUTPUT_FLOAT_ALPHA||(derivative&&(bump>NODE_BUMP_OFFSET_DY||store>1u))){svm_error(s,16u);return;}
    var desc=attribute_find(s,id,select(0u,0xffffffffu,(id&0x80000000u)!=0u));
    if(!desc.found){desc.kind=output;}
    var value:vec3<f32>;var dx:vec3<f32>;var dy:vec3<f32>;
    if(id==ATTR_STD_GENERATED&&!desc.found){
        let sd=(*s).geometry;value=sd.P;dx=sd.dPdu*sd.du.x+sd.dPdv*sd.dv.x;dy=sd.dPdu*sd.du.y+sd.dPdv*sd.dv.y;
        if(sd.object_id!=NONE){let object=table_at(G_objects,sd.object_id,SIZE_objects);
            value=surface_object_point(sd,value,true);dx=surface_object_direction(sd,dx,true);dy=surface_object_direction(sd,dy,true);}
    }else{
        let data=attribute_read(s,desc);value=attribute_convert(data.value,desc.kind,output,false);
        dx=attribute_convert(data.dx,desc.kind,output,true);dy=attribute_convert(data.dy,desc.kind,output,true);
    }
    if((*s).error!=0u){return;}
    if(derivative){let width=heap_float(node+N_Attr_bump_filter_width);
        if(bump==NODE_BUMP_OFFSET_DX){value+=dx*width;}else if(bump==NODE_BUMP_OFFSET_DY){value+=dy*width;}}
    let offset=svm_byte(node+N_Attr_out_offset);attribute_store(s,offset,output,value);
    if(derivative&&store!=0u){let stride=select(1u,3u,output==NODE_ATTR_OUTPUT_FLOAT3);attribute_store(s,offset+stride,output,dx);attribute_store(s,offset+2u*stride,output,dy);}
}
fn svm_vertex_color(s:ptr<function,SvmState>,node:u32,derivative:bool){
    let desc=attribute_find(s,svm_byte(node+N_VertexColor_layer_id),0u);
    let bump=svm_byte(node+N_VertexColor_bump_offset);
    if(derivative&&bump>NODE_BUMP_OFFSET_DY){svm_error(s,16u);return;}
    var color=vec3<f32>(0);var alpha=0.0;
    if(desc.found){
        if(desc.kind!=NODE_ATTR_FLOAT3&&desc.kind!=NODE_ATTR_FLOAT4&&desc.kind!=NODE_ATTR_RGBA){svm_error(s,16u);return;}
        var data=attribute_read(s,desc);
        if(derivative){let width=heap_float(node+N_VertexColor_bump_filter_width);
            if(bump==NODE_BUMP_OFFSET_DX){data.value+=data.dx*width;}else if(bump==NODE_BUMP_OFFSET_DY){data.value+=data.dy*width;}}
        color=data.value.xyz;alpha=select(1.0,data.value.w,desc.kind==NODE_ATTR_FLOAT4||desc.kind==NODE_ATTR_RGBA);
    }
    if((*s).error!=0u){return;}
    svm_optional_store3(s,svm_byte(node+N_VertexColor_color_offset),color);svm_optional_store(s,svm_byte(node+N_VertexColor_alpha_offset),alpha);
}
// SPDX-FileCopyrightText: 2011-2026 Blender Foundation
// SPDX-FileCopyrightText: 2011-2013 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Adapted from pinned Cycles kernel/device/cpu/image.h, kernel/util/image_2d.h,
// kernel/svm/image.h, kernel/camera/projection.h and util/math_float3.h.
// Resident native images use checked virtual addresses and software sampling.
struct ImageInfo {data:u32, kind:u32, width:i32, height:i32, interpolation:u32, extension:u32,}
struct ImageFraction {index:i32, weight:f32,}
fn image_fraction(x:f32)->ImageFraction{
    // Preserve the reference's negative-integer convention, including closest.
    let i=i32(x)-select(0,1,x<0.0);return ImageFraction(i,x-f32(i));
}
fn image_wrap(x:i32,width:i32,mode:u32)->i32{
    if(mode==EXTENSION_REPEAT){let m=x%width;return select(m,m+width,m<0);}
    if(mode==EXTENSION_EXTEND){return clamp(x,0,width-1);}
    if(mode==EXTENSION_MIRROR){let m=abs(x+select(0,1,x<0))%(2*width);return select(m,2*width-m-1,m>=width);}
    return x;
}
fn image_read(info:ImageInfo,x:i32,y:i32)->vec4<f32>{
    let ix=image_wrap(x,info.width,info.extension);let iy=image_wrap(y,info.height,info.extension);
    if(ix<0||ix>=info.width||iy<0||iy>=info.height){return vec4<f32>(0);}
    let pixel=u32(iy)*u32(info.width)+u32(ix);let kind=info.kind;
    if(kind==IMAGE_DATA_TYPE_FLOAT4){return heap_vec4(info.data+pixel*16u);}
    if(kind==IMAGE_DATA_TYPE_FLOAT){return vec4<f32>(heap_float(info.data+pixel*4u));}
    if(kind==IMAGE_DATA_TYPE_BYTE4){let bits=load_word(info.data+pixel*4u);return vec4<f32>(f32(bits&255u),f32((bits>>8u)&255u),f32((bits>>16u)&255u),f32(bits>>24u))*(1.0/255.0);}
    if(kind==IMAGE_DATA_TYPE_BYTE){return vec4<f32>(f32(svm_byte(info.data+pixel))*(1.0/255.0));}
    if(kind==IMAGE_DATA_TYPE_HALF){let a=info.data+pixel*2u;return vec4<f32>(unpack2x16float(load_word(a))[(a%4u)/2u]);}
    if(kind==IMAGE_DATA_TYPE_HALF4){let a=info.data+pixel*8u;return vec4<f32>(unpack2x16float(load_word(a)),unpack2x16float(load_word(a+4u)));}
    if(kind==IMAGE_DATA_TYPE_USHORT){return vec4<f32>(f32(load_half(info.data+pixel*2u))*(1.0/65535.0));}
    let a=info.data+pixel*8u;return vec4<f32>(f32(load_half(a)),f32(load_half(a+2u)),f32(load_half(a+4u)),f32(load_half(a+6u)))*(1.0/65535.0);
}
fn image_cubic_weights(t:f32)->vec4<f32>{
    return vec4<f32>((((-1.0/6.0)*t+0.5)*t-0.5)*t+(1.0/6.0),((0.5*t-1.0)*t)*t+(2.0/3.0),((-0.5*t+0.5)*t+0.5)*t+(1.0/6.0),(1.0/6.0)*t*t*t);
}
fn image_interpolate(info:ImageInfo,xy:vec2<f32>)->vec4<f32>{
    if(info.interpolation==INTERPOLATION_CLOSEST){return image_read(info,image_fraction(xy.x).index,image_fraction(xy.y).index);}
    let fx=image_fraction(xy.x-0.5);let fy=image_fraction(xy.y-0.5);let ix=fx.index;let iy=fy.index;let tx=fx.weight;let ty=fy.weight;
    if(info.interpolation==INTERPOLATION_LINEAR){
        return (1.0-ty)*(1.0-tx)*image_read(info,ix,iy)+(1.0-ty)*tx*image_read(info,ix+1,iy)+ty*(1.0-tx)*image_read(info,ix,iy+1)+ty*tx*image_read(info,ix+1,iy+1);
    }
    let u=image_cubic_weights(tx);let v=image_cubic_weights(ty);var result=vec4<f32>(0);
    for(var j=0;j<4;j++){
        let y=iy+j-1;
        result+=v[u32(j)]*(u.x*image_read(info,ix-1,y)+u.y*image_read(info,ix,y)+u.z*image_read(info,ix+1,y)+u.w*image_read(info,ix+2,y));
    }
    return result;
}
fn image_sample(s:ptr<function,SvmState>,input_id:i32,input_uv:vec2<f32>)->vec4<f32>{
    var id=input_id;var uv=input_uv;
    if(!all(abs(uv)<vec2<f32>(1.0e9))){svm_error(s,128u);return vec4<f32>(0);}
    if(id<0){
        let tx=i32(uv.x);let ty=i32(uv.y);if(tx<0||ty<0||tx>=10){return vec4<f32>(1,0,1,1);}
        // IDs are encoded as -tableOffset-1; unsigned arithmetic avoids INT_MIN overflow.
        let at=0u-bitcast<u32>(id)-1u;let count=table_count(G_image_texture_udims);
        if(at>=count||ty>(2147483647-1010)/10){svm_error(s,128u);return vec4<f32>(0);}
        let address=table_at(G_image_texture_udims,at,SIZE_image_texture_udims);let n=load_word(address+IU_tile);
        if(n>count-at-1u){svm_error(s,128u);return vec4<f32>(0);}
        id=i32(KERNEL_IMAGE_NONE);let tile=1001+10*ty+tx;
        for(var i=0u;i<n;i++){
            let entry=table_at(G_image_texture_udims,at+1u+i,SIZE_image_texture_udims);
            if(bitcast<i32>(load_word(entry+IU_tile))==tile){id=bitcast<i32>(load_word(entry+IU_image_texture_id));uv-=vec2<f32>(f32(tx),f32(ty));break;}
        }
    }
    if(id==i32(KERNEL_IMAGE_NONE)){return vec4<f32>(1,0,1,1);}
    if(id<0||u32(id)>=table_count(G_image_textures)){svm_error(s,128u);return vec4<f32>(0);}
    let tex=table_at(G_image_textures,u32(id),SIZE_image_textures);
    if(load_word(tex+IT_tile_descriptor_offset)!=KERNEL_TILE_LOAD_NONE){svm_error(s,256u);return vec4<f32>(0);}
    if(load_word(tex+IT_use_transform_3d)!=0u){svm_error(s,16u);return vec4<f32>(0);}
    let info_id=load_word(tex+IT_image_info_id);if(info_id==KERNEL_IMAGE_NONE){return vec4<f32>(1,0,1,1);}
    if(info_id>=table_count(G_image_info)||info_id>=table_count(G_image_bounds)){svm_error(s,128u);return vec4<f32>(0);}
    let a=table_at(G_image_info,info_id,SIZE_image_info);let bounds=table_at(G_image_bounds,info_id,SIZE_image_bounds);
    var info:ImageInfo;info.data=load_word(a+II_data);info.kind=load_word(a+II_data_type);
    if(load_word(a+II_data+4u)!=0u){svm_error(s,128u);return vec4<f32>(0);}
    if(info.data==0u){return vec4<f32>(0);}
    info.width=bitcast<i32>(load_word(a+II_width));info.height=bitcast<i32>(load_word(a+II_height));
    info.interpolation=load_word(a+II_interpolation);info.extension=load_word(a+II_extension);
    if(info.kind>IMAGE_DATA_TYPE_USHORT||info.width<=0||info.height<=0||info.width>1073741823||info.height>1073741823||info.interpolation>=INTERPOLATION_NUM_TYPES||info.extension>=EXTENSION_NUM_TYPES){svm_error(s,128u);return vec4<f32>(0);}
    var pixel_bytes=4u;
    switch(info.kind){case IMAGE_DATA_TYPE_FLOAT4:{pixel_bytes=16u;}case IMAGE_DATA_TYPE_HALF4,IMAGE_DATA_TYPE_USHORT4:{pixel_bytes=8u;}case IMAGE_DATA_TYPE_BYTE:{pixel_bytes=1u;}case IMAGE_DATA_TYPE_HALF,IMAGE_DATA_TYPE_USHORT:{pixel_bytes=2u;}default:{}}
    let bytes=load_word(bounds+4u);
    if(info.data!=load_word(bounds)||bytes/pixel_bytes/u32(info.width)<u32(info.height)){svm_error(s,128u);return vec4<f32>(0);}
    let xy=uv*vec2<f32>(f32(info.width),f32(info.height));
    if(!all(abs(xy)<vec2<f32>(2147483000.0))){svm_error(s,128u);return vec4<f32>(0);}
    var result=image_interpolate(info,xy);
    if(info.kind==IMAGE_DATA_TYPE_FLOAT||info.kind==IMAGE_DATA_TYPE_HALF||info.kind==IMAGE_DATA_TYPE_BYTE||info.kind==IMAGE_DATA_TYPE_USHORT){result.w=1.0;}
    return result;
}
fn svm_image_sample(s:ptr<function,SvmState>,id:i32,uv:vec2<f32>,flags:u32)->vec4<f32>{
    if((flags&~(NODE_IMAGE_ALPHA_UNASSOCIATE|NODE_IMAGE_COMPRESS_AS_SRGB))!=0u){svm_error(s,16u);return vec4<f32>(0);}
    var result=image_sample(s,id,uv);let alpha=result.w;
    if((flags&NODE_IMAGE_ALPHA_UNASSOCIATE)!=0u&&alpha!=1.0&&alpha!=0.0){result/=alpha;result.w=alpha;}
    if((flags&NODE_IMAGE_COMPRESS_AS_SRGB)!=0u){result=vec4<f32>(attribute_srgb(result.x),attribute_srgb(result.y),attribute_srgb(result.z),alpha);}
    return result;
}
fn image_projection(s:ptr<function,SvmState>,co:vec3<f32>,projection:u32)->vec2<f32>{
    if(projection==NODE_IMAGE_PROJ_FLAT){return co.xy;}
    let v=(co-vec3<f32>(0.5))*2.0;
    if(projection==NODE_IMAGE_PROJ_SPHERE){
        let l=dot(v,v);if(l<=0.0){return vec2<f32>(0);}
        var u=0.0;if(v.x!=0.0||v.y!=0.0){u=0.5-atan2(v.x,v.y)*0.15915494;}
        return vec2<f32>(u,1.0-c_safe_acosf(v.z/sqrt(l))*0.31830987);
    }
    if(projection==NODE_IMAGE_PROJ_TUBE){let l=sqrt(v.x*v.x+v.y*v.y);if(l>0.0){return vec2<f32>((1.0-atan2(v.x/l,v.y/l)/3.1415927)*0.5,(v.z+1.0)*0.5);}return vec2<f32>(0);}
    svm_error(s,16u);return vec2<f32>(0);
}
fn environment_projection(s:ptr<function,SvmState>,input:vec3<f32>,projection:u32)->vec2<f32>{
    var v=surface_normalize(input);
    if(projection==0u){if(all(v==vec3<f32>(0))){return vec2<f32>(0);}return vec2<f32>((atan2(v.y,v.x)-3.1415927)/-6.2831855,(c_safe_acosf(v.z/length(v))-3.1415927)/-3.1415927);}
    if(projection!=1u){svm_error(s,16u);return vec2<f32>(0);}
    v.y-=1.0;let div=2.0*sqrt(max(-0.5*v.y,0.0));if(div>0.0){v/=div;}return (v.xz+vec2<f32>(1))*0.5;
}
fn image_box(s:ptr<function,SvmState>,co:vec3<f32>,id:i32,blend:f32,flags:u32)->vec4<f32>{
    var n=(*s).geometry.N;let object=(*s).geometry.object_id;
    if(object!=NONE){n=surface_object_normal((*s).geometry,n,true);}
    let signed_n=n;n=abs(n);n/=(n.x+n.y+n.z);let limit=0.5*(1.0+blend);var weight=vec3<f32>(0);
    if(n.x>limit*(n.x+n.y)&&n.x>limit*(n.x+n.z)){weight.x=1.0;}
    else if(n.y>limit*(n.x+n.y)&&n.y>limit*(n.y+n.z)){weight.y=1.0;}
    else if(n.z>limit*(n.x+n.z)&&n.z>limit*(n.y+n.z)){weight.z=1.0;}
    else if(blend>0.0){
        if(n.z<(1.0-limit)*(n.y+n.x)){weight.x=clamp((n.x/(n.x+n.y)-0.5*(1.0-blend))/blend,0.0,1.0);weight.y=1.0-weight.x;}
        else if(n.x<(1.0-limit)*(n.y+n.z)){weight.y=clamp((n.y/(n.y+n.z)-0.5*(1.0-blend))/blend,0.0,1.0);weight.z=1.0-weight.y;}
        else if(n.y<(1.0-limit)*(n.x+n.z)){weight.x=clamp((n.x/(n.x+n.z)-0.5*(1.0-blend))/blend,0.0,1.0);weight.z=1.0-weight.x;}
        else{weight=((2.0-limit)*n+vec3<f32>(limit-1.0))/(2.0*limit-1.0);}
    }else{weight.x=1.0;}
    // One svm_image_sample call site (drivers inline each call); the X, Y, Z
    // projections are still accumulated in the original order.
    var result=vec4<f32>(0);
    for(var axis=0u;axis<3u;axis++){
        if(!(weight[axis]>0.0)){continue;}
        var uv=vec2<f32>(select(co.y,1.0-co.y,signed_n.x<0.0),co.z);
        if(axis==1u){uv=vec2<f32>(select(co.x,1.0-co.x,signed_n.y>0.0),co.z);}
        else if(axis==2u){uv=vec2<f32>(select(co.y,1.0-co.y,signed_n.z>0.0),co.x);}
        result+=weight[axis]*svm_image_sample(s,id,uv,flags);
    }
    return result;
}
fn svm_image(s:ptr<function,SvmState>,node:u32,mode:u32,derivative:bool){
    // These native records have matching offsets, checked by the layout generator.
    let offset=svm_byte(node+N_TexImage_co);let co=svm_load3(s,offset);
    if(derivative){let dx=svm_load3(s,offset+3u);let dy=svm_load3(s,offset+6u);
        if(!all(abs(dx)<=vec3<f32>(3.402823e38))||!all(abs(dy)<=vec3<f32>(3.402823e38))){svm_error(s,128u);}}
    if((*s).error!=0u){return;}
    let id=bitcast<i32>(load_word(node+N_TexImage_id));let flags=svm_byte(node+N_TexImage_flags);var result:vec4<f32>;
    if(mode==1u){result=image_box(s,co,id,heap_float(node+N_TexImageBox_blend),flags);}
    else{var uv:vec2<f32>;let projection=load_word(node+N_TexImage_projection);
        if(mode==2u){uv=environment_projection(s,co,projection);}else{uv=image_projection(s,co,projection);}
        if((*s).error==0u){result=svm_image_sample(s,id,uv,flags);}}
    if((*s).error!=0u){return;}
    svm_optional_store3(s,svm_byte(node+N_TexImage_out_offset),result.xyz);svm_optional_store(s,svm_byte(node+N_TexImage_alpha_offset),result.w);
}
// SPDX-FileCopyrightText: 2011-2025 Blender Foundation
// SPDX-FileCopyrightText: 2011-2013 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles svm/tex_coord.h, geom/triangle.h, geom/object.h,
// svm/util.h and util/math_dual.h. Native payload layouts, checked attribute
// loads and explicit dual vectors replace C++ pointers and templates.
struct NormalDual {value:vec3<f32>, dx:vec3<f32>, dy:vec3<f32>,}
fn normal_transpose(address:u32,v:vec3<f32>)->vec3<f32>{
    return v.x*heap_vec3(address)+v.y*heap_vec3(address+16u)+v.z*heap_vec3(address+32u);
}
fn normal_object_transpose(sd:SurfaceData,v:vec3<f32>,inverse:bool)->vec3<f32>{
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){if(inverse){return motion_transpose(sd.motion_itfm,v);}return motion_transpose(sd.motion_tfm,v);}
    let object=table_at(G_objects,sd.object_id,SIZE_objects);return normal_transpose(object+select(O_tfm,O_itfm,inverse),v);
}
fn normal_attribute(s:ptr<function,SvmState>,id:u32)->AttributeDesc{
    return attribute_find(s,id,select(0u,0xffffffffu,(id&0x80000000u)!=0u));
}
fn normal_finite(v:vec3<f32>)->bool{
    return all((bitcast<vec3<u32>>(v)&vec3<u32>(0x7f800000u))!=vec3<u32>(0x7f800000u));
}
fn normal_smooth_object(s:ptr<function,SvmState>,object:u32)->vec3<f32>{
    let sd=(*s).geometry;var indices:vec3<u32>;
    if((sd.object_flag&SD_OBJECT_HAS_CORNER_NORMALS)!=0u){
        if(sd.prim>(0xffffffffu-2u)/3u){svm_error(s,64u);return sd.N;}
        indices=vec3<u32>(3u*sd.prim)+vec3<u32>(0,1,2);
    }else{
        if(sd.prim>=table_count(G_tri_vindex)){svm_error(s,64u);return sd.N;}
        let at=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);
        indices=vec3<u32>(load_word(at),load_word(at+4u),load_word(at+8u));
    }
    let desc=AttributeDesc(ATTR_ELEMENT_VERTEX_NORMAL,NODE_ATTR_FLOAT3,bitcast<i32>(load_word(object+O_normal_offset)),true);
    var n:array<vec3<f32>,3>;
    for(var j=0u;j<3u;j++){
        n[j]=attribute_fetch(s,desc,indices[j]).xyz;
        if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u){n[j]=surface_normalize(normal_object_transpose(sd,n[j],false));}
    }
    let result=surface_normalize(((1.0-sd.u)-sd.v)*n[0]+sd.u*n[1]+sd.v*n[2]);
    return select(result,sd.Ng,all(result==vec3<f32>(0)));
}
fn svm_normal_map(s:ptr<function,SvmState>,node:u32){
    let space=load_word(node+N_NormalMap_space);let invert=load_word(node+N_NormalMap_invert_green);
    let original=load_word(node+N_NormalMap_use_original_base);let out=svm_byte(node+N_NormalMap_normal_offset);
    if(space>NODE_NORMAL_MAP_BLENDER_WORLD||invert>1u||original>1u){svm_error(s,16u);return;}
    var color=2.0*(svm_input3(s,node+N_NormalMap_color)-vec3<f32>(0.5));
    var strength=svm_input(s,node+N_NormalMap_strength);
    if(invert!=0u){color.y=-color.y;}
    let sd=(*s).geometry;let backface=(sd.runtime_flag&SR_BACKFACING)!=0u;
    var object=0u;
    if(sd.object_id!=NONE){
        if(sd.object_id>=table_count(G_objects)){svm_error(s,64u);return;}
        object=table_at(G_objects,sd.object_id,SIZE_objects);
    }
    var N:vec3<f32>;var linear=false;var invalid_normal=false;
    if(space==NODE_NORMAL_MAP_TANGENT){
        if(sd.object_id==NONE||(sd.primitive_type&PRIMITIVE_TRIANGLE)==0u){svm_store3(s,out,sd.N);return;}
        let attr=normal_attribute(s,load_word(node+N_NormalMap_attr));
        let sign_attr=normal_attribute(s,load_word(node+N_NormalMap_attr_sign));
        if(!attr.found||!sign_attr.found){svm_store3(s,out,sd.N);return;}
        if(attr.kind!=NODE_ATTR_FLOAT3||sign_attr.kind!=NODE_ATTR_FLOAT){svm_error(s,16u);return;}
        let tangent=attribute_read(s,attr).value.xyz;let sign=attribute_read(s,sign_attr).value.x;
        var normal:vec3<f32>;
        if((sd.shader&SHADER_SMOOTH_NORMAL)!=0u){
            var undisplaced:AttributeDesc;
            if(original!=0u){undisplaced=attribute_find(s,ATTR_STD_NORMAL_UNDISPLACED,0u);}
            if(undisplaced.found){
                if(undisplaced.kind!=NODE_ATTR_FLOAT3){svm_error(s,16u);return;}
                normal=attribute_read(s,undisplaced).value.xyz;linear=true;
            }else{normal=normal_smooth_object(s,object);}
        }else{
            normal=select(sd.Ng,-sd.Ng,backface);
            normal=surface_normalize(normal_object_transpose(sd,normal,false));
        }
        if(!linear){color.x*=strength;color.y*=strength;color.z=1.0+clamp(strength,0.0,1.0)*(color.z-1.0);}
        let B=sign*cross(normal,tangent);
        N=surface_normalize(color.x*tangent+color.y*B+color.z*normal);
        invalid_normal=all(N==vec3<f32>(0));
        N=surface_normalize(normal_object_transpose(sd,N,true));
    }else{
        linear=true;
        if(space==NODE_NORMAL_MAP_BLENDER_OBJECT||space==NODE_NORMAL_MAP_BLENDER_WORLD){color.y=-color.y;color.z=-color.z;}
        N=color;
        if(space==NODE_NORMAL_MAP_OBJECT||space==NODE_NORMAL_MAP_BLENDER_OBJECT){
            if(sd.object_id!=NONE){invalid_normal=all(N==vec3<f32>(0));N=surface_normalize(normal_object_transpose(sd,N,true));}
        }else{N=surface_normalize(N);}
    }
    if(backface){N=-N;}
    if(linear&&strength!=1.0){strength=max(strength,0.0);N=surface_normalize(sd.N+(N-sd.N)*strength);}
    if(invalid_normal||all(N==vec3<f32>(0))||!normal_finite(N)){N=sd.N;}
    svm_store3(s,out,N);
}
fn normal_dual_normalize(a:NormalDual)->NormalDual{
    // Cycles dual inversesqrt returns zero for nonpositive squared length.
    let len2=dot(a.value,a.value);var inv=0.0;var slope=0.0;
    if(len2>0.0){inv=1.0/sqrt(len2);slope=-0.5*(inv/len2);}
    let dx=dot(a.value,a.dx)+dot(a.dx,a.value);let dy=dot(a.value,a.dy)+dot(a.dy,a.value);
    return NormalDual(a.value*inv,a.value*(dx*slope)+a.dx*inv,a.value*(dy*slope)+a.dy*inv);
}
fn normal_radial(v:vec3<f32>,axis:u32,center:f32)->vec3<f32>{
    if(axis==NODE_TANGENT_AXIS_X){return vec3<f32>(0,-(v.z-center),v.y-center);}
    if(axis==NODE_TANGENT_AXIS_Y){return vec3<f32>(-(v.z-center),0,v.x-center);}
    return vec3<f32>(-(v.y-center),v.x-center,0);
}
fn svm_tangent(s:ptr<function,SvmState>,node:u32,derivative:bool){
    let kind=load_word(node+N_Tangent_direction_type);let axis=load_word(node+N_Tangent_axis);
    if(kind>NODE_TANGENT_UVMAP||axis>NODE_TANGENT_AXIS_Z){svm_error(s,16u);return;}
    let sd=(*s).geometry;let out=svm_byte(node+N_Tangent_tangent_offset);
    let desc=normal_attribute(s,load_word(node+N_Tangent_attr));var tangent:NormalDual;
    if(kind==NODE_TANGENT_UVMAP&&!desc.found){
        svm_store3(s,out,vec3<f32>(0));
        if(derivative){svm_store3(s,out+3u,vec3<f32>(0));svm_store3(s,out+6u,vec3<f32>(0));}return;
    }
    if(desc.found){
        if(desc.kind!=NODE_ATTR_FLOAT2&&desc.kind!=NODE_ATTR_FLOAT3){svm_error(s,16u);return;}
        let data=attribute_read(s,desc);tangent=NormalDual(data.value.xyz,data.dx.xyz,data.dy.xyz);
    }else{tangent=NormalDual(sd.P,sd.dPdu*sd.du.x+sd.dPdv*sd.dv.x,sd.dPdu*sd.du.y+sd.dPdv*sd.dv.y);}
    if(kind==NODE_TANGENT_RADIAL){tangent=NormalDual(normal_radial(tangent.value,axis,0.5),normal_radial(tangent.dx,axis,0.0),normal_radial(tangent.dy,axis,0.0));}
    if((*s).error!=0u){return;}
    if(sd.object_id!=NONE){
        let object=table_at(G_objects,sd.object_id,SIZE_objects);
        tangent=NormalDual(normal_object_transpose(sd,tangent.value,true),normal_object_transpose(sd,tangent.dx,true),normal_object_transpose(sd,tangent.dy,true));
        if(derivative){tangent=normal_dual_normalize(tangent);}else{tangent.value=surface_normalize(tangent.value);}
    }
    tangent=NormalDual(cross(tangent.value,sd.N),cross(tangent.dx,sd.N),cross(tangent.dy,sd.N));
    if(derivative){tangent=normal_dual_normalize(tangent);}else{
        // The plain native path produces nonfinite values at a degenerate frame.
        // Reject that case explicitly rather than returning a fabricated tangent.
        if(dot(tangent.value,tangent.value)==0.0){svm_error(s,16u);return;}
        tangent.value=surface_normalize(tangent.value);
    }
    svm_store3(s,out,cross(sd.N,tangent.value));
    if(derivative){svm_store3(s,out+3u,cross(sd.N,tangent.dx));svm_store3(s,out+6u,cross(sd.N,tangent.dy));}
}
// Copyright (c) 2026 Jeremy Ellis and contributors
// Copyright (c) 2018-2024, Arm Limited.
// SPDX-License-Identifier: Apache-2.0 AND MIT
// Bounded adaptation of Arm Optimized Routines v25.01 sincosf polynomials.
// Source pin: third_party/arm-math-import.json; selected MIT grant:
// third_party/arm-optimized-routines/LICENSE.
// Changes: bounded disk-angle reduction and two-component Horner evaluation
// using original CodeBrix float helpers; no binary64 requirement. Matches the
// observed scalar platform sinf/cosf polynomial, not correctly rounded libm.
// Inputs are binary32 disk/area sampling angles in [-pi,pi].
fn native_disk_sincos(angle:f32)->vec2<f32>{
    if(angle==0.0){return vec2<f32>(angle,1);}
    var n=0;
    if(angle>=0.7853981633974483){n=1;}
    if(angle>=2.356194490192345){n=2;}
    if(angle<=-0.7853981633974483){n=-1;}
    if(angle<=-2.356194490192345){n=-2;}
    var x=vec2<f32>(angle,0);
    if(n!=0){x=vec2<f32>(rounded_add(angle,-f32(n)*1.5707963705062866),f32(n)*4.3711390001862426e-8);}
    var sine=rounded_add(x.x,x.y);var cosine=1.0;
    if(abs(x.x)>=0.000244140625){
      let z=trig_mul(x,x);
      var sn=trig_coeff(vec2<f32>(0.008332177996635437,1.4950340965214082e-10),trig_mul(z,vec2<f32>(-0.0001951729936990887,3.885231546207901e-12)));
      sn=trig_coeff(vec2<f32>(-0.16666655242443085,2.9874200890844804e-09),trig_mul(z,sn));
      sn=trig_coeff(vec2<f32>(1,0),trig_mul(z,sn));
      sn=trig_mul(x,sn);
      var cs=trig_coeff(vec2<f32>(-0.0013886763481423259,-3.129527886835959e-11),trig_mul(z,vec2<f32>(2.4390450562350452e-05,1.4121409391224665e-13)));
      cs=trig_coeff(vec2<f32>(0.04166662320494652,1.1939799149374153e-10),trig_mul(z,cs));
      cs=trig_coeff(vec2<f32>(-0.5,2.7489177600870107e-09),trig_mul(z,cs));
      cs=trig_coeff(vec2<f32>(1.0,0.0),trig_mul(z,cs));
      sine=rounded_add(sn.x,sn.y);cosine=rounded_add(cs.x,cs.y);
    }
    if(n==1){return vec2<f32>(cosine,-sine);}
    if(n==2||n== -2){return vec2<f32>(-sine,-cosine);}
    if(n== -1){return vec2<f32>(-cosine,sine);}
    return vec2<f32>(sine,cosine);
}

// Bounded HG azimuth adapter, 0 <= angle <= 2*pi. The same reviewed Arm
// polynomial is used, with two-component reduction covering all four quadrants.
// trig_product retains the rounding residual of 3*(pi/2), unlike a single float.
fn native_phase_sincos(angle:f32)->vec2<f32>{
    if(angle==0.0){return vec2<f32>(angle,1);}
    let n=i32(floor(angle*0.6366197723675814+0.5));
    var x=vec2<f32>(angle,0);
    if(n!=0){let product=trig_product(f32(n),1.5707963705062866);
        x=vec2<f32>(rounded_add(angle,-product.x),rounded_add(-product.y,f32(n)*4.3711390001862426e-8));}
    var sine=rounded_add(x.x,x.y);var cosine=1.0;
    if(abs(x.x)>=0.000244140625){
      let z=trig_mul(x,x);
      var sn=trig_coeff(vec2<f32>(0.008332177996635437,1.4950340965214082e-10),trig_mul(z,vec2<f32>(-0.0001951729936990887,3.885231546207901e-12)));
      sn=trig_coeff(vec2<f32>(-0.16666655242443085,2.9874200890844804e-09),trig_mul(z,sn));
      sn=trig_coeff(vec2<f32>(1,0),trig_mul(z,sn));
      sn=trig_mul(x,sn);
      var cs=trig_coeff(vec2<f32>(-0.0013886763481423259,-3.129527886835959e-11),trig_mul(z,vec2<f32>(2.4390450562350452e-05,1.4121409391224665e-13)));
      cs=trig_coeff(vec2<f32>(0.04166662320494652,1.1939799149374153e-10),trig_mul(z,cs));
      cs=trig_coeff(vec2<f32>(-0.5,2.7489177600870107e-09),trig_mul(z,cs));
      cs=trig_coeff(vec2<f32>(1.0,0.0),trig_mul(z,cs));
      sine=rounded_add(sn.x,sn.y);cosine=rounded_add(cs.x,cs.y);
    }
    if((n&3)==1){return vec2<f32>(cosine,-sine);}
    if((n&3)==2){return vec2<f32>(-sine,-cosine);}
    if((n&3)==3){return vec2<f32>(-cosine,sine);}
    return vec2<f32>(sine,cosine);
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native spot attenuation and area rectangle/ellipse sampling and evaluation.
// Adapted from the reviewed Cycles kernel/light/{spot,area,common}.h snapshot.
const EMITTER_PI:f32=3.141592653589793;
const EMITTER_INV_PI:f32=0.3183098861837907;
fn emitter_normalize_len(v:vec3<f32>)->vec4<f32>{
    let t=rounded_sqrt(rounded_dot(v,v));if(t==0.0){return vec4<f32>(0);}
    return vec4<f32>(rounded_divide(v.x,t),rounded_divide(v.y,t),rounded_divide(v.z,t),t);
}
fn emitter_sphere_hit(origin:vec3<f32>,direction:vec3<f32>,position:vec3<f32>,radius:f32)->f32{
    let delta=position-origin;let d2=dot(delta,delta);let r2=radius*radius;let cosine=dot(delta,direction);
    if(d2>r2&&cosine<0.0){return -1.0;}let perpendicular=delta-cosine*direction;let s2=dot(perpendicular,perpendicular);
    if(s2>r2){return -1.0;}let root=sqrt(max(r2-s2,0.0));let t=cosine-select(-root,root,d2-r2>=0.0);
    return select(-1.0,t,t>0.0&&t<bitcast<f32>(0x7f7fffffu));
}
fn spot_attenuation(light:u32,direction:vec3<f32>)->f32{
    let object=table_at(G_objects,load_word(light+L_object_id),SIZE_objects);let a=object+O_itfm;
    let x=heap_vec4(a);let y=heap_vec4(a+16u);let z=heap_vec4(a+32u);
    let transformed=rounded_normalize(vec3<f32>(dot(x.xyz,direction),dot(y.xyz,direction),dot(z.xyz,direction)));
    let f=(-transformed.z-heap_float(light+L_spot_cos))*heap_float(light+L_spot_smooth);
    if(f<=0.0){return 0.0;}if(f>=1.0){return 1.0;}let ff=f*f;return 3.0*ff-2.0*ff*f;
}
fn emitter_disk(random:vec2<f32>)->vec2<f32>{
    let a=rounded_add(2.0*random.x,-1.0);let b=rounded_add(2.0*random.y,-1.0);if(a==0.0&&b==0.0){return vec2<f32>(0);}
    var radius:f32;var phi:f32;if(a*a>b*b){radius=a;phi=(EMITTER_PI*0.25)*rounded_divide(b,a);}
    else{radius=b;phi=rounded_add(EMITTER_PI*0.5,-(EMITTER_PI*0.25)*rounded_divide(a,b));}
    let sc=native_disk_sincos(phi);return radius*vec2<f32>(sc.y,sc.x);
}
// Original quarter-turn rotation retains the float32 cosine of pi/2.
fn emitter_quarter_turn(p:vec3<f32>,axis:vec3<f32>)->vec3<f32>{
    let c= -4.371138828673793e-8;let s=1.0;let m=1.0-c;
    return vec3<f32>((c+m*axis.x*axis.x)*p.x+(m*axis.x*axis.y-axis.z*s)*p.y+(m*axis.x*axis.z+axis.y*s)*p.z,
      (m*axis.x*axis.y+axis.z*s)*p.x+(c+m*axis.y*axis.y)*p.y+(m*axis.y*axis.z-axis.x*s)*p.z,
      (m*axis.x*axis.z-axis.y*s)*p.x+(m*axis.y*axis.z+axis.x*s)*p.y+(c+m*axis.z*axis.z)*p.z);
}
// Original bounded arcsine adapter. The binomial Taylor series is evaluated
// with paired binary32 values after reducing its argument to [0, 1/2].
// Seventeen terms bound the truncation tail below 2e-13.
fn emitter_asin(value:f32)->f32{
    let a=abs(clamp(value,-1.0,1.0));let reduce=a>0.5;var z=vec2<f32>(a,0);var x=trig_product(a,a);
    if(reduce){x=vec2<f32>((1.0-a)*0.5,0);let root=rounded_sqrt(x.x);z=vec2<f32>(root,0);
      if(root>0.0){let product=trig_product(root,root);z.y=rounded_divide(rounded_add(rounded_add(x.x,-product.x),-product.y),2.0*root);}}
    var polynomial=vec2<f32>(0.0042409072630107403,-1.6933137720281428e-10);
    polynomial=trig_coeff(vec2<f32>(0.0046601435169577599,-3.00426636972735e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.005153309553861618,1.284582861538591e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0057400376535952091,1.7246714344731083e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0064472104422748089,-1.30385160446167e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0073125259950757027,-1.2147685755854067e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0083903353661298752,4.4348694029308498e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0097616091370582581,3.9213582089072777e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.011551801115274429,-2.1913472343893612e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.013964843936264515,-1.8626451492309571e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.017352763563394547,8.5968237656813406e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.022372158244252205,8.4665688601407135e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0303819440305233,4.1392114427354598e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0446428582072258,-1.064368656703404e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.075000002980232239,-2.9802322387695314e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.1666666716337204,-4.9670537312825518e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(1,0),trig_mul(polynomial,x));
    var result=trig_mul(z,polynomial);if(reduce){result=trig_coeff(vec2<f32>(1.5707963705062866,-4.3711390001862426e-8),-2.0*result);}
    let answer=rounded_add(result.x,result.y);return select(answer,-answer,value<0.0);
}
struct AreaRegion {P:vec3<f32>,U:vec3<f32>,V:vec3<f32>,len_u:f32,len_v:f32,rectangle:bool,valid:bool}
fn area_region(light:u32,P:vec3<f32>)->AreaRegion{
    var r=AreaRegion(heap_vec3(light+L_co),heap_vec3(light+L_area_axis_u),heap_vec3(light+L_area_axis_v),heap_float(light+L_area_len_u),heap_float(light+L_area_len_v),heap_float(light+L_area_invarea)>0.0,true);
    if(heap_float(light+L_area_normalize_spread)<=0.0){return r;}
    let N=heap_vec3(light+L_area_dir);let t=dot(N,P-r.P);let closest=P-t*N;
    let spread=t*heap_float(light+L_area_tan_half_spread);let su=dot(r.U,closest-r.P);let sv=dot(r.V,closest-r.P);
    let round= !r.rectangle&&r.len_u==r.len_v;var sample_spread=spread==0.0;
    if(round&&!sample_spread){
        let distance=length(vec2<f32>(su,sv));let radius=r.len_u*0.5;
        if(distance>=radius+spread){r.valid=false;return r;}
        sample_spread=distance<=abs(radius-spread)&&spread<radius;
        if(distance>abs(radius-spread)){
            let len_u=radius+spread-distance;var len_v=2.0*min(radius,spread);
            if(abs(radius*radius-spread*spread)<distance*distance){let x=distance+(spread*spread-radius*radius)/distance;len_v=sqrt(max(4.0*spread*spread-x*x,0.0));}
            let rectangle_area=len_u*len_v;let circle_area=EMITTER_PI*radius*radius;let spread_area=EMITTER_PI*spread*spread;
            if(rectangle_area<min(circle_area,spread_area)){
                r.rectangle=true;r.U=rounded_normalize(r.P-closest);r.V=emitter_quarter_turn(r.U,N);r.len_u=len_u;r.len_v=len_v;
                r.P=0.5*(r.P+closest+r.U*(spread-radius));return r;
            }sample_spread=spread_area<circle_area;
        }
    }else if(!round&&!sample_spread){
        let min_u=max(su-spread,-r.len_u*0.5);let max_u=min(su+spread,r.len_u*0.5);
        let min_v=max(sv-spread,-r.len_v*0.5);let max_v=min(sv+spread,r.len_v*0.5);
        if(min_u>=max_u||min_v>=max_v){r.valid=false;return r;}
        let lu=max_u-min_u;let lv=max_v-min_v;let rectangle_area=lu*lv;
        let ellipse_area=select((EMITTER_PI*0.25)*r.len_u*r.len_v,bitcast<f32>(0x7f7fffffu),r.rectangle);let spread_area=EMITTER_PI*spread*spread;
        if(r.rectangle||rectangle_area<min(ellipse_area,spread_area)){
            r.rectangle=true;r.len_u=lu;r.len_v=lv;r.P+=r.U*(0.5*(min_u+max_u))+r.V*(0.5*(min_v+max_v));return r;
        }r.rectangle=false;sample_spread=spread_area<ellipse_area;
    }
    if(sample_spread){r.rectangle=false;r.P+=r.U*su+r.V*sv;r.len_u=2.0*spread;r.len_v=2.0*spread;}
    return r;
}
struct AreaRect {P:vec3<f32>,pdf:f32}
fn area_rectangle(P:vec3<f32>,r:AreaRegion,random:vec2<f32>,sample_coord:bool)->AreaRect{
    var out=AreaRect(r.P,0.0);var Z=rounded_cross(r.U,r.V);let direction=r.P-P;var z0=rounded_dot(direction,Z);if(z0>0.0){Z= -Z;z0= -z0;}
    let xc=rounded_dot(direction,r.U);let yc=rounded_dot(direction,r.V);let x0=rounded_add(xc,-0.5*r.len_u);let x1=rounded_add(xc,0.5*r.len_u);let y0=rounded_add(yc,-0.5*r.len_v);let y1=rounded_add(yc,0.5*r.len_v);
    var nz=vec4<f32>(-y0,x1,y1,-x0);for(var i=0u;i<4u;i++){nz[i]=rounded_divide(nz[i],rounded_sqrt(rounded_add(nz[i]*nz[i],z0*z0)));}
    let g0=emitter_asin(-nz.x*nz.y);let g1=emitter_asin(-nz.y*nz.z);let g2=emitter_asin(-nz.z*nz.w);let g3=emitter_asin(-nz.w*nz.x);
    let S= -rounded_add(rounded_add(rounded_add(g0,g1),g2),g3);
    if(sample_coord){
        let b0=nz.x;let b1=nz.z;let au=rounded_add(rounded_add(random.x*S,g2),g3);let sc=native_disk_sincos(au);var fu=0.0;if(sc.x!=0.0){fu=rounded_divide(rounded_add(sc.y*b0,b1),sc.x);}
        let inverse=rounded_divide(1.0,rounded_sqrt(rounded_add(fu*fu,b0*b0)));let cu=clamp(bitcast<f32>((bitcast<u32>(inverse)&0x7fffffffu)|(bitcast<u32>(fu)&0x80000000u)),-1.0,1.0);
        let xu=clamp(rounded_divide(-(cu*z0),max(rounded_sqrt(max(rounded_add(1.0,-cu*cu),0.0)),1e-7)),x0,x1);let d2=rounded_add(xu*xu,z0*z0);
        let h0=rounded_divide(y0,rounded_sqrt(rounded_add(d2,y0*y0)));let h1=rounded_divide(y1,rounded_sqrt(rounded_add(d2,y1*y1)));let hv=rounded_add(h0,random.y*rounded_add(h1,-h0));let hv2=hv*hv;
        var yv=y1;if(hv2<1.0-1e-6){yv=hv*rounded_sqrt(rounded_divide(d2,rounded_add(1.0,-hv2)));}out.P=rounded_add3(rounded_add3(rounded_add3(P,xu*r.U),yv*r.V),z0*Z);
    }
    if(S<1e-5||min(min(nz.x*nz.x,nz.y*nz.y),min(nz.z*nz.z,nz.w*nz.w))>0.99999){
        let t=rounded_sqrt(rounded_dot(direction,direction));let denom=z0*r.len_u*r.len_v;out.pdf=select(0.0,rounded_divide(-t*t*t,denom),denom!=0.0);
    }else{out.pdf=rounded_divide(1.0,S);}return out;
}
struct AreaEval {P:vec3<f32>,D:vec3<f32>,t:f32,pdf:f32,eval_fac:f32,valid:bool}
fn area_evaluate(light:u32,origin:vec3<f32>,direction:vec3<f32>,distance:f32,random:vec2<f32>,sample_coord:bool)->AreaEval{
    var out:AreaEval;let N=heap_vec3(light+L_area_dir);let center=heap_vec3(light+L_co);
    if(sample_coord&&dot(center-origin,N)>0.0){return out;}
    let region=area_region(light,origin);if(!region.valid){return out;}
    out.P=region.P;let spread=heap_float(light+L_area_tan_half_spread);let normalization=heap_float(light+L_area_normalize_spread);
    if(region.rectangle){let rect=area_rectangle(origin,region,random,sample_coord);out.P=rect.P;out.pdf=rect.pdf;}
    else if(spread==0.0){out.pdf=1.0;}
    else{if(sample_coord){let disk=emitter_disk(random);out.P+=region.U*(region.len_u*0.5*disk.x)+region.V*(region.len_v*0.5*disk.y);}
        out.pdf=(4.0*EMITTER_INV_PI)/(region.len_u*region.len_v);}
    out.D=direction;out.t=distance;if(sample_coord){let ray=emitter_normalize_len(out.P-origin);out.D=ray.xyz;out.t=ray.w;}
    out.eval_fac=EMITTER_INV_PI*abs(heap_float(light+L_area_invarea));
    if(normalization>0.0){let tangent=length(cross(-out.D,N))/dot(-out.D,N);var attenuation=max((spread-tangent)*normalization,0.0);
        if(spread==0.0){attenuation=select(EMITTER_PI,0.0,tangent>1e-5);}out.eval_fac*=attenuation;}
    if(!region.rectangle&&spread>0.0){let cosine=dot(N,-out.D);out.pdf*=select(0.0,out.t*out.t/cosine,cosine>0.0);}
    if(out.eval_fac<=0.0){return out;}
    if(sample_coord&&normalization>0.0){let plane=out.P-center;let u=dot(plane,heap_vec3(light+L_area_axis_u));let v=dot(plane,heap_vec3(light+L_area_axis_v));
        let eu=(0.5+1e-7)*heap_float(light+L_area_len_u)+1e-6;let ev=(0.5+1e-7)*heap_float(light+L_area_len_v)+1e-6;
        if(heap_float(light+L_area_invarea)<0.0){if((u/eu)*(u/eu)+(v/ev)*(v/ev)>1.0){return out;}}
        else if(abs(u)>eu||abs(v)>ev){return out;}
    }out.valid=true;return out;
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Cycles background shader evaluation and native film/pass semantics.
// SVM support is shared with subsequent native shader entries.
fn film_add(address:u32,value:f32){
    var old=load_word(address);
    loop{let next=bitcast<u32>(bitcast<f32>(old)+value);let result=atomicCompareExchangeWeak(&heap[address/4u],old,next);
        if(result.exchanged){break;}old=result.old_value;}
}
fn film_rgb(base:u32,offset:u32,value:vec3<f32>){
    if(offset==PASS_NONE){return;}
    film_add(base+offset*4u,value.x);film_add(base+(offset+1u)*4u,value.y);film_add(base+(offset+2u)*4u,value.z);
}
fn background_visible(shader:u32,visibility:u32,flag:u32)->bool{
    return !(((shader&SHADER_EXCLUDE_DIFFUSE)!=0u&&(visibility&PATH_RAY_VISIBILITY_DIFFUSE)!=0u)||
        ((shader&SHADER_EXCLUDE_GLOSSY)!=0u&&(visibility&PATH_RAY_VISIBILITY_GLOSSY)!=0u&&(flag&PATH_RAY_REFLECT)!=0u)||
        ((shader&SHADER_EXCLUDE_TRANSMIT)!=0u&&(visibility&PATH_RAY_VISIBILITY_TRANSMIT)!=0u)||
        ((shader&SHADER_EXCLUDE_SCATTER)!=0u&&(visibility&PATH_RAY_VISIBILITY_VOLUME_SCATTER)!=0u)||
        ((shader&SHADER_EXCLUDE_CAMERA)!=0u&&(visibility&VIS_CAMERA)!=0u));
}
@compute @workgroup_size(64) fn shade_background(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
    if(state_half(S_path_queued_kernel,index)!=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND){fail();return;}
    let flag=state_word(S_path_flag,index);let visibility=state_byte(S_path_visibility,index);
    if((flag&BACKGROUND_UNSUPPORTED_FLAGS)!=0u){fail();return;}
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_path_render_pixel_index,index);
    if(pixel>=p(11u)/stride){fail();return;}let base=p(4u)+pixel*stride*4u;
    integrate_suns(index,base);if(load_word(p(17u))!=0u){return;}
    let light_flags=data_word(D_film_light_pass_flag);let throughput=heap_vec3(state_address(S_path_throughput,index,12u));
    let transparent_ray=data_word(D_background_transparent)!=0u&&(flag&PATH_RAY_TRANSPARENT_BACKGROUND)!=0u;
    var transparency=0.0;if(transparent_ray){transparency=(throughput.x+throughput.y+throughput.z)*(1.0/3.0);}
    var light=vec3<f32>(0);let shader=data_word(D_background_surface_shader);
    if((!transparent_ray||(light_flags&MASK_BACKGROUND)!=0u)&&background_visible(shader,visibility,flag)){
        let shader_id=shader&SHADER_MASK;if(shader_id>=p(19u)){fail();return;}
        let address=p(18u)+shader_id*SIZE_shaders;
        if((load_word(address+SH_flags)&SD_HAS_CONSTANT_EMISSION)!=0u){light=heap_vec3(address+SH_constant_emission);}
        else{light=svm_world(index,shader_id);if(load_word(p(17u))!=0u){return;}}
        if(path_ao(index)){light*=data_float(D_integrator_ao_bounces_factor);}
    }
    var contribution=throughput*light;
    contribution=select(contribution,vec3<f32>(0),(bitcast<vec3<u32>>(contribution)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
    let bounce=state_half(S_path_bounce,index);
    let limit=select(data_float(D_integrator_sample_clamp_direct),data_float(D_integrator_sample_clamp_indirect),i32(bounce)-1>0);
    let total=abs(contribution.x)+abs(contribution.y)+abs(contribution.z);if(total>limit){contribution*=limit/total;}
    if(transparent_ray){volume_transparent_pass(index,base,transparency);}
    else{volume_guidance_pass(index,base,contribution);}
    let sample_id=state_word(S_path_sample,index);
    if((light_flags&MASK_COMBINED)!=0u){
        if(!transparent_ray){film_rgb(base,data_word(D_film_pass_combined),contribution);}
        film_add(base+(data_word(D_film_pass_combined)+3u)*4u,transparency);
    }
    if(!transparent_ray&&data_word(D_film_pass_adaptive_aux_buffer)!=PASS_NONE&&((countOneBits(sample_id&0xaaaaaaaau)&1u)!=0u)){
        film_rgb(base,data_word(D_film_pass_adaptive_aux_buffer),contribution*2.0);
        film_add(base+(data_word(D_film_pass_adaptive_aux_buffer)+3u)*4u,0.0);
    }
    if(light_flags!=0u){
        let group=data_word(D_background_lightgroup);let group_offset=data_word(D_film_pass_lightgroup);
        if(group!=NONE&&group_offset!=PASS_NONE){film_rgb(base,group_offset+group*3u,contribution);}
        if((flag&PATH_RAY_ANY_PASS)==0u){film_rgb(base,data_word(D_film_pass_background),contribution);}
        else if(p(20u)!=0u&&(flag&PATH_RAY_SURFACE_PASS)!=0u){
            let diffuse=heap_vec3(state_address(S_path_pass_diffuse_weight,index,12u));let glossy=heap_vec3(state_address(S_path_pass_glossy_weight,index,12u));
            film_rgb(base,select(data_word(D_film_pass_glossy_indirect),data_word(D_film_pass_glossy_direct),bounce==1u),glossy*contribution);
            film_rgb(base,select(data_word(D_film_pass_transmission_indirect),data_word(D_film_pass_transmission_direct),bounce==1u),(vec3<f32>(1)-diffuse-glossy)*contribution);
            film_rgb(base,select(data_word(D_film_pass_diffuse_indirect),data_word(D_film_pass_diffuse_direct),bounce==1u),diffuse*contribution);
        }
    }
    if((flag&PATH_RAY_TRANSPARENT_BACKGROUND)!=0u&&(flag&PATH_RAY_SINGLE_PASS_DONE)==0u&&sample_id==0u){
        let flags=data_word(D_film_pass_flag);
        if((flags&MASK_DEPTH)!=0u){store_word(base+data_word(D_film_pass_depth)*4u,0u);}
        if((flags&MASK_OBJECT_ID)!=0u){store_word(base+data_word(D_film_pass_object_id)*4u,0u);}
        if((flags&MASK_MATERIAL_ID)!=0u){store_word(base+data_word(D_film_pass_material_id)*4u,0u);}
        if((flags&MASK_POSITION)!=0u){let address=base+data_word(D_film_pass_position)*4u;store_word(address,0u);store_word(address+4u,0u);store_word(address+8u,0u);}
    }
    volume_write_depth(index);
    let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[(counter+DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND*4u)/4u],1u);
    state_u16(S_path_queued_kernel,index,0u);
}
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2004 NVIDIA Corporation
// SPDX-FileCopyrightText: 2008-2014 Larry Gritz
// SPDX-FileCopyrightText: 2009-2014 Sony Pictures Imageworks Inc., et al.
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Point/sun forward evaluation, native MIS and emission film routing. The
// bounded positive-argument atan2 follows the pinned Cycles math_fast.h.
fn forward_mis_weight(index:u32,pdf:f32)->f32{
    if((state_word(S_path_flag,index)&PATH_RAY_MIS_SKIP)!=0u){return 1.0;}
    let forward=heap_float(state_address(S_path_mis_ray_pdf,index,4u));
    let nee=pdf*data_float(D_integrator_distribution_pdf_lights);
    return (forward*forward)/(forward*forward+nee*nee);
}
fn point_forward_pdf(index:u32,light:u32)->f32{
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));
    let delta=origin-heap_vec3(light+L_co);let radius=heap_float(light+L_radius);let r2=radius*radius;
    if(load_word(light+L_is_sphere)!=0u){let d2=rounded_dot(delta,delta);
        if(d2>r2){let sine2=r2/d2;var angle=select(0.5*sine2,1.0-sqrt(max(1.0-sine2,0.0)),sine2>0.0004);if(load_word(light+L_type)==LIGHT_SPOT){angle=min(angle,1.0-heap_float(light+L_spot_spread_cos));}return 0.15915494309189535/angle;}
        if((state_word(S_path_flag,index)&PATH_RAY_MIS_HAD_TRANSMISSION)!=0u){return 0.07957747154594767;}
        return max(dot(heap_vec3(state_address(S_path_mis_origin_n,index,12u)),direction),0.0)*0.3183098861837907;
    }
    let t=heap_float(state_address(S_isect_t,index,4u));if(t==bitcast<f32>(0x7f7fffffu)){return 0.0;}
    let normal=rounded_normalize(delta);let cosine=dot(normal,-direction);
    let inverse_area=select(1.0,1.0/(r2*3.141592653589793),r2>0.0);
    return select(0.0,inverse_area*(t*t/cosine),cosine>0.0);
}
fn sun_forward_visible(light:u32,direction:vec3<f32>)->bool{
    let angle=heap_float(light+L_sun_half_angle);if(angle==0.0){return false;}
    let axis= -heap_vec3(light+L_co);let a=rounded_sqrt(rounded_dot(axis+direction,axis+direction));
    let b=rounded_sqrt(rounded_dot(axis-direction,axis-direction));
    var k=0.0;if(b!=0.0){k=select(select(b/a,a/b,b>a),1.0,a==b);}
    let s=rounded_add(1.0,-rounded_add(1.0,-k));let t=s*s;
    let numerator=s*rounded_add(0.43157974*t,1.0);
    let denominator=rounded_add(rounded_add(0.05831938*t,0.76443945)*t,1.0);
    var half_angle=rounded_divide(numerator,denominator);if(b>a){half_angle=rounded_add(1.5707963267948966,-half_angle);}
    return !(2.0*half_angle>angle);
}
fn forward_emission(index:u32,base:u32,light:u32,eval:vec3<f32>,weight:f32){
    var value=heap_vec3(state_address(S_path_throughput,index,12u))*eval*weight;
    value=select(value,vec3<f32>(0),(bitcast<vec3<u32>>(value)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));
    let bounce=state_half(S_path_bounce,index);let limit=select(data_float(D_integrator_sample_clamp_direct),data_float(D_integrator_sample_clamp_indirect),i32(bounce)-1>0);
    let total=abs(value.x)+abs(value.y)+abs(value.z);if(total>limit){value*=limit/total;}
    let flags=data_word(D_film_light_pass_flag);if((flags&MASK_COMBINED)!=0u){film_rgb(base,data_word(D_film_pass_combined),value);}
    if(data_word(D_film_pass_adaptive_aux_buffer)!=PASS_NONE&&(countOneBits(state_word(S_path_sample,index)&0xaaaaaaaau)&1u)!=0u){film_rgb(base,data_word(D_film_pass_adaptive_aux_buffer),value*2.0);}
    if(flags==0u){return;}let flag=state_word(S_path_flag,index);let group=load_word(table_at(G_objects,load_word(light+L_object_id),SIZE_objects)+O_lightgroup);
    if(group!=NONE&&data_word(D_film_pass_lightgroup)!=PASS_NONE){film_rgb(base,data_word(D_film_pass_lightgroup)+3u*group,value);}
    if((flag&PATH_RAY_ANY_PASS)==0u){film_rgb(base,data_word(D_film_pass_emission),value);}
    else if(p(20u)!=0u&&(flag&PATH_RAY_SURFACE_PASS)!=0u){let diffuse=heap_vec3(state_address(S_path_pass_diffuse_weight,index,12u));let glossy=heap_vec3(state_address(S_path_pass_glossy_weight,index,12u));
        film_rgb(base,select(data_word(D_film_pass_glossy_indirect),data_word(D_film_pass_glossy_direct),bounce==1u),glossy*value);
        film_rgb(base,select(data_word(D_film_pass_transmission_indirect),data_word(D_film_pass_transmission_direct),bounce==1u),(vec3<f32>(1)-diffuse-glossy)*value);
        film_rgb(base,select(data_word(D_film_pass_diffuse_indirect),data_word(D_film_pass_diffuse_direct),bounce==1u),diffuse*value);}
}
fn forward_constant_emission(light:u32)->vec3<f32>{
    let shader=table_at(G_shaders,load_word(light+L_shader_id)&SHADER_MASK,SIZE_shaders);
    return heap_vec3(shader+SH_constant_emission)*heap_vec3(light+L_strength);
}
fn integrate_suns(index:u32,base:u32){
    let direction=heap_vec3(state_address(S_ray_D,index,12u));let visibility=state_byte(S_path_visibility,index);let flag=state_word(S_path_flag,index);
    let previous=state_word(S_isect_prim,index);let start=select(previous,0u,previous==NONE);
    for(var lamp=start;lamp<data_word(D_integrator_num_lights);lamp++){
        let light=table_at(G_lights,lamp,SIZE_lights);let shader=load_word(light+L_shader_id);
        if(load_word(light+L_type)!=LIGHT_SUN||(shader&SHADER_USE_MIS)==0u){continue;}
        if(!sun_forward_visible(light,direction)||!background_visible(shader,visibility,flag)){continue;}
        let eval=forward_constant_emission(light)*heap_float(light+L_sun_eval);if(all(eval==vec3<f32>(0))){continue;}
        forward_emission(index,base,light,eval,forward_mis_weight(index,heap_float(light+L_sun_pdf)));
    }
}
@compute @workgroup_size(64) fn shade_light_forward(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)||state_half(S_path_queued_kernel,index)!=DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD){fail();return;}
    let flag=state_word(S_path_flag,index);let visibility=state_byte(S_path_visibility,index);
    if((flag&BACKGROUND_UNSUPPORTED_FLAGS)!=0u||(visibility&PATH_RAY_VISIBILITY_VOLUME_SCATTER)!=0u){fail();return;}
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_path_render_pixel_index,index);if(pixel>=p(11u)/stride){fail();return;}
    let lamp=state_word(S_isect_prim,index);if(lamp>=table_count(G_lights)||state_word(S_isect_type,index)!=PRIMITIVE_LAMP){fail();return;}
    let light=table_at(G_lights,lamp,SIZE_lights);if((load_word(light+L_type)!=LIGHT_POINT&&load_word(light+L_type)!=LIGHT_SPOT&&load_word(light+L_type)!=LIGHT_AREA)||load_word(light+L_object_id)!=state_word(S_isect_object,index)){fail();return;}
    let t=heap_float(state_address(S_isect_t,index,4u));state_f32(S_ray_tmin,index,select(bitcast<f32>(bitcast<u32>(t)+1u),1.1754943508222875e-38,t==0.0));
    if(background_visible(load_word(light+L_shader_id),visibility,flag)){
        let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));var factor=heap_float(light+L_point_eval);var pdf=0.0;
        if(load_word(light+L_type)==LIGHT_AREA){
//#if LIGHT_AREA else atomicOr(&heap[p(17u)/4u],2u);fail();return;
            let area=area_evaluate(light,origin,direction,t,vec2<f32>(0),false);factor=select(0.0,area.eval_fac,area.valid);pdf=area.pdf;
//#endif
        }
        else{pdf=point_forward_pdf(index,light);if(load_word(light+L_type)==LIGHT_SPOT){let delta=origin-heap_vec3(light+L_co);let radius=heap_float(light+L_radius);
            if(load_word(light+L_is_sphere)==0u||dot(delta,delta)>radius*radius){factor*=spot_attenuation(light,-direction);}}}
        let eval=forward_constant_emission(light)*factor;
        if(any(eval!=vec3<f32>(0))){forward_emission(index,p(4u)+pixel*stride*4u,light,eval,forward_mis_weight(index,pdf));}
    }
    let transparent=state_half(S_path_transparent_bounce,index)+1u;state_u16(S_path_transparent_bounce,index,transparent);
    let next=select(K_CLOSEST,0u,transparent>=data_word(D_integrator_transparent_max_bounce));
    let counter=load_word(p(7u)+S_queue_counter)/4u;atomicSub(&heap[counter+DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD],1u);
    if(next!=0u){atomicAdd(&heap[counter+next],1u);}state_u16(S_path_queued_kernel,index,next);
}
)CBWGSL"
