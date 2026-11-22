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

Items marked **PROVISIONAL** are additions that await the maintainer's approval:
until it is given, their numbers, names and meaning may still change.

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
  library will accept every previously published size. A C initializer that
  lists the fields by position must list the new trailing fields too (compilers
  warn about missing fields under `-Wextra`); a designated initializer, or a
  zeroed struct whose fields are then assigned, leaves them zero.
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
  The delivered film (**PROVISIONAL**): before it uses a render's or bake's
  output, the host verifies that Cycles' output driver delivered the film it asked
  for: one tile of the render's size (the film, or the camera border's region) at
  offset 0; render buffers of that size, with a full window and at that place in a
  film of the requested size (the buffers the passes are read with: the full frame
  of a tiled render, else the path tracer's effective tile, which Cycles scales by
  its resolution divider; the overlay of `integrator/path_trace.cpp` exposes them,
  `codebrix_delivered_buffer_params`); Combined with four components and every
  other pass read with the components the host reads; and every component of every
  pixel of those passes written (the host fills the destination with a marker NaN no
  render writes, and none may remain). Otherwise the render or bake fails with
  `CBC_FAILED` and "The film the output driver delivered is not the one requested:
  ..." naming what differed, and the output is not written. No accepted scene
  triggers it on the CPU or Metal backends (measured: the managed tests, the host
  gate and the whole oracle corpus, tiled renders, camera borders and bakes
  included). It is the check that would have refused the film of survey row 9, a
  Metal render with GPU denoising at a denoiser upscale factor, which Cycles renders
  at the reduced resolution and the film does not upscale (measured with that
  refusal lifted in a scratch build: "render buffers of 16 x 12 ... for a render of
  32 x 24"); a bake refuses the factor up front as a render does (Sampling,
  Baking), and such a Metal bake with the up-front refusal lifted fails here in
  the same way ("render buffers of 16 x 16 ... for a render of 32 x 32"), so the
  check stays behind both refusals as a second line of defence.

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
custom (OSL) camera) leaves the error on the Cycles
device. The next `cbc_host_render` on that host recovers it:

- The WebGPU device clears the error when the device itself is intact (no lost
  device and no uncaptured WebGPU error): it drops the commands encoded before
  the failure and clears its kernel error word. The host then tags the whole
  scene for a complete device update (every manager uploads its data again, as
  for a new scene), emits a `CBC_EVENT_STATUS` event whose message starts with
  `Device recovered`, and renders. The device-tier record and pipeline tier are
  not touched. Remove the cause first; otherwise the render fails again.
- Cycles' Metal device records the errors a scene causes (for example an
  image wider or taller than Metal's 16384-pixel texture limit, which a texture
  limit of 16384 or less avoids: "Images" below) without losing the Metal
  device. The host clears the recorded error and re-uploads the whole
  scene in the same way, with the same `Device recovered` status event, so a
  consumer sees the same behaviour on Apple Silicon as on WebGPU. Remove the
  cause first; otherwise the render fails again.
- A device that cannot recover (a lost or otherwise fatally failed WebGPU
  device, or an error on the CPU device, which has no recovery path)
  keeps the host faulted: every later render returns `CBC_FAILED` with a message
  that starts with `Host faulted:` and names the original error. Destroy the
  host and create a new one.

Measured on Linux WebGPU (native/tools/scene_edit_gate.py): after a rejected
custom camera, removing the cause and rendering again on the
same host meets the practical criterion against a fresh CPU render.

## Threading and ownership

- Every function is safe to call from any thread. Calls on one host serialize
  with each other and with rendering; `cbc_host_render` holds the host for the
  whole render.
- `cbc_host_cancel`, `cbc_host_is_rendering`, `cbc_host_backend` and
  `cbc_host_destroy` may run concurrently with a render. Cancel makes the render
  return `CBC_CANCELLED`; destroy removes the handle immediately, cancels and
  waits for cleanup. Either, like an event callback's nonzero return, stops the
  batch of samples being path traced: the render does not finish that batch
  first.
- Failed and cancelled renders never write the caller's output buffer.
- A requested GPU render never falls back to the CPU.
- `cbc_host_color_config_path`, `cbc_host_tier_config` and
  `cbc_host_pipeline_cache_config` are process-wide; call them before creating
  hosts.
- Process exit with live hosts is supported (an exit hook closes them; it runs
  the release callbacks of their tiled images, "Tiled images").
- A tiled image's read callback (**PROVISIONAL**) runs on native render threads
  during `cbc_host_render` and `cbc_host_bake`, never concurrently with the same
  host's other read and release callbacks ("Tiled images").

## Events (progress, status, errors)

`cbc_host_set_event_callback(host, callback, user_data)` registers one callback
per host (`NULL` clears it). The callback receives a `CBCEvent`:

| Field | Meaning |
| --- | --- |
| `struct_size` | `sizeof(CBCEvent)` as built; read only fields that fit |
| `kind` | `CBC_EVENT_STATUS` (status text changed), `CBC_EVENT_PROGRESS` (sample counter changed), `CBC_EVENT_ERROR` (the render is failing; `message` is the error), `CBC_EVENT_FILM` (**PROVISIONAL**: an intermediate film of a host created with `CBC_HOST_PROGRESSIVE_RESULTS`, below) |
| `samples_done`, `samples_total` | completed and requested samples per pixel (for a film, the samples in it) |
| `elapsed_seconds` | time since the render started |
| `message` | UTF-8 status or error text, valid only during the call |
| `film`, `width`, `height` | **PROVISIONAL**, `CBC_EVENT_FILM` only: `width * height * 4` floats, valid only during the call; `NULL` and 0 for the other kinds |

Rules:

- The callback runs only while `cbc_host_render` executes, on a native render
  thread or on the thread that called `cbc_host_render`. It is never invoked
  concurrently with itself for the same host.
- It must not call back into the library: every `cbc_host_` function except
  `cbc_host_error` and `cbc_host_version` returns `CBC_NOT_READY` from inside a
  callback, with a message that names the callback ("Host functions cannot be
  called from an event callback"; the same for a tiled image's read and release
  callbacks, "Tiled images" below). (`cbc_host_cancel` is also refused there;
  use the return value.)
- Returning nonzero requests cancellation; the render returns `CBC_CANCELLED`.
- `samples_done` counts only samples that Cycles finished. Cycles counts a batch
  of samples when it schedules it, and a cancel request stops the batch being
  rendered, so after a cancel request (`cbc_host_cancel`, `cbc_host_destroy` or
  a nonzero return) no further progress event is sent and status events carry
  the `samples_done` of the last progress event (0 if there was none).
- Registration serializes with rendering. After `cbc_host_set_event_callback`
  or `cbc_host_destroy` returns, the previous callback is never invoked again,
  so `user_data` may then be freed. Events are suppressed while a host closes.
- Keep callbacks short; they run inside the render loop.

### Progressive results

**PROVISIONAL.** A host created with `CBC_HOST_PROGRESSIVE_RESULTS` in
`CBCHostConfig.flags` (a trailing field: the configuration is 40 bytes; the
earlier 32 bytes mean no flags; another bit, or a nonzero `reserved2`, is
`CBC_INVALID`) sends an intermediate film through its event callback at each of
Cycles' display updates during `cbc_host_render`: after the first sample, then
about every 1 to 2 seconds of rendering, and at the end. The choice is fixed per
host because Cycles schedules a session's work by it: the host's session is not
headless (`SessionParams::headless` false, still a background render), so it
renders in smaller batches and the display updates exist at all
(`RenderScheduler`; a headless session has none). `CBC_EVENT_FILM` carries the
Combined film so far: scene-linear RGBA, lower-left origin, the whole film with
zero outside a camera border, normalised by the `samples_done` samples it holds
(noisy until a denoised result exists: with denoising, only the last film is
denoised). It follows the progress event of its samples; the last film of a
completed render is the returned film. With tiles (`cbc_host_tile_size`) a film
holds the tiles rendered so far ("Tiled rendering"). Films never run concurrently with the
other events, never after `cbc_host_render` returns, and never for
`cbc_host_bake`; returning nonzero cancels the render, and no film is sent after
a cancel request. Each update copies the Combined pass from the device once.
On the CPU the final film of a progressive host is bit for bit that of a host
without the flag (measured: a plain, an adaptive-sampling, a volume, a denoised
and a camera-border scene); on Metal and on the WebGPU device it meets the
practical criterion.

## Backends and capability queries

