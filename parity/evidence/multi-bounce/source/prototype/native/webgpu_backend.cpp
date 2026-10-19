// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "cycles_webgpu.h"
#include "webgpu.h"
#include "wgpu.h"
#include "codebrix_wgpu.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
template<class T, auto Release> struct Owned {
  T value{};
  Owned() = default;
  explicit Owned(T v):value(v) {}
  ~Owned() { if(value) Release(value); }
  Owned(const Owned&) = delete;
  Owned& operator=(const Owned&) = delete;
  Owned(Owned&& o) noexcept: value(o.value) { o.value=nullptr; }
  operator T() const { return value; }
};
using Buffer = Owned<WGPUBuffer,wgpuBufferRelease>;
using BindGroup = Owned<WGPUBindGroup,wgpuBindGroupRelease>;
using Pipeline = Owned<WGPUComputePipeline,wgpuComputePipelineRelease>;
static std::string str(WGPUStringView s) { return s.data?std::string(s.data,s.length):""; }
static void copy_text(const std::string &s,char *out,size_t capacity) {
  if(out && capacity) { const size_t n=std::min(capacity-1,s.size()); std::memcpy(out,s.data(),n); out[n]=0; }
}
static constexpr const char shader_source[] =
#include "render_shader.inc"
;
static constexpr size_t MAX_SCENE_BYTES=64*1024*1024;

// Validate the prototype format before allocating GPU resources. In particular,
// reject cyclic/deep BVHs and references outside each table, including instances.
static void validate_scene(const void *data,size_t bytes) {
  if(!data || bytes<16*8 || bytes%16 || bytes>MAX_SCENE_BYTES) throw std::runtime_error("Invalid scene byte count");
  const auto *raw=static_cast<const unsigned char*>(data);
  auto f=[&](size_t row,size_t component) { float v; std::memcpy(&v,raw+row*16+component*4,4); return v; };
  auto integer=[&](size_t row,size_t component) { int32_t v; std::memcpy(&v,raw+row*16+component*4,4); return v; };
  auto number=[&](size_t row,size_t component) -> size_t {
    const float v=f(row,component);
    if(!std::isfinite(v)||v<0||v>4194304||v!=std::floor(v)) throw std::runtime_error("Invalid scene metadata");
    return static_cast<size_t>(v);
  };
  const size_t triangles=number(0,0), leaves=number(0,1), indices=number(0,2), vertices=number(0,3), rows=bytes/16;
  if(!triangles || leaves<5 || (leaves-1)%4 || indices<=leaves || vertices<=indices ||
     vertices>=rows || triangles>(rows-vertices)/4 || vertices+4*triangles!=rows)
    throw std::runtime_error("Invalid scene table layout");
  for(size_t i=vertices;i<rows;i++) for(int j=0;j<4;j++)
    if(!std::isfinite(f(i,j))) throw std::runtime_error("Nonfinite scene values");
  for(size_t i=vertices+3*triangles;i<rows;i++) for(int j=0;j<3;j++)
    if(f(i,j)<0 || f(i,j)>1) throw std::runtime_error("Diffuse albedo outside [0,1]");
  std::vector<uint8_t> visiting(leaves);
  std::function<void(int32_t,unsigned)> check=[&](int32_t node,unsigned depth) {
    if(depth>=190) throw std::runtime_error("BVH exceeds prototype stack capacity");
    if(node>=0) {
      size_t n=static_cast<size_t>(node);
      if(n<1 || (n-1)%4 || n+3>=leaves) throw std::runtime_error("Invalid BVH node offset");
      if(visiting[n]) throw std::runtime_error("Cyclic/shared BVH node outside prototype scope");
      visiting[n]=1;
      for(int axis=1;axis<=3;axis++) for(int c=0;c<4;c++)
        if(!std::isfinite(f(n+axis,c))) throw std::runtime_error("Nonfinite BVH bounds");
      check(integer(n,2),depth+1); check(integer(n,3),depth+1);
    } else {
      const int64_t leaf=-int64_t(node)-1;
      if(leaf>=int64_t(indices-leaves)) throw std::runtime_error("Invalid leaf offset");
      size_t row=leaves+size_t(leaf);
      int32_t first=integer(row,0),last=integer(row,1),type=integer(row,3);
      if(first<0 || last<first || uint64_t(last)>vertices-indices || (type&0xffff)!=1)
        throw std::runtime_error("Unsupported BVH leaf or primitive type");
      for(int32_t i=first;i<last;i++) if(number(indices+i,0)>=triangles)
        throw std::runtime_error("Invalid triangle index");
    }
  };
  check(1,0);
}
}

