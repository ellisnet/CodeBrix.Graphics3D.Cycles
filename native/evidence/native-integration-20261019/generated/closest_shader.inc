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
// SPDX-FileCopyrightText: 2009-2010 NVIDIA Corporation
// SPDX-FileCopyrightText: 2009-2012 Intel Corporation
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2014-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native BVH2 traversal, static triangle instances, and Cycles closest-stage
// termination/scheduling. Triangle arithmetic is generated from upstream AST.
fn clamp_direction(d:vec3<f32>)->vec3<f32>{
    let tiny=8.271806E-25;let signed_tiny=bitcast<vec3<f32>>((bitcast<vec3<u32>>(d)&vec3<u32>(0x80000000u))|vec3<u32>(bitcast<u32>(tiny)));
    return select(signed_tiny,d,abs(d)>vec3<f32>(tiny));
}
fn inverse_direction(d:vec3<f32>)->vec3<f32>{
    // Approximate reciprocal can reject a slab exactly at ray.tmin/tmax.
    return vec3<f32>(rounded_divide(1.0,d.x),rounded_divide(1.0,d.y),rounded_divide(1.0,d.z));
}
fn transform_vector(a:u32,v:vec3<f32>,point:bool)->vec3<f32>{
    let x=heap_vec4(a);let y=heap_vec4(a+16u);let z=heap_vec4(a+32u);
    var result=vec3<f32>(x.x,y.x,z.x)*v.x+(vec3<f32>(x.y,y.y,z.y)*v.y+vec3<f32>(x.z,y.z,z.z)*v.z);
    if(point){result+=vec3<f32>(x.w,y.w,z.w);}return result;
}
fn bounds_hit(origin:vec3<f32>,inv:vec3<f32>,tmin:f32,tmax:f32,node:u32,visibility:u32)->vec3<f32>{
    let a=table_at(G_bvh_nodes,node,SIZE_bvh_nodes);
    if(node>=table_count(G_bvh_nodes)||table_count(G_bvh_nodes)-node<4u){fail();return vec3<f32>(0);}
    let x=heap_vec4(a+16u);let y=heap_vec4(a+32u);let z=heap_vec4(a+48u);
    let lo0=(vec3<f32>(x.x,y.x,z.x)-origin)*inv;let hi0=(vec3<f32>(x.z,y.z,z.z)-origin)*inv;
    let lo1=(vec3<f32>(x.y,y.y,z.y)-origin)*inv;let hi1=(vec3<f32>(x.w,y.w,z.w)-origin)*inv;
    let near0=max(max(tmin,min(lo0.x,hi0.x)),max(min(lo0.y,hi0.y),min(lo0.z,hi0.z)));
    let far0=min(min(tmax,max(lo0.x,hi0.x)),min(max(lo0.y,hi0.y),max(lo0.z,hi0.z)));
    let near1=max(max(tmin,min(lo1.x,hi1.x)),max(min(lo1.y,hi1.y),min(lo1.z,hi1.z)));
    let far1=min(min(tmax,max(lo1.x,hi1.x)),min(max(lo1.y,hi1.y),max(lo1.z,hi1.z)));
    let mask=select(0u,1u,far0>=near0&&(load_word(a)&visibility)!=0u)|select(0u,2u,far1>=near1&&(load_word(a+4u)&visibility)!=0u);
    return vec3<f32>(f32(mask),near0,near1);
}
struct NativeHit {t:f32,u:f32,v:f32,object:u32,prim:u32,kind:u32}
fn trace_native_filtered(origin:vec3<f32>,direction:vec3<f32>,tmin:f32,tmax:f32,time:f32,visibility:u32,self_object:u32,self_prim:u32,light_object:u32,light_prim:u32)->NativeHit{
    var result=NativeHit(tmax,0.0,0.0,NONE,NONE,PRIM_TYPE_NONE);
    if(table_count(G_prim_index)==0u){return result;}
    var pos=origin;var dir=clamp_direction(direction);var inv=inverse_direction(dir);
    var stack:array<i32,192>;stack[0]=0x76543210i;var sp=0i;
    var node=bitcast<i32>(data_word(D_bvh_root));var object=NONE;var visits=0u;
    loop{
        // Malformed cycles in a scene table must fail instead of hanging a GPU.
        visits++;if(visits>1048576u){fail();return result;}
        while(node>=0&&node!=0x76543210i){
            visits++;if(visits>1048576u){fail();return result;}
            let bounds=bounds_hit(pos,inv,tmin,result.t,u32(node),visibility);
            let a=table_at(G_bvh_nodes,u32(node),SIZE_bvh_nodes);
            node=bitcast<i32>(load_word(a+8u));var other=bitcast<i32>(load_word(a+12u));let mask=u32(bounds.x);
            if(mask==3u){if(bounds.z<bounds.y){let tmp=node;node=other;other=tmp;}
                sp++;if(sp>=192){fail();return result;}stack[u32(sp)]=other;
            }else if(mask==2u){node=other;}
            else if(mask==0u){if(sp<0){fail();return result;}node=stack[u32(sp)];sp--;}
        }
        if(node<0){
            let a=table_at(G_bvh_leaf_nodes,u32(-node-1),SIZE_bvh_leaf_nodes);
            let first=bitcast<i32>(load_word(a));let end=load_word(a+4u);let kind=load_word(a+12u);
            if(first>=0){
                if(end>table_count(G_prim_index)||u32(first)>end){fail();return result;}
                if(sp<0){fail();return result;}node=stack[u32(sp)];sp--;
                if(!native_triangle_kind(kind&PRIMITIVE_ALL)&&u32(first)!=end){fail();return result;}
                for(var index=u32(first);index<end;index++){
                    var prim_object=object;if(object==NONE){prim_object=load_word(table_at(G_prim_object,index,SIZE_prim_object));}
                    let prim=load_word(table_at(G_prim_index,index,SIZE_prim_index));
                    if(prim_object==self_object&&prim==self_prim){continue;}
                    if(prim_object==light_object&&prim==light_prim){continue;}
                    if((load_word(table_at(G_prim_visibility,index,SIZE_prim_visibility))&visibility)==0u){continue;}
                    let vertices=native_triangle_vertices(prim_object,prim,kind&PRIMITIVE_ALL,time);if(vertices.error!=0u){return result;}
                    let va=vertices.a;let vb=vertices.b;let vc=vertices.c;
                    var u:f32;var v:f32;var t:f32;
                    if(c_ray_triangle_intersect(pos,dir,tmin,result.t,va,vb,vc,&u,&v,&t)){
                        result=NativeHit(t,u,v,prim_object,prim,kind&PRIMITIVE_ALL);
                        if((visibility&PATH_RAY_VISIBILITY_SHADOW_OPAQUE)!=0u){return result;}
                    }
                }
            }else{
                if(object!=NONE){fail();return result;}
                object=load_word(table_at(G_prim_object,u32(-first-1),SIZE_prim_object));
                let obj=table_at(G_objects,object,SIZE_objects);
                let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));
                if((flags&SD_OBJECT_MOTION)!=0u){
//#if MOTION_OBJECT else atomicOr(&heap[p(17u)/4u],2u);return result;
                    let inverse=motion_inverse(motion_object(object,time));pos=motion_point(inverse,origin);dir=clamp_direction(motion_direction(inverse,direction));
//#endif
                }
                else{pos=transform_vector(obj+O_itfm,origin,true);dir=clamp_direction(transform_vector(obj+O_itfm,direction,false));}
                inv=inverse_direction(dir);
                sp++;if(sp>=192){fail();return result;}stack[u32(sp)]=0x76543210i;
                node=bitcast<i32>(load_word(table_at(G_object_node,object,SIZE_object_node)));
            }
        }
        if(node==0x76543210i){
            if(sp<0){break;}
            if(object==NONE){fail();return result;}
            pos=origin;dir=clamp_direction(direction);inv=inverse_direction(dir);object=NONE;
            node=stack[u32(sp)];sp--;if(node==0x76543210i){break;}
        }
    }
    return result;
}
fn trace_native(origin:vec3<f32>,direction:vec3<f32>,tmin:f32,tmax:f32,time:f32,visibility:u32,self_object:u32,self_prim:u32)->NativeHit{
    return trace_native_filtered(origin,direction,tmin,tmax,time,visibility,self_object,self_prim,NONE,NONE);
}
// Point emitters use the same native geometry and strict ray interval tests as Cycles.
fn intersect_point_lights(index:u32,origin:vec3<f32>,direction:vec3<f32>,tmin:f32,tmax:f32,input:NativeHit)->NativeHit{
    var hit=input;let visibility=state_byte(S_path_visibility,index);let flag=state_word(S_path_flag,index);
    for(var lamp=0u;lamp<data_word(D_integrator_num_lights);lamp++){
        let light=table_at(G_lights,lamp,SIZE_lights);let light_kind=load_word(light+L_type);if(light_kind!=LIGHT_POINT&&light_kind!=LIGHT_SPOT&&light_kind!=LIGHT_AREA){continue;}
        let shader=load_word(light+L_shader_id);let object=load_word(light+L_object_id);
        if((visibility&VIS_CAMERA)!=0u){if((shader&LIGHT_EXCLUDE_CAMERA)!=0u){continue;}}
        else if((shader&SHADER_USE_MIS)==0u){continue;}
        if((flag&PATH_RAY_SHADOW_CATCHER_PASS)!=0u&&(shader&LIGHT_EXCLUDE_SHADOW_CATCHER)!=0u){continue;}
        if(state_word(S_isect_prim,index)==lamp&&state_word(S_isect_object,index)==object&&state_word(S_isect_type,index)==PRIMITIVE_LAMP){continue;}
        let position=heap_vec3(light+L_co);var t=0.0;
        if(light_kind==LIGHT_AREA){
            let N=heap_vec3(light+L_area_dir);let invarea=abs(heap_float(light+L_area_invarea));if(invarea==0.0||dot(direction,N)>=0.0){continue;}
            t=rounded_divide(-(dot(origin,N)-dot(position,N)),dot(direction,N));if(!(t>tmin&&t<tmax)){continue;}
            let plane=origin+t*direction-position;let u=dot(plane,heap_vec3(light+L_area_axis_u)/heap_float(light+L_area_len_u));let v=dot(plane,heap_vec3(light+L_area_axis_v)/heap_float(light+L_area_len_v));
            if(u< -0.5||u>0.5||v< -0.5||v>0.5){continue;}if(heap_float(light+L_area_invarea)<0.0&&u*u+v*v>0.25){continue;}
        }else{
        if(light_kind==LIGHT_SPOT&&dot(direction,origin-position)>=0.0){continue;}
        let radius=heap_float(light+L_radius);if(radius==0.0){continue;}
        if(load_word(light+L_is_sphere)!=0u){
            let delta=position-origin;let r2=radius*radius;let d2=rounded_dot(delta,delta);let cosine=rounded_dot(delta,direction);
            if(d2>r2&&cosine<0.0){continue;}let perpendicular=rounded_add3(delta,-cosine*direction);let sine2=rounded_dot(perpendicular,perpendicular);
            if(sine2>r2){continue;}let root=rounded_sqrt(rounded_add(r2,-sine2));
            t=rounded_add(cosine,-bitcast<f32>((bitcast<u32>(root)&0x7fffffffu)|(bitcast<u32>(rounded_add(d2,-r2))&0x80000000u)));
            if(!(t>tmin&&t<tmax)){continue;}
        }else{
            let vp=origin-position;let normal=rounded_normalize(vp);let dp=rounded_dot(vp,normal);let cosine=rounded_dot(normal,-direction);
            if(!(dp*cosine>0.0)){continue;}t=rounded_divide(dp,cosine);if(t<0.0){continue;}
            let point=rounded_add3(origin,t*direction);let delta=point-position;
            if(!(rounded_dot(delta,delta)<radius*radius&&t>tmin&&t<tmax)){continue;}
        }
        }
        if(t>=hit.t){continue;}hit=NativeHit(t,0.0,0.0,object,lamp,PRIMITIVE_LAMP);
    }return hit;
}
fn terminate_surface(index:u32,shader_flags:u32)->bool{
    var flag=state_word(S_path_flag,index);
    if(path_ao(index)){
        if((shader_flags&(SD_HAS_TRANSPARENT_SHADOW|SD_HAS_EMISSION))!=0u){flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;state_u32(S_path_flag,index,flag);}
        else if(volume_active(index)){flag|=PATH_RAY_TERMINATE_AFTER_VOLUME;state_u32(S_path_flag,index,flag);}
        else{return true;}
    }
    var probability=1.0;var use_rr=false;
    if((flag&PATH_RAY_TRANSPARENT)!=0u){use_rr=state_half(S_path_transparent_bounce,index)>data_word(D_integrator_transparent_min_bounce);}
    else{use_rr=state_half(S_path_bounce,index)>data_word(D_integrator_min_bounce);}
    if(use_rr){let beta=abs(heap_vec3(state_address(S_path_throughput,index,12u)));probability=min(sqrt(max(beta.x,max(beta.y,beta.z))),1.0);}
    state_f32(S_path_continuation_probability,index,probability);
    if(probability!=1.0){
        let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_TERMINATE).x;
        if(probability==0.0||random>=probability){
            if((shader_flags&SD_HAS_EMISSION)!=0u){state_u32(S_path_flag,index,flag|PATH_RAY_TERMINATE_ON_NEXT_SURFACE);}
            else if(volume_active(index)){state_u32(S_path_flag,index,flag|PATH_RAY_TERMINATE_IN_NEXT_VOLUME);}
            else{return true;}
        }
    }
    return false;
}
@compute @workgroup_size(64) fn intersect_closest(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    if(index>=p(10u)){fail();return;}
    if(state_half(S_path_queued_kernel,index)!=K_CLOSEST){fail();return;}
    let origin=heap_vec3(state_address(S_ray_P,index,12u));let direction=heap_vec3(state_address(S_ray_D,index,12u));
    let tmin=heap_float(state_address(S_ray_tmin,index,4u));var tmax=heap_float(state_address(S_ray_tmax,index,4u));
    let self_prim=state_word(S_isect_prim,index);let self_object=state_word(S_isect_object,index);
    if(path_ao(index)){
        tmax=data_float(D_integrator_ao_bounces_distance);
        if(self_object!=NONE){let distance=heap_float(table_at(G_objects,self_object,SIZE_objects)+O_ao_distance);if(distance!=0.0){tmax=distance;}}
    }
    var visibility=state_byte(S_path_visibility,index);let flag=state_word(S_path_flag,index);
    if((visibility&PATH_RAY_VISIBILITY_TRANSMIT)!=0u){visibility&=~(PATH_RAY_VISIBILITY_DIFFUSE|PATH_RAY_VISIBILITY_GLOSSY);}
    if((flag&PATH_RAY_SHADOW_CATCHER_PASS)!=0u){visibility<<=16u;}
    var hit=NativeHit(0.0,0.0,0.0,NONE,NONE,PRIM_TYPE_NONE);
    // Match Cycles' early validity test, including zero-length conditional rays.
    if((bitcast<u32>(origin.x)&0x7f800000u)!=0x7f800000u&&(bitcast<u32>(direction.x)&0x7f800000u)!=0x7f800000u&&dot(direction,direction)!=0.0&&tmin<bitcast<f32>(0x7f7fffffu)){
        hit=trace_native(origin,direction,tmin,tmax,heap_float(state_address(S_ray_time,index,4u)),visibility,self_object,self_prim);
    }
    let skip_lights=(data_word(D_integrator_filter_closures)&FILTER_CLOSURE_DIRECT_LIGHT)!=0u&&state_half(S_path_bounce,index)==1u;
    if(data_word(D_integrator_use_light_mis)!=0u&&!skip_lights){hit=intersect_point_lights(index,origin,direction,tmin,tmax,hit);}
    if(load_word(p(17u))!=0u){return;}
    state_f32(S_isect_t,index,hit.t);state_f32(S_isect_u,index,hit.u);state_f32(S_isect_v,index,hit.v);
    state_u32(S_isect_object,index,hit.object);state_u32(S_isect_prim,index,hit.prim);state_u32(S_isect_type,index,hit.kind);
    var next=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;var shader=0u;var sorted=false;
    if(volume_active(index)){
        var flags=0u;if(hit.prim!=NONE&&(hit.kind&PRIMITIVE_LAMP)==0u){let id=load_word(table_at(G_tri_shader,hit.prim,SIZE_tri_shader))&SHADER_MASK;flags=load_word(table_at(G_shaders,id,SIZE_shaders)+SH_flags);}
        next=select(DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME,0u,terminate_surface(index,flags));
    }
    else if(hit.prim!=NONE&&(hit.kind&PRIMITIVE_LAMP)!=0u){next=DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD;}
    else if(hit.prim!=NONE){
        shader=load_word(table_at(G_tri_shader,hit.prim,SIZE_tri_shader))&SHADER_MASK;
        let flags=load_word(table_at(G_shaders,shader,SIZE_shaders)+SH_flags);
        if(terminate_surface(index,flags)){next=0u;}
        else{next=select(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,(flags&SD_HAS_RAYTRACE)!=0u);sorted=true;}
    }else if((data_word(D_integrator_filter_closures)&FILTER_CLOSURE_DIRECT_LIGHT)!=0u&&state_half(S_path_bounce,index)==1u){next=0u;}
    if(sorted){
        let divisor=load_word(p(7u)+S_sort_partition_divisor);let key=shader+data_word(D_max_shaders)*(index/divisor);
        let limit=select(p(15u),p(16u),next==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE);
        if(key>=limit){fail();return;}
        let counter=load_word(p(7u)+S_sort_key_counter+next*POINTER_SIZE);
        atomicAdd(&heap[(counter+key*4u)/4u],1u);state_u32(S_path_shader_sort_key,index,key);
    }
    let counter=load_word(p(7u)+S_queue_counter);
    atomicSub(&heap[(counter+K_CLOSEST*4u)/4u],1u);
    if(next==0u){volume_write_depth(index);}
    if(next!=0u){atomicAdd(&heap[(counter+next*4u)/4u],1u);}
    state_u16(S_path_queued_kernel,index,next);
}
)CBWGSL"
