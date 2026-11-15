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
/* PROVISIONAL (ABI.md "Progressive results"): flags and reserved2 are trailing fields; the library accepts struct_size
 * 32 (no flags) and 40. CBC_HOST_PROGRESSIVE_RESULTS creates the host's session with Cycles' display updates (background
 * scheduling, not headless) and sends CBC_EVENT_FILM events during its renders; other flag bits and a nonzero
 * reserved2 are CBC_INVALID. A positional initializer names every field, for example
 * {sizeof(config), CBC_HOST_ABI, CBC_HOST_GPU, 0, {0}, 0, 0}. */
enum { CBC_HOST_PROGRESSIVE_RESULTS = 1 };
typedef struct CBCHostConfig {
  uint32_t struct_size, abi_version, backend, threads;
  uint32_t reserved[4];
  uint32_t flags;      /* PROVISIONAL: CBC_HOST_* flags */
  uint32_t reserved2;  /* PROVISIONAL: must be zero */
} CBCHostConfig;
typedef struct CBCRenderConfig {
  uint32_t struct_size, width, height, samples;
  uint32_t reserved[4];
} CBCRenderConfig;
/* Stable C value kinds; independent of native enum values or structure layout.
 * PROVISIONAL (ABI.md): CBC_VALUE_QUATERNION and CBC_VALUE_SPHERICAL_HARMONICS_REST are geometry attribute kinds only. A
 * quaternion is 4 floats per element in Cycles' order w, x, y, z. Spherical-harmonics "rest" coefficients are 45 floats
 * per element: the 15 RGB coefficients of bands 1 to 3 in Cycles' order (L1 M-1, L1 M0, L1 M1, L2 M-2, ..., L3 M3), each
 * in [-1, 1]; Cycles stores them at 8 bits over that range. */
enum { CBC_VALUE_UNSUPPORTED = 0, CBC_VALUE_BOOLEAN = 1, CBC_VALUE_INT = 2,
       CBC_VALUE_UINT = 3, CBC_VALUE_UINT64 = 4, CBC_VALUE_FLOAT = 5,
       CBC_VALUE_COLOR = 6, CBC_VALUE_VECTOR = 7, CBC_VALUE_POINT = 8,
       CBC_VALUE_NORMAL = 9, CBC_VALUE_POINT2 = 10, CBC_VALUE_STRING = 11,
       CBC_VALUE_ENUM = 12, CBC_VALUE_TRANSFORM = 13, CBC_VALUE_NODE = 14,
       CBC_VALUE_CLOSURE = 15, CBC_VALUE_FLOAT4 = 16, CBC_VALUE_BYTE4 = 17,
       CBC_VALUE_QUATERNION = 18, CBC_VALUE_SPHERICAL_HARMONICS_REST = 19 };
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
/* CBC_ATTR_SPLAT_BOUNDING_RADIUS (PROVISIONAL, ABI.md "Point clouds"): cbc_host_attribute_get_floats only; reads the
 * bounding radius Cycles computed for each splat of a Gaussian splat cloud (render_as gaussian_splats) at the last
 * render or bake, instead of the authored radius. Name "radius"; CBC_NOT_READY until a render has computed it for the
 * cloud as it is (before the first render and after an edit of the cloud). */
enum { CBC_ATTR_STANDARD = 1, CBC_ATTR_SUBDIVISION = 2, CBC_ATTR_SPLAT_BOUNDING_RADIUS = 4 };
typedef struct CBCAttributeConfig {
  uint32_t struct_size, value_type, domain, flags;
  uint32_t reserved[4];
} CBCAttributeConfig;
enum { CBC_IMAGE_FLOAT = 1, CBC_IMAGE_FLOAT4 = 2, CBC_IMAGE_HALF = 3,
       CBC_IMAGE_HALF4 = 4, CBC_IMAGE_BYTE = 5, CBC_IMAGE_BYTE4 = 6,
       CBC_IMAGE_USHORT = 7, CBC_IMAGE_USHORT4 = 8 };
/* tile_size (PROVISIONAL): cbc_host_image_create_tiled only, the side of the square tiles Cycles' texture cache loads:
 * 0 (64, the default) or a power of two from 64 to 128 on the Metal backend and to 1024 on the others (ABI.md "Tiled
 * images"); 0 for cbc_host_image_create and cbc_host_image_update. */
