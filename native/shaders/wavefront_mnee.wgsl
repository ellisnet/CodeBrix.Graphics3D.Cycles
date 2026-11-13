//#if MNEE
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Modified from pinned Cycles kernel/integrator/mnee.h, intersect_mnee.h,
// state_util.h (integrator_state_write_mnee) and light/point.h
// (point_light_mnee_sample_update): manifold next-event estimation in WGSL.
// DEVICE_KERNEL_INTEGRATOR_INTERSECT_MNEE runs as seven dispatches over one
// GPU-private record per dispatched path (native/cycles/mnee.cpp), which keeps
// every entry inside the pipeline size guard:
//   mnee_receiver   receiver ShaderData (shader_setup_from_ray)
//   mnee_seed       light sample, probe ray (caster hits)
//   mnee_eval       caster shaders, refractive closure, half-vector offsets
//   mnee_setup      manifold vertices of the seed hits
//   mnee_solve      the Newton walk
//   mnee_transfer   light update, transfer matrix, geometry term, visibility
//   mnee_contribute solution shaders, BSDF terms, the MNEE shadow slot and the
//                   queue transition of every path
// Every float sum of products, every division and square root goes through the
// correctly rounded adapters (rounded_add/dot/cross/divide/sqrt) in the pinned
// operation order: the walk's convergence and iteration count must not depend
// on driver contraction. The whole file is one MNEE specialization region, so
// a scene without caustics compiles byte-identical surface WGSL.
// See native/THIRD-PARTY-NOTICES.txt.
const MNEE_MAX_ITERATIONS:u32=64u;
const MNEE_MAX_INTERSECTION_COUNT:u32=10u;
const MNEE_SOLVER_THRESHOLD:f32=0.001;
const MNEE_MINIMUM_STEP_SIZE:f32=0.0001;
const MNEE_MAX_CAUSTIC_CASTERS:u32=6u;
const MNEE_MIN_DISTANCE:f32=0.001;
const MNEE_MIN_PROGRESS_DISTANCE:f32=0.0001;
const MNEE_MIN_DETERMINANT:f32=0.0001;
const MNEE_PROJECTION_DISTANCE_MULTIPLIER:f32=2.0;
const MNEE_FLT_MAX:f32=3.4028234663852886e38;
// Record layout (bytes). MR_CLOSURES and MNEE_RECORD_BYTES come from mnee.cpp.
const MR_SD:u32=0u;
const MR_STAGE:u32=240u;
const MR_LS:u32=244u;
const MR_META:u32=316u;
const MR_HITS:u32=380u;
const MR_V:u32=572u;
const MR_T:u32=1628u;
const MR_OUT:u32=2684u;
const MN_VERTEX_BYTES:u32=176u;
// Stage words: the entry that continues the path's walk; MN_DONE = queue transition only.
const MN_EVAL:u32=1u;
const MN_SOLVE:u32=2u;
const MN_CONTRIBUTE:u32=3u;
const MN_DONE:u32=4u;
const MN_SETUP:u32=5u;
const MN_TRANSFER:u32=6u;
// Meta words (diagnostics for the state gate).
const MM_COUNT:u32=0u;
const MM_ITERATIONS:u32=4u;
const MM_SOLVER:u32=8u;
const MM_RESULT:u32=12u;
const MM_D:u32=16u;
const MM_FIXED:u32=28u;
const MM_BOUNCES:u32=32u;

// The invocation's traversal stack (MR_STACK of its record): the surface module's copy of the
// closest traversal keeps its stack there (native/tools/build_cycles.py, mnee_traversal).
var<private> mn_stack_at:u32=0u;
fn mn_record(slot:u32)->u32{
    let bytes=arrayLength(&heap)*4u;let base=p(26u);
    if(base==0u||base%16u!=0u||base>bytes||MNEE_RECORD_BYTES>bytes-base||slot>(bytes-base-MNEE_RECORD_BYTES)/MNEE_RECORD_BYTES){fail();return 0u;}
    mn_stack_at=base+slot*MNEE_RECORD_BYTES+MR_STACK;
    return base+slot*MNEE_RECORD_BYTES;
}
fn mn_index(id:u32)->u32{var index=id;if(p(2u)!=0u){index=load_word(p(2u)+id*4u);}return index;}
fn mn_f(at:u32)->f32{return bitcast<f32>(load_word(at));}
fn mn_sf(at:u32,v:f32){store_word(at,bitcast<u32>(v));}
fn mn_s3(at:u32,v:vec3<f32>){mn_sf(at,v.x);mn_sf(at+4u,v.y);mn_sf(at+8u,v.z);}
fn mn_v2(at:u32)->vec2<f32>{return vec2<f32>(mn_f(at),mn_f(at+4u));}
fn mn_s2(at:u32,v:vec2<f32>){mn_sf(at,v.x);mn_sf(at+4u,v.y);}
fn mn_v4(at:u32)->vec4<f32>{return heap_vec4(at);}
fn mn_s4(at:u32,v:vec4<f32>){mn_sf(at,v.x);mn_sf(at+4u,v.y);mn_sf(at+8u,v.z);mn_sf(at+12u,v.w);}

// Binary32 adapters with the oracle's float3 operation order (util/math_float3.h).
fn mn_sub(a:vec3<f32>,b:vec3<f32>)->vec3<f32>{return rounded_add3(a,-b);}
fn mn_len(v:vec3<f32>)->f32{return rounded_sqrt(rounded_dot(v,v));}
fn mn_len2(v:vec2<f32>)->f32{return rounded_sqrt(rounded_add(v.x*v.x,v.y*v.y));}
fn mn_dot2(a:vec2<f32>,b:vec2<f32>)->f32{return rounded_add(a.x*b.x,a.y*b.y);}
// float3 / float: SSE divides each lane, scalar float3 multiplies by 1/f.
fn mn_div3(v:vec3<f32>,f:f32)->vec3<f32>{
    if(NATIVE_FLOAT3_RECIPROCAL_NORMALIZE!=0u){return v*rounded_divide(1.0,f);}
    return vec3<f32>(rounded_divide(v.x,f),rounded_divide(v.y,f),rounded_divide(v.z,f));
}
// float4 / float multiplies by 1/f on every host (util/math_float4.h).
fn mn_div4(v:vec4<f32>,f:f32)->vec4<f32>{return v*rounded_divide(1.0,f);}
fn mn_normalize(v:vec3<f32>)->vec3<f32>{return surface_native_normalize(v);}
// normalize_len: t = len(a); a * (1/t).
fn mn_normalize_len(v:vec3<f32>)->vec4<f32>{let t=mn_len(v);return vec4<f32>(v*rounded_divide(1.0,t),t);}
// safe_normalize_len: t = len(a); t != 0 ? a / t : a.
fn mn_safe_normalize_len(v:vec3<f32>)->vec4<f32>{let t=mn_len(v);if(t!=0.0){return vec4<f32>(mn_div3(v,t),t);}return vec4<f32>(v,t);}
struct MnFrame {a:vec3<f32>,b:vec3<f32>}
fn mn_orthonormals(n:vec3<f32>)->MnFrame{
    var a=vec3<f32>(n.z-n.y,n.x-n.z,n.y-n.x);if(n.x==n.y&&n.x==n.z){a=vec3<f32>(n.z-n.y,n.x+n.z,-n.y-n.x);}
    a=mn_normalize(a);return MnFrame(a,rounded_cross(n,a));
}
fn mn_m22v(a:vec4<f32>,b:vec2<f32>)->vec2<f32>{return vec2<f32>(rounded_add(a.x*b.x,a.y*b.y),rounded_add(a.z*b.x,a.w*b.y));}
fn mn_m22(a:vec4<f32>,b:vec4<f32>)->vec4<f32>{
    return vec4<f32>(rounded_add(a.x*b.x,a.y*b.z),rounded_add(a.x*b.y,a.y*b.w),rounded_add(a.z*b.x,a.w*b.z),rounded_add(a.z*b.y,a.w*b.w));
}
fn mn_det(m:vec4<f32>)->f32{return rounded_add(m.x*m.w,-(m.y*m.z));}
// mat22_inverse: x = inverse (when |det| >= MNEE_MIN_DETERMINANT), w-slot of the
// returned pair is det (0 = singular).
struct MnInverse {m:vec4<f32>,det:f32}
fn mn_inverse(m:vec4<f32>)->MnInverse{
    let det=mn_det(m);var out:MnInverse;if(abs(det)<MNEE_MIN_DETERMINANT){out.det=0.0;return out;}
    out.m=mn_div4(vec4<f32>(m.w,-m.y,-m.z,m.x),det);out.det=det;return out;
}

struct MnVertex {p:vec3<f32>,dp_du:vec3<f32>,dp_dv:vec3<f32>,n:vec3<f32>,ng:vec3<f32>,dn_du:vec3<f32>,dn_dv:vec3<f32>,
    uv:vec2<f32>,object:u32,prim:u32,shader:u32,eta:f32,n_offset:vec2<f32>,constraint:vec2<f32>,a:vec4<f32>,b:vec4<f32>,c:vec4<f32>,ci:u32}
fn mn_read(at:u32)->MnVertex{var v:MnVertex;
    v.p=heap_vec3(at);v.dp_du=heap_vec3(at+12u);v.dp_dv=heap_vec3(at+24u);v.n=heap_vec3(at+36u);v.ng=heap_vec3(at+48u);
    v.dn_du=heap_vec3(at+60u);v.dn_dv=heap_vec3(at+72u);v.uv=mn_v2(at+84u);v.object=load_word(at+92u);v.prim=load_word(at+96u);
    v.shader=load_word(at+100u);v.eta=mn_f(at+104u);v.n_offset=mn_v2(at+108u);v.constraint=mn_v2(at+116u);
    v.a=mn_v4(at+124u);v.b=mn_v4(at+140u);v.c=mn_v4(at+156u);v.ci=load_word(at+172u);return v;}
fn mn_write(at:u32,v:MnVertex){
    mn_s3(at,v.p);mn_s3(at+12u,v.dp_du);mn_s3(at+24u,v.dp_dv);mn_s3(at+36u,v.n);mn_s3(at+48u,v.ng);
    mn_s3(at+60u,v.dn_du);mn_s3(at+72u,v.dn_dv);mn_s2(at+84u,v.uv);store_word(at+92u,v.object);store_word(at+96u,v.prim);
    store_word(at+100u,v.shader);mn_sf(at+104u,v.eta);mn_s2(at+108u,v.n_offset);mn_s2(at+116u,v.constraint);
    mn_s4(at+124u,v.a);mn_s4(at+140u,v.b);mn_s4(at+156u,v.c);store_word(at+172u,v.ci);}

// Light sample of the record (integrator_state_write_mnee fields plus the
// selection pdf that light_sample_update re-applies).
struct MnLight {P:vec3<f32>,D:vec3<f32>,t:f32,pdf:f32,eval_fac:f32,pdf_selection:f32,shader:u32,object:u32,prim:u32,kind:u32,group:u32,Ng:vec3<f32>}
fn mn_light_write(at:u32,ls:MnLight){
    mn_s3(at,ls.P);mn_s3(at+12u,ls.D);mn_sf(at+24u,ls.t);mn_sf(at+28u,ls.pdf);mn_sf(at+32u,ls.eval_fac);mn_sf(at+36u,ls.pdf_selection);
    store_word(at+40u,ls.shader);store_word(at+44u,ls.object);store_word(at+48u,ls.prim);store_word(at+52u,ls.kind);store_word(at+56u,ls.group);mn_s3(at+60u,ls.Ng);}
fn mn_light_read(at:u32)->MnLight{var ls:MnLight;
    ls.P=heap_vec3(at);ls.D=heap_vec3(at+12u);ls.t=mn_f(at+24u);ls.pdf=mn_f(at+28u);ls.eval_fac=mn_f(at+32u);ls.pdf_selection=mn_f(at+36u);
    ls.shader=load_word(at+40u);ls.object=load_word(at+44u);ls.prim=load_word(at+48u);ls.kind=load_word(at+52u);ls.group=load_word(at+56u);ls.Ng=heap_vec3(at+60u);return ls;}

// scene_intersect with PATH_RAY_VISIBILITY_TRANSMIT and intersection_ray_valid.
fn mn_trace(P:vec3<f32>,D:vec3<f32>,tmin:f32,tmax:f32,time:f32,self_object:u32,self_prim:u32,light_object:u32,light_prim:u32)->NativeHit{
    var none=NativeHit(tmax,0.0,0.0,NONE,NONE,PRIM_TYPE_NONE);
    if((bitcast<u32>(P.x)&0x7f800000u)==0x7f800000u||(bitcast<u32>(D.x)&0x7f800000u)==0x7f800000u||rounded_dot(D,D)==0.0){return none;}
    return trace_native_filtered(P,D,tmin,tmax,time,PATH_RAY_VISIBILITY_TRANSMIT,self_object,self_prim,light_object,light_prim);
}
fn mn_t_offset(t:f32)->f32{return select(bitcast<f32>(bitcast<u32>(t)+1u),1.1754943508222875e-38,t==0.0);}
fn mn_object_flag(object:u32)->u32{return load_word(table_at(G_object_flag,object,4u));}
fn mn_queue(index:u32,next:u32,shader:u32){
    // integrator_path_next_sorted(INTERSECT_MNEE -> SHADE_SURFACE, shader).
    let divisor=load_word(p(7u)+S_sort_partition_divisor);let key=shader+data_word(D_max_shaders)*(index/divisor);
    if(key>=p(15u)){fail();return;}
    let sort=load_word(p(7u)+S_sort_key_counter+next*POINTER_SIZE);atomicAdd(&heap[(sort+key*4u)/4u],1u);state_u32(S_path_shader_sort_key,index,key);
    let counter=load_word(p(7u)+S_queue_counter)/4u;atomicSub(&heap[counter+MNEE_K_INTERSECT],1u);atomicAdd(&heap[counter+next],1u);
    state_u16(S_path_queued_kernel,index,next);
}