| Backend | Value | Notes |
| --- | --- | --- |
| `CBC_HOST_WEBGPU` | 1 | CodeBrix WGSL device on the unchanged CodeBrix.WebGpu.Native runtime |
| `CBC_HOST_CPU_REFERENCE` | 2 | Cycles CPU device; always compiled |
| `CBC_HOST_METAL` | 3 | Cycles' Metal device; osx-arm64 (Apple Silicon) build only |
| `CBC_HOST_GPU` | 4 | "the GPU backend of this binary": Metal in the osx-arm64 build, WebGPU in every other build, osx-x64 included |
| `CBC_HOST_CPU` | 5 | Cycles CPU device with Cycles' automatic BVH (Embree); compiled in builds with Embree (every build script). `CBC_HOST_CPU_REFERENCE` keeps Cycles' BVH2: it is the reference the GPU backends are validated against; the two agree closely, not bit for bit (native/DEVICE.md, open item 46). The capability report names it `cpu-embree` |

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
translation unit compiled with their flags; `maxImageDimension`, **PROVISIONAL**,
the largest width or height the backend's device takes for an image loaded whole,
below; `maxImageTileSize`, **PROVISIONAL**, the largest tile side of a tiled image,
"Tiled images"), `optionalLibraries`
(OSL, Embree, OpenImageDenoise, OpenVDB: compiled or not), `denoising`
(added within schema 1: `available`; when true also `denoiser`,
`openimagedenoise`, and `denoiseDevice`, the Open Image Denoise device per
compiled backend: `cpu` for the CPU and WebGPU backends, whose renders Cycles
denoises on the host, `metal` for Metal), `imageInput` (the
eight formats, origin, whether texture filenames are read), `output`,
`passTypes` (every registered Cycles pass type and value), `events`, `limits`,
`determinism` (a pointer to the section below), `baking` (see Baking) and, with a host, `host`
(`backend`, `deviceDescription`, `deviceId`, `deviceReport`: the
`cbc_host_device_report_json` object with the WebGPU tier decision, adapter
identity, record/canary status, compile budget, warnings, compiled pipelines and
the pipeline cache's state, and `tiledRendering`, **PROVISIONAL**: the host's tiled-rendering
counters, "Tiled rendering"). The device report's `warnings` also carries the pipeline-build size
guard's warnings, and its `sizeGuard` object states the guard's threshold
(`thresholdBytes`, 0 when disabled), the largest inlined entry compiled so far
(`largestEntryBytes`, `largestEntry`), its instruction estimate
(`estimatedInstructions`) and the number of size warnings (`warnings`); each
pipeline row adds `inlinedBytes`. A size warning never sets `degraded` and never
changes the compiled variant (native/cycles/pipelines.h, size guard). A WebGPU device report also
states `wgslArithmetic`, the CPU oracle arithmetic the generated WGSL adapters reproduce: `x64-sse4.2`
(every x86-64 build, and Android ARM64 with `CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2`),
`arm64-scalar` (the Android ARM64 product default and Linux ARM64) or `arm64-sse2neon`; oracle films
record it and `native/oracle/compare.py` gates them against the CPU gold standard of that ISA family. The report states what this source tree does today; scene features
it does not list as supported may still fail at render time with `CBC_FAILED`.

Image size per backend (**PROVISIONAL**, `backendScope.<backend>.maxImageDimension`;
`limits.maxImageDimension` stays the largest side `cbc_host_image_create` accepts,
`INT_MAX`): Cycles' Metal device refuses an image wider or taller than 16384 pixels
when it allocates it (`MetalDevice::image_alloc`, `device/metal/device_impl.mm`:
"Texture exceeds maximum allowed size of 16384 x 16384"; measured: 16384 x 1
renders, 16385 x 1 fails the render with that message, `GpuRenderTests`), so `metal`
states 16384 (a texture limit of 16384 or less brings an image within it, "Images"
below); the WebGPU device refuses a side above `INT32_MAX / 2` (1073741823,
"Invalid resident image dimensions", `WebGPUDevice::mem_alloc`,
`native/cycles/device.cpp`; READ, not run here), and its storage binding also bounds
an image's bytes (AGENT-README.txt, "EXCEEDING THE GPU STORAGE-BINDING LIMIT"); the
CPU device checks none itself, but its kernels compute `2 * width` and `2 * height`
in `int` for the Mirror extension (`wrap_mirror`, `kernel/util/image.h`), defined
only up to 1073741823, so `cpu` and `cpu-embree` state 1073741823 and the host
refuses at render and bake, on those backends, an image bound whole that loads
wider or taller (`CBC_UNSUPPORTED`, "Unsupported feature: an image wider or taller
than 1073741823 pixels on the CPU backends ..."; measured with an image of
1073741824 x 1). The size it loads at is the one Cycles computes from the texture
limit ("Images" below: `ImageManager::device_load_image` and
`ImageCache::load_full`, in binary32, in which a limit of 1073741823 is 2^30 and
leaves a side of 2^30 whole; a limit of 536870912 or less brings any image within
it). The value applies to an image loaded whole; a tiled image
(`cbc_host_image_create_tiled`) is loaded in tiles through Cycles' texture cache
(READ: its device images are the tile pool, not the image) and is bounded by its
tile size instead (`maxImageTileSize`, "Tiled images").

Facts measured for the current WebGPU device (Linux host gate): object shadow
terminator offsets are implemented (geometry offset; shading offset 0 to 1, larger
shading offsets render too); perspective, orthographic and panoramic cameras with depth of field (not stereo),
every sampling pattern and adaptive sampling; it writes the light passes (diffuse, glossy and transmission direct, indirect and colour,
emission, background, AO), light groups, the data passes (depth, position, normal, roughness, UV, object and
material index, mist, sample count, the motion pass and motion weight without motion blur; render time stays zero,
as on Cycles' Metal device), AOVs, cryptomatte and the denoising feature passes (albedo, normal, specular albedo,
roughness, depth, backward and specular motion; with deforming geometry too) and the volume majorant passes that volume
scattering guidance reads, the volume light passes, and refuses by name every other pass (for example the path guiding
passes); path guiding must be switched
off for WebGPU renders (adaptive sampling renders); the light tree and light linking render. Denoising
(the integrator's `use_denoise`, off by default, and its other denoise sockets, set like any integrator
property) works on every backend: Cycles denoises a WebGPU render with Open Image Denoise on the CPU (the
render buffers are copied to the host) and a CPU render the same way; the Combined output is then the
denoised image, also when denoising is switched on between two renders of one scene (see Passes for
the noisy version and the shadow catcher passes).

## Pipeline cache

A WebGPU device on a Vulkan adapter keeps the driver's compiled pipelines on disk,
so a later process (or a later host in this one) does not compile them again.
The cache changes compile time only, never a film. It needs no call:

| `cbc_host_pipeline_cache_config(config, utf8_directory)` | Cache |
| --- | --- |
| `CBC_PIPELINE_CACHE_OFF` | none |
| `CBC_PIPELINE_CACHE_DEFAULT`, directory given | in that directory (created if missing) |
| `CBC_PIPELINE_CACHE_DEFAULT`, `NULL` or empty, or no call | in the record directory of `cbc_host_tier_config` if one is set; else on Linux in the user's cache directory, `$XDG_CACHE_HOME/cycles/webgpu-pipelines`, where an unset, empty or relative `XDG_CACHE_HOME` means `~/.cache`; else none |

The call is process-wide and applies to hosts created afterwards. An Android
process has no user cache directory, so an app passes one (its cache or files
directory); without one it renders normally and compiles every time.

Which platforms benefit: Linux and Android, which use Vulkan. The WebGPU runtime
offers no cache on macOS on Intel (Metal) or on Windows (Direct3D 12); the same
call succeeds there and the report says `not-offered`. Cycles' Metal device on
Apple Silicon keeps its own kernel cache in the user's cache directory; its
report says `backend`, as do the CPU devices.

The device report (`cbc_host_device_report_json`, and `deviceReport` in the
capability report) has a `pipelineCache` object on every backend: `active`,
`reason` (`active`, `off`, `no-directory`, `not-offered`,
`directory-unavailable`, `create-failed`, `faulted`, `backend`, `not-requested`,
`fingerprint-mode`)
and `directory`, and on a WebGPU device also `directorySource` (`consumer`,
`record-directory`, `user-cache`, `environment`), `folder` (the adapter's folder
in the directory), `key` (the adapter's cache key), `hits` and `misses` (pipelines
created so far with and without data from disk), `ignored` (files not used:
`damaged`, `otherAdapter`, `otherRuntime`, `rejected`, `outdated`), `loadedBytes`,
`loadMs`, `saves`, `savedBytes`, `saveMs`, `saveFailures`, `maxBytes` and `failure`.

