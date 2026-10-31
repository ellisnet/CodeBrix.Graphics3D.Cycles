// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "webgpu_device.h"
#include "cycles_backend.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#include <filesystem>
#else
#include <unistd.h>
#include <dlfcn.h>
#endif
namespace cbc {
#ifdef _WIN32
static std::string utf8(const std::filesystem::path &path){const auto s=path.u8string();return std::string(s.begin(),s.end());}
// The file of the module (DLL or executable) that contains an address, in UTF-8.
static std::filesystem::path module_file(const void *address){
  HMODULE module=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCWSTR>(address),&module))return {};
  std::wstring name(32768,L'\0');const DWORD n=GetModuleFileNameW(module,name.data(),DWORD(name.size()));
  if(n==0||n>=name.size())return {};name.resize(n);return std::filesystem::path(name);
}
static void module_anchor(){}
#endif
static std::string text(WGPUStringView s){return s.data?std::string(s.data,s.length):"";}
namespace {std::mutex diagnostics_mutex;std::vector<std::string> active_diagnostics;}
void note_diagnostic(const std::string &entry){
  std::lock_guard lock(diagnostics_mutex);
  if(std::find(active_diagnostics.begin(),active_diagnostics.end(),entry)!=active_diagnostics.end())return;
  active_diagnostics.push_back(entry);
  std::fprintf(stderr,"CodeBrix Cycles diagnostic active (not a validation run): %s\n",entry.c_str());
}
std::vector<std::string> diagnostics(){std::lock_guard lock(diagnostics_mutex);return active_diagnostics;}
const char *diagnostic_env(const char *name){
  const char *value=std::getenv(name);if(!value||!*value)return nullptr;
  note_diagnostic(std::string(name)+"="+value);return value;
}
static std::string lower(std::string s){for(char &c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
static std::string json_escape(const std::string &s){
  std::string out;for(char c:s){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(static_cast<unsigned char>(c)<0x20)out+=' ';else out+=c;}return out;
}
using Pending=std::shared_ptr<Completion>;
// A one-shot callback owns its shared state, including after a wait times out.
static Pending take(void *p){std::unique_ptr<Pending> owner(static_cast<Pending*>(p));return *owner;}
void Device::set_error(const std::string &message){std::lock_guard lock(error_mutex);fatal=message;}
void Device::check(){std::lock_guard lock(error_mutex);if(!fatal.empty())throw Error(CBC_BACKEND_GPU_ERROR,fatal);}
void Device::wait(const Pending &pending,bool gpu){
  // A software adapter (the CODEBRIX_CYCLES_WEBGPU_ADAPTER=software diagnostic:
  // WARP, llvmpipe) runs the kernels on the CPU, far slower than any GPU.
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(adapter_type==WGPUAdapterType_CPU?3600:60);
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
#ifdef _WIN32
  // The address of an imported function is its import thunk in this module, so
  // the runtime is found by its module name.
  if(HMODULE runtime=GetModuleHandleW(L"codebrix_wgpu.dll")){
    std::wstring name(32768,L'\0');const DWORD n=GetModuleFileNameW(runtime,name.data(),DWORD(name.size()));
    if(n&&n<name.size()){name.resize(n);library_path=utf8(std::filesystem::path(name));}
  }
  if(library_path.empty())throw Error(CBC_BACKEND_GPU_ERROR,"Cannot identify WebGPU library");
#else
  Dl_info loaded{};if(!dladdr(reinterpret_cast<void*>(&wgpuCodebrixGetApiVersion),&loaded))throw Error(CBC_BACKEND_GPU_ERROR,"Cannot identify WebGPU library");
  library_path=loaded.dli_fname;
#endif
#ifdef __APPLE__
  // The shipped runtime's macOS builds contain only wgpu's Metal backend.
  const WGPUInstanceBackend backend_flag=WGPUInstanceBackend_Metal;const std::string backend_name="Metal";
  // Metal compiles WGSL with fast math by default, which removes NaN checks, reassociates
  // and approximates transcendentals; the kernels rely on IEEE floating point
  // (docs/TESTING-MACOS-X64.md, M1). The runtime's switch is process-wide and applies to
  // every pipeline compiled afterwards, so it is set before any pipeline exists.
  if(wgpuCodebrixSetMetalPreciseMath(1)!=WGPUStatus_Success)
    throw Error(CBC_BACKEND_GPU_ERROR,"The WebGPU runtime refused IEEE floating point on Metal (wgpuCodebrixSetMetalPreciseMath)");
  metal_precise_math=true;
#elif defined(_WIN32)
  // Windows: Direct3D 12 by default (part of Windows 10 and 11, on every GPU
  // vendor's driver), with the DXC shader compiler shipped beside this library;
  // FXC cannot compile the Cycles kernels. Vulkan is an explicit opt-in
  // (CODEBRIX_CYCLES_WEBGPU_BACKEND=vulkan); a backend never falls back to another.
  std::string requested;if(const char *b=std::getenv("CODEBRIX_CYCLES_WEBGPU_BACKEND"))requested=b;
  if(!requested.empty()&&requested!="d3d12"&&requested!="vulkan")
    throw Error(CBC_BACKEND_INVALID,"CODEBRIX_CYCLES_WEBGPU_BACKEND must be d3d12 or vulkan, not '"+requested+"'");
  const bool vulkan=requested=="vulkan";
  const WGPUInstanceBackend backend_flag=vulkan?WGPUInstanceBackend_Vulkan:WGPUInstanceBackend_DX12;const std::string backend_name=vulkan?"Vulkan":"D3D12";
  std::string dxc;
  if(!vulkan){
    if(const char *path=std::getenv("CODEBRIX_CYCLES_DXC_PATH"))dxc=path;
    else{const std::filesystem::path self=module_file(reinterpret_cast<const void*>(&module_anchor));
      if(!self.empty())dxc=utf8(self.parent_path()/"dxcompiler.dll");}
    std::error_code ignored;
    if(dxc.empty()||!std::filesystem::is_regular_file(std::filesystem::u8path(dxc),ignored))
      throw Error(CBC_BACKEND_GPU_ERROR,"The DXC shader compiler (dxcompiler.dll) was not found"+(dxc.empty()?std::string():" at "+dxc)+
        "; it ships beside the Cycles host library (set CODEBRIX_CYCLES_WEBGPU_BACKEND=vulkan to use Vulkan instead)");
    dxc_path=dxc;
  }
  backend=backend_name;
#else
  const WGPUInstanceBackend backend_flag=WGPUInstanceBackend_Vulkan;const std::string backend_name="Vulkan";
#endif
  WGPUInstanceExtras extra{};extra.chain.sType=static_cast<WGPUSType>(WGPUSType_InstanceExtras);extra.backends=backend_flag;
  // Diagnostic instance flags (opt-in; see note_diagnostic): wgpu debug and
  // validation layers, and adapters wgpu hides as non-conformant (on Windows on
  // ARM devices without a native Vulkan driver, Mesa's Dozen, Vulkan on D3D12).
  if(const char *flags=diagnostic_env("CODEBRIX_CYCLES_WEBGPU_INSTANCE_FLAGS")){
    std::string list=lower(flags);
    for(size_t at=0;at<=list.size();){const size_t end=std::min(list.find(',',at),list.size());const std::string name=list.substr(at,end-at);at=end+1;
      if(name=="debug")extra.flags|=WGPUInstanceFlag_Debug;else if(name=="validation")extra.flags|=WGPUInstanceFlag_Validation;
      else if(name=="gpu-based-validation")extra.flags|=WGPUInstanceFlag_GPUBasedValidation;else if(name=="discard-hal-labels")extra.flags|=WGPUInstanceFlag_DiscardHalLabels;
      else if(name=="with-env")extra.flags|=WGPUInstanceFlag_WithEnv;
      else if(!name.empty())throw Error(CBC_BACKEND_INVALID,"CODEBRIX_CYCLES_WEBGPU_INSTANCE_FLAGS: unknown flag '"+name+"' (debug, validation, gpu-based-validation, discard-hal-labels, with-env)");}
  }
  if(const char *allow=diagnostic_env("CODEBRIX_CYCLES_WEBGPU_ALLOW_NONCOMPLIANT")){
    if(std::strcmp(allow,"1")!=0)throw Error(CBC_BACKEND_INVALID,"CODEBRIX_CYCLES_WEBGPU_ALLOW_NONCOMPLIANT must be 1");
    extra.flags|=WGPUInstanceFlag_AllowUnderlyingNoncompliantAdapter;
  }
  // Diagnostic: wgpu's log (the D3D12 debug layer and Vulkan validation messages
  // the debug and validation instance flags enable arrive only here), on stderr.
  if(const char *level=diagnostic_env("CODEBRIX_CYCLES_WEBGPU_LOG")){
    const std::string name=lower(level);
    const WGPULogLevel value=name=="error"?WGPULogLevel_Error:name=="warn"?WGPULogLevel_Warn:name=="info"?WGPULogLevel_Info:
      name=="debug"?WGPULogLevel_Debug:name=="trace"?WGPULogLevel_Trace:WGPULogLevel_Off;
    if(value==WGPULogLevel_Off)throw Error(CBC_BACKEND_INVALID,std::string("CODEBRIX_CYCLES_WEBGPU_LOG must be error, warn, info, debug or trace, not '")+level+"'");
    wgpuSetLogCallback([](WGPULogLevel l,WGPUStringView message,void*){
      static const char *const names[]={"off","error","warn","info","debug","trace"};
      std::fprintf(stderr,"wgpu %s: %s\n",l<=WGPULogLevel_Trace?names[l]:"log",text(message).c_str());},nullptr);
    wgpuSetLogLevel(value);
  }
#ifdef _WIN32
  if(!vulkan){
    extra.dx12ShaderCompiler=WGPUDx12Compiler_Dxc;extra.dxcPath={dxc_path.c_str(),dxc_path.size()};extra.dxcMaxShaderModel=WGPUDxcMaxShaderModel_V6_7;
    // Diagnostic: a lower DXC shader model changes the DXIL a driver compiles.
    if(const char *model=diagnostic_env("CODEBRIX_CYCLES_DXC_MAX_SHADER_MODEL")){
      if(std::strlen(model)!=3||model[0]!='6'||model[1]!='.'||model[2]<'0'||model[2]>'7')
        throw Error(CBC_BACKEND_INVALID,std::string("CODEBRIX_CYCLES_DXC_MAX_SHADER_MODEL must be 6.0 to 6.7, not '")+model+"'");
      extra.dxcMaxShaderModel=static_cast<WGPUDxcMaxShaderModel>(WGPUDxcMaxShaderModel_V6_0+(model[2]-'0'));
    }
  }
#endif
  WGPUInstanceDescriptor desc{};desc.nextInChain=&extra.chain;instance=Handle<WGPUInstance,wgpuInstanceRelease>(wgpuCreateInstance(&desc));
  if(!instance.value)throw Error(CBC_BACKEND_GPU_ERROR,"Cannot create "+backend_name+" WebGPU instance");
  std::vector<WGPUAdapter> candidates(wgpuInstanceEnumerateAdapters(instance,nullptr,nullptr));
  wgpuInstanceEnumerateAdapters(instance,nullptr,candidates.data());
  // Adapter choice. By default the first hardware (integrated or discrete)
  // adapter (on Apple, an Intel one first if there is one). Diagnostic CODEBRIX_CYCLES_WEBGPU_ADAPTER: "hardware" (the default
  // rule), "software" (a CPU adapter: WARP on Direct3D 12, llvmpipe on Vulkan,
  // a full-kernel reference on the same machine) or a case-insensitive substring
  // of the adapter's device name or description.
  std::string choice="hardware";if(const char *want=diagnostic_env("CODEBRIX_CYCLES_WEBGPU_ADAPTER"))choice=lower(want);
  // Apple (osx-x64), default rule only: an Intel adapter is preferred to any other hardware
  // adapter, whatever the enumeration order. Intel MacBook Pros with Intel + AMD graphics then
  // render on the supported, validated GPU; AMD GPUs are not supported, may work (they get the
  // conservative tier as a precaution; docs/TESTING-MACOS-X64.md). Metal reports no vendor
  // ID, so the device name decides.
#ifdef __APPLE__
  const bool prefer_intel=choice=="hardware";
#else
  const bool prefer_intel=false;
#endif
  for(auto a:candidates){WGPUAdapterInfo info{};
    if(wgpuAdapterGetInfo(a,&info)!=WGPUStatus_Success){wgpuAdapterRelease(a);continue;}
    const std::string device_text=text(info.device),description_text=text(info.description);
    adapters_seen.push_back("{\"device\":\""+json_escape(device_text)+"\",\"description\":\""+json_escape(description_text)+"\",\"adapterType\":"+
      std::to_string(uint32_t(info.adapterType))+",\"backendType\":"+std::to_string(uint32_t(info.backendType))+",\"vendorId\":"+std::to_string(info.vendorID)+'}');
    const bool hardware=info.adapterType==WGPUAdapterType_IntegratedGPU||info.adapterType==WGPUAdapterType_DiscreteGPU;
    const bool wanted=choice=="hardware"?hardware&&device_text.find("llvmpipe")==std::string::npos:
                      choice=="software"?info.adapterType==WGPUAdapterType_CPU:
                      lower(device_text).find(choice)!=std::string::npos||lower(description_text).find(choice)!=std::string::npos;
    if(adapter.value&&wanted&&prefer_intel&&adapter_device.find("Intel")==std::string::npos&&device_text.find("Intel")!=std::string::npos){
      wgpuAdapterRelease(adapter.value);adapter.value=nullptr;layered_on.clear();}
    if(!adapter.value&&wanted){adapter.value=a;adapter_name=device_text+" / "+description_text;
      adapter_vendor=text(info.vendor);adapter_architecture=text(info.architecture);adapter_device=device_text;adapter_description=description_text;
      adapter_vendor_id=info.vendorID;adapter_device_id=info.deviceID;adapter_type=uint32_t(info.adapterType);adapter_backend=uint32_t(info.backendType);
      // Mesa's Dozen names its devices "Microsoft Direct3D12 (<GPU>)": Vulkan
      // layered on D3D12, never native Vulkan evidence.
      if(info.backendType==WGPUBackendType_Vulkan&&device_text.rfind("Microsoft Direct3D12",0)==0)layered_on="Direct3D 12 (Mesa Dozen)";}
    else wgpuAdapterRelease(a);
    wgpuAdapterInfoFreeMembers(info);
  }
  if(!adapter.value){
    if(choice=="hardware")throw Error(CBC_BACKEND_GPU_ERROR,"No hardware "+backend_name+" adapter");
    std::string seen;for(auto &s:adapters_seen)seen+=(seen.empty()?"":", ")+s;
    throw Error(CBC_BACKEND_GPU_ERROR,"No "+backend_name+" adapter matches CODEBRIX_CYCLES_WEBGPU_ADAPTER="+choice+" (adapters: "+(seen.empty()?"none":seen)+")");
  }
  WGPUDeviceDescriptor dd{};
  // The Cycles device keeps all its memory in one storage binding addressed by 32-bit
  // offsets. WebGPU's default limits cap a storage binding at 128 MiB, which a scene with a
  // few 2K textures exceeds, so the two buffer-size limits are raised to what the adapter
  // supports (at most 2 GiB). Every other limit stays at the WebGPU default. The arena only
  // grows on demand, so scenes that fit the default limits allocate exactly as before.
  WGPULimits supported=WGPU_LIMITS_INIT,required=WGPU_LIMITS_INIT;
  if(wgpuAdapterGetLimits(adapter,&supported)==WGPUStatus_Success){
    const uint64_t cap=uint64_t(1)<<31;
    if(supported.maxBufferSize!=WGPU_LIMIT_U64_UNDEFINED)required.maxBufferSize=std::min(supported.maxBufferSize,cap);
    if(supported.maxStorageBufferBindingSize!=WGPU_LIMIT_U64_UNDEFINED)required.maxStorageBufferBindingSize=std::min(supported.maxStorageBufferBindingSize,cap);
    // wgpu gives an adapter that is not fully WebGPU compliant (a downlevel adapter: the
    // Raspberry Pi 5's V3D 7.1 on Mesa V3DV) its downlevel defaults for unrequested limits,
    // 4 storage buffers per stage instead of WebGPU's 8, below the 7 the kernels bind
    // (docs/TESTING-LINUX-ARM64.md, L1). These three are requested at the WebGPU default,
    // capped at what the adapter supports: a compliant adapter gets exactly its defaults.
    if(supported.maxStorageBuffersPerShaderStage!=WGPU_LIMIT_U32_UNDEFINED)required.maxStorageBuffersPerShaderStage=std::min(supported.maxStorageBuffersPerShaderStage,8u);
    if(supported.maxUniformBufferBindingSize!=WGPU_LIMIT_U64_UNDEFINED)required.maxUniformBufferBindingSize=std::min<uint64_t>(supported.maxUniformBufferBindingSize,65536);
    if(supported.maxComputeWorkgroupStorageSize!=WGPU_LIMIT_U32_UNDEFINED)required.maxComputeWorkgroupStorageSize=std::min(supported.maxComputeWorkgroupStorageSize,16384u);
    dd.requiredLimits=&required;
  }
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
  if(data&&out.value)wgpuQueueWriteBuffer(queue,out,0,data,bytes);
  pop_scope();
  if(!out.value)throw Error(CBC_BACKEND_GPU_ERROR,"Buffer allocation failed");
  ++buffer_creations;return out;
}
// A Vulkan driver compiler that exhausts its allocator or faults during
// pipeline creation terminates the process; no WebGPU error can be returned.
// While a pipeline is created, a scoped handler names the stage on stderr
// (async-signal-safe write) and then re-raises with the previous disposition.
namespace {
char crash_note[256];size_t crash_note_bytes=0;
volatile sig_atomic_t crash_record_fd=-1;
#ifdef _WIN32
// Windows: a driver fault is an unhandled structured exception (the process-wide
// filter below) and abort() raises SIGABRT; the CRT has no sigaction. The record
// line keeps the "signal=<n>" form with the matching CRT signal number.
LPTOP_LEVEL_EXCEPTION_FILTER previous_filter=nullptr;void (*previous_abort)(int)=SIG_DFL;
void crash_record(int signal){
  if(crash_note_bytes){DWORD ignored;WriteFile(GetStdHandle(STD_ERROR_HANDLE),crash_note,DWORD(crash_note_bytes),&ignored,nullptr);}
  if(crash_record_fd>=0){
    char line[24]="signal=";size_t n=7;char digits[12];size_t d=0;unsigned v=unsigned(signal);
    do{digits[d++]=char('0'+v%10);v/=10;}while(v&&d<sizeof(digits));
    while(d)line[n++]=digits[--d];
    line[n++]='\n';
    (void)_write(crash_record_fd,line,unsigned(n));
  }
}
LONG WINAPI crash_filter(EXCEPTION_POINTERS *e){
  const DWORD code=e&&e->ExceptionRecord?e->ExceptionRecord->ExceptionCode:0;
  crash_record(code==EXCEPTION_ILLEGAL_INSTRUCTION||code==EXCEPTION_PRIV_INSTRUCTION?SIGILL:
               code==EXCEPTION_INT_DIVIDE_BY_ZERO||code==EXCEPTION_FLT_DIVIDE_BY_ZERO||code==EXCEPTION_FLT_INVALID_OPERATION?SIGFPE:SIGSEGV);
  return previous_filter?previous_filter(e):EXCEPTION_CONTINUE_SEARCH;
}
void abort_breadcrumb(int signal){crash_record(signal);std::signal(SIGABRT,previous_abort);std::raise(SIGABRT);}
struct CrashBreadcrumb {
  explicit CrashBreadcrumb(const char *entry,int record_fd){
    crash_record_fd=record_fd;
    const int n=std::snprintf(crash_note,sizeof(crash_note),"\nCodeBrix Cycles: the GPU driver terminated while creating WebGPU compute pipeline '%s'\n",entry);
    crash_note_bytes=n>0?std::min(size_t(n),sizeof(crash_note)-1):0;
    previous_filter=SetUnhandledExceptionFilter(crash_filter);previous_abort=std::signal(SIGABRT,abort_breadcrumb);
  }
  ~CrashBreadcrumb(){SetUnhandledExceptionFilter(previous_filter);std::signal(SIGABRT,previous_abort);crash_note_bytes=0;crash_record_fd=-1;}
};
#else
constexpr int crash_signals[]={SIGSEGV,SIGABRT,SIGBUS,SIGILL,SIGFPE};
struct sigaction previous_actions[5];
void crash_breadcrumb(int signal){
  if(crash_note_bytes){ssize_t ignored=write(2,crash_note,crash_note_bytes);(void)ignored;}
  if(crash_record_fd>=0){
    char line[24]="signal=";size_t n=7;char digits[12];size_t d=0;unsigned v=unsigned(signal);
    do{digits[d++]=char('0'+v%10);v/=10;}while(v&&d<sizeof(digits));
    while(d)line[n++]=digits[--d];
    line[n++]='\n';
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
#endif
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
  if(!result.value)throw Error(CBC_BACKEND_GPU_ERROR,std::string("Pipeline '")+entry+"' creation failed");
  ++pipeline_creations;return result;
}
void Device::read(Buffer &buffer,void *out,size_t bytes){
  auto p=std::make_shared<Completion>();WGPUBufferMapCallbackInfo cb{};cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=new Pending(p);
  cb.callback=[](WGPUMapAsyncStatus s,WGPUStringView msg,void *data,void*){auto p=take(data);
    if(s!=WGPUMapAsyncStatus_Success)p->error="Readback: "+text(msg);
    p->done.store(true,std::memory_order_release);};
  wgpuBufferMapAsync(buffer,WGPUMapMode_Read,0,bytes,cb);wait(p,true);
  const void *mapped=wgpuBufferGetConstMappedRange(buffer,0,bytes);
  if(!mapped){wgpuBufferUnmap(buffer);throw Error(CBC_BACKEND_GPU_ERROR,"Missing readback data");}
  std::memcpy(out,mapped,bytes);wgpuBufferUnmap(buffer);
}
}
