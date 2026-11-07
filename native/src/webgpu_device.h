// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "webgpu.h"
#include "wgpu.h"
#include "codebrix_wgpu.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cbc {
struct Error:std::runtime_error { int status; Error(int s,const std::string &m):std::runtime_error(m),status(s){} };
// Opt-in diagnostics (environment variables, all off by default): the WGSL
// override, adapter choice, non-conformant adapters, the DXC shader-model cap,
// instance debug flags and the debug output mode. Each active one is noted here
// ("NAME=value"), printed once to stderr and listed in the device report
// (deviceReport.webgpu.diagnostics), so a diagnostic run is never mistaken for
// validation; the gates refuse to pass while any is listed. Process-wide, as
// the environment is.
void note_diagnostic(const std::string &entry);
std::vector<std::string> diagnostics();
// The value of a diagnostic environment variable (nullptr when unset or empty);
// a set one is noted.
const char *diagnostic_env(const char *name);
template<class T,auto Release> struct Handle {
  T value{};
  Handle()=default; explicit Handle(T v):value(v){}
  ~Handle(){if(value) Release(value);}
  Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
  Handle(Handle &&other) noexcept:value(std::exchange(other.value,nullptr)){}
  Handle& operator=(Handle &&other) noexcept {if(this!=&other){if(value) Release(value);value=std::exchange(other.value,nullptr);}return *this;}
  operator T()const{return value;}
};
using Buffer=Handle<WGPUBuffer,wgpuBufferRelease>;
using Pipeline=Handle<WGPUComputePipeline,wgpuComputePipelineRelease>;
// Test seam (fingerprint mode, cycles/fingerprint.h): called before a compute pipeline is created; null otherwise.
inline void (*pipeline_creation_hook)(const char *entry)=nullptr;
using BindGroup=Handle<WGPUBindGroup,wgpuBindGroupRelease>;
using BindLayout=Handle<WGPUBindGroupLayout,wgpuBindGroupLayoutRelease>;
using PipelineLayout=Handle<WGPUPipelineLayout,wgpuPipelineLayoutRelease>;
struct Completion {
  std::atomic<bool> done{false}; std::string error;
  WGPUDevice requested_device=nullptr;
  ~Completion(){if(requested_device)wgpuDeviceRelease(requested_device);}
};
class Device {
  // Error callbacks can run on implementation threads. Keep state alive until
  // after all handles (including the instance) have been released.
  std::mutex error_mutex; std::string fatal;
  Handle<WGPUInstance,wgpuInstanceRelease> instance;
  Handle<WGPUAdapter,wgpuAdapterRelease> adapter;
  Handle<WGPUDevice,wgpuDeviceRelease> device;
  Handle<WGPUQueue,wgpuQueueRelease> queue;
  void set_error(const std::string &message);
 public:
  WGPULimits limits{}; std::string adapter_name,library_path;
  // Windows only: the WebGPU backend in use ("D3D12" or "Vulkan") and, on
  // D3D12, the DXC shader compiler the runtime loads. Empty elsewhere.
  std::string backend,dxc_path;
  // Every adapter the instance enumerated (JSON objects, for the device report)
  // and, for an adapter that is Vulkan layered on another API (Mesa Dozen on
  // Direct3D 12), what it runs on; empty for a native driver.
  std::vector<std::string> adapters_seen;std::string layered_on;
  // Apple: IEEE floating point requested from the runtime for every WGSL pipeline (M1).
  bool metal_precise_math=false;
  // Adapter identity for the device-tier record and known-adapter table
  // (device_tier.h): WGPUAdapterInfo strings and PCI-style ids as reported.
  std::string adapter_vendor,adapter_architecture,adapter_device,adapter_description;
  uint32_t adapter_vendor_id=0,adapter_device_id=0,adapter_type=0,adapter_backend=0;
  uint64_t buffer_creations=0,pipeline_creations=0;
  // Persistent pipeline cache (native/cycles/pipeline_cache.h). A device requested
  // with request_pipeline_cache enables the runtime's pipeline-cache feature when
  // the adapter offers it (Vulkan only); key is then the adapter's cache key.
  bool pipeline_cache_offered=false;std::string pipeline_cache_key;
  // Pipelines whose creation failed through a cache and were created without it.
  uint64_t pipeline_cache_faults=0;
  explicit Device(bool request_pipeline_cache=false);
  WGPUDevice get()const{return device.value;}
  WGPUQueue get_queue()const{return queue.value;}
  void check();
  void wait(const std::shared_ptr<Completion> &completion,bool gpu);
  void push_scope(){wgpuDevicePushErrorScope(device,WGPUErrorFilter_Validation);}
  void pop_scope();
  Buffer buffer(size_t bytes,WGPUBufferUsage usage,const void *data=nullptr);
  // label (default: entry) names the stage in the crash breadcrumb. While the
  // pipeline is created, a crash signal is also appended as "signal=<n>" to
  // record_fd (the device-tier pending breadcrumb; -1: none). cache (optional) is
  // chained into the creation. A creation that fails through it is repeated without
  // it. A process that dies while creating the pipeline removes cache_file, so a
  // driver that fails on its own cached data cannot fail again on every run.
  Pipeline pipeline(WGPUShaderModule shader,WGPUPipelineLayout layout,const char *entry,const char *label=nullptr,int record_fd=-1,
                    WGPUCodebrixPipelineCache cache=nullptr,const char *cache_file=nullptr);
  void read(Buffer &buffer,void *out,size_t bytes);
};
}
