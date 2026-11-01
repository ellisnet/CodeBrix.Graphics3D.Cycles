// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-FileCopyrightText: 2019-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Cycles init_from_camera, perspective camera, path initialization and Sobol.
// Native byte offsets are generated from the compiled upstream types.
fn filter_value(value:f32)->f32{
    let x=clamp(value,0.0,1.0)*f32(FILTER_SIZE-1u);let i=min(u32(x),FILTER_SIZE-1u);
    let a=p(8u)+(data_word(D_tables_filter_table_offset)+i)*4u;let t=x-f32(i);let first=heap_float(a);
    if(t==0.0){return first;}
    // Cycles lookup_table_read: (1-t)*data0 + t*data1, both products rounded
    // before the sum (integer add, so no driver can contract it).
    return rounded_add((1.0-t)*first,t*heap_float(p(8u)+(data_word(D_tables_filter_table_offset)+min(i+1u,FILTER_SIZE-1u))*4u));
}
fn camera_shutter(value:f32)->f32{
    let x=clamp(value,0.0,1.0)*f32(SHUTTER_SIZE-1u);let i=min(u32(x),SHUTTER_SIZE-1u);
    let address=p(8u)+(data_word(D_cam_shutter_table_offset)+i)*4u;let t=x-f32(i);let first=heap_float(address);
    if(t==0.0){return first;}
    return rounded_add((1.0-t)*first,t*heap_float(p(8u)+(data_word(D_cam_shutter_table_offset)+min(i+1u,SHUTTER_SIZE-1u))*4u));
}
// Preserve native float3 normalization at transparent/volume boundary corners.
// Captures retain the originating host's SSE division or scalar reciprocal rule.
fn camera_normalize(v:vec3<f32>)->vec3<f32>{
    if(CAMERA_NATIVE_VECTOR_DIVIDE!=0u){return rounded_normalize(v);}
    return v*rounded_divide(1.0,rounded_sqrt(rounded_dot(v,v)));
}
// Cycles transform_direction of the originating oracle host, bit for bit: the
// SSE build accumulates z*c2, then y*c1, then x*c0 (madd is unfused at the SSE4.2
// baseline), the scalar build takes the float4 dot (x+y)+z per row. Products
// are rounded before the integer adds, so no driver can contract them (the
// shared motion_direction leaves that to the driver).
fn camera_direction(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    let c0=vec3<f32>(t.x.x,t.y.x,t.z.x)*v.x;let c1=vec3<f32>(t.x.y,t.y.y,t.z.y)*v.y;let c2=vec3<f32>(t.x.z,t.y.z,t.z.z)*v.z;
    if(CAMERA_NATIVE_VECTOR_DIVIDE!=0u){return rounded_add3(c0,rounded_add3(c1,c2));}
    return rounded_add3(rounded_add3(c0,c1),c2);
}
// Cycles len(float3): the square root of the (x+y)+z dot, correctly rounded.
fn camera_length(v:vec3<f32>)->f32{return rounded_sqrt(rounded_dot(v,v));}
// Cycles interp(a,b,t) = a + t*(b-a), the product rounded before the sum.
fn camera_interp(a:vec3<f32>,b:vec3<f32>,t:f32)->vec3<f32>{return rounded_add3(a,(b-a)*t);}
fn camera_projection(address:u32,v:vec4<f32>)->vec3<f32>{
    // Native float4 dot rounds paired sums before perspective division.
    // Fused projection multiply-add can make a ray cross a box corner twice.
    let w=motion_dot4(heap_vec4(address+48u),v);
    let c=vec3<f32>(motion_dot4(heap_vec4(address),v),motion_dot4(heap_vec4(address+16u),v),motion_dot4(heap_vec4(address+32u),v));
    if(w==0.0){return vec3<f32>(0);}
    if(CAMERA_NATIVE_VECTOR_DIVIDE!=0u){return vec3<f32>(rounded_divide(c.x,w),rounded_divide(c.y,w),rounded_divide(c.z,w));}
    return c*rounded_divide(1.0,w);
}
//#if CAMERA_PROJECTIONS
// Orthographic and panoramic cameras and depth of field (pinned kernel/camera/camera.h
// camera_sample_aperture, camera_sample_perspective, camera_sample_orthographic,
// camera_sample_panorama, camera_sample_to_ray; camera/projection.h panorama_to_direction;
// sample/mapping.h sample_uniform_disk, regular_polygon_sample). Every sum, quotient and square
// root is a correctly rounded binary32 step in the pinned order (integer adds: no driver can
// contract or reassociate them); sine and cosine follow the scalar platform's polynomial
// (native_disk_sincos, up to 5*pi/4) or the correctly rounded wide adapter beyond; arccosine,
// arcsine and tangent are correctly rounded, as glibc's acosf, asinf and tanf are.
struct CameraRay {P:vec3<f32>,D:vec3<f32>,dP:f32,dD:f32,tmax:f32,valid:bool}
fn cp_sincos(angle:f32)->vec2<f32>{
    if(abs(angle)<=3.9269908169872414){return native_disk_sincos(angle);}
    return trig_sincos_wide(angle);
}
// Cycles transform_point of the originating oracle host (camera_direction plus the translation).
fn cp_point(t:NativeTransform,v:vec3<f32>)->vec3<f32>{
    let c0=vec3<f32>(t.x.x,t.y.x,t.z.x)*v.x;let c1=vec3<f32>(t.x.y,t.y.y,t.z.y)*v.y;let c2=vec3<f32>(t.x.z,t.y.z,t.z.z)*v.z;
    let w=vec3<f32>(t.x.w,t.y.w,t.z.w);
    // SSE: ((w + z*c2) + y*c1) + x*c0; scalar float4 dots: (x*c0 + y*c1) + (z*c2 + w).
    if(CAMERA_NATIVE_VECTOR_DIVIDE!=0u){return rounded_add3(rounded_add3(rounded_add3(w,c2),c1),c0);}
    return rounded_add3(rounded_add3(c0,c1),rounded_add3(c2,w));
}
// The double-float arccosine of trig_acos (wavefront_trig.wgsl) before its final rounding.
fn cp_acos_pair(value:f32)->vec2<f32>{
    let absolute=abs(clamp(value,-1.0,1.0));let high=rounded_add(1.0,-absolute);
    let low=rounded_add(rounded_add(1.0,-high),-absolute);
    let x=vec2<f32>(high*.5,low*.5);let root=rounded_sqrt(x.x);
    if(root==0.0){return vec2<f32>(0.0);}
    let product=trig_product(root,root);
    let residual=rounded_add(rounded_add(x.x,-product.x),rounded_add(x.y,-product.y));
    let z=vec2<f32>(root,rounded_divide(residual,2.0*root));
    var coefficients=array<vec2<f32>,29>(vec2<f32>(0.0018622264033183455,3.0847819958157441e-12),vec2<f32>(0.0019650335889309645,2.7346319219495596e-11),vec2<f32>(0.0020776609890162945,4.3501873110696199e-11),vec2<f32>(0.0022014740388840437,-6.5173905335058624e-11),vec2<f32>(0.0023380918428301811,4.9281793923494632e-11),vec2<f32>(0.0024894487578421831,-7.9595299535412112e-11),vec2<f32>(0.0026578705292195082,1.0898778190701619e-10),vec2<f32>(0.0028461783658713102,3.5237631822865323e-11),vec2<f32>(0.0030578216537833214,-4.5252907324155878e-12),vec2<f32>(0.0032970595639199018,-6.0446416969006656e-11),vec2<f32>(0.0035692052915692329,1.0225670180399882e-10),vec2<f32>(0.0038809645920991898,-3.3261520707844605e-11),vec2<f32>(0.0042409072630107403,-1.6933137704511214e-10),vec2<f32>(0.0046601435169577599,-3.004266366929409e-11),vec2<f32>(0.005153309553861618,1.2845828654267644e-10),vec2<f32>(0.0057400376535952091,1.7246714473229119e-11),vec2<f32>(0.0064472104422748089,-1.3038516013391677e-10),vec2<f32>(0.0073125259950757027,-1.2147685721913826e-10),vec2<f32>(0.0083903353661298752,4.4348693988005561e-10),vec2<f32>(0.0097616091370582581,3.9213582034292038e-10),vec2<f32>(0.011551801115274429,-2.1913472425527658e-10),vec2<f32>(0.013964843936264515,-1.8626451422920631e-10),vec2<f32>(0.017352763563394547,8.5968237736877562e-10),vec2<f32>(0.022372158244252205,8.4665688696028418e-10),vec2<f32>(0.0303819440305233,4.139211438880519e-10),vec2<f32>(0.0446428582072258,-1.0643686557121335e-09),vec2<f32>(0.075000002980232239,-2.9802322415450888e-09),vec2<f32>(0.1666666716337204,-4.9670537405344106e-09),vec2<f32>(1,0));
    var polynomial=vec2<f32>(0.0017680810997262597,2.0789158589051837e-11);
    for(var k=0u;k<29u;k++){polynomial=trig_coeff(coefficients[k],trig_mul(polynomial,x));}
    return 2.0*trig_mul(z,polynomial);
}
// asinf (correctly rounded): pi/2 - acos(|x|) in double-float, one final rounding; asin(x) = x below 2^-12.
fn cp_asin(value:f32)->f32{
    let x=clamp(value,-1.0,1.0);let a=abs(x);
    if(a<0.000244140625){return x;}
    let reduced=trig_coeff(vec2<f32>(1.5707963705062866,-4.3711390001862426e-8),-cp_acos_pair(a));
    let r=rounded_add(reduced.x,reduced.y);return select(r,-r,x<0.0);
}
// tanf (correctly rounded) for |x| below about 3*pi/8: the double-float sine and cosine of the
// reduced angle (pi/2 - |x| above pi/4), their quotient corrected by its exact residual.
fn cp_tan(value:f32)->f32{
    let swap=abs(value)>0.7853981633974483;var x=vec2<f32>(value,0.0);
    if(swap){x=vec2<f32>(rounded_add(1.5707963705062866,-abs(value)),-4.3711390001862426e-8);}
    let sc=trig_sincos_series(x);
    var n=sc.xy;var d=sc.zw;if(swap){n=sc.zw;d=sc.xy;}
    let q=rounded_divide(n.x,d.x);let product=trig_product(q,d.x);
    let residual=rounded_add(rounded_add(rounded_add(n.x,-product.x),-product.y),rounded_add(n.y,-(q*d.y)));
    let t=rounded_add(q,rounded_divide(residual,d.x));
    return select(t,select(t,-t,value<0.0),swap);
}
// camera_sample_aperture: the disk (sample_uniform_disk, polar_to_cartesian) or regular_polygon_sample with
// blades corners, and the anamorphic ratio; every sine and cosine through one inlined body.
fn cp_aperture(rand:vec2<f32>)->vec2<f32>{
    let blades=data_float(CP_D_blades);var angles=vec2<f32>(0);var count=1u;var radius=0.0;var u=0.0;var v=0.0;
    if(blades==0.0){
        let a=rounded_add(2.0*rand.x,-1.0);let b=rounded_add(2.0*rand.y,-1.0);
        if(a==0.0&&b==0.0){return vec2<f32>(0);}
        if(a*a>b*b){radius=a;angles.x=0.7853981633974483*rounded_divide(b,a);}
        else{radius=b;angles.x=rounded_add(1.5707963705062866,-0.7853981633974483*rounded_divide(a,b));}
    }else{
        let uc=rand.x*blades;let corner=floor(uc);u=rounded_sqrt(rounded_add(uc,-corner));v=rand.y*u;u=rounded_add(1.0,-u);
        let angle=rounded_divide(3.1415927410125732,blades);
        angles=vec2<f32>(angle,rounded_add(data_float(CP_D_bladesrotation),(corner*2.0)*angle));count=2u;
    }
    var s0=vec2<f32>(0);var s1=vec2<f32>(0);let steps=rounded_steps(count);
    for(var k=0u;k<steps;k++){let sc=cp_sincos(select(angles.y,angles.x,k==0u));s0=select(s0,sc,k==0u);s1=select(s1,sc,k==1u);}
    var bokeh:vec2<f32>;
    if(blades==0.0){bokeh=vec2<f32>(radius*s0.y,radius*s0.x);}
    else{let q=vec2<f32>(rounded_add(u,v)*s0.y,rounded_add(u,-v)*s0.x);
        bokeh=vec2<f32>(rounded_add(s1.y*q.x,-(s1.x*q.y)),rounded_add(s1.x*q.x,s1.y*q.y));}
    bokeh.x*=data_float(CP_D_inv_aperture_ratio);
    return bokeh;
}
// panorama_to_direction of the camera-space raster position (u, v): the type's angles first, then every
// sine and cosine through one inlined body, then the direction (fisheye_to_direction for the fisheyes:
// phi = safe_acosf(safe_divide(u, r)), negated below v = 0).
fn cp_panorama(u_in:f32,v_in:f32)->vec3<f32>{
    let kind=data_word(CP_D_panorama_type);var u=u_in;var v=v_in;
    if(kind==CP_PANORAMA_MIRRORBALL){
        let x=rounded_add(2.0*u,-1.0);let z=rounded_add(2.0*v,-1.0);
        if(rounded_add(x*x,z*z)>1.0){return vec3<f32>(0);}
        let y=-rounded_sqrt(max(rounded_add(rounded_add(1.0,-(x*x)),-(z*z)),0.0));
        // 2 * dot(dir, I) * dir - I with I = (0, -1, 0): dot(dir, I) = -y.
        let d=2.0*(-y);return vec3<f32>(d*x,rounded_add(d*y,1.0),d*z);
    }
    if(kind==CP_PANORAMA_EQUIANGULAR_CUBEMAP_FACE){
        var t=vec2<f32>(0);let steps=rounded_steps(2u);
        for(var k=0u;k<steps;k++){let x=select(rounded_add(v,-0.5),rounded_add(0.5,-u),k==0u)*1.5707963705062866;
            t=select(t,vec2<f32>(cp_tan(x)),vec2<u32>(0u,1u)==vec2<u32>(k));}
        return camera_normalize(vec3<f32>(1.0,t.x,t.y));
    }
    var angles=vec3<f32>(0);var count=1u;var r=0.0;var z=0.0;var limit=1.0;
    if(kind==CP_PANORAMA_EQUIRECTANGULAR){
        let range=heap_vec4(p(6u)+CP_D_equirectangular_range);
        angles=vec3<f32>(rounded_add(range.z*v,range.w),rounded_add(range.x*u,range.y),0.0);count=2u;
    }else if(kind==CP_PANORAMA_CENTRAL_CYLINDRICAL){
        let range=heap_vec4(p(6u)+CP_D_central_cylindrical_range);
        angles.x=rounded_add(range.x,u*rounded_add(range.y,-range.x));z=rounded_add(range.z,v*rounded_add(range.w,-range.z));
    }else{
        let fov=data_float(CP_D_fisheye_fov);
        if(kind==CP_PANORAMA_FISHEYE_EQUIDISTANT){u=rounded_add(u,-0.5)*2.0;v=rounded_add(v,-0.5)*2.0;}
        else{u=rounded_add(u,-0.5)*data_float(CP_D_sensorwidth);v=rounded_add(v,-0.5)*data_float(CP_D_sensorheight);}
        r=rounded_sqrt(rounded_add(u*u,v*v));
        var theta=0.0;
        if(kind==CP_PANORAMA_FISHEYE_EQUIDISTANT){theta=(r*fov)*0.5;}
        else if(kind==CP_PANORAMA_FISHEYE_LENS_POLYNOMIAL){let r2=r*r;
            theta=-rounded_add(data_float(CP_D_fisheye_lens_polynomial_bias),motion_dot4(heap_vec4(p(6u)+CP_D_fisheye_lens_polynomial_coefficients),vec4<f32>(r,r2,r2*r,r2*r2)));}
        else{let lens=data_float(CP_D_fisheye_lens);theta=2.0*cp_asin(rounded_divide(r,2.0*lens));angles.z=fov*0.25;}
        var phi=trig_acos(select(0.0,rounded_divide(u,r),r!=0.0));if(v<0.0){phi=-phi;}
        angles=vec3<f32>(theta,phi,angles.z);count=select(2u,3u,kind==CP_PANORAMA_FISHEYE_EQUISOLID);
    }
    var s0=vec2<f32>(0);var s1=vec2<f32>(0);var s2=vec2<f32>(0);let steps=rounded_steps(count);
    for(var k=0u;k<steps;k++){let sc=cp_sincos(select(select(angles.z,angles.y,k==1u),angles.x,k==0u));
        s0=select(s0,sc,k==0u);s1=select(s1,sc,k==1u);s2=select(s2,sc,k==2u);}
    if(kind==CP_PANORAMA_EQUIRECTANGULAR){return vec3<f32>(s0.x*s1.y,s0.x*s1.x,s0.y);}
    if(kind==CP_PANORAMA_CENTRAL_CYLINDRICAL){return vec3<f32>(s0.y,s0.x,z);}
    if(kind==CP_PANORAMA_FISHEYE_EQUIDISTANT){if(r>1.0){return vec3<f32>(0);}}
    else if(kind==CP_PANORAMA_FISHEYE_LENS_POLYNOMIAL){if(abs(angles.x)>0.5*data_float(CP_D_fisheye_fov)){return vec3<f32>(0);}}
    else if(r>(2.0*data_float(CP_D_fisheye_lens))*s2.x){return vec3<f32>(0);}
    return vec3<f32>(s0.y,(-s1.y)*s0.x,s1.x*s0.x);
}
// camera_panorama_direction: the camera-space position of the raster point, then its direction.
fn cp_panorama_direction(x:f32,y:f32)->vec3<f32>{
    let c=camera_projection(p(6u)+D_cam_rastertocamera,vec4<f32>(x,y,0.0,1.0));return cp_panorama(c.x,c.y);
}
// camera_sample for every type but the pinhole perspective camera (the existing code in init_camera):
// pc is the perspective camera-space position (with perspective motion), world the camera transform.
fn camera_projected_ray(lens_rand:vec2<f32>,raster:vec2<f32>,pc:vec3<f32>,world:NativeTransform)->CameraRay{
    var out:CameraRay;out.valid=true;
    let kind=data_word(CP_D_type);let aperture=data_float(CP_D_aperturesize);let scale=data_float(D_cam_differential_scale);
    var lens=vec2<f32>(0);if(aperture>0.0){lens=cp_aperture(lens_rand)*aperture;}
    if(kind==CP_CAMERA_ORTHOGRAPHIC){
        var D=vec3<f32>(0,0,1);var P:vec3<f32>;let near=data_float(D_cam_nearclip);
        if(aperture>0.0){
            D=camera_normalize(vec3<f32>(rounded_add(0.0,-lens.x),rounded_add(0.0,-lens.y),data_float(CP_D_focaldistance)));
            P=rounded_add3(rounded_add3(pc,vec3<f32>(lens,0.0)),D*rounded_divide(near,D.z));
        }else{P=rounded_add3(pc,vec3<f32>(0.0,0.0,near));}
        out.P=cp_point(world,P);out.D=camera_normalize(camera_direction(world,D));
        out.dP=(0.5*(camera_length(heap_vec3(p(6u)+D_cam_dx))+camera_length(heap_vec3(p(6u)+D_cam_dy))))*scale;
        out.dD=0.0;out.tmax=data_float(D_cam_cliplength);return out;
    }
    if(kind==CP_CAMERA_PERSPECTIVE){
        var P=vec3<f32>(0);var D=pc;
        if(aperture>0.0){
            let ft=rounded_divide(data_float(CP_D_focaldistance),D.z);
            P=vec3<f32>(lens,0.0);D=camera_normalize(rounded_add3(D*ft,-P));
        }
        out.P=cp_point(world,P);out.D=camera_normalize(camera_direction(world,D));
        let center=camera_direction(world,pc);let direction=camera_normalize(center);
        let dx=camera_normalize(center+heap_vec3(p(6u)+D_cam_dx))-direction;
        let dy=camera_normalize(center+heap_vec3(p(6u)+D_cam_dy))-direction;
        out.dD=(0.5*(camera_length(dx)+camera_length(dy)))*scale;
        let zi=rounded_divide(1.0,camera_normalize(pc).z);let near=data_float(D_cam_nearclip)*zi;
        out.P=rounded_add3(out.P,near*out.D);out.dP=near*out.dD;out.tmax=data_float(D_cam_cliplength)*zi;return out;
    }
    // CAMERA_PANORAMA: camera_sample_panorama, then camera_sample_to_ray.
    // The direction at the raster point and one pixel along x and y, through one inlined body.
    var center=vec3<f32>(0);var Dx=vec3<f32>(0);var Dy=vec3<f32>(0);let points=rounded_steps(3u);
    for(var k=0u;k<points;k++){
        let d=cp_panorama_direction(select(raster.x,rounded_add(raster.x,1.0),k==1u),select(raster.y,rounded_add(raster.y,1.0),k==2u));
        center=select(center,d,vec3<bool>(k==0u));Dx=select(Dx,d,vec3<bool>(k==1u));Dy=select(Dy,d,vec3<bool>(k==2u));
    }
    if(all(center==vec3<f32>(0))){out.valid=false;return out;}
    var P=vec3<f32>(0);var D=center;
    if(aperture>0.0){
        let focus=camera_normalize(center);let Pfocus=focus*data_float(CP_D_focaldistance);
        let U=camera_normalize(rounded_add3(vec3<f32>(1,0,0),-(focus.x*focus)));
        let V=camera_normalize(rounded_cross(focus,U));
        P=rounded_add3(U*lens.x,V*lens.y);D=camera_normalize(rounded_add3(Pfocus,-P));
    }
    out.P=cp_point(world,P);out.D=camera_normalize(camera_direction(world,D));
    let c=camera_normalize(camera_direction(world,center));
    let ddx=rounded_add3(camera_normalize(camera_direction(world,Dx)),-c);let ddy=rounded_add3(camera_normalize(camera_direction(world,Dy)),-c);
    out.dD=(0.5*(camera_length(ddx)+camera_length(ddy)))*scale;
    let near=data_float(D_cam_nearclip);
    out.P=rounded_add3(out.P,near*out.D);out.dP=near*out.dD;out.tmax=data_float(D_cam_cliplength);
    return out;
}
//#endif
@compute @workgroup_size(64) fn init_camera(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}
    var path=0u;var x=0u;var y=0u;var sample_id=0u;var pixel=0u;var sample_offset=0u;
    let retry=p(2u)==0u;
    if(!retry){
        let tile_index=id.x/p(5u);if(tile_index>=p(3u)){return;}
        let t=p(2u)+tile_index*T_size;let tile_work=id.x%p(5u);
        let work_size=load_word(t+T_work_size);if(tile_work>=work_size){return;}
        let w=load_word(t+T_w);let h=load_word(t+T_h);let samples=load_word(t+T_num_samples);
        let begin=load_word(t+T_path_index_offset);
        if(w==0u||h==0u||samples==0u||w>p(10u)||h>p(10u)/w||samples>p(10u)/(w*h)||work_size!=w*h*samples||work_size>p(5u)||begin>=p(10u)||work_size>p(10u)-begin){fail();return;}
        path=begin+tile_work;
        var pixel_offset=tile_work/samples;var sample_in_tile=tile_work%samples;
        if(data_float(D_integrator_scrambling_distance)<0.9){sample_in_tile=tile_work/(w*h);pixel_offset=tile_work%(w*h);}
        x=load_word(t+T_x)+pixel_offset%w;y=load_word(t+T_y)+pixel_offset/w;
        let stride=load_word(t+T_stride);let offset=load_word(t+T_offset);
        // Signed tile offset intentionally wraps in the original uint index.
        // Validate the final index before accessing the render allocation.
        pixel=offset+x+y*stride;
        sample_id=load_word(t+T_start_sample)+sample_in_tile;sample_offset=load_word(t+T_sample_offset);
    }else{
        if(id.x>=p(13u)){return;}path=id.x;if(p(12u)!=0u){path=load_word(p(12u)+id.x*4u);}
        if(path>=p(10u)){fail();return;}
        pixel=load_word(state_address(S_path_render_pixel_index,path,4u));
        let width=u32(data_float(D_cam_width));if(width==0u){fail();return;}
        x=pixel%width;y=pixel/width;sample_id=load_word(state_address(S_path_sample,path,4u));
        if(load_half(state_address(S_path_queued_kernel,path,2u))!=K_CAMERA){fail();return;}
    }
    let stride=data_word(D_film_pass_stride);
    if(pixel>=p(11u)/stride){fail();return;}
    let film=p(4u)+pixel*stride*4u;
    if(!retry){
        state_u32(S_path_render_pixel_index,path,pixel);state_u16(S_path_queued_kernel,path,0u);
        let adaptive=data_word(D_film_pass_adaptive_aux_buffer);
        if(adaptive!=PASS_NONE&&heap_float(film+(adaptive+3u)*4u)!=0.0){return;}
        let count=data_word(D_film_pass_sample_count);
        if(count!=PASS_NONE){sample_id=atomicAdd(&heap[(film+count*4u)/4u],1u)+sample_offset;}
    }
