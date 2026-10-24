# CodeBrix Cycles native C ABI

Copyright (c) 2026 Jeremy Ellis and contributors

This is the consumer reference for `libcodebrix_cycles_host` (`.so`, `.dylib`,
`.dll`), declared in [cycles_host.h](cycles_host.h). Any language that can call C
functions (Rust through a thin `extern "C"` shim, Python `ctypes`, C#
`DllImport`) drives Cycles through it. The .NET package wraps exactly this
surface. Maintainer notes are in [README.md](README.md).

The ABI is experimental, not a released contract, but it already follows the
rules below so that bindings written today keep working.

## Versioning and the additive-only rule

`CBC_HOST_ABI` is 1. Within one ABI integer the surface only grows:

> Add functions, enum values and trailing struct fields; never change or
> renumber existing signatures, enum values, error codes or layouts. A breaking
> change bumps `CBC_HOST_ABI`.

There is no deprecation machinery. On ELF platforms every export carries the
symbol version `CBC_HOST_EXPERIMENT_1`; `cbc_host_abi_version()` returns the
ABI integer, and `cbc_host_version()` returns:

| Field | Meaning |
| --- | --- |
| `abi_version` | `CBC_HOST_ABI` (1) |
| `library_version` | `"<major>.<minor>.<yyyyMMdd>.<build>"`, for example `1.0.20261020.0` |
| `cycles_revision` | the pinned upstream Cycles commit, 40 hex digits |

The library version has one source: `native/host/targets.cmake` sets
`<major>.<minor>`, stamps the UTC configure date (CMake honours
`SOURCE_DATE_EPOCH`, so reproducible builds can pin it) and takes `<build>` from
the CMake cache variable `CODEBRIX_HOST_BUILD_NUMBER` (default 0). The Cycles
revision is read from `third_party/cycles-import.json`. The managed package has
its own MSBuild date-stamped version; the .NET wrapper reports both its package
version and this native version.

## Conventions

- **Handles** (`CBCHost`, `CBCNode`, `CBCImage`) are opaque 64-bit integers,
  never pointers. 0 is never a valid handle. Handles from one host are invalid
  on another; stale, foreign and deleted handles return `CBC_INVALID`.
- **Strings** are NUL-terminated UTF-8. Input strings and arrays are copied
  before the call returns; the caller keeps ownership of its memory.
- **Structs** start with `uint32_t struct_size`, which must equal the size the
  library was built with; input structs also carry `abi_version` where shown
  and reserved fields that must be zero. Wrong sizes, versions or nonzero
  reserved words return `CBC_INVALID`. When a trailing field is added, the
  library will accept every previously published size.
- **Outputs** (`*_json`, getters, pass readback): pass `capacity` and a buffer;
  `required` is always set (element count, or bytes including the NUL for text).
  `NULL` with capacity 0 is a size query that returns `CBC_SMALL_BUFFER`. On any
  failure the destination is left unchanged. Query and copy are separate calls:
  another edit may change the size in between, so retry on `CBC_SMALL_BUFFER`.
- **Arrays of vectors** are flattened: float2 = 2, float3 = 3, transform = 12
  floats (3x4 row-major).
- **Images** are tightly packed rows, row 0 at v = 0 (bottom), native-endian
  float/half.
- **Render output** is raw scene-linear float32 RGBA, lower-left origin. The
  library never encodes or decodes image files for the product profile; decode
  textures in the application (for .NET, CodeBrix.Imaging) and pass pixels.

## Error codes

| Code | Value | Meaning |
| --- | --- | --- |
| `CBC_OK` | 0 | success |
| `CBC_INVALID` | 1 | bad argument, handle, size, socket, value or reference |
| `CBC_UNSUPPORTED` | 2 | valid request this binary or device does not provide |
| `CBC_FAILED` | 3 | native failure (device error, render failure) |
| `CBC_CANCELLED` | 4 | the render was cancelled |
| `CBC_NOT_READY` | 5 | wrong state (host closing, nothing rendered yet, call from a callback, color config while hosts live, an edit of a baked volume) |
| `CBC_SMALL_BUFFER` | 6 | output buffer too small; `required` holds the size |