// Cycles fast_sincosf (util/math_fast.h) with every madd unfused.
fn mn_fast_sincos(angle:f32)->vec2<f32>{
    let q=i32(round(angle*0.31830987334251404));let qf=f32(q);var x=angle;
    x=rounded_add(qf*-3.140625,x);x=rounded_add(qf*-0.000967502593994140625,x);
    x=rounded_add(qf*-1.5099067240953445e-07,x);x=rounded_add(qf*-5.126688136514179e-12,x);
    x=rounded_add(1.5707963705062866,-rounded_add(1.5707963705062866,-x));
    let s=x*x;if((q&1)!=0){x=-x;}
    var su=2.6083159809786593541503e-06;su=rounded_add(su*s,-0.0001981069071916863322258);su=rounded_add(su*s,0.00833307858556509017944336);
    su=rounded_add(su*s,-0.166666597127914428710938);su=rounded_add(s*(su*x),x);
    var cu=-2.71811842367242206819355e-07;cu=rounded_add(cu*s,2.47990446951007470488548e-05);cu=rounded_add(cu*s,-0.00138888787478208541870117);
    cu=rounded_add(cu*s,0.0416666641831398010253906);cu=rounded_add(cu*s,-0.5);cu=rounded_add(cu*s,1.0);
    if((q&1)!=0){cu=-cu;}
    return vec2<f32>(clamp(su,-1.0,1.0),clamp(cu,-1.0,1.0));
}
//#if MNEE_ANISOTROPIC
// atanf(ratio * tanf(angle)) for mnee_sample_bsdf_dh's anisotropic branch, both correctly rounded as glibc's are: the
// arithmetic of family_tan and of family_atan / trig_atan2(x, 1) (wavefront_microfacet.wgsl, wavefront_trig.wgsl), operation
// for operation, with the reduction by pi/2 and the double-float sine and cosine series they share run by one loop body
// (the two calls inlined cost about 40,000 B more of the MNEE entry that samples the casters).
fn mn_atan_tan(ratio:f32,first:f32)->f32{
    var angle=first;var x=0.0;var theta=0.0;
    for(var k=0u;k<2u;k++){
        let n=i32(floor(angle*0.6366197723675814+0.5));let product=trig_product(f32(n),1.5707963705062866);
        let head=trig_two_sum(angle,-product.x);let r=trig_two_sum(head.x,rounded_add(head.y,rounded_add(-product.y,f32(n)*4.3711390001862426e-8)));
        let sc=select(trig_sincos_series(r),vec4<f32>(r,1.0,0.0),k==0u&&abs(r.x)<1e-6);
        if(k==0u){
            // family_tan: the quotient of the two series, rounded once.
            var num=sc.xy;var den=sc.zw;if((n&1)!=0){num=sc.zw;den=-sc.xy;}
            let q=rounded_divide(num.x,den.x);let qd=trig_product(q,den.x);
            let correction=rounded_divide(rounded_add(rounded_add(rounded_add(num.x,-qd.x),-qd.y),rounded_add(num.y,-(q*den.y))),den.x);
            x=ratio*rounded_add(q,correction);
            // family_atan: x itself near 0, pi/2 minus 1/x near the pole, else trig_atan2(x, 1) from the driver's value.
            if(abs(x)<1e-6){return x;}
            if(abs(x)>1e6){let inverse=rounded_divide(1.0,x);return select(rounded_add(-1.5707963705062866,rounded_add(4.371139000186243e-8,-inverse)),rounded_add(1.5707963705062866,rounded_add(-4.371139000186243e-8,-inverse)),x>0.0);}
            theta=atan2(x,1.0);if(theta!=theta){return theta;}
            angle=theta;continue;
        }
        let quadrant=((n%4)+4)%4;var sn=sc.xy;var cs=sc.zw;
        if(quadrant==1){sn=sc.zw;cs=-sc.xy;}else if(quadrant==2){sn=-sc.xy;cs=-sc.zw;}else if(quadrant==3){sn=-sc.zw;cs=sc.xy;}
        let a=trig_mul(vec2<f32>(x,0.0),cs);let b=trig_mul(vec2<f32>(1.0,0.0),sn);
        let numerator=rounded_add(rounded_add(a.x,-b.x),rounded_add(a.y,-b.y));
        let denominator=rounded_add(1.0*cs.x,x*sn.x);if(denominator==0.0){return theta;}
        let sum=trig_coeff(vec2<f32>(theta,0.0),vec2<f32>(rounded_divide(numerator,denominator),0.0));return rounded_add(sum.x,sum.y);
    }
    return theta;
}
// mnee_sample_bsdf_dh's angle for alpha_x != alpha_y (integrator/mnee.h): phi = atanf(alpha_y / alpha_x *
// tanf(2 pi v + pi/2)), plus pi when v > 0.5. The azimuth is in the manifold vertex's frame (dp_du, dp_dv), as pinned:
// the walk does not take the closure's tangent.
fn mn_dh_phi(alpha_x:f32,alpha_y:f32,v:f32)->f32{
    let phi=mn_atan_tan(rounded_divide(alpha_y,alpha_x),rounded_add(6.2831854820251465*v,1.5707963705062866));
    if(v>0.5){return rounded_add(phi,3.1415927410125732);}
    return phi;
}
// Its alpha2 = 1 / (cos_phi^2 / alpha_x^2 + sin_phi^2 / alpha_y^2); sc = (sin_phi, cos_phi).
fn mn_dh_alpha2(alpha_x:f32,alpha_y:f32,sc:vec2<f32>)->f32{
    return rounded_divide(1.0,rounded_add(rounded_divide(sc.y*sc.y,alpha_x*alpha_x),rounded_divide(sc.x*sc.x,alpha_y*alpha_y)));
}
//#endif
// mnee_sample_bsdf_dh for a GGX lobe. An anisotropic lobe (a Glass BSDF or a Principled transmission lobe with a tangent)
// takes the pinned anisotropic branch (MNEE_ANISOTROPIC, set from the shader census with caustics; one fast_sincosf and
// one mapping to the offset for both branches); in a module without that region it reports the unsupported-handler
// error 2 instead of approximating.
fn mn_sample_dh(alpha_x:f32,alpha_y:f32,u:f32,v:f32)->vec2<f32>{
//#if MNEE_ANISOTROPIC else     if(alpha_x!=alpha_y){atomicOr(&heap[p(17u)/4u],2u);return vec2<f32>(0);}let sc=mn_fast_sincos(v*6.2831854820251465);let alpha2=alpha_x*alpha_x;
    var phi=v*6.2831854820251465;if(alpha_x!=alpha_y){phi=mn_dh_phi(alpha_x,alpha_y,v);}
    let sc=mn_fast_sincos(phi);var alpha2=alpha_x*alpha_x;if(alpha_x!=alpha_y){alpha2=mn_dh_alpha2(alpha_x,alpha_y,sc);}
//#endif
    let tan2=alpha2*rounded_divide(u,rounded_add(1.0,-u));
    let cos2=rounded_divide(1.0,rounded_add(1.0,tan2));let sin_theta=rounded_sqrt(max(rounded_add(1.0,-cos2),0.0));
    return vec2<f32>(sc.y*sin_theta,sc.x*sin_theta);
}
fn mn_refractive(kind:u32)->bool{
    return (kind>=MNEE_CLOSURE_REFRACTION_FIRST&&kind<=MNEE_CLOSURE_REFRACTION_LAST)||(kind>=MNEE_CLOSURE_GLASS_FIRST&&kind<=MNEE_CLOSURE_GLASS_LAST);
}
// Raw closure slot ci of the record's closure storage, as the CPU's pointer into
// sd_mnee->closure reads it (no count check).
fn mn_closure(at:u32,ci:u32)->SvmClosure{var r:SvmResult;r.closure_base=at+MR_CLOSURES;r.count=ci+1u;return svm_closure_read(r,ci);}
fn mn_shader_eval(index:u32,sd:SurfaceData,at:u32)->SvmResult{
    return svm_evaluate_context(index,sd,MNEE_CAUSTICS_MAX_CLOSURE,at+MR_CLOSURES,0u,PATH_RAY_VISIBILITY_DIFFUSE,svm_lcg(index),false);
}

// native_triangle_vertices for a static caster triangle (mnee_seed accepts
// triangle casters only; motion is refused with caustics on the host), with the
// three corners fetched in straight-line code: same checks, same corners. With
// the shared fetch (a vec3 index and a corner array written in a loop) inside
// these entries, the Adreno 730 driver (EV031.36.08.33) changed a scene-table
// word that no entry stores to while mnee_setup ran, and caustic scenes stopped
// with the invalid-data error; with this fetch they render.
fn mn_static_vertices(object:u32,prim:u32)->TriangleVertices{
    var result:TriangleVertices;
    if(object>=table_count(G_objects)||object>=table_count(G_object_flag)||prim>=table_count(G_tri_vindex)){fail();result.error=1u;return result;}
    let flags=load_word(table_at(G_object_flag,object,SIZE_object_flag));
//#if !MNEE_MOTION
    if((flags&SD_OBJECT_HAS_VERTEX_MOTION)!=0u){fail();result.error=1u;return result;}
//#endif
//#if MNEE_MOTION
    // With motion blur a deforming caster keeps its centre-frame corners: the pinned vertex fetch tests
    // type & PRIMITIVE_TRIANGLE, which PRIMITIVE_MOTION_TRIANGLE also sets (motion_triangle_vertices_and_normals
    // is never reached), and shader_setup_from_sample and shadow_ray_smooth_surface_offset see PRIMITIVE_TRIANGLE.
//#endif
    let at=table_at(G_tri_vindex,prim,SIZE_tri_vindex);let i0=load_word(at);let i1=load_word(at+4u);let i2=load_word(at+8u);
    result.a=triangle_position(object,i0,0u);result.b=triangle_position(object,i1,0u);result.c=triangle_position(object,i2,0u);
    if(load_word(p(17u))!=0u){result.error=1u;}return result;
}
// terminator_shadow_origin / terminator_smooth_offset (wavefront_terminator.wgsl)
// for the static caster triangle, corners through mn_static_vertices.
fn mn_terminator_smooth_offset(sd:SurfaceData,Ng:vec3<f32>)->vec3<f32>{
    let vertices=mn_static_vertices(sd.object_id,sd.prim);
    let N=terminator_normals(sd);let V=array<vec3<f32>,3>(vertices.a,vertices.b,vertices.c);
    let u=rounded_add(rounded_add(1.0,-sd.u),-sd.v);let v=sd.u;let w=sd.v;
    let P=terminator_mix(V[0],V[1],V[2],u,v,w);var n=terminator_mix(N[0],N[1],N[2],u,v,w);
    if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)==0u){n=surface_object_direction(sd,n,false);}
    let a=rounded_dot(N[2]-N[0],V[0]-V[2]);let b=rounded_dot(N[2]-N[1],V[1]-V[2]);let c=rounded_dot(N[1]-N[0],V[1]-V[0]);
    var h=rounded_add(rounded_add((a*u)*rounded_add(u,-1.0),((rounded_add(rounded_add(a,b),c))*u)*v),(b*v)*rounded_add(v,-1.0));
    if(rounded_dot(n,Ng)>0.0){
        var h0=max(max(rounded_dot(V[1]-V[0],N[0]),rounded_dot(V[2]-V[0],N[0])),0.0);
        var h1=max(max(rounded_dot(V[0]-V[1],N[1]),rounded_dot(V[2]-V[1],N[1])),0.0);
        var h2=max(max(rounded_dot(V[0]-V[2],N[2]),rounded_dot(V[1]-V[2],N[2])),0.0);
        h0=max(rounded_add(rounded_dot(V[0]-P,N[0]),h0),0.0);h1=max(rounded_add(rounded_dot(V[1]-P,N[1]),h1),0.0);h2=max(rounded_add(rounded_dot(V[2]-P,N[2]),h2),0.0);
        h=max(min(min(h0,h1),h2),h*0.5);
    }else{
        var h0=max(max(rounded_dot(V[0]-V[1],N[0]),rounded_dot(V[0]-V[2],N[0])),0.0);
        var h1=max(max(rounded_dot(V[1]-V[0],N[1]),rounded_dot(V[1]-V[2],N[1])),0.0);
        var h2=max(max(rounded_dot(V[2]-V[0],N[2]),rounded_dot(V[2]-V[1],N[2])),0.0);
        h0=max(rounded_add(rounded_dot(P-V[0],N[0]),h0),0.0);h1=max(rounded_add(rounded_dot(P-V[1],N[1]),h1),0.0);h2=max(rounded_add(rounded_dot(P-V[2],N[2]),h2),0.0);
        h=min(-min(min(h0,h1),h2),h*0.5);
    }
    return n*h;
}
fn mn_terminator_shadow_origin(sd:SurfaceData,L:vec3<f32>)->vec4<f32>{
    let cutoff=heap_float(terminator_object(sd)+O_shadow_terminator_geometry_offset);
    var NL=rounded_dot(sd.N,L);let transmit=NL<0.0;if(NL<0.0){NL=-NL;}
    let Ng=select(sd.Ng,-sd.Ng,transmit);let NgL=rounded_dot(Ng,L);
    let amount=select(clamp(rounded_add(1.0,-rounded_divide(NgL,cutoff)),0.0,1.0),
                      clamp(rounded_add(2.0,-rounded_divide(rounded_add(NgL,NL),cutoff)),0.0,1.0),NL<cutoff);
    if(amount>0.0){return vec4<f32>(rounded_add3(sd.P,mn_terminator_smooth_offset(sd,Ng)*amount),select(0.0,1.0,NgL>0.0));}
    return vec4<f32>(sd.P,1.0);
}
// mnee_setup_manifold_vertex for a static triangle hit (casters are refused
// unless PRIMITIVE_TRIANGLE; object motion is refused with caustics on the host).
fn mn_setup_vertex(object:u32,prim:u32,u:f32,v:f32,time:f32,eta:f32,n_offset:vec2<f32>,ci:u32)->MnVertex{
    var vtx:MnVertex;var sd:SurfaceData;
    sd.object_id=object;sd.prim=prim;sd.primitive_type=PRIMITIVE_TRIANGLE;sd.time=time;sd.object_flag=mn_object_flag(object);sd.u=u;sd.v=v;
//#if MOTION_OBJECT
    // shader_setup_object_transforms at the ray time (MNEE with object motion).
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){sd.motion_tfm=motion_object(object,time);sd.motion_itfm=motion_inverse(sd.motion_tfm);}
//#endif
    let shader=load_word(table_at(G_tri_shader,prim,4u));sd.shader=shader;
    let vertices=mn_static_vertices(object,prim);
    var verts=array<vec3<f32>,3>(vertices.a,vertices.b,vertices.c);var normals=terminator_normals(sd);
    var P=rounded_add3(rounded_add3(verts[0],u*mn_sub(verts[1],verts[0])),v*mn_sub(verts[2],verts[0]));
    if((sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)==0u){
        P=surface_object_point(sd,P,false);
        for(var j=0u;j<3u;j++){verts[j]=surface_object_point(sd,verts[j],false);normals[j]=surface_object_normal(sd,normals[j],false);}
    }
    var dp_du=mn_sub(verts[1],verts[0]);var dp_dv=mn_sub(verts[2],verts[0]);
    vtx.ng=mn_normalize(rounded_cross(dp_du,dp_dv));if((sd.object_flag&SD_OBJECT_NEGATIVE_SCALE)!=0u){vtx.ng=-vtx.ng;}
    let w=rounded_add(rounded_add(1.0,-u),-v);
    let interpolated=mn_normalize_len(rounded_add3(rounded_add3(normals[0]*w,normals[1]*u),normals[2]*v));
    vtx.n=interpolated.xyz;let inv_n_len=rounded_divide(1.0,interpolated.w);
    var dn_du=inv_n_len*mn_sub(normals[1],normals[0]);var dn_dv=inv_n_len*mn_sub(normals[2],normals[0]);
    dn_du=mn_sub(dn_du,vtx.n*rounded_dot(vtx.n,dn_du));dn_dv=mn_sub(dn_dv,vtx.n*rounded_dot(vtx.n,dn_dv));
    let inv_len_dp_du=rounded_divide(1.0,mn_len(dp_du));dp_du*=inv_len_dp_du;dn_du*=inv_len_dp_du;
    let dpdu_dot_dpdv=rounded_dot(dp_du,dp_dv);dp_dv=mn_sub(dp_dv,dpdu_dot_dpdv*dp_du);dn_dv=mn_sub(dn_dv,dpdu_dot_dpdv*dn_du);
    let inv_len_dp_dv=rounded_divide(1.0,mn_len(dp_dv));dp_dv*=inv_len_dp_dv;dn_dv*=inv_len_dp_dv;
    let frame=mn_orthonormals(vtx.ng);vtx.dp_du=frame.a;vtx.dp_dv=frame.b;
    let cos_theta=rounded_dot(dp_du,vtx.dp_du);let sin_theta=-rounded_dot(dp_dv,vtx.dp_du);
    vtx.dn_du=mn_sub(cos_theta*dn_du,sin_theta*dn_dv);vtx.dn_dv=rounded_add3(sin_theta*dn_du,cos_theta*dn_dv);
    vtx.p=P;vtx.a=vec4<f32>(0);vtx.c=vec4<f32>(0);vtx.b=vec4<f32>(1,0,0,1);vtx.constraint=vec2<f32>(0);vtx.n_offset=n_offset;
    vtx.eta=eta;vtx.uv=vec2<f32>(u,v);vtx.object=object;vtx.prim=prim;vtx.shader=shader;vtx.ci=ci;
    return vtx;
}

