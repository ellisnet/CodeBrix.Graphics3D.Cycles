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
    if(domain==ATTR_ELEMENT_OBJECT||domain==ATTR_ELEMENT_MESH){result.value=attribute_fetch(s,desc,0u);return result;}
    if(sd.primitive_type!=PRIMITIVE_TRIANGLE){svm_error(s,16u);return result;}
    if(domain==ATTR_ELEMENT_FACE){result.value=attribute_fetch(s,desc,sd.prim);return result;}
    var indices:vec3<u32>;
    if(domain==ATTR_ELEMENT_VERTEX){
        if(sd.prim>=table_count(G_tri_vindex)){svm_error(s,64u);return result;}
        let address=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);indices=vec3<u32>(load_word(address),load_word(address+4u),load_word(address+8u));
    }else if(domain==ATTR_ELEMENT_CORNER){
        if(sd.prim>(0xffffffffu-2u)/3u){svm_error(s,64u);return result;}indices=vec3<u32>(sd.prim*3u)+vec3<u32>(0,1,2);
    }else{svm_error(s,16u);return result;}
    let a=attribute_fetch(s,desc,indices.x);let b=attribute_fetch(s,desc,indices.y);let c=attribute_fetch(s,desc,indices.z);
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
            value=surface_transform_point(object+O_itfm,value);dx=surface_transform_direction(object+O_itfm,dx);dy=surface_transform_direction(object+O_itfm,dy);}
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
