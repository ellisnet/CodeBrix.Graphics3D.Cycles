//#if FILM_PASSES_ANY
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Film data passes of the surface stage: pinned Cycles film/data_passes.h
// film_write_data_passes, with integrator/surface_shader.h (alpha, transparency,
// average normal and roughness, the diffuse/glossy/transmission colours),
// closure/bsdf.h bsdf_albedo and bsdf_get_roughness_pass_squared,
// closure/bsdf_microfacet.h bsdf_microfacet_estimate_albedo, camera/camera.h
// camera_z_depth and camera_distance, geom/primitive.h primitive_uv and
// geom/object.h object_pass_id and shader_pass_id. Typed GPU records replace
// ShaderData pointers; the constants come from film_passes.cpp. Also
// film/denoising_passes.h film_write_denoising_features_surface.
fn fp_microfacet(kind:u32)->bool{
    return (kind>=FP_CLOSURE_BSDF_MICROFACET_GGX_ID&&kind<=FP_CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID)||
        (kind>=FP_CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID&&kind<=FP_CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID)||
        (kind>=FP_CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID&&kind<=FP_CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID);
}
fn fp_diffuse(kind:u32)->bool{return kind>=FP_CLOSURE_BSDF_DIFFUSE_ID&&kind<=FP_CLOSURE_BSDF_TRANSLUCENT_ID;}
fn fp_glossy(kind:u32)->bool{return (kind>=FP_CLOSURE_BSDF_MICROFACET_GGX_ID&&kind<=FP_CLOSURE_BSDF_HAIR_REFLECTION_ID)||kind==FP_CLOSURE_BSDF_HAIR_CHIANG_ID||kind==FP_CLOSURE_BSDF_HAIR_HUANG_ID;}
fn fp_transmission(kind:u32)->bool{return kind>=FP_CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID&&kind<=FP_CLOSURE_BSDF_HAIR_TRANSMISSION_ID;}
fn fp_glass(kind:u32)->bool{return kind>=FP_CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID&&kind<=FP_CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID;}
fn fp_refraction(kind:u32)->bool{return kind>=FP_CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID&&kind<=FP_CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID;}
fn fp_bssrdf(kind:u32)->bool{return kind>=FP_CLOSURE_BSSRDF_BURLEY_ID&&kind<=FP_CLOSURE_BSSRDF_RANDOM_WALK_SKIN_ID;}
fn fp_bsdf(kind:u32)->bool{return kind!=FP_CLOSURE_NONE_ID&&kind<=FP_CLOSURE_BSDF_TRANSPARENT_ID;}
fn fp_average(v:vec3<f32>)->f32{return (v.x+v.y+v.z)*(1.0/3.0);}
// mix(a, b, t) = a + t*(b - a) (util/math_float3.h), each operation rounded once.
fn fp_mix3(a:vec3<f32>,b:vec3<f32>,t:f32)->vec3<f32>{return rounded_add3(a,t*rounded_add3(b,-a));}
// bsdf_microfacet_estimate_albedo: the lookup-table branches of the generalized
// Schlick (exponent < 0), F82 tint and dielectric Fresnel models (no thin film),
// the thin glass transmission lobe, otherwise microfacet_fresnel at dot(wi, N).
// Microfacet records hold the Fresnel model in `roughness` (surface_closure.h):
// 0 none or the Glass node's generalized Schlick (glass kinds), 1 dielectric,
// 3 conductor, 4 generalized Schlick, 5 F82 tint.
// The reflection and transmission parts of bsdf_microfacet_estimate_albedo for one closure,
// so one inlined estimate serves every lobe selection (the colour passes and the denoising
// albedo): albedo = weight * (reflection * eval_reflection + transmission * eval_transmission).
struct AlbedoParts {r:vec3<f32>,t:vec3<f32>,microfacet:bool}
fn fp_albedo_parts(sd:SurfaceData,c:SvmClosure)->AlbedoParts{
//#if SVM_BSDF_HAIR_CHIANG
    var parts:AlbedoParts;parts.microfacet=fp_microfacet(c.kind);
    if(c.kind==FP_CLOSURE_BSDF_HAIR_CHIANG_ID){parts.r=hair_chiang_albedo(sd,c);return parts;}
//#endif
//#if !SVM_BSDF_HAIR_CHIANG
//#if SVM_BSDF_HAIR_HUANG else     var parts:AlbedoParts;parts.microfacet=fp_microfacet(c.kind);if(!parts.microfacet){return parts;}
    var parts:AlbedoParts;parts.microfacet=fp_microfacet(c.kind);
//#endif
//#endif
//#if SVM_BSDF_HAIR_HUANG
    // bsdf_albedo: reflection and transmission alike (its one colour pass is the glossy one).
    if(c.kind==HH_CLOSURE_BSDF_HAIR_HUANG_ID){parts.microfacet=true;parts.r=huang_albedo(sd,c);parts.t=parts.r;return parts;}
//#endif
//#if SVM_BSDF_HAIR_CHIANG
    if(!parts.microfacet){return parts;}
//#endif
//#if !SVM_BSDF_HAIR_CHIANG
//#if SVM_BSDF_HAIR_HUANG
    if(!parts.microfacet){return parts;}
//#endif
//#endif
    let cos_ni=rounded_dot(sd.wi,c.N);let model=u32(c.roughness);let glass=fp_glass(c.kind);
    var st:SvmState;let rough=rounded_sqrt(rounded_sqrt(c.alpha_x*c.alpha_y));
    let schlick=(model==4u&&!(c.a>0.1))||(model==0u&&glass);
    let f82=model==5u&&!(c.a>0.1);
    if(schlick||f82||(model==1u&&c.ior>1.0)){
        // One table read: ggx_gen_schlick_ior_s at z = sqrt(|(ior - 1) / (ior + 1)|), or ggx_gen_schlick_s at 0.5 (F82).
        var z=0.5;var table=data_word(FP_D_ggx_gen_schlick_s);
        if(!f82){z=rounded_sqrt(abs(rounded_divide(rounded_add(c.ior,-1.0),rounded_add(c.ior,1.0))));table=data_word(FP_D_ggx_gen_schlick_ior_s);}
        let s=microfacet_lut3(&st,rough,cos_ni,z,table);
        if(f82){parts.r=fp_mix3(c.multiscatter,vec3<f32>(1),s);return parts;}
        if(schlick){
            let f0=select(c.multiscatter,vec3<f32>(c.fresnel_f0),model==0u);
            let F=fp_mix3(f0,vec3<f32>(1),s);
            parts.r=F*c.reflection_tint;parts.t=rounded_add3(vec3<f32>(1),-F)*c.transmission_tint;return parts;
        }
        let F=rounded_add(ggx_f0(c.ior),s*rounded_add(1.0,-ggx_f0(c.ior)));
        parts.r=vec3<f32>(F);if(glass){parts.t=vec3<f32>(rounded_add(1.0,-F));}return parts;
    }
    if(model==0u&&c.kind==FP_CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID){parts.t=vec3<f32>(1);return parts;}
    // microfacet_fresnel: no Fresnel (purely reflective or refractive closures)
    // or the Principled models (ggx_fresnel); has_reflection / has_transmission.
    if(model==0u&&!fp_refraction(c.kind)&&!glass){parts.r=vec3<f32>(1);return parts;}
    // ggx_fresnel reads the lobes from the GGX kinds: Beckmann records take the GGX kind with the same lobes.
    var g=c;
    if(c.kind==FP_CLOSURE_BSDF_MICROFACET_BECKMANN_ID){g.kind=FP_CLOSURE_BSDF_MICROFACET_GGX_ID;}
    else if(c.kind==FP_CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID){g.kind=FP_CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID;}
    else if(c.kind==FP_CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID){g.kind=FP_CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID;}
    let f=ggx_fresnel(g,cos_ni);
    let has_reflection=!fp_refraction(c.kind);let has_transmission=glass||!has_reflection;
    parts.r=f.reflection*select(0.0,1.0,has_reflection);parts.t=f.transmission*select(0.0,1.0,has_transmission);
    return parts;
}
// bsdf_albedo with the lobe selection (eval_reflection, eval_transmission).
fn fp_albedo(c:SvmClosure,parts:AlbedoParts,reflection:bool,transmission:bool)->vec3<f32>{
//#if SVM_BSDF_HAIR_CHIANG
    // weight * bsdf_hair_chiang_albedo, whatever the lobe selection.
    if(c.kind==FP_CLOSURE_BSDF_HAIR_CHIANG_ID){return c.weight*parts.r;}
//#endif
    if(!parts.microfacet){return c.weight;}
//#if SVM_BSDF_HAIR_HUANG
    if(c.kind==HH_CLOSURE_BSDF_HAIR_HUANG_ID){return c.weight*parts.r;}
//#endif
    return c.weight*(parts.r*select(0.0,1.0,reflection)+parts.t*select(0.0,1.0,transmission));
}
// bsdf_get_roughness_pass_squared (negative: no roughness).
fn fp_roughness_squared(c:SvmClosure)->f32{
    if(c.kind==FP_CLOSURE_BSDF_OREN_NAYAR_ID||c.kind==FP_CLOSURE_BSDF_ROUGH_TRANSLUCENT_ID){let r=c.roughness*c.roughness;return r*r;}
    if(fp_diffuse(c.kind)){return -1.0;}
    if(c.kind==FP_CLOSURE_BSDF_TRANSPARENT_ID||c.kind==FP_CLOSURE_BSDF_RAY_PORTAL_ID){return 0.0;}
    if(fp_microfacet(c.kind)){return c.alpha_x*c.alpha_y;}
    return 1.0;
}
fn fp_film_store(address:u32,value:f32){store_word(address,bitcast<u32>(value));}
// camera_distance (perspective or orthographic camera).
fn fp_camera_distance(P:vec3<f32>)->f32{
    let t=p(6u)+FP_D_cameratoworld;let camP=vec3<f32>(heap_float(t+12u),heap_float(t+28u),heap_float(t+44u));
    let d=rounded_add3(P,-camP);
    if(data_word(FP_D_type)==FP_CAMERA_ORTHOGRAPHIC){return abs(rounded_dot(d,vec3<f32>(heap_float(t+8u),heap_float(t+24u),heap_float(t+40u))));}
    return rounded_sqrt(rounded_dot(d,d));
}
//#if CAMERA_PROJECTIONS
// camera_z_depth: the camera-space z for perspective and orthographic cameras, the distance to the
// camera position for panoramic ones.
fn fp_z_depth(P:vec3<f32>)->f32{
    if(data_word(FP_D_type)!=FP_CAMERA_PANORAMA){return surface_transform_point(p(6u)+FP_D_worldtocamera,P).z;}
    let t=p(6u)+FP_D_cameratoworld;let d=rounded_add3(P,-vec3<f32>(heap_float(t+12u),heap_float(t+28u),heap_float(t+44u)));
    return rounded_sqrt(rounded_dot(d,d));
}
//#endif
// primitive_uv: the ATTR_STD_UV attribute (0 when the mesh has none).
fn fp_uv(sd:SurfaceData)->vec3<f32>{
    var st:SvmState;st.geometry=sd;let desc=attribute_find(&st,FP_ATTR_STD_UV,0u);
    if(!desc.found){return vec3<f32>(0);}
    let value=attribute_read(&st,desc).value;return vec3<f32>(value.x,value.y,1.0);
}
//#if FILM_MOTION
// camera_motion_vector (perspective camera): the world-to-raster projections of the
// centre and of the pre and post positions; transform_perspective with SSE float4 dots.
fn fp_dot4(a:vec4<f32>,b:vec4<f32>)->f32{return rounded_add(rounded_add(a.x*b.x,a.y*b.y),rounded_add(a.z*b.z,a.w*b.w));}
fn fp_perspective(address:u32,a:vec3<f32>)->vec3<f32>{
    let b=vec4<f32>(a,1.0);let c=vec3<f32>(fp_dot4(heap_vec4(address),b),fp_dot4(heap_vec4(address+16u),b),fp_dot4(heap_vec4(address+32u),b));
    let w=fp_dot4(heap_vec4(address+48u),b);if(w==0.0){return vec3<f32>(0);}
    return vec3<f32>(rounded_divide(c.x,w),rounded_divide(c.y,w),rounded_divide(c.z,w));
}
// primitive_motion_data_without_camera for triangles: the shading point (with deforming geometry, FILM_MOTION_DEFORM,
// the pre and post positions of the motion steps) through the object motion pass transforms (push words 5 and 8).
struct MotionData {center:vec3<f32>,pre:vec3<f32>,post:vec3<f32>,error:bool}
fn fp_motion_data(sd:SurfaceData)->MotionData{
    var m:MotionData;if(sd.object_id>=p(8u)/FPM_TRANSFORMS_PER_OBJECT){fail();m.error=true;return m;}
    let tfm=p(5u)+sd.object_id*FPM_TRANSFORMS_PER_OBJECT*FPM_TRANSFORM_BYTES;
//#if CURVES
    if((sd.primitive_type&PRIMITIVE_CURVE)!=0u){return curve_motion_data(sd,tfm);}
//#endif
//#if POINTS
    // primitive_motion_data_without_camera for a point: the point's center (point_motion_center_location), and the
    // pre and post positions of its motion steps, which the object motion pass transforms take.
    if((sd.primitive_type&PRIMITIVE_POINT)!=0u){
        let object=table_at(G_objects,sd.object_id,SIZE_objects);let offset=bitcast<i32>(load_word(object+O_position_offset));
        var center=point_at(offset,sd.prim).xyz;
        if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)==0u){center=surface_object_point(sd,center,false);}
        var pre=center;var post=center;
        if((sd.object_flag&SD_OBJECT_HAS_VERTEX_MOTION)!=0u){
            let numverts=bitcast<i32>(load_word(object+O_numverts));
            pre=point_at(offset+numverts,sd.prim).xyz;
            if(load_half(object+O_num_geom_steps)>2u){post=point_at(offset+2*numverts,sd.prim).xyz;}
            else{post=surface_transform_point(object+O_itfm,center);}
        }
        if(load_word(p(17u))!=0u){m.error=true;return m;}
        m.center=center;m.pre=surface_transform_point(tfm+FPM_PRE*FPM_TRANSFORM_BYTES,pre);m.post=surface_transform_point(tfm+FPM_POST*FPM_TRANSFORM_BYTES,post);
        return m;
    }
