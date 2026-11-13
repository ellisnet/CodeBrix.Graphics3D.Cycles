// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles geom/attribute.h, geom/triangle.h, geom/primitive.h,
// svm/attribute.h, svm/util.h, util/color.h and
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
//#if CURVES
        if((sd.primitive_type&PRIMITIVE_CURVE)!=0u){return curve_attribute_read(s,desc,domain);}
//#endif
//#if POINTS
        // point_attribute (geom/point.h): a value per point, otherwise zero.
        if((sd.primitive_type&PRIMITIVE_POINT)!=0u){if(domain==ATTR_ELEMENT_VERTEX){result.value=attribute_fetch(s,desc,sd.prim);}return result;}
//#endif
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
    // Cycles triangle_interpolate (1-u-v)*f0 + u*f1 + v*f2 and triangle_attribute_dfdx/dfdy,
    // summed left to right with every product rounded first: each product passes
    // the order word (header word 3, always 0) so no driver can fuse it into the
    // sum or reassociate across it (the svmn_order mechanism).
    let order=load_word(p(17u)+12u);
    let w=attribute_order(vec4<f32>(1.0-sd.u),order).x-sd.v;
    result.value=(attribute_order(w*a,order)+attribute_order(sd.u*b,order))+attribute_order(sd.v*c,order);
    result.dx=(attribute_order(sd.du.x*b,order)+attribute_order(sd.dv.x*c,order))-attribute_order((sd.du.x+sd.dv.x)*a,order);
    result.dy=(attribute_order(sd.du.y*b,order)+attribute_order(sd.dv.y*c,order))-attribute_order((sd.du.y+sd.dv.y)*a,order);
    return result;
}
//#if CURVES
// curve_attribute (geom/curve.h): per key mix(f0,f1,u) with df = du*(f1-f0), per curve the value, otherwise zero.
fn curve_attribute_read(s:ptr<function,SvmState>,desc:AttributeDesc,domain:u32)->AttributeValue{
    var result:AttributeValue;let sd=(*s).geometry;let o=cv_order();
    if(domain==CV_ATTR_ELEMENT_CURVE_KEY){
        if(sd.prim>=table_count(G_curves)){svm_error(s,64u);return result;}
        let k0=load_word(table_at(G_curves,sd.prim,16u)+4u)+(sd.primitive_type>>PRIMITIVE_NUM_BITS);
        var f0=vec4<f32>(0);var f1=vec4<f32>(0);let count=rounded_steps(2u);
        for(var k=0u;k<count;k++){let value=attribute_fetch(s,desc,k0+select(1u,0u,k==0u));if(k==0u){f0=value;}else if(k==1u){f1=value;}}
        let delta=cvb4(f1-f0,o);
        result.value=cvb4(f0+cvb4(sd.u*delta,o),o);result.dx=cvb4(sd.du.x*delta,o);result.dy=cvb4(sd.du.y*delta,o);
    }else if(domain==CV_ATTR_ELEMENT_CURVE){result.value=attribute_fetch(s,desc,sd.prim);}
    return result;
}
//#endif
//#if SVM_NODE_POINT_INFO
// svm_node_point_info (svm/geometry.h, geom/point.h point_position, point_radius). Random is an attribute node; its case
// stores nothing, as pinned.
fn svm_point_info(s:ptr<function,SvmState>,node:u32){
    let kind=load_word(node+N_PointInfo_info_type);let out=svm_byte(node+N_PointInfo_out_offset);
    if(kind==PT_INFO_POINT_POSITION){
        var data=vec3<f32>(0);
//#if POINTS
        let sd=(*s).geometry;
        if((sd.primitive_type&PRIMITIVE_POINT)!=0u){
            data=point_record(sd.object_id,sd.prim,sd.primitive_type,sd.time).xyz;
            if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)==0u){data=surface_object_point(sd,data,false);}
        }
//#endif
        svm_store3(s,out,data);
    }else if(kind==PT_INFO_POINT_RADIUS){
        var data=0.0;
//#if POINTS
        let sd=(*s).geometry;
        if((sd.primitive_type&PRIMITIVE_POINT)!=0u&&sd.object_id<table_count(G_objects)){
            data=point_at(bitcast<i32>(load_word(table_at(G_objects,sd.object_id,SIZE_objects)+O_position_offset)),sd.prim).w;
            if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)==0u){let o=pt_order();
                // 1.0f / M_SQRT3_F, the constant the compiler folds.
                let normalized=ptb(data*bitcast<f32>(0x3f13cd3au),o);
                data=pt_len(surface_object_direction(sd,vec3<f32>(normalized),false),o);
            }
        }
