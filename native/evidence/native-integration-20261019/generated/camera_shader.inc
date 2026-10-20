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
// SPDX-FileCopyrightText: 2019-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Cycles init_from_camera, perspective camera, path initialization and Sobol.
// Native byte offsets are generated from the compiled upstream types.
fn filter_value(value:f32)->f32{
    let x=clamp(value,0.0,1.0)*f32(FILTER_SIZE-1u);let i=min(u32(x),FILTER_SIZE-1u);
    let a=p(8u)+(data_word(D_tables_filter_table_offset)+i)*4u;let t=x-f32(i);let first=heap_float(a);
    if(t==0.0){return first;}
    return (1.0-t)*first+t*heap_float(p(8u)+(data_word(D_tables_filter_table_offset)+min(i+1u,FILTER_SIZE-1u))*4u);
}
fn camera_shutter(value:f32)->f32{
    let x=clamp(value,0.0,1.0)*f32(SHUTTER_SIZE-1u);let i=min(u32(x),SHUTTER_SIZE-1u);
    let address=p(8u)+(data_word(D_cam_shutter_table_offset)+i)*4u;let t=x-f32(i);let first=heap_float(address);
    if(t==0.0){return first;}
    return (1.0-t)*first+t*heap_float(p(8u)+(data_word(D_cam_shutter_table_offset)+min(i+1u,SHUTTER_SIZE-1u))*4u);
}
// Preserve native float3 normalization at transparent/volume boundary corners.
// Captures retain the originating host's SSE division or scalar reciprocal rule.
fn camera_normalize(v:vec3<f32>)->vec3<f32>{
    if(CAMERA_NATIVE_VECTOR_DIVIDE!=0u){return rounded_normalize(v);}
    return v*rounded_divide(1.0,rounded_sqrt(rounded_dot(v,v)));
}
fn camera_projection(address:u32,v:vec4<f32>)->vec3<f32>{
    // Native float4 dot rounds paired sums before perspective division.
    // Fused projection multiply-add can make a ray cross a box corner twice.
    let w=motion_dot4(heap_vec4(address+48u),v);
    let c=vec3<f32>(motion_dot4(heap_vec4(address),v),motion_dot4(heap_vec4(address+16u),v),motion_dot4(heap_vec4(address+32u),v));
    if(w==0.0){return vec3<f32>(0);}
    if(CAMERA_NATIVE_VECTOR_DIVIDE!=0u){return vec3<f32>(rounded_divide(c.x,w),rounded_divide(c.y,w),rounded_divide(c.z,w));}
    return c*rounded_divide(1.0,w);
}
@compute @workgroup_size(64) fn init_camera(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}
    var path=0u;var x=0u;var y=0u;var sample_id=0u;var pixel=0u;var sample_offset=0u;
    let retry=p(2u)==0u;
    if(!retry){
        let tile_index=id.x/p(5u);if(tile_index>=p(3u)){return;}
        let t=p(2u)+tile_index*T_size;let tile_work=id.x%p(5u);
        let work_size=load_word(t+T_work_size);if(tile_work>=work_size){return;}
        let w=load_word(t+T_w);let h=load_word(t+T_h);let samples=load_word(t+T_num_samples);
        let begin=load_word(t+T_path_index_offset);
        if(w==0u||h==0u||samples==0u||w>p(10u)||h>p(10u)/w||samples>p(10u)/(w*h)||work_size!=w*h*samples||work_size>p(5u)||begin>=p(10u)||work_size>p(10u)-begin){fail();return;}
        path=begin+tile_work;
        var pixel_offset=tile_work/samples;var sample_in_tile=tile_work%samples;
        if(data_float(D_integrator_scrambling_distance)<0.9){sample_in_tile=tile_work/(w*h);pixel_offset=tile_work%(w*h);}
        x=load_word(t+T_x)+pixel_offset%w;y=load_word(t+T_y)+pixel_offset/w;
        let stride=load_word(t+T_stride);let offset=load_word(t+T_offset);
        // Signed tile offset intentionally wraps in the original uint index.
        // Validate the final index before accessing the render allocation.
        pixel=offset+x+y*stride;
        sample_id=load_word(t+T_start_sample)+sample_in_tile;sample_offset=load_word(t+T_sample_offset);
    }else{
        if(id.x>=p(13u)){return;}path=id.x;if(p(12u)!=0u){path=load_word(p(12u)+id.x*4u);}
        if(path>=p(10u)){fail();return;}
        pixel=load_word(state_address(S_path_render_pixel_index,path,4u));
        let width=u32(data_float(D_cam_width));if(width==0u){fail();return;}
        x=pixel%width;y=pixel/width;sample_id=load_word(state_address(S_path_sample,path,4u));
        if(load_half(state_address(S_path_queued_kernel,path,2u))!=K_CAMERA){fail();return;}
    }
    let stride=data_word(D_film_pass_stride);
    if(pixel>=p(11u)/stride){fail();return;}
    let film=p(4u)+pixel*stride*4u;
    if(!retry){
        state_u32(S_path_render_pixel_index,path,pixel);state_u16(S_path_queued_kernel,path,0u);
        let adaptive=data_word(D_film_pass_adaptive_aux_buffer);
        if(adaptive!=PASS_NONE&&heap_float(film+(adaptive+3u)*4u)!=0.0){return;}
        let count=data_word(D_film_pass_sample_count);
        if(count!=PASS_NONE){sample_id=atomicAdd(&heap[(film+count*4u)/4u],1u)+sample_offset;}
    }
    let rng=pixel_hash(x,y,data_word(D_integrator_seed));
    var uv=vec2<f32>(0.5);if(sample_id!=0u){uv=native_rng(sample_id,rng,RNG_FILTER).xy;}
    var raster=vec2<f32>(f32(x),f32(y));
    if(data_word(D_integrator_pixel_jitter)==0x7f7fffffu){raster+=vec2<f32>(filter_value(uv.x),filter_value(uv.y));}
    else{raster-=vec2<f32>(data_float(D_integrator_pixel_jitter),data_float(D_integrator_pixel_jitter+4u));}
    var time=0.5;
    if(data_float(D_cam_shuttertime)!=-1.0){
        time=camera_shutter(native_rng(sample_id,rng,RNG_LENS_TIME).x);
        if(data_word(D_cam_rolling_shutter_type)!=0u){
            let row_time=1.0-f32(y)/data_float(D_cam_height);let duration=data_float(D_cam_rolling_shutter_duration);
            if(duration!=0.0){time=(time-0.5)*duration;time+=(row_time-0.5)*(1.0-duration)+0.5;}
            else{time=row_time;}
        }
    }
    let rt=p(6u)+D_cam_rastertocamera;let v=vec4<f32>(raster,0.0,1.0);
    var pc=camera_projection(rt,v);
    if(data_word(D_cam_have_perspective_motion)!=0u){
        if(time<0.5){let before=camera_projection(p(6u)+D_cam_perspective_pre,v);pc=before+(pc-before)*(time*2.0);}
        else{let after=camera_projection(p(6u)+D_cam_perspective_post,v);pc=pc+(after-pc)*((time-0.5)*2.0);}
    }
    var world=motion_transform_load(p(6u)+D_cam_cameratoworld);
    let steps=data_word(D_cam_num_motion_steps);
    if(steps!=0u){
        // Removed by scene specialization when the camera has no motion steps (pipelines.h).
//#if MOTION_CAMERA else atomicOr(&heap[p(17u)/4u],2u);return;
        world=motion_interpolate(G_camera_motion,0u,steps,time);
//#endif
    }
    if(load_word(p(17u))!=0u){return;}
    let center=motion_direction(world,pc);let direction=camera_normalize(center);
    let dx=camera_normalize(center+heap_vec3(p(6u)+D_cam_dx))-direction;
    let dy=camera_normalize(center+heap_vec3(p(6u)+D_cam_dy))-direction;
    let dd=(0.5*(length(dx)+length(dy)))*data_float(D_cam_differential_scale);
    let zi=rounded_divide(1.0,camera_normalize(pc).z);let near=data_float(D_cam_nearclip)*zi;
    let origin=vec3<f32>(world.x.w,world.y.w,world.z.w)+near*direction;
    state_vec3(S_ray_P,path,origin);state_vec3(S_ray_D,path,direction);
    state_f32(S_ray_dP,path,near*dd);state_f32(S_ray_dD,path,dd);
    state_f32(S_ray_tmin,path,0.0);state_f32(S_ray_tmax,path,data_float(D_cam_cliplength)*zi);state_f32(S_ray_time,path,time);
    state_u32(S_path_sample,path,sample_id);state_u32(S_path_rng_pixel,path,rng);state_u16(S_path_rng_offset,path,RNG_BOUNCE);
    state_u16(S_path_bounce,path,0u);state_u16(S_path_diffuse_bounce,path,0u);state_u16(S_path_glossy_bounce,path,0u);
    state_u16(S_path_transmission_bounce,path,0u);state_u16(S_path_transparent_bounce,path,0u);
    state_u16(S_path_volume_bounce,path,0u);state_u16(S_path_volume_bounds_bounce,path,0u);
    state_u8(S_path_visibility,path,VIS_CAMERA);state_u8(S_path_mnee,path,0u);state_u32(S_path_flag,path,INITIAL_FLAG);
    state_f32(S_path_mis_ray_pdf,path,0.0);state_u32(S_path_min_ray_pdf,path,0x7f7fffffu);
    state_f32(S_path_continuation_probability,path,1.0);state_vec3(S_path_throughput,path,vec3<f32>(1.0));
    state_f32(S_path_optical_depth,path,0.0);state_u32(S_isect_object,path,NONE);state_u32(S_isect_prim,path,NONE);state_u32(S_isect_type,path,PRIM_TYPE_NONE);
    if(volume_enabled()){
        store_word(volume_stack_address(path,0u,VS_object),data_word(D_background_object_index));
        store_word(volume_stack_address(path,0u,VS_shader),data_word(D_background_volume_shader));
        store_word(volume_stack_address(path,1u,VS_object),NONE);store_word(volume_stack_address(path,1u,VS_shader),NONE);
    }
    let counter=load_word(p(7u)+S_queue_counter);
    if(retry){atomicSub(&heap[(counter+K_CAMERA*4u)/4u],1u);}
    let next=select(K_CLOSEST,DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK,data_word(D_cam_is_inside_volume)!=0u);
    atomicAdd(&heap[(counter+next*4u)/4u],1u);state_u16(S_path_queued_kernel,path,next);
}
)CBWGSL"