//#if SAMPLING_SOBOL_BURLEY else     let rng=pixel_hash(x,y,data_word(D_integrator_seed));
    // path_rng_pixel_init: the blue-noise patterns place each pixel along one sequence (scrambled Morton curve).
    let blue_noise=data_word(SB_D_sampling_pattern)==SB_BLUE_NOISE_PURE||data_word(SB_D_sampling_pattern)==SB_BLUE_NOISE_FIRST;
    let rng=select(pixel_hash(x,y,data_word(D_integrator_seed)),sb_pixel(x,y,data_word(D_integrator_seed)),blue_noise);
//#endif
//#if SAMPLING_SOBOL_BURLEY else     var uv=vec2<f32>(0.5);if(sample_id!=0u){uv=native_rng(sample_id,rng,RNG_FILTER).xy;}
    var uv=vec2<f32>(0.5);if(sample_id!=0u){uv=native_rng_pattern(sample_id,rng,RNG_FILTER,2u).xy;}
//#endif
    var raster=vec2<f32>(f32(x),f32(y));
    if(data_word(D_integrator_pixel_jitter)==0x7f7fffffu){raster+=vec2<f32>(filter_value(uv.x),filter_value(uv.y));}
    else{raster-=vec2<f32>(data_float(D_integrator_pixel_jitter),data_float(D_integrator_pixel_jitter+4u));}