//#endif
        svm_store(s,out,data);
    }
}
//#endif
//#if SVM_NODE_PARTICLE_INFO
// lookup3 final (util/hash.h hash_uint2): kx is the particle index, ky is 0.
fn pi_rot(x:u32,k:u32)->u32{return (x<<k)|(x>>(32u-k));}
fn pi_hash(kx:u32)->u32{
    var a=0xdeadbeefu+(2u<<2u)+13u;var b=a;var c=a;a+=kx;
    c^=b;c-=pi_rot(b,14u);a^=c;a-=pi_rot(c,11u);b^=a;b-=pi_rot(a,25u);c^=b;c-=pi_rot(b,16u);a^=c;a-=pi_rot(c,4u);b^=a;b-=pi_rot(a,14u);c^=b;c-=pi_rot(b,24u);
    return c;
}
// (float)n rounded to nearest, ties to even (the C conversion).
fn pi_u32_float(n:u32)->f32{
    if(n<0x1000000u){return f32(n);}
    let drop=8u-countLeadingZeros(n);var m=n>>drop;let rest=n&((1u<<drop)-1u);let half=1u<<(drop-1u);
    if(rest>half||(rest==half&&(m&1u)!=0u)){m+=1u;}
    return ldexp(f32(m),i32(drop));
}
// svm_node_particle_info (svm/geometry.h, geom/object.h): object_particle_id is 0, the dummy particle, without an object.
fn svm_particle_info(s:ptr<function,SvmState>,node:u32){
    let kind=load_word(node+N_ParticleInfo_info_type);let out=svm_byte(node+N_ParticleInfo_out_offset);let object=(*s).geometry.object_id;
    var particle=0u;
    if(object!=NONE){if(object>=table_count(G_objects)){svm_error(s,64u);return;}particle=load_word(table_at(G_objects,object,SIZE_objects)+PI_O_particle_index);}
    if(particle>=table_count(G_particles)){svm_error(s,64u);return;}
    let at=table_at(G_particles,particle,SIZE_particles);
    switch(kind){
        case PI_INFO_PAR_INDEX:{svm_store(s,out,pi_u32_float(load_word(at+PI_P_index)));}
        // uint_to_float_incl: n * (1.0f / (float)0xFFFFFFFFu), which is 2^-32.
        case PI_INFO_PAR_RANDOM:{svm_store(s,out,pi_u32_float(pi_hash(load_word(at+PI_P_index)))*2.3283064365386963e-10);}
        case PI_INFO_PAR_AGE:{svm_store(s,out,heap_float(at+PI_P_age));}
        case PI_INFO_PAR_LIFETIME:{svm_store(s,out,heap_float(at+PI_P_lifetime));}
        case PI_INFO_PAR_LOCATION:{svm_store3(s,out,heap_vec3(at+PI_P_location));}
        case PI_INFO_PAR_SIZE:{svm_store(s,out,heap_float(at+PI_P_size));}
        case PI_INFO_PAR_VELOCITY:{svm_store3(s,out,heap_vec3(at+PI_P_velocity));}
        case PI_INFO_PAR_ANGULAR_VELOCITY:{svm_store3(s,out,heap_vec3(at+PI_P_angular_velocity));}
        default:{}
    }
}
//#endif
//#if SVM_NODE_HAIR_INFO
// svm_node_hair_info (svm/geometry.h, geom/curve.h curve_thickness, curve_tangent_normal). Intercept, length and random
// are attribute nodes; their cases here store nothing, as pinned.
fn svm_hair_info(s:ptr<function,SvmState>,node:u32){
    let kind=load_word(node+N_HairInfo_info_type);let out=svm_byte(node+N_HairInfo_out_offset);let sd=(*s).geometry;
    let curve=(sd.primitive_type&HI_PRIMITIVE_CURVE)!=0u;
    if(kind==CV_INFO_CURVE_IS_STRAND){svm_store(s,out,select(0.0,1.0,curve));}
    else if(kind==CV_INFO_CURVE_THICKNESS){
        var data=0.0;
//#if CURVES
        if(curve){data=curve_thickness(s);}
//#endif
        svm_store(s,out,data);
    }
    else if(kind==CV_INFO_CURVE_TANGENT_NORMAL){
        var data=vec3<f32>(0);
//#if CURVES
        if(curve){let o=cv_order();
            let projection=rounded_divide(cv_dot(sd.dPdu,-sd.wi,o),cv_dot(sd.dPdu,sd.dPdu,o));
            data=cv_normalize(-cvb3(-sd.wi-cvb3(sd.dPdu*projection,o),o),o);}
//#endif
        svm_store3(s,out,data);
    }
}
//#if CURVES
fn curve_thickness(s:ptr<function,SvmState>)->f32{
    let sd=(*s).geometry;let o=cv_order();
    if(sd.prim>=table_count(G_curves)){svm_error(s,64u);return 0.0;}
    let k0=load_word(table_at(G_curves,sd.prim,16u)+4u)+(sd.primitive_type>>PRIMITIVE_NUM_BITS);
//#if !CURVE_MOTION
    let position_offset=bitcast<i32>(load_word(table_at(G_objects,sd.object_id,SIZE_objects)+O_position_offset));
    let w0=curve_key(position_offset,k0).w;let w1=curve_key(position_offset,k0+1u).w;
//#endif
//#if CURVE_MOTION
    var w0=0.0;var w1=0.0;
    if((sd.primitive_type&PRIMITIVE_MOTION)!=0u){
        let frames=triangle_motion_time(sd.object_id,sd.time);if(frames.error!=0u){svm_error(s,64u);return 0.0;}
        w0=curve_motion_key(sd.object_id,frames,k0,o).w;w1=curve_motion_key(sd.object_id,frames,k0+1u,o).w;
    }else{
        let position_offset=bitcast<i32>(load_word(table_at(G_objects,sd.object_id,SIZE_objects)+O_position_offset));
        w0=curve_key(position_offset,k0).w;w1=curve_key(position_offset,k0+1u).w;
    }
//#endif
    let r=cvb(2.0*cvb(cvb(cvb(w1-w0,o)*sd.u,o)+w0,o),o);
    if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u){return r;}
    let normalized=cvb(r*0.5773502588272095,o);
    return cv_len(surface_object_direction(sd,vec3<f32>(normalized),false),o);
}
//#endif
//#endif
fn attribute_order(x:vec4<f32>,order:u32)->vec4<f32>{return bitcast<vec4<f32>>(bitcast<vec4<u32>>(x)^vec4<u32>(order));}
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
    // svm_node_attr_volume: an attribute a volume shader does not find is the node's Missing value (Fac: its average,
    // Alpha: Missing Alpha as stored, not averaged).
    if((*s).volume&&!desc.found){
        let offset=svm_byte(node+N_Attr_out_offset);
        if(output==NODE_ATTR_OUTPUT_FLOAT3){svm_store3(s,offset,svm_input3(s,node+N_Attr_missing));}
        else if(output==NODE_ATTR_OUTPUT_FLOAT){let missing=svm_input3(s,node+N_Attr_missing);svm_store(s,offset,(missing.x+missing.y+missing.z)*(1.0/3.0));}
        else{svm_store(s,offset,svm_input(s,node+N_Attr_missing_alpha));}
        return;
    }
    if(id==ATTR_STD_GENERATED&&!desc.found){
        let sd=(*s).geometry;value=sd.P;dx=sd.dPdu*sd.du.x+sd.dPdv*sd.dv.x;dy=sd.dPdu*sd.du.y+sd.dPdv*sd.dv.y;
        if(sd.object_id!=NONE){let object=table_at(G_objects,sd.object_id,SIZE_objects);
            value=surface_object_point(sd,value,true);dx=surface_object_direction(sd,dx,true);dy=surface_object_direction(sd,dy,true);}
    }else if(!desc.found){
        // svm_node_attr_surface_eval: an attribute that is not found is the node's Missing value (Alpha output: Missing
        // Alpha in every component), with zero derivatives.
        if(output==NODE_ATTR_OUTPUT_FLOAT_ALPHA){value=vec3<f32>(svm_input(s,node+N_Attr_missing_alpha));}
        else{value=svm_input3(s,node+N_Attr_missing);}
        dx=vec3<f32>(0);dy=vec3<f32>(0);
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
