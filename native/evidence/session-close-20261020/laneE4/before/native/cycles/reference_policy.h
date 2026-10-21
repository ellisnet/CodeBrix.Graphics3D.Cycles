// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Validation reference policy (native/DEVICE.md, "Validation reference policy").
//
// A gate's pass/fail comparison uses a correctly rounded CPU reference. The Linux x64
// glibc build is that reference today: glibc's acosf/asinf (and the other libm functions
// the pinned Cycles code calls) are correctly rounded, and the WGSL kernels reproduce them.
// Android's bionic libm is not correctly rounded, so a CPU reference computed on an
// Android device differs from the correctly rounded one by bionic's error.
//
// Without either environment variable the gate behaves exactly as before: it compares the
// GPU with the CPU reference computed in the same process.
//
//   CODEBRIX_CYCLES_REFERENCE_EXPORT=DIR  write each independent CPU reference this gate
//                                         computes to DIR/<key>.f32 (a Linux x64 run).
//   CODEBRIX_CYCLES_REFERENCE_IMPORT=DIR  read DIR/<key>.f32 (produced by the Linux export
//                                         from the same sources and inputs) and use it as the
//                                         comparison reference. The locally computed CPU
//                                         reference is kept as a diagnostic only: its
//                                         deviation from the imported reference is written to
//                                         the diagnostic file and never decides pass/fail.
//
// Only references that are independent of the GPU (fixed inputs, whole CPU renders) are
// routed through here. Lockstep checks whose CPU input is the device's own GPU state cannot
// be produced elsewhere in advance and stay local (DEVICE.md lists them).
//
// The GPU consumes the scene tables the device's own host code prepared (light records,
// motion decompositions, camera data: Cycles' scene preparation also calls libm), so the
// reference must start from those same tables. The scene exchange moves them to Linux:
//
//   CODEBRIX_CYCLES_SCENE_DUMP=DIR    (device pass) write DIR/<key>.scene, the prepared scene
//                                     tables, KernelData and any fixed gate inputs, plus the
//                                     local CPU reference DIR/<key>.local.f32, then skip all
//                                     GPU work (prepare_only()).
//   CODEBRIX_CYCLES_SCENE_LOAD=DIR    (Linux pass, with ..._REFERENCE_EXPORT) replace the
//                                     locally prepared tables with DIR/<key>.scene before the
//                                     CPU reference runs, and skip all GPU work
//                                     (reference_only()).
//   CODEBRIX_CYCLES_SCENE_VERIFY=DIR  (device pass, with ..._REFERENCE_IMPORT) require this
//                                     run's local CPU reference to equal DIR/<key>.local.f32
//                                     bit for bit: the scene this run prepared feeds the
//                                     reference exactly as the dumped one did, so the imported
//                                     reference belongs to these inputs. (Raw table bytes are
//                                     not compared: some Cycles records carry uninitialized
//                                     padding.)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "wavefront.h"
#include "scene/devicescene.h"
namespace reference_policy {
inline const char *environment(const char *name){const char *value=std::getenv(name);return value&&*value?value:nullptr;}
inline int64_t ordered(float value){int32_t bits;std::memcpy(&bits,&value,4);return bits<0?int64_t(INT32_MIN)-bits:bits;}
// Units in the last place between two finite floats (0 for equal bits; +0/-0 count as equal).
inline uint64_t ulps(float a,float b){if(a==b)return 0;const int64_t d=ordered(a)-ordered(b);return uint64_t(d<0?-d:d);}
inline std::string escape(const std::string &text){std::string out;for(char c:text){if(c=='"'||c=='\\')out+='\\';out+=c;}return out;}
inline std::string read_file(const std::filesystem::path &path){
  std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Missing scene exchange file "+path.string());
  std::ostringstream text;text<<file.rdbuf();return text.str();
}
// Returns the reference the gate must compare against for KEY. DIAGNOSTIC is the file that
// receives the local-versus-imported record in import mode (one JSON object per key, appended).
inline std::vector<float> resolve(const std::string &key,const std::vector<float> &local,const std::string &diagnostic){
  const char *exported=environment("CODEBRIX_CYCLES_REFERENCE_EXPORT"),*imported=environment("CODEBRIX_CYCLES_REFERENCE_IMPORT");
  if(exported&&imported)throw std::runtime_error("Reference export and import are mutually exclusive");
  if(exported){
    std::filesystem::create_directories(exported);
    std::ofstream file(std::filesystem::path(exported)/(key+".f32"),std::ios::binary);
    file.write(reinterpret_cast<const char*>(local.data()),std::streamsize(local.size()*4));
    if(!file)throw std::runtime_error("Could not export reference "+key);
    return local;
  }
  if(!imported)return local;
  if(const char *verify=environment("CODEBRIX_CYCLES_SCENE_VERIFY")){
    const auto dumped=read_file(std::filesystem::path(verify)/(key+".local.f32"));
    if(dumped.size()!=local.size()*4||std::memcmp(dumped.data(),local.data(),dumped.size()))
      throw std::runtime_error("Local CPU reference differs from the scene-dump pass; the imported reference does not belong to this run's inputs: "+key);
  }
  const auto path=std::filesystem::path(imported)/(key+".f32");
  std::ifstream file(path,std::ios::binary);
  if(!file)throw std::runtime_error("Missing imported reference "+path.string());
  std::vector<float> reference(local.size());
  file.read(reinterpret_cast<char*>(reference.data()),std::streamsize(reference.size()*4));
  if(!file||file.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Imported reference size differs from the local reference: "+key);
  uint64_t differing=0,nonfinite=0,practical=0,strict=0,maximum_ulps=0;double maximum=0,maximum_ratio=0;size_t at=0;
  for(size_t i=0;i<local.size();i++){
    const float a=local[i],e=reference[i];
    if(std::memcmp(&a,&e,4)==0)continue;
    differing++;
    if(!std::isfinite(a)||!std::isfinite(e)){nonfinite++;continue;}
    const double error=std::abs(double(a)-e),bound=5e-5+2e-4*std::abs(double(e));
    practical+=error>bound;strict+=error>1e-7+1e-7*std::abs(double(e));
    maximum_ulps=std::max(maximum_ulps,ulps(a,e));
    if(error>maximum){maximum=error;at=i;}
    maximum_ratio=std::max(maximum_ratio,error/bound);
  }
  std::ofstream out(diagnostic,std::ios::app);
  out<<std::setprecision(17)<<"{\"key\":\""<<escape(key)<<"\",\"role\":\"diagnostic\",\"comparison\":\"local CPU reference (this device's C library) versus imported correctly rounded reference\""
     <<",\"importedReference\":\""<<escape(path.string())<<"\",\"floats\":"<<local.size()<<",\"bitwiseDiffering\":"<<differing<<",\"nonFinite\":"<<nonfinite
     <<",\"practicalExceedances\":"<<practical<<",\"strictDifferences\":"<<strict<<",\"maximumAbsoluteDeviation\":"<<maximum<<",\"maximumUlps\":"<<maximum_ulps
     <<",\"maximumPracticalRatio\":"<<maximum_ratio<<",\"maximumAt\":"<<at<<"}\n";
  if(!out)throw std::runtime_error("Could not write reference diagnostic "+diagnostic);
  return reference;
}
inline bool prepare_only(){return environment("CODEBRIX_CYCLES_SCENE_DUMP")!=nullptr;}
// Dump pass: keep the local CPU reference beside the scene (checked by ..._SCENE_VERIFY).
inline void record_local(const std::string &key,const std::vector<float> &local){
  const char *dump=environment("CODEBRIX_CYCLES_SCENE_DUMP");if(!dump)return;
  std::ofstream file(std::filesystem::path(dump)/(key+".local.f32"),std::ios::binary);
  file.write(reinterpret_cast<const char*>(local.data()),std::streamsize(local.size()*4));
  if(!file)throw std::runtime_error("Could not write local reference "+key);
}
inline bool reference_only(){return environment("CODEBRIX_CYCLES_SCENE_LOAD")!=nullptr;}
// Fixed gate inputs that travel with the scene (for example the emitter probe's path states,
// whose directions are normalized with the host's float3 arithmetic).
struct Input {std::string name;void *data;size_t bytes;};
inline void put(std::string &out,const std::string &name,const void *data,uint64_t element,uint64_t count){
  const uint64_t header[]={name.size(),element,count};out.append(reinterpret_cast<const char*>(header),sizeof(header));out+=name;
  out.append(static_cast<const char*>(data),size_t(element*count));
}
inline std::string serialize(ccl::DeviceScene &scene,const std::vector<Input> &inputs){
  std::string out="CBCSCENE1";
#define CBC_EXCHANGE_TABLE(name,type) put(out,#name,scene.name.data(),sizeof(*scene.name.data()),scene.name.size());
  CBC_SCENE_TABLES(CBC_EXCHANGE_TABLE) CBC_EXCHANGE_TABLE(svm_nodes,uint) CBC_EXCHANGE_TABLE(sample_pattern_lut,float)
#undef CBC_EXCHANGE_TABLE
  put(out,"KernelData",&scene.data,sizeof(scene.data),1);
  for(auto &input:inputs)put(out,"input."+input.name,input.data,1,input.bytes);
  return out;
}
// Call once per case after scene.device_update and before the CPU reference (and before the
// fixed INPUTS are uploaded to the GPU). Returns true when the gate must skip all GPU work.
inline bool exchange_scene(ccl::DeviceScene &scene,const std::string &key,const std::vector<Input> &inputs){
  const char *dump=environment("CODEBRIX_CYCLES_SCENE_DUMP"),*load=environment("CODEBRIX_CYCLES_SCENE_LOAD");
  if(dump&&load)throw std::runtime_error("Scene dump and load are mutually exclusive");
  if(dump){
    std::filesystem::create_directories(dump);const auto bytes=serialize(scene,inputs);
    std::ofstream file(std::filesystem::path(dump)/(key+".scene"),std::ios::binary);file.write(bytes.data(),std::streamsize(bytes.size()));
    if(!file)throw std::runtime_error("Could not write scene exchange "+key);
    return true;
  }
  if(!load)return false;
  const auto bytes=read_file(std::filesystem::path(load)/(key+".scene"));size_t at=9;
  if(bytes.compare(0,9,"CBCSCENE1"))throw std::runtime_error("Not a scene exchange file: "+key);
  auto take=[&](const std::string &name,void *target,uint64_t element,auto &&resize){
    uint64_t header[3];if(at+sizeof(header)>bytes.size())throw std::runtime_error("Truncated scene exchange "+key);
    std::memcpy(header,bytes.data()+at,sizeof(header));at+=sizeof(header);
    if(bytes.compare(at,header[0],name)||header[0]!=name.size())throw std::runtime_error("Scene exchange order differs at "+name);
    at+=header[0];if(header[1]!=element)throw std::runtime_error("Scene exchange element size differs for "+name);
    const uint64_t size=header[1]*header[2];if(at+size>bytes.size())throw std::runtime_error("Truncated scene exchange "+key);
    void *data=resize(header[2]);if(!data)data=target;if(size)std::memcpy(data,bytes.data()+at,size_t(size));at+=size;
  };
#define CBC_EXCHANGE_TABLE(name,type) take(#name,nullptr,sizeof(*scene.name.data()),[&](uint64_t count)->void*{if(scene.name.size()!=count)scene.name.alloc(count);return count?static_cast<void*>(scene.name.data()):nullptr;});
  CBC_SCENE_TABLES(CBC_EXCHANGE_TABLE) CBC_EXCHANGE_TABLE(svm_nodes,uint) CBC_EXCHANGE_TABLE(sample_pattern_lut,float)
#undef CBC_EXCHANGE_TABLE
  take("KernelData",&scene.data,sizeof(scene.data),[](uint64_t count)->void*{if(count!=1)throw std::runtime_error("KernelData count");return nullptr;});
  for(auto &input:inputs)take("input."+input.name,input.data,1,[&](uint64_t count)->void*{if(count!=input.bytes)throw std::runtime_error("Scene exchange input size differs: "+input.name);return nullptr;});
  if(at!=bytes.size())throw std::runtime_error("Scene exchange has trailing data: "+key);
  return true;
}
}