//#if CAMERA_PROJECTIONS
    // rand_time_lens (integrate_camera_sample): one 3D sample for the shutter time and the lens.
    var lens_time=vec4<f32>(0);
    if(data_float(D_cam_shuttertime)!=-1.0||data_float(CP_D_aperturesize)>0.0){
//#if SAMPLING_SOBOL_BURLEY else         lens_time=native_rng(sample_id,rng,RNG_LENS_TIME);
        lens_time=native_rng_pattern(sample_id,rng,RNG_LENS_TIME,3u);
//#endif
    }
//#endif
    var time=0.5;
    if(data_float(D_cam_shuttertime)!=-1.0){
//#if !CAMERA_PROJECTIONS
//#if SAMPLING_SOBOL_BURLEY else         time=camera_shutter(native_rng(sample_id,rng,RNG_LENS_TIME).x);
        time=camera_shutter(native_rng_pattern(sample_id,rng,RNG_LENS_TIME,3u).x);
//#endif
//#endif
//#if CAMERA_PROJECTIONS
        time=camera_shutter(lens_time.x);
//#endif
        if(data_word(D_cam_rolling_shutter_type)!=0u){
            // Every sum is an integer rounded_add: a driver that reassociates
            // constants ((1-q)-0.5 -> 0.5-q) or fuses a product would change the time.
            let row_time=rounded_add(1.0,-rounded_divide(f32(y),data_float(D_cam_height)));let duration=data_float(D_cam_rolling_shutter_duration);
            if(duration!=0.0){time=rounded_add(time,-0.5)*duration;
                time=rounded_add(time,rounded_add(rounded_add(row_time,-0.5)*rounded_add(1.0,-duration),0.5));}
            else{time=row_time;}
        }
    }
    let rt=p(6u)+D_cam_rastertocamera;let v=vec4<f32>(raster,0.0,1.0);
    var pc=camera_projection(rt,v);
    if(data_word(D_cam_have_perspective_motion)!=0u){
        if(time<0.5){let before=camera_projection(p(6u)+D_cam_perspective_pre,v);pc=camera_interp(before,pc,time*2.0);}
        else{let after=camera_projection(p(6u)+D_cam_perspective_post,v);pc=camera_interp(pc,after,(time-0.5)*2.0);}
    }
    var world=motion_transform_load(p(6u)+D_cam_cameratoworld);
    let steps=data_word(D_cam_num_motion_steps);
    if(steps!=0u){
        // Removed by scene specialization when the camera has no motion steps (pipelines.h).
//#if MOTION_CAMERA else atomicOr(&heap[p(17u)/4u],2u);return;
        world=motion_interpolate(G_camera_motion,0u,steps,time);
//#endif
    }
    if(load_word(p(17u))!=0u){return;}
