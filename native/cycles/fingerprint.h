// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Fingerprint mode (maintainer validation tool, native/tools/fingerprint.py): CODEBRIX_CYCLES_FINGERPRINT=FILE.
// The process prepares the scene exactly as a render does, on this device with its own arithmetic, and hashes what
// decides the render instead of rendering it. A process with the variable unset never creates this object: every
// hook in the device is one null-pointer test, so a normal render compiles and uploads what it always did.
//
// CODEBRIX_CYCLES_FINGERPRINT_KIND=render (default; the host and the oracle runner) hashes three logs:
//  - modules: the text every pipeline of the visit below would compile, comments removed, as a sorted set;
//  - data: every upload and clear (address, size, content) up to the first integrator dispatch, and the
//    scene-preparation kernels' dispatches (background map, displacement, volume density);
//  - commands: every upload, clear and dispatch (entry, workgroup size, work, push words) of a walk of the native
//    scheduler that executes nothing, driven by a fixed synthetic readback script (the visit).
// The scene-preparation kernels are the one thing compiled and executed for real: their output is uploaded scene data
// and decides the specialization of later modules, so neither can be known without them.
// CODEBRIX_CYCLES_FINGERPRINT_KIND=prefix (gate binaries, whose steps depend on what they read back) stops at the first
// pipeline after the control module: uploads so far, the production WGSL sources' hash and the first entry's name.
#include "../oracle/sha256.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include "webgpu.h"

namespace ccl {
struct WebGPUFingerprint {
  enum Kind {Render,Prefix};
  Kind kind=Render;
  std::string path,adapter,driver,shader_sources,pipeline_cache;
  std::recursive_mutex mutex;
  std::string data_log,command_log,module_detail;
  std::vector<std::string> module_lines;
  std::vector<std::string> refusals,evaluations;
  std::map<WGPUComputePipeline,std::string> names;
  // The synthetic readback script (WebGPUDevice::fingerprint_begin_visit): one (kernel, work) per queue counter readback.
  std::vector<std::pair<int,int>> visit;
  size_t visit_step=0;
  bool visit_ready=false,integrator_started=false,preparing=false;
  // Nothing has run since the last scene-preparation kernel: readbacks need no round trip to the GPU.
  bool dry_phase=false;
  uint64_t uploads=0,upload_bytes=0,clears=0,dispatches=0,compiled=0;
  std::chrono::steady_clock::time_point begin=std::chrono::steady_clock::now(),evaluation_begin;
  std::string last_code,last_sha,label;
  size_t mask_stride=0,mask_begin=0,mask_end=0;

