// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "wavefront_test.h"
#include "wavefront.h"
#include "svm_surface_reference.h"
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
      "if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}\n"
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
static cbc::Pipeline pipeline(WebGPUDevice &d,const std::string &source){
  WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={source.data(),source.size()};
  WGPUShaderModuleDescriptor desc{};desc.nextInChain=&wgsl.chain;d.gpu.push_scope();
  cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&desc));d.gpu.pop_scope();
  d.surface_geometry=d.gpu.pipeline(module,d.pipeline_layout,"prepare_shader_data");
  return d.gpu.pipeline(module,d.pipeline_layout,"surface_svm_probe");
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
static void evaluate(WebGPUDevice &d,Scene &scene,cbc::Pipeline &shader,const std::vector<Field> &layout,
    const std::vector<IntegratorStateCPU> &states,const std::vector<int> &ids,const std::string &label,Metrics &m,const std::string &capture="",bool dense=false){
  auto expected=original_surface_svm(scene.dscene,ids,states);upload(d,layout,states);
  device_vector<int> selected(&d,"svm_surface_indices",MEM_READ_WRITE);selected.alloc(ids.size());std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
  device_vector<uint> output(&d,"svm_surface_output",MEM_READ_WRITE);output.alloc(16+ids.size()*sizeof(SurfaceSvmRecord)/4);std::fill_n(output.data(),output.size(),0xa5a5a5a5u);output.copy_to_device();
  device_vector<uint> scratch(&d,"svm_surface_closures",MEM_READ_WRITE);scratch.alloc(16+ids.size()*MAX_CLOSURE*sizeof(SurfaceClosureRecord)/4);std::fill_n(scratch.data(),scratch.size(),0xa5a5a5a5u);std::fill(scratch.data()+8,scratch.data()+scratch.size()-8,0);scratch.copy_to_device();
  std::array<uint32_t,32> p{};p[1]=ids.size();p[2]=dense?0:selected.device_pointer;p[4]=output.device_pointer+32;p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[10]=states.size();p[14]=d.scene_tables();p[17]=d.error_word;p[21]=scene.dscene.svm_nodes.device_pointer;p[22]=scene.dscene.svm_nodes.size();p[23]=scratch.device_pointer+32;
  PreparedSurfaceCache cache(d,p);
  if(!capture.empty()){
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
  for(size_t i=0;i<ids.size();i++)require(!std::memcmp(sb+32+i*MAX_CLOSURE*sizeof(SurfaceClosureRecord),bytes+32+i*sizeof(SurfaceSvmRecord)+offsetof(SurfaceSvmRecord,closures),MAX_CLOSURE*sizeof(SurfaceClosureRecord)),label+" closure scratch mismatch");
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
