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
// direction_to_equirectangular (kernel/camera/projection.h): binary32 steps in
// the host's order with the precise arctangent/arccosine. The driver
// atan2/acos/division (up to 2e-3 relative on Intel) moved environment-texture
// lookups near bright texels beyond the practical criterion.
fn image_equirectangular(direction:vec3<f32>)->vec2<f32>{
    if(all(direction==vec3<f32>(0))){return vec2<f32>(0);}
    let u=rounded_divide(rounded_add(trig_atan2(direction.y,direction.x),-3.1415927410125732),-6.2831854820251465);
    let length=rounded_sqrt(rounded_dot(direction,direction));
    let v=rounded_divide(rounded_add(trig_acos(rounded_divide(direction.z,length)),-3.1415927410125732),-3.1415927410125732);
    return vec2<f32>(u,v);
}
fn environment_projection(s:ptr<function,SvmState>,input:vec3<f32>,projection:u32)->vec2<f32>{
    var v=surface_normalize(input);
    if(projection==0u){return image_equirectangular(v);}
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
