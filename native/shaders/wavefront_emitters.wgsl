// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native spot attenuation and area rectangle/ellipse sampling and evaluation.
// Adapted from the reviewed Cycles kernel/light/{spot,area,common}.h snapshot.
const EMITTER_PI:f32=3.141592653589793;
const EMITTER_INV_PI:f32=0.3183098861837907;
// safe_normalize_len and normalize of the oracle host: per-component division
// (SSE) or reciprocal multiplication (scalar float3, ARM64), selected by the
// generated NATIVE_FLOAT3_RECIPROCAL_NORMALIZE.
fn emitter_normalize_len(v:vec3<f32>)->vec4<f32>{
    let t=rounded_sqrt(rounded_dot(v,v));if(t==0.0){return vec4<f32>(0);}
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return vec4<f32>(v*rounded_divide(1.0,t),t);}
    return vec4<f32>(rounded_divide(v.x,t),rounded_divide(v.y,t),rounded_divide(v.z,t),t);
}
fn emitter_native_normalize(v:vec3<f32>)->vec3<f32>{
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return v*rounded_divide(1.0,rounded_sqrt(rounded_dot(v,v)));}
    return rounded_normalize(v);
}
fn emitter_sphere_hit(origin:vec3<f32>,direction:vec3<f32>,position:vec3<f32>,radius:f32)->f32{
    let delta=position-origin;let d2=dot(delta,delta);let r2=radius*radius;let cosine=dot(delta,direction);
    if(d2>r2&&cosine<0.0){return -1.0;}let perpendicular=delta-cosine*direction;let s2=dot(perpendicular,perpendicular);
    if(s2>r2){return -1.0;}let root=sqrt(max(r2-s2,0.0));let t=cosine-select(-root,root,d2-r2>=0.0);
    return select(-1.0,t,t>0.0&&t<bitcast<f32>(0x7f7fffffu));
}
//#if LIGHT_SPOT
// spot_light_to_local and spot_light_attenuation (light/spot.h) in the oracle's float3 order: transform_direction(itfm, ray)
// as the host computes it (SSE: x*c0 + (y*c1 + z*c2); scalar float4 dots: (x*c0 + y*c1) + z*c2), safe_normalize (the
// reciprocal of the length), and smoothstepf, every sum and product rounded on its own. A steep blend (spot_smooth near 1e4)
// turns one last bit of the local direction into about 1e-3 of attenuation (native/DEVICE.md, item 151).
fn spot_attenuation(light:u32,direction:vec3<f32>)->f32{
    let object=table_at(G_objects,load_word(light+L_object_id),SIZE_objects);let a=object+O_itfm;
    let x=heap_vec4(a);let y=heap_vec4(a+16u);let z=heap_vec4(a+32u);
    let c0=vec3<f32>(x.x,y.x,z.x)*direction.x;let c1=vec3<f32>(x.y,y.y,z.y)*direction.y;let c2=vec3<f32>(x.z,y.z,z.z)*direction.z;
    var d=rounded_add3(rounded_add3(c0,c1),c2);if(CAMERA_NATIVE_VECTOR_DIVIDE!=0u){d=rounded_add3(c0,rounded_add3(c1,c2));}
    let transformed=rounded_safe_normalize(d);
    let f=rounded_add(-transformed.z,-heap_float(light+L_spot_cos))*heap_float(light+L_spot_smooth);
    if(f<=0.0){return 0.0;}if(f>=1.0){return 1.0;}let ff=f*f;return rounded_add(3.0*ff,-((2.0*ff)*f));
}
//#endif
//#if !LIGHT_SPOT
fn spot_attenuation(light:u32,direction:vec3<f32>)->f32{
    let object=table_at(G_objects,load_word(light+L_object_id),SIZE_objects);let a=object+O_itfm;
    let x=heap_vec4(a);let y=heap_vec4(a+16u);let z=heap_vec4(a+32u);
    let transformed=rounded_normalize(vec3<f32>(dot(x.xyz,direction),dot(y.xyz,direction),dot(z.xyz,direction)));
    let f=(-transformed.z-heap_float(light+L_spot_cos))*heap_float(light+L_spot_smooth);
    if(f<=0.0){return 0.0;}if(f>=1.0){return 1.0;}let ff=f*f;return 3.0*ff-2.0*ff*f;
}
//#endif
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
    // Horner steps in a loop (the same 16 operations in order) rather than 16
    // inlined copies; with area_rectangle's loop over its four arcsines this
    // shrinks the area-light shade_surface_light module, whose cold compile
    // crashed the Intel Mesa 25.0.7 compiler in 3 of 4 runs before and in none
    // of 7 after (brw_opt_combine_constants driver bug, phaseE3 report).
    var terms=array<vec2<f32>,16>(
    vec2<f32>(0.0046601435169577599,-3.00426636972735e-11),
    vec2<f32>(0.005153309553861618,1.284582861538591e-10),
    vec2<f32>(0.0057400376535952091,1.7246714344731083e-11),
    vec2<f32>(0.0064472104422748089,-1.30385160446167e-10),
    vec2<f32>(0.0073125259950757027,-1.2147685755854067e-10),
    vec2<f32>(0.0083903353661298752,4.4348694029308498e-10),
    vec2<f32>(0.0097616091370582581,3.9213582089072777e-10),
    vec2<f32>(0.011551801115274429,-2.1913472343893612e-10),
    vec2<f32>(0.013964843936264515,-1.8626451492309571e-10),
    vec2<f32>(0.017352763563394547,8.5968237656813406e-10),
    vec2<f32>(0.022372158244252205,8.4665688601407135e-10),
    vec2<f32>(0.0303819440305233,4.1392114427354598e-10),
    vec2<f32>(0.0446428582072258,-1.064368656703404e-09),
    vec2<f32>(0.075000002980232239,-2.9802322387695314e-09),
    vec2<f32>(0.1666666716337204,-4.9670537312825518e-09),
    vec2<f32>(1,0));
    var polynomial=vec2<f32>(0.0042409072630107403,-1.6933137720281428e-10);
    for(var i=0u;i<16u;i++){polynomial=trig_coeff(terms[i],trig_mul(polynomial,x));}
    var result=trig_mul(z,polynomial);if(reduce){result=trig_coeff(vec2<f32>(1.5707963705062866,-4.3711390001862426e-8),-2.0*result);}
    let answer=rounded_add(result.x,result.y);return select(answer,-answer,value<0.0);
}
struct AreaRegion {P:vec3<f32>,U:vec3<f32>,V:vec3<f32>,len_u:f32,len_v:f32,rectangle:bool,valid:bool}
fn area_region(light:u32,P:vec3<f32>)->AreaRegion{
    var r=AreaRegion(heap_vec3(light+L_co),heap_vec3(light+L_area_axis_u),heap_vec3(light+L_area_axis_v),heap_float(light+L_area_len_u),heap_float(light+L_area_len_v),heap_float(light+L_area_invarea)>0.0,true);
    if(heap_float(light+L_area_normalize_spread)<=0.0){return r;}
    // area_light_spread_clamp_light in the pinned association, every product rounded before its sum (rounded_dot,
    // rounded_add, rounded_add3: the oracle kernels' -ffp-contract=off order), so no driver contraction changes it.
    let N=heap_vec3(light+L_area_dir);let t=rounded_dot(N,P-r.P);let closest=rounded_add3(P,-(t*N));
    let spread=t*heap_float(light+L_area_tan_half_spread);let su=rounded_dot(r.U,closest-r.P);let sv=rounded_dot(r.V,closest-r.P);
    let round= !r.rectangle&&r.len_u==r.len_v;var sample_spread=spread==0.0;
    if(round&&!sample_spread){
        let distance=rounded_sqrt(rounded_add(su*su,sv*sv));let radius=r.len_u*0.5;
        if(distance>=radius+spread){r.valid=false;return r;}
        sample_spread=distance<=abs(radius-spread)&&spread<radius;
        if(distance>abs(radius-spread)){
            let len_u=radius+spread-distance;var len_v=2.0*min(radius,spread);
            if(abs(rounded_add(radius*radius,-(spread*spread)))<distance*distance){let x=rounded_add(distance,rounded_divide(rounded_add(spread*spread,-(radius*radius)),distance));
                len_v=rounded_sqrt(max(rounded_add((2.0*spread)*(2.0*spread),-(x*x)),0.0));}
            let rectangle_area=len_u*len_v;let circle_area=EMITTER_PI*(radius*radius);let spread_area=EMITTER_PI*(spread*spread);
            if(rectangle_area<min(circle_area,spread_area)){
                r.rectangle=true;r.U=emitter_native_normalize(r.P-closest);r.V=emitter_quarter_turn(r.U,N);r.len_u=len_u;r.len_v=len_v;
                r.P=0.5*rounded_add3(r.P+closest,r.U*(spread-radius));return r;
            }sample_spread=spread_area<circle_area;
        }
    }else if(!round&&!sample_spread){
        let min_u=max(su-spread,-r.len_u*0.5);let max_u=min(su+spread,r.len_u*0.5);
        let min_v=max(sv-spread,-r.len_v*0.5);let max_v=min(sv+spread,r.len_v*0.5);
        if(min_u>=max_u||min_v>=max_v){r.valid=false;return r;}
        let lu=max_u-min_u;let lv=max_v-min_v;let rectangle_area=lu*lv;
        let ellipse_area=select((EMITTER_PI*0.25)*r.len_u*r.len_v,bitcast<f32>(0x7f7fffffu),r.rectangle);let spread_area=EMITTER_PI*(spread*spread);
        if(r.rectangle||rectangle_area<min(ellipse_area,spread_area)){
            r.rectangle=true;r.len_u=lu;r.len_v=lv;r.P=rounded_add3(rounded_add3(r.P,r.U*(0.5*(min_u+max_u))),r.V*(0.5*(min_v+max_v)));return r;
        }r.rectangle=false;sample_spread=spread_area<ellipse_area;
    }
    if(sample_spread){r.rectangle=false;r.P=rounded_add3(rounded_add3(r.P,r.U*su),r.V*sv);r.len_u=spread*2.0;r.len_v=spread*2.0;}
    return r;
}
struct AreaRect {P:vec3<f32>,pdf:f32}
fn area_rectangle(P:vec3<f32>,r:AreaRegion,random:vec2<f32>,sample_coord:bool)->AreaRect{
    var out=AreaRect(r.P,0.0);var Z=rounded_cross(r.U,r.V);let direction=r.P-P;var z0=rounded_dot(direction,Z);if(z0>0.0){Z= -Z;z0= -z0;}
    let xc=rounded_dot(direction,r.U);let yc=rounded_dot(direction,r.V);let x0=rounded_add(xc,-0.5*r.len_u);let x1=rounded_add(xc,0.5*r.len_u);let y0=rounded_add(yc,-0.5*r.len_v);let y1=rounded_add(yc,0.5*r.len_v);
    var nz=vec4<f32>(-y0,x1,y1,-x0);for(var i=0u;i<4u;i++){nz[i]=rounded_divide(nz[i],rounded_sqrt(rounded_add(nz[i]*nz[i],z0*z0)));}
    // One emitter_asin body evaluated four times (see emitter_asin).
    var asin_inputs=vec4<f32>(-nz.x*nz.y,-nz.y*nz.z,-nz.z*nz.w,-nz.w*nz.x);var angles:vec4<f32>;
    for(var i=0u;i<4u;i++){angles[i]=emitter_asin(asin_inputs[i]);}
    let g0=angles.x;let g1=angles.y;let g2=angles.z;let g3=angles.w;
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
//#if LIGHT_AREA_DEGENERATE
    // An axis whose length underflows to zero (native/DEVICE.md, item 148; the host refuses a spread of zero). The CPU's
    // axis holds infinities and NaNs (normalize_len of a zero vector), the device's copy a zero axis. area_light_spread_clamp_light
    // then rejects every point off the light's plane (min_u = max(NaN, -0) = -0 >= max_u = min(NaN, 0) = 0), except for a
    // disk with both axes zero, whose NaN distance takes neither branch; a next-event sample there has a NaN direction and a
    // zero spread attenuation (max(NaN, 0) = 0), so no sample is valid. A hit from a camera ray keeps eval_fac with the ray's
    // direction (area_light_eval with sample_coord false, invarea 1 since the area is zero).
    if(heap_float(light+L_area_len_u)==0.0||heap_float(light+L_area_len_v)==0.0){
        if(sample_coord){return out;}
        let invarea=heap_float(light+L_area_invarea);let spread=heap_float(light+L_area_tan_half_spread);
        let round=invarea<0.0&&heap_float(light+L_area_len_u)==0.0&&heap_float(light+L_area_len_v)==0.0;
        if(!round&&rounded_dot(N,origin-center)*spread!=0.0){return out;}
        out.P=center;out.D=direction;out.t=distance;out.eval_fac=EMITTER_INV_PI*abs(invarea);let normalization=heap_float(light+L_area_normalize_spread);
        if(normalization>0.0){let tangent=length(cross(-out.D,N))/dot(-out.D,N);out.eval_fac*=max((spread-tangent)*normalization,0.0);}
        out.valid=out.eval_fac>0.0;return out;
    }
//#endif
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
//#if LIGHT_LAMP_SHADER
// Point, spot, area and sun lights whose emission shader is not constant: the ShaderData at the light point,
// light_normal_uv_from_position (light/light.h with point.h point_light_uv, spot.h spot_light_to_local and
// spot_light_uv, area.h area_light_uv, sun.h sun_light_uv) and shader_setup_from_sample for a lamp
// (geom/shader_data.h), every sum, quotient and square root correctly rounded in the host's order.
// The host's transform_direction (SSE: x*c0 + (y*c1 + z*c2); scalar float4 dots: (x*c0 + y*c1) + z*c2).
fn lamp_direction(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    let c0=vec3<f32>(t.x.x,t.y.x,t.z.x)*v.x;let c1=vec3<f32>(t.x.y,t.y.y,t.z.y)*v.y;let c2=vec3<f32>(t.x.z,t.y.z,t.z.z)*v.z;
    if(CAMERA_NATIVE_VECTOR_DIVIDE!=0u){return rounded_add3(c0,rounded_add3(c1,c2));}
    return rounded_add3(rounded_add3(c0,c1),c2);
}
fn lamp_itfm(light:u32)->NativeTransform{return motion_transform_load(table_at(G_objects,load_word(light+L_object_id),SIZE_objects)+O_itfm);}
// float3 / float: SSE hosts divide each lane, the scalar operator multiplies by 1/f.
fn lamp_div3(v:vec3<f32>,f:f32)->vec3<f32>{
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return v*rounded_divide(1.0,f);}
    return vec3<f32>(rounded_divide(v.x,f),rounded_divide(v.y,f),rounded_divide(v.z,f));
}
struct LampNormalUV {Ng:vec3<f32>,uv:vec2<f32>}
fn lamp_normal_uv(light:u32,P:vec3<f32>,D:vec3<f32>)->LampNormalUV{
    var out:LampNormalUV;let kind=load_word(light+L_type);
    if(kind==LIGHT_SPOT||kind==LIGHT_POINT){
        out.Ng=-D;if(load_word(light+L_is_sphere)!=0u){out.Ng=surface_native_normalize(rounded_add3(P,-heap_vec3(light+L_co)));}
        if(kind==LIGHT_SPOT){
            var ray=rounded_safe_normalize(lamp_direction(lamp_itfm(light),-D));ray.z=-ray.z;
            let factor=rounded_divide(heap_float(light+LS_half_cot_half_spot_angle),ray.z);
            out.uv=vec2<f32>(rounded_add(ray.y*factor,0.5),-(rounded_add(ray.x,ray.y)*factor));
        }else{
            // map_to_sphere of the normal in the light's object space.
            let co=lamp_direction(lamp_itfm(light),out.Ng);let l=rounded_dot(co,co);var u=0.0;var v=0.0;
            if(l>0.0){
                if(!(co.x==0.0&&co.y==0.0)){u=rounded_add(0.5,-(trig_atan2_guarded(co.x,co.y)*0.15915493667125702));}
                v=rounded_add(1.0,-(trig_acos(rounded_divide(co.z,rounded_sqrt(l)))*0.31830987334251404));
            }
            out.uv=vec2<f32>(v,rounded_add(rounded_add(1.0,-u),-v));
        }
    }else if(kind==LIGHT_AREA){
        out.Ng=heap_vec3(light+L_area_dir);
        let inv_u=lamp_div3(heap_vec3(light+L_area_axis_u),heap_float(light+L_area_len_u));
        let inv_v=lamp_div3(heap_vec3(light+L_area_axis_v),heap_float(light+L_area_len_v));
        let inplane=rounded_add3(P,-heap_vec3(light+L_co));
//#if LIGHT_AREA_DEGENERATE
        // A zero-length axis (item 148): the CPU's clamp(NaN, -0.5, 0.5) is min(max(NaN, -0.5), 0.5) = -0.5.
        var u=-0.5;if(heap_float(light+L_area_len_u)!=0.0){u=clamp(rounded_dot(inplane,inv_u),-0.5,0.5);}
        var v=-0.5;if(heap_float(light+L_area_len_v)!=0.0){v=clamp(rounded_dot(inplane,inv_v),-0.5,0.5);}
//#endif
//#if !LIGHT_AREA_DEGENERATE
        let u=clamp(rounded_dot(inplane,inv_u),-0.5,0.5);let v=clamp(rounded_dot(inplane,inv_v),-0.5,0.5);
//#endif
        out.uv=vec2<f32>(rounded_add(v,0.5),rounded_add(-u,-v));
    }else{
        out.Ng=-D;let delta=rounded_add3(D,-heap_vec3(light+L_co));
        let fac=rounded_divide(heap_float(light+LS_half_inv_sin_half_angle),rounded_sqrt(rounded_dot(delta,delta)));
        let t=lamp_itfm(light);let u=rounded_dot(D,t.x.xyz)*fac;let v=rounded_dot(D,t.y.xyz)*fac;
        out.uv=vec2<f32>(rounded_add(v,0.5),rounded_add(-u,-v));
    }
    return out;
}
// shader_setup_from_sample(P, Ng, -D, shader, object, light, u, v, t, time, object_space false, is_lamp true).
fn lamp_shader_data(light_index:u32,P:vec3<f32>,D:vec3<f32>,t:f32,time:f32)->SurfaceData{
    var sd:SurfaceData;
    if(light_index>=table_count(G_lights)){surface_failure(&sd);return sd;}
    let light=table_at(G_lights,light_index,SIZE_lights);let shader=load_word(light+L_shader_id);
    if((shader&SHADER_MASK)>=table_count(G_shaders)){surface_failure(&sd);return sd;}
    let nuv=lamp_normal_uv(light,P,D);
    sd.P=P;sd.N=nuv.Ng;sd.Ng=nuv.Ng;sd.wi=-D;sd.shader=shader;sd.primitive_type=PRIMITIVE_LAMP;
    sd.object_id=load_word(light+L_object_id);sd.prim=light_index;sd.u=nuv.uv.x;sd.v=nuv.uv.y;sd.time=time;sd.ray_length=t;
    sd.runtime_flag=0u;sd.shader_flag=load_word(table_at(G_shaders,shader&SHADER_MASK,SIZE_shaders)+SH_flags);
    sd.object_flag=0u;if(sd.object_id!=NONE){sd.object_flag=load_word(table_at(G_object_flag,sd.object_id,4u));}
//#if LIGHT_MOTION
    // shader_setup_object_transforms: a moving light object's motion transform at the sample's time (object-space inputs
    // of its emission shader).
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){sd.motion_tfm=motion_object(sd.object_id,time);sd.motion_itfm=motion_inverse(sd.motion_tfm);
        if(load_word(p(17u))!=0u){surface_failure(&sd);return sd;}}
//#endif
    if(rounded_dot(sd.Ng,sd.wi)<0.0){sd.runtime_flag|=SR_BACKFACING;sd.Ng=-sd.Ng;sd.N=-sd.N;}
    return sd;
}
//#endif
//#if LIGHT_LAMP_SHADER
// The emission of a lamp's (non-constant) shader at a light point (shade_light.h integrate_light_nee and
// light/sample.h light_sample_shader_eval_forward: surface_shader_eval with PATH_RAY_EMISSION for all closures).
fn lamp_shader_emission(light_index:u32,P:vec3<f32>,D:vec3<f32>,t:f32,time:f32)->vec3<f32>{
    let sd=lamp_shader_data(light_index,P,D,t,time);if(sd.error!=0u){return vec3<f32>(0);}
    let r=svm_evaluate_context(NONE,sd,0u,0u,PATH_RAY_EMISSION,0u,0u,false);if(r.error!=0u){return vec3<f32>(0);}
    return r.emission;
}
//#endif
