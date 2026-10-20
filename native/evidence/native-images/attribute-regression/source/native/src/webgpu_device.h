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

namespace cbc {
struct Error:std::runtime_error { int status; Error(int s,const std::string &m):std::runtime_error(m),status(s){} };
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
  uint64_t buffer_creations=0,pipeline_creations=0;
  Device();
  WGPUDevice get()const{return device.value;}
  WGPUQueue get_queue()const{return queue.value;}
  void check();
  void wait(const std::shared_ptr<Completion> &completion,bool gpu);
  void push_scope(){wgpuDevicePushErrorScope(device,WGPUErrorFilter_Validation);}
  void pop_scope();
  Buffer buffer(size_t bytes,WGPUBufferUsage usage,const void *data=nullptr);
  Pipeline pipeline(WGPUShaderModule shader,WGPUPipelineLayout layout,const char *entry);
  void read(Buffer &buffer,void *out,size_t bytes);
};
}