// mnee_compute_constraint_derivatives over the vertices at `base`.
fn mn_constraints(base:u32,count:u32,surface_P:vec3<f32>,fixed:bool,light:vec3<f32>)->bool{
    for(var vi=0u;vi<count;vi++){
        var v=mn_read(base+vi*MN_VERTEX_BYTES);
        var wi:vec3<f32>;if(vi==0u){wi=mn_sub(surface_P,v.p);}else{wi=mn_sub(heap_vec3(base+(vi-1u)*MN_VERTEX_BYTES),v.p);}
        var ili=mn_len(wi);if(ili<MNEE_MIN_DISTANCE){return false;}ili=rounded_divide(1.0,ili);wi*=ili;
        var wo:vec3<f32>;
        if(vi==count-1u){if(fixed){wo=light;}else{wo=mn_sub(light,v.p);}}else{wo=mn_sub(heap_vec3(base+(vi+1u)*MN_VERTEX_BYTES),v.p);}
        var ilo=mn_len(wo);if(ilo<MNEE_MIN_DISTANCE){return false;}ilo=rounded_divide(1.0,ilo);wo*=ilo;
        var eta=v.eta;if(rounded_dot(wi,v.ng)<0.0){eta=rounded_divide(1.0,eta);}
        var H=-rounded_add3(wi,eta*wo);let ilh=rounded_divide(1.0,mn_len(H));H*=ilh;
        ilo*=eta*ilh;ili*=ilh;
        let dp_du_dot_n=rounded_dot(v.dp_du,v.n);var s=mn_sub(v.dp_du,dp_du_dot_n*v.n);
        let inv_len_s=rounded_divide(1.0,mn_len(s));s*=inv_len_s;let t=rounded_cross(v.n,s);
        var dH_du:vec3<f32>;var dH_dv:vec3<f32>;
        if(vi>0u){
            let prev=mn_read(base+(vi-1u)*MN_VERTEX_BYTES);
            dH_du=mn_sub(prev.dp_du,wi*rounded_dot(wi,prev.dp_du))*ili;dH_dv=mn_sub(prev.dp_dv,wi*rounded_dot(wi,prev.dp_dv))*ili;
            dH_du=mn_sub(dH_du,H*rounded_dot(dH_du,H));dH_dv=mn_sub(dH_dv,H*rounded_dot(dH_dv,H));dH_du=-dH_du;dH_dv=-dH_dv;
            v.a=vec4<f32>(rounded_dot(dH_du,s),rounded_dot(dH_dv,s),rounded_dot(dH_du,t),rounded_dot(dH_dv,t));
        }
        if(vi==count-1u&&fixed){
            dH_du=ili*rounded_add3(-v.dp_du,wi*rounded_dot(wi,v.dp_du));dH_dv=ili*rounded_add3(-v.dp_dv,wi*rounded_dot(wi,v.dp_dv));
        }else{
            let k=rounded_add(ili,ilo);
            dH_du=rounded_add3(rounded_add3(-v.dp_du*k,wi*(rounded_dot(wi,v.dp_du)*ili)),wo*(rounded_dot(wo,v.dp_du)*ilo));
            dH_dv=rounded_add3(rounded_add3(-v.dp_dv*k,wi*(rounded_dot(wi,v.dp_dv)*ili)),wo*(rounded_dot(wo,v.dp_dv)*ilo));
        }
        dH_du=mn_sub(dH_du,H*rounded_dot(dH_du,H));dH_dv=mn_sub(dH_dv,H*rounded_dot(dH_dv,H));dH_du=-dH_du;dH_dv=-dH_dv;
        var ds_du=-inv_len_s*rounded_add3(rounded_dot(v.dp_du,v.dn_du)*v.n,dp_du_dot_n*v.dn_du);
        var ds_dv=-inv_len_s*rounded_add3(rounded_dot(v.dp_du,v.dn_dv)*v.n,dp_du_dot_n*v.dn_dv);
        ds_du=mn_sub(ds_du,s*rounded_dot(s,ds_du));ds_dv=mn_sub(ds_dv,s*rounded_dot(s,ds_dv));
        let dt_du=rounded_add3(rounded_cross(v.dn_du,s),rounded_cross(v.n,ds_du));
        let dt_dv=rounded_add3(rounded_cross(v.dn_dv,s),rounded_cross(v.n,ds_dv));
        v.b=vec4<f32>(rounded_add(rounded_dot(dH_du,s),rounded_dot(H,ds_du)),rounded_add(rounded_dot(dH_dv,s),rounded_dot(H,ds_dv)),
                      rounded_add(rounded_dot(dH_du,t),rounded_dot(H,dt_du)),rounded_add(rounded_dot(dH_dv,t),rounded_dot(H,dt_dv)));
        if(vi<count-1u){
            let next=mn_read(base+(vi+1u)*MN_VERTEX_BYTES);
            dH_du=mn_sub(next.dp_du,wo*rounded_dot(wo,next.dp_du))*ilo;dH_dv=mn_sub(next.dp_dv,wo*rounded_dot(wo,next.dp_dv))*ilo;
            dH_du=mn_sub(dH_du,H*rounded_dot(dH_du,H));dH_dv=mn_sub(dH_dv,H*rounded_dot(dH_dv,H));dH_du=-dH_du;dH_dv=-dH_dv;
            v.c=vec4<f32>(rounded_dot(dH_du,s),rounded_dot(dH_dv,s),rounded_dot(dH_du,t),rounded_dot(dH_dv,t));
        }
        v.constraint=vec2<f32>(rounded_add(rounded_dot(s,H),-v.n_offset.x),rounded_add(rounded_dot(t,H),-v.n_offset.y));
        mn_write(base+vi*MN_VERTEX_BYTES,v);
    }
    return true;
}
// mnee_solve_matrix_h_to_x: dx for every vertex into `dx` (record words).
fn mn_solve_h_to_x(base:u32,count:u32,dx:ptr<function,array<vec2<f32>,6>>)->bool{
    var Li:array<vec4<f32>,6>;var C:array<vec2<f32>,6>;
    let v0=mn_read(base);let first=mn_inverse(v0.b);if(first.det==0.0){return false;}Li[0]=first.m;C[0]=v0.constraint;
    for(var k=1u;k<count;k++){
        let vk=mn_read(base+k*MN_VERTEX_BYTES);let previous_c=mn_v4(base+(k-1u)*MN_VERTEX_BYTES+156u);
        let A=mn_m22(vk.a,Li[k-1u]);let Lk=vk.b-mn_m22(A,previous_c);
        let inverse=mn_inverse(Lk);if(inverse.det==0.0){return false;}Li[k]=inverse.m;
        C[k]=vk.constraint-mn_m22v(A,C[k-1u]);
    }
    (*dx)[count-1u]=mn_m22v(Li[count-1u],C[count-1u]);
    for(var k=i32(count)-2;k>-1;k--){let c=mn_v4(base+u32(k)*MN_VERTEX_BYTES+156u);(*dx)[k]=mn_m22v(Li[k],C[k]-mn_m22v(c,(*dx)[k+1]));}
    return true;
}

// The INTERSECT_MNEE entry points are compiled only into the MNEE module (MNEE_ENTRIES, a module-variant bit:
// mnee.cpp builds them with the caustic casters' SVM regions only); other surface modules leave them out.
//#if MNEE_ENTRIES
@compute @workgroup_size(64) fn mnee_receiver(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}let at=mn_record(id.x);if(at==0u){return;}store_word(at+MR_STAGE,0u);
    let index=mn_index(id.x);if(index>=p(10u)){fail();return;}
    if(state_half(S_path_queued_kernel,index)!=MNEE_K_INTERSECT){fail();return;}
    let sd=surface_from_ray(index);if(sd.error!=0u){return;}surface_data_write(at+MR_SD,sd);store_word(at+MR_STAGE,MN_DONE);
//#if MNEE_BACKGROUND
    // background_light_sample for a background lamp mnee_seed may select (mn_background_sample): it takes only the
    // shading point and the light random numbers, so it runs here for every receiver (entry size), into MR_LS.
    if(data_word(D_integrator_use_direct_light)!=0u){
//#if SAMPLING_SOBOL_BURLEY else         let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_LIGHT).xyz;
        let random=native_rng_pattern(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_LIGHT,3u).xyz;
//#endif
//#if BACKGROUND_PORTAL else         let sampled=bgl_sample(random.xy);
        let sampled=bgl_portal_light_sample(sd.P,random.xy);
//#endif
        mn_s4(at+MR_LS,sampled);
    }
//#endif
}
//#endif
//#if MNEE_ENTRIES
//#if LIGHT_SELECT
// The light selection of light_sample_from_position (light tree or light linking; surface.cpp's shade_surface_light_select
// for the receiver: no shader is evaluated here, so its runtime flags hold no transmission). Own entry: the walk inlines to
// more than half a megabyte (compiler pressure), so it stays out of mnee_seed.
@compute @workgroup_size(1) fn mnee_select(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}let at=mn_record(id.x);if(at==0u){return;}if(load_word(at+MR_STAGE)!=MN_DONE){return;}
    if(data_word(D_integrator_use_direct_light)==0u){return;}
    let index=mn_index(id.x);let sd=surface_data_read(at+MR_SD);var r:SvmResult;r.runtime_flag=sd.runtime_flag;
    let result=light_selection_compute(index,sd,r,true,false);if(load_word(p(17u))!=0u){return;}
    light_selection_write(at+MR_SELECT-LIGHT_SELECT_RECORD,result.selection,0.0);
}
//#endif
//#if MNEE_LIGHT_STAGES
// light_sample for the spot and area lights (light/spot.h spot_light_sample, light/area.h area_light_sample) of the
// selection mnee_seed takes (the same light_select), into MR_LS: own entry before mnee_seed (compiler pressure: the
// samplers inlined into mnee_seed pass the size guard). Other lamp types leave MR_LS as it is (the background sample).
@compute @workgroup_size(64) fn mnee_light_sample(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}let at=mn_record(id.x);if(at==0u){return;}if(load_word(at+MR_STAGE)!=MN_DONE){return;}
    if(data_word(D_integrator_use_direct_light)==0u){return;}
    let index=mn_index(id.x);let sd=surface_data_read(at+MR_SD);
//#if LIGHT_SELECT
    light_select_record=at+MR_SELECT-LIGHT_SELECT_RECORD;
//#endif
//#if LIGHT_SELECT else     let selection=light_select(index);if(selection.prim>=0){return;}
    let selection=light_select(index);if(!selection.valid||selection.prim>=0){return;}
//#endif
    let kind=load_word(table_at(G_lights,~bitcast<u32>(selection.prim),SIZE_lights)+L_type);
    var sample:NativeLightSample;
//#if MNEE_SPOT
    if(kind==LIGHT_SPOT){sample=mn_spot_sample(index,sd,selection,mn_point_sample(index,sd,selection));}
//#endif
//#if MNEE_AREA
    if(kind==LIGHT_AREA){sample=mn_area_sample(index,sd,selection);}
//#endif
    if(kind!=LIGHT_SPOT&&kind!=LIGHT_AREA){return;}
    var ls:MnLight;ls.P=sample.P;ls.D=sample.D;ls.t=sample.t;ls.pdf=sample.pdf;ls.eval_fac=sample.eval_fac;
    ls.shader=sample.shader;ls.object=sample.object_id;ls.prim=sample.prim;mn_light_write(at+MR_LS,ls);
}
//#endif
@compute @workgroup_size(64) fn mnee_seed(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}let at=mn_record(id.x);if(at==0u){return;}if(load_word(at+MR_STAGE)!=MN_DONE){return;}
    let index=mn_index(id.x);let sd=surface_data_read(at+MR_SD);
    for(var w=0u;w<16u;w++){store_word(at+MR_META+w*4u,0u);}
    store_word(at+MR_STAGE,MN_DONE);
    if(data_word(D_integrator_use_direct_light)==0u){return;}
    // light_sample_from_position through the shared selection; a selected
    // emissive triangle ends the walk (pinned: ls.type == LIGHT_TRIANGLE).
//#if LIGHT_SELECT
    light_select_record=at+MR_SELECT-LIGHT_SELECT_RECORD;
//#endif
//#if LIGHT_SELECT else     let selection=light_select(index);if(selection.prim>=0){return;}
    // A failed or unlinked selection is no sample.
    let selection=light_select(index);if(!selection.valid||selection.prim>=0){return;}
//#endif
    // A lamp without use_caustics ends the walk before any state change whatever
    // its sample is; the device accepts caustics only from point and sun lamps.
    let lamp=table_at(G_lights,~bitcast<u32>(selection.prim),SIZE_lights);if(load_word(lamp+MNEE_L_use_caustics)==0u){return;}
//#if SHADOW_CATCHER
    // light_sample: a shadow catcher pass path takes no sample of a lamp that excludes the catcher.
    if((state_word(S_path_flag,index)&SC_PATH_PASS)!=0u&&(load_word(lamp+L_shader_id)&SC_EXCLUDE)!=0u){return;}
//#endif
    var sample:NativeLightSample;let kind=load_word(lamp+L_type);
//#if MNEE_LIGHT_STAGES else     if(kind==LIGHT_POINT){sample=mn_point_sample(index,sd,selection);}else if(kind==LIGHT_SUN){sample=mn_sun_sample(index,selection);}
    if(kind==LIGHT_POINT){sample=mn_point_sample(index,sd,selection);}else if(kind==LIGHT_SUN){sample=mn_sun_sample(index,selection);}
    // Spot and area lights: the sample mnee_light_sample took for this record (MR_LS).
//#if MNEE_SPOT
    else if(kind==LIGHT_SPOT){sample=mn_staged_sample(at);}
//#endif
//#if MNEE_AREA
    else if(kind==LIGHT_AREA){sample=mn_staged_sample(at);}
//#endif
//#endif
//#if MNEE_BACKGROUND
    else if(kind==LIGHT_BACKGROUND){sample=mn_background_sample(index,selection,at);}
//#endif
    else{atomicOr(&heap[p(17u)/4u],2u);return;}
    if(load_word(p(17u))!=0u){return;}
    if(!(sample.pdf>0.0)){return;}
    let light=table_at(G_lights,sample.prim,SIZE_lights);
    var ls:MnLight;ls.P=sample.P;ls.D=sample.D;ls.t=sample.t;ls.pdf=sample.pdf;ls.eval_fac=sample.eval_fac;
//#if LIGHT_SELECT else     ls.pdf_selection=data_float(D_integrator_distribution_pdf_lights);ls.shader=sample.shader;ls.object=sample.object_id;ls.prim=sample.prim;
    ls.pdf_selection=select(data_float(D_integrator_distribution_pdf_lights),selection.pdf,selection.tree);ls.shader=sample.shader;ls.object=sample.object_id;ls.prim=sample.prim;
