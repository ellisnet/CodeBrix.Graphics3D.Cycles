// SPDX-FileCopyrightText: 2011-2026 Blender Foundation
// SPDX-FileCopyrightText: 2011-2013 Intel Corporation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Adapted from pinned Cycles kernel/util/image.h, kernel/util/image_2d.h,
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
// abs(v)<bound on both components, false for NaN, as a test on the bits (bound
// is the f32 bound's bit pattern): the Intel Vulkan driver on Windows compiles
// the float comparison as true for NaN (docs/TESTING-WINDOWS-X64.md, W2).
fn image_below(v:vec2<f32>,bound:u32)->bool{return all((bitcast<vec2<u32>>(v)&vec2<u32>(0x7fffffffu))<vec2<u32>(bound));}
//#if TILED_IMAGES
// Tiled images (Cycles' texture cache), adapted from pinned kernel/util/image_2d.h (kernel_image_tile_wrap,
// kernel_image_tile_map), kernel/util/image.h (kernel_image_interp), util/math_fast.h (fast_log2f) and
// kernel/sample/lcg.h (lcg_step_float). The derivatives of the lookup's uv (the dual part of the pinned dual2): set by
// svm_image around its lookups, zero otherwise (a lookup without derivatives, as the pinned dual2 of a float2).
var<private> image_tile_duv:vec4<f32>;
// The lcg state of an emission evaluation made for a path (wavefront_svm.wgsl svm_evaluate_context_seeded): set by the
// entries that evaluate a lamp's or an emissive triangle's shader for a main or a shadow path.
var<private> image_tile_lcg:u32=0u;
// integrator_state_lcg_init of a shadow path (kernel/integrator/state_util.h): lcg_state_init(rng_pixel, rng_offset,
// sample, 0xb4bc3953), hash_uint3 as svm_lcg.
fn svm_lcg_shadow(index:u32)->u32{
    var a=0xdeadbf08u+(state_word(ITS_rng_pixel,index)^0xb4bc3953u);
    var b=0xdeadbf08u+state_half(ITS_rng_offset,index);var c=0xdeadbf08u+state_word(ITS_sample,index);
    c^=b;c-=(b<<14u)|(b>>18u);a^=c;a-=(c<<11u)|(c>>21u);
    b^=a;b-=(a<<25u)|(a>>7u);c^=b;c-=(b<<16u)|(b>>16u);
    a^=c;a-=(c<<4u)|(c>>28u);b^=a;b-=(a<<14u)|(a>>18u);c^=b;c-=(b<<24u)|(b>>8u);return c;
}
// The derivatives of the node's coordinate (the dual part of the pinned dual3), for the box projection's three lookups.
var<private> image_tile_dx:vec3<f32>;var<private> image_tile_dy:vec3<f32>;
// status 0: xy and info address a loaded tile; 1: zero (outside a Clip image, or an error); 2: the Missing colour (a tile
// that failed to load); 3: the image's average colour (a tile that is requested: the dispatch is marked as missed and
// runs again once the tile is loaded, device.cpp tile_transaction).
struct ImageTile {status:u32, info:u32, xy:vec2<f32>,}
fn image_tile_map(s:ptr<function,SvmState>,tex:u32,input_uv:vec2<f32>)->ImageTile{
    var out:ImageTile;out.status=1u;var uv=input_uv;
    // kernel_image_tile_wrap.
    let extension=load_word(tex+IT_extension);
    if(extension==EXTENSION_REPEAT){uv=uv-floor(uv);}
    else if(extension==ITE_CLIP){if(!(uv.x>=0.0&&uv.x<=1.0&&uv.y>=0.0&&uv.y<=1.0)){return out;}}
    else if(extension==EXTENSION_EXTEND){uv=clamp(uv,vec2<f32>(0),vec2<f32>(1));}
    else if(extension==EXTENSION_MIRROR){let t=uv*0.5;uv=2.0*(t-floor(t));uv=select(uv,vec2<f32>(2.0)-uv,uv>=vec2<f32>(1));}
    else{return out;}
    let tex_width=bitcast<i32>(load_word(tex+IT_width));let tex_height=bitcast<i32>(load_word(tex+IT_height));
    let levels=bitcast<i32>(load_word(tex+IT_tile_levels));let shift=load_word(tex+IT_tile_size_shift);
    let base=load_word(tex+IT_tile_descriptor_offset);
    if(tex_width<=0||tex_height<=0||levels<=0||levels>31||shift>24u){svm_error(s,128u);return out;}
    // The mip level: 0.5 * fast_log2f(max(mindxy_sq, maxdxy_sq / 256)) of the squared texel footprints, each sum and each
    // madd of the polynomial rounded on its own (the oracle has no fused multiply-add), and the two sums that move the
    // pixel coordinate into the tile pool (a product plus an offset), all through one rounded_add body.
    let d=image_tile_duv;var sums=vec2<f32>(0);var f=0.0;var f4=0.0;var exponent=0.0;var hi=bitcast<f32>(0xbc188b0du);var lo=bitcast<f32>(0x3ef5162du);var flevel=0.0;
    var xy=vec2<f32>(0);var pool=vec2<f32>(0);var descriptor=0u;
    for(var i=0u;i<rounded_steps(12u);i++){
        var a=0.0;var b=0.0;
        if(i==0u){a=d.x*d.x;b=d.z*d.z;}else if(i==1u){a=d.y*d.y;b=d.w*d.w;}
        else if(i<6u){a=f*hi;b=bitcast<f32>(array<u32,4>(0x3d5541c9u,0xbe0cd4fdu,0x3e77adbdu,0xbeb1d206u)[i-2u]);}
        else if(i<8u){a=f*lo;b=bitcast<f32>(array<u32,2>(0xbf389e54u,0x3fb8aa10u)[i-6u]);}
        else if(i==8u){a=f4*hi;b=f*lo;}else if(i==9u){a=flevel;b=exponent;}
        else if(i==10u){a=xy.x;b=pool.x;}else{a=xy.y;b=pool.y;}
        let v=rounded_add(a,b);
        if(i==0u){sums.x=v;}
        else if(i==1u){sums.y=v;
            let w=f32(tex_width);let h=f32(tex_height);let du=sums.x*(w*w);let dv=sums.y*(h*h);
            // max(a, b) is a > b ? a : b and min(a, b) is a < b ? a : b; the operands are never NaN (finite derivatives).
            let maxd=select(dv,du,du>dv);let mind=select(dv,du,du<dv);let scaled=maxd*0.00390625;
            var x=select(scaled,mind,mind>scaled);
            // clamp(x, FLT_MIN, FLT_MAX).
            if(!(x>1.17549435e-38)){x=1.17549435e-38;}
            if(!(x<3.40282347e+38)){x=3.40282347e+38;}
            let xb=bitcast<u32>(x);exponent=f32(bitcast<i32>(xb>>23u)-127);
            f=bitcast<f32>((xb&0x007fffffu)|0x3f800000u)-1.0;let f2=f*f;f4=f2*f2;}
        else if(i<6u){hi=v;}else if(i<8u){lo=v;}else if(i==8u){flevel=v;}
        else if(i==9u){
            flevel=0.5*v;
            var lcg=(*s).output.lcg_state;
            if(lcg!=0u){
                flevel+=0.5;
                // lcg_step_float: (float)rng, correctly rounded, times 2^-32.
                lcg=1103515245u*lcg+12345u;var r=0.0;
                if(lcg!=0u){let lz=countLeadingZeros(lcg);let lb=lcg<<lz;let low=lb&0xffu;var mantissa=lb>>8u;
                    if(low>0x80u||(low==0x80u&&(mantissa&1u)!=0u)){mantissa+=1u;}
                    r=bitcast<f32>(((158u-lz)<<23u)+mantissa-0x800000u)*2.3283064365386963e-10;}
                (*s).output.lcg_state=lcg;
                flevel+=(r-0.5)*0.5;
            }
            flevel+=data_float(D_image_mip_bias);
            // int(flevel) as the oracle's cvttss2si: INT_MIN outside the int range (and for NaN), which the clamp makes level 0.
            var level=0;if(flevel>-2147483648.0&&flevel<2147483648.0){level=clamp(i32(flevel),0,levels-1);}
            let width=max(1,tex_width>>u32(level));let height=max(1,tex_height>>u32(level));
            xy=vec2<f32>(uv.x*f32(width),(1.0-uv.y)*f32(height));
            let ix=clamp(i32(xy.x),0,width-1);let iy=clamp(i32(xy.y),0,height-1);
            let tile_size_padded=(1<<shift)+i32(KERNEL_IMAGE_TEX_PADDING)*2;
            let tile_x=ix>>shift;let tile_y=iy>>shift;
            let count=table_count(G_image_texture_tile_descriptors);
            if(base>=count||u32(level)>=count-base){svm_error(s,128u);return out;}
            let level_start=load_word(table_at(G_image_texture_tile_descriptors,base+u32(level),4u));
            let across=u32((width+((1<<shift)-1))>>shift);
            let tile_offset=level_start+u32(tile_x)+u32(tile_y)*across;
            if(tile_offset>=count-base||base+tile_offset>=table_count(G_image_texture_tile_access_state)){svm_error(s,128u);return out;}
            let access_index=base+tile_offset;let descriptor_address=table_at(G_image_texture_tile_descriptors,access_index,4u);
            descriptor=load_word(descriptor_address);
            let access_address=table_at(G_image_texture_tile_access_state,access_index,1u);let access_shift=8u*(access_address%4u);
            if(descriptor>=KERNEL_TILE_LOAD_FAILED){
                // The pinned GPU kernels' request: the descriptor, the access state the host reads back, and the miss.
                if(descriptor==KERNEL_TILE_LOAD_NONE){descriptor=KERNEL_TILE_LOAD_REQUEST;store_word(descriptor_address,descriptor);
                    atomicOr(&heap[access_address/4u],KERNEL_TILE_ACCESS_REQUESTED<<access_shift);}
                if(descriptor==KERNEL_TILE_LOAD_REQUEST){store_word(table_at(G_image_tile_miss,0u,4u),1u);}
                out.status=select(3u,2u,descriptor==KERNEL_TILE_LOAD_FAILED);return out;
            }
            if(((load_word(access_address)>>access_shift)&255u)!=KERNEL_TILE_ACCESS_USED){atomicOr(&heap[access_address/4u],KERNEL_TILE_ACCESS_USED<<access_shift);}
            // xy += (PADDING - (tile_x << shift) + offset * tile_size_padded, PADDING - (tile_y << shift)): steps 10 and 11.
            let offset=i32(descriptor>>24u);
            pool=vec2<f32>(f32(i32(KERNEL_IMAGE_TEX_PADDING)-(tile_x<<shift)+offset*tile_size_padded),f32(i32(KERNEL_IMAGE_TEX_PADDING)-(tile_y<<shift)));}
        else if(i==10u){xy.x=v;}else{xy.y=v;}
    }
    out.status=0u;out.info=descriptor&0xffffffu;out.xy=xy;return out;
}
//#endif
//#if TILED_VOLUMES
// lcg_state_init (kernel/sample/lcg.h): hash_uint3(rng_hash ^ scramble, rng_offset, sample) (util/hash.h), as svm_lcg.
fn image_tile_lcg_init(rng_hash:u32,rng_offset:u32,sample:u32,scramble:u32)->u32{
    var a=0xdeadbf08u+(rng_hash^scramble);var b=0xdeadbf08u+rng_offset;var c=0xdeadbf08u+sample;
    c^=b;c-=(b<<14u)|(b>>18u);a^=c;a-=(c<<11u)|(c>>21u);
    b^=a;b-=(a<<25u)|(a>>7u);c^=b;c-=(b<<16u)|(b>>16u);
    a^=c;a-=(c<<4u)|(c>>28u);b^=a;b-=(a<<14u)|(a>>18u);c^=b;c-=(b<<24u)|(b>>8u);return c;
}
// The lcg state a volume evaluation of vh_eval starts from and leaves (the pinned ShaderData's lcg_state, which every
// volume_shader_sample of one integration advances in turn).
var<private> image_tile_volume_lcg:u32=0u;
//#endif
// kernel_image_interp_with_udim: missing is the value of an image that is not there (a UDIM coordinate outside the
// tile grid, no tile for the UDIM, or no image: KERNEL_IMAGE_NONE).
fn image_sample(s:ptr<function,SvmState>,input_id:i32,input_uv:vec2<f32>,missing:vec4<f32>)->vec4<f32>{
    var id=input_id;var uv=input_uv;
    if(!image_below(uv,0x4e6e6b28u)){svm_error(s,128u);return vec4<f32>(0);}// 1.0e9
    if(id<0){
        let tx=i32(uv.x);let ty=i32(uv.y);if(tx<0||ty<0||tx>=10){return missing;}
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
    if(id==i32(KERNEL_IMAGE_NONE)){return missing;}
    if(id<0||u32(id)>=table_count(G_image_textures)){svm_error(s,128u);return vec4<f32>(0);}
    let tex=table_at(G_image_textures,u32(id),SIZE_image_textures);
//#if TILED_IMAGES else     if(load_word(tex+IT_tile_descriptor_offset)!=KERNEL_TILE_LOAD_NONE){svm_error(s,256u);return vec4<f32>(0);}
    var tile:ImageTile;tile.info=NONE;
    if(load_word(tex+IT_tile_descriptor_offset)!=KERNEL_TILE_LOAD_NONE){
        tile=image_tile_map(s,tex,uv);
        if(tile.status==1u){return vec4<f32>(0);}
        if(tile.status==2u){return missing;}
        if(tile.status==3u){return heap_vec4(tex+IT_average_color);}
    }
//#endif
    if(load_word(tex+IT_use_transform_3d)!=0u){svm_error(s,16u);return vec4<f32>(0);}
//#if TILED_IMAGES else     let info_id=load_word(tex+IT_image_info_id);if(info_id==KERNEL_IMAGE_NONE){return missing;}
    var info_id=load_word(tex+IT_image_info_id);if(tile.info!=NONE){info_id=tile.info;}
    if(info_id==KERNEL_IMAGE_NONE){return missing;}
//#endif
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
//#if TILED_IMAGES else     let xy=uv*vec2<f32>(f32(info.width),f32(info.height));
    var xy=uv*vec2<f32>(f32(info.width),f32(info.height));if(tile.info!=NONE){xy=tile.xy;}
//#endif
    if(!image_below(xy,0x4efffffbu)){svm_error(s,128u);return vec4<f32>(0);}// 2147483000.0 (2147483008 as f32)
    var result=image_interpolate(info,xy);
    if(info.kind==IMAGE_DATA_TYPE_FLOAT||info.kind==IMAGE_DATA_TYPE_HALF||info.kind==IMAGE_DATA_TYPE_BYTE||info.kind==IMAGE_DATA_TYPE_USHORT){result.w=1.0;}
    return result;
}
fn svm_image_sample(s:ptr<function,SvmState>,id:i32,uv:vec2<f32>,flags:u32,missing:vec4<f32>)->vec4<f32>{
    if((flags&~(NODE_IMAGE_ALPHA_UNASSOCIATE|NODE_IMAGE_COMPRESS_AS_SRGB))!=0u){svm_error(s,16u);return vec4<f32>(0);}
    var result=image_sample(s,id,uv,missing);let alpha=result.w;
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
// direction_to_equirectangular (kernel/camera/projection.h): binary32 steps in
// the host's order with the precise arctangent/arccosine. The driver
// atan2/acos/division (up to 2e-3 relative on Intel) moved environment-texture
// lookups near bright texels beyond the practical criterion.
fn image_equirectangular(direction:vec3<f32>)->vec2<f32>{
    if(all(direction==vec3<f32>(0))){return vec2<f32>(0);}
//#if TRIG_ATAN2_GUARD else     let u=rounded_divide(rounded_add(trig_atan2(direction.y,direction.x),-3.1415927410125732),-6.2831854820251465);
    let u=rounded_divide(rounded_add(trig_atan2_guarded(direction.y,direction.x),-3.1415927410125732),-6.2831854820251465);
//#endif
    let length=rounded_sqrt(rounded_dot(direction,direction));
    let v=rounded_divide(rounded_add(trig_acos(rounded_divide(direction.z,length)),-3.1415927410125732),-3.1415927410125732);
    return vec2<f32>(u,v);
}
//#if SVM_TEX_ENVIRONMENT
fn environment_projection(s:ptr<function,SvmState>,input:vec3<f32>,projection:u32)->vec2<f32>{
    var v=surface_normalize(input);
    if(projection==0u){return image_equirectangular(v);}
    if(projection!=1u){svm_error(s,16u);return vec2<f32>(0);}
    v.y-=1.0;let div=2.0*sqrt(max(-0.5*v.y,0.0));if(div>0.0){v/=div;}return (v.xz+vec2<f32>(1))*0.5;
}
//#endif
//#if SVM_TEX_IMAGE_BOX
fn image_box(s:ptr<function,SvmState>,co:vec3<f32>,id:i32,blend:f32,flags:u32,missing:vec4<f32>)->vec4<f32>{
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
//#if TILED_IMAGES
        // The dual parts of the three uv pairs above (1 - x negates the derivative).
        {let dx=image_tile_dx;let dy=image_tile_dy;
            var flip=signed_n.x<0.0;var du=vec2<f32>(dx.y,dy.y);var dv=vec2<f32>(dx.z,dy.z);
            if(axis==1u){flip=signed_n.y>0.0;du=vec2<f32>(dx.x,dy.x);}
            else if(axis==2u){flip=signed_n.z>0.0;dv=vec2<f32>(dx.x,dy.x);}
            if(flip){du=-du;}
            image_tile_duv=vec4<f32>(du.x,dv.x,du.y,dv.y);}
//#endif
        result+=weight[axis]*svm_image_sample(s,id,uv,flags,missing);
    }
    return result;
}
//#endif
//#if TILED_IMAGE_PROJECTIONS
// A lookup with derivatives (a tiled image) through the sphere, tube or an environment projection maps its coordinate
// with dual numbers, whose values differ from the plain forms above: adapted from pinned kernel/svm/image.h
// (texco_remap_square, svm_node_tex_image_mapping, svm_node_tex_environment_projection), util/math_dual.h (the
// operators, safe_normalize, inversesqrt, sqrt, atan2, acos, safe_acos, map_to_sphere, map_to_tube) and
// kernel/camera/projection.h (direction_to_equirectangular_range and direction_to_mirrorball of a dual3). A dual1 is
// vec3(value, d/dx, d/dy). Every product that a sum or a difference takes passes the order word first (0 in every
// dispatch, opaque to the compiler), so it is rounded as in the oracle, which has no fused multiply-add; the divisions
// and square roots are the correctly rounded adapters, atan2f and acosf the precise ones (wavefront_trig.wgsl).
fn image_dual_fence(x:vec3<f32>)->vec3<f32>{return bitcast<vec3<f32>>(bitcast<vec3<u32>>(x)^vec3<u32>(load_word(p(17u)+12u)));}
// Each helper returns its dual through the order word too, as the oracle stores it: no product of a later operation
// is reassociated with it.
// dot(a, a) = reduce_add(a * a): the product rule `(uv)' = uv' + u'v` per component, each sum (x + y) + z.
fn image_dual_dot(a:NormalDual)->vec3<f32>{
    let v=image_dual_fence(a.value*a.value);
    let x=image_dual_fence(a.value*a.dx)+image_dual_fence(a.dx*a.value);let y=image_dual_fence(a.value*a.dy)+image_dual_fence(a.dy*a.value);
    return image_dual_fence(vec3<f32>((v.x+v.y)+v.z,(x.x+x.y)+x.z,(y.x+y.y)+y.z));
}
// inversesqrt: f = inversesqrtf(u) (1.0f / sqrtf(u), 0 unless u > 0), chain_rule(u, f, -0.5f * safe_divide(f, u)).
fn image_dual_inversesqrt(u:vec3<f32>)->vec3<f32>{
    var f=0.0;if(u.x>0.0){f=rounded_divide(1.0,rounded_sqrt(u.x));}
    var q=0.0;if(u.x!=0.0){q=rounded_divide(f,u.x);}let k=image_dual_fence(vec3<f32>(-0.5*q)).x;return image_dual_fence(vec3<f32>(f,u.yz*k));
}
// sqrt: chain_rule(u, sqrtf(u), 0.5f / f).
fn image_dual_sqrt(u:vec3<f32>)->vec3<f32>{let f=rounded_sqrt(u.x);return image_dual_fence(vec3<f32>(f,u.yz*rounded_divide(0.5,f)));}
// u / v of two dual1: inv_v = 1.0f / v, u_v = u / v, ((u' - u_v * v') * inv_v).
fn image_dual_divide(u:vec3<f32>,v:vec3<f32>)->vec3<f32>{
    let inv=rounded_divide(1.0,v.x);let q=rounded_divide(u.x,v.x);
    return image_dual_fence(vec3<f32>(q,(u.yz-image_dual_fence(vec3<f32>(0.0,q*v.yz)).yz)*inv));
}
// u * v of two dual1, and a * b of a dual3 and a dual1 (the product rule).
fn image_dual_multiply(u:vec3<f32>,v:vec3<f32>)->vec3<f32>{
    let a=image_dual_fence(u.x*v);let b=image_dual_fence(vec3<f32>(0.0,u.yz*v.x));return image_dual_fence(vec3<f32>(a.x,a.yz+b.yz));
}
fn image_dual_scale(a:NormalDual,b:vec3<f32>)->NormalDual{
    return NormalDual(image_dual_fence(a.value*b.x),image_dual_fence(image_dual_fence(a.value*b.y)+image_dual_fence(a.dx*b.x)),
                      image_dual_fence(image_dual_fence(a.value*b.z)+image_dual_fence(a.dy*b.x)));
}
// atan2(y, x): inv_len = safe_divide(1.0f, sqr(x) + sqr(y)), chain_rule(x, y, atan2f(y, x), -y * inv_len, x * inv_len).
// atan2f(+-0, +0) is +-0 and atan2f(+-0, -0) is +-pi (glibc, C99 Annex F), which the driver's atan2 does not give.
fn image_dual_atan2(y:vec3<f32>,x:vec3<f32>)->vec3<f32>{
    let squares=image_dual_fence(vec3<f32>(x.x*x.x,y.x*y.x,0.0));let sum=squares.x+squares.y;var inv=0.0;if(sum!=0.0){inv=rounded_divide(1.0,sum);}
    let dfd=image_dual_fence(vec3<f32>(-y.x*inv,x.x*inv,0.0));
    let a=image_dual_fence(vec3<f32>(0.0,x.yz*dfd.x));let b=image_dual_fence(vec3<f32>(0.0,y.yz*dfd.y));
    var f=trig_atan2_guarded(y.x,x.x);
    if(y.x==0.0&&x.x==0.0){f=bitcast<f32>((bitcast<u32>(y.x)&0x80000000u)|select(0u,0x40490fdbu,(bitcast<u32>(x.x)&0x80000000u)!=0u));}
    return image_dual_fence(vec3<f32>(f,a.yz+b.yz));
}
// acos: chain_rule(u, acosf(u), -inversesqrtf(1.0f - sqr(u))); safe_acos: acosf(clamp(u, -1, 1)) (clamp is min(max(u, -1), 1)
// of the ternary forms) and a zero derivative where fabsf(u) >= 1.
fn image_dual_acos(u:vec3<f32>,safe:bool)->vec3<f32>{
    let t=1.0-image_dual_fence(vec3<f32>(u.x*u.x)).x;var g=0.0;if(t>0.0){g=rounded_divide(1.0,rounded_sqrt(t));}
    var dfdu=-g;var a=u.x;
    if(safe){if(abs(u.x)>=1.0){dfdu=0.0;}a=select(-1.0,a,a>-1.0);a=select(1.0,a,a<1.0);}
    return image_dual_fence(vec3<f32>(trig_acos(a),u.yz*dfdu));
}
// kind: the Image Texture node's projection (NODE_IMAGE_PROJ_SPHERE or NODE_IMAGE_PROJ_TUBE) or 4 plus the Environment
// Texture node's (0 equirectangular, 1 mirror ball). Returns the uv and sets its derivatives (image_tile_duv).
fn image_tile_projection(s:ptr<function,SvmState>,co:NormalDual,kind:u32)->vec2<f32>{
    var u=vec3<f32>(0);var v=vec3<f32>(0);
    if(kind<4u){
        // texco_remap_square: (co - 0.5) * 2.
        let c=NormalDual(image_dual_fence((co.value-vec3<f32>(0.5))*2.0),image_dual_fence(co.dx*2.0),image_dual_fence(co.dy*2.0));
        let cx=vec3<f32>(c.value.x,c.dx.x,c.dy.x);let cy=vec3<f32>(c.value.y,c.dx.y,c.dy.y);let cz=vec3<f32>(c.value.z,c.dx.z,c.dy.z);
        if(kind==NODE_IMAGE_PROJ_SPHERE){
            // map_to_sphere: u = 0.5f - atan2(x, y) * M_1_2PI_F (0 where x and y are 0), v = 1.0f - safe_acos(z * inversesqrt(l))
            // * M_1_PI_F, both 0 unless l = dot(co, co) > 0.
            let l=image_dual_dot(c);
            if(l.x>0.0){
                if(!(c.value.x==0.0&&c.value.y==0.0)){let a=image_dual_fence(image_dual_atan2(cx,cy)*0.15915494);u=vec3<f32>(0.5-a.x,-a.yz);}
                let b=image_dual_fence(image_dual_acos(image_dual_multiply(cz,image_dual_inversesqrt(l)),true)*0.31830987);v=vec3<f32>(1.0-b.x,-b.yz);
            }
        }
        else if(kind==NODE_IMAGE_PROJ_TUBE){
            // map_to_tube: u = (1.0f - atan2(x, y) / M_PI_F) * 0.5f (a dual divided by a float multiplies by its reciprocal),
            // v = (z + 1.0f) * 0.5f, both 0 unless len(make_float2(co)) > 0.
            let squares=image_dual_fence(vec3<f32>(c.value.x*c.value.x,c.value.y*c.value.y,0.0));
            if(squares.x+squares.y>0.0){
                let a=image_dual_fence(image_dual_atan2(cx,cy)*0.31830987);u=image_dual_fence(vec3<f32>(1.0-a.x,-a.yz)*0.5);v=image_dual_fence(vec3<f32>(cz.x+1.0,cz.yz)*0.5);
            }
        }
        else{svm_error(s,16u);}
    }
    else{
        // safe_normalize: a * inversesqrt(len_squared(a)), zero where len_squared is zero.
        var d=NormalDual(vec3<f32>(0),vec3<f32>(0),vec3<f32>(0));let l=image_dual_dot(co);if(l.x!=0.0){d=image_dual_scale(co,image_dual_inversesqrt(l));}
        if(kind==4u){
            // direction_to_equirectangular_range with the range (-M_2PI_F, M_PI_F, -M_PI_F, M_PI_F), zero for a zero direction:
            // u = (atan2(y, x) - M_PI_F) / -M_2PI_F, v = (acos(z / len(dir)) - M_PI_F) / -M_PI_F, each division a product with
            // the reciprocal (1.0f / -M_2PI_F, 1.0f / -M_PI_F).
            if(any(d.value!=vec3<f32>(0))){
                let a=image_dual_atan2(vec3<f32>(d.value.y,d.dx.y,d.dy.y),vec3<f32>(d.value.x,d.dx.x,d.dy.x));
                u=image_dual_fence(vec3<f32>(a.x-3.1415927410125732,a.yz)*-0.15915493667125702);
                let b=image_dual_acos(image_dual_divide(vec3<f32>(d.value.z,d.dx.z,d.dy.z),image_dual_sqrt(image_dual_dot(d))),false);
                v=image_dual_fence(vec3<f32>(b.x-3.1415927410125732,b.yz)*-0.31830987334251404);
            }
        }
        else if(kind==5u){
            // direction_to_mirrorball: dir.val.y -= 1.0f; dir = dir * 0.5f * inversesqrt(-0.5f * dir.y());
            // u = 0.5f * (dir.x() + 1.0f), v = 0.5f * (dir.z() + 1.0f).
            d.value.y-=1.0;
            d=image_dual_scale(NormalDual(image_dual_fence(d.value*0.5),image_dual_fence(d.dx*0.5),image_dual_fence(d.dy*0.5)),
                           image_dual_inversesqrt(image_dual_fence(vec3<f32>(d.value.y,d.dx.y,d.dy.y)*-0.5)));
            u=image_dual_fence(vec3<f32>(d.value.x+1.0,d.dx.x,d.dy.x)*0.5);v=image_dual_fence(vec3<f32>(d.value.z+1.0,d.dx.z,d.dy.z)*0.5);
        }
        else{svm_error(s,16u);}
    }
    image_tile_duv=vec4<f32>(u.y,v.y,u.z,v.z);return vec2<f32>(u.x,v.x);
}
//#endif
fn svm_image(s:ptr<function,SvmState>,node:u32,mode:u32,derivative:bool){
    // These native records have matching offsets, checked by the layout generator.
    let offset=svm_byte(node+N_TexImage_co);let co=svm_load3(s,offset);
    if(derivative){let dx=svm_load3(s,offset+3u);let dy=svm_load3(s,offset+6u);
        if(!all(abs(dx)<=vec3<f32>(3.402823e38))||!all(abs(dy)<=vec3<f32>(3.402823e38))){svm_error(s,128u);}}
    if((*s).error!=0u){return;}
    let id=bitcast<i32>(load_word(node+N_TexImage_id));let flags=svm_byte(node+N_TexImage_flags);var result:vec4<f32>;
    // The node's Missing colour and Missing Alpha (same offsets in the three image records).
    let missing=vec4<f32>(svm_input3(s,node+N_TexImage_missing),svm_input(s,node+N_TexImage_missing_alpha));
    if((*s).error!=0u){return;}
//#if TILED_IMAGES
    image_tile_dx=vec3<f32>(0);image_tile_dy=vec3<f32>(0);
    if(derivative){image_tile_dx=svm_load3(s,offset+3u);image_tile_dy=svm_load3(s,offset+6u);}
    // The flat projection's uv is the coordinate's xy; the sphere, tube and environment projections set their own
    // (image_tile_projection).
    image_tile_duv=vec4<f32>(image_tile_dx.x,image_tile_dx.y,image_tile_dy.x,image_tile_dy.y);
//#endif
//#if SVM_TEX_IMAGE_BOX else     if(false){}
    if(mode==1u){result=image_box(s,co,id,heap_float(node+N_TexImageBox_blend),flags,missing);}
//#endif
    else{var uv:vec2<f32>;let projection=load_word(node+N_TexImage_projection);
//#if TILED_IMAGE_PROJECTIONS
        if(derivative&&(mode==2u||projection!=NODE_IMAGE_PROJ_FLAT)){uv=image_tile_projection(s,NormalDual(co,image_tile_dx,image_tile_dy),select(projection,4u+projection,mode==2u));}else{
//#endif
//#if SVM_TEX_ENVIRONMENT else         uv=image_projection(s,co,projection);
        if(mode==2u){uv=environment_projection(s,co,projection);}else{uv=image_projection(s,co,projection);}
//#endif
//#if TILED_IMAGE_PROJECTIONS
        }
//#endif
        if((*s).error==0u){result=svm_image_sample(s,id,uv,flags,missing);}}
//#if TILED_IMAGES
    image_tile_duv=vec4<f32>(0);
//#endif
    if((*s).error!=0u){return;}
    svm_optional_store3(s,svm_byte(node+N_TexImage_out_offset),result.xyz);svm_optional_store(s,svm_byte(node+N_TexImage_alpha_offset),result.w);
}
