// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#ifndef CODEBRIX_CYCLES_HOST_EXPERIMENT_H
#define CODEBRIX_CYCLES_HOST_EXPERIMENT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Experimental ABI; not a released contract. Handles never expose C++ layouts. */
typedef uint64_t CBCHost;
typedef uint64_t CBCNode;
typedef uint64_t CBCImage;
/* CBC_HOST_METAL selects Cycles' own Metal device in Apple Silicon builds;
 * other builds return CBC_UNSUPPORTED for it. */
enum { CBC_HOST_ABI = 1, CBC_HOST_WEBGPU = 1, CBC_HOST_CPU_REFERENCE = 2, CBC_HOST_METAL = 3 };
enum { CBC_OK = 0, CBC_INVALID = 1, CBC_UNSUPPORTED = 2, CBC_FAILED = 3,
       CBC_CANCELLED = 4, CBC_NOT_READY = 5, CBC_SMALL_BUFFER = 6 };
typedef struct CBCHostConfig {
  uint32_t struct_size, abi_version, backend, threads;
  uint32_t reserved[4];
} CBCHostConfig;
typedef struct CBCRenderConfig {
  uint32_t struct_size, width, height, samples;
  uint32_t reserved[4];
} CBCRenderConfig;
/* Stable C value kinds; independent of native enum values or structure layout. */
enum { CBC_VALUE_UNSUPPORTED = 0, CBC_VALUE_BOOLEAN = 1, CBC_VALUE_INT = 2,
       CBC_VALUE_UINT = 3, CBC_VALUE_UINT64 = 4, CBC_VALUE_FLOAT = 5,
       CBC_VALUE_COLOR = 6, CBC_VALUE_VECTOR = 7, CBC_VALUE_POINT = 8,
       CBC_VALUE_NORMAL = 9, CBC_VALUE_POINT2 = 10, CBC_VALUE_STRING = 11,
       CBC_VALUE_ENUM = 12, CBC_VALUE_TRANSFORM = 13, CBC_VALUE_NODE = 14,
       CBC_VALUE_CLOSURE = 15, CBC_VALUE_FLOAT4 = 16, CBC_VALUE_BYTE4 = 17 };
enum { CBC_PROPERTY_ARRAY = 1, CBC_PROPERTY_LINKABLE = 2,
       CBC_PROPERTY_INTERNAL = 4, CBC_PROPERTY_READABLE = 8, CBC_PROPERTY_WRITABLE = 16 };
typedef struct CBCPropertyInfo {
  uint32_t struct_size, value_type, flags, components;
  uint64_t count, reserved;
} CBCPropertyInfo;
enum { CBC_ATTR_MESH = 1, CBC_ATTR_FACE = 2, CBC_ATTR_VERTEX = 3,
       CBC_ATTR_CORNER = 4, CBC_ATTR_CURVE = 5, CBC_ATTR_CURVE_KEY = 6,
       CBC_ATTR_VERTEX_NORMAL = 7, CBC_ATTR_CORNER_NORMAL = 8,
       CBC_ATTR_CORNER_BYTE = 9, CBC_ATTR_CURVE_KEY_NORMAL = 10 };
enum { CBC_ATTR_STANDARD = 1, CBC_ATTR_SUBDIVISION = 2 };
typedef struct CBCAttributeConfig {
  uint32_t struct_size, value_type, domain, flags;
  uint32_t reserved[4];
} CBCAttributeConfig;
enum { CBC_IMAGE_FLOAT = 1, CBC_IMAGE_FLOAT4 = 2, CBC_IMAGE_HALF = 3,
       CBC_IMAGE_HALF4 = 4, CBC_IMAGE_BYTE = 5, CBC_IMAGE_BYTE4 = 6,
       CBC_IMAGE_USHORT = 7, CBC_IMAGE_USHORT4 = 8 };
typedef struct CBCImageConfig {
  uint32_t struct_size, width, height, format;
  uint32_t reserved[4];
} CBCImageConfig;

uint32_t cbc_host_abi_version(void);
/* Selects the process-wide native OCIO configuration. Call before creating a
 * host or after every host and its in-flight operations have finished. Changes
 * while a host exists return CBC_NOT_READY. Failed loads retain the old config. */