typedef struct CBCImageConfig {
  uint32_t struct_size, width, height, format;
  uint32_t tile_size;
  uint32_t reserved[3];
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
/* Existing singleton nodes: camera, background, film, integrator, scene_attributes (the Scene Time node's time and frame). */
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
 * Standard mode accepts the names ABI.md lists per geometry (P, N, uv, generated,
 * radius, tangent, tangent_sign, ...) for applicable geometry/domain combinations.
 * Other names create custom attributes. Normal domains use native packed-normal
 * storage; readback returns its decoded direction. Existing topology must be
 * established first. Quaternion attributes (PROVISIONAL) are accepted on meshes,
 * point clouds and curves (CBC_ATTR_CURVE, CBC_ATTR_CURVE_KEY, CBC_ATTR_MESH),
 * spherical-harmonics attributes (PROVISIONAL) on meshes and point clouds only;
 * spherical-harmonics values outside [-1, 1] are CBC_INVALID, and readback returns
 * the stored 8-bit value.
 * PROVISIONAL (ABI.md): on point clouds the standard names radiance:base
 * (CBC_VALUE_FLOAT4), scale (CBC_VALUE_VECTOR), rotation (CBC_VALUE_QUATERNION) and
 * radiance:sh (CBC_VALUE_SPHERICAL_HARMONICS_REST), on CBC_ATTR_VERTEX, are the
 * attributes of a Gaussian splat (render_as gaussian_splats). */
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
/* Curve geometry, like cbc_host_mesh: xyz has 3 floats and radius 1 float per key; first_key has one index per curve,
 * the first 0 and strictly increasing, each curve running to the next one's first key (key_count for the last). Values
 * are validated and copied; every curve initially uses material 0 (the curve_shader socket). Replaces the geometry;
 * further curve attributes follow it, since their size derives from it. */
int cbc_host_curves(CBCHost host, CBCNode hair, const float *xyz, const float *radius, uint64_t key_count,
                    const uint32_t *first_key, uint64_t curve_count);
/* Deformation motion for a hair geometry, as cbc_host_motion_positions for a mesh: steps (odd, 3..129) time-ordered key
 * arrays, count = steps * key_count keys (xyz 3 floats per key), the center step equal to the current keys bit for bit.
 * radius holds one float per key per step (its center step equal to the current radii), or is null to keep the center
 * radii at every step. steps == 1 with count 0 removes it. Sets the geometry's motion_steps and use_motion_blur sockets.
 * Call it again after cbc_host_curves changes the key count. */
int cbc_host_curve_motion(CBCHost host, CBCNode hair, uint32_t steps, const float *xyz, const float *radius, uint64_t count);
/* How a hair geometry's curves are rendered: flat ribbons facing the ray (the default), Catmull-Rom thick curves, or
 * linear thick segments. Values match Cycles' CurveShapeType. */
enum { CBC_CURVE_RIBBON = 0, CBC_CURVE_THICK = 1, CBC_CURVE_THICK_LINEAR = 2 };
int cbc_host_curve_shape(CBCHost host, CBCNode hair, uint32_t shape);
/* Scene-wide ribbon tessellation: 2^subdivisions sections per curve segment, 0 to 4 (default 3). Cycles' hair_subdivisions. */
int cbc_host_curve_subdivisions(CBCHost host, uint32_t subdivisions);
/* Point cloud geometry, like cbc_host_curves: xyz has 3 floats and radius 1 float per point. Values are validated and
 * copied; every point initially uses material 0 (the shader socket). Replaces the geometry and removes its motion;
 * further point attributes follow it, since their size derives from it. A cloud rendered as Gaussian splats
 * (PROVISIONAL) is bounded by the radius Cycles computes from radiance:base and scale; reads of radius and the center
 * step of cbc_host_point_motion keep the radius given here. */
int cbc_host_points(CBCHost host, CBCNode pointcloud, const float *xyz, const float *radius, uint64_t count);
/* Deformation motion for a point cloud: steps (odd, 3..129) time-ordered arrays, count = steps * point_count entries,
 * 3 floats per position and 1 float per radius; the center step must equal the points bit for bit. steps == 1 with
 * count 0 removes the motion. Sets the motion_steps and use_motion_blur sockets. */
int cbc_host_point_motion(CBCHost host, CBCNode pointcloud, uint32_t steps, const float *xyz, const float *radius, uint64_t count);
/* The particles of a particle system, read by the Particle Info node of objects whose particle_system and
 * particle_index sockets name this system and a particle in it. index has 1 value per particle; values has
 * CBC_PARTICLE_FLOATS floats per particle: age, lifetime, location xyz, rotation xyzw, size, velocity xyz, angular velocity xyz. */
enum { CBC_PARTICLE_FLOATS = 16 };
int cbc_host_particles(CBCHost host, CBCNode particle_system, const uint32_t *index, const float *values, uint64_t count);
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
 * budget, warnings, compiled pipelines and the pipeline cache's state.
 * Serializes with rendering. */
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

/* Backend-neutral GPU request: Cycles' Metal device in the osx-arm64 (Apple
 * Silicon) build, WebGPU in every other build, osx-x64 (Intel macOS) included.
 * Accepted by cbc_host_create and the queries below. */
enum { CBC_HOST_GPU = 4 };
/* The Cycles CPU device with Cycles' own BVH choice: Embree in builds that include it (every
 * shipped RID). CBC_HOST_CPU_REFERENCE keeps Cycles' BVH2 (the reference whose films the GPU
 * backends are validated against). CBC_UNSUPPORTED in a build without Embree. */
enum { CBC_HOST_CPU = 5 };
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
 * returns CBC_CANCELLED). message is UTF-8 and valid only during the call.
 * PROVISIONAL: CBC_EVENT_FILM, sent only by hosts created with
 * CBC_HOST_PROGRESSIVE_RESULTS during cbc_host_render (never a bake): film holds
 * width * height * 4 floats, the Combined film so far (scene-linear RGBA,
 * lower-left origin, zero outside a camera border, samples_done samples), valid
 * only during the call; film is NULL and width, height 0 for the other kinds.
 * struct_size is 56 with the trailing fields; read only fields that fit. */
