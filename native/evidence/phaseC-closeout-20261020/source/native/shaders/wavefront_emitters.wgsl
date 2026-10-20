// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Native spot attenuation and area rectangle/ellipse sampling and evaluation.
// Adapted from the reviewed Cycles kernel/light/{spot,area,common}.h snapshot.
const EMITTER_PI:f32=3.141592653589793;
const EMITTER_INV_PI:f32=0.3183098861837907;
fn emitter_normalize_len(v:vec3<f32>)->vec4<f32>{
    let t=rounded_sqrt(rounded_dot(v,v));if(t==0.0){return vec4<f32>(0);}
    return vec4<f32>(rounded_divide(v.x,t),rounded_divide(v.y,t),rounded_divide(v.z,t),t);
}
fn emitter_sphere_hit(origin:vec3<f32>,direction:vec3<f32>,position:vec3<f32>,radius:f32)->f32{
    let delta=position-origin;let d2=dot(delta,delta);let r2=radius*radius;let cosine=dot(delta,direction);
    if(d2>r2&&cosine<0.0){return -1.0;}let perpendicular=delta-cosine*direction;let s2=dot(perpendicular,perpendicular);
    if(s2>r2){return -1.0;}let root=sqrt(max(r2-s2,0.0));let t=cosine-select(-root,root,d2-r2>=0.0);
    return select(-1.0,t,t>0.0&&t<bitcast<f32>(0x7f7fffffu));
}
fn spot_attenuation(light:u32,direction:vec3<f32>)->f32{
    let object=table_at(G_objects,load_word(light+L_object_id),SIZE_objects);let a=object+O_itfm;
    let x=heap_vec4(a);let y=heap_vec4(a+16u);let z=heap_vec4(a+32u);
    let transformed=rounded_normalize(vec3<f32>(dot(x.xyz,direction),dot(y.xyz,direction),dot(z.xyz,direction)));
    let f=(-transformed.z-heap_float(light+L_spot_cos))*heap_float(light+L_spot_smooth);
    if(f<=0.0){return 0.0;}if(f>=1.0){return 1.0;}let ff=f*f;return 3.0*ff-2.0*ff*f;
}
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
    var polynomial=vec2<f32>(0.0042409072630107403,-1.6933137720281428e-10);
    polynomial=trig_coeff(vec2<f32>(0.0046601435169577599,-3.00426636972735e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.005153309553861618,1.284582861538591e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0057400376535952091,1.7246714344731083e-11),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0064472104422748089,-1.30385160446167e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0073125259950757027,-1.2147685755854067e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0083903353661298752,4.4348694029308498e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0097616091370582581,3.9213582089072777e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.011551801115274429,-2.1913472343893612e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.013964843936264515,-1.8626451492309571e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.017352763563394547,8.5968237656813406e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.022372158244252205,8.4665688601407135e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0303819440305233,4.1392114427354598e-10),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.0446428582072258,-1.064368656703404e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.075000002980232239,-2.9802322387695314e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(0.1666666716337204,-4.9670537312825518e-09),trig_mul(polynomial,x));
    polynomial=trig_coeff(vec2<f32>(1,0),trig_mul(polynomial,x));
    var result=trig_mul(z,polynomial);if(reduce){result=trig_coeff(vec2<f32>(1.5707963705062866,-4.3711390001862426e-8),-2.0*result);}
    let answer=rounded_add(result.x,result.y);return select(answer,-answer,value<0.0);
}
struct AreaRegion {P:vec3<f32>,U:vec3<f32>,V:vec3<f32>,len_u:f32,len_v:f32,rectangle:bool,valid:bool}
fn area_region(light:u32,P:vec3<f32>)->AreaRegion{
    var r=AreaRegion(heap_vec3(light+L_co),heap_vec3(light+L_area_axis_u),heap_vec3(light+L_area_axis_v),heap_float(light+L_area_len_u),heap_float(light+L_area_len_v),heap_float(light+L_area_invarea)>0.0,true);
    if(heap_float(light+L_area_normalize_spread)<=0.0){return r;}
    let N=heap_vec3(light+L_area_dir);let t=dot(N,P-r.P);let closest=P-t*N;
    let spread=t*heap_float(light+L_area_tan_half_spread);let su=dot(r.U,closest-r.P);let sv=dot(r.V,closest-r.P);
    let round= !r.rectangle&&r.len_u==r.len_v;var sample_spread=spread==0.0;
    if(round&&!sample_spread){
        let distance=length(vec2<f32>(su,sv));let radius=r.len_u*0.5;
        if(distance>=radius+spread){r.valid=false;return r;}
        sample_spread=distance<=abs(radius-spread)&&spread<radius;
        if(distance>abs(radius-spread)){
            let len_u=radius+spread-distance;var len_v=2.0*min(radius,spread);
            if(abs(radius*radius-spread*spread)<distance*distance){let x=distance+(spread*spread-radius*radius)/distance;len_v=sqrt(max(4.0*spread*spread-x*x,0.0));}
            let rectangle_area=len_u*len_v;let circle_area=EMITTER_PI*radius*radius;let spread_area=EMITTER_PI*spread*spread;
            if(rectangle_area<min(circle_area,spread_area)){
                r.rectangle=true;r.U=rounded_normalize(r.P-closest);r.V=emitter_quarter_turn(r.U,N);r.len_u=len_u;r.len_v=len_v;
                r.P=0.5*(r.P+closest+r.U*(spread-radius));return r;
            }sample_spread=spread_area<circle_area;
        }
    }else if(!round&&!sample_spread){
        let min_u=max(su-spread,-r.len_u*0.5);let max_u=min(su+spread,r.len_u*0.5);
        let min_v=max(sv-spread,-r.len_v*0.5);let max_v=min(sv+spread,r.len_v*0.5);
        if(min_u>=max_u||min_v>=max_v){r.valid=false;return r;}
        let lu=max_u-min_u;let lv=max_v-min_v;let rectangle_area=lu*lv;
        let ellipse_area=select((EMITTER_PI*0.25)*r.len_u*r.len_v,bitcast<f32>(0x7f7fffffu),r.rectangle);let spread_area=EMITTER_PI*spread*spread;
        if(r.rectangle||rectangle_area<min(ellipse_area,spread_area)){
            r.rectangle=true;r.len_u=lu;r.len_v=lv;r.P+=r.U*(0.5*(min_u+max_u))+r.V*(0.5*(min_v+max_v));return r;
        }r.rectangle=false;sample_spread=spread_area<ellipse_area;
    }
    if(sample_spread){r.rectangle=false;r.P+=r.U*su+r.V*sv;r.len_u=2.0*spread;r.len_v=2.0*spread;}
    return r;
}
struct AreaRect {P:vec3<f32>,pdf:f32}
fn area_rectangle(P:vec3<f32>,r:AreaRegion,random:vec2<f32>,sample_coord:bool)->AreaRect{
    var out=AreaRect(r.P,0.0);var Z=rounded_cross(r.U,r.V);let direction=r.P-P;var z0=rounded_dot(direction,Z);if(z0>0.0){Z= -Z;z0= -z0;}
    let xc=rounded_dot(direction,r.U);let yc=rounded_dot(direction,r.V);let x0=rounded_add(xc,-0.5*r.len_u);let x1=rounded_add(xc,0.5*r.len_u);let y0=rounded_add(yc,-0.5*r.len_v);let y1=rounded_add(yc,0.5*r.len_v);
    var nz=vec4<f32>(-y0,x1,y1,-x0);for(var i=0u;i<4u;i++){nz[i]=rounded_divide(nz[i],rounded_sqrt(rounded_add(nz[i]*nz[i],z0*z0)));}
    let g0=emitter_asin(-nz.x*nz.y);let g1=emitter_asin(-nz.y*nz.z);let g2=emitter_asin(-nz.z*nz.w);let g3=emitter_asin(-nz.w*nz.x);
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