//#endif
//#if FILM_MOTION_DEFORM else     m.center=sd.P;m.pre=surface_transform_point(tfm+FPM_PRE*FPM_TRANSFORM_BYTES,sd.P);m.post=surface_transform_point(tfm+FPM_POST*FPM_TRANSFORM_BYTES,sd.P);
    var pre=sd.P;var post=sd.P;
    if((sd.object_flag&SD_OBJECT_HAS_VERTEX_MOTION)!=0u){
        // primitive_motion_position: the motion steps after the centre positions, triangle_interpolate with every
        // product rounded before its sum; with two steps the post position is the centre in object space.
        let at=table_at(G_tri_vindex,sd.prim,SIZE_tri_vindex);let vi=vec3<u32>(load_word(at),load_word(at+4u),load_word(at+8u));
        let w=rounded_add(1.0-sd.u,-sd.v);
        pre=rounded_add3(rounded_add3(w*triangle_position(sd.object_id,vi.x,1u),sd.u*triangle_position(sd.object_id,vi.y,1u)),sd.v*triangle_position(sd.object_id,vi.z,1u));
        if(load_half(table_at(G_objects,sd.object_id,SIZE_objects)+O_num_geom_steps)>2u){
            post=rounded_add3(rounded_add3(w*triangle_position(sd.object_id,vi.x,2u),sd.u*triangle_position(sd.object_id,vi.y,2u)),sd.v*triangle_position(sd.object_id,vi.z,2u));}
        else{post=surface_transform_point(table_at(G_objects,sd.object_id,SIZE_objects)+O_itfm,sd.P);}
        if(load_word(p(17u))!=0u){m.error=true;return m;}
    }
    m.center=sd.P;m.pre=surface_transform_point(tfm+FPM_PRE*FPM_TRANSFORM_BYTES,pre);m.post=surface_transform_point(tfm+FPM_POST*FPM_TRANSFORM_BYTES,post);
