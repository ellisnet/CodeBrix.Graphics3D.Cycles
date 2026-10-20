// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// GPU kernel replay only: scene/state preparation still runs in the Linux host.
#include "webgpu_device.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
static std::vector<char> read(const char *path){
  std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read replay input");
  return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
int main(int argc,char **argv){try{
  if(argc!=7&&argc!=8)throw std::runtime_error("Usage: arena_replay SHADER ARENA PARAMS OUTPUT ENTRY REPORT_PREFIX [WORKGROUP_SIZE]");
  auto source=read(argv[1]);auto arena=read(argv[2]);auto params=read(argv[3]);
  if(params.size()!=128||arena.empty()||arena.size()%4)throw std::runtime_error("Invalid replay buffer lengths");
  std::array<uint32_t,32> words{};std::memcpy(words.data(),params.data(),params.size());cbc::Device d;
  uint64_t workgroup_size=64;
  if(argc==8){size_t used=0;workgroup_size=std::stoull(argv[7],&used);if(used!=std::strlen(argv[7]))throw std::runtime_error("Invalid workgroup size");}
  if(!workgroup_size||workgroup_size>d.limits.maxComputeWorkgroupSizeX||workgroup_size>d.limits.maxComputeInvocationsPerWorkgroup)
    throw std::runtime_error("Invalid workgroup size");
  const uint64_t groups=(uint64_t(words[1])+workgroup_size-1)/workgroup_size;
  if(!groups||groups>d.limits.maxComputeWorkgroupsPerDimension)throw std::runtime_error("Invalid dispatch");
  auto storage=d.buffer(arena.size(),WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst|WGPUBufferUsage_CopySrc,arena.data());
  auto uniform=d.buffer(params.size(),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst,params.data());
  auto result=d.buffer(arena.size(),WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
  std::array<WGPUBindGroupLayoutEntry,2> entries{};
  for(unsigned i=0;i<2;i++){entries[i].binding=i;entries[i].visibility=WGPUShaderStage_Compute;entries[i].buffer.type=i?WGPUBufferBindingType_Uniform:WGPUBufferBindingType_Storage;}
  WGPUBindGroupLayoutDescriptor bd{};bd.entryCount=2;bd.entries=entries.data();d.push_scope();
  cbc::BindLayout layout(wgpuDeviceCreateBindGroupLayout(d.get(),&bd));d.pop_scope();
  WGPUPipelineLayoutDescriptor pd{};pd.bindGroupLayoutCount=1;pd.bindGroupLayouts=&layout.value;d.push_scope();
  cbc::PipelineLayout pl(wgpuDeviceCreatePipelineLayout(d.get(),&pd));d.pop_scope();
  WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={source.data(),source.size()};
  WGPUShaderModuleDescriptor sd{};sd.nextInChain=&wgsl.chain;d.push_scope();
  cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.get(),&sd));d.pop_scope();
  auto pipeline=d.pipeline(module,pl,argv[5]);std::array<WGPUBindGroupEntry,2> bindings{};
  bindings[0].binding=0;bindings[0].buffer=storage;bindings[0].size=arena.size();bindings[1].binding=1;bindings[1].buffer=uniform;bindings[1].size=params.size();
  WGPUBindGroupDescriptor gd{};gd.layout=layout;gd.entryCount=2;gd.entries=bindings.data();d.push_scope();
  cbc::BindGroup group(wgpuDeviceCreateBindGroup(d.get(),&gd));d.pop_scope();
  cbc::Handle<WGPUCommandEncoder,wgpuCommandEncoderRelease> encoder(wgpuDeviceCreateCommandEncoder(d.get(),nullptr));
  cbc::Handle<WGPUComputePassEncoder,wgpuComputePassEncoderRelease> pass(wgpuCommandEncoderBeginComputePass(encoder,nullptr));
  wgpuComputePassEncoderSetPipeline(pass,pipeline);wgpuComputePassEncoderSetBindGroup(pass,0,group,0,nullptr);
  wgpuComputePassEncoderDispatchWorkgroups(pass,uint32_t(groups),1,1);wgpuComputePassEncoderEnd(pass);
  wgpuCommandEncoderCopyBufferToBuffer(encoder,storage,0,result,0,arena.size());d.push_scope();
  cbc::Handle<WGPUCommandBuffer,wgpuCommandBufferRelease> command(wgpuCommandEncoderFinish(encoder,nullptr));d.pop_scope();
  wgpuQueueSubmit(d.get_queue(),1,&command.value);std::vector<char> output(arena.size());d.read(result,output.data(),output.size());d.check();
  std::ofstream file(argv[4],std::ios::binary);file.write(output.data(),output.size());if(!file)throw std::runtime_error("Cannot write replay result");
  std::ofstream(std::string(argv[6])+".adapter.txt")<<d.adapter_name;std::ofstream(std::string(argv[6])+".library.txt")<<d.library_path;
  std::cout<<"{\"passed\":true,\"gpuKernelReplayOnly\":true,\"paths\":"<<words[1]<<",\"arenaBytes\":"<<arena.size()<<"}\n";
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
