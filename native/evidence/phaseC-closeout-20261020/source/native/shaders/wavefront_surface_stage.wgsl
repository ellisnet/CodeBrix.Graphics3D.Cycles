// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// GPU-private stage records retain all evaluated shader and closure metadata.
// The first word is cleared before validation and set only after a full write.
struct SurfaceStage {sd:SurfaceData,r:SvmResult}
fn surface_stage_write(at:u32,sd:SurfaceData,r:SvmResult){
surface_data_write(at+4u,sd);
store_word(at+244u,r.closure_base);
store_word(at+248u,r.count);
store_word(at+252u,r.remaining);
store_word(at+256u,r.runtime_flag);
store_word(at+260u,r.error);
store_word(at+264u,r.lcg_state);
store_word(at+268u,bitcast<u32>(r.N.x));
store_word(at+272u,bitcast<u32>(r.N.y));
store_word(at+276u,bitcast<u32>(r.N.z));
store_word(at+280u,bitcast<u32>(r.emission.x));
store_word(at+284u,bitcast<u32>(r.emission.y));
store_word(at+288u,bitcast<u32>(r.emission.z));
store_word(at+292u,bitcast<u32>(r.transparency.x));
store_word(at+296u,bitcast<u32>(r.transparency.y));
store_word(at+300u,bitcast<u32>(r.transparency.z));
store_word(at,1u);
}
fn surface_stage_read(at:u32)->SurfaceStage{let sd=surface_data_read(at+4u);var r:SvmResult;
r.closure_base=load_word(at+244u);
r.count=load_word(at+248u);
r.remaining=load_word(at+252u);
r.runtime_flag=load_word(at+256u);
r.error=load_word(at+260u);
r.lcg_state=load_word(at+264u);
r.N.x=bitcast<f32>(load_word(at+268u));
r.N.y=bitcast<f32>(load_word(at+272u));
r.N.z=bitcast<f32>(load_word(at+276u));
r.emission.x=bitcast<f32>(load_word(at+280u));
r.emission.y=bitcast<f32>(load_word(at+284u));
r.emission.z=bitcast<f32>(load_word(at+288u));
r.transparency.x=bitcast<f32>(load_word(at+292u));
r.transparency.y=bitcast<f32>(load_word(at+296u));
r.transparency.z=bitcast<f32>(load_word(at+300u));
return SurfaceStage(sd,r);
}