//#endif
    return m;
}
//#if CURVES
// primitive_motion_data_without_camera for curves: curve_motion_center_location (the linear position between the segment's
// centre keys, in world space unless baked), primitive_motion_position (mix of the segment's keys in a motion step).
fn curve_motion_data(sd:SurfaceData,tfm:u32)->MotionData{
    var m:MotionData;let o=cv_order();
    if(sd.prim>=table_count(G_curves)){fail();m.error=true;return m;}
    let k0=load_word(table_at(G_curves,sd.prim,16u)+4u)+(sd.primitive_type>>PRIMITIVE_NUM_BITS);
    let object=table_at(G_objects,sd.object_id,SIZE_objects);let position_offset=bitcast<i32>(load_word(object+O_position_offset));
    let a=curve_key(position_offset,k0).xyz;let b=curve_key(position_offset,k0+1u).xyz;
    var center=cvb3(cvb3(b*sd.u,o)+cvb3(a*cvb(1.0-sd.u,o),o),o);
    if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)==0u){center=surface_object_point(sd,center,false);}
    var pre=center;var post=center;
    if((sd.object_flag&SD_OBJECT_HAS_VERTEX_MOTION)!=0u){
        let numverts=i32(load_word(object+O_numverts));
        pre=cv_mix4(curve_key(position_offset+numverts,k0),curve_key(position_offset+numverts,k0+1u),sd.u,o).xyz;
        if(load_half(object+O_num_geom_steps)>2u){post=cv_mix4(curve_key(position_offset+2*numverts,k0),curve_key(position_offset+2*numverts,k0+1u),sd.u,o).xyz;}
        else{post=surface_object_point(sd,post,true);}
    }
    if(load_word(p(17u))!=0u){m.error=true;return m;}
    m.center=center;m.pre=surface_transform_point(tfm+FPM_PRE*FPM_TRANSFORM_BYTES,pre);m.post=surface_transform_point(tfm+FPM_POST*FPM_TRANSFORM_BYTES,post);
    return m;
}
//#endif
// project_reflection (Ray Tracing Gems chapter 32.4.3): the point seen in a planar reflector.
fn fp_project_reflection(rP:vec3<f32>,rN:vec3<f32>,P:vec3<f32>)->vec3<f32>{
    let o=rounded_add3(P,-rP);let N=select(rN,-rN,rounded_dot(rN,o)<0.0);
    let T=surface_orthogonal(N);let B=rounded_cross(N,T);
    let local=vec3<f32>(rounded_dot(o,T),rounded_dot(o,B),rounded_dot(o,N));
    let z_o=max(local.z,1e-5);let z_i=rounded_divide(1.0,-rounded_divide(1.0,z_o));let ratio=rounded_divide(z_i,z_o);
    let x_i=-ratio*local.x;let y_i=-ratio*local.y;
    // to_global(v, T, B, N) = T * v.x + B * v.y + N * v.z.
    return rounded_add3(rP,rounded_add3(rounded_add3(T*x_i,B*y_i),N*z_i));
}
fn fp_camera_motion_vector(center:vec3<f32>,pre:vec3<f32>,post:vec3<f32>)->vec4<f32>{
//#if CAMERA_PANORAMA_INVERSE
    // Panoramic cameras: the camera-space directions (world-to-camera, motion_pass_pre and motion_pass_post) through
    // direction_to_panorama, scaled to the raster.
    if(data_word(D_cam_type)==CPI_CAMERA_PANORAMA){return cpi_motion_vector(center,pre,post,true);}
//#endif
    let c=fp_perspective(p(6u)+FPM_D_worldtoraster,center);
    let a=rounded_add3(fp_perspective(p(6u)+FPM_D_perspective_pre,pre),-c);let b=rounded_add3(c,-fp_perspective(p(6u)+FPM_D_perspective_post,post));
    return vec4<f32>(a.x,a.y,b.x,b.y);
}
//#endif
//#if FILM_DATA_PASSES
//#if FILM_CRYPTOMATTE
// film_write_cryptomatte_slots (__ATOMIC_PASS_WRITE__): the first empty slot is
// claimed for the id with a compare-and-swap (a slot another invocation claimed for
// a different id moves on to the next), a slot holding the id or the last slot
// takes the weight.
fn film_cryptomatte_slots(buffer:u32,slots:u32,id:f32,weight:f32){
    if(weight==0.0){return;}
    for(var slot=0u;slot<slots;slot++){
        let at=buffer+slot*8u;
        if(bitcast<f32>(load_word(at))==0.0){
            // atomic_compare_and_swap_float(buffer + slot*2, ID_NONE, id); a weak exchange
            // can fail spuriously, so it is retried while the slot still holds ID_NONE.
            var old=0u;
            loop{let result=atomicCompareExchangeWeak(&heap[at/4u],0u,bitcast<u32>(id));old=result.old_value;
                if(result.exchanged||old!=0u){break;}}
            if(old!=0u&&bitcast<f32>(old)!=id){continue;}
            film_add(at+4u,weight);return;
        }
        if(bitcast<f32>(load_word(at))==id||slot==slots-1u){film_add(at+4u,weight);return;}
    }
}
//#endif
// film_write_data_passes for one surface hit (pinned order).
fn film_data_passes(index:u32,sd:SurfaceData,r:SvmResult){
    var flag=state_word(S_path_flag,index);
    if((flag&PATH_RAY_TRANSPARENT_BACKGROUND)==0u||(flag&FP_PATH_RAY_SHADOW_CATCHER_PASS)!=0u){return;}
    let pass_flag=data_word(D_film_pass_flag);if(pass_flag==0u){return;}
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_path_render_pixel_index,index);let base=p(4u)+pixel*stride*4u;
    // surface_shader_transparency / surface_shader_alpha (SD_HAS_ONLY_VOLUME never reaches this stage).
    var transparency=vec3<f32>(0);if((r.runtime_flag&(FP_SR_TRANSPARENT|FP_SR_RAY_PORTAL))!=0u){transparency=r.transparency;}
    let alpha=clamp(vec3<f32>(1)-transparency,vec3<f32>(0),vec3<f32>(1));
    if((flag&PATH_RAY_SINGLE_PASS_DONE)==0u){
        if(state_word(S_path_sample,index)==0u){
//#if CAMERA_PROJECTIONS else             if((pass_flag&MASK_DEPTH)!=0u){fp_film_store(base+data_word(D_film_pass_depth)*4u,surface_transform_point(p(6u)+FP_D_worldtocamera,sd.P).z);}
            if((pass_flag&MASK_DEPTH)!=0u){fp_film_store(base+data_word(D_film_pass_depth)*4u,fp_z_depth(sd.P));}
//#endif
            if((pass_flag&MASK_OBJECT_ID)!=0u){
                var id=0.0;if(sd.object_id!=FP_OBJECT_NONE){id=heap_float(table_at(G_objects,sd.object_id,SIZE_objects)+FP_O_pass_id);}
                fp_film_store(base+data_word(D_film_pass_object_id)*4u,id);
            }
            if((pass_flag&MASK_MATERIAL_ID)!=0u){
                let shader=sd.shader&SHADER_MASK;if(shader>=p(19u)){fail();return;}
                fp_film_store(base+data_word(D_film_pass_material_id)*4u,f32(bitcast<i32>(load_word(p(18u)+shader*SIZE_shaders+FP_SH_pass_id))));
            }
            if((pass_flag&MASK_POSITION)!=0u){let a=base+data_word(D_film_pass_position)*4u;fp_film_store(a,sd.P.x);fp_film_store(a+4u,sd.P.y);fp_film_store(a+8u,sd.P.z);}
        }
        let threshold=data_float(FP_D_pass_alpha_threshold);
        if((r.runtime_flag&(FP_SR_TRANSPARENT|FP_SR_RAY_PORTAL))==0u||threshold==0.0||fp_average(alpha)>=threshold){
            if((pass_flag&FP_MASK_NORMAL)!=0u){
                // surface_shader_average_normal.
                var n=vec3<f32>(0);
                for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);
                    if(c.kind!=FP_CLOSURE_NONE_ID&&c.kind<=FP_CLOSURE_BSSRDF_RANDOM_WALK_SKIN_ID){n=rounded_add3(n,c.N*abs(fp_average(c.weight)));}}
                if(all(n==vec3<f32>(0))){n=sd.N;}else{n=surface_native_normalize(n);}
                film_rgb(base,data_word(FP_D_pass_normal),n);
            }
            if((pass_flag&FP_MASK_ROUGHNESS)!=0u){
                // surface_shader_average_roughness.
                var roughness=0.0;var sum=0.0;
                for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);if(!fp_bsdf(c.kind)){continue;}
                    let value=fp_roughness_squared(c);
                    if(value>=0.0){let weight=abs(fp_average(c.weight));roughness=rounded_add(roughness,weight*rounded_sqrt(rounded_sqrt(value)));sum=rounded_add(sum,weight);}}
                film_add(base+data_word(FP_D_pass_roughness)*4u,select(1.0,rounded_divide(roughness,sum),sum>0.0));
            }
            if((pass_flag&FP_MASK_UV)!=0u){film_rgb(base,data_word(FP_D_pass_uv),fp_uv(sd));}
