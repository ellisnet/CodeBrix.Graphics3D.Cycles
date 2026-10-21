// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#ifndef CODEBRIX_CYCLES_HOST_EXPERIMENT_H
#define CODEBRIX_CYCLES_HOST_EXPERIMENT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Experimental ABI; not a released contract. Handles never expose C++ layouts.
 * The consumer reference is ABI.md beside this header.
 * Additive only: add functions, enum values and trailing struct fields; never
 * change or renumber existing signatures, enum values, error codes or layouts.
 * A breaking change bumps CBC_HOST_ABI. */
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
/* WebGPU pipeline tier selection (graceful degradation; native/DEVICE.md).
 * Process-wide; applies to WebGPU hosts created after the call. tier is
 * CBC_TIER_AUTO (explicit request > persisted device record > known-adapter
 * table > full), CBC_TIER_FULL or CBC_TIER_CONSERVATIVE. record_directory is an
 * app-writable directory for the persisted device-identity record (created if
 * missing; NULL or empty disables the record, as does CBC_TIER_RECORD_OFF).
 * CBC_TIER_CANARY compiles one representative module at device creation; its
 * failure selects the conservative tier before the scene compiles. A slow
 * compile never changes the tier. Environment variables documented in
 * native/DEVICE.md override these settings for gates. */
enum { CBC_TIER_AUTO = 0, CBC_TIER_FULL = 1, CBC_TIER_CONSERVATIVE = 2 };
enum { CBC_TIER_RECORD_OFF = 1, CBC_TIER_RECORD_READ_ONLY = 2, CBC_TIER_CANARY = 4 };
typedef struct CBCTierConfig {
  uint32_t struct_size, abi_version, tier, flags;
  uint32_t reserved[4];
} CBCTierConfig;
int cbc_host_tier_config(const CBCTierConfig *config, const char *utf8_record_directory);
/* Capability report for the host's device as JSON (required includes NUL):
 * backend, adapter, chosen tier and why (override, opt-in, record, table,
 * default, canary, failure), record path/status, canary result, compile
 * budget, warnings and compiled pipelines. Serializes with rendering. */
int cbc_host_device_report_json(CBCHost host, char *json, uint64_t capacity, uint64_t *required);
/* Synchronous scene-linear RGBA, lower-left origin. Output changes only on success.
 * All edits serialize with rendering. Cancel and destruction may run concurrently.
 * A requested WebGPU render never falls back to CPU rendering. */
int cbc_host_render(CBCHost host, const CBCRenderConfig *config, float *rgba, uint64_t float_count);

/* ---- Additions to ABI 1 (Phase D). Every struct below is size-checked. ---- */

/* Library identity. library_version is "<major>.<minor>.<yyyyMMdd>.<build>",
 * stamped by the native build; cycles_revision is the pinned upstream Cycles
 * commit. Both are NUL-terminated ASCII. Callable at any time, including from
 * an event callback. */
typedef struct CBCVersionInfo {
  uint32_t struct_size, abi_version;
  uint32_t reserved[2];
  char library_version[32];
  char cycles_revision[48];
} CBCVersionInfo;
int cbc_host_version(CBCVersionInfo *info);

/* Backend-neutral GPU request: Cycles' Metal device in macOS builds, WebGPU in
 * every other build. Accepted by cbc_host_create and the queries below. */
enum { CBC_HOST_GPU = 4 };
/* Maps a requested backend (CBC_HOST_GPU, CBC_HOST_WEBGPU, CBC_HOST_METAL or
 * CBC_HOST_CPU_REFERENCE) to the concrete backend this binary would create.
 * Returns CBC_UNSUPPORTED when the backend is not compiled into this binary.
 * It does not probe hardware; cbc_host_create reports unavailable devices. */
int cbc_host_resolve_backend(uint32_t requested, uint32_t *resolved);
/* The concrete backend of an existing host (never CBC_HOST_GPU). */
int cbc_host_backend(CBCHost host, uint32_t *backend);
/* Capability report as JSON (required includes NUL). host may be 0 for the
 * binary-level report (version, compiled backends, image formats, pass types,
 * limits, determinism reference); a host adds its backend, device identity and
 * the WebGPU tier decision (cbc_host_device_report_json). See ABI.md. */
int cbc_host_capabilities_json(CBCHost host, char *json, uint64_t capacity, uint64_t *required);

/* Deletes a node and invalidates its handle. Scene nodes: objects, geometry
 * (mesh, hair, pointcloud, volume), lights, particle systems, passes and
 * shaders made by cbc_host_create_node; deleting a shader also invalidates the
 * handles of its graph nodes. Shader graph nodes are disconnected and removed;
 * the shader output node and the scene singletons cannot be deleted. A node
 * that another node's socket still references is rejected with CBC_INVALID;
 * clear the reference first. Serializes with rendering. */
int cbc_host_delete_node(CBCHost host, CBCNode node);

/* Replaces the pixels of an existing image handle (same rules and formats as
 * cbc_host_image_create; size and format may change). Every texture still bound
 * to this handle uses the new pixels on the next render; bindings made before
 * cbc_host_image_release keep the pixels they had at release. */
int cbc_host_image_update(CBCHost host, CBCImage image, const CBCImageConfig *config, const void *pixels, uint64_t byte_count);

/* Deformation motion for a mesh: steps (odd, 3..129) time-ordered position
 * arrays of vertex_count*3 floats; the center step must equal the current
 * positions bit for bit. steps == 1 with count 0 removes deformation motion.
 * Sets the mesh motion_steps/use_motion_blur sockets; Cycles regenerates motion
 * vertex normals. Call again after cbc_host_mesh changes the vertex count.
 * Object and camera motion use the generic "motion" transform-array sockets. */
int cbc_host_motion_positions(CBCHost host, CBCNode mesh, uint32_t steps, const float *xyz, uint64_t count);

/* Render events. A callback runs on a native render thread or the thread that
 * called cbc_host_render, never concurrently with itself for the same host,
 * and only while cbc_host_render is executing. It must not call any cbc_host_
 * function other than cbc_host_error and cbc_host_version: such calls return
 * CBC_NOT_READY. Returning nonzero requests cancellation (cbc_host_render then
 * returns CBC_CANCELLED). message is UTF-8 and valid only during the call. */
enum { CBC_EVENT_STATUS = 1, CBC_EVENT_PROGRESS = 2, CBC_EVENT_ERROR = 3 };
typedef struct CBCEvent {
  uint32_t struct_size, kind;
  uint64_t samples_done, samples_total;
  double elapsed_seconds;
  const char *message;
} CBCEvent;
typedef int (*CBCEventCallback)(void *user_data, const CBCEvent *event);
/* Registers (or with NULL clears) the host's callback. Serializes with
 * rendering; after it returns the previous callback is never invoked again. */
int cbc_host_set_event_callback(CBCHost host, CBCEventCallback callback, void *user_data);

/* Pass readback from the most recent successful cbc_host_render. Enable a pass
 * with cbc_host_create_node("pass") and its "type"/"name" sockets; passes are
 * read by name ("Combined" is always present). Values are copied raw
 * scene-linear float32, lower-left origin, components per pixel as reported.
 * A render that starts clears the previous results (CBC_NOT_READY until a
 * render succeeds). */
int cbc_host_pass_floats(CBCHost host, const char *name, float *values, uint64_t capacity, uint64_t *required, uint32_t *components);
/* JSON list of the passes captured by the most recent successful render. */
int cbc_host_render_passes_json(CBCHost host, char *json, uint64_t capacity, uint64_t *required);
#ifdef __cplusplus
}
#endif
#endif
