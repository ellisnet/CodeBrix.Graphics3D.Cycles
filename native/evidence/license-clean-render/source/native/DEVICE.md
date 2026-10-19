# Registered Cycles device and wavefront queue

The new `native/cycles` implementation registers `DEVICE_WEBGPU` with the
original Cycles device factory and implements `DeviceQueue`. The Linux gate
passes against the unchanged CodeBrix.WebGpu.Native package. This connects
scene storage, scheduler control, limited camera initialization, closest
intersection, selected SVM surface bounces and background/film output to WebGPU.
A complete bounded scene-to-image loop now runs through the original native GPU
scheduler. Full `Session` lifecycle and general feature support remain pending. The earlier
[snapshot renderer](README.md) remains
the separate, working image reference on Linux and Android.

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

The isolated surface integration gate additionally passes 19 cases / 18,360
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

Unsupported direct lights/MIS, image textures, most closures, volumes, motion,
curves/points, guiding, MNEE and general film/session modes still require work.
The broad roadmap is `/home/jeremy/ClaudeHome/PLAN_Cycles_Targetting_WGSL_WebGPU.md`;
all nine requested RIDs and full functionality remain its target.

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
and array members. MNEE's cross-path ownership repair is explicitly rejected
until implemented.

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
Sun lights, background MIS, volumes, shadow catchers, denoising, path guiding,
light/shadow linking and the background motion pass are explicitly unsupported
in this entry. Surface and shadow integration remain next. The earlier standalone snapshot renderer's material
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

## Next integration

The selected native surface-bounce entry and complete scheduler loop now pass.
Next are image textures/attributes and broader SVM/closure support, then direct
light/shadow entries and MIS operating on Cycles' native state and scene tables.
The separate full-functionality plan also brings forward native Android hosts,
other platform builds and early feasibility checks for advanced functionality.
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
evidence remains subject to the release review in `../RELEASE-LICENSE-PLAN.md`.
