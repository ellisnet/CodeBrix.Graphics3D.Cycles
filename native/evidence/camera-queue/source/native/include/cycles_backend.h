// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
# define CBC_BACKEND_API __declspec(dllexport)
#else
# define CBC_BACKEND_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define CBC_BACKEND_ABI_VERSION 1u
typedef struct CBCBackend CBCBackend;
typedef enum CBCBackendStatus {
  CBC_BACKEND_OK=0, CBC_BACKEND_INVALID=1, CBC_BACKEND_UNSUPPORTED=2,
  CBC_BACKEND_GPU_ERROR=3, CBC_BACKEND_NOT_READY=4
} CBCBackendStatus;
typedef struct CBCBackendConfig {
  uint32_t struct_size, abi_version, batch_paths, reserved;
} CBCBackendConfig;
// Experimental snapshot format 1: static triangle BVH2, supported SVM closures,
// static perspective/tabulated Sobol camera, constant spherical lamps with MIS.
// Tables are copied. Caller memory may be freed immediately after set_scene.
typedef struct CBCBackendScene {
  uint32_t struct_size, format_version;
  const void *geometry; size_t geometry_bytes;
  const void *camera; size_t camera_bytes;
  const void *lights; size_t light_bytes;
} CBCBackendScene;
typedef struct CBCBackendStats {
  uint32_t struct_size, width, height, samples;
  uint32_t bounces, batch_paths, batches, dispatches;
  uint64_t scene_bytes, scratch_bytes, film_bytes, readback_bytes;
  uint64_t pipeline_creations, buffer_creations, completed_renders, scene_generation;
} CBCBackendStats;
CBC_BACKEND_API uint32_t cbc_backend_abi_version(void);
CBC_BACKEND_API int cbc_backend_create(const CBCBackendConfig *config,CBCBackend **out,char *error,size_t capacity);
CBC_BACKEND_API void cbc_backend_destroy(CBCBackend *backend);
CBC_BACKEND_API int cbc_backend_adapter(CBCBackend *backend,char *out,size_t capacity);
CBC_BACKEND_API int cbc_backend_webgpu_library(CBCBackend *backend,char *out,size_t capacity);
// Failed validation leaves the previous scene and GPU queue resources intact.
CBC_BACKEND_API int cbc_backend_set_scene(CBCBackend *backend,const CBCBackendScene *scene,char *error,size_t capacity);
// Synchronous linear RGBA, Cycles' lower-left film origin. Overwrites output only
// after successful readback/validation. The context serializes API operations;
// destroy must not race another call. No software renderer fallback.
CBC_BACKEND_API int cbc_backend_render(CBCBackend *backend,float *rgba,size_t float_count,
                                     CBCBackendStats *stats,char *error,size_t capacity);
#ifdef __cplusplus
}
#endif
