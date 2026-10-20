// Copyright (c) 2026 Jeremy Ellis and contributors
// Copyright (c) 2018-2024, Arm Limited.
// SPDX-License-Identifier: Apache-2.0 AND MIT
// Bounded adaptation of Arm Optimized Routines v25.01 sincosf polynomials.
// Source pin: third_party/arm-math-import.json; selected MIT grant:
// third_party/arm-optimized-routines/LICENSE.
// Changes: bounded disk-angle reduction and two-component Horner evaluation
// using original CodeBrix float helpers; no binary64 requirement. Matches the
// observed scalar platform sinf/cosf polynomial, not correctly rounded libm.
// Inputs are binary32 disk/area sampling angles in [-pi,pi].
fn native_disk_sincos(angle:f32)->vec2<f32>{
    if(angle==0.0){return vec2<f32>(angle,1);}
    var n=0;
    if(angle>=0.7853981633974483){n=1;}
    if(angle>=2.356194490192345){n=2;}
    if(angle<=-0.7853981633974483){n=-1;}
    if(angle<=-2.356194490192345){n=-2;}
    var x=vec2<f32>(angle,0);
    if(n!=0){x=vec2<f32>(rounded_add(angle,-f32(n)*1.5707963705062866),f32(n)*4.3711390001862426e-8);}
    var sine=rounded_add(x.x,x.y);var cosine=1.0;
    if(abs(x.x)>=0.000244140625){
      let z=trig_mul(x,x);
      var sn=trig_coeff(vec2<f32>(0.008332177996635437,1.4950340965214082e-10),trig_mul(z,vec2<f32>(-0.0001951729936990887,3.885231546207901e-12)));
      sn=trig_coeff(vec2<f32>(-0.16666655242443085,2.9874200890844804e-09),trig_mul(z,sn));
      sn=trig_coeff(vec2<f32>(1,0),trig_mul(z,sn));
      sn=trig_mul(x,sn);
      var cs=trig_coeff(vec2<f32>(-0.0013886763481423259,-3.129527886835959e-11),trig_mul(z,vec2<f32>(2.4390450562350452e-05,1.4121409391224665e-13)));
      cs=trig_coeff(vec2<f32>(0.04166662320494652,1.1939799149374153e-10),trig_mul(z,cs));
      cs=trig_coeff(vec2<f32>(-0.5,2.7489177600870107e-09),trig_mul(z,cs));
      cs=trig_coeff(vec2<f32>(1.0,0.0),trig_mul(z,cs));
      sine=rounded_add(sn.x,sn.y);cosine=rounded_add(cs.x,cs.y);
    }
    if(n==1){return vec2<f32>(cosine,-sine);}
    if(n==2||n== -2){return vec2<f32>(-sine,-cosine);}
    if(n== -1){return vec2<f32>(-cosine,sine);}
    return vec2<f32>(sine,cosine);
}

// Bounded HG azimuth adapter, 0 <= angle <= 2*pi. The same reviewed Arm
// polynomial is used, with two-component reduction covering all four quadrants.
// trig_product retains the rounding residual of 3*(pi/2), unlike a single float.
fn native_phase_sincos(angle:f32)->vec2<f32>{
    if(angle==0.0){return vec2<f32>(angle,1);}
    let n=i32(floor(angle*0.6366197723675814+0.5));
    var x=vec2<f32>(angle,0);
    if(n!=0){let product=trig_product(f32(n),1.5707963705062866);
        x=vec2<f32>(rounded_add(angle,-product.x),rounded_add(-product.y,f32(n)*4.3711390001862426e-8));}
    var sine=rounded_add(x.x,x.y);var cosine=1.0;
    if(abs(x.x)>=0.000244140625){
      let z=trig_mul(x,x);
      var sn=trig_coeff(vec2<f32>(0.008332177996635437,1.4950340965214082e-10),trig_mul(z,vec2<f32>(-0.0001951729936990887,3.885231546207901e-12)));
      sn=trig_coeff(vec2<f32>(-0.16666655242443085,2.9874200890844804e-09),trig_mul(z,sn));
      sn=trig_coeff(vec2<f32>(1,0),trig_mul(z,sn));
      sn=trig_mul(x,sn);
      var cs=trig_coeff(vec2<f32>(-0.0013886763481423259,-3.129527886835959e-11),trig_mul(z,vec2<f32>(2.4390450562350452e-05,1.4121409391224665e-13)));
      cs=trig_coeff(vec2<f32>(0.04166662320494652,1.1939799149374153e-10),trig_mul(z,cs));
      cs=trig_coeff(vec2<f32>(-0.5,2.7489177600870107e-09),trig_mul(z,cs));
      cs=trig_coeff(vec2<f32>(1.0,0.0),trig_mul(z,cs));
      sine=rounded_add(sn.x,sn.y);cosine=rounded_add(cs.x,cs.y);
    }
    if((n&3)==1){return vec2<f32>(cosine,-sine);}
    if((n&3)==2){return vec2<f32>(-sine,-cosine);}
    if((n&3)==3){return vec2<f32>(-cosine,sine);}
    return vec2<f32>(sine,cosine);
}