A cache problem is never an error to the consumer. A file that is missing, cut
short, damaged, or from another adapter, driver or runtime is ignored and replaced.
A directory that cannot be used, a save that fails and a pipeline that fails
through the cache all leave the device compiling without one, and the report says
so. Processes may share a directory: each pipeline has its own file, replaced
whole. The directory may be deleted at any time. Each adapter's folder is kept to
`maxBytes` (1 GiB), the least recently used pipelines going first. Maintainer
details: `native/DEVICE.md` "Pipeline cache".

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
| Singletons | `cbc_host_scene_node`: `camera`, `background`, `film`, `integrator`, `scene_attributes` (`time` in seconds and `frame`, which the Scene Time node reads) |
| Scene nodes | `cbc_host_create_node`: `mesh`, `hair`, `pointcloud`, `volume`, `object`, `particle_system`, `pass`, `shader`, `pointlight`, `spotlight`, `arealight`, `sunlight`, `backgroundlight` |
| Lights | create the light, set `strength`, `used_shaders` (an emission shader), type-specific sockets (`radius`/`is_sphere`, `angle`/`smooth`, `sizeu`/`sizev`/`ellipse`/`spread`, sun `angle`), then an `object` whose `geometry` is the light and whose `tfm` places it. Area and sun lights emit along the object's -Z. An area light whose axis (a column of the object's `tfm` times `sizeu` or `sizev`) has no zero component but a length that underflows to zero is refused at render and bake on every backend (**PROVISIONAL**; `CBC_UNSUPPORTED`, "area lights with an axis of infinities": Cycles' kernel axis, `normalize_len` in `AreaLight::copy_to_kernel`, is then all infinities, whose comparisons depend on the signs of each ray's components, and the CPU and Metal films differ widely); a portal, and a light Cycles disables by its strength, its size or a zero transform column, is not refused. An axis that underflows with a zero component (for example a size of 1e-23 on an unrotated light) holds a NaN instead and renders as Cycles renders it: no light sample, an unlit strip or an emitting plane for camera rays (native/DEVICE.md item 148). |
| Cameras | `camera_type` (`perspective`, `orthograph`, `panorama`; `custom` needs OSL, not compiled), `panorama_type`, `fov`, `matrix`, clipping, `shuttertime`, `motion` (transform array). The orthographic view size follows the scale of `matrix`. `viewplane.*` set by the caller (lens shift, sensor fit) is kept for every render (left below right, bottom below top); all four zero restores the automatic viewplane, which the render otherwise computes from the film's aspect. `full_width`, `full_height` come from each render's `CBCRenderConfig` (a set that would change them is `CBC_INVALID`). `viewplane_pre.*`, `viewplane_post.*`, `border_pre_left` and `border_pre_bottom` are Cycles' inputs for the motion pass of a camera whose viewplane or border moved since the previous frame (the previous and next viewplane, empty when all four are zero: the current one is used; the previous border origin, used when both are 0 or more, default -1); they change the motion pass and the denoising motion guides only. `border.*` (fractions of the film, lower-left origin; the default 0..1 or all zero is the whole film) renders only that region: the film and every pass come back at full size with zero outside it; the region's pixel edges are the fractions times the film size rounded to the nearest pixel; a border with left >= right or bottom >= top, outside 0..1, or covering no whole pixel is `CBC_INVALID` |
| Sampling | `CBCRenderConfig.samples` is the render's sample count (the integrator's `aa_samples` follows it; a set that would change it is `CBC_INVALID`); the integrator's `use_sample_subset`, `sample_subset_offset`, `sample_subset_length` render a subset of those samples; `cbc_host_render_time_limit` stops a render early. With denoising, a `denoiser_upscale_factor` other than 1 is refused at render and bake on every backend (`CBC_UNSUPPORTED`, "a denoiser upscale factor other than 1": the CPU and WebGPU backends' Open Image Denoise runs on the CPU, which does not upscale, and with GPU denoising on the Metal backend Cycles leaves the render at the reduced resolution, so no backend returns an upscaled film; a bake refuses it before anything bakes, **PROVISIONAL**). The film's `display_pass` and `show_active_pixels` are viewport settings with no effect on the returned film, and `use_sample_count` is Cycles' session state (set by every render); a set that would change any of them is `CBC_INVALID` (read the sample count through a `sample_count` pass) |
| Integrator, film | sockets of the singletons, for example `max_bounce`, `sampling_pattern`, `seed`, `motion_blur`, `exposure`, `filter_type`. The integrator's `filter_glossy` is 1.0 unless the caller sets it (Cycles' default; 0 switches the glossy filter off). `denoiser_type` `dlss` is refused at a denoising render (`CBC_UNSUPPORTED`; the library denoises with `openimagedenoise`). `denoiser_passes` is Cycles' bit set of denoiser guides: albedo 1, specular albedo 2, normal 4, roughness 8, depth 16, backward motion 32, specular motion 64 (default albedo and normal); there is no other guide, and a value with any other bit is `CBC_INVALID` at a denoising render |
| Shader graphs | `cbc_host_shader_output`, `cbc_host_shader_node`, `cbc_host_connect`. A link between different value types (a colour into a float input, a float into a shader input, ...) gets Cycles' own conversion node when the graph is built; a closure output links only into a closure input (`CBC_UNSUPPORTED`); a link into a Boolean input (the UV Map node's `from dupli`, the Map Range node's `Clamp`) is refused by `cbc_host_connect` on every backend (`CBC_UNSUPPORTED`, "A Boolean input cannot be linked: Cycles has no conversion to a Boolean socket (set the input's value instead)": Cycles' conversion nodes cover float, int, colour, vector, point, normal, string and closure only, and it would build one from a null node type), as is any other link between value types outside that list ("Cycles has no conversion between the value types of this link"). Dispersion: a `principled_bsdf` whose `Transmission Dispersion Scale` and `Transmission Weight` are each linked or not 0 (Cycles' own test for a dispersive material) renders with dispersion on every backend. The Attribute, Image Texture and Environment Texture nodes have `Missing` (colour; default black on the Attribute node, magenta 1, 0, 1 on the texture nodes) and `Missing Alpha` (default 1) inputs: the value the node returns when its attribute or image does not exist. Refused by the WebGPU device (`CBC_FAILED`, the message names the node): `boolean_math`, `integer_math` and `get_vector_component` reached with a linked input (with constant inputs Cycles folds them away and every backend renders them) |
| Geometry | `cbc_host_mesh` (triangles; every face starts on material 0), the `shader`/`smooth` face sockets, attributes (`cbc_host_attribute_*`; on meshes the standard names `P`, `N`, `uv`, `generated`, `tangent`, `tangent_sign`, and `pointiness` per vertex and `random_per_island` per face, which the Geometry node reads and the caller computes). Lookup order: a shader that asks for an attribute by name (the Attribute and UV Map nodes) gets the attribute of that name first, also when the name is a standard name (`uv`, `color`, `pointiness`, ...) and the attribute was created without `CBC_ATTR_STANDARD`, then the standard attribute of that name; a shader that asks for a standard attribute without a name (the Texture Coordinate node's UV, the Geometry node's Pointiness, tangents) gets the attribute created with `CBC_ATTR_STANDARD`, else an attribute that has the standard's name, value type and domain. A request for a tangent also requests the UV map it is computed from. Quaternion and spherical-harmonics attributes (value kinds `CBC_VALUE_QUATERNION` 18 and `CBC_VALUE_SPHERICAL_HARMONICS_REST` 19; custom names on `CBC_ATTR_MESH`, `CBC_ATTR_FACE`, `CBC_ATTR_VERTEX` and `CBC_ATTR_CORNER` of a mesh and on `CBC_ATTR_MESH` and `CBC_ATTR_VERTEX` of a point cloud; quaternions also on `CBC_ATTR_MESH`, `CBC_ATTR_CURVE` and `CBC_ATTR_CURVE_KEY` of hair, the curve domains the pinned kernels fetch attributes from (`curve_attribute`, `kernel/geom/curve.h`: per key mixed along each segment, per curve; `primitive_surface_attribute`: the mesh domain); on volume geometry both, and spherical harmonics on hair, are `CBC_UNSUPPORTED` ("... quaternion attributes are accepted on meshes, point clouds and curves only", "... spherical-harmonics attributes are accepted on meshes and point clouds only (the pinned kernels read them only for Gaussian splats, kernel/geom/gsplat.h, and never on curves)"): a quaternion is 4 floats per element in Cycles' order w, x, y, z, stored and read back exactly; the Attribute node returns w, x, y as `Color` and `Vector`, their average as `Fac` and z as `Alpha`, and between vertices (and the keys of a curve segment) the components are interpolated one by one, unnormalized. A spherical-harmonics attribute holds the coefficients of bands 1 to 3 (band 0 is not part of it): 45 floats per element, 15 RGB coefficients in Cycles' order (L1 M-1, L1 M0, L1 M1, L2 M-2, ..., L3 M3), each in [-1, 1]; a value outside that range is `CBC_INVALID` ("Spherical-harmonics coefficients must lie in [-1, 1]"), never clipped. Cycles stores each coefficient at 8 bits over [-1, 1] (`quantize_spherical_harmonics`), so `cbc_host_attribute_get_floats` returns the stored value, as it returns a packed normal: at most 1/255 (half a step) from the value set. The Attribute node finds such an attribute and returns black with `Alpha` 1 (the pinned kernels have no shader reading of it). It cannot be a subdivision attribute (`CBC_UNSUPPORTED`: Cycles does not interpolate it over subdivided faces); a quaternion can. Every backend renders both. Subdivision: `cbc_host_subd_mesh` gives a mesh a control cage of polygon faces, tessellated by each render with Cycles' adaptive dicing when `subdivision_type` is `linear` (subdivision attributes with `CBC_ATTR_SUBDIVISION`), and by each bake of it (**PROVISIONAL**, object space only: Baking); subdivision faces with `subdivision_type` none are `CBC_INVALID` at render; Catmull-Clark is refused at render and bake (`CBC_UNSUPPORTED`): it needs OpenSubdiv, which the library does not include |
| Curves | `cbc_host_curves` (keys, one radius per key, each curve's first key; every curve starts on material 0), `cbc_host_curve_shape` per hair geometry, `cbc_host_curve_subdivisions` for the scene, `cbc_host_curve_motion` (deformation motion, below), the `curve_shader` socket, attributes on `CBC_ATTR_CURVE` and `CBC_ATTR_CURVE_KEY` after the curves (standard names `uv`, `generated`, `curve_intercept` per key, `curve_length` and `curve_random` per curve, which the Hair Info node reads), quaternion attributes as on meshes (Geometry, above). Curves and points with a Both displacement shader (a `used_shaders` entry whose `displacement_method` is `both` and whose output's `Displacement` input is linked) are refused at render and bake on every backend (`CBC_UNSUPPORTED`, "curves and points with a Both displacement shader": Cycles' bump from displacement reads values it never sets there, so the pinned kernels have no defined result; use `bump` or `true`) |
| Point clouds | `cbc_host_points` (positions, one radius per point; every point starts on material 0), `cbc_host_point_motion` (deformation motion with one radius per position), the `shader` socket, attributes on `CBC_ATTR_VERTEX`, standard attribute `point_random`, quaternion and spherical-harmonics attributes as on meshes (above). A Both displacement shader is refused as on curves (above). Gaussian splats: with `render_as` `gaussian_splats` each point renders as a 3D Gaussian on every backend, from the standard point attributes `radiance:base` (`CBC_VALUE_FLOAT4`: the band-0 spherical-harmonics coefficients of red, green and blue, then the opacity), `scale` (`CBC_VALUE_VECTOR`, the Gaussian's standard deviation along its axes), `rotation` (`CBC_VALUE_QUATERNION`, w, x, y, z; Cycles normalizes it) and, optionally, `radiance:sh` (`CBC_VALUE_SPHERICAL_HARMONICS_REST`, bands 1 to 3), each on `CBC_ATTR_VERTEX` and set with `CBC_ATTR_STANDARD` (Cycles finds them by standard only; another value kind or domain is `CBC_INVALID`). `radiance` is not a standard name here. At render a splat cloud without `radiance:base`, `scale` or `rotation` is `CBC_INVALID` ("Gaussian splats need the standard point attributes radiance:base, scale and rotation, set with CBC_ATTR_STANDARD (missing: ...)"): the pinned Cycles would draw plain points with no message, and its defaults (`create_missing_gsplat_attributes`) have no caller in the pinned source, so the host supplies none. A rotation none of whose components has a normal binary32 square (its squared length w*w + x*x + y*y + z*z is then zero or subnormal, and a GPU may flush it to zero) or whose squared length overflows binary32 is `CBC_INVALID` too ("Gaussian splat rotation of point N has zero length ...", "... is too long ..."): Cycles divides by the squared length (`quaternion_to_rotation`). Without a used shader (or for points whose `shader` index is past `used_shaders`) Cycles' default splat shader emits the radiance it evaluates from the coefficients for the view direction in object space; an Attribute node named `radiance` in a caller shader reads the same value. At every device update Cycles replaces a splat cloud's radius (center and motion steps) with each splat's bounding radius, computed from the opacity and `scale` (`PointCloud::update_gsplat_radii`); `cbc_host_attribute_get_*` return the radius given to `cbc_host_points` and `cbc_host_point_motion`, whose center step is compared with it, and the next edit of the cloud restores it before Cycles computes its own again. The bounding radius itself: `cbc_host_attribute_get_floats(host, cloud, "radius", CBC_ATTR_SPLAT_BOUNDING_RADIUS, ...)` (the flag, 4, may be combined with `CBC_ATTR_STANDARD`; no other call takes it, "Unknown attribute flags") returns the radius Cycles computed for each splat at the last render or bake, one float per point (the center step): 0 for an opacity below 0.01, else the largest component of `|sqrt(2 ln(opacity / 0.01)) * scale|`, computed on the host for every device. Before the first render, after an edit of the cloud and after a render that stopped before its device update finished it is `CBC_NOT_READY` ("The bounding radius of a Gaussian splat cloud is Cycles' and is read after a render or bake has computed it for the cloud as it is ..."); a geometry other than a point cloud with `render_as` `gaussian_splats`, a name other than `radius` and `CBC_ATTR_SUBDIVISION` are `CBC_INVALID` ("CBC_ATTR_SPLAT_BOUNDING_RADIUS reads the bounding radius of a Gaussian splat cloud only ...", "... reads the standard attribute radius only", "... not a subdivision attribute"). A splat cloud is never baked with its object transform. Motion: object motion and `cbc_host_point_motion` (positions; Cycles bounds each step itself); the splat attributes' own motion is not exposed |
| Particles | `cbc_host_particles` on a `particle_system`, read by the Particle Info node of objects with `particle_system` and `particle_index` sockets; `CBC_PARTICLE_FLOATS` floats per particle |
| BVH | Every backend builds Cycles' BVH2 with Cycles' defaults (static BVH, unaligned nodes for curves), except `CBC_HOST_CPU` (Embree) and MetalRT. BVH spatial splits (**PROVISIONAL**): `cbc_host_bvh_spatial_split(host, enabled)` is `SceneParams::use_bvh_spatial_split`; with 1 the BVH2 builder may split a primitive between nodes in the BVH of each geometry Cycles keeps in object space (`Geometry::is_instanced`: one that several objects use, or one with true displacement, subsurface scattering or subdivision; never in the scene's top level, for curves or for geometry with motion blur, `bvh/build.cpp`), and `CBC_HOST_CPU` builds at Embree's high quality (`bvh/embree.cpp`); MetalRT ignores it (`device/metal/bvh.mm`), the Metal backend's BVH2 (GPUs without ray-tracing hardware) honours it. A speed setting that changes no film by design: rays find the same closest hits (measured: the CPU reference and `CBC_HOST_CPU` bit for bit, Metal within its repeat noise, `BvhSpatialSplitTests`). 0, the default, or 1, else `CBC_INVALID`. A change has every BVH built again at the next render or bake (the host has Cycles rebuild each geometry's BVH without touching the geometry, so true displacement is not applied twice, and drop the scene BVH, as when a geometry is added). Rendered on every backend (the WebGPU device traverses Cycles' BVH2 and honours it) |
| Images | `cbc_host_image_create` (8 formats), `cbc_host_image_bind`, `cbc_host_image_update`, `cbc_host_image_release`; tiled images read on demand by Cycles' texture cache through a caller callback, `cbc_host_image_create_tiled` (**PROVISIONAL**, "Tiled images" below). UDIM tiles (**PROVISIONAL**): `cbc_host_image_bind_tiles(host, node, tiles, images, count)` binds `images[i]` to an Image Texture node at tile `tiles[i]`, Cycles' multi-loader UDIM texture; the tile covering UV [u, u + 1) x [v, v + 1) is 1001 + 10 * v + u (u 0 to 9), taken from the integer parts of the UV truncated toward zero, and the image is sampled at the UV minus them; a UV outside every bound tile (u of 10 or more, u or v of -1 or less, or an unbound tile) reads the node's `Missing` inputs. Tiles are distinct and 1001 to `INT_MAX`, every image is a live handle of the host and all have one format (Cycles decodes every tile with the first tile's settings, `ImageTextureNode::compile`); each violation, and a count of 0, is `CBC_INVALID` naming the tile, with nothing changed. An Environment Texture node is `CBC_UNSUPPORTED` ("UDIM tiles bind to Image Texture nodes only, as in Cycles"). It replaces the node's binding; `cbc_host_image_bind(node, 0)` removes it. The same image may be bound at several tiles (each is its own device image). The node's `tiles` socket is Cycles' list of UDIM tiles read from image files (the `<UDIM>` token of its file name), which this library does not read, so it has no effect on bound pixels: a set that would change it is `CBC_INVALID` (**PROVISIONAL**: "The Image Texture node's tiles socket is Cycles' list of UDIM tiles read from image files, which this library does not read; bind images as UDIM tiles with cbc_host_image_bind_tiles"); its default, the empty list, reads back and may be set again. Rendered on every backend. Texture limit (**PROVISIONAL**): `cbc_host_texture_limit(host, max_size)` is Cycles' texture limit (`SceneParams::texture_limit`): when a render or bake loads an image whose larger side exceeds `max_size`, Cycles reduces it on the host, before any device sees it, by the power-of-two scale that brings that side to `max_size` or below (`ImageManager::device_load_image`, `ImageCache::load_full`: each side times the scale, rounded down, at least 1 pixel, a box filter), so a 64 x 64 image under a limit of 40 loads as 32 x 32. It applies to caller images and to Cycles' own (the Sky Texture's 512 x 256 image), and brings an image within Metal's texture limit (Cycles' Metal device refuses a side above 16384, "Device errors"). 0, the default, keeps every image at its size; above `INT_MAX` is `CBC_INVALID`. The same value is always accepted; a different one is `CBC_NOT_READY` once a render or bake has loaded images, because Cycles does not load an image again (`need_load`) and the images already loaded would keep the old size. Rendered on every backend |
| Passes | `cbc_host_create_node("pass")` with `type` and a unique `name`; read with `cbc_host_pass_floats` |