//#if FILM_MOTION
            if((pass_flag&FPM_MASK_MOTION)!=0u){
                // primitive_motion_vector: the shading point through the object motion pass transforms
                // (no deformation: refused), then camera_motion_vector.
                let m=fp_motion_data(sd);if(m.error){return;}
                let speed=fp_camera_motion_vector(m.center,m.pre,m.post);
                let at=base+data_word(FPM_D_pass_motion)*4u;film_add(at,speed.x);film_add(at+4u,speed.y);film_add(at+8u,speed.z);film_add(at+12u,speed.w);
                film_add(base+data_word(FPM_D_pass_motion_weight)*4u,1.0);
            }
//#endif
            flag|=PATH_RAY_SINGLE_PASS_DONE;state_u32(S_path_flag,index,flag);
        }
    }
    let throughput=heap_vec3(state_address(S_path_throughput,index,12u));
//#if FILM_CRYPTOMATTE
    let crypt=data_word(FPC_D_cryptomatte_passes);
    if(crypt!=0u&&(pass_flag&FPC_MASK_CRYPTOMATTE)!=0u){
        // matte_weight = average(throughput) * (1 - average(surface_shader_transparency)).
        let weight=fp_average(throughput)*rounded_add(1.0,-fp_average(transparency));
        if(weight>0.0){
            let depth=data_word(FPC_D_cryptomatte_depth);var at=base+data_word(FPC_D_pass_cryptomatte)*4u;
            let has_object=sd.object_id!=FP_OBJECT_NONE;let object=select(0u,table_at(G_objects,sd.object_id,SIZE_objects),has_object);
            if((crypt&FPC_CRYPT_OBJECT)!=0u){film_cryptomatte_slots(at,depth*2u,select(0.0,heap_float(object+FPC_O_cryptomatte_object),has_object),weight);at+=depth*16u;}
            if((crypt&FPC_CRYPT_MATERIAL)!=0u){
                let shader=sd.shader&SHADER_MASK;if(shader>=p(19u)){fail();return;}
                film_cryptomatte_slots(at,depth*2u,heap_float(p(18u)+shader*SIZE_shaders+FPC_SH_cryptomatte_id),weight);at+=depth*16u;}
            if((crypt&FPC_CRYPT_ASSET)!=0u){film_cryptomatte_slots(at,depth*2u,select(0.0,heap_float(object+FPC_O_cryptomatte_asset),has_object),weight);at+=depth*16u;}
        }
    }
