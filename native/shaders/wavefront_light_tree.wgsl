// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Light tree selection and its forward pdf, adapted from the pinned
// kernel/light/tree.h (light_tree_sample<false>, light_tree_pdf<false>) and the
// *_light_tree_parameters functions of kernel/light/{point,spot,area,sun,
// background,triangle}.h. The CPU oracle is the fixed-baseline SSE4.2 build
// without contraction: float3 sums and dots are (x+y)+z, len is sqrt of that
// dot, normalize divides each component, normalize_len and safe_normalize
// multiply by the reciprocal, and every sum of products rounds both products
// (rounded_add keeps GPU compilers from fusing or reassociating them). The
// selection is a sequence of comparisons on these values, so the emitter chosen
// for a random number is the CPU's when the arithmetic is.
//
// Drivers inline every call. The whole traversal (sampling and forward pdf,
// inner nodes and leaves, every emitter kind) therefore evaluates importance
// through ONE call site (light_tree_walk's item loop).
//
// Not ported: in_volume_segment (volumes with direct light stay refused on the
// host) and the dedicated shadow-linking trees.

// util/math_fast.h fast_sincosf (x = sin, y = cos) and fast_cosf: SLEEF argument
// reduction and polynomials, each madd an unfused product and sum.
fn lt_fast_reduce(angle:f32)->vec3<f32>{
    let q=round(angle*bitcast<f32>(0x3ea2f983u));let n=i32(q);
    var x=rounded_add(q*bitcast<f32>(0xc0490000u),angle);x=rounded_add(q*bitcast<f32>(0xba7da000u),x);
    x=rounded_add(q*bitcast<f32>(0xb4222000u),x);x=rounded_add(q*bitcast<f32>(0xacb4611au),x);
    let half_pi=bitcast<f32>(0x3fc90fdbu);x=rounded_add(half_pi,-rounded_add(half_pi,-x));
    return vec3<f32>(x,x*x,select(1.0,-1.0,(n&1)!=0));
}
fn lt_fast_cos_poly(s:f32)->f32{
    var u=bitcast<f32>(0xb491ed89u);u=rounded_add(u*s,bitcast<f32>(0x37d0078bu));u=rounded_add(u*s,bitcast<f32>(0xbab60b58u));
    u=rounded_add(u*s,bitcast<f32>(0x3d2aaaaau));u=rounded_add(u*s,-0.5);return rounded_add(u*s,1.0);
}
fn lt_fast_cos(angle:f32)->f32{let r=lt_fast_reduce(angle);return clamp(lt_fast_cos_poly(r.y)*r.z,-1.0,1.0);}
fn lt_fast_sincos(angle:f32)->vec2<f32>{
    let r=lt_fast_reduce(angle);let x=r.x*r.z;let s=r.y;
    var u=bitcast<f32>(0x362f0a87u);u=rounded_add(u*s,bitcast<f32>(0xb94fbaebu));u=rounded_add(u*s,bitcast<f32>(0x3c088777u));
    u=rounded_add(u*s,bitcast<f32>(0xbe2aaaa6u));u=rounded_add(s*(u*x),x);
    return vec2<f32>(clamp(u,-1.0,1.0),clamp(lt_fast_cos_poly(s)*r.z,-1.0,1.0));
}
// util/math_fast.h fast_acosf.
fn lt_fast_acos(value:f32)->f32{
    let f=abs(value);let m=select(1.0,rounded_add(1.0,-rounded_add(1.0,-f)),f<1.0);
    var poly=rounded_add(bitcast<f32>(0x3d9fb43bu),m*bitcast<f32>(0xbcb1485du));poly=rounded_add(bitcast<f32>(0xbe5a6b93u),m*poly);
    poly=rounded_add(bitcast<f32>(0x3fc90fdbu),m*poly);let a=rounded_sqrt(rounded_add(1.0,-m))*poly;
    return select(a,rounded_add(bitcast<f32>(0x40490fdbu),-a),value<0.0);
}
// glibc cosf (spot energy and the importance cone test) on [0, pi].
fn lt_cosf(angle:f32)->f32{return native_disk_sincos(angle).y;}
fn lt_sin_from_cos(c:f32)->f32{return rounded_sqrt(max(rounded_add(1.0,-(c*c)),0.0));}
fn lt_len(v:vec3<f32>)->f32{return rounded_sqrt(rounded_dot(v,v));}
// normalize_len: a * (1 / len).
fn lt_normalize_len(v:vec3<f32>)->vec4<f32>{let t=lt_len(v);return vec4<f32>(v*rounded_divide(1.0,t),t);}
// safe_normalize_len: a / len, per component.
fn lt_safe_normalize_len(v:vec3<f32>)->vec4<f32>{
    let t=lt_len(v);if(t==0.0){return vec4<f32>(v,t);}
    return vec4<f32>(rounded_divide(v.x,t),rounded_divide(v.y,t),rounded_divide(v.z,t),t);
}
fn lt_safe_normalize(v:vec3<f32>)->vec3<f32>{let t=lt_len(v);if(t==0.0){return v;}return v*rounded_divide(1.0,t);}
fn lt_smoothstep(f:f32)->f32{if(f<=0.0){return 0.0;}if(f>=1.0){return 1.0;}let ff=f*f;return rounded_add(3.0*ff,-((2.0*ff)*f));}
fn lt_node(index:u32)->u32{return table_at(G_light_tree_nodes,index,SIZE_light_tree_nodes);}
fn lt_emitter(index:u32)->u32{return table_at(G_light_tree_emitters,index,SIZE_light_tree_emitters);}
fn lt_type(node:u32)->u32{return load_word(node+LTN_type)&255u;}
fn lt_is_mesh(emitter:u32)->bool{return bitcast<i32>(load_word(emitter+LTE_id))>=0&&load_word(emitter+LTE_object)==NONE;}
fn lt_is_triangle(emitter:u32)->bool{return bitcast<i32>(load_word(emitter+LTE_id))>=0&&load_word(emitter+LTE_object)!=NONE;}
// SSE transform_point: translation joins the z product first (madd z, y, x).
fn lt_transform_point(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    if(TRIANGLE_LIGHT_NATIVE_SSE!=0u){
        let z=rounded_add3(v.z*vec3<f32>(t.x.z,t.y.z,t.z.z),vec3<f32>(t.x.w,t.y.w,t.z.w));
        return rounded_add3(v.x*vec3<f32>(t.x.x,t.y.x,t.z.x),rounded_add3(v.y*vec3<f32>(t.x.y,t.y.y,t.z.y),z));
    }
    return vec3<f32>(motion_dot4(vec4<f32>(v,1),t.x),motion_dot4(vec4<f32>(v,1),t.y),motion_dot4(vec4<f32>(v,1),t.z));
}
// light_tree_to_local_space<false>: a mesh light's subtree lives in object space.
fn lt_to_local(object:u32,P:ptr<function,vec3<f32>>,N:ptr<function,vec3<f32>>){
    let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));if((flags&LT_SD_OBJECT_TRANSFORM_APPLIED)!=0u){return;}
    let at=table_at(G_objects,object,SIZE_objects);var itfm=motion_transform_load(at+LT_O_itfm);
    if((flags&LT_SD_OBJECT_MOTION)!=0u){itfm=motion_inverse(light_tree_object_motion(object));}
    *P=lt_transform_point(itfm,*P);
    if(any(*N!=vec3<f32>(0))){*N=rounded_normalize(motion_transpose(motion_transform_load(at+LT_O_tfm),*N));}
}
// Tree root of the receiver's light set (light_tree_root_node_index).
fn lt_root(receiver:u32)->u32{
    if(!light_link_enabled()){return 0u;}
    return data_word(LT_D_link_sets+light_link_receiver_set(receiver)*LT_LINK_SET_STRIDE);
}
struct LtCone {axis:vec3<f32>,theta_o:f32,theta_e:f32}
// light_tree_importance<false>: (maximum, minimum) importance.
// o = fast_sincosf(theta_o); cos_spread = fast_cosf(theta_e - theta_o); cos_sum = cosf(theta_o + theta_e)
// (only read when that sum is at most pi): computed once by the caller.
fn lt_importance(N:vec3<f32>,transmission:bool,to_centroid:vec3<f32>,cos_u:f32,cone:LtCone,max_distance:f32,min_distance:f32,energy:f32,o:vec2<f32>,cos_spread:f32,cos_sum:f32)->vec2<f32>{
    let sin_u=lt_sin_from_cos(cos_u);
    var cos_i=rounded_dot(to_centroid,N);if(transmission){cos_i=abs(cos_i);}
    let sin_i=lt_sin_from_cos(cos_i);
    let min_incidence=select(rounded_add(cos_i*cos_u,sin_i*sin_u),1.0,cos_i>=cos_u);
    if(!transmission&&min_incidence<0.0){return vec2<f32>(0);}
    let max_incidence=max(rounded_add(cos_i*cos_u,-(sin_i*sin_u)),0.0);
    var cos_t=1.0;var sin_t=0.0;
    if(any(cone.axis!=-to_centroid)){cos_t=rounded_dot(cone.axis,-to_centroid);sin_t=lt_sin_from_cos(cos_t);}
    let cos_minus=rounded_add(cos_t*cos_u,sin_t*sin_u);
    var min_outgoing=1.0;
    if(!(cos_t>=cos_u||cos_minus>=o.y)){
        let sum=rounded_add(cone.theta_o,cone.theta_e);
        if(sum>bitcast<f32>(0x40490fdbu)||cos_minus>cos_sum){min_outgoing=rounded_add(cos_minus*o.y,lt_sin_from_cos(cos_minus)*o.x);}
        else{return vec2<f32>(0);}
    }
    let maximum=abs(((min_incidence*energy)*min_outgoing)*rounded_divide(1.0,min_distance*min_distance));
    var minimum=0.0;let cos_plus=rounded_add(cos_t*cos_u,-(sin_t*sin_u));let spread=rounded_add(cone.theta_e,-cone.theta_o);
    if(!(spread<0.0||cos_t<0.0||cos_u<0.0||cos_plus<cos_spread)){
        let max_outgoing=rounded_add(cos_plus*o.y,-(lt_sin_from_cos(cos_plus)*o.x));
        minimum=abs(rounded_divide((max_incidence*energy)*max_outgoing,max_distance*max_distance));
    }
    return vec2<f32>(maximum,minimum);
}
// Importance of a node (light_tree_node_importance) or of an emitter
// (light_tree_emitter_importance: mesh lights use their subtree's node). Each
// arithmetic helper has one call site (drivers inline every call): the centroid
// direction, the corner loop (area light 4, triangle 3), the two spot-light
// arccosines and cosines, and the distant cosine are shared across kinds.
fn lt_item_importance(P:vec3<f32>,N:vec3<f32>,transmission:bool,item:u32,item_is_node:bool)->vec2<f32>{
    var node=item;var is_node=item_is_node;
    if(!is_node){let e=lt_emitter(item);if(lt_is_mesh(e)){node=load_word(e+LTE_mesh_node);is_node=true;}}
    var cone:LtCone;var to_centroid:vec3<f32>;var cos_u=0.0;var max_distance=0.0;var min_distance=0.0;var energy=0.0;var visible=true;
    // kind: 0 node, 1 distant node, then LT_LIGHT_* + 2 for lamps, 16 triangle.
    var kind=0u;var light=0u;var centroid=vec3<f32>(0);var corners=0u;var spot_acos=vec2<f32>(0);var v:array<vec3<f32>,3>;var eu=vec3<f32>(0);var ev=vec3<f32>(0);
    if(is_node){
        let at=lt_node(node);cone=LtCone(heap_vec3(at+LTN_axis),heap_float(at+LTN_theta_o),heap_float(at+LTN_theta_e));energy=heap_float(at+LTN_energy);
        if(lt_type(at)==LT_LIGHT_TREE_DISTANT){kind=1u;to_centroid=-cone.axis;max_distance=1.0;}
        else{
            let low=heap_vec3(at+LTN_bbox_min);let high=heap_vec3(at+LTN_bbox_max);centroid=0.5*rounded_add3(low,high);
            visible=transmission||rounded_add(rounded_dot(N,centroid-P),rounded_dot(abs(N),abs(high-centroid)))>0.0;
            let unit=lt_normalize_len(centroid-P);to_centroid=unit.xyz;max_distance=unit.w;
            let outside=centroid-P;let radius=high-centroid;let d2=rounded_dot(outside,outside);let r2=rounded_dot(radius,radius);
            cos_u=select(rounded_sqrt(max(rounded_add(1.0,-rounded_divide(r2,d2)),0.0)),-1.0,d2<=r2);
            max_distance=max(0.5*lt_len(centroid-high),max_distance);
        }
        min_distance=max_distance;
    }else{
        let e=lt_emitter(item);cone=LtCone(vec3<f32>(0),heap_float(e+LTE_theta_o),heap_float(e+LTE_theta_e));energy=heap_float(e+LTE_energy);
        let id=bitcast<i32>(load_word(e+LTE_id));
        if(id<0){
            light=table_at(G_lights,~bitcast<u32>(id),SIZE_lights);let type_=load_word(light+L_type);kind=type_+2u;centroid=heap_vec3(light+L_co);
            if(type_==LT_LIGHT_SPOT){cone.axis=heap_vec3(light+L_spot_dir);}
            else if(type_==LT_LIGHT_POINT){cone.axis=lt_safe_normalize(P-centroid);}
            else if(type_==LT_LIGHT_AREA){cone.axis=heap_vec3(light+L_area_dir);corners=4u;
                eu=heap_vec3(light+L_area_axis_u)*heap_float(light+L_area_len_u);ev=heap_vec3(light+L_area_axis_v)*heap_float(light+L_area_len_v);}
            else if(type_==LT_LIGHT_BACKGROUND){centroid=vec3<f32>(0,0,1);cone.axis=vec3<f32>(0,0,-1);cos_u=-1.0;max_distance=1.0;min_distance=1.0;to_centroid=-centroid;}
            else if(type_==LT_LIGHT_SUN){cone.axis=centroid;min_distance=1.0;to_centroid=-centroid;}
            else{return vec2<f32>(0);}
        }else{
            // Emissive triangle in its mesh's (local) space: compute_emitter_centroid_and_dir.
            kind=16u;corners=3u;let object=load_word(e+LTE_object);let at=table_at(G_tri_vindex,u32(id),SIZE_tri_vindex);
            v[0]=triangle_position(object,load_word(at),0u);v[1]=triangle_position(object,load_word(at+4u),0u);v[2]=triangle_position(object,load_word(at+8u),0u);
            let sum=rounded_add3(rounded_add3(v[0],v[1]),v[2]);centroid=vec3<f32>(rounded_divide(sum.x,3.0),rounded_divide(sum.y,3.0),rounded_divide(sum.z,3.0));
            let sampling=load_word(e+LTE_emission_sampling);
            if(sampling==LT_EMISSION_SAMPLING_FRONT||sampling==LT_EMISSION_SAMPLING_BACK){
                var dir=lt_safe_normalize(rounded_cross(v[1]-v[0],v[2]-v[0]));if(sampling==LT_EMISSION_SAMPLING_BACK){dir=-dir;}
                let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));
                if((flags&LT_SD_OBJECT_TRANSFORM_APPLIED)!=0u&&(flags&LT_SD_OBJECT_NEGATIVE_SCALE)!=0u){dir=-dir;}cone.axis=dir;
            }else{cone.axis=lt_safe_normalize(v[0]-v[1]);}
        }
        // *_light_tree_parameters for point, spot, area and triangle emitters.
        if(kind==LT_LIGHT_POINT+2u||kind==LT_LIGHT_SPOT+2u||kind==LT_LIGHT_AREA+2u||kind==16u){
            let unit=lt_safe_normalize_len(centroid-P);to_centroid=unit.xyz;max_distance=unit.w;min_distance=unit.w;
        }
        if(corners!=0u){
            cos_u=bitcast<f32>(0x7f7fffffu);var above=false;
            for(var i=0u;i<corners;i++){
                var corner=v[min(i,2u)];
                if(kind!=16u){corner=rounded_add3(rounded_add3((f32(i&1u)-0.5)*eu,(0.5*f32(i32(i&2u)-1))*ev),centroid);}
                let to_corner=lt_safe_normalize_len(corner-P);cos_u=min(cos_u,rounded_dot(to_centroid,to_corner.xyz));max_distance=max(max_distance,to_corner.w);
                above=above||rounded_dot(to_corner.xyz,N)>0.0;
            }
            if(kind==16u){visible=(cone.theta_o!=0.0||rounded_dot(cone.axis,to_centroid)<0.0)&&above;}
            else{visible=rounded_dot(cone.axis,to_centroid)<0.0&&rounded_add(rounded_add(rounded_dot(N,centroid-P),abs(rounded_dot(N,eu))),abs(rounded_dot(N,ev)))>0.0;}
        }
        if(kind==LT_LIGHT_POINT+2u||kind==LT_LIGHT_SPOT+2u){
            let m=min_distance;let radius=heap_float(light+L_radius);
            if(load_word(light+L_is_sphere)!=0u){
                if(m>radius){cos_u=lt_sin_from_cos(rounded_divide(radius,m));
                    max_distance=select(m*rounded_divide(1.0,cos_u),rounded_divide(m,cos_u),kind==LT_LIGHT_POINT+2u);}
                else{cos_u=-1.0;max_distance=rounded_divide(radius,bitcast<f32>(0x3fb504f3u));min_distance=max_distance;}
            }else{let hypotenuse=rounded_sqrt(rounded_add(radius*radius,m*m));cos_u=rounded_divide(m,hypotenuse);max_distance=hypotenuse;}
            if(kind==LT_LIGHT_SPOT+2u){
                // fast_acosf(axis . -to_centroid) and fast_acosf(cos_u) of the spot blend scaling.
                for(var k=0u;k<2u;k++){spot_acos[k]=lt_fast_acos(select(cos_u,rounded_dot(cone.axis,-to_centroid),k==0u));}
            }else{cone.theta_o=0.0;}
        }
        visible=visible||transmission;
    }
    // glibc cosf: the spot's cos(max(0, acos - acos)) and cos(theta_e), and the
    // importance cone's cos(theta_o + theta_e) (read only when the sum is at most pi).
    var c=vec3<f32>(0);
    for(var k=0u;k<3u;k++){
        var angle=min(rounded_add(cone.theta_o,cone.theta_e),bitcast<f32>(0x40490fdbu));
        if(k==0u){angle=max(0.0,rounded_add(spot_acos.x,-spot_acos.y));}else if(k==1u){angle=cone.theta_e;}
        c[k]=lt_cosf(angle);
    }
    if(kind==LT_LIGHT_SPOT+2u){energy*=lt_smoothstep(rounded_add(c.x,-c.y)*heap_float(light+L_spot_smooth));}
    // fast_sincosf(theta_o), fast_cosf(theta_e - theta_o), and the distant node's
    // fast_cosf(theta_o + theta_e) or the sun's fast_cosf(theta_e) for cos(theta_u).
    var f:array<vec2<f32>,3>;
    for(var k=0u;k<3u;k++){
        var angle=select(cone.theta_e,rounded_add(cone.theta_o,cone.theta_e),kind==1u);
        if(k==0u){angle=cone.theta_o;}else if(k==1u){angle=rounded_add(cone.theta_e,-cone.theta_o);}
        f[k]=lt_fast_sincos(angle);
    }
    if(kind==1u||kind==LT_LIGHT_SUN+2u){cos_u=f[2].y;if(kind!=1u){max_distance=rounded_divide(1.0,cos_u);}}
    if(!visible||load_word(p(17u))!=0u){return vec2<f32>(0);}
    return lt_importance(N,transmission,to_centroid,cos_u,cone,max_distance,min_distance,energy,f[0],f[1].y,c.z);
}
// sample_reservoir.
struct LtReservoir {selected:i32,weight:f32,total:f32,rand:f32}
fn lt_reservoir(r:LtReservoir,index:i32,weight:f32)->LtReservoir{
    var out=r;if(!(weight>0.0)){return out;}out.total=rounded_add(out.total,weight);
    if(out.selected== -1){out.selected=index;out.weight=weight;return out;}
    let threshold=rounded_divide(weight,out.total);
    if(out.rand<=threshold){out.selected=index;out.weight=weight;out.rand=rounded_divide(out.rand,threshold);}
    else{out.rand=rounded_divide(rounded_add(out.rand,-threshold),rounded_add(1.0,-threshold));}
    out.rand=clamp(out.rand,0.0,1.0);return out;
}
struct LtWalk {emitter:i32,pdf:f32,object:u32}
// sample: light_tree_sample<false> (emitter, pdf_selection, mesh object).
// otherwise: light_tree_pdf<false> of emitter goal_emitter (object goal_object).
fn light_tree_walk(sample:bool,P_in:vec3<f32>,N_in:vec3<f32>,transmission:bool,receiver:u32,rand_in:f32,goal_object:u32,goal_emitter:u32)->LtWalk{
    var out=LtWalk(-1,0.0,NONE);
    var P=P_in;var N=N_in;var rand=rand_in;var node=lt_root(receiver);
    var pdf_leaf=1.0;var pdf_selection=1.0;
    var pdf=1.0;var subtree=NONE;var trail=0u;var goal=goal_emitter;
    if(!sample){let e=lt_emitter(goal_emitter);
        if(lt_is_triangle(e)){goal=load_word(table_at(G_light_to_tree,goal_object,4u));subtree=load_word(lt_emitter(goal)+LTE_mesh_node);
            let root=lt_node(subtree);trail=load_word(root+LTN_bit_trail);if(lt_type(root)==LT_LIGHT_TREE_INSTANCE){subtree=load_word(root+LTN_reference);}}
        else{trail=load_word(e+LTE_bit_trail);}}
    // A mesh light's subtree continues in its object space (one transform call site).
    var local_object=NONE;
    for(var step=0u;step<256u;step++){
        if(load_word(p(17u))!=0u){return LtWalk(-1,0.0,NONE);}
        if(local_object!=NONE){lt_to_local(local_object,&P,&N);local_object=NONE;}
        let at=lt_node(node);let leaf=lt_type(at)>=LT_LIGHT_TREE_LEAF;
        var count=2u;var first=0u;if(leaf){count=load_word(at+LTN_num_emitters);first=load_word(at+LTN_first_emitter);}
        if(leaf&&count==0u){return LtWalk(-1,0.0,NONE);}
        var sample_max=false;if(sample&&leaf){sample_max=rand>0.5;if(count>1u){rand=rounded_add(rand*2.0,-select(0.0,1.0,sample_max));}}
        // Leaf sampling: reservoir on importance[!sample_max] and the other measure's sums.
        var pick=LtReservoir(-1,0.0,0.0,0.0);var other_weight=0.0;var other_total=0.0;var has=0u;var extra=0u;
        // Leaf pdf: goal and total importance, number with importance.
        var goal_importance=vec2<f32>(0);var total=vec2<f32>(0);var with_importance=0u;
        var left=vec2<f32>(0);var right=vec2<f32>(0);
        var j=0u;
        loop{
            if(j>=count+extra){break;}
            var item=0u;var is_node=false;var skip=false;
            if(leaf){item=select(u32(pick.selected),first+j,j<count);}
            else{let child=load_word(at+select(LTN_right_child,LTN_left_child,j==0u));let c=lt_node(child);let n=load_word(c+LTN_num_emitters);
                if(n==1u){item=load_word(c+LTN_first_emitter);}else if(n!=0u){item=child;is_node=true;}else{skip=true;}}
            var importance=vec2<f32>(0);if(!skip){importance=lt_item_importance(P,N,transmission,item,is_node);}
            if(!leaf){if(j==0u){left=importance;}else{right=importance;}}
            else if(j>=count){
                // The fallback fallback (no minimum importance) took this emitter: its
                // maximum importance is selected_importance[0], the other measure's slot.
                other_weight=importance.x;
            }else if(sample){
                let index=i32(first+j);let chosen=select(importance.y,importance.x,sample_max);let measure=select(importance.x,importance.y,sample_max);
                pick.rand=rand;pick=lt_reservoir(pick,index,chosen);rand=pick.rand;
                if(pick.selected==index){other_weight=measure;}
                other_total=rounded_add(other_total,measure);has|=select(0u,1u,importance.x>0.0)<<j;
            }else{
                with_importance+=select(0u,1u,importance.x>0.0);if(first+j==goal){goal_importance=importance;}
                total=vec2<f32>(rounded_add(total.x,importance.x),rounded_add(total.y,importance.y));
            }
            j++;
            if(leaf&&sample&&j==count){
                if(has==0u){return LtWalk(-1,0.0,NONE);}
                // total_importance[1]: the minimum measure's sum.
                let min_total=select(pick.total,other_total,sample_max);
                if(min_total==0.0){
                    if(sample_max){other_weight=1.0;other_total=f32(countOneBits(has));}
                    else{var fallback=LtReservoir(-1,0.0,0.0,rand);var bits=has;
                        for(var i=0u;i<count;i++){fallback=lt_reservoir(fallback,i32(first+i),f32(bits&1u));bits>>=1u;}
                        rand=fallback.rand;pick.selected=fallback.selected;pick.weight=fallback.weight;pick.total=fallback.total;extra=1u;}
                }
            }
        }
        if(load_word(p(17u))!=0u){return LtWalk(-1,0.0,NONE);}
        if(leaf&&sample){
            // selected/total importance indexed [0] maximum, [1] minimum.
            var weight=vec2<f32>(pick.weight,other_weight);var sums=vec2<f32>(pick.total,other_total);
            if(!sample_max){weight=vec2<f32>(other_weight,pick.weight);sums=vec2<f32>(other_total,pick.total);}
            pdf_selection*=0.5*rounded_add(rounded_divide(weight.x,sums.x),rounded_divide(weight.y,sums.y));
            let e=lt_emitter(u32(pick.selected));
            if(lt_is_mesh(e)){let object=load_word(e+LTE_mesh_object);local_object=object;node=load_word(e+LTE_mesh_node);
                let mesh=lt_node(node);if(lt_type(mesh)==LT_LIGHT_TREE_INSTANCE){node=load_word(mesh+LTN_reference);}out.object=object;continue;}
            out.emitter=pick.selected;out.pdf=pdf_selection*pdf_leaf;return out;
        }
        if(leaf){
            if(!(goal_importance.x>0.0)){return LtWalk(-1,0.0,NONE);}
            pdf*=0.5*rounded_add(rounded_divide(goal_importance.x,total.x),select(rounded_divide(1.0,f32(with_importance)),rounded_divide(goal_importance.y,total.y),total.y>0.0));
            if(subtree!=NONE){local_object=goal_object;node=subtree;subtree=NONE;goal=goal_emitter;trail=load_word(lt_emitter(goal_emitter)+LTE_bit_trail);continue;}
            out.pdf=pdf;return out;
        }
        // Inner node: get_left_probability.
        let total_max=rounded_add(left.x,right.x);if(total_max==0.0){return LtWalk(-1,0.0,NONE);}
        let total_min=rounded_add(left.y,right.y);let probability_max=rounded_divide(left.x,total_max);
        let probability_min=select(0.5*(select(0.0,1.0,left.x>0.0)+select(0.0,1.0,right.x==0.0)),rounded_divide(left.y,total_min),total_min>0.0);
        let left_probability=0.5*rounded_add(probability_max,probability_min);
        let left_index=load_word(at+LTN_left_child);let right_index=load_word(at+LTN_right_child);
        if(sample){
            var choice=LtReservoir(i32(left_index),0.0,left_probability,rand);choice=lt_reservoir(choice,i32(right_index),rounded_add(1.0,-left_probability));rand=choice.rand;
            node=u32(choice.selected);pdf_leaf*=select(rounded_add(1.0,-left_probability),left_probability,node==left_index);
        }else{
            trail>>=load_word(at+LTN_bit_skip)&255u;let go_left=(trail&1u)==0u;trail>>=1u;
            node=select(right_index,left_index,go_left);pdf*=select(rounded_add(1.0,-left_probability),left_probability,go_left);
            if(pdf==0.0){return LtWalk(-1,0.0,NONE);}
        }
    }
    fail();return LtWalk(-1,0.0,NONE);
}
// Forward (BSDF-sampled) hit of a lamp, sun or background object: the tree's
// selection pdf from the ray origin and the scattering normal
// (light_sample_mis_weight_forward_lamp / _distant / _background).
fn light_tree_forward_pdf(index:u32,emitter:u32)->f32{
    if((state_byte(S_path_visibility,index)&LT_PATH_RAY_VISIBILITY_VOLUME_SCATTER)!=0u){fail();return 0.0;}
    let flag=state_word(S_path_flag,index);
    return light_tree_walk(false,heap_vec3(state_address(S_ray_P,index,12u)),heap_vec3(state_address(S_path_mis_origin_n,index,12u)),
        (flag&LT_PATH_RAY_MIS_HAD_TRANSMISSION)!=0u,light_link_receiver_forward(index),0.0,0u,emitter).pdf;
}
fn light_tree_lamp_pdf(index:u32,object:u32)->f32{return light_tree_forward_pdf(index,load_word(table_at(G_light_to_tree,object,4u)));}