### Motion

Object motion is the object's `motion` socket (an odd or even number of 3x4
transforms around `tfm`), camera motion the camera's `motion` socket, and
deformation `cbc_host_motion_positions` (odd step count 3..129, time-ordered
vertex arrays whose center equals the mesh's authored positions); for a hair
geometry `cbc_host_curve_motion` (the same steps, time-ordered key arrays whose
center equals the current keys, with radii per step or the center radii when
`radius` is null). All need
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
geometry's attributes. `cbc_host_attribute_get_*` return the authored values,
also the radius of a Gaussian splat cloud, which Cycles replaces at render
(Point clouds, above): the host keeps the authored radius from a render to the
next edit of the cloud, which writes it back first; Cycles' own radius reads
with `CBC_ATTR_SPLAT_BOUNDING_RADIUS` until that edit. A refused
attribute call is no edit (**PROVISIONAL**): `cbc_host_attribute_floats`, `_bytes`
and `_remove` check every argument against the authored attributes they would edit
before they restore anything, so one that fails changes nothing (no restore, no
re-render of the geometry; a splat cloud's bounding radius stays readable).

Displaced meshes (**PROVISIONAL**: the rule below). Cycles applies true
displacement (`displacement_method` `true` or `both`) by adding it to a mesh's
current positions and recomputing its normals from them whenever it updates the
mesh (`GeometryManager::displace`), so a second update would displace displaced
positions. The host keeps the authored attributes of every triangle mesh a render
displaces, from that render to the next edit of the mesh, and restores them before
Cycles displaces the mesh again: an edit of the mesh (sockets, attributes,
`cbc_host_mesh`, deformation motion) restores them first; a render restores them
when one of the mesh's displacement shaders changed (its graph, a value or image
in it, its `displacement_method`), when the object the displacement is evaluated
with (the first object that uses the mesh, as in Cycles) is another one or was
edited (`tfm`, `motion` or any other socket), when the scene's motion mode changed
(motion blur switched, a motion pass added or removed), and after a device recovery ("Device errors"), which updates every geometry; then Cycles
displaces the authored mesh again. A mesh whose shaders no longer displace gets its
authored attributes back. `cbc_host_attribute_get_*` return the authored values of
a displaced mesh. Measured on the CPU reference: after an edit of the `smooth`
flags, the face `shader` indices, a material no geometry used before, an attribute
the displacement reads, a value of the displacement shader, its method (to `bump`
and back), the object's `tfm` or a new instance, the film is bit for bit that of a
new host built with the edit (`DisplacementEditTests`); on Metal, after an edit and
after a device recovery, within the practical criterion. Subdivision meshes are
tessellated from their cage before each displacement and need no copy. Cycles
compiles only the shaders that nodes reference; a shader that becomes referenced
after a render (a `used_shaders` entry, the background `shader`) is compiled for
the next render (`Shader::tag_used`), as for a new host. Known limitations of the
rule (not tracked by the host): a displacement shader that reads a scene value the
host does not watch, such as the Scene Time node (`scene_attributes` `time` and
`frame`), is not displaced again when that value changes, so a displaced mesh keeps
the displacement of the earlier time until the mesh, its shaders or its object are
edited; and on the Metal backend with MetalRT, when the scene grows beyond MetalRT's
default limits (more than 16,777,216 instances or 268,435,456 primitives), Cycles
switches to its extended limits and tags every geometry modified
(`MetalDevice::set_bvh_limits`, called by `Scene::update_kernel_features`,
`scene/scene.cpp`) inside the render's own scene update, without the host restoring
the authored positions first, so a displaced mesh could be displaced twice. The second is known only from
reading the source; no film shows it.

Subdivision surfaces after a render (**PROVISIONAL**: the two rules below). Cycles
dices a cage again only when its positions or its dicing settings changed
(`Mesh::need_tessellation`), so an edit of a subdivision attribute alone
(`cbc_host_attribute_floats`, `_bytes` or `_remove` with `CBC_ATTR_SUBDIVISION`)
would reach no later render: the host tags the cage's positions, and the next
render or bake dices it again with the edit. A new cage (`cbc_host_subd_mesh`)
after Cycles has diced the mesh starts from no diced vertices (`Mesh::clear_non_sockets`);
before, the count of vertices the last dicing added survived, sized the new cage's
attributes wrongly and could end the process. Measured on the CPU reference: after a
UV layout edit and after a new cage, a render and a bake are bit for bit those of a
new host built with the edit (`SubdivisionBakingTests`).

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
size may change) and every texture still bound to that handle, as an image or
as a UDIM tile, uploads the new pixels on the next render; bindings made before
a release keep their pixels. An update that would give a UDIM tile another
format than a tile of the same binding bound to another image is `CBC_INVALID`,
with nothing changed. A tiled image (**PROVISIONAL**, below) cannot be updated
(`CBC_INVALID`); its handle, its bindings and Cycles' image of it each keep its
reader, and its release callback runs once the last of them lets go.