`cbc_host_error()` copies the calling thread's last message (thread-local,
UTF-8, at most 2,047 bytes). A successful call clears it.

**Device errors.** A render that fails with `CBC_FAILED` because the device
rejected the scene (for example an unimplemented WebGPU feature such as a
non-Combined pass or a non-perspective camera) leaves the error on the Cycles
device. The next `cbc_host_render` on that host recovers it:

- The WebGPU device clears the error when the device itself is intact (no lost
  device and no uncaptured WebGPU error): it drops the commands encoded before
  the failure and clears its kernel error word. The host then tags the whole
  scene for a complete device update (every manager uploads its data again, as
  for a new scene), emits a `CBC_EVENT_STATUS` event whose message starts with
  `Device recovered`, and renders. The device-tier record and pipeline tier are
  not touched. Remove the cause first; otherwise the render fails again.
- A device that cannot recover (a lost or otherwise fatally failed WebGPU
  device, or an error on the CPU or Metal device, which have no recovery path)
  keeps the host faulted: every later render returns `CBC_FAILED` with a message
  that starts with `Host faulted:` and names the original error. Destroy the
  host and create a new one.

Measured on Linux WebGPU (native/tools/scene_edit_gate.py): after a rejected
depth pass or orthographic camera, removing the cause and rendering again on the
same host meets the practical criterion against a fresh CPU render.

## Threading and ownership

- Every function is safe to call from any thread. Calls on one host serialize
  with each other and with rendering; `cbc_host_render` holds the host for the
  whole render.
- `cbc_host_cancel`, `cbc_host_is_rendering`, `cbc_host_backend` and
  `cbc_host_destroy` may run concurrently with a render. Cancel makes the render
  return `CBC_CANCELLED`; destroy removes the handle immediately, cancels and
  waits for cleanup.
- Failed and cancelled renders never write the caller's output buffer.
- A requested GPU render never falls back to the CPU.
- `cbc_host_color_config_path` and `cbc_host_tier_config` are process-wide;
  call them before creating hosts.
- Process exit with live hosts is supported (an exit hook closes them).

## Events (progress, status, errors)

`cbc_host_set_event_callback(host, callback, user_data)` registers one callback
per host (`NULL` clears it). The callback receives a `CBCEvent`:

| Field | Meaning |
| --- | --- |
| `struct_size` | `sizeof(CBCEvent)` as built; read only fields that fit |
| `kind` | `CBC_EVENT_STATUS` (status text changed), `CBC_EVENT_PROGRESS` (sample counter changed), `CBC_EVENT_ERROR` (the render is failing; `message` is the error) |
| `samples_done`, `samples_total` | completed and requested samples per pixel |
| `elapsed_seconds` | time since the render started |
| `message` | UTF-8 status or error text, valid only during the call |

Rules:

- The callback runs only while `cbc_host_render` executes, on a native render
  thread or on the thread that called `cbc_host_render`. It is never invoked
  concurrently with itself for the same host.
- It must not call back into the library: every `cbc_host_` function except
  `cbc_host_error` and `cbc_host_version` returns `CBC_NOT_READY` from inside a
  callback. (`cbc_host_cancel` is also refused there; use the return value.)
- Returning nonzero requests cancellation; the render returns `CBC_CANCELLED`.
- Registration serializes with rendering. After `cbc_host_set_event_callback`
  or `cbc_host_destroy` returns, the previous callback is never invoked again,
  so `user_data` may then be freed. Events are suppressed while a host closes.
- Keep callbacks short; they run inside the render loop.

## Backends and capability queries