//#endif
    ls.kind=load_word(light+L_type);ls.group=load_word(table_at(G_objects,ls.object,SIZE_objects)+O_lightgroup);
    if(rounded_dot(ls.D,sd.N)<0.0){return;}
    if(load_word(light+MNEE_L_use_caustics)==0u){return;}
    mn_light_write(at+MR_LS,ls);
    // Probe ray from the receiver toward the light sample.
    var D=ls.D;var tmax=ls.t;
    if(ls.t!=MNEE_FLT_MAX){let ray=mn_normalize_len(mn_sub(ls.P,sd.P));D=ray.xyz;tmax=ray.w;}
    mn_s3(at+MR_META+MM_D,D);
    var tmin=0.0;var self_object=sd.object_id;var self_prim=sd.prim;var count=0u;
    for(var isect_count=0u;isect_count<MNEE_MAX_INTERSECTION_COUNT;isect_count++){
        let hit=mn_trace(sd.P,D,tmin,tmax,sd.time,self_object,self_prim,ls.object,ls.prim);if(load_word(p(17u))!=0u){return;}
        if(hit.prim==NONE){break;}
        if((mn_object_flag(hit.object)&SD_OBJECT_CAUSTICS_CASTER)!=0u){
            if(count>=MNEE_MAX_CAUSTIC_CASTERS){return;}
            if((hit.kind&PRIMITIVE_TRIANGLE)==0u){return;}
            if((load_word(table_at(G_tri_shader,hit.prim,4u))&SHADER_SMOOTH_NORMAL)==0u){return;}
            let h=at+MR_HITS+count*32u;
            mn_sf(h,hit.t);mn_sf(h+4u,hit.u);mn_sf(h+8u,hit.v);store_word(h+12u,hit.prim);store_word(h+16u,hit.object);store_word(h+20u,hit.kind);
            count++;
        }
        self_object=hit.object;self_prim=hit.prim;tmin=mn_t_offset(hit.t);
    }
    store_word(at+MR_META+MM_COUNT,count);
    if(count==0u){state_u8(S_path_mnee,index,state_byte(S_path_mnee,index)&~MNEE_PATH_VALID);return;}
    store_word(at+MR_STAGE,MN_EVAL);
}
//#endif
//#if MNEE_ENTRIES
@compute @workgroup_size(64) fn mnee_eval(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}let at=mn_record(id.x);if(at==0u){return;}if(load_word(at+MR_STAGE)!=MN_EVAL){return;}
    store_word(at+MR_STAGE,MN_DONE);
    let index=mn_index(id.x);let sd=surface_data_read(at+MR_SD);let count=load_word(at+MR_META+MM_COUNT);
    let D=heap_vec3(at+MR_META+MM_D);
    for(var k=0u;k<count;k++){
        let h=at+MR_HITS+k*32u;
        let hit=surface_setup_hit(load_word(h+20u),load_word(h+16u),load_word(h+12u),mn_f(h+4u),mn_f(h+8u),mn_f(h),sd.time,D,sd.dP,0.0);
        if(hit.error!=0u){return;}
        let r=mn_shader_eval(index,hit,at);if(r.error!=0u){return;}
        var found=false;
        for(var ci=0u;ci<r.count;ci++){
            let c=svm_closure_read(r,ci);
            if(mn_refractive(c.kind)){
                found=true;
                var eta=c.ior;if((r.runtime_flag&SR_BACKFACING)!=0u){eta=rounded_divide(1.0,c.ior);}
                var n_offset=vec2<f32>(0);
                if(c.alpha_x>0.0&&c.alpha_y>0.0){
//#if SAMPLING_SOBOL_BURLEY else                     let random=native_rng(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_SURFACE_BSDF).xy;
                    let random=native_rng_pattern(state_word(S_path_sample,index),state_word(S_path_rng_pixel,index),state_half(S_path_rng_offset,index)+PRNG_SURFACE_BSDF,2u).xy;
//#endif
//#if MNEE_BECKMANN
                    if(c.kind==MNEE_CLOSURE_REFRACTION_FIRST){n_offset=mn_beckmann_dh(c.alpha_x,c.alpha_y,random.x,random.y);}else{
//#endif
                    n_offset=mn_sample_dh(c.alpha_x,c.alpha_y,random.x,random.y);if(load_word(p(17u))!=0u){return;}
//#if MNEE_BECKMANN
                    }if(load_word(p(17u))!=0u){return;}
//#endif
                }
                store_word(h+24u,ci);mn_sf(h+28u,eta);mn_s2(at+MR_T+k*MN_VERTEX_BYTES+108u,n_offset);
                break;
            }
        }
        if(!found){return;}
    }
    // Mark the walk invalid, then valid once the depth limits allow it.
    state_u8(S_path_mnee,index,state_byte(S_path_mnee,index)&~MNEE_PATH_VALID);
    if(state_half(S_path_transmission_bounce,index)+count-1u>=data_word(D_integrator_max_transmission_bounce)){return;}
    if(state_half(S_path_diffuse_bounce,index)+1u>=data_word(D_integrator_max_diffuse_bounce)){return;}
    if(state_half(S_path_bounce,index)+count>=data_word(D_integrator_max_bounce)){return;}
    state_u8(S_path_mnee,index,state_byte(S_path_mnee,index)|MNEE_PATH_VALID);
    let ls=mn_light_read(at+MR_LS);
    var fixed=ls.t==MNEE_FLT_MAX;
    if(ls.kind==LIGHT_AREA&&heap_float(table_at(G_lights,ls.prim,SIZE_lights)+L_area_tan_half_spread)==0.0){fixed=true;}
    store_word(at+MR_META+MM_FIXED,select(0u,1u,fixed));
    store_word(at+MR_STAGE,MN_SETUP);
}
//#endif
//#if MNEE_ENTRIES
@compute @workgroup_size(64) fn mnee_setup(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}let at=mn_record(id.x);if(at==0u){return;}if(load_word(at+MR_STAGE)!=MN_SETUP){return;}
    let time=surface_data_read(at+MR_SD).time;let count=load_word(at+MR_META+MM_COUNT);
    for(var k=0u;k<count;k++){let h=at+MR_HITS+k*32u;
        let n_offset=mn_v2(at+MR_T+k*MN_VERTEX_BYTES+108u);
        mn_write(at+MR_V+k*MN_VERTEX_BYTES,mn_setup_vertex(load_word(h+16u),load_word(h+12u),mn_f(h+4u),mn_f(h+8u),time,mn_f(h+28u),n_offset,load_word(h+24u)));}
    if(load_word(p(17u))!=0u){return;}
    store_word(at+MR_STAGE,MN_SOLVE);
}
//#endif
//#if MNEE_ENTRIES
//#if !MNEE_WALK_STAGES
@compute @workgroup_size(64) fn mnee_solve(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}let at=mn_record(id.x);if(at==0u){return;}if(load_word(at+MR_STAGE)!=MN_SOLVE){return;}
    store_word(at+MR_STAGE,MN_DONE);
    let sd=surface_data_read(at+MR_SD);let count=load_word(at+MR_META+MM_COUNT);let ls=mn_light_read(at+MR_LS);
    let fixed=load_word(at+MR_META+MM_FIXED)!=0u;
    let light_sample=select(ls.P,ls.D,fixed);
    var dx:array<vec2<f32>,6>;var beta=0.1;var reduce_stepsize=false;var resolve_constraint=true;
    // projection_ray.tmin is set once (pinned): later misses on other objects keep advancing it.
    var tmin=0.0;var solved=0u;var iterations=0u;
    for(var iteration=0u;iteration<MNEE_MAX_ITERATIONS;iteration++){
        iterations=iteration+1u;
        if(resolve_constraint){
            if(!mn_constraints(at+MR_V,count,sd.P,fixed,light_sample)){solved=2u;break;}
            var norm=0.0;for(var vi=0u;vi<count;vi++){norm=max(norm,mn_len2(mn_v2(at+MR_V+vi*MN_VERTEX_BYTES+116u)));}
            // Diagnostic: the first 48 constraint norms reuse the consumed hit records.
            if(iteration<48u){mn_sf(at+MR_HITS+iteration*4u,norm);}
            if(norm<MNEE_SOLVER_THRESHOLD){solved=1u;break;}
            if(!mn_solve_h_to_x(at+MR_V,count,&dx)){solved=3u;break;}
        }
        var stuck=false;
        for(var vi=0u;vi<count;vi++){
            let mv=mn_read(at+MR_V+vi*MN_VERTEX_BYTES);
            let step=rounded_add3(dx[vi].x*mv.dp_du,dx[vi].y*mv.dp_dv);
            var tentative_p=mn_sub(mv.p,beta*step);
            if(vi==0u){if(rounded_dot(sd.Ng,mn_sub(tentative_p,sd.P))<=0.0){tentative_p=rounded_add3(mv.p,beta*step);}}
            var origin:vec3<f32>;var self_object:u32;var self_prim:u32;
            if(vi==0u){self_object=sd.object_id;self_prim=sd.prim;origin=sd.P;}
            else{let pv=mn_read(at+MR_V+(vi-1u)*MN_VERTEX_BYTES);self_object=pv.object;self_prim=pv.prim;origin=pv.p;}
            let ray=mn_normalize_len(mn_sub(tentative_p,origin));let tmax=ray.w*MNEE_PROJECTION_DISTANCE_MULTIPLIER;
            var success=false;var hit:NativeHit;
            for(var isect_count=0u;isect_count<MNEE_MAX_INTERSECTION_COUNT;isect_count++){
                hit=mn_trace(origin,ray.xyz,tmin,tmax,sd.time,self_object,self_prim,NONE,NONE);if(load_word(p(17u))!=0u){return;}
                if(hit.prim==NONE){break;}
                if(hit.object==mv.object){success=true;break;}
                self_object=hit.object;self_prim=hit.prim;tmin=mn_t_offset(hit.t);
            }
            if(!success){reduce_stepsize=true;break;}
            let tv=mn_setup_vertex(hit.object,hit.prim,hit.u,hit.v,sd.time,mv.eta,mv.n_offset,mv.ci);
            mn_write(at+MR_T+vi*MN_VERTEX_BYTES,tv);
            if(mn_len(mn_sub(tv.p,mv.p))<MNEE_MIN_PROGRESS_DISTANCE){stuck=true;break;}
        }
        if(stuck){solved=4u;break;}
        if(!reduce_stepsize){
            for(var vi=0u;vi<count;vi++){
                let tv=mn_read(at+MR_T+vi*MN_VERTEX_BYTES);
                var previous=sd.P;if(vi>0u){previous=heap_vec3(at+MR_T+(vi-1u)*MN_VERTEX_BYTES);}
                let wi=mn_sub(previous,tv.p);
                var wo:vec3<f32>;
                if(vi==count-1u){if(fixed){wo=ls.D;}else{wo=mn_sub(ls.P,tv.p);}}else{wo=mn_sub(heap_vec3(at+MR_T+(vi+1u)*MN_VERTEX_BYTES),tv.p);}
                if(rounded_dot(tv.n,wi)*rounded_dot(tv.n,wo)>=0.0){reduce_stepsize=true;break;}
            }
        }
        if(reduce_stepsize){
            reduce_stepsize=false;resolve_constraint=false;beta*=0.5;
            if(beta<MNEE_MINIMUM_STEP_SIZE){solved=5u;break;}
            continue;
        }
        for(var vi=0u;vi<count;vi++){mn_write(at+MR_V+vi*MN_VERTEX_BYTES,mn_read(at+MR_T+vi*MN_VERTEX_BYTES));}
        resolve_constraint=true;beta=min(1.0,2.0*beta);
    }
    store_word(at+MR_META+MM_ITERATIONS,iterations);store_word(at+MR_META+MM_SOLVER,select(solved,6u,solved==0u));
    if(solved==1u){store_word(at+MR_STAGE,MN_TRANSFER);}
}
//#endif
//#if MNEE_WALK_STAGES
// MNEE_WALK_STAGES: the walk of mnee_newton_solver in two entries (compiler pressure: with motion and curve or point leaves
// the walk's traversal and mnee_setup_manifold_vertex inlined into one entry passed the compile envelope). mnee_solve takes
// the constraints and the Newton step of an iteration, then the checks after its projection; mnee_project projects every
// vertex onto its caster. The host dispatches mnee_solve, then mnee_project and mnee_solve in turn once per iteration;
// beta, tmin, the iteration, the flags and dx persist in the record (MR_WALK, the closure slots past
// MNEE_CAUSTICS_MAX_CLOSURE, which no MNEE shader evaluation reaches). The operations and their order are the one-entry
// walk's, so the walk's outcome, iterations and vertices are bit-identical.
const MN_PROJECT:u32=7u;
const MN_WALK:u32=8u;
const MR_WALK:u32=MR_CLOSURES+MNEE_CAUSTICS_MAX_CLOSURE*SVM_CLOSURE_BYTES;
const MW_ITERATION:u32=0u;
const MW_BETA:u32=4u;
const MW_FLAGS:u32=8u;
const MW_TMIN:u32=12u;
const MW_DX:u32=16u;
const MW_RESOLVE:u32=1u;
const MW_REDUCE:u32=2u;
const MW_STUCK:u32=4u;
@compute @workgroup_size(64) fn mnee_solve(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}let at=mn_record(id.x);if(at==0u){return;}let stage=load_word(at+MR_STAGE);if(stage!=MN_SOLVE&&stage!=MN_WALK){return;}
    store_word(at+MR_STAGE,MN_DONE);
    let sd=surface_data_read(at+MR_SD);let count=load_word(at+MR_META+MM_COUNT);let ls=mn_light_read(at+MR_LS);
    let fixed=load_word(at+MR_META+MM_FIXED)!=0u;
    let light_sample=select(ls.P,ls.D,fixed);let w=at+MR_WALK;
    var beta=0.1;var resolve_constraint=true;var iteration=0u;var solved=0u;
    // projection_ray.tmin is set once (pinned): later misses on other objects keep advancing it (mnee_project).
    if(stage==MN_SOLVE){mn_sf(w+MW_TMIN,0.0);}
    else{
        // The rest of iteration `iteration` after its projection.
        iteration=load_word(w+MW_ITERATION);beta=mn_f(w+MW_BETA);let flags=load_word(w+MW_FLAGS);
        var reduce_stepsize=(flags&MW_REDUCE)!=0u;
        if((flags&MW_STUCK)!=0u){solved=4u;}
        else{
            if(!reduce_stepsize){
                for(var vi=0u;vi<count;vi++){
                    let tv=mn_read(at+MR_T+vi*MN_VERTEX_BYTES);
                    var previous=sd.P;if(vi>0u){previous=heap_vec3(at+MR_T+(vi-1u)*MN_VERTEX_BYTES);}
                    let wi=mn_sub(previous,tv.p);
                    var wo:vec3<f32>;
                    if(vi==count-1u){if(fixed){wo=ls.D;}else{wo=mn_sub(ls.P,tv.p);}}else{wo=mn_sub(heap_vec3(at+MR_T+(vi+1u)*MN_VERTEX_BYTES),tv.p);}
                    if(rounded_dot(tv.n,wi)*rounded_dot(tv.n,wo)>=0.0){reduce_stepsize=true;break;}
                }
            }
            if(reduce_stepsize){
                resolve_constraint=false;beta*=0.5;
                if(beta<MNEE_MINIMUM_STEP_SIZE){solved=5u;}
            }else{
                for(var vi=0u;vi<count;vi++){mn_write(at+MR_V+vi*MN_VERTEX_BYTES,mn_read(at+MR_T+vi*MN_VERTEX_BYTES));}
                resolve_constraint=true;beta=min(1.0,2.0*beta);
            }
        }
        if(solved==0u){iteration++;}
    }
    // Iteration `iteration` up to its projection.
    if(solved==0u&&iteration<MNEE_MAX_ITERATIONS){
        if(resolve_constraint){
            if(!mn_constraints(at+MR_V,count,sd.P,fixed,light_sample)){solved=2u;}
            else{
                var norm=0.0;for(var vi=0u;vi<count;vi++){norm=max(norm,mn_len2(mn_v2(at+MR_V+vi*MN_VERTEX_BYTES+116u)));}
                // Diagnostic: the first 48 constraint norms reuse the consumed hit records.
                if(iteration<48u){mn_sf(at+MR_HITS+iteration*4u,norm);}
                if(norm<MNEE_SOLVER_THRESHOLD){solved=1u;}
                else{
                    var dx:array<vec2<f32>,6>;
                    if(!mn_solve_h_to_x(at+MR_V,count,&dx)){solved=3u;}
                    else{for(var vi=0u;vi<count;vi++){mn_s2(w+MW_DX+vi*8u,dx[vi]);}}
                }
            }
        }
        if(solved==0u){
            store_word(w+MW_ITERATION,iteration);mn_sf(w+MW_BETA,beta);store_word(w+MW_FLAGS,select(0u,MW_RESOLVE,resolve_constraint));
            store_word(at+MR_STAGE,MN_PROJECT);return;
        }
    }
    let iterations=select(iteration,iteration+1u,solved!=0u);
    store_word(at+MR_META+MM_ITERATIONS,iterations);store_word(at+MR_META+MM_SOLVER,select(solved,6u,solved==0u));
    if(solved==1u){store_word(at+MR_STAGE,MN_TRANSFER);}
}
// The projection of iteration MW_ITERATION: every vertex's tentative position traced onto its caster.
@compute @workgroup_size(64) fn mnee_project(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}let at=mn_record(id.x);if(at==0u){return;}if(load_word(at+MR_STAGE)!=MN_PROJECT){return;}
    store_word(at+MR_STAGE,MN_DONE);
    let sd=surface_data_read(at+MR_SD);let count=load_word(at+MR_META+MM_COUNT);let w=at+MR_WALK;
    let beta=mn_f(w+MW_BETA);var tmin=mn_f(w+MW_TMIN);var reduce_stepsize=false;var stuck=false;
    for(var vi=0u;vi<count;vi++){
        let mv=mn_read(at+MR_V+vi*MN_VERTEX_BYTES);let dx=mn_v2(w+MW_DX+vi*8u);
        let step=rounded_add3(dx.x*mv.dp_du,dx.y*mv.dp_dv);
        var tentative_p=mn_sub(mv.p,beta*step);
        if(vi==0u){if(rounded_dot(sd.Ng,mn_sub(tentative_p,sd.P))<=0.0){tentative_p=rounded_add3(mv.p,beta*step);}}
        var origin:vec3<f32>;var self_object:u32;var self_prim:u32;
        if(vi==0u){self_object=sd.object_id;self_prim=sd.prim;origin=sd.P;}
        else{let pv=mn_read(at+MR_V+(vi-1u)*MN_VERTEX_BYTES);self_object=pv.object;self_prim=pv.prim;origin=pv.p;}
        let ray=mn_normalize_len(mn_sub(tentative_p,origin));let tmax=ray.w*MNEE_PROJECTION_DISTANCE_MULTIPLIER;
        var success=false;var hit:NativeHit;
        for(var isect_count=0u;isect_count<MNEE_MAX_INTERSECTION_COUNT;isect_count++){
            hit=mn_trace(origin,ray.xyz,tmin,tmax,sd.time,self_object,self_prim,NONE,NONE);if(load_word(p(17u))!=0u){return;}
            if(hit.prim==NONE){break;}
            if(hit.object==mv.object){success=true;break;}
            self_object=hit.object;self_prim=hit.prim;tmin=mn_t_offset(hit.t);
        }
        if(!success){reduce_stepsize=true;break;}
        let tv=mn_setup_vertex(hit.object,hit.prim,hit.u,hit.v,sd.time,mv.eta,mv.n_offset,mv.ci);
        mn_write(at+MR_T+vi*MN_VERTEX_BYTES,tv);
        if(mn_len(mn_sub(tv.p,mv.p))<MNEE_MIN_PROGRESS_DISTANCE){stuck=true;break;}
    }
    mn_sf(w+MW_TMIN,tmin);
    store_word(w+MW_FLAGS,(load_word(w+MW_FLAGS)&MW_RESOLVE)|select(0u,MW_REDUCE,reduce_stepsize)|select(0u,MW_STUCK,stuck));
    store_word(at+MR_STAGE,MN_WALK);
}
//#endif
//#endif
//#if MNEE_ENTRIES
//#if MNEE_LIGHT_STAGES
// light_sample_update at the walk's last vertex (mnee_path_contribution) for mnee_transfer: own entry after mnee_solve
// (compiler pressure: the area light's update inlined into mnee_transfer).
@compute @workgroup_size(64) fn mnee_light_update(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}let at=mn_record(id.x);if(at==0u){return;}if(load_word(at+MR_STAGE)!=MN_TRANSFER){return;}
    let index=mn_index(id.x);let count=load_word(at+MR_META+MM_COUNT);var ls=mn_light_read(at+MR_LS);
    let last=mn_read(at+MR_V+(count-1u)*MN_VERTEX_BYTES);
    mn_light_update(&ls,last.p,last.n,state_word(S_path_flag,index));mn_light_write(at+MR_LS,ls);
}
//#endif
//#endif
// light_sample_update for the lamp types the device accepts with caustics (point, spot, area; sun and background
// lights keep their sample).
fn mn_light_update(ls:ptr<function,MnLight>,P:vec3<f32>,N:vec3<f32>,path_flag:u32){
    let light=table_at(G_lights,(*ls).prim,SIZE_lights);
    if((*ls).kind==LIGHT_POINT){
        let ray=mn_safe_normalize_len(mn_sub((*ls).P,P));(*ls).D=ray.xyz;(*ls).t=ray.w;
        let radius=heap_float(light+L_radius);
        if(load_word(light+L_is_sphere)!=0u){
            let co=heap_vec3(light+L_co);let delta=mn_sub(P,co);let d_sq=rounded_dot(delta,delta);let r_sq=radius*radius;let t_sq=(*ls).t*(*ls).t;
            let jacobian=rounded_divide(0.5*abs(rounded_add(rounded_add(d_sq,-r_sq),-t_sq)),(radius*(*ls).t)*t_sq);
            var pdf:f32;
            if(d_sq>r_sq){
                let ratio=rounded_divide(r_sq,d_sq);
                let one_minus=select(0.5*ratio,rounded_add(1.0,-rounded_sqrt(max(rounded_add(1.0,-ratio),0.0))),ratio>0.0004);
                pdf=rounded_divide(0.15915493667125702,one_minus);
            }else if((path_flag&PATH_RAY_MIS_HAD_TRANSMISSION)!=0u){pdf=0.15915493667125702*0.5;}
            else{pdf=max(rounded_dot(N,(*ls).D),0.0)*0.31830987334251404;}
            (*ls).pdf=pdf*jacobian;(*ls).Ng=mn_normalize(mn_sub((*ls).P,co));
        }else{(*ls).pdf=((*ls).eval_fac*4.0)*3.1415927410125732;(*ls).Ng=-(*ls).D;}
    }
//#if MNEE_SPOT
    if((*ls).kind==LIGHT_SPOT){mn_spot_update(ls,light,P,N,path_flag);}
//#endif
//#if MNEE_AREA
    if((*ls).kind==LIGHT_AREA){mn_area_update(ls,light,P);}
//#endif
    (*ls).pdf*=(*ls).pdf_selection;
}
// mnee_compute_transfer_matrix: x = dx1/dxlight, y = |dh/dx|, z = 1 on success.
fn mn_transfer(at:u32,sd:SurfaceData,ls:MnLight,fixed:bool,count:u32)->vec3<f32>{
    var U:array<vec4<f32>,6>;let base=at+MR_V;
    let v0=mn_read(base);var Li=mn_inverse(v0.b);if(Li.det==0.0){return vec3<f32>(0);}
    var det_dh_dx=Li.det;
    for(var k=1u;k<count;k++){
        let previous_c=mn_v4(base+(k-1u)*MN_VERTEX_BYTES+156u);U[k-1u]=mn_m22(Li.m,previous_c);
        let vk=mn_read(base+k*MN_VERTEX_BYTES);let Lk=vk.b-mn_m22(vk.a,U[k-1u]);Li=mn_inverse(Lk);if(Li.det==0.0){return vec3<f32>(0);}
        det_dh_dx*=Li.det;
    }
    let mi=count-1u;let m=mn_read(base+mi*MN_VERTEX_BYTES);
    let s=mn_normalize(mn_sub(m.dp_du,rounded_dot(m.dp_du,m.n)*m.n));let t=rounded_cross(m.n,s);
    let frame=mn_orthonormals(ls.Ng);let dp_du=frame.a;let dp_dv=frame.b;
    var wi:vec3<f32>;if(count==1u){wi=mn_sub(sd.P,m.p);}else{wi=mn_sub(heap_vec3(base+(mi-1u)*MN_VERTEX_BYTES),m.p);}
    let ili=rounded_divide(1.0,mn_len(wi));wi*=ili;
    var eta=m.eta;if(rounded_dot(wi,m.ng)<0.0){eta=rounded_divide(1.0,eta);}
    var dxn_dwn:f32;var dc:vec4<f32>;
    if(fixed){
        let wo=ls.D;var H=-rounded_add3(wi,eta*wo);let ilh=rounded_divide(1.0,mn_len(H));H*=ilh;let ilo=-eta*ilh;
        let cos_theta=rounded_dot(wo,m.n);let sin_theta=rounded_sqrt(max(rounded_add(1.0,-cos_theta*cos_theta),0.0));
        let cos_phi=rounded_dot(wo,s);let sin_phi=rounded_sqrt(max(rounded_add(1.0,-cos_phi*cos_phi),0.0));
        var dH_dtheta=ilo*mn_sub(cos_theta*rounded_add3(cos_phi*s,sin_phi*t),sin_theta*m.n);
        var dH_dphi=(ilo*sin_theta)*rounded_add3(-sin_phi*s,cos_phi*t);
        dH_dtheta=mn_sub(dH_dtheta,H*rounded_dot(dH_dtheta,H));dH_dphi=mn_sub(dH_dphi,H*rounded_dot(dH_dphi,H));
        dc=vec4<f32>(rounded_dot(dH_dtheta,s),rounded_dot(dH_dphi,s),rounded_dot(dH_dtheta,t),rounded_dot(dH_dphi,t));
        dxn_dwn=rounded_divide(1.0,max(MNEE_MIN_DISTANCE,abs(sin_theta)));
    }else{
        var wo=mn_sub(ls.P,m.p);var ilo=rounded_divide(1.0,mn_len(wo));wo*=ilo;
        var H=-rounded_add3(wi,eta*wo);let ilh=rounded_divide(1.0,mn_len(H));H*=ilh;ilo*=eta*ilh;
        var dH_du=mn_sub(dp_du,wo*rounded_dot(wo,dp_du))*ilo;var dH_dv=mn_sub(dp_dv,wo*rounded_dot(wo,dp_dv))*ilo;
        dH_du=mn_sub(dH_du,H*rounded_dot(dH_du,H));dH_dv=mn_sub(dH_dv,H*rounded_dot(dH_dv,H));dH_du=-dH_du;dH_dv=-dH_dv;
        dc=vec4<f32>(rounded_dot(dH_du,s),rounded_dot(dH_dv,s),rounded_dot(dH_du,t),rounded_dot(dH_dv,t));
        dxn_dwn=1.0;
    }
    var Tp=-mn_m22(Li.m,dc);
    for(var k=i32(count)-2;k>-1;k--){Tp=-mn_m22(U[k],Tp);}
    return vec3<f32>(abs(mn_det(Tp))*dxn_dwn,abs(det_dh_dx),1.0);
}
// shader_setup_from_sample(P, Ng=n, I, shader, object, prim, u, v, t, time,
// object_space=false, is_lamp=false) for a static caster triangle.
fn mn_setup_from_sample(P:vec3<f32>,Ng:vec3<f32>,I:vec3<f32>,shader:u32,object:u32,prim:u32,u:f32,v:f32,t:f32,time:f32)->SurfaceData{
    var sd:SurfaceData;sd.P=P;sd.N=Ng;sd.Ng=Ng;sd.wi=I;sd.shader=shader;sd.primitive_type=PRIMITIVE_TRIANGLE;sd.object_id=object;sd.prim=prim;
    sd.u=u;sd.v=v;sd.time=time;sd.ray_length=t;sd.runtime_flag=0u;
    let shader_id=shader&SHADER_MASK;if(shader_id>=table_count(G_shaders)){surface_failure(&sd);return sd;}
    sd.shader_flag=load_word(table_at(G_shaders,shader_id,SIZE_shaders)+SH_flags);sd.object_flag=mn_object_flag(object);
//#if MOTION_OBJECT
    // shader_setup_object_transforms at the sample time (MNEE with object motion).
    if((sd.object_flag&SD_OBJECT_MOTION)!=0u){sd.motion_tfm=motion_object(object,time);sd.motion_itfm=motion_inverse(sd.motion_tfm);}
//#endif
    let applied=(sd.object_flag&SD_OBJECT_TRANSFORM_APPLIED)!=0u;
    if((shader&SHADER_SMOOTH_NORMAL)!=0u){
        let normals=terminator_normals(sd);let w=rounded_add(rounded_add(1.0,-u),-v);
        let interpolated=surface_normalize(rounded_add3(rounded_add3(w*normals[0],u*normals[1]),v*normals[2]));
        sd.N=select(Ng,interpolated,any(interpolated!=vec3<f32>(0)));
        if(!applied){sd.N=surface_object_normal(sd,sd.N,false);}
    }
    let vertices=mn_static_vertices(object,prim);
    sd.dPdu=mn_sub(vertices.b,vertices.a);sd.dPdv=mn_sub(vertices.c,vertices.a);
    if(!applied){sd.dPdu=surface_object_direction(sd,sd.dPdu,false);sd.dPdv=surface_object_direction(sd,sd.dPdv,false);}
    if(rounded_dot(sd.Ng,sd.wi)<0.0){sd.runtime_flag|=SR_BACKFACING;sd.Ng=-sd.Ng;sd.N=-sd.N;sd.dPdu=-sd.dPdu;sd.dPdv=-sd.dPdv;}
    sd.dP=0.0;sd.dI=0.0;sd.du=vec2<f32>(0);sd.dv=vec2<f32>(0);
    return sd;
}
// mnee_eval_bsdf_contribution.
fn mn_bsdf_contribution(c:SvmClosure,wi:vec3<f32>,wo:vec3<f32>)->vec3<f32>{
    let cosNI=rounded_dot(c.N,wi);let cosNO=rounded_dot(c.N,wo);
    let Ht=mn_normalize(-rounded_add3(c.ior*wo,wi));let cosHI=rounded_dot(Ht,wi);
    let alpha2=c.alpha_x*c.alpha_y;let cosThetaM=rounded_dot(c.N,Ht);
//#if MNEE_BECKMANN else     let li=0.5*rounded_add(rounded_sqrt(rounded_add(1.0,alpha2*max(rounded_add(rounded_divide(1.0,cosNI*cosNI),-1.0),0.0))),-1.0);
//#endif
//#if MNEE_BECKMANN else     let lo=0.5*rounded_add(rounded_sqrt(rounded_add(1.0,alpha2*max(rounded_add(rounded_divide(1.0,cosNO*cosNO),-1.0),0.0))),-1.0);
//#endif
//#if MNEE_BECKMANN else     let G=rounded_divide(1.0,rounded_add(rounded_add(1.0,li),lo));
    // bsdf_G<GGX> or, for a Beckmann refraction closure (MNEE_CLOSURE_REFRACTION_FIRST), bsdf_G<BECKMANN>: 1 / ((1 + lambda(NI))
    // + lambda(NO)), one lambda body for both cosines; and the Fresnel of a refraction lobe for the Beckmann closure.
    let beckmann=c.kind==MNEE_CLOSURE_REFRACTION_FIRST;var G=1.0;
    for(var k=0u;k<2u;k++){G=rounded_add(G,mn_lambda(beckmann,alpha2,select(cosNO,cosNI,k==0u)));}
    G=rounded_divide(1.0,G);var lobes=c;if(beckmann){lobes.kind=MNEE_CLOSURE_REFRACTION_LAST;}
//#endif
//#if MNEE_BECKMANN else     let f=ggx_fresnel(c,cosHI);
    let f=ggx_fresnel(lobes,cosHI);
//#endif
    return ((c.weight*f.transmission)*G)*abs(rounded_divide(cosHI,cosNI*(cosThetaM*cosThetaM)));
}
// mnee_path_contribution up to the solution shaders: the light update, the
// bounce counters, the transfer matrix and the visibility of every vertex.
// An empty result leaves the bounce counters as the pinned kernel leaves them.
//#if MNEE_ENTRIES
@compute @workgroup_size(64) fn mnee_transfer(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}let at=mn_record(id.x);if(at==0u){return;}if(load_word(at+MR_STAGE)!=MN_TRANSFER){return;}
    store_word(at+MR_STAGE,MN_DONE);store_word(at+MR_META+MM_RESULT,1u);
    let index=mn_index(id.x);let sd=surface_data_read(at+MR_SD);
    let count=load_word(at+MR_META+MM_COUNT);let fixed=load_word(at+MR_META+MM_FIXED)!=0u;var ls=mn_light_read(at+MR_LS);
    let wo_ray=mn_normalize_len(mn_sub(heap_vec3(at+MR_V),sd.P));let wo=wo_ray.xyz;let wo_len=wo_ray.w;
    var throughput=vec3<f32>(1);
    let last=mn_read(at+MR_V+(count-1u)*MN_VERTEX_BYTES);