//#if !CAMERA_PROJECTIONS
    let center=camera_direction(world,pc);let direction=camera_normalize(center);
    let dx=camera_normalize(center+heap_vec3(p(6u)+D_cam_dx))-direction;
    let dy=camera_normalize(center+heap_vec3(p(6u)+D_cam_dy))-direction;
    let dd=(0.5*(camera_length(dx)+camera_length(dy)))*data_float(D_cam_differential_scale);
    let zi=rounded_divide(1.0,camera_normalize(pc).z);let near=data_float(D_cam_nearclip)*zi;
    // Cycles: ray->P += nearclip*ray->D, the product rounded before the sum.
    let origin=rounded_add3(vec3<f32>(world.x.w,world.y.w,world.z.w),near*direction);
    state_vec3(S_ray_P,path,origin);state_vec3(S_ray_D,path,direction);
    state_f32(S_ray_dP,path,near*dd);state_f32(S_ray_dD,path,dd);
    state_f32(S_ray_tmin,path,0.0);state_f32(S_ray_tmax,path,data_float(D_cam_cliplength)*zi);state_f32(S_ray_time,path,time);
//#endif
//#if CAMERA_PROJECTIONS
    let ray=camera_projected_ray(lens_time.yz,raster,pc,world);
    if(!ray.valid){
        // camera_sample returned zero (outside a fisheye's image circle or the mirror ball): no path for
        // this sample (integrator_path_terminate for a retried path).
        if(retry){let counter=load_word(p(7u)+S_queue_counter);atomicSub(&heap[(counter+K_CAMERA*4u)/4u],1u);state_u16(S_path_queued_kernel,path,0u);}
        return;
    }
    state_vec3(S_ray_P,path,ray.P);state_vec3(S_ray_D,path,ray.D);
    state_f32(S_ray_dP,path,ray.dP);state_f32(S_ray_dD,path,ray.dD);
    state_f32(S_ray_tmin,path,0.0);state_f32(S_ray_tmax,path,ray.tmax);state_f32(S_ray_time,path,time);