struct CBCContext {
  std::mutex mutex;
  // Callback state must outlive all WebGPU handles during destruction.
  std::string adapter_name, failure;
  bool done=false;
  Owned<WGPUInstance,wgpuInstanceRelease> instance;
  Owned<WGPUAdapter,wgpuAdapterRelease> adapter;
  Owned<WGPUDevice,wgpuDeviceRelease> device;
  Owned<WGPUQueue,wgpuQueueRelease> queue;
  Owned<WGPUBindGroupLayout,wgpuBindGroupLayoutRelease> bindings;
  Owned<WGPUPipelineLayout,wgpuPipelineLayoutRelease> layout;
  std::vector<Pipeline> pipelines;
  std::unique_ptr<Buffer> scene;
  size_t scene_bytes=0;

  void wait(bool gpu) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    while(!done && failure.empty() && std::chrono::steady_clock::now()<end) {
      if(gpu) {
        WGPUCodebrixPollResult result;
        if(wgpuCodebrixDevicePoll(device,true,1000000000,nullptr,&result)!=WGPUStatus_Success &&
           result!=WGPUCodebrixPollResult_Timeout) throw std::runtime_error("GPU polling failed");
      }
      wgpuInstanceProcessEvents(instance);
      if(!done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(!failure.empty()) throw std::runtime_error(failure);
    if(!done) throw std::runtime_error("WebGPU callback timed out");
  }
  void scope() {
    done=false;
    WGPUPopErrorScopeCallbackInfo cb{}; cb.mode=WGPUCallbackMode_AllowProcessEvents; cb.userdata1=this;
    cb.callback=[](WGPUPopErrorScopeStatus s,WGPUErrorType e,WGPUStringView msg,void *p,void*) {
      auto &c=*static_cast<CBCContext*>(p);
      if(s!=WGPUPopErrorScopeStatus_Success || e!=WGPUErrorType_NoError) c.failure="Validation: "+str(msg);
      c.done=true;
    };
    wgpuDevicePopErrorScope(device,cb); wait(false);
  }
  Buffer buffer(size_t bytes,WGPUBufferUsage usage,const void *initial=nullptr) {
    WGPUBufferDescriptor desc{}; desc.size=bytes; desc.usage=usage;
    wgpuDevicePushErrorScope(device,WGPUErrorFilter_Validation);
    Buffer b(wgpuDeviceCreateBuffer(device,&desc));
    if(initial) wgpuQueueWriteBuffer(queue,b,0,initial,bytes);
    scope(); return b;
  }
  void initialize() {
    if(wgpuCodebrixGetApiVersion()!=WGPU_CODEBRIX_API_VERSION) throw std::runtime_error("CodeBrix WebGPU ABI mismatch");
    WGPUInstanceExtras extra{}; extra.chain.sType=static_cast<WGPUSType>(WGPUSType_InstanceExtras);
    extra.backends=WGPUInstanceBackend_Vulkan;
    WGPUInstanceDescriptor desc{}; desc.nextInChain=&extra.chain;
    instance.value=wgpuCreateInstance(&desc);
    if(!instance.value) throw std::runtime_error("Cannot create Vulkan WebGPU instance");
    std::vector<WGPUAdapter> candidates(wgpuInstanceEnumerateAdapters(instance,nullptr,nullptr));
    wgpuInstanceEnumerateAdapters(instance,nullptr,candidates.data());
    for(auto a:candidates) {
      WGPUAdapterInfo info{};
      if(wgpuAdapterGetInfo(a,&info)!=WGPUStatus_Success) { wgpuAdapterRelease(a); continue; }
      bool hardware=info.adapterType==WGPUAdapterType_IntegratedGPU || info.adapterType==WGPUAdapterType_DiscreteGPU;
      if(!adapter.value && hardware && str(info.device).find("llvmpipe")==std::string::npos) {
        adapter.value=a; adapter_name=str(info.device)+" / "+str(info.description);
      } else wgpuAdapterRelease(a);
      wgpuAdapterInfoFreeMembers(info);
    }
    if(!adapter.value) throw std::runtime_error("No hardware Vulkan adapter");
    WGPUDeviceDescriptor dd{};
    dd.uncapturedErrorCallbackInfo.userdata1=this;
    dd.uncapturedErrorCallbackInfo.callback=[](WGPUDevice const*,WGPUErrorType,WGPUStringView m,void *p,void*) {
      static_cast<CBCContext*>(p)->failure="Uncaptured GPU error: "+str(m);
    };
    dd.deviceLostCallbackInfo.userdata1=this; dd.deviceLostCallbackInfo.mode=WGPUCallbackMode_AllowProcessEvents;
    dd.deviceLostCallbackInfo.callback=[](WGPUDevice const*,WGPUDeviceLostReason reason,WGPUStringView m,void *p,void*) {
      if(reason!=WGPUDeviceLostReason_Destroyed) static_cast<CBCContext*>(p)->failure="Device lost: "+str(m);
    };
    WGPURequestDeviceCallbackInfo cb{}; cb.mode=WGPUCallbackMode_AllowProcessEvents; cb.userdata1=this;
    cb.callback=[](WGPURequestDeviceStatus s,WGPUDevice d,WGPUStringView m,void *p,void*) {
      auto &c=*static_cast<CBCContext*>(p); c.device.value=d;
      if(s!=WGPURequestDeviceStatus_Success) c.failure="Request device: "+str(m);
      c.done=true;
    };
    done=false; wgpuAdapterRequestDevice(adapter,&dd,cb); wait(false);
    if(!device.value) throw std::runtime_error("No WebGPU device");
    queue.value=wgpuDeviceGetQueue(device);
    std::array<WGPUBindGroupLayoutEntry,5> entries{};
    for(unsigned i=0;i<entries.size();i++) {
      entries[i].binding=i; entries[i].visibility=WGPUShaderStage_Compute;
      entries[i].buffer.type=i==0?WGPUBufferBindingType_Uniform:(i==4?WGPUBufferBindingType_ReadOnlyStorage:WGPUBufferBindingType_Storage);
    }
    WGPUBindGroupLayoutDescriptor bd{}; bd.entryCount=entries.size(); bd.entries=entries.data();
    wgpuDevicePushErrorScope(device,WGPUErrorFilter_Validation);
    bindings.value=wgpuDeviceCreateBindGroupLayout(device,&bd); scope();
    WGPUPipelineLayoutDescriptor ld{}; ld.bindGroupLayoutCount=1; ld.bindGroupLayouts=&bindings.value;
    layout.value=wgpuDeviceCreatePipelineLayout(device,&ld);
    WGPUShaderSourceWGSL source{}; source.chain.sType=WGPUSType_ShaderSourceWGSL;
    source.code={shader_source,sizeof(shader_source)-1};
    WGPUShaderModuleDescriptor sd{}; sd.nextInChain=&source.chain;
    wgpuDevicePushErrorScope(device,WGPUErrorFilter_Validation);
    Owned<WGPUShaderModule,wgpuShaderModuleRelease> shader(wgpuDeviceCreateShaderModule(device,&sd)); scope();
    for(const char *entry:{"camera","intersect","shade"}) {
      WGPUComputePipelineDescriptor pd{}; pd.layout=layout; pd.compute.module=shader;
      pd.compute.entryPoint={entry,std::strlen(entry)};
      wgpuDevicePushErrorScope(device,WGPUErrorFilter_Validation);
      pipelines.emplace_back(wgpuDeviceCreateComputePipeline(device,&pd)); scope();
    }
  }
  void render(const CBCCamera &camera,uint32_t width,uint32_t height,uint32_t samples,float *rgba,size_t count) {
    if(!scene) throw std::runtime_error("No scene loaded");
    if(!width||!height||width>512||height>512||!samples||samples>64||!rgba||count!=size_t(width)*height*4)
      throw std::runtime_error("Invalid image dimensions, sample count or output size");
    static_assert(sizeof(CBCCamera)==21*sizeof(float));
    std::array<float,21> values;
    std::memcpy(values.data(),&camera,sizeof(camera));
    for(float v:values) if(!std::isfinite(v)) throw std::runtime_error("Nonfinite camera configuration");
    auto nonzero=[](const float *v) { return v[0]*v[0]+v[1]*v[1]+v[2]*v[2]>1e-10f; };
    if(!nonzero(camera.forward)||!nonzero(camera.right)||!nonzero(camera.up)||camera.tan_half_fov<=0||camera.aspect<=0||camera.light_power<0)
      throw std::runtime_error("Invalid camera basis or lighting");
    const size_t bytes=count*sizeof(float);
    auto film=buffer(bytes,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    auto paths=buffer(bytes*2,WGPUBufferUsage_Storage);
    auto hits=buffer(bytes,WGPUBufferUsage_Storage);
    auto readback=buffer(bytes,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    std::vector<std::array<uint32_t,64>> configs(samples);
    for(uint32_t s=0;s<samples;s++) {
      auto &p=configs[s];
      auto row=[&](int r,const float *v,float w) { for(int j=0;j<3;j++) p[r*4+j]=std::bit_cast<uint32_t>(v[j]); p[r*4+3]=std::bit_cast<uint32_t>(w); };
      row(0,camera.origin,0.0001f); row(1,camera.forward,camera.tan_half_fov);
      row(2,camera.right,camera.aspect); row(3,camera.up,10000.0f);
      row(4,camera.light_position,camera.light_power); row(5,camera.background,0);
      p[24]=width; p[25]=height; p[26]=s; p[27]=samples;
    }
    auto uniform=buffer(configs.size()*256,WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst,configs.data());
    std::vector<BindGroup> groups;
    for(uint32_t s=0;s<samples;s++) {
      std::array<WGPUBindGroupEntry,5> e{};
      WGPUBuffer handles[]={uniform.value,film.value,paths.value,hits.value,scene->value};
      size_t sizes[]={112,bytes,bytes*2,bytes,scene_bytes};
      for(unsigned i=0;i<5;i++) { e[i].binding=i; e[i].buffer=handles[i]; e[i].size=sizes[i]; }
      e[0].offset=s*256;
      WGPUBindGroupDescriptor d{}; d.layout=bindings; d.entryCount=e.size(); d.entries=e.data();
      wgpuDevicePushErrorScope(device,WGPUErrorFilter_Validation);
      groups.emplace_back(wgpuDeviceCreateBindGroup(device,&d)); scope();
    }
    Owned<WGPUCommandEncoder,wgpuCommandEncoderRelease> encoder(wgpuDeviceCreateCommandEncoder(device,nullptr));
    wgpuCommandEncoderClearBuffer(encoder,film,0,bytes);
    for(uint32_t s=0;s<samples;s++) for(unsigned stage=0;stage<3;stage++) {
      Owned<WGPUComputePassEncoder,wgpuComputePassEncoderRelease> pass(wgpuCommandEncoderBeginComputePass(encoder,nullptr));
      wgpuComputePassEncoderSetPipeline(pass,pipelines[stage]);
      wgpuComputePassEncoderSetBindGroup(pass,0,groups[s],0,nullptr);
      wgpuComputePassEncoderDispatchWorkgroups(pass,(width*height+63)/64,1,1);
      wgpuComputePassEncoderEnd(pass);
    }
    wgpuCommandEncoderCopyBufferToBuffer(encoder,film,0,readback,0,bytes);
    wgpuDevicePushErrorScope(device,WGPUErrorFilter_Validation);
    Owned<WGPUCommandBuffer,wgpuCommandBufferRelease> command(wgpuCommandEncoderFinish(encoder,nullptr)); scope();
    wgpuQueueSubmit(queue,1,&command.value);
    WGPUBufferMapCallbackInfo cb{}; cb.mode=WGPUCallbackMode_AllowProcessEvents; cb.userdata1=this;
    cb.callback=[](WGPUMapAsyncStatus s,WGPUStringView m,void *p,void*) {
      auto &c=*static_cast<CBCContext*>(p);
      if(s!=WGPUMapAsyncStatus_Success) c.failure="Readback: "+str(m);
      c.done=true;
    };
    done=false; wgpuBufferMapAsync(readback,WGPUMapMode_Read,0,bytes,cb); wait(true);
    const void *mapped=wgpuBufferGetConstMappedRange(readback,0,bytes);
    if(!mapped) throw std::runtime_error("No mapped image");
    std::memcpy(rgba,mapped,bytes); wgpuBufferUnmap(readback);
    for(size_t i=0;i<count;i++) if(!std::isfinite(rgba[i])) throw std::runtime_error("Nonfinite rendered image");
  }
};

template<class F> static int protect(char *error,size_t capacity,F function) noexcept {
  try { function(); copy_text("",error,capacity); return 0; }
  catch(const std::exception &e) { copy_text(e.what(),error,capacity); return 1; }
  catch(...) { copy_text("Unknown native exception",error,capacity); return 1; }
}
extern "C" {
uint32_t cbc_abi_version() { return 1; }
int cbc_create(CBCContext **out,char *error,size_t capacity) {
  if(out) *out=nullptr;
  return protect(error,capacity,[&] { if(!out) throw std::runtime_error("Null context output");
    auto c=std::make_unique<CBCContext>(); c->initialize(); *out=c.release(); });
}
void cbc_destroy(CBCContext *c) { delete c; }
int cbc_adapter(CBCContext *c,char *text,size_t capacity) {
  if(!c||!text||!capacity) return 1;
  copy_text(c->adapter_name,text,capacity); return 0;
}
int cbc_set_scene(CBCContext *c,const void *data,size_t bytes,char *error,size_t capacity) {
  return protect(error,capacity,[&] {
    if(!c) throw std::runtime_error("Null context"); std::lock_guard lock(c->mutex);
    validate_scene(data,bytes);
    auto next=std::make_unique<Buffer>(c->buffer(bytes,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst,data));
    c->scene=std::move(next); c->scene_bytes=bytes;
  });
}
int cbc_render(CBCContext *c,const CBCCamera *camera,uint32_t width,uint32_t height,uint32_t samples,
               float *rgba,size_t count,char *error,size_t capacity) {
  return protect(error,capacity,[&] {
    if(!c||!camera) throw std::runtime_error("Null context/camera"); std::lock_guard lock(c->mutex);
    c->render(*camera,width,height,samples,rgba,count);
  });
}
}
