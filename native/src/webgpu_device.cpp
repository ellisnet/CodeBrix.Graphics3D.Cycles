// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "webgpu_device.h"
#include "cycles_backend.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <thread>
#include <vector>
namespace cbc {
static std::string text(WGPUStringView s){return s.data?std::string(s.data,s.length):"";}
using Pending=std::shared_ptr<Completion>;
// A one-shot callback owns its shared state, including after a wait times out.
static Pending take(void *p){std::unique_ptr<Pending> owner(static_cast<Pending*>(p));return *owner;}
void Device::set_error(const std::string &message){std::lock_guard lock(error_mutex);fatal=message;}
void Device::check(){std::lock_guard lock(error_mutex);if(!fatal.empty())throw Error(CBC_BACKEND_GPU_ERROR,fatal);}
void Device::wait(const Pending &pending,bool gpu){
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
  while(!pending->done.load(std::memory_order_acquire)){
    check(); if(std::chrono::steady_clock::now()>=deadline){set_error("GPU callback timeout");check();}
    if(gpu){WGPUCodebrixPollResult result;
      if(wgpuCodebrixDevicePoll(device,true,1000000000,nullptr,&result)!=WGPUStatus_Success && result!=WGPUCodebrixPollResult_Timeout)
        throw Error(CBC_BACKEND_GPU_ERROR,"GPU polling failed");}
    wgpuInstanceProcessEvents(instance);
    if(!pending->done.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  check(); if(!pending->error.empty())throw Error(CBC_BACKEND_GPU_ERROR,pending->error);
}
void Device::pop_scope(){
  auto p=std::make_shared<Completion>(); WGPUPopErrorScopeCallbackInfo cb{};
  cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=new Pending(p);
  cb.callback=[](WGPUPopErrorScopeStatus status,WGPUErrorType type,WGPUStringView msg,void *data,void*){
    auto p=take(data);if(status!=WGPUPopErrorScopeStatus_Success||type!=WGPUErrorType_NoError)p->error="GPU validation: "+text(msg);
    p->done.store(true,std::memory_order_release);
  };
  wgpuDevicePopErrorScope(device,cb);wait(p,false);
}
Device::Device(){
  if(wgpuCodebrixGetApiVersion()!=WGPU_CODEBRIX_API_VERSION)throw Error(CBC_BACKEND_GPU_ERROR,"WebGPU ABI mismatch");
  Dl_info loaded{};if(!dladdr(reinterpret_cast<void*>(&wgpuCodebrixGetApiVersion),&loaded))throw Error(CBC_BACKEND_GPU_ERROR,"Cannot identify WebGPU library");
  library_path=loaded.dli_fname;
#ifdef __APPLE__
  // The shipped runtime's macOS builds contain only wgpu's Metal backend.
  const WGPUInstanceBackend backend_flag=WGPUInstanceBackend_Metal;const std::string backend_name="Metal";
#else
  const WGPUInstanceBackend backend_flag=WGPUInstanceBackend_Vulkan;const std::string backend_name="Vulkan";
#endif
  WGPUInstanceExtras extra{};extra.chain.sType=static_cast<WGPUSType>(WGPUSType_InstanceExtras);extra.backends=backend_flag;
  WGPUInstanceDescriptor desc{};desc.nextInChain=&extra.chain;instance=Handle<WGPUInstance,wgpuInstanceRelease>(wgpuCreateInstance(&desc));
  if(!instance.value)throw Error(CBC_BACKEND_GPU_ERROR,"Cannot create "+backend_name+" WebGPU instance");
  std::vector<WGPUAdapter> candidates(wgpuInstanceEnumerateAdapters(instance,nullptr,nullptr));
  wgpuInstanceEnumerateAdapters(instance,nullptr,candidates.data());
  for(auto a:candidates){WGPUAdapterInfo info{};
    if(wgpuAdapterGetInfo(a,&info)!=WGPUStatus_Success){wgpuAdapterRelease(a);continue;}
    bool hardware=info.adapterType==WGPUAdapterType_IntegratedGPU||info.adapterType==WGPUAdapterType_DiscreteGPU;
    if(!adapter.value&&hardware&&text(info.device).find("llvmpipe")==std::string::npos){adapter.value=a;adapter_name=text(info.device)+" / "+text(info.description);
      adapter_vendor=text(info.vendor);adapter_architecture=text(info.architecture);adapter_device=text(info.device);adapter_description=text(info.description);
      adapter_vendor_id=info.vendorID;adapter_device_id=info.deviceID;adapter_type=uint32_t(info.adapterType);adapter_backend=uint32_t(info.backendType);}
    else wgpuAdapterRelease(a);wgpuAdapterInfoFreeMembers(info);
  }
  if(!adapter.value)throw Error(CBC_BACKEND_GPU_ERROR,"No hardware "+backend_name+" adapter");
  WGPUDeviceDescriptor dd{};
  dd.uncapturedErrorCallbackInfo.userdata1=this;
  dd.uncapturedErrorCallbackInfo.callback=[](WGPUDevice const*,WGPUErrorType,WGPUStringView msg,void *data,void*){
    static_cast<Device*>(data)->set_error("Uncaptured GPU error: "+text(msg));};
  dd.deviceLostCallbackInfo.userdata1=this;dd.deviceLostCallbackInfo.mode=WGPUCallbackMode_AllowProcessEvents;
  dd.deviceLostCallbackInfo.callback=[](WGPUDevice const*,WGPUDeviceLostReason reason,WGPUStringView msg,void *data,void*){
    if(reason!=WGPUDeviceLostReason_Destroyed)static_cast<Device*>(data)->set_error("Device lost: "+text(msg));};
  auto p=std::make_shared<Completion>();WGPURequestDeviceCallbackInfo cb{};
  cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=new Pending(p);
  cb.callback=[](WGPURequestDeviceStatus status,WGPUDevice device,WGPUStringView msg,void *data,void*){
    auto p=take(data);p->requested_device=device;
    if(status!=WGPURequestDeviceStatus_Success)p->error="Request device: "+text(msg);
    p->done.store(true,std::memory_order_release);
  };
  wgpuAdapterRequestDevice(adapter,&dd,cb);wait(p,false);
  device.value=std::exchange(p->requested_device,nullptr);
  if(!device.value)throw Error(CBC_BACKEND_GPU_ERROR,"No WebGPU device");
  queue.value=wgpuDeviceGetQueue(device);
  if(wgpuDeviceGetLimits(device,&limits)!=WGPUStatus_Success)throw Error(CBC_BACKEND_GPU_ERROR,"Cannot query device limits");
  if(limits.maxStorageBuffersPerShaderStage<7)throw Error(CBC_BACKEND_UNSUPPORTED,"Seven storage bindings required");
}
Buffer Device::buffer(size_t bytes,WGPUBufferUsage usage,const void *data){
  if(!bytes||bytes%4||bytes>limits.maxBufferSize||((usage&WGPUBufferUsage_Storage)&&bytes>limits.maxStorageBufferBindingSize))
    throw Error(CBC_BACKEND_UNSUPPORTED,"Buffer exceeds WebGPU limits");
  WGPUBufferDescriptor d{};d.size=bytes;d.usage=usage;push_scope();Buffer out(wgpuDeviceCreateBuffer(device,&d));
  if(data&&out.value)wgpuQueueWriteBuffer(queue,out,0,data,bytes);pop_scope();
  if(!out.value)throw Error(CBC_BACKEND_GPU_ERROR,"Buffer allocation failed");++buffer_creations;return out;
}
// A Vulkan driver compiler that exhausts its allocator or faults during
// pipeline creation terminates the process; no WebGPU error can be returned.
// While a pipeline is created, a scoped handler names the stage on stderr
// (async-signal-safe write) and then re-raises with the previous disposition.
namespace {
char crash_note[256];size_t crash_note_bytes=0;
volatile sig_atomic_t crash_record_fd=-1;
constexpr int crash_signals[]={SIGSEGV,SIGABRT,SIGBUS,SIGILL,SIGFPE};
struct sigaction previous_actions[5];
void crash_breadcrumb(int signal){
  if(crash_note_bytes){ssize_t ignored=write(2,crash_note,crash_note_bytes);(void)ignored;}
  if(crash_record_fd>=0){
    char line[24]="signal=";size_t n=7;char digits[12];size_t d=0;unsigned v=unsigned(signal);
    do{digits[d++]=char('0'+v%10);v/=10;}while(v&&d<sizeof(digits));
    while(d)line[n++]=digits[--d];line[n++]='\n';
    ssize_t ignored=write(crash_record_fd,line,n);(void)ignored;
  }
  for(size_t i=0;i<5;i++)if(crash_signals[i]==signal)sigaction(signal,&previous_actions[i],nullptr);
  raise(signal);
}
struct CrashBreadcrumb {
  explicit CrashBreadcrumb(const char *entry,int record_fd){
    crash_record_fd=record_fd;
    const int n=std::snprintf(crash_note,sizeof(crash_note),"\nCodeBrix Cycles: the Vulkan driver terminated while creating WebGPU compute pipeline '%s'\n",entry);
    crash_note_bytes=n>0?std::min(size_t(n),sizeof(crash_note)-1):0;
    struct sigaction action{};action.sa_handler=crash_breadcrumb;sigemptyset(&action.sa_mask);
    for(size_t i=0;i<5;i++)sigaction(crash_signals[i],&action,&previous_actions[i]);
  }
  ~CrashBreadcrumb(){for(size_t i=0;i<5;i++)sigaction(crash_signals[i],&previous_actions[i],nullptr);crash_note_bytes=0;crash_record_fd=-1;}
};
std::mutex crash_mutex;
}
Pipeline Device::pipeline(WGPUShaderModule shader,WGPUPipelineLayout layout,const char *entry,const char *label,int record_fd){
  WGPUComputePipelineDescriptor d{};d.layout=layout;d.compute.module=shader;d.compute.entryPoint={entry,std::strlen(entry)};
  // Driver out-of-memory and internal compiler errors are reported with the
  // stage name instead of only reaching the uncaptured-error callback.
  std::lock_guard crash_lock(crash_mutex);CrashBreadcrumb breadcrumb(label?label:entry,record_fd);
  // Test hook for the device-tier gates: terminate exactly as a driver crash
  // would, inside the breadcrumb scope, when creating the named stage.
  if(const char *crash=std::getenv("CODEBRIX_CYCLES_TEST_CRASH_PIPELINE"))if(std::strcmp(crash,label?label:entry)==0)raise(SIGABRT);
  wgpuDevicePushErrorScope(device,WGPUErrorFilter_Internal);wgpuDevicePushErrorScope(device,WGPUErrorFilter_OutOfMemory);
  push_scope();Pipeline result(wgpuDeviceCreateComputePipeline(device,&d));
  std::string failure;
  for(int i=0;i<3;i++){try{pop_scope();}catch(const Error &e){if(failure.empty())failure=e.what();}}
  if(!failure.empty())throw Error(CBC_BACKEND_GPU_ERROR,std::string("Pipeline '")+entry+"' creation failed: "+failure);
  if(!result.value)throw Error(CBC_BACKEND_GPU_ERROR,std::string("Pipeline '")+entry+"' creation failed");++pipeline_creations;return result;
}
void Device::read(Buffer &buffer,void *out,size_t bytes){
  auto p=std::make_shared<Completion>();WGPUBufferMapCallbackInfo cb{};cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=new Pending(p);
  cb.callback=[](WGPUMapAsyncStatus s,WGPUStringView msg,void *data,void*){auto p=take(data);
    if(s!=WGPUMapAsyncStatus_Success)p->error="Readback: "+text(msg);p->done.store(true,std::memory_order_release);};
  wgpuBufferMapAsync(buffer,WGPUMapMode_Read,0,bytes,cb);wait(p,true);
  const void *mapped=wgpuBufferGetConstMappedRange(buffer,0,bytes);
  if(!mapped){wgpuBufferUnmap(buffer);throw Error(CBC_BACKEND_GPU_ERROR,"Missing readback data");}
  std::memcpy(out,mapped,bytes);wgpuBufferUnmap(buffer);
}
}