| Backend | Value | Notes |
| --- | --- | --- |
| `CBC_HOST_WEBGPU` | 1 | CodeBrix WGSL device on the unchanged CodeBrix.WebGpu.Native runtime |
| `CBC_HOST_CPU_REFERENCE` | 2 | Cycles CPU device; always compiled |
| `CBC_HOST_METAL` | 3 | Cycles' Metal device; osx-arm64 (Apple Silicon) build only |
| `CBC_HOST_GPU` | 4 | "the GPU backend of this binary": Metal in the osx-arm64 build, WebGPU in every other build, osx-x64 included |

The GPU backend is fixed per RID. macOS on Apple Silicon (osx-arm64) renders
with Cycles' own Metal device; macOS on Intel (osx-x64) and every Windows, Linux
and Android RID render with the WGSL device on WebGPU. The osx-x64 build is
configured without Cycles' Metal device, so its WebGPU runtime reaches the GPU
through Metal but Cycles' Metal kernels are never used there.

Ask for `CBC_HOST_GPU` to stay platform-neutral; `cbc_host_backend` and the
capability report reveal what was chosen. `cbc_host_resolve_backend` answers
without creating a device (`CBC_UNSUPPORTED` when not compiled in); device
availability is reported by `cbc_host_create`.

`cbc_host_capabilities_json(0, ...)` describes the binary; with a host it adds
the device. Fields (schemaVersion 1): `abiVersion`, `libraryVersion`,
`cyclesRevision`, `backends` (`compiled`, `gpu`, `gpuFallbackToCpu`: false),
`backendScope` (per compiled backend: camera types, primitives, passes and the
classes of scene features the device rejects at render time; `cpu.arithmetic`
states the CPU kernels' `isa`, `fpContract` and `reciprocalMath`, from a
translation unit compiled with their flags), `optionalLibraries`
(OSL, Embree, OpenImageDenoise, OpenVDB: compiled or not), `imageInput` (the
eight formats, origin, whether texture filenames are read), `output`,
`passTypes` (every registered Cycles pass type and value), `events`, `limits`,
`determinism` (a pointer to the section below) and, with a host, `host`
(`backend`, `deviceDescription`, `deviceId`, `deviceReport`: the
`cbc_host_device_report_json` object with the WebGPU tier decision, adapter
identity, record/canary status, compile budget, warnings and compiled
pipelines). The device report's `warnings` also carries the pipeline-build size
guard's warnings, and its `sizeGuard` object states the guard's threshold
(`thresholdBytes`, 0 when disabled), the largest inlined entry compiled so far
(`largestEntryBytes`, `largestEntry`), its instruction estimate
(`estimatedInstructions`) and the number of size warnings (`warnings`); each
pipeline row adds `inlinedBytes`. A size warning never sets `degraded` and never
changes the compiled variant (native/cycles/pipelines.h, size guard). The report states what this source tree does today; scene features
it does not list as supported may still fail at render time with `CBC_FAILED`.

Facts measured for the current WebGPU device (Linux host gate): object shadow
terminator offsets are implemented (geometry offset; shading offset 0 to 1, larger
shading offsets are rejected at render time); only the
perspective camera; only `Combined` is written (data passes and light passes
fail with "surface data passes not implemented"); the integrator defaults
(denoising, adaptive sampling, light tree, guiding enabled) must be switched
off for WebGPU renders; light trees are not implemented.

## Scene authoring

Scenes are Cycles nodes addressed through the native registry
(`cbc_host_registry_json`: node types, sockets, value kinds, flags, enum
labels). The generic socket layer sets and reads any stored socket:
`cbc_host_set_floats/ints/uints/string/strings/nodes`, the matching getters and
`cbc_host_property_info`. It covers lights, cameras, integrator, film, objects,
geometry, passes and shader nodes; dedicated exports exist only for what the
socket layer cannot express (lifetime, buffers, callbacks, queries).