//#if MNEE_LIGHT_STAGES else     mn_light_update(&ls,last.p,last.n,state_word(S_path_flag,index));mn_light_write(at+MR_LS,ls);
    // mnee_light_update updated the light sample (light_sample_update).
//#endif
    let transmission_bounce=state_half(S_path_transmission_bounce,index);let diffuse_bounce=state_half(S_path_diffuse_bounce,index);
    let bounce=state_half(S_path_bounce,index);
    store_word(at+MR_META+MM_BOUNCES,transmission_bounce|(diffuse_bounce<<16u));store_word(at+MR_META+MM_BOUNCES+4u,bounce);
    state_u16(S_path_diffuse_bounce,index,diffuse_bounce+1u);
    state_u16(S_path_transmission_bounce,index,transmission_bounce+count-1u);state_u16(S_path_bounce,index,bounce+count);
    throughput*=rounded_divide(ls.eval_fac,ls.pdf);
    let transfer=mn_transfer(at,sd,ls,fixed,count);if(transfer.z==0.0){store_word(at+MR_META+MM_RESULT,2u);return;}
    let dw0_dx1=rounded_divide(abs(rounded_dot(wo,heap_vec3(at+MR_V+36u))),wo_len*wo_len);
    throughput*=min(dw0_dx1*transfer.x,2.0);
    var P=sd.P;var self_object=sd.object_id;var self_prim=sd.prim;
    for(var vi=0u;vi<count;vi++){
        let v=mn_read(at+MR_V+vi*MN_VERTEX_BYTES);
        let probe=mn_normalize_len(mn_sub(v.p,P));
        let hit=mn_trace(P,probe.xyz,0.0,probe.w,sd.time,self_object,self_prim,ls.object,ls.prim);if(load_word(p(17u))!=0u){return;}
        if(hit.prim!=NONE&&(hit.object!=v.object||abs(rounded_add(probe.w,-hit.t))>MNEE_MIN_DISTANCE)){
            // The solution shaders of the vertices before vi were evaluated with these counters.
            if(vi>0u){state_u16(S_path_transmission_bounce,index,transmission_bounce+vi-1u);state_u16(S_path_bounce,index,bounce+vi);}
            store_word(at+MR_META+MM_RESULT,3u);return;}
        self_object=v.object;self_prim=v.prim;P=v.p;
    }
    mn_s3(at+MR_OUT,throughput);mn_s3(at+MR_OUT+12u,wo);
    store_word(at+MR_STAGE,MN_CONTRIBUTE);
}
//#endif
//#if MNEE_ENTRIES
@compute @workgroup_size(64) fn mnee_contribute(@builtin(global_invocation_id) id:vec3<u32>){
    svm_invocation(id.x);
    if(id.x>=p(1u)){return;}let at=mn_record(id.x);if(at==0u){return;}
    let stage=load_word(at+MR_STAGE);if(stage!=MN_CONTRIBUTE&&stage!=MN_DONE){return;}
    let index=mn_index(id.x);let sd=surface_data_read(at+MR_SD);
    if(stage==MN_CONTRIBUTE){mn_contribution(at,index,id.x,sd);if(load_word(p(17u))!=0u){return;}}
    store_word(at+MR_STAGE,0u);
    mn_queue(index,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,sd.shader&SHADER_MASK);
}
//#endif
fn mn_contribution(at:u32,index:u32,slot:u32,sd:SurfaceData){
    let count=load_word(at+MR_META+MM_COUNT);let fixed=load_word(at+MR_META+MM_FIXED)!=0u;let ls=mn_light_read(at+MR_LS);
    var throughput=heap_vec3(at+MR_OUT);let receiver_wo=heap_vec3(at+MR_OUT+12u);var wo=receiver_wo;
    var wo_len=mn_len(mn_sub(heap_vec3(at+MR_V),sd.P));
    let bounces=load_word(at+MR_META+MM_BOUNCES);let transmission_bounce=bounces&65535u;let diffuse_bounce=bounces>>16u;let bounce=load_word(at+MR_META+MM_BOUNCES+4u);
    var emission:SurfaceData;var emission_N=vec3<f32>(0);
    for(var vi=0u;vi<count;vi++){
        let v=mn_read(at+MR_V+vi*MN_VERTEX_BYTES);
        let wi=-wo;let wi_len=wo_len;
        emission=mn_setup_from_sample(v.p,v.n,wi,v.shader,v.object,v.prim,v.uv.x,v.uv.y,wi_len,sd.time);if(emission.error!=0u){return;}
        state_u16(S_path_transmission_bounce,index,transmission_bounce+vi);state_u16(S_path_bounce,index,bounce+1u+vi);
        let r=mn_shader_eval(index,emission,at);if(r.error!=0u){return;}emission_N=r.N;
        var next:vec3<f32>;
        if(vi==count-1u){if(fixed){next=ls.D;}else{next=mn_sub(ls.P,v.p);}}else{next=mn_sub(heap_vec3(at+MR_V+(vi+1u)*MN_VERTEX_BYTES),v.p);}
        let ray=mn_normalize_len(next);wo=ray.xyz;wo_len=ray.w;
        throughput*=mn_bsdf_contribution(mn_closure(at,v.ci),wi,wo);
    }
    state_u16(S_path_transmission_bounce,index,transmission_bounce);state_u16(S_path_diffuse_bounce,index,diffuse_bounce);state_u16(S_path_bounce,index,bounce);
    store_word(at+MR_META+MM_RESULT,4u);mn_s3(at+MR_OUT,throughput);
    // light_sample_to_surface_shadow_ray from the last solution vertex.
    emission.N=emission_N;var start=vec4<f32>(emission.P,1.0);
    if(terminator_geometry_active(emission)){start=mn_terminator_shadow_origin(emission,ls.D);}
    // integrator_shadow_path_init(MNEE_PENDING) + integrator_state_write_mnee, in this
    // path's reserved slot (mnee.cpp: base + dispatch slot, independent of atomic order).
    let shadow=load_word(p(28u))+slot;if(shadow>=p(25u)){fail();return;}
    let counter=load_word(p(7u)+S_queue_counter)/4u;atomicAdd(&heap[counter+MNEE_K_PENDING],1u);
    state_u16(S_shadow_path_queued_kernel,shadow,MNEE_K_PENDING);
    state_u32(S_path_mnee_shadow_state,index,shadow);state_u32(MNEE_S_I2_prim,shadow,index);
    state_f32(MNEE_S_I0_t,shadow,ls.P.x);state_f32(MNEE_S_I0_u,shadow,ls.P.y);state_f32(MNEE_S_I0_v,shadow,ls.P.z);
    state_f32(MNEE_S_I1_t,shadow,receiver_wo.x);state_f32(MNEE_S_I1_u,shadow,receiver_wo.y);state_f32(MNEE_S_I1_v,shadow,receiver_wo.z);
    state_f32(S_shadow_ray_tmin,shadow,ls.t);state_f32(S_shadow_ray_tmax,shadow,ls.pdf);state_f32(MNEE_S_I2_t,shadow,ls.eval_fac);
    state_u32(S_shadow_ray_self_light_object,shadow,ls.object);state_u32(S_shadow_ray_self_light_prim,shadow,ls.prim);
    state_u32(MNEE_S_I0_object,shadow,ls.shader);state_u32(MNEE_S_I0_prim,shadow,ls.group+1u);state_u32(MNEE_S_I0_type,shadow,ls.kind);
    state_u32(MNEE_S_I1_prim,shadow,count);state_vec3(S_shadow_path_throughput,shadow,throughput);
    state_vec3(S_shadow_ray_D,shadow,ls.D);state_vec3(S_shadow_ray_P,shadow,start.xyz);state_f32(S_shadow_ray_dP,shadow,0.0);state_f32(S_shadow_ray_dD,shadow,0.0);
    state_u32(MNEE_S_I1_object,shadow,select(NONE,emission.object_id,start.w!=0.0));state_u32(MNEE_S_I1_type,shadow,select(NONE,emission.prim,start.w!=0.0));
    state_u8(S_path_mnee,index,state_byte(S_path_mnee,index)|MNEE_PATH_SAMPLED);
    store_word(at+MR_META+MM_RESULT,5u);
}
// integrate_surface_direct_light for a path with PATH_MNEE_SAMPLED: the light
// sample, throughput and ray come from the MNEE shadow slot, which becomes the
// shadow path (no MIS weight, no light-threshold roulette, no dD widening).
fn mnee_direct_light(index:u32,sd:SurfaceData,r:SvmResult){
    let shadow=state_word(S_path_mnee_shadow_state,index);if(shadow>=p(25u)){fail();return;}
    let lsP=vec3<f32>(heap_float(state_address(MNEE_S_I0_t,shadow,4u)),heap_float(state_address(MNEE_S_I0_u,shadow,4u)),heap_float(state_address(MNEE_S_I0_v,shadow,4u)));
    let lsD=vec3<f32>(heap_float(state_address(MNEE_S_I1_t,shadow,4u)),heap_float(state_address(MNEE_S_I1_u,shadow,4u)),heap_float(state_address(MNEE_S_I1_v,shadow,4u)));
    let ls_t=heap_float(state_address(S_shadow_ray_tmin,shadow,4u));
    let ls_object=state_word(S_shadow_ray_self_light_object,shadow);let ls_prim=state_word(S_shadow_ray_self_light_prim,shadow);
    let ls_shader=state_word(MNEE_S_I0_object,shadow);let group_plus=state_word(MNEE_S_I0_prim,shadow);let count=state_word(MNEE_S_I1_prim,shadow);
    let shader=table_at(G_shaders,ls_shader&SHADER_MASK,SIZE_shaders);
//#if LIGHT_NEE else var emission=heap_vec3(shader+SH_constant_emission);
    let constant=(load_word(shader+SH_flags)&SD_HAS_CONSTANT_EMISSION)!=0u;
    var emission=select(vec3<f32>(1),heap_vec3(shader+SH_constant_emission),constant);
//#endif
    emission*=heap_vec3(table_at(G_lights,ls_prim,SIZE_lights)+L_strength);
    var bsdf=light_bsdf(sd,r,lsD,ls_shader);
    let factor=emission*heap_vec3(state_address(S_shadow_path_throughput,shadow,12u));bsdf.eval*=factor;bsdf.diffuse*=factor;bsdf.glossy*=factor;
    if(all(bsdf.eval==vec3<f32>(0))){return;}
    var origin=heap_vec3(state_address(S_shadow_ray_P,shadow,12u));var direction=heap_vec3(state_address(S_shadow_ray_D,shadow,12u));var tmax=ls_t;
    if(ls_t!=MNEE_FLT_MAX){let ray=mn_safe_normalize_len(mn_sub(lsP,origin));direction=ray.xyz;tmax=ray.w;}
    let self_object=state_word(MNEE_S_I1_object,shadow);let self_prim=state_word(MNEE_S_I1_type,shadow);
    if(self_object!=NONE){origin=surf_ray_offset_at(sd,origin,direction);}
//#if LIGHT_NEE else let next=K_SHADOW_INTERSECT;
    let next=select(K_LIGHT_NEE,K_SHADOW_INTERSECT,constant);
//#endif
    let counter=load_word(p(7u)+S_queue_counter)/4u;atomicSub(&heap[counter+MNEE_K_PENDING],1u);atomicAdd(&heap[counter+next],1u);
    state_u16(S_shadow_path_queued_kernel,shadow,next);
    let dP=heap_float(state_address(S_shadow_ray_dP,shadow,4u));let dD=heap_float(state_address(S_shadow_ray_dD,shadow,4u));
    state_vec3(S_shadow_ray_P,shadow,origin);state_vec3(S_shadow_ray_D,shadow,direction);
    state_f32(S_shadow_ray_tmin,shadow,select(MNEE_FLT_MAX,0.0,(ls_shader&SHADER_CAST_SHADOW)!=0u));state_f32(S_shadow_ray_tmax,shadow,tmax);
    state_f32(S_shadow_ray_time,shadow,heap_float(state_address(S_ray_time,index,4u)));state_f32(S_shadow_ray_dP,shadow,dP);state_f32(S_shadow_ray_dD,shadow,dD);
    state_u32(S_shadow_self_object,shadow,self_object);state_u32(S_shadow_self_prim,shadow,self_prim);
    state_u32(S_shadow_ray_self_light_object,shadow,ls_object);state_u32(S_shadow_ray_self_light_prim,shadow,ls_prim);
//#if LIGHT_TREE else     state_f32(S_shadow_path_bsdf_eval_average,shadow,(bsdf.eval.x+bsdf.eval.y+bsdf.eval.z)*(1.0/3.0));
    // Not allocated with KERNEL_FEATURE_LIGHT_TREE (shadow_state_template.h).
    if((data_word(D_kernel_features)&LT_FEATURE_LIGHT_TREE)==0u){state_f32(S_shadow_path_bsdf_eval_average,shadow,(bsdf.eval.x+bsdf.eval.y+bsdf.eval.z)*(1.0/3.0));}
//#endif
    state_u32(S_shadow_path_render_pixel_index,shadow,state_word(S_path_render_pixel_index,index));
    state_u32(S_shadow_path_sample,shadow,state_word(S_path_sample,index));state_u32(S_shadow_path_rng_pixel,shadow,state_word(S_path_rng_pixel,index));
    state_u16(S_shadow_path_rng_offset,shadow,state_half(S_path_rng_offset,index));
    state_u16(S_shadow_path_transparent_bounce,shadow,state_half(S_path_transparent_bounce,index));
    state_u16(S_shadow_path_volume_bounds_bounce,shadow,state_half(S_path_volume_bounds_bounce,index));
    state_u16(S_shadow_path_glossy_bounce,shadow,state_half(S_path_glossy_bounce,index));
//#if MNEE_VOLUME
    // integrate_direct_light_shadow_init_common: the main path's volume stack goes to the MNEE shadow path.
    vl_copy_volume_stack(index,shadow);
//#endif
    state_vec3(S_shadow_path_throughput,shadow,heap_vec3(state_address(S_path_throughput,index,12u))*bsdf.eval);
    state_u16(S_shadow_path_transmission_bounce,shadow,state_half(S_path_transmission_bounce,index)+count-1u);
    state_u16(S_shadow_path_diffuse_bounce,shadow,state_half(S_path_diffuse_bounce,index)+1u);
    state_u16(S_shadow_path_bounce,shadow,state_half(S_path_bounce,index)+count);
    state_u8(S_shadow_path_lightgroup,shadow,group_plus);
    var flag=state_word(S_path_flag,index);
    if(p(20u)!=0u){var diffuse=svm_divide(bsdf.diffuse,bsdf.eval);var glossy=svm_divide(bsdf.glossy,bsdf.eval);
        if((flag&PATH_RAY_ANY_PASS)!=0u){diffuse=heap_vec3(state_address(S_path_pass_diffuse_weight,index,12u));glossy=heap_vec3(state_address(S_path_pass_glossy_weight,index,12u));}
        else{flag|=PATH_RAY_SURFACE_PASS;}
        state_vec3(S_shadow_path_pass_diffuse_weight,shadow,diffuse);state_vec3(S_shadow_path_pass_glossy_weight,shadow,glossy);
    }
    state_u8(S_shadow_path_visibility,shadow,state_byte(S_path_visibility,index));state_u32(S_shadow_path_flag,shadow,flag);
}
// End of integrator_shade_surface: clear PATH_MNEE_SAMPLED and terminate a
// pending MNEE slot the direct-light step did not reuse.
@compute @workgroup_size(64) fn mnee_cleanup(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)||(p(28u)!=0u&&load_word(p(17u))!=0u)){return;}
    let index=mn_index(id.x);if(index>=p(10u)){fail();return;}
    let flags=state_byte(S_path_mnee,index);if((flags&MNEE_PATH_SAMPLED)==0u){return;}
    state_u8(S_path_mnee,index,flags&~MNEE_PATH_SAMPLED);
    let shadow=state_word(S_path_mnee_shadow_state,index);if(shadow>=p(25u)){fail();return;}
    if(state_half(S_shadow_path_queued_kernel,shadow)==MNEE_K_PENDING){
        let counter=load_word(p(7u)+S_queue_counter)/4u;atomicSub(&heap[counter+MNEE_K_PENDING],1u);state_u16(S_shadow_path_queued_kernel,shadow,0u);}
}
// sample_uniform_cone (sample/mapping.h).
struct MnCone {D:vec3<f32>,cos_theta:f32,pdf:f32}
fn mn_cone(N:vec3<f32>,one_minus_cos:f32,random:vec2<f32>)->MnCone{
    var out=MnCone(N,1.0,1.0);if(!(one_minus_cos>0.0)){return out;}
    var xy=ggx_disk_sample(random);let r2=rounded_add(xy.x*xy.x,xy.y*xy.y);
    out.cos_theta=rounded_add(1.0,-(r2*one_minus_cos));
    xy*=rounded_sqrt(max(one_minus_cos*rounded_add(2.0,-(one_minus_cos*r2)),0.0));
    out.pdf=rounded_divide(0.15915493667125702,one_minus_cos);
    let frame=mn_orthonormals(N);out.D=rounded_add3(rounded_add3(xy.x*frame.a,xy.y*frame.b),out.cos_theta*N);return out;
}
// sun_light_sample (light/sun.h) through the same cone.
fn mn_sun_sample(index:u32,selection:LightSelection)->NativeLightSample{
    var ls:NativeLightSample;ls.is_light=true;ls.prim=~bitcast<u32>(selection.prim);let light=table_at(G_lights,ls.prim,SIZE_lights);
    if(f32(state_half(S_path_bounce,index))>heap_float(light+L_max_bounces)){return ls;}
    ls.shader=load_word(light+L_shader_id);ls.object_id=load_word(light+L_object_id);
    let cone=mn_cone(heap_vec3(light+L_co),heap_float(light+L_sun_angle),selection.random.xy);
    ls.P=cone.D;ls.D=-cone.D;ls.pdf=cone.pdf;ls.t=MNEE_FLT_MAX;ls.eval_fac=heap_float(light+L_sun_eval);
//#if LIGHT_SELECT else     ls.pdf*=data_float(D_integrator_distribution_pdf_lights);return ls;
    ls.pdf*=select(data_float(D_integrator_distribution_pdf_lights),selection.pdf,selection.tree);return ls;
//#endif
}
// point_light_sample (light/point.h) with the oracle's float3 operation order:
// lightN /= d multiplies by 1/d, the cone and the law of cosines use rounded
// sums. Used by the MNEE seed (the shared lamp sampler keeps its own order).
fn mn_point_sample(index:u32,sd:SurfaceData,selection:LightSelection)->NativeLightSample{
    var ls:NativeLightSample;let random=selection.random;ls.is_light=true;ls.prim=~bitcast<u32>(selection.prim);
    let light=table_at(G_lights,ls.prim,SIZE_lights);
    if(f32(state_half(S_path_bounce,index))>heap_float(light+L_max_bounces)){return ls;}
    ls.shader=load_word(light+L_shader_id);ls.object_id=load_word(light+L_object_id);
    let co=heap_vec3(light+L_co);let radius=heap_float(light+L_radius);let r_sq=radius*radius;
    var lightN=mn_sub(sd.P,co);let d_sq=rounded_dot(lightN,lightN);let d=rounded_sqrt(d_sq);lightN=lightN*rounded_divide(1.0,d);
    ls.eval_fac=heap_float(light+L_point_eval);
    if(load_word(light+L_is_sphere)!=0u){
        var cos_theta:f32;
        if(d_sq>r_sq){
            let s_sq=rounded_divide(r_sq,d_sq);
            let one_minus_cos=select(0.5*s_sq,rounded_add(1.0,-rounded_sqrt(max(rounded_add(1.0,-s_sq),0.0))),s_sq>0.0004);
            let cone=mn_cone(-lightN,one_minus_cos,random.xy);ls.D=cone.D;ls.pdf=cone.pdf;cos_theta=cone.cos_theta;
        }else{
            let sampled=surface_cos_hemisphere(sd.N,random.xy);ls.D=sampled.xyz;ls.pdf=sampled.w;cos_theta=-rounded_dot(ls.D,lightN);
        }
        let root=rounded_sqrt(max(rounded_add(rounded_add(r_sq,-d_sq),d_sq*(cos_theta*cos_theta)),0.0));
        ls.t=rounded_add(d*cos_theta,-select(-root,root,rounded_add(d_sq,-r_sq)>=0.0));
        ls.P=rounded_add3(sd.P,ls.D*ls.t);let Ng=mn_normalize(mn_sub(ls.P,co));ls.P=rounded_add3(Ng*radius,co);
    }else{
        ls.P=co;
        if(r_sq>0.0){let frame=mn_orthonormals(lightN);let disk=ggx_disk_sample(random.xy);ls.P=rounded_add3(ls.P,rounded_add3(frame.a*disk.x,frame.b*disk.y)*radius);}
        let ray=mn_safe_normalize_len(mn_sub(ls.P,sd.P));ls.D=ray.xyz;ls.t=ray.w;
        let invarea=select(1.0,rounded_divide(1.0,r_sq*3.1415927410125732),r_sq>0.0);
        // light_pdf_area_to_solid_angle(lightN, -D, t): cos = dot(Ng, I); t*t / cos when cos > 0.
        let cosine=rounded_dot(lightN,-ls.D);var pdf=0.0;if(cosine>0.0){pdf=rounded_divide(ls.t*ls.t,cosine);}
        ls.pdf=invarea*pdf;
    }
//#if LIGHT_SELECT else     ls.pdf*=data_float(D_integrator_distribution_pdf_lights);return ls;
    ls.pdf*=select(data_float(D_integrator_distribution_pdf_lights),selection.pdf,selection.tree);return ls;
//#endif
}
//#if MNEE_SPOT
// Spot lights with caustics (light/spot.h, util/math_intersect.h), every sum, quotient and square root correctly
// rounded in the oracle's float3 order, as mn_point_sample.
// spot_light_to_local: safe_normalize(transform_direction(itfm, ray)) with z negated; the host's transform_direction
// order (lamp_direction, wavefront_emitters.wgsl).
fn mn_spot_local(light:u32,ray:vec3<f32>)->vec3<f32>{
    let a=table_at(G_objects,load_word(light+L_object_id),SIZE_objects)+O_itfm;let x=heap_vec4(a);let y=heap_vec4(a+16u);let z=heap_vec4(a+32u);
    let c0=vec3<f32>(x.x,y.x,z.x)*ray.x;let c1=vec3<f32>(x.y,y.y,z.y)*ray.y;let c2=vec3<f32>(x.z,y.z,z.z)*ray.z;
    var d=rounded_add3(rounded_add3(c0,c1),c2);if(CAMERA_NATIVE_VECTOR_DIVIDE!=0u){d=rounded_add3(c0,rounded_add3(c1,c2));}
    // safe_normalize: the reciprocal of the length (util/math_float3.h; native/DEVICE.md, item 151).
    var local=d;let t=mn_len(d);if(t!=0.0){local=d*rounded_divide(1.0,t);}local.z=-local.z;return local;
}
// spot_light_attenuation: smoothstepf((ray.z - cos_half_spot_angle) * spot_smooth).
fn mn_spot_attenuation(light:u32,local:vec3<f32>)->f32{
    let f=rounded_add(local.z,-heap_float(light+L_spot_cos))*heap_float(light+L_spot_smooth);
    if(f<=0.0){return 0.0;}if(f>=1.0){return 1.0;}let ff=f*f;return rounded_add(3.0*ff,-((2.0*ff)*f));
}
// ray_sphere_intersect(P, D, 0, FLT_MAX, centre, radius): xyz the hit, w its distance (negative: no hit).
fn mn_sphere_hit(P:vec3<f32>,D:vec3<f32>,centre:vec3<f32>,radius:f32)->vec4<f32>{
    let none=vec4<f32>(0.0,0.0,0.0,-1.0);
    let d_vec=mn_sub(centre,P);let r_sq=radius*radius;let d_sq=rounded_dot(d_vec,d_vec);let d_cos=rounded_dot(d_vec,D);
    if(d_sq>r_sq&&d_cos<0.0){return none;}
    let perpendicular=mn_sub(d_vec,d_cos*D);let s_sq=rounded_dot(perpendicular,perpendicular);if(s_sq>r_sq){return none;}
    let root=rounded_sqrt(rounded_add(r_sq,-s_sq));let t=rounded_add(d_cos,-select(-root,root,rounded_add(d_sq,-r_sq)>=0.0));
    if(t>0.0&&t<MNEE_FLT_MAX){return vec4<f32>(rounded_add3(P,D*t),t);}
    return none;
}
// spot_light_sample (not in a volume segment) from the point light's sample of the same record (mn_point_sample: the same
// fields, the visible cone, the hemisphere inside the sphere, the disk, the law of cosines and the pdf times the selection
// pdf): the spread cone where it is the smaller one, then the spot attenuation. A sample the pinned code rejects has pdf 0.
fn mn_spot_sample(index:u32,sd:SurfaceData,selection:LightSelection,point:NativeLightSample)->NativeLightSample{
    var ls=point;let light=table_at(G_lights,ls.prim,SIZE_lights);
    if(f32(state_half(S_path_bounce,index))>heap_float(light+L_max_bounces)){return ls;}
    let co=heap_vec3(light+L_co);let radius=heap_float(light+L_radius);let r_sq=radius*radius;
    let delta=mn_sub(sd.P,co);let d_sq=rounded_dot(delta,delta);
    let sphere=load_word(light+L_is_sphere)!=0u;let outside=d_sq>r_sq;
    if(sphere&&outside){
        let one_minus_spread=rounded_add(1.0,-heap_float(light+L_spot_spread_cos));
        let s_sq=rounded_divide(r_sq,d_sq);
        let one_minus_cos=select(0.5*s_sq,rounded_add(1.0,-rounded_sqrt(max(rounded_add(1.0,-s_sq),0.0))),s_sq>0.0004);
        if(!(one_minus_cos<one_minus_spread)){
            // Sample the spread cone, then the sphere it reaches (ray_sphere_intersect), remapped onto the sphere.
            let cone=mn_cone(-heap_vec3(light+L_spot_dir),one_minus_spread,selection.random.xy);ls.D=cone.D;
//#if LIGHT_SELECT else             ls.pdf=cone.pdf*data_float(D_integrator_distribution_pdf_lights);
            ls.pdf=cone.pdf*select(data_float(D_integrator_distribution_pdf_lights),selection.pdf,selection.tree);
//#endif
            let hit=mn_sphere_hit(sd.P,ls.D,co,radius);if(hit.w<0.0){ls.pdf=0.0;return ls;}
            ls.t=hit.w;let Ng=mn_normalize(mn_sub(hit.xyz,co));ls.P=rounded_add3(Ng*radius,co);
        }
    }
    if(!sphere||outside){ls.eval_fac*=mn_spot_attenuation(light,mn_spot_local(light,-ls.D));}
    if(ls.eval_fac==0.0){ls.pdf=0.0;}
    return ls;
}
// spot_light_mnee_sample_update (the selection pdf is re-applied by mn_light_update).
fn mn_spot_update(ls:ptr<function,MnLight>,light:u32,P:vec3<f32>,N:vec3<f32>,path_flag:u32){
    let ray=mn_safe_normalize_len(mn_sub((*ls).P,P));(*ls).D=ray.xyz;(*ls).t=ray.w;
    (*ls).eval_fac=heap_float(light+L_point_eval);
    let radius=heap_float(light+L_radius);var use_attenuation=true;
    if(load_word(light+L_is_sphere)!=0u){
        let co=heap_vec3(light+L_co);let delta=mn_sub(P,co);let d_sq=rounded_dot(delta,delta);let r_sq=radius*radius;let t_sq=(*ls).t*(*ls).t;
        let jacobian=rounded_divide(0.5*abs(rounded_add(rounded_add(d_sq,-r_sq),-t_sq)),(radius*(*ls).t)*t_sq);
        var pdf:f32;
        if(d_sq>r_sq){
            let s_sq=rounded_divide(r_sq,d_sq);
            let one_minus_cos=select(0.5*s_sq,rounded_add(1.0,-rounded_sqrt(max(rounded_add(1.0,-s_sq),0.0))),s_sq>0.0004);
            pdf=rounded_divide(0.15915493667125702,min(one_minus_cos,rounded_add(1.0,-heap_float(light+L_spot_spread_cos))));
        }else if((path_flag&PATH_RAY_MIS_HAD_TRANSMISSION)!=0u){pdf=0.15915493667125702*0.5;}
        else{pdf=max(rounded_dot(N,(*ls).D),0.0)*0.31830987334251404;}
        (*ls).pdf=pdf*jacobian;(*ls).Ng=mn_normalize(mn_sub((*ls).P,co));use_attenuation=d_sq>r_sq;
    }else{(*ls).pdf=((*ls).eval_fac*4.0)*3.1415927410125732;(*ls).Ng=-(*ls).D;}
    let local=mn_spot_local(light,-(*ls).D);
    if(use_attenuation){(*ls).eval_fac*=mn_spot_attenuation(light,local);}
}
//#endif
//#if MNEE_BECKMANN
// Beckmann refraction casters (integrator/mnee.h mnee_sample_bsdf_dh and mnee_eval_bsdf_contribution for
// CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID; Beckmann glass takes the GGX forms there, as pinned).
// logf of a positive normal x, correctly rounded: e ln2 + 2 atanh((m-1)/(m+1)) in double-float (family_log_parts,
// wavefront_microfacet.wgsl), rounded once.
fn mn_logf(x:f32)->f32{
    var bits=bitcast<u32>(x);var e=f32(i32(bits>>23u)-127);var m=bitcast<f32>((bits&0x007fffffu)|0x3f800000u);
    if(m>1.4142135){m*=0.5;e+=1.0;}
    let num=m-1.0;let den=trig_two_sum(m,1.0);let q=rounded_divide(num,den.x);let qd=trig_product(q,den.x);
    let sl=rounded_divide(rounded_add(rounded_add(rounded_add(num,-qd.x),-qd.y),-(q*den.y)),den.x);let sv=vec2<f32>(q,sl);
    let z=trig_mul(sv,sv);
    var coefficients=array<vec2<f32>,10>(vec2<f32>(0.05263157933950424,-3.9213582381236733e-10),vec2<f32>(0.05882352963089943,-2.1913472425527658e-10),
        vec2<f32>(0.06666667014360428,-3.47693762670076e-09),vec2<f32>(0.07692307978868484,-2.8656079731348427e-09),vec2<f32>(0.09090909361839294,-2.709302115988521e-09),
        vec2<f32>(0.1111111119389534,-8.278422947149977e-10),vec2<f32>(0.1428571492433548,-6.38621200366174e-09),vec2<f32>(0.20000000298023224,-2.9802322831784522e-09),
        vec2<f32>(0.3333333432674408,-9.934107758624577e-09),vec2<f32>(1.0,0.0));
    var polynomial=vec2<f32>(0.0476190485060215,-8.869738832295582e-10);
    for(var i=0u;i<10u;i++){polynomial=trig_coeff(coefficients[i],trig_mul(polynomial,z));}
    let lm=trig_mul(vec2<f32>(2.0*sv.x,2.0*sv.y),polynomial);
    let le=trig_product(e,0.6931471824645996);let tail=rounded_add(le.y,e*-1.9046542121259336e-09);
    let sum=trig_two_sum(le.x,lm.x);let l=vec2<f32>(sum.x,rounded_add(rounded_add(sum.y,lm.y),tail));return rounded_add(l.x,l.y);
}
// mnee_sample_bsdf_dh for a Beckmann refraction lobe: tan2_theta = alpha2 * -logf(1 - u). The Refraction BSDF node
// makes isotropic lobes only (svm/closure.h sets alpha_y = alpha_x), so the anisotropic branch is mirrored but not
// reached from a shader graph.
fn mn_beckmann_dh(alpha_x:f32,alpha_y:f32,u:f32,v:f32)->vec2<f32>{
//#if MNEE_ANISOTROPIC else     if(alpha_x!=alpha_y){atomicOr(&heap[p(17u)/4u],2u);return vec2<f32>(0);}let sc=mn_fast_sincos(v*6.2831854820251465);let alpha2=alpha_x*alpha_x;
    var phi=v*6.2831854820251465;if(alpha_x!=alpha_y){phi=mn_dh_phi(alpha_x,alpha_y,v);}
    let sc=mn_fast_sincos(phi);var alpha2=alpha_x*alpha_x;if(alpha_x!=alpha_y){alpha2=mn_dh_alpha2(alpha_x,alpha_y,sc);}
//#endif
    let tan2=alpha2*(-mn_logf(rounded_add(1.0,-u)));
    let cos2=rounded_divide(1.0,rounded_add(1.0,tan2));let sin_theta=rounded_sqrt(max(rounded_add(1.0,-cos2),0.0));
    return vec2<f32>(sc.y*sin_theta,sc.x*sin_theta);
}
// bsdf_lambda<GGX> or bsdf_lambda<BECKMANN>(alpha2, cos_N) (closure/bsdf_microfacet.h): for Beckmann the rational
// approximation of bsdf_lambda_from_sqr_alpha_tan_n, inversesqrtf as 1 / sqrtf.
fn mn_lambda(beckmann:bool,alpha2:f32,cosine:f32)->f32{
    let x=alpha2*max(rounded_add(rounded_divide(1.0,cosine*cosine),-1.0),0.0);
    if(!beckmann){return 0.5*rounded_add(rounded_sqrt(rounded_add(1.0,x)),-1.0);}
    if(x<0.39){return 0.0;}
    let a=rounded_divide(1.0,rounded_sqrt(x));
    return rounded_divide(rounded_add(rounded_add(0.396*a,-1.259)*a,1.0),rounded_add(2.181*a,3.535)*a);
}
//#endif
//#if MNEE_BACKGROUND
// Background lights with caustics: light_sample's LIGHT_BACKGROUND branch (light/light.h, background_light_sample of
// light/background.h through bgl_sample, wavefront_background_light.wgsl; taken by mnee_receiver into MR_LS): D = -sample,
// P = Ng = sample direction reversed, t = FLT_MAX (a fixed-direction walk), eval_fac 1.
fn mn_background_sample(index:u32,selection:LightSelection,at:u32)->NativeLightSample{
    var ls:NativeLightSample;ls.is_light=true;ls.prim=~bitcast<u32>(selection.prim);
    let light=table_at(G_lights,ls.prim,SIZE_lights);
    if(f32(state_half(S_path_bounce,index))>heap_float(light+L_max_bounces)){return ls;}
    ls.shader=load_word(light+L_shader_id);ls.object_id=load_word(light+L_object_id);
    let sampled=mn_v4(at+MR_LS);
    ls.D=sampled.xyz;ls.P=-sampled.xyz;ls.pdf=sampled.w;ls.t=MNEE_FLT_MAX;ls.eval_fac=1.0;
    if(load_word(p(17u))!=0u){ls.pdf=0.0;return ls;}
//#if LIGHT_SELECT else     ls.pdf*=data_float(D_integrator_distribution_pdf_lights);return ls;
    ls.pdf*=select(data_float(D_integrator_distribution_pdf_lights),selection.pdf,selection.tree);return ls;
//#endif
}
//#endif
//#endif
//#if MNEE_LIGHT_STAGES
// The spot or area light sample mnee_light_sample wrote into MR_LS.
fn mn_staged_sample(at:u32)->NativeLightSample{
    let ls=mn_light_read(at+MR_LS);var s:NativeLightSample;s.P=ls.P;s.D=ls.D;s.t=ls.t;s.pdf=ls.pdf;s.eval_fac=ls.eval_fac;
    s.shader=ls.shader;s.object_id=ls.object;s.prim=ls.prim;s.is_light=true;return s;
}
//#endif
//#if MNEE_AREA
// Area lights with caustics (light/area.h): area_light_spread_clamp_light from the region's current centre
// (area_light_eval starts from *light_P, the light's centre when sampling, the sampled point when
// light_sample_update calls it), then area_light_eval and area_light_sample with the rectangle sampler
// area_rectangle (wavefront_emitters.wgsl), every sum, product and quotient in the oracle's order.
fn mn_area_region(light:u32,P:vec3<f32>,start:vec3<f32>)->AreaRegion{
    var r=AreaRegion(start,heap_vec3(light+L_area_axis_u),heap_vec3(light+L_area_axis_v),heap_float(light+L_area_len_u),heap_float(light+L_area_len_v),heap_float(light+L_area_invarea)>0.0,true);
    if(heap_float(light+L_area_normalize_spread)<=0.0){return r;}
    let N=heap_vec3(light+L_area_dir);let t=rounded_dot(N,mn_sub(P,r.P));let closest=mn_sub(P,t*N);
    let spread=t*heap_float(light+L_area_tan_half_spread);let su=rounded_dot(r.U,mn_sub(closest,r.P));let sv=rounded_dot(r.V,mn_sub(closest,r.P));
    let round= !r.rectangle&&r.len_u==r.len_v;var sample_spread=spread==0.0;
    if(round&&!sample_spread){
        let distance=rounded_sqrt(rounded_add(su*su,sv*sv));let radius=r.len_u*0.5;
        if(distance>=radius+spread){r.valid=false;return r;}
        sample_spread=distance<=abs(radius-spread)&&spread<radius;
        if(distance>abs(radius-spread)){
            let len_u=(radius+spread)-distance;var len_v=2.0*min(radius,spread);
            if(abs(rounded_add(radius*radius,-(spread*spread)))<distance*distance){let x=rounded_add(distance,rounded_divide(rounded_add(spread*spread,-(radius*radius)),distance));
                len_v=rounded_sqrt(rounded_add((2.0*spread)*(2.0*spread),-(x*x)));}
            let rectangle_area=len_u*len_v;let circle_area=EMITTER_PI*(radius*radius);let spread_area=EMITTER_PI*(spread*spread);
            if(rectangle_area<min(circle_area,spread_area)){
                r.rectangle=true;r.U=emitter_native_normalize(mn_sub(r.P,closest));r.V=emitter_quarter_turn(r.U,N);r.len_u=len_u;r.len_v=len_v;
                r.P=0.5*rounded_add3(r.P+closest,r.U*(spread-radius));return r;
            }sample_spread=spread_area<circle_area;
        }
    }else if(!round&&!sample_spread){
        let min_u=max(su-spread,-r.len_u*0.5);let max_u=min(su+spread,r.len_u*0.5);
        let min_v=max(sv-spread,-r.len_v*0.5);let max_v=min(sv+spread,r.len_v*0.5);
        if(min_u>=max_u||min_v>=max_v){r.valid=false;return r;}
        let lu=max_u-min_u;let lv=max_v-min_v;let rectangle_area=lu*lv;
        let ellipse_area=select(((EMITTER_PI*0.25)*r.len_u)*r.len_v,MNEE_FLT_MAX,r.rectangle);let spread_area=EMITTER_PI*(spread*spread);
        if(r.rectangle||rectangle_area<min(ellipse_area,spread_area)){
            r.rectangle=true;r.len_u=lu;r.len_v=lv;r.P=rounded_add3(rounded_add3(r.P,r.U*(0.5*(min_u+max_u))),r.V*(0.5*(min_v+max_v)));return r;
        }r.rectangle=false;sample_spread=spread_area<ellipse_area;
    }
    if(sample_spread){r.rectangle=false;r.P=rounded_add3(rounded_add3(r.P,r.U*su),r.V*sv);r.len_u=spread*2.0;r.len_v=spread*2.0;}
    return r;
}
// area_light_eval<false>(klight, ray_P, &ls->P, ls, rand, sample_coord): pdf, eval_fac and, with sample_coord, the
// new point, direction and distance. A rejected region leaves the sample as it was (false).
fn mn_area_eval(ls:ptr<function,MnLight>,light:u32,ray_P:vec3<f32>,random:vec2<f32>,sample_coord:bool)->bool{
    let N=heap_vec3(light+L_area_dir);let spread=heap_float(light+L_area_tan_half_spread);let normalization=heap_float(light+L_area_normalize_spread);
    let region=mn_area_region(light,ray_P,(*ls).P);if(!region.valid){return false;}
    var P=region.P;
    if(region.rectangle){let rect=area_rectangle(ray_P,region,random,sample_coord);P=rect.P;(*ls).pdf=rect.pdf;}
    else if(spread==0.0){(*ls).pdf=1.0;}
    else{
        if(sample_coord){let disk=emitter_disk(random);let ru=(region.U*region.len_u)*0.5;let rv=(region.V*region.len_v)*0.5;P=rounded_add3(P,rounded_add3(ru*disk.x,rv*disk.y));}
        (*ls).pdf=rounded_divide(4.0*0.31830987334251404,region.len_u*region.len_v);
    }
    if(sample_coord){(*ls).P=P;let ray=mn_safe_normalize_len(mn_sub(P,ray_P));(*ls).D=ray.xyz;(*ls).t=ray.w;}
    (*ls).eval_fac=0.31830987334251404*abs(heap_float(light+L_area_invarea));
    if(normalization>0.0){
        // area_light_spread_attenuation with tan_angle(-D, Ng) = len(cross(-D, Ng)) / dot(-D, Ng).
        let tan_a=rounded_divide(mn_len(rounded_cross(-(*ls).D,N)),rounded_dot(-(*ls).D,N));
        var attenuation=max(rounded_add(spread,-tan_a)*normalization,0.0);if(spread==0.0){attenuation=select(3.1415927410125732,0.0,tan_a>1e-5);}
        (*ls).eval_fac*=attenuation;
    }
    if(!region.rectangle&&spread>0.0){let cosine=rounded_dot(N,-(*ls).D);(*ls).pdf*=select(0.0,rounded_divide((*ls).t*(*ls).t,cosine),cosine>0.0);}
    return (*ls).eval_fac>0.0;
}
// area_light_sample (not in a volume segment); a sample the pinned code rejects has pdf 0.
fn mn_area_sample(index:u32,sd:SurfaceData,selection:LightSelection)->NativeLightSample{
    var ls:NativeLightSample;let random=selection.random;ls.is_light=true;ls.prim=~bitcast<u32>(selection.prim);
    let light=table_at(G_lights,ls.prim,SIZE_lights);
    if(f32(state_half(S_path_bounce,index))>heap_float(light+L_max_bounces)){return ls;}
    ls.shader=load_word(light+L_shader_id);ls.object_id=load_word(light+L_object_id);
    let co=heap_vec3(light+L_co);if(rounded_dot(mn_sub(co,sd.P),heap_vec3(light+L_area_dir))>0.0){return ls;}
//#if LIGHT_AREA_DEGENERATE
    // A zero-length axis: area_light_sample rejects every sample (wavefront_emitters.wgsl area_evaluate; item 148).
    if(heap_float(light+L_area_len_u)==0.0||heap_float(light+L_area_len_v)==0.0){return ls;}
//#endif
    var m:MnLight;m.P=co;if(!mn_area_eval(&m,light,sd.P,random.xy,true)){return ls;}
    if(heap_float(light+L_area_normalize_spread)>0.0){
        let inplane=mn_sub(m.P,co);let u=rounded_dot(inplane,heap_vec3(light+L_area_axis_u));let v=rounded_dot(inplane,heap_vec3(light+L_area_axis_v));
        let eu=rounded_add(0.50000011920928955*heap_float(light+L_area_len_u),1e-6);let ev=rounded_add(0.50000011920928955*heap_float(light+L_area_len_v),1e-6);
        if(heap_float(light+L_area_invarea)<0.0){let a=rounded_divide(u,eu);let b=rounded_divide(v,ev);if(rounded_add(a*a,b*b)>1.0){return ls;}}
        else if(abs(u)>eu||abs(v)>ev){return ls;}
    }
    ls.P=m.P;ls.D=m.D;ls.t=m.t;ls.eval_fac=m.eval_fac;ls.pdf=m.pdf;if(!(ls.pdf>0.0)){ls.pdf=0.0;return ls;}
//#if LIGHT_SELECT else     ls.pdf*=data_float(D_integrator_distribution_pdf_lights);return ls;
    ls.pdf*=select(data_float(D_integrator_distribution_pdf_lights),selection.pdf,selection.tree);return ls;
//#endif
}
// area_light_mnee_sample_update (the selection pdf is re-applied by mn_light_update); Ng is the light's normal.
fn mn_area_update(ls:ptr<function,MnLight>,light:u32,P:vec3<f32>){
    (*ls).Ng=heap_vec3(light+L_area_dir);
    if(heap_float(light+L_area_tan_half_spread)==0.0){_=mn_area_eval(ls,light,P,vec2<f32>(0),true);return;}
    let ray=mn_safe_normalize_len(mn_sub((*ls).P,P));(*ls).D=ray.xyz;(*ls).t=ray.w;
    _=mn_area_eval(ls,light,P,vec2<f32>(0),false);
    // pdf /= light_pdf_area_to_solid_angle(Ng, -D, t).
    let cosine=rounded_dot((*ls).Ng,-(*ls).D);var factor=0.0;if(cosine>0.0){factor=rounded_divide((*ls).t*(*ls).t,cosine);}
    (*ls).pdf=rounded_divide((*ls).pdf,factor);
}
//#endif