//#endif
    if((pass_flag&(FP_MASK_DIFFUSE_COLOR|FP_MASK_GLOSSY_COLOR|FP_MASK_TRANSMISSION_COLOR))!=0u){
        // surface_shader_diffuse, _glossy and _transmission times the throughput.
        var diffuse=vec3<f32>(0);var glossy=vec3<f32>(0);var transmission=vec3<f32>(0);
        for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);let parts=fp_albedo_parts(sd,c);
            if((pass_flag&FP_MASK_DIFFUSE_COLOR)!=0u&&(fp_diffuse(c.kind)||fp_bssrdf(c.kind))){diffuse=rounded_add3(diffuse,fp_albedo(c,parts,true,true));}
            if((pass_flag&FP_MASK_GLOSSY_COLOR)!=0u&&(fp_glossy(c.kind)||fp_glass(c.kind))){glossy=rounded_add3(glossy,fp_albedo(c,parts,true,false));}
            if((pass_flag&FP_MASK_TRANSMISSION_COLOR)!=0u&&(fp_transmission(c.kind)||fp_glass(c.kind))){transmission=rounded_add3(transmission,fp_albedo(c,parts,false,true));}}
        if((pass_flag&FP_MASK_DIFFUSE_COLOR)!=0u){film_rgb(base,data_word(FP_D_pass_diffuse_color),diffuse*throughput);}
        if((pass_flag&FP_MASK_GLOSSY_COLOR)!=0u){film_rgb(base,data_word(FP_D_pass_glossy_color),glossy*throughput);}
        if((pass_flag&FP_MASK_TRANSMISSION_COLOR)!=0u){film_rgb(base,data_word(FP_D_pass_transmission_color),transmission*throughput);}
    }
    if((pass_flag&FP_MASK_MIST)!=0u){
        // Depth into 0..1, the falloff, modulated by transparency; the pass
        // accessor writes 1 - the accumulated value.
        var mist=clamp(rounded_add(fp_camera_distance(sd.P),-data_float(FP_D_mist_start))*data_float(FP_D_mist_inv_depth),0.0,1.0);
        let falloff=data_float(FP_D_mist_falloff);
        if(falloff==1.0){}else if(falloff==2.0){mist=mist*mist;}else if(falloff==0.5){mist=rounded_sqrt(mist);}else{mist=pow(mist,falloff);}
        film_add(base+data_word(FP_D_pass_mist)*4u,rounded_add(1.0,-mist)*fp_average(throughput*alpha));
    }
}
//#endif
//#if FILM_DENOISING
// smoothstep (util/math_base.h), each operation rounded once.
fn fp_smoothstep(edge0:f32,edge1:f32,x:f32)->f32{
    if(x<edge0){return 0.0;}if(x>=edge1){return 1.0;}
    let t=rounded_divide(rounded_add(x,-edge0),rounded_add(edge1,-edge0));return rounded_add(3.0,-2.0*t)*(t*t);
}
fn fp_finite3(v:vec3<f32>)->vec3<f32>{return select(v,vec3<f32>(0),(bitcast<vec3<u32>>(v)&vec3<u32>(0x7f800000u))==vec3<u32>(0x7f800000u));}
fn fp_finite(v:f32)->f32{return select(v,0.0,(bitcast<u32>(v)&0x7f800000u)==0x7f800000u);}
// film_write_denoising_features_surface (the motion features are refused with the motion pass).
fn film_denoising_surface(index:u32,sd:SurfaceData,r:SvmResult){
//#if CAMERA_PANORAMA_INVERSE
    // A panoramic camera's backward and specular motion features (mutually exclusive) share one camera_motion_vector call
    // site below, so direction_to_panorama is inlined once for both (pipeline size guard).
    var dn_motion_kind=0u;var dn_motion_in=array<vec3<f32>,3>();var dn_motion_depth=0.0;
//#endif
    var flag=state_word(S_path_flag,index);
    if((flag&DN_PATH_RAY_DENOISING_FEATURES)==0u||(flag&FP_PATH_RAY_SHADOW_CATCHER_PASS)!=0u){return;}
    let options=data_word(DN_D_denoising_pass_options_flag);let weighting=(options&DN_USE_ALBEDO_ROUGHNESS_WEIGHTING)!=0u;
    let stride=data_word(D_film_pass_stride);let pixel=state_word(S_path_render_pixel_index,index);let base=p(4u)+pixel*stride*4u;
    var normal=vec3<f32>(0);var diffuse_albedo=vec3<f32>(0);var specular_albedo=vec3<f32>(0);var transparent_albedo=vec3<f32>(0);
    var specular_roughness=0.0;var sum_weight=0.0;var sum_nonspecular_weight=0.0;
    for(var i=0u;i<r.count;i++){let c=svm_closure_read(r,i);
        if(c.kind==FP_CLOSURE_NONE_ID||c.kind>FP_CLOSURE_BSSRDF_RANDOM_WALK_SKIN_ID){continue;}
        // Transparency always passes through.
        if(c.kind==FP_CLOSURE_BSDF_TRANSPARENT_ID){transparent_albedo=rounded_add3(transparent_albedo,c.weight);continue;}
        let albedo=fp_albedo(c,fp_albedo_parts(sd,c),true,true);let weight=fp_average(albedo);
//#if SVM_BSDF_HAIR_HUANG else         normal=rounded_add3(normal,c.N*weight);
        // safe_normalize(sd->dPdu) for the Huang model: its T holds that.
        normal=rounded_add3(normal,select(c.N,c.T,c.kind==HH_CLOSURE_BSDF_HAIR_HUANG_ID)*weight);
//#endif
        // sqrtf(bsdf_get_specular_roughness_squared): 0 singular, alpha_x * alpha_y microfacet, else 1.
        var squared=1.0;if(c.kind==FP_CLOSURE_BSDF_RAY_PORTAL_ID){squared=0.0;}else if(fp_microfacet(c.kind)){squared=c.alpha_x*c.alpha_y;}
        let roughness=rounded_sqrt(squared);
        // Transition smoothly from specular to diffuse between 0.0 and 0.15 roughness.
        let diffuse_weight=fp_smoothstep(0.0,0.15,roughness);
        if(weighting){diffuse_albedo=rounded_add3(diffuse_albedo,albedo*diffuse_weight);specular_albedo=rounded_add3(specular_albedo,albedo*rounded_add(1.0,-diffuse_weight));}
        else if(fp_diffuse(c.kind)||fp_bssrdf(c.kind)){diffuse_albedo=rounded_add3(diffuse_albedo,albedo);}
        else if(fp_glossy(c.kind)||fp_glass(c.kind)){specular_albedo=rounded_add3(specular_albedo,albedo);}
        specular_roughness=rounded_add(specular_roughness,rounded_sqrt(roughness)*weight);
        sum_weight=rounded_add(sum_weight,weight);sum_nonspecular_weight=rounded_add(sum_nonspecular_weight,weight*diffuse_weight);
    }
    // Fraction of non-transparent closures, for smooth blending at transparent surfaces.
    let transparent_weight=fp_average(transparent_albedo);let total_weight=rounded_add(sum_weight,transparent_weight);
    var feature_weight=0.0;
    if(sum_weight>0.0){normal=vec3<f32>(rounded_divide(normal.x,sum_weight),rounded_divide(normal.y,sum_weight),rounded_divide(normal.z,sum_weight));
        specular_roughness=rounded_divide(specular_roughness,sum_weight);feature_weight=fp_smoothstep(0.0,0.5,rounded_divide(sum_nonspecular_weight,sum_weight));}
    let deferred_feature_weight=rounded_add(1.0,-feature_weight);
    let follow=(options&DN_FOLLOW_REFLECTIONS)!=0u;if(!follow){feature_weight=1.0;}
    let bounce=state_half(S_path_bounce,index);let first=bounce==0u;
    let throughput=heap_vec3(state_address(S_path_denoising_feature_throughput,index,12u));
    if(first||follow){
        if(data_word(DN_D_pass_denoising_depth)!=PASS_NONE){
            // denoising_depth_compute: the ray length minus tmin, or the camera z depth step.
            let d=rounded_add(sd.ray_length,-heap_float(state_address(S_ray_tmin,index,4u)));var depth=d;
            if(!follow){let prev=rounded_add3(sd.P,sd.wi*d);
//#if CAMERA_PROJECTIONS else                 depth=rounded_add(surface_transform_point(p(6u)+FP_D_worldtocamera,sd.P).z,-surface_transform_point(p(6u)+FP_D_worldtocamera,prev).z);}
                depth=rounded_add(fp_z_depth(sd.P),-fp_z_depth(prev));}
//#endif
            film_add(base+data_word(DN_D_pass_denoising_depth)*4u,fp_finite(depth*fp_average(throughput)));
        }
        if(data_word(DN_D_pass_denoising_normal)!=PASS_NONE&&feature_weight>0.0){
            // Transform the normal into camera space.
            let n=surface_transform_direction(p(6u)+FP_D_worldtocamera,normal);
            let opaque_fraction=select(1.0,rounded_divide(sum_weight,total_weight),total_weight>0.0);
            film_rgb(base,data_word(DN_D_pass_denoising_normal),fp_finite3(((n*opaque_fraction)*feature_weight)*fp_average(throughput)));
        }
        if(data_word(DN_D_pass_denoising_albedo)!=PASS_NONE&&feature_weight>0.0){
            film_rgb(base,data_word(DN_D_pass_denoising_albedo),fp_finite3((diffuse_albedo*feature_weight)*throughput));
        }
    }
    if(first){
        if(data_word(DN_D_pass_denoising_roughness)!=PASS_NONE){
            film_add(base+data_word(DN_D_pass_denoising_roughness)*4u,fp_finite(specular_roughness*fp_average(throughput)));}
        if(data_word(DN_D_pass_denoising_specular_albedo)!=PASS_NONE){
            film_rgb(base,data_word(DN_D_pass_denoising_specular_albedo),fp_finite3(specular_albedo*throughput));}
//#if FILM_MOTION
        if(data_word(FPM_D_pass_denoising_backward_motion)!=PASS_NONE){
            // primitive_motion_vector_backward_depth_delta: the motion vector and the camera-space depth step.
            let m=fp_motion_data(sd);if(m.error){return;}
            let center_cam=surface_transform_point(p(6u)+FP_D_worldtocamera,m.center);let pre_cam=surface_transform_point(p(6u)+FPM_D_motion_pass_pre,m.pre);
//#if CAMERA_PANORAMA_INVERSE else             let motion=fp_camera_motion_vector(m.center,m.pre,m.post);
            dn_motion_kind=1u;dn_motion_in=array<vec3<f32>,3>(m.center,m.pre,m.post);dn_motion_depth=rounded_add(pre_cam.z,-center_cam.z);
//#endif
//#if CAMERA_PANORAMA_INVERSE else             film_rgb(base,data_word(FPM_D_pass_denoising_backward_motion),vec3<f32>(motion.x,motion.y,rounded_add(pre_cam.z,-center_cam.z)));
//#endif
        }
//#endif
    }
//#if FILM_MOTION
    else if(bounce==1u&&(flag&DN_PATH_RAY_REFLECT)!=0u&&data_word(FPM_D_pass_denoising_specular_motion)!=PASS_NONE){
        // primitive_motion_vector_reflection from the reflector (the ray origin and the MIS origin normal).
        let m=fp_motion_data(sd);if(m.error){return;}
        let rP=heap_vec3(state_address(S_ray_P,index,12u));let rN=heap_vec3(state_address(S_path_mis_origin_n,index,12u));
//#if CAMERA_PANORAMA_INVERSE else         let motion=fp_camera_motion_vector(fp_project_reflection(rP,rN,m.center),fp_project_reflection(rP,rN,m.pre),fp_project_reflection(rP,rN,m.post));
        dn_motion_kind=2u;dn_motion_in=array<vec3<f32>,3>(fp_project_reflection(rP,rN,m.center),fp_project_reflection(rP,rN,m.pre),fp_project_reflection(rP,rN,m.post));
//#endif
//#if CAMERA_PANORAMA_INVERSE else         let at=base+data_word(FPM_D_pass_denoising_specular_motion)*4u;film_add(at,motion.x);film_add(at+4u,motion.y);
//#endif
    }
//#endif
//#if CAMERA_PANORAMA_INVERSE
//#if FILM_MOTION
    if(dn_motion_kind!=0u){
        let motion=fp_camera_motion_vector(dn_motion_in[0],dn_motion_in[1],dn_motion_in[2]);
        if(dn_motion_kind==1u){film_rgb(base,data_word(FPM_D_pass_denoising_backward_motion),vec3<f32>(motion.x,motion.y,dn_motion_depth));}
        else{let at=base+data_word(FPM_D_pass_denoising_specular_motion)*4u;film_add(at,motion.x);film_add(at+4u,motion.y);}
    }
//#endif
//#endif
    // Portion deferred to the next bounce. Specularity uses the feature weight, transparent
    // always passes through (without the specular motion pass only transparent when not
    // following reflections).
    var deferred_albedo=rounded_add3(specular_albedo*deferred_feature_weight,transparent_albedo);
    // When not following reflections, but the specular motion pass is enabled, still need to
    // continue to the first bounce, but with no weight for the albedo pass.
    var specular_motion=false;
//#if FILM_MOTION
    specular_motion=data_word(FPM_D_pass_denoising_specular_motion)!=PASS_NONE;
//#endif
    if(!follow&&!specular_motion){deferred_albedo=transparent_albedo;}
    let deferred_any=deferred_albedo;
    let a=abs(deferred_any);
    if(max(max(a.x,a.y),a.z)>1e-4){if(!follow){deferred_albedo=transparent_albedo;}state_vec3(S_path_denoising_feature_throughput,index,throughput*deferred_albedo);}
    else{flag&=~DN_PATH_RAY_DENOISING_FEATURES;state_u32(S_path_flag,index,flag);}
}
//#endif
// The surface stage's data passes and denoising features, between
// connect_surface_light (holdout, emission, termination: records in state 2)
// and the bounce (shade_surface).
@compute @workgroup_size(64) fn shade_surface_passes(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)||(p(28u)!=0u&&load_word(p(17u))!=0u)){return;}let at=p(24u)+id.x*SURFACE_STAGE_BYTES;if(load_word(at)!=2u){return;}
    var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    let stage=surface_stage_read(at);
//#if SUBSURFACE
    // The subsurface exit point (ss_exit): integrate_surface skips film_write_data_passes and the denoising features.
    if((stage.sd.runtime_flag&SS_SR_EXIT_POINT)!=0u){return;}
//#endif
//#if FILM_DATA_PASSES
    film_data_passes(index,stage.sd,stage.r);
//#endif
//#if FILM_DENOISING
    film_denoising_surface(index,stage.sd,stage.r);
//#endif
}
//#endif
