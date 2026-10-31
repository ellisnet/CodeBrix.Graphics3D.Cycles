// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "wavefront_test.h"
#include "wavefront.h"
#include "svm_surface_reference.h"
#include "reference_policy.h"
#include <filesystem>
#include <map>
#include <iomanip>
#include "kernel/svm/node_types.h"
#include "scene/scene.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
struct OutputField {std::string name,expression;size_t offset,size;bool floating;};
static std::vector<OutputField> output_fields(){
  std::vector<OutputField> out;
#define F(name,fp) out.push_back({#name,"r." #name,offsetof(SurfaceSvmRecord,name),sizeof(SurfaceSvmRecord::name),fp});
  F(count,false) F(remaining,false) F(runtime_flag,false) F(lcg_state,false) F(N,true) F(emission,true) F(transparency,true)
#undef F
  for(unsigned i=0;i<MAX_CLOSURE;i++){
    std::string prefix="closures["+std::to_string(i)+"]";
#define F(name,fp) out.push_back({prefix+"." #name,"r."+prefix+"." #name,offsetof(SurfaceSvmRecord,closures)+i*sizeof(SurfaceClosureRecord)+offsetof(SurfaceClosureRecord,name),sizeof(SurfaceClosureRecord::name),fp});
    F(weight,true) F(sample_weight,true) F(N,true) F(kind,false) F(roughness,true) F(a,true) F(b,true) F(multiscatter,true)
    F(alpha_x,true) F(alpha_y,true) F(ior,true) F(energy_scale,true) F(T,true) F(fresnel_f0,true) F(reflection_tint,true) F(transmission_tint,true)
#undef F
  }return out;
}
static std::string probe_source(){
  std::ostringstream s;s<<svm_surface_source();
  s<<"@compute @workgroup_size(64) fn surface_svm_probe(@builtin(global_invocation_id) id:vec3<u32>){\n"
      "svm_invocation(id.x);if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}\n"
      "let sd=surface_cached(id.x);if(sd.error!=0u){return;}let r=svm_evaluate(index,sd,data_word(D_max_closures),p(23u)+id.x*SVM_CLOSURE_CAPACITY*SVM_CLOSURE_BYTES);if(r.error!=0u){return;}\n"
      "let base=p(4u)+id.x*"<<sizeof(SurfaceSvmRecord)<<"u;\n";
  const char *components[]={".x",".y",".z"};
  auto emit=[&](const OutputField &f,const std::string &base,const std::string &expression,size_t offset){
    for(size_t i=0;i<f.size/4;i++)s<<"store_word("<<base<<"+"<<offset+4*i<<"u,"<<(f.floating?"bitcast<u32>(":"")<<expression<<(f.size>4?components[i]:"")<<(f.floating?")":"")<<");\n";
  };
  auto fs=output_fields();for(auto &f:fs)if(f.offset<offsetof(SurfaceSvmRecord,closures))emit(f,"base",f.expression,f.offset);
  s<<"for(var ci=0u;ci<SVM_CLOSURE_CAPACITY;ci++){let cb=base+"<<offsetof(SurfaceSvmRecord,closures)<<"u+ci*"<<sizeof(SurfaceClosureRecord)<<"u;let c=svm_closure_read(r,ci);\n";
  for(auto &f:fs)if(f.name.starts_with("closures[0]."))emit(f,"cb","c."+f.name.substr(12),f.offset - offsetof(SurfaceSvmRecord,closures));
  s<<"}\n";
  s<<"}\n";return s.str();
}
// The probe module is scene-specialized (gate_probe_wgsl, device.h) and follows
// the uploaded programs like the renderer's pipelines: refresh_probe rebuilds it
// before a dispatch whose programs reach a region the module was built without
// (typed programs and rejection cases upload new programs after creation).
static std::string probe_raw,probe_module;static ShaderFeatures probe_features;
static ShaderFeatures probe_scene_features(WebGPUDevice &d){
  // Native refusals (unsupported Principled features) keep the current module:
  // the case under test then reports through the WGSL handler, as before.
  // Glass records carry the canonical generalized-Schlick fields (model in
  // roughness, f0 in multiscatter) only in modules with the Principled region;
  // the probe compares those canonical records, so it keeps that region whenever
  // glass is reachable (the arithmetic is identical either way; films match).
  try{ShaderFeatures f=d.shader_features();if(f&(SF_BSDF_GLASS|SF_BSDF_BECKMANN))f|=SF_BSDF_PRINCIPLED;return f;}
  catch(const std::exception&){return probe_features;}
}
static cbc::Pipeline pipeline(WebGPUDevice &d,const std::string &probe){
  probe_raw=probe;probe_features=probe_scene_features(d);probe_module=specialize_wgsl(probe,probe_features);
  // Optional diagnostic: every probe module this gate builds, numbered, with its feature mask.
  if(const char *dump=std::getenv("CODEBRIX_SVM_PROBE_DUMP")){static unsigned built=0;const std::string name=std::string(dump)+"/probe-"+std::to_string(built++);
    std::ofstream(name+".wgsl")<<probe_module;std::ofstream features(name+".features.txt");features<<std::hex;
    // ShaderFeatures is 256 bits (pipelines.h): the words from the highest non-zero one down, so a mask within bits 0-63 prints as before.
    unsigned top=shader_feature_words-1;while(top&&!probe_features.words[top])top--;features<<probe_features.words[top];
    for(unsigned w=top;w-->0;){features<<std::setw(16)<<std::setfill('0')<<probe_features.words[w];}
    features<<'\n';}
  const std::string &source=probe_module;
  WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={source.data(),source.size()};
  WGPUShaderModuleDescriptor desc{};desc.nextInChain=&wgsl.chain;d.gpu.push_scope();
  cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&desc));d.gpu.pop_scope();
  d.surface_geometry=d.gpu.pipeline(module,d.pipeline_layout,"prepare_shader_data");
  return d.gpu.pipeline(module,d.pipeline_layout,"surface_svm_probe");
}
static void refresh_probe(WebGPUDevice &d,cbc::Pipeline &shader){
  if(!probe_raw.empty()&&(probe_scene_features(d)&~probe_features).any())shader=pipeline(d,probe_raw);
}
// The prepass is executed by the GPU before every probe, including rejections.
// Captures retain that GPU-generated cache, never CPU-derived ShaderData.
struct PreparedSurfaceCache {
  device_vector<uint> storage;
  PreparedSurfaceCache(WebGPUDevice &d,std::array<uint32_t,32> &p):storage(&d,"surface_data_cache",MEM_READ_WRITE){
    storage.alloc(16+size_t(p[1])*surface_data_cache_bytes/4);
    std::fill_n(storage.data(),storage.size(),0xa5a5a5a5u);storage.copy_to_device();
    p[26]=storage.device_pointer+32;p[27]=surface_data_cache_bytes;
    if(p[1])d.dispatch(p,p[1],d.surface_geometry,64);
  }
  void verify(){storage.copy_from_device();for(unsigned i=0;i<8;i++)require(storage[i]==0xa5a5a5a5u&&storage[storage.size()-1-i]==0xa5a5a5a5u,"ShaderData cache guard");}
};
struct Metrics {unsigned cases=0;uint64_t paths=0,integers=0,floats=0,state_checks=0,guards=0,strict=0;double maximum=0;std::set<uint> closures;};
// Validation reference policy, device-scene flow (reference_policy.h), one exchange key per evaluated case
// ("svm-N", in evaluation order): the dump pass writes the scene tables this device prepared, the selected
// path states and its local CPU records; the Linux pass loads them and exports the glibc records; the
// import pass compares the GPU with the imported records (the local records are a diagnostic). The dump
// and Linux passes do no GPU work. A probe gate evaluates hundreds of programs on one scene, so the
// tables other than the program and KernelData are written once per distinct content
// (svm-base-N.scene, CBCSCENE1 records) and each case file (svm-N.case) names its base and carries the
// program, KernelData, the selected path indices and their path, ray and intersection state (what the
// CPU reference reads; the other state members are the same zero-initialized values on both sides).
// Without the variables nothing changes.
static bool svm_exchange_mode(){return reference_policy::prepare_only()||reference_policy::reference_only();}
struct SvmExchangeRecord {uint64_t element;std::string data;};
struct SvmExchangeState {decltype(IntegratorStateCPU::path) path;decltype(IntegratorStateCPU::ray) ray;decltype(IntegratorStateCPU::isect) isect;};
static std::map<std::string,SvmExchangeRecord> svm_exchange_read(const std::string &bytes,const std::string &magic){
  if(bytes.compare(0,magic.size(),magic))throw std::runtime_error("Not an SVM exchange file ("+magic+")");
  std::map<std::string,SvmExchangeRecord> out;size_t at=magic.size();
  while(at<bytes.size()){uint64_t h[3];if(at+sizeof(h)>bytes.size())throw std::runtime_error("Truncated SVM exchange file");std::memcpy(h,bytes.data()+at,sizeof(h));at+=sizeof(h);
    if(at+h[0]>bytes.size()||h[1]*h[2]>bytes.size()-at-h[0])throw std::runtime_error("Truncated SVM exchange file");
    std::string name=bytes.substr(at,h[0]);at+=h[0];out[name]={h[1],bytes.substr(at,h[1]*h[2])};at+=h[1]*h[2];}
  return out;
}
template<class V>static void svm_exchange_apply(V &table,const std::map<std::string,SvmExchangeRecord> &records,const std::string &name){
  auto it=records.find(name);if(it==records.end())throw std::runtime_error("SVM exchange table missing: "+name);
  const size_t element=sizeof(*table.data());if(it->second.element!=element)throw std::runtime_error("SVM exchange element size differs: "+name);
  const size_t count=it->second.data.size()/element;if(table.size()!=count)table.alloc(count);if(count)std::memcpy(table.data(),it->second.data.data(),count*element);
}
static std::string svm_exchange_tables(DeviceScene &s){
  std::string out="CBCSCENE1";
#define CBC_SVM_TABLE(name,type) reference_policy::put(out,#name,s.name.data(),sizeof(*s.name.data()),s.name.size());
  CBC_SCENE_TABLES(CBC_SVM_TABLE) CBC_SVM_TABLE(sample_pattern_lut,float)
#undef CBC_SVM_TABLE
  return out;
}
static std::vector<SurfaceSvmRecord> svm_reference_records(Scene &scene,const std::vector<IntegratorStateCPU> &states,const std::vector<int> &ids,bool &exchange_only){
  static unsigned exchange_case=0,bases=0;static std::string last_tables,loaded_base;
  const std::string key="svm-"+std::to_string(exchange_case++);DeviceScene &ds=scene.dscene;
  std::vector<SurfaceSvmRecord> local;exchange_only=svm_exchange_mode();
  if(const char *dump=std::getenv("CODEBRIX_CYCLES_SCENE_DUMP")){
    std::filesystem::create_directories(dump);const std::string tables=svm_exchange_tables(ds);
    if(tables!=last_tables){last_tables=tables;std::ofstream(std::filesystem::path(dump)/("svm-base-"+std::to_string(bases++)+".scene"),std::ios::binary)<<tables;}
    const std::string base="svm-base-"+std::to_string(bases-1);std::vector<SvmExchangeState> selected;for(int i:ids)selected.push_back({states[i].path,states[i].ray,states[i].isect});
    std::string out="CBCSVMCASE1";reference_policy::put(out,"base",base.data(),1,base.size());reference_policy::put(out,"svm_nodes",ds.svm_nodes.data(),4,ds.svm_nodes.size());
    reference_policy::put(out,"KernelData",&ds.data,sizeof(ds.data),1);reference_policy::put(out,"ids",ids.data(),sizeof(int),ids.size());
    reference_policy::put(out,"states",selected.data(),sizeof(SvmExchangeState),selected.size());
    std::ofstream file(std::filesystem::path(dump)/(key+".case"),std::ios::binary);file<<out;if(!file)throw std::runtime_error("Could not write SVM exchange case "+key);
    local=original_surface_svm(ds,ids,states);
  }else if(const char *load=std::getenv("CODEBRIX_CYCLES_SCENE_LOAD")){
    const auto records=svm_exchange_read(reference_policy::read_file(std::filesystem::path(load)/(key+".case")),"CBCSVMCASE1");
    const std::string base=records.at("base").data;
    if(base!=loaded_base){const auto tables=svm_exchange_read(reference_policy::read_file(std::filesystem::path(load)/(base+".scene")),"CBCSCENE1");
#define CBC_SVM_TABLE(name,type) svm_exchange_apply(ds.name,tables,#name);
      CBC_SCENE_TABLES(CBC_SVM_TABLE) CBC_SVM_TABLE(sample_pattern_lut,float)
#undef CBC_SVM_TABLE
      loaded_base=base;}
    svm_exchange_apply(ds.svm_nodes,records,"svm_nodes");
    const auto &kernel=records.at("KernelData");if(kernel.element!=sizeof(ds.data)||kernel.data.size()!=sizeof(ds.data))throw std::runtime_error("SVM exchange KernelData size differs");std::memcpy(&ds.data,kernel.data.data(),sizeof(ds.data));
    const auto &indices=records.at("ids");const auto &blob=records.at("states");
    if(indices.data.size()!=ids.size()*sizeof(int)||(!ids.empty()&&std::memcmp(indices.data.data(),ids.data(),indices.data.size()))||blob.data.size()!=ids.size()*sizeof(SvmExchangeState))
      throw std::runtime_error("SVM exchange path selection differs: "+key);
    auto loaded=states;for(size_t k=0;k<ids.size();k++){SvmExchangeState e;std::memcpy(static_cast<void*>(&e),blob.data.data()+k*sizeof(e),sizeof(e));loaded[ids[k]].path=e.path;loaded[ids[k]].ray=e.ray;loaded[ids[k]].isect=e.isect;}
    local=original_surface_svm(ds,ids,loaded);
  }else local=original_surface_svm(ds,ids,states);
  std::vector<float> words(local.size()*sizeof(SurfaceSvmRecord)/4);if(!local.empty())std::memcpy(words.data(),local.data(),words.size()*4);
  reference_policy::record_local(key,words);
  const char *diagnostic=std::getenv("CODEBRIX_CYCLES_REFERENCE_DIAGNOSTIC");
  const auto reference=reference_policy::resolve(key,words,diagnostic?diagnostic:"svm-reference-diagnostic.jsonl");
  if(!local.empty())std::memcpy(static_cast<void*>(local.data()),reference.data(),reference.size()*4);
  return local;
}
static void evaluate(WebGPUDevice &d,Scene &scene,cbc::Pipeline &shader,const std::vector<Field> &layout,
    const std::vector<IntegratorStateCPU> &states,const std::vector<int> &ids,const std::string &label,Metrics &m,const std::string &capture="",bool dense=false){
  bool exchange_only=false;auto expected=svm_reference_records(scene,states,ids,exchange_only);
  if(exchange_only){for(size_t i=0;i<ids.size();i++)for(unsigned j=0;j<expected[i].count&&j<MAX_CLOSURE;j++)m.closures.insert(expected[i].closures[j].kind);
    m.cases++;m.paths+=ids.size();std::cerr<<"Exchanged "<<label<<" ("<<ids.size()<<" paths)\n";return;}
  upload(d,layout,states);
  device_vector<int> selected(&d,"svm_surface_indices",MEM_READ_WRITE);selected.alloc(ids.size());std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
  device_vector<uint> output(&d,"svm_surface_output",MEM_READ_WRITE);output.alloc(16+ids.size()*sizeof(SurfaceSvmRecord)/4);std::fill_n(output.data(),output.size(),0xa5a5a5a5u);output.copy_to_device();
  device_vector<uint> scratch(&d,"svm_surface_closures",MEM_READ_WRITE);scratch.alloc(16+ids.size()*MAX_CLOSURE*sizeof(SurfaceClosureRecord)/4);std::fill_n(scratch.data(),scratch.size(),0xa5a5a5a5u);std::fill(scratch.data()+8,scratch.data()+scratch.size()-8,0);scratch.copy_to_device();
  std::array<uint32_t,32> p{};p[1]=ids.size();p[2]=dense?0:selected.device_pointer;p[4]=output.device_pointer+32;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[10]=states.size();p[14]=d.scene_tables();p[17]=d.error_word;p[21]=scene.dscene.svm_nodes.device_pointer;p[22]=scene.dscene.svm_nodes.size();p[23]=scratch.device_pointer+32;
  refresh_probe(d,shader);PreparedSurfaceCache cache(d,p);
  if(!capture.empty()){
    std::ofstream(capture+".wgsl")<<probe_module;
    auto staging=d.gpu.buffer(d.arena_bytes,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);wgpuCommandEncoderCopyBufferToBuffer(d.commands(),d.arena,0,staging,0,d.arena_bytes);d.flush();std::vector<uint8_t> arena(d.arena_bytes);d.gpu.read(staging,arena.data(),arena.size());
    std::ofstream(capture+".arena.bin",std::ios::binary).write(reinterpret_cast<const char*>(arena.data()),arena.size());std::ofstream(capture+".params.bin",std::ios::binary).write(reinterpret_cast<const char*>(p.data()),sizeof(p));std::ofstream(capture+".reference.bin",std::ios::binary).write(reinterpret_cast<const char*>(expected.data()),expected.size()*sizeof(SurfaceSvmRecord));
    std::ofstream info(capture+".capture.json");info<<"{\"entry\":\"surface_svm_probe\",\"recordSize\":"<<sizeof(SurfaceSvmRecord)<<",\"count\":"<<ids.size()<<",\"outputOffset\":"<<p[4]<<",\"errorOffset\":"<<p[17]<<",\"closureScratch\":{\"offset\":"<<p[23]<<",\"stride\":"<<MAX_CLOSURE*sizeof(SurfaceClosureRecord)<<",\"recordOffset\":"<<offsetof(SurfaceSvmRecord,closures)<<"},\"fields\":[";bool first=true;
    for(auto &f:output_fields()){if(!first)info<<',';first=false;info<<"{\"name\":\""<<f.name<<"\",\"offset\":"<<f.offset<<",\"size\":"<<f.size<<",\"floating\":"<<(f.floating?"true":"false")<<"}";}info<<"]}\n";
  }
  d.dispatch(p,ids.size()+63,shader);uint error_word=0;d.download(d.error_word,&error_word,4);output.copy_from_device();healthy(d);auto bytes=reinterpret_cast<const uint8_t*>(output.data());
  if(error_word){for(size_t i=0;i<ids.size();i++){uint marker;std::memcpy(&marker,bytes+32+i*sizeof(SurfaceSvmRecord),4);if(marker==0xa5a5a5a5u){throw std::runtime_error(label+" SVM error mask="+std::to_string(error_word)+" first incomplete path="+std::to_string(i)+" shader="+std::to_string(scene.dscene.tri_shader[states[ids[i]].isect.prim]&SHADER_MASK));}}throw std::runtime_error("SVM error word set");}
  for(size_t i=0;i<ids.size();i++){
    for(auto &f:output_fields())for(size_t k=0;k<f.size;k+=4){uint a,e;std::memcpy(&a,bytes+32+i*sizeof(SurfaceSvmRecord)+f.offset+k,4);std::memcpy(&e,reinterpret_cast<const char*>(&expected[i])+f.offset+k,4);
      if(!f.floating){m.integers++;require(a==e,label+" integer "+f.name+" path "+std::to_string(i)+" actual="+std::to_string(a)+" expected="+std::to_string(e));}
      else{m.floats++;float av=std::bit_cast<float>(a),ev=std::bit_cast<float>(e);double error=std::abs(double(av)-ev);m.maximum=std::max(m.maximum,error);m.strict+=error>1e-7+1e-7*std::abs(double(ev));
        if(!(std::isfinite(av)&&std::isfinite(ev)&&error<=5e-5+2e-4*std::abs(double(ev)))){SurfaceSvmRecord got{};std::memcpy(&got,bytes+32+i*sizeof(got),sizeof(got));auto &want=expected[i];std::cerr<<std::hex<<"Oren diagnostic a="<<std::bit_cast<uint>(got.closures[0].a)<<"/"<<std::bit_cast<uint>(want.closures[0].a)<<" b="<<std::bit_cast<uint>(got.closures[0].b)<<"/"<<std::bit_cast<uint>(want.closures[0].b)<<std::dec<<'\n';}
        require(std::isfinite(av)&&std::isfinite(ev)&&error<=5e-5+2e-4*std::abs(double(ev)),label+" float "+f.name+" path "+std::to_string(i)+" component "+std::to_string(k/4)+" actual="+std::to_string(av)+" expected="+std::to_string(ev));}}
    for(unsigned j=0;j<expected[i].count;j++)m.closures.insert(expected[i].closures[j].kind);
  }
  for(unsigned i=0;i<32;i++){require(bytes[i]==0xa5&&bytes[output.size()*4-1-i]==0xa5,label+" output guard");m.guards+=2;}
  scratch.copy_from_device();healthy(d);auto sb=reinterpret_cast<const uint8_t*>(scratch.data());
  for(unsigned i=0;i<32;i++){require(sb[i]==0xa5&&sb[scratch.size()*4-1-i]==0xa5,label+" closure scratch guard");m.guards+=2;}
#if CBC_BSDF_FIXTURE==1
  // A Metallic BSDF closure removed by closure_alloc_extra stays set up in its slot beyond the
  // count (marked a = -1), as in the native closure array; every other slot matches the output.
  for(size_t i=0;i<ids.size();i++)for(unsigned j=0;j<MAX_CLOSURE;j++){
    const uint8_t *slot=sb+32+(i*MAX_CLOSURE+j)*sizeof(SurfaceClosureRecord);SurfaceClosureRecord scratch_record;std::memcpy(&scratch_record,slot,sizeof(scratch_record));
    const bool removed=scratch_record.a==-1.0f&&(scratch_record.kind==CLOSURE_BSDF_MICROFACET_GGX_ID||scratch_record.kind==CLOSURE_BSDF_MICROFACET_BECKMANN_ID);
    require(removed||!std::memcmp(slot,bytes+32+i*sizeof(SurfaceSvmRecord)+offsetof(SurfaceSvmRecord,closures)+j*sizeof(SurfaceClosureRecord),sizeof(SurfaceClosureRecord)),label+" closure scratch mismatch");}
#else
  for(size_t i=0;i<ids.size();i++)require(!std::memcmp(sb+32+i*MAX_CLOSURE*sizeof(SurfaceClosureRecord),bytes+32+i*sizeof(SurfaceSvmRecord)+offsetof(SurfaceSvmRecord,closures),MAX_CLOSURE*sizeof(SurfaceClosureRecord)),label+" closure scratch mismatch");
#endif
  auto unchanged=download(d,layout,states.size());for(auto &f:layout)for(size_t i=0;i<states.size();i++){require(!std::memcmp(reinterpret_cast<const char*>(&unchanged[i])+f.offset,reinterpret_cast<const char*>(&states[i])+f.offset,f.stride),label+" changed path state "+f.name);m.state_checks++;}
  cache.verify();healthy(d);m.guards+=64;
  m.cases++;m.paths+=ids.size();std::cerr<<"Passed "<<label<<" ("<<ids.size()<<" paths)\n";
}
static SVMInputFloat value(float f){return {std::bit_cast<uint>(f)};}
static SVMInputFloat3 color(float x,float y,float z){return {value(x),value(y),value(z)};}
struct Program {
  std::vector<uint> words;
  Program(DeviceScene &scene):words(scene.data.max_shaders*4,uint(NODE_END)){for(int i=0;i<scene.data.max_shaders;i++){words[i*4]=NODE_SHADER_JUMP;words[i*4+1]=words.size();}}
  template<class T>void add(uint op,const T &n){words.push_back(op);auto b=std::bit_cast<std::array<uint,sizeof(T)/4>>(n);words.insert(words.end(),b.begin(),b.end());}
  void weight(float x,float y,float z){add(NODE_CLOSURE_SET_WEIGHT,SVMNodeClosureSetWeight{.rgb=make_float3(x,y,z)});}
  void refraction(float roughness,float ior,SVMStackOffset normal=SVM_STACK_INVALID,ClosureType kind=CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID){add(NODE_CLOSURE_BSDF,SVMNodeClosureBsdf{.closure_type=kind,.mix_weight_offset=SVM_STACK_INVALID});auto n=SVMNodeRefractionBsdfData{.roughness=value(roughness),.ior=value(ior),.normal_offset=normal};auto b=std::bit_cast<std::array<uint,sizeof(n)/4>>(n);words.insert(words.end(),b.begin(),b.end());}
  void glass(float roughness,float ior,float thickness=0,SVMStackOffset normal=SVM_STACK_INVALID,ClosureType kind=CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID){add(NODE_CLOSURE_BSDF,SVMNodeClosureBsdf{.closure_type=kind,.mix_weight_offset=SVM_STACK_INVALID});auto n=SVMNodeGlassBsdfData{.color=color(.2,.5,.9),.roughness=value(roughness),.ior=value(ior),.thin_film_thickness=value(thickness),.thin_film_ior=value(1.3f),.normal_offset=normal};auto b=std::bit_cast<std::array<uint,sizeof(n)/4>>(n);words.insert(words.end(),b.begin(),b.end());}
  void diffuse(float roughness,SVMStackOffset normal=SVM_STACK_INVALID){add(NODE_CLOSURE_BSDF,SVMNodeClosureBsdf{.closure_type=CLOSURE_BSDF_DIFFUSE_ID,.mix_weight_offset=SVM_STACK_INVALID});auto n=SVMNodeDiffuseBsdfData{.color=color(.2,.5,.9),.roughness=value(roughness),.normal_offset=normal};auto b=std::bit_cast<std::array<uint,sizeof(n)/4>>(n);words.insert(words.end(),b.begin(),b.end());}
  void glossy(ClosureType type,float roughness,float anisotropy=0,float rotation=0,SVMStackOffset tangent=SVM_STACK_INVALID,SVMStackOffset normal=SVM_STACK_INVALID){
    add(NODE_CLOSURE_BSDF,SVMNodeClosureBsdf{.closure_type=type,.mix_weight_offset=SVM_STACK_INVALID});
    auto n=SVMNodeGlossyBsdfData{.color=color(.2,.5,.9),.roughness=value(roughness),.anisotropy=value(anisotropy),.rotation=value(rotation),.normal_offset=normal,.tangent_offset=tangent};
    auto b=std::bit_cast<std::array<uint,sizeof(n)/4>>(n);words.insert(words.end(),b.begin(),b.end());
  }
  void simple(ClosureType type,SVMStackOffset normal=SVM_STACK_INVALID){add(NODE_CLOSURE_BSDF,SVMNodeClosureBsdf{.closure_type=type,.mix_weight_offset=SVM_STACK_INVALID});auto n=SVMNodeSimpleBsdfData{.normal_offset=normal};auto b=std::bit_cast<std::array<uint,sizeof(n)/4>>(n);words.insert(words.end(),b.begin(),b.end());}
  void install(DeviceScene &s){words.push_back(NODE_END);s.svm_nodes.alloc(words.size());std::copy(words.begin(),words.end(),s.svm_nodes.data());s.svm_nodes.copy_to_device();}
};
