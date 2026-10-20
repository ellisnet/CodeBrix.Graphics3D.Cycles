// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "cycles_backend.h"
#include "scene_data.h"
#include "webgpu_device.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>
namespace cbc {
static constexpr char shader_source[]=
#include "backend_shader.inc"
;
struct SceneResources {
  CBCBackendStats stats{};
  Buffer geometry,camera,lights,paths,shadows,film,config,status,readback;
  BindGroup bindings;
  uint32_t uniform_stride;
  std::vector<uint32_t> batch_sizes;
  std::vector<float> staging;
};
class Backend {
 public:
  std::mutex mutex;
  Device device;
  BindLayout bindings;
  PipelineLayout layout;
  std::vector<Pipeline> pipelines;
  std::unique_ptr<SceneResources> scene;
  uint32_t batch_capacity;
  uint64_t renders=0,generation=0;
  explicit Backend(uint32_t batch):batch_capacity(batch){
    std::array<WGPUBindGroupLayoutEntry,8> entries{};
    for(unsigned i=0;i<entries.size();i++){
      auto &e=entries[i];e.binding=i;e.visibility=WGPUShaderStage_Compute;
      e.buffer.type=i==5?WGPUBufferBindingType_Uniform:((i==0||i==2||i==3)?WGPUBufferBindingType_ReadOnlyStorage:WGPUBufferBindingType_Storage);
      if(i==5){e.buffer.hasDynamicOffset=true;e.buffer.minBindingSize=16;}
    }
    WGPUBindGroupLayoutDescriptor bd{};bd.entryCount=entries.size();bd.entries=entries.data();device.push_scope();
    bindings=BindLayout(wgpuDeviceCreateBindGroupLayout(device.get(),&bd));device.pop_scope();
    WGPUPipelineLayoutDescriptor ld{};ld.bindGroupLayoutCount=1;ld.bindGroupLayouts=&bindings.value;device.push_scope();
    layout=PipelineLayout(wgpuDeviceCreatePipelineLayout(device.get(),&ld));device.pop_scope();
    WGPUShaderSourceWGSL source{};source.chain.sType=WGPUSType_ShaderSourceWGSL;source.code={shader_source,sizeof(shader_source)-1};
    WGPUShaderModuleDescriptor sd{};sd.nextInChain=&source.chain;device.push_scope();
    Handle<WGPUShaderModule,wgpuShaderModuleRelease> shader(wgpuDeviceCreateShaderModule(device.get(),&sd));device.pop_scope();
    for(const char *entry:{"main","shadow_resolve","accumulate"})pipelines.push_back(device.pipeline(shader,layout,entry));
  }
  void set_scene(const CBCBackendScene &source){
    device.check();SceneData data(source);auto next=std::make_unique<SceneResources>();auto &s=next->stats;
    s.struct_size=sizeof(s);s.width=data.width;s.height=data.height;s.samples=data.samples;s.bounces=data.bounces;
    const uint64_t pixels=uint64_t(s.width)*s.height;s.film_bytes=pixels*16;s.readback_bytes=s.film_bytes+16;
    s.batch_paths=std::min<uint64_t>(batch_capacity,pixels);
    const uint64_t tiles=(pixels+s.batch_paths-1)/s.batch_paths,batches=tiles*s.samples;
    if(batches>65536)throw Error(CBC_BACKEND_UNSUPPORTED,"Render exceeds current queue batch limit");
    s.batches=uint32_t(batches);s.dispatches=s.batches*3;
    next->uniform_stride=std::max(16u,device.limits.minUniformBufferOffsetAlignment);
    const size_t config_bytes=size_t(next->uniform_stride)*s.batches;
    if(config_bytes>0xffffffffu)throw Error(CBC_BACKEND_UNSUPPORTED,"Uniform offset range exceeded");
    std::vector<uint32_t> configs(config_bytes/4);
    for(uint32_t sample=0;sample<s.samples;sample++)for(uint32_t start=0;start<pixels;start+=s.batch_paths){
      const uint32_t count=std::min<uint64_t>(s.batch_paths,pixels-start);
      const size_t row=next->batch_sizes.size()*next->uniform_stride/4;
      configs[row]=uint32_t(sample*pixels+start);configs[row+1]=count;configs[row+2]=uint32_t(pixels);configs[row+3]=s.samples;
      next->batch_sizes.push_back(count);
    }
    const size_t path_bytes=size_t(s.batch_paths)*16*16,shadow_bytes=size_t(s.batch_paths)*s.bounces*32;
    s.scratch_bytes=path_bytes+shadow_bytes+config_bytes+16;
    s.scene_bytes=(data.geometry.size()+data.camera.size()+data.lights.size())*4;
    next->geometry=device.buffer(data.geometry.size()*4,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst,data.geometry.data());
    next->camera=device.buffer(data.camera.size()*4,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst,data.camera.data());
    next->lights=device.buffer(data.lights.size()*4,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst,data.lights.data());
    next->paths=device.buffer(path_bytes,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst);
    next->shadows=device.buffer(shadow_bytes,WGPUBufferUsage_Storage);
    next->film=device.buffer(s.film_bytes,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    next->config=device.buffer(config_bytes,WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst,configs.data());
    next->status=device.buffer(16,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    next->readback=device.buffer(s.readback_bytes,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    next->staging.resize(s.readback_bytes/4);
    std::array<WGPUBindGroupEntry,8> entries{};
    WGPUBuffer handles[]={next->camera,next->paths,next->geometry,next->lights,next->shadows,next->config,next->film,next->status};
    size_t sizes[]={data.camera.size()*4,path_bytes,data.geometry.size()*4,data.lights.size()*4,shadow_bytes,16,size_t(s.film_bytes),16};
    for(unsigned i=0;i<entries.size();i++){entries[i].binding=i;entries[i].buffer=handles[i];entries[i].size=sizes[i];}
    WGPUBindGroupDescriptor gd{};gd.layout=bindings;gd.entryCount=entries.size();gd.entries=entries.data();device.push_scope();
    next->bindings=BindGroup(wgpuDeviceCreateBindGroup(device.get(),&gd));device.pop_scope();
    s.scene_generation=++generation;scene=std::move(next);
  }
  void render(float *out,size_t count,CBCBackendStats *stats){
    device.check();if(!scene)throw Error(CBC_BACKEND_NOT_READY,"No scene loaded");
    auto &r=*scene;auto &s=r.stats;
    if(!out||count!=s.film_bytes/4||!stats||stats->struct_size!=sizeof(*stats))throw Error(CBC_BACKEND_INVALID,"Invalid output/statistics size");
    Handle<WGPUCommandEncoder,wgpuCommandEncoderRelease> encoder(wgpuDeviceCreateCommandEncoder(device.get(),nullptr));
    wgpuCommandEncoderClearBuffer(encoder,r.film,0,s.film_bytes);wgpuCommandEncoderClearBuffer(encoder,r.status,0,16);
    for(uint32_t batch=0;batch<s.batches;batch++){
      // Reused scratch must not let an incomplete dispatch pass on old results.
      wgpuCommandEncoderClearBuffer(encoder,r.paths,0,size_t(s.batch_paths)*16*16);
      for(auto &pipeline:pipelines){
      Handle<WGPUComputePassEncoder,wgpuComputePassEncoderRelease> pass(wgpuCommandEncoderBeginComputePass(encoder,nullptr));
      const uint32_t offset=batch*r.uniform_stride;
      wgpuComputePassEncoderSetPipeline(pass,pipeline);
      wgpuComputePassEncoderSetBindGroup(pass,0,r.bindings,1,&offset);
      wgpuComputePassEncoderDispatchWorkgroups(pass,(r.batch_sizes[batch]+63)/64,1,1);wgpuComputePassEncoderEnd(pass);
      }
    }
    wgpuCommandEncoderCopyBufferToBuffer(encoder,r.film,0,r.readback,0,s.film_bytes);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,r.status,0,r.readback,s.film_bytes,16);
    device.push_scope();Handle<WGPUCommandBuffer,wgpuCommandBufferRelease> command(wgpuCommandEncoderFinish(encoder,nullptr));device.pop_scope();
    wgpuQueueSubmit(device.get_queue(),1,&command.value);device.read(r.readback,r.staging.data(),s.readback_bytes);
    std::array<uint32_t,4> status;std::memcpy(status.data(),r.staging.data()+count,16);
    if(status[0]||status[1]!=s.batches||status[2]!=uint64_t(s.width)*s.height*s.samples)
      throw Error(CBC_BACKEND_GPU_ERROR,"Kernel output/completion validation failed (flags="+std::to_string(status[0])+
                  ", batches="+std::to_string(status[1])+", paths="+std::to_string(status[2])+")");
    for(size_t i=0;i<count;i++)if(!std::isfinite(r.staging[i]))throw Error(CBC_BACKEND_GPU_ERROR,"Nonfinite rendered image");
    std::memcpy(out,r.staging.data(),count*4);s.completed_renders=++renders;s.pipeline_creations=device.pipeline_creations;
    s.buffer_creations=device.buffer_creations;*stats=s;
  }
};
static void copy_text(const std::string &s,char *out,size_t capacity){if(out&&capacity){size_t n=std::min(capacity-1,s.size());std::memcpy(out,s.data(),n);out[n]=0;}}
template<class F>int protect(char *error,size_t capacity,F function)noexcept{
  try{function();copy_text("",error,capacity);return CBC_BACKEND_OK;}
  catch(const Error &e){copy_text(e.what(),error,capacity);return e.status;}
  catch(const std::exception &e){copy_text(e.what(),error,capacity);return CBC_BACKEND_GPU_ERROR;}
  catch(...){copy_text("Unknown native exception",error,capacity);return CBC_BACKEND_GPU_ERROR;}
}
}
struct CBCBackend {cbc::Backend impl;explicit CBCBackend(uint32_t batch):impl(batch){}};
extern "C" {
uint32_t cbc_backend_abi_version(){return CBC_BACKEND_ABI_VERSION;}
int cbc_backend_create(const CBCBackendConfig *config,CBCBackend **out,char *error,size_t capacity){
  if(out)*out=nullptr;
  return cbc::protect(error,capacity,[&]{
    if(!out||!config||config->struct_size!=sizeof(*config)||config->abi_version!=CBC_BACKEND_ABI_VERSION||config->reserved||
       config->batch_paths<64||config->batch_paths>65536)throw cbc::Error(CBC_BACKEND_INVALID,"Invalid backend configuration");
    auto backend=std::make_unique<CBCBackend>(config->batch_paths);*out=backend.release();});
}
void cbc_backend_destroy(CBCBackend *backend){delete backend;}
int cbc_backend_adapter(CBCBackend *b,char *out,size_t capacity){return cbc::protect(nullptr,0,[&]{
  if(!b||!out||!capacity)throw cbc::Error(CBC_BACKEND_INVALID,"Null text output");
  std::lock_guard lock(b->impl.mutex);cbc::copy_text(b->impl.device.adapter_name,out,capacity);});}
int cbc_backend_webgpu_library(CBCBackend *b,char *out,size_t capacity){return cbc::protect(nullptr,0,[&]{
  if(!b||!out||!capacity)throw cbc::Error(CBC_BACKEND_INVALID,"Null text output");
  std::lock_guard lock(b->impl.mutex);cbc::copy_text(b->impl.device.library_path,out,capacity);});}
int cbc_backend_set_scene(CBCBackend *b,const CBCBackendScene *scene,char *error,size_t capacity){
  return cbc::protect(error,capacity,[&]{if(!b||!scene)throw cbc::Error(CBC_BACKEND_INVALID,"Null backend/scene");
    std::lock_guard lock(b->impl.mutex);b->impl.set_scene(*scene);});}
int cbc_backend_render(CBCBackend *b,float *rgba,size_t count,CBCBackendStats *stats,char *error,size_t capacity){
  return cbc::protect(error,capacity,[&]{if(!b)throw cbc::Error(CBC_BACKEND_INVALID,"Null backend");
    std::lock_guard lock(b->impl.mutex);b->impl.render(rgba,count,stats);});}
}
