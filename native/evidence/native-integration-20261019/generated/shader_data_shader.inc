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
)CBWGSL"