  static WebGPUFingerprint *instance(){
    static WebGPUFingerprint *value=[]()->WebGPUFingerprint*{
      const char *file=std::getenv("CODEBRIX_CYCLES_FINGERPRINT");if(!file||!*file)return nullptr;
      auto *f=new WebGPUFingerprint;f->path=file;
      const char *kind=std::getenv("CODEBRIX_CYCLES_FINGERPRINT_KIND");f->kind=kind&&std::strcmp(kind,"prefix")==0?Prefix:Render;
      return f;}();
    return value;
  }
  static std::string text(const std::string &value){
    std::string out;for(char c:value){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(uint8_t(c)>=32)out+=c;else out+=' ';}return out;
  }
  // WGSL has no string literals, so "//" always starts a comment; comments cannot change what is compiled.
  static std::string without_comments(const std::string &code){
    std::string out;out.reserve(code.size());
    for(size_t i=0;i<code.size();){
      if(code.compare(i,2,"//")==0){while(i<code.size()&&code[i]!='\n')i++;continue;}
      out+=code[i++];
    }
    return out;
  }
  std::string &stream(){return integrator_started?command_log:data_log;}
  void upload(uint32_t address,const void *data,size_t bytes){
    std::lock_guard lock(mutex);uploads++;upload_bytes+=bytes;
    // A record's padding that the scene code copies from the stack (mask, set by mem_copy_to): the hash does not see it, the upload is untouched.
    std::vector<uint8_t> masked;
    if(mask_stride){masked.assign(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+bytes);for(size_t i=0;i+mask_stride<=bytes;i+=mask_stride)std::memset(masked.data()+i+mask_begin,0,mask_end-mask_begin);data=masked.data();mask_stride=0;}
    // Diagnosis of a changed hash: every upload as DIR/<sequence>-<address>.bin.
    if(const char *dir=std::getenv("CODEBRIX_CYCLES_FINGERPRINT_DUMP"))std::ofstream(std::string(dir)+"/"+std::to_string(uploads)+"-"+std::to_string(address)+".bin",std::ios::binary).write(static_cast<const char*>(data),std::streamsize(bytes));
    stream()+="U "+std::to_string(address)+' '+std::to_string(bytes)+' '+oracle::sha256(data,bytes)+' '+label+'\n';label.clear();
  }
  void clear(uint32_t address,size_t bytes){
    std::lock_guard lock(mutex);clears++;stream()+="Z "+std::to_string(address)+' '+std::to_string(bytes)+'\n';
  }
  // True when the dispatch really runs: a scene-preparation kernel, or anything before a prefix run stops.
  bool dispatch(WGPUComputePipeline pipeline,const std::array<uint32_t,32> &params,uint32_t work,uint32_t workgroup_size,bool control){
    std::lock_guard lock(mutex);dispatches++;
    auto it=names.find(pipeline);const std::string entry=control?"control":it==names.end()?"unknown":it->second;
    const bool real=preparing||kind==Prefix;if(real)dry_phase=false;else{integrator_started=true;dry_phase=true;}
    std::string line="D "+entry+' '+std::to_string(workgroup_size)+' '+std::to_string(work);
    for(uint32_t word:params)line+=' '+std::to_string(word);
    stream()+=line+'\n';return real;
  }
  void module(const char *name,const char *entry,uint32_t workgroup_size,const std::string &code,const std::string &features){
    std::lock_guard lock(mutex);
    if(code!=last_code){last_code=code;const std::string bare=without_comments(code);last_sha=oracle::sha256(bare.data(),bare.size());}
    module_lines.push_back(std::string(name)+' '+entry+' '+std::to_string(workgroup_size)+' '+last_sha);
    module_detail+=module_lines.back()+' '+features+'\n';
  }
  std::string modules_hash() const{
    std::vector<std::string> lines=module_lines;std::sort(lines.begin(),lines.end());lines.erase(std::unique(lines.begin(),lines.end()),lines.end());
    std::string all;for(const auto &line:lines)all+=line+'\n';
    return oracle::sha256(all.data(),all.size());
  }
  static std::string list(const std::vector<std::string> &items){
    std::string out="[";for(size_t i=0;i<items.size();i++){if(i)out+=',';out+='"'+text(items[i])+'"';}return out+']';
  }
  void finish(const char *stopped_at=nullptr){
    std::lock_guard lock(mutex);
    if(stopped_at)data_log+="S "+std::string(stopped_at)+' '+shader_sources+'\n';
    std::string refused;for(const auto &line:refusals)refused+=line+'\n';
    const std::string commands=command_log+refused;
    const std::string m=modules_hash(),d=oracle::sha256(data_log.data(),data_log.size()),c=oracle::sha256(commands.data(),commands.size());
    const std::string joined=std::string("cycles-fingerprint-v1 ")+(kind==Prefix?"prefix ":"render ")+m+' '+d+' '+c;
    std::ofstream log(path+".log",std::ios::binary);log<<"# modules\n"<<module_detail<<"# data\n"<<data_log<<"# commands\n"<<commands;
    std::ofstream out(path,std::ios::binary);
    out<<"{\"format\":\"cycles-fingerprint-v1\",\"kind\":\""<<(kind==Prefix?"prefix":"render")<<"\",\"fingerprint\":\""<<oracle::sha256(joined.data(),joined.size())
       <<"\",\"modulesSha256\":\""<<m<<"\",\"dataSha256\":\""<<d<<"\",\"commandsSha256\":\""<<c<<"\",\"modules\":"<<module_lines.size()
       <<",\"uploads\":"<<uploads<<",\"uploadBytes\":"<<upload_bytes<<",\"clears\":"<<clears<<",\"dispatches\":"<<dispatches<<",\"pipelinesCompiled\":"<<compiled
       <<",\"sceneEvaluations\":"<<list(evaluations)<<",\"refusals\":"<<list(refusals)<<",\"stoppedAt\":\""<<(stopped_at?stopped_at:"")<<"\",\"driver\":\""<<text(driver)<<"\",\"adapter\":"<<(adapter.empty()?"null":adapter)<<",\"pipelineCache\":"<<(pipeline_cache.empty()?"null":pipeline_cache)
       <<",\"seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<"}\n";
  }
};
}