| Topic | How |
| --- | --- |
| Singletons | `cbc_host_scene_node`: `camera`, `background`, `film`, `integrator` |
| Scene nodes | `cbc_host_create_node`: `mesh`, `hair`, `pointcloud`, `volume`, `object`, `particle_system`, `pass`, `shader`, `pointlight`, `spotlight`, `arealight`, `sunlight`, `backgroundlight` |
| Lights | create the light, set `strength`, `used_shaders` (an emission shader), type-specific sockets (`radius`/`is_sphere`, `angle`/`smooth`, `sizeu`/`sizev`/`ellipse`/`spread`, sun `angle`), then an `object` whose `geometry` is the light and whose `tfm` places it. Area and sun lights emit along the object's -Z. |
| Cameras | `camera_type` (`perspective`, `orthograph`, `panorama`; `custom` needs OSL, not compiled), `panorama_type`, `fov`, `matrix`, clipping, `shuttertime`, `motion` (transform array) |
| Integrator, film | sockets of the singletons, for example `max_bounce`, `sampling_pattern`, `seed`, `motion_blur`, `exposure`, `filter_type` |
| Shader graphs | `cbc_host_shader_output`, `cbc_host_shader_node`, `cbc_host_connect` |
| Geometry | `cbc_host_mesh` (triangles; every face starts on material 0), the `shader`/`smooth` face sockets, attributes (`cbc_host_attribute_*`) |
| Images | `cbc_host_image_create` (8 formats), `cbc_host_image_bind`, `cbc_host_image_update`, `cbc_host_image_release` |
| Passes | `cbc_host_create_node("pass")` with `type` and a unique `name`; read with `cbc_host_pass_floats` |

### Motion