//#endif
    state_u32(S_path_sample,path,sample_id);state_u32(S_path_rng_pixel,path,rng);state_u16(S_path_rng_offset,path,RNG_BOUNCE);
    state_u16(S_path_bounce,path,0u);state_u16(S_path_diffuse_bounce,path,0u);state_u16(S_path_glossy_bounce,path,0u);
    state_u16(S_path_transmission_bounce,path,0u);state_u16(S_path_transparent_bounce,path,0u);
    state_u16(S_path_volume_bounce,path,0u);state_u16(S_path_volume_bounds_bounce,path,0u);
    state_u8(S_path_visibility,path,VIS_CAMERA);state_u8(S_path_mnee,path,0u);state_u32(S_path_flag,path,INITIAL_FLAG);
//#if FILM_DENOISING
    // path_state_init_integrator: denoising features start at the camera with unit throughput.
    if((data_word(D_kernel_features)&DN_KERNEL_FEATURE_DENOISING)!=0u){
        state_u32(S_path_flag,path,INITIAL_FLAG|DN_PATH_RAY_DENOISING_FEATURES);state_vec3(S_path_denoising_feature_throughput,path,vec3<f32>(1.0));}
//#endif
    state_f32(S_path_mis_ray_pdf,path,0.0);state_u32(S_path_min_ray_pdf,path,0x7f7fffffu);
    state_f32(S_path_continuation_probability,path,1.0);state_vec3(S_path_throughput,path,vec3<f32>(1.0));
    state_f32(S_path_optical_depth,path,0.0);state_u32(S_isect_object,path,NONE);state_u32(S_isect_prim,path,NONE);state_u32(S_isect_type,path,PRIM_TYPE_NONE);
//#if LIGHT_LINKING
    // path_state_init: the forward light-linking receiver starts as the background object.
    if((data_word(D_kernel_features)&KF_LIGHT_LINKING)!=0u){state_u32(S_path_mis_ray_object,path,data_word(D_background_object_index));}
//#endif
    if(volume_enabled()){
        store_word(volume_stack_address(path,0u,VS_object),data_word(D_background_object_index));
        store_word(volume_stack_address(path,0u,VS_shader),data_word(D_background_volume_shader));
        store_word(volume_stack_address(path,1u,VS_object),NONE);store_word(volume_stack_address(path,1u,VS_shader),NONE);
    }
    let counter=load_word(p(7u)+S_queue_counter);
    if(retry){atomicSub(&heap[(counter+K_CAMERA*4u)/4u],1u);}
    let next=select(K_CLOSEST,DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK,data_word(D_cam_is_inside_volume)!=0u);
    atomicAdd(&heap[(counter+next*4u)/4u],1u);state_u16(S_path_queued_kernel,path,next);
}