enum { CBC_EVENT_STATUS = 1, CBC_EVENT_PROGRESS = 2, CBC_EVENT_ERROR = 3, CBC_EVENT_FILM = 4 };
typedef struct CBCEvent {
  uint32_t struct_size, kind;
  uint64_t samples_done, samples_total;
  double elapsed_seconds;
  const char *message;
  const float *film;      /* PROVISIONAL, CBC_EVENT_FILM only */
  uint32_t width, height; /* PROVISIONAL, CBC_EVENT_FILM only */
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

/* Persistent WebGPU pipeline cache (ABI.md "Pipeline cache"). Process-wide;
 * applies to WebGPU hosts created after the call. With CBC_PIPELINE_CACHE_DEFAULT
 * compiled pipelines are kept in utf8_directory (created if missing); NULL or
 * empty means the record directory of cbc_host_tier_config if one is set, else
 * the user's cache directory on Linux, else no cache (Android has no such
 * directory, so an Android app passes one). CBC_PIPELINE_CACHE_OFF turns caching
 * off. Only Vulkan adapters have a cache; elsewhere the call succeeds and the
 * device report says it is not active. Damaged, outdated or foreign cache data
 * is ignored and rebuilt, never an error. The result is in the report's
 * "pipelineCache" object. Environment variables documented in native/DEVICE.md
 * override these settings for gates. */
enum { CBC_PIPELINE_CACHE_DEFAULT = 0, CBC_PIPELINE_CACHE_OFF = 1 };
typedef struct CBCPipelineCacheConfig {
  uint32_t struct_size, abi_version, mode, flags;
  uint32_t reserved[4];
} CBCPipelineCacheConfig;
int cbc_host_pipeline_cache_config(const CBCPipelineCacheConfig *config, const char *utf8_directory);

/* ---- Baking (additions to ABI 1). See ABI.md "Baking". ---- */

/* What a bake writes, one per bake. COMBINED has 4 components (RGBA, alpha 0 on
 * pixels no triangle covers); ROUGHNESS, OBJECT_ID, MATERIAL_ID, DEPTH, MIST and
 * AOV_VALUE have 1; AOV_COLOR 4; the others 3. BACKGROUND evaluates the world in
 * the direction of each surface point seen from the origin. */
enum { CBC_BAKE_COMBINED = 1, CBC_BAKE_EMISSION = 2, CBC_BAKE_BACKGROUND = 3, CBC_BAKE_AO = 4,
       CBC_BAKE_DIFFUSE = 5, CBC_BAKE_DIFFUSE_DIRECT = 6, CBC_BAKE_DIFFUSE_INDIRECT = 7, CBC_BAKE_DIFFUSE_COLOR = 8,
       CBC_BAKE_GLOSSY = 9, CBC_BAKE_GLOSSY_DIRECT = 10, CBC_BAKE_GLOSSY_INDIRECT = 11, CBC_BAKE_GLOSSY_COLOR = 12,
       CBC_BAKE_TRANSMISSION = 13, CBC_BAKE_TRANSMISSION_DIRECT = 14, CBC_BAKE_TRANSMISSION_INDIRECT = 15, CBC_BAKE_TRANSMISSION_COLOR = 16,
       CBC_BAKE_POSITION = 17, CBC_BAKE_NORMAL = 18, CBC_BAKE_UV = 19, CBC_BAKE_ROUGHNESS = 20,
       CBC_BAKE_OBJECT_ID = 21, CBC_BAKE_MATERIAL_ID = 22, CBC_BAKE_DEPTH = 23, CBC_BAKE_MIST = 24,
       CBC_BAKE_AOV_COLOR = 25, CBC_BAKE_AOV_VALUE = 26 };
/* USE_CAMERA: the view direction points at the scene camera instead of along the
 * normal. INCLUDE_ALBEDO: light passes keep the surface colour. */
enum { CBC_BAKE_USE_CAMERA = 1, CBC_BAKE_INCLUDE_ALBEDO = 2 };
typedef struct CBCBakeConfig {
  uint32_t struct_size, width, height, samples;
  uint32_t type, flags;
  uint32_t reserved[2];
  CBCNode object;           /* An object whose geometry is a triangle mesh or (PROVISIONAL) a linear subdivision cage diced in object space. */
  const char *uv_attribute; /* NULL or empty: the standard "uv" attribute. A float2 corner or vertex attribute (of a cage: a subdivision attribute). */
  const char *aov_name;     /* AOV types only: the name of an existing AOV pass node of that type. */
  uint64_t reserved2;
} CBCBakeConfig;
/* Bakes one mesh's UV layout into raw float32, values[(y * width + x) * components],
 * row 0 at v = 0: pixel (x, y) is sampled at UV ((x + 0.5) / width, (y + 0.5) / height).
 * Pixels no triangle covers read zero. required (floats) and components are always
 * set; NULL with capacity 0 is a size query that returns CBC_SMALL_BUFFER and does
 * not bake. Synchronous; events, cancel and threading are those of cbc_host_render.
 * A bake clears the results of the last render (cbc_host_pass_floats then returns
 * CBC_NOT_READY until a render succeeds). Every backend bakes; a combination the WebGPU
 * device does not implement fails by name (CBC_FAILED) and never falls back to the CPU. */
int cbc_host_bake(CBCHost host, const CBCBakeConfig *config, float *values, uint64_t capacity, uint64_t *required, uint32_t *components);
/* The map a bake with this config uses, without rendering, on any backend. Per pixel:
 * triangles[i] is the covering triangle's index in the mesh (of a subdivision cage: in Cycles'
 * tessellation of it, PROVISIONAL), or -1; coordinates (may be
 * NULL) holds six floats, u, v, du/dx, du/dy, dv/dx, dv/dy, where the point is
 * u * V0 + v * V1 + (1 - u - v) * V2 and x, y are in pixels (all zero where uncovered).
 * Where UV triangles overlap, the lowest triangle index wins. capacity and required
 * count pixels; NULL triangles with capacity 0 is a size query (CBC_SMALL_BUFFER). */
int cbc_host_bake_map(CBCHost host, const CBCBakeConfig *config, int32_t *triangles, float *coordinates, uint64_t capacity, uint64_t *required);

/* Subdivision geometry for a mesh whose subdivision_type is linear: the control cage's vertex_count positions (xyz, 3
 * floats each) and face_count polygon faces, face_corners[f] (at least 3) corners each, listed in order in corners
 * (corner_count indices into the positions). Replaces the mesh's geometry and its subdivision attributes; every face
 * starts on material 0 (subd_shader) and flat (subd_smooth). Each render tessellates it with Cycles' adaptive dicing
 * (subd_dicing_rate, subd_max_level, subd_adaptive_space; pixel space measures in the render camera), and so does each
 * bake (PROVISIONAL: object space only; ABI.md "Baking"). */
int cbc_host_subd_mesh(CBCHost host, CBCNode mesh, const float *xyz, uint64_t vertex_count, const uint32_t *face_corners,
                       uint64_t face_count, const uint32_t *corners, uint64_t corner_count);

/* ---- Render time limit (addition to ABI 1). ---- */

/* Wall-clock time limit of this host's later renders and bakes, in seconds (Cycles' render time limit). 0, the
 * default, renders every sample; otherwise a render stops once the limit has passed, after its first batch of
 * samples, and returns the samples done by then, so its film is not reproducible. Serializes with rendering. */
int cbc_host_render_time_limit(CBCHost host, double seconds);

/* ---- UDIM tiles (addition to ABI 1, PROVISIONAL; ABI.md "Images"). ---- */

/* Binds count images to one Image Texture node as Cycles' UDIM tiles: images[i] at tile tiles[i], the tile covering
 * UV [u, u + 1) x [v, v + 1) being 1001 + 10 * v + u (u 0 to 9). Tiles are distinct, 1001 to INT_MAX; every image is a
 * live handle of this host and all have the same CBC_IMAGE_* format (Cycles decodes every tile with the first tile's
 * settings). A UV outside every bound tile reads the node's Missing inputs. Replaces any earlier binding of the node;
 * cbc_host_image_bind(node, 0) removes it, and cbc_host_image_update of a member image applies at the next render (an
 * update that would leave the tiles of a binding with different formats is CBC_INVALID).
 * Image Texture nodes only: an Environment Texture node is CBC_UNSUPPORTED. On the WebGPU backend a render or bake of a
 * scene with a tiles binding is CBC_UNSUPPORTED ("WebGPU UDIM tiles not implemented"). Nothing changes on failure. */
int cbc_host_image_bind_tiles(CBCHost host, CBCNode texture_node, const uint32_t *tiles, const CBCImage *images, uint64_t count);

/* ---- Texture limit (addition to ABI 1, PROVISIONAL; ABI.md "Images"). ---- */

/* Cycles' texture limit: an image whose larger side exceeds max_size is reduced when a render or bake loads it, on the
 * host and before any device sees it: halved, as often as its larger side needs to be at most max_size, by a box filter
 * (each side times the power-of-two scale, rounded down, at least 1 pixel). It applies to caller images and to Cycles' own (the Sky Texture's), and
 * brings an image within Metal's 16384-pixel texture limit. 0, the default, keeps every image at its size; max_size is
 * 0 to INT_MAX, else CBC_INVALID. The same value is always accepted; a different one is CBC_NOT_READY once a render or
 * bake has loaded images (Cycles does not load an image again). On the WebGPU backend a render or bake with a limit
 * other than 0 is CBC_UNSUPPORTED ("WebGPU texture limit not implemented"). */
int cbc_host_texture_limit(CBCHost host, uint32_t max_size);

/* ---- BVH spatial splits (addition to ABI 1, PROVISIONAL; ABI.md "Scene authoring"). ---- */

/* Cycles' BVH spatial splits (SceneParams::use_bvh_spatial_split): the BVH2 builder splits primitives between nodes in
 * the BVH of each geometry Cycles keeps in object space (Geometry::is_instanced: used by several objects, or with true
 * displacement, subsurface scattering or subdivision; never in the scene's top level, for curves or for geometry with
 * motion blur), and the
 * Embree CPU backend (CBC_HOST_CPU) builds at Embree's high quality; MetalRT ignores it, as in Cycles. A speed setting:
 * it changes how rays find the closest hit, not which one, so renders do not change by design. enabled is 0 (the
 * default) or 1, else CBC_INVALID. A change has every BVH built again at the next render or bake. On the WebGPU backend a
 * render or bake with it enabled is CBC_UNSUPPORTED ("WebGPU BVH spatial splits not implemented"). */
int cbc_host_bvh_spatial_split(CBCHost host, uint32_t enabled);

/* ---- Tiled rendering (addition to ABI 1, PROVISIONAL; ABI.md "Tiled rendering"). ---- */

/* Later renders and bakes of this host render in Cycles' tiles of tile_size x tile_size pixels (SessionParams::
 * use_auto_tile and tile_size). 0, the default, renders the whole film (or its camera border) in one tile; tile_size is
 * 0, 8 to 128, or a multiple of 128 up to 8192 (the sizes Cycles uses as given), else CBC_INVALID. Cycles uses one tile
 * when the tile's area is at least the film's and no side exceeds 8192. The device holds one tile's buffers at a time;
 * the library keeps the finished tiles in system memory (no file is written) and, once the last tile is done, denoises
 * the whole film and returns it, every pass included, as for an untiled render. With tiles the time limit applies to
 * each tile, and adaptive sampling renders 4 pixels beyond each tile's edges (not for bakes). On the WebGPU backend a
 * render or bake with a tile size other than 0 is CBC_UNSUPPORTED ("WebGPU tiled rendering not implemented"). */
int cbc_host_tile_size(CBCHost host, uint32_t tile_size);

/* ---- Tiled images, Cycles' texture cache (addition to ABI 1, PROVISIONAL; ABI.md "Tiled images"). ---- */

/* Reads one rectangle of mip level `level` of a tiled image into pixels: width x height pixels starting at column x and
 * row y of that level, rows bottom first (row y at v = y / level_height, as in cbc_host_image_create), row_bytes apart,
 * tightly packed in the image's CBC_IMAGE_* format (float and half values finite). Level 0 is the image; level L is max(1, image_width >> L) x
 * max(1, image_height >> L) pixels, and the levels run from 0 to the first one whose sides are both at most 64 (an image
 * smaller than 64 pixels in both sides has level 0 only). The pixels of every level are the caller's own (Cycles' own
 * levels are 2 x 2 box reductions). Return 0, or nonzero to fail the render or bake (CBC_FAILED, naming the image, the
 * level and the rectangle). Called only during cbc_host_render and cbc_host_bake, on any native thread, never
 * concurrently with this host's other read and release callbacks; it must not call any cbc_host_ function other than
 * cbc_host_error and cbc_host_version (those calls return CBC_NOT_READY). */
typedef int (*CBCImageTileReadCallback)(void *user_data, uint32_t level, uint32_t x, uint32_t y, uint32_t width,
                                        uint32_t height, void *pixels, uint64_t row_bytes);
/* Called exactly once for every call that passed it, when the library no longer uses user_data: after the image handle
 * is released, no binding uses it and Cycles has freed its image (at the next render or bake), at cbc_host_destroy, at
 * process exit with the host live, or, when the call that passed it fails, before that call returns. Never concurrently
 * with this host's read callbacks; the same rule for library calls as the read callback. */
typedef void (*CBCReleaseCallback)(void *user_data);
/* A tiled image: Cycles' texture cache loads only the tiles (64 x 64, or config->tile_size, PROVISIONAL) and mip levels
 * a render samples, through read, on demand (the CPU device at once, the Metal device after a cache miss restarts the
 * path). config as for cbc_host_image_create (width, height, one of the eight formats) plus tile_size; read is
 * required, release may be NULL. Bind it with
 * cbc_host_image_bind or as UDIM tiles of tiled images only; cbc_host_image_update of it is CBC_INVALID. A read that
 * fails, or a reader stopped by a cancel, is read again by the next render. On the WebGPU backend a render or bake of a
 * scene that binds one is CBC_UNSUPPORTED ("WebGPU tiled images not implemented"). */
int cbc_host_image_create_tiled(CBCHost host, const CBCImageConfig *config, CBCImageTileReadCallback read,
                                CBCReleaseCallback release, void *user_data, CBCImage *image);
#ifdef __cplusplus
}
#endif
#endif
