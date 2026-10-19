// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
# define CBC_API __declspec(dllexport)
#else
# define CBC_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CBCContext CBCContext;
typedef struct CBCCamera {
  float origin[3], forward[3], right[3], up[3];
  float tan_half_fov, aspect;
  float light_position[3], light_power;
  float background[3];
} CBCCamera;

// Experimental ABI 1. All functions report failure instead of throwing across C.
// Error strings and adapter strings are copied into caller-owned storage.
CBC_API uint32_t cbc_abi_version(void);
CBC_API int cbc_create(CBCContext **context, char *error, size_t error_capacity);
CBC_API void cbc_destroy(CBCContext *context);
CBC_API int cbc_adapter(CBCContext *context, char *text, size_t capacity);
// Packed static-triangle scene described in prototype/README.md. Copies bytes.
// Caller memory can be released after this call. Unsupported structures fail.
CBC_API int cbc_set_scene(CBCContext *context, const void *data, size_t bytes,
                         char *error, size_t error_capacity);
// Synchronous linear RGBA output, row-major, exactly width*height*4 floats.
// Limited to 512x512 and 1..64 samples. No implicit software fallback.
CBC_API int cbc_render(CBCContext *context, const CBCCamera *camera,
                      uint32_t width, uint32_t height, uint32_t samples,
                      float *rgba, size_t float_count, char *error, size_t error_capacity);
#ifdef __cplusplus
}
#endif