int cbc_host_color_config_path(const char *utf8_path);
/* Error is thread-local, UTF-8, and copied. required includes the trailing NUL. */
int cbc_host_error(char *buffer, uint64_t capacity, uint64_t *required);
int cbc_host_create(const CBCHostConfig *config, CBCHost *out);
/* Removes the handle immediately, cancels active work, then waits for cleanup. */
int cbc_host_destroy(CBCHost host);
int cbc_host_cancel(CBCHost host);
int cbc_host_is_rendering(CBCHost host, uint32_t *rendering);
/* Existing singleton nodes: camera, background, film, integrator. */
int cbc_host_scene_node(CBCHost host, const char *name, CBCNode *out);
int cbc_host_create_node(CBCHost host, const char *type, CBCNode *out);
int cbc_host_shader_output(CBCHost host, CBCNode shader, CBCNode *out);
int cbc_host_shader_node(CBCHost host, CBCNode shader, const char *type, CBCNode *out);
int cbc_host_connect(CBCHost host, CBCNode from, const char *output, CBCNode to, const char *input);
/* Values are copied before returning. Socket names are native registration names. */
int cbc_host_set_floats(CBCHost host, CBCNode node, const char *socket, const float *values, uint64_t count);
int cbc_host_set_ints(CBCHost host, CBCNode node, const char *socket, const int64_t *values, uint64_t count);
int cbc_host_set_string(CBCHost host, CBCNode node, const char *socket, const char *utf8);
int cbc_host_set_nodes(CBCHost host, CBCNode node, const char *socket, const CBCNode *values, uint64_t count);
/* Arrays use flattened components: float2=2, float3=3, transform=12 row-major.
 * Getters set required even when capacity is insufficient, leave the destination
 * unchanged on failure, and accept NULL with capacity zero for a size query.
 * required for strings/JSON includes NUL. Query and copy are separate operations;
 * callers must handle another edit changing the required size between calls. */
int cbc_host_property_info(CBCHost host, CBCNode node, const char *socket, CBCPropertyInfo *info);
int cbc_host_get_floats(CBCHost host, CBCNode node, const char *socket, float *values, uint64_t capacity, uint64_t *required);
int cbc_host_get_ints(CBCHost host, CBCNode node, const char *socket, int64_t *values, uint64_t capacity, uint64_t *required);
int cbc_host_set_uints(CBCHost host, CBCNode node, const char *socket, const uint64_t *values, uint64_t count);
int cbc_host_get_uints(CBCHost host, CBCNode node, const char *socket, uint64_t *values, uint64_t capacity, uint64_t *required);
int cbc_host_set_strings(CBCHost host, CBCNode node, const char *socket, const char *const *values, uint64_t count);
int cbc_host_get_string(CBCHost host, CBCNode node, const char *socket, uint64_t index, char *value, uint64_t capacity, uint64_t *required);
int cbc_host_get_nodes(CBCHost host, CBCNode node, const char *socket, CBCNode *values, uint64_t capacity, uint64_t *required);
int cbc_host_node_type(CBCHost host, CBCNode node, char *name, uint64_t capacity, uint64_t *required);
/* Registration metadata describes native properties, not WebGPU feature support. */
int cbc_host_registry_json(char *json, uint64_t capacity, uint64_t *required);
/* Attribute element counts derive from current geometry. Values are copied.
 * Standard mode currently accepts P, N, uv, generated, radius, tangent and
 * tangent_sign for applicable geometry/domain combinations. Other names create
 * custom attributes. Normal domains use native packed-normal storage; readback
 * returns its decoded direction. Existing topology must be established first. */
int cbc_host_attribute_floats(CBCHost host, CBCNode geometry, const char *name, const CBCAttributeConfig *config, const float *values, uint64_t count);
int cbc_host_attribute_bytes(CBCHost host, CBCNode geometry, const char *name, const CBCAttributeConfig *config, const uint8_t *values, uint64_t count);
int cbc_host_attribute_get_floats(CBCHost host, CBCNode geometry, const char *name, uint32_t flags, float *values, uint64_t capacity, uint64_t *required);
int cbc_host_attribute_get_bytes(CBCHost host, CBCNode geometry, const char *name, uint32_t flags, uint8_t *values, uint64_t capacity, uint64_t *required);
int cbc_host_attribute_remove(CBCHost host, CBCNode geometry, const char *name, uint32_t flags);
/* Copies a tightly packed 2D image, with row zero at v=0. Float/half inputs use
 * native endian storage. Sampling, alpha and colorspace come from the bound
 * native image/environment node properties. Releasing an image handle does not
 * invalidate graph bindings. Binding zero detaches memory input and restores
 * native filename loading, except in builds without image file readers, which
 * reject texture filenames with CBC_UNSUPPORTED. A new immutable image can
 * replace an existing binding. */
int cbc_host_image_create(CBCHost host, const CBCImageConfig *config, const void *pixels, uint64_t byte_count, CBCImage *image);
int cbc_host_image_bind(CBCHost host, CBCNode texture_node, CBCImage image);
int cbc_host_image_release(CBCHost host, CBCImage image);
/* Triangle geometry is validated and copied. Every face initially uses material 0. */
int cbc_host_mesh(CBCHost host, CBCNode mesh, const float *xyz, uint64_t vertex_count,
                  const uint32_t *indices, uint64_t triangle_count);
/* Synchronous scene-linear RGBA, lower-left origin. Output changes only on success.
 * All edits serialize with rendering. Cancel and destruction may run concurrently.
 * A requested WebGPU render never falls back to CPU rendering. */
int cbc_host_render(CBCHost host, const CBCRenderConfig *config, float *rgba, uint64_t float_count);
#ifdef __cplusplus
}
#endif
#endif
