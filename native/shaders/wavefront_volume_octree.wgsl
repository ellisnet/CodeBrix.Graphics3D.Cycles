// SPDX-FileCopyrightText: 2011-2025 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
//#if VOLUME_HETEROGENEOUS
// Pinned Cycles volume octree tracing (integrator/shade_volume.h OctreeTracing, volume_voxel_get,
// volume_octree_setup, volume_octree_advance without its setup), shared by the volume and shadow modules; each module
// reads its own volume stack through vh_stack_shader and vh_stack_object.
const VH_OCTREE_MAX_DEPTH:u32=7u;const VH_MANTISSA_BITS:u32=23u;const VH_OVERLAP_EXP:f32=5e-4;const VH_MAX_STEPS:i32=1024;const VH_MAX_STEPS_U:u32=1024u;
const VH_THROUGHPUT_EPSILON:f32=1e-6;const VH_PRNG_EXPANSION_ORDER:u32=6u;const VH_PRNG_SHADE_OFFSET:u32=7u;const VH_PRNG_BOUNCE_NUM:u32=16u;
// A run-time value (push word 31, which no dispatch sets) so 1 - r rounds in binary32 as on the CPU.
fn vh_geometric_r()->f32{return bitcast<f32>(bitcast<u32>(0.9)|p(31u));}
struct VhOctree {
    node:u32, t:vec2<f32>, ray_P:vec3<f32>, ray_D:vec3<f32>, current:vec3<u32>, object:u32, shader:u32,
    scale:u32, next_scale:u32, octant_mask:u32, no_overlap:bool, sigma:vec2<f32>,
}
fn vh_node(index:u32)->u32{return table_at(G_volume_tree_nodes,index,SIZE_volume_tree_nodes);}
fn vh_root(object:u32,shader:u32)->u32{
    var root=load_word(table_at(G_volume_tree_root_ids,object,4u));
    for(var guard=0u;guard<0x7fffffffu;guard++){let address=table_at(G_volume_tree_roots,root,SIZE_volume_tree_roots);if((shader&SHADER_MASK)==load_word(address+28u)||root==0u){return address;}root--;}
    return 0u;
}
// OctreeTracing::to_octree_space.
fn vh_to_octree(o:ptr<function,VhOctree>,P:vec3<f32>,D:vec3<f32>,scale:vec3<f32>,translation:vec3<f32>)->bool{
    if(any((bitcast<vec3<u32>>(scale)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u))){return false;}
    var local=rounded_add3(rounded_add3(P,D*(*o).t.x)*scale,translation);var ray_D=D*scale;
    let positive=ray_D>vec3<f32>(0);
    (*o).octant_mask=select(0u,1u,positive.x)|select(0u,2u,positive.y)|select(0u,4u,positive.z);
    local=select(local,vec3<f32>(rounded_add(3.0,-local.x),rounded_add(3.0,-local.y),rounded_add(3.0,-local.z)),positive);
    local=min(local,vec3<f32>(bitcast<f32>(0x3fffffffu)));(*o).current=bitcast<vec3<u32>>(local);
    ray_D=-abs(ray_D);(*o).ray_D=ray_D;(*o).ray_P=rounded_add3(local,-(ray_D*(*o).t.x));
    return all(local>vec3<f32>(1.0));
}
fn vh_octant(o:VhOctree)->u32{
    let s=o.scale;return ((((o.current.x>>s)&1u)|(((o.current.y>>s)&1u)<<1u)|(((o.current.z>>s)&1u)<<2u)))^o.octant_mask;
}
// volume_voxel_get.
fn vh_voxel_get(o:ptr<function,VhOctree>){
    for(var guard=0u;guard<32u;guard++){let first=bitcast<i32>(load_word(vh_node((*o).node)+4u));if(first==-1){return;}(*o).scale-=1u;(*o).node=u32(first)+vh_octant(*o);}
}
// OctreeTracing::ray_voxel_intersect with find_next_pos.
fn vh_voxel_intersect(o:ptr<function,VhOctree>,ray_tmax:f32)->f32{
    let mask=0xffffffffu<<(*o).scale;let bbox=bitcast<vec3<f32>>((*o).current&vec3<u32>(mask));
    var it=vec3<f32>(rounded_divide(rounded_add(bbox.x,-(*o).ray_P.x),(*o).ray_D.x),rounded_divide(rounded_add(bbox.y,-(*o).ray_P.y),(*o).ray_D.y),
                     rounded_divide(rounded_add(bbox.z,-(*o).ray_P.z),(*o).ray_D.z));
    it=select(vec3<f32>(bitcast<f32>(0x7f7fffffu)),it,it>vec3<f32>((*o).t.x));
    let tmax=min(it.x,min(it.y,it.z));
    let half=1.0/f32(2u<<VH_OCTREE_MAX_DEPTH);
    let next=bitcast<vec3<u32>>(select(rounded_add3((*o).ray_D*tmax,(*o).ray_P),
        vec3<f32>(rounded_add(bbox.x,-half),rounded_add(bbox.y,-half),rounded_add(bbox.z,-half)),it==vec3<f32>(tmax)));
    let diff=((*o).current.x^next.x)|((*o).current.y^next.y)|((*o).current.z^next.z);
    (*o).current=next;(*o).next_scale=32u-countLeadingZeros(diff);
    return min(tmax,ray_tmax);
}
// volume_object_get_extrema for camera rays or shaders without a Light Path node: the node's density bounds.
fn vh_extrema(o:VhOctree)->vec2<f32>{
    let node=vh_node(o.node);var density=1.0;
    if(o.object!=NONE){density=object_volume_density(o.object);}
    return vec2<f32>(heap_float(node+8u),heap_float(node+12u))*density;
}
// volume_octree_setup's stack entry that is the active one (volume_octree_setup_single returns true and leaves its local
// tracer as constructed: no node, no entry, the interval {t.min, FLT_MAX}, zero extrema). The loop then takes it like any
// other entry: it replaces the active octree when that one's t.max is FLT_MAX as well, keeping the summed extrema, and the
// set-up reports no segment unless a later stack entry replaces it in turn.
fn vh_octree_skip(g:ptr<function,VhOctree>){
    if(bitcast<f32>(0x7f7fffffu)<=(*g).t.y){
        var local:VhOctree;local.t=vec2<f32>((*g).t.x,bitcast<f32>(0x7f7fffffu));local.scale=VH_MANTISSA_BITS;local.node=NONE;
        local.object=NONE;local.shader=NONE;local.sigma=(*g).sigma;*g=local;
    }
}
// volume_octree_setup<false> over the path's volume stack (entries in stack order).
fn vh_octree_setup(index:u32,origin:vec3<f32>,direction:vec3<f32>,ray_tmax:f32,g:ptr<function,VhOctree>)->bool{
    if((*g).no_overlap){return (*g).t.x<(*g).t.y;}
    let skip_object=(*g).object;let skip_shader=(*g).shader;var i=0u;
    let limit=data_word(D_volume_stack_size);
    for(;i<limit;i++){
        let shader=vh_stack_shader(index,i);if(shader==NONE){break;}
        let object=vh_stack_object(index,i);
        if(object==skip_object&&shader==skip_shader){vh_octree_skip(g);continue;}
        let root=vh_root(object,shader);
        var local:VhOctree;local.t=vec2<f32>((*g).t.x,bitcast<f32>(0x7f7fffffu));local.scale=VH_MANTISSA_BITS;local.node=load_word(root+12u);
        local.object=object;local.shader=shader;
        var P=origin;var D=direction;
        if((load_word(table_at(G_object_flag,object,SIZE_object_flag))&VH_TRANSFORM_APPLIED)==0u){
            let itfm=table_at(G_objects,object,SIZE_objects)+O_itfm;P=surface_transform_point(itfm,origin);D=surface_transform_direction(itfm,direction);
        }
        if(vh_to_octree(&local,P,D,heap_vec3(root),heap_vec3(root+16u))){vh_voxel_get(&local);local.t.y=vh_voxel_intersect(&local,ray_tmax);}
        else{local.t.y=ray_tmax;}
        let extrema=vh_extrema(local);(*g).sigma=(*g).sigma+extrema;
        if(local.t.y<=(*g).t.y){local.sigma=(*g).sigma;*g=local;}
    }
    if(i==1u){(*g).no_overlap=true;}
    return (*g).node!=NONE&&(*g).t.x<(*g).t.y;
}
// volume_octree_advance<false>.
fn vh_octree_advance(index:u32,origin:vec3<f32>,direction:vec3<f32>,ray_tmax:f32,o:ptr<function,VhOctree>)->bool{
    if((*o).t.y>=ray_tmax){return false;}
    if((*o).next_scale>VH_MANTISSA_BITS){
        if(abs(rounded_add((*o).t.y,-ray_tmax))<=VH_OVERLAP_EXP){return false;}
        (*o).t=vec2<f32>((*o).t.y,ray_tmax);(*o).node=load_word(vh_root((*o).object,(*o).shader)+12u);
    }else{
        for(;(*o).scale<(*o).next_scale;(*o).scale++){(*o).node=load_word(vh_node((*o).node));}
        vh_voxel_get(o);(*o).t.x=(*o).t.y;(*o).t.y=vh_voxel_intersect(o,ray_tmax);
    }
    (*o).sigma=vh_extrema(*o);return true;
}
//#if VOLUME_LIGHT_PATH
// volume_object_get_extrema with a Light Path node (VOLUME_LIGHT_PATH): rays other than camera rays estimate the extrema of
// an entry whose shader has the node from its own evaluations (volume_estimate_extrema) instead of the baked node extrema.
fn vlp_estimates(visibility:u32,shader:u32)->bool{
    return (visibility&VLP_VISIBILITY_CAMERA)==0u&&(load_word(table_at(G_shaders,shader&SHADER_MASK,SIZE_shaders)+SH_flags)&VLP_LIGHT_PATH)!=0u;
}
// The entry's octree from the segment start t0 (the local octree of volume_octree_setup).
fn vlp_local(index:u32,origin:vec3<f32>,direction:vec3<f32>,ray_tmax:f32,t0:f32,object:u32,shader:u32)->VhOctree{
    let root=vh_root(object,shader);
    var local:VhOctree;local.t=vec2<f32>(t0,bitcast<f32>(0x7f7fffffu));local.scale=VH_MANTISSA_BITS;local.node=load_word(root+12u);
    local.object=object;local.shader=shader;
    var P=origin;var D=direction;
    if((load_word(table_at(G_object_flag,object,SIZE_object_flag))&VH_TRANSFORM_APPLIED)==0u){
        let itfm=table_at(G_objects,object,SIZE_objects)+O_itfm;P=surface_transform_point(itfm,origin);D=surface_transform_direction(itfm,direction);
    }
    if(vh_to_octree(&local,P,D,heap_vec3(root),heap_vec3(root+16u))){vh_voxel_get(&local);local.t.y=vh_voxel_intersect(&local,ray_tmax);}
    else{local.t.y=ray_tmax;}
    return local;
}
// volume_estimate_extrema's samples: count (1 homogeneous, 4 otherwise), the shading offset, the step.
struct VlpEstimate {t:vec2<f32>, ext:vec2<f32>, k:u32, n:u32, shade:f32, step:f32, object:u32, shader:u32, homogeneous:bool, resume:u32}
fn vlp_start(t:vec2<f32>,object:u32,shader:u32,sample:u32,pixel:u32,offset:u32,resume:u32)->VlpEstimate{
    var e:VlpEstimate;e.t=t;e.object=object;e.shader=shader;e.resume=resume;e.k=0u;
    e.homogeneous=(load_word(table_at(G_shaders,shader&SHADER_MASK,SIZE_shaders)+SH_flags)&VH_HETEROGENEOUS)==0u;
    e.n=select(4u,1u,e.homogeneous);e.shade=0.5;
    if(!e.homogeneous){e.shade=native_rng_pattern(sample,pixel,offset+VH_PRNG_SHADE_OFFSET,2u).y;}
    e.step=rounded_divide(t.y-t.x,f32(e.n));e.ext=vec2<f32>(bitcast<f32>(0x7f7fffffu),-bitcast<f32>(0x7f7fffffu));return e;
}
// t.min + (shade_offset + i) * step_size, the quotient correctly rounded and the sum never contracted (the estimate's
// positions decide the majorant, whose last bits steer the whole walk).
fn vlp_shade_t(e:VlpEstimate)->f32{return rounded_add(e.t.x,(e.shade+f32(e.k))*e.step);}
// One evaluation of the entry (the reduced maxima of its extinction and emission); true when the estimate is complete.
fn vlp_add(e:ptr<function,VlpEstimate>,sigma_t:vec3<f32>,emission:vec3<f32>)->bool{
    let v=max(max(sigma_t.x,max(sigma_t.y,sigma_t.z)),max(emission.x,max(emission.y,emission.z)));
    (*e).ext=vec2<f32>(min((*e).ext.x,v),max((*e).ext.y,v));(*e).k++;
    if((*e).k<(*e).n){return false;}
    if(!(*e).homogeneous){(*e).ext.y=max(0.5,(*e).ext.y*1.5);}
    return true;
}
//#endif
// path_state_rng_scramble (hash_hp_seeded_uint, offset divisible by 4).
fn vh_scramble(offset:u32,seed:u32)->u32{
    var i=offset^(seed^(seed<<19u));i^=i>>16u;i*=0x21f0aaadu;i^=i>>15u;i*=0xd35a2d97u;i^=i>>15u;return (i^0xe6fe3bebu)&~3u;
}
//#if VOLUME_RAY_MARCHING
// volume_stack_step_size: the smallest step size of the heterogeneous entries (FLT_MAX: one step).
fn vr_step_size(index:u32,entries:u32)->f32{
    var size=bitcast<f32>(0x7f7fffffu);
    for(var slot=0u;slot<entries;slot++){
        let shader=vh_stack_shader(index,slot);if(shader==NONE){break;}
        if((load_word(table_at(G_shaders,shader&SHADER_MASK,SIZE_shaders)+SH_flags)&VH_HETEROGENEOUS)!=0u){
            size=min(size,heap_float(table_at(G_volume_step_size,vh_stack_object(index,slot),4u)));}
    }
    return size;
}
//#endif
//#endif
