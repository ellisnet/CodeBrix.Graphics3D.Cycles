// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Measurement-only probe: creates one compute pipeline from a WGSL file with
// the unchanged shipped runtime and reports wall time and process peak memory.
// Usage: compile_probe FILE.wgsl ENTRY [checks]
//   checks: "default" (wgpuDeviceCreateShaderModule), or a trusted-module
//   runtime-check mask for attribution experiments only (never production).
#include "webgpu_device.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
static long status_kb(const char *key){std::ifstream in("/proc/self/status");std::string line;
  while(std::getline(in,line))if(line.rfind(key,0)==0)return std::atol(line.c_str()+std::strlen(key));return -1;}
int main(int argc,char **argv){
  if(argc<3){std::cerr<<"Usage: compile_probe FILE ENTRY [default|MASK]\n";return 2;}
  std::string mode=argc>3?argv[3]:"default";
  try{
    std::ifstream in(argv[1],std::ios::binary);if(!in)throw std::runtime_error("Cannot read WGSL");
    std::string source{std::istreambuf_iterator<char>(in),{}};
    cbc::Device d;const long base_hwm=status_kb("VmHWM:"),base_rss=status_kb("VmRSS:");
    std::array<WGPUBindGroupLayoutEntry,2> entries{};for(unsigned i=0;i<2;i++){entries[i].binding=i;entries[i].visibility=WGPUShaderStage_Compute;entries[i].buffer.type=i?WGPUBufferBindingType_Uniform:WGPUBufferBindingType_Storage;}
    WGPUBindGroupLayoutDescriptor bd{};bd.entryCount=2;bd.entries=entries.data();cbc::BindLayout layout(wgpuDeviceCreateBindGroupLayout(d.get(),&bd));
    WGPUPipelineLayoutDescriptor pd{};pd.bindGroupLayoutCount=1;pd.bindGroupLayouts=&layout.value;cbc::PipelineLayout pl(wgpuDeviceCreatePipelineLayout(d.get(),&pd));
    std::cerr<<"Creating module for "<<argv[2]<<std::endl;
    auto t0=std::chrono::steady_clock::now();
    WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={source.data(),source.size()};
    WGPUShaderModuleDescriptor sd{};sd.nextInChain=&wgsl.chain;d.push_scope();
    cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(mode=="default"?wgpuDeviceCreateShaderModule(d.get(),&sd):
      wgpuDeviceCreateShaderModuleTrusted(d.get(),&sd,WGPUShaderRuntimeChecks(std::strtoull(mode.c_str(),nullptr,0))));d.pop_scope();
    auto t1=std::chrono::steady_clock::now();const long module_hwm=status_kb("VmHWM:");
    std::cerr<<"Creating pipeline "<<argv[2]<<std::endl;
    auto pipeline=d.pipeline(module,pl,argv[2]);
    auto t2=std::chrono::steady_clock::now();
    auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
    std::cout<<"{\"entry\":\""<<argv[2]<<"\",\"mode\":\""<<mode<<"\",\"wgslBytes\":"<<source.size()<<",\"moduleMs\":"<<ms(t0,t1)<<",\"pipelineMs\":"<<ms(t1,t2)
      <<",\"baseVmHWMkB\":"<<base_hwm<<",\"baseVmRSSkB\":"<<base_rss<<",\"moduleVmHWMkB\":"<<module_hwm<<",\"peakVmHWMkB\":"<<status_kb("VmHWM:")
      <<",\"adapter\":\""<<d.adapter_name<<"\",\"passed\":true}"<<std::endl;
    return 0;
  }catch(const std::exception &e){std::cout<<"{\"entry\":\""<<argv[2]<<"\",\"passed\":false,\"error\":\""<<e.what()<<"\",\"peakVmHWMkB\":"<<status_kb("VmHWM:")<<"}"<<std::endl;return 1;}
}
