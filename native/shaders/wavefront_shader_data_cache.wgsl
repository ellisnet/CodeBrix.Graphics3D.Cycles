// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Scalar GPU-only cache separates geometric setup from SVM compilation.
// p(26) is the first record and p(27) is its stride in bytes.
fn surface_data_write(at:u32,sd:SurfaceData){
store_word(at+0u,bitcast<u32>(sd.P.x));
store_word(at+4u,bitcast<u32>(sd.P.y));
store_word(at+8u,bitcast<u32>(sd.P.z));
store_word(at+12u,bitcast<u32>(sd.N.x));
store_word(at+16u,bitcast<u32>(sd.N.y));
store_word(at+20u,bitcast<u32>(sd.N.z));
store_word(at+24u,bitcast<u32>(sd.Ng.x));
store_word(at+28u,bitcast<u32>(sd.Ng.y));
store_word(at+32u,bitcast<u32>(sd.Ng.z));
store_word(at+36u,bitcast<u32>(sd.wi.x));
store_word(at+40u,bitcast<u32>(sd.wi.y));
store_word(at+44u,bitcast<u32>(sd.wi.z));
store_word(at+48u,bitcast<u32>(sd.dPdu.x));
store_word(at+52u,bitcast<u32>(sd.dPdu.y));
store_word(at+56u,bitcast<u32>(sd.dPdu.z));
store_word(at+60u,bitcast<u32>(sd.dPdv.x));
store_word(at+64u,bitcast<u32>(sd.dPdv.y));
store_word(at+68u,bitcast<u32>(sd.dPdv.z));
store_word(at+72u,bitcast<u32>(sd.du.x));
store_word(at+76u,bitcast<u32>(sd.du.y));
store_word(at+80u,bitcast<u32>(sd.dv.x));
store_word(at+84u,bitcast<u32>(sd.dv.y));
store_word(at+88u,bitcast<u32>(sd.u));
store_word(at+92u,bitcast<u32>(sd.v));
store_word(at+96u,bitcast<u32>(sd.time));
store_word(at+100u,bitcast<u32>(sd.ray_length));
store_word(at+104u,bitcast<u32>(sd.dP));
store_word(at+108u,bitcast<u32>(sd.dI));
store_word(at+112u,sd.object_id);
store_word(at+116u,sd.prim);
store_word(at+120u,sd.primitive_type);
store_word(at+124u,sd.shader);
store_word(at+128u,sd.runtime_flag);
store_word(at+132u,sd.shader_flag);
store_word(at+136u,sd.object_flag);
store_word(at+140u,sd.error);
store_word(at+144u,bitcast<u32>(sd.motion_tfm.x.x));
store_word(at+148u,bitcast<u32>(sd.motion_tfm.x.y));
store_word(at+152u,bitcast<u32>(sd.motion_tfm.x.z));
store_word(at+156u,bitcast<u32>(sd.motion_tfm.x.w));
store_word(at+160u,bitcast<u32>(sd.motion_tfm.y.x));
store_word(at+164u,bitcast<u32>(sd.motion_tfm.y.y));
store_word(at+168u,bitcast<u32>(sd.motion_tfm.y.z));
store_word(at+172u,bitcast<u32>(sd.motion_tfm.y.w));
store_word(at+176u,bitcast<u32>(sd.motion_tfm.z.x));
store_word(at+180u,bitcast<u32>(sd.motion_tfm.z.y));
store_word(at+184u,bitcast<u32>(sd.motion_tfm.z.z));
store_word(at+188u,bitcast<u32>(sd.motion_tfm.z.w));
store_word(at+192u,bitcast<u32>(sd.motion_itfm.x.x));
store_word(at+196u,bitcast<u32>(sd.motion_itfm.x.y));
store_word(at+200u,bitcast<u32>(sd.motion_itfm.x.z));
store_word(at+204u,bitcast<u32>(sd.motion_itfm.x.w));
store_word(at+208u,bitcast<u32>(sd.motion_itfm.y.x));
store_word(at+212u,bitcast<u32>(sd.motion_itfm.y.y));
store_word(at+216u,bitcast<u32>(sd.motion_itfm.y.z));
store_word(at+220u,bitcast<u32>(sd.motion_itfm.y.w));
store_word(at+224u,bitcast<u32>(sd.motion_itfm.z.x));
store_word(at+228u,bitcast<u32>(sd.motion_itfm.z.y));
store_word(at+232u,bitcast<u32>(sd.motion_itfm.z.z));
store_word(at+236u,bitcast<u32>(sd.motion_itfm.z.w));
}
fn surface_data_read(at:u32)->SurfaceData{var sd:SurfaceData;
sd.P.x=bitcast<f32>(load_word(at+0u));
sd.P.y=bitcast<f32>(load_word(at+4u));
sd.P.z=bitcast<f32>(load_word(at+8u));
sd.N.x=bitcast<f32>(load_word(at+12u));
sd.N.y=bitcast<f32>(load_word(at+16u));
sd.N.z=bitcast<f32>(load_word(at+20u));
sd.Ng.x=bitcast<f32>(load_word(at+24u));
sd.Ng.y=bitcast<f32>(load_word(at+28u));
sd.Ng.z=bitcast<f32>(load_word(at+32u));
sd.wi.x=bitcast<f32>(load_word(at+36u));
sd.wi.y=bitcast<f32>(load_word(at+40u));
sd.wi.z=bitcast<f32>(load_word(at+44u));
sd.dPdu.x=bitcast<f32>(load_word(at+48u));
sd.dPdu.y=bitcast<f32>(load_word(at+52u));
sd.dPdu.z=bitcast<f32>(load_word(at+56u));
sd.dPdv.x=bitcast<f32>(load_word(at+60u));
sd.dPdv.y=bitcast<f32>(load_word(at+64u));
sd.dPdv.z=bitcast<f32>(load_word(at+68u));
sd.du.x=bitcast<f32>(load_word(at+72u));
sd.du.y=bitcast<f32>(load_word(at+76u));
sd.dv.x=bitcast<f32>(load_word(at+80u));
sd.dv.y=bitcast<f32>(load_word(at+84u));
sd.u=bitcast<f32>(load_word(at+88u));
sd.v=bitcast<f32>(load_word(at+92u));
sd.time=bitcast<f32>(load_word(at+96u));
sd.ray_length=bitcast<f32>(load_word(at+100u));
sd.dP=bitcast<f32>(load_word(at+104u));
sd.dI=bitcast<f32>(load_word(at+108u));
sd.object_id=load_word(at+112u);
sd.prim=load_word(at+116u);
sd.primitive_type=load_word(at+120u);
sd.shader=load_word(at+124u);
sd.runtime_flag=load_word(at+128u);
sd.shader_flag=load_word(at+132u);
sd.object_flag=load_word(at+136u);
sd.error=load_word(at+140u);
sd.motion_tfm.x.x=bitcast<f32>(load_word(at+144u));
sd.motion_tfm.x.y=bitcast<f32>(load_word(at+148u));
sd.motion_tfm.x.z=bitcast<f32>(load_word(at+152u));
sd.motion_tfm.x.w=bitcast<f32>(load_word(at+156u));
sd.motion_tfm.y.x=bitcast<f32>(load_word(at+160u));
sd.motion_tfm.y.y=bitcast<f32>(load_word(at+164u));
sd.motion_tfm.y.z=bitcast<f32>(load_word(at+168u));
sd.motion_tfm.y.w=bitcast<f32>(load_word(at+172u));
sd.motion_tfm.z.x=bitcast<f32>(load_word(at+176u));
sd.motion_tfm.z.y=bitcast<f32>(load_word(at+180u));
sd.motion_tfm.z.z=bitcast<f32>(load_word(at+184u));
sd.motion_tfm.z.w=bitcast<f32>(load_word(at+188u));
sd.motion_itfm.x.x=bitcast<f32>(load_word(at+192u));
sd.motion_itfm.x.y=bitcast<f32>(load_word(at+196u));
sd.motion_itfm.x.z=bitcast<f32>(load_word(at+200u));
sd.motion_itfm.x.w=bitcast<f32>(load_word(at+204u));
sd.motion_itfm.y.x=bitcast<f32>(load_word(at+208u));
sd.motion_itfm.y.y=bitcast<f32>(load_word(at+212u));
sd.motion_itfm.y.z=bitcast<f32>(load_word(at+216u));
sd.motion_itfm.y.w=bitcast<f32>(load_word(at+220u));
sd.motion_itfm.z.x=bitcast<f32>(load_word(at+224u));
sd.motion_itfm.z.y=bitcast<f32>(load_word(at+228u));
sd.motion_itfm.z.z=bitcast<f32>(load_word(at+232u));
sd.motion_itfm.z.w=bitcast<f32>(load_word(at+236u));
return sd;
}
fn surface_cache_address(slot:u32)->u32{
    let base=p(26u);let stride=p(27u);let bytes=arrayLength(&heap)*4u;
    if(base==0u||base%4u!=0u||stride%4u!=0u||stride<SURFACE_DATA_CACHE_BYTES||base>bytes||SURFACE_DATA_CACHE_BYTES>bytes-base){fail();return 0u;}
    if(slot>(bytes-base-SURFACE_DATA_CACHE_BYTES)/stride){fail();return 0u;}
    return base+slot*stride;
}
fn surface_cached(slot:u32)->SurfaceData{
    let at=surface_cache_address(slot);if(at==0u){var sd:SurfaceData;sd.error=1u;return sd;}
    return surface_data_read(at);
}
@compute @workgroup_size(64) fn prepare_shader_data(@builtin(global_invocation_id) id:vec3<u32>){
    if(id.x>=p(1u)){return;}let at=surface_cache_address(id.x);if(at==0u){return;}
    var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}
    surface_data_write(at,surface_from_ray(index));
}