Object motion is the object's `motion` socket (an odd or even number of 3x4
transforms around `tfm`), camera motion the camera's `motion` socket, and
deformation `cbc_host_motion_positions` (odd step count 3..129, time-ordered
vertex arrays whose center equals the mesh's authored positions). All need
`integrator.motion_blur` and a camera `shuttertime`. Motion may be set before
or after a render (next section).

### Edits after a render

Every edit made through this ABI after a render applies to the next render
exactly as if the scene had been built with it: the CPU reference renders the
edited host bit for bit like a new host that applies the same calls before its
first render (native/tools/scene_edit_gate.py: object `tfm`, object, camera and
deformation motion added or removed, motion blur switched on, a material value,
a light's strength and transform, a replaced mesh, a new instance, an attribute
and the `smooth` flags; the WebGPU device meets the practical criterion).

How: the pinned Cycles static-BVH update path bakes a single-user mesh, hair or
point cloud with its object transform (it rewrites the geometry's positions,
normals and radii in place). Before each render the host keeps a copy of the
authored attributes of every geometry that render may bake. A later edit of
the geometry itself (`cbc_host_mesh`, attributes, deformation motion, sockets)
first restores those authored attributes; a render whose bake would differ from
the existing one (the object's `tfm` or `motion`, motion blur, the number of
objects using the geometry, or a shader edit that may add displacement or
subsurface scattering) restores them before Cycles' own device update, which
then bakes or instances the geometry as it would for a new scene. A restored
geometry rebuilds the BVH; the authored copy costs one extra copy of each such
geometry's attributes. `cbc_host_attribute_get_*` return the authored values.

Refused: volume geometry with voxel data cannot be restored once baked; an edit
of such a volume, or of its object's `tfm`, `motion` or `geometry`, returns
`CBC_NOT_READY` (delete and recreate the object and volume instead).

### Lifetime and deletion

`cbc_host_delete_node` deletes objects, geometry, lights, particle systems,
passes and API-created shaders, and removes shader-graph nodes (disconnecting
their links). The handle becomes invalid at once; deleting a shader also
invalidates its graph-node handles. A node still referenced by another node's
socket (an object's `geometry`, a mesh's `used_shaders`, the background
`shader`, ...) is refused with `CBC_INVALID`: clear the reference or delete the
referrer first. Singletons and the shader output node cannot be deleted. Cycles
keeps a deleted shader allocated and unused until the host is destroyed.

Image handles are caller references to copied pixels. Bindings keep their own
reference, so releasing a handle never invalidates a binding.
`cbc_host_image_update` replaces the pixels behind a live handle (format and
size may change) and every texture still bound to that handle uploads the new
pixels on the next render; bindings made before a release keep their pixels.

### Passes

`Combined` is always captured. Other passes are captured when a pass node has a
nonempty `name` (other than `Combined`); the first pass of a duplicated name
wins. After a successful render, `cbc_host_render_passes_json` lists
`name`, `type`, `components` and `written`, and `cbc_host_pass_floats` copies
one pass (`components` floats per pixel, raw scene-linear values, lower-left
origin). A render that starts clears earlier results; until a render succeeds
both return `CBC_NOT_READY`. A pass the device did not write returns
`CBC_UNSUPPORTED`. AOVs are `aov_color`/`aov_value` passes fed by an
`aov_output` shader node with the same name.

## Exports (ABI 1, 48 functions)

| Function | Purpose |
| --- | --- |
| `cbc_host_abi_version` | ABI integer (1) |
| `cbc_host_version` | library version, ABI integer and pinned Cycles revision |
| `cbc_host_error` | copy the calling thread's last error message |
| `cbc_host_color_config_path` | select the process-wide OCIO configuration |
| `cbc_host_tier_config` | process-wide WebGPU pipeline-tier policy (`CBCTierConfig`) |
| `cbc_host_resolve_backend` | map a requested backend (including `CBC_HOST_GPU`) to this binary's concrete backend |
| `cbc_host_create` | create a host (`CBCHostConfig`) |
| `cbc_host_destroy` | remove a host, cancelling and waiting for active work |
| `cbc_host_backend` | concrete backend of a host |
| `cbc_host_capabilities_json` | binary or host capability report |
| `cbc_host_device_report_json` | device and WebGPU pipeline-tier report |
| `cbc_host_registry_json` | native node/socket/enum registry |
| `cbc_host_scene_node` | handle of a scene singleton |
| `cbc_host_create_node` | create a scene node |
| `cbc_host_delete_node` | delete a node and invalidate its handle |
| `cbc_host_node_type` | registered type name of a node |
| `cbc_host_shader_output` | output node of a shader graph |
| `cbc_host_shader_node` | add a node to a shader graph |
| `cbc_host_connect` | link two shader-graph sockets |
| `cbc_host_property_info` | value kind, flags, components and count of a socket (`CBCPropertyInfo`) |
| `cbc_host_set_floats` / `cbc_host_get_floats` | float, vector, color, point2, transform and their arrays |
| `cbc_host_set_ints` / `cbc_host_get_ints` | bool, int, enum value, uint, and bool/int arrays |
| `cbc_host_set_uints` / `cbc_host_get_uints` | uint and uint64 sockets |
| `cbc_host_set_string` / `cbc_host_get_string` | string sockets and enum labels |
| `cbc_host_set_strings` | string arrays |
| `cbc_host_set_nodes` / `cbc_host_get_nodes` | node references and arrays |
| `cbc_host_mesh` | replace a mesh's triangles |
| `cbc_host_motion_positions` | deformation motion steps of a mesh |
| `cbc_host_attribute_floats` / `cbc_host_attribute_bytes` | set a geometry attribute (`CBCAttributeConfig`) |
| `cbc_host_attribute_get_floats` / `cbc_host_attribute_get_bytes` | read a geometry attribute |
| `cbc_host_attribute_remove` | remove a geometry attribute |
| `cbc_host_image_create` | copy caller pixels into a new image handle (`CBCImageConfig`) |
| `cbc_host_image_update` | replace the pixels of an image handle |
| `cbc_host_image_bind` | bind an image (or 0) to an image/environment texture node |
| `cbc_host_image_release` | release an image handle |
| `cbc_host_set_event_callback` | register the host's event callback |
| `cbc_host_render` | render synchronously into caller RGBA (`CBCRenderConfig`) |
| `cbc_host_cancel` | cancel the active render |
| `cbc_host_is_rendering` | whether a render is active |
| `cbc_host_pass_floats` | copy one captured pass |
| `cbc_host_render_passes_json` | list the passes captured by the last render |

## Structs and enums

| Struct | Size | Fields |
| --- | --- | --- |
| `CBCHostConfig` | 32 | `struct_size`, `abi_version`, `backend`, `threads` (0 = automatic, at most 1024), 4 reserved |
| `CBCRenderConfig` | 32 | `struct_size`, `width`, `height` (1..32768), `samples`, 4 reserved |
| `CBCPropertyInfo` | 32 | `struct_size`, `value_type`, `flags`, `components`, `count`, reserved |
| `CBCAttributeConfig` | 32 | `struct_size`, `value_type`, `domain`, `flags`, 4 reserved |
| `CBCImageConfig` | 32 | `struct_size`, `width`, `height`, `format`, 4 reserved |
| `CBCTierConfig` | 32 | `struct_size`, `abi_version`, `tier`, `flags`, 4 reserved |
| `CBCVersionInfo` | 96 | `struct_size`, `abi_version`, 2 reserved, `library_version[32]`, `cycles_revision[48]` |
| `CBCEvent` | 40 | `struct_size`, `kind`, `samples_done`, `samples_total`, `elapsed_seconds`, `message` |

Enums (values are fixed): backends (above); error codes (above); value kinds
`CBC_VALUE_*` 0..17; property flags `CBC_PROPERTY_*` (1, 2, 4, 8, 16);
attribute domains `CBC_ATTR_MESH`..`CBC_ATTR_CURVE_KEY_NORMAL` (1..10) and flags
`CBC_ATTR_STANDARD` (1), `CBC_ATTR_SUBDIVISION` (2); image formats
`CBC_IMAGE_FLOAT`..`CBC_IMAGE_USHORT4` (1..8); tiers `CBC_TIER_AUTO/FULL/CONSERVATIVE`
(0..2) and tier flags (1, 2, 4); events `CBC_EVENT_STATUS/PROGRESS/ERROR` (1..3).

## Determinism

Criteria. The practical criterion is `|a - b| <= 5e-5 + 2e-4 * |b|` for every
component; strict differences (`1e-7 + 1e-7 * |b|`) are counted and reported,
never tuned away. Gates never relax either.

Bit-identical (measured):

- CPU reference renders of the same scene repeat bit for bit in a process and
  across processes on one machine (host gate: C and .NET consumers, 1,560
  color-reload films, deleting/recreating nodes and images back to an earlier
  scene state).
- WebGPU scene specialization on versus off gives the same film on Linux x64
  Intel (glass_render, film
  `db8e84f9990299d2b2ce1c53a390f56e1d4beb73ec7326aa55da4c1b074802f6`; the
  Principled and node renders also match). "Off" is the superset mode
  (`CODEBRIX_CYCLES_SHADER_SPECIALIZATION=superset:G`, every group): the whole
  unspecialized module (`=0`, a diagnostic) exceeds the Intel Mesa 25.0.7
  backend's 16-bit instruction/register range (native/DEVICE.md, "Linux
  reference fixes on the updated Mesa"). Both are desktop-only and refused on
  Android, so this is shown on Linux only.
- Full versus conservative pipeline tier gave a bit-identical glass_render film
  on the Pixel 4 XL. On Linux Intel the conservative tier (workgroup size 1) can
  change last bits; it must and does meet the practical criterion.

What varies:

- GPU film accumulation uses atomic floating-point addition, so repeated WebGPU
  (and Metal) renders of one scene are not bitwise repeatable once several
  samples of a pixel are in flight; measured repeat differences are about
  3e-8 to 9e-8 at 8 to 64 samples, and every film still meets both criteria
  against the CPU. The strict-difference count of a GPU run varies from run to
  run within that envelope.
- CPU references differ between architectures: the ARM64 and x64 CPU oracles
  differ by ULPs (NEON versus SSE grouping and contraction); a GPU can match the
  x64 oracle bit for bit where the ARM64 oracle differs.
- CPU references depend on the platform's C library. Cycles calls libm
  (`acosf`, `asinf`, `atan2f`, `sinf`, `cosf`, `tanf`, ...) in its kernels and in
  scene preparation. glibc rounds `acosf`/`asinf`/`atanf`/`atan2f`/`tanf`
  correctly and the GPU kernels follow the correctly rounded results; on a
  platform whose C library is not correctly rounded (Android's bionic), CPU
  reference renders differ from the GPU by that library's error (1 ulp per call,
  amplified by sampling). The correctly rounded reference is the authority; see
  native/DEVICE.md, "Validation reference policy".
- Metal's hardware bilinear filtering is not bit-exact with Cycles' CPU texture
  interpolation and exceeded the practical criterion in 4 components of the
  linearly filtered byte4 starter texture (worst 1.16 times the tolerance). The
  CPU is the authority for texture filtering; this is a documented, bounded
  Metal deviation.
- GPU drivers may contract floating-point operations. Closest-hit and shadow
  traversal are therefore written so that no driver can fuse them, and reproduce
  the x64 CPU oracle's ray/triangle and instance arithmetic bit for bit on Linux;
  ShaderData positions and object transforms are written the same way. Other
  stages can still differ by ULPs, which can flip edge-grazing samples (one
  triangle-sampler sample on the Adreno 730, native/DEVICE.md "Open device
  items").

Practical consequence: compare GPU output to the CPU reference with the
practical criterion; expect bit identity only for CPU repeats on one machine
and for the specialization/tier cases listed above.

## Minimal example

[`native/tests/host/example.c`](../tests/host/example.c) is a complete consumer
(about 100 lines) that the host gate builds with `-Wall -Wextra -Werror` and
runs on the CPU reference and through `CBC_HOST_GPU`. Its shape:

```c
#include "cycles_host.h"

CBCVersionInfo version = {sizeof(version)};
cbc_host_version(&version);                       /* library, ABI, Cycles revision */

CBCHostConfig config = {sizeof(config), CBC_HOST_ABI, CBC_HOST_GPU, 0, {0}};
CBCHost host = 0;
if (cbc_host_create(&config, &host) != CBC_OK) { /* cbc_host_error(...) */ }

CBCNode integrator, camera, material, diffuse, output, mesh, object;
cbc_host_scene_node(host, "integrator", &integrator);
const int64_t off = 0;                            /* features WebGPU does not implement */
cbc_host_set_ints(host, integrator, "use_light_tree", &off, 1);
cbc_host_set_ints(host, integrator, "use_denoise", &off, 1);
cbc_host_set_ints(host, integrator, "use_adaptive_sampling", &off, 1);
cbc_host_set_ints(host, integrator, "use_guiding", &off, 1);
cbc_host_scene_node(host, "camera", &camera);
const float matrix[12] = {1,0,0,0, 0,1,0,0, 0,0,1,-4};
cbc_host_set_floats(host, camera, "matrix", matrix, 12);
cbc_host_create_node(host, "shader", &material);
cbc_host_shader_node(host, material, "diffuse_bsdf", &diffuse);
cbc_host_shader_output(host, material, &output);
cbc_host_connect(host, diffuse, "BSDF", output, "Surface");
cbc_host_create_node(host, "mesh", &mesh);
cbc_host_set_nodes(host, mesh, "used_shaders", &material, 1);
const float xyz[9] = {-1.5f,-1,0, 1.5f,-1,0, 0,1.5f,0};
const uint32_t triangle[3] = {0, 1, 2};
cbc_host_mesh(host, mesh, xyz, 3, triangle, 1);
cbc_host_create_node(host, "object", &object);
cbc_host_set_nodes(host, object, "geometry", &mesh, 1);

static float rgba[32 * 24 * 4];
CBCRenderConfig render = {sizeof(render), 32, 24, 16, {0}};
cbc_host_render(host, &render, rgba, 32 * 24 * 4);  /* scene-linear RGBA */
cbc_host_destroy(host);
```

A Rust binding needs only `#[repr(C)]` copies of the structs, `u64` handles,
`extern "C"` declarations and a `CStr` for text; nothing in the ABI depends on
C++ types or on .NET.
