// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Standalone C++ probe: directly links the native library supplied by the NuGet.
#include "webgpu.h"
#include "wgpu.h"
#include "codebrix_wgpu.h"
#include <dlfcn.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static WGPUInstance instance;
static WGPUDevice device;
static bool done;
static std::string failure;
static std::string str(WGPUStringView s) { return s.data ? std::string(s.data, s.length) : ""; }
static WGPUStringView view(const std::string &s) { return {s.data(),s.size()}; }
static void wait(bool gpu) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (!done && failure.empty() && std::chrono::steady_clock::now() < deadline) {
    if (gpu) {
      WGPUCodebrixPollResult result;
      if (wgpuCodebrixDevicePoll(device, true, 1000000000, nullptr, &result) != WGPUStatus_Success
          && result != WGPUCodebrixPollResult_Timeout) throw std::runtime_error("Device polling failed");
    }
    wgpuInstanceProcessEvents(instance);
    if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!failure.empty()) throw std::runtime_error(failure);
  if (!done) throw std::runtime_error("Callback timeout");
}
static void check_scope() {
  done = false;
  WGPUPopErrorScopeCallbackInfo cb{}; cb.mode = WGPUCallbackMode_AllowProcessEvents;
  cb.callback = [](WGPUPopErrorScopeStatus s, WGPUErrorType e, WGPUStringView m, void*, void*) {
    if (s != WGPUPopErrorScopeStatus_Success || e != WGPUErrorType_NoError) failure = str(m);
    done = true;
  };
  wgpuDevicePopErrorScope(device, cb); wait(false);
}
static std::vector<char> read(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("Cannot read " + path);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
static std::string quote(const std::string &s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c < 32) out += '?';
    else out += c;
  }
  return out + '"';
}
int main(int argc, char **argv) {
  try {
    // root name groups input-file output-file [scene-file]
    if (argc < 6) throw std::runtime_error("Usage: gpu_probe ROOT NAME GROUPS INPUT OUTPUT [SCENE]");
    const std::string root = argv[1], name = argv[2];
    const unsigned groups = std::stoul(argv[3]);
    if (wgpuCodebrixGetApiVersion() != WGPU_CODEBRIX_API_VERSION) throw std::runtime_error("Native ABI mismatch");
    Dl_info loaded{};
    if(!dladdr(reinterpret_cast<void*>(&wgpuCodebrixGetApiVersion),&loaded))
      throw std::runtime_error("Cannot identify loaded WebGPU library");
    WGPUInstanceExtras extras{}; extras.chain.sType = static_cast<WGPUSType>(WGPUSType_InstanceExtras);
    extras.backends = WGPUInstanceBackend_Vulkan;
    WGPUInstanceDescriptor desc{}; desc.nextInChain = &extras.chain;
    instance = wgpuCreateInstance(&desc);
    if (!instance) throw std::runtime_error("No instance");
    const size_t count = wgpuInstanceEnumerateAdapters(instance, nullptr, nullptr);
    std::vector<WGPUAdapter> adapters(count);
    wgpuInstanceEnumerateAdapters(instance, nullptr, adapters.data());
    WGPUAdapter adapter = nullptr;
    std::string adapter_name, driver;
    for (auto candidate : adapters) {
      WGPUAdapterInfo info{};
      if (wgpuAdapterGetInfo(candidate, &info) != WGPUStatus_Success) throw std::runtime_error("No adapter info");
      bool hardware = info.adapterType == WGPUAdapterType_IntegratedGPU || info.adapterType == WGPUAdapterType_DiscreteGPU;
      if (!adapter && hardware && str(info.device).find("llvmpipe") == std::string::npos) {
        adapter = candidate; adapter_name = str(info.device); driver = str(info.description);
      } else wgpuAdapterRelease(candidate);
      wgpuAdapterInfoFreeMembers(info);
    }
    if (!adapter) throw std::runtime_error("No hardware Vulkan adapter (software adapters rejected)");
    WGPUDeviceDescriptor dd{};
    dd.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType, WGPUStringView m, void*, void*) { failure = str(m); };
    dd.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    dd.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView m, void*, void*) {
      if (reason != WGPUDeviceLostReason_Destroyed) failure = "Device lost: " + str(m);
    };
    WGPURequestDeviceCallbackInfo request{}; request.mode = WGPUCallbackMode_AllowProcessEvents;
    request.callback = [](WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView m, void*, void*) {
      if (s != WGPURequestDeviceStatus_Success) failure = "Device request: " + str(m);
      device = d; done = true;
    };
    done = false; wgpuAdapterRequestDevice(adapter, &dd, request); wait(false);
    if (!device) throw std::runtime_error("No device");
    WGPULimits limits{}; wgpuDeviceGetLimits(device, &limits);
    auto queue = wgpuDeviceGetQueue(device);
    auto bytes = read(root + '/' + name + ".wgsl"); const std::string code(bytes.begin(), bytes.end());
    WGPUShaderSourceWGSL source{}; source.chain.sType = WGPUSType_ShaderSourceWGSL; source.code = view(code);
    WGPUShaderModuleDescriptor sm{}; sm.nextInChain = &source.chain;
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    auto shader = wgpuDeviceCreateShaderModule(device, &sm); check_scope();
    std::vector<WGPUBuffer> buffers;
    std::vector<WGPUBindGroupEntry> entries;
    for (int i = 4; i < argc; ++i) {
      auto data = read(root + '/' + argv[i]);
      WGPUBufferDescriptor bd{}; bd.size = data.size();
      bd.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
      auto b = wgpuDeviceCreateBuffer(device, &bd);
      wgpuQueueWriteBuffer(queue, b, 0, data.data(), data.size());
      buffers.push_back(b);
      WGPUBindGroupEntry entry{}; entry.binding = i-4; entry.buffer = b; entry.size = data.size();
      entries.push_back(entry);
    }
    WGPUComputePipelineDescriptor pd{}; pd.compute.module = shader; pd.compute.entryPoint = {"main",4};
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    auto pipeline = wgpuDeviceCreateComputePipeline(device, &pd); check_scope();
    auto layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupDescriptor bg{}; bg.layout = layout; bg.entryCount = entries.size(); bg.entries = entries.data();
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    auto group = wgpuDeviceCreateBindGroup(device, &bg); check_scope();
    WGPUBufferDescriptor rd{}; rd.size = entries[1].size; rd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    auto readback = wgpuDeviceCreateBuffer(device, &rd);
    auto encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
    auto pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, buffers[1], 0, readback, 0, rd.size);
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    auto command = wgpuCommandEncoderFinish(encoder, nullptr); check_scope();
    wgpuQueueSubmit(queue, 1, &command);
    WGPUBufferMapCallbackInfo map{}; map.mode = WGPUCallbackMode_AllowProcessEvents;
    map.callback = [](WGPUMapAsyncStatus s, WGPUStringView m, void*, void*) {
      if (s != WGPUMapAsyncStatus_Success) failure = "Map: " + str(m);
      done = true;
    };
    done = false; wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, rd.size, map); wait(true);
    const void *mapped = wgpuBufferGetConstMappedRange(readback, 0, rd.size);
    if (!mapped) throw std::runtime_error("No readback data");
    std::ofstream output(root + '/' + name + ".gpu.bin", std::ios::binary);
    output.write(static_cast<const char*>(mapped), rd.size);
    if (!output) throw std::runtime_error("Cannot save output");
    wgpuBufferUnmap(readback);
    WGPUCodebrixBuildInfo build{}; wgpuCodebrixGetBuildInfo(&build);
    std::cout << "{\"case\":" << quote(name) << ",\"adapter\":" << quote(adapter_name)
      << ",\"driver\":" << quote(driver) << ",\"nativeTarget\":" << quote(str(build.targetTriple))
      << ",\"loadedLibrary\":" << quote(loaded.dli_fname)
      << ",\"host\":\"native C++\",\"optionalFeatures\":0,\"enabledStorageBindings\":"
      << limits.maxStorageBuffersPerShaderStage << ",\"enabledBindingBytes\":"
      << limits.maxStorageBufferBindingSize << ",\"outputBytes\":" << rd.size << "}\n";
    wgpuCommandBufferRelease(command); wgpuComputePassEncoderRelease(pass); wgpuCommandEncoderRelease(encoder);
    wgpuBufferRelease(readback); wgpuBindGroupRelease(group); wgpuBindGroupLayoutRelease(layout);
    wgpuComputePipelineRelease(pipeline); wgpuShaderModuleRelease(shader);
    for (auto b : buffers) wgpuBufferRelease(b);
    wgpuQueueRelease(queue); wgpuDeviceRelease(device); wgpuAdapterRelease(adapter); wgpuInstanceRelease(instance);
    return 0;
  } catch (const std::exception &ex) { std::cerr << ex.what() << '\n'; return 1; }
}
