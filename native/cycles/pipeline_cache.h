// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Persistent WebGPU pipeline cache: the driver's compiled code for this adapter,
// reloaded by later processes so that a pipeline compiled once is not compiled
// again. It is an optimization only: data that is missing, damaged, outdated or
// from another adapter is ignored and rebuilt, never an error, and it cannot change
// a film, because a driver returns the same pipeline from it as from a compile.
//
// Fingerprint mode (fingerprint.h) never has a cache: reason "fingerprint-mode".
// Which directory (first match wins):
//   1. CODEBRIX_CYCLES_PIPELINE_CACHE=off, then CODEBRIX_CYCLES_PIPELINE_CACHE_DIR=<dir>
//      (gates and validation runners; the host never sets either);
//   2. the host API's switch and directory (cbc_host_pipeline_cache_config);
//   3. the record directory of the host API's tier configuration;
//   4. only in a process that created a host (allow_default_pipeline_cache) and on
//      Linux: the user's cache directory, $XDG_CACHE_HOME or else ~/.cache, then
//      cycles/webgpu-pipelines, the root Cycles' Metal device caches its kernels in
//      (util/path.cpp path_cache_get). Android has no user cache directory.
// Otherwise there is no cache, so the gate programs, which link the device without
// the host, compile cold unless a run asks for a cache.
//
// Only adapters the runtime offers the feature on (Vulkan) have a cache; everywhere
// else the same API is reported as not active. One file per pipeline (WGSL and entry
// point): <directory>/<adapter cache key>/<hash>.bin, loaded just before that pipeline
// is created and saved at once if the driver added anything. So a process reads only
// what its scene needs, processes sharing a directory lose no work, and the oldest
// pipelines can be dropped at the size limit. Format and costs: native/DEVICE.md
// "Pipeline cache".
#include <cstddef>
#include <cstdint>
#include <string>
#include "webgpu_device.h"

namespace ccl {
struct PipelineCacheConfig {bool off=false;std::string directory;};
void set_pipeline_cache_config(const PipelineCacheConfig &config);
// Called by the host library before it creates a device.
void allow_default_pipeline_cache();
// Whether a device created now would use a cache (asks the runtime for the feature).
bool pipeline_cache_wanted();

class PipelineDiskCache {
 public:
  // Never throws: every failure leaves the device without a cache and says why.
  void open(cbc::Device &gpu);
  std::string report_json() const;
  // One pipeline creation: construct it just before cbc::Device::pipeline with its
  // cache() and file(), call compiled() after it. Never throws.
  class Use {
   public:
    Use(PipelineDiskCache &owner,const std::string &code,const char *entry);
    ~Use();
    Use(const Use&)=delete;Use &operator=(const Use&)=delete;
    WGPUCodebrixPipelineCache cache() const{return handle;}
    const char *file() const{return handle?path.c_str():nullptr;}
    void compiled();
    // Whether the pipeline came from data already on disk.
    bool hit=false;
   private:
    PipelineDiskCache &owner;
    WGPUCodebrixPipelineCache handle=nullptr;
    std::string path;
    size_t known_bytes=0;
    bool loaded=false;
  };
 private:
  void enforce_limit();
  cbc::Device *device=nullptr;
  std::string directory,source,reason="off",folder,key,failure,runtime;
  size_t max_bytes=0;
  uint64_t hits=0,misses=0,damaged=0,other_adapter=0,other_runtime=0,rejected=0,outdated=0,saves=0,save_failures=0,saved_bytes=0,loaded_bytes=0;
  double load_ms=0,save_ms=0;
  bool active=false;
};
}
