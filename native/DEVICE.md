# Registered Cycles device and wavefront queue

The new `native/cycles` implementation registers `DEVICE_WEBGPU` with the
original Cycles device factory and implements `DeviceQueue`. The Linux gate
passes against the unchanged CodeBrix.WebGpu.Native package. This connects
scene storage, scheduler control, limited camera initialization, closest
intersection, selected SVM surface bounces and background/film output to WebGPU.
A complete bounded scene-to-image loop now runs through the original native GPU
scheduler. The maintained [experimental C host and .NET wrapper](host/README.md)
now cover a bounded `Session` lifecycle; general feature support and complete
platform/application validation remain pending. The earlier
[snapshot renderer](README.md) remains
the separate, working image reference on Linux and Android.

Platform scope: this WGSL device is the GPU backend on eight of the nine RIDs,
including macOS on Intel (osx-x64), where the WebGPU runtime reaches the GPU
through Metal. macOS on Apple Silicon (osx-arm64) renders with Cycles' own Metal
device instead and never uses this device; see
[host/MACOS_ARM64.md](host/MACOS_ARM64.md).

The latest support and validation status is in
[Integrated handoffs and current limits](#integrated-handoffs-and-current-limits-2026-10-19).
Earlier milestone sections preserve their original test scope and limitations.

## Current session milestone

Prove that Cycles’ native scene preparation and render scheduler can drive a
complete, repeatable GPU render through the shipped CodeBrix.WebGpu.Native
runtime. Full Cycles functionality remains the longer-term objective, with
all previously requested RIDs retained, including Windows ARM64 and Linux
RISC-V64. This session does not include Metal development.

Acceptance criteria:

- Native scene preparation and native shader compilation feed a complete WGSL
  render loop with multiple bounces and selected rough diffuse, translucent,
  transparent, emissive and mixed materials.
- Images and intermediate path states are compared against the original CPU
  kernels, with recorded tolerances and failures.
- At least one ForestCommunity model renders using the supported material subset.
- The relevant GPU workload runs on Linux and the three Android test devices,
  including API 33, through unchanged shipped WebGPU binaries. Native scene
  replay establishes GPU portability; native Cycles Android host builds are
  a separate milestone.
- Unsupported features fail explicitly; tests, runtime hashes and provenance
  make the result reproducible.

Status: the bounded rendering proof passes on Linux, with complete command replay
on all three Android devices. The [native-render checkpoint](evidence/native-render/validation.json)
records the acceptance results. This does not establish full Cycles parity.
The current host uses filtered source and independent Apache-2.0 atomics.
See the root release-license plan for dependency and package audit status.

## Complete native scheduler render

`render_gate.cpp` prepares a real `Scene` on `WebGPUDevice`, compiles its shader
graphs through Cycles, and drives the original scheduler's camera, closest,
surface and background entries until all paths terminate. The selected surface
kernel includes closure sampling/mixtures, emission, holdout, roulette throughput,
ray offsets and native queue/film updates. It uses buffer-backed closure scratch.
It does not use the earlier snapshot renderer as a scheduler substitute.

Two 64 x 48, 8-sample fixtures pass independent original-CPU image comparisons:

| Fixture | Kernel stages | Path invocations | Surface shaders | Max bounce / transparent counter | Max CPU image error |
| --- | ---: | ---: | ---: | ---: | ---: |
| Mixed-material enclosure | 157 | 136,344 | 21 | 5 / 3 | 5.43594e-5 |
| ForestCommunity bench, rough clay | 120 | 165,304 | 1 | 5 / 0 | 1.19209e-6 |

Each fixture renders twice on the Linux native host with bitwise-identical GPU
film. The first run also compares intermediate state and film to the original
CPU kernels from identical incoming state. A separate CPU path loop supplies the
independent final image. Integer state and counters are checked exactly; floats
use `5e-5 + 2e-4 * abs(reference)`. Stricter `1e-7 + 1e-7 * abs(reference)`
diagnostics still report differences, so this is not strict numerical parity.

The bench has one explicitly recorded shared-edge exception: CPU primitive 178
and GPU primitive 179 at stage 47/path 752. Both are flat, constant-clay triangles
of the same object/material, share an edge, and have matching hit position and
geometric normal. Only their primitive ID and two barycentrics use this semantic
check; independent film comparison remains unchanged. `run.shared-edges.json`
retains both intersections. This exception does not apply to smooth/textured
geometry or material/attribute seams. The triangle dot adapter preserves the
original SSE CPU reference's y/z-first addition grouping.

That frozen isolated surface integration gate additionally passes 19 cases / 18,360
paths, 1,478,656 integer and 2,510,736 float checks, plus 20 explicit rejection
cases. Maximum absolute float error is 7.53403e-5 within the relative/absolute
tolerance; stricter differences, including film accumulation order, remain.

`RenderCapture` records the second native render's actual uploads, clears,
dispatch parameters and scheduling readbacks. The small native replay driver
checks every scheduling readback before proceeding and never injects expected
results. The mixed scene has 901 commands / 346 exact readbacks; the bench has
688 commands / 272 exact readbacks. Both pass on Linux and all Android devices:

| GPU target | API | Mixed-scene maximum CPU film error | Bench maximum CPU film error |
| --- | --- | ---: | ---: |
| Linux x64 Intel UHD | Vulkan | 5.43594e-5 | 1.19209e-6 |
| Pixel 4 XL ARM64 Adreno 640 | Android 33 | 3.05176e-5 | 2.14577e-6 |
| Samsung SM-X700 ARM64 Adreno 730 | Android 36 | 3.05176e-5 | 2.14577e-6 |
| HP Android x64 Intel Xe | Android 33 | 5.43594e-5 | 9.53674e-7 |

All replay films compare against the independent CPU image at the same tolerance.
Every terminal path slot is zero, device error words are zero and native scene
tables remain unchanged. Package runtime hashes and loaded paths are checked.
Android executes complete GPU command sequences captured from the Linux native
host; a complete native Cycles host on Android remains a separate gate.

The preview at `work/render/bench-preview/ForestCommunity-bench-clay-1024x768-32spp.png`
is 1024 x 768 at 32 samples. It uses a blue constant world (RGB 0.25/0.4/0.6,
strength 0.8), no lamps, and rough diffuse clay in an open-front enclosure.
Original textures/materials are not represented. Raw linear film is retained;
the display preview uses Reinhard mapping and gamma 2.2, not a finished OCIO
display pipeline. Preview mode is not the independent-CPU proof mode.

Reproduce the full gate with `python3 native/tools/render_gate.py run` after
preparing the bench geometry as documented in `parity/ASSETS.md`. It rebuilds,
runs earlier regressions, native proof fixtures and four-device replays. `collect`
validates existing working outputs and tests that a changed expected scheduling
readback is rejected. `archive` creates `evidence/native-render` once and refuses
to overwrite it. No executable or native dependency library is archived.

General lighting/MIS, tiled/sparse image paths, most closures, volumes, motion,
curves/points, guiding, MNEE and general film/session modes still require work.
The broad roadmap is the project plan, kept outside the repository;
full functionality on all nine requested RIDs remains its target, through this
device on every RID except osx-arm64, which uses Cycles' Metal device.

## What is implemented

`Scene::device_update` runs directly against `WebGPUDevice`. Native Cycles
builds the BVH and SVM programs and uploads its original device tables. The
backend keeps their raw layouts, including `KernelData`, and assigns virtual
byte addresses to `device_ptr` values. Scene data is no longer prepared on a
CPU device for this test. Scene preparation itself remains native host work,
as it does with other Cycles GPU devices.

The first allocator uses one WebGPU storage-buffer arena with 16-byte aligned
allocations, coalescing free ranges, stable virtual addresses and GPU-to-GPU
growth copies. Zero/copy/readback operations respect queue order. It is bounded
by the device's single storage-binding limit; paging or multiple arenas will
be needed before claiming large-scene support. This is not a performance gate.

The original `PathTraceWork::create` selects `PathTraceWorkGPU` for this device.
The original GPU scheduler allocates its native structure-of-arrays state,
uploads `IntegratorStateGPU`, queues reset and reads its queue counters through
the new backend. No CPU rendering or alternate scheduler stands in for these
operations.

Twelve scheduler kernels have WGSL implementations:

- Reset both main and shadow path queues.
- Select queued main/shadow paths, active main paths, terminated main/shadow
  paths, and main/shadow paths needing compaction.
- Compute shader-count prefix sums and select sorted paths with deferred work
  when a dispatch limit is reached.
- Compact main and shadow states.

WGSL's 32-bit atomic storage also carries Cycles' 8-bit and 16-bit fields.
Masked atomic updates prevent adjacent state writes from overwriting each
other. State-copy descriptors come from the original state templates and native
`sizeof` values; all allocated fields are copied, including vector payloads
and array members. MNEE's cross-path ownership repair was explicitly rejected
at this checkpoint; it is implemented since "Manifold next-event estimation
(caustics)" below.

## Control checkpoint

The [Linux report](evidence/device-queue/linux-x64/validation.json) records:

| Check | Result |
| --- | --- |
| Device discovery, type names and factory creation | Pass |
| Original GPU path-work selection, allocation and reset | Pass |
| Original scene tables uploaded and read back unchanged | 11 |
| Reset sizes, including zero and partial workgroups | 7 |
| Path-selection modes, membership/counts and guards | 7 |
| Sorting limits, including zero and partial buckets | 6 |
| Main and shadow compaction versus original helpers | 2 |
| Native state fields / state slots exercised | 123 / 4,099 |
| Invalid inputs, unsupported MNEE and invalid GPU sort key | 7 rejected |
| Camera dispatch at this earlier checkpoint | Explicit error; implemented below in the next checkpoint |
| Cycles allocation accounting after cleanup | Zero bytes |

The prefix-sum and sorted-index references execute the original upstream
helpers. State compaction executes the untouched original state-copy/move
function bodies using host arrays, with MNEE disabled in that oracle. Every
byte of every tested state field is compared, including untouched guards.
Selection membership is compared exactly; atomic reservations may produce a
different valid order within a shader bucket, so sorting verifies bucket
membership, unique indices, counts, deferred counters and output guards.

Additional tests force arena growth after a queued GPU write and verify its
result at the same virtual address. They check range reuse, a host upload
following a pending kernel, and an unaligned partial-byte readback. A bad sort
key reaches the GPU check and is reported through the queue's status word.

The main gate made 30 compute dispatches. Its numerous host transfers inspect
individual fields for correctness; their counts are not a production workload
or a throughput measurement. Only Linux x64/Intel Vulkan has executed this
new registered-device gate. The previous four-device image evidence remains
valid for the snapshot renderer, not for these new scheduler kernels.

## Camera entry through the original scheduler

`DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA` now runs WGSL against the original
native `KernelData`, sampler/filter tables and `IntegratorStateGPU` arrays.
The original scheduler's `enqueue_work_tiles` uploads the actual work tiles,
dispatches the camera entry, reads the queue counters, and selects
`INTERSECT_CLOSEST` next. No CPU-prepared per-path rays or replacement scheduler
are involved. Native `offsetof`/`sizeof` values generate the WGSL layout
constants for the current ABI.

This entry implements static perspective pinhole rays, tabulated Sobol, pixel
filter or explicit jitter, clipping, compact ray differentials, path state
initialization and the queue transition. It also implements atomic film sample
reservation, the existing adaptive-pixel skip flag, and indexed camera retries.
Other camera modes, motion/depth of field and optional initialization features
(volumes, guiding, denoising, light linking and portals) are explicitly rejected.
Reading adaptive skip flags is not a complete adaptive-sampling implementation.

The [camera report](evidence/camera-queue/linux-x64/validation.json) records:

| Check | Result |
| --- | --- |
| Configurations, covering four scrambling distances and both work orderings | 24 |
| Initialized paths across multi-tile dispatches | 22,972 |
| Indexed retry paths and queue-counter transitions | 65 |
| Integer/state/guard comparisons | 3,422,188 |
| Floating-point comparisons | 254,276 |
| Untouched state bytes checked | 11,734,032 |
| Unsupported configurations, invalid film/pointers/tiles/indices | 12 rejected |
| Original scheduler selects closest intersection next | Pass |
| Cycles allocation accounting after cleanup | Zero bytes |

The reference executes the unchanged original CPU camera, path initialization
and film functions on the same prepared scene data. All non-ray state and film
bytes must match exactly. Atomic sample assignment can reorder samples among
lanes: the test first proves one-to-one membership in the independently computed
CPU `(pixel, sample)` set, then compares every resulting state. Untouched slots
and skipped pixels retain their expected bytes.

Ray checks retain the earlier camera tolerance, `5e-5 + 2e-4 * abs(reference)`.
Maximum absolute errors were `9.53674e-7` for position/direction/time/minimum
distance, `6.70552e-8` for compact differentials, and `0.000244141` for maximum
distance (the fixture uses a 1,000-unit clip length). The separate strict
`1e-7 + 1e-7 * abs(reference)` comparison reports **7,668 failing components**;
strict numerical parity has not passed. These are retained in the evidence,
not excluded or hidden by a changed tolerance.

The twelve scheduler-control kernels and six package-audit tests also pass with
this expanded backend. Their rerun is saved alongside the camera evidence. The
control gate now uses the still-unimplemented intersection entry to verify
unsupported dispatch rejection at that checkpoint. This integrated camera gate has run only on
Linux x64/Intel Vulkan; it does not establish Android or Metal camera parity.

## Closest intersection and shader queues

`DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST` now reads the original native BVH2,
triangle, object, shader and path-state arrays. It traverses static triangles
and static native instances, including nonuniform and negative scales. Object
and primitive identities remain separate for self-hit rejection. Node and
primitive visibility checks preserve Cycles' transmission and reflection rules.
The triangle-intersection arithmetic is generated from the pinned upstream AST;
the BVH traversal and stage scheduling are WGSL adaptations of the original
functions. Camera and intersection share native state access and Sobol helpers.

The stage writes intersection records, makes Russian-roulette and AO bounce
termination decisions, and transitions paths to background, surface or raytrace
surface queues. Emissive hits preserve the original deferred-termination flags.
It writes the original GPU shader-sort keys and counters; the original scheduler
then consumes those counters using its prefix and sorted-path entries. Selecting
a raytrace shader queue does not implement AO/bevel material evaluation itself.
Likewise, the complete roulette path still needs shading's throughput handling.

The [closest-intersection report](evidence/closest-queue/linux-x64/validation.json)
records:

| Check | Result |
| --- | --- |
| Original scheduler camera → closest dispatch | Pass |
| Configurations / paths | 22 / 36,226 |
| Native instance leaves / distinct hit objects | 3 / 5 |
| Surface hits / misses | 4,107 / 32,119 |
| Terminated paths | 8,496 |
| Fractional continuation probabilities / emissive deferred termination | 1,700 / 390 |
| Raytrace surface queue selections | 568 |
| Original scheduler consumes surface and raytrace sort counters | 2 cases |
| Direct dispatch sizes, without an index array | 1, 63, 64, 65, 193 |
| Exact state/guard comparisons / float comparisons | 3,009,016 / 144,904 |
| Unsupported or invalid inputs | 16 rejected |
| Cycles allocation accounting after cleanup | Zero bytes |

The original CPU `integrator_intersect_closest` runs on the same scene and ray
inputs. For this stage comparison, both implementations receive the GPU camera
rays, which isolates intersection from camera rounding; the separate camera
regression still compares against the original CPU camera. The oracle enables
Cycles' optional local-struct initialization hook to make otherwise undefined
CPU miss fields deterministic. Original function bodies are unchanged. GPU-only
sorting keys/counters are checked against the original sorting formula, and
the original scheduler's sort routines verify their consumption.

The maximum intersection/continuation absolute error is `9.53674e-7`. The
existing `5e-5 + 2e-4 * abs(reference)` gate passes; the separately retained
`1e-7 + 1e-7 * abs(reference)` check has **370 failing components**. This does
not resolve the earlier strict numerical failures. All nonnumeric state,
queue counters, shader-sort counters and untouched guards must match exactly.
The camera and control regression gates retain their previous results.

Motion geometry, curves, points, volumes, shadow catchers/linking, MNEE,
non-tabulated samplers and forward light intersection remain explicitly
unsupported in this entry. It is not a complete Cycles render yet. Traversal
uses the upstream 192-entry stack and a 1,048,576-iteration failure guard; large
scene limits and performance remain work items. Invalid indices, roots,
geometry references and leaf types report errors. Only Linux x64/Intel Vulkan
has executed this integrated gate, using the unchanged package binary.

## Background entry and native film

The registered backend now runs `integrator_shade_background` for Cycles'
constant-emission world fast path. A native Cycles scene goes through the
original GPU scheduler's camera, closest-intersection and background entries
to the original film layout: 64 x 48 pixels at eight samples per pixel. This
world-only scene exercises the complete implemented sequence; surface shading
and full `Session` rendering remain unfinished.

The film port implements combined RGB/transparency, the background pass,
lightgroups, diffuse/glossy/transmission direct and indirect routing, light
clamping, AO background scaling, adaptive auxiliary accumulation and background
depth/position/object/material passes. Shader visibility exclusions follow
Cycles' path flags. Float accumulation uses integer compare-and-swap on the
existing storage arena, with no optional GPU features or runtime changes.
As in Cycles, combined alpha storage accumulates transparency; it is not final
display alpha. Original CPU `integrator_shade_background` supplies the oracle.

The Linux gate covers 26 cases and 55,681 paths, including noncontiguous queues,
partial workgroups and 4,096 paths accumulating to one pixel. All 3,940,352
state comparisons and 3,833,856 film comparisons pass the existing tolerance
`5e-5 + 2e-4 * abs(reference)`. An initial run had 195 stricter float failures
at `1e-7 + 1e-7 * abs(reference)`, with maximum absolute error 0.000244141;
atomic accumulation order can change those counts. This is not bitwise parity.
Twelve rejection cases leave the film and queue counters unchanged, and Cycles
allocations return to zero after cleanup. Controls, camera and closest-stage
regressions run alongside it.

The SVM extension below handles selected nonconstant world shaders.
Sun forward/MIS, background MIS, volumes, shadow catchers, denoising, path guiding,
light/shadow linking and the background motion pass are explicitly unsupported
in this entry. Later surface and opaque-shadow progress is recorded below; full
direct lighting and transparent shadows remain open. The earlier standalone snapshot renderer's material
support is not yet connected to this registered shading entry.

The root [license inventory](../THIRD-PARTY-NOTICES.txt) records the filtered
source, independent atomic replacement and current dependency audit scope.
These development results do not certify future native packages.

## Native SVM world shaders

The background entry interprets Cycles' original typed SVM bytecode in WGSL.
Native Cycles compiles each graph and uploads its `svm_nodes` table unchanged;
the backend derives node sizes, field offsets and enum values from the same
compiled C++ headers. The per-path stack stores 32-bit words, preserving integer
values whose bit patterns would be NaNs when viewed as floats. It checks stack
initialization, bytecode bounds and a 65,536-instruction limit. Unsupported
opcodes report an error with a diagnostic mask.

The current subset covers 25 opcodes: shader/conditional jumps and termination,
scalar/vector constants, geometry and selected world texture coordinates,
scalar/vector math, conversions, clamp, vector components, checker and gradient
textures, gamma/invert/brightness-contrast, closure weights, mixed closures,
and background/emission accumulation. Derivative variants, image textures and
the rest of SVM are unfinished. This interpreter currently sets up background
shader data only; it does not establish surface or volume shader support.

The Linux x64 gate renders 20 nonconstant graphs compiled by Cycles, each at
64 x 48 pixels and one sample, through the original camera → closest → background
scheduler. A further 161 typed bytecode programs cover all 41 scalar operations,
30 vector operations and eight conversion kinds, conditional jump outcomes,
zero divisors, signed/zero/unit directions and inverse-trigonometric endpoints.
The original CPU background/SVM code receives identical state and bytecode.
Across 102,817 paths, all 25,948,160 state checks and 2,224,128 film comparisons
pass the existing `5e-5 + 2e-4 * abs(reference)` tolerance. The initial successful
run recorded 7,483 stricter float differences and maximum absolute error
0.0000610352; this remains approximate numerical parity. Ten invalid programs
are rejected with the expected diagnostic bits, unchanged single-path film and
queue counters. That check does not guarantee rollback of a multi-path batch.
Cycles allocation accounting returns to zero after cleanup.

This gate exposed an arcsine difference beyond the accepted tolerance on the
Intel GPU. Original CodeBrix adapters now use a Taylor series on a reduced
interval and half-angle identities for arcsine/arccosine. The comparison
tolerance was retained, and endpoint/threshold cases were added. WGSL permits
implementation-dependent accuracy within its specified limits; see the
[WGSL floating-point accuracy rules](https://www.w3.org/TR/2026/CRD-WGSL-20260921/#floating-point-accuracy).
The change is entirely in our generated shader; the shipped WebGPU binary
remains unchanged. This integrated SVM gate has run on Linux x64/Intel Vulkan
only. Android, other GPU vendors, shader-stack cost and performance still need
validation. The previews are clipped linear PPM images, not color-managed output.

## Native surface shader-data preparation

`wavefront_shader_data.wgsl` implements the static-triangle portion of Cycles'
`shader_setup_from_ray`. It reads the original intersection state, vertex
indices/positions, object transforms, flags, shader IDs and packed normal table.
It reconstructs the hit position, geometric and smooth normals, incoming
direction, backface flags, parameter tangents, compact ray differentials and
UV differentials. Vertex and corner normals use the pinned revision's
octahedral 32-bit encoding. Signed position/normal offsets are preserved.
Motion/other primitive types and invalid references are rejected explicitly.

The Linux gate compares every defined output field with the original CPU
function, including actual camera → closest-intersection hits and controlled
rays across nine objects. It covers flat/smooth/corner normals, shared instances,
nonuniform and mirrored transforms, transforms baked into vertices, all three
differential projection axes, backfaces, triangle vertices/edges, dense and
sparse inputs, zero work and signed offsets. Across 11 cases and 12,066 hits,
84,462 integer checks, 337,848 float comparisons and 1,576,960 unchanged path-state
checks pass. All 5,324 output guard bytes remain unchanged. Twelve malformed or
unsupported inputs are rejected before writing a result. Cycles allocation
accounting returns to zero. There are 451 differences at the stricter float
tolerance; maximum absolute error is 0.000000476837.

A portable replay harness executes the same compiled WGSL and a captured
1,314-hit native buffer set using the unchanged package runtime on four devices:

| Device | System | Result at existing tolerance | Stricter float differences |
| --- | --- | --- | --- |
| Linux Intel UHD | Linux x64 / Vulkan | Pass | 49 |
| Pixel 4 XL, Adreno 640 | Android ARM64 / API 33 | Pass | 3 |
| Samsung SM-X700, Adreno 730 | Android ARM64 / API 36 | Pass | 3 |
| HP laptop, Intel Xe | Android x64 / API 33 | Pass | 49 |

Each replay checks 36,792 float values, 9,198 integers and all 4,661,736 bytes
outside the output range. The maximum float error on each device is
0.000000476837. The driver compiles for Android API 33, and deployed/loaded
WebGPU binary hashes match the package. This is GPU kernel portability
evidence: native scene preparation still runs on Linux. The full Cycles
Android host, complete surface SVM coverage and surface-bounce scheduler
entry remain unfinished. The probe calls reusable shader-data functions;
it does not register a substitute for `integrator_shade_surface`.

## Native surface SVM evaluation

`wavefront_svm.wgsl` now evaluates selected surface programs using the same
native bytecode as the world interpreter. The GPU first constructs shader data
from original scene and intersection tables. It then sets up Lambert, Oren-Nayar,
translucent and transparent closures, holdout, emission, mixed/additive graphs,
selected surface texture coordinates and explicit normal updates. CPU-prepared
closure inputs are not substituted for this evaluation.

The Linux gate executes the original CPU `shader_setup_from_ray` and
`surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE>` as its oracle. Twenty
Cycles-compiled graphs and fourteen typed test programs cover 8,874 evaluations
in 23 cases. All 603,432 integer checks, 7,463,034 float comparisons, 3,297,280
unchanged state checks and 2,944 guard bytes pass at the existing
`5e-5 + 2e-4 * abs(reference)` tolerance. There are 1,513 differences at the
stricter `1e-7 + 1e-7 * abs(reference)` tolerance; the maximum absolute error is
0.00000762939. These are tolerance comparisons, not bitwise float parity.

The cases include closure capacities 0, 1, 2 and 64; more than 64 requested
lobes; holdout flags after exhaustion; repeated transparent lobes with signed
weights; negative/tiny BSDF weights; termination, emission and shadow modes;
zero/explicit normals; dense/sparse indices and zero work. Roughness tests
straddle the Lambert/Oren-Nayar threshold and include values outside [0, 1].
RNG initialization reads the original packed 16-bit RNG offset. Undefined
holdout normal/sample-weight fields are omitted from the semantic comparison
and represented as zeros in the test record. All eleven unsupported/malformed
program or closure-storage checks report the expected error and leave output untouched. Cycles'
allocation accounting returns to zero.

The threshold tests exposed cancellation amplification in Oren-Nayar's
energy-compensation term. An original CodeBrix binary32 addition adapter
preserves separate rounding for its product sums using integer significands,
guard bits and ties-to-even rounding. Constants are explicitly binary32.
No CPU reference or comparison tolerance was changed, and no additional math
library or runtime modification was introduced.

The portable capture contains 1,020 evaluations of the twenty compiled graphs,
including near-threshold roughness. It passes on Linux x64, Pixel 4 XL ARM64
API 33, Samsung SM-X700 ARM64 API 36 and HP Android x64 API 33 using the unchanged
shipped runtime. Each replay compares 857,820 floats and 69,360 integers, verifies
all 3,655,680 closure scratch bytes against the output records, and checks that
12,018,384 bytes outside those two ranges remain unchanged. Maximum absolute
error is 0.00000762939 on every device. Strict float differences are 219 on Linux
and HP, 598 on Pixel and 627 on Samsung.

The initial function-local 64-closure array caused pipeline creation failures
on both ARM64 devices; Pixel's compiler reported register spilling. Closures
now occupy bounded per-path storage in the same GPU arena, with explicit bounds
and alignment checks, while retaining the 64-closure capacity. This change fixes
both drivers without changing WebGPU, weakening comparisons or reducing the
supported closure count. It also removes the large private result copy.
CPU tests verify the 64-lobe exhaustion case; this replay has mixed materials
with fewer live lobes, and is not a mobile 64-lobe stress benchmark.
The capture validates GPU evaluation from a Linux-prepared scene; it does not
establish Android native host support. This probe is not a registered surface
queue entry, BSDF sampling, a surface bounce, or a complete render. Image textures,
derivatives/bump, generated-attribute tangents, volume coordinates and other
closure families still require implementation; unsupported programs fail
explicitly. Production performance and shader compilation cost remain open.

## Native SVM texture, colour and utility nodes

`wavefront_svm_nodes.wgsl` translates the pinned `kernel/svm` node bodies for
32 further opcodes, sharing the surface and world interpreter:

| Family | Opcodes and modes |
| --- | --- |
| Procedural textures | `NODE_TEX_NOISE` (1-4D; fBM, multifractal, hybrid, ridged, hetero terrain; detail, roughness, lacunarity, offset, gain, distortion, normalize, colour), `NODE_TEX_VORONOI` (1-4D; F1, F2, smooth F1, distance to edge, n-sphere radius; Euclidean, Manhattan, Chebychev, Minkowski; fractal octaves, normalize), `NODE_TEX_WHITE_NOISE` (1-4D), `NODE_TEX_WAVE` (bands/rings, every direction and profile, distortion), `NODE_TEX_MAGIC` (depth 0-10), `NODE_TEX_BRICK` |
| Mapping and ranges | `NODE_MAPPING` (point, texture, vector, normal), `NODE_TEXTURE_MAPPING`, `NODE_MIN_MAX`, `NODE_MAP_RANGE` and `NODE_VECTOR_MAP_RANGE` (linear, stepped, smoothstep, smootherstep, clamp) |
| Colour | `NODE_MIX` (legacy), `NODE_MIX_COLOR` (all 20 blend types, factor/result clamps), `NODE_MIX_FLOAT`, `NODE_MIX_VECTOR`, `NODE_MIX_VECTOR_NON_UNIFORM`, `NODE_HSV`, `NODE_SEPARATE_COLOR`/`NODE_COMBINE_COLOR` (RGB, HSV, HSL), `NODE_RGB_RAMP`, `NODE_CURVES`, `NODE_FLOAT_CURVE` (inline tables, interpolation/extrapolation), `NODE_BLACKBODY` |
| Shading information | `NODE_FRESNEL`, `NODE_LAYER_WEIGHT`, `NODE_VECTOR_ROTATE`, `NODE_VECTOR_TRANSFORM` (world/object/camera, motion-aware), `NODE_NORMAL`, `NODE_OBJECT_INFO`, `NODE_CAMERA`, `NODE_SCENE_TIME`, `NODE_LIGHT_PATH` |

Each family is a scene-driven specialization region (`SVM_NODE_*` feature bits
35-52 in `pipelines.h`; bits 30-31 are reserved for the shadow-terminator lane; shared helpers sit in `SVM_NODES_*` regions kept when
any user is reachable). The census also walks ramp/curve tables. A scene that
uses none of these nodes compiles byte-identical modules to the previous tree;
its handlers and their layout/enum constants are removed textually.
`NODE_LIGHT_PATH` serves ray-type flags, backfacing and ray length in every
surface, background and shadow evaluation, and the bounce depths in the
surface and background stages (the host adds `PATH_RAY_EMISSION` there). Depths
in a shadow evaluation and every light-path output in volume evaluations
report unsupported (error 2), as do the deferred opcodes; invalid node
parameters report error 16 and truncated ramp tables error 4.

Exactness. The x86-64 oracle compiles the SSE4.2 branches of `noise.h` and
`util/math_float*.h`; the WGSL follows their association (vector fade, lane
order of Perlin interpolation, truncating float3/float4 `fmod`, exact scalar
`fmodf`, `dpps` dot order). Hashes are wrapping 32-bit integer Jenkins/PCG
code with a nearest-even integer-to-float conversion. Products that the host
rounds before an addition in noise, distortion, hash offsets and wave go
through `rounded_add`, so a driver cannot contract them into a fused
multiply-add: before this, high-octave and distorted noise differed by up to
1% on Intel. Hashes of float bits (white noise, 1D Voronoi) and high-octave
noise amplify a one-ulp input difference, so the SVM gate quantizes texture
coordinates to 1/64 (exact on both sides); the render variant keeps raw
positions with well-conditioned parameters. Magic texture at depth 9-10 with
distortion 1.6 exceeds the tolerance on a few paths (GPU `sin`/`cos` accuracy
amplified by the chain) and is gated at distortion 1.0. Cycles'
`make_float4(float3)` sets w to 1, so the 3D Voronoi W output is 1/scale.

`native/tools/nodes_gate.py` builds `codebrix_cycles_nodes_{svm,world,render}_gate`
(fixture `nodes_fixture.h`) and runs each with specialization on and off:

- SVM: 60 Cycles-compiled graphs and typed programs for every opcode and mode
  (394 cases, 29,715 evaluations, 2,020,620 integer and 51,614,955 float
  checks) against original `surface_shader_eval`; 10,985 strict differences,
  maximum absolute error 9.67e-5; 31 rejections (11 shared + 20 node cases).
- World: 20 compiled worlds (43 opcodes) through the original camera →
  closest → background scheduler, 2,224,128 film checks, 7,754 strict
  differences, maximum 6.10e-5.
- Render: independent CPU image, bitwise repeat and capture with 61 surface
  shaders, 144 stages, 146,933 paths; maximum stage error 7.73e-5 (0.51 of the
  practical bound), 4,208 strict image differences, maximum image error 1.75e-4.
- Specialized and unspecialized films and world images are bit-identical. At session close the
  node render and the worlds still are; the SVM probe is validated specialized only (open item 11).

Compile cost (Linux Intel UHD, cold): a render scene using every node family
compiles `prepare_surface` in about 25 s and `shade_background` in about 15 s
specialized. The unspecialized module grows: inlined `prepare_surface` 1.30 MB
→ 1.88 MB and `shade_background` 1.35 MB → 1.94 MB; unspecialized compiles take
92-102 s and 20-26 s (38 s and 5 s before). Glass, render and world scenes compile
byte-identical modules (the glass film is unchanged by the nodes) with unchanged compile times. Scene specialization, already required on
Android, keeps other scenes unchanged. Device results are in "Materials on the devices and scene-specialized
probe modules" below.


### Wavelength and wireframe (M3, 2026-10-20, Linux)

Two further opcodes run in the same interpreter, each in its own
specialization region after the reconciled table (`SVM_NODE_WAVELENGTH` bit 53,
`SVM_NODE_WIREFRAME` 54; bits 30-31 stay reserved for the shadow-terminator
lane, Principled uses 32-34 and the nodes above 35-52). Scenes without them
compile byte-identical modules.

- `NODE_WAVELENGTH` (`wavelength.h`, `svm_math_wavelength_color_xyz`): the
  pinned `cie_color_match` rows are switch constants (no private array and no
  new table upload); truncation, the extrapolated (-1,0) row range, the zero
  colour outside the table and NaN follow the host; `xyz_to_rgb` reads
  `film.xyz_to_r/g/b`.
- `NODE_WIREFRAME` (`wireframe.h`): triangle corners come from the existing
  vertex tables (motion triangles included) and the object or motion
  transform of the shading point; pixel size uses the compact `dP`
  differential; bump offsets (centre/dx/dy) are honoured. World and volume
  evaluations have no primitive and return 0, as on the host.

`native/tools/nodes_gate.py` now covers 63 compiled graphs, the typed
wavelength (11) and wireframe (12) programs, 32 rejections and 22 compiled
worlds. The render variant keeps the 60-triangle scene with the wavelength
graph in a visible slot and leaves wireframe out: wireframe is a step function
of the hit point, which the independent CPU image reproduces only to the last
bits (it is gated by the SVM gate on identical hit inputs and the world gate).

`NODE_TEX_GABOR` was ported and passed the node SVM, world and render gates,
but with its code present the unspecialized SVM probe module (which every SVM
gate compiles whole) made the Intel Mesa 25.0.7 driver mis-evaluate Principled
paths (SVM stack error 8, wrong closure values or a lost device in
`principled_svm`; a stubbed Gabor body passes). It was therefore not shipped.
Update (lane L, 2026-10-22): the cause is the Intel Mesa 25.0.7 16-bit
instruction/register limit, and Gabor now ships on the scene-specialized path
("Linux reference fixes on the updated Mesa"). `NODE_RADIAL_TILING` stays
deferred; AOV output needs a render-buffer path from the SVM evaluation.

### Bump stages and derivative variants (M4, 2026-10-20, Linux)

The bump node and the dual-number derivative variants run in the same
interpreter (`wavefront_svm_derivatives.wgsl`; mapping variants in
`wavefront_svm_nodes.wgsl`), each in its own specialization region after the
M3 table: `SVM_NODE_BUMP` bit 55, `SVM_NODE_BUMP_STATE` 56,
`SVM_NODE_GEOMETRY_DERIVATIVE` 57, `SVM_NODE_TEX_COORD_DERIVATIVE` 58,
`SVM_NODE_VALUE_DERIVATIVE` 59 (value f/v, conversion, separate/combine
vector), `SVM_NODE_VECTOR_MATH_DERIVATIVE` 60, `SVM_NODE_MAPPING_DERIVATIVE` 61;
`SVM_DERIVATIVE_ANY` holds the shared dual helpers. `SF_ALL=(1<<62)-1`; bits 62
and 63 are the last free bits of the 64-bit mask.

- `NODE_SET_BUMP` (`displace.h`): the surface normal is perturbed from the
  centre/dx/dy heights with `differential_from_compact(Ng, dP)` (or the saved
  full differential), strength, distance, invert and object space. Background
  evaluation stores zero, as the host background node mask has no bump feature.
- `NODE_ENTER/LEAVE_BUMP_EVAL` (`bump.h`) save and restore P and dP. The
  undisplaced-position branch needs true displacement and reports
  unsupported (16).
- Derivative variants keep (value, d/dx, d/dy) in adjacent stack slots and
  apply the first-order bump offsets. Geometry: P/N/Ng/I/uv (tangent reports
  16). Texture coordinates: object, normal (smooth `triangle_smooth_normal`
  with the du/dv taps; motion-triangle smooth normals report 16), reflection,
  camera, window, object-with-transform and dupli outputs; the plain and
  derivative opcodes share one `svm_texco` call site. Vector math: all 28
  operations with the `math_dual.h` product, quotient and chain rules.

`native/tools/nodes_gate.py` now covers 65 compiled graphs (graphs 63/64:
procedural bump from a geometry-position height and from an object-coordinate
height with linked normal, invert and object space), 112 new typed programs
(geometry 30, texture coordinate 32, value/conversion 3, vector math 28,
mapping 6, set bump 10, bump state 1) and 23 compiled worlds (world 22: bump
in a background). The render variant is unchanged.

Opcode coverage at session close, computed from the `case` labels of the merged
WGSL dispatch (plus `NODE_END`) against `compatibility/opcodes.json` (111
entries; `NODE_NONE`, `NODE_PAD1` and `NODE_NUM` are not opcodes): 89 of the
108 opcodes are implemented. Not implemented: set displacement, displacement,
vector displacement, volume coefficients, Principled volume, particle/hair/point
info, sky, Gabor (withheld), light falloff, IES, radial tiling, bevel, ambient
occlusion, raycast and the three AOV opcodes.

Intel Mesa 25.0.7: with these handlers present the whole unspecialized SVM
probe module compiles for about 9 minutes and then mis-evaluates unrelated
graphs (SVM error mask 24 on the noise graphs), the class seen with Gabor. The
SVM probe gates therefore use the scene-specialized probe module ("Materials on
the devices and scene-specialized probe modules" below): `nodes_svm` compiles
only the regions its uploaded programs reach, and `refresh_probe` rebuilds the
module when typed programs reach a further region. The specialized results equal
those of the earlier probe feature mask. Unspecialized modules were an open item
(see "Session close: specialization-off status" below); the superset mode of lane
L replaces them for the on/off check ("Linux reference fixes on the updated Mesa").

## Build and reproduce

Use the pinned source/dependency setup from the prototype instructions, then:

```sh
python3 native/tools/device_gate.py run
python3 native/tools/camera_queue_gate.py run
python3 native/tools/closest_queue_gate.py run
python3 native/tools/background_queue_gate.py run
python3 native/tools/svm_world_gate.py run
python3 native/tools/shader_data_gate.py run --android
python3 native/tools/svm_surface_gate.py run --android
python3 native/tests/test_webgpu_audit.py
```

The build uses a separate CMake directory, `native/work/cycles-build`. A
generated overlay changes three upstream files: the device enumeration,
factory/discovery functions and report name. The reference checkout remains
clean. The exact patch, generated files, compiler commands, source snapshots,
reference helpers, package audit and results are saved in
`evidence/device-queue`; `device_gate.py archive` creates this checkpoint once
and refuses to overwrite it. No native dependency binaries are archived.
The camera runner also rebuilds and reruns the control gate. Its separate
`camera_queue_gate.py archive` checkpoint is `evidence/camera-queue`, with source
hashes, original reference headers, compiler commands, runtime identity and
both test reports. It also refuses to overwrite an existing checkpoint.
`closest_queue_gate.py run` includes both previous gates, then the closest-stage
checks. Its `archive` command creates `evidence/closest-queue` once, including
the generated shader/translation evidence, original reference headers and all
three reports. The reference checkout remains unmodified.
`background_queue_gate.py run` includes those three gates before checking
background shading and native film. Its `archive` command creates
`evidence/background-queue` once, including the root provenance/license records,
generated shaders, reference headers and four sets of results. It saves hashes
of the native dependencies, not their binaries.
`svm_world_gate.py run` includes all four preceding gates and the six runtime
audit tests, then checks the SVM extension. Its `archive` command creates
`evidence/svm-world` once, with the original SVM headers, generated shaders,
native bytecode/previews, license records and all five test reports. Earlier
checkpoints remain unchanged.
`shader_data_gate.py run` includes those five gates, checks surface shader data
and runs a Linux buffer replay. Add `--android` to replay on the three connected
devices listed above. Its `archive` command creates `evidence/shader-data` once.
That checkpoint includes the captured arena, parameters and original CPU
results, plus each device's result records, source/runtime hashes and all six
native gate reports. The large input arena and replay records are compressed;
the manifest explains how to reconstruct and hash each complete output arena.
No executable or native dependency library is copied into the checkpoint.

The package remains `CodeBrix.WebGpu.Native.MitLicenseForever` 1.0.290.1433.
During this gate the sibling repository received an external documentation
change in `include/codebrix_wgpu_procs.rs`: an updated Rust inclusion example.
All nine native binaries and every C header still match the NuGet archive and
cache. The new builder explicitly permits only whole-line comment differences
in that unused Rust helper; it records both hashes and the exception. Package
cache bytes must still match the archive exactly. Rust code, C-header and
native-binary differences still reject the build; six audit tests verify these
boundaries. Earlier builders retain their strict all-files identity check.

## Native static-triangle attributes

`attribute_gate.py` extends native scene-table transport and the surface SVM
interpreter with named/standard attribute lookup, 64-bit identity, object-map
chains, signed offsets and checked table access. Object/mesh/face/vertex/corner
values include float/float2/float3/float4, packed normals and byte colors. Native
Rec709 conversion, UV seams, generated-coordinate fallback, interpolation,
attribute bump offsets and stored derivatives are compared with unmodified CPU
`shader_setup_from_ray` and `surface_shader_eval`.

The Linux gate has 48 Cycles-compiled graphs and 328 typed programs, 329 cases /
5,088 evaluations, seven rejected malformed/unsupported inputs, unchanged state
and output guards, and zero Cycles allocations after cleanup. Maximum absolute
error is 1.78814e-7; 17 component differences exceed the strict diagnostic bound.
The captured 48-graph workload has 1,152 evaluations; the derivative/rejection
programs currently execute only on Linux.

A complete native 64x48/8-spp UV-checker scene exercises 156 stages / 135,912
paths, five bounces and three transparent bounces. Its independent CPU film
passes the existing `5e-5 + 2e-4 * abs(reference)` criterion and the GPU repeat
is bitwise identical, with no shared-edge equivalence. Maximum absolute film
error is 0.000137329; 840 components exceed the strict diagnostic bound.
This is procedural shading through native UV attributes, not image sampling or
original glTF material fidelity. Existing synthetic/clay regressions also pass.

```bash
python3 native/tools/attribute_gate.py run
python3 native/tools/attribute_gate.py collect
python3 native/tools/attribute_gate.py archive
```

`run` includes the existing regression chain and replays both the native graph
capture and complete UV-render commands on Linux, Pixel, Samsung and HP Android.
Each replay checks runtime identity, original CPU output and immutable scene
bytes; the complete render also verifies every scheduler readback. `collect`
checks existing results without rerunning passing workloads. `archive` creates
`native/evidence/native-attributes` once and refuses to overwrite it. The first
capture invocations were recorded after direct execution while the collection
driver was being implemented; their invocation metadata states this explicitly.

Other geometry domains, matrix/volume consumers, production image coverage, general bump
state and full native Android hosts remain unfinished. Strict discrepancies are
retained, and no all-feature or all-RID completion claim is made.

## Resident native images

The resident image backend now stores native `device_image` pixels in the same
bounded GPU arena and publishes Cycles `KernelImageInfo` plus checked allocation
bounds. Logical texture/UDIM descriptors remain native Cycles tables. ImageManager
continues to prepare pixels; the WebGPU backend does not replace host decoding
or conformance. Queue initialization and image updates publish current descriptors,
and image frees remove their ownership records. Large scenes remain bounded by
the single storage-binding arena; sparse and tiled cache paths remain unfinished.

`image_gate.py` validates eight resident formats (float/half/byte/ushort, scalar
and RGBA), nearest/linear/cubic/Smart filters and repeat/extend/clip/mirror modes.
The native ImageManager fixture has 128 images/compiled graphs. Original CPU
sampling covers 115,650 cases, including boundaries, negative coordinates,
UDIM hits/misses, HDR, alpha unassociation and compressed-sRGB decoding. Fourteen
malformed-input cases check descriptor/range/format/coordinate errors, explicit
tiled-cache rejection and untouched output. Another 176 typed programs exercise
flat/sphere/tube/box image projections, both environment projections, color/alpha
outputs, and derivative opcodes. Resident images follow native non-mip sampling;
this is not proof of tiled footprints, stochastic mip selection or cache retries.

The initial Linux sampling comparison passes the unchanged absolute/relative
criterion. Its largest absolute difference is 0.328125 at CPU value 225665.84375
(after HDR/near-transparent unassociation and sRGB conversion); the largest
ratio to the allowed error across the corpus is 0.00727692016. Strict failures
remain recorded. The graph/typed corpus has 1,824 evaluations and retains its
strict discrepancies. The complete `--images` native render compares independent
CPU film and exact GPU repetition. `image_gate.py run` includes the complete
attribute/prior-render regression chain and four-device sampler, graph and full
render replay; `collect` rechecks saved evidence; `archive` refuses to overwrite
`native/evidence/native-images`.

All four devices pass the 115,650-case sampler and 768-evaluation native graph
captures. The typed projection/derivative and malformed-input programs currently
execute on Linux only. The complete native image render has 155 stages and
136,029 path invocations, with bitwise-identical Linux GPU repetition and no
shared-edge equivalence. Its recorded loop passes on Linux x64, Pixel API 33,
Samsung API 36 and HP Android x64 API 33: 890 commands, 377 dispatches and 342
exact scheduling readbacks per replay. The maximum independent CPU-film error is
3.67164612e-5 on Linux/HP and 3.05175781e-5 on Pixel/Samsung. Strict image failures
remain (2,202 / 2,205 / 1,844 / 1,850, respectively). All scene bytes remain
unchanged and every path terminates. Earlier attribute, selected-material and
clay-bench regressions also pass. The [image checkpoint](evidence/native-images/validation.json)
contains these results and fresh earlier regressions; historical checkpoints are
preserved. Android results establish GPU command replay, with native Android
Cycles hosts still pending.

```bash
python3 native/tools/image_gate.py run
python3 native/tools/image_gate.py collect
python3 native/tools/image_gate.py archive
```

These initial fixtures use generated input pixels through native ImageManager.
The next section extends validation to encoded file inputs and replacements.
Production OCIO configurations, additional file/animation/UDIM coverage, broader
edit and resize stress, sparse 3D images, tiled cache residency, original
ForestCommunity materials, general closures and native Android hosts remain open.

## File-image and replacement validation

`native/tools/image_file_gate.py` exercises the existing native ImageManager and
reviewed OIIO/OCIO build with eighteen encoded PNG, JPEG and OpenEXR fixtures.
One through four channels, byte/ushort/half/float precision, five alpha modes and
five color selections produce 450 parameter variants. Its original OCIO matrix
configuration is validated, serialized and loaded from disk. Known source pixels
also verify lossless decoding, channel expansion, image orientation and the
matrix conversion independently of the CPU sampler; JPEG remains a lossy-codec
transport comparison, with no exact pre-encoding pixel claim.

Sixteen native replacement cycles change dimensions and precision while an old
image read is queued. Each verifies the old result, the newly loaded pixels,
native handle deduplication, logical descriptor reuse, animation-frame update
signals and unchanged unrelated image storage. This tests replacement by native
handle release/re-add, not a completed host application edit API. Both `<UDIM>`
and `<UVTILE>` filename patterns build native three-tile mappings. Missing and
corrupt inputs retain the original missing-image behavior. Cleanup leaves zero
Cycles allocation bytes and no image owners or image-info/bounds tables.

The Linux gate checks 154,931 samples, including 34,483 known lossless texels
(maximum error 5.96046448e-8). The final 116,352-case snapshot is the four-device
GPU replay corpus, now passed on Linux, Pixel API 33, Samsung API 36 and HP Android
x64 API 33. Actual file decoding, OCIO setup, replacement/lifetime and
known-texel assertions currently execute on Linux only. Recorded diagnostics
separate very large HDR values after near-zero-alpha unassociation/sRGB conversion
from ordinary-color errors; the same absolute/relative tolerances apply.

The TIFF plugin remains deliberately excluded by the reviewed dependency profile;
this gate records that gap rather than enabling excluded implementation code.
A compatible TIFF/LZW path and other unavailable format paths remain required.
No dependency revision or imported implementation was added for this work.
Production OCIO configurations, cache/mip residency, broader animation/lifetime
stress, original material renders and all native platform hosts remain open.

```bash
python3 native/tools/image_file_gate.py run
python3 native/tools/image_file_gate.py collect
python3 native/tools/image_file_gate.py archive
```

`archive` creates `native/evidence/native-image-files` once, preserving file
fixtures, the OCIO configuration, captures, replay arenas and test sources. Prior
native-image rendering evidence remains separately frozen; the collector checks
identity of its native implementation and four earlier native executables for
that historical checkpoint. After renderer edits, the normal gate supplies a
fresh image/attribute/render regression chain with matching source hashes.
For separate collection after that fresh regression, use
`python3 native/tools/image_file_gate.py collect --regression native/work/images/validation.json`.
Without that option, the standalone file gate retains its original strict
renderer-identity requirement and rejects changed renderer binaries.
The [file-image checkpoint](evidence/native-image-files/validation.json) records
the results and explicit proof limits.

## Native normal maps and tangents

`python3 native/tools/normal_gate.py run` checks native static-triangle
normal-map spaces (tangent, object, world and Blender object/world), both green
channel conventions, original/displaced base normals, strengths below zero and
above one, missing attributes, zero colors and out-of-range finite colors.
Native Mesh tangent generation supplies standard/named UV frames. Shared mesh
instances, nonuniform/mirrored transforms, front/backfaces, flat/smooth/corner
normals and separate undisplaced normals feed the original CPU SVM comparison.
Radial/UV tangents include analytic dx/dy and FLOAT2/FLOAT3 attribute consumers.

The Linux corpus has 48 compiled graphs, 398 typed programs and 10,704 surface
SVM evaluations. Ten malformed/degenerate cases must set an error without writing
an output record. A degenerate plain tangent produces nonfinite values in the
original implementation; this backend explicitly rejects it. The derivative
zero-frame semantics are separate. Full bump state, motion and other geometry
remain open. No original asset material-fidelity claim is made.

The gate also runs a complete 64x48, 8-sample native image-normal-mapped render,
checks independent CPU film, repeats the GPU film bitwise, and replays the graphs,
selected nonzero tangent derivatives and full recorded render on four GPUs.
It reruns previous image/attribute/render gates and file-image checks following
renderer edits. `collect` rechecks existing results; `archive` freezes
`native/evidence/native-normals` once. Android replay validates the recorded GPU
work; native Android Cycles hosts remain pending. Strict diagnostics are retained
alongside the unchanged 5e-5 + 2e-4*abs(reference) acceptance criterion.

## GGX reflection work in progress

The current GGX reflection changes are not a frozen rendering checkpoint. The
native setup corpus passes 167 cases and 16 malformed-input rejections. The
earlier full native render stopped on a sharp-GGX PDF comparison. At the retained
stage-6 input, a one-step difference between host `cosf` and correctly rounded
binary32 cosine changes the sampled half-vector enough to exceed the unchanged
PDF tolerance. The original CPU reference and acceptance limits remain intact.
The corrected bounded adapter now uses the MIT option of Arm Optimized Routines
v25.01, pinned at 3752b981a6f3b89b5468476a87568a29fd9e0822. The four original
source/data files and selected MIT grant are retained under
`third_party/arm-optimized-routines`, with hashes/license basis in
`third_party/arm-math-import.json`. Original CodeBrix two-component arithmetic
evaluates the polynomial in WGSL without requiring binary64. This adds no linked
math library. `tools/arm_math_source.py` verifies the subset before native builds
and source auditing; root and native notices retain Arm attribution.

Its development backend passes the unchanged full native gate (149 stages /
140,006 paths, independent CPU film and bitwise repeat) and all 34,838 scalar
checks on four GPUs. The integrated build still requires fresh native/device and
broader regression validation. `native/tools/native_trig_gate.py` supplies a
larger corpus with independent platform `sinf`/`cosf` calls. The original Taylor
adapter and its rounded-binary64 gate remain separate.

The composed surface shader also exposed execution-layout sensitivity on the
tested Linux Intel GPU: size 64 reproduces incorrect packed state and ray
directions. Its size-1 variant passes Linux and Samsung, but HP aborts in the
vendor compiler and Pixel exceeds the 600-second test timeout. The current path
therefore separates preparation/closure evaluation (size 64) from sampling and
state updates (size 1), with a 208-byte GPU-private record per invocation. The
ready word is cleared before validation and set only after the complete record
is written. The native scheduler retains one surface operation and its existing
queue transitions; no CPU readback connects these two GPU dispatches.

The captured split, including the actual native-generated module, passes all
77,824 integer checks, 110,592 float comparisons, film, queue and guard checks on
Linux, Pixel, Samsung and HP with unchanged package binaries. Strict differences
remain reported. The current native GGX bounce gate passes 22 valid cases /
21,420 paths and 21 invalid-input checks. The generic bounce gate passes 18 valid
cases / 17,340 paths and the same 21 rejections. These results predate the newly
integrated native trig adapter. Broader regression
and performance assessment remain open. A retained branchless arithmetic variant
fails Linux, so it is not the production default. Working diagnostics are under
`native/work/microfacet`; older immutable evidence describes its own source pins.

Global closure capacity now requires 1..MAX_CLOSURE, matching native Scene's
minimum of one. The previously injected global-zero positive case can leave the
CPU with a BSDF flag but no allocated closure, making sampling undefined. All
1,020 original paths are retained as a rejection case, with the same flags,
weights, film allocation and queue counts; every path field, film value and
counter must remain unchanged. Minimum-capacity-one and local zero-storage SVM
evaluation remain covered. This reclassification leaves historical evidence
intact and does not change numerical acceptance.

A complete image diagnostic records exactly one practical state-float mismatch
(the PDF above), zero integer-state mismatches and zero CPU/GPU queue-count
differences over 149 stages / 140,006 path invocations. Its independent CPU film
passes the existing tolerance, with maximum error 1.90735e-6 and 173 strict
differences; repeated GPU film is bitwise identical. It explicitly reports
`diagnosticOnly=true` and `stageGatePassed=false`. Four-device replay of this fixed
capture checks scheduling and film; it cannot establish the failed stage gate.
Single-kernel replay now allows 600 seconds by default, matching full-render
replay, after Pixel exceeded the previous 180-second compilation/execution limit.

Dispatch sizes are explicit in native recording and replay. Command-stream v2
adds kind 5: the existing three-word header contains kind, entry and invocation
count, followed by a workgroup-size word and the 128-byte parameter block. Kind 3
retains its original implicit size 64, so v1 captures remain replayable. Entry 5
selects `prepare_surface` from `surface.wgsl`; entry 4 selects `shade_surface`.
Tests
cover an actual size-1 surface dispatch, old and converted full-render captures,
invalid sizes/grid counts, truncated records and unknown command kinds. Expected
scheduling readbacks remain assertions and are never injected into GPU state.

## Next integration

The earlier bounded surface-bounce and complete scheduler-loop checkpoints pass
for their recorded sources and fixtures; GGX reflection has since passed bounded
native-render regressions. Point/sun NEE and shadow-path creation now have bounded
native-render coverage below. Next are transparent shadows, remaining emitters,
forward/MIS and broader rendering features.
Use the existing Linux and Android hosts for relevant feature regressions.
Jeremy's 2026-10-19 priority defers additional platform ports and release packaging
until rendering functionality is complete, unless a concrete feature needs them.
The existing WGSL rendering functions provide tested behavior to adapt, but
the existing complete-path shader must not be disguised as those individual
kernels. Queue transitions, counters, shadow-path creation, film writes and
compaction need end-to-end comparison through the original scheduler.

Full session rendering, texture bindings, other GPU platforms, large scenes,
shared-device handoff and final NuGet/RID packaging remain unfinished. All
unimplemented kernel IDs currently produce a named error rather than a
fallback render. Full rendering parity remains the objective.

`svm_surface_gate.py run --android` includes the prior six native gates and the
surface evaluation comparisons, then replays the compiled material capture on
Linux and the three connected Android devices. Its `archive` command creates
`evidence/svm-surface` once, preserving capture/replay data, input hashes,
reference sources and prior-gate reports. It never overwrites checkpoints or
archives a complete upstream checkout or native dependency binaries. All such
evidence remains subject to the release review in `../docs/RELEASE-LICENSE-PLAN.md`.

## Registered opaque shadow stages (2026-10-19)

The registered queue now implements `INTEGRATOR_INTERSECT_SHADOW` and
`INTEGRATOR_SHADE_SHADOW` for opaque static triangles and native instances.
Originating and light primitives are excluded by their native object/primitive
identities. The stages preserve native shadow-state and queue-counter semantics
and write combined, adaptive auxiliary, light-group and surface direct/indirect
film passes. Transparent shadows, volumes, AO, shadow linking and shadow catchers
remain explicit errors. This initial shadow-stage checkpoint preceded the
point/sun light-sampling and shadow-state integration recorded below; stage tests
alone do not prove a complete direct-light render.

A boundary test exposed ordinary WGSL division rounding an exact depth of 4 to
3.999999761581421. The existing binary32 rounding helper now computes BVH inverse
directions and generated triangle depth division. This preserves ray bounds;
the original failed decision and independent scalar probes are retained.

Run the shadow stage and affected traversal/GGX regressions with a new output
folder:

```bash
python3 native/tools/shadow_gate.py --output native/work/shadow-check-local
```

The initial successful run is `native/work/shadow-stages/linux-v4/validation`.
It passes 60 cases/3,846 paths against unmodified original CPU shadow kernels,
with 1,204,320 exact state checks, 1,111,680 film-component checks and 18 invalid
case rejections. Shadow film comparisons have zero strict differences, maximum
absolute error 5.960464477539063e-8. Existing closest-intersection and GGX full
render regressions also pass; closest-stage strict floating diagnostics remain
reported separately.

`native/work/shadow-stages/android-v1/validation` records the same native test
on Pixel ARM64, Samsung ARM64 and HP Android x64: all pass with the same coverage
and film-error results through unchanged shipped WebGPU binaries. These isolated
feature builds reuse the existing reviewed Android dependency prefixes. Payload,
compiler/header/archive and ELF reviews pass; no new platform port or release
package is implied. Full direct-light and transparent-shadow integration remains
required.

The recorder/replayer now includes shadow intersection and shading as pipeline
IDs 6 and 7. Captures using these entries have format `native-command-stream-v3`
and include `shadow.wgsl`; existing v1/v2 captures retain their original pipeline
IDs and shader requirements. Capture metadata records film stride, workload and
shadow queue bounds. Validation checks all shadow paths terminate and compares
every film pass against the original CPU reference. A shadow-stage replay is
reported separately from a full render. `shadow_gate.py` now records and replays
one direct and one indexed shadow batch on Linux as part of its acceptance run;
both pass in `native/work/shadow-capture/linux-v1`, alongside the original shadow,
closest-intersection and GGX render gates. Each replay verifies four scheduling
readbacks, all 4,096 shadow slots terminated and 9,264 film components, with zero
strict film differences.

`native/work/shadow-capture/compatibility-v2` passes nine additional replays:
historical v1/v2 and the current render capture on Linux, plus both shadow batches
on Pixel, Samsung and HP Android. The shadow batches have zero strict film
differences on all devices, maximum absolute error 5.960464477539063e-8.
Historical full-render strict diagnostics remain reported (1,230 v1 and 173 v2);
their practical image criteria pass unchanged. Altering an expected scheduling
readback is rejected, as is a nonterminal shadow slot. Source, binary, runtime and
capture input hashes remained unchanged throughout validation. These Android
runs are command replay; the separate native-host shadow proof above remains the
native execution evidence. An initial work-runner import-path error is retained
with its original source/log; the corrected compatibility run passes.

## Native point/sun next-event estimation (2026-10-19)

The native surface stage now samples the original light CDF and point/sun records,
evaluates supported diffuse/translucent/GGX mixtures, applies light termination,
and allocates shadow paths through the original native scheduler. Point lights
support zero radius, disk and sphere geometry; sun lights support finite cones.
Constant emission, cast-shadow switches, bounce limits and existing shadow film
passes use native semantics. CPU table copies only validate capabilities; GPU
kernels perform the rendering work.

Full forward/MIS, light trees, triangle/area/spot emitters, nonconstant light
shaders and transparent shadows remain explicitly unsupported (smooth
shadow-terminator offsets were added in Phase E3; see below). These remain required work. The direct-light contribution
is generated by native scene preparation and WGSL kernels without changing the
shipped WebGPU library.

```bash
python3 native/tools/lighting_gate.py --output native/work/lighting-check-local
```

`native/work/direct-light/linux-v4` passes eight 32x24, 8-sample scenes against
unmodified original CPU integrator/shadow kernels. The test exercises 29,527 CPU
reference shadow paths (2,707 blocked, 26,820 visible) and 49,152 film comparisons,
with maximum absolute error 8.344650268554688e-6. All practical comparisons pass;
5,616 strict differences remain reported. Seven scenes repeat bitwise; the
light-termination scene has six differing repeated GPU components, maximum
1.1920928955078125e-7. This is not a determinism fix or a strict-parity claim.

All eight complete native command streams replay successfully on Linux, including
shadow allocator readbacks and terminal main/shadow queues. Existing shadow,
closest, surface, background and GGX render regressions pass; their strict
numerical diagnostics remain intact. Earlier failed fixture/reference-wrapper
runs and the test scheduler overload mistake are retained in linux-v1/v2/v3.
The corrected test uses the full native scheduling method, including shadow
compaction and allocation bookkeeping.

Actual native tests on Samsung ARM64 API36 and HP Android x64 API33 pass all eight
scenes and 49,152 film comparisons per device, with byte-identical generated WGSL.
Samsung reports maximum image error 6.020069122314453e-6 and 2,342 strict
differences; HP reports 8.344650268554688e-6 and 5,584 strict differences. Both
exercise the same 29,527 reference shadow paths as Linux and leak no native
allocations. The light-termination scene repeats with five differing components
on Samsung (maximum 2.384185791015625e-7) and eight on HP (1.1920928955078125e-7).
Other scenes repeat bitwise.

Pixel ARM64 API33 timed out after 900 seconds in its first GPU scene, after the
CPU reference film completed. The Android coordinator is retained as failed in
`native/work/direct-light/android-v1`; this feature is not yet validated on Pixel.
Separate post-timeout review confirms unchanged source/binary/runtime pins.
The process is absent and its CPU film is retained. Pipeline timing diagnosis is
underway; no numerical acceptance criteria were loosened. The first diagnostic
attempt rejected a stale existing replay binary before device execution; the
next diagnostic builds an isolated current harness. These are functionality
tests on existing RIDs, with no new platform or release-package claim.

## Native perspective camera motion

The camera entry now reads Cycles' native decomposed camera-motion array and
shutter inverse-CDF table. WGSL performs translation/rotation/scale interpolation,
perspective FOV motion and top-to-bottom rolling shutter, including durations
zero and one. Native scene preparation still computes the decomposed transforms
and shutter table. Existing object/deformation-motion and curve guards remain;
this is camera motion against static triangle geometry, not complete motion blur.

`python3 native/tools/camera_motion_gate.py --output NEW_DIRECTORY` builds and
runs the original CPU camera/path-state oracle, malformed-configuration checks,
a stationary image control and eight independent CPU/GPU moving-camera images.
The images exercise multiple bounces and transparent/diffuse/GGX materials. The
runner requires both CPU and GPU moving images to differ measurably from the
stationary control, records unchanged source/binary/runtime hashes, and retains
strict diagnostics alongside the established practical tolerances. Camera state
cases include two/three/five transform steps, small/large rotations, scale/shear,
nonuniform shutter curves, FOV changes, rolling shutter and indexed retries.

The first rotating-image attempt was correctly rejected by the independent film
comparison even though camera-state and per-stage tolerances passed. The camera
port now uses paired binary32 quaternion dot/normalization and an original
bounded Taylor acos adapter with the existing precise sin/cos helper. The failed
attempt remains in `native/work/camera-motion-v1`; subsequent raw films are saved
before image comparisons so a failed comparison cannot discard its evidence.

Linux development evidence is recorded under `native/work/camera-motion-v2`.
No Android camera-motion execution or complete platform coverage is claimed by
this lane. Depth of field, other camera models, object/deformation motion, hair,
subdivision and motion-vector passes require their own implementations and tests.

## Parallel functionality integration (2026-10-19)

The main source now includes three separately validated Linux slices: registered
point/sun forward lighting and MIS; single-scatter GGX glass/refraction; and native
perspective camera motion. Their isolated gate results and sealed source deltas
are recorded in the sibling `.parallel/{lighting,materials,geometry}` worktrees.
The primary agent has merged their shared scene layouts, canonical closure records,
shader assembly and recorder entries. Main-source regression and combined-scene
validation passes in `native/work/parallel-integration/validation-v1`: the full
point/sun MIS and no-MIS corpus, glass/GGX state and image checks, camera-motion
corpus and 24 complete command replays. The eight combined scenes check 49,152
film components, with maximum absolute error 3.337860107421875e-5; 5,769 strict
differences and five differing repeated-film components remain recorded.

`native/tools/feature_combination_gate.py` exercises all three slices in eight
native scenes with independent original CPU films and full command replay.
`lighting_mis_gate.py`, `glass_gate.py` and `camera_motion_gate.py` retain their
independent feature corpora and existing regression checks. Strict diagnostics
and repeated-film differences are reported under unchanged comparison criteria.

Forward sphere/disk lamp hits, finite suns and their NEE/forward MIS weights now
have integrated Linux evidence. Broader emitters, light trees, environment MIS,
nonconstant emitter shaders and transparent shadows remain required. Glass covers
single-scatter GGX reflection/transmission, backfacing IOR, TIR, mixtures and pass
classification; thin film, multi-GGX glass and full Principled remain required.
Camera motion is limited to perspective pinhole cameras and static triangles.
Focused native Android builds pass for ARM64/x64 using the existing toolchains.
Static and moving-camera tests pass on Pixel, Samsung and HP. Forward-light state
checks pass on Samsung and HP. Glass remains unresolved: HP aborts during shader
compilation with Scudo allocation exhaustion; Samsung fails a near-singular
refraction PDF comparison. Neither reaches the combined-image corpus. The failed
coordinator, per-device outputs and unchanged post-failure source/binary checks are
retained in `native/work/parallel-integration/android-v1/validation`.

Every WebGPU test uses the unchanged runtime from
`CodeBrix.WebGpu.Native.MitLicenseForever` 1.0.290.1433. Staged hashes are checked
against the package, and successful native runs verify the loaded library path.
Copied-shader SVM-storage diagnostics also timed out on Pixel and HP; they have
not been promoted to production or counted as passed features.

## Spot and area lighting extension (2026-10-19, Linux main validation passed)

Registered NEE, lamp intersections and forward MIS now include constant spot
lights and rectangle/ellipse/disk area lights with native shape/spread sampling.
The sealed lighting lane passed 4,096 independent native sampler probes, six
invalid-input checks, 53 broad-light state cases, 12 independent CPU/GPU images,
point/sun and combined glass/camera regressions, and 20 complete command replays.
Its first rectangle-image mismatch was fixed by preserving native float operation
order and bounded trigonometric evaluation; failed attempts remain retained.

The source delta is merged into main with reviewed in-repository derivations and
attribution. `python3 native/tools/broad_lighting_gate.py --output NEW_DIRECTORY`
builds and executes the maintained corpus. Main validation passes all 26 reports
in `native/work/parallel-integration/lighting-round2-v1`, with source, executable
and runtime pins unchanged. The 12 new scenes exercise 2,141 forward-light paths
and 27,285 shadow paths; all 73,728 film comparisons pass the practical criterion.
Maximum absolute image error is 1.010894775390625e-4; 6,253 strict differences
and 13 repeated-film component differences remain recorded. All native
allocations are released. Build-input review covers 263 compilation entries,
1,296 compiler-observed headers, 11 static archives and six gate executables;
the source-license audit passes 25,610 files with zero errors.

No Android spot/area claim is made yet. Mesh/environment emitters,
textured light shaders, light trees and degenerate area dimensions remain
guarded. Transparent shadows now pass the integrated corpus below. No WebGPU
runtime or numerical tolerance was changed.


## Integrated handoffs and current limits (2026-10-19)

The working tree combines the sealed transparent-shadow, deforming-triangle and
finite homogeneous-volume slices. The combined Linux regression is PASS in
`native/work/parallel-integration/all-lanes-validation-v2`: 29 targets built,
78 independent CPU/GPU scene comparisons and 45 exact command replays with
22,789 scheduling readbacks. It includes the new feature corpora and shared
world-volume, glass/GGX, camera-motion and broader-lighting regressions. All
production source, executable and package-runtime hashes stayed unchanged during
testing. Strict differences and repeated-film differences remain recorded.

The first combined run crashed in Intel's Vulkan compiler while creating the
transparent-shadow pipeline. Reducing that shading workgroup from 64 to 1,
with matching dispatch sizing, passes the unchanged corpus. Opaque traversal
retains 64; the original crash/backtraces are preserved in `all-lanes-crash-v1`.
Large-scene performance and wider driver coverage remain unmeasured.

- Transparent/colored shadows support static triangles and bounded hit storage
  with resumed traversal. Moving transparent shadows, volume shadow attenuation,
  light/shadow linking, AO, MNEE and shadow catchers remain guarded. The stable
  GPU queue gather scans path capacity serially; large-scene cost is unmeasured.
- Deforming triangles use native motion frames, interpolated vertex/corner
  normals, instance transforms and opaque-shadow traversal. Curves, points,
  subdivision, animated emitters and transparent-shadow/volume combinations
  remain unsupported or unvalidated; their guards are retained.
- Finite homogeneous volumes include native GPU density preparation, camera-
  inside stack initialization, entry/exit and overlapping HG media. Volume
  NEE/MIS, emission/light passes, shadow attenuation, heterogeneous media and
  moving scenes remain guarded. Mixed-anisotropy duplicate compaction is
  rejected because the independent native CPU closure assignment loses HG data
  in padding; the original failure is preserved and the oracle is unchanged.
- These handoffs now have combined Linux evidence. Their new Android paths
  remain unvalidated. Full Cycles lighting/MIS is not complete: mesh/environment
  sampling and MIS, light trees, textured emission and remaining interactions
  still require implementation and proof.

All tests continue to use the unchanged CodeBrix.WebGpu.Native.MitLicenseForever
1.0.290.1433 binaries, independent original CPU references and unchanged practical
and strict diagnostic criteria. Recorder v5 uses slots 9 (ShaderData), 10 (volume
stack), 11 (volume transport), 12 (transparent shadows), 13 (surface lighting)
and 14 (homogeneous density); earlier capture formats remain supported.


## Wrap-up: triangle-light candidate deferred (2026-10-19)

All completed sub-agent handoffs above remain integrated and uncommitted.
A subsequent primary-agent triangle-light/MIS candidate passes focused sampler,
surface and four static-image/replay checks, but its final transparent-lighting
regression crashes. The candidate and failure evidence are preserved under
`native/work/mesh-light-v1/deferred-candidate`; it is excluded from production.
The validated pre-extension source was restored by exact hash, retaining the
original triangle-light guards and recorder v5. Restoration and follow-up results
are recorded under `native/work/mesh-light-v1` and in section 44 of Jeremy's plan.
Before reintegration, resolve the transparent-lighting regression and the retained
instanced-emitter image mismatch, then validate moving emitters and broader
interactions. Environment/textured emission, light trees and remaining full MIS
work are unfinished. No percentage increase or new Android coverage is claimed.
Superseded: the transparent-lighting crash was diagnosed and the candidate was
reintegrated; see the integration section below.

## Compiler pressure: scene specialization, light-stage split and pipeline tiers (2026-10-19)

Vulkan drivers inline every WGSL function into each entry point. The generic SVM
interpreter therefore gave the driver one function of about 2.3 MB of inlined
WGSL for `prepare_surface`, `shade_background`, `shade_volume` and the
transparent-shadow stage. The HP Intel compiler (Mesa 24.0.8) crashed or exhausted
Scudo's 256 MiB size-class regions on these entries, and the Pixel 4 XL Adreno
compiler needed minutes (509 s for `prepare_surface`).

- **Scene-driven specialization** (`native/cycles/pipelines.{h,cpp}`). Optional
  handlers are comment regions (`//#if FEATURE else FALLBACK` ... `//#endif`); the
  unspecialized module keeps every handler. Before creating a pipeline the device
  decodes the uploaded `svm_nodes` exactly as the WGSL interpreter does (shader
  headers, shader and conditional jumps, node, BSDF-data and texture-transform
  record sizes) and keeps only reachable SVM node handlers and closure setups,
  plus area-light and object/camera-motion code when the uploaded lights, object
  flags or camera use them. A removed handler's fallback reports error bit 2; it
  never produces substitute radiance. Enabled features only grow during a device
  lifetime, so a scene update reaching a new handler recompiles the affected
  pipelines. `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0` compiles every handler.
  Captures record the module the device actually compiles.
- **Direct-lighting split.** Light sampling and BSDF/MIS/shadow connection run
  in separate pipelines (`shade_surface_light`, `connect_surface_light`); the
  sample is stored bit-exactly in the GPU-private stage record. Recorder v6 adds
  pipeline entry 15; earlier formats replay unchanged. The integrated partition
  is described in the next section.
- **Call-site consolidation** (unchanged arithmetic and order): one image call for
  all image node layouts, one `attribute_fetch` call in `attribute_read`, one
  `svm_image_sample` in box projection.
- **Tiers and reporting.** Each pipeline creation is timed against a declared
  budget (60 s default, `CODEBRIX_CYCLES_PIPELINE_BUDGET_MS`); an overrun marks the
  session degraded with a warning but never changes the variant. A non-fatal
  creation failure retries once in the conservative tier (workgroup size 1, which
  can change last bits on Linux Intel but must meet the practical criterion);
  device loss and driver crashes are reported with the stage name (a scoped signal
  breadcrumb names the stage if the driver terminates the process). Gate and
  replay JSON carry `pipelineReport` (tier, degraded, warnings, compile times).
  The shipped runtime exposes no pipeline-cache API; Linux Mesa's disk cache is
  the only warm path observed (41 s cold, 8 ms warm); HP and Pixel shell
  processes recompile every time.
- Measurements and the original HP/Pixel validations are in the B3 handoff; the
  integrated tree's Linux validation is described in the next section and its
  Android device results in the two sections after it.

## Integrated lighting, specialization and ARM adapters (2026-10-19)

The working tree now combines three further slices on Linux x64 with the
unchanged CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433 runtime. Linux
validation ran every corpus with a fresh, empty Mesa shader cache, so driver
compile failures cannot be hidden by the disk cache; outputs are in
`native/work/phaseC/` and the frozen checkpoint
`native/evidence/native-integration-20261019/`.

- **Scene-driven specialization and pipeline tiers** (`native/cycles/pipelines.{h,cpp}`)
  as described in the previous section, now also covering emissive triangles
  (`LIGHT_TRIANGLE`: any triangle in the light distribution or any shader flagged
  as a MIS emitter) and the triangle-light private object-motion code
  (`MOTION_OBJECT`). Uploading the light distribution counts as a scene change.
  `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0` still compiles every handler. Gate
  probes that compile their own copy of the surface module specialize it the
  same way.
- **Emissive triangle lights.** Flat-CDF constant emission with native
  solid-angle/area sampling, centre-area selection, sidedness and forward-hit
  MIS (`wavefront_triangle_light.wgsl`), including moving and deforming emitters.
  The triangle sampler keeps private copies of the motion arithmetic it needs; the
  shared motion shader is unchanged. Static instanced emitters remain rejected:
  the GPU matches the CPU at identical inputs, but closest-hit barycentrics differ
  by 1-3 ULP (driver FMA contraction and the oracle's SSE reciprocal), which flips
  edge-grazing samples. Light trees, environment and textured/nonconstant
  emission remain unimplemented.
- **Direct-lighting split.** `shade_surface_light` samples the lamp or emissive
  triangle and the forward-hit triangle MIS weight; `connect_surface_light`
  applies holdout, emission, roulette and the sampled light's BSDF/MIS/shadow
  setup. The GPU-private surface record carries the weight and the 13-word light
  sample. (An alternative partition that sampled lights after emission and
  roulette inside one entry produced wrong moving-emitter images on Linux Intel
  only when the area-light handler was specialized away, and compiled for 149 s;
  it was not adopted.)
- **Transparent-shadow split.** A GPU-only `prepare_shadow_shader_data` stage
  (workgroup 64) caches per-hit ShaderData for `shade_shadow_transparent`
  (workgroup 1), which avoids an intermittent Intel Mesa 25.0.7 compiler crash
  (`brw_opt_combine_constants`) on the combined entry.
- **ARM64 oracle rounding.** `NATIVE_FLOAT3_RECIPROCAL_NORMALIZE` (from
  `__KERNEL_SSE__` on the oracle host) selects per-component division or
  reciprocal-length multiplication for ShaderData normals, matching each CPU
  oracle's `normalize(float3)`. Cosine-hemisphere sampling and the specular
  valid-normal adjustment use the existing rounding adapters, which are bit-exact
  on Intel and Adreno; on Linux these only move results closer to the CPU oracle.

Command recorder formats (replay reads every format):

| Format | Adds pipeline entries |
| --- | --- |
| v1 | 0 control, 1 init_camera, 2 intersect_closest, 3 shade_background, 4 shade_surface, 5 prepare_surface (implicit size 64) |
| v2 | explicit dispatch sizes (kind 5 commands) |
| v3 | 6 intersect_shadow, 7 shade_shadow |
| v4 | 8 shade_light_forward |
| v5 | 9 prepare_shader_data, 10 intersect_volume_stack, 11 shade_volume, 12 shade_shadow_transparent, 13 shade_surface_light, 14 shader_eval_volume_density |
| v6 | 15 connect_surface_light |
| v7 | 16 prepare_shadow_shader_data |

Remaining guards and gaps: static instanced triangle emitters; moving
transparent shadows; volume NEE/MIS, shadow attenuation and heterogeneous media;
light trees, environment MIS and textured emission; light/shadow linking, AO,
shadow catchers and MNEE. The emitter-sampling entry with object motion still
exceeds the 60 s compile budget cold on Linux (warning only). Old captures replay
with their own unspecialized modules and can still crash the driver with a cold
cache. ARM64 and Android x64 device results are in the next two sections.

## Integrated tree on ARM64 Android devices (2026-10-20)

The integrated tree ran natively on the Samsung SM-X700 (API 36, Adreno 730; the full-fidelity device) and the
Pixel 4 XL (API 33, Adreno 640; the old-hardware gauge). It used the unchanged runtime, Android ARM64 binaries
built from the same sources with NDK 29.0.14206865 at API 33, and the practical criterion unchanged.
Outputs are in `native/work/phaseC2/arm64/`.

- ARM64 oracle rounding works on the device. On the Samsung, glass_bounce (path 126), glass_render and
  microfacet_render pass with exactly the numbers recorded when the adapters were introduced. On the Pixel,
  glass_bounce and glass_render now also pass.
- Samsung passes these, with 71 on-device replays of its own v3/v6/v7 captures:
  - transparent shadows (v7) and transparent lighting;
  - point/sun/spot/area lighting, MIS and broad lighting;
  - feature combination;
  - object and deforming motion;
  - camera motion;
  - glass/GGX SVM, bounce and render;
  - shader data, closest and opaque shadows;
  - finite and closed homogeneous volumes.
  Linux Phase C v6/v7 captures replay within tolerance, except the triangle mode-2 capture (below).
- Specialized pipelines compile in at most 32.6 s on the Samsung and 70 s on the Pixel. The emissive-triangle
  `shade_surface_light` with object motion took 204.8 s on the Samsung. It still renders correctly, with a
  degraded-latency warning.
- Specialization is mandatory on Adreno. The every-handler `shade_surface_light`
  (`CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0`) crashes the Adreno 730 driver and exhausts memory on the Adreno 640.
  Bit-identity between specialized and unspecialized modules is therefore established on Linux only.
- Open on ARM64 in this run, each revisited in Phase E (sections below):
  - Emissive-triangle sampling and the emitter-sample probe failed against the on-device (bionic) oracle. Under the
    validation reference policy the probe passes and only the deforming emitter still fails the sampler gate.
  - Triangle image mode 2 diverged by one path decision on Adreno 730, even when replaying the x64 capture; it passes
    since the exact traversal.
  - On the Pixel 4 XL, object/deforming motion images, feature combination, the volume-boundary state test and the
    camera-inside closed volume failed in this run; they pass after the Adreno 640 fix described in
    "Pixel 4 XL (Adreno 640) function-scope array fix" below.

## Android x64 (HP, Intel Mesa 24.0.8) on the integrated tree (2026-10-20)

The integrated tree was built for android-x64 (NDK 29.0.14206865, API 33) and run on the HP with cold compiles
(`MESA_SHADER_CACHE_DISABLE=true`; the device has no Mesa disk cache) and the unchanged 1.0.290.1433 runtime. The
build uses SSE normalization (`NATIVE_FLOAT3_RECIPROCAL_NORMALIZE=0`). Glass/GGX (svm, bounce incl. path 126,
render), microfacet, the transparent-shadow split (v7, `prepare_shadow_shader_data`), spot/area and point/sun
lighting with MIS and forward light, feature combination, the emitter-sample probe for lamps, finite homogeneous
volumes and volume boundaries, camera motion, object motion and deforming-triangle images pass the practical
criterion at full tier with no compile-budget warnings; 65 v6/v7 command replays pass with 31,277 exact
scheduling readbacks. Evidence: `native/work/phaseC2/x64/`.

Three pipelines still exhaust Scudo's 256 MiB per-size-class regions inside the HP's Intel compiler and abort the
process, on every cold compile: the emissive-triangle `shade_surface_light` with object motion (triangle gate
mode 5), the triangle sampler gate's `emitter_sample_probe`, and the unspecialized `shade_surface_light`
(`CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0`). Emissive triangle lights are therefore not validated on the HP beyond
static triangle images 0-3 (which pass, with replays), and the specialization on/off bit-identity check cannot
run there. Phase E resolved this: the first two compile since the emissive-triangle compile-load change, and
the unspecialized configuration is refused on Android (sections below).

## Device tiers: persisted record, known adapters and init canary

WebGPU reports adapter identity and limits but not shader-compiler capacity, and
a vendor compiler crash ends the process. The WebGPU device therefore chooses
its pipeline tier once, when it is created and before any renderer pipeline
compiles (`native/cycles/device_tier.{h,cpp}`):

1. an explicit request (host API or `CODEBRIX_CYCLES_PIPELINE_TIER`), reason
   `override` (full) or `opt-in` (conservative);
2. the persisted device-identity record, reason `record`: a full-tier crash
   breadcrumb or pipeline-creation failure selects conservative; full-tier
   compiles without any failure select full (so the record overrides the table);
3. the known-adapter table, reason `table`;
4. full fidelity, reason `default`.

The optional init canary then compiles one representative module (the surface
module with no optional handlers, entry `prepare_surface`) in the full tier. A
canary failure selects conservative before the scene compiles (reason `canary`);
a canary crash leaves a breadcrumb, so the next run starts conservative from the
record; a canary over its budget (10 s, `CODEBRIX_CYCLES_PIPELINE_CANARY_BUDGET_MS`)
only warns. During rendering a non-fatal creation failure still retries in the
conservative tier (reason `failure`) and is recorded. A slow compile never
changes the tier: it warns and marks the session degraded, because the
conservative tier (workgroup size 1) is not bit-identical to full fidelity. The
only exception is the explicit old-hardware opt-in
`CODEBRIX_CYCLES_PIPELINE_BUDGET_ACTION=conservative` (reason `budget-opt-in`,
never recorded). Full-tier WGSL is unchanged by this mechanism.

Known-adapter table (`known_adapters()`; device and driver are substrings of the
WebGPU adapter device name and driver description):

| Entry | Device | Driver | Tier |
| --- | --- | --- | --- |
| Pixel 4 XL Adreno 640 | `Adreno (TM) 640` | any | full tier, 20 s compile budget, not a reference device, verdicts per the device reference policy |
| Samsung SM-X700 Adreno 730 | `Adreno (TM) 730` | any | full |
| Samsung Galaxy S20 FE Adreno 650 | `Adreno (TM) 650` | any | full tier, 20 s compile budget (warnings only), not a reference device; refuses the texture, colour and utility node region `SVM_NODES_ANY` ("Adreno 650 lane"); caustic (MNEE) scenes are refused before any pipeline compiles ("Manifold next-event estimation (caustics)") |
| HP Intel Xe (Android x64) | `Intel(R) Xe Graphics` | `Mesa 24.0.8` | full |
| Linux Intel UHD ADL-S | `Intel(R) UHD Graphics (ADL-S GT1)` | `Mesa 25.0.7` | full |
| AMD GPU on Metal (untested precaution; Apple builds only) | `AMD` | any | **conservative**: osx-x64 policy "not supported, may work" for AMD GPUs in Intel Macs; no AMD GPU tested, and the Intel Metal compiler miscompiles `prepare_surface` with more than one invocation per workgroup (docs/TESTING-MACOS-X64.md, M2) |

An unknown mobile adapter (Android build, or a Qualcomm, ARM, Imagination,
Samsung or Apple vendor id or Adreno/Mali/PowerVR/Xclipse device name) starts
full with a 20 s compile budget instead of 60 s (warnings only). A table entry
may carry its own budget: the Pixel 4 XL entry is full with 20 s, because full
tier meets the practical criterion there and the conservative tier was not
faster (see below). One table entry selects the conservative tier: AMD GPUs on
Metal, compiled into Apple builds only (on Windows and Linux AMD adapters are
untouched), as an untested precaution (docs/TESTING-MACOS-X64.md). Otherwise the
conservative tier remains a reactive fallback after a crash breadcrumb or a
creation failure, or an explicit opt-in. Because the table starts it
conservative, the persisted record never collects full-tier compiles for such an
adapter, so the record cannot switch it back to full; only an explicit request
does. An explicit `CODEBRIX_CYCLES_PIPELINE_BUDGET_MS` replaces any budget.
A table entry may also name one shader feature region the adapter refuses
(`refused_region`, a name from `shader_feature_names()`): a scene whose referenced
shaders reach it is refused in `shader_features()` before any pipeline compiles,
with a message naming the GPU and the features (as unimplemented Principled
features are), and the tier report's `knownAdapter` carries `refusedRegion` and
`refused`. Only the Adreno 650 entry uses it. `CODEBRIX_CYCLES_TEST_ADAPTER`
applies it too, so the refusal can be exercised on Linux.

**Persisted record.** Enabled only when the application supplies a writable
directory (`cbc_host_tier_config`, or `CODEBRIX_CYCLES_DEVICE_RECORD_DIR`); missing
directories are created; problems become warnings and never fail rendering. The
file is `codebrix-cycles-device-<fnv1a64 of vendor, architecture, device,
driver description, vendor id, device id>.txt`, UTF-8 text, one `key=value` per
line, fields free of newlines and `|`:

```
codebrix-cycles-device-record 1
vendor=<WGPUAdapterInfo.vendor>
architecture=<WGPUAdapterInfo.architecture>
device=<WGPUAdapterInfo.device>
description=<WGPUAdapterInfo.description (driver)>
runtime=codebrix-api=<n>;wgpu-native=<v>;wgpu-core=<v>;target=<triple>;patches=<list>
shaderHash=fnv1a64:<16 hex digits over every production WGSL module>
stage=<tier>|<module>|<entry>|<successes>|<failures>|<last ms>|<max ms>
breadcrumb=<tier>|<module>|<entry>|signal or interrupted|<signal>|<unix time>
failure=<tier>|<module>|<entry>|<message>
```

Unknown keys are ignored; at most 16 breadcrumbs and 16 failures are kept. A
record whose identity, runtime or shader hash differs is reported
(`identity-mismatch`, `stale-runtime`, `stale-shader-hash`), never applied, and
replaced by the next write. Writes take an advisory `flock` on `<record>.lock`,
re-read the file and rename a temporary copy, so concurrent processes merge.
Before each pipeline creation the device writes `<record>.pending-<pid>-<device>-<serial>`
(runtime, shader hash, tier, module, entry, time); the crash handler appends
`signal=<n>`; the file is removed when creation returns. A pending file whose
process no longer runs becomes a breadcrumb on the next start (`interrupted` if
no signal was written, which includes out-of-memory kills and forced stops while
compiling). Delete the record to start again at full fidelity.

**Host C API (additive; ABI 1).** `cbc_host_tier_config(const CBCTierConfig *,
const char *record_directory)` sets the process-wide request (`CBC_TIER_AUTO`,
`CBC_TIER_FULL`, `CBC_TIER_CONSERVATIVE`) and flags (`CBC_TIER_RECORD_OFF`,
`CBC_TIER_RECORD_READ_ONLY`, `CBC_TIER_CANARY`) for hosts created afterwards;
`CBCTierConfig` is size/ABI/reserved checked. `cbc_host_device_report_json`
returns the capability report: backend, adapter identity, tier, reason and
detail, known-adapter entry, compile budget and its source, record mode, path,
status and breadcrumbs, canary result, environment overrides, warnings and
compiled pipelines. The .NET wrapper exposes `CyclesHost.ConfigureTiers` and
`DeviceReportJson`. Gate and replay JSON carry the same decision under
`pipelineReport.decision`; the pipeline log adds `tierReason`.

**Environment overrides (gates).** `CODEBRIX_CYCLES_PIPELINE_TIER=full|conservative|auto`,
`CODEBRIX_CYCLES_DEVICE_RECORD_DIR=<dir>`,
`CODEBRIX_CYCLES_DEVICE_RECORD=on|off|ignore|readonly` (`ignore` reads and reports
without applying or writing), `CODEBRIX_CYCLES_PIPELINE_CANARY=1|0`, and the
existing `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0`. Test hooks used only by
`native/tools/tier_gate.py`: `CODEBRIX_CYCLES_TEST_ADAPTER` (simulated identity for
the decision; the real adapter still compiles), `CODEBRIX_CYCLES_TEST_CRASH_PIPELINE`
(raise `SIGABRT` inside the breadcrumb scope of the named stage, or
`canary:prepare_surface`) and `CODEBRIX_CYCLES_TEST_CANARY=fail` (a canary
module the WebGPU front end rejects).

`native/tools/tier_gate.py` runs each scenario as a separate process of
`tests/host/tier_consumer.c` on Linux: fresh device, record reuse, a real crash
breadcrumb followed by a conservative start, stale shader hash and runtime,
table lookups (including the real adapter and an unknown mobile adapter),
record-over-table, overrides, canary pass/slow/failure/crash and a slow compile
that keeps the full tier. Every film meets the practical criterion against the
original CPU session.

**Device confirmation (2026-10-20).** Android builds of the merged tree (NDK
29.0.14206865, API 33, unchanged runtime) report these adapters and decisions,
with glass_render, transparent_lighting and lighting_mis passing the practical
criterion (outputs in `native/work/phaseC4/`):

| Device | Adapter device / driver description | Decision | Tier warnings |
| --- | --- | --- | --- |
| Samsung SM-X700 | `Adreno (TM) 730` / Adreno driver EV031.36.08.33 | full, `table` | none |
| HP Android x64 | `Intel(R) Xe Graphics (TGL GT2)` / `Mesa 24.0.8` | full, `table` | none |
| Pixel 4 XL | `Adreno (TM) 640` / Adreno driver EV031.31.04.01 | full, `table` (20 s budget) | compile-budget warnings only |

- On the Pixel 4 XL, the conservative tier (the former table entry) and the
  full tier (`CODEBRIX_CYCLES_PIPELINE_TIER=full`) give the same image errors and
  strict counts (glass_render film bit-identical); the conservative tier was not
  faster to compile (`shade_surface_light` up to 79 s against up to 70 s in
  transparent_lighting, 444 s against 410 s wall). The opt-in canary compiled in 2.0 s
  against its 10 s budget.
- The Samsung and HP glass_render films are bit-identical across runs with and
  without a device record. Specialized against unspecialized bit-identity cannot
  be checked on these devices, because the unspecialized surface module does not
  compile there (see "Open device items").
- A device record in a shell-writable folder under `/data/local/tmp` is created
  with the documented format, reused (`full`, reason `record`), and its
  `<record>.lock` serializes writers: with another process holding
  `flock(LOCK_EX)` for 60 s, a gate's record writes waited for it (about 56 s
  longer wall time) and the stage counts merged.
- On the HP, `CODEBRIX_CYCLES_TEST_CRASH_PIPELINE=prepare_surface` aborted the
  process; Android's signal chaining still let the handler append `signal=6` to
  the pending file. The next run consumed it as a breadcrumb and started
  conservative (reason `record`, workgroup size 1, practical criterion met, film
  bit-identical to full tier); after deleting the record the device returned to
  full (reason `table`).
- The Android x64 host gate passes on the HP with 38 exports.

## Managed WebGPU renders on Android and the 48-export host (2026-10-20)

Android builds of the ABI 1 tree (D2 typed API, D3 48 exports; NDK 29.0.14206865,
API 33, unchanged WebGPU runtime) pass `android_host_gate.py` on the Samsung
SM-X700 and the HP (48 exports in both binary reviews; 1,579 films each, 0
practical failures, 0 required strict differences; GPU lifecycle repeats within
the strict criterion). The validation app gained a WebGPU mode
(`android_app_gate.py --mode webgpu`, see MAINTAINER-README.txt): inside the
installed app the typed API renders three scenes (glass sphere, diffuse with a
sun, textured plane through image upload; 32x24, 16 samples;
`tools/hosttests/WebGpuScenes/WebGpuValidationScenes.cs`) on `CyclesBackend.Gpu`
and on `CyclesBackend.CpuReference`, and the runner recomputes every comparison
from the extracted films (outputs in `native/work/phaseD5/android/`):

| Device | Glass max / strict | Diffuse+sun max / strict | Textured plane max / strict | Practical | Tier decision | Compile-budget warnings | App time |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Samsung SM-X700 (Adreno 730) | 2.32e-6 / 301 | 4.17e-7 / 549 | 2.38e-7 / 30 | 0 | full, `table` (60 s budget) | none | 178 s |
| HP Android x64 (Intel Xe, Mesa 24.0.8) | 3.71e-5 / 319 | 1.43e-6 / 567 | 2.38e-7 / 32 | 0 | full, `table` (60 s budget) | none | 195 s |
| Pixel 4 XL (Adreno 640), optional gauge | 2.26e-6 / 315 | 4.17e-7 / 537 | 2.38e-7 / 35 | 0 | full, `table` (20 s budget) | `shade_surface_light` 121-125 s; `shade_surface` 24 s, `prepare_surface` 25 s, `shade_background` 21 s | 641 s |

Strict counts are out of 3,072 floats per film. On every device the GPU render
reported native progress events, a render cancelled from its first progress
event threw `OperationCanceledException` without faulting the device, and a
re-render on the same scene met the practical criterion. GPU render times are
dominated by first-use pipeline compiles (Samsung 40-48 s, HP 37-64 s, Pixel
135-181 s per scene). The ARM64 CPU reference films are bit-identical between
the Samsung and the Pixel. Smooth-shaded objects in these scenes used a zero
shadow-terminator geometry offset at the time; Phase E3 implemented the offsets
and the scenes now keep the default (confirmed on all three devices in the Phase E merge).

## Emissive-triangle compile load and the Android oracles' C library (2026-10-20)

**Compile load.** Drivers inline every WGSL call, and `shade_surface_light`
with `MOTION_OBJECT` + `LIGHT_TRIANGLE` held four copies of the emissive-triangle
vertex computation (the forward-hit MIS weight and the emitter sample each needed
the path-time and centre-time vertices, each with the object-motion transform,
its 30-step double-word arccosine and the motion sine/cosine). That entry inlined
to 3.35 MB of WGSL and ran the HP's Intel Mesa 24.0.8 compiler out of memory
(Scudo); the Samsung took 205 s and Linux 65-75 s cold. The sampler now reads
vertices from `triangle_light_prefill`, which computes every set an invocation can
use, in the original order, from one call site; the arccosine, the motion
sine/cosine and its disk sine/cosine are triangle-light-private copies whose
Horner series run as loops (the same operations in the same order). The shared
motion and trigonometry helpers are unchanged. Inlined size: mode-5
`shade_surface_light` 3.35 -> 1.32 MB, triangle without motion 1.60 -> 1.16 MB,
the triangle sampler gate's probe 2.15 -> 1.20 MB. Every Linux result is
byte-identical (triangle sampler and emitter-sample bins, all five triangle
images' GPU/CPU/repeat films and command streams). Cold single-pipeline compiles:

| Entry (cold, one pipeline per process) | Linux Intel Mesa 25.0.7 | HP Intel Xe, Mesa 24.0.8 | Samsung Adreno 730 |
| --- | --- | --- | --- |
| `shade_surface_light`, triangle + object motion (mode 5) | 75.3 -> 24.3 s | Scudo OOM at 2.25 GB after 21 s -> 27.4-27.6 s / 2.08 GB (2 of 2) | 203.9 s / 3.28 GB -> 29.4-30.0 s / 0.49 GB |
| `shade_surface_light`, triangle, no motion (mode 1) | 14.9 -> 14.7 s | 24.9 s / 2.61 GB -> 20.5 s / 1.84 GB | 34.1 s / 0.54 GB -> 20.1 s / 0.39 GB |
| `emitter_sample_probe` (triangle sampler gate) | compiled cold in both | Scudo OOM at 1.85 GB after 13 s -> 21.9-22.0 s / 1.99 GB (2 of 2) | 80.5 s / 1.47 GB -> 24.4-24.7 s / 0.41 GB |
| `connect_surface_light` (mode 5) | unchanged | 10.8 -> 10.9 s / 0.32 GB | 1.7 -> 1.7 s |
| `prepare_surface` (mode 5) | unchanged | 5.9 -> 5.9 s / 0.34 GB | 2.6 -> 2.6 s |

**Android oracles use bionic's arccosine and arcsine.** The Samsung triangle
sampler failures (24 in 22 of 4,096 samples) and the emitter-probe area-light
pdf (1.10 times the tolerance) are not SSE-versus-scalar float3 arithmetic. At
all 18 failing samples on static emitters the ARM64 oracle's
`acosf(cos_alpha)` (`kernel/light/triangle.h`, `safe_acosf`) differs from the
correctly rounded value, which glibc returns and the WGSL reproduces; the
probe's pdf difference is one `asinf` in the rectangle solid angle
(`kernel/light/area.h`), 2 ulp in one term reproducing both oracle pdfs exactly.
Over every binary32 input in [-1, 1], bionic arm64 `acosf` differs from the
correctly rounded result at 5,387,136 inputs and `asinf` at 682,540; bionic
x86_64 (the HP) differs at 5,717,766 and 4,220,748 with different results again;
glibc is correctly rounded. Evidence for this section: `native/work/e1/` in the E1 worktree (`libm/`, `analysis/`, `logs/probe-table-*.jsonl`, `runs/`); the sealed handoff is in `native/evidence/phaseE-merge-20261020/laneE1/`. Matching these
oracles on the GPU would mean reproducing bionic's libm (a separate, licensed
implementation, different per Android ABI). The decision taken instead is the
validation reference policy below: the correctly rounded glibc reference decides,
bionic is a diagnostic.

**Specialization off on Android.** `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0` is now
refused on Android when the WebGPU device is created, with a message naming the
variable, before any pipeline compiles. It remains a desktop A/B diagnostic.

**Device results with these changes** (unchanged runtime, cold compiles, practical
criterion unchanged). HP: the triangle sampler gate now compiles (22 s); the
surface gate and all five triangle images pass, including the moving/deforming
emitter (mode 5) and the static-instanced rejection, with five on-device replays
and the five Linux triangle captures replayed exactly. Samsung: the surface gate,
triangle images 0 and 1 and the Linux captures of modes 0, 1, 3 and 5 pass
(the mode-5 capture's `shade_surface_light` now compiles in 29 s). On both,
glass_render, lighting_mis and transparent_lighting match the previous device runs
(glass bit for bit; the others within each run's own repeat variation).

## Pixel 4 XL (Adreno 640) function-scope array fix (2026-10-20)

The three Pixel-only failure families from the integrated-tree run were one driver defect class: the Adreno 640
compiler (driver EV031.31.04.01) miscompiles dynamically indexed arrays held inside function-scope aggregates in
large entries. Each family was reproduced by replaying the unchanged Linux x64 Phase C captures on the Pixel
(`native/work/e2/probe/`): the first scheduling readback that differs isolates the stage, and arena dumps after
each dispatch, compared with the same replay on Linux after injecting the Linux state, isolate the field.

- **feature_combination (error 512).** `prepare_surface` raised the microfacet lookup-table bounds error in
  `microfacet_lut3` (generalized Schlick for glass) because `table_count` read 0: the table-descriptor address
  `p(14)` arrived with its low 16 bits cleared (0x270000 instead of 0x27E7F0). Coordinates, offsets and the table
  were correct; adding an atomic probe hid the fault. The B3-era capture of the same scene replays cleanly; the
  larger `prepare_surface` after the ARM rounding adapters exposed the latent fault.
- **Object and deforming motion images (zero film).** `prepare_surface` produced no closures: its
  stage record held `count=0, remaining=0` where Linux has `count=1, remaining=11`, so shading created no shadow rays
  and the film stayed zero.
  The B3-era deforming capture fails the same way; the fault predates B3 (motion images had not run on the Pixel).
- **Volume-boundary state and camera-inside closed volume (integer stack mismatches).** In
  `intersect_volume_stack` every entry of the function-local `array<NativeHit,64>` read back as the last hit
  appended, so the sort and the enclosure test picked the wrong object for the initial volume stack.

The fix keeps every computation and comparison unchanged:
- `wavefront_svm.wgsl`: the SVM value stack and its initialization mask live in one module-scope
  `var<private>` array (`svm_stack`, words 0..254 and mask words 255..262) instead of two arrays inside the
  function-local `SvmState`; the mask is cleared with constant-index stores at the start of every evaluation
  (evaluations never nest). Two separate private arrays lost mask bits on this driver (error 8), and an
  in-struct vector mask still failed, so the single array is deliberate.
- `wavefront_volume_stack.wgsl`: hits are parallel scalar arrays (t, object, prim) filled through a pointer; a
  pointer-passed array of structs still failed.

Results with the fix (Pixel, full tier by table, 20 s budget, warnings only):

| Corpus (Pixel, single confirmation run) | Before | After |
| --- | --- | --- |
| feature_combination (+1 replay) | error 512 | PASS; replay 408 readbacks, max 3.87e-7 |
| object/deforming motion images, moving and stationary (+2 replays) | zero film, all four | PASS, all four; replays 414/420 readbacks |
| volume_boundary state test | `volume_stack[0].object` mismatch | PASS |
| camera-inside closed volume (+1 replay) | `volume_stack[1].object` mismatch | PASS; replay 1,116 readbacks, max 1.53e-5 |
| regression: lighting_mis, glass_bounce, glass_render, transparent shadow/lighting, camera static/motion, GGX set | passed in C2 | PASS; lighting_mis (8), glass_render and transparent (8) films bit-identical to the C2 Pixel films |

Linux x64 results are unchanged: every deterministic film of the affected gates is bit-identical before and after,
the only differing films are atomic-accumulation cases whose own repeat differs within the run, the glass film is
unchanged with specialization on and off, and all-lanes passes. The conservative tier does not change these outcomes
(emulated on the pre-fix WGSL with workgroup-1 modules and dispatches, the same failures occur), so it is still never
selected for this adapter. The fix does not change compile times: on the Pixel the motion entries still take
50-73 s (`intersect_closest`, `intersect_shadow`, `prepare_surface`, `prepare_shader_data`) and the surface/camera
entries 21-31 s against the 20 s budget, as warnings. Evidence: `native/work/e2/` in the E2 worktree.

## Validation reference policy (2026-10-20)

Decision (2026-10-20): Android device validation judges the device GPU against a **correctly
rounded CPU reference**. Today that is the Linux x64 glibc CPU oracle; Linux ARM64 glibc (a
Raspberry Pi 5) and macOS are planned to confirm it in Phase F. The CPU reference a gate computes on
the Android device itself, with Android's bionic C library, is kept as a **diagnostic** column: its
deviation from the glibc reference is reported per gate and it never decides pass or fail. No libm
code is imported or emulated.

**Two gold standards (2026-10-24).** The unmodified pinned Cycles CPU kernels and the unmodified pinned
Cycles Metal backend (the osx-arm64 product device) are both accepted reference evidence. Metal films
are never judged against CPU films for pass or fail, and their differences from the CPU (path
divergence, hardware texture filtering, MetalRT motion interpolation; `native/host/MACOS_ARM64.md`) are
characteristics of the Metal reference, not deviations. The WGSL device (every candidate backend) is
judged against the CPU gold standard, the only one available where it runs; WGSL-versus-Metal pairs
are reported as information (`native/oracle/compare.py`, `native/oracle/README.md`). The earlier
2026-10-19 record of Metal's texture filtering as a bounded deviation (`native/host/MACOS_ARM64.md`,
"Results and findings (2026-10-19)") stays as history; this rule supersedes its wording.

**Why bionic is a diagnostic.** Cycles calls the C library's binary32 functions (`safe_acosf` and
`safe_asinf` in light and emitter sampling, `atan2f`/`sinf`/`cosf`/`tanf`/`expf`/`logf`/`powf` in
closures, camera, SVM nodes and scene preparation). glibc 2.41 returns the correctly rounded
`acosf`, `asinf`, `atanf`, `atan2f`, `tanf` and `sqrtf`, and the WGSL kernels reproduce those values.
bionic does not, and arm64 and x86_64 bionic differ from each other. Measured with
`native/tools/libm_probe.py` (records in the X1 worktree's `native/work/x1/libm/`; the Linux record of the merge is `native/work/merge1/reference/libm-linux-x64-glibc.json`):

| Function (domain) | Linux x64 glibc 2.41 | Samsung SM-X700 bionic arm64 | HP 87FE bionic x86_64 |
| --- | --- | --- | --- |
| `acosf` (all 2,130,706,434 binary32 in [-1,1]) | 0 | 5,387,132 | 5,717,762 |
| `asinf` (same) | 0 | 682,532 | 4,220,746 |
| `atanf` (every 16th binary32, 267,386,880) | 0 | 394,660 | 394,660 |
| `tanf` (same) | 0 | 18,985,998 | 18,985,998 |
| `atan2f` (2^24 pairs in [-8,8]^2) | 0 | 2,708,169 | 2,708,169 |
| `sinf` / `cosf` (every 16th binary32) | 1,836,020 / 1,764,666 | same values as glibc | 1,836,018 / 1,764,664 (other results) |
| `expf` / `logf` / `powf` | 10,698 / 26,322 / 10,435 | same values as glibc | same values as glibc |
| `sqrtf` | 0 | 0 | 0 |

Counts are inputs whose result differs from the correctly rounded one (at most 1 ulp everywhere);
inputs within 2^-50 of a rounding midpoint are counted separately as undecidable (at most 10 per
function). glibc's own `sinf`/`cosf`/`expf`/`logf`/`powf` are not correctly rounded either; for those
the Linux reference is glibc's result, which the Samsung's bionic matches bit for bit. The decisive
functions for the earlier Android failures are `acosf`/`asinf`: all 18 static-emitter triangle-sampler
failures and the emitter-probe area-light pdf are 1-ulp bionic errors (Phase E1).

**Two routes by which the device C library reaches a result.** The device's CPU *kernels* (the local
reference) call bionic, and the device's *scene preparation* (light records, transforms and motion
decompositions, BVH, lookup tables, the gates' own fixed inputs) runs on the device too and is what the
GPU consumes. A reference prepared on Linux therefore differs from the GPU's inputs: on the HP the
emitter probe's light records differ in 8 words and the GPU result follows them (24 practical
differences against a Linux-prepared reference, 0 against the reference computed from the HP's own
scene). The authoritative comparison is therefore the **device-scene flow**:

1. the device runs the gate once with `CODEBRIX_CYCLES_SCENE_DUMP` (no GPU work): it writes the scene
   tables it prepared, `KernelData`, the gate's fixed inputs and its local CPU reference;
2. Linux x64 runs the same gate binary with `CODEBRIX_CYCLES_SCENE_LOAD` (CPU only) and exports the
   glibc reference computed from exactly that scene (`native/tools/android_reference.py
   scene-reference`, which also records per table how the device-prepared inputs differ from Linux's);
3. the reference is pushed beside the staged binaries and the device runs the gate with
   `CODEBRIX_CYCLES_REFERENCE_IMPORT` and `CODEBRIX_CYCLES_SCENE_VERIFY` (its local reference must equal
   the dump pass's bit for bit, so the imported reference belongs to these inputs); the gate compares
   its GPU output with the imported reference and appends the local-versus-glibc deviation to
   `reference-diagnostic.jsonl`;
4. the host recomputes every comparison from the pulled files (`android_reference.py compare`):
   GPU versus glibc (authoritative, must agree with the device's verdict), local versus glibc and GPU
   versus local (diagnostics, the latter being the comparison used before this policy).

Provenance per reference: Linux gate binary SHA-256, glibc version, source pins, device and Linux scene
SHA-256, every reference file's SHA-256 and the set digest (`oracleSha256`). Without any of these
environment variables every gate behaves exactly as before (Linux results unchanged).

Reference data flow per Android gate:

| Gate (Android) | Before | After |
| --- | --- | --- |
| `triangle_sample`, `emitter_sample` (sampler arrays) | on-device bionic CPU | device-scene glibc reference; bionic diagnostic |
| Image gates from `lighting_gate.cpp` (`lighting`, `lighting_mis`, `broad_lighting`, `transparent_lighting`, `triangle_lighting`, `feature_combination`, `object_motion_render`, `deforming_motion_render`) | on-device bionic CPU film | device-scene glibc film; bionic film diagnostic |
| `render_gate.cpp` independent image (`glass_render`, `microfacet_render`, `render`, volume renders) | on-device bionic CPU film | Linux-prepared glibc film (`android_reference.py export`); no scene exchange yet |
| `render_gate.cpp` lockstep stage checks (in the device-scene flow) | on-device bionic CPU, with the device's own arithmetic adapters on the GPU | **stage-state exchange** (2026-10-23): the device run records every stage's GPU input and output; the Linux x64 glibc gate judges them from the device's dumped scene (`android_reference.py stage-judge`); the GPU uses the reference oracle's adapters; `CODEBRIX_RENDER_LOCAL_LOCKSTEP=1` keeps the earlier local check as a diagnostic |
| State gates (`surface`, `closest`, `shadow`, `shader_data`, glass/microfacet `svm`/`bounce`, `principled_bounce`, `camera`, `volume`, `volume_boundary`, `light_state`, `broad_light_state`, `transparent_shadow`, `svm_world`) | on-device bionic CPU | **unchanged (local)**: not yet routed through the policy (open item 5) |
| Command replays of the device's own captures and of Linux captures (`render_replay`) | the capture's recorded readbacks and film | unchanged: no CPU reference on the device; the Linux captures carry the Linux x64 film |
| `android_host_gate.py` (host API films) | Linux reference run (`host_gate.py`) | unchanged: already a Linux reference; the GPU 64-sample film (`run.3`) is judged practical-required with strict recorded since merge wave 7 (see "Merge wave 7") |
| `android_app_gate.py` CPU suite | Linux references packaged in the APK | unchanged |
| `android_app_gate.py --mode webgpu` (managed API) | in-app CPU film (device scene, bionic) | `--linux-reference`: Linux glibc CPU films rendered by `GpuRenderTests` with `CODEBRIX_CYCLES_REFERENCE_EXPORT` (Linux-prepared scene; the managed API has no scene exchange); in-app CPU film diagnostic |

Results under the policy in the X1 worktree, before E1 and X2 were merged (unchanged 1.0.290.1433 runtime;
strict counts are of the reached cases; the merged-tree results are in "Merged tree on the devices"):

| Device / gate | GPU vs glibc reference (authoritative) | Local bionic CPU vs glibc (diagnostic) | Before (GPU vs local bionic) |
| --- | --- | --- | --- |
| Samsung `emitter_sample` | **PASS**, 135 strict, max 9.06e-6 | 1,192 floats differ, 1 practical (the area-light pdf) | FAIL (sample 4093 pdf) |
| Samsung `triangle_sample` | **FAIL**: 0 failures on the static emitters; 41 practical in 24 samples, all on the deforming emitter | 4,620 floats differ, 65 practical | FAIL, 24 practical in 22 samples (18 static, libm) |
| Samsung `triangle_lighting` | modes 0-1 PASS; **mode 2 FAIL** (component 1484, 0.351310 vs 0.311020; passes since the exact traversal); modes 3-5 not reached | 1,973 floats differ, 0 practical | same mode-2 failure |
| Samsung `glass_render` / `lighting_mis` / `transparent_lighting` | PASS: 98 image strict / 1,162 / 974 | 2,280 / 4,509 / 5,108 floats differ, 0 practical | PASS; glass film `8199ad80...` bit for bit as C2/E1 |
| HP `emitter_sample` | **PASS**, 235 strict, max 5.1e-3 | 617 floats differ, 0 practical | PASS |
| HP `triangle_sample` | not reached: compiler memory (compiles since the E1 change; passes in the merged tree) | - | same |
| HP `triangle_lighting` | modes 0-3 PASS (699 strict); mode 5 not reached (passes in the merged tree) | 195 floats differ, 0 practical | same |
| HP `glass_render` / `lighting_mis` / `transparent_lighting` | PASS: 152 image strict / 2,769 / 1,933 | 168 / 10 / 317 floats differ, 0 practical | PASS; glass film `bafd466d...` bit for bit as C2/E1 |
| Linux v6 triangle captures replayed | Samsung tri0/1/3/4 PASS, tri2 FAIL (passes in the merged tree); HP tri0-3 PASS, tri4 compiler memory (passes in the merged tree) | - | same |
| Managed WebGPU mode (glass, diffuse+sun, textured plane) | Samsung and HP PASS against Linux films, 0 practical (max 2.8e-5 / 2.3e-6) | in-app CPU films differ from Linux's (up to 41 strict differences per film), 0 practical | PASS against the in-app film |

The Samsung's remaining sampler failures are on the deforming emitter only. There the Samsung's own CPU
reference and the glibc reference computed from the Samsung's scene disagree in 44 fields, and against
the Linux-prepared reference the HP's x86_64 bionic CPU agrees bit for bit on 561 of the emitter's 731
samples where the Samsung's agrees on 108: it is ARM64
float3 arithmetic (the `NATIVE_FLOAT3_RECIPROCAL_NORMALIZE` class from B2), not libm, which an x64
reference cannot reproduce. Re-judged against glibc, Phase E1's HP sampler run (with E1's compile
fix) has no static-emitter failure either; it keeps 5 practical differences on the
deforming emitter. The Linux ARM64 glibc reference (Pi 5, Phase F) is the lane that confirms or
refutes this; macOS confirms the libm part on a second correctly rounded library.

Reproduce: `python3 native/tools/libm_probe.py android-arm64 OUT.json --serial SERIAL` (or
`android-x64`, `linux`); `python3 native/tools/android_reference.py export|scene-reference|compare
--help`. The device runner used for these results is `native/work/x1/tools/run_corpus.py
--device-scene --reference <export>` (copied for the merge as `native/work/merge1/tools/run_corpus.py`).

### Reference devices and the publish rule (2026-10-27)

A reference device is one where everything ported so far must pass, judged under
this policy, before a release is published: every host-gate step, every device
gate, every oracle scene and bench scene the device renders, and the managed
tests. "Not ready to publish" is the answer while any of them fails there.
Functionality not yet ported (volume rendering on the WebGPU device) is outside
"ported so far"; a driver workaround that gives the CPU kernels' images (W1,
M2) counts as passing, a refused or masked feature does not.

| Device | RID | GPU route | Correctly rounded reference |
| --- | --- | --- | --- |
| Linux x64 desktop (Intel UHD ADL-S, Mesa 25.0.7) | linux-x64 | WGSL on Vulkan | itself (glibc 2.41) |
| Samsung SM-X700 (Adreno 730) | android-arm64 | WGSL on Vulkan | Linux x64, device-scene flow |
| HP 87FE (Intel Xe, Mesa 24.0.8) | android-x64 | WGSL on Vulkan | Linux x64, device-scene flow |
| **Mac mini 2018 (Intel UHD 630)** | osx-x64 | WGSL on Metal (runtime 1.0.300.210, precise math) | Linux x64 (docs/TESTING-MACOS-X64.md) |

The Apple Silicon Mac mini (M2 Pro) is not a WGSL reference device: osx-arm64
ships Cycles' own Metal device, not the WGSL device. It is the comparison
between the pre-existing Cycles GPU target (Metal) and the device developed
here; its Metal films are reference evidence of their own (the two gold
standards above) and are reported beside the WGSL films, never judged against
them.

Apple's libm is not correctly rounded either: on the Intel Mac the in-process
CPU reference disagrees with glibc on `linux-gates/triangle-lighting-2` (3
practical failures) and in the `light_tree`, `microfacet_render` and `mnee`
device gates, while the Mac GPU agrees with glibc (the scene exchange, the
render stage exchange and, new on 2026-10-27, the MNEE stage-state exchange in
`mnee_gate.cpp` - `CODEBRIX_CYCLES_STAGE_RECORD` on the device,
`CODEBRIX_CYCLES_STAGE_JUDGE` with `android_reference.py stage-judge` on Linux;
the same flow serves the Android devices, closing open item 5 for that gate). The Mac is therefore judged
like an Android device: the Linux x64 reference decides, the Mac's own CPU
reference is a diagnostic. The 2026-10-27 reference was produced in a Linux
container with the same toolchain (`native/evidence/linux-x64-docker-reference-20261027/`:
Debian 13, GCC 14.2.0, clang 19.1.7, CMake 3.31.6, glibc 2.41), with
`native/tools/macos_x64_reference_exchange.py` running the exchanges against the
container of `native/tools/linux_reference/`.

**Linux x64 reference rule (Jeremy, 2026-10-29).** Linux x64 and Android testing
and validation are done on the dedicated Linux x64 machine only, and every Linux
x64 reference other platforms compare against comes from that machine and is
committed: the newest `native/evidence/linux-x64-reference-<date>/` (now
`linux-x64-reference-20261029`, which supersedes the Docker set; its CPU films are
bitwise identical to the Docker set's), with the Android counterpart
`native/evidence/android-reference-20261029/`. The Docker route is not used for new
evidence. Devices without a Linux x64 side (Windows, the Intel Mac, the Raspberry
Pi) run the device-gate exchanges as a two-visit hand-off with that machine,
`native/tools/reference_exchange_handoff.py` (`device-dump`, `linux-reference`,
`device-import`, `linux-judge`; docs/TESTING-WINDOWS-ARM64.md, "Device-gate
exchange with the Linux x64 machine"). Tests use only the
CodeBrix.WebGpu.Native.MitLicenseForever 1.0.300.210 binaries and the .NET SDK
10.0.403 (docs/TESTING-*.md, "MANDATORY TEST RULES").

## Exact traversal and intersection arithmetic (2026-10-20)

The closest, shadow (opaque and transparent) and volume-stack traversals, and
the emissive-triangle planar hit, now reproduce the CPU oracle's ray/triangle
and instance-push arithmetic bit for bit. Before, the WGSL used plain products
and sums that drivers may fuse into FMA, and an exact divide where the x86
oracle takes an approximate reciprocal.

- **Contraction-proof sums.** `c_ray_triangle_intersect` (in
  `shaders/wavefront_triangle.wgsl`) rounds every product before it reaches a
  sum, using the integer `rounded_add`. No driver can fuse it. The generated AST
  version stays in the module as `c_ray_triangle_intersect_ast` for review.
- **Oracle grouping.** A new layout constant, `NATIVE_TRIANGLE_ARITHMETIC`, is
  emitted from the source host, the same way `NATIVE_FLOAT3_RECIPROCAL_NORMALIZE`
  is:
  - **0 (scalar build, ARM64 without SSE2NEON).** `ray_triangle_dot` is
    `(x+y)+z` and the reciprocal is a correctly rounded `1/x`.
  - **1 (x86 SSE4.2 build).** `ray_triangle_dot` is `x+(y+z)`. `madd`/`msub` are
    unfused. The reciprocal is `RCPSS` followed by one Newton step,
    `r*(2-r*x)`, with each operation rounded.
  - **2 (any other SSE build, i.e. SSE2NEON or an unrecognised `RCPSS`).** SSE
    grouping with `1/x`; this mode is documented as not oracle-exact.
- **`RCPSS` model.** On the Intel i7-12850HX that produces the Linux oracle, the
  result depends only on the sign, the exponent and the top 11 mantissa bits.
  - Its 12-bit mantissa is the value nearest to `2^25/(4097+2i)`, with exponent
    `253-e`. Denormal inputs give infinity, infinities give zero, and underflow
    flushes to zero.
  - An exhaustive check of all 2^32 inputs against the instruction found no
    difference.
  - `wavefront_layout()` re-checks every mantissa class on the host before it
    emits mode 1. A host whose `RCPSS` differs, such as another x86 vendor, gets
    mode 2.
- **Instance push.** `transform_vector` and the object-motion push compute
  `transform_point`/`transform_direction` exactly as the oracle does:
  - SSE builds: transpose and accumulate `((w+z)+y)+x`.
  - Scalar builds: the float4 dot `(x+y)+(z+w)`, using a signed zero for
    directions.
  - The shared `wavefront_motion.wgsl` is unchanged.
- **Closest-stage lamp hits.** The area-light quad test (`area_light_intersect`
  plus `ray_quad_intersect`) and the spot back-face test use rounded dots, sums
  and quotients.
- **min/max.** Builtin `min`/`max` equal Cycles' `(a<b)?a:b` for every non-NaN
  input. A `select()` form also mirrors NaN inputs, but it made an intermittent
  Intel Mesa 25.0.7 backend crash (`brw_opt_combine_constants`) frequent in the
  triangle-light module.

Gates:

- **`closest_gate`.** Adds a precision fixture (`cycles/precision_fixture.h`):
  - Geometry: the Principled bench sliver (bench triangle 328, 0.62 m x 5.7 mm),
    seven synthetic slivers shared by three instances (mirrored, rotated,
    scaled), and an instanced emissive quad.
  - Rays: 8,192, aimed at edges, vertices, the quad diagonal and interiors, with
    0-2 ULP perturbations. They include the exact ray that stopped the
    Principled bench at stage 30.
  - Every hit's t, u, v, prim, object and type must equal the CPU bit for bit.
- **`shadow_gate`.** Adds 4,096 near-tangent shadow rays on the same fixture:
  - tmax is within 1e-6 of the target.
  - The aimed emitter triangle is excluded as the light.
  - The blocked decision and every state field must match exactly.
- **Triangle gate.** Now renders static instanced triangle emitters (mode 4) and
  checks them against an independent CPU image plus a replay. The renderer guard
  is lifted.

Command 574 of the Samsung triangle mode-2 replay:

- Re-dispatched on Intel with the Adreno's own arena, the shadow ray is blocked
  with the old traversal and with the exact one. With Intel's arena it is
  unblocked with both. The decision follows its inputs.
- The inputs already differ at bounce 0. The camera ray and `isect.t` are
  bit-identical, but `isect.u` is `0x3e39c8be` on the Adreno against
  `0x3e39c8c0` on Intel. That barycentric comes from the closest stage, which is
  now integer-exact on every GPU.
- Confirmed on the devices in the Phase E merge: the Samsung and the Pixel replay
  the re-recorded Linux mode-2 capture within tolerance and pass mode 2 natively
  ("Merged tree on the devices").

Cost on Linux (Intel Mesa 25.0.7, cold cache, median of three runs):

- `intersect_closest`: 0.55 s before, 1.15 s after (71.6 KB to 76.5 KB of WGSL).
- `intersect_shadow`: 0.69 s before, 1.09 s after.
- `init_camera` and `shade_shadow` are unchanged (0.74 to 0.76 s, 0.04 s).
- Every module stays on the full tier.
- Textual inlined size: `intersect_closest` +76-92 KB, `intersect_shadow`
  +76 KB, `intersect_volume_stack` +38 KB, `shade_surface_light` +21 KB.

## Shadow terminator offsets (Phase E3)

`native/shaders/wavefront_terminator.wgsl` implements the object
`shadow_terminator_geometry_offset` exactly as `light/sample.h`
`shadow_ray_offset`/`shadow_ray_smooth_surface_offset` (the shadow-ray origin of
a smooth-shaded triangle is lifted along the interpolated normal by the local
parabolic envelope, the ray then skips its own primitive only when the light is
on the geometric normal's side, and the self-intersection offset applies to
the lifted origin) and the `shadow_terminator_shading_offset` as
`closure/bsdf.h` `shift_cos_in` (after the bump term in BSDF evaluation for
`cos >= 0` and in sampling for every reflection). The cosine uses the existing
`native_disk_sincos` adapter, so the device rejects shading offsets above 1
(multiplier above 2) with "WebGPU shadow terminator shading offset outside [0,
1] not implemented". Both are scene-specialized: `SHADOW_TERMINATOR_GEOMETRY`
when direct lighting, a smooth triangle and an object with a positive geometry
offset are uploaded, `SHADOW_TERMINATOR_SHADING` when an object's multiplier
exceeds 1. Gate: `native/tools/shadow_terminator_gate.py`
(`codebrix_cycles_shadow_terminator_gate`, a `lighting_gate.cpp` variant).

Results (Linux; the merged-tree numbers): `shadow_terminator_gate.py` 10/10 cases against the original
CPU kernels, 0 practical, max 1.53e-5, with 10 native command replays; the offsets change the CPU image
(sun geometry 69 components, sun shading 390, point geometry 21, point shading 384), and zero offsets
leave every deterministic image film unchanged.

**Intel Mesa recompile crash (Linux).** Creating `shade_surface_light` again when `LIGHT_AREA` or
`MOTION_OBJECT` appears later in one device lifetime intermittently crashes the Intel Mesa 25.0.7
backend (`brw_opt_combine_constants` reading a garbage register number; not a WGSL error). Area lamp
sampling now runs its arcsine series as loops (same operations and order; films unchanged), which
took the `LIGHT_AREA` recompile from 3 of 4 cold crashes to 0 of 7; a later recompile after an area
light still crashed 2 of 4, so the GPU ABI test keeps its area light on the CPU (open item 4).

## Merged tree on the devices (Phase E merge, 2026-10-20)

The Phase E changes above (emissive-triangle compile load, Pixel function-scope arrays, host edits and
shadow terminator, validation reference policy, exact traversal) were merged and validated together. The
runtime stayed the unchanged 1.0.290.1433 package; the shipped `libcodebrix_wgpu.so` was verified by hash
on each device before every corpus. Every sampler and image gate was judged with the device-scene flow of
the validation reference policy (glibc reference computed on Linux x64 from the device's own prepared
scene; the device's bionic CPU result is a diagnostic). Lockstep stage checks and state gates (surface,
closest, shadow, camera, volume, volume_boundary, transparent_shadow) still compare against the device's
own CPU. Every device ran the full tier, decided by the known-adapter table, with no tier warning.

**Linux x64.** Clean build; the maintained all-lanes corpus, the host gate, `dotnet test`, the
triangle gate with six images (static instanced emitter included), the closest/shadow
precision fixtures, the shadow-terminator and scene-edit gates and the motion, volume and
feature-combination gates all pass. Every number equals the lanes' own final runs, apart from 1-2
strict differences in atomic-accumulation cases whose own repeat films differ within the run. The
glass_render film is bit-identical with specialization on and off
(`a072683ce5d76309bdd93f81cc18a8c924c7a71260dd44c51c43871f7b0f851a`; the exact traversal changed it
from the earlier `a8e69a34...`, and Phase E4's ShaderData arithmetic later moved it to
`db8e84f9990299d2b2ce1c53a390f56e1d4beb73ec7326aa55da4c1b074802f6`).

**Samsung SM-X700 (Adreno 730).**
- Triangle image mode 2 now passes. The Linux mode-2 capture that failed at command 574 replays
  within tolerance (415 readbacks, max 6.41e-7), and the native mode-2 image passes against the glibc
  reference (0 practical, max 1.69e-5). The shadow-ray decision now follows the CPU reference: its
  bounce-0 barycentric is integer-exact (see "Exact traversal and intersection arithmetic").
- Static instanced triangle emitters (image mode 4) fail: 9 practical differences against the glibc
  reference (max 0.0127). The device's own ARM64 CPU result also differs from glibc there (6
  practical). The Linux mode-4 capture diverges at command 438, the queue count after surface
  shading (`shade_surface_light`/`connect_surface_light`/`shade_surface`). Modes 0-3 pass. Mode 5
  was not reached natively (the gate stops at the first failure); its Linux capture replays.
- The triangle sampler still fails on the deforming emitter only (41 practical; ARM64 float3
  arithmetic), as before.
- These pass against the glibc reference:
  - the emitter probe;
  - broad lighting, feature combination, lighting MIS and transparent lighting (0 practical in each);
  - the shadow-terminator gate (10 cases, 195 strict) and its 10 device replays;
  - E3's 10 Linux shadow-terminator captures replayed on the device;
  - glass_render (97 image strict);
  - object and deforming motion images.
- Finite and closed volumes, the volume-boundary state test, camera static/motion and the
  transparent-shadow gate pass against the device CPU.
- The closest/shadow precision fixtures reproduce the device CPU bit for bit (0 bit mismatches, 0
  shadow decision mismatches).
- The managed app (glass, diffuse+sun with the default shadow-terminator offset, textured plane)
  passes against the Linux films: 0 practical, max 1.38e-5.

**HP Android x64 (Intel Xe, Mesa 24.0.8).**
- The emissive-triangle corpus passes completely against the glibc reference. The sampler passes
  (0 practical, 865 strict; the bionic CPU differs from glibc by 32 practical, the earlier failure).
  All six images pass, including the static instanced emitter and the moving/deforming emitter (0
  practical), with six device replays.
- Mode 5 and the sampler probe now compile within the Scudo limit.
- The emitter probe, the shadow-terminator gate and its replays, glass_render, lighting MIS,
  transparent lighting, broad lighting, feature combination, volumes and motion pass.
- Precision fixtures: 0 bit mismatches, 0 shadow decision mismatches.
- The six re-recorded Linux triangle captures replay with the same strict counts as Linux's own replays,
  and the ten shadow-terminator captures replay.
- The managed app passes against the Linux films: 0 practical, max 2.55e-5, no capability warning.

**Pixel 4 XL (Adreno 640, old-hardware gauge; 20 s compile budget, warnings only).** One confirmation
run per corpus.
- The formerly failing E2 corpora pass against the glibc reference with their replays: feature
  combination (0 practical), lighting MIS, the four object/deforming motion images, the
  volume-boundary state test, the camera-inside closed volume, and camera static/motion.
- E3's 10 Linux shadow-terminator captures replay within tolerance.
- Triangle images 0-3 pass, and Linux captures 0-3 and 5 replay. Mode 4 fails exactly as on the
  Samsung (same component, same values), and the mode-4 Linux capture diverges at the same command
  438.
- The triangle sampler gate failed with a garbage GPU error mask. Phase E5 traced this to a GPU
  reset during the probe's single 4,096-invocation dispatch, not to an out-of-bounds store; see
  "Pixel 4 XL triangle sampler: GPU reset, not an out-of-bounds store (Phase E5)".
- Compile-budget warnings (over 20 s; warnings only, no tier change): `intersect_closest` 86-87 s and
  `intersect_shadow` 74-83 s in the motion images, `prepare_surface` 48-50 s, emissive-triangle
  `shade_surface_light` 65 s, `init_camera` 27 s, `shade_volume` 27 s, and `shade_surface` 21-23 s.
- The closest/shadow precision fixtures reproduce the device CPU bit for bit.
- The managed app passes against the Linux films (0 practical, max 1.38e-5) and reports the over-budget
  compiles as capability warnings. App time was 421 s (128 s on the Samsung, 197 s on the HP).

**Gate binaries and failed device creation.** A gate whose WebGPU device cannot be created now exits
with status 1. It prints `<gate> device creation: WebGPU device creation failed: <reason>` instead of
crashing on the failed cast (`require_webgpu_device`, `native/cycles/device.h`). Verified on both
Android devices: the specialization-off refusal, which previously ended the gate with exit 139, now
exits 1 with that message.

## Principled BSDF closures (2026-10-20, Linux)

The registered device evaluates the Principled BSDF node
(`svm_node_closure_bsdf`, `CLOSURE_BSDF_PRINCIPLED_ID`, including
`principled_bsdf_emission`) with the pinned closure setup, Fresnel models,
energy preservation and lookup tables:

| Principled component | Native closure | Device record |
| --- | --- | --- |
| Alpha | transparent | existing transparent closure |
| Sheen | `CLOSURE_BSDF_SHEEN_ID` (LTC, `sheen_ltc` table); invalid LTC becomes `CLOSURE_NONE_ID` as natively | roughness, transformA/B in `a`/`b`, frame in `T`/`multiscatter` |
| Coat (tint, normal, IOR) | GGX reflection, dielectric Fresnel, energy preservation | Fresnel model 1 |
| Emission | emission, attenuated by sheen/coat albedo and coat tint | existing emission |
| Metallic | GGX reflection, F82-tint Fresnel | model 5, f0 in `multiscatter`, b in `reflection_tint` |
| Transmission | GGX glass, generalized Schlick (`exponent < 0`) | model 4, f0 in `multiscatter` |
| Thin wall | GGX reflection (constant Fresnel) plus `CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID`, or transparency off camera | thin-glass record (GGX of the mirrored direction) |
| Specular (IOR level, tint) | GGX reflection, generalized Schlick | model 4 |
| Thin film | Belcour/Barla Airy summation with the native `thin_film_table` for dielectric, metallic and transmissive lobes | film thickness/IOR in `a`/`b` when active |
| Diffuse | Lambert or Oren-Nayar with base colour | existing diffuse records |
| Subsurface | **not implemented** | rejected (below) |

Both GGX distributions are supported; multiscatter GGX uses `ggx_E`/`ggx_Eavg`
and `ggx_glass_E`/`ggx_glass_inv_E` with their averages. Every lookup-table read
is bounds checked against the uploaded table (error 512), including the sheen,
generalized-Schlick and thin-film tables. The canonical closure record keeps its
112-byte size; fields are reused per kind as documented in
`native/cycles/surface_closure.h`; in modules that contain the Principled
handler the Glass node records its generalized-Schlick model and f0 the same
way (identical arithmetic, so films do not change).

**Subsurface is rejected, never approximated.** A Principled node whose
subsurface weight is a constant above the closure cutoff, or is linked, is
refused by the native layer before any pipeline or dispatch
(`WebGPUDevice::shader_features`, message `Unsupported feature: Principled BSDF
subsurface scattering ...`). The WGSL handler independently reports error 16
if a subsurface weight above the cutoff is ever evaluated.

**Specialization.** Three appended feature bits keep the code out of scenes that
cannot reach it: `SVM_BSDF_PRINCIPLED` (bit 32), `SVM_BSDF_THIN_FILM` (33) and
`SVM_BSDF_THIN_WALL` (34); `SVM_BSDF_MICROFACET` now also follows Principled.
Principled bits are counted only from shaders the scene references (triangle
shaders, lights, world): Cycles' own `default_surface` is a Principled BSDF and
would otherwise compile Principled into every scene. Existing scenes are
unchanged: the glass scene's specialized modules are byte-identical to the
pre-Principled tree and its film is unchanged by the Principled code. Specialization on/off
films were bit-identical for the Principled render until the bump/derivative opcodes (open item 11). The Principled lobes run in
native order through one loop so that the albedo estimate, energy preservation,
allocation and layering each have a single call site (drivers inline every
call).

**Validation (Linux, cold shader caches).** `principled_svm`, `principled_bounce`
and `principled_render` gate targets (fixture `principled_fixture.h`) compare
against the unmodified CPU kernels at the practical criterion: 237 SVM cases
(19,788 paths, 34.4 M floats, maximum error 9.5e-7, 966 strict differences,
19 GPU rejection cases, 3 native subsurface rejections), 29 bounce cases
(28,560 paths, practical ratio at most 0.11) and a 64x48, 8-sample image of
the 20 fixture graphs plus six Principled spheres (121,235 paths, maximum image
error 6.4e-6, bitwise repeat). The Principled
version of the ForestCommunity bench render passes with exact traversal
(183,114 paths, maximum image error 1.4e-6). Recorder captures of the Principled
render replay on Linux in the current format. Device results are in "Materials
on the devices and scene-specialized probe modules" below.

Run the three gates with `python3 native/tools/principled_gate.py --output
native/work/<new-folder>` (add `--skip-build` after building the
`codebrix_cycles_principled_{svm,bounce,render}_gate` targets). The bench variant
is `codebrix_cycles_principled_render_gate PREFIX TRIANGLES` with the prepared
bench geometry (`native/work/render/bench/triangles.bin`).

## Materials on the devices and scene-specialized probe modules (materials merge, 2026-10-20)

**Probe gates compile scene-specialized modules.** The SVM probe gates (svm_surface,
normal, attribute, image graphs and sampler, image files, and the microfacet, glass,
Principled and node SVM variants) used to compile the whole unspecialized SVM source.
After the materials merge that module made Intel Mesa 25.0.7 (Linux, UHD ADL-S)
miscompile the module-scope `svm_stack` added by the Pixel function-scope array fix:
`normal_gate` failed in all four variants with SVM error 8, while the same code passed
with M3's function-local stack and with a specialized probe. The node handlers added by
the merge are unreachable for that fixture, so the failure is the driver, of the same class
as the withheld Gabor finding. Probe modules are now built like the renderer's
pipelines:
- `gate_probe_wgsl` (`native/cycles/device.h`) specializes them for the uploaded scene.
- `refresh_probe` (`native/cycles/surface_svm_test.h`) rebuilds the probe before a
  dispatch whose programs (typed programs, rejection cases) reach a region the module
  was built without.
- Glass probes keep the Principled region, because the canonical glass records
  (Fresnel model in `roughness`, f0 in `multiscatter`) are written only there.
- `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0` still selects the unspecialized module.
- Every probe gate reports exactly the numbers it reported with the unspecialized
  module.
- The shader-data probe has no SVM interpreter and stays unspecialized: its negative
  test needs the unspecialized regions.

The unspecialized module is checked in two ways. For correctness, by the
specialization-off render runs (glass_render, principled_render and nodes_render on/off
films bit for bit); since the bump and derivative handlers not every one of them
completes on Intel Mesa 25.0.7 (see "Session close: specialization-off status").
For cost, by a compile-only smoke step that records size and cold compile time, warns
and never fails a gate:
`python3 native/tools/unspecialized_compile_smoke.py --wgsl SURFACE.wgsl --output
native/work/<new-folder>`, where SURFACE.wgsl is the `surface.wgsl` written by a render
gate run with `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=0` (for example
`nodes_gate.py --unspecialized nodes_render`). Measurements are in that section.

**Device runs of the materials gates.** Runtime unchanged; the shipped
`libcodebrix_wgpu.so` was hash-verified before every corpus.
- Every run was on the full tier, decided by the table. The Samsung and the HP had no
  tier warning. The Pixel used its 20 s compile budget, as warnings only.
- Each device ran from scene-specialized modules.
- `principled_render` and `nodes_render` are judged against the Linux glibc export of
  the same scenes (Linux-prepared; render_gate has no scene exchange).
- The SVM, bounce and world gates have no reference exchange and compare against the
  device's own CPU.

| Gate | Samsung SM-X700 (Adreno 730) | HP (Intel Xe, Mesa 24.0.8) | Pixel 4 XL (Adreno 640) |
| --- | --- | --- | --- |
| principled_svm | device lost creating the probe pipeline | pass (237 cases, max 9.5e-7) | SVM error 8 |
| principled_bounce | device lost creating `prepare_surface` | pass (29 cases) | SVM error 8 |
| principled_render | device lost creating `prepare_surface` | pass, 0 practical vs glibc; on-device replay pass (322 readbacks, max 6.4e-6) | SVM error 8 |
| nodes_svm | typed 2D noise (fBM, variant 2): 1.792859 vs 1.791512, practical failure | pass (63 graphs, 417 cases, max 9.67e-5) | SVM error 8 and 16 |
| nodes_world | pass (22 worlds, max 6.10e-5) | pass (22 worlds, max 6.10e-5) | SVM error 8 |
| nodes_render | image component 6720 0.928481 vs glibc 0.928078, practical failure | pass, 0 practical vs glibc; on-device replay pass (320 readbacks, max 3.03e-4) | SVM error 8 and 16 |
| glass_render (specialized) | pass | pass | pass |
| specialization off | refused (exit 1) | refused (exit 1) | refused (exit 1) |
| managed app, 3 scenes | 0 practical, max 1.38e-5 | 0 practical, max 2.55e-5 | 0 practical, max 1.38e-5 (compile warnings) |

**Details.**
- On the HP each Principled bounce case recompiles `prepare_surface` (67-70 s). With the
  Mesa cache disabled the gate exceeds a 900 s job; with a per-job scratch cache (cold
  at the start, as on Linux) it passes in 979 s.
- The Pixel's E2 regression corpus passes on the merged tree against glibc, with
  replays: lighting_mis and feature_combination.
- On-device spec on/off identity cannot be measured, because spec off is refused on
  Android; bit-identity is shown on Linux.

**HP compile probes** (node render surface module, one fresh process each, cache
disabled):

| Module / entry | Time | Peak RSS |
| --- | --- | --- |
| specialized `prepare_surface` | 38.5 s | 1.43 GB |
| specialized `shade_surface_light` | 4.0 s | 0.71 GB |
| unspecialized `prepare_surface` | driver terminated (segmentation fault, about 2.58 GB RSS) | about 2.58 GB |
| unspecialized `shade_surface_light` | 40.5 s | 2.97 GB |
## Pixel 4 XL triangle sampler: GPU reset, not an out-of-bounds store (Phase E5, 2026-10-20)

The merged-tree Pixel run reported the triangle sampler gate (`codebrix_cycles_triangle_sample_gate`)
as a float written into the GPU error word. That reading was wrong. A diagnostic copy of the gate
(work folder only) timed the dispatch and dumped the output buffer and error word.
- The gate dispatches all 4,096 probe invocations at workgroup size 1 in one submission. On the
  Adreno 640 this runs for about 4 s. The submission ended after 2.05 s, and the next submission
  reported `Parent device is lost`. The error-word value read back from the lost device is not data
  that any shader wrote: it changed between runs (0x3F2823AA in the merge run, 0x3F07024E in three runs here),
  and it matches no value the probe writes.
- The same WGSL, unchanged, completes when dispatched in slices: 64 invocations take 106 ms, slices
  of 256 take 220-300 ms each (4.0 s in total), and slices of 1,024 and 2,048 take 1.0 s and 2.0 s.
  The error word stays 0, the guards are intact, and every sample is written.
- The output does not depend on how the dispatch is sliced, or on the form of E1's triangle-light
  cache. The array-of-structs cache, a struct-of-arrays copy and a copy with no cache give
  bit-identical outputs. The struct-of-arrays copy takes the same time. The copy with no cache is
  about 25% faster, still over 3 s for one dispatch. So the Adreno 640 array defect class (see
  "Pixel 4 XL (Adreno 640) function-scope array fix") is not involved.

Fix (test harness only; no shader, kernel or production change): the triangle variant of
`native/cycles/emitter_sample_gate.cpp` dispatches the probe in slices of 256 invocations. Each
slice is synchronized and its error word checked before the next. The slice start (`p[30]`) only
offsets the invocation index, so every invocation computes exactly what it did in one dispatch. The
emitter (lamp) probe still uses one dispatch. On Linux the sampler, emitter and triangle-image
outputs are bit-identical to the merged tree.

Pixel result (full tier by table, 20 s budget, glibc reference from the device's own scene):
- The sampler now runs to completion. The static, instanced and moving emitters pass: 0 practical,
  734 strict, max 4.88e-4. The 42 practical differences (25 samples, max 0.743) are all on the
  deforming emitter (object 5), the ARM64 deforming-emitter difference the Samsung also showed (24 of
  the 25 samples). The reference oracle's arithmetic (Phase E4) resolves it on the merged tree.
- The emitter probe passes (0 practical, 125 strict, max 9.06e-6).
- Triangle images 0-3 passed on the Phase E5 tree; modes 4 and 5 pass since Phase E4 (see the next
  section). After the session-close merge the deforming-emitter samples pass too (the sampler gate
  uses the reference oracle's arithmetic, Phase E4), so the whole Pixel sampler passes.
- As a check on E2's fix, feature_combination (0 practical, 1,300 strict) and the object-moving
  motion image pass, with one replay each. Deterministic films are bit-identical to the merged-tree
  Pixel run. Only feature_combination case 5 differs, by 4 values of up to 1.19e-7; that case's own
  gpu and repeat films also differ within each run.
## Static instanced emitters on the Adreno: ShaderData arithmetic and the reference oracle (Phase E4, 2026-10-20)

Triangle image mode 4 (static instanced emitters) failed on the Samsung SM-X700 and the Pixel 4 XL
with the same values. Two separate causes were found; both are fixed.

- **Contractible ShaderData arithmetic (all GPUs).** Replaying the Linux x64 mode-4 capture on the
  Samsung with arena dumps after each dispatch, and with the Linux arena injected before
  `prepare_shader_data` (command 433), localised the divergence at command 438 (queue count 343 on
  Linux, 342 on the Adreno). One light sample of path 127 lies exactly on a vertex of the instanced
  emitter, and `c_ray_triangle_intersect` hits it or misses it depending on one ULP of ShaderData
  `P.z`. With identical inputs, `prepare_shader_data` produced different `P` on Intel and Adreno:
  `sd.P=(a+u*dPdu)+v*dPdv` and the static object transform (`surface_transform_point`) were plain
  WGSL products and sums, which drivers may fuse; the transform also grouped the translation
  differently from the CPU oracle. Intel Mesa fuses them; the Adreno result already matched the
  unfused oracle. `wavefront_shader_data.wgsl` now rounds every product before its sum and uses the
  oracle grouping of the closest-stage instance push (SSE `((w+z)+y)+x`, scalar `(x+y)+(z+w)`).
  After the change `P` is bit-identical on Intel and the Adreno at command 433, and every Linux
  triangle image moved toward the CPU film (mode 4: 77 -> 10 strict, maximum 3.31e-5 -> 3.73e-7).
  Probe variants that rewrite the triangle-light cache and key arrays (the Adreno 640 array class)
  did not change the replay, so the compiler defect is not involved here.
- **Android ARM64 emitted its own scalar oracle's adapters.** The native device images were judged
  against the Linux x64 glibc reference, but the Android ARM64 build is a scalar build: its layout
  emits `CAMERA_NATIVE_VECTOR_DIVIDE` 0, `NATIVE_FLOAT3_RECIPROCAL_NORMALIZE` 1,
  `NATIVE_TRIANGLE_ARITHMETIC` 0, `GGX_NATIVE_RECIPROCAL_NORMALIZE` 1 and
  `TRIANGLE_LIGHT_NATIVE_SSE` 0 (not mode 2 as recorded earlier). The Adreno therefore reproduced the
  device's ARM64 CPU (mode 4 component 312: GPU 0.0646473, device CPU 0.0646473, glibc 0.0658437).
  Gates whose every check uses the imported reference (`lighting_gate.cpp` images and the
  `emitter_sample_gate.cpp` samplers) now call `reference_policy::use_reference_oracle_arithmetic()`;
  on Android with `CODEBRIX_CYCLES_REFERENCE_IMPORT` this sets
  `CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2`, and `reference_oracle_sse()` (camera.cpp)
  makes an Android scalar build emit the x64 SSE4.2 oracle's five constants (triangle mode 1, the
  verified `RCPSS` model). Gates with local lockstep checks (`render_gate.cpp`) and the product keep
  the device's own oracle; selecting the x64 constants process-wide made the Samsung `glass_render`
  lockstep stage check fail against the local CPU (`path.mis_ray_pdf`), which is why the selection
  is per gate. Linux x64 and Android x64 builds are unchanged (SSE builds always use the SSE oracle).

Results (full tier by table, device-scene glibc reference, bionic CPU a diagnostic):
- Samsung SM-X700: `triangle_lighting` all six images pass (mode 4: 0 practical, 11 strict, max 3.73e-7; it
  was 9 practical, max 0.0127) with all six on-device replays; the six Linux triangle captures replay (tri4: 415
  readbacks, max 3.73e-7) and the ten terminator captures replay. Strict counts fell in every image corpus
  (broad lighting 2,184 -> 993, transparent 978 -> 693, lighting_mis 1,188 -> 1,070, motion 1,461/1,484 ->
  1,306/1,258, terminator 195 -> 124). Regression, emitter probe, precision fixtures (local CPU, 0 mismatches),
  volume and the specialization-off refusal pass. The sampler improves from 41 to 2 practical (open item 1).
- Linux x64: the triangle images move toward the CPU (2,076 -> 1,776 strict; mode 4 77 -> 10, maximum 3.31e-5 ->
  3.73e-7); every other gate passes with strict counts within a few values of before. The glass_render
  specialization on/off film is `db8e84f9...` for both (was `a072683c...`).
- Pixel 4 XL (confirmation, 20 s compile budget as warnings): all six triangle images pass (mode 4: 8 strict,
  max 3.58e-7). The sampler's error mask was a GPU reset (Phase E5, previous section); on the merged tree the
  Pixel sampler passes against glibc.
- The device's bionic CPU film is unchanged; for mode 4 it is itself 6 practical away from glibc, so on-device
  replays of image captures are judged against the same glibc reference as their source gate.

## Session close: the E4/E5/M4 merge on the devices (2026-10-20)

Merged tree: the materials merge plus Phase E4 (ShaderData arithmetic and the reference oracle), Phase E5
(sliced sampler dispatch) and the bump/derivative opcodes. Runtime unchanged; the shipped
`libcodebrix_wgpu.so` was hash-verified before every corpus. Every run was on the full tier, decided by the
table, with no tier change; adapter strings match the table rows. Image and sampler gates are judged against
the glibc reference computed from the device's own scene, and on-device replays of image captures are judged
against the same reference as their source gate (the bionic CPU film is a diagnostic). The SVM, world and
state gates compare against the device's own CPU.

| Gate | HP (Intel Xe, Mesa 24.0.8) | Samsung SM-X700 (Adreno 730) | Pixel 4 XL (Adreno 640) |
| --- | --- | --- | --- |
| triangle sampler | pass (0 practical, 865 strict) | 2 values in sample 3133 (open item 1) | pass (0 practical, 865 strict) |
| triangle images, modes 0-5, and 6 on-device replays | pass | pass | pass |
| Linux triangle (6) and terminator (10) captures | pass | pass | not run |
| emitter probe | pass | pass | pass |
| broad lighting, lighting_mis, transparent, terminator, regression | pass | not rerun | feature_combination pass |
| object-moving images | not rerun | not rerun | SVM error 8 (open item 2) |
| specialization off | refused (exit 1) | refused (not rerun) | refused (not rerun) |
| nodes_svm | pass (65 graphs) | the typed 2D noise program of item 9 | device lost creating the pipeline |
| nodes_world | pass (23 worlds) | pass (23 worlds) | SVM error 8 |
| nodes_render | pass, 0 practical vs glibc; replay pass | image component 6720 (item 9) | SVM error 8 and 16 |
| managed app, 3 scenes | 0 practical | 0 practical | 0 practical (compile warnings) |

The bump and derivative opcodes leave the Adreno 730 results unchanged (same noise program, same render
pixel) and the Adreno 640 still fails every node gate.

**Pixel 4 XL object motion under the reference oracle's arithmetic.** The object-moving motion images fail on
the Pixel with SVM error 8 when the gate runs with an imported reference. That is the only case in which
`lighting_gate.cpp` selects the x64 oracle's arithmetic adapters on an Android scalar build (Phase E4). Diagnostic
runs of the same image gate, with the same staged libraries:

| Binary | Default (device oracle) | `CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2` |
| --- | --- | --- |
| merged tree | pass | SVM error 8 |
| Phase E4 sources without the materials work | pass | SVM error 8 |

So the failure follows the x64 adapter constants on the Adreno 640 (the materials code is not involved). The
product and the gates with local checks keep the device's own oracle and pass. Phase E4 validated the adapters
on the Pixel for the triangle images only.

**Specialization-off status.** The bump and derivative handlers make the unspecialized modules larger than
Intel Mesa 25.0.7 handles reliably, so the render gates are validated with specialization on. The Linux
specialization-off runs were made once each, with a declared budget:

| Render | Result | Unspecialized `prepare_surface` cold compile |
| --- | --- | --- |
| glass_render | pass; film bit-identical to specialization on | 649 s |
| principled_render | SVM error 8 during the render (wrong evaluation) | 607 s |
| nodes_render | pass; films bit-identical to specialization on | 617 s |

`nodes_gate.py` therefore runs only `nodes_world` unspecialized by default (`--unspecialized` selects
others). The compile smoke step measured the unspecialized node-render surface module:
- `prepare_surface`: 3,447,736 inlined bytes (2.3 MB is the Intel Android danger zone), 604 s cold, 10.0 GB peak;
- `shade_surface_light`: 1,578,630 bytes, 24 s.

Both are warnings only. Restoring a reliable specialization-off check was open item 11; lane L restored it
with the superset groups ("Linux reference fixes on the updated Mesa").

## Linux reference fixes on the updated Mesa (lane L, 2026-10-22)

Linux x64 is a reference device (device reference policy, 2026-10-22): every
item must work there, with no driver exception. Lane L resolved open items 4,
11, 12 and 13 on Linux.

**Driver identity.** `vulkaninfo`: `Intel(R) UHD Graphics (ADL-S GT1)`, driverInfo
`Mesa 25.0.7-2+deb13u1`, driverVersion 25.0.7, apiVersion 1.4.305. This is the
identity recorded at session close (2026-10-20, every `adapter.txt` of the merge
runs). The package `mesa-vulkan-drivers 25.0.7-2+deb13u1` was installed on
2026-07-11; the apt run of 2026-10-22 changed no Mesa package.
`libvulkan_intel.so` sha256 `c40c2c5002eda54fbe4106339fbc64784a03c6b0b1f6291c0e10386e1c298f85`.
Kernel 6.12.113 (also the session-close kernel).

**Root cause (items 4, 11 and 12).** Mesa 25.0.7's Intel backend pass
`brw_opt_combine_constants` stores a promoted constant's virtual register (VGRF)
number and its first and last use instruction numbers (IPs) in 16-bit fields.
Upstream Mesa widened them to 32 bits in commit b7e1ac830984 (2025-09-18, issue
#9489, "In very large shaders, first_use_ip, last_use_ip, and even (register) nr
can overflow 16 bits"); 25.0.7 does not have it.
- When the VGRF number wraps, `deallocate_slots` finds no matching register and
  reaches `unreachable()`. In a release build it then reads past its array. It
  crashes when the read hits unmapped memory and corrupts register bookkeeping
  when it does not. This is the "intermittent" SIGSEGV of item 4: the E3 crash
  register number 13,056 is 78,592 modulo 65,536.
- When the IPs wrap, the constant's load can be placed after some of its uses
  (wrong code without a crash).
- Every entry point whose backend IR has more than 65,535 instructions or
  virtual registers when the pass runs is therefore at risk.

Measurement: gdb with a breakpoint on `brw_opt_combine_constants` (Mesa debug
information from the debuginfod cache) records the VGRF count and the last
instruction IP of every compile. `native/tools/intel_compile_envelope.py` does
this for every entry point of the modules a gate dumps
(`CODEBRIX_CYCLES_WGSL_DUMP=DIR`, a new diagnostic). The inlined WGSL size
(`unspecialized_compile_smoke.py` `inlined_bytes`) is about 15.5 to 19.5 bytes per
backend instruction.

| Entry (scene) | Before (IP / VGRF) | After (IP / VGRF) |
| --- | --- | --- |
| `shade_surface_light` (ABI test, point + triangle MIS) | 60,590 / 41,024 | 44,485 / 30,340 (lamp) and 42,065 / 28,213 (triangle stage) |
| `shade_surface_light` (+ area light) | 75,734 / 51,344 | as above |
| `shade_surface_light` (+ area light + object motion) | 85,885 / 58,120 | as above |
| `emitter_sample_probe` | 44,416 / 30,347 | unchanged |
| `prepare_surface`, node render with Gabor (68 triangles) | 64,996 / 40,956 | 51,579 / 31,892 |
| `prepare_surface`, Principled render | not measured | 43,235 / 28,044 |
| `shade_surface`, Principled render | not measured | 52,509 / 35,763 |
| `prepare_surface`, superset of item 11 compiled whole (Principled scene) | 136,293 / 86,119 | replaced by superset groups |
| `prepare_surface`, unspecialized | about 190,000 (3.47 MB inlined) | diagnostic only |

**Fixes.**
- *Light-stage split (item 4).* With emissive triangles in the scene
  (`LIGHT_TRIANGLE`), `shade_surface_light` samples only a selected lamp (record
  state 1 -> 5). A new entry, `shade_surface_light_triangle`, dispatched next,
  prefills the triangle vertices, computes the forward-hit MIS weight and samples
  a selected triangle (5 -> 3).
  - Both recompute the same inputs from the unchanged stage record and path
    state, and call the same functions as the combined path, which scenes
    without emissive triangles still use.
  - Recorder v8 adds pipeline entry 17. `render_replay` accepts v1 to v8.
  - The triangle lighting gate gives every numeric field of the session-close
    run (sampler, six images, six replays); only the new entry's dispatch counts
    are added.
- *Horner loops.* The 29-step `motion_acos` and the 7- and 8-step
  `precise_disk_sincos` polynomials run as loops over their coefficient pairs:
  the same operations in the same order, so results are bit-identical. The glass
  film `db8e84f9...` and the Principled film `fd3a7f2b...` are unchanged.
- *ABI test.* The GPU ABI test renders the area light on WebGPU again (point,
  spot, area and sun in one device lifetime; the area light is followed by the
  object-motion recompile). The host gate compares the four light films with the
  CPU films. The practical criterion is required and strict differences are
  recorded:
  - point and spot: about 125 and 22 (sphere-light sampling in the last bits);
  - area: 0 or 1 (the WebGPU film's atomic accumulation order varies between
    runs);
  - sun: 0.
- *Superset specialization-off mode (item 11).*
  `CODEBRIX_CYCLES_SHADER_SPECIALIZATION=superset:G` selects every region except
  the bump/derivative and Gabor regions the scene does not reach
  (`SF_SUPERSET_SCENE_ONLY`). Compiled whole, that superset is about twice the
  16-bit range for `prepare_surface`. The device therefore compiles it in
  groups:
  - Group G is the scene plus the G-th greedy fill of the remaining superset
    regions (ascending bit order).
  - Each fill keeps every surface and background entry within 1,000,000 inlined
    bytes (`CODEBRIX_CYCLES_SUPERSET_BUDGET_BYTES`), or within the scene's own
    size when that is larger.
  - Regions that do not fit next to the scene are reported as `supersetUnfit`.
    Other gate scenes cover them.
  - Pipeline logs and reports carry `specializationMode`, `supersetGroup`,
    `supersetGroups`, `supersetGroupAdded` and `supersetUnfit`.

  `native/tools/specialization_check.py` runs every group and requires every film
  to be bit-identical to the specialized run. glass_gate.py and
  principled_gate.py (their render kinds) and nodes_gate.py (nodes_world,
  nodes_render) use it by default (`--off-mode superset`; `unspecialized` keeps
  the whole-module diagnostic, `none` skips). `=0` and `superset` are refused on
  Android like `=0` before.
- *Gabor (item 12).* `NODE_TEX_GABOR` ships on the scene-specialized path:
  - Feature bit 63 (`SF_TEX_GABOR`; only bit 62 stays free). It is a scene-only
    region in the superset mode.
  - The M3 precise-trigonometry WGSL body, census and layout.
  - Node SVM gate: compiled graphs 65 (2D, per-path orientation, anisotropy 0.6)
    and 66 (3D isotropic); 12 typed programs (2D and 3D; anisotropic, partly
    isotropic and isotropic; constant and per-path inputs; frequency clamp;
    unused outputs); rejection 20 is now an invalid Gabor type (16).
  - World gate: world 23 (3D anisotropic).
  - The late graphs of the node SVM fixture take 5 rays each (7 before) to stay
    within the state capacity.
  - With Gabor present the node-render `prepare_surface` was at 64,996 backend
    instructions, 0.8% below the 16-bit range, which is why its large modules
    mis-evaluated. The Horner loops bring it to 51,579.
- *Node render scene (item 13).* The render gate scene has 68 triangles again:
  the two Gabor graphs and six repeated continuous graphs follow M2's 60 slots.
  - It passes against the independent CPU image. The worst image component is at
    0.126 of the practical bound (component 3162), the same component and value
    as the 60-triangle scene.
  - The M3 failure (one pixel at 1.25 times the bound) was measured on a tree
    without the exact traversal (X2) and the contraction-proof ShaderData (E4).
    On the current tree the extra triangles move no visible component near the
    bound. (Inference: the original pixel's path took a last-bit hit-point
    difference that X2/E4 removed; that tree's arithmetic no longer exists to
    re-isolate.)
  - The node render film changes with the scene: `94848a0cddf9c42a...` (60
    triangles) becomes `b9d1da3fe595962cadc02925ab10378c099db2bec1c4f80993a3bb1012c419ce`.
    Its image maximum error stays at 1.03e-4.

**Results (Linux, cold Mesa caches).**

| Check | Result | Folder (`native/work/l/validation/`) |
| --- | --- | --- |
| Envelope, ABI modules (28 entries) | every entry below 60,000; worst 44,485 instructions / 30,340 VGRFs | `env-abi` |
| Envelope, material and superset modules (83 entries) | every entry below 60,000; worst 52,509 / 35,763 | `env-materials` |
| All-lanes coordinator (29 targets) | PASS, 1,256 s. 1,729 of 1,918 report fields equal session close. The rest: the new entry-17 dispatch counts, the superset records, and run-to-run atomic-order counters (broad lighting strict 3,572 to 3,569; broad_light_state strict 13 at session close, 11 here, 12 and 12 on two reruns) | `f3-all-lanes` |
| Host gate | PASS: 48 exports, 786 reference films, 3,146 comparisons (4 new light films), 0 practical failures, 148 strict differences (point/spot light films), max 1.53e-5 | `f4-host-gate` |
| Triangle lighting gate | PASS; every report field equal to session close | `f3-triangle` |
| Shadow terminator gate | PASS; every field equal to session close | `f3-terminator` |
| Scene-edit gate | PASS. GPU strict counts vary run to run, as within one run (the identical "before" scenes give 371 to 375) | `f4-scene-edit` |
| principled_gate.py (+ 8 superset groups) | PASS; every field equal to session close; film `fd3a7f2b...` | `v2-principled-gate` |
| nodes_gate.py (+ superset nodes_world 6 groups, nodes_render 1) | PASS: 67 graphs, 541 cases, 32 rejections; 24 worlds; 68-triangle render image max 1.03e-4 | `f3-nodes-gate` |
| Glass superset (4 groups) | bit-identical; glass film `db8e84f9...` unchanged | `s-glass-superset`, `f3-all-lanes/out/glass_gate` |
| ABI test GPU, 10 cold runs (area light on WebGPU) / emitter probe, 10 cold runs | 10/10 / 10/10 | `f3-abi-*`, `f3-emit-*` |
| dotnet build / test / pack | 0 warnings, 0 errors / 162 of 162 / package created | `f3-dotnet-*` |
| audit.py, notices.py --check, inventory, guards | PASS | `f3-audits` |

**Compile time and memory** (one fresh process per entry, cold cache, peak RSS
from `compile_probe`):

| Module / entry | Instructions / VGRFs | Cold pipeline | Peak RSS |
| --- | --- | --- | --- |
| Principled render, scene-specialized `prepare_surface` | 43,235 / 28,044 | 25.2 s | 1.27 GB |
| Principled superset group 0 `prepare_surface` | 48,577 / 31,160 | 30.6 s | 1.45 GB |
| Unspecialized `prepare_surface` | about 190,000 (estimate) | 1,237 s (with two gates running; 604 s at session close) | 10.26 GB |
| Item-11 superset compiled whole, `prepare_surface` | 136,293 / 86,119 | 143 s (in the render, under gdb) | not measured |

A whole superset render group compiles in 19 to 67 s (its largest pipeline takes 16.5 to 21.3 s). Glass needs
4 groups, Principled 8, nodes_world 6 (over its 24 worlds) and nodes_render 1. Together the groups place
every superset region next to at least one gate scene: the glass groups leave no region unfit.

**Re-establishment on 2026-10-22 (same driver).**
- Item 4: 0 of 10 cold ABI runs (area light on WebGPU, baseline WGSL) and 0 of 10
  cold emitter-probe runs crashed. The baseline light stage nevertheless compiled
  at 75,734 and 85,885 instructions, inside the overflow range. The crash
  depends on heap contents, so its absence in 10 runs is not a fix; the split is.
- Item 11: the item-11 superset compiled whole passed the Principled render once
  (143 s `prepare_surface`), but at 136,293 instructions it is inside the
  overflow range, like the unspecialized modules.
- Item 13: the 68-triangle scene passes on the current tree (before any lane-L
  arithmetic change).

## Samsung reference fixes (lane S, 2026-10-22)

Device reference policy (2026-10-22): the Samsung SM-X700 (Adreno 730) is the ARM64/Adreno reference device. This lane
worked on open items 1, 8 and 9, reran the object-moving motion images on the Samsung and measured item 3 there. Runtime
unchanged (shipped `libcodebrix_wgpu.so` arm64 `8113cbfa...` hash-verified before every corpus); full tier by table.

**Item 8, Principled pipelines on the Adreno 730: root cause and fix.** Creating `prepare_surface` (and the Principled
SVM probe) failed in the Adreno driver (`AdrenoVK-0: Failed to link shaders` in logcat; wgpu then reports `Parent device
is lost`). A compile-only probe on the Samsung (`native/tools/compile_probe.cpp` built for android-arm64) reproduces it
in 16 s from the specialized Principled render module. Bisection (about 45 variants): the failure needs the thin-film
Fresnel path and the thin-wall path to be live at the same time, but it is not module size (removing the Glass handler or
anisotropy still fails), not workgroup size (64/32/16/1 all fail), not WGSL runtime checks, not dynamic vector indexing
and not loop nesting. It is per-invocation private memory: with the 263-word module-scope `svm_stack` shrunk to 128 words
the module links, at 160 words it fails, and with the stack in storage memory it links. The register spills of the large
Principled handler plus the private SVM stack exceed what the driver can allocate per invocation.

Fix: the SVM value stack and its initialization mask now live in a per-invocation slot of the device arena
(`wavefront_svm.wgsl`). `WebGPUDevice` allocates 4,096 slots of 263 words (`num_concurrent_states` is at most 4,096) and
writes the region address and slot count after the error word (`p(17)+4`, `p(17)+8`). Every entry point of the modules
that contain the SVM interpreter (background, light forward, shadow, surface, volume, the SVM and emitter probes) names
its invocation first (`svm_invocation(id.x)`); an evaluation without a valid slot reports error 1 rather than sharing
memory. Reads, mask checks, error codes and results are unchanged: on Linux the glass film (`db8e84f9...`) and the
Principled film (`fd3a7f2b...`) are bit-identical. This also keeps the large stack out of private memory, the class of
the Adreno 640 function-scope array defect.

Results on the Samsung: `principled_svm` PASS (126 s) and `principled_bounce` PASS (320 s, against the device CPU as
before). `principled_render` now compiles and runs, but fails a local lockstep stage check: stage 40, path 2441,
`path.mis_ray_pdf` 1751.469 against the device CPU's 1747.274 (open item 8 below). Making the GGX masking terms, the sheen
LTC value, `principled_rotate` and the anisotropy aspect contraction-proof (rounded products before sums, operation
order as the pinned sources) leaves the Samsung value unchanged and makes Linux produce the same 1751.470 and fail the same
check: the Adreno computes the unfused arithmetic, and Linux agrees with the CPU only because Intel Mesa fuses those
products. That experiment was reverted (Linux films unchanged); the mismatch is a WGSL-versus-CPU difference to localise
on Linux with the contraction-proof forms applied.

**Device-scene references for render and SVM probe gates.** `render_gate.cpp` images and the SVM probe gates
(`surface_svm_test.h`, all `svm_surface_gate.cpp` variants) now follow the device-scene flow of the validation reference
policy when the `CODEBRIX_CYCLES_SCENE_*`/`CODEBRIX_CYCLES_REFERENCE_*` variables are set (no change without them). The
SVM probes exchange one key per evaluated case (`svm-N`): the scene tables other than the program are written once per
distinct content (`svm-base-N.scene`), each case file carries the program, `KernelData`, the selected path indices and
their path/ray/intersection state; the dump and Linux passes do no GPU work and skip the GPU-only rejection checks. The
device-prepared Principled scene differs from the Linux-prepared one in the BVH, 1,143 lookup-table words, 54 vertex words
and the object records, so Linux-prepared references are not valid inputs for the Samsung.

**Item 9 under the device-scene flow.** `nodes_world` PASS (23 worlds, 378 s). `nodes_svm` against the glibc records of
the Samsung's own scene: the same typed 2D noise program (case `svm-45`, "noise 2d type 0 variant 2") fails at path 48
(0.566344 against 0.567463); the device CPU (scalar ARM64 noise branch) also differs from glibc in that case (6 practical,
max 1.35e-3). `nodes_render` against the glibc film of the Samsung's own scene: component 6720 0.928481 against 0.928078,
the same glibc value as the Linux-prepared film, while the Samsung GPU and the Samsung CPU agree (0.928481/0.928485): the
pixel follows the device-own arithmetic adapters that `render_gate.cpp` keeps for its local lockstep checks, not the
scene preparation.

**Item 9 localised: the Adreno 730 reassociates the Perlin fade products.** The failing typed noise case was captured
on Linux (arena, parameters, specialized probe module) and replayed with `arena_replay` on both GPUs: from identical
inputs Intel reproduces the CPU record bit for bit (path 48: 0.5674633) and the Adreno gives 0.5663444 (41 of 51 paths
differ). Replaying WGSL variants on the Samsung with opaque ordering barriers (an XOR with a word that is always zero)
around single expressions isolates it to `svmn_perlin`'s fade, `(t*t)*(t*inner)`: with only that expression ordered the
Samsung matches the CPU at path 48 and every value of the case is within 0.001 of the practical bound (before: 6 values
above it, ratio up to 6.8). The fade now goes through `svmn_order` with header word 3 after the error word, which the
device keeps at zero. Linux node gates and the glass film are unchanged (`db8e84f9...`). On the Samsung the nodes_svm gate (device-scene glibc reference) now passes 512 cases, including the formerly failing typed 2D noise case, and then stops: the Vulkan driver terminates (SIGSEGV) while creating the probe pipeline for the case after "vector math derivative 29" (case 489): the cumulative scene-specialized probe module, never reached on the Samsung before (open). nodes_world passes (376 s); nodes_render still fails at component 6720 (render_gate's device-own adapters, see above).

**Item 1.** Rounding the motion step fraction (`time*(steps-1)-step`, contractible in three places) does not change sample
3133; reverted. The failing sample is on the deforming emitter (object 5, prim 4): GPU P.y 0.382186, glibc 0.379992, device
CPU 0.380744 (the device CPU is itself a practical failure there); the pdf differs by 1 ulp, so the spherical-triangle
sampling amplifies a one-ulp difference upstream of the sample.

**Object-moving motion images on the Samsung (session-close "not rerun").** Both pass, with all eight on-device replays:
with the device's own oracle against the device CPU (moving and stationary images, 521 s corpus), and with the reference
oracle's arithmetic against the glibc reference of the Samsung's own scene (host re-judged PASS, 567 s corpus).

**Item 3 measured on the Adreno 730.** `dispatch_timing` (a copy of `render_replay` in the lane's work folder that submits
every dispatch alone and waits for it) replayed the Samsung's triangle mode-4 and mode-5 captures and a Linux Principled
render capture on the Samsung. Longest single submissions: 12.4 ms (mode 4, `shade_surface_light`, 415 invocations), 22.4
ms (mode 5, `shade_surface_light`, 399 invocations, about 0.056 ms per invocation), 29.3 ms (Principled `shade_surface`,
2,543 invocations); no reset or timeout. At 0.056 ms per invocation a full 4,096-path `shade_surface_light` dispatch is
about 0.23 s, roughly an order of magnitude below the Adreno 640's 2 s reset; no slicing is needed on the Adreno 730.

Evidence: lane S handoff (`native/work/s/handoff/` in the lane worktree; device and validation records, compile bisection
log, per-dispatch timings).


## Merge wave 4: lanes L, S and P on the devices (2026-10-22)

Tree: 3392df3d plus lane L (Intel Mesa envelope: light-stage split with `shade_surface_light_triangle`, recorder v8
entry 17, superset groups, Gabor on bit 63, 68-triangle node render), lane S (SVM stack in a per-invocation arena slot,
noise fade ordering barrier, device-scene reference flow for `render_gate.cpp` images and the SVM probe gates) and
lane P (docs). Runtime unchanged (shipped `libcodebrix_wgpu.so` hash-verified before every corpus); full tier by table
on every device. Image, sampler, render and SVM-probe gates are judged against the glibc reference computed from the
device's own dumped scene; replays of image captures are judged against the same reference; state gates against the
device's own CPU. Evidence: `native/work/merge4/runs/<device>/`, tables in `native/work/merge4/device/device-tables.md`.

| Gate | Samsung SM-X700 (reference) | HP (reference, Mesa 24.0.8) |
| --- | --- | --- |
| triangle sampler | sample 3133 (open item 1) | pass |
| triangle images 0-5 + 6 replays (v8 captures) | pass | pass |
| Linux triangle (6, v8) and terminator (10) captures | pass | pass |
| emitter probe | pass | pass |
| broad lighting, lighting_mis + 4 replays | pass | pass |
| regression (glass, lighting_mis, transparent lighting) | pass | pass |
| transparent shadow + lighting + 10 replays | pass | pass |
| shadow terminator + 10 replays | pass | pass |
| specialization refusals (`=0` and `superset`) | refused (exit 1); glass spec on passes | refused (exit 1); glass spec on passes |
| object-moving/stationary images, own oracle and x64 oracle, 8 + 8 replays | pass | not run |
| principled_svm / principled_bounce | pass / pass | pass / pass |
| principled_render | stage 40 `mis_ray_pdf` (open item 2) | pass, replay pass |
| nodes_svm (67 graphs incl. Gabor) | compiled graph 44 mis-evaluates (open item 3) | pass |
| nodes_world (24 worlds incl. Gabor) | pass | pass |
| nodes_render (68 triangles) | component 6720 (open item 4) | pass, replay pass |
| Android host gate (48 exports) | pass | pass |
| managed app, 3 scenes | pass | pass |

**No HP exception is needed.** Every HP job passed, so no Mesa 24.0.8 or Bliss OS determination was required. The
specialization diagnostics (`=0` and `superset`) are refused on Android by design (the device constructor throws
before any compile), which the refusal jobs verified.

**Samsung node SVM probe: Gabor in the module breaks graph 44 (new).** Lane S's run (without Gabor) passed the
compiled graphs and 512 typed cases. With lane L's Gabor graphs in the fixture, the scene-specialized probe module
contains the Gabor handler, and graph 44 (Fresnel and two Layer Weight nodes) gives a closure weight of 0.070653
against 0.079591 (glibc; the device CPU agrees with glibc) on 27 paths of all three objects. The captured dispatch
(`native/work/merge4/diag/svm309`) replayed with `arena_replay`: on Linux the merged module matches glibc exactly; on
the Samsung the merged module, a variant with lane S's unrolled Horner steps, and a variant with the Horner arrays
replaced by a switch lookup all fail identically; a variant with the Gabor handler body emptied makes graph 44 equal
to Linux and glibc exactly. So the Adreno 730 mis-evaluates Fresnel/Layer Weight when the Gabor handler is in the
same module; the Horner loop arrays are not involved. Not fixed in the merge.

**Pixel 4 XL (Adreno 640).** Only the provisional materials jobs ran (see the lane P section): all five are not
supported. **Samsung Galaxy S20 FE 5G (Adreno 650).** Lane Q's failing jobs were rerun on the merged tree (timeout
max(600 s, 3 x this merge's Samsung wall)); the results are added to the lane Q section at the end.



## Samsung follow-up fixes (lane S2, 2026-10-22)

Samsung SM-X700 (Adreno 730), the ARM64/Adreno reference device. Runtime unchanged (shipped `libcodebrix_wgpu.so`
hash-verified before every corpus); full tier by table. Images, sampler and SVM probes are judged against the glibc
reference of the device's own dumped scene. Diagnosis used `arena_replay` of dispatches captured on the Samsung,
replayed with WGSL variants on both GPUs; the opt-in capture switches added for this are
`CODEBRIX_SURFACE_DISPATCH_STAGE` and `CODEBRIX_SURFACE_DISPATCH_ENTRY` (render_gate, any lockstep stage and the
`prepare_surface` dispatch), `CODEBRIX_EMITTER_PROBE_CAPTURE` (sampler probe slice), `CODEBRIX_SVM_PROBE_DUMP` (every
SVM probe module) and `CODEBRIX_SURFACE_ORACLE_INPUT` (this host's CPU surface oracle on captured path states).

**Node SVM graph 44 with the Gabor handler (open item 3(a)): FIXED.** Probe variants that store one intermediate of
`svm_fresnel_node` showed `backfacing` computed correctly but `eta` left at the IOR on the Adreno 730: with the Gabor
handler in the module the compiler dropped the nested `if(backfacing){eta=1.0/eta;}`. A minimal Gabor body (its input
loads, arithmetic and three stores) is enough to trigger it; loads alone or stores alone are not. The reciprocal is now
chosen with `select` (same operations): the Samsung replay of the captured dispatch has no practical failure, and the
Linux records are bit-identical to before (node render film `b9d1da3f...` unchanged).

**Node SVM driver crash after case 500 (open item 3(b)): FIXED.** The probe module that first contains the mapping
derivative region (case 501, "mapping derivative 0") terminated the Adreno 730 compiler (SIGSEGV, immediately) in
`compile_probe` too. Bisection: `svmn_dual_direction`, three `svmn_direction(mat3x3,...)` calls in one expression.
It now builds the three transform columns once and applies them (the same products and sums); the module links
(184 s). The full `nodes_svm` gate (541 cases, 67 graphs including Gabor) now passes on the Samsung (1,563 s on the final tree).

**Principled render stage 40 (open item 2): FIXED.** Path 2441 samples an anisotropic GGX lobe; the sharp coat lobes
(alpha 0.0025) evaluated at that direction make the MIS pdf ill-conditioned. Running this host's CPU oracle on the
captured stage input showed the x64 and ARM64 CPUs agree (1747.274); the GPU differed only in the closure tangent
`T` (one ULP in two components), from `principled_rotate`: (a) the quadrant-reduced angle was rounded to one float
before the disk sine/cosine, losing one ULP of `cos` against libm; (b) the rotation products were contractible, and
Intel and the Adreno contracted them differently. The reduction now keeps the two-part angle
(`precise_disk_sincos_pair`, which `precise_disk_sincos` calls with a zero residual, so its other callers are
unchanged) and the rotation rounds every product before its sum in `rotate_around_axis` order. A binary32 emulation
of the pinned function reproduces the CPU tangent exactly, and the replayed `prepare_surface` now gives it on Linux.
On the Samsung every Principled lockstep stage now passes; the render then stops at the image against glibc
(components 9114 and 9362, 1.546950 against 1.545995), where the GPU equals the device CPU image: the open item 4
class below. On Linux the Principled gate passes with fewer strict stage differences (946, before 1,025); its GPU film
changes to `e81feb12...` (glass `db8e84f9...` and node render `b9d1da3f...` unchanged).

**Node render component 6720 (open item 4): root cause shown, not fixed.** With
`CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2` (the reference oracle's arithmetic adapters) the Samsung node render
passes every lockstep stage and the image against glibc (component 6720 = 0.928078, the glibc value). The same
adapters make the Principled render fail its local lockstep at stage 1 (path 552, `mis_ray_pdf` 44.0036 against the
device CPU's 43.9791): the local stage checks then compare SSE-adapter GPU arithmetic with the scalar ARM64 CPU. So
the image follows the adapters, and fixing it under the policy needs the lockstep checks on the glibc flow as well
(open item 5).

**Triangle sampler sample 3133 (open item 1): localised, not fixed.** The sampler probe slice captured on the Samsung
and replayed on Linux Intel gives the glibc record bit for bit (P.y 0.3799915); the Samsung GPU gives 0.3821864 from
the same arena and module, so the deviation is in the Adreno 730's compilation of our WGSL, not in the dumped scene.
Probe stores trace the first difference to one ULP in the deforming instanced emitter's interpolated object transform
(row y, column x), amplified by the spherical-triangle sampling; storing intermediate values of the motion transform
makes the Samsung result correct, and ordering barriers on the quaternion products, the compose outputs, the
rotation, the interpolated rows, the motion time and the point products did not. The device's own ARM64 CPU also
misses the practical bound there (0.380744); that is a separate CPU question for the Raspberry Pi 5 glibc ARM64
reference (Phase F): evaluate sample 3133 of the dumped Samsung scene with the Pi 5 oracle and compare it with the x64
glibc record.


## Open device items (merge wave 4, 2026-10-22)

One list for the reference devices (Samsung SM-X700, Linux x64, HP). None is masked, and no tolerance was relaxed.
Old numbers (session close, 2026-10-20) are given in brackets. The Adreno 650 (S20) results are not open items of the
reference devices; see "Samsung Galaxy S20 FE 5G (Adreno 650) sweep" and "Adreno 650 lane" further below.

1. (resolved by lane S3, see "Resolved" below) Was: **Samsung triangle sampler** [1]. Sample 3133 on the deforming instanced emitter (object 5, prim 4): `P.y` 0.382186
   against the glibc 0.379992. Lane S2: the Samsung capture replayed on Linux Intel gives the glibc record bit for bit,
   so the deviation is the Adreno 730's compilation of our WGSL, entering as one ULP in the emitter's interpolated
   object transform (see "Samsung follow-up fixes (lane S2)"); not fixed. The device's own ARM64 CPU (0.380744) also
   misses the bound: the Raspberry Pi 5 glibc ARM64 oracle (Phase F) on the dumped Samsung scene decides whether that
   CPU difference is upstream ARM64 arithmetic. Reproducer: `run_corpus.py samsung triangle X 2400 900 --reference
   ../reference/linux-x64 --device-scene --jobs triangle_sample`.
2. (resolved by lane S2, see "Resolved" below)
3. (resolved by lane S2, see "Resolved" below)
4. (resolved by lane S3, see "Resolved" below) Was: **Samsung render images against glibc** [9]. Node render component 6720 (GPU 0.928481, glibc 0.928078) and, since
   lane S2's stage-40 fix, Principled render components 9114 and 9362 (1.546950 against 1.545995): in both the GPU
   equals the device CPU image. `render_gate.cpp` keeps the device's own arithmetic adapters for its local lockstep
   checks. With the reference oracle's adapters (`CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2`) the node render
   passes completely (lockstep and image), but the Principled render then fails its local lockstep at stage 1 against
   the scalar device CPU. Fixing it needs the lockstep stage checks on the glibc flow too (item 5); a design decision.
   Reproducer: `--jobs nodes_render,principled_render` as above.
5. **Checks not yet on the glibc reference** [5]. On the glibc flow (device-scene): the sampler arrays, the
   `lighting_gate.cpp` images, the SVM probe gates (`nodes_svm`, `principled_svm`) and, since lane S3, the
   `glass_render`, `principled_render` and `nodes_render` gates completely (image and every lockstep stage, through
   the stage-state exchange). Still judged against the device's own CPU: the state gates `surface`, `closest`,
   `shadow`, `shader_data`, `glass_svm`, `glass_bounce`, `microfacet_svm`, `microfacet_bounce`, `principled_bounce`,
   `nodes_world` (`svm_world`), `camera`, `volume`, `volume_boundary`, `light_state`, `broad_light_state` and
   `transparent_shadow`, and the MNEE state gate `mnee` (its images, `mnee_render`, take the device-scene flow).
   Since merge wave 6 the other `render_gate.cpp` binaries (`microfacet_render`, `render`
   static and with camera motion, `volume_render`, `closed_volume_render`) also run in the device-scene flow through
   the stage-state exchange (they pass on the Samsung and the HP). The managed-API scenes (`android_app_gate.py
   --mode webgpu` and `--mode bundle`) have no scene dump and stay outside the device-scene flow (judged against
   Linux-prepared glibc films). Follow-up: move the remaining state gates onto the glibc flow; the on-device CPU
   then judges nothing, which also settles whether it should be built at the oracle's fixed baseline (it keeps
   Cycles' flags for now).
6. **Not yet ported** [14]: true displacement (`NODE_SET_DISPLACEMENT`, displacement and vector displacement) and the
   bump-state branch that needs the undisplaced position; tangent and motion-triangle smooth-normal derivatives
   (rejected, error 16); `NODE_RADIAL_TILING`; AOV output; sky, IES, light falloff, bevel, raycast, volume
   coefficient and particle/hair/point info nodes.
7. **Feature mask** [15]. CLOSED (Step zero, 2026-10-22): the mask is 256 bits (`ShaderFeatures`, four 64-bit
   words); bits 0-61 and 63 keep their meaning, bit 62 and bits 64-255 are free. See "Step zero" below.
8. **Contractible ShaderData differentials** [16]. `du`, `dv` and `dP` still differ by ULPs between Intel and the
   Adreno; no current film depends on them.
9. (resolved by lane m1-oracle, see "Resolved" below) Was: **Milestone 1 bench against the Linux CPU oracle**. The
   bench with its original materials failed the practical criterion on Linux WGSL (47), the Samsung (51) and the HP
   (47 components of 12,288) against a Linux x64 CPU oracle built `-march=native` with FMA.
10. (resolved by lane m1-oracle, see "Resolved" below) Was: **Camera-ray last-bit parity**, and the factor-only
   bench's pixel (45,22) (3 components, max 0.0278).
11. (resolved by lane m1-lighting, see "Resolved" below) Was: **HDRI bench on WGSL** (`bench_01-hdri.json` was
   refused until environment importance sampling and MIS landed).
12. (resolved by lane m2-shrink for the bench and the HDRI bench, see "Resolved" below) Was: **Size-guard warnings on
   the bench graph** (HDRI bench `prepare_surface` 61,690 Intel backend instructions). Remaining: the textured-plane
   control's `prepare_surface` (1,114,357 bytes, 52,126 instructions) is 4,357 bytes above the size guard.
13. **First-render compile times** over the 60 s pipeline budget (warnings, the jobs pass). Since lane m2-shrink the
   bench's `prepare_surface` takes 71-74 s on the Samsung (was 116 s) and 123 s on the HP (was 147 s); first GPU
   renders bench 97 s and 189 s, HDRI bench 206 s and 270 s (were 177 s and 233 s, 344 s and 327 s). The Samsung
   `nodes_world` gate 98.3 s (merge wave 7). The HP's `principled_bounce` and `principled_render` (57-59 s) are
   within 5 percent of the budget.
14. **Texture-sampling last-bit parity.** Image texture lookups still differ from the CPU in the last bit (1,356
   components at 1 spp on the bench, max 3.6e-7). The bench's closest component is at 0.59 of the practical
   tolerance on Linux, and the HP against Linux WGSL at 0.91. Recommended next small kernel follow-up.
15. **Motion arithmetic last bits.** `motion_acos` rounds pi - acos(|x|) in two steps and is one ULP off for about
   9% of negative inputs; the shared decomposed-motion interpolation (`wavefront_motion.wgsl`) leaves strict
   differences with camera motion (1,616). Both are within the practical bound; fixing them changes motion films.
16. **macOS recipes at the fixed baseline.** The macOS arm64 and x64 recipes apply the fixed-baseline flags but are
   untested at the baseline (`native/host/MACOS_ARM64.md`, `native/host/MACOS_X64.md`).
17. (resolved, see "Resolved" below) Was: **Replays with shadow-path compaction.** The light-tree gate's case 2 (lamps and emissive triangles under an HDRI
   world, light tree on) does not replay: the native scheduler's shadow-path compaction (control op 2, modes 2 and 3)
   counts the active shadow paths above a slot index, and which slots those paths hold follows the order of the atomic
   shadow-slot allocation in `surface_direct_light`, which differs from run to run (Linux 54, 55, 57 and 59 at the
   same command). The films are unaffected (repeat differences are last-bit film accumulation only) and the case
   passes its image and probe checks on every device. What differs in the replay: only the 4-byte readbacks of the
   compaction counter after the shadow kernels (first at command 51 of the case-2 capture; recorded 54, replayed 55,
   57 or 59); the films, the light-tree probe, every lockstep state and every judgment are unaffected; the other six
   captures replay exactly. Pre-existing: the atomic shadow-slot allocator is unchanged code, and disabling the new
   `light_tree_forward` entry in a copy of the capture leaves the counts varying. Making the replay exact needs a
   deterministic shadow-slot reservation for every scene (the transparent-shadow path already has one); a design
   decision. Reproducer: run `codebrix_cycles_light_tree_gate OUT 2`, then `render_replay.run('linux-x64',
   OUT/case-2, DIR)` (`native/tools/render_replay.py`) twice. Merge wave 8: the ambient-occlusion stage reserves its shadow slots stably whenever AO is on
   (two reserved slots per surface input and the stable serial path-array scan; "Ambient occlusion"), which made its
   own replays exact; scenes without AO keep the atomic allocator unchanged, so this case still does not replay.
18. **Ambient occlusion remainder.** The AO node's `only_local` from an object without its own BVH in a scene that
   also has instanced objects is refused (error 16 when the local traversal reaches an instance leaf); ambient
   occlusion with object or deformation motion has no gate yet; on the Adreno 650 the AO node and the AO shadow rays
   are refused (see "Ambient occlusion"). Bevel and raycast nodes stay unported (item 6).
19. (resolved, see "Resolved" below) Was: **MNEE on the Adreno 730 (Samsung SM-X700).** The `INTERSECT_MNEE`
   dispatches stopped with the invalid-data error (mask 1) in `mnee` and `mnee_render`; the Adreno 650 failed the same way.
20. **MNEE with the light tree (refused).** Caustic scenes with the integrator's light tree, light or shadow
   linking, AO, volumes or motion blur are refused before rendering ("Manifold next-event estimation (caustics)");
   MNEE would call the light tree's selection in its own entry. Shadow linking is the next lighting work (Milestone 2,
   wave B); the light-tree combination is open with it.
21. (resolved in merge wave 8 part 1c, see "Resolved" below) Was: **Volume gate rejection text (merge wave 8).** The shadow catcher with volumes now has its own refusal ("WebGPU
   shadow catcher with volumes not implemented"); `codebrix_cycles_volume_gate` rejection case 9 still expects
   "optional volume integration", so the volume gate and the all-lanes `volume_boundary_gate` (its world regression)
   stop there on Linux. The refusal itself works; the gates after case 9 were not exercised in this wave.
22. (resolved in merge wave 8 part 1c, see "Resolved" below) Was: **Host gate managed reload on Linux (merge wave 8).** `tools/hosttests/ImageModes` reads
   `imageInput.fileReading` of the capability report as a Boolean, but the Linux host reports the string
   "development-only", so the managed-reload phase of `native/tools/host_gate.py` aborts (SIGABRT). Every other phase
   passes (1,586 comparisons, 0 practical, 151 strict); the managed-reload films were not compared in this wave.
23. **Shadow catcher with ambient occlusion (merge wave 8).** Not refused and not gated. In `shade_shadow` the AO
   branch (AO pass and fast GI's additive term) writes Combined directly and returns before the shadow catcher
   handling, while the pinned `film_write_direct_light` routes the additive term through `film_write_combined_pass`
   (shadow catcher split included). Scenes with both may differ from the CPU.
24. (resolved, see "Resolved" below) Was: **Light-tree gate driver stops at the known replay (merge wave 8).** `native/tools/light_tree_gate.py` replays the
   seven captures in order and stops at case 2 (item 17), so it exits 1 although the gate passes; replays 3-6 pass
   when run on their own.
25. **Two CPU gold standards differ on the bench (merge wave 8).** The macOS arm64 CPU films of the bench and HDRI
   bench (16 samples) differ from the Linux x64 CPU films in 32 and 67 components beyond the practical criterion
   (max 0.0042 and 0.012). With both CPU films gating, the WGSL bench and HDRI bench are "not perfect" against the
   arm64 CPU film while perfect against the Linux one; the four starter scenes are perfect against both. Resolved in
   the comparator (merge wave 8): a candidate gates against the CPU film of its own host ISA family (x64 for the Linux
   and Android WGSL backends) and cross-ISA pairs are reported, so the bench and HDRI bench read "perfect" with the
   Mac films present; the cross-ISA CPU difference itself stays recorded here (the macOS arm64 recipe is untested at
   the fixed baseline on this side, item 16).
26. **New compiler warning sites (merge wave 8).** `-Wmisleading-indentation` (a one-line `for`/`if` followed by a
   statement on the same line) in `ao_state_gate.cpp:187`, `light_tree_reference.cpp:56`,
   `shadow_catcher_gate.cpp:147`, `mnee_gate.cpp:262`, `mnee_reference.cpp:53,54,83` and `device.cpp:247`
   (`mnee_entry_of`).

Resolved, or closed under the device reference policy:
- [17 of this list] Replays with shadow-path compaction: RESOLVED (on the maintainer's go-ahead, 2026-10-29). Every
  surface dispatch of a scene with direct light (or ambient occlusion) now reserves one fixed shadow slot per input
  (control op 5, `surface.cpp`; two per input with AO, as before), and every path-array build of such a scene uses the
  stable serial scan (`queue.cpp`, `controls.wgsl` op 2 and 3). Both halves are needed: with the reservation alone the
  slot follows the order of the atomic work arrays (cases 1, 2, 5 and 6 then fail to replay), with the stable scans
  alone the atomic slot allocation in `surface_direct_light` remains (case 2 fails). Every reserved slot is one the
  pinned atomic allocator could hand out; unused reservations stay terminated, and the unmodified `PathTraceWorkGPU`
  makes every scheduling decision (it budgets one shadow path per input before the kernel, so the reservation never
  overflows). No WGSL changes: the modules of the light-tree, shadow catcher and AO image gates are byte-identical to
  before, and so are the size guard and the compile envelope. Lit scenes record different command streams (the same
  shading dispatches; more control dispatches and shadow compactions, e.g. the light-tree case-2 capture 279 -> 323
  control dispatches), and on Linux two captures of the same light-tree case are byte-identical. Results: all seven light-tree captures
  replay exactly on Linux, the Samsung SM-X700 and the HP (case 2: 540 scheduling readbacks on each); the light-tree,
  shadow catcher, AO state and image, transparent shadow and lighting, broad lighting, lighting MIS, feature
  combination, triangle lighting and glass gates, the host gate and the bench, HDRI bench, control, light-tree,
  shadow catcher and AO oracle scenes pass on Linux with the previous numbers apart from strict counts of the
  run-to-run atomic film family; films bit-identical except films of that family (light-tree cases 2 and 5, AO image
  cases 3, 4, 6 and 7, broad lighting 3/7/8/9, feature combination 5, lighting MIS 5, transparent lighting 2/4/5 and
  the Session WGSL films, all of which already differ between two runs of the previous build). The Samsung and the HP
  pass the light-tree, shadow catcher, AO and transparent corpora with all their replays against the glibc
  device-scene references. Cost: path-array builds of lit scenes become single-thread scans over the path capacity
  (at most 4,096 states); on Linux (Intel, Mesa 25.0.7) warm walls of small renders rise by about 15-25 % (light-tree
  gate 10.6-11.3 s -> 12.2-13.7 s; bench 64x48, 16 samples 1.07 -> 1.35 s; HDRI bench 1.46 -> 1.66 s); cold first
  renders are compile-dominated and unchanged. The warm cost on the Android devices was not measured. A parallel stable
  scan (`controls.wgsl`) would remove most of it.
- [24 of this list] Light-tree gate driver stops at the known replay: RESOLVED (same change). `light_tree_gate.py`
  replays every capture, records a failed replay in `results.json` and fails after the last one, naming the failed
  cases; with item 17 resolved it exits 0 with all seven replays.
- [19 of this list] MNEE on the Adreno 730: FIXED (equivalent WGSL in the MNEE region; "Manifold next-event estimation
  (caustics)", "Adreno 730 fix"). `mnee` and `mnee_render` (with the five replays) pass on the Samsung SM-X700; caustic
  scenes are refused on the Adreno 650.
- [21 of this list] Volume gate rejection text: RESOLVED (merge wave 8 part 1c, on the maintainer's go-ahead).
  `volume_gate.cpp` rejection case 9 now expects the shadow catcher's own refusal ("shadow catcher with volumes");
  the volume gate (every case, including those after case 9) and `volume_boundary_gate` pass on Linux, films
  identical to merge wave 7.
- [22 of this list] Host gate managed reload on Linux: RESOLVED (merge wave 8 part 1c, on the maintainer's
  go-ahead). `tools/hosttests/ImageModes` accepts `imageInput.fileReading` as a Boolean or as the host's
  "development-only" string (files are read); any other value fails the tool. `host_gate.py` passes on Linux with
  every phase, 3,146 comparisons, 0 practical failures.
- [12 of this list] Size-guard warnings on the bench graph: RESOLVED for the bench and the HDRI bench (lane m2-shrink;
  single-body loops in the shared rounded-arithmetic helpers: HDRI bench `prepare_surface` 51,287 instructions, every
  bench-family entry below 60,000, every bench and HDRI bench entry below the size guard; see "Surface entries under
  the Intel compile envelope"). The textured-plane control's `prepare_surface` still warns (item 12).
- [9 of this list] Milestone 1 bench against the CPU oracle: RESOLVED (lane m1-oracle). The Linux x64 CPU oracle is
  built at a fixed baseline (SSE4.2 kernels, no contraction, no reciprocal math) and camera rays, the
  scrambling-distance jitter and triangle attributes are bit-exact in the WGSL: the bench and the HDRI bench pass the
  practical criterion on Linux WGSL, the Samsung and the HP, and compare.py reports "perfect" (see "Fixed-baseline
  CPU oracle, camera-ray and attribute parity, the bench passes"; merge wave 7 reruns the bundles on the merged tree).
- [10 of this list] Camera-ray last-bit parity: RESOLVED (lane m1-oracle; 0 differing bits in every ray field over
  the camera gate's 24 cases). Pixel (45,22) of the factor-only bench was the oracle's FMA build; it passes on the
  fixed baseline without a WGSL change.
- [11 of this list] HDRI bench on WGSL: RESOLVED (lane m1-lighting; environment importance sampling and MIS, see
  "Environment lighting and non-constant emitters").
- [1 of this list] Samsung triangle sampler sample 3133: FIXED (lane S3; ordering barrier on the triangle-light
  quaternion normalization; see "Samsung render images and sampler on the glibc flow").
- [4 of this list] Samsung node and Principled render images: FIXED (lane S3; render_gate lockstep stages judged on
  the glibc flow through the stage-state exchange, the GPU on the reference oracle's adapters; both renders pass).
- [2 of this list] Samsung principled_render stage 40 `mis_ray_pdf`: FIXED (lane S2; Principled tangent rotation: two-part
  angle reduction and contraction-proof rotation).
- [3 of this list] Samsung node SVM probe: (a) graph 44 with the Gabor handler FIXED (Fresnel reciprocal by `select`);
  (b) the driver crash on the mapping-derivative probe module FIXED (`svmn_dual_direction`); `nodes_svm` passes.
- [4] Intel Mesa 25.0.7 cold-compile crash: FIXED (lane L; 16-bit IP/VGRF fields in `brw_opt_combine_constants`,
  light stage split, Horner loops; every compiled entry below 60,000 backend instructions, re-measured in merge 4:
  worst 52,509).
- [11] Specialization on/off identity on Linux: FIXED (lane L; superset groups, every group bit-identical).
- [12] Gabor: shipped on the scene-specialized path (lane L); its Adreno 730 interaction was fixed by lane S2.
- [13] Node render scene limit: FIXED (68 triangles; root cause recorded as an inference).
- [8] Principled device loss on the Adreno 730: FIXED (lane S; SVM stack in a per-invocation arena slot);
  principled_svm and principled_bounce pass. The remaining render failure (open item 4) was fixed by
  lane S3.
- [9] Typed 2D noise program on the Adreno 730: FIXED (lane S; fade product ordering barrier).
- [3] Long submissions: measured on the Adreno 730, longest 29 ms, no reset (lane S); the Adreno 640 part is closed
  under the policy.
- [2], [6], [7], [10] Adreno 640 items: closed under the device reference policy (not supported on the Adreno 640;
  "Pixel 4 XL triage under the device reference policy"). The materials verdicts were settled in merge wave 4:
  Principled not supported (3x), surface nodes not supported (fails).
- Earlier: the Samsung triangle mode-2 decision (exact traversal); the HP compile memory (emissive-triangle compile
  load); the specialization-off crashes on Android (refused by design); the Pixel feature-combination, motion and
  volume-stack failures (function-scope array fix); the Android oracle libm differences (validation reference policy);
  the Pixel triangle sampler's error mask (Phase E5); static instanced triangle emitters (mode 4, Phase E4); the
  deforming-emitter sampler on the Pixel 4 XL (Phase E4).

## Step zero: feature mask widened, pipeline size guard (lane z0, 2026-10-22)

Feature mask. `native/cycles/pipelines.h` replaces the 64-bit `ShaderFeatureBit` enum with `ShaderFeatures`, a fixed
array of `shader_feature_words` (4) 64-bit words: bit n is word n/64, bit n%64. Every assigned bit keeps its number
(0-61, Gabor 63); `SF_ALL` is bits 0-61 plus 63; `SF_SUPERSET_SCENE_ONLY` is unchanged; superset groups still add the
candidate regions in ascending bit order, now across every word. The scene census, the Principled census, the
WGSL region markers (by name, unchanged), the probe specialization (`surface_svm_test.h`), the per-entry pipeline
features and the superset bookkeeping use the new type. No persisted format carried the numeric mask: the
pipeline log, the device and process reports, the superset records and the device record name features, so no
format version changed. Compile-time checks pin bits 0, 61 and 63, the unassigned bit 62 and the word layout.

Bit identity (Linux x64, Intel UHD ADL-S GT1, Mesa 25.0.7-2+deb13u1). "Before" ran the seeded merge wave 4
binaries; "after" ran the rebuilt Step zero binaries.

| Check | Before | After |
| --- | --- | --- |
| glass_render GPU film | `db8e84f9...` | `db8e84f9...`; all films, 4 superset groups identical |
| principled_render GPU film | `fd3a7f2b...` | `fd3a7f2b...`; all films, 10 superset groups identical |
| nodes_render GPU film | `b9d1da3f...` | `b9d1da3f...`; all films, 1 superset group identical |
| compiled WGSL modules (specialized and every superset group, 48 dumps) | | byte-identical |
| host gate, 3,146 film comparisons | merge wave 4 run | 3,128 identical; the 18 GPU films (`gpu`, `gpu-exit`, `abi-gpu`) differ between two runs of the same Step zero binary in exactly the same 18, so they vary run to run (atomic film accumulation order); practical criterion met |

HP (Android x64, Intel Xe, Mesa 24.0.8). Android x64 rebuilt with the change. glass, Principled and node render
judged runs (merge wave 4 commands, merge wave 4's device-scene glibc reference images, scene verification against
merge wave 4's dumped scenes, which my dumps equal): GPU films `7d484ace...`, `e9f7279d...`, `96d86223...`, identical to
merge wave 4's HP films, all PASS. Android host gate (48 exports, 1,579 film comparisons): PASS, 1,568 identical to
merge wave 4; the 11 GPU films (`gpu`, `gpu-exit`) differ between two runs of the same binary. A second run of the
same binary failed on one strict-required GPU film value (`gpu-exit/run.3`, 1 value, 1.49e-7, practical 0): the
same run-to-run variation crossing the strict bound once (INFERENCE: not caused by Step zero, whose compiled WGSL
and material films are identical). Merge wave 5 (2026-10-23): the HP Android host gate ran twice on the merged tree and
`gpu-exit/run.3` had 0 strict differences in both runs (`native/evidence/merge-20261023/merge5/device/hp-hostgate-run1-vs-run2.txt`).

Pipeline-build size guard (lane L decision 4). Each pipeline build measures the inlined WGSL size of every entry it
compiles (`wgsl_inlined_bytes`, the measure the superset grouping uses) and warns at 1,110,000 bytes (lowered from
lane z0's 1,200,000 at merge wave 5): 65,535 backend instructions at the densest measured density of 16.9 bytes per
instruction (65,535 x 16.9 = 1,107,542, rounded up), about 55,500 at the central density of 20 (merge wave 4
envelope, surface entries above 500 KB: 16.9 to 22.1 bytes per instruction on Mesa 25.0.7). Largest current gate
entries at Step zero: node render `shade_background` 1,126,723 bytes (40,103 measured instructions),
`prepare_surface` 1,072,302 (48,439), Principled `shade_surface` 960,137 (52,509). At that size the node-render gate
scene would warn although its measured count is safe (a false warning is accepted; a missed one would be a driver
crash; the message says the results are unchanged). On the merged wave-5 tree the largest gate entries are node render
`shade_background` 1,095,718 bytes, Principled `shade_surface` 950,819 and glass `shade_surface` 658,413, so no gate
scene warns at 1,110,000. The warning goes to stderr before the compile, into the device
report's `warnings` and `sizeGuard` (host ABI, managed `ICapabilityReport.Warnings`) and the process report; it
never changes the variant, the tier or `degraded`. `CODEBRIX_CYCLES_PIPELINE_SIZE_WARN_BYTES` overrides (0
disables). Gate check `native/tools/pipeline_size_guard_check.py`: glass_render at the default and at 200,000 bytes
(8 warnings), films bit-identical. On the HP the three material renders report the same largest entries and no
warning. The 1,200,000-byte threshold lane z0 shipped left a gap (an entry between about 1.11 MB and 1.2 MB could pass
65,535 instructions at the densest measured ratio without a warning); the merge wave 5 threshold closes it to the
rounding of the 16.9 ratio.

## Pixel 4 XL triage under the device reference policy (lane P, 2026-10-22)

Under the device reference policy (2026-10-22), the Pixel 4 XL (Adreno 640, API 33) is not a reference device.
The ARM64 reference is the Samsung SM-X700 (Adreno 730). A job that fails on the Pixel 4 XL, or takes more than
three times its Samsung wall time, is not supported on the Adreno 640. There is no Pixel-only investigation or fix.
The consumer-facing list is the AGENT-README section "NOT SUPPORTED ON ADRENO 640 (PIXEL 4 XL CLASS DEVICES)".
The merged Pixel-specific fixes stay in place: the SVM stack outside `SvmState` (since lane S in a per-invocation
arena slot), the volume-stack scalar arrays and the sliced sampler dispatch. The known-adapter table and the device-tier code are unchanged.

Method. The tree is 3392df3d. The Android arm64 gates, render_replay and the validation APK were built from that
tree, with the runtime unchanged (`libcodebrix_wgpu.so` hash-verified before every job). Each job ran alone on the
device, with the same flags as the merge-wave-3 Pixel chain:
- image and sampler gates: the device-scene glibc reference flow;
- materials: the exported Linux references;
- every gate: `CODEBRIX_CYCLES_PIPELINE_BUDGET_MS=20000`, compile-budget overruns as warnings only;
- the full tier, chosen by the table.

The per-job hard timeout is the cutoff, 3 x the Samsung wall. The 3x rule carries a 5-second floor on cutoffs
(adb launch latency): cutoff = max(5 s, 3 x the Samsung wall). Samsung walls
come from:
- `native/work/merge3`, else `merge2`, else `merge1` device runs (in main);
- for four jobs with no Samsung record, one lane-P timing session on the Samsung: object and deforming stationary
  images, and lighting_mis replays 0 and 1.

A job still running at its cutoff was stopped and recorded as `FAIL (3x cutoff at N s)`. The gate process ended
with its adb session, and the post-cutoff process lists show no codebrix process. Replays of a device capture run
only when their source job passed. The Linux triangle and terminator captures were regenerated in the lane-P tree,
because merge 3 had deleted their arenas. The Pixel walls are single runs. Evidence: lane P handoff `device/`
(per-job `runs/pixel/<corpus>-p-<step>-<n>/`, the logs, and `triage-table.{json,md}`).

| Corpus | Job | Samsung wall (s) | Pixel wall (s) | Cutoff (s) | Pixel result | Verdict |
| --- | --- | --- | --- | --- | --- | --- |
| triangle | triangle_sample | 25.8 (fails) | 77.3 | 77.0 | FAIL (3x cutoff at 77 s) | NOT SUPPORTED (3x) |
| triangle | surface | 97.4 | 199.7 | 292.0 | PASS | SUPPORTED |
| triangle | triangle_lighting | 159.0 | 477.2 | 477.0 | FAIL (3x cutoff at 477 s) | NOT SUPPORTED (3x) |
| triangle | replay-0 | 21.4 | - | 64.0 | not run (source job failed) | NOT RUN (source job not supported) |
| triangle | replay-1 | 39.1 | - | 117.0 | not run (source job failed) | NOT RUN (source job not supported) |
| triangle | replay-2 | 39.2 | - | 118.0 | not run (source job failed) | NOT RUN (source job not supported) |
| triangle | replay-3 | 39.4 | - | 118.0 | not run (source job failed) | NOT RUN (source job not supported) |
| triangle | replay-4 | 39.0 | - | 117.0 | not run (source job failed) | NOT RUN (source job not supported) |
| triangle | replay-5 | 87.0 | - | 261.0 | not run (source job failed) | NOT RUN (source job not supported) |
| emitter | emitter_sample | 11.3 | 21.4 | 34.0 | PASS | SUPPORTED |
| hostcap-linux-triangle | tri0 | 21.7 | 61.4 | 65.0 | PASS | SUPPORTED |
| hostcap-linux-triangle | tri1 | 40.0 | 120.0 | 120.0 | FAIL (3x cutoff at 120 s) | NOT SUPPORTED (3x) |
| hostcap-linux-triangle | tri2 | 40.0 | 120.1 | 120.0 | FAIL (3x cutoff at 120 s) | NOT SUPPORTED (3x) |
| hostcap-linux-triangle | tri3 | 40.0 | 120.0 | 120.0 | FAIL (3x cutoff at 120 s) | NOT SUPPORTED (3x) |
| hostcap-linux-triangle | tri4 | 39.1 | 117.1 | 117.0 | FAIL (3x cutoff at 117 s) | NOT SUPPORTED (3x) |
| hostcap-linux-triangle | tri5 | 87.9 | 264.1 | 264.0 | FAIL (3x cutoff at 264 s) | NOT SUPPORTED (3x) |
| hostcap-linux-terminator | term0 | 17.2 | 39.2 | 52.0 | PASS | SUPPORTED |
| hostcap-linux-terminator | term1 | 19.6 | 51.5 | 59.0 | PASS | SUPPORTED |
| hostcap-linux-terminator | term2 | 19.5 | 51.2 | 58.0 | PASS | SUPPORTED |
| hostcap-linux-terminator | term3 | 19.4 | 50.6 | 58.0 | PASS | SUPPORTED |
| hostcap-linux-terminator | term4 | 33.7 | 101.1 | 101.0 | FAIL (3x cutoff at 101 s) | NOT SUPPORTED (3x) |
| hostcap-linux-terminator | term5 | 33.7 | 101.1 | 101.0 | FAIL (3x cutoff at 101 s) | NOT SUPPORTED (3x) |
| hostcap-linux-terminator | term6 | 33.5 | 100.1 | 100.0 | FAIL (3x cutoff at 100 s) | NOT SUPPORTED (3x) |
| hostcap-linux-terminator | term7 | 33.7 | 101.1 | 101.0 | FAIL (3x cutoff at 101 s) | NOT SUPPORTED (3x) |
| hostcap-linux-terminator | term8 | 33.7 | 101.1 | 101.0 | FAIL (3x cutoff at 101 s) | NOT SUPPORTED (3x) |
| hostcap-linux-terminator | term9 | 72.8 | 218.0 | 218.0 | FAIL (3x cutoff at 218 s) | NOT SUPPORTED (3x) |
| materials | nodes_svm | 879.2 (fails) | 158.3 | 2638.0 | FAIL: device lost | PROVISIONAL (NOT SUPPORTED (fails)) |
| materials | nodes_world | 509.8 | 6.8 | 1529.0 | FAIL: SVM error 8 | NOT SUPPORTED (fails) |
| materials | nodes_render | 153.2 (fails) | 143.1 | 460.0 | FAIL: SVM error 24 (8+16) | PROVISIONAL (NOT SUPPORTED (fails)) |
| materials | nodes-replay | - (fails) | - | - | not run (no Samsung baseline, deferred to merge) | NO BASELINE |
| materials | principled_svm | 71.2 (fails) | 115.0 | 214.0 | FAIL: SVM error 8 (probe) | PROVISIONAL (NOT SUPPORTED (fails)) |
| materials | principled_bounce | 26.3 (fails) | 79.2 | 79.0 | FAIL (3x cutoff at 79 s) | PROVISIONAL (NOT SUPPORTED (3x)) |
| materials | principled_render | 28.7 (fails) | 86.2 | 86.0 | FAIL (3x cutoff at 86 s) | PROVISIONAL (NOT SUPPORTED (3x)) |
| materials | principled-replay | - (fails) | - | - | not run (no Samsung baseline, deferred to merge) | NO BASELINE |
| lighting | broad_lighting | 68.3 | 203.7 | 205.0 | PASS | SUPPORTED |
| lighting | broad_lighting-replay-0 | 20.3 | 61.1 | 61.0 | FAIL (3x cutoff at 61 s) | NOT SUPPORTED (3x) |
| lighting | broad_lighting-replay-1 | 20.4 | 61.1 | 61.0 | FAIL (3x cutoff at 61 s) | NOT SUPPORTED (3x) |
| lighting | feature_combination | 42.8 | 128.2 | 128.0 | FAIL (3x cutoff at 128 s) | NOT SUPPORTED (3x) |
| lighting | feature_combination-replay-0 | 27.6 | - | 83.0 | not run (source job failed) | NOT RUN (source job not supported) |
| lighting | feature_combination-replay-1 | 27.9 | - | 84.0 | not run (source job failed) | NOT RUN (source job not supported) |
| lighting | lighting_mis | 33.4 | 96.4 | 100.0 | PASS | SUPPORTED |
| lighting | lighting_mis-replay-0 | 15.6 | 47.2 | 47.0 | FAIL (3x cutoff at 47 s) | NOT SUPPORTED (3x) |
| lighting | lighting_mis-replay-1 | 16.0 | 48.2 | 48.0 | FAIL (3x cutoff at 48 s) | NOT SUPPORTED (3x) |
| regression | glass_render | 79.9 | 121.4 | 240.0 | PASS | SUPPORTED |
| regression | lighting_mis | 33.4 | 96.4 | 100.0 | PASS | SUPPORTED |
| regression | transparent_lighting | 107.5 | 64.7 | 322.0 | FAIL: device lost | NOT SUPPORTED (fails) |
| transparent | transparent_shadow | 16.1 | 13.9 | 48.0 | FAIL: invalid state/data (mask 1) | NOT SUPPORTED (fails) |
| transparent | transparent_lighting | 106.9 | 64.7 | 321.0 | FAIL: device lost | NOT SUPPORTED (fails) |
| transparent | transparent_lighting-replay-0 | 23.7 | - | 71.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-1 | 23.7 | - | 71.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-2 | 30.5 | - | 92.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-3 | 30.9 | - | 93.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-4 | 31.4 | - | 94.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-5 | 31.9 | - | 96.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-6 | 30.2 | - | 91.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent_lighting-replay-7 | 30.8 | - | 92.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent-shadow-replay-direct | 3.9 | - | 12.0 | not run (source job failed) | NOT RUN (source job not supported) |
| transparent | transparent-shadow-replay-indexed | 4.0 | - | 12.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | shadow_terminator | 147.4 | 442.3 | 442.0 | FAIL (3x cutoff at 442 s) | NOT SUPPORTED (3x) |
| terminator | terminator-replay-0 | 15.3 | - | 46.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-1 | 17.5 | - | 52.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-2 | 17.6 | - | 53.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-3 | 17.6 | - | 53.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-4 | 29.8 | - | 89.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-5 | 29.8 | - | 89.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-6 | 29.8 | - | 89.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-7 | 29.6 | - | 89.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-8 | 29.9 | - | 90.0 | not run (source job failed) | NOT RUN (source job not supported) |
| terminator | terminator-replay-9 | 67.7 | - | 203.0 | not run (source job failed) | NOT RUN (source job not supported) |
| spec-refusal | glass_render-spec | 76.8 | 121.4 | 230.0 | PASS | SUPPORTED |
| spec-refusal | glass_render-nospec-refused | 0.3 | 0.9 | 5.0 | PASS | SUPPORTED |
| motion | object-moving-images | 84.9 | 252.9 | 255.0 | FAIL: SVM error 8 | NOT SUPPORTED (fails) |
| motion | object-replay-motion-0 | 66.8 | - | 200.0 | not run (source job failed) | NOT RUN (source job not supported) |
| motion | object-stationary-images | 36.2 | 109.2 | 109.0 | FAIL (3x cutoff at 109 s) | NOT SUPPORTED (3x) |
| motion | deforming-moving-images | 110.8 | 116.0 | 332.0 | FAIL: SVM error 8 | NOT SUPPORTED (fails) |
| motion | deforming-replay-motion-0 | 29.6 | - | 89.0 | not run (source job failed) | NOT RUN (source job not supported) |
| motion | deforming-stationary-images | 36.3 | 109.2 | 109.0 | FAIL (3x cutoff at 109 s) | NOT SUPPORTED (3x) |
| motion | camera-static | 7.9 | 10.0 | 24.0 | PASS | SUPPORTED |
| motion | camera-motion | 14.2 | 37.3 | 43.0 | PASS | SUPPORTED |
| precision | closest | 21.4 | 40.4 | 64.0 | PASS | SUPPORTED |
| precision | shadow | 16.2 | 16.7 | 49.0 | PASS | SUPPORTED |
| precision | shadow-replay-direct | 1.5 | 2.0 | 5.0 | PASS | SUPPORTED |
| precision | shadow-replay-indexed | 1.5 | 2.0 | 5.0 | PASS | SUPPORTED |
| volume | volume-state | 13.2 | 24.9 | 40.0 | PASS | SUPPORTED |
| volume | volume-absorption | 120.8 | 140.1 | 362.0 | PASS | SUPPORTED |
| volume | volume-isotropic | 172.0 | 179.2 | 516.0 | PASS | SUPPORTED |
| volume | volume-forward | 170.8 | 180.4 | 512.0 | PASS | SUPPORTED |
| volume | volume-backward | 171.7 | 182.8 | 515.0 | PASS | SUPPORTED |
| volume | boundary-state | 32.4 | 81.0 | 97.0 | PASS | SUPPORTED |
| volume | boundary-compaction-guard | 6.4 | 19.2 | 19.0 | FAIL (3x cutoff at 19 s) | NOT SUPPORTED (3x) |
| volume | closed-single | 214.4 | 219.4 | 643.0 | PASS | SUPPORTED |
| volume | closed-overlap | 319.9 | 345.9 | 960.0 | PASS | SUPPORTED |
| volume | closed-inside | 302.9 | 337.5 | 909.0 | PASS | SUPPORTED |
| volume | volume-replay-absorption | 19.5 | 50.1 | 58.0 | PASS | SUPPORTED |
| volume | volume-replay-isotropic | 20.6 | 50.5 | 62.0 | PASS | SUPPORTED |
| volume | volume-replay-forward | 20.5 | 50.3 | 62.0 | PASS | SUPPORTED |
| volume | volume-replay-backward | 20.7 | 54.8 | 62.0 | PASS | SUPPORTED |
| volume | closed-replay-single | 21.5 | 54.1 | 64.0 | PASS | SUPPORTED |
| volume | closed-replay-overlap | 33.9 | 102.1 | 102.0 | FAIL (3x cutoff at 102 s) | NOT SUPPORTED (3x) |
| volume | closed-replay-inside | 34.1 | 102.2 | 102.0 | FAIL (3x cutoff at 102 s) | NOT SUPPORTED (3x) |
| app | managed-app-3-scenes | 134.4 | >403 (result timeout) | 403.0 | FAIL (3x cutoff at 403 s) | NOT SUPPORTED (3x) |
| motion (item 2) | object-moving-images, device oracle, no reference import | 84.9 | 255.3 | 255.0 | FAIL (3x cutoff at 255 s) | NOT SUPPORTED (3x) |
| motion (item 2) | object-moving-images, CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2, no reference import | 84.9 | 251.8 | 255.0 | FAIL: SVM error 8 | NOT SUPPORTED (fails) |

Summary:
- Supported (within 3x): the point-lamp emitter probe; surface state; perspective camera, static and with motion;
  the closest and shadow precision fixtures and their replays; lighting_mis and broad lighting; the specialized
  glass render; the specialization-off refusal; every homogeneous and closed volume render (including camera
  inside) with most volume replays; Linux capture replays tri0 and term0-3.
- Fails on the Adreno 640:
  - SVM error 8: object motion (with the x64 reference oracle's arithmetic), deformation motion and node worlds;
  - mask 1 (invalid state): transparent shadow;
  - device lost at queue submit: transparent lighting.
  The two transparent gates passed on this device in earlier sessions; they fail in this single run, and under the
  policy they were not investigated.
- More than 3x:
  - the emissive-triangle sampler and images;
  - feature_combination and shadow_terminator;
  - stationary motion-capable scenes;
  - the managed validation app (result not delivered within 403 s);
  - device replays of lighting captures, overlapping closed volumes and most Linux triangle and terminator captures;
  - the volume compaction guard case.
- Materials, settled at merge wave 4 (2026-10-22) after lane S, one run each with cutoff max(5 s, 3 x that merge's
  Samsung wall): principled_svm (cutoff 335 s), principled_bounce (915 s), principled_render (148 s) and nodes_svm
  (461 s) were stopped at their cutoffs; nodes_render failed with SVM error 24 (8 + 16) at 217 s (cutoff 386 s). The
  materials and node replays did not run (their source jobs did not pass). Principled BSDF materials are therefore not
  supported on the Adreno 640 (3x) and texture/colour/utility nodes in surface shaders are not supported (fails). The
  Samsung walls of principled_render, nodes_svm and nodes_render are times to their (open) Samsung failures. Evidence:
  `native/work/merge4/runs/pixel/materials-p-*` and `native/work/merge4/device/pixel-cutoffs.json`.

Dispositions DECIDED by Jeremy (2026-10-22): the following are definitively not supported on Pixel 4 level hardware
(Adreno 640) and will NOT be rerun: object and deformation motion blur, node-based world shaders, transparent shadows
and transparent lighting, emissive triangle lights, feature combination, shadow terminator, stationary renders of
motion-capable scenes, the managed validation app, and most on-device replays. A full functionality test on a Pixel 5
(Adreno 620) later is the one exception the policy allows.

Diagnostic-only results (maintainer material; not in the consumer AGENT-README):
- **The x64 reference-oracle import is not supported on Adreno 640.** This is
  `CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2`, which the image gates select when they import a glibc
  reference. Object motion fails with SVM error 8 under it (item 2).
- **On-device replays of captured command streams exceed 3x** for these captures: lighting (lighting_mis,
  broad_lighting), overlapping and camera-inside closed volumes, Linux triangle captures tri1-5 and Linux terminator
  captures term4-9. These pass on the Pixel: the precision shadow replays, the volume replays (absorption, isotropic,
  forward, backward, closed-single), tri0 and term0-3.
- **The volume-boundary malformed-input (compaction) guard case exceeds 3x:** 19.2 s against a 19 s cutoff
  (`CODEBRIX_VOLUME_COMPACTION_CASE=1`). The volume-boundary state checks pass.

Compile times on the Pixel (recorded jobs; item 7). Every job stayed on the full tier, and the slow entries were
warnings only:
- node-render `prepare_surface`: 119.7 s;
- object motion: `intersect_closest` 85 s, `prepare_shader_data` 58 s, `prepare_surface` 56 s;
- deformation motion: `prepare_surface` 54 s;
- lighting: `shade_surface` 27-32 s;
- closed volumes: `shade_volume` 29-30 s;
- glass: `shade_surface` 24 s;
- camera motion: `init_camera` 27 s.
Jobs stopped at the cutoff left no pulled pipeline log. Under the 3x rule, the compile time is part of each job's
wall time.


## Samsung Galaxy S20 FE 5G (Adreno 650) sweep (lane Q, 2026-10-22)

A sweep only: every completed device functionality was run once on a new device, with no product, gate,
tolerance or tier-table change and no diagnosis. Tree 3392df3d; runtime unchanged
(CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433; staged `libcodebrix_wgpu.so` arm64 `8113cbfa...`
hash-verified before every corpus). The runner is the merge-wave-3 tool set retargeted to this device
(`native/work/q/tools`, remote prefix `codebrix-q-`). Image and sampler gates were judged against the glibc reference
computed on Linux x64 from the S20's own dumped scene. Their replays were judged against the same reference. The
S20's bionic CPU result was recorded as the diagnostic column. The state, SVM and world gates compare against the
S20's own CPU. `principled_render` and `nodes_render` used the Linux-prepared export. All of this matches the
tablet runs.

**Device identity.**
- Device: SM-G781U1, serial RFCRB0MPHYH, Android 13 / API 33, board `kona` (Snapdragon 865), arm64-v8a,
  5.7 GB RAM.
- Build: `samsung/r8quex/r8q:13/TP1A.220624.014/G781U1UESNGYI1`.
- WebGPU adapter:
  - device `Adreno (TM) 650`, vendor `Qualcomm Technologies Inc. Adreno Vulkan Driver`, vendor id 20803,
    device id 100990978;
  - driver description `Driver Build: db7e7066a2, I1fad33e957, 1746950372 Date: 05/11/25 Compiler Version:
    EV031.32.02.10`.
- Tier: the adapter is not in the known-adapter table (`knownAdapter` null).
  - Every pipeline of every run used the full tier, reason `default` ("first run on this device: full fidelity"),
    with the unknown-mobile 20 s compile budget (`budgetSource` `unknown-mobile`).
  - Slow compiles only warned. No run changed tier or fell back to conservative.
- Init canary (opt-in, `glass_render`): passed in 1,586 ms.
- Device record: written in the documented format (status `valid`; 9 full-tier stages; no failure or breadcrumb).
  The persisted copy is under `native/work/q/runs/s20/canary-q/output/canary-glass_render/record/`.

**Sweep table.** Walls are the gate run itself, excluding the scene-dump and reference steps. Tablet walls and
results are the SM-X700's latest runs (merge waves 1-3). The host-gate and app rows count the whole tool. The
Evidence column is relative to `native/work/q/`. No job reached its timeout, which was max(600 s, 3 x the tablet
wall).

| Corpus | Job | S20 wall s | Tablet wall s | Tablet | S20 | Class | practical / strict / max (vs glibc unless "local") | Compile > 20 s | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| canary-q | canary-glass_render | 97.9 | 79.9 | PASS (glass_render, no canary run) | PASS | - | local 33 / 9.54e-07 | - | runs/s20/canary-q |
| emitter-q | emitter_sample | 22.4 | 11.3 | PASS | PASS | - | 0 / 125 / 9.06e-06 | - | runs/s20/emitter-q |
| hostcap-linux-terminator-q2 | term replays (10) | 42-368 | 17-73 | PASS | PASS 10/10 | - | readbacks 428/418/418/428/455/446/446/455/407/443; strict 0/0/0/0/27/19/19/27/8/21; max 6.2e-05 | - | runs/s20/hostcap-linux-terminator-q2 |
| hostcap-linux-triangle-q2 | tri replays (6) | 44-367 | 22-88 | PASS | PASS 6/6 | - | readbacks 289/417/415/473/415/464; strict 12/0/12/233/5/237; max 1.79e-06 | - | runs/s20/hostcap-linux-triangle-q2 |
| lighting-fc | feature_combination | 91.3 | 42.8 | PASS | PASS | - | 0 / 1179 / 6.02e-06 | init_camera 22 | runs/s20/lighting-fc |
| lighting-fc | feature_combination replays (2) | 72-72 | 28-28 | PASS | PASS 2/2 | - | readbacks 408/461; strict 12/9; max 2.41e-06 | - | runs/s20/lighting-fc |
| lighting-q | lighting_mis | 87.4 | 33.4 | PASS | PASS | - | 0 / 1057 / 6.08e-06 | shade_surface 27 | runs/s20/lighting-q |
| lighting-q | broad_lighting | 174.9 | 68.3 | PASS | PASS | - | 0 / 951 / 3.81e-05 | shade_surface 27, shade_surface 27 | runs/s20/lighting-q |
| lighting-q | lighting_mis replays (2) | 46-49 | - | not run on tablet | PASS 2/2 | - | readbacks 414/450; strict 0/0; max 1.19e-07 | - | runs/s20/lighting-q |
| lighting-q | broad_lighting replays (2) | 45-46 | 20-20 | PASS | PASS 2/2 | - | readbacks 414/450; strict 0/13; max 4.17e-07 | - | runs/s20/lighting-q |
| materials-nr | nodes_render | 14.0 | 153.2 | FAIL pixel 6720 (item 9) | FAIL | device lost | - | - | runs/s20/materials-nr |
| materials-nr | nodes-replay | - | - | not run (source failed) | FAIL | not run: source job failed | - | - | runs/s20/materials-nr |
| materials-nsvm | nodes_svm | 19.2 | 879.2 | FAIL typed 2D noise (item 9) | FAIL | device lost | - | - | runs/s20/materials-nsvm |
| materials-nw | nodes_world | 6.1 | 509.8 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | - | runs/s20/materials-nw |
| materials-nwiso | nodes_world | 6.1 | 509.8 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | - | runs/s20/materials-nwiso |
| materials-pr | principled_svm | 152.0 | 71.2 | FAIL device lost (item 8) | FAIL | numerical practical | - | - | runs/s20/materials-pr |
| materials-pr | principled_bounce | 251.6 | 26.3 | FAIL device lost (item 8) | FAIL | numerical practical (GPU kernel reported invalid data) | - | prepare_surface 152, shade_surface 68 | runs/s20/materials-pr |
| materials-pr | principled_render | 255.9 | 28.7 | FAIL device lost (item 8) | FAIL | numerical practical (GPU kernel reported invalid data) | - | prepare_surface 151, shade_surface 68 | runs/s20/materials-pr |
| materials-pr | principled-replay | - | - | not run (source failed) | FAIL | not run: source job failed | - | - | runs/s20/materials-pr |
| motion-q | object-moving-images | 194.1 | 84.9 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | intersect_closest 74, prepare_shader_data 43 | runs/s20/motion-q |
| motion-q | deforming-moving-images | 84.0 | 110.8 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | prepare_surface 43 | runs/s20/motion-q |
| motion-q | camera-static | 9.1 | 7.9 | PASS | PASS | - | local 0 / 0 | - | runs/s20/motion-q |
| motion-q | camera-motion | 29.3 | 14.2 | PASS | PASS | - | local 2818 / 0 | init_camera 20 | runs/s20/motion-q |
| motion-q | object replays (1) | - | 67-67 | PASS | not run 1/1 (source failed) | not run: source job failed | - | - | runs/s20/motion-q |
| motion-q | deforming replays (1) | - | 30-30 | PASS | not run 1/1 (source failed) | not run: source job failed | - | - | runs/s20/motion-q |
| precision-q | closest | 33.1 | 21.4 | PASS | PASS | - | local 0 / 0 | - | runs/s20/precision-q |
| precision-q | shadow | 12.8 | 16.2 | PASS | PASS | - | local 0 / 5.96e-08 | - | runs/s20/precision-q |
| precision-q | shadow replays (2) | 2-2 | 2-2 | PASS | PASS 2/2 | - | readbacks 4/4; strict 0/0; max 5.96e-08 | - | runs/s20/precision-q |
| regression-q | glass_render | 95.7 | 79.9 | PASS | PASS | - | 0 / 100 / 1.91e-06 | - | runs/s20/regression-q |
| regression-q | lighting_mis | 63.4 | 33.4 | PASS | PASS | - | 0 / 1057 / 6.08e-06 | - | runs/s20/regression-q |
| regression-q | transparent_lighting | 46.9 | 106.9 | PASS | FAIL | device lost | - | - | runs/s20/regression-q |
| spec-refusal-q | glass_render-spec | 101.0 | 76.8 | PASS | PASS | - | local 33 / 9.54e-07 | - | runs/s20/spec-refusal-q |
| spec-refusal-q | glass_render-nospec-refused | 0.3 | 0.3 | PASS (refused) | PASS | - | - | - | runs/s20/spec-refusal-q |
| terminator-q | shadow_terminator | 421.2 | 147.4 | PASS | PASS | - | 0 / 120 / 6.2e-05 | intersect_closest 75, intersect_shadow 56 | runs/s20/terminator-q |
| terminator-q | terminator replays (10) | 28-376 | 15-68 | PASS | PASS 10/10 | - | readbacks 428/418/418/428/455/446/446/455/407/443; strict 0/0/0/0/27/19/19/27/8/20; max 6.2e-05 | - | runs/s20/terminator-q |
| transparent-iso | transparent_shadow | 10.2 | 16.1 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | - | runs/s20/transparent-iso |
| transparent-iso | transparent-shadow replays (2) | - | 4-4 | PASS | not run 2/2 (source failed) | not run: source job failed | - | - | runs/s20/transparent-iso |
| transparent-q | transparent_shadow | 10.4 | 16.1 | PASS | FAIL | numerical practical (GPU kernel reported invalid data) | - | - | runs/s20/transparent-q |
| transparent-q | transparent_lighting | 46.8 | 106.9 | PASS | FAIL | device lost | - | - | runs/s20/transparent-q |
| transparent-q | transparent_lighting replays (8) | - | 24-32 | PASS | not run 8/8 (source failed) | not run: source job failed | - | - | runs/s20/transparent-q |
| transparent-q | transparent-shadow replays (2) | - | 4-4 | PASS | not run 2/2 (source failed) | not run: source job failed | - | - | runs/s20/transparent-q |
| triangle-q | triangle_sample | 80.1 | 25.8 | FAIL 2 practical (sample 3133) | PASS | - | 0 / 865 / 0.000488 | - | runs/s20/triangle-q |
| triangle-q | surface | 141.3 | 97.4 | PASS | PASS | - | local 1244 / 3.81e-06 | - | runs/s20/triangle-q |
| triangle-q | triangle_lighting | 472.9 | 159.0 | PASS | PASS | - | 0 / 490 / 1.79e-06 | shade_surface_light 107, intersect_closest 75 | runs/s20/triangle-q |
| triangle-q | triangle_lighting replays (6) | 43-356 | 21-87 | PASS | PASS 6/6 | - | readbacks 289/417/415/473/415/464; strict 12/0/6/233/8/231; max 1.79e-06 | - | runs/s20/triangle-q |
| volume-ci | closed-inside | 281.5 | 302.9 | PASS | PASS | - | local 1788 / 1.53e-05 | shade_volume 21 | runs/s20/volume-ci |
| volume-ci | closed replays (1) | 72-72 | 34-34 | PASS | PASS 1/1 | - | readbacks 1116; strict 1788; max 1.53e-05 | - | runs/s20/volume-ci |
| volume-co | closed-overlap | 293.9 | 319.9 | PASS | PASS | - | local 857 / 3.05e-05 | shade_volume 20 | runs/s20/volume-co |
| volume-co | closed replays (1) | 71-71 | 34-34 | PASS | PASS 1/1 | - | readbacks 1192; strict 857; max 3.05e-05 | - | runs/s20/volume-co |
| volume-cs | closed-single | 196.5 | 214.4 | PASS | PASS | - | local 608 / 3.05e-05 | - | runs/s20/volume-cs |
| volume-cs | closed replays (1) | 40-40 | 22-22 | PASS | PASS 1/1 | - | readbacks 838; strict 608; max 3.05e-05 | - | runs/s20/volume-cs |
| volume-q | volume-state | 19.4 | 13.2 | PASS | PASS | - | local 8551 / 0.00013 | - | runs/s20/volume-q |
| volume-q | volume-absorption | 124.4 | 120.8 | PASS | PASS | - | local 552 / 2.29e-05 | - | runs/s20/volume-q |
| volume-q | volume-isotropic | 179.0 | 172.0 | PASS | PASS | - | local 1439 / 2.29e-05 | - | runs/s20/volume-q |
| volume-q | volume-forward | 220.6 | 170.8 | PASS | PASS | - | local 1472 / 3.62e-05 | - | runs/s20/volume-q |
| volume-q | volume-backward | 219.1 | 171.7 | PASS | PASS | - | local 1382 / 2.29e-05 | - | runs/s20/volume-q |
| volume-q | boundary-state | 83.2 | 32.4 | PASS | PASS | - | local 6492 / 0.000244 | shade_surface 24 | runs/s20/volume-q |
| volume-q | boundary-compaction-guard | 21.6 | 6.4 | PASS | PASS | - | local 0 / 0 | - | runs/s20/volume-q |
| volume-q | volume replays (4) | 55-56 | 20-21 | PASS | PASS 4/4 | - | readbacks 464/672/664/664; strict 552/1439/1472/1382; max 3.62e-05 | - | runs/s20/volume-q |
| host-gate | android-host-gate | 136 | 62.0 | PASS 48 exports (wall from file times) | PASS | - | 48 exports, 1579 films, 0 practical, 0 required strict, max 1.43e-05 (cpu/cpu-exit/reload/stress/gpu/gpu-exit all rc 0) | - | device/codebrix-q-host-gate (log device/android-host-gate.log) |
| app | app-webgpu | 379 | 173.0 | PASS 0 practical | PASS | - | 3 scenes vs Linux glibc films: 0 practical, 921 strict, max 1.38e-05; tier full; cancellation ok=True; capability warning: pipeline 'shade_surface_light' took 53882 ms, over the 20000 ms compile budget (full tier); first-use latency is degraded on this driver | shade_surface_light 54 | android-app/webgpu-s20 (log device/app-s20.log) |

The merge-wave-3 Linux captures could not be replayed as they were. merge3's scratch cleanup had deleted their
`run.arena.bin` inputs, so the first hostcap attempt stopped at the first push. That was a harness input problem, not
a device result. The captures were re-recorded on Linux in this worktree (`triangle_lighting_gate.py` and
`shadow_terminator_gate.py`, both pass) and replayed as `hostcap-*-q2`. `transparent_shadow` and `nodes_world` each ran
directly after a device-lost process, so each was repeated once in isolation (`transparent-iso`, `materials-nwiso`;
fresh stage, 60 s idle). Both reproduce.

**Differs from the tablet (SM-X700, Adreno 730).**
- These pass on the tablet and fail on the S20:
  - `transparent_lighting` (regression and transparent corpora): the device is lost in mode 0
    (`wgpuQueueSubmit: Parent device is lost`, exit 134). Its 8 replays did not run.
  - `transparent_shadow`: the kernel reports invalid data, mask 1. Its 2 replays did not run.
  - The object- and deforming-motion images: SVM error 8 (`invalid/uninitialized SVM stack`). Their replays did not
    run. Camera static and motion pass.
  - `nodes_world`: SVM error 8 at world 0.
- `triangle_sample` passes on the S20 (0 practical, 865 strict) and fails on the tablet (open item 1). The S20's own
  bionic CPU differs from glibc there by 72 practical values.
- These fail on both devices, in different ways:
  - Principled SVM, bounce and render: SVM error 8 on the S20; on the tablet the device is lost (open item 8).
  - `nodes_svm`: the S20 loses the device creating `surface_svm_probe`; on the tablet the typed 2D noise program
    fails (item 9).
  - `nodes_render`: the S20 loses the device creating `prepare_surface`; on the tablet image component 6720 fails
    (item 9).
- Speed: the volume renders take 0.9-1.3 x the tablet's wall. Triangle, lighting, terminator, boundary-state and
  replay jobs take 2-6 x.
- Compile times: compiles over the 20 s unknown-mobile budget (warnings only) reached:
  - Principled `prepare_surface` 152 s;
  - emissive-triangle `shade_surface_light` 107 s;
  - motion and terminator `intersect_closest` 75 s;
  - `shade_surface_light` 54 s in the managed app (reported as its capability warning).
- The SVM error 8 failures (motion images, node world, Principled) have the same signature as the Adreno 640 items 2
  and 10. This is an observation, not a diagnosis.

The S20 was left with no `codebrix-q-*` folders, no lane Q app and no codebrix process. Folders and packages that were
on the device before this sweep (`codebrix-ncdbg`, four `com.codebrix.*` test apps) were not touched.

### S20 rerun on the merged tree (merge wave 4, 2026-10-22)

Lane Q's failing jobs, rerun once each on the merge-wave-4 tree (lanes L, S, P), one job per run, timeout max(600 s,
3 x the merge-wave-4 Samsung wall), same reference flow as the tablet (device-scene glibc). Adapter "Adreno (TM) 650",
unknown to the tier table (full tier by default, 20 s unknown-mobile compile budget); the table was not changed.
Record only; nothing was fixed. Evidence: `native/work/merge4/runs/s20/`.

| Job | Timeout s | S20 wall s | Result (first error line) |
| --- | --- | --- | --- |
| transparent_lighting | 600 | 47.5 | FAIL: `Parent device is lost` (8 replays not run) |
| transparent_shadow | 600 | 11.0 | FAIL: invalid data (mask 1): invalid state/data |
| object-moving-images | 600 | 110.9 | FAIL: lighting stage, mask 8 (invalid/uninitialized SVM stack) |
| deforming-moving-images | 600 | 82.7 | FAIL: lighting stage, mask 8 (invalid/uninitialized SVM stack) |
| nodes_world | 1452 | 6.2 | FAIL: compiled world 0, mask 8 (invalid/uninitialized SVM stack) |
| principled_svm | 600 | 401.1 | PASS |
| principled_bounce | 915 | 915.3 | TIMEOUT at the cutoff |
| principled_render | 600 | 233.1 | FAIL: stage 40 `path.mis_ray_pdf` 1751.469 against 1747.274 (as the Samsung) |
| nodes_svm | 600 | - | FAIL: the scene-dump pass lost the device (`wgpuDeviceCreateComputePipeline: Parent device is lost`) |
| nodes_render | 600 | 17.2 | FAIL: `wgpuDeviceCreateComputePipeline: Parent device is lost` |

principled_svm now passes on the S20 (it failed in lane Q's sweep on 3392df3d; the merged tree adds lane S's arena
stack). The SVM-stack (mask 8) failures of motion and node worlds remain on the merged tree.

## Pipeline cache (lane ZC, 2026-10-22)

The shipped CodeBrix.WebGpu.Native 1.0.290.1433 runtime exposes no pipeline-cache API on any backend
(`WGPUNativeFeature_PipelineCache` is advertised and can be enabled, but no export creates, attaches or serializes
a cache; audit and the export list a future runtime would need: MAINTAINER-README.txt "PIPELINE CACHE"). The host
therefore has no cache of its own; what remains is each driver's cache, measured here with the seeded merge-4
binaries (no code change). Evidence: `native/work/zc-pipeline-cache/` (`runs/linux-seeded/results.json`,
`audit/probe-*.txt`, `audit/driver-cache-strings.txt`).

Linux, Intel UHD ADL-S, Mesa 25.0.7: Mesa's on-disk cache serves every pipeline. Cold = fresh
`MESA_SHADER_CACHE_DIR`, warm = the same directory again, disabled = `MESA_SHADER_CACHE_DISABLE=true`. The GPU film
is bit-identical in all three runs and equal to the reference film.

| Gate | Cold wall / compile s | Warm wall / compile s | Disabled wall / compile s | Cache size | GPU film |
| --- | --- | --- | --- | --- | --- |
| glass_render | 44.4 / 22.6 | 27.7 / 0.022 | 43.9 / 21.7 | 4.25 MB | `db8e84f9…` all three |
| principled_render | 65.0 / 47.7 | 16.5 / 0.025 | 61.4 / 44.8 | 5.67 MB | `fd3a7f2b…` all three |
| nodes_render | 53.8 / 39.6 | 16.4 / 0.027 | 52.5 / 38.4 | 5.21 MB | `b9d1da3f…` all three |
| host ABI consumer test (gpu, 59 pipelines) | 51.5 / 49.7 | 1.8 / 0.127 | - | - | not reproducible run to run (atomic accumulation; differs between the two warm runs too) |

The largest single pipeline (Principled `prepare_surface`) drops from 18.9 s to 4 ms. Every merge-report wall on
Linux is cold, because the runners give each run a fresh scratch cache.

HP, Intel Xe TGL GT2, Mesa 24.0.8 (Bliss OS): no driver cache. With `MESA_SHADER_CACHE_DIR` or `XDG_CACHE_HOME`
pointing at a writable directory the driver writes nothing and a repeated compile costs the same (probe: 4.6 ms
each time; the Linux control drops 5.4 ms to 0.2 ms and writes the database). The driver binary lacks the
`MESA_SHADER_CACHE_DISABLE` check, so the runners' `MESA_SHADER_CACHE_DISABLE=true` changes nothing there. Merge-4
cold walls are therefore also every-run walls: glass_render 70.7 s (35.7 s compile), principled_render 152.7 s
(118.7 s), nodes_render 111.8 s (77.4 s), principled_bounce 930.7 s (916.5 s, of which 800 s re-compile the same 5
pipelines on 8 fresh devices).

Samsung SM-X700, Adreno 730: no effective implicit cache seen. principled_bounce repeats identical pipelines at
full cost (252 of 290 compile seconds are repeats; `prepare_surface` 14.8-16.8 s each time), and the same
transparent_lighting `shade_surface_light` cost 8.5 s and 8.2 s in two separate processes.

## Adreno 650 lane (Samsung Galaxy S20 FE 5G, 2026-10-22)

The S20 is not a reference device. Its standard is: fix, or degrade gracefully (an explicit refusal that names the GPU
class and the feature; never a silent wrong image, a lost device or a hang). Tolerances, the runtime
(CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433) and the other table entries are unchanged.

**Device and adapter (as the host reports it).** SM-G781U1, Android 13 / API 33, Snapdragon 865. WebGPU adapter:
vendor `Qualcomm Technologies Inc. Adreno Vulkan Driver` (vendor id 20803 = 0x5143), device `Adreno (TM) 650` (device
id 100990978 = 0x6050002), description `Driver Build: db7e7066a2, I1fad33e957, 1746950372 Date: 05/11/25 Compiler
Version: EV031.32.02.10`. Tier report now: reason `table`, entry "Samsung Galaxy S20 FE Adreno 650", full tier,
20 s compile budget (`budgetSource` `table`, warnings only), `refusedRegion` `SVM_NODES_ANY`. Before this lane the
adapter was unknown to the table (full tier, unknown-mobile 20 s budget), so compile behaviour is unchanged.

**Root causes.** Each was localised on the S20 with instrumented builds of the failing gate; the Adreno 730 compiler
(EV031.36) compiles the same WGSL correctly. All four are miscompiles of the EV031.32 shader compiler in large entry
points; the first three have fixes with unchanged arithmetic, the fourth is refused.
1. **Dynamic vector component (error 8 in node worlds).** In `NODE_SEPARATE_VECTOR`, `v[component]` (component read
   from the bytecode) made the store to the node's output slot disappear: the decoded output offset (3) and component
   (2) were right, the slot stayed unwritten, and the next node read an uninitialized stack slot. Fix: a select chain
   (`svm_component`, `wavefront_svm.wgsl`; also the derivative variant).
2. **Second traversal stack (transparent shadows: mask 1; transparent lighting: lost device).** `intersect_shadow`
   inlines the opaque and the shadow-all traversal, each with a 192-entry private stack. The shadow-all traversal lost
   the pushed instance sentinel: with distinct failure codes per check, the failing check was `object!=NONE` on an
   instance leaf. With 63 or more transparent layers the stage reported invalid state; in the lit transparent renders
   the GPU faulted (`IOCTL_KGSL_GPU_COMMAND ... errno 35`, device lost). Fix: the shadow-all stack lives in the
   invocation's SVM arena slot (words 0-191; `intersect_shadow` evaluates no SVM program), pushes, pops and bounds
   unchanged (`wavefront_shadow_traversal.wgsl`).
3. **Per-component normalize (error 8 in motion images, x64 oracle arithmetic).** Android images judged against the
   imported glibc reference compile the x64 oracle's arithmetic (`CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2`).
   With it, object and deformation motion images lost an SVM store in the light stage. Overriding the five oracle
   constants one at a time showed that only `NATIVE_FLOAT3_RECIPROCAL_NORMALIZE=0` (per-component division) triggers
   it: every normalize then inlines the integer `rounded_divide` three times. Fix: `surface_native_normalize` runs the
   three divisions through one `rounded_divide` body, in a loop whose bound the compiler cannot see (header word 3 after
   the error word, always 0); same quotients (`wavefront_shader_data.wgsl`).
4. **Texture, colour and utility node interpreters (refused).** At these module sizes the compiler fails in more than
   one way. (a) `Failed to link shaders ... Assertion failed: false && "PSEUDOIMUL32 must not be seen here"` (device
   lost) for the node render `prepare_surface` and the node SVM probe. Reproduced with `native/tools/compile_probe.cpp`
   on the Linux-dumped module in 11 s; a case-by-case bisection kept 12 handlers that are all needed together (a size
   or register-pressure threshold, not one construct); workgroup sizes 1 and 32 fail alike. (b) Silent wrong code in
   the world interpreter: after fix 1 and an fBM loop restructure, worlds 0-4 passed, but world 5's ramp took the error
   branch of `size<2||size>0x0fffffff` with size 256 (the same condition evaluated inside the branch is false); the
   conservative tier fails the same way. A compiler that takes false branches cannot be patched site by site, so these
   scenes are refused on this GPU class.

**Degradation.** The known-adapter table entry for `Adreno (TM) 650` names the feature region `SVM_NODES_ANY`
(noise, white noise, wave, magic, brick, Voronoi and Gabor textures; mapping, map range, mix, HSV, colour
separate/combine, ramps and curves, Fresnel/layer weight, vector rotate, blackbody, normal, object info/camera/scene
time/vector transform, light path, wavelength, wireframe, mapping derivatives). `shader_features()` refuses a scene
whose referenced shaders reach it before any pipeline compiles (as for unimplemented Principled features), with:
"WebGPU: texture, colour and utility shader nodes are not supported on Adreno (TM) 650 GPUs (this scene uses ...):
the shader compiler of this GPU class miscompiles or fails to link their shader programs; render the scene on another
GPU or on the CPU device". The WebGPU capability scope lists "texture, colour and utility shader nodes on Adreno 650
GPUs" under `renderTimeRejections`. On Linux the refusal is exercised with `CODEBRIX_CYCLES_TEST_ADAPTER` (simulated
Adreno 650: node world refused, glass render unaffected); the real Linux adapter is unaffected.

**Per-job results on the S20.** Device-scene flow (glibc x64 reference from the S20's own dumped scene), practical
`5e-5 + 2e-4*abs(ref)`, strict `1e-7 + 1e-7*abs(ref)` counted. Before = the merge-wave-4 rerun above; after = this
lane's final tree. Walls are the gate run itself.

| Job | Before | After |
| --- | --- | --- |
| transparent_shadow | FAIL, invalid data (mask 1), 11.0 s | PASS, 21.9 s (state gate: strict 0, max 1.19e-07) |
| transparent_lighting | FAIL, device lost, 47.5 s (8 replays not run) | PASS, 274 s: 0 practical, 683 strict, max 6.02e-06; 8/8 replays PASS |
| transparent-shadow replays (2) | not run | PASS 2/2 |
| object-moving-images | FAIL, mask 8, 110.9 s | PASS, 141 s: 0 practical, 1,276 strict, max 6.08e-06; 8/8 replays PASS |
| deforming-moving-images | FAIL, mask 8, 82.7 s | PASS, 222 s (final tree; 318 s in the separate replay run): 0 practical, 1,213 strict, max 6.08e-06; 8/8 replays PASS |
| nodes_world | FAIL, mask 8 at world 0, 6.2 s | refused (exit 1, message, no pipeline compiled), 0 s |
| nodes_svm | FAIL, device lost creating the probe | refused (exit 1, message, no pipeline compiled), 1 s |
| nodes_render | FAIL, device lost creating `prepare_surface`, 17.2 s | refused (exit 1, message, no pipeline compiled), 1 s |
| principled_bounce | stopped at 915 s | PASS, 1,631 s against the device CPU, as on the Samsung (strict 3,259; max 0.125 on `path.mis_ray_pdf` 840718.4, practical ratio 0.126); slow (5.3 x the Samsung's 305 s; compiles up to 125 s), not a failure under the S20 standard |
| principled_render | FAIL, stage 40 `path.mis_ray_pdf` | unchanged: FAIL, stage 40 `path.mis_ray_pdf` 1751.469 against 1747.274, 174 s (the Samsung's open item 2). Merge wave 5 (2026-10-23): stage 40 passes after lane S2's principled_rotate fix; the job now fails later, `Independent CPU image 9114 actual=1.546950 expected=1.545995`, the same failure as the Samsung (device-CPU image flow; open on both). Adreno 650 lane 2 (2026-10-23): unchanged, 301 s, 2 practical, 262 strict, max 9.54e-04 (recorded only) |
| principled_svm | PASS, 401 s | not rerun in the lane. Merge wave 5 (2026-10-23): FAIL, `Native compiled graphs SVM error mask=8 first incomplete path=119 shader=12`, 85 s. Fixed in "Adreno 650 lane 2" (the trigger was the merge-5 `principled_rotate`, not fixes 1 or 3): PASS, 413 s, strict 557, max 3.59e-06 |

Targeted regression set on the final tree: triangle_sample PASS 115 s (0 practical, 865 strict, max 4.88e-04),
surface PASS 213 s (state gate, strict 1,244, max 3.81e-06), glass_render PASS 133 s (0 practical, 100 strict, max
1.91e-06), lighting_mis PASS 87 s (0 practical, 1,057 strict, max 6.08e-06).

**Linux (reference) on the final tree.** Fresh Linux build; all-lanes PASS (1,274 s), host gate PASS, device-tier
gate PASS (31 scenarios), transparent-shadow gate PASS, node gate PASS (1,797 s; all 176 films bit-identical to merge
wave 4, node render `b9d1da3f...`), Principled gate PASS (802 s; all 44 films bit-identical, `fd3a7f2b...`); glass film `db8e84f9...` unchanged; in all-lanes 263 films are
bit-identical to merge wave 4 and 17 differ, all in atomic-accumulation cases whose own repeat differs within a run.
Intel compile envelope on the transparent-lighting and node-render modules: worst 46,790 (`prepare_surface`, node
render; it was 48,438 before fix 3), every entry below 60,000.

Evidence (lane worktree `native/work/a650-s20/`): `runs/s20/*-v3*` and `runs/s20/pixel-motion-v4def` (final S20
runs), `runs/s20/*-v1*` (first fix set), `diag/quick/` (instrumented runs, `index.txt`), `diag/probe-results.jsonl`
and `diag/bisect-nr/` (compile bisection), `validation/v2-*` (Linux).

### Adreno 650 lane 2 (2026-10-23)

**Principled SVM regression (error 8), fixed.** At merge wave 5 the S20's `principled_svm` failed with
`Native compiled graphs SVM error mask=8 first incomplete path=119 shader=12` (it passed at merge wave 4). The probe
module the S20 compiled at merge wave 5 differs from merge wave 4's only in four places: `precise_disk_sincos_pair`
and the non-contractible `principled_rotate` (lane S2), the per-component normalize of fix 3 (dead under the device's
own arithmetic, which this gate uses) and the unused `svm_component` of fix 1. The feature-mask widening and the
shared-device change of merge wave 5 left the module text unchanged. Arena replays of the failing capture on the
S20 (the captured buffers, with only the module text changed) located the trigger:

| Module variant | S20 replay |
| --- | --- |
| merge wave 5 as shipped | error 8, paths 119-135 and later groups wrong |
| merge wave 4 | error word 0, 0 practical |
| merge wave 5 without fix 3's loop | error 8 |
| merge wave 5 with S2's reduction kept and the old contractible rotation | error word 0, 0 practical |
| merge wave 5 with the old reduction and S2's rotation | error 8 |

The rotation computes each output row as `rounded_add(rounded_add(e0*x,e1*y),e2*z)` with nine entries
`e=rounded_add(u,v)`: fifteen inlined copies of the integer `rounded_add` body, and with them the Adreno 650 compiler
(EV031.32.02.10) dropped an SVM store in the probe. This is the same class of failure as fix 3. **Fix:**
`principled_rotate` (`wavefront_svm_microfacet.wgsl`) evaluates the three rows through one body in a loop. The loop
bound is opaque to the compiler (header word 3 after the error word, always 0; a larger value would only add
iterations that write nothing). The sums and their order are unchanged. A first variant, all fifteen sums through a
single `rounded_add` in one 15-step loop, fixed the probe but made the S20's `transparent_shadow` fail with mask 8
(the shadow module contains the same function). It was not kept. The three-row form passes both.

**S20, final tree** (device-scene flow, glibc x64 reference from the S20's own scene; practical
`5e-5 + 2e-4*abs(ref)`, strict counted): principled_svm PASS 413 s (20 graphs, strict 557, max 3.59e-06);
principled_bounce PASS 486 s (device CPU, strict 2,796, max 0.125, compiles up to 133 s);
transparent_lighting PASS 194 s (0 / 683 / 6.02e-06), replays 8/8; transparent_shadow PASS 18 s, replays 2/2;
object-moving-images PASS 199 s (0 / 1,276 / 6.08e-06), replays 8/8; deforming-moving-images PASS 320 s
(0 / 1,213 / 6.08e-06), replays 8/8; triangle_sample PASS 80 s (0 / 865 / 4.88e-04); surface PASS 45 s (strict
1,244); glass_render PASS 97 s (0 / 100 / 1.91e-06); lighting_mis PASS 64 s (0 / 1,057 / 6.08e-06). The node
refusals are still exact: nodes_world, nodes_svm and nodes_render exit 1 with the message, and no pipeline is
compiled. principled_render still fails at image component 9114 (1.546950 against 1.545995; 2 practical, 262
strict), the tablet's open item. The numbers equal merge wave 5's.

**Linux (reference).** All-lanes PASS: 259 of 280 films equal merge wave 5. The 21 that differ are in the 11
run-to-run atomic cases already listed at merge wave 5. Host gate PASS (the 12 differing files are input EXRs that
differ only in their capture-date header). Principled gate PASS: 48 of 48 films equal, GPU film `e81feb12...`. Node
gate PASS, 176 of 176 equal. Glass `db8e84f9...` and node render `b9d1da3f...` superset identity PASS. Device-tier
gate PASS (31 scenarios). Simulated Adreno 650: node world refused, glass render film unchanged. The size-guard
check passes. Intel compile envelope worst 52,047 (unchanged). The smaller module lets `SVM_NODE_WIREFRAME` fit into
a Principled superset group, so that check now runs 11 groups instead of 12, all identical. The change is in shared
WGSL: the Adreno 730 and the HP compile it too.

## Merge wave 5: device tables

Generated by `native/work/merge5/tools/device_tables.py` from each run's `results.json` (merge wave 5, part 2; the runs are frozen in `native/evidence/merge-20261023/merge5/runs/<device>/`). Wall times are device seconds per job; "Judged against" names the reference the result was judged against (glibc device-scene flow, the device's own CPU, or the source capture's CPU film).

### samsung

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m5 | principled_svm | 119.0 | PASS | glibc, device-scene | strictFloatFailures=634, maximumAbsoluteError=3.61e-06 |
| materials-m5 | principled_bounce | 85.0 | PASS | device's own CPU / gate | strictFloatFailures=3466, maximumAbsoluteError=0.125 |
| materials-m5 | principled_render | 81.4 | FAIL (merge wave 5; PASS since lane S3, see "Samsung render images and sampler on the glibc flow"): Rendered repeat=0 sample=6 stages=127 Rendered repeat=0 sample=7 stages=145 Independent CPU image 9114 actual=1.546950 expected=1.545995 | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=2 strict=267 max=0.000954 |
| materials-m5 | nodes_svm | 1500.5 | PASS | glibc, device-scene | strictFloatFailures=18450, maximumAbsoluteError=0.00195 |
| materials-m5 | nodes_world | 495.3 | PASS | device's own CPU / gate | strictFloatFailures=16242, maximumAbsoluteError=6.1e-05 |
| materials-m5 | nodes_render | 133.2 | FAIL (merge wave 5; PASS since lane S3): Rendered repeat=0 sample=6 stages=127 Rendered repeat=0 sample=7 stages=144 Independent CPU image 6720 actual=0.928481 expected=0.928078 | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=1 strict=3998 max=0.000403 |
| materials-m5 | principled-replay | - | not run (source job failed; PASS since lane S3) | device's own CPU / gate |  |
| materials-m5 | nodes-replay | - | not run (source job failed; PASS since lane S3) | device's own CPU / gate |  |
| triangle-m5 | triangle_sample | 20.9 | FAIL (merge wave 5; PASS since lane S3): lorIO Info]: Color management disabled. (Specify the $OCIO environment variable to enable.) Emitter sample=3133 field=2 got=0.382186 expected=0.379992 | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=2 strict=1069 max=0.00219 |
| triangle-m5 | surface | 24.1 | PASS | device's own CPU / gate | strictFloatFailures=1246, maximumAbsoluteError=3.81e-06 |
| triangle-m5 | triangle_lighting | 78.6 | PASS | glibc, device-scene | maximumAbsoluteError=1.79e-06; host re-judged vs glibc PASS: practical=0 strict=519 max=1.79e-06 |
| triangle-m5 | replay-0 | 15.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| triangle-m5 | replay-1 | 20.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=5 |
| triangle-m5 | replay-2 | 21.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=8 |
| triangle-m5 | replay-3 | 22.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=235 |
| triangle-m5 | replay-4 | 21.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.73e-07, strictImageFailures=11 |
| triangle-m5 | replay-5 | 35.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=248 |
| emitter-m5 | emitter_sample | 7.7 | PASS | glibc, device-scene | maximumAbsoluteError=9.06e-06; host re-judged vs glibc PASS: practical=0 strict=135 max=9.06e-06 |
| lighting-m5 | lighting_mis | 29.4 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1070 max=6.08e-06 |
| lighting-m5 | broad_lighting | 56.5 | PASS | glibc, device-scene | maximumAbsoluteError=3.81e-05; host re-judged vs glibc PASS: practical=0 strict=993 max=3.81e-05 |
| lighting-m5 | lighting_mis-replay-0 | 16.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m5 | lighting_mis-replay-1 | 17.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m5 | broad_lighting-replay-0 | 16.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m5 | broad_lighting-replay-1 | 17.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.17e-07, strictImageFailures=15 |
| regression-m5 | glass_render | 63.1 | PASS | glibc, device-scene | strictImageFailures=97, strictStageFailures=1087; host re-judged vs glibc PASS: practical=0 strict=97 max=1.91e-06 |
| regression-m5 | lighting_mis | 29.9 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1070 max=6.08e-06 |
| regression-m5 | transparent_lighting | 90.8 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=693 max=6.02e-06 |
| transparent-m5 | transparent_shadow | 18.1 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m5 | transparent_lighting | 89.6 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=693 max=6.02e-06 |
| transparent-m5 | transparent_lighting-replay-0 | 19.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m5 | transparent_lighting-replay-1 | 19.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m5 | transparent_lighting-replay-2 | 25.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=77 |
| transparent-m5 | transparent_lighting-replay-3 | 25.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.29e-06, strictImageFailures=134 |
| transparent-m5 | transparent_lighting-replay-4 | 25.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=168 |
| transparent-m5 | transparent_lighting-replay-5 | 26.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.9e-07, strictImageFailures=3 |
| transparent-m5 | transparent_lighting-replay-6 | 24.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=103 |
| transparent-m5 | transparent_lighting-replay-7 | 25.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=208 |
| transparent-m5 | transparent-shadow-replay-direct | 6.3 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m5 | transparent-shadow-replay-indexed | 6.4 | PASS | source capture CPU film | maximumAbsoluteImageError=0, strictImageFailures=0 |
| terminator-m5 | shadow_terminator | 96.0 | PASS | glibc, device-scene | maximumAbsoluteError=6.2e-05; host re-judged vs glibc PASS: practical=0 strict=124 max=6.2e-05 |
| terminator-m5 | terminator-replay-0 | 12.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-1 | 14.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-2 | 14.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-3 | 14.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-4 | 24.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m5 | terminator-replay-5 | 24.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m5 | terminator-replay-6 | 24.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m5 | terminator-replay-7 | 24.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m5 | terminator-replay-8 | 24.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.93e-05, strictImageFailures=8 |
| terminator-m5 | terminator-replay-9 | 33.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.91e-05, strictImageFailures=24 |
| spec-refusal-m5 | glass_render-spec | 62.8 | PASS | glibc, device-scene | strictImageFailures=97, strictStageFailures=1087; host re-judged vs glibc PASS: practical=0 strict=97 max=1.91e-06 |
| spec-refusal-m5 | glass_render-nospec-refused | 0.4 | PASS | expected refusal |  |
| spec-refusal-m5 | glass_render-superset-refused | 0.2 | PASS | expected refusal |  |
| hostcap-linux-triangle | tri0 | 15.4 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| hostcap-linux-triangle | tri1 | 20.6 | PASS | source capture CPU film | maximumAbsoluteImageError=1.49e-07, strictImageFailures=1 |
| hostcap-linux-triangle | tri2 | 21.1 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| hostcap-linux-triangle | tri3 | 21.7 | PASS | source capture CPU film | maximumAbsoluteImageError=1.76e-06, strictImageFailures=236 |
| hostcap-linux-triangle | tri4 | 21.4 | PASS | source capture CPU film | maximumAbsoluteImageError=3.73e-07, strictImageFailures=8 |
| hostcap-linux-triangle | tri5 | 34.7 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-06, strictImageFailures=258 |
| hostcap-linux-terminator | term0 | 12.2 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 14.4 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 14.7 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 14.8 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 24.7 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term5 | 24.9 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term6 | 25.0 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term7 | 25.1 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term8 | 25.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.93e-05, strictImageFailures=8 |
| hostcap-linux-terminator | term9 | 33.6 | PASS | source capture CPU film | maximumAbsoluteImageError=2.93e-05, strictImageFailures=22 |
| pixel-motion-own | object-moving-images | 45.2 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-stationary-images | 36.0 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-replay-motion-0 | 30.0 | PASS | source capture CPU film | maximumAbsoluteImageError=3.58e-07, strictImageFailures=10 |
| pixel-motion-own | object-replay-motion-1 | 31.4 | PASS | source capture CPU film | maximumAbsoluteImageError=2.86e-06, strictImageFailures=26 |
| pixel-motion-own | object-replay-motion-2 | 31.4 | PASS | source capture CPU film | maximumAbsoluteImageError=2.4e-06, strictImageFailures=290 |
| pixel-motion-own | object-replay-motion-3 | 32.7 | PASS | source capture CPU film | maximumAbsoluteImageError=1.02e-07, strictImageFailures=1 |
| pixel-motion-own | object-replay-motion-4 | 31.9 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=295 |
| pixel-motion-own | object-replay-motion-5 | 31.5 | PASS | source capture CPU film | maximumAbsoluteImageError=4.53e-06, strictImageFailures=59 |
| pixel-motion-own | object-replay-motion-6 | 31.9 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=342 |
| pixel-motion-own | object-replay-motion-7 | 31.6 | PASS | source capture CPU film | maximumAbsoluteImageError=6.02e-06, strictImageFailures=246 |
| pixel-motion-x64 | object-moving-images | 45.8 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1306 max=6.08e-06 |
| pixel-motion-x64 | object-stationary-images | 36.9 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1157 max=6.08e-06 |
| pixel-motion-x64 | object-replay-motion-0 | 30.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.68e-06, strictImageFailures=18 |
| pixel-motion-x64 | object-replay-motion-1 | 32.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=16 |
| pixel-motion-x64 | object-replay-motion-2 | 32.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.52e-06, strictImageFailures=303 |
| pixel-motion-x64 | object-replay-motion-3 | 32.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.08e-09, strictImageFailures=0 |
| pixel-motion-x64 | object-replay-motion-4 | 32.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=302 |
| pixel-motion-x64 | object-replay-motion-5 | 32.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.29e-06, strictImageFailures=72 |
| pixel-motion-x64 | object-replay-motion-6 | 32.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=344 |
| pixel-motion-x64 | object-replay-motion-7 | 32.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=251 |

### hp

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m5 | principled_svm | 168.7 | PASS | glibc, device-scene | strictFloatFailures=946, maximumAbsoluteError=9.54e-07 |
| materials-m5 | principled_bounce | 236.3 | PASS | device's own CPU / gate | strictFloatFailures=5234, maximumAbsoluteError=0.125 |
| materials-m5 | principled_render | 152.1 | PASS | glibc, device-scene | strictImageFailures=92, strictStageFailures=926; host re-judged vs glibc PASS: practical=0 strict=92 max=6.44e-06 |
| materials-m5 | nodes_svm | 812.7 | PASS | glibc, device-scene | strictFloatFailures=10978, maximumAbsoluteError=0.0107 |
| materials-m5 | nodes_world | 258.0 | PASS | device's own CPU / gate | strictFloatFailures=9125, maximumAbsoluteError=6.1e-05 |
| materials-m5 | nodes_render | 105.9 | PASS | glibc, device-scene | strictImageFailures=1843, strictStageFailures=3987; host re-judged vs glibc PASS: practical=0 strict=1843 max=0.000103 |
| materials-m5 | principled-replay | 120.7 | PASS | source capture CPU film | maximumAbsoluteImageError=6.44e-06, strictImageFailures=94 |
| materials-m5 | nodes-replay | 73.6 | PASS | source capture CPU film | maximumAbsoluteImageError=0.000103, strictImageFailures=1849 |
| triangle-m5 | triangle_sample | 22.9 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| triangle-m5 | surface | 39.3 | PASS | device's own CPU / gate | strictFloatFailures=1810, maximumAbsoluteError=3.81e-06 |
| triangle-m5 | triangle_lighting | 88.3 | PASS | glibc, device-scene | maximumAbsoluteError=2.32e-06; host re-judged vs glibc PASS: practical=0 strict=887 max=2.32e-06 |
| triangle-m5 | replay-0 | 37.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| triangle-m5 | replay-1 | 45.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| triangle-m5 | replay-2 | 45.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| triangle-m5 | replay-3 | 46.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-06, strictImageFailures=427 |
| triangle-m5 | replay-4 | 45.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.73e-07, strictImageFailures=10 |
| triangle-m5 | replay-5 | 64.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.26e-06, strictImageFailures=427 |
| emitter-m5 | emitter_sample | 19.8 | PASS | glibc, device-scene | maximumAbsoluteError=0.00513; host re-judged vs glibc PASS: practical=0 strict=235 max=0.00513 |
| lighting-m5 | lighting_mis | 47.7 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2681 max=8.34e-06 |
| lighting-m5 | broad_lighting | 69.4 | PASS | glibc, device-scene | maximumAbsoluteError=5.34e-05; host re-judged vs glibc PASS: practical=0 strict=1740 max=5.34e-05 |
| lighting-m5 | lighting_mis-replay-0 | 37.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=3 |
| lighting-m5 | lighting_mis-replay-1 | 38.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=11 |
| lighting-m5 | broad_lighting-replay-0 | 39.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=1 |
| lighting-m5 | broad_lighting-replay-1 | 38.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.07e-07, strictImageFailures=26 |
| regression-m5 | glass_render | 70.1 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=1155; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| regression-m5 | lighting_mis | 49.9 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2681 max=8.34e-06 |
| regression-m5 | transparent_lighting | 77.6 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1682 max=7.75e-06 |
| transparent-m5 | transparent_shadow | 12.7 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m5 | transparent_lighting | 78.8 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1682 max=7.75e-06 |
| transparent-m5 | transparent_lighting-replay-0 | 42.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.49e-07, strictImageFailures=1 |
| transparent-m5 | transparent_lighting-replay-1 | 42.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=8 |
| transparent-m5 | transparent_lighting-replay-2 | 48.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=93 |
| transparent-m5 | transparent_lighting-replay-3 | 48.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.01e-06, strictImageFailures=323 |
| transparent-m5 | transparent_lighting-replay-4 | 49.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-06, strictImageFailures=435 |
| transparent-m5 | transparent_lighting-replay-5 | 49.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-07, strictImageFailures=4 |
| transparent-m5 | transparent_lighting-replay-6 | 48.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.56e-06, strictImageFailures=299 |
| transparent-m5 | transparent_lighting-replay-7 | 49.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.79e-06, strictImageFailures=519 |
| transparent-m5 | transparent-shadow-replay-direct | 5.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m5 | transparent-shadow-replay-indexed | 5.9 | PASS | source capture CPU film | maximumAbsoluteImageError=7.45e-09, strictImageFailures=0 |
| terminator-m5 | shadow_terminator | 151.5 | PASS | glibc, device-scene | maximumAbsoluteError=1.53e-05; host re-judged vs glibc PASS: practical=0 strict=97 max=1.53e-05 |
| terminator-m5 | terminator-replay-0 | 31.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-1 | 40.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-2 | 40.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-3 | 40.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m5 | terminator-replay-4 | 67.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| terminator-m5 | terminator-replay-5 | 67.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| terminator-m5 | terminator-replay-6 | 67.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| terminator-m5 | terminator-replay-7 | 67.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| terminator-m5 | terminator-replay-8 | 67.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=5 |
| terminator-m5 | terminator-replay-9 | 80.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.1e-06, strictImageFailures=12 |
| spec-refusal-m5 | glass_render-spec | 70.2 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=1155; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| spec-refusal-m5 | glass_render-nospec-refused | 0.3 | PASS | expected refusal |  |
| spec-refusal-m5 | glass_render-superset-refused | 0.2 | PASS | expected refusal |  |
| hostcap-linux-triangle | tri0 | 38.6 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| hostcap-linux-triangle | tri1 | 47.7 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| hostcap-linux-triangle | tri2 | 48.0 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| hostcap-linux-triangle | tri3 | 47.8 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-06, strictImageFailures=430 |
| hostcap-linux-triangle | tri4 | 48.3 | PASS | source capture CPU film | maximumAbsoluteImageError=3.73e-07, strictImageFailures=10 |
| hostcap-linux-triangle | tri5 | 66.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.26e-06, strictImageFailures=425 |
| hostcap-linux-terminator | term0 | 33.1 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 41.3 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 40.8 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 41.0 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 68.7 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| hostcap-linux-terminator | term5 | 68.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| hostcap-linux-terminator | term6 | 68.5 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| hostcap-linux-terminator | term7 | 69.5 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| hostcap-linux-terminator | term8 | 70.0 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-07, strictImageFailures=5 |
| hostcap-linux-terminator | term9 | 83.1 | PASS | source capture CPU film | maximumAbsoluteImageError=1.07e-06, strictImageFailures=17 |

### s20

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| a650-refusals-m5 | nodes_world-refused | 0.7 | PASS | expected refusal |  |
| a650-refusals-m5 | nodes_svm-refused | 0.4 | PASS | expected refusal |  |
| a650-refusals-m5 | nodes_render-refused | 0.6 | PASS | expected refusal |  |
| triangle-s20-triangle_sample | triangle_sample | 78.5 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| regression-s20-glass_render | glass_render | 95.8 | PASS | glibc, device-scene | strictImageFailures=100, strictStageFailures=1079; host re-judged vs glibc PASS: practical=0 strict=100 max=1.91e-06 |
| regression-s20-lighting_mis | lighting_mis | 63.2 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1057 max=6.08e-06 |
| triangle-s20-surface | surface | 45.2 | PASS | device's own CPU / gate | strictFloatFailures=1244, maximumAbsoluteError=3.81e-06 |
| transparent-s20-transparent_lighting | transparent_lighting | 193.7 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=683 max=6.02e-06 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-0 | 50.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-1 | 50.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-2 | 60.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=72 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-3 | 61.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.55e-06, strictImageFailures=133 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-4 | 62.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=167 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-5 | 62.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.9e-07, strictImageFailures=3 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-6 | 61.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=103 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-7 | 61.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=205 |
| transparent-s20-transparent_shadow | transparent_shadow | 16.4 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-s20-transparent_shadow | transparent-shadow-replay-direct | 11.1 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_shadow | transparent-shadow-replay-indexed | 11.0 | PASS | source capture CPU film | maximumAbsoluteImageError=0, strictImageFailures=0 |
| materials-s20-principled_render | principled_render | 213.6 | FAIL: Rendered repeat=0 sample=6 stages=127 Rendered repeat=0 sample=7 stages=145 Independent CPU image 9114 actual=1.546950 expected=1.545995 | glibc, device-scene | ; host re-judged vs glibc FAIL: practical=2 strict=262 max=0.000954 |
| materials-s20-principled_render | principled-replay | - | not run (source job failed) | device's own CPU / gate |  |
| materials-s20-principled_svm | principled_svm | 85.2 | FAIL: r management disabled. (Specify the $OCIO environment variable to enable.) Native compiled graphs SVM error mask=8 first incomplete path=119 shader=12 | glibc, device-scene | Fixed in "Adreno 650 lane 2": PASS 413 s, strict 557, max 3.59e-06 |
| pixel-motion-s20-object-moving-images | object-moving-images | 139.9 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1276 max=6.08e-06 |
| pixel-motion-s20-object-moving-images | object-replay-motion-0 | 117.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.56e-06, strictImageFailures=12 |
| pixel-motion-s20-object-moving-images | object-replay-motion-1 | 120.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| pixel-motion-s20-object-moving-images | object-replay-motion-2 | 119.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.52e-06, strictImageFailures=301 |
| pixel-motion-s20-object-moving-images | object-replay-motion-3 | 118.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-s20-object-moving-images | object-replay-motion-4 | 120.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=294 |
| pixel-motion-s20-object-moving-images | object-replay-motion-5 | 120.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.05e-06, strictImageFailures=70 |
| pixel-motion-s20-object-moving-images | object-replay-motion-6 | 122.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=338 |
| pixel-motion-s20-object-moving-images | object-replay-motion-7 | 160.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=249 |
| pixel-motion-s20-deforming-moving-images | deforming-moving-images | 315.3 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1213 max=6.08e-06 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-0 | 85.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=2 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-1 | 119.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-07, strictImageFailures=19 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-2 | 119.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.4e-06, strictImageFailures=304 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-3 | 119.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-4 | 120.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=280 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-5 | 119.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.17e-06, strictImageFailures=49 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-6 | 129.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=300 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-7 | 170.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=259 |
| materials-s20-principled_bounce | principled_bounce | 455.2 | PASS | device's own CPU / gate | strictFloatFailures=2796, maximumAbsoluteError=0.125 |

## Samsung render images and sampler on the glibc flow (lane S3, 2026-10-23)

Samsung SM-X700 (Adreno 730). Runtime unchanged (shipped `libcodebrix_wgpu.so` arm64 hash-verified before every
corpus); full tier by table. Every result below is judged against the glibc reference of the device's own dumped scene.

**Render gates: lockstep stages on the glibc flow (open items 4 and 5).** `render_gate.cpp` used to judge each
lockstep stage (the GPU's output state for one kernel dispatch against the CPU oracle run on the same GPU input) on
the device, with the device's own CPU and therefore with the device's own arithmetic adapters on the GPU, while its
image was judged against glibc. Lane S2 showed the consequence: with the device adapters the images followed the
device CPU (node render component 6720, Principled components 9114 and 9362); with the reference oracle's adapters
the Principled lockstep failed against the scalar device CPU at stage 1. In the device-scene flow
(`CODEBRIX_CYCLES_REFERENCE_IMPORT` with `CODEBRIX_CYCLES_SCENE_VERIFY`) the gate now:
- generates the reference oracle's (Linux x64 SSE4.2) arithmetic adapters, as the sampler and lighting gates do;
- records, for every lockstep stage of the first repeat, the GPU input state (every layout field of every path), the
  film before the stage, the kernel, the GPU output state, the film after it and the GPU queue counter, delta-encoded
  in `PREFIX.stages.bin` (about 23-27 MB for the node and Principled renders);
- keeps its GPU-only checks on the device (bitwise repeat, coverage, termination, the image against the imported
  glibc film, and the scene check of the local CPU image).
The Linux x64 gate binary then judges the record (`CODEBRIX_CYCLES_SCENE_LOAD` with
`CODEBRIX_CYCLES_STAGE_JUDGE=FILE`, CPU only; `native/tools/android_reference.py stage-judge`): it rebuilds each
stage's input, runs the glibc CPU oracle on it and compares the recorded GPU output with the same code as the local
check (practical criterion fatal, strict counted, integer fields exact, film, queue counters, the clay shared-edge
rule). A device job passes only when the device run and the stage judge both pass, and the judge must cover the
same number of stages as the device. On Linux the judge reproduces the local lockstep exactly (node render: 144
stages, 4,123 strict, maximum ratio 0.2488; Principled: 145 stages, 946 strict, 0.0502; same films). Render replays
of a device-scene render job are judged against the same glibc film. `CODEBRIX_RENDER_LOCAL_LOCKSTEP=1` restores the
earlier device pass (device adapters, local lockstep) as a diagnostic. Without these variables nothing changes:
Linux films are bit-identical (node render `b9d1da3f...`, Principled `e81feb12...`, glass `db8e84f9...`).

| Samsung job | Result | Image against glibc | Stages judged on Linux |
| --- | --- | --- | --- |
| `nodes_render` | **PASS** (112 s) | 0 practical, 1,728 strict, max 3.72e-5 | 144 stages, 4,028 strict, max 7.63e-6, ratio 0.0485 |
| `principled_render` | **PASS** (71 s) | 0 practical, 68 strict, max 6.44e-6 | 145 stages, 660 strict, max 0.0625, ratio 0.0496 |
| `glass_render` | PASS (47 s) | 0 practical, 63 strict, max 1.91e-6 | 149 stages, 1,070 strict, ratio 0.0034 |
| `nodes-replay`, `principled-replay` | PASS (88 s, 43 s) | 1,728 / 68 strict (glibc film) | - |

So no Adreno 730 deviation remains in either render: the earlier image failures were the device-CPU arithmetic the
GPU had been made to reproduce. The HP passes the same flow (Principled 142 s: image 92 strict, stages 927 strict;
node render 95 s: 1,843 / 3,987; glass 60 s: 104 / 1,157; both replays), with the same image counts as merge wave 5.

**Triangle sampler sample 3133 (open item 1): FIXED.** Store probes in `triangle_light_vertices_compute`, replayed on
the Samsung from the captured probe slice, showed the deforming emitter's interpolated object transform off by 4 and
2 ULPs in exactly the two elements dominated by the quaternion product `w*z` (rows x/y, columns y/x): the rotation's
z component differed. Ordering barriers on the normalized quaternion (`triangle_light_normalize4`) remove it; barriers
on the products downstream (earlier attempts) did not, so the Adreno 730 reassociated the reciprocal-length product
with the next product the caller applies to the normalized value (the slerp sine weight; the compose scale on the
linear branch) (inference from where the barrier works). Each component of `triangle_light_normalize4` now leaves through
the header-word-3 ordering barrier (XOR with a word the device keeps at zero, as `svmn_order`). Samsung replay of the
captured slice: all 1,024 samples equal the Linux replay bit for bit (before: 15 differed), sample 3133 `P.y`
0.3799915 = glibc. Gate: `triangle_sample` PASS on the Samsung (0 practical, 865 strict, max 4.88e-4; before 2
practical, 1,069 strict, max 2.19e-3), `triangle_lighting` and its six replays pass; the HP passes (865 strict, as
before). Linux GPU outputs of the sampler, emitter probe, triangle lighting, shadow terminator and the three render
films are bit-identical to merge wave 5; the device ARM64 CPU's own miss at 3133 (0.380744) is a CPU diagnostic for
the Raspberry Pi 5 glibc ARM64 reference (Phase F), not a GPU item.

Regression set on the final tree (Samsung): `nodes_svm` PASS (541 cases, 18,450 strict, max 1.95e-3, 1,500 s), `principled_svm` PASS (121 s), `surface` PASS,
`lighting_mis` PASS, `glass_render` PASS, `triangle_sample` and `triangle_lighting` PASS.

## Milestone 1 bench through the managed bundle path (lane m1-asset, 2026-10-23)

The textured ForestCommunity bench (784 triangles, three materials, six reviewed 2K CC0 JPEGs) is
converted with its original materials by the glTF example tool (mapping "blender": Principled BSDF,
Image Texture, Mix Color, Separate Color, tangent-space Normal Map from the glTF TANGENT; textures
decoded with CodeBrix.Imaging) into a cbcOracleScene bundle (`native/oracle/scenes/assets/bench_01.json`
plus locally generated raw files, `make_bench_bundle.py`). The same bundle is rendered by the C oracle
runner and by the library's `OracleSceneBundle` loader (bit-identical CPU films on Linux), on Linux
WGSL, and inside the Android validation app (`android_app_gate.py --mode bundle`), judged against the
Linux x64 CPU film of the same bundle. Lighting: the converter's default sun and a constant world
(the HDRI variant `bench_01-hdri.json` needs environment importance sampling on WGSL). 64x48, 16 samples,
12,288 components per film. Practical / strict counts against the Linux CPU film:

| Scene | Linux WGSL (Intel UHD, Mesa 25.0.7) | Samsung SM-X700 (Adreno 730) | HP Android x64 (Intel Xe, Mesa 24.0.8) |
| --- | --- | --- | --- |
| Textured-plane control (same graph, 64x64 synthetic textures) | 0 / 1,641, max 1.42e-4 | 0 / 1,598, max 8.89e-5; first render 142 s | 0 / 1,636, max 1.09e-4; first render 214 s |
| Bench, original materials | 47 / 1,590, max 8.46e-3 | 51 / 1,602, max 8.46e-3; first render 138 s | 47 / 1,587, max 8.46e-3; first render 201 s |

Findings:
- Device memory. The WebGPU device keeps all scene data in one storage binding. WebGPU's default
  limit (128 MiB) failed the bench on Linux ("device arena exhausted"); the device now requests the
  adapter's maxBufferSize and maxStorageBufferBindingSize (at most 2 GiB); allocation is unchanged
  for scenes that fit the default (the arena still grows on demand; Linux GPU films of untouched
  scenes stay within their repeat variation, CPU films bit-identical). The exhaustion message now
  reports the limit, the arena, the bytes in use and the request. With the base-colour textures in
  colorspace `__builtin_srgb`, Cycles converts 8-bit images to half4 at load (two 32 MiB images; 128 MiB
  of images in all), which exceeded the Samsung's binding; `scene_linear_srgb` keeps them 8-bit and
  decodes sRGB while sampling (what Blender does) and the bench fits.
- Comparator (`native/oracle/compare.py`): the textured-plane control is "perfect" over the reference
  backends (Linux CPU, Linux WGSL, Samsung WGSL, HP WGSL: every pair 0 practical failures). The in-app
  Android CPU films are diagnostics (bionic C library); the HP's differs from the Linux CPU in 3
  components of the control.
- The practical failures on the bench are not texture-specific defects. Comparator matrix (bench):
  the three WGSL films agree with each other far better than with the oracle (Linux WGSL vs HP WGSL 0
  failures / 34 strict; Samsung WGSL vs either 11); the ARM64 CPU on the Samsung differs from the Linux
  x64 CPU in 41 components; the three WGSL films vs the Linux CPU 47 / 51 / 47. The Linux x64 CPU oracle is built with
  Cycles' `WITH_CYCLES_NATIVE_ONLY` (`-march=native -ffp-contract=on -freciprocal-math`; AVX2 + FMA on
  the i7-12850HX), while the WGSL device models the SSE4.2 arithmetic the gates' references use. With
  the CPU kernels rebuilt `-msse4.2` (diagnostic, not shipped), the Linux WGSL bench has 11 failures
  (max 6.2e-4) instead of 47, barycentric differences drop from 531 to 74 of about 1,500 hit pixels
  (1 spp), and the remainder follows camera-ray directions that differ in the last bit (95 components
  at 1 spp; the camera row of docs/PARITY.md lists these). High-frequency 2K textures turn those
  ulp-level u/v differences into texel differences above the practical criterion. The textured-plane
  control (64x64 textures) passes everywhere. Decision needed (not taken by the lane): build the Linux
  CPU oracle as the SSE4.2 baseline, and close the camera-ray last-bit differences.
- Decoder identity matters: CodeBrix.Imaging and libjpeg-turbo decode the six JPEGs to different
  pixels, so bench bundles record the decoder in `bundle-manifest.json`; copy a verified bundle between
  machines instead of decoding again.

Open items of the bench were items 9 to 13 of "Open device items" (9 to 11 since resolved). Evidence, frozen in
`native/evidence/merge-20261023b/m1-asset/`: `android-app/device-3-bench3-samsung`, `device-3-plane2-samsung`,
`device-4-bench3-hp`, `device-4-plane2-hp`, `oracles/`, `comparator-proof/` and the run logs.

## Merge wave 6: device tables

Generated by `native/work/merge6/tools/device_tables.py` from each run's `results.json` (merge wave 6, part 2; the runs are frozen in `native/evidence/merge-20261023b/merge6/runs/<device>/`). Wall times are device seconds per job; "Judged against" names the reference the result was judged against (glibc device-scene flow, the device's own CPU, or the source capture's CPU film).

Summary (merge wave 6, both parts). Every job passed: the Samsung SM-X700 116 of 116 (jobs and replays), the HP 96
of 96, the S20 41 of 41 (`principled_svm` 418 s; `principled_render` PASS through the stage exchange, 64 strict).
Linux films unchanged (glass, node render and Principled gate films as in merge wave 5). The Samsung strict counts
dropped with the stage exchange, as lane S3 predicted: Principled render 267 -> 68, node render 3,998 -> 1,728,
glass render 97 -> 63, triangle sampler 1,069 -> 865. The Samsung `nodes_world` pipeline compile took 95.8 s, over
the 60 s budget (the job passes; open item 13). 59 leftover scratch folders from 2026-10-18/19 were removed on each of
the Samsung and the HP (about 5.8 GB each). The bench bundle failed against the old (`-march=native`) oracle as
expected (open item 9, since resolved).

### samsung

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m6 | principled_svm | 118.6 | PASS | glibc, device-scene | strictFloatFailures=634, maximumAbsoluteError=3.61e-06 |
| materials-m6 | principled_bounce | 83.8 | PASS | device's own CPU / gate | strictFloatFailures=3466, maximumAbsoluteError=0.125 |
| materials-m6 | principled_render | 73.4 | PASS | glibc, device-scene | strictImageFailures=68, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=68 max=6.44e-06 |
| materials-m6 | nodes_svm | 1486.7 | PASS | glibc, device-scene | strictFloatFailures=18450, maximumAbsoluteError=0.00195 |
| materials-m6 | nodes_world | 492.8 | PASS | device's own CPU / gate | strictFloatFailures=16242, maximumAbsoluteError=6.1e-05 |
| materials-m6 | nodes_render | 113.5 | PASS | glibc, device-scene | strictImageFailures=1728, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1728 max=3.72e-05 |
| materials-m6 | principled-replay | 42.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.44e-06, strictImageFailures=68 |
| materials-m6 | nodes-replay | 90.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.72e-05, strictImageFailures=1728 |
| triangle-m6 | triangle_sample | 20.6 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| triangle-m6 | surface | 23.9 | PASS | device's own CPU / gate | strictFloatFailures=1246, maximumAbsoluteError=3.81e-06 |
| triangle-m6 | triangle_lighting | 77.5 | PASS | glibc, device-scene | maximumAbsoluteError=1.79e-06; host re-judged vs glibc PASS: practical=0 strict=516 max=1.79e-06 |
| triangle-m6 | replay-0 | 15.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| triangle-m6 | replay-1 | 20.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=5 |
| triangle-m6 | replay-2 | 20.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=8 |
| triangle-m6 | replay-3 | 21.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=235 |
| triangle-m6 | replay-4 | 21.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.73e-07, strictImageFailures=11 |
| triangle-m6 | replay-5 | 34.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=245 |
| emitter-m6 | emitter_sample | 7.8 | PASS | glibc, device-scene | maximumAbsoluteError=9.06e-06; host re-judged vs glibc PASS: practical=0 strict=135 max=9.06e-06 |
| regression-m6 | glass_render | 48.4 | PASS | glibc, device-scene | strictImageFailures=63, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=63 max=1.91e-06 |
| regression-m6 | lighting_mis | 29.7 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1070 max=6.08e-06 |
| regression-m6 | transparent_lighting | 89.5 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=693 max=6.02e-06 |
| lighting-m6 | lighting_mis | 29.0 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1070 max=6.08e-06 |
| lighting-m6 | broad_lighting | 57.0 | PASS | glibc, device-scene | maximumAbsoluteError=3.81e-05; host re-judged vs glibc PASS: practical=0 strict=993 max=3.81e-05 |
| lighting-m6 | lighting_mis-replay-0 | 16.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m6 | lighting_mis-replay-1 | 16.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m6 | broad_lighting-replay-0 | 16.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m6 | broad_lighting-replay-1 | 17.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.17e-07, strictImageFailures=15 |
| transparent-m6 | transparent_shadow | 17.9 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m6 | transparent_lighting | 90.5 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=693 max=6.02e-06 |
| transparent-m6 | transparent_lighting-replay-0 | 19.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m6 | transparent_lighting-replay-1 | 20.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m6 | transparent_lighting-replay-2 | 25.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=77 |
| transparent-m6 | transparent_lighting-replay-3 | 25.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.29e-06, strictImageFailures=134 |
| transparent-m6 | transparent_lighting-replay-4 | 26.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=168 |
| transparent-m6 | transparent_lighting-replay-5 | 26.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.9e-07, strictImageFailures=3 |
| transparent-m6 | transparent_lighting-replay-6 | 25.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=103 |
| transparent-m6 | transparent_lighting-replay-7 | 25.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=208 |
| transparent-m6 | transparent-shadow-replay-direct | 6.0 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m6 | transparent-shadow-replay-indexed | 6.3 | PASS | source capture CPU film | maximumAbsoluteImageError=0, strictImageFailures=0 |
| terminator-m6 | shadow_terminator | 98.0 | PASS | glibc, device-scene | maximumAbsoluteError=6.2e-05; host re-judged vs glibc PASS: practical=0 strict=124 max=6.2e-05 |
| terminator-m6 | terminator-replay-0 | 12.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-1 | 14.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-2 | 14.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-3 | 14.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-4 | 24.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m6 | terminator-replay-5 | 24.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m6 | terminator-replay-6 | 24.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m6 | terminator-replay-7 | 25.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m6 | terminator-replay-8 | 25.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.93e-05, strictImageFailures=8 |
| terminator-m6 | terminator-replay-9 | 33.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.91e-05, strictImageFailures=24 |
| spec-refusal-m6 | glass_render-spec | 46.3 | PASS | glibc, device-scene | strictImageFailures=63, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=63 max=1.91e-06 |
| spec-refusal-m6 | glass_render-nospec-refused | 0.2 | PASS | expected refusal |  |
| spec-refusal-m6 | glass_render-superset-refused | 0.2 | PASS | expected refusal |  |
| hostcap-linux-triangle | tri0 | 15.4 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| hostcap-linux-triangle | tri1 | 20.7 | PASS | source capture CPU film | maximumAbsoluteImageError=1.49e-07, strictImageFailures=1 |
| hostcap-linux-triangle | tri2 | 20.9 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| hostcap-linux-triangle | tri3 | 21.3 | PASS | source capture CPU film | maximumAbsoluteImageError=1.76e-06, strictImageFailures=236 |
| hostcap-linux-triangle | tri4 | 21.1 | PASS | source capture CPU film | maximumAbsoluteImageError=3.73e-07, strictImageFailures=8 |
| hostcap-linux-triangle | tri5 | 34.6 | PASS | source capture CPU film | maximumAbsoluteImageError=1.79e-06, strictImageFailures=257 |
| hostcap-linux-terminator | term0 | 12.2 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 14.3 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 14.5 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 14.7 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 24.9 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term5 | 25.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term6 | 25.3 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term7 | 25.1 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term8 | 25.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.93e-05, strictImageFailures=8 |
| hostcap-linux-terminator | term9 | 33.8 | PASS | source capture CPU film | maximumAbsoluteImageError=2.93e-05, strictImageFailures=22 |
| arm-m6 | microfacet_render | 44.7 | PASS | glibc, device-scene | strictImageFailures=100, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=100 max=2.38e-06 |
| motion-m6 | render-static | 45.3 | PASS | glibc, device-scene | strictImageFailures=327, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=327 max=7.63e-06 |
| motion-m6 | render-motion-0 | 43.7 | PASS | glibc, device-scene | strictImageFailures=345, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=345 max=2.53e-05 |
| motion-m6 | render-motion-1 | 43.7 | PASS | glibc, device-scene | strictImageFailures=306, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=306 max=7.63e-06 |
| motion-m6 | render-motion-2 | 43.8 | PASS | glibc, device-scene | strictImageFailures=446, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=446 max=0.000188 |
| motion-m6 | render-motion-3 | 42.1 | PASS | glibc, device-scene | strictImageFailures=655, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=655 max=4.01e-05 |
| motion-m6 | render-motion-4 | 42.2 | PASS | glibc, device-scene | strictImageFailures=445, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=445 max=1.53e-05 |
| motion-m6 | render-motion-5 | 42.9 | PASS | glibc, device-scene | strictImageFailures=465, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=465 max=3.05e-05 |
| motion-m6 | render-motion-6 | 41.3 | PASS | glibc, device-scene | strictImageFailures=586, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=586 max=4.58e-05 |
| motion-m6 | render-motion-7 | 41.8 | PASS | glibc, device-scene | strictImageFailures=443, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=443 max=1.53e-05 |
| pixel-motion-own | object-moving-images | 45.7 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-stationary-images | 35.3 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-replay-motion-0 | 29.6 | PASS | source capture CPU film | maximumAbsoluteImageError=3.58e-07, strictImageFailures=10 |
| pixel-motion-own | object-replay-motion-1 | 30.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.86e-06, strictImageFailures=26 |
| pixel-motion-own | object-replay-motion-2 | 30.9 | PASS | source capture CPU film | maximumAbsoluteImageError=2.4e-06, strictImageFailures=290 |
| pixel-motion-own | object-replay-motion-3 | 30.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.02e-07, strictImageFailures=1 |
| pixel-motion-own | object-replay-motion-4 | 31.2 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=295 |
| pixel-motion-own | object-replay-motion-5 | 31.2 | PASS | source capture CPU film | maximumAbsoluteImageError=4.53e-06, strictImageFailures=59 |
| pixel-motion-own | object-replay-motion-6 | 31.5 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=342 |
| pixel-motion-own | object-replay-motion-7 | 31.4 | PASS | source capture CPU film | maximumAbsoluteImageError=6.02e-06, strictImageFailures=246 |
| pixel-motion-x64 | object-moving-images | 45.3 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1306 max=6.08e-06 |
| pixel-motion-x64 | object-stationary-images | 36.3 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1157 max=6.08e-06 |
| pixel-motion-x64 | object-replay-motion-0 | 30.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.68e-06, strictImageFailures=18 |
| pixel-motion-x64 | object-replay-motion-1 | 32.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=16 |
| pixel-motion-x64 | object-replay-motion-2 | 32.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.52e-06, strictImageFailures=303 |
| pixel-motion-x64 | object-replay-motion-3 | 32.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.08e-09, strictImageFailures=0 |
| pixel-motion-x64 | object-replay-motion-4 | 32.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=302 |
| pixel-motion-x64 | object-replay-motion-5 | 33.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.29e-06, strictImageFailures=72 |
| pixel-motion-x64 | object-replay-motion-6 | 32.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=344 |
| pixel-motion-x64 | object-replay-motion-7 | 32.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=251 |
| volume-m6 | volume-state | 10.6 | PASS | device's own CPU / gate |  |
| volume-m6 | volume-absorption | 64.0 | PASS | glibc, device-scene | strictImageFailures=840, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=840 max=2.29e-05 |
| volume-m6 | volume-isotropic | 87.9 | PASS | glibc, device-scene | strictImageFailures=1437, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1437 max=3.05e-05 |
| volume-m6 | volume-forward | 82.4 | PASS | glibc, device-scene | strictImageFailures=1479, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1479 max=3.05e-05 |
| volume-m6 | volume-backward | 83.5 | PASS | glibc, device-scene | strictImageFailures=1369, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1369 max=3.05e-05 |
| volume-m6 | boundary-state | 23.2 | PASS | device's own CPU / gate |  |
| volume-m6 | boundary-compaction-guard | 4.4 | PASS | device's own CPU / gate |  |
| volume-m6 | closed-single | 97.0 | PASS | glibc, device-scene | strictImageFailures=819, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=819 max=3.05e-05 |
| volume-m6 | closed-overlap | 156.9 | PASS | glibc, device-scene | strictImageFailures=1013, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1013 max=3.05e-05 |
| volume-m6 | closed-inside | 147.0 | PASS | glibc, device-scene | strictImageFailures=2061, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=2061 max=3.05e-05 |
| volume-m6 | volume-replay-absorption | 15.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.29e-05, strictImageFailures=840 |
| volume-m6 | volume-replay-isotropic | 17.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1437 |
| volume-m6 | volume-replay-forward | 17.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1479 |
| volume-m6 | volume-replay-backward | 17.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1369 |
| volume-m6 | closed-replay-single | 18.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=819 |
| volume-m6 | closed-replay-overlap | 28.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1013 |
| volume-m6 | closed-replay-inside | 28.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=2061 |

### hp

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m6 | principled_svm | 163.8 | PASS | glibc, device-scene | strictFloatFailures=946, maximumAbsoluteError=9.54e-07 |
| materials-m6 | principled_bounce | 236.7 | PASS | device's own CPU / gate | strictFloatFailures=5237, maximumAbsoluteError=0.125 |
| materials-m6 | principled_render | 140.6 | PASS | glibc, device-scene | strictImageFailures=92, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=92 max=6.44e-06 |
| materials-m6 | nodes_svm | 806.4 | PASS | glibc, device-scene | strictFloatFailures=10978, maximumAbsoluteError=0.0107 |
| materials-m6 | nodes_world | 256.4 | PASS | device's own CPU / gate | strictFloatFailures=9125, maximumAbsoluteError=6.1e-05 |
| materials-m6 | nodes_render | 94.5 | PASS | glibc, device-scene | strictImageFailures=1843, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1843 max=0.000103 |
| materials-m6 | principled-replay | 117.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.44e-06, strictImageFailures=92 |
| materials-m6 | nodes-replay | 72.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=0.000103, strictImageFailures=1843 |
| triangle-m6 | triangle_sample | 22.7 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| triangle-m6 | surface | 38.2 | PASS | device's own CPU / gate | strictFloatFailures=1813, maximumAbsoluteError=3.81e-06 |
| triangle-m6 | triangle_lighting | 87.5 | PASS | glibc, device-scene | maximumAbsoluteError=2.32e-06; host re-judged vs glibc PASS: practical=0 strict=887 max=2.32e-06 |
| triangle-m6 | replay-0 | 36.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| triangle-m6 | replay-1 | 44.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| triangle-m6 | replay-2 | 45.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| triangle-m6 | replay-3 | 45.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-06, strictImageFailures=427 |
| triangle-m6 | replay-4 | 45.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.73e-07, strictImageFailures=10 |
| triangle-m6 | replay-5 | 63.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.26e-06, strictImageFailures=427 |
| emitter-m6 | emitter_sample | 19.8 | PASS | glibc, device-scene | maximumAbsoluteError=0.00513; host re-judged vs glibc PASS: practical=0 strict=235 max=0.00513 |
| regression-m6 | glass_render | 59.4 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| regression-m6 | lighting_mis | 49.5 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2681 max=8.34e-06 |
| regression-m6 | transparent_lighting | 76.5 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1682 max=7.75e-06 |
| lighting-m6 | lighting_mis | 48.1 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2681 max=8.34e-06 |
| lighting-m6 | broad_lighting | 68.6 | PASS | glibc, device-scene | maximumAbsoluteError=5.34e-05; host re-judged vs glibc PASS: practical=0 strict=1742 max=5.34e-05 |
| lighting-m6 | lighting_mis-replay-0 | 38.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=3 |
| lighting-m6 | lighting_mis-replay-1 | 38.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=11 |
| lighting-m6 | broad_lighting-replay-0 | 38.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=1 |
| lighting-m6 | broad_lighting-replay-1 | 38.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.07e-07, strictImageFailures=26 |
| transparent-m6 | transparent_shadow | 12.8 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m6 | transparent_lighting | 76.8 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1682 max=7.75e-06 |
| transparent-m6 | transparent_lighting-replay-0 | 41.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.49e-07, strictImageFailures=1 |
| transparent-m6 | transparent_lighting-replay-1 | 42.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=8 |
| transparent-m6 | transparent_lighting-replay-2 | 47.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=93 |
| transparent-m6 | transparent_lighting-replay-3 | 48.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.01e-06, strictImageFailures=323 |
| transparent-m6 | transparent_lighting-replay-4 | 48.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-06, strictImageFailures=435 |
| transparent-m6 | transparent_lighting-replay-5 | 49.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-07, strictImageFailures=4 |
| transparent-m6 | transparent_lighting-replay-6 | 48.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.56e-06, strictImageFailures=299 |
| transparent-m6 | transparent_lighting-replay-7 | 48.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.79e-06, strictImageFailures=519 |
| transparent-m6 | transparent-shadow-replay-direct | 5.8 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m6 | transparent-shadow-replay-indexed | 5.8 | PASS | source capture CPU film | maximumAbsoluteImageError=7.45e-09, strictImageFailures=0 |
| terminator-m6 | shadow_terminator | 148.9 | PASS | glibc, device-scene | maximumAbsoluteError=1.53e-05; host re-judged vs glibc PASS: practical=0 strict=97 max=1.53e-05 |
| terminator-m6 | terminator-replay-0 | 31.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-1 | 39.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-2 | 39.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-3 | 39.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m6 | terminator-replay-4 | 66.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| terminator-m6 | terminator-replay-5 | 66.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| terminator-m6 | terminator-replay-6 | 67.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| terminator-m6 | terminator-replay-7 | 66.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| terminator-m6 | terminator-replay-8 | 67.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=5 |
| terminator-m6 | terminator-replay-9 | 79.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.1e-06, strictImageFailures=12 |
| spec-refusal-m6 | glass_render-spec | 59.0 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| spec-refusal-m6 | glass_render-nospec-refused | 0.3 | PASS | expected refusal |  |
| spec-refusal-m6 | glass_render-superset-refused | 0.3 | PASS | expected refusal |  |
| hostcap-linux-triangle | tri0 | 39.6 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| hostcap-linux-triangle | tri1 | 47.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| hostcap-linux-triangle | tri2 | 47.6 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=14 |
| hostcap-linux-triangle | tri3 | 47.8 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-06, strictImageFailures=430 |
| hostcap-linux-triangle | tri4 | 47.4 | PASS | source capture CPU film | maximumAbsoluteImageError=3.73e-07, strictImageFailures=10 |
| hostcap-linux-triangle | tri5 | 66.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.26e-06, strictImageFailures=425 |
| hostcap-linux-terminator | term0 | 33.3 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 40.8 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 40.7 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 41.0 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 68.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| hostcap-linux-terminator | term5 | 68.0 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| hostcap-linux-terminator | term6 | 68.5 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=18 |
| hostcap-linux-terminator | term7 | 68.4 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=22 |
| hostcap-linux-terminator | term8 | 69.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-07, strictImageFailures=5 |
| hostcap-linux-terminator | term9 | 82.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.07e-06, strictImageFailures=17 |
| arm-m6 | microfacet_render | 58.8 | PASS | glibc, device-scene | strictImageFailures=119, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=119 max=1.91e-06 |
| motion-m6 | render-static | 44.3 | PASS | glibc, device-scene | strictImageFailures=406, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=406 max=7.63e-06 |
| motion-m6 | render-motion-0 | 47.7 | PASS | glibc, device-scene | strictImageFailures=422, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=422 max=2.53e-05 |
| motion-m6 | render-motion-1 | 47.9 | PASS | glibc, device-scene | strictImageFailures=386, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=386 max=1.14e-05 |
| motion-m6 | render-motion-2 | 47.5 | PASS | glibc, device-scene | strictImageFailures=601, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=601 max=0.000188 |
| motion-m6 | render-motion-3 | 48.1 | PASS | glibc, device-scene | strictImageFailures=792, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=792 max=3.71e-05 |
| motion-m6 | render-motion-4 | 46.7 | PASS | glibc, device-scene | strictImageFailures=660, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=660 max=4.48e-05 |
| motion-m6 | render-motion-5 | 46.9 | PASS | glibc, device-scene | strictImageFailures=647, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=647 max=0.000135 |
| motion-m6 | render-motion-6 | 46.0 | PASS | glibc, device-scene | strictImageFailures=761, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=761 max=2.86e-05 |
| motion-m6 | render-motion-7 | 46.6 | PASS | glibc, device-scene | strictImageFailures=661, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=661 max=4.48e-05 |
| volume-m6 | volume-state | 11.8 | PASS | device's own CPU / gate |  |
| volume-m6 | volume-absorption | 59.6 | PASS | glibc, device-scene | strictImageFailures=1143, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1143 max=1.53e-05 |
| volume-m6 | volume-isotropic | 73.7 | PASS | glibc, device-scene | strictImageFailures=1611, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1611 max=3.24e-05 |
| volume-m6 | volume-forward | 75.1 | PASS | glibc, device-scene | strictImageFailures=1669, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1669 max=4.67e-05 |
| volume-m6 | volume-backward | 75.7 | PASS | glibc, device-scene | strictImageFailures=1617, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1617 max=4.01e-05 |
| volume-m6 | boundary-state | 45.9 | PASS | device's own CPU / gate |  |
| volume-m6 | boundary-compaction-guard | 6.9 | PASS | device's own CPU / gate |  |
| volume-m6 | closed-single | 89.2 | PASS | glibc, device-scene | strictImageFailures=976, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=976 max=3.05e-05 |
| volume-m6 | closed-overlap | 143.0 | PASS | glibc, device-scene | strictImageFailures=1260, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1260 max=4.58e-05 |
| volume-m6 | closed-inside | 141.1 | PASS | glibc, device-scene | strictImageFailures=2284, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=2284 max=2.29e-05 |
| volume-m6 | volume-replay-absorption | 25.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=1143 |
| volume-m6 | volume-replay-isotropic | 26.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.24e-05, strictImageFailures=1611 |
| volume-m6 | volume-replay-forward | 26.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.67e-05, strictImageFailures=1669 |
| volume-m6 | volume-replay-backward | 26.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.01e-05, strictImageFailures=1617 |
| volume-m6 | closed-replay-single | 27.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=976 |
| volume-m6 | closed-replay-overlap | 52.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.58e-05, strictImageFailures=1260 |
| volume-m6 | closed-replay-inside | 53.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.29e-05, strictImageFailures=2284 |

### s20

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-s20-principled_svm | principled_svm | 418.3 | PASS | glibc, device-scene | strictFloatFailures=557, maximumAbsoluteError=3.59e-06 |
| a650-refusals-m6 | nodes_world-refused | 0.5 | PASS | expected refusal |  |
| a650-refusals-m6 | nodes_svm-refused | 0.4 | PASS | expected refusal |  |
| a650-refusals-m6 | nodes_render-refused | 0.5 | PASS | expected refusal |  |
| triangle-s20-triangle_sample | triangle_sample | 79.4 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| regression-s20-glass_render | glass_render | 84.6 | PASS | glibc, device-scene | strictImageFailures=64, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=64 max=1.91e-06 |
| regression-s20-lighting_mis | lighting_mis | 63.7 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1057 max=6.08e-06 |
| triangle-s20-surface | surface | 45.4 | PASS | device's own CPU / gate | strictFloatFailures=1244, maximumAbsoluteError=3.81e-06 |
| transparent-s20-transparent_lighting | transparent_lighting | 193.5 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=683 max=6.02e-06 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-0 | 50.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-1 | 51.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-2 | 61.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=72 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-3 | 61.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.55e-06, strictImageFailures=133 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-4 | 62.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=167 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-5 | 63.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.9e-07, strictImageFailures=3 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-6 | 61.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=103 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-7 | 62.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=205 |
| transparent-s20-transparent_shadow | transparent_shadow | 15.5 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-s20-transparent_shadow | transparent-shadow-replay-direct | 11.1 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_shadow | transparent-shadow-replay-indexed | 11.2 | PASS | source capture CPU film | maximumAbsoluteImageError=0, strictImageFailures=0 |
| materials-s20-principled_render | principled_render | 207.5 | PASS | glibc, device-scene | strictImageFailures=64, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=64 max=6.44e-06 |
| materials-s20-principled_render | principled-replay | 168.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.44e-06, strictImageFailures=64 |
| pixel-motion-s20-object-moving-images | object-moving-images | 140.4 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1276 max=6.08e-06 |
| pixel-motion-s20-object-moving-images | object-replay-motion-0 | 120.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.56e-06, strictImageFailures=12 |
| pixel-motion-s20-object-moving-images | object-replay-motion-1 | 120.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| pixel-motion-s20-object-moving-images | object-replay-motion-2 | 121.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.52e-06, strictImageFailures=301 |
| pixel-motion-s20-object-moving-images | object-replay-motion-3 | 121.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-s20-object-moving-images | object-replay-motion-4 | 131.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=294 |
| pixel-motion-s20-object-moving-images | object-replay-motion-5 | 177.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.05e-06, strictImageFailures=70 |
| pixel-motion-s20-object-moving-images | object-replay-motion-6 | 173.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=338 |
| pixel-motion-s20-object-moving-images | object-replay-motion-7 | 180.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=249 |
| pixel-motion-s20-deforming-moving-images | deforming-moving-images | 322.8 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1213 max=6.08e-06 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-0 | 120.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=2 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-1 | 176.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-07, strictImageFailures=19 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-2 | 176.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.4e-06, strictImageFailures=304 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-3 | 176.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-4 | 153.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=280 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-5 | 125.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.17e-06, strictImageFailures=49 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-6 | 123.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=300 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-7 | 123.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=259 |
| materials-s20-principled_bounce | principled_bounce | 336.0 | PASS | device's own CPU / gate | strictFloatFailures=2796, maximumAbsoluteError=0.125 |

## Environment lighting and non-constant emitters (lane m1-lighting, 2026-10-23)

The registered device now samples the world as a light, as the pinned Cycles does, and shades non-constant light
shaders at the sampled point.

- **Importance map.** Cycles' unmodified `LightManager::device_update_background` builds
  `light_background_marginal_cdf` / `light_background_conditional_cdf` from the world shader through `ShaderEval`;
  on this device that is the new WGSL entry `shader_eval_background` (`kernel_background_evaluate`, background module,
  dispatched in slices of the 4,096 SVM stack slots). The two tables are scene tables after the existing ones
  (`wavefront.h`), so the CPU references and the Android scene exchange carry exactly the device-built map.
  Cycles enables the background light only for a direction-dependent world with `use_mis`
  (`LightManager::test_enabled_lights`); a consumer must create a `backgroundlight` object for world MIS and should set
  its `map_resolution` (with 0 the pinned code picks 4,096 x 2,048 when there is no sky-texture sun, whose evaluation
  buffers exceed one 128 MiB storage binding and fail with an allocation error).
- **Light stage.** A `LIGHT_BACKGROUND` lamp samples the map (`background_map_sample`, binary32 steps in the host's
  order, precise sine/cosine/arccosine; `wavefront_background_light.wgsl`). Portals, sky-texture sun guiding, more than
  one background light and non-constant point/spot/area/sun shaders are refused on the host.
- **SHADE_LIGHT_NEE.** When the sampled light's shader has no constant emission, `connect_surface_light` skips the
  light-threshold roulette and queues the shadow path to `DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_NEE`
  (`light_sample_shader_eval_nee_constant`). That kernel is two entries (`prepare_light_nee` writes the light point's
  ShaderData: world for the background light, `triangle_light_uv` + `shader_setup_from_ray` for a triangle;
  `shade_light_nee` evaluates the emission shader, applies the roulette with `bsdf_eval_average` and multiplies the
  shadow throughput), so each entry stays inside the compile envelope. Capture entries 18-20, recorder v9.
- **Background stage.** `light_sample_mis_weight_forward_background` (background light PDF x lamp selection
  probability, power heuristic) and `background_light_clamp_dD` for non-camera rays.
- **Fixed along the way.** (a) The triangle self-intersection skip of `integrate_surface_direct_light` was missing
  in NEE; it is reachable once an emitter also has a BSDF (mixed emission/diffuse emitter failed by 1e-3 before).
  (b) The environment-texture equirectangular projection used the driver's atan2/acos and division (up to 2e-3
  relative on Intel; HDRI sample evaluations near bright texels were 5.7e-4 off). It now takes binary32 steps in
  the host's order with `trig_atan2` and `trig_acos` (`wavefront_trig.wgsl`), which equal glibc 2.41's correctly
  rounded atan2f/acosf bit for bit on a 65,536-input probe (`CODEBRIX_ENVIRONMENT_TRIG_PROBE`), and the map
  directions use `native_disk_sincos` (glibc sinf/cosf bit for bit on [-pi, pi]). This exactness is required, not
  cosmetic: with a 2000-radiance sun texel, one ULP of an environment coordinate moves a linearly interpolated
  lookup by more than the practical bound (the bench-like case 11 failed by 8e-4 relative with 1-ULP arithmetic).
  `motion_acos` (used by motion code, left unchanged) is one ULP off for about 9% of negative inputs because it
  rounds pi - acos(|x|) in two steps.
- **Feature bits** 64 `LIGHT_BACKGROUND` and 65 `LIGHT_NEE`; scenes without them compile the same handlers as
  before, and every film of the Linux regression set is bit-identical to merge wave 5 except 1-ULP differences in
  cases whose own repeat already differs (atomic film-add order: lighting_mis 5, transparent_lighting 4/5,
  broad_lighting 3/7/8/9, feature_combination 5).

Gate: `codebrix_cycles_environment_gate OUT [CASE...]` (12 cases: procedural and HDRI worlds with MIS at map
resolutions 32-512 (case 11: a 512 x 256 HDRI with a 1.5-degree, 2000-radiance sun, as in the bench HDRI), procedural and HDRI worlds without a background light, image/UV, procedural and mixed
emission/BSDF triangle emitters, and an emitter with an HDRI world). Each case checks the importance-map pixels and
2,048 light samples with the sampled light's shader evaluation against the original CPU kernels, then the native
scheduler image twice (repeat bitwise, command capture). All three CPU references go through the reference policy
(keys `case-N.map`, `.samples`, `.film`).

| Device | Environment gate (12 cases) | Wall | Regression set of the touched stages |
| --- | --- | --- | --- |
| Linux x64 (Intel, Mesa 25.0.7) | PASS: map 628,224 checks (56 strict, max 1.9e-6), samples 200,704 (3,393 strict, max 1.56e-3 on values up to 2000), films 73,728 (3,970 strict, max 1.63e-4), repeat bitwise, 12,572 SHADE_LIGHT_NEE paths | 127 s | PASS; films bit-identical to merge wave 5 apart from the atomic-order cases above; superset identity (glass, Principled, environment groups) bit-identical |
| HP (Mesa 24.0.8, Bliss) | PASS against the glibc device-scene references (host re-judged), 4 replays PASS | 350 s (slowest pipeline 33.5 s) | PASS (lighting_mis, triangle sampler and images, transparent lighting, emitter probe, background, glass/Principled/node renders, image, node worlds, replays) |
| Samsung SM-X700 (Adreno 730) | PASS against the glibc device-scene references (map max 0, films max 1.45e-5), 12 replays PASS | 433 s (slowest pipeline 18.4 s) | PASS except open items 1 and 4 with unchanged values (triangle sampler 3133, Principled render 9114, node render 6720; the lane's tree predates their fix by lane S3) |

Evidence: the lane's runs, frozen in `native/evidence/merge-20261023c/m1-lighting/runs/` (`dev26`, `regress3`,
`superset1/2`, `envelope2`, `samsung/m1l-s2`, `hp/m1l-hp4`; earlier rounds `hp/m1l-hp1` and `samsung/m1l-s1` ran the
same regression set on the earlier binaries); the ledger checkpoint `native/evidence/m1-lighting-20261023/environment-light/`.
Compile envelope of the new entries (Linux, gate scenes): prepare_light_nee 18,875, shade_light_nee 22,285,
shader_eval_background 39,118 backend instructions; on a Principled + texture scene shade_light_nee is about the size of
prepare_surface, so it carries that scene's size-guard warning too.

## Fixed-baseline CPU oracle, camera-ray and attribute parity, the bench passes (lane m1-oracle, 2026-10-23)

**Oracle definition (decision of 2026-10-23).** The CPU device every GPU film is judged against is an unmodified
Cycles CPU kernel built at a fixed arithmetic baseline, so its films do not depend on the machine that built it:

- x86-64: Cycles' SSE4.2 kernels only. `build_cycles.py` turns `WITH_CYCLES_NATIVE_ONLY` off (it added
  `-march=native`) and records Cycles' AVX2/F16C probes as absent (no AVX2 kernel, no FMA).
- No floating-point contraction and no reciprocal math: `inject_cycles.cmake` adds
  `-ffp-contract=off -fno-reciprocal-math` as directory compile options, which follow Cycles' own
  `-ffp-contract=on -freciprocal-math` on every command line (all Cycles and CodeBrix translation units; the
  reference colorspace object in `host/targets.cmake` gets them too). Android keeps Cycles' flags (its CPU is a
  diagnostic). Linux ARM64: the ARMv8-A base level (no `-march`/`-mcpu`), cross build verified here; macOS arm64
  and x64: the same rule in their recipes, UNTESTED (MACOS_ARM64.md, MACOS_X64.md).
- The CPU kernel compile line actually used (Linux x64, from the compile database, recorded in
  `native/work/cycles-build.json` and in every oracle run's `meta.json` as `cpuKernelCompile`):
  `-msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -ffp-contract=on -freciprocal-math -mfpmath=sse -O3
  -ffp-contract=off -fno-reciprocal-math` (the last occurrence decides). `build_cycles.py` refuses any other
  line. `native/cycles/cpu_arithmetic.cpp` is compiled into `cycles_kernel_cpu` with the kernels' flags, refuses
  an x86-64 oracle with AVX/FMA or reciprocal math, and states the arithmetic in the capability report:
  `backendScope.cpu.arithmetic` = `{"isa":"x86-64 SSE4.2","fpContract":"off","reciprocalMath":"off"}`.
- Fused multiply-add instructions in `libcodebrix_cycles_host.so`: 8,806 before, 0 after (x64); 21,010 in the
  previous ARM64 build, 0 after.

**What changes and what does not.**

- Unaffected (proved): the device-scene glibc flow. The gate binaries built before and after the flag change
  produce bit-identical references from the devices' dumped scenes (HP `triangle_lighting` 6 films and
  `triangle_sample`, S20 `lighting_mis` 8 films), equal to the merge-5 references.
- Changed: every Session-based CPU film (C runner, managed CpuReference backend, host-gate references) and the
  Linux-local independent CPU images of the gates. The gates' own reference programs were already compiled
  `-ffp-contract=off -msse4.2`, but they share the Cycles scene preparation, which was a `-march=native` FMA
  build: the glass render gate's CPU film is `5c441a51` with the old binaries and `da52a2d8` with the new
  ones, same sources. Repeat renders stay bitwise identical; no GPU-versus-CPU verdict got worse.

| Film set (same sources, old flags -> fixed baseline) | CPU films changed | GPU vs CPU practical failures (sum) | strict (sum) |
| --- | --- | --- | --- |
| Oracle corpus: 4 starters + 45 Linux-gate scenes (2 refused by WGSL, as before) | 49 of 49 | 0 -> 0 | 5,403 -> 4,859 |
| glTF tool: sample, sample-sun, factor-only bench, textured plane, bench (Blender mapping) | 5 of 5 | 50 -> 11 | 4,899 -> 3,773 |
| Host gate: 786 reference films + inputs + ABI CPU films | 809 of 854 files | 0 -> 0 | 149 -> 153 (ABI light films, strict not required) |

Selected old -> new CPU film-0 hashes: starter diffuse-triangle `e03583fd` -> `a9a2d6e7`, glass-and-area-light
`028cf600` -> `73dc5469`, glossy-sphere `7baffd34` -> `ea6832c7`, textured-plane `2b2c63e0` -> `0b443dbf`;
bench (Blender mapping) `445d954e` -> `f4b08b95`, factor-only bench `8873becc` -> `44683a24`, textured-plane
control `5a25410b` -> `a1d20c08`. The full table (54 films with practical/strict before and after) is frozen in
`native/evidence/merge-20261023c/m1-oracle/` (`hash-table-flags.md`, `hash-table-old-final.md`,
`hash-table-new-final.md` and their `.json`). The factor-only bench's pixel (45,22) (3 practical failures, max 0.0278, a single-path divergence at the
seed) was the FMA: it passes on the baseline without any WGSL change. The environment gate (12 cases) passes on the
baseline: map 628,224 checks (56 strict), samples 200,704 (3,179 strict, was 3,393), films 73,728 (2,684 strict,
max 2.86e-5; was 3,970 and 1.63e-4).

**Camera rays bit-exact (`wavefront_camera.wgsl`).** The camera gate now reports, per ray field, how many
components differ from the CPU in any bit (`rayBitDifferences`, by case and with examples). On the baseline it
found `ray.D` 15,258, `ray.P` 216, `dD` 12,519, `dP` 12,090 and `tmax` 15 of 22,972 paths (all within the
practical bound). Causes and fixes, all with integer `rounded_add` sums so no driver can contract them:

- `transform_direction` (camera to world): Cycles' SSE build accumulates `z*c2`, then `y*c1`, then `x*c0` with
  unfused `madd`; the WGSL let the driver fuse them. New `camera_direction` (SSE grouping, or the scalar
  `(x+y)+z` dot for a scalar oracle).
- `ray->P += nearclip*D`, `interp()` of the perspective motion, the filter and shutter table lookups
  (`(1-t)*a + t*b`) and `len()` of the differentials (the builtin `length` is not correctly rounded).
- The scrambling-distance Cranley-Patterson jitter in `native_rng` (`wavefront_common.wgsl`, shared): the
  product was fused into the sum (cases with distance 0.75 and 0.9 differed; 0.125 is exact either way).
- Rolling shutter: Intel Mesa reassociated `(1 - y/h) - 0.5` with the constant; every sum is now opaque.

Result: 0 differing bits in every ray field over the 24 cases. With camera motion (`--motion`) strict differences
fall from 3,533 (old binaries) to 1,616; time and tmax are exact, the rest is the shared decomposed-motion
interpolation (`wavefront_motion.wgsl`, not changed here; within the practical bound).

**Triangle attributes (`wavefront_attributes.wgsl`, shared).** With exact camera rays the bench still had 2
practical failures. Stage outputs rendered at 1 spp (emission shaders that output Incoming, Position,
Parametric, UV, texture colour, normal, roughness) located them: rays, positions and barycentrics equal the CPU
bit for bit, UVs differed in 296 components. `triangle_interpolate` `(1-u-v)*f0 + u*f1 + v*f2` and the
`dfdx/dfdy` sums were fused by the driver. Each product now passes the order word (header word 3, always 0; the
`svmn_order` mechanism) before the left-to-right sum: UVs are bit-identical and the bench passes. The cost is 112
backend instructions in the bench's `prepare_surface`.

`motion_inverse_cross` modelled Cycles' AVX2-only `_mm_fmsub_ps`; the baseline oracle takes the unfused
`cross()`, so it is now `rounded_cross` (object and deforming motion scenes: practical 0 before and after,
strict within run-to-run variation).

**The bench (Milestone 1 acceptance).** 64x48, 16 spp, 12,288 components, Blender mapping, judged against the
Linux CPU film of the same bundle; device films through the validation app's bundle mode (managed loader):

| Scene | Linux WGSL (Intel, Mesa 25.0.7) | Samsung SM-X700 | HP (Mesa 24.0.8) | compare.py over the four |
| --- | --- | --- | --- | --- |
| bench_01 (sun + constant world) | 0 / 199, max 6.97e-5 | 0 / 110, max 4.30e-5, 183 s | 0 / 212, max 3.78e-5, 236 s | perfect |
| bench_01-hdri (importance-sampled HDRI, map 512) | 0 / 367, max 9.16e-5 | 0 / 316, max 7.71e-5, 355 s | 0 / 435, max 9.17e-5, 330 s | perfect |
| textured-plane control | 0 / 656, max 5.53e-5 | 0 / 503, max 1.65e-4, 192 s | 0 / 776, max 1.62e-4, 241 s | perfect |

Practical / strict, maximum absolute error, first GPU render including pipeline compiles. The closest component
is bench pixel (46,19) R at 0.59 of the tolerance on Linux (HP against Linux WGSL 0.91): texture sampling still
differs from the CPU in the last bit (1,356 components at 1 spp, max 3.6e-7), the next site to make exact.

Open items (in "Open device items"): the bench's `prepare_surface` size (item 12), texture-sampling last-bit
parity (item 14), the motion arithmetic's last bits (item 15), the macOS recipes at the baseline (item 16).
Evidence, frozen in `native/evidence/merge-20261023c/m1-oracle/`: the hash tables above, `camera/` (per-field ray
bit differences), `android-app/s1-*` and `h1-*` (the bundle runs on the Samsung and the HP), `oracles-assets/` and `comparator/`,
`linux-arm64-record/`, `regress/`, and the S20 runs `runs/s20` and `runs/s20-run2-baseline-without-a650`.

## Merge wave 7 (2026-10-23)

Host gate criterion: the GPU 64-sample host film (`gpu/run.3`, `gpu-exit/run.3`, against the session64 CPU
reference) is judged practical-required with strict differences recorded, as the ABI light films are, in
`host_gate.py` and `android_host_gate.py`, because its atomic accumulation order varies between runs of one binary and
the fixed-baseline CPU reference moved one float step: on the merged tree it failed strict in 3 of 4 Linux runs at
component 1742 (difference 1.49e-7 against a bound of 1.30e-7, every run within practical). The default Android and
Linux ARM64 gate set (`build_cycles_android.py`) now includes `principled_svm`, `transparent_shadow` and
`codebrix_cycles_environment_gate`.

## Merge wave 7: device tables

Generated by `native/work/merge7/tools/device_tables.py` from each run's `results.json` (merge wave 7, part 2; the runs are frozen in `native/evidence/merge-20261023c/merge7/runs/<device>/`). Wall times are device seconds per job; "Judged against" names the reference the result was judged against (glibc device-scene flow, the device's own CPU, or the source capture's CPU film).

### samsung

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m7 | principled_svm | 118.6 | PASS | glibc, device-scene | strictFloatFailures=634, maximumAbsoluteError=3.61e-06 |
| materials-m7 | principled_bounce | 85.3 | PASS | device's own CPU / gate | strictFloatFailures=3466, maximumAbsoluteError=0.125 |
| materials-m7 | principled_render | 68.4 | PASS | glibc, device-scene | strictImageFailures=67, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=67 max=6.44e-06 |
| materials-m7 | nodes_svm | 1506.7 | PASS | glibc, device-scene | strictFloatFailures=18450, maximumAbsoluteError=0.00195 |
| materials-m7 | nodes_world | 505.8 | PASS | device's own CPU / gate | strictFloatFailures=16230, maximumAbsoluteError=6.1e-05 |
| materials-m7 | nodes_render | 115.5 | PASS | glibc, device-scene | strictImageFailures=1689, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1689 max=3.72e-05 |
| materials-m7 | principled-replay | 43.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.44e-06, strictImageFailures=67 |
| materials-m7 | nodes-replay | 91.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.72e-05, strictImageFailures=1689 |
| triangle-m7 | triangle_sample | 21.3 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| triangle-m7 | surface | 24.1 | PASS | device's own CPU / gate | strictFloatFailures=1246, maximumAbsoluteError=3.81e-06 |
| triangle-m7 | triangle_lighting | 80.6 | PASS | glibc, device-scene | maximumAbsoluteError=1.79e-06; host re-judged vs glibc PASS: practical=0 strict=490 max=1.79e-06 |
| triangle-m7 | replay-0 | 16.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| triangle-m7 | replay-1 | 21.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=0 |
| triangle-m7 | replay-2 | 21.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=6 |
| triangle-m7 | replay-3 | 22.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=233 |
| triangle-m7 | replay-4 | 21.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.58e-07, strictImageFailures=8 |
| triangle-m7 | replay-5 | 35.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-06, strictImageFailures=231 |
| emitter-m7 | emitter_sample | 8.0 | PASS | glibc, device-scene | maximumAbsoluteError=9.06e-06; host re-judged vs glibc PASS: practical=0 strict=135 max=9.06e-06 |
| regression-m7 | glass_render | 50.3 | PASS | glibc, device-scene | strictImageFailures=63, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=63 max=1.91e-06 |
| regression-m7 | lighting_mis | 29.5 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1057 max=6.08e-06 |
| regression-m7 | transparent_lighting | 91.4 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=685 max=6.02e-06 |
| lighting-m7 | lighting_mis | 29.4 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1057 max=6.08e-06 |
| lighting-m7 | broad_lighting | 59.6 | PASS | glibc, device-scene | maximumAbsoluteError=3.81e-05; host re-judged vs glibc PASS: practical=0 strict=953 max=3.81e-05 |
| lighting-m7 | lighting_mis-replay-0 | 16.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m7 | lighting_mis-replay-1 | 17.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m7 | broad_lighting-replay-0 | 17.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| lighting-m7 | broad_lighting-replay-1 | 17.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.17e-07, strictImageFailures=13 |
| transparent-m7 | transparent_shadow | 17.6 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m7 | transparent_lighting | 92.8 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=685 max=6.02e-06 |
| transparent-m7 | transparent_lighting-replay-0 | 20.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m7 | transparent_lighting-replay-1 | 20.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m7 | transparent_lighting-replay-2 | 25.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=73 |
| transparent-m7 | transparent_lighting-replay-3 | 26.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.55e-06, strictImageFailures=133 |
| transparent-m7 | transparent_lighting-replay-4 | 27.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=167 |
| transparent-m7 | transparent_lighting-replay-5 | 27.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.9e-07, strictImageFailures=3 |
| transparent-m7 | transparent_lighting-replay-6 | 26.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=103 |
| transparent-m7 | transparent_lighting-replay-7 | 26.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=206 |
| transparent-m7 | transparent-shadow-replay-direct | 6.0 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m7 | transparent-shadow-replay-indexed | 6.1 | PASS | source capture CPU film | maximumAbsoluteImageError=0, strictImageFailures=0 |
| terminator-m7 | shadow_terminator | 100.7 | PASS | glibc, device-scene | maximumAbsoluteError=6.2e-05; host re-judged vs glibc PASS: practical=0 strict=114 max=6.2e-05 |
| terminator-m7 | terminator-replay-0 | 13.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-1 | 15.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-2 | 15.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-3 | 15.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-4 | 25.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m7 | terminator-replay-5 | 25.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m7 | terminator-replay-6 | 25.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| terminator-m7 | terminator-replay-7 | 25.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| terminator-m7 | terminator-replay-8 | 26.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.93e-05, strictImageFailures=8 |
| terminator-m7 | terminator-replay-9 | 37.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.91e-05, strictImageFailures=14 |
| spec-refusal-m7 | glass_render-spec | 47.5 | PASS | glibc, device-scene | strictImageFailures=63, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=63 max=1.91e-06 |
| spec-refusal-m7 | glass_render-nospec-refused | 0.2 | PASS | expected refusal |  |
| spec-refusal-m7 | glass_render-superset-refused | 0.2 | PASS | expected refusal |  |
| environment-m7 | environment | 455.8 | PASS | glibc, device-scene | maximumAbsoluteError=3.81e-06; host re-judged vs glibc PASS: practical=0 strict=349 max=3.81e-06 |
| environment-m7 | environment-replay-0 | 25.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.86e-06, strictImageFailures=48 |
| environment-m7 | environment-replay-1 | 39.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-06, strictImageFailures=36 |
| environment-m7 | environment-replay-2 | 33.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.86e-06, strictImageFailures=40 |
| environment-m7 | environment-replay-3 | 40.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-06, strictImageFailures=30 |
| environment-m7 | environment-replay-4 | 42.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=62 |
| environment-m7 | environment-replay-5 | 36.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=28 |
| environment-m7 | environment-replay-6 | 64.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=5 |
| environment-m7 | environment-replay-7 | 61.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| environment-m7 | environment-replay-8 | 61.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.54e-07, strictImageFailures=41 |
| environment-m7 | environment-replay-9 | 63.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.53e-07, strictImageFailures=4 |
| environment-m7 | environment-replay-10 | 64.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.43e-06, strictImageFailures=39 |
| environment-m7 | environment-replay-11 | 61.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.43e-06, strictImageFailures=16 |
| arm-m7 | microfacet_render | 46.0 | PASS | glibc, device-scene | strictImageFailures=100, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=100 max=2.38e-06 |
| motion-m7 | render-static | 45.7 | PASS | glibc, device-scene | strictImageFailures=326, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=326 max=7.63e-06 |
| motion-m7 | render-motion-0 | 42.9 | PASS | glibc, device-scene | strictImageFailures=322, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=322 max=2.53e-05 |
| motion-m7 | render-motion-1 | 43.8 | PASS | glibc, device-scene | strictImageFailures=287, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=287 max=7.63e-06 |
| motion-m7 | render-motion-2 | 44.4 | PASS | glibc, device-scene | strictImageFailures=351, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=351 max=1.67e-05 |
| motion-m7 | render-motion-3 | 44.4 | PASS | glibc, device-scene | strictImageFailures=602, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=602 max=3.36e-05 |
| motion-m7 | render-motion-4 | 43.8 | PASS | glibc, device-scene | strictImageFailures=328, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=328 max=1e-05 |
| motion-m7 | render-motion-5 | 44.5 | PASS | glibc, device-scene | strictImageFailures=312, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=312 max=1.57e-05 |
| motion-m7 | render-motion-6 | 42.7 | PASS | glibc, device-scene | strictImageFailures=515, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=515 max=4.58e-05 |
| motion-m7 | render-motion-7 | 43.1 | PASS | glibc, device-scene | strictImageFailures=327, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=327 max=1e-05 |
| pixel-motion-own | object-moving-images | 45.2 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-stationary-images | 36.3 | PASS | device's own CPU / gate | maximumAbsoluteError=6.08e-06 |
| pixel-motion-own | object-replay-motion-0 | 31.2 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-07, strictImageFailures=6 |
| pixel-motion-own | object-replay-motion-1 | 32.2 | PASS | source capture CPU film | maximumAbsoluteImageError=2.92e-06, strictImageFailures=17 |
| pixel-motion-own | object-replay-motion-2 | 32.2 | PASS | source capture CPU film | maximumAbsoluteImageError=2.43e-06, strictImageFailures=290 |
| pixel-motion-own | object-replay-motion-3 | 32.5 | PASS | source capture CPU film | maximumAbsoluteImageError=1.02e-07, strictImageFailures=1 |
| pixel-motion-own | object-replay-motion-4 | 32.9 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=288 |
| pixel-motion-own | object-replay-motion-5 | 33.9 | PASS | source capture CPU film | maximumAbsoluteImageError=4.53e-06, strictImageFailures=59 |
| pixel-motion-own | object-replay-motion-6 | 33.0 | PASS | source capture CPU film | maximumAbsoluteImageError=6.08e-06, strictImageFailures=335 |
| pixel-motion-own | object-replay-motion-7 | 32.9 | PASS | source capture CPU film | maximumAbsoluteImageError=6.02e-06, strictImageFailures=241 |
| pixel-motion-x64 | object-moving-images | 46.7 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1180 max=6.08e-06 |
| pixel-motion-x64 | object-stationary-images | 36.8 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1135 max=6.08e-06 |
| pixel-motion-x64 | object-replay-motion-0 | 33.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.62e-06, strictImageFailures=8 |
| pixel-motion-x64 | object-replay-motion-1 | 34.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.36e-07, strictImageFailures=10 |
| pixel-motion-x64 | object-replay-motion-2 | 34.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.4e-06, strictImageFailures=291 |
| pixel-motion-x64 | object-replay-motion-3 | 34.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-x64 | object-replay-motion-4 | 34.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=271 |
| pixel-motion-x64 | object-replay-motion-5 | 34.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.29e-06, strictImageFailures=45 |
| pixel-motion-x64 | object-replay-motion-6 | 34.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=329 |
| pixel-motion-x64 | object-replay-motion-7 | 35.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=226 |
| volume-m7 | volume-state | 10.3 | PASS | device's own CPU / gate |  |
| volume-m7 | volume-absorption | 62.0 | PASS | glibc, device-scene | strictImageFailures=834, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=834 max=2.29e-05 |
| volume-m7 | volume-isotropic | 84.0 | PASS | glibc, device-scene | strictImageFailures=1429, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1429 max=3.05e-05 |
| volume-m7 | volume-forward | 79.0 | PASS | glibc, device-scene | strictImageFailures=1472, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1472 max=3.05e-05 |
| volume-m7 | volume-backward | 79.3 | PASS | glibc, device-scene | strictImageFailures=1366, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1366 max=3.05e-05 |
| volume-m7 | boundary-state | 23.2 | PASS | device's own CPU / gate |  |
| volume-m7 | boundary-compaction-guard | 4.4 | PASS | device's own CPU / gate |  |
| volume-m7 | closed-single | 98.9 | PASS | glibc, device-scene | strictImageFailures=813, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=813 max=3.05e-05 |
| volume-m7 | closed-overlap | 153.2 | PASS | glibc, device-scene | strictImageFailures=1007, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1007 max=3.05e-05 |
| volume-m7 | closed-inside | 145.5 | PASS | glibc, device-scene | strictImageFailures=2054, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=2054 max=3.05e-05 |
| volume-m7 | volume-replay-absorption | 16.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.29e-05, strictImageFailures=834 |
| volume-m7 | volume-replay-isotropic | 17.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1429 |
| volume-m7 | volume-replay-forward | 17.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1472 |
| volume-m7 | volume-replay-backward | 17.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1366 |
| volume-m7 | closed-replay-single | 18.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=813 |
| volume-m7 | closed-replay-overlap | 28.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=1007 |
| volume-m7 | closed-replay-inside | 28.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=2054 |
| hostcap-linux-triangle | tri0 | 16.8 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=12 |
| hostcap-linux-triangle | tri1 | 21.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| hostcap-linux-triangle | tri2 | 21.3 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=12 |
| hostcap-linux-triangle | tri3 | 22.1 | PASS | source capture CPU film | maximumAbsoluteImageError=1.76e-06, strictImageFailures=233 |
| hostcap-linux-triangle | tri4 | 21.8 | PASS | source capture CPU film | maximumAbsoluteImageError=2.53e-07, strictImageFailures=5 |
| hostcap-linux-triangle | tri5 | 35.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.79e-06, strictImageFailures=237 |
| hostcap-linux-terminator | term0 | 12.7 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 14.7 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 14.9 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 15.0 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 25.5 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term5 | 26.0 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term6 | 25.6 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-05, strictImageFailures=19 |
| hostcap-linux-terminator | term7 | 26.6 | PASS | source capture CPU film | maximumAbsoluteImageError=6.2e-05, strictImageFailures=27 |
| hostcap-linux-terminator | term8 | 26.2 | PASS | source capture CPU film | maximumAbsoluteImageError=2.93e-05, strictImageFailures=9 |
| hostcap-linux-terminator | term9 | 35.2 | PASS | source capture CPU film | maximumAbsoluteImageError=2.91e-05, strictImageFailures=15 |

### hp

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-m7 | principled_svm | 162.3 | PASS | glibc, device-scene | strictFloatFailures=946, maximumAbsoluteError=9.54e-07 |
| materials-m7 | principled_bounce | 234.2 | PASS | device's own CPU / gate | strictFloatFailures=5230, maximumAbsoluteError=0.125 |
| materials-m7 | principled_render | 140.2 | PASS | glibc, device-scene | strictImageFailures=91, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=91 max=6.44e-06 |
| materials-m7 | nodes_svm | 802.8 | PASS | glibc, device-scene | strictFloatFailures=10978, maximumAbsoluteError=0.0107 |
| materials-m7 | nodes_world | 257.2 | PASS | device's own CPU / gate | strictFloatFailures=9052, maximumAbsoluteError=6.1e-05 |
| materials-m7 | nodes_render | 96.4 | PASS | glibc, device-scene | strictImageFailures=1794, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1794 max=0.000103 |
| materials-m7 | principled-replay | 118.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.44e-06, strictImageFailures=91 |
| materials-m7 | nodes-replay | 73.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=0.000103, strictImageFailures=1794 |
| triangle-m7 | triangle_sample | 23.1 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| triangle-m7 | surface | 38.8 | PASS | device's own CPU / gate | strictFloatFailures=1801, maximumAbsoluteError=3.81e-06 |
| triangle-m7 | triangle_lighting | 102.6 | PASS | glibc, device-scene | maximumAbsoluteError=2.32e-06; host re-judged vs glibc PASS: practical=0 strict=867 max=2.32e-06 |
| triangle-m7 | replay-0 | 37.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| triangle-m7 | replay-1 | 46.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| triangle-m7 | replay-2 | 47.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.41e-07, strictImageFailures=12 |
| triangle-m7 | replay-3 | 46.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-06, strictImageFailures=426 |
| triangle-m7 | replay-4 | 46.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=7 |
| triangle-m7 | replay-5 | 66.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.32e-06, strictImageFailures=413 |
| emitter-m7 | emitter_sample | 20.0 | PASS | glibc, device-scene | maximumAbsoluteError=0.00513; host re-judged vs glibc PASS: practical=0 strict=235 max=0.00513 |
| regression-m7 | glass_render | 60.9 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| regression-m7 | lighting_mis | 52.2 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2682 max=8.34e-06 |
| regression-m7 | transparent_lighting | 79.7 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1679 max=7.75e-06 |
| lighting-m7 | lighting_mis | 52.1 | PASS | glibc, device-scene | maximumAbsoluteError=8.34e-06; host re-judged vs glibc PASS: practical=0 strict=2682 max=8.34e-06 |
| lighting-m7 | broad_lighting | 68.0 | PASS | glibc, device-scene | maximumAbsoluteError=5.34e-05; host re-judged vs glibc PASS: practical=0 strict=1732 max=5.34e-05 |
| lighting-m7 | lighting_mis-replay-0 | 39.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=3 |
| lighting-m7 | lighting_mis-replay-1 | 39.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=11 |
| lighting-m7 | broad_lighting-replay-0 | 39.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.79e-07, strictImageFailures=1 |
| lighting-m7 | broad_lighting-replay-1 | 39.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.07e-07, strictImageFailures=26 |
| transparent-m7 | transparent_shadow | 12.4 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-m7 | transparent_lighting | 78.9 | PASS | glibc, device-scene | maximumAbsoluteError=7.75e-06; host re-judged vs glibc PASS: practical=0 strict=1679 max=7.75e-06 |
| transparent-m7 | transparent_lighting-replay-0 | 43.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.49e-07, strictImageFailures=1 |
| transparent-m7 | transparent_lighting-replay-1 | 43.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=8 |
| transparent-m7 | transparent_lighting-replay-2 | 49.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.91e-06, strictImageFailures=89 |
| transparent-m7 | transparent_lighting-replay-3 | 49.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.01e-06, strictImageFailures=323 |
| transparent-m7 | transparent_lighting-replay-4 | 49.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-06, strictImageFailures=435 |
| transparent-m7 | transparent_lighting-replay-5 | 50.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.75e-07, strictImageFailures=4 |
| transparent-m7 | transparent_lighting-replay-6 | 48.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.56e-06, strictImageFailures=299 |
| transparent-m7 | transparent_lighting-replay-7 | 50.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.79e-06, strictImageFailures=520 |
| transparent-m7 | transparent-shadow-replay-direct | 5.8 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-m7 | transparent-shadow-replay-indexed | 5.8 | PASS | source capture CPU film | maximumAbsoluteImageError=7.45e-09, strictImageFailures=0 |
| terminator-m7 | shadow_terminator | 155.1 | PASS | glibc, device-scene | maximumAbsoluteError=1.53e-05; host re-judged vs glibc PASS: practical=0 strict=107 max=1.53e-05 |
| terminator-m7 | terminator-replay-0 | 32.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-1 | 40.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-2 | 41.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-3 | 40.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| terminator-m7 | terminator-replay-4 | 68.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=25 |
| terminator-m7 | terminator-replay-5 | 68.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=21 |
| terminator-m7 | terminator-replay-6 | 68.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=21 |
| terminator-m7 | terminator-replay-7 | 68.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=25 |
| terminator-m7 | terminator-replay-8 | 67.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=5 |
| terminator-m7 | terminator-replay-9 | 82.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.54e-07, strictImageFailures=10 |
| spec-refusal-m7 | glass_render-spec | 59.7 | PASS | glibc, device-scene | strictImageFailures=104, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=104 max=9.54e-07 |
| spec-refusal-m7 | glass_render-nospec-refused | 0.3 | PASS | expected refusal |  |
| spec-refusal-m7 | glass_render-superset-refused | 0.3 | PASS | expected refusal |  |
| environment-m7 | environment | 348.2 | PASS | glibc, device-scene | maximumAbsoluteError=2.86e-05; host re-judged vs glibc PASS: practical=0 strict=1343 max=2.86e-05 |
| environment-m7 | environment-replay-0 | 46.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.25e-06, strictImageFailures=122 |
| environment-m7 | environment-replay-1 | 71.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-06, strictImageFailures=135 |
| environment-m7 | environment-replay-2 | 62.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.96e-06, strictImageFailures=146 |
| environment-m7 | environment-replay-3 | 71.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=8.82e-06, strictImageFailures=123 |
| environment-m7 | environment-replay-4 | 71.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=216 |
| environment-m7 | environment-replay-5 | 62.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.86e-05, strictImageFailures=130 |
| environment-m7 | environment-replay-6 | 108.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=8.52e-06, strictImageFailures=65 |
| environment-m7 | environment-replay-7 | 110.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.77e-07, strictImageFailures=7 |
| environment-m7 | environment-replay-8 | 110.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.69e-05, strictImageFailures=163 |
| environment-m7 | environment-replay-9 | 110.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.66e-06, strictImageFailures=47 |
| environment-m7 | environment-replay-10 | 110.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.91e-06, strictImageFailures=136 |
| environment-m7 | environment-replay-11 | 110.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.43e-06, strictImageFailures=53 |
| hostcap-linux-triangle-run1 | tri0 | 39.9 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| hostcap-linux-triangle-run1 | tri1 | 49.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| hostcap-linux-triangle-run1 | tri2 | 49.7 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=12 |
| hostcap-linux-triangle-run1 | tri3 | 49.1 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-06, strictImageFailures=426 |
| hostcap-linux-triangle-run1 | tri4 | 49.4 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-07, strictImageFailures=7 |
| hostcap-linux-triangle-run1 | tri5 | 69.3 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-06, strictImageFailures=412 |
| hostcap-linux-terminator-run1 | term0 | 34.4 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator-run1 | term1 | 41.5 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator-run1 | term2 | 42.2 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator-run1 | term3 | 41.3 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator-run1 | term4 | 69.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=25 |
| hostcap-linux-terminator-run1 | term5 | 68.4 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=21 |
| hostcap-linux-terminator-run1 | term6 | 69.5 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=21 |
| hostcap-linux-terminator-run1 | term7 | 69.5 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=25 |
| hostcap-linux-terminator-run1 | term8 | 70.2 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=8 |
| hostcap-linux-terminator-run1 | term9 | 83.8 | PASS | source capture CPU film | maximumAbsoluteImageError=1.07e-06, strictImageFailures=12 |
| arm-m7 | microfacet_render | 60.3 | PASS | glibc, device-scene | strictImageFailures=119, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=119 max=1.91e-06 |
| motion-m7 | render-static | 45.1 | PASS | glibc, device-scene | strictImageFailures=400, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=400 max=7.63e-06 |
| motion-m7 | render-motion-0 | 49.3 | PASS | glibc, device-scene | strictImageFailures=407, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=407 max=2.53e-05 |
| motion-m7 | render-motion-1 | 48.9 | PASS | glibc, device-scene | strictImageFailures=361, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=361 max=1.14e-05 |
| motion-m7 | render-motion-2 | 49.7 | PASS | glibc, device-scene | strictImageFailures=442, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=442 max=1.67e-05 |
| motion-m7 | render-motion-3 | 49.9 | PASS | glibc, device-scene | strictImageFailures=657, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=657 max=3.36e-05 |
| motion-m7 | render-motion-4 | 49.1 | PASS | glibc, device-scene | strictImageFailures=443, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=443 max=4.48e-05 |
| motion-m7 | render-motion-5 | 49.4 | PASS | glibc, device-scene | strictImageFailures=382, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=382 max=1.57e-05 |
| motion-m7 | render-motion-6 | 48.3 | PASS | glibc, device-scene | strictImageFailures=592, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=592 max=4.58e-05 |
| motion-m7 | render-motion-7 | 48.4 | PASS | glibc, device-scene | strictImageFailures=444, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=444 max=4.48e-05 |
| volume-m7 | volume-state | 11.8 | PASS | device's own CPU / gate |  |
| volume-m7 | volume-absorption | 60.2 | PASS | glibc, device-scene | strictImageFailures=1133, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1133 max=1.53e-05 |
| volume-m7 | volume-isotropic | 76.5 | PASS | glibc, device-scene | strictImageFailures=1607, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1607 max=3.24e-05 |
| volume-m7 | volume-forward | 76.6 | PASS | glibc, device-scene | strictImageFailures=1665, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1665 max=4.67e-05 |
| volume-m7 | volume-backward | 75.8 | PASS | glibc, device-scene | strictImageFailures=1609, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1609 max=4.01e-05 |
| volume-m7 | boundary-state | 46.7 | PASS | device's own CPU / gate |  |
| volume-m7 | boundary-compaction-guard | 7.1 | PASS | device's own CPU / gate |  |
| volume-m7 | closed-single | 91.6 | PASS | glibc, device-scene | strictImageFailures=974, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=974 max=3.05e-05 |
| volume-m7 | closed-overlap | 148.6 | PASS | glibc, device-scene | strictImageFailures=1251, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=1251 max=4.58e-05 |
| volume-m7 | closed-inside | 144.5 | PASS | glibc, device-scene | strictImageFailures=2283, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=2283 max=2.29e-05 |
| volume-m7 | volume-replay-absorption | 27.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.53e-05, strictImageFailures=1133 |
| volume-m7 | volume-replay-isotropic | 27.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.24e-05, strictImageFailures=1607 |
| volume-m7 | volume-replay-forward | 27.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.67e-05, strictImageFailures=1665 |
| volume-m7 | volume-replay-backward | 27.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.01e-05, strictImageFailures=1609 |
| volume-m7 | closed-replay-single | 28.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.05e-05, strictImageFailures=974 |
| volume-m7 | closed-replay-overlap | 54.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.58e-05, strictImageFailures=1251 |
| volume-m7 | closed-replay-inside | 55.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.29e-05, strictImageFailures=2283 |
| hostcap-linux-triangle | tri0 | 40.4 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=9 |
| hostcap-linux-triangle | tri1 | 49.1 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| hostcap-linux-triangle | tri2 | 49.0 | PASS | source capture CPU film | maximumAbsoluteImageError=6.41e-07, strictImageFailures=12 |
| hostcap-linux-triangle | tri3 | 48.9 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-06, strictImageFailures=426 |
| hostcap-linux-triangle | tri4 | 49.5 | PASS | source capture CPU film | maximumAbsoluteImageError=2.38e-07, strictImageFailures=7 |
| hostcap-linux-triangle | tri5 | 68.9 | PASS | source capture CPU film | maximumAbsoluteImageError=2.32e-06, strictImageFailures=412 |
| hostcap-linux-terminator | term0 | 34.4 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term1 | 41.3 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term2 | 41.6 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term3 | 41.6 | PASS | source capture CPU film | maximumAbsoluteImageError=5.96e-08, strictImageFailures=0 |
| hostcap-linux-terminator | term4 | 68.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=25 |
| hostcap-linux-terminator | term5 | 68.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=21 |
| hostcap-linux-terminator | term6 | 68.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=21 |
| hostcap-linux-terminator | term7 | 69.1 | PASS | source capture CPU film | maximumAbsoluteImageError=1.53e-05, strictImageFailures=25 |
| hostcap-linux-terminator | term8 | 70.1 | PASS | source capture CPU film | maximumAbsoluteImageError=4.77e-07, strictImageFailures=8 |
| hostcap-linux-terminator | term9 | 84.9 | PASS | source capture CPU film | maximumAbsoluteImageError=1.07e-06, strictImageFailures=12 |

### s20

| Corpus | Job | Wall s | Result | Judged against | Numbers |
| --- | --- | --- | --- | --- | --- |
| materials-s20-principled_svm | principled_svm | 413.4 | PASS | glibc, device-scene | strictFloatFailures=557, maximumAbsoluteError=3.59e-06 |
| a650-refusals-m7 | nodes_world-refused | 0.5 | PASS | expected refusal |  |
| a650-refusals-m7 | nodes_svm-refused | 0.4 | PASS | expected refusal |  |
| a650-refusals-m7 | nodes_render-refused | 0.5 | PASS | expected refusal |  |
| triangle-s20-triangle_sample | triangle_sample | 82.6 | PASS | glibc, device-scene | maximumAbsoluteError=0.000488; host re-judged vs glibc PASS: practical=0 strict=865 max=0.000488 |
| regression-s20-glass_render | glass_render | 87.6 | PASS | glibc, device-scene | strictImageFailures=64, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=64 max=1.91e-06 |
| regression-s20-lighting_mis | lighting_mis | 66.9 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1057 max=6.08e-06 |
| triangle-s20-surface | surface | 46.5 | PASS | device's own CPU / gate | strictFloatFailures=1244, maximumAbsoluteError=3.81e-06 |
| transparent-s20-transparent_lighting | transparent_lighting | 203.8 | PASS | glibc, device-scene | maximumAbsoluteError=6.02e-06; host re-judged vs glibc PASS: practical=0 strict=683 max=6.02e-06 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-0 | 52.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-1 | 54.0 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-2 | 64.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=3.81e-06, strictImageFailures=72 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-3 | 64.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=1.55e-06, strictImageFailures=133 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-4 | 65.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=167 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-5 | 65.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=7.9e-07, strictImageFailures=3 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-6 | 64.2 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=103 |
| transparent-s20-transparent_lighting | transparent_lighting-replay-7 | 64.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=205 |
| transparent-s20-transparent_shadow | transparent_shadow | 16.2 | PASS | device's own CPU / gate | maximumAbsoluteError=1.19e-07 |
| transparent-s20-transparent_shadow | transparent-shadow-replay-direct | 11.2 | PASS | source capture CPU film | maximumAbsoluteImageError=1.19e-07, strictImageFailures=0 |
| transparent-s20-transparent_shadow | transparent-shadow-replay-indexed | 11.0 | PASS | source capture CPU film | maximumAbsoluteImageError=0, strictImageFailures=0 |
| materials-s20-principled_render | principled_render | 212.2 | PASS | glibc, device-scene | strictImageFailures=64, strictStageFailures=0; host re-judged vs glibc PASS: practical=0 strict=64 max=6.44e-06 |
| materials-s20-principled_render | principled-replay | 173.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.44e-06, strictImageFailures=64 |
| pixel-motion-s20-object-moving-images | object-moving-images | 149.7 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1181 max=6.08e-06 |
| pixel-motion-s20-object-moving-images | object-replay-motion-0 | 128.4 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.62e-06, strictImageFailures=7 |
| pixel-motion-s20-object-moving-images | object-replay-motion-1 | 133.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=5.36e-07, strictImageFailures=10 |
| pixel-motion-s20-object-moving-images | object-replay-motion-2 | 182.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.4e-06, strictImageFailures=291 |
| pixel-motion-s20-object-moving-images | object-replay-motion-3 | 189.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-s20-object-moving-images | object-replay-motion-4 | 190.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=272 |
| pixel-motion-s20-object-moving-images | object-replay-motion-5 | 189.6 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.29e-06, strictImageFailures=45 |
| pixel-motion-s20-object-moving-images | object-replay-motion-6 | 191.1 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=329 |
| pixel-motion-s20-object-moving-images | object-replay-motion-7 | 191.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=227 |
| pixel-motion-s20-deforming-moving-images | deforming-moving-images | 234.5 | PASS | glibc, device-scene | maximumAbsoluteError=6.08e-06; host re-judged vs glibc PASS: practical=0 strict=1151 max=6.08e-06 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-0 | 85.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.38e-07, strictImageFailures=2 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-1 | 131.3 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=4.17e-07, strictImageFailures=7 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-2 | 143.5 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.4e-06, strictImageFailures=304 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-3 | 188.9 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=9.31e-09, strictImageFailures=0 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-4 | 188.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=280 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-5 | 188.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=2.98e-06, strictImageFailures=34 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-6 | 187.7 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.08e-06, strictImageFailures=300 |
| pixel-motion-s20-deforming-moving-images | deforming-replay-motion-7 | 186.8 | PASS | glibc device-scene (replay) | maximumAbsoluteImageError=6.02e-06, strictImageFailures=224 |
| materials-s20-principled_bounce | principled_bounce | 337.3 | PASS | device's own CPU / gate | strictFloatFailures=2796, maximumAbsoluteError=0.125 |

## Surface entries under the Intel compile envelope (lane m2-shrink, 2026-10-24)

The textured-Principled bench made the largest consumer-scene entries of the tree: the HDRI bench's `prepare_surface`
measured 61,690 Intel backend instructions on Mesa 25.0.7 (above the 60,000 envelope every gate entry keeps, below the
65,535 limit of "Linux reference fixes on the updated Mesa"), and the size guard warned on `prepare_surface`,
`shade_light_nee`, `shade_background`, `shader_eval_background` and `shade_shadow_transparent`.

**Where the size was.** Not in the entry partition. The contraction-proof binary32 add `rounded_add`
(`wavefront_float.wgsl`, an integer implementation, about 1.3 KB of WGSL and about 56 Intel instructions per inlined
copy) was inlined 355 times into the HDRI bench's `prepare_surface`: 34% of its inlined WGSL. The copies came from the
helpers that call it two to six times per call (`rounded_add3` 96, `rounded_dot` 82, `surface_transform_row` 54,
`surface_decode_normal` 30, the trigonometry helpers 36). In the background module 475 KB of `shade_background`'s
1.57 MB were the closure setup behind `if(SVM_SURFACE)`, which is false there: the driver removes it, the size guard's
text measure did not.

**Change** (no entry, dispatch, record, region, feature bit, tier or threshold changes):
- Single-body loops. `rounded_add3`, `rounded_dot`, `rounded_normalize`, `rounded_sqrt` (root steps),
  `rounded_divide` (quotient steps), `surface_transform_row`, `surface_decode_normal` and `trig_two_sum` run their
  sums (steps, quotients) through one inlined body in a loop, the pattern of "Adreno 650 lane 2". The bound is
  `rounded_steps(n)` = `n|p(31u)`: push word 31, which no dispatch sets (0), so the compiler cannot unroll the loop;
  iterations past n write nothing. Operands are selected by `select` chains or branches, never by dynamic vector
  indexing. Each sum keeps its operands, operation and order, so every result is bit-identical. Push word 31 is
  reserved for this bound.
- `trig_coeff` keeps its three inlined sums. With its loop form as well, the Adreno 650 (S20) compiler dropped an SVM
  store in the Principled SVM probe (error 8 at path 119, shader 12, the "Adreno 650 lane 2" signature). Arena
  replays of the failing capture on the S20 with single helpers reverted: reverting `trig_coeff`, `rounded_add3` or
  `rounded_dot` each removes the error; reverting `surface_transform_row` or `surface_decode_normal` does not. The
  shipped form passes the replay (0 practical, the same 114 strict as the unchanged module) and the full gate.
- The `NODE_CLOSURE_BSDF` case calls `svm_closure_bsdf`, emitted by `svm_layout()` next to `SVM_SURFACE`: `svm_bsdf`
  in surface modules, error 2 elsewhere (what `if(SVM_SURFACE)` compiled to). The background module's text no longer
  carries the dead closure setup.

Intel backend instructions / inlined WGSL bytes (`intel_compile_envelope.py`, Mesa 25.0.7, before -> after):

| Entry (HDRI bench) | Instructions | Inlined bytes |
| --- | --- | --- |
| `prepare_surface` | 61,690 -> 51,287 | 1,362,166 -> 1,101,295 |
| `shade_light_nee` | 53,393 -> 44,135 | 1,339,634 -> 1,078,251 |
| `shader_eval_background` | 44,352 -> 40,944 | 1,612,338 -> 935,537 |
| `shade_surface` | 43,969 -> 36,166 | 838,577 -> 644,252 |
| `shade_surface_light` | 43,030 -> 40,293 | 858,649 -> 708,769 |
| `shade_shadow_transparent` | 38,964 -> 31,689 | 1,357,328 -> 1,096,457 |
| `shade_background` | 33,086 -> 30,682 | 1,572,290 -> 876,791 |
| `prepare_shader_data` | 12,894 -> 8,396 | 393,257 -> 240,132 |

Every entry of the three bench modules is below 60,000 (worst: bench and HDRI bench 51,287, textured-plane control
52,126; the check exits 0). The size guard no longer warns on the bench or the HDRI bench; it still warns on the
textured-plane control's `prepare_surface` (1,114,357 bytes, 4,357 over; 52,126 instructions). No scene-specialized
gate entry grew: worst gate entries ABI set 44,585 -> 40,441, environment set 43,078 -> 40,341, materials set 52,341 ->
46,850. Smaller entries change the superset grouping (`specialization_check.py`; superset-group entries can be larger
because each group now holds more regions), all groups bit-identical: glass 5 -> 3 groups, Principled 11 -> 6
(unfit regions 9 -> 1), node render 1 -> 4 (unfit 21 -> 9; the scene's own entries are now below the
1,000,000-byte group budget), node world 7 -> 6.

**Bit identity (Linux x64, Intel UHD ADL-S GT1, Mesa 25.0.7).** A GPU identity probe compares every rewritten helper
with its previous definition on 4 x 1,000,000 records of random and special binary32 inputs: 0 differing results
(a deliberately reordered `rounded_dot` is detected). Films, against the unchanged tree's runs: bench, HDRI bench and
textured-plane control at 1 spp (WGSL and CPU, three repeats each) bit-identical; at 16 spp the WGSL films vary run to
run on the unchanged tree too (repeat 0 against 1: 1,178 / 2,689 / 2,361 differing components) and after against
before is within that (0 practical, at most 11 strict); CPU films `f4b08b95` / `fc72f30a` / `a1d20c08` unchanged.
Principled gate (GPU `2e11701d`, CPU `cc861de5`), node gate and node render (`39fa5c6b` / `58156b43`), glass
(`f7c827af` / `da52a2d8`), environment gate (36 films), triangle images (18), the SVM probes and the state gates
(svm_surface, svm_world, nodes_svm, principled_svm, surface, shader_data, closest, camera, device, attribute, image,
normal, image files, render with attributes, images and normals, nodes_world): identical films and reports.
Differences only in the known run-to-run atomic family (transparent-shadow suite: broad_lighting 3/7/8/9,
feature_combination 5, lighting_mis 5, transparent_lighting 2/4/5), the capture-date headers of the gates' input EXRs
(host gate 12, image-file gate 8), and the background gate's film statistics, which vary run to run (strict 196 and
210 in two runs of the same binary; 206, 212 and 180 at merge waves 5, 6 and 7).

**Devices** (judged as the gates define: the glibc reference of the device's own scene, or the Linux CPU film for
the managed-app bundles; practical / strict / maximum):

| Device | Job | Result | Numbers (practical / strict / max) | Merge wave 7 strict |
| --- | --- | --- | --- | --- |
| Samsung SM-X700 | principled_svm | PASS | strict 634, max 3.61e-06 | 634 |
| Samsung SM-X700 | principled_render | PASS | 0 / 67 / 6.44e-06, stage strict 0 | 67 |
| Samsung SM-X700 | nodes_render | PASS | 0 / 1,689 / 3.72e-05 | 1,689 |
| Samsung SM-X700 | environment | PASS | per case as merge wave 7 | as merge wave 7 |
| Samsung SM-X700 | bench bundle (managed app) | PASS | 0 / 113 / 4.29e-05 | 113 |
| Samsung SM-X700 | HDRI bench bundle (managed app) | PASS | 0 / 329 / 7.71e-05 | 329 |
| HP (Android x64) | principled_svm | PASS | strict 946, max 9.54e-07 | 946 |
| HP | principled_render | PASS | 0 / 91 / 6.44e-06 | 91 |
| HP | nodes_render | PASS | 0 / 1,794 / 1.03e-04 | 1,794 |
| HP | environment | PASS | per case as merge wave 7 | as merge wave 7 |
| HP | bench bundle (managed app) | PASS | 0 / 209 / 3.78e-05 | 213 |
| HP | HDRI bench bundle (managed app) | PASS | 0 / 446 / 9.16e-05 | 430 |
| S20 (Adreno 650) | principled_svm | PASS | strict 557, max 3.59e-06 | 557 |
| S20 | transparent_shadow | PASS | strict 0, max 1.19e-07 | 0 |

The lockstep and SVM gates give merge wave 7's strict counts exactly; the managed-app bundle films move a little run
to run (atomic accumulation).

First-render compile times (seconds, cold, merge wave 7 -> this change):

| Entry | Samsung bench | Samsung HDRI | HP bench | HP HDRI |
| --- | --- | --- | --- | --- |
| `prepare_surface` | 115.5 -> 70.9 | 116.5 -> 73.5 | 146.5 -> 122.8 | 146.5 -> 123.6 |
| `shade_light_nee` | | 117.7 -> 72.9 | | 54.6 -> 44.5 |
| `shade_background` | 35.6 -> 5.4 | 39.9 -> 23.6 | 11.7 -> 10.5 | 17.2 -> 15.5 |
| `shader_eval_background` | | 36.9 -> 9.4 | | 22.6 -> 21.3 |
| first GPU render | 177.2 -> 97.2 | 343.8 -> 206.1 | 232.8 -> 189.3 | 326.9 -> 270.1 |

The S20 `principled_svm` gate took 365 s (413 s at merge wave 7).

## Light trees and light linking (lane m2-lighttree, 2026-10-24)

The integrator's light tree (`use_light_tree`, `KERNEL_FEATURE_LIGHT_TREE`) and light linking
(`KERNEL_FEATURE_LIGHT_LINKING`: receiver light sets and emitter set membership) render on the WebGPU device, as the
pinned kernel does them, on the flat light distribution and on the tree. The unmodified `LightManager` builds the tree
(`device_update_tree`, including the per-receiver specialized trees of light linking); the device uploads its arrays
unchanged and validates them before any dispatch (`validate_light_tree`, `native/cycles/lighting.cpp`: node kinds,
child, instance, emitter and lamp indices, leaf sizes, per-object lookups and every light-set root).

Data and stages
- Scene tables: `light_tree_nodes`, `light_tree_emitters`, `light_to_tree`, `object_lookup_offset`,
  `triangle_to_tree` and `object_prim_offset` (`CBC_LIGHT_TREE_TABLES`, `native/cycles/wavefront.h`). Their GPU
  descriptors follow the device tables, so no earlier table index moved; the CPU references and the Android scene
  exchange carry them.
- `wavefront_light_tree.wgsl`: `light_tree_sample<false>` and `light_tree_pdf<false>` (node and emitter importance
  for point, spot, area, sun, background and triangle emitters, the distant node, the leaf reservoirs with their
  uniform fallback, inner-node splitting, mesh subtrees in object space, instance nodes, bit trails). Cycles'
  `fast_sincosf`, `fast_cosf` and `fast_acosf` are reproduced operation by operation and glibc `cosf` through the
  existing glibc-exact disk sine/cosine; every sum of products goes through `rounded_add` (contraction- and
  reassociation-proof), and divisions and square roots are correctly rounded. Selections are integer decisions on
  these values: the selected emitter equals the CPU's for every probe state on Linux, the Samsung and the HP.
- `wavefront_light_link.wgsl`: `light_link_object_match`, the NEE receiver and the forward receiver
  (`path.mis_ray_object`, written at path start and at every non-transparent surface bounce).
- New surface entry `shade_surface_light_select` (feature bit 96 `LIGHT_SELECT`): selects the emitter for each surface
  path (tree or flat distribution, then the link match) and computes the tree's pdf of an emissive-triangle hit for
  its forward MIS weight; the lamp and triangle samplers read the 8-word record (stage record byte 360). New
  background entry `light_tree_forward` (bit 94 `LIGHT_TREE`): per-path tree pdfs of the background light, of every
  MIS sun and of a lamp hit, before `shade_background` and `shade_light_forward` read them. Both are their own
  entries because the traversal alone inlines to about 0.55 MB of WGSL (0.70 MB with object motion); Linux Intel
  measures `shade_surface_light_select` at 40,987 backend instructions and `light_tree_forward` at 23,149. The
  worst gate entry of the light-tree gate is the existing `shade_surface_light` (lamp sampling with the
  background light and an area light) in case 2 (lamps and emissive triangles under an HDRI world): 58,001
  backend instructions, 1,999 below the 60,000 envelope (1,101,856 bytes inlined, under the 1,110,000-byte size
  guard); the next additions to that entry need a split. A probe
  entry that inlined the traversal and both emitter samplers (1.5 MB) was miscompiled by Intel Mesa 25.0.7 (wrong
  selections that moved with unrelated code edits): the gate's probes keep the device's split.
- Bit 95 `LIGHT_LINKING`: link checks on lamp intersection (closest), emissive surfaces (connect), sun lights
  (background) and NEE. With the tree, the threshold roulette and `shadow_path.bsdf_eval_average` are skipped as in
  the pinned kernel. Capture entries 21 and 22 (recorder format v10).
- Refused (unchanged): shadow linking (blocker sets and the dedicated-light kernels), volumes with direct light (so
  the tree's volume-segment variant is not used).

Gate `codebrix_cycles_light_tree_gate` (`native/cycles/light_tree_gate.cpp`, `native/tools/light_tree_gate.py`), 7 cases:
0 lamps (point, spot, area, sun), 1 a 12-triangle mesh light with a second mesh light and a point, 2 lamps with
emissive triangles under an HDRI world with its background light, 3 one emitter mesh instanced three times (instance
nodes), 4 a moving and deforming emitter, 5 light linking on the flat CDF (tree off), 6 light linking on the tree
(per-receiver trees). Per case: a 4,096-state probe of the selection and the light sample (selection validity,
emitter, sample validity, prim and object exact; selection pdf, point, direction, distance, pdf and eval factor
practical), the tree's forward pdf of a target emitter (`light_tree_pdf<false>`), the GPU image against the
original CPU render (twice, the second captured), and host guards (corrupted tree tables refused).

| Device | Probe valid / forward pdfs / link rejections | Probe numeric strict / max | Films strict / max | Wall |
| --- | --- | --- | --- | --- |
| Linux | 24,047 / 12,820 / 2,379 | 26,771 / 0.0227 | 4,874 / 2.85e-5 | 110 s |
| Samsung SM-X700 | 24,048 / 12,820 / 2,378 | 13,498 / 0.0145 (host re-judge per key 663-3,992) | 3,012 / 1.53e-5 | 732 s |
| HP | 24,047 / 12,820 / 2,379 | 26,731 / 0.0227 (host re-judge per key 1,048-8,165) | 4,932 / 2.85e-5 | 485 s |
All practical PASS; Android judged against the glibc references computed from the device's own scene, host
re-judged (14 references per device, 0 practical failures). The slowest pipeline compile is 17.8 s on the Samsung and 26.2 s on the HP. Replays on Linux,
the Samsung and the HP: all seven captures replay exactly (case 2 since the stable shadow-slot reservation for every
scene with direct light; "Open device items", item 17, resolved). Regression on the devices (Samsung and HP): lighting_mis,
triangle_sample, emitter_sample, triangle_lighting, transparent_lighting, light_state, broad_lighting, glass_render and
principled_render pass; the Samsung environment gate passes with replays of cases 2, 8 and 11. The S20 pair
(principled_svm, transparent_shadow) passes.
Regression (Linux): every gate of the lighting, emitter, environment and materials set passes; films bit-identical to
the wave-7 references except GPU films whose own repeat differs (atomic film accumulation: lighting_mis 5,
transparent_lighting 3, 4, 5). Superset on/off identity (glass_render, 5 groups including the new regions) holds.
Oracle scenes `light-tree/lamps-and-mesh-lights` and `light-tree/light-linking` (Session through the public host
API): CPU vs WGSL on Linux "perfect" (0 practical, 825 and 928 strict).

## Shadow catcher and its film passes (lane m2-shadowcatcher, 2026-10-23/24)

The registered device renders the shadow catcher as the pinned Cycles does and writes the first film passes beyond
Combined: `PASS_SHADOW_CATCHER`, `PASS_SHADOW_CATCHER_SAMPLE_COUNT` and `PASS_SHADOW_CATCHER_MATTE` (and the background
pass of the approximate shadow catcher). The host reads them with the pinned pass accessor (`HostOutput`,
`cbc_host_pass_floats`, managed `ReadPass`); nothing on the device converts passes.

- **Passes.** The surface stage's data-pass refusal now masks only the three shadow catcher bits; every other data
  pass keeps "WebGPU surface data passes not implemented". `validate_shadow_catcher()` (`shadow_catcher.cpp`, called by
  the closest, surface, background and shadow stages) checks the three offsets (3, 1 and 4 floats) and requires them
  with `integrator.has_shadow_catcher`. Writes follow `film/light_passes.h` exactly (`wavefront_shadow_catcher.wgsl`):
  matte paths (no `PATH_RAY_SHADOW_CATCHER_HIT`) add to the matte, shadow catcher paths
  (`PATH_RAY_SHADOW_CATCHER_PASS`) add to the catcher pass instead of Combined, `PATH_RAY_SHADOW_CATCHER_BACKGROUND`
  paths write only the background pass, transparency goes to the matte alpha, light passes and light groups are skipped
  for catcher paths, and `film_write_data_passes` keeps its `PATH_RAY_SINGLE_PASS_DONE` bookkeeping.
- **Path split.** `intersect_closest` makes the split decision (`kernel_shadow_catcher_is_path_split_bounce`), writes
  the sample count and matte alpha, sets `PATH_RAY_SHADOW_CATCHER_HIT` and records the new path's kernel and shader in
  a per-path mark. Two dispatches of the `shadow_catcher` module then take new path indices from
  `next_main_path_index` in ascending path order (one of the orders the upstream atomic allows; renders and the
  integer state stay reproducible), copy every main-path field with the compaction descriptors
  (`integrator_state_copy_only`) and initialize the copy: `PATH_RAY_SHADOW_CATCHER_PASS`, and the sorted
  `SHADE_SURFACE` (key from the new index) or `SHADE_BACKGROUND` with `PATH_RAY_SHADOW_CATCHER_BACKGROUND` when the
  background pass exists and the film is opaque. The background stage continues such paths to the sorted surface
  kernel. `DEVICE_KERNEL_INTEGRATOR_SHADOW_CATCHER_COUNT_POSSIBLE_SPLITS` is implemented, so the pinned
  `PathTraceWorkGPU` halves new paths and predicts splits as upstream.
- **Visibility and lights.** Shadow catcher paths and their shadow rays trace with the visibility shifted by 16 bits
  (opaque mask likewise); lamps with `SHADER_EXCLUDE_SHADOW_CATCHER` and mesh lights of non-catcher objects are not
  sampled by them.
- **Refused.** A shadow catcher with a volume ("WebGPU shadow catcher with volumes not implemented") or with caustics
  (MNEE). On the Adreno 650 (S20, not a reference device) a shadow catcher with transparent shadows and direct light is
  refused before any pipeline compiles: the transparent shadow stage of such a scene stopped with SVM error 8
  ("invalid/uninitialized SVM stack"); every other case, a transparent pane without direct light included, passes
  there. The cause on that compiler is not determined.
- **Feature bit** 102 `SHADOW_CATCHER` (scene-only in superset mode). Every edit is a specialization region whose
  fallback is the previous line verbatim, so modules of scenes without a shadow catcher are byte-identical to merge
  wave 7 after specialization. Capture entries 21-23 (the `shadow_catcher` module), recorder v10.

Gate: `codebrix_cycles_shadow_catcher_gate OUT` (`native/tools/shadow_catcher_gate.py`). Cases 0-3 (no direct light:
transparent film; the approximate catcher with an opaque film; a transparent pane over the catcher; a holdout object)
judge every GPU stage against the original CPU kernels on the same input states: all path fields (flags, visibility,
sort keys), every film float, the queue counters, `next_main_path_index` and the split-count kernel. Cases 4-9 (point
light; a light marked as catcher plus a sun; HDRI world with background-light MIS and an area light; a
transparent-shadow caster; the approximate catcher with a point light; a mesh light over a glossy catcher) compare the
raw film (Combined and the three passes) with an independent CPU render (two CPU states per pixel, as the CPU
megakernel). Case 10 checks the volume refusal. Device-scene flow: images through the scene exchange; the state stages
are recorded per case (`run/case-N/run.stages.bin`) and judged on Linux (`CODEBRIX_CYCLES_STAGE_JUDGE` = the device's
run directory).

| Device | Shadow catcher gate (11 cases) | Wall | Regression |
| --- | --- | --- | --- |
| Linux x64 (Intel, Mesa 25.0.7) | PASS: 447 state stages (13,046,589 integer and 20,957,184 float checks, 5,824 strict, max 1.38e-4), images max 2.69e-5, repeat bitwise, volume refused, 6 replays PASS | 28 s | PASS: device reference export films bit-identical to merge wave 7 except the atomic-order cases lighting_mis 5 and transparent_lighting 4/5; render static and camera motion, volume renders, transparent shadow, microfacet render, host gate, background and closest queue gates, glass superset identity, `dotnet test` Release and Debug 183/183, compile envelope (gate entries worst 58,250), size guard |
| Samsung SM-X700 (Adreno 730) | PASS against the glibc device-scene references (films max 1.74e-5); the 447 state stages judged on Linux (148 strict, max 9.5e-7); 6 replays PASS | 210 s (slowest pipeline 16.0 s) | not rerun: no module of a scene without a shadow catcher changes |
| HP (Mesa 24.0.8, Bliss) | PASS against the glibc device-scene references (films max 2.67e-5); state stages judged on Linux (1,347 strict, max 1.38e-4); 6 replays PASS | 219 s (slowest pipeline 25.8 s) | not rerun (as above) |
| Samsung Galaxy S20 FE (Adreno 650) | PASS with case 7 refused as above (films max 1.74e-5); state stages judged on Linux; 5 replays PASS | 502 s (slowest pipeline 43.8 s) | principled_svm and transparent_shadow PASS |

Oracle scenes `native/oracle/scenes/shadow-catcher/catcher-point-light.json` and `catcher-approximate.json`
(`author_shadow_catcher.py`; the passes are named pass nodes, the output contract records Combined): WGSL against the CPU
oracle on Linux "perfect" (practical 0; 334 and 23 strict). Managed test `gpu_shadow_catcher_passes_match_the_cpu_reference`
reads the three passes through `ReadPass` on both backends (transparent and approximate film).

## Ambient occlusion (lane m2-ao, 2026-10-23)

The registered device implements Cycles' ambient occlusion in its three forms, as the pinned kernels do.

- **Fast GI, replace mode** (`ao_bounces` with a non-zero `ao_factor`, `ao_distance`; per-object `ao_distance`).
  Already present in the closest stage (AO bounce termination, the shortened ray `tmax`, the object's AO distance)
  and the background stage (`ao_bounces_factor`); this section adds its image gates.
- **AO shadow rays** (`integrate_surface_ao`, for fast GI's additive mode, `KERNEL_FEATURE_AO_ADDITIVE` with
  `ao_additive_factor`, and for the AO pass, `KERNEL_FEATURE_AO_PASS`). `connect_surface_light` calls
  `surface_ao` (`wavefront_ao.wgsl`) after the direct light, before the BSDF bounce: `surface_shader_ao` (diffuse
  closure weights times the additive factor, their weighted normal), the cosine-weighted direction from
  `PRNG_SURFACE_BSDF`, the shadow-terminator origin and self-intersection offset, `tmax = ao_distance`, throughput
  times `surface_shader_alpha`, flag `PATH_RAY_SHADOW_FOR_AO`, `unshadowed_throughput`. The shadow stage traces it
  like every shadow path (opaque or transparent); `shadow_write_film` has the pinned AO branch of
  `film_write_direct_light` (the AO pass for camera paths, the additive combined term). With AO on, every surface
  input gets two fixed shadow slots (control op 5, as every scene with direct light gets one) and the path arrays use
  the stable serial scan: with atomic slots and atomic path-array indices the shadow compaction moved paths in a
  run-dependent order, so the scheduler's counts, and the command replays, differed between runs.
- **The AO pass.** Written by those rays, read back unchanged by the host (`cbc_host_pass_floats`) and the managed
  `CyclesScene.ReadPass`; the capability report lists `ao` under `backendScope.webgpu.passes`.
- **The Ambient Occlusion node** (`NODE_AMBIENT_OCCLUSION`). Shaders with the node are flagged `SD_HAS_RAYTRACE`
  and the closest stage queues them to `DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE`, which now runs the surface
  stage with one different SVM entry: `prepare_surface_raytrace`, the surface module specialized with
  `SVM_NODE_RAYTRACE` (`svm_ao_occlusion`: `num_samples` hemisphere rays from `PRNG_SURFACE_AO` through
  `path_branched_rng_2D`, `inside`, the global radius when the distance is 0 and unlinked, an opaque any-hit
  traversal, or with `only_local` the object's own BVH from `object_node` with the instance transform pushed, as
  `bvh/local.h`). Every other SVM evaluation runs the node as Cycles' non-raytrace node masks do (`ao = 1`). The
  other surface entries are shared; push word 29 names the queue. Capture entry 21, recorder v10.
- **Specialization.** Bits 106 `SVM_NODE_AO` (scene feature), 107 `SVM_NODE_RAYTRACE` (a module variant used only
  for `prepare_surface_raytrace`, not a scene feature and not in the off mode) and 108 `AO_SHADOW`. Modules of
  scenes without AO are unchanged; on Intel Mesa 25.0.7 `prepare_surface_raytrace` measures 42,215 backend
  instructions (envelope run over the AO gate modules, worst entry 43,078).
- **Refused, never approximated.** `only_local` from an object without its own BVH in a scene with instanced objects
  (a local traversal that reaches an instance leaf reports error 16); bevel and raycast nodes stay unported (error
  2). Ambient occlusion with object or deformation motion has no gate yet. On the Adreno 650 (S20) the AO node and
  the AO shadow rays are refused before any pipeline compiles (below).

Gates (Linux, Samsung SM-X700 and HP; images judged against the glibc reference of each device's own scene):

| Gate | Linux | Samsung SM-X700 | HP |
| --- | --- | --- | --- |
| `ao_state` (9 cases: AO ray setup and shadow-stage film against the CPU; AO node on the raytrace queue and under the non-raytrace mask) | PASS, 6,324 AO rays (311 occluded), 288 node paths, max 7.63e-6, strict 1,055 | PASS (device CPU), max 7.63e-6, strict 602, 177 s | PASS (device CPU), max 7.63e-6, strict 908, 330 s |
| `ao_render` (8 images: replace, per-object distance, additive, additive with transparent shadows, additive with an HDRI world, AO pass, AO node, local AO on instances) | PASS, max 1.57e-5, strict 8,730 | PASS, max 1.38e-5, strict 1,739 (glibc), 164 s | PASS, max 1.57e-5, strict 4,361 (glibc), 270 s |
| `ao_render` on-device replays (8) | n/a | 8/8 PASS | 8/8 PASS |
| AO oracle scenes (`ao/fast-gi-additive`, `ao/ao-node`, `compare.py`) | perfect (max 1.91e-6) | | |
| Managed AO pass read-back (`WebGpuAmbientOcclusionTests`) | PASS | | |
| AO corpus rerun on the final binaries (state + image) | | PASS, 378 s | PASS, 662 s |

The fast GI termination and per-object AO distance state checks remain the closest gate's cases. The regression set
on the same binaries passes on Linux (surface, closest, shadow, transparent shadow and lighting, lighting MIS, broad
lighting, feature combination, glass, Principled and node gates with their superset identity, host gate; untouched
films bit-identical apart from the known run-to-run atomic family; bench, HDRI bench and control bundles: CPU films
identical, WGSL films practical 0), on the Samsung SM-X700 and on the HP (surface, closest, shadow, transparent
shadow and lighting, lighting MIS, glass render, Principled SVM and render, node SVM and render).

**Adreno 650 (S20, not a reference device).** Fast GI images 0, 1 (replace) and 2-5 (AO shadow rays, AO pass) passed,
but the AO node image and the AO state gate failed with SVM error 8 (invalid stack). The known-adapter table now
refuses scenes with the AO node or AO shadow rays on this GPU class before any pipeline compiles ("ambient occlusion
shader nodes and ambient occlusion shadow rays (fast GI additive mode, AO pass) are not supported on Adreno (TM) 650
GPUs"; `KnownAdapter::also_refused_region` = `AO_ANY`); the capability scope lists it under `renderTimeRejections`.
On the S20 the three refusals and the replace-mode images pass, as do principled_svm, transparent_shadow, surface,
closest, shadow, lighting_mis and glass_render.

## Manifold next-event estimation (caustics)

`DEVICE_KERNEL_INTEGRATOR_INTERSECT_MNEE` runs on the WebGPU device (`cycles/mnee.cpp`,
`shaders/wavefront_mnee.wgsl`). The device reports `has_mnee`, so a scene with a caustic caster,
a caustic receiver and a lamp with shadow caustics renders with `integrator.use_caustics` and
`KERNEL_FEATURE_MNEE` exactly as on the CPU device. Before this, the device reported no MNEE
support and such a scene rendered without the manifold walk on WebGPU while the CPU rendered
with it.

What runs, in the pinned order (`kernel/integrator/intersect_mnee.h`, `mnee.h`):

- **Closest stage.** The `PATH_MNEE_RECEIVER_ANCESTOR` / `PATH_MNEE_CULL_LIGHT_CONNECTION`
  update after the intersection, the caustic-lamp culling of the lamp-intersection test, and
  the routing of a caustic receiver to `INTERSECT_MNEE` instead of the surface queue.
- **`INTERSECT_MNEE`**, seven dispatches over one GPU-private record per path, so every entry
  stays inside the pipeline size guard: `mnee_receiver` (ShaderData), `mnee_seed` (the light
  sample and the probe ray: caster hits, triangle and smooth-normal checks), `mnee_eval` (the
  caster shaders with the caustics closure limit, the first refractive or glass closure, the
  index of refraction and the sampled half-vector offset), `mnee_setup` (the manifold
  vertices), `mnee_solve` (the Newton walk: constraint derivatives, the block-tridiagonal
  solve, projection to the surfaces, step control; the pinned constants and limits),
  `mnee_transfer` (the light-sample update, the transfer matrix, the clamped geometry term, the
  visibility of every vertex) and `mnee_contribute` (the shaders at the solution vertices, the
  refractive BSDF terms, the shadow ray from the last vertex, the MNEE shadow slot and the move
  to the sorted surface queue). The bounce counters the pinned kernel leaves modified when the
  transfer matrix or a visibility check fails are reproduced.
- **Surface stage.** A path with `PATH_MNEE_SAMPLED` takes its light sample, throughput and
  ray from the MNEE slot (no MIS weight, no light-threshold roulette); on a caustic caster a
  caustic lamp's transmitted light is skipped; `mnee_cleanup` (after `shade_surface`) clears
  the flag and terminates a slot the surface did not use.
- **Background stage.** Caustic background and sun lamps are culled for paths with
  `PATH_MNEE_CULL_LIGHT_CONNECTION`.
- **Queue.** Main and shadow compaction rewrite the MNEE ownership words as the pinned state
  moves do (`controls.wgsl` op 4). In caustic scenes the MNEE and surface shadow slots are
  reserved one per dispatched path and the queue selection is serial, as with transparent
  shadows, so slot numbers and compaction decisions do not depend on atomic order and command
  replays reproduce every scheduler readback.

Every MNEE region is the specialization feature `MNEE` (bit 110, superset scene-only): a scene
without caustics compiles byte-identical WGSL. Command captures with MNEE dispatches are
recorder v10 (entries 21-27 the INTERSECT_MNEE entries, 28 `mnee_cleanup`, all in
`surface.wgsl`); captures without them keep the v9 layout.

Arithmetic. The walk is chaotic: one unit in the last place in a Jacobian changes the
iteration count. Every sum of products, division and square root goes through the correctly
rounded adapters in the pinned operation order, including the float4 rule of the oracle
(`float4 / float` multiplies by the reciprocal on every host, `float3 / float` divides each
lane on SSE hosts), Cycles' `fast_sincosf` with unfused steps, and MNEE's own point and sun
light sampling (the shared lamp sampler divides where the oracle multiplies by `1/d`; changing
it would change untouched films). With these, the solver's outcome and iteration count equal
the pinned walk path by path on Linux.

Refused before any dispatch (explicit errors, never an image without caustics): spot, area and
background lamps with shadow caustics, MNEE with the light tree, light or shadow linking, AO,
volumes or motion blur. An anisotropic refractive caster reports the unsupported-handler error
(error 2).

On the Adreno 650 (Samsung Galaxy S20, not a reference device) every scene with
`integrator.use_caustics` is refused before any pipeline compiles: "WebGPU: caustics (manifold
next-event estimation) are not supported on Adreno (TM) 650 GPUs: caustic scenes render
incorrectly on this GPU class; render the scene on another GPU or on the CPU device". With the
Adreno 730 fix below and no refusal, the Adreno 650 rendered `mnee_render` case 0 outside the
practical criterion (a film value 0.839279 where the glibc reference has 0.840097) and the state
gate failed (case 2: 5 surface shadow paths where the pinned kernel queues 7); before that fix it
stopped with the invalid-data error like the Adreno 730. The cause on the Adreno 650 is not
diagnosed.

**Adreno 730 fix.** On the Samsung SM-X700 (driver EV031.36.08.33) the first `INTERSECT_MNEE`
dispatches stopped with the invalid-data error (mask 1). Found with WGSL overrides on the unchanged
binaries (`CODEBRIX_CYCLES_WGSL_LOAD`, every `fail()` call tagged): the check that fired was the
corner-normal index range check (`triangle_frame_index`), reached from `terminator_normals` in
`mnee_setup`, because the caster triangle's third vertex index read `0x00108039` instead of 76.
The same word read 76 at the start of every entry before `mnee_setup`, and changed while
`mnee_setup` ran although no MNEE entry stores to the scene tables: with `mnee_setup` writing no
vertex data at all the word still changed; with its `native_triangle_vertices` call removed it
did not. The same WGSL (with either host's arithmetic constants) hits no `fail()` on Intel Mesa.
The fix keeps every computation and comparison: the three MNEE call sites of the shared vertex fetch
(`mn_setup_vertex`, `mn_setup_from_sample`, and the shadow-terminator origin of `mn_contribution`)
use `mn_static_vertices`, the same checks and corner reads for a static triangle in straight-line
code (no `vec3` index or corner array written in a loop), and the terminator origin goes through an
MNEE copy of `terminator_shadow_origin` / `terminator_smooth_offset` that differs only in that
fetch. Removing `mn_setup_vertex`'s own corner and normal arrays (constant indices, or plain
variables) or unrolling its normal fetch did not help; moving the solver's arrays to module scope
(the Adreno 640 pattern) did not either. The fault is in shared code only as it compiles inside
these entries (INFERENCE: an Adreno 730 compiler defect; the same fetch works in the other entries
on this device). Every edit is in the MNEE region, so scenes without caustics compile
byte-identical WGSL.

Gates (`native/tools/mnee_gate.py`; fixtures `cycles/mnee_fixture.h`: sharp glass sphere and
point lamp, rough refraction slab and spherical lamp, glass sphere and glossy caster under a
sun, casters without a receiver, one caustic and one ordinary lamp):

- `codebrix_cycles_mnee_gate`, lockstep state: every path the pinned CPU wavefront queues at
  `INTERSECT_MNEE` (all bounces) through the WGSL kernel and the pinned
  `integrator_intersect_mnee` on the same input state; main-path fields, sort keys, queue
  counters and the MNEE slot payload compared (integers exact); the walk's outcome, iteration
  count, half-vector offsets and final vertices against the pinned helpers replayed step by
  step; then the surface stage (MNEE consumption and cleanup), the MNEE shadow paths and the
  closest stage of the same paths.
- `codebrix_cycles_mnee_render_gate` (lighting-gate variant), independent CPU images of the five
  fixtures, a captured repeat and an uncaptured repeat that queues four samples per work tile.
- the queue control gate's two MNEE compaction cases (the pinned moves with `__MNEE__`).
- command replays of the five captures.

Oracle scene: `native/oracle/scenes/mnee/glass-sphere-caustic.json` (fixture case 0 through the
public host API).

Linux x64 (Intel Mesa 25.0.7):

| Check | Result |
| --- | --- |
| `codebrix_cycles_mnee_gate` (cases 0, 1, 2, 4; 8 samples, every bounce) | PASS: 23,757 INTERSECT_MNEE paths, 52 sampled, 2,358,243 integer and 5,477,650 float checks; walk outcome and iteration count equal the pinned walk for every solved path; 458 strict values, maximum 0.125 (a singular-lobe `mis_ray_pdf` near 1e6 in the surface lockstep) |
| `codebrix_cycles_mnee_render_gate` (5 cases, 32 x 24, 8 samples) | PASS, 0 practical; strict 159 / 2,834 / 0 / 161 / 236, maximum 6.1e-5 (case 1: spherical lamp, whose ordinary NEE uses the shared lamp sampler); MNEE-sampled paths 10 / 19 / 17 / 0 / 6 |
| Command replays of the five captures | PASS (strict 45 / 940 / 0 / 43 / 48) |
| Queue control gate | PASS, compaction cases 4 (2 with MNEE ownership against the pinned moves) |
| Oracle scene `mnee/glass-sphere-caustic` | wgsl-linux-intel against cpu-x64-linux: perfect, 0 practical, 3 strict, maximum 5.1e-7 |
| Intel compile envelope (Mesa 25.0.7) | PASS, largest `mnee_solve` 48,339 instructions; `mnee_contribute` 42,207, `mnee_seed` 34,658 |
| Scenes without caustics | glass, microfacet and Principled render films, the superset films (`f7c827af`/`da52a2d8`, `2e11701d`/`cc861de5`) and the bench, HDRI bench and control CPU films (`f4b08b95`, `fc72f30a`, `a1d20c08`) unchanged; lighting/lighting MIS case 5 and transparent lighting 2/4/5 vary run to run as before |

Android:

| Device | Gate | Wall | Result |
| --- | --- | --- | --- |
| HP (Mesa 24.0.8) | `mnee_render`, glibc device-scene reference | 224 s | PASS, 0 practical; strict 43 / 940 / 0 / 41 / 48, maximum 1.23e-5; local (bionic) CPU identical to glibc |
| HP | `mnee` (state, device's own CPU) | 268 s | PASS: 23,757 paths, 54 sampled, walk outcome and iteration count exact, 461 strict, maximum 0.125 (`mis_ray_pdf` near 1e6) |
| HP | queue control gate, closest gate | 2 s, 27 s | PASS (MNEE compaction cases included) |
| HP | five on-device replays of the device's own captures | 101-177 s each | PASS (strict 43 / 940 / 0 / 41 / 48) |
| Samsung SM-X700 (Adreno 730) | `mnee_render`, glibc device-scene reference | 120 s | PASS, 0 practical; strict 776, maximum 3.05e-5 (before the Adreno 730 fix: invalid data at the first `INTERSECT_MNEE` dispatch) |
| Samsung SM-X700 | `mnee` (state, device's own CPU) | 192 s | PASS: 23,757 paths, 53 sampled, walk outcome and iteration count exact, 409 strict, maximum 0.125 |
| Samsung SM-X700 | five on-device replays of the device's own captures | 20-56 s each | PASS (strict 24 / 696 / 0 / 22 / 35) |
| Samsung SM-X700 | queue control gate, closest gate | 3 s, 16 s | PASS |
| S20 (Adreno 650, not a reference device) | `principled_svm`, `transparent_shadow` | 415 s, 16 s | PASS (shared-WGSL regression) |
| S20 | `mnee_render`, `mnee` | about 1 s each | refused before any pipeline compiles (caustics on the Adreno 650), no image |