### Tiled images

**PROVISIONAL.** `cbc_host_image_create_tiled(host, config, read, release,
user_data, image)` creates an image whose pixels the caller supplies on demand:
Cycles' texture cache (`scene/image_cache.cpp`) loads only the tiles (64 x 64 by
default) and the mip levels a render or bake samples, through `read`, which writes one
rectangle of one level. No file and no image format is involved; decode your
own files (for .NET, CodeBrix.Imaging) and serve their pixels.

- `config` is that of `cbc_host_image_create` (width, height, one of the eight
  formats) plus `tile_size`; `read` is required (`CBC_INVALID` without it),
  `release` may be `NULL`. Bind the handle with `cbc_host_image_bind` (Image and
  Environment Texture nodes) or as UDIM tiles: a tiles binding holds tiled images
  only or plain images only (`CBC_INVALID`, naming the tile), because Cycles
  selects mip levels by the first tile's metadata.
- **Tile size (PROVISIONAL).** `config->tile_size` is the side of the square tiles
  Cycles loads: 0 (64, the default) or a power of two from 64 to 128 on the Metal
  backend and from 64 to 1024 on the others; anything else is `CBC_INVALID`
  ("Invalid tiled image tile size N: 0 (the default, 64) or a power of two from
  64 to M ...", and `release` runs before the call returns). The capability report
  states each backend's largest size (`backendScope.<backend>.maxImageTileSize`,
  **PROVISIONAL**: 128 on `metal`, 1024 on `cpu`, `cpu-embree` and `webgpu`). The pinned Cycles' texture cache takes square
  power-of-two tiles (`ImageCache::load_image_tiled`; the kernel maps texels to
  tiles by shifts) and never uses one below 64 (`ImageMetaData::detect_tiles`
  loads a file's smaller tiles in blocks of
  `DebugFlags().texture_cache.min_tile_size`, 64). The tiles of one format and
  interpolation share pool images of 64 tiles of (size + 4) x (size + 4) pixels in
  a row (`ImageCache::alloc_tile`), and Cycles' Metal device refuses an image wider
  than 16384 (`MetalDevice::image_alloc`), so 128 there. On the CPU the bound is
  the library's, for memory: Cycles allocates a pool image whole at the first
  tile load of its format and interpolation, 64 x (size + 4)^2 pixels in its
  storage type (for byte4 tiles of 64 1.2 MB, of 1024 271 MB, 1.1 GB for float4;
  of 2048 four times as much, of 65536 1.1 TB of address space), and a tile of
  t x t pixels is read and staged whole; an allocation the platform refuses is
  Cycles' `std::bad_alloc`, which it does not handle. So the CPU backends stop at
  1024, below what Cycles' CPU kernel would address (it finds a texel of the pool
  by a float pixel coordinate, `kernel_image_tile_map`, `ImageInterpolator`, which
  holds every texel centre up to a pool width of 2^23, reached by tiles of 65536).
  `cbc_host_image_create` and `cbc_host_image_update` need
  `tile_size` 0 (`CBC_INVALID`, "A tile size applies to tiled images only ...").
  Measured at every size on the CPU (images 8 rows high) and at 64 and 128 on
  Metal: Cycles selects the mip levels from the footprint and reads them in tiles
  of that size, and Closest renders bit for bit like the same pixels bound
  plainly.
- **Levels.** Level 0 is the image; level L is `max(1, width >> L)` x
  `max(1, height >> L)` pixels, and the levels run from 0 to the first one whose
  sides are both at most the tile size (64 by default). An image smaller than
  its tile size in both sides is a
  plain image to Cycles (`ImageMetaData::detect_tiles`): it is read once, whole,
  as level 0. The levels are the caller's pixels (Cycles' own are 2 x 2 box
  reductions); Cycles selects a level per sample from the ray footprint
  (`kernel_image_tile_map`: log2 of the texel footprint, stochastically within
  half a level), and a texture limit (`cbc_host_texture_limit`) skips the top
  levels instead of reducing pixels.
