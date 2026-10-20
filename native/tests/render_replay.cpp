// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Replays commands recorded from the native Cycles scheduler. Every scheduling
// readback is checked before later commands execute; expected bytes are never
// injected as replacement GPU results.
#include "webgpu_device.h"
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
static std::vector<char> read(const std::filesystem::path &path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read "+path.string());return {std::istreambuf_iterator<char>(in),{}};}
static void require(bool value,const std::string &message){if(!value)throw std::runtime_error(message);}
int main(int argc,char **argv){try{
  require(argc==3,"Usage: render_replay CAPTURE_DIRECTORY OUTPUT_PREFIX");std::filesystem::path input=argv[1];std::string output=argv[2];auto initial=read(input/"run.arena.bin");auto commands=read(input/"run.commands.bin");
  require(!initial.empty()&&initial.size()%4==0,"Invalid capture arena");cbc::Device d;auto arena=d.buffer(initial.size(),WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst|WGPUBufferUsage_CopySrc,initial.data());
  std::array<WGPUBindGroupLayoutEntry,2> entries{};for(unsigned i=0;i<2;i++){entries[i].binding=i;entries[i].visibility=WGPUShaderStage_Compute;entries[i].buffer.type=i?WGPUBufferBindingType_Uniform:WGPUBufferBindingType_Storage;}
  WGPUBindGroupLayoutDescriptor bd{};bd.entryCount=2;bd.entries=entries.data();d.push_scope();cbc::BindLayout layout(wgpuDeviceCreateBindGroupLayout(d.get(),&bd));d.pop_scope();
  WGPUPipelineLayoutDescriptor pd{};pd.bindGroupLayoutCount=1;pd.bindGroupLayouts=&layout.value;d.push_scope();cbc::PipelineLayout pl(wgpuDeviceCreatePipelineLayout(d.get(),&pd));d.pop_scope();
  const char *names[]={"controls","camera","closest","background","surface","surface","shadow","shadow","background","surface","volume_stack","volume","shadow","surface","volume"};const char *entry[]={"control","init_camera","intersect_closest","shade_background","shade_surface","prepare_surface","intersect_shadow","shade_shadow","shade_light_forward","prepare_shader_data","intersect_volume_stack","shade_volume","shade_shadow_transparent","shade_surface_light","shader_eval_volume_density"};std::array<cbc::Pipeline,15> pipelines;
  std::array<uint64_t,15> pipeline_dispatches{};
  cbc::Handle<WGPUCommandEncoder,wgpuCommandEncoderRelease> encoder;
  auto encode=[&](){if(!encoder.value)encoder=decltype(encoder)(wgpuDeviceCreateCommandEncoder(d.get(),nullptr));return encoder.value;};
  auto flush=[&](){if(!encoder.value)return;d.push_scope();cbc::Handle<WGPUCommandBuffer,wgpuCommandBufferRelease> command(wgpuCommandEncoderFinish(encoder,nullptr));d.pop_scope();wgpuQueueSubmit(d.get_queue(),1,&command.value);encoder={};d.check();};
  size_t cursor=0;uint64_t number=0,dispatches=0,readbacks=0,readback_bytes=0;auto take=[&](size_t bytes){require(bytes<=commands.size()-cursor,"Truncated command stream");const char *p=commands.data()+cursor;cursor+=bytes;return p;};
  auto range=[&](uint32_t address,uint32_t length){require(address%4==0&&uint64_t(address)+length<=initial.size(),"Command outside arena");};
  while(cursor<commands.size()){
    std::array<uint32_t,3> h{};std::memcpy(h.data(),take(12),12);number++;auto [kind,address,length]=h;
    if(kind==1){range(address,length);require(length%4==0,"Unaligned upload");auto bytes=take(length);flush();wgpuQueueWriteBuffer(d.get_queue(),arena,address,bytes,length);}
    else if(kind==2){range(address,length);require(length%4==0,"Unaligned clear");wgpuCommandEncoderClearBuffer(encode(),arena,address,length);}
    else if(kind==3||kind==5){
      uint32_t workgroup_size=64;if(kind==5)std::memcpy(&workgroup_size,take(4),4);
      require(workgroup_size>0&&workgroup_size<=d.limits.maxComputeWorkgroupSizeX&&
              workgroup_size<=d.limits.maxComputeInvocationsPerWorkgroup,"Invalid workgroup size");
      const uint64_t groups=(uint64_t(length)+workgroup_size-1)/workgroup_size;
      require(address<pipelines.size()&&names[address]&&entry[address]&&length>0&&groups<=d.limits.maxComputeWorkgroupsPerDimension,"Invalid dispatch");auto params=take(128);
      if(!pipelines[address].value){
        auto started=std::chrono::steady_clock::now();std::cerr<<"Creating pipeline "<<entry[address]<<" at command "<<number<<std::endl;
        auto source=read(input/(std::string(names[address])+".wgsl"));WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={source.data(),source.size()};WGPUShaderModuleDescriptor sd{};sd.nextInChain=&wgsl.chain;d.push_scope();cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.get(),&sd));d.pop_scope();pipelines[address]=d.pipeline(module,pl,entry[address]);
        std::cerr<<"Created pipeline "<<entry[address]<<" in "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<" ms"<<std::endl;
      }
      auto uniform=d.buffer(128,WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst,params);std::array<WGPUBindGroupEntry,2> bindings{};bindings[0].binding=0;bindings[0].buffer=arena;bindings[0].size=initial.size();bindings[1].binding=1;bindings[1].buffer=uniform;bindings[1].size=128;
      WGPUBindGroupDescriptor gd{};gd.layout=layout;gd.entryCount=2;gd.entries=bindings.data();d.push_scope();cbc::BindGroup group(wgpuDeviceCreateBindGroup(d.get(),&gd));d.pop_scope();
      cbc::Handle<WGPUComputePassEncoder,wgpuComputePassEncoderRelease> pass(wgpuCommandEncoderBeginComputePass(encode(),nullptr));wgpuComputePassEncoderSetPipeline(pass,pipelines[address]);wgpuComputePassEncoderSetBindGroup(pass,0,group,0,nullptr);wgpuComputePassEncoderDispatchWorkgroups(pass,uint32_t(groups),1,1);wgpuComputePassEncoderEnd(pass);dispatches++;pipeline_dispatches[address]++;
    }
    else if(kind==4){range(address,length);const char *expected=take(length);size_t padded=(size_t(length)+3)&~size_t(3);require(uint64_t(address)+padded<=initial.size(),"Readback past arena");auto staging=d.buffer(padded,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);wgpuCommandEncoderCopyBufferToBuffer(encode(),arena,address,staging,0,padded);flush();std::vector<char> actual(padded);d.read(staging,actual.data(),padded);
      require(!std::memcmp(actual.data(),expected,length),"Native scheduler decision readback differs at command "+std::to_string(number)+" address="+std::to_string(address));readbacks++;readback_bytes+=length;
    }else throw std::runtime_error("Unknown command kind");
  }
  auto staging=d.buffer(initial.size(),WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);wgpuCommandEncoderCopyBufferToBuffer(encode(),arena,0,staging,0,initial.size());flush();std::vector<char> actual(initial.size());d.read(staging,actual.data(),actual.size());d.check();std::ofstream file(output+".arena.bin",std::ios::binary);file.write(actual.data(),actual.size());require(bool(file),"Cannot save replay arena");
  std::ofstream(output+".adapter.txt")<<d.adapter_name;std::ofstream(output+".library.txt")<<d.library_path;
  std::cout<<"{\"passed\":true,\"nativeCommandReplay\":true,\"commands\":"<<number<<",\"dispatches\":"<<dispatches<<",\"exactSchedulingReadbacks\":"<<readbacks<<",\"exactSchedulingReadbackBytes\":"<<readback_bytes<<",\"pipelineDispatches\":[";
  for(size_t i=0;i<pipeline_dispatches.size();i++){if(i)std::cout<<',';std::cout<<pipeline_dispatches[i];}std::cout<<"]}\n";
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
