// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "device/dummy/device.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace ccl {
static constexpr char control_source[]=
#include "controls_shader.inc"
;
std::string controls_wgsl(){return control_source;}
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static size_t aligned(size_t n){require(n<=UINT32_MAX-15,"WebGPU virtual address overflow");return (n+15)&~size_t(15);}
WebGPUDevice::WebGPUDevice(const DeviceInfo &i,Stats &s,Profiler &p,bool h):Device(i,s,p,h){
  std::array<WGPUBindGroupLayoutEntry,2> entries{};
  for(unsigned n=0;n<2;n++){entries[n].binding=n;entries[n].visibility=WGPUShaderStage_Compute;
    entries[n].buffer.type=n?WGPUBufferBindingType_Uniform:WGPUBufferBindingType_Storage;}
  WGPUBindGroupLayoutDescriptor bd{};bd.entryCount=2;bd.entries=entries.data();gpu.push_scope();
  bind_layout=cbc::BindLayout(wgpuDeviceCreateBindGroupLayout(gpu.get(),&bd));gpu.pop_scope();
  WGPUPipelineLayoutDescriptor pd{};pd.bindGroupLayoutCount=1;pd.bindGroupLayouts=&bind_layout.value;gpu.push_scope();
  pipeline_layout=cbc::PipelineLayout(wgpuDeviceCreatePipelineLayout(gpu.get(),&pd));gpu.pop_scope();
  WGPUShaderSourceWGSL source{};source.chain.sType=WGPUSType_ShaderSourceWGSL;source.code={control_source,sizeof(control_source)-1};
  WGPUShaderModuleDescriptor sd{};sd.nextInChain=&source.chain;gpu.push_scope();
  cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> shader(wgpuDeviceCreateShaderModule(gpu.get(),&sd));gpu.pop_scope();
  controls=gpu.pipeline(shader,pipeline_layout,"control");error_word=allocate(4);zero(error_word,4);
}
WebGPUDevice::~WebGPUDevice(){try{flush();}catch(const std::exception&){} }
WGPUCommandEncoder WebGPUDevice::commands(){if(!encoder.value)encoder=decltype(encoder)(wgpuDeviceCreateCommandEncoder(gpu.get(),nullptr));return encoder;}
void WebGPUDevice::flush(){if(!encoder.value)return;
  gpu.push_scope();cbc::Handle<WGPUCommandBuffer,wgpuCommandBufferRelease> command(wgpuCommandEncoderFinish(encoder,nullptr));gpu.pop_scope();
  wgpuQueueSubmit(gpu.get_queue(),1,&command.value);encoder={};submissions++;gpu.check();
}
uint32_t WebGPUDevice::allocate(size_t bytes){
  require(bytes>0,"Empty device allocation");const size_t extent=aligned(bytes);
  for(;;){
    for(auto it=free_ranges.begin();it!=free_ranges.end();++it)if(it->second>=extent){
      uint32_t address=it->first;size_t tail=it->second-extent;free_ranges.erase(it);
      if(tail)free_ranges.emplace(address+extent,tail);
      allocations.emplace(address,Allocation{bytes,extent});return address;
    }
    const size_t limit=std::min<uint64_t>(gpu.limits.maxBufferSize,gpu.limits.maxStorageBufferBindingSize)&~size_t(15);
    require(extent<=limit-16,"WebGPU allocation exceeds storage binding limit");
    require(arena_bytes<limit,"WebGPU device arena exhausted (storage binding limit)");
    size_t next=std::min(limit,std::max({size_t(65536),arena_bytes*2,arena_bytes+extent+16}));
    require(next>arena_bytes,"WebGPU device arena cannot grow");flush();
    auto replacement=gpu.buffer(next,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    if(arena.value){wgpuCommandEncoderCopyBufferToBuffer(commands(),arena,0,replacement,0,arena_bytes);flush();}
    const uint32_t old=arena_bytes?uint32_t(arena_bytes):16;size_t added=next-old;
    auto last=free_ranges.empty()?free_ranges.end():std::prev(free_ranges.end());
    if(last!=free_ranges.end()&&last->first+last->second==old)last->second+=added;
    else free_ranges.emplace(old,added);
    arena=std::move(replacement);arena_bytes=next;growths++;
  }
}
void WebGPUDevice::release(uint32_t address){
  auto it=allocations.find(address);require(it!=allocations.end(),"Free of unknown WebGPU pointer");
  auto [free_it,inserted]=free_ranges.emplace(address,it->second.extent);require(inserted,"Duplicate WebGPU free");allocations.erase(it);
  if(free_it!=free_ranges.begin()){auto previous=std::prev(free_it);if(previous->first+previous->second==free_it->first){previous->second+=free_it->second;free_ranges.erase(free_it);free_it=previous;}}
  auto next=std::next(free_it);if(next!=free_ranges.end()&&free_it->first+free_it->second==next->first){free_it->second+=next->second;free_ranges.erase(next);}
}
size_t WebGPUDevice::remaining(uint64_t address)const{
  require(address&&address<=UINT32_MAX,"Invalid WebGPU virtual pointer");auto it=allocations.upper_bound(uint32_t(address));
  require(it!=allocations.begin(),"Unknown WebGPU virtual pointer");--it;
  require(address-it->first<it->second.bytes,"WebGPU pointer outside allocation");return it->second.bytes-(address-it->first);
}
void WebGPUDevice::validate(uint64_t address,size_t bytes)const{require(bytes<=remaining(address),"WebGPU range outside allocation");}
void WebGPUDevice::upload(uint32_t address,const void *source,size_t bytes){
  if(!bytes)return;
  validate(address,bytes);require(address%4==0&&source,"Unaligned/null WebGPU upload");flush();
  std::vector<uint32_t> padded((bytes+3)/4);std::memcpy(padded.data(),source,bytes);
  wgpuQueueWriteBuffer(gpu.get_queue(),arena,address,padded.data(),padded.size()*4);uploads++;gpu.check();
  if(observer)observer->upload(address,padded.data(),padded.size()*4);
}
void WebGPUDevice::download(uint32_t address,void *target,size_t bytes){
  if(!bytes)return;
  validate(address,bytes);require(target,"Null WebGPU download");
  const uint32_t begin=address&~3u;size_t length=(address-begin+bytes+3)&~size_t(3);
  auto staging=gpu.buffer(length,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
  wgpuCommandEncoderCopyBufferToBuffer(commands(),arena,begin,staging,0,length);flush();
  std::vector<uint8_t> result(length);gpu.read(staging,result.data(),length);std::memcpy(target,result.data()+address-begin,bytes);downloads++;
  if(observer)observer->readback(address,target,bytes);
}
void WebGPUDevice::zero(uint32_t address,size_t bytes){if(!bytes)return;validate(address,bytes);require(address%4==0,"Unaligned WebGPU clear");wgpuCommandEncoderClearBuffer(commands(),arena,address,(bytes+3)&~size_t(3));if(observer)observer->clear(address,(bytes+3)&~size_t(3));}
void WebGPUDevice::sync(){
  uint32_t error=0;download(error_word,&error,4);
  if(error){
    std::string message="WebGPU scheduler kernel reported invalid data (mask "+std::to_string(error)+"):";
    for(auto [bit,label]:{std::pair{1u," invalid state/data"},std::pair{2u," unsupported SVM opcode"},
        std::pair{4u," SVM bytecode outside table"},std::pair{8u," invalid/uninitialized SVM stack"},
        std::pair{16u," unsupported SVM parameter"},std::pair{32u," SVM instruction limit"}})
      if(error&bit)message+=label;
    throw std::runtime_error(message);
  }
}
void WebGPUDevice::mem_alloc(device_memory &mem){guard([&]{require(mem.type!=MEM_IMAGE_TEXTURE,"WebGPU image textures not implemented");
  if(!mem.device_pointer&&mem.memory_size()){mem.device_pointer=allocate(mem.memory_size());mem.device_size=mem.memory_size();stats.mem_alloc(mem.device_size);}});}
void WebGPUDevice::mem_copy_to(device_memory &mem){guard([&]{mem_alloc(mem);require(!have_error(),"WebGPU allocation failed");
  if(mem.memory_size())upload(uint32_t(mem.device_pointer),mem.host_pointer,mem.memory_size());
  if(mem.type==MEM_GLOBAL)globals[mem.global_name()]=uint32_t(mem.device_pointer);});}
void WebGPUDevice::mem_copy_from(device_memory &mem,size_t y,size_t w,size_t h,size_t elem){guard([&]{
  require(w==0||elem<=SIZE_MAX/w,"Readback size overflow");size_t row=w*elem;
  require(!row||(y<=SIZE_MAX/row&&h<=SIZE_MAX/row),"Readback range overflow");size_t offset=y*row,bytes=h*row;
  require(offset<=mem.memory_size()&&bytes<=mem.memory_size()-offset&&mem.host_pointer,"Invalid Cycles readback range");
  if(bytes)download(mem.device_pointer+offset,static_cast<char*>(mem.host_pointer)+offset,bytes);});}
void WebGPUDevice::mem_zero(device_memory &mem){guard([&]{mem_alloc(mem);require(!have_error(),"WebGPU allocation failed");if(mem.memory_size())zero(uint32_t(mem.device_pointer),mem.memory_size());});}
void WebGPUDevice::mem_free(device_memory &mem){std::lock_guard lock(mutex);try{
  if(mem.device_pointer){flush();release(uint32_t(mem.device_pointer));stats.mem_free(mem.device_size);
    if(mem.type==MEM_GLOBAL)globals.erase(mem.global_name());
    mem.device_pointer=0;mem.device_size=0;}
  }catch(const std::exception &e){set_error(e.what());}}
void WebGPUDevice::mem_move_to_host(device_memory&){set_error("WebGPU host-memory fallback not implemented");}
void WebGPUDevice::const_copy_to(const char *name,void *host,size_t size){guard([&]{
  if(std::strcmp(name,"data")==0){require(size==sizeof(data),"KernelData layout mismatch");data=*static_cast<KernelData*>(host);}
  else if(std::strcmp(name,"integrator_state")==0){require(size==sizeof(state),"IntegratorStateGPU layout mismatch");update_state(*static_cast<IntegratorStateGPU*>(host));}
  else throw std::runtime_error("Unsupported WebGPU constant: "+std::string(name));
  auto &address=constants[name];if(address&&remaining(address)!=size){release(address);address=0;}
  if(!address)address=allocate(size);
  upload(address,host,size);
});}
void WebGPUDevice::dispatch(const std::array<uint32_t,32> &params,uint32_t work,WGPUComputePipeline pipeline){
  if(observer){
    const WGPUComputePipeline actual=pipeline?pipeline:controls.value;
    unsigned entry=actual==controls.value?0:actual==camera.value?1:actual==closest.value?2:actual==background.value?3:actual==surface.value?4:5;
    require(entry<5,"Recorder encountered an unknown pipeline");observer->dispatch(params,work,entry);
  }
  auto uniform=gpu.buffer(sizeof(params),WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst,params.data());
  std::array<WGPUBindGroupEntry,2> entries{};entries[0].binding=0;entries[0].buffer=arena;entries[0].size=arena_bytes;
  entries[1].binding=1;entries[1].buffer=uniform;entries[1].size=sizeof(params);
  WGPUBindGroupDescriptor bd{};bd.layout=bind_layout;bd.entryCount=2;bd.entries=entries.data();gpu.push_scope();
  cbc::BindGroup group(wgpuDeviceCreateBindGroup(gpu.get(),&bd));gpu.pop_scope();
  cbc::Handle<WGPUComputePassEncoder,wgpuComputePassEncoderRelease> pass(wgpuCommandEncoderBeginComputePass(commands(),nullptr));
  wgpuComputePassEncoderSetPipeline(pass,pipeline?pipeline:controls.value);wgpuComputePassEncoderSetBindGroup(pass,0,group,0,nullptr);
  wgpuComputePassEncoderDispatchWorkgroups(pass,(work+63)/64,1,1);wgpuComputePassEncoderEnd(pass);dispatches++;
}
unique_ptr<Device> device_webgpu_create(const DeviceInfo &i,Stats &s,Profiler &p,bool h){
  try{return make_unique<WebGPUDevice>(i,s,p,h);}catch(const std::exception &e){auto bad=i;bad.type=DEVICE_DUMMY;bad.error_msg=e.what();return device_dummy_create(bad,s,p,h);}}
void device_webgpu_info(vector<DeviceInfo> &devices){try{
  cbc::Device gpu;DeviceInfo info;info.type=DEVICE_WEBGPU;info.id="WEBGPU_"+gpu.adapter_name;info.description=gpu.adapter_name;
  info.has_gpu_queue=true;info.has_mnee_=false;info.has_execution_optimization=false;devices.push_back(info);
}catch(const std::exception&){} }
}