- **The read callback** `int read(user_data, level, x, y, width, height, pixels,
  row_bytes)` writes `width` x `height` pixels from column `x` and row `y` of
  the level, rows bottom first (row y at v = y / level height, the order of
  `cbc_host_image_create`), `row_bytes` apart, in the image's format, and
  returns 0. Float and half values must be finite, as for
  `cbc_host_image_create` (a value that is not fails the render: "Tiled image N:
  the tile reader wrote a nonfinite value (...)", never zeroed). The host asks for a tile with its 2-pixel border (the padding
  Cycles' Linear and Cubic filtering reads across tile edges) in one call when
  the tile is inside the image, and for the wrapped strips separately at the
  image's edges with the Periodic extension; outside the image the border
  follows the texture's extension with the kernel's own rules (clamp, periodic,
  mirror, zero for Clip). It runs on native render threads (the CPU device's
  worker threads on a cache miss, the GPU device's tile loading between kernel
  steps, the scene update), only during `cbc_host_render` and `cbc_host_bake`,
  and never concurrently with this host's other read and release callbacks:
  the host serializes them, so the caller's reader need not be thread-safe. It
  must not call the library (`CBC_NOT_READY`, "Host functions cannot be called
  from a tiled image's read callback").
- **Failure, never a Missing colour.** Cycles samples the texture's `Missing`
  colour for a tile that did not load (`KERNEL_TILE_LOAD_FAILED`); instead a
  nonzero return stops every further read, cancels the render and fails it with
  `CBC_FAILED` and "Tiled image N: the tile reader failed (returned S; level L,
  x X, y Y, W x H)" (the output is not written); every message of such a
  failure starts with "Tiled image N:". A cancel request
  (`cbc_host_cancel`, `cbc_host_destroy`, an event callback's nonzero return)
  stops further reads too: the threads waiting for the reader give up at once,
  so a render that waits on slow reads stops after the read in progress. Either
  way the next render reads the image's tiles anew.
- **Release.** `release(user_data)` runs exactly once for every call that passed
  it: when the handle is released, no binding uses the image and Cycles has
  freed its image (at the next render or bake), at `cbc_host_destroy`, at
  process exit with the host live (the exit hook), or, when the call fails or is
  refused, before it returns. It runs in the callback slot (never concurrently
  with a read of the host) and must not call the library either.
- **Cycles' behaviour to know.** The cache keeps the tiles it loaded for later
  renders, so an unchanged scene reads nothing more. Outside the image (UVs
  beyond [0, 1]) Cycles' tiled path wraps the UV before it filters
  (`kernel_image_tile_wrap`: Clip returns zero, Extend clamps the UV), whereas a
  plain image wraps each texel, so Linear with Clip and Cubic with Clip or Extend
  differ from a plain image there. A Linear or Cubic lookup adds the tile's
  slot in Cycles' tile pool to its pixel coordinate before it filters, so its
  last bits follow the order in which the cache loaded the tiles: with several
  render threads two renders of one scene in two processes can differ in the
  last bits (measured on the CPU: two films in six runs on four threads, one
  film in six runs on one thread). Measured on the CPU: Closest renders bit for
  bit like the same pixels bound plainly for every extension, and Linear and
  Cubic meet the practical criterion inside the image (the tiled path's pixel
  coordinates round differently); on Metal, where a cache miss restarts the
  path and the tiles load between kernel steps, the film meets the practical
  criterion against the CPU's. Inside a tiled render (`cbc_host_tile_size`) and a
  camera-border render, and both, a tiled image renders bit for bit like the same
  pixels bound plainly in the same render, on the CPU and on Metal (measured with
  Closest at mip level 0): a GPU restarts a path after a cache miss with its own
  state and pixel; only a miss of the camera ray itself, which needs an OSL
  camera that this library does not include, would rebuild the pixel from the
  camera width (`kernel/integrator/init_from_camera.h`).
- Rendered on every backend. The WebGPU device maps a lookup to its mip level
  and tile as the CPU kernel does and, when a lookup asks for a tile that is not
  loaded, has the host load the requested tiles and runs that kernel again from
  the state it started with (`native/cycles/image.cpp`, `tile_transaction`), so
  the run that counts reads loaded tiles only, as the CPU does; its film meets
  the practical criterion against the CPU's, through every projection (Cycles
  maps the coordinate of the sphere, tube and environment projections with dual
  numbers there, and so does the device) and in volume shaders.

### Tiled rendering

**PROVISIONAL.** `cbc_host_tile_size(host, tile_size)` has the host's later
renders and bakes render in Cycles' tiles of `tile_size` x `tile_size` pixels
(`SessionParams::use_auto_tile` and `tile_size`), one after another, so the
device holds the render buffers of one tile at a time.

- **Sizes.** 0, the default, renders the whole film (or its camera border) in
  one tile. Otherwise 8 to 128, or a multiple of 128 up to 8192: the sizes Cycles
  uses as given (`TileManager::compute_render_tile_size` rounds a larger size up to
  a multiple of 128 and stops at 8192). Any other value is `CBC_INVALID` ("Tile size
  must be 0 (one tile), 8 to 128, or a multiple of 128 up to 8192"). Cycles renders
  one tile when the tile's area is at least that of the film (or border region)
  and no side exceeds 8192 (`Session::get_effective_tile_size`).
- **No file.** Cycles writes the finished tiles of a tiled render to a tile file
  through OpenImageIO and reads it back for the full frame (`session/tile.cpp`). In
  this library the shared overlay of `integrator/path_trace.cpp`
  (`native/tools/cycles_overlay.py`) keeps them in system memory instead, with
  exactly what that file would hold, on every platform: no file is written and no
  image format or OpenImageIO plugin is involved. After the last tile, on the
  thread that called `cbc_host_render` or `cbc_host_bake`, the host reads the full
  frame back (`Session::process_full_buffer_from_disk`): with denoising Cycles
  denoises the whole film once, and the film and every pass are returned as for
  an untiled render (a camera border's region at its place, zero outside). Status
  events name the steps ("Reading full buffer from memory", "Denoising",
  "Finishing"). The finished tiles take one copy of the whole film's render
  buffers (every pass) in system memory, two for a moment while they are read
  back, freed before the render returns; a cancelled or failed render drops them
  unread. The device report states `tiledRendering`: `tileSize`, the tiles
  kept in memory for the host's renders and bakes (`tilesKeptInMemory`), the full
  frames read back (`framesReadBack`) and the tiles held now (`tilesHeld`, 0
  outside a render).
- **What Cycles does differently with tiles.** It adds a sample count pass (the
  film's `use_sample_count`, which normalises each pixel by its own count), renders
  4 pixels beyond each tile's edges for adaptive sampling (not when baking), and
  applies `cbc_host_render_time_limit` to each tile.
- **Results.** Measured on the CPU reference: a tiled film and every pass are bit
  for bit the untiled ones (plain, named passes with light groups, Cryptomatte and
  a sample count pass, camera border, adaptive sampling with its sample counts,
  bakes; `TiledRenderingTests`, `render-settings/tiles-glass-passes`). With
  denoising, Open Image Denoise receives the noisy image divided by each pixel's
  sample count instead of the sums an untiled render without a sample count pass
  hands it (`integrator/denoiser_oidn.cpp`): the tiled film is bit for bit an
  untiled render with a `sample_count` pass, and meets the practical criterion
  against one without (measured `render-settings/tiles-border-denoise`: maximum
  2.7e-5, 0 practical failures). On Metal a tiled film meets the practical
  criterion against the untiled Metal film without adaptive sampling. With adaptive
  sampling, Cycles' Metal device stops pixels of small render buffers (tiles are
  small buffers) at other samples than the CPU device, which decides identically
  at every size (measured `render-settings/tiles-glass-passes`: 327 of 3,072
  pixels stop at 16 of 32 samples in tiles of 16, 806 practical failures against
  the untiled Metal film; native/DEVICE.md item 172). On the WebGPU device a tiled
  film meets the practical criterion against the CPU film, with adaptive sampling
  too (measured `render-settings/tiles-glass-passes`: the sample count pass equals
  the CPU's in every pixel; native/DEVICE.md item 217).
- **Bakes** render in tiles too; each tile reads its region of the bake map.
- **Progressive results.** On a host created with `CBC_HOST_PROGRESSIVE_RESULTS`
  each film places the tile being rendered in the film beside the tiles rendered
  before it (zero where no tile has rendered yet; `samples_done` counts the
  samples of the tile being rendered), and the last film of a completed render is
  the returned film.
- **Cancel.** A cancel request stops the tile being rendered: the render returns
  `CBC_CANCELLED`, the output is not written and nothing stays in memory.
- Rendered on every backend.

### Passes

`Combined` is always captured. Other passes are captured when a pass node has a
nonempty `name` (other than `Combined`); the first pass of a duplicated name
wins.

Denoising (integrator `use_denoise`): the passes Cycles' Open Image Denoise
denoiser writes, `Combined`, `shadow_catcher` and `shadow_catcher_matte`
(caller-created ones included), are captured denoised by default. A pass
node's `mode` socket set to `noisy` (added within ABI 1; no new export)
requests the undenoised version in the same render, for example a `combined`
pass named `Noisy Image` with `mode` `noisy` beside `Combined`; use one noisy
request per pass type. In a render without denoising a noisy request reads the
same values as its twin (Combined, or the other pass of its type). Every other
pass type is never denoised and its `mode` has no effect. Light group passes
are never denoised: with denoising they keep their noisy values beside the
denoised `Combined`. After a successful render, `cbc_host_render_passes_json` lists
`name`, `type`, `components` and `written`, and `cbc_host_pass_floats` copies
one pass (`components` floats per pixel, raw scene-linear values, lower-left
origin). A render that starts clears earlier results; until a render succeeds
both return `CBC_NOT_READY`. A pass the device did not write returns
`CBC_UNSUPPORTED`. AOVs are `aov_color`/`aov_value` passes fed by an
`aov_output` shader node with the same name. A light group pass is a `combined`
pass with a `lightgroup` (and a name); lamps, emissive objects and the world
join it through their object's, or the background's, `lightgroup` socket.
Cryptomatte: the film's `cryptomatte_passes` is a flag set (set it with
`cbc_host_set_ints` to any union of `object` 1, `material` 2 and `asset` 4)
and `cryptomatte_depth` the number of four-component `cryptomatte` passes per
type, created in the order objects, materials, assets (for example
`CryptoObject00`, `CryptoObject01`, `CryptoMaterial00`, ...). The ids hash the
native node names: objects and shaders have a string property `name` (the node
name, not a socket; `cbc_host_property_info`, `cbc_host_set_string` and
`cbc_host_get_string` accept it), and objects an `asset_name` socket. The motion
pass is written when `integrator.motion_blur` is off (Cycles' motion pass mode).

## Baking

`cbc_host_bake` bakes one mesh's UV layout into a raw float buffer through Cycles'
own bake kernel; `cbc_host_bake_map` returns the map that bake uses. The CPU
backends (`CBC_HOST_CPU_REFERENCE`, `CBC_HOST_CPU`) and the WebGPU device
(`CBC_HOST_WEBGPU`, its port of the same kernel) bake every type. `CBC_HOST_METAL` runs the
same host code on Cycles' own Metal bake kernel; it is not yet built or validated.
On the WebGPU device a bake of a mesh with deformation motion, or of a texel on a triangle with
no area, is refused by name (`CBC_FAILED`, with the device's message) and never falls back to the CPU. The capability report's
`baking` object states this: `backends` gives `validated` or `unvalidated`
per compiled backend, `types` the bake types.

```c
CBCBakeConfig config = {0};
config.struct_size = sizeof(config);
config.width = 1024; config.height = 1024; config.samples = 64;
config.type = CBC_BAKE_COMBINED;
config.object = object;                /* its geometry is a mesh with a "uv" corner attribute */
uint64_t required = 0; uint32_t components = 0;
cbc_host_bake(host, &config, NULL, 0, &required, &components); /* CBC_SMALL_BUFFER; does not bake */
float *pixels = malloc(required * sizeof(float));
cbc_host_bake(host, &config, pixels, required, &required, &components);
```

| `CBCBakeConfig` field | Meaning |
| --- | --- |
| `width`, `height` | output size, 1..32768 |
| `samples` | samples per pixel; `CBC_BAKE_POSITION` always takes one |
| `type` | one `CBC_BAKE_*` value, below |
| `flags` | `CBC_BAKE_USE_CAMERA`: the view direction points at the scene camera instead of along the normal. `CBC_BAKE_INCLUDE_ALBEDO`: light passes keep the surface colour |
| `object` | an object whose geometry is a triangle mesh, or (**PROVISIONAL**) a linear subdivision cage diced in object space (below) |
| `uv_attribute` | `NULL` or empty: the standard `uv` attribute. Otherwise a float2 corner or vertex attribute by name; of a subdivision cage, one of its subdivision attributes (`CBC_ATTR_SUBDIVISION`) |
| `aov_name` | AOV types only: the name of an existing AOV pass node of that type; `NULL` otherwise |

**Output.** `values[(y * width + x) * components]`, raw float32, row 0 at v = 0, the
same order as image input, so a baked image can be bound as a texture unchanged.
Pixel (x, y) is sampled at UV ((x + 0.5) / width, (y + 0.5) / height), jittered over
the texel from the second sample on (a pixel centre exactly on a triangle's edge is
jittered from the first). UVs outside [0, 1) reach no pixel: there is no tile or wrap.
A pixel no triangle covers reads zero, except `CBC_BAKE_DEPTH` (1e10) and
`CBC_BAKE_MIST` (1). `CBC_BAKE_COMBINED` alpha is 1 on covered pixels and 0 elsewhere.

| Type | Components | What it writes |
| --- | --- | --- |
| `CBC_BAKE_COMBINED` | 4 | the full light of the surface, RGBA |
| `CBC_BAKE_EMISSION` | 3 | light the surface emits |
| `CBC_BAKE_BACKGROUND` | 3 | the world, in the direction of the surface point seen from the origin (not the surface's shading) |
| `CBC_BAKE_AO` | 3 | ambient occlusion, up to the integrator's AO distance |
| `CBC_BAKE_DIFFUSE`, `_DIRECT`, `_INDIRECT` | 3 | diffuse light, divided by the diffuse colour unless `CBC_BAKE_INCLUDE_ALBEDO` |
| `CBC_BAKE_DIFFUSE_COLOR` | 3 | the diffuse colour |
| `CBC_BAKE_GLOSSY`, `_DIRECT`, `_INDIRECT`, `_COLOR` | 3 | the same for glossy |
| `CBC_BAKE_TRANSMISSION`, `_DIRECT`, `_INDIRECT`, `_COLOR` | 3 | the same for transmission |
| `CBC_BAKE_POSITION` | 3 | world-space position at the texel centre |
| `CBC_BAKE_NORMAL` | 3 | world-space shading normal |
| `CBC_BAKE_UV` | 3 | the standard `uv` attribute, third component 1 |
| `CBC_BAKE_ROUGHNESS` | 1 | the surface's roughness |
| `CBC_BAKE_OBJECT_ID`, `CBC_BAKE_MATERIAL_ID` | 1 | the object's and the shader's `pass_id` |
| `CBC_BAKE_DEPTH`, `CBC_BAKE_MIST` | 1 | as the render passes of those names, from the scene camera |
| `CBC_BAKE_AOV_COLOR`, `CBC_BAKE_AOV_VALUE` | 4, 1 | the AOV pass named `aov_name`, fed by its `aov_output` shader node |

Other pass types (volume, shadow catcher, cryptomatte, motion, denoising feature
passes) are not bake types. Film exposure applies as in a render. A bake runs one type at a
time because the kernel chooses its path by which passes exist.

**Map.** `cbc_host_bake_map` takes the same configuration and renders nothing, on any
backend. Per pixel: `triangles[i]`, the covering triangle's index in the mesh or -1,
and optionally six floats `u, v, du/dx, du/dy, dv/dx, dv/dy`, where the point is
`u * V0 + v * V1 + (1 - u - v) * V2` of the triangle's corners and x, y are pixels. A pixel
belongs to a triangle when its centre is inside or on the triangle's UV image; where
triangles overlap in UV space the lowest index wins; a triangle with no UV area
covers nothing. Triangles sharing an edge leave no crack.

**Subdivision surfaces (PROVISIONAL).** A mesh with `subdivision_type` `linear` and a
cage (`cbc_host_subd_mesh`) is baked through the triangles Cycles dices from the cage
for the bake (`Mesh::tessellate` in the device update, at the mesh's `subd_dicing_rate`
and `subd_max_level`). The host dices a copy of the cage the same way before it maps the
texels, through the diced mesh's UV attribute, which Cycles interpolates over each face
from the cage's (`subd/interpolation.cpp`); `triangles[i]` then indexes Cycles'
tessellation, the triangles the bake shades. Then it bakes as any mesh, every type on
every backend, with true displacement applied to the diced mesh. The UV layout is one of
the cage's subdivision attributes (`CBC_ATTR_SUBDIVISION`; otherwise `CBC_INVALID`, "The
bake mesh has no such UV attribute among its subdivision attributes ..."). Measured on the
CPU reference: every bake type, also with true displacement, is bit for bit the bake of
an ordinary mesh built from the same tessellation and kept in object space, as Cycles
keeps every subdivision surface (`SubdivisionBakingTests`); on Metal within the practical
criterion of it. Refused, `CBC_UNSUPPORTED`, on every backend: dicing in pixel space
(`subd_adaptive_space` `pixel`, Cycles' default; "Unsupported feature: bakes of
subdivision surfaces diced in pixel space ...": it measures in the pixels of a render
camera, which a bake does not have, so the triangles would depend on the scene's last
render; dice in `object` space) and Catmull-Clark ("Unsupported feature: Catmull-Clark
subdivision needs OpenSubdiv ..."). The pixel-space refusal covers every subdivision
surface of the baked scene, not only the bake target (**PROVISIONAL**): a bake of any
object is refused when another object's mesh is a linear cage diced in pixel space
(the same words, "... another subdivision surface of the baked scene has
subd_adaptive_space pixel ..."), because Cycles' device update dices such a surface in
the scene's dicing camera (`GeometryManager::device_update`, `scene/geometry.cpp`),
which the host keeps equal to the render camera at a render and which a bake does not
set: the bake would see it diced in the camera of the scene's last render (Cycles'
default before any render), or as the last render left it when it was not modified
since (`Mesh::need_tessellation`). `cbc_host_bake_map` maps the target alone and is not
refused for another surface. A mesh with `subdivision_type` `linear` and
triangles but no cage is baked as its triangles (Cycles dices nothing).

**What the package does not do.** It fills no margin: pixels no triangle covers stay
empty, and an application that wants a margin dilates its baked image from
`cbc_host_bake_map`. It does not project from other meshes (selected-to-active):
each bake shades the object's own surface. Normals are world-space; tangent and object
space are the application's to convert.

**Scene state.** A bake leaves the scene as it found it: the caller's pass nodes,
`is_bake_target` sockets and the film's `display_pass` are back afterwards, and a later render,
with or without edits between, is bit for bit that of a new host on the CPU reference, and within the
practical criterion of it on the WebGPU device, whose film adds in atomic order (native/tools/bake_gate.py). Caller passes other than AOVs do not take part in a bake.
A bake clears the last render's results (`cbc_host_pass_floats` and
`cbc_host_render_passes_json` return `CBC_NOT_READY` until a render succeeds) and writes no
results of its own to them. Failed and cancelled bakes never write the output buffer.
Events, `cbc_host_cancel` and threading are those of `cbc_host_render`. The integrator's
settings apply: adaptive sampling works; with denoising the Combined output is denoised as an image of the
UV layout, so values bleed across island edges and into empty pixels. A denoiser upscale factor
other than 1 is refused, as at render, before anything bakes (**PROVISIONAL** for bakes;
`CBC_UNSUPPORTED`, "Unsupported feature: a denoiser upscale factor other than 1 ...", every
backend). In a scene with an
approximate shadow catcher and an opaque background the film gains a background pass, which
the kernel reads as a request to bake the world: the bake is the world's, as Cycles writes it.

## Exports (ABI 1, 65 functions)

| Function | Purpose |
| --- | --- |
| `cbc_host_abi_version` | ABI integer (1) |
| `cbc_host_version` | library version, ABI integer and pinned Cycles revision |
| `cbc_host_error` | copy the calling thread's last error message |
| `cbc_host_color_config_path` | select the process-wide OCIO configuration |
| `cbc_host_tier_config` | process-wide WebGPU pipeline-tier policy (`CBCTierConfig`) |
| `cbc_host_pipeline_cache_config` | process-wide switch and directory of the persistent WebGPU pipeline cache (`CBCPipelineCacheConfig`) |
| `cbc_host_resolve_backend` | map a requested backend (including `CBC_HOST_GPU`) to this binary's concrete backend |
| `cbc_host_create` | create a host (`CBCHostConfig`; `flags` **PROVISIONAL**: `CBC_HOST_PROGRESSIVE_RESULTS`) |
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
| `cbc_host_curves` | replace a hair geometry's curves |
| `cbc_host_curve_shape` | ribbon, thick or thick linear curves of a hair geometry |
| `cbc_host_curve_subdivisions` | ribbon sections per curve segment for the scene, 2^n, n 0 to 4 |
| `cbc_host_curve_motion` | deformation motion steps of a hair geometry (keys, optional radii) |
| `cbc_host_points` | replace a point cloud's points |
| `cbc_host_point_motion` | deformation motion steps of a point cloud, one radius per position |
| `cbc_host_particles` | replace a particle system's particles |
| `cbc_host_attribute_floats` / `cbc_host_attribute_bytes` | set a geometry attribute (`CBCAttributeConfig`) |
| `cbc_host_attribute_get_floats` / `cbc_host_attribute_get_bytes` | read a geometry attribute |
| `cbc_host_attribute_remove` | remove a geometry attribute |
| `cbc_host_image_create` | copy caller pixels into a new image handle (`CBCImageConfig`) |
| `cbc_host_image_update` | replace the pixels of an image handle |
| `cbc_host_image_bind` | bind an image (or 0) to an image/environment texture node |
| `cbc_host_image_bind_tiles` | **PROVISIONAL**: bind images to an Image Texture node as UDIM tiles |
| `cbc_host_image_create_tiled` | **PROVISIONAL**: a tiled image whose tiles and mip levels Cycles' texture cache reads through a caller callback ("Tiled images") |
| `cbc_host_image_release` | release an image handle |
| `cbc_host_set_event_callback` | register the host's event callback |
| `cbc_host_render` | render synchronously into caller RGBA (`CBCRenderConfig`) |
| `cbc_host_cancel` | cancel the active render |
| `cbc_host_is_rendering` | whether a render is active |
| `cbc_host_pass_floats` | copy one captured pass |
| `cbc_host_render_passes_json` | list the passes captured by the last render |
| `cbc_host_bake` | bake one mesh's UV layout to raw floats (`CBCBakeConfig`) |
| `cbc_host_bake_map` | the map of a bake: triangle and coordinates per pixel, no rendering |
| `cbc_host_subd_mesh` | replace a mesh's geometry with a subdivision cage (polygon faces) |
| `cbc_host_render_time_limit` | wall-clock time limit of the host's later renders and bakes (0 = every sample) |
| `cbc_host_texture_limit` | **PROVISIONAL**: Cycles' texture limit, the largest image side a render or bake loads (0 = every image at its size) |
| `cbc_host_bvh_spatial_split` | **PROVISIONAL**: Cycles' BVH spatial splits (0 = off, the default) |
| `cbc_host_tile_size` | **PROVISIONAL**: Cycles' tile size of later renders and bakes, the finished tiles kept in memory (0 = one tile, the default; "Tiled rendering") |

## Structs and enums

| Struct | Size | Fields |
| --- | --- | --- |
| `CBCHostConfig` | 40 (32 accepted) | `struct_size`, `abi_version`, `backend`, `threads` (0 = automatic, at most 1024), 4 reserved, `flags` and `reserved2` (**PROVISIONAL**, trailing; 32 bytes means no flags) |
| `CBCRenderConfig` | 32 | `struct_size`, `width`, `height` (1..32768), `samples`, 4 reserved |
| `CBCPropertyInfo` | 32 | `struct_size`, `value_type`, `flags`, `components`, `count`, reserved |
| `CBCAttributeConfig` | 32 | `struct_size`, `value_type`, `domain`, `flags`, 4 reserved |
| `CBCImageConfig` | 32 | `struct_size`, `width`, `height`, `format`, `tile_size` (**PROVISIONAL**: tiled images only, else 0), 3 reserved |
| `CBCTierConfig` | 32 | `struct_size`, `abi_version`, `tier`, `flags`, 4 reserved |
| `CBCPipelineCacheConfig` | 32 | `struct_size`, `abi_version`, `mode`, `flags` (0), 4 reserved |
| `CBCVersionInfo` | 96 | `struct_size`, `abi_version`, 2 reserved, `library_version[32]`, `cycles_revision[48]` |
| `CBCEvent` | 56 | `struct_size`, `kind`, `samples_done`, `samples_total`, `elapsed_seconds`, `message`, `film`, `width`, `height` (the last three **PROVISIONAL**, trailing) |
| `CBCBakeConfig` | 64 | `struct_size`, `width`, `height`, `samples`, `type`, `flags`, 2 reserved, `object`, `uv_attribute`, `aov_name`, reserved |

Callback types: `CBCEventCallback`; `CBCImageTileReadCallback` and
`CBCReleaseCallback` (**PROVISIONAL**, "Tiled images").

Enums (values are fixed): backends (above); error codes (above); value kinds
`CBC_VALUE_*` 0..19 (18 `CBC_VALUE_QUATERNION` and 19 `CBC_VALUE_SPHERICAL_HARMONICS_REST` are
geometry attributes only); property flags `CBC_PROPERTY_*` (1, 2, 4, 8, 16);
attribute domains `CBC_ATTR_MESH`..`CBC_ATTR_CURVE_KEY_NORMAL` (1..10) and flags
`CBC_ATTR_STANDARD` (1), `CBC_ATTR_SUBDIVISION` (2), `CBC_ATTR_SPLAT_BOUNDING_RADIUS` (4, reads only); curve shapes
`CBC_CURVE_RIBBON/THICK/THICK_LINEAR` (0..2); image formats
`CBC_IMAGE_FLOAT`..`CBC_IMAGE_USHORT4` (1..8); tiers `CBC_TIER_AUTO/FULL/CONSERVATIVE`
(0..2) and tier flags (1, 2, 4); pipeline cache `CBC_PIPELINE_CACHE_DEFAULT/OFF` (0, 1); events `CBC_EVENT_STATUS/PROGRESS/ERROR/FILM` (1..4; 4 **PROVISIONAL**);
host flags `CBC_HOST_PROGRESSIVE_RESULTS` (1, **PROVISIONAL**);
bake types `CBC_BAKE_COMBINED`..`CBC_BAKE_AOV_VALUE` (1..26) and bake flags (1, 2).

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
  CPU kernels and the Metal backend are both gold standards: this is a
  characteristic of the Metal reference, reported, not a failure (native/DEVICE.md,
  "Validation reference policy").
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

CBCHostConfig config = {sizeof(config), CBC_HOST_ABI, CBC_HOST_GPU, 0, {0}, 0, 0};
CBCHost host = 0;
if (cbc_host_create(&config, &host) != CBC_OK) { /* cbc_host_error(...) */ }

CBCNode integrator, camera, material, diffuse, output, mesh, object;
cbc_host_scene_node(host, "integrator", &integrator);
const int64_t off = 0;                            /* a plain, deterministic film for the example */
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
